# OKO stand services: TFTP (boot.img), NFS root and optional VPN servers.
# The container relies on the host kernel for nfsd and on host networking, so
# it is started with `privileged: true` and `network_mode: host`.
FROM debian:trixie-slim

ENV DEBIAN_FRONTEND=noninteractive

# The policy-rc.d stub keeps package postinst scripts from trying to start
# systemd units during the image build.
RUN printf '#!/bin/sh\nexit 101\n' > /usr/sbin/policy-rc.d \
    && chmod +x /usr/sbin/policy-rc.d \
    && apt-get update \
    && apt-get install -y --no-install-recommends \
        busybox \
        iproute2 \
        nfs-kernel-server \
        openvpn \
        rsync \
        supervisor \
        tftpd-hpa \
        wireguard-tools \
    && rm -rf /var/lib/apt/lists/* /usr/sbin/policy-rc.d \
    && rm -rf /usr/share/doc/* /usr/share/man/* /var/cache/apt/* \
    && mkdir -p /srv/tftp /srv/nfs/rootfs

COPY entrypoint.sh /usr/local/bin/oko-services-entrypoint
COPY conf/ /etc/oko-services/
COPY conf/exports /etc/exports

# Keep the supervisor master config in sync with the mounted conf/ directory,
# so edits under services/conf take effect on restart without a rebuild.
RUN chmod +x /usr/local/bin/oko-services-entrypoint \
    && ln -sf /etc/oko-services/supervisord.conf /etc/supervisor/supervisord.conf

# 69/tftp, 111/portmapper, 2049/nfs, 20048/mountd
EXPOSE 69/udp 111/tcp 111/udp 2049/tcp 2049/udp 20048/tcp 20048/udp

ENTRYPOINT ["/usr/local/bin/oko-services-entrypoint"]