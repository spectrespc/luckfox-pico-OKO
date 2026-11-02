#!/bin/bash
set -e

# Тулчейн уже настроен через ENV в Dockerfile, но на всякий случай обновим PATH для текущей сессии
export PATH="/workspace/tools/linux/toolchain/arm-rockchip830-linux-uclibcgnueabihf/bin:$PATH"
export CROSS_COMPILE="arm-rockchip830-linux-uclibcgnueabihf-"
export ARCH="arm"

# Выполняем основную команду (например, /bin/bash)
# exec заменяет текущий процесс, предотвращая рекурсию
exec "$@"
