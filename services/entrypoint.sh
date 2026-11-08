#!/usr/bin/env bash
# Start the OKO stand services: the kernel NFS server (NFSv3) and the TFTP
# daemon, both supervised by supervisord.
set -euo pipefail

mkdir -p /srv/nfs/rootfs

# The nfsd pseudo-filesystem is normally absent in a fresh container. The
# privileged container shares the host kernel module, so mounting it here is
# enough to make rpc.nfsd work.
if ! mountpoint -q /proc/fs/nfsd; then
    mount -t nfsd nfsd /proc/fs/nfsd 2>/dev/null || true
fi

# NFSv3 clients (the DUT kernel) discover the NFS and mountd ports through the
# portmapper, so rpcbind must listen on 111. With network_mode: host a host
# portmapper may already own the port; in that case reuse it.
rpcbind -w 2>/dev/null || true
for _ in 1 2 3 4 5 6 7 8 9 10; do
    if ss -H -ltn 'sport = :111' | grep -q .; then
        break
    fi
    sleep 0.2
done

# The kernel nfsd threads survive a container restart, and rpc.nfsd with an
# unchanged thread count does not register again with the fresh portmapper.
# Reset the thread count first so every start re-registers the NFS service.
rpc.nfsd 0 2>/dev/null || true
rpc.nfsd 8
exportfs -ra

exec supervisord -n -c /etc/supervisor/supervisord.conf