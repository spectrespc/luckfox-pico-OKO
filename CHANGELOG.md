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

- `FEATURES.md` в корне: сводка поддерживаемого функционала — платформа и состав
  образа, видео/стриминг и статусы гейтов, WebUI/API, сеть, `oko-ec`, microSD,
  известные ограничения и команды проверки; продублировано краткой
  English-сводкой. Ссылки добавлены в `README_RU.md`, `STRUCTURE.md` и
  `AGENTS.md`.
- `FEATURES.md` in the repository root: a summary of supported functionality —
  platform and image contents, video/streaming and gate statuses, WebUI/API,
  networking, `oko-ec`, microSD, known limitations and verification commands,
  with a short English digest. Links added to `README_RU.md`, `STRUCTURE.md`
  and `AGENTS.md`.
- I2C3-M1 переведён на 100 кГц (`clock-frequency = <100000>`) в активном
  `rv1106g-luckfox-pico-pro-max-sc230ai.dts`; устройства SHT21/INA3221/
  STM32 ACPI-EC на `/dev/i2c-3` работают на этой скорости.
- I2C3-M1 runs at 100 kHz (`clock-frequency = <100000>`) in the active
  `rv1106g-luckfox-pico-pro-max-sc230ai.dts`; the SHT21/INA3221/STM32
  ACPI-EC devices on `/dev/i2c-3` use that bus speed.
- WebUI/API: `/api/network` и секция **Network → Ethernet** переключают
  DHCP/static IPv4. Настройки `oko_net_mode`, `oko_ipaddr`, `oko_netmask`,
  `oko_gateway`, `oko_dns` и постоянный `oko_mac` хранятся в env-разделе
  U-Boot (`fw_printenv`/`fw_setenv`, `/etc/fw_env.config`), а не в
  `/userdata`, и переживают reboot и покомпонентную прошивку. Полный
  `update.img` перезаписывает `env.img`, но MAC восстанавливается из serial;
  статические настройки после полной прошивки задаются заново.
- WebUI/API: `/api/network` and **Network → Ethernet** switch DHCP/static
  IPv4. `oko_net_mode`, `oko_ipaddr`, `oko_netmask`, `oko_gateway`,
  `oko_dns` and the persistent `oko_mac` live in the U-Boot env partition
  (`fw_printenv`/`fw_setenv`, `/etc/fw_env.config`), not in `/userdata`, so
  they survive a reboot and component flashing. A full `update.img` rewrites
  `env.img`, but the MAC is re-derived from the serial number; static
  settings must be re-applied after a full flash.
- Buildroot: включены `BR2_PACKAGE_UBOOT_TOOLS=y` и
  `BR2_PACKAGE_UBOOT_TOOLS_FWPRINTENV=y` (`/usr/sbin/fw_printenv`,
  `fw_setenv`); добавлен `/etc/fw_env.config` для `/dev/mtd0`.
- Buildroot: enabled `BR2_PACKAGE_UBOOT_TOOLS=y` and
  `BR2_PACKAGE_UBOOT_TOOLS_FWPRINTENV=y` (`/usr/sbin/fw_printenv`,
  `fw_setenv`); added `/etc/fw_env.config` for `/dev/mtd0`.
- `divinus`: медиа-фичи этапа divinus-media-features — ePTZ (кроп VPSS),
  поворот 90/180/270 (VENC), SmartP/SVC/motion deblur/QP, второй поток
  `/live/1` (RTSP на путь), AIISP (`/api/aiisp`, модель
  `rkmodel_1080_1920.aiisp`), IVS (`/api/ivs`: MD/OD, снимок, клип, OSD),
  runtime IQ и HDR API; непроверенные комбинации блокируются `gating.c`
  с причиной. WebUI получил секции Geometry, Encoding, Night image, Image,
  Analytics, Sub stream, Streams и Storage. Результаты гейтов —
  `project/app/divinus/source/doc/README.md`.
- `divinus`: media feature set from the divinus-media-features stage — ePTZ
  (VPSS crop), 90/180/270 rotation (VENC), SmartP/SVC/motion deblur/QP, a
  second `/live/1` stream (per-path RTSP), AIISP (`/api/aiisp`, model
  `rkmodel_1080_1920.aiisp`), IVS (`/api/ivs`: MD/OD, snapshot, clip, OSD),
  runtime IQ and HDR APIs; unverified combinations are refused by `gating.c`
  with a reason. The WebUI gained Geometry, Encoding, Night image, Image,
  Analytics, Sub stream, Streams and Storage sections. Gate results are in
  `project/app/divinus/source/doc/README.md`.
- microSD: udev-скрипт `oko-sdcard` монтирует карту в `/mnt/sdcard` и пишет
  `/run/oko-sd.state`; `record.source`, `record.max_bytes` и
  `record.min_free_mb` выбирают поток и чистят старые сегменты. Запись в `/oem`
  и `/userdata` запрещена, без реально смонтированной карты — отказ.
  DTS `&sdmmc` переведён из `non-removable`/SDIO в режим съёмной карты.
- microSD: the `oko-sdcard` udev helper mounts the card at `/mnt/sdcard` and
  writes `/run/oko-sd.state`; `record.source`, `record.max_bytes` and
  `record.min_free_mb` select the feed and prune the oldest segments. Writing
  to `/oem` or `/userdata` is refused, and recording without a real mount is
  blocked. The `&sdmmc` DTS node no longer uses `non-removable`/SDIO flags.
- Сборка: `RK_AIISP_MODEL=rkmodel_1080_1920.aiisp` в BoardConfig копирует
  выбранную модель в `/oem/usr/lib`; из oem удаляются стресс/AVS-демо
  (`sample_*_stresstest`, `sample_*avs*`, `rk_mpi_avs_test`, `rkisp_demo`,
  `j2s4b_dev`), чтобы уменьшить размер oem.
- Build: `RK_AIISP_MODEL=rkmodel_1080_1920.aiisp` in the board config installs
  the selected model in `/oem/usr/lib`; stress/AVS demos
  (`sample_*_stresstest`, `sample_*avs*`, `rk_mpi_avs_test`, `rkisp_demo`,
  `j2s4b_dev`) are pruned from oem to reduce its size.
- `oko-osd`: в эмуляцию телеметрии добавлены LTE (RSRP, dBm + 1–5 полос) и
  WiFi (RSSI, dBm + 1–5 полос); значения меняются раз в 15 с вместе с
  GPS/температурой/батареей.
- `oko-osd`: added simulated LTE (RSRP in dBm + 1–5 bars) and WiFi (RSSI in dBm
  + 1–5 bars) to the telemetry; values change every 15 s together with
  GPS/temperature/battery.
- `project/app/divinus/`: вендоренный OpenIPC/divinus (MIT, upstream-коммит
  `bca1932ed8b348af68fb33a82a67218e8f45db7a`), Makefile-обёртка для сборки в общем
  цикле SDK (кросс-компилятор `arm-rockchip830-linux-uclibcgnueabihf`, hard-float)
  со стейджингом `/oem/usr/bin/divinus` и `/oem/usr/share/divinus/divinus.yaml`,
  фабричный конфиг под SC230AI (H.265 1920x1080/25, JPEG, web 80 и RTSP 554
  с обязательной аутентификацией, watchdog 0) и `README.md` с пином коммита
  и правилами обновления.
- `RkLunch.sh`: переключатель `/userdata/divinus.enable` — opt-in запуск `divinus`
  вместо `rkipc`; без маркера порядок инициализации и запуск `rkipc` не меняются.
- `project/app/divinus/`: vendored OpenIPC/divinus (MIT, upstream commit
  `bca1932ed8b348af68fb33a82a67218e8f45db7a`), a Makefile wrapper for the SDK build
  loop (cross-compiler `arm-rockchip830-linux-uclibcgnueabihf`, hard-float) that
  stages `/oem/usr/bin/divinus` and `/oem/usr/share/divinus/divinus.yaml`, a
  SC230AI factory config (H.265 1920x1080/25, JPEG, web 80 and RTSP 554 with
  authentication enabled, watchdog 0), and a `README.md` with the pinned commit
  and update rules.
- `RkLunch.sh`: `/userdata/divinus.enable` toggle for opt-in `divinus` startup
  instead of `rkipc`; without the marker the init order and `rkipc` startup are
  unchanged.
- `project/app/divinus/source/src/compat.c`: локальный патч ABI `mmap` для
  uClibc hard-float — собственный 64-битный `mmap` (из-за
  `_FILE_OFFSET_BITS=64` и `-rdynamic`) перехватывал вызовы `librockit.so`
  с 32-битным `off_t`, offset читался из неверного stack-слота, и кольцо VENC
  не отображалось (`mmap part 1 failed!`). Для `__ARM_PCS_VFP && __UCLIBC__`
  используется libc-вариант `mmap`/`mmap64`; `LD_PRELOAD` не нужен.
- `project/app/divinus/source/src/compat.c`: local ABI `mmap` patch for uClibc
  hard-float — the package's own 64-bit `mmap` (caused by
  `_FILE_OFFSET_BITS=64` plus `-rdynamic`) interposed the 32-bit `off_t`
  `mmap` calls of `librockit.so`, the offset was read from the wrong stack slot
  and the VENC ring could not be mapped (`mmap part 1 failed!`). On
  `__ARM_PCS_VFP && __UCLIBC__` the libc `mmap`/`mmap64` is used; no
  `LD_PRELOAD` is required.
- Тестовый маркер `S99oko-test` в оверлее `overlay-luckfox-buildroot-init`:
  скрипт пишет `/tmp/oko-test-ok` (`marker=OKO-TEST-2026.10-01`, uptime, версия
  ядра) и строку `OKO-TEST: …` в `/dev/kmsg`; доказывает, что загружена сборка
  после этой правки.
- `project/cfg/BoardConfig_IPC/overlay/overlay-luckfox-buildroot-init/`:
  `etc/init.d/S20oko-osd` (активация `divinus` по умолчанию при отсутствии
  `/userdata/rkipc.enable`, бутстрап OSD-конфига, создание `/tmp/osd.fifo` и
  запуск демона) и `usr/bin/oko-osd` (эмуляция GPS/температуры/АКБ в
  `/tmp/osd-dyn.txt` каждые 15 с; сообщения из FIFO в `/tmp/osd-msg.txt`
  с TTL 60 с). UART/ADC-оборудование не используется.
- `project/app/divinus/`: фабричный `divinus.yaml` включает OSD (`reg0` —
  дата/время `$t`, `reg1` — телеметрия, `reg2` — внешние сообщения) и локальный
  патч `source/src/region.c`: текст региона `@<path>` рендерится из первой строки
  файла, перечитывается раз в секунду, обновляет кадр при изменении и гасит
  регион при пустом/нечитаемом файле.
- `project/app/divinus/`: локальный патч `source/src/hal/plus/rk_hal.c` и
  `rk_rgn.h` для OSD на RV1106: преобразование растра `ARGB1555` в `BGRA5551`,
  выравнивание позиции и размера региона на 16 пикселей, поле `BITMAP_S`
  перед указателем данных (`pData` на смещении 16 в используемом
  `librockit.so`), выделение canvas региона и отказ от повторного прикрепления
  уже прикреплённого региона (иначе `RK_MPI_RGN_SetBitMap` возвращает ошибку,
  а повторный attach приводит к oops).
- `project/app/divinus/`: local `source/src/hal/plus/rk_hal.c` and `rk_rgn.h`
  patch for RV1106 OSD: converts `ARGB1555` text bitmaps to `BGRA5551`, aligns
  the region position and size to 16 pixels, adds the `BITMAP_S` field before
  the data pointer (`pData` at offset 16 in the shipped `librockit.so`),
  allocates the region canvas and skips re-attaching an already attached region
  (otherwise `RK_MPI_RGN_SetBitMap` fails and a repeated attach leads to an
  oops).
- Overlay `overlay-luckfox-buildroot-init`: `etc/init.d/S20oko-osd` (enables
  `divinus` by default unless `/userdata/rkipc.enable` exists, bootstraps the OSD
  config, creates `/tmp/osd.fifo`, starts the daemon) and `usr/bin/oko-osd`
  (simulated GPS/temperature/battery written to `/tmp/osd-dyn.txt` every 15 s;
  FIFO messages shown in `/tmp/osd-msg.txt` for 60 s). No UART/ADC hardware is
  used.
- `project/app/divinus/`: the factory `divinus.yaml` enables OSD (`reg0` —
  date/time `$t`, `reg1` — telemetry, `reg2` — external messages) and a local
  `source/src/region.c` patch renders `@<path>` region text from the first line
  of a file, re-reads it once per second, refreshes the frame on change and
  hides the region while the file is empty or unreadable.
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
- `bench/`: утилиты тестового стенда в репозитории вместо `/tmp` —
  `serial_exec.py` (UART), `dut.py` (SSH), `netboot.py` (`catch`/`reboot`/`cmd`
  для U-Boot), `osd_check.py` (попарный диф OSD-регионов по RTSP), а также
  `setup.sh`, `run.sh`, `requirements.txt` и `README.md`; venv, логи и кадры —
  в `output/bench/` (gitignore).
- `bench/`: bench utilities in the repository instead of `/tmp` —
  `serial_exec.py` (UART), `dut.py` (SSH), `netboot.py` (`catch`/`reboot`/`cmd`
  for U-Boot), `osd_check.py` (pairwise OSD region diff over RTSP), plus
  `setup.sh`, `run.sh`, `requirements.txt` and `README.md`; the venv, logs and
  frames live in `output/bench/` (gitignore).
- `project/app/divinus/tests/bmp_align_test.c`: хостовый тест выравнивания
  растра OSD (660x46 → 672x48, stride 672), не входит в Makefile пакета;
  правило ре-вендоринга требует прогонять его после HAL-патча.
- `project/app/divinus/tests/bmp_align_test.c`: host test for the OSD raster
  alignment (660x46 → 672x48, stride 672), not part of the package Makefile;
  the re-vendoring rules require running it after the HAL patch.
- I2C3-M1 включён по умолчанию на 100 кГц:
  `sysdrv/source/kernel/arch/arm/boot/dts/rv1106g-luckfox-pico-pro-max.dts`,
  блок `&i2c3` — `status = "okay"`, `clock-frequency = <100000>`,
  `pinctrl-0 = <&i2c3m1_xfer>` (выводы `GPIO1_D2`/`GPIO1_D3`). Устройство
  `/dev/i2c-3` появляется при загрузке без вызова `luckfox-config`; группы
  I2C3-M0 из общего dtsi больше не мультиплексируются.
- I2C3-M1 enabled by default at 100 kHz:
  `sysdrv/source/kernel/arch/arm/boot/dts/rv1106g-luckfox-pico-pro-max.dts`,
  the `&i2c3` node — `status = "okay"`, `clock-frequency = <100000>`,
  `pinctrl-0 = <&i2c3m1_xfer>` (`GPIO1_D2`/`GPIO1_D3` pins). `/dev/i2c-3` is
  available at boot without running `luckfox-config`; the I2C3-M0 groups from
  the shared dtsi are no longer muxed.
- `project/app/oko-ec/`: утилита `/oem/usr/bin/oko-ec` для устройств на
  `/dev/i2c-3` — SHT21 `0x40`, INA226 `0x41`, STM32 ACPI-EC `0x42`. Команды
  `scan`, `info`, `status`, `battery`, `cells`, `temp`, `ina226`, `sht21`,
  `sensors`, OTA STM32 (`flash`/`enter-update`/`reset`) и демон
  `oko-ec daemon` (интервал 2 с, `/tmp/oko-ec.json`, `/tmp/oko-ec.osd`,
  `/tmp/oko-ec.pid`). `oko-osd` берёт реальные `T`/`BAT` из свежего
  `/tmp/oko-ec.osd` (≤ 30 с) и иначе работает на симуляции. Датчики читаются
  через `i2c-dev` без драйверов ядра; INA226 — без калибровочного регистра
  (шунт 100 мОм по умолчанию), SHT21 — no-hold с CRC8. Опция `--json`/`--quiet`,
  адреса/шунт/тайминги переопределяются. Вендорен хостовый инструмент upstream
  `spectrespc/stm32_acpi-ec` на коммите
  `41728ddaf6936d49a5fb65676ca5203dd631b4f6` (файла `LICENSE` в upstream нет);
  `S21oko-ec` запускает демон при наличии `/dev/i2c-3` и `/oem/usr/bin/oko-ec`,
  opt-out — `/userdata/oko-ec.disable`.
- `project/app/oko-ec/`: the `/oem/usr/bin/oko-ec` utility for devices on
  `/dev/i2c-3` — SHT21 `0x40`, INA226 `0x41`, STM32 ACPI-EC `0x42`. Commands:
  `scan`, `info`, `status`, `battery`, `cells`, `temp`, `ina226`, `sht21`,
  `sensors`, STM32 OTA (`flash`/`enter-update`/`reset`) and the
  `oko-ec daemon` loop (2 s interval, `/tmp/oko-ec.json`, `/tmp/oko-ec.osd`,
  `/tmp/oko-ec.pid`). `oko-osd` uses real `T`/`BAT` from a fresh
  `/tmp/oko-ec.osd` (≤ 30 s) and otherwise keeps simulating them. Sensors are
  read through `i2c-dev` without kernel drivers; INA226 uses no calibration
  register (100 mOhm shunt by default), SHT21 is no-hold with CRC8.
  `--json`/`--quiet` are supported and addresses/shunt/timings are overridable.
  The upstream host tool `spectrespc/stm32_acpi-ec` is vendored at commit
  `41728ddaf6936d49a5fb65676ca5203dd631b4f6` (no `LICENSE` file in upstream);
  `S21oko-ec` starts the daemon when `/dev/i2c-3` and `/oem/usr/bin/oko-ec`
  exist, with `/userdata/oko-ec.disable` as opt-out.
- `oko-ec`: поддержка INA3221 (die ID `0x3220`, три канала, 13-битные значения
  `D15:D3`, LSB 40 мкВ/8 мВ) с автоопределением чипа по `0xFF` и выбором канала
  `--ina-channel 1..3` (`OKO_EC_INA_CHANNEL`); INA226 (`0x2260`) продолжает
  поддерживаться. OSD (reg1) выводит четыре строки: SHT21 (температура и
  влажность), STM32 (напряжение/ток/температура/SOC/ячейки), INA3221
  (напряжение/ток/мощность) и LTE/WiFi; при отсутствии источника — `--`, без
  свежих данных — прежняя симуляция `T`/`BAT`. На стенде на `0x41` определён
  INA3221 (силовые входы пока не подключены, показания 0).
- `oko-ec`: INA3221 support (die ID `0x3220`, three channels, 13-bit values in
  `D15:D3`, 40 uV/8 mV LSB) with chip auto-detection via register `0xFF` and
  channel selection `--ina-channel 1..3` (`OKO_EC_INA_CHANNEL`); INA226
  (`0x2260`) remains supported. The OSD region reg1 now shows four lines:
  SHT21 (temperature and humidity), STM32 (voltage/current/temperature/SOC/
  cells), INA3221 (voltage/current/power) and LTE/WiFi; a missing source is
  shown as `--`, and stale data falls back to the previous `T`/`BAT`
  simulation. The testbench device at `0x41` is an INA3221 (power inputs are
  not connected yet, so the readings are zero).

- Разметка SPI NAND: `boot` увеличен с 4 до 6 МБ, `oem` — с 30 до 40 МБ,
  `rootfs` уменьшен с 210 до 198 МБ
  (`256K(env),256K@256K(idblock),512K(uboot),6M(boot),40M(oem),10M(userdata),198M(rootfs)`,
  всего 255 МБ). После этой сборки нужна **полная прошивка** `update.img`:
  покомпонентная выкатка со старой таблицей несовместима, `/userdata` и данные
  на NAND теряются.
- SPI NAND layout: `boot` grows from 4 to 6 MiB, `oem` from 30 to 40 MiB and
  `rootfs` shrinks from 210 to 198 MiB
  (`256K(env),256K@256K(idblock),512K(uboot),6M(boot),40M(oem),10M(userdata),198M(rootfs)`,
  255 MiB total). This build requires a **full `update.img` flash**: component
  flashing with the old table is incompatible, and `/userdata` plus the data on
  NAND are lost.
- WebUI/API storage: `/api/storage` получил `mount`, `unmount` и `format`
  (FAT32/MBR, метка `OKO`, один раздел), поля `device`, `fs`,
  `format_supported`, `format_busy` и одноразовый `format_token`; формат
  требует `confirm=FORMAT`, блокируется при записи и сериализован с udev через
  `/run/oko-sd.lock`. Секция Storage получила `Mount`, `Unmount`, `Format card`
  и скрытый токен.
- WebUI/API storage: `/api/storage` gained `mount`, `unmount` and `format`
  (FAT32/MBR, label `OKO`, one partition), plus `device`, `fs`,
  `format_supported`, `format_busy` and a one-shot `format_token`; formatting
  requires `confirm=FORMAT`, is refused while recording and is serialized with
  udev through `/run/oko-sd.lock`. The Storage section gained `Mount`,
  `Unmount`, `Format card` and a hidden token field.
- `VERIFIED_FUNCTIONS.md`: реестр проверенных на стенде функций (дата, плата,
  стенд `192.168.52.97`, доказательства, ограничения) и правила его ведения;
  ссылки добавлены в `README_RU.md` и `STRUCTURE.md`. После прогона 2026-11-04
  обновлены статусы разметки, формата, hotplug, AIISP, RTSP и записи.
- `VERIFIED_FUNCTIONS.md`: a registry of functions verified on the bench (date,
  board, `192.168.52.97` test stand, evidence and limitations) with maintenance
  rules; linked from `README_RU.md` and `STRUCTURE.md`. The 2026-11-04 run
  updated the statuses for the NAND layout, format, hotplug, AIISP, RTSP and
  recording.
- Buildroot: `sysdrv/tools/board/buildroot/luckfox_pico_defconfig` включает
  `BR2_PACKAGE_UTIL_LINUX_BINARIES=y` и `BR2_PACKAGE_UTIL_LINUX_WIPEFS=y`,
  поэтому в rootfs появляются `sfdisk`, `wipefs`, `fdisk` и другие утилиты
  (нужны для форматирования microSD).
- Buildroot: `sysdrv/tools/board/buildroot/luckfox_pico_defconfig` enables
  `BR2_PACKAGE_UTIL_LINUX_BINARIES=y` and `BR2_PACKAGE_UTIL_LINUX_WIPEFS=y`, so
  the rootfs contains `sfdisk`, `wipefs`, `fdisk` and the rest of the basic
  util-linux set (needed to format the microSD card).
- WebUI: страница `/` переработана в восемь разделов — **Live, Image, Streams,
  Events, Storage, Network, OSD, System** — с боковым меню, липкой статус-строкой
  (IP, аптайм, load, температура, microSD), переключателем **EN/RU** (по
  умолчанию `en`, выбор в `localStorage`), тостами ошибок (`reason`/`hint`) и
  индикацией неприменённых изменений; описание — в
  `project/app/divinus/source/doc/WEBUI_REORG.md`.
- WebUI: the `/` page is reworked into eight sections — **Live, Image, Streams,
  Events, Storage, Network, OSD, System** — with a sidebar, a sticky status bar
  (IP, uptime, load, SoC temperature, microSD), an **EN/RU** switch (default
  `en`, choice kept in `localStorage`), error toasts built from `reason`/`hint`,
  and unsaved-change indicators; see
  `project/app/divinus/source/doc/WEBUI_REORG.md`.
- API: `/api/cmd` получил `reboot`, `export` (in-memory YAML как вложение) и
  `log` (хвост `/var/log/messages`, до 256 КиБ); добавлены `POST
  /api/cmd/import` (multipart, замена `divinus.yaml` с ротацией `.bak`),
  `POST /api/web-auth` (смена пароля WebUI с проверкой текущего),
  `/api/recordings` (+ `/file`, `/delete`), `/api/telemetry`; `/api/status`
  теперь отдаёт `ip`, `sd_free_mb`, `sd_total_mb`.
- API: `/api/cmd` gained `reboot`, `export` (in-memory YAML as an attachment) and
  `log` (tail of `/var/log/messages`, up to 256 KiB); added `POST
  /api/cmd/import` (multipart, replaces `divinus.yaml` and rotates `.bak`),
  `POST /api/web-auth` (WebUI password change with current-password check),
  `/api/recordings` (+ `/file`, `/delete`) and `/api/telemetry`; `/api/status`
  now returns `ip`, `sd_free_mb` and `sd_total_mb`.
- NTP: `/api/time` принимает `ntp=on|off` и `ntp_server`; ключи
  `ntp_enable`/`ntp_server` пишутся в секцию `system` YAML. `ntp.c` на старте
  учитывает их через 10 с (после `S49ntp`): при выключенном NTP останавливает
  демон, при заданном сервере поднимает `ntpd -c /tmp/divinus-ntp.conf`, при
  пустом сервере не трогает вендорский `/etc/init.d/S49ntp` (или поднимает его,
  если тот не стартовал). Проверено тремя перезагрузками стенда.
- NTP: `/api/time` accepts `ntp=on|off` and `ntp_server`; the `ntp_enable` /
  `ntp_server` keys are stored in the YAML `system` section. Ten seconds after
  startup (after `S49ntp`) `ntp.c` applies them: a disabled client stops the
  daemon, a configured server starts `ntpd -c /tmp/divinus-ntp.conf`, and an
  empty server leaves the vendor `/etc/init.d/S49ntp` daemon alone (or starts it
  if it did not come up). Verified with three bench reboots.
- Стенд: `bench/api_check.py` (матрица GET/POST и негативных проверок API),
  `bench/ui_check.py` (headless Chromium: разделы, EN/RU, dirty-state,
  отсутствие горизонтальной прокрутки на 1440 и 390 px, скриншоты в
  `output/bench/ui/`) и `bench/deploy_divinus.py` (выкатка собранного бинарника
  в `/oem` по SSH для быстрой итерации).
- Bench: `bench/api_check.py` (GET/POST and negative API matrix),
  `bench/ui_check.py` (headless Chromium: sections, EN/RU, dirty state, no
  horizontal scroll at 1440 and 390 px, screenshots in `output/bench/ui/`) and
  `bench/deploy_divinus.py` (installs the built binary into `/oem` over SSH for
  fast iteration).
- Документация OKO: `project/app/divinus/source/doc/WEBUI_REORG.md` (разбор
  «до/после», карта разделов, матрица функций ipcweb, backlog), обновлены
  `endpoints.md`, `INIT_ORDER.md` (порядок NTP и откат конфига),
  `TESTBENCH.md` (§1, §7B, §8), `STRUCTURE.md`, `bench/README.md`,
  `FEATURES.md`, `VERIFIED_FUNCTIONS.md`; `doc/webui.png` заменён, прежний
  экран сохранён как `doc/webui-before.png`.
- OKO documentation: `project/app/divinus/source/doc/WEBUI_REORG.md` (before and
  after, section map, ipcweb feature matrix, backlog), updated
  `endpoints.md`, `INIT_ORDER.md` (NTP ordering and config rollback),
  `TESTBENCH.md` (§1, §7B, §8), `STRUCTURE.md`, `bench/README.md`,
  `FEATURES.md`, `VERIFIED_FUNCTIONS.md`; `doc/webui.png` is replaced and the
  previous screen is kept as `doc/webui-before.png`.
- `divinus`: NPU-аналитика YOLOv5n. RKNN подключается через `dlopen`
  `librknnmrt.so` (`hal/plus/rk_rknn.h`, копия `rknn_api.h` 1.6.0), конверсия
  NV12→RGB888 — на CPU (`yolo_convert_nv12_rgb`): librga на этом образе
  непригодна, легаси `c_RkRgaBlit` падает с SIGSEGV, а im2d
  `importbuffer_fd` не импортирует ни MMZ-, ни dma-heap-дескрипторы; эта же
  RGA-операция на куче и вешала плату. Новые модули:
  `analytics/yolo.c` (тред захвата, кольцо пре-ролла, событие, отправитель),
  `analytics/yolo_postprocess.c` (декод трёх ветвей и NMS),
  `analytics/models.c` (каталог `/userdata/models`, атомарная загрузка,
  активация с откатом). Секция `[yolo]` (`enable`, `source`, `model`,
  `input_width/height`, `fps`, `conf_threshold`, `nms_threshold`, `classes`,
  `pre_seconds`, `post_seconds`, `max_frames`) добавлена в разбор и печать
  конфига; API — `GET/POST /api/yolo`, `GET /api/models`,
  `POST /api/models`, `/api/models/activate`, `/api/models/delete`; WebUI —
  карточки «Object detection (NPU)» и «Models» в разделе Events.
  Событие отправляется N последовательными `PUT` с `X-Event-Id`,
  `X-Frame-Index`, `X-Frame-Total`, `X-Frame-Ts`, `X-Event-Cause`, `X-Model`;
  `http_post.host` теперь принимает `host:port`. Гейт `yolo` в `gating.c`
  проверяет runtime, модель и занятость канала VPSS 3; RGA не используется
  (препроцессинг на CPU). `yolo.source` по умолчанию `vpss3`, при IVS/MJPEG
  санитайзер переводит на `vi0`.
  Замер M0 на стенде: `librknnmrt.so` — **1.6.0** (не 1.5.2), драйвер 0.9.2
  совместим, `/proc/rknpu/load` = 77% на YOLOv5n 640×640, ~9 кадр/с,
  `MemAvailable` −8.7 МБ; включение VI selfpath (chn1) в этой сборке
  привело к зависанию платы, поэтому `source: vi1` заблокирован.
  Проверено на стенде (`source: vpss3`, YOLOv5n 640×640, 3 fps): детекция
  идёт, NPU 28–33%, событие отдаёт 6 JPEG 640×640 с заголовками
  `X-Event-*`, модель из `/tmp`; `api_check.py` — 78/78, RTSP/OSD — 20
  кадров без регрессии; загрузка/активация/удаление модели и откат при
  `rknn_init` проверены. CPU-конверсия добавляет ~28% одного ядра на 3 fps
  при 640×640 (для целевых 640×384 дешевле). Загрузка модели 3 МиБ на стенде
  не проходит по свободному месту `/userdata` (~1.5 МиБ).
- `divinus`: YOLOv5n NPU analytics. The RKNN runtime is loaded with
  `dlopen("librknnmrt.so")` (`hal/plus/rk_rknn.h`, a copy of the 1.6.0
  `rknn_api.h`), and NV12→RGB888 conversion runs on the CPU
  (`yolo_convert_nv12_rgb`): librga is unusable on this image, the legacy
  `c_RkRgaBlit` segfaults and im2d `importbuffer_fd` imports neither MMZ nor
  dma-heap descriptors; the same RGA operation on heap memory also hung the
  board. New modules: `analytics/yolo.c` (capture thread,
  pre-roll ring, event assembly, sender), `analytics/yolo_postprocess.c`
  (three-branch decode and NMS), `analytics/models.c` (`/userdata/models`,
  atomic upload, activation with rollback). The `[yolo]` section (`enable`,
  `source`, `model`, `input_width/height`, `fps`, `conf_threshold`,
  `nms_threshold`, `classes`, `pre_seconds`, `post_seconds`, `max_frames`) is
  parsed and dumped; the API adds `GET/POST /api/yolo`, `GET /api/models`,
  `POST /api/models`, `/api/models/activate`, `/api/models/delete`; the WebUI
  adds the "Object detection (NPU)" and "Models" cards to Events. An event is
  sent as N `PUT` requests carrying `X-Event-Id`, `X-Frame-Index`,
  `X-Frame-Total`, `X-Frame-Ts`, `X-Event-Cause`, `X-Model`;
  `http_post.host` now accepts `host:port`. The `yolo` gate in `gating.c`
  checks the runtime, the model file and VPSS channel 3; RGA is not used
  (pre-processing runs on the CPU). `yolo.source` defaults to `vpss3`, and the
  sanitiser switches to `vi0` when IVS or MJPEG owns the channel. M0 bench measurements: `librknnmrt.so` is **1.6.0** (not
  1.5.2), driver 0.9.2 is compatible, `/proc/rknpu/load` is 77% on YOLOv5n
  640×640 at ~9 fps with `MemAvailable` down 8.7 MB; enabling VI selfpath
  (chn1) hung this board, so `source: vi1` is blocked.
  Verified on the bench (`source: vpss3`, YOLOv5n 640×640, 3 fps): detection
  runs, NPU 28–33%, an event delivers 6 JPEGs at 640×640 with the
  `X-Event-*` headers from a model in `/tmp`; `api_check.py` is 78/78 and
  RTSP/OSD captured 20 frames without regressions; model upload, activation,
  deletion and the `rknn_init` rollback were exercised. The CPU conversion
  costs about 28% of one core at 3 fps for 640×640 (cheaper at the target
  640×384). A 3 MiB model upload does not fit the ~1.5 MiB free in
  `/userdata` on this bench.


### Изменено

- `FEATURES.md` переработан: вместо разнородных списков и прозы — короткий
  паспорт платформы и одна сводная таблица функционала с колонками «Группа»,
  «Функционал», «Описание», «Управление (API, секция YAML)», «Что добавлено
  (OKO vs вендорский V1.4)» и «Проверено и работает». English-раздел сжат до
  зеркальной таблицы `Feature | What was added | Verified & works`; ограничения
  привязаны к строкам таблицы или вынесены в «Примечания». Строка `FEATURES.md`
  в карте документации `AGENTS.md` приведена к новой структуре.
- `FEATURES.md` reworked: the mixed lists and prose are replaced by a short
  platform passport and one consolidated feature table with `Group`, `Feature`,
  `Description`, `Control (API, YAML section)`, `What was added (OKO vs vendor
  V1.4)` and `Verified & works` columns. The English section is reduced to a
  mirrored `Feature | What was added | Verified & works` table; limitations are
  attached to table rows or collected under "Notes". The `FEATURES.md` row in
  the `AGENTS.md` documentation map now matches the new structure.
- `project/app/divinus/source/src/record.c`: запись разрешена только ниже
  реально смонтированного `/mnt/sdcard`; `/oem`, `/userdata`, `/tmp` и `/`
  отвергаются. Добавлен `storageBusy`: во время формата/размонтирования новый
  сегмент не открывается. Раз в секунду и перед ротацией проверяется
  монтирование: при извлечении карты файл закрывается, запись останавливается,
  NAND не используется. `start=1` при отказе возвращает `blocked` с причиной.
- `project/app/divinus/source/src/record.c`: recording is allowed only below a
  really mounted `/mnt/sdcard`; `/oem`, `/userdata`, `/tmp` and `/` are refused.
  `storageBusy` prevents a new segment from opening during format/unmount. The
  mount is checked once per second and before rotation: if the card disappears,
  the file is closed, recording stops and SPI NAND is never used. A failed
  `start=1` now returns `blocked` with a reason.
- SC230AI DTS: новый `rv1106g-luckfox-pico-pro-max-sc230ai.dts` (копия Pro Max
  DTS) выключает неиспользуемые `&sc3336` и `&mis5001`; активный BoardConfig
  использует этот файл. Общий вендорский dtsi и DTS других плат не изменены.
- SC230AI DTS: the new `rv1106g-luckfox-pico-pro-max-sc230ai.dts` (a copy of the
  Pro Max DTS) disables the unused `&sc3336` and `&mis5001`; the active board
  config points at this file. The shared vendor dtsi and other board DTS files
  are unchanged.
- `rv1106-sc230ai.config`: явно выключены `CONFIG_VIDEO_SC3336`,
  `CONFIG_VIDEO_IMX415`, `CONFIG_VIDEO_MIS5001`, `CONFIG_VIDEO_MIA1321`;
  собирается только `CONFIG_VIDEO_SC230AI=m`.
- `rv1106-sc230ai.config`: `CONFIG_VIDEO_SC3336`, `CONFIG_VIDEO_IMX415`,
  `CONFIG_VIDEO_MIS5001` and `CONFIG_VIDEO_MIA1321` are explicitly disabled;
  only `CONFIG_VIDEO_SC230AI=m` is built.
- `project/app/divinus/source/src/region.c`: буфер локального `@file`-провайдера
  текста OSD увеличен с 80 до 256 байт (и `region_file_prev`), чтобы строка
  телеметрии могла содержать SHT21/STM32/INA3221/LTE/WiFi; поведение для
  коротких строк не меняется.
- `project/app/divinus/source/src/region.c`: the local OSD `@file` text provider
  buffer is increased from 80 to 256 bytes (including `region_file_prev`), so the
  telemetry line can carry SHT21/STM32/INA3221/LTE/WiFi; short strings behave as
  before.
- `sc230ai`: режим 1920×1080 переведён с 25 на **30 fps** (`VTS = 0x0465`,
  HTS остаётся 2200, MIPI link 371.25 Мбит/с не менялся). Проверено на DUT:
  `v4l2-ctl` даёт стабильные **30.01–30.02 fps** на 300 кадрах, ошибок
  CSI/MIPI нет. Модуль собран отдельно и загружен через
  `/oem/usr/ko/sc230ai.ko` без полной перепрошивки.
- `sc230ai`: в проект перенесена запись internal sync master/slave регистров
  при старте и остановке потока (из upstream 6.1 / X5): `0x300a`, `0x3032`,
  `0x3222`, `0x3224`, `0x3614`. При `NO_SYNC` поведение не меняется; проверены
  загрузка модуля, 30 fps и OSD.
- `sc230ai`: ported internal sync master/slave register writes at stream
  start/stop from upstream 6.1 / X5: `0x300a`, `0x3032`, `0x3222`, `0x3224`,
  `0x3614`. With `NO_SYNC` behavior is unchanged; module load, 30 fps and OSD
  were verified.
- `sc230ai`: добавлен экспериментальный **HDR_X2** (SHDR/DCG, 2 lane,
  742.5 Мбит/с, 1920×1080@30): таблица из SigmaStar, link frequency
  371.25 МГц, VTS 2250, запись long/short exposure и gain в `set_hdrae()`.
  По умолчанию не включается (`rockchip,camera-hdr-mode = <1>`); HDR IQ для
  SC230AI на RV1106 не проверялся. Linear 30 fps работает без регрессий.
- `sc230ai`: added experimental **HDR_X2** (SHDR/DCG, 2 lanes,
  742.5 Mbps, 1920×1080@30): SigmaStar register table, 371.25 MHz link
  frequency, VTS 2250, and long/short exposure + gain writes in
  `set_hdrae()`. Disabled by default (`rockchip,camera-hdr-mode = <1>`);
  SC230AI HDR IQ on RV1106 has not been validated. Linear 30 fps is unaffected.
- `divinus`: основной поток (`mp4`) и `mjpeg` переведены на **30 fps**
  (`fps: 30`), битрейт `mp4` остаётся 4096 кбит/с. На DUT измерено
  **30.08 fps** за 20 с и ≈4.73 Мбит/с; OSD работает.
- `sc230ai`: the 1920×1080 mode is switched from 25 to **30 fps**
  (`VTS = 0x0465`, HTS stays 2200, MIPI link 371.25 Mbps unchanged). Verified
  on the DUT: `v4l2-ctl` reports stable **30.01–30.02 fps** over 300 frames with
  no CSI/MIPI errors. The module was built separately and loaded through
  `/oem/usr/ko/sc230ai.ko` without a full reflash.
- `divinus`: the main stream (`mp4`) and `mjpeg` are switched to **30 fps**
  (`fps: 30`); the `mp4` bitrate remains 4096 kbps. Measured on the DUT:
  **30.08 fps** over 20 s and ≈4.73 Mbps; OSD works.
- OSD: добавлен чёрный контур текста — `regN_outl: 0x8000`,
  `regN_thick: 1.0` для всех регионов. Конфиг применён на DUT и
  сохранён в `/userdata/divinus.yaml`; контур проверен по кадрам RTSP.
- OSD: added a black text outline — `regN_outl: 0x8000`, `regN_thick: 1.0`
  for all regions. Applied on the DUT and persisted in
  `/userdata/divinus.yaml`; verified from RTSP frames.
- OSD: добавлен `reg4` в левом нижнем углу — реальные загрузка CPU,
  процент свободной памяти (`MemAvailable/MemTotal`) и температура
  `soc-thermal`; файл `/tmp/osd-sys.txt`, обновление раз в 5 с.
- OSD: added `reg4` in the bottom-left corner — real CPU load, free memory
  percentage (`MemAvailable/MemTotal`) and `soc-thermal` temperature; source
  `/tmp/osd-sys.txt`, updated every 5 s.
- `divinus`: битрейт основного RTSP-потока (`mp4.bitrate`, H.265 CBR)
  увеличен вдвое — `2048` → `4096` кбит/с; измеренный поток вырос
  с ≈2.28 до ≈4.5–4.9 Мбит/с. На DUT значение применено и сохранено в
  `/userdata/divinus.yaml`.
- `divinus`: main RTSP stream bitrate (`mp4.bitrate`, H.265 CBR) doubled —
  `2048` → `4096` kbps; the measured stream grew from ≈2.28 to ≈4.5–4.9 Mbps.
  Applied on the DUT and persisted in `/userdata/divinus.yaml`.
- OSD: размер шрифта всех регионов — `24.0` px; время перенесено в правый
  нижний угол, GPS — в правый верхний, телеметрия `T/BAT/LTE/WiFi` разбита на
  четыре строки в левом верхнем углу, сообщения — под ней в `reg3`. Цвет —
  ярко-зелёный `regN_color: 0x07e0` (RGB565). `S20oko-osd` мигрирует старый
  `/userdata/divinus.yaml` (без `reg0_size`/`reg0_color`/`reg3_text`) с
  сохранением первого бэкапа `/userdata/divinus.yaml.bak`.
- OSD: font size for all regions is `24.0` px; time moved to bottom-right, GPS
  to top-right, `T/BAT/LTE/WiFi` split into four lines in the top-left, and
  messages placed below them in `reg3`. Color is bright green
  `regN_color: 0x07e0` (RGB565). `S20oko-osd` migrates an old
  `/userdata/divinus.yaml` (without `reg0_size`/`reg0_color`/`reg3_text`) and
  keeps the first backup `/userdata/divinus.yaml.bak`.
- `divinus` RGN (`rk_hal.c`): в конверсии BGRA5551 выставляются оба бита
  альфы (бит 0 и бит 15); сборки librockit для RV1106 различаются, без этого
  не белый цвет OSD (зелёный/красный) рендерится полностью прозрачным.
- `divinus` RGN (`rk_hal.c`): the BGRA5551 conversion now sets both alpha bits
  (bit 0 and bit 15); RV1106 librockit builds differ, and otherwise a non-white
  OSD color (green/red) is rendered fully transparent.
- Активная конфигурация переключена на
  `project/cfg/BoardConfig_IPC/BoardConfig-SPI_NAND-Buildroot-RV1106_Luckfox_Pico_Pro_Max-SC230AI.mk`:
  SPI NAND, Buildroot, SC230AI, фрагмент ядра только `rv1106-sc230ai.config`.
  Сборка, прошивка и проверки выполняются в автономном режиме, без TFTP/NFS.
- `Dockerfile`, `docker-entrypoint.sh`, `docker-compose.yml`, `start.sh`: убраны
  TFTP-сервер, пакеты `tftpd-hpa`/`atftp` и соответствующие сообщения; контейнер
  больше не запускает сетевых сервисов.
- `AGENTS.md`, `README_RU.md`, `TESTBENCH.md`, `INIT_ORDER.md`, `STRUCTURE.md`,
  `bench/README.md`, `.kilo/command/bench.md`, `.kilo/command/docs-check.md`,
  `.kilo/skills/oko-docs/SKILL.md`: netboot удалён из описания стенда, прошивки,
  диагностики и утилит; основной режим — SPI NAND.
- После перехода на автономную загрузку DUT получает адрес по DHCP; фактический
  адрес на `2026-11-01` — `192.168.52.65`. `TESTBENCH.md` §5 и примеры в
  `README_RU.md` / `bench/README.md` / `/bench` обновлены; `192.168.52.150`
  остаётся резервным адресом для DHCP-резервирования.
- Active configuration switched to
  `project/cfg/BoardConfig_IPC/BoardConfig-SPI_NAND-Buildroot-RV1106_Luckfox_Pico_Pro_Max-SC230AI.mk`:
  SPI NAND, Buildroot, SC230AI, kernel fragment `rv1106-sc230ai.config` only.
  Build, flashing and checks run standalone without TFTP/NFS.
- After the switch to standalone boot the DUT obtains its address via DHCP; the
  actual address on `2026-11-01` is `192.168.52.65`. `TESTBENCH.md` §5 and the
  examples in `README_RU.md` / `bench/README.md` / `/bench` were updated;
  `192.168.52.150` remains a reserved address for a DHCP reservation.
- `Dockerfile`, `docker-entrypoint.sh`, `docker-compose.yml`, `start.sh`: removed
  the TFTP server, the `tftpd-hpa`/`atftp` packages and related messages; the
  container no longer starts network services.
- `AGENTS.md`, `README_RU.md`, `TESTBENCH.md`, `INIT_ORDER.md`, `STRUCTURE.md`,
  `bench/README.md`, `.kilo/command/bench.md`, `.kilo/command/docs-check.md`,
  `.kilo/skills/oko-docs/SKILL.md`: netboot removed from the bench, flashing,
  diagnostics and tooling docs; SPI NAND is the primary mode.
- Поведение запуска стримера: `divinus` стартует по умолчанию — маркер
  `/userdata/divinus.enable` создаёт `S20oko-osd`; штатный `rkipc` запускается
  только при opt-out `/userdata/rkipc.enable`. Логика `RkLunch.sh` не менялась.
- `S20oko-osd` перезаписывает `/userdata/divinus.yaml` фабричным, если в нём нет
  `reg0_text`/`reg0_size`/`reg0_color` (старый или ручной конфиг); первый прежний
  файл сохраняется как `divinus.yaml.bak`, ручные правки такого файла не
  переживают перезагрузку.
- `INIT_ORDER.md`, `TESTBENCH.md`, `STRUCTURE.md`, `README_RU.md`: divinus по
  умолчанию, строка `S20oko-osd` в порядке init, OSD-чек-лист и файлы оверлея.
- Streamer startup: `divinus` now starts by default — `S20oko-osd` creates
  `/userdata/divinus.enable`; the stock `rkipc` runs only when the
  `/userdata/rkipc.enable` opt-out marker exists. `RkLunch.sh` itself is
  unchanged.
- `S20oko-osd` replaces `/userdata/divinus.yaml` with the factory file when it
  lacks `reg0_text` (old or hand-edited config); such a file does not survive a
  reboot.
- `INIT_ORDER.md`, `TESTBENCH.md`, `STRUCTURE.md`, `README_RU.md`: default
  divinus startup, the `S20oko-osd` init step, the OSD checklist and the new
  overlay files.
- `STRUCTURE.md`: добавлен пакет `project/app/divinus/` (вендорские исходники,
  фабричный конфиг, Makefile) и уточнён состав `project/app/`.
- `INIT_ORDER.md`: ветка `divinus.enable` в `post_chk` и зависимости `divinus`
  (общий rockit/ISP-конвейер с `rkipc`, `dlopen` вендорных библиотек,
  патч ABI `mmap` в `compat.c`).
- `TESTBENCH.md`: чек-лист divinus (`/api/status`, `/image.jpg`, RTSP-корень),
  диагностика сбоев и особенности портов/конфига.
- `STRUCTURE.md`: added the `project/app/divinus/` package (vendored sources,
  factory config, Makefile) and clarified the `project/app/` contents.
- `INIT_ORDER.md`: documented the `divinus.enable` branch in `post_chk` and the
  `divinus` dependencies (shared rockit/ISP pipeline with `rkipc`, `dlopen` of
  vendor libraries, the `compat.c` `mmap` ABI patch).
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
- `TESTBENCH.md`: новый режим «C. Покомпонентная прошивка изменённых разделов» —
  маппинг исходников на разделы SPI NAND (`oem`, `rootfs`, `userdata`, `boot`),
  команды `./rkflash.sh oem|rootfs|userdata|rd` из Maskrom, правило «полный
  `update.img` только при первичной прошивке или смене таблицы разделов, loader,
  U-Boot env», сверка хешей образов с `.flashed.sha256` и альтернатива сырыми
  `mtd erase/write` из `sd_update.txt`.
- `README_RU.md`: в быстром старте прошивка полным образом — `./rkflash.sh update`
  (без аргумента скрипт ничего не прошивает), добавлена ссылка на покомпонентный
  режим; команда `/bench` дополнена режимами прошивки и `divinus` в чек-листе.
- `TESTBENCH.md`: new "C. Per-partition flashing of changed sections" mode — a
  source-to-SPI-NAND-partition map (`oem`, `rootfs`, `userdata`, `boot`), the
  `./rkflash.sh oem|rootfs|userdata|rd` commands from Maskrom, the rule that a full
  `update.img` is only for the first flash or a partition table, loader, or U-Boot
  env change, image hash comparison against `.flashed.sha256`, and the raw
  `mtd erase/write` alternative from `sd_update.txt`.
- `README_RU.md`: the quick start now flashes the full image with
  `./rkflash.sh update` (the script flashes nothing without an argument) and links
  to the per-partition mode; the `/bench` command now lists the flashing modes and
  `divinus` in its checklist.
- `TESTBENCH.md`: зафиксировано покрытие шрифта `simsun_en` — ASCII `0x21–0x7e`
  и несколько CJK-иероглифов, кириллицы нет; текст OSD пишется латиницей.
- `TESTBENCH.md`: documented the `simsun_en` font coverage — ASCII `0x21–0x7e`
  plus a few CJK characters, no Cyrillic; OSD text must be Latin.
- Логи netboot и OSD-проверок перенесены из `/tmp/kilo` в `output/bench/`
  (`netboot-<ts>.log`, `osd-<ts>-*.png`); `netboot-catch.py` читает
  `OKO_NETBOOT_LOG` и создаёт каталог сам. Обновлены `TESTBENCH.md` (§4, §7B,
  §8, §10), `README_RU.md`, `STRUCTURE.md` (папка `bench/`, `output/bench/`),
  команда `/bench` и правило в `AGENTS.md` §6.
- Netboot and OSD-check artifacts moved from `/tmp/kilo` to `output/bench/`
  (`netboot-<ts>.log`, `osd-<ts>-*.png`); `netboot-catch.py` honours
  `OKO_NETBOOT_LOG` and creates the directory itself. Updated `TESTBENCH.md`
  (§4, §7B, §8, §10), `README_RU.md`, `STRUCTURE.md` (`bench/`, `output/bench/`),
  the `/bench` command and the rule in `AGENTS.md` §6.
- WebUI: запись настроек переведена на `POST application/x-www-form-urlencoded`
  (сервер подставляет тело в `req->query`, обработчики не менялись); `GET`
  оставлен для совместимости и помечен устаревшим в `endpoints.md`. Кнопка
  `Apply` активна только при отличиях от последнего прочитанного состояния,
  ответ сервера разбирается вместо безусловного «success».
- WebUI: writes now use `POST application/x-www-form-urlencoded` (the server
  maps the body to `req->query`, so the handlers are unchanged); `GET` is kept
  for compatibility and marked deprecated in `endpoints.md`. `Apply` is enabled
  only when a field differs from the last read state, and the server response is
  parsed instead of assuming success.
- `/api/osd/{id}`: `color` и `outl` отдаются как `#RRGGBB` с ведущими нулями,
  `text`/`img`/`font` экранируются в JSON; пустые `text`/`img` очищают значение;
  PNG-загрузка прописывает `img=/tmp/osd{id}.png`, поэтому картинка реально
  отображается.
- `/api/osd/{id}`: `color` and `outl` are returned as zero-padded `#RRGGBB`, and
  `text`/`img`/`font` are JSON-escaped; empty `text`/`img` clear the value; a PNG
  upload sets `img=/tmp/osd{id}.png`, so the image is actually displayed.
- `divinus.yaml` (фабричный и рабочий): в `system` добавлены `ntp_enable` и
  `ntp_server`; существующие файлы без этих ключей читаются со значениями по
  умолчанию.
- `divinus.yaml` (factory and working copies): `system` gains `ntp_enable` and
  `ntp_server`; existing files without the keys parse with defaults.
- `unescape_uri()` вызывается после разделения `key=value`, поэтому `%3D` в
  значении параметра больше не влияет на разбор, а `NULL` для параметра без
  значения безопасен.
- `unescape_uri()` now runs after the `key=value` split, so an encoded `%3D` in
  a value no longer affects parsing, and `NULL` for a parameter without a value
  is safe.
- `divinus`: OSD на RV1106 обновляется через canvas
  (`RK_MPI_RGN_GetCanvasInfo`/`RK_MPI_RGN_UpdateCanvas`): растр пишется прямо
  в память региона, без `calloc` и копии на каждый кадр. Если символы
  отсутствуют, `GetCanvasInfo` вернул ошибку или размер/stride canvas не
  совпал, остаётся прежний путь `RK_MPI_RGN_SetBitMap` с однократным
  предупреждением на handle. Позиция и `fgAlpha` меняются через
  `RK_MPI_RGN_SetDisplayAttr` вместо detach/attach, поэтому `opal` из
  `/api/osd/<id>` применяется и при неизменной позиции. Упаковка
  `ARGB1555 → BGRA5551` вынесена в `hal/plus/rk_rgn_pack.c`, добавлен хостовый
  тест `tests/rk_rgn_pack_test.c`, `tests/bmp_align_test.c` переведён на общий
  helper. `u32CanvasNum` остаётся равным 1.
- `divinus`: the RV1106 OSD is updated through the canvas interface
  (`RK_MPI_RGN_GetCanvasInfo`/`RK_MPI_RGN_UpdateCanvas`) — the raster is
  written directly into the region memory, without a `calloc` and a copy per
  frame. If the symbols are missing, `GetCanvasInfo` fails or the canvas
  size/stride does not match, the previous `RK_MPI_RGN_SetBitMap` path is used
  with a single warning per handle. Position and `fgAlpha` are changed with
  `RK_MPI_RGN_SetDisplayAttr` instead of detach/attach, so `opal` from
  `/api/osd/<id>` is applied even when the position is unchanged. The
  `ARGB1555 → BGRA5551` packing moved to `hal/plus/rk_rgn_pack.c`, a host test
  `tests/rk_rgn_pack_test.c` was added, and `tests/bmp_align_test.c` now uses
  the shared helper. `u32CanvasNum` stays at 1.

- Разметка SPI NAND: `userdata` расширен с 10 до 40 МБ за счёт `rootfs`
  (198→168 МБ), сумма `userdata`+`rootfs` сохранена (208 МБ). После прошивки
  полным `update.img` том `ubi5_0` — 29.1 МиБ (свободно ~28 МиБ), загрузка
  модели 3 МиБ через `/api/models` проверена; `rootfs` занимает ~54 МиБ из
  168 МБ. Обновлены `BoardConfig`, `TESTBENCH.md` (LBA/размеры),
  `INIT_ORDER.md` и `FEATURES.md`. Если `reboot loader` не переводит плату в
  loader-режим, U-Boot на UART останавливается `<Ctrl+C>` и команда
  `download` уводит её в `Maskrom`.
- SPI NAND layout: `userdata` grew from 10 to 40 MB by shrinking `rootfs`
  (198→168 MB); the combined size stays 208 MB. After flashing the full
  `update.img` the `ubi5_0` volume is 29.1 MiB (~28 MiB free), and a 3 MiB
  model upload through `/api/models` was verified; `rootfs` uses ~54 MiB of
  its 168 MB. `BoardConfig`, `TESTBENCH.md` (LBA/sizes), `INIT_ORDER.md` and
  `FEATURES.md` were updated. When `reboot loader` does not enter loader mode,
  stopping U-Boot on UART with `<Ctrl+C>` and issuing `download` switches the
  board to `Maskrom`.

- `bench/ui_check.py`: добавлена браузерная проверка раздела Events — список
  источников YOLO не содержит `vi1`, модель загружается через карточку
  Models, ошибка `rknn_init` показывается в тосте, удаление убирает строку.
  Прогон выполнен в headless Chromium (без root `libasound.so.2` распакован
  из `libasound2t64`), итог — `UI check passed`, скриншоты в
  `output/bench/ui/`; способ запуска описан в `bench/README.md`.
- `bench/ui_check.py`: added a browser check of the Events section — the YOLO
  source list has no `vi1`, a model is uploaded from the Models card, the
  `rknn_init` error appears in a toast, and deleting the row removes it. The
  run used headless Chromium (without root, `libasound.so.2` was unpacked from
  `libasound2t64`) and ended with `UI check passed`; screenshots are in
  `output/bench/ui/`, and the command is documented in `bench/README.md`.

- Зафиксирована проверка AIISP + YOLO на `vpss3` (демо-модель 640×640,
  3 fps): AIISP on/off не роняет детекцию, main 30.2–32.8 fps, NPU 28%,
  `librkpostisp.so` загружен, пропусков в `/live/0` не видно; результаты — в
  `VERIFIED_FUNCTIONS.md`. Ночная точность и CMA не измерялись (нет
  цели/разметки), целевая модель 640×384 ещё не собрана.
- Recorded the AIISP + YOLO check on `vpss3` (640×640 demo model, 3 fps):
  toggling AIISP does not break detection, main runs at 30.2–32.8 fps with 28%
  NPU, `librkpostisp.so` is loaded and `/live/0` shows no gaps; results are in
  `VERIFIED_FUNCTIONS.md`. Night accuracy and CMA were not measured (no
  target/labels), and the target 640×384 model is not built yet.

### Удалено

- Из oem активной платы удалены `sample_*` и `simple_*` (включая прежние
  стресс/AVS-демо); диагностические `rk_*_test`/`rk_mpi_*_test`, `rkipc`,
  `rkaiq_*`, `divinus` и `oko-*` сохранены.
- `sample_*` and `simple_*` (including the old stress/AVS demos) are removed
  from the active board oem; diagnostic `rk_*_test`/`rk_mpi_*_test`, `rkipc`,
  `rkaiq_*`, `divinus` and `oko-*` are kept.
- Из oem удалены сенсорные модули и IQ других плат (`sc3336`, `mis5001`,
  `imx415`, `mia1321` и другие сенсорные `.ko`); остаётся только `sc230ai.ko`
  и `sc230ai_CMK-OT2115-PC1_30IRC-F16.json`. Вендорские исходники и DTS других
  плат не тронуты.
- Sensor modules and IQ files of the other boards (`sc3336`, `mis5001`,
  `imx415`, `mia1321` and other sensor `.ko` files) are removed from oem; only
  `sc230ai.ko` and `sc230ai_CMK-OT2115-PC1_30IRC-F16.json` remain. Vendor
  sources and DTS files of the other boards are untouched.
- Netboot-режим: файлы `BoardConfig-SPI_NAND-Buildroot-RV1106_Luckfox_Pico_Pro_Max-NETBOOT.mk`,
  `BoardConfig-SPI_NAND-Buildroot-RV1106_Luckfox_Pico_Pro_Max-NETBOOT-SC230AI.mk`,
  фрагмент ядра `sysdrv/source/kernel/arch/arm/configs/rv1106-netboot.config`,
  утилиты `bench/netboot.py` и `netboot-catch.py`.
- NFS-root в `S20linkmount` (`project/build.sh`): ветка монтирования
  `oem`/`userdata` с хоста и сообщения `netboot: …`; разделы всегда берутся из
  SPI NAND (ubifs).
- Netboot-ветка в `RkLunch.sh`: статическая сеть из `ip=` больше не
  поддерживается, `network_init` всегда настраивает MAC и запускает DHCP.
- Netboot mode: the
  `BoardConfig-SPI_NAND-Buildroot-RV1106_Luckfox_Pico_Pro_Max-NETBOOT.mk` and
  `BoardConfig-SPI_NAND-Buildroot-RV1106_Luckfox_Pico_Pro_Max-NETBOOT-SC230AI.mk`
  files, the `sysdrv/source/kernel/arch/arm/configs/rv1106-netboot.config`
  kernel fragment, and the `bench/netboot.py` / `netboot-catch.py` tools.
- NFS root in `S20linkmount` (`project/build.sh`): the host-side
  `oem`/`userdata` mount branch and `netboot: …` messages; partitions always come
  from SPI NAND (ubifs).
- Netboot branch in `RkLunch.sh`: static networking from `ip=` is no longer
  supported; `network_init` always configures the MAC and starts DHCP.
- `divinus`: приведены к вендорскому ABI структуры HAL, которые передаются в
  librockit: `VIDEO_FRAME_S` (`pVirAddr` — 2 элемента, а не 3) и хвост
  `VI_CHN_ATTR_S` (`stShareBufChn` — три `int`, а не один). Прежние размеры
  приводили к чтению за границей структуры при `RK_MPI_VI_SetChnAttr` и
  сдвигу `u64PTS`. Проверено `_Static_assert` по заголовкам SDK для
  VI/VPSS/VENC/VIDEO_FRAME.
- `divinus`: HAL structures passed to librockit now match the vendor ABI:
  `VIDEO_FRAME_S` (`pVirAddr` holds two elements, not three) and the
  `VI_CHN_ATTR_S` tail (`stShareBufChn` is three `int`s, not one). The old
  sizes made `RK_MPI_VI_SetChnAttr` read past the object and shifted
  `u64PTS`. Verified with `_Static_assert` against the SDK headers for
  VI/VPSS/VENC/VIDEO_FRAME.


### Исправлено

- `divinus`: `yolo.source=vi1` закрыт на всех уровнях — `gating.c` блокирует
  его с причиной, `GET`/`POST /api/yolo?source=vi1` и неизвестные значения
  возвращают HTTP 400 `{"error":"invalid"}`, из WebUI опция удалена, а
  стартовый санитайзер переводит сохранённый `vi1` в `vpss3`. В zero-copy
  входе учитывается `w_stride` (строки пишутся с шагом тензора), при
  активации проверяются 4D-NHWC выходы и одинаковая размерность каналов всех
  ветвей, атрибуты входа/выходов пишутся в лог. Хостовый
  `yolo_postprocess_test` покрывает 80 классов (C = 255, `C % 16 != 0`) и
  проверяет, что каналы NHWC не выравниваются до 16; `bench/api_check.py`
  проверяет отказ для `vi1` и неизвестного значения.
- `divinus`: `yolo.source=vi1` is closed at every level — `gating.c` blocks it
  with a reason, `GET`/`POST /api/yolo?source=vi1` and unknown values return
  HTTP 400 `{"error":"invalid"}`, the WebUI option is removed, and the startup
  sanitiser turns a saved `vi1` into `vpss3`. The zero-copy input honours
  `w_stride` (rows are written at the tensor stride), activation validates
  4D-NHWC outputs and identical channel counts in all branches, and the input
  and output attributes are logged. The host `yolo_postprocess_test` covers 80
  classes (C = 255, `C % 16 != 0`) and verifies that NHWC channels are not
  padded to 16; `bench/api_check.py` checks the rejection of `vi1` and of an
  unknown value.
- DHCP: причина смены IP — MAC хранился в `/data/ethaddr.txt` (раздел
  `/userdata`), а `dhcpcd` использовал DUID из `/var/db/dhcpcd`; оба
  сбрасываются при полной прошивке. Дополнительно на `eth0` работали два
  клиента (`S41dhcpcd` и `udhcpc` из `RkLunch.sh`). Теперь MAC выводится из
  `/proc/device-tree/serial-number` и хранится в env (`oko_mac`), DHCP-клиент
  один — `dhcpcd` с client ID по MAC (`clientid`, без `duid`),
  `S41dhcpcd` заменён заглушкой, а `RkLunch.sh` вызывает `oko-net apply`.
  Проверено: после reboot MAC `02:47:8d:05:67:56` и IP `192.168.52.97`
  сохраняются; static `.220` также переживает reboot.
- DHCP: the changing IP was caused by the MAC living in `/data/ethaddr.txt`
  (`/userdata`) and `dhcpcd` using a DUID from `/var/db/dhcpcd`; both are
  recreated by a full firmware update. Two clients also ran on `eth0`
  (`S41dhcpcd` and `udhcpc` from `RkLunch.sh`). Now the MAC is derived from
  `/proc/device-tree/serial-number` and stored in env (`oko_mac`), there is
  one DHCP client (`dhcpcd` with `clientid` by MAC, no `duid`), `S41dhcpcd`
  is a stub, and `RkLunch.sh` calls `oko-net apply`. Verified: after reboot
  MAC `02:47:8d:05:67:56` and IP `192.168.52.97` are preserved; static
  `.220` also survives a reboot.
- WebUI/MP4: HTTP-поток `/video.mp4` больше не отдаёт пустой chunk и не рвёт
  соединение. `send_mp4_to_client()` теперь, как и recorder, режет Annex B
  через `nal_split()`: VPS/SPS/PPS HEVC приходят одним access unit, и старый
  обход `pack->nalu[]` не находил их, поэтому `moov` не строился. Проверено:
  PyAV декодирует 30 кадров hevc 1920x1080 с `http://<DUT>/video.mp4`.
- WebUI/MP4: the `/video.mp4` HTTP stream no longer sends an empty chunk and
  closes. `send_mp4_to_client()` now splits Annex B with `nal_split()` like the
  recorder: HEVC VPS/SPS/PPS arrive in one access unit, and the old
  `pack->nalu[]` walk missed them, so `moov` was never built. Verified: PyAV
  decodes 30 hevc 1920x1080 frames from `http://<DUT>/video.mp4`.
- WebUI: вкладки MP4/MJPEG показывают понятное сообщение, если поток выключен,
  а для MP4 добавлена подсказка про H.265 и MJPEG. MJPEG при
  `mjpeg.enable=true` отдаёт `multipart/x-mixed-replace` (проверено).
- WebUI: the MP4/MJPEG tabs show a clear message when the stream is disabled,
  and the MP4 tab hints at H.265 vs MJPEG. With `mjpeg.enable=true` the MJPEG
  endpoint returns a `multipart/x-mixed-replace` stream (verified).
- microSD/udev: eudev в образе читает только `/lib/udev/rules.d`, поэтому
  правило `60-oko-sdcard.rules` перенесено из `/etc/udev/rules.d` в
  `/lib/udev/rules.d`; теперь `oko-sdcard add/remove` действительно вызывается
  и пишет `/run/oko-sd.state`. Вендорское `61-sd-cards-auto-mount.rules`
  заменено заглушкой: его безусловный remove-`umount` снимал только что
  отформатированную карту на stale-событиях `sfdisk`/`partprobe`; stale-remove
  для ещё существующего `mmcblkX` теперь игнорируется. Проверено: format
  создаёт один FAT32-раздел `OKO`, а unbind/bind `mmc1:aaaa` даёт `present=0`
  → `present=1` и автомонтирование.
- microSD/udev: eudev in this image reads only `/lib/udev/rules.d`, so
  `60-oko-sdcard.rules` moved from `/etc/udev/rules.d` to `/lib/udev/rules.d`;
  `oko-sdcard add/remove` is now actually invoked and writes
  `/run/oko-sd.state`. The vendor `61-sd-cards-auto-mount.rules` is replaced by
  a stub: its unconditional remove-`umount` detached the freshly formatted card
  on stale `sfdisk`/`partprobe` events; stale remove events for an existing
  `mmcblkX` are now ignored. Verified: format creates one FAT32 `OKO` partition
  and unbinding/binding `mmc1:aaaa` changes `present=0` to `present=1` with
  automount.
- `oko-sdcard`: определение карты переведено с `/sys/block/*/removable`
  (в этом 5.10-ядре всегда `0`) на `device/type=SD`; `unmount` снимает все
  наложения `/mnt/sdcard`, включая старые p1+p2. VFAT монтируется с
  `uid=1000,gid=1000,fmask=0133,dmask=0022`.
- `oko-sdcard`: card detection now uses `device/type=SD` because
  `/sys/block/*/removable` is always `0` in this 5.10 kernel; `unmount` drops
  all stacked `/mnt/sdcard` mounts, including legacy p1+p2. VFAT mounts with
  `uid=1000,gid=1000,fmask=0133,dmask=0022`.
- Сборка: выбранная AIISP-модель копируется в oem **до** `build_mkimg oem`;
  раньше `cp` из post-clean выполнялся после создания `oem.img`, и
  `rkmodel_1080_1920.aiisp` отсутствовал в прошивке.
- Build: the selected AIISP model is copied into oem **before**
  `build_mkimg oem`; previously the post-clean `cp` ran after `oem.img` was
  created, so `rkmodel_1080_1920.aiisp` was missing from the firmware.
- `divinus/record`: H.265-запись больше не создаёт пустые `.mp4`. VENC-пак
  для HEVC приходит одним NAL-элементом на весь access unit; `record.c` теперь
  сам режет Annex B через `nal_split` и передаёт VPS/SPS/PPS/IDR/слайсы в
  muxer. Проверено: 8-секундный клип декодируется как 222 кадра 1920x1080.
  Заодно исправлено удаление старых сегментов: `recordPath` больше не
  исключается из квоты, когда файл уже закрыт.
- `divinus/record`: H.265 recording no longer produces empty `.mp4` files. The
  VENC pack for HEVC arrives as one NAL entry for the whole access unit, so
  `record.c` now splits Annex B with `nal_split` and passes
  VPS/SPS/PPS/IDR/slices to the muxer. Verified: an 8 s clip decodes as 222
  frames of 1920x1080. The quota prune was also fixed: a closed `recordPath`
  is no longer skipped, so old segments are actually removed.
- `divinus/rtsp`: `PLAY` больше не вызывает блокирующий геттер канала под уже
  взятым мьютексом RTSP. Дедлок останавливал видео-поток, из-за чего
  `RK_MPI_RGN_SetBitMap` начинал возвращать `0xfffffffe` и OSD пропадал.
  Неизвестный или выключенный `/live/<n>` теперь возвращает 404, а не main.
- `divinus/rtsp`: `PLAY` no longer calls the locking channel getter while the
  RTSP mutex is already held. The deadlock stalled the video thread, which made
  `RK_MPI_RGN_SetBitMap` return `0xfffffffe` and dropped the OSD. An unknown or
  disabled `/live/<n>` now returns 404 instead of falling back to main.
- `divinus`: второй поток при выключении делает soft-stop и оставляет канал
  выделенным (повторное включение мгновенное); создание канала и смена
  параметров энкодера применяются перезапуском в безопасном порядке
  `main → sub → JPEG`. Это устраняет разрушение OSD на RV1106.
- `divinus`: disabling the sub stream is now a soft stop that keeps the channel
  allocated (re-enabling is immediate); creating the channel or changing
  encoder parameters is applied by a restart in the safe
  `main -> sub -> JPEG` order. This removes the RV1106 OSD corruption.
- `divinus/hal/config.h`: `MAX_SECTIONS` увеличено с 16 до 40. Новые секции
  (eptz/rotate/venc/substream/aiisp/ivs/iq/hdr) выводили файл за лимит
  `find_sections()`, и после `app_config_save()` конфиг читался частично
  (`Section 'osd' not found`, `Configuration error code: 1`).
- `divinus/hal/config.h`: `MAX_SECTIONS` raised from 16 to 40. The new
  sections (eptz/rotate/venc/substream/aiisp/ivs/iq/hdr) exceeded
  `find_sections()`'s table, so after `app_config_save()` the configuration
  was read partially (`Section 'osd' not found`, `Configuration error code: 1`).
- `divinus/app_config.c`: сохранение OSD пишет `regX_*` с отступом в два
  пробела, как фабричный YAML; раньше отступ 4 пробела ломал разбор `osd`
  после рестарта и HDR-перезапуск терял регионы.
- `divinus/app_config.c`: OSD save emits `regX_*` with the factory two-space
  indentation; the previous four-space indent broke the `osd` section after a
  restart, so the HDR restart lost the overlays.
- `divinus/hal/plus/rk_hal.c`: `rk_video_destroy()` больше не прерывает
  teardown при ошибке `UnBind` — вызывающий код уже отвязал пару, и повторная
  отвязка оставляла VENC/VPSS канал неразрушенным.
- `divinus/hal/plus/rk_hal.c`: `rk_video_destroy()` no longer aborts teardown
  on an `UnBind` error; callers already unbound the pair, and the second
  unbind left the VENC/VPSS channel alive.
- OSD: RGN переведён на один canvas (`region.overlay.canvas = 1`). При двух
  canvas драйвер периодически отдавал кадр с OSD предыдущей секунды — время на
  видео шло не по порядку. Проверено 60 с: кадров со «старым» временем нет.
- OSD: the RGN now uses a single canvas (`region.overlay.canvas = 1`). With two
  canvases the driver periodically emitted a frame with the previous second's
  OSD, so the displayed time was not sequential. Verified for 60 s with no
  stale-time frames.
- `netboot-catch.py`: после появления login-приглашения (или по таймауту 180 с)
  скрипт закрывает `/dev/ttyACM*` и завершается, больше не держит serial и не
  спамит `^C` в консоль; окно ожидания U-Boot остаётся 900 с.
- `docker-entrypoint.sh`: TFTP-корень больше не world-writable (`0755`), сервер
  запускается без `--create` — загрузка файлов на хост отключена; скачивание
  `boot.img` для netboot работает как прежде.
- `netboot-catch.py`: after detecting the login prompt (or after a 180 s timeout)
  the script closes `/dev/ttyACM*` and exits, so it no longer holds the serial
  port or floods the console with `^C`; the U-Boot catch window stays at 900 s.
- `docker-entrypoint.sh`: the TFTP root is no longer world-writable (`0755`) and
  the server runs without `--create`, disabling uploads; downloading `boot.img`
  for netboot keeps working.
- `TESTBENCH.md` §7C, `README_RU.md`, команда `/bench`: исправлена процедура
  покомпонентной прошивки. `rkflash.sh oem/rootfs` (`upgrade_tool di`) в чистом
  Maskrom без загруженного loader отвечает `parameter is invalid`; нужен
  `db download.bin` + `wl <LBA> <размер всего раздела в секторах>` (`oem`
  0x2800/0xF000, `rootfs` 0x16800/0x69000). `wl` с длиной образа оставляет в
  хвосте старые UBI-PEB → `ubi_attach: bad image sequence number` и panic.
- `TESTBENCH.md` §7C, `README_RU.md`, the `/bench` command: fixed the
  per-partition flashing procedure. `rkflash.sh oem/rootfs` (`upgrade_tool di`)
  in bare Maskrom without a loaded loader reports `parameter is invalid`; use
  `db download.bin` plus `wl <LBA> <full partition size in sectors>` (`oem`
  0x2800/0xF000, `rootfs` 0x16800/0x69000). Passing the image length to `wl`
  leaves stale UBI PEBs → `ubi_attach: bad image sequence number` and panic.
- `netboot-catch.py`: `OKO_NETBOOT_LOG` без каталога (например, `netboot.log`)
  больше не падает на `os.makedirs('')` — родительский каталог нормализуется
  в `.`.
- `netboot-catch.py`: `OKO_NETBOOT_LOG` without a directory component (for
  example, `netboot.log`) no longer fails on `os.makedirs('')`; the parent
  directory is normalized to `.`.
- `INIT_ORDER.md`: убрано дублирование условия бутстрапа `divinus.yaml` —
  замена при отсутствии файла или `reg0_text` описана один раз, резервное
  копирование в `post_chk` отмечено отдельно.
- `INIT_ORDER.md`: removed the duplicated `divinus.yaml` bootstrap condition —
  replacement when the file or `reg0_text` is missing is described once, and
  the `post_chk` fallback is called out separately.
- `divinus.yaml`, который не разбирается (например, после ручной правки или
  импорта), больше не оставляет устройство без стримера: `main.c` при ошибке
  разбора один раз восстанавливает `divinus.yaml.bak` и повторяет разбор,
  и только затем завершается. Проверено на стенде: битый `web_port` →
  восстановление, `/api/status` отвечает.
- A `divinus.yaml` that no longer parses (after a manual edit or an import) no
  longer leaves the device without a streamer: on a parse error `main.c`
  restores `divinus.yaml.bak` once and retries, exiting only if that also
  fails. Verified on the bench: a broken `web_port` is rolled back and
  `/api/status` keeps answering.
- `/api/time?ts=`: отметка времени читалась в `short`, из-за чего эпоха
  обрезалась и на устройство выставлялось случайное время; теперь `long` с
  проверкой нижней границы.
- `/api/time?ts=`: the timestamp was read into a `short`, so the epoch was
  truncated and the device got a random time; it is a `long` with a lower-bound
  check now.
- Проверки символов: `compat.c` определяет функцию `__ctype_b`, а `-rdynamic`
  экспортирует её поверх таблицы ctype uClibc, поэтому любой макрос
  `isalnum()`/`isspace()` разыменовывал код как таблицу и ронял процесс
  (`SIGSEGV`). В `escape_url()`, разборе ONVIF и проверке имени NTP-сервера
  заменено на явные ASCII-сравнения.
- Character checks: `compat.c` defines a function named `__ctype_b` and
  `-rdynamic` exports it over the uClibc ctype table, so every
  `isalnum()`/`isspace()` macro dereferenced code as a table and crashed the
  process (`SIGSEGV`). `escape_url()`, the ONVIF parser and the NTP server-name
  check now use explicit ASCII comparisons.
- `send_file()` отдаёт только файлы под `/mnt/sdcard/` и резолвит симлинки
  (`realpath`). Раньше любой несовпавший URI трактовался как абсолютный путь от
  `/`, а MIME-тип был равен самому URI, поэтому `GET //etc/passwd` отдавал файл
  системы. **Ломающее изменение** для сторонних скриптов, которые скачивали
  файлы вне карты; для архива записей используйте `/api/recordings/file`.
- `send_file()` serves only files under `/mnt/sdcard/` and resolves symlinks
  (`realpath`). Previously any unmatched URI was treated as an absolute path
  from `/` and the MIME type was the URI itself, so `GET //etc/passwd` leaked the
  system file. **Breaking change** for third-party scripts that downloaded files
  outside the card; use `/api/recordings/file` for the recording archive.

### Безопасность

- `project/app/divinus/files/divinus.yaml`: фабричный конфиг включает
  аутентификацию web и RTSP (`admin` / `oko-change-me`), `web_auth_skiplocal`
  выключен; пароль — публичный фабричный дефолт, его нужно сменить до подключения
  камеры к недоверенной сети (см. `TESTBENCH.md` §6).
- `project/app/divinus/files/divinus.yaml`: the factory config now enables web and
  RTSP authentication (`admin` / `oko-change-me`) and disables
  `web_auth_skiplocal`; the password is a public factory default and must be
  changed before the camera is connected to an untrusted network (see
  `TESTBENCH.md` §6).
- `POST /api/cmd/import` принимает только `multipart/form-data`, ограничен
  128 КиБ, требует подстроки `system:` и `web_port:` и пишет файл только после
  этих проверок; предыдущий конфиг сохраняется как `.bak` до перезаписи.
- `POST /api/cmd/import` accepts `multipart/form-data` only, is limited to
  128 KiB, requires the `system:` and `web_port:` markers, and writes the file
  only after those checks; the previous config is kept as `.bak` before the
  overwrite.
- Смена пароля WebUI (`/api/web-auth`) требует текущий пароль и выполняется
  только методом `POST`; пароли по-прежнему не читаются ни одним GET-эндпоинтом.
- The WebUI password change (`/api/web-auth`) requires the current password and
  is `POST`-only; passwords are still never returned by any GET endpoint.

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