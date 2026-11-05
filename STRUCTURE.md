# Структура проекта

Репозиторий — это Luckfox Pico SDK (Rockchip RV1103/RV1106) с доработками под проект
**OKO**: сборка в Docker-окружении, автономная загрузка с SPI NAND и поддержка
сенсора SC230AI. Драйвер шагового двигателя 28BYJ-48 и прошивка сопроцессора
на RT-Thread входят в вендорский SDK.

Активная конфигурация платы задаётся симлинком `.BoardConfig.mk`:

```
.BoardConfig.mk -> project/cfg/BoardConfig_IPC/BoardConfig-SPI_NAND-Buildroot-RV1106_Luckfox_Pico_Pro_Max-SC230AI.mk
```

То есть SPI NAND, Buildroot, плата Luckfox Pico Pro Max, камера SC230AI.

## Корневые папки

| Папка | Назначение |
|---|---|
| `config/` | Извлечённые дефконфиги для быстрой настройки: `buildroot_defconfig`, `kernel_defconfig`, `dts_config`. |
| `bench/` | Утилиты тестового стенда: UART/SSH-команды, проверка OSD по RTSP, матрица HTTP API (`api_check.py`), проверка WebUI в headless Chromium (`ui_check.py`), выкатка бинарника в `/oem` (`deploy_divinus.py`), venv. В сборке прошивки не участвует. |
| `media/` | Медиастек Rockchip: кодеки, 2D-ускорение, ISP, NPU, медиафреймворк и примеры. |
| `project/` | Скрипты сборки, конфигурации плат и прикладные приложения. Основная точка входа сборки. |
| `sysdrv/` | Системный уровень: исходники U-Boot, ядра, Buildroot, прошивка MCU и модули ядра. |
| `tools/` | Хостовые инструменты: кросс-тулчейн, утилиты упаковки и прошивки образов. |
| `output/` | Результаты сборки: готовые образы в `image/`, промежуточные артефакты в `out/`, лог `build_all.log`. В `output/bench/` — venv, кадры OSD-проверок и скриншоты WebUI `ui/` (gitignore). |
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
- `app/` — прикладные приложения: `rkipc` (штатное IPC-приложение), `divinus`
  (вендоренный OpenIPC-стример, по умолчанию вместо rkipc), `uvc_app_tiny`,
  `ipcweb` (веб-интерфейс), `rk_smart_door`, `wifi_app`, `fastboot_client`,
  `component` (общие компоненты).
- `app/divinus/` — пакет `divinus`: `source/` (вендорские `src/`, `res/`, `doc/`,
  лицензии, пин коммита и локальные патчи — `compat.c` (ABI `mmap`), `region.c`
  (`@file`-провайдер текста OSD), `hal/plus/rk_hal.c`/`rk_rgn.h` (RGN OSD на
  RV1106) и OKO-модуль `ntp.c` (NTP-клиент WebUI); переработанный WebUI —
  `res/index.html` + `doc/WEBUI_REORG.md`; описание патчей — в `README.md`),
  `files/divinus.yaml` (фабричный
  конфиг с OSD-регионами), `Makefile` (кросс-сборка и стейджинг в
  `/oem/usr/bin` и `/oem/usr/share/divinus`), `tests/bmp_align_test.c`
  (хостовый тест выравнивания растра OSD, в сборку не входит). Сборка входит в
  общий цикл `project/app/Makefile`.
- `app/oko-ec/` — пакет `oko-ec`: `source/` (вендоренный хостовый инструмент
  `spectrespc/stm32_acpi-ec` на пине `41728ddaf6936d49a5fb65676ca5203dd631b4f6`
  — `main.c`, `crc_host.*`, `i2c_transport.*`; локальные `ec_proto.*`,
  `sensors_math.*`, `sensors.*`, `daemon.*`), `Makefile` (кросс-сборка и
  стейджинг `/oem/usr/bin/oko-ec`), `tests/sensors_math_test.c` (хостовый тест,
  в сборку не входит) и `README.md` с пином upstream и локальными патчами.
  Работает с SHT21 `0x40`, INA3221/INA226 `0x41` и STM32 ACPI-EC `0x42` на
  `/dev/i2c-3`:
  команды чтения, OTA STM32 и демон `oko-ec daemon`, который пишет
  `/tmp/oko-ec.json`, `/tmp/oko-ec.osd` и `/tmp/oko-ec.pid`.
- `cfg/BoardConfig_IPC/` — конфигурации плат (`BoardConfig-*.mk`), папка `overlay/`
  с оверлеями rootfs и pre/post-скрипты (`*-pre.sh`, `*-post.sh`). В оверлее OKO
  `overlay-luckfox-buildroot-init/`: `etc/init.d/S20oko-osd` (divinus по
  умолчанию, бутстрап OSD-конфига, запуск демона), `etc/init.d/S21oko-ec`
  (запуск `oko-ec daemon`, opt-out `/userdata/oko-ec.disable`), `usr/bin/oko-osd`
  (телеметрия и FIFO-сообщения для OSD; реальные `T`/`BAT` из
  `/tmp/oko-ec.osd`), `usr/bin/oko-sdcard` + `lib/udev/rules.d/60-oko-sdcard.rules`
  (монтирование microSD в `/mnt/sdcard`, состояние `/run/oko-sd.state`,
  `mount`/`unmount`/`format` для `/api/storage`, lock `/run/oko-sd.lock`;
  eudev в этом образе читает только `/lib/udev/rules.d`),
  `usr/bin/oko-net` + `etc/fw_env.config` (MAC, DHCP/static в env-разделе
  U-Boot), `etc/init.d/S41dhcpcd` (заглушка, DHCP только через `oko-net`) и
  `etc/dhcpcd.conf` (client ID по MAC, без DUID); `etc/init.d/S99oko-test`
  (маркер тестовой сборки).
- `cfg-all-items-introduction.txt` — описание всех параметров BoardConfig.
- `scripts/`, `make_meta/`, `sfc_scripts/` — вспомогательные скрипты сборки и
  упаковки метаданных/разделов.

### `sysdrv/`

- `source/uboot` — исходники загрузчика U-Boot.
- `source/kernel` — исходники ядра Linux; активный DTS
  `arch/arm/boot/dts/rv1106g-luckfox-pico-pro-max-sc230ai.dts` (Pro Max +
  выключенные `&sc3336`/`&mis5001`), фрагмент `rv1106-sc230ai.config` собирает
  только `sc230ai`.
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
- `start.sh` — сборка образа, запуск контейнера и вход в shell.
- `AGENTS.md` — правила агента `code`: карта документации, делегирование, инструменты.
- `README_RU.md` — русскоязычная точка входа OKO-форка: быстрый старт, сборка, ссылки.
- `FEATURES.md` — сводка поддерживаемого функционала: платформа, видео и стриминг,
  сеть, телеметрия `oko-ec`, microSD и известные ограничения.
- `STRUCTURE.md`, `INIT_ORDER.md`, `TESTBENCH.md` — документация проекта: структура
  репозитория, порядок инициализации, параметры тестового стенда.
- `VERIFIED_FUNCTIONS.md` — реестр проверенных на стенде функций: статус,
  доказательство, ограничения и команды воспроизведения.
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