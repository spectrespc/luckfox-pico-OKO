# SDCC toolchain for the OKO STM8S103F3P6 motor controller.
#
# The main SDK builder (Dockerfile at the repository root) has no SDCC: the
# STM8 firmware is built by this separate image so `./build.sh app` never
# depends on it.  See project/app/stm8-motor/README.md for the commands.
#
# Pinned:
#   - Debian bookworm ships SDCC 4.2.0 (the upstream project asks for >= 4.0)
#   - bschwand/STM8-SPL-SDCC at 14b1afd61e8155bceecf78b358b225b34920f0f6
#     provides Libraries/STM8S_StdPeriph_Driver/STM8S103/spl.lib, the layout
#     the upstream Makefile expects under $HOME/STM8-SPL-SDCC.
FROM debian:bookworm-slim

ARG SPL_REPO=https://github.com/bschwand/STM8-SPL-SDCC.git
ARG SPL_COMMIT=14b1afd61e8155bceecf78b358b225b34920f0f6
ARG STM8FLASH_REPO=https://github.com/vdudouyt/stm8flash.git
ARG STM8FLASH_COMMIT=b2b5818689d1458ba0353ac8edd356fb3c81a52c

RUN apt-get update \
 && apt-get install -y --no-install-recommends \
        bash build-essential ca-certificates git libusb-1.0-0-dev make pkg-config \
        python3 sdcc \
 && rm -rf /var/lib/apt/lists/*

RUN git clone "$SPL_REPO" /root/STM8-SPL-SDCC \
 && git -C /root/STM8-SPL-SDCC checkout --detach "$SPL_COMMIT" \
 && make -C /root/STM8-SPL-SDCC/Libraries/STM8S_StdPeriph_Driver \
        DEVICE=STM8S103 \
 && rm -rf /root/STM8-SPL-SDCC/.git

RUN git clone "$STM8FLASH_REPO" /root/stm8flash \
 && git -C /root/stm8flash checkout --detach "$STM8FLASH_COMMIT" \
 && make -C /root/stm8flash \
 && cp /root/stm8flash/stm8flash /usr/local/bin/ \
 && rm -rf /root/stm8flash

ENV HOME=/root
WORKDIR /work

# Fail the image build early if the compiler or the SPL archive is missing.
RUN sdcc -v | head -1 \
 && test -f /root/STM8-SPL-SDCC/Libraries/STM8S_StdPeriph_Driver/STM8S103/spl.lib \
 && command -v stm8flash

CMD ["bash"]