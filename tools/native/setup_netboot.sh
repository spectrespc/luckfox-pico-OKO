#!/usr/bin/env bash
# Configure the host services for the OKO netboot loop: TFTP for boot.img and
# NFS for the root filesystem.
#
#   tools/native/setup_netboot.sh
#
# Run as a user with sudo (the password is asked by sudo, never stored here).
# The TFTP root is the absolute <repo>/output/image; the server chroots there,
# which is why the packaged ProtectHome=yes unit needs a read-only override.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
TFTP_DIR="$ROOT/output/image"
NFS_DIR="/srv/nfs/rootfs"
NFS_NET="192.168.52.0/24"
NFS_OPTS="rw,sync,no_subtree_check,no_root_squash,insecure"

if [ "$(id -u)" -eq 0 ]; then
    SUDO=()
elif [ -n "${SUDO_ASKPASS:-}" ]; then
    SUDO=(sudo -A)
else
    SUDO=(sudo)
fi

"${SUDO[@]}" apt-get update
"${SUDO[@]}" apt-get install -y tftpd-hpa nfs-kernel-server nfs-common acl

mkdir -p "$TFTP_DIR"

# in.tftpd drops to the tftp user and cannot read the repository when a parent
# directory (for example a 0700 home) denies traversal. Grant execute-only ACLs
# on the ancestors and read access on the TFTP root itself, instead of
# loosening their modes.
"${SUDO[@]}" setfacl -m u:tftp:r-x "$TFTP_DIR"
ancestor="$(dirname "$TFTP_DIR")"
while [ "$ancestor" != "/" ]; do
    "${SUDO[@]}" setfacl -m u:tftp:--x "$ancestor"
    ancestor="$(dirname "$ancestor")"
done

"${SUDO[@]}" tee /etc/default/tftpd-hpa >/dev/null <<EOF
TFTP_USERNAME="tftp"
TFTP_DIRECTORY="$TFTP_DIR"
TFTP_ADDRESS="0.0.0.0:69"
TFTP_OPTIONS="--listen --secure"
EOF

# The packaged unit sets ProtectHome=yes, which hides /home inside the
# sandbox and makes the repository-based TFTP root unreachable. Allow
# read-only access to /home for this unit only.
"${SUDO[@]}" mkdir -p /etc/systemd/system/tftpd-hpa.service.d
"${SUDO[@]}" tee /etc/systemd/system/tftpd-hpa.service.d/oko.conf >/dev/null <<EOF
[Service]
ProtectHome=read-only
EOF
"${SUDO[@]}" systemctl daemon-reload

if ! grep -qs "^$NFS_DIR[[:space:]]" /etc/exports; then
    echo "$NFS_DIR $NFS_NET($NFS_OPTS)" | "${SUDO[@]}" tee -a /etc/exports >/dev/null
fi
"${SUDO[@]}" exportfs -ra

"${SUDO[@]}" systemctl enable tftpd-hpa
"${SUDO[@]}" systemctl restart tftpd-hpa
"${SUDO[@]}" systemctl restart nfs-kernel-server

if command -v ufw >/dev/null 2>&1 && "${SUDO[@]}" ufw status 2>/dev/null | grep -q 'Status: active'; then
    "${SUDO[@]}" ufw allow 69/udp
    "${SUDO[@]}" ufw allow 2049/tcp
    "${SUDO[@]}" ufw allow 2049/udp
fi

echo "== TFTP root: $TFTP_DIR"
echo "== NFS export: $NFS_DIR -> $NFS_NET"
/usr/sbin/showmount -e localhost || true
ss -lun 'sport = :69' || true