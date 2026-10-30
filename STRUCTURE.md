# Структура проекта

Репозиторий — это Luckfox Pico SDK (Rockchip RV1103/RV1106) с доработками под проект
**OKO**: сборка в Docker-окружении, загрузка по сети (TFTP/NFS) и поддержка сенсора
SC230AI. Драйвер шагового двигателя 28BYJ-48 и прошивка сопроцессора на RT-Thread
входят в вендорский SDK.

Активная конфигурация платы задаётся симлинком `.BoardConfig.mk`:

```
.BoardConfig.mk -> project/cfg/BoardConfig_IPC/BoardConfig-SPI_NAND-Buildroot-RV1106_Luckfox_Pico_Pro_Max-NETBOOT-SC230AI.mk
```

То есть SPI NAND, Buildroot, плата Luckfox Pico Pro Max, режим netboot, камера SC230AI.

## Корневые папки

| Папка | Назначение |
|---|---|
| `config/` | Извлечённые дефконфиги для быстрой настройки: `buildroot_defconfig`, `kernel_defconfig`, `dts_config`. |
| `media/` | Медиастек Rockchip: кодеки, 2D-ускорение, ISP, NPU, медиафреймворк и примеры. |
| `project/` | Скрипты сборки, конфигурации плат и прикладные приложения. Основная точка входа сборки. |
| `sysdrv/` | Системный уровень: исходники U-Boot, ядра, Buildroot, прошивка MCU и модули ядра. |
| `tools/` | Хостовые инструменты: кросс-тулчейн, утилиты упаковки и прошивки образов. |
| `output/` | Результаты сборки: готовые образы в `image/`, промежуточные артефакты в `out/`, лог `build_all.log`. |
| `IMAGE/` | Готовые релизные образы: папка конкретного релиза с `IMAGES/`, `PATCHES/`, `DEBUG_FILES/` и `build_info.txt`. |
| `.kilo/` | Служебная конфигурация Kilo: команды (`command/`), skill `oko-docs` (`skills/`), планы (`plans/`), настройки (`kilo.jsonc`). Проектных агентов здесь нет. |

## Ключевые вложенные папки

### `config/`

- `buildroot_defconfig` — сохранённая конфигурация Buildroot.
- `kernel_defconfig` — сохранённая конфигурация ядра Linux.
- `dts_config` — настройки Device Tree: выбор `.dts` и связанные dts-файлы под плату.

Дефконфиги здесь — это снимки для удобной правки; рабочая сборка берёт их из
`project/cfg/` и `sysdrv/source/`.

### `media/`

Медиастек Rockchip, собирается как набор библиотек и примеров:

- `mpp` — аппаратные кодеки (кодирование/декодирование видео).
- `rga` — 2D-графика и масштабирование.
- `isp`, `iva`, `ive`, `rkpostisp` — обработка изображения с камеры и ускорение.
- `rockit` — фреймворк построения медиапайплайнов.
- `avs`, `common_algorithm` — вспомогательные алгоритмы и панорамная сборка.
- `libdrm`, `libv4l`, `alsa-lib` — системные библиотеки вывода/ввода.
- `luckfox` — платформо-специфичная библиотека (`crypto`, `examples`).
- `samples`, `security`, `sysutils`, `cfg` — примеры, безопасность и служебные утилиты.

### `project/`

- `build.sh` — основной скрипт сборки SDK.
- `rkflash.sh` — прошивка собранного образа на плату.
- `app/` — прикладные приложения: `rkipc` (IPC-приложение), `uvc_app_tiny`,
  `ipcweb` (веб-интерфейс), `rk_smart_door`, `wifi_app`, `fastboot_client`,
  `component` (общие компоненты).
- `cfg/BoardConfig_IPC/` — конфигурации плат (`BoardConfig-*.mk`), папка `overlay/`
  с оверлеями rootfs и pre/post-скрипты (`*-pre.sh`, `*-post.sh`).
- `cfg-all-items-introduction.txt` — описание всех параметров BoardConfig.
- `scripts/`, `make_meta/`, `sfc_scripts/` — вспомогательные скрипты сборки и
  упаковки метаданных/разделов.

### `sysdrv/`

- `source/uboot` — исходники загрузчика U-Boot.
- `source/kernel` — исходники ядра Linux.
- `source/buildroot` — исходники Buildroot (корневая файловая система).
- `source/mcu` — прошивка сопроцессора MCU на RT-Thread (вендорская часть SDK).
- `source/objs_kernel` — промежуточные объекты сборки ядра.
- `drv_ko/kmpp`, `drv_ko/rockit`, `drv_ko/wifi` — модули ядра для медиастека и Wi-Fi.
- `drv_ko/motor` — драйвер шагового двигателя 28BYJ-48 из вендорского SDK
  (`motor_24byj48.c`/`.h`, `sample`, `src`); в текущей конфигурации выключен.
- `drv_ko/insmod_ko.sh` — загрузка модулей ядра.
- `cfg/`, `tools/`, `out/` — конфигурации, вспомогательные инструменты и артефакты сборки.

### `tools/`

- `linux/toolchain/` — кросс-компилятор `arm-rockchip830-linux-uclibcgnueabihf`.
- `linux/Linux_Pack_Firmware`, `linux/Linux_Upgrade_Tool` — упаковка образов и
  прошивка по USB.
- `linux/SocToolKit` — низкоуровневые утилиты работы с SoC.
- `windows/` — Windows-версии утилит (`DriverAssitant`, `FactoryTool`, `SocToolKit`).

## Корневые файлы

- `build.sh`, `rkflash.sh` — симлинки на `project/build.sh` и `project/rkflash.sh`.
- `Dockerfile`, `docker-compose.yml`, `docker-entrypoint.sh` — Docker-окружение сборки.
- `start.sh` — сборка образа, запуск контейнера, подготовка TFTP-корня и вход в shell.
- `netboot-catch.py` — сервисный скрипт захвата лога netboot через последовательный порт (UART).
- `AGENTS.md` — правила агента `code`: карта документации, делегирование, инструменты.
- `README_RU.md` — русскоязычная точка входа OKO-форка: быстрый старт, сборка, ссылки.
- `STRUCTURE.md`, `INIT_ORDER.md`, `TESTBENCH.md` — документация проекта: структура
  репозитория, порядок инициализации, параметры тестового стенда.
- `CHANGELOG.md` — история изменений OKO-форка (Keep a Changelog + SemVer).
- `README.md` / `README_CN.md` — документация SDK (EN/CN).
- `UPDATE_LOG.md` / `UPDATE_LOG_CN.md` — журнал изменений SDK (EN/CN).

## Служебные папки

- `.kilo/` — конфигурация Kilo: `command/` (команды `/bench`, `/docs-check`),
  `skills/oko-docs/`, `plans/`, `kilo.jsonc`; к сборке не относится. Проектные агенты
  `.kilo/agent/*.md` не создаются.
- `.git/` — служебные данные Git, в описании структуры не рассматривается.

## Примечания

- `IMAGE/` содержит релизную папку
  `IPC_SPI_NAND_BUILDROOT_RV1103_LUCKFOX_PICO_MINI_..._RELEASE_TEST` — это сборка под
  **RV1103 Pico Mini**, она не соответствует активной конфигурации (RV1106 Pro Max).
  Папка описывается как есть, без привязки к текущей плате.
- Каталоги `out/`, `build/`, `objs_kernel/` — артефакты сборки, их внутреннее
  содержимое намеренно не детализируется.