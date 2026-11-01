#!/bin/bash
set -e

# 1. Создаём папку для TFTP, если её нет
mkdir -p "$TFTP_ROOT" 2>/dev/null || true
chmod 755 "$TFTP_ROOT" 2>/dev/null || true

# 2. Запускаем TFTP сервер в фоне (если ещё не запущен)
if ! pgrep -x in.tftpd > /dev/null; then
    echo "🚀 Запуск TFTP сервера на порту 69/udp..."
    echo "   TFTP Root: $TFTP_ROOT"
    sudo /usr/sbin/in.tftpd --listen --secure --verbose --user root "$TFTP_ROOT" &
    sleep 1
    if pgrep -x in.tftpd > /dev/null; then
        echo "✅ TFTP сервер запущен (PID: $(pgrep -x in.tftpd))"
    fi
else
    echo "✅ TFTP сервер уже запущен (PID: $(pgrep -x in.tftpd))"
fi

# 3. Тулчейн уже настроен через ENV в Dockerfile, но на всякий случай обновим PATH для текущей сессии
export PATH="/workspace/tools/linux/toolchain/arm-rockchip830-linux-uclibcgnueabihf/bin:$PATH"
export CROSS_COMPILE="arm-rockchip830-linux-uclibcgnueabihf-"
export ARCH="arm"

# 4. Выполняем основную команду (например, /bin/bash)
# exec заменяет текущий процесс, предотвращая рекурсию
exec "$@"
