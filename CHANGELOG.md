# Чанжлог

Формат — [Keep a Changelog 1.1.0](https://keepachangelog.com/ru/1.1.0/),
версионирование — [SemVer 2.0.0](https://semver.org/lang/ru/).

Проект — форк SDK **Luckfox Pico V1.4**; базовая точка отсчёта — вендорский SDK
V1.4. История V1.0–V1.4 в этот файл не переносится и находится в `UPDATE_LOG.md`
(EN) и `UPDATE_LOG_CN.md` (CN).

## Правила ведения

1. Каждое изменение (функция, исправление, удаление) добавляется в `[Не выпущено]`
   в момент изменения. При выпуске раздел превращается в версию с датой
   в формате `ГГГГ-ММ-ДД`.
2. SemVer: **major** — ломающие изменения совместимости прошивки или поддержки
   оборудования; **minor** — новые функции; **patch** — исправления.
3. Категории: `Добавлено`, `Изменено`, `Исправлено`, `Удалено`, `Устарело`,
   `Безопасность`. Пустые категории опускаются.
4. История не переписывается: исправление ошибки в записи оформляется новой
   записью, а не правкой прошлой.
5. Файл обязательно обновляется при каждом изменении поведения, сборки,
   конфигурации или документации проекта.
6. `UPDATE_LOG.md` / `UPDATE_LOG_CN.md` — неприкосновенный вендорский архив.

## [Не выпущено]

### Добавлено

- `project/app/divinus/`: вендоренный OpenIPC/divinus (MIT, upstream-коммит
  `bca1932ed8b348af68fb33a82a67218e8f45db7a`), Makefile-обёртка для сборки в общем
  цикле SDK (кросс-компилятор `arm-rockchip830-linux-uclibcgnueabihf`, hard-float)
  со стейджингом `/oem/usr/bin/divinus` и `/oem/usr/share/divinus/divinus.yaml`,
  фабричный конфиг под SC230AI (H.265 1920x1080/25, JPEG, RTSP 554, web 80,
  watchdog 0) и `README.md` с пином коммита и правилами обновления.
- `RkLunch.sh`: переключатель `/userdata/divinus.enable` — opt-in запуск `divinus`
  вместо `rkipc`; без маркера порядок инициализации и запуск `rkipc` не меняются.
- `project/app/divinus/`: vendored OpenIPC/divinus (MIT, upstream commit
  `bca1932ed8b348af68fb33a82a67218e8f45db7a`), a Makefile wrapper for the SDK build
  loop (cross-compiler `arm-rockchip830-linux-uclibcgnueabihf`, hard-float) that
  stages `/oem/usr/bin/divinus` and `/oem/usr/share/divinus/divinus.yaml`, a
  SC230AI factory config (H.265 1920x1080/25, JPEG, RTSP 554, web 80, watchdog 0),
  and a `README.md` with the pinned commit and update rules.
- `RkLunch.sh`: `/userdata/divinus.enable` toggle for opt-in `divinus` startup
  instead of `rkipc`; without the marker the init order and `rkipc` startup are
  unchanged.
- `project/app/divinus/shim/rk_mmap_offset.c` + `libdivinus-mmap.so`: узкий
  LD_PRELOAD-обход бага rockit на RV1106 — mmap кольца VENC вызывается без
  корректного page offset (`mmap part 1 failed!`), из-за чего энкодер не отдавал
  кадры. Shim принудительно ставит offset 0 только для больших
  `MAP_SHARED|MAP_FIXED` mmap2 и подключается лишь к процессу `divinus`
  (`LD_PRELOAD` в `RkLunch.sh`); `rkipc` не затрагивается.
- `project/app/divinus/shim/rk_mmap_offset.c` + `libdivinus-mmap.so`: a narrow
  LD_PRELOAD workaround for the RV1106 rockit bug where the VENC ring mmap2 call
  lacks a valid page offset (`mmap part 1 failed!`), leaving the encoder without
  frames. The shim forces offset 0 only for large `MAP_SHARED|MAP_FIXED` mmap2
  calls and is applied to the `divinus` process alone (`LD_PRELOAD` in
  `RkLunch.sh`); `rkipc` is unaffected.
- Тестовый маркер `S99oko-test` в оверлее `overlay-luckfox-buildroot-init`:
  скрипт пишет `/tmp/oko-test-ok` (`marker=OKO-TEST-2026.10-01`, uptime, версия
  ядра) и строку `OKO-TEST: …` в `/dev/kmsg`; доказывает, что загружена сборка
  после этой правки.
- Netboot-монтирование `oem` и `userdata` с хоста: `mount_part()` в генерируемом
  `project/build.sh` `S20linkmount` при `root=/dev/nfs` монтирует
  `<host>:<nfsroot>/.nfsroot/<раздел>` по NFS (сервер и путь парсятся из
  `nfsroot=` в `/proc/cmdline`, в том числе без завершающих опций; при неудачном
  разборе печатается причина); при ошибке монтирования — откат на ubifs из SPI NAND.
- `RkLunch.sh`: ветка выбора `/oem/usr/share/rkipc-300w.ini` для сенсора
  `sc230ai` (раньше `post_chk` завершался с `Error: not found rkipc.ini !!!`).
- `RkLunch.sh`: в netboot (`nfsroot=` в cmdline) `network_init` не меняет MAC и
  не запускает `udhcpc` — статический адрес из `ip=` сохраняется, NFS-соединения
  корня и разделов не разрываются.

### Изменено

- `STRUCTURE.md`: добавлен пакет `project/app/divinus/` (вендорские исходники,
  фабричный конфиг, Makefile) и уточнён состав `project/app/`.
- `INIT_ORDER.md`: ветка `divinus.enable` в `post_chk` и зависимости `divinus`
  (общий rockit/ISP-конвейер с `rkipc`, `dlopen` вендорных библиотек,
  `LD_PRELOAD`-обход rockit).
- `TESTBENCH.md`: чек-лист divinus (`/api/status`, `/image.jpg`, RTSP-корень),
  диагностика сбоев и особенности портов/конфига.
- `STRUCTURE.md`: added the `project/app/divinus/` package (vendored sources,
  factory config, Makefile) and clarified the `project/app/` contents.
- `INIT_ORDER.md`: documented the `divinus.enable` branch in `post_chk` and the
  `divinus` dependencies (shared rockit/ISP pipeline with `rkipc`, `dlopen` of
  vendor libraries, the rockit `LD_PRELOAD` workaround).
- `TESTBENCH.md`: added the divinus checklist (`/api/status`, `/image.jpg`, RTSP
  root) plus failure diagnostics and port/config notes.
- `INIT_ORDER.md`: источник `oem`/`userdata` при netboot, ветка `S20linkmount`,
  строка `S99oko-test` в порядке init-скриптов, поведение сети при netboot.
- `TESTBENCH.md`: `.nfsroot` внутри экспорта `/srv/nfs/rootfs`, деплой rsync с
  бэкапом, проверки маркера и NFS-монтирований, статический IP в netboot.
- `INIT_ORDER.md`: зафиксировано условие, что сборка прошивки OKO выполняется в
  Docker-контейнере `luckfox_oko_build_env` (`docker compose build` / `up -d`,
  запуск через `start.sh`); в разделе источников сборки добавлена строка про
  Docker-окружение. Сборка без Docker остаётся вендорским fallback в `README_RU.md`.
- `INIT_ORDER.md`: recorded that the OKO firmware build runs in the
  `luckfox_oko_build_env` Docker container (`docker compose build` / `up -d`, started via
  `start.sh`), and added a Docker environment row to the build sources table. Building
  without Docker remains a vendor fallback in `README_RU.md`.
- Уточнены правила делегирования субагентам: промпт самодостаточный, с выжимкой фактов,
  без путей и номеров разделов документации и без имён skills; зафиксирована область
  применения `AGENTS.md` (исполняет только агент `code`, правила дополняют базовые
  инструкции) и привязка команд `/bench`, `/docs-check` к `code`.
- Refined sub-agent delegation rules: the prompt is self-contained with distilled facts,
  without documentation paths, section numbers, or skill names; recorded that `AGENTS.md`
  applies only to the `code` agent and supplements the base instructions, and that
  `/bench` and `/docs-check` are bound to `code`.
- `STRUCTURE.md`: описание `.kilo/` приведено к фактическому составу (команды, skill,
  планы), в список корневых файлов добавлены документы OKO и `AGENTS.md`;
  `.kilo/.gitignore` больше не игнорирует сам себя.
- `STRUCTURE.md`: the `.kilo/` description now matches the actual layout (commands, skill,
  plans), OKO documents and `AGENTS.md` were added to the root files list, and
  `.kilo/.gitignore` no longer ignores itself.

## [0.1.0] — 2026-10-29

Первая версия OKO-форка. База — Luckfox SDK V1.4.

### Добавлено

- Docker-окружение сборки: `Dockerfile` (Ubuntu 22.04, `luckfox-builder:22.04`,
  пользователь `builder` с UID/GID хоста, кросс-тулчейн Rockchip в `PATH`,
  `tftpd-hpa`), `docker-compose.yml` (контейнер `luckfox_oko_build_env`,
  `network_mode: host`, `TFTP_ROOT=/workspace/output/image`),
  `docker-entrypoint.sh` (запуск TFTP-сервера на `69/udp`) и `start.sh`
  (сборка образа, запуск контейнера, Git LFS, вход в shell).
- Конфигурация платы
  `project/cfg/BoardConfig_IPC/BoardConfig-SPI_NAND-Buildroot-RV1106_Luckfox_Pico_Pro_Max-NETBOOT-SC230AI.mk`
  (RV1106 Pro Max, SPI NAND, Buildroot, NETBOOT, SC230AI, `RKIPC_RV1106`,
  CMA 66M, `RK_ENABLE_WIFI=y`) и локальный симлинк `.BoardConfig.mk` на неё
  (симлинк не версионируется — `.gitignore` его исключает, создаётся
  `./build.sh lunch`). Рядом присутствует промежуточный вариант `…-NETBOOT.mk`
  без SC230AI.
- Фрагменты конфигурации ядра `rv1106-netboot.config` (`CONFIG_IP_PNP`,
  `CONFIG_ROOT_NFS`) и `rv1106-sc230ai.config`; подключены через
  `RK_KERNEL_DEFCONFIG_FRAGMENT`.
- Локальные симлинки быстрого доступа к конфигурации (не версионируются,
  каталог `config/` в `.gitignore`): `config/buildroot_defconfig`,
  `config/kernel_defconfig`, `config/dts_config`.
- Автоматизация netboot по последовательному порту: `netboot-catch.py`
  (последний `/dev/ttyACM*`, 115200, настройка `ipaddr`/`netmask`/`serverip`/
  `gatewayip`/`sys_bootargs`, `tftp` → `boot_fit`, лог `/tmp/kilo/netboot-run4.log`).
- Документация проекта: `README_RU.md`, `CHANGELOG.md`, `TESTBENCH.md`,
  `STRUCTURE.md`, `INIT_ORDER.md`.
- `.gitignore` для артефактов сборки (`output/`, `IMAGE/`, `config/`, `*.o`,
  `*.ko`, `build/`), core-дампов и Python-кэша.

### Изменено

- `sysdrv/drv_ko/insmod_ko.sh`: добавлена загрузка `sc230ai.ko` и `sc230ai`
  в список выгрузки неиспользуемых сенсоров (`__rmmod_camera_sensor`).
- Драйвер шагового двигателя 28BYJ-48 из вендорского SDK V1.4
  (`sysdrv/drv_ko/motor`, строка `__insmod motor.ko`) остаётся **выключенным**:
  `RK_ENABLE_MOTOR` не задан, модуль не собирается, а загрузка пропускается
  проверкой `-f`.
- Device tree `sysdrv/source/kernel/arch/arm/boot/dts/rv1106-luckfox-pico-pro-max-ipc.dtsi`
  адаптирован под текущую плату и сенсор: добавлены регуляторы и узел SC230AI,
  endpoint CSI D-PHY, mux линии сброса.
- Драйвер `sysdrv/source/kernel/drivers/media/i2c/sc230ai.c`: для не-thunderboot
  сборки линии `reset`/`pwdn` запрашиваются в состоянии `GPIOD_OUT_LOW`.