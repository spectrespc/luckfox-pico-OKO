FROM ubuntu:22.04

ENV DEBIAN_FRONTEND=noninteractive
ENV LANG=en_US.UTF-8
ENV LC_ALL=en_US.UTF-8
ENV TFTP_ROOT=/workspace/output/image

ARG UID=1000
ARG GID=1000
ARG USERNAME=builder

RUN apt-get update && apt-get install -y \
    sudo locales && locale-gen en_US.UTF-8 && \
    apt-get install -y \
    git ssh make gcc gcc-multilib g++-multilib module-assistant expect g++ \
    gawk texinfo libssl-dev bison flex fakeroot cmake unzip gperf autoconf \
    device-tree-compiler libncurses5-dev pkg-config bc python-is-python3 \
    passwd openssl openssh-server openssh-client vim file cpio rsync \
    git-lfs repo python3 python3-pip python3-dev ninja-build \
    squashfs-tools mtd-utils e2fsprogs dosfstools mtools xz-utils bzip2 \
    p7zip-full zip wget curl \
    bash-completion \
    tftpd-hpa \
    atftp \
    && apt-get clean \
    && rm -rf /var/lib/apt/lists/*

RUN pip3 install --upgrade pip setuptools wheel

RUN groupadd -g $GID $USERNAME && \
    useradd -u $UID -g $GID -m -s /bin/bash $USERNAME && \
    echo "$USERNAME ALL=(ALL) NOPASSWD:ALL" >> /etc/sudoers

# Настройка bash-completion
RUN echo '' >> /etc/bash.bashrc && \
    echo '# Enable bash completion' >> /etc/bash.bashrc && \
    echo 'if ! shopt -oq posix; then' >> /etc/bash.bashrc && \
    echo '  if [ -f /usr/share/bash-completion/bash_completion ]; then' >> /etc/bash.bashrc && \
    echo '    . /usr/share/bash-completion/bash_completion' >> /etc/bash.bashrc && \
    echo '  elif [ -f /etc/bash_completion ]; then' >> /etc/bash.bashrc && \
    echo '    . /etc/bash_completion' >> /etc/bash.bashrc && \
    echo '  fi' >> /etc/bash.bashrc && \
    echo 'fi' >> /etc/bash.bashrc

COPY --chown=$USERNAME:$USERNAME docker-entrypoint.sh /usr/local/bin/
RUN chmod +x /usr/local/bin/docker-entrypoint.sh

WORKDIR /workspace
USER $USERNAME

# ==========================================
# НАСТРОЙКА ТУЛЧЕЙНА НАПРЯМУЮ (Самый надежный способ)
# ==========================================
ENV PATH="/workspace/tools/linux/toolchain/arm-rockchip830-linux-uclibcgnueabihf/bin:${PATH}"
ENV CROSS_COMPILE="arm-rockchip830-linux-uclibcgnueabihf-"
ENV ARCH="arm"

# Приветственное сообщение
RUN echo 'echo "==========================================="' >> ~/.bashrc && \
    echo 'echo "  Luckfox Pico OKO Build Environment"' >> ~/.bashrc && \
    echo 'echo "  OS: Ubuntu 22.04"' >> ~/.bashrc && \
    echo 'echo "  User: $(whoami) (UID: $UID)"' >> ~/.bashrc && \
    echo 'echo "  Workspace: /workspace"' >> ~/.bashrc && \
    echo 'echo "  TFTP Root: $TFTP_ROOT"' >> ~/.bashrc && \
    echo 'echo "==========================================="' >> ~/.bashrc && \
    echo 'echo ""' >> ~/.bashrc && \
    echo 'echo "✅ Тулчейн Rockchip и TFTP-сервер инициализированы."' >> ~/.bashrc && \
    echo 'echo ""' >> ~/.bashrc

ENTRYPOINT ["/usr/local/bin/docker-entrypoint.sh"]
CMD ["/bin/bash"]
