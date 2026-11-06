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
| `bench/` | Утилиты тестового стенда: UART/SSH-команды, проверка OSD по RTSP (`osd_check.py`), матрица HTTP API (`api_check.py`), WebUI в headless Chromium (`ui_check.py`), AF-метрика и CSV (`focus_check.py`), версии/прошивка контроллеров (`firmware_check.py`), выкатка бинарника в `/oem` (`deploy_divinus.py`), venv. В сборке прошивки не участвует. |
| `media/` | Медиастек Rockchip: кодеки, 2D-ускорение, ISP, NPU, медиафреймворк и примеры. |
| `project/` | Скрипты сборки, конфигурации плат и прикладные приложения. Основная точка входа сборки. |
| `sysdrv/` | Системный уровень: исходники U-Boot, ядра, Buildroot, прошивка MCU и модули ядра. |
| `tools/` | Хостовые инструменты: кросс-тулчейн, утилиты упаковки и прошивки образов. |
| `output/` | Результаты сборки: готовые образы в `image/`, промежуточные артефакты в `out/`, лог `build_all.log`. В `output/bench/` — venv, кадры OSD-проверок, CSV/снимки фокуса в `focus/`, скриншоты WebUI `ui/` (gitignore). |
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
  (`@file`-провайдер текста OSD), `hal/plus/rk_hal.c`/`rk_rgn.h`/
  `rk_rgn_pack.c`/`rk_rgn_pack.h` (RGN OSD на RV1106: canvas-обновление с
  fallback на `SetBitmap`, `SetDisplayAttr`, упаковка `ARGB1555 → BGRA5551`),
  OKO-модуль `ntp.c` (NTP-клиент WebUI) и NPU-аналитика:
  `analytics/yolo.c` (тред детекции, кольцо кадров, событие),
  `analytics/yolo_postprocess.c` (декод YOLOv5 и NMS),
  `analytics/models.c` (модели в `/userdata/models`), `hal/plus/rk_rknn.h`
  (`dlopen` `librknnmrt.so`); NV12→RGB888 считается на CPU в `yolo.c`;
  переработанный WebUI —
  `res/index.html` + `doc/WEBUI_REORG.md`; описание патчей — в `README.md`),
  `files/divinus.yaml` (фабричный
  конфиг с OSD-регионами), `Makefile` (кросс-сборка и стейджинг в
  `/oem/usr/bin` и `/oem/usr/share/divinus`), `tests/bmp_align_test.c`
  (хостовый тест выравнивания растра OSD, в сборку не входит),
  `tests/rk_rgn_pack_test.c` (хостовый тест упаковки `ARGB1555 → BGRA5551`,
  в сборку не входит) и
  `tests/yolo_postprocess_test.c` (хостовый тест декода/NMS, в сборку не
  входит), `focus.c`/`focus.h` (тред AF-статистики), `focus_geom.*`,
  `focus_proto.*`, `focus_motor.*`, `flash.*` (фоновый воркер прошивки),
  `firmware_status.*` (кэш версий STM32/STM8),
  `vpn.c`/`vpn.h` (API VPN: профили, живое состояние, импорт/экспорт,
  вызовы `oko-vpn`) и `vpn_conf.c`/`vpn_conf.h` (парсер wg-quick `.conf`,
  общий с `tests/vpn_conf_test.c`),
  `tests/focus_geom_test.c` и `tests/focus_proto_test.c`. Сборка входит в
  общий цикл `project/app/Makefile`.
- `app/stm8-motor/` — полные исходники прошивки STM8S103F3P6 (фокус/зум,
  I2C-загрузчик, `oko-motor`, тесты). Git subtree
  `spectrespc/stm8s103_i2c_motor` на пине
  `ef106e6513afaf15db71c2cbc57628a9dfb932c3`: `src/core`, `src/hal`,
  `src/i2c`, `src/motor`, `src/boot` (boot `0x8000–0x89FF`, заголовок
  `0x8A00`, приложение `0x8A10–0x9FFF`), `src/common` (CRC/заголовок/кадры),
  `tools/mkapp.py` (+ `ihx2bin.py`, `merge_ihx.py`), `util/motorctl.c`
  (`oko-motor`), `tests/` и `Makefile.stm8` (SDCC, запускается в
  `tools/stm8-sdcc`). `Makefile` dual-mode: standalone собирает прошивку, в
  SDK — только `oko-motor`. Push/pull — `tools/stm8_push.sh` /
  `tools/stm8_pull.sh` (`app/STM8_FIRMWARE.md`).
- `app/oko-flash/` — диспетчер прошивки контроллеров (`oko-flash`): STM32 →
  `oko-ec flash`, STM8 → `oko-motor flash`; вызывается воркером `divinus`.
- `app/vpn-tools/` — вендорские `wg` (`wireguard-tools` 1.0.20210914) и `awg`
  (`amneziawg-tools` 3.1.20260812, коммит `ee0f0a9a`) из `vendor/*/src`;
  собираются app-тулчейном и кладутся в `/oem/usr/bin`. `wg-quick`/`awg-quick`
  и хуки не устанавливаются: конфигурацию применяет `/usr/bin/oko-vpn`.
  Версии и причины вендоринга — `README.md` пакета.
- `app/oko-ec/`
- `app/stm32_acpi-ec/` — полные исходники прошивки STM32 ACPI-EC
  (`spectrespc/stm32_acpi-ec`, git subtree, пин
  `41728ddaf6936d49a5fb65676ca5203dd631b4f6`): `boot/`, `app/`, `hal/`,
  `common/`, CMake-сборка, тесты и `tools/bootloader_tool`. Отдельная
  CMake-сборка, в `./build.sh` и образ OKO не входит; отправка изменений —
  `tools/stm32_push.sh`, обновление — `tools/stm32_pull.sh`
  (`app/STM32_FIRMWARE.md`).
- `app/STM32_FIRMWARE.md` — сборка, пин и порядок push/pull для STM32.
- `app/STM8_FIRMWARE.md` — сборка, пин и порядок push/pull для STM8.
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
  U-Boot), `usr/bin/oko-vpn` (применение профилей WireGuard/AmneziaWG,
  маршруты, guard полного туннеля, autostart/watchdog; профили и состояние —
  `/userdata/vpn`), `etc/init.d/S45oko-vpn` (autostart после появления
  default route и watchdog), `etc/init.d/S41dhcpcd` (заглушка, DHCP только
  через `oko-net`) и `etc/dhcpcd.conf` (client ID по MAC, без DUID);
  `etc/init.d/S99oko-test` (маркер тестовой сборки).
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
- `drv_ko/amneziawg` — out-of-tree модуль `amneziawg.ko` (вендорский слепок
  `amneziawg-linux-kernel-module` @ `v3.1.20260906`, без правок); собирается
  при `RK_ENABLE_AWG=y`, грузится по требованию из `oko-vpn`, встроенный
  `CONFIG_WIREGUARD=y` остаётся. Версия и проверка — `README.md` каталога.
- `drv_ko/insmod_ko.sh` — загрузка модулей ядра.
- `cfg/`, `tools/`, `out/` — конфигурации, вспомогательные инструменты и артефакты сборки.

### `tools/`

- `linux/toolchain/` — кросс-компилятор `arm-rockchip830-linux-uclibcgnueabihf`.
- `linux/Linux_Pack_Firmware`, `linux/Linux_Upgrade_Tool` — упаковка образов и
  прошивка по USB.
- `linux/SocToolKit` — низкоуровневые утилиты работы с SoC.
- `rknn-toolkit2/` — Docker-сборка YOLOv5n → ONNX → INT8 `.rknn` для `rv1106`:
  `Dockerfile.toolkit` (rknn-toolkit2 1.6.0), `Dockerfile.export` (Torch/
  YOLOv5), `export_yolov5.sh`, `convert_rknn.sh`, `convert.py`; артефакты —
  в `output/rknn/`.
- `stm32_push.sh`, `stm32_pull.sh` — commit и `git subtree push/pull` для
  `project/app/stm32_acpi-ec` относительно remote `stm32`.
- `stm8_push.sh`, `stm8_pull.sh` — то же для `project/app/stm8-motor` и
  remote `stm8`; коммитится только этот каталог.
- `stm8-sdcc/` — отдельный Docker-образ для прошивки STM8: Debian bookworm
  (SDCC 4.2), `bschwand/STM8-SPL-SDCC` на пине
  `14b1afd61e8155bceecf78b358b225b34920f0f6`; используется только
  `project/app/stm8-motor/Makefile.stm8`, в основной builder не входит.
- `windows/` — Windows-версии утилит (`DriverAssitant`, `FactoryTool`, `SocToolKit`).

## Корневые файлы

- `build.sh`, `rkflash.sh` — симлинки на `project/build.sh` и `project/rkflash.sh`.
- `Dockerfile`, `docker-compose.yml`, `docker-entrypoint.sh` — Docker-окружение сборки.
- `start.sh` — сборка образа, запуск контейнера и вход в shell.
- `AGENTS.md` — правила агента `code`: карта документации, делегирование, инструменты.
- `README_RU.md` — русскоязычная точка входа OKO-форка: быстрый старт, сборка, ссылки.
- `FEATURES.md` — точка входа в описание функционала: паспорт платформы,
  разделение возможностей по статусу проверки, общие примечания и команды.
- `FEATURES_VERIFIED.md` — подтверждённые функции и возможности: видео и
  стриминг, сеть, телеметрия `oko-ec`, WebUI/API, microSD.
- `FEATURES_UNVERIFIED.md` — ограниченные и заблокированные возможности,
  непроверенные комбинации потоков, датчиков и microSD.
- `STRUCTURE.md`, `INIT_ORDER.md`, `TESTBENCH.md` — документация проекта: структура
  репозитория, порядок инициализации, параметры тестового стенда.
- `VERIFIED_FUNCTIONS.md` — реестр проверенных на стенде функций: статус,
  доказательство, ограничения и команды воспроизведения.
- `KNOWN_ISSUES.md` — известные ошибки и ограничения с обходами: NPU/YOLO,
  RGA, OSD, IVS, прошивка, безопасность и инструменты.
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