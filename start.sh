#!/bin/bash
export CURRENT_UID=$(id -u)
export CURRENT_GID=$(id -g)

echo "🔄 Сборка Docker-образа (если он изменился)..."
docker compose build

echo "🚀 Запуск контейнера..."
docker compose up -d

# Git LFS
echo "📥 Проверка Git LFS..."
docker compose exec -T luckfox-builder bash -c '
if [ -d .git ] && [ -f .gitattributes ]; then
    git lfs install 2>/dev/null || true
    LFS_STATUS=$(git lfs status --porcelain 2>/dev/null)
    if [ -n "$LFS_STATUS" ]; then
        echo "📥 Подтягиваю LFS-файлы..."
        git lfs pull
    else
        echo "✅ LFS-файлы актуальны."
    fi
fi
'

# Создаём папку output/image, если её нет
mkdir -p output/image 2>/dev/null || true

echo ""
echo "==========================================="
echo "  🎯 Контейнер готов к работе!"
echo "==========================================="
echo "  📁 TFTP Root: ./output/image"
echo "  🌐 TFTP адрес: $(hostname -I | awk '{print $1}')"
echo "  📡 TFTP порт: 69/udp"
echo ""
echo "  Пример использования с устройства:"
echo "    tftp -g -r update.img <IP-хоста>"
echo ""
echo "  Для прошивки через U-Boot:"
echo "    => setenv serverip <IP-вашего-хоста>"
echo "    => tftp 0x40000000 update.img"
echo "==========================================="
echo ""

echo "💻 Вход в оболочку..."
docker compose exec luckfox-builder /bin/bash
