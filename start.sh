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
echo "  📁 Образы: ./output/image"
echo ""
echo "  Сборка прошивки:"
echo "    ./build.sh firmware"
echo ""
echo "  Прошивка SPI NAND:"
echo "    ./rkflash.sh update"
echo "==========================================="
echo ""

echo "💻 Вход в оболочку..."
docker compose exec luckfox-builder /bin/bash
