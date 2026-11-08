#!/usr/bin/env bash
# Start the OKO stand services container: TFTP for boot.img and NFS for the
# root filesystem.
#
#   tools/native/setup_netboot.sh
#
# The services run in the container defined by services/docker-compose.yml with
# host networking and privileged mode, so no sudo is needed here. Host ufw
# rules (69/udp, 111, 2049, 20048) are intentionally left untouched.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
COMPOSE="$ROOT/services/docker-compose.yml"
NFS_DIR="$ROOT/output/srv/nfs/rootfs"
VPN_DIR="$ROOT/output/srv/vpn"

if ! command -v docker >/dev/null 2>&1; then
    echo "error: docker not found; install docker.io and join the docker group" >&2
    exit 1
fi
if ! docker compose version >/dev/null 2>&1; then
    echo "error: the docker compose plugin is not available" >&2
    exit 1
fi

mkdir -p "$NFS_DIR" "$VPN_DIR"

docker compose -f "$COMPOSE" up -d --build

# Diagnostics: the export must be visible and the DUT-facing ports listened on.
if command -v showmount >/dev/null 2>&1; then
    showmount -e localhost || true
fi
ss -lun 'sport = :69' || true
ss -tln 'sport = :2049' || true

echo "== TFTP root: $ROOT/output/image -> /srv/tftp"
echo "== NFS export: $ROOT/output/srv/nfs/rootfs -> /srv/nfs/rootfs (192.168.52.0/24)"