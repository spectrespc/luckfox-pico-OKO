# Порядок инициализации прошивки

Документ описывает полную цепочку запуска прошивки Luckfox Pico Pro Max (RV1106):
BootROM → U-Boot → ядро Linux → busybox init → init-скрипты → загрузка модулей ядра →
запуск приложения `rkipc`, а также зависимости между разделами, модулями и сервисами.

Активная конфигурация определяется симлинком `.BoardConfig.mk`, который указывает на
`project/cfg/BoardConfig_IPC/BoardConfig-SPI_NAND-Buildroot-RV1106_Luckfox_Pico_Pro_Max-SC230AI.mk`.

Канонический источник порядка — исходные скрипты и сборочные файлы. Каталог
`output/out/` — это артефакт последней сборки; он может устареть и используется только
для сверки фактического набора файлов.

Сборка прошивки в форке OKO выполняется в Docker-контейнере `luckfox_oko_build_env`
(образ `luckfox-builder:22.04`), настроенном в `docker-compose.yml` и запускаемом
через `start.sh` (`docker compose build` + `docker compose up -d`). Команды сборки —
внутри контейнера в `/workspace`; артефакты — в `output/image/`. Быстрый старт и
зависимости — в `README_RU.md`; сборка на хосте без Docker там же описана как
вендорский fallback.

## 1. Активная конфигурация и пути загрузки

| Параметр | Значение | Комментарий |
|---|---|---|
| `RK_CHIP` | `rv1106` | RV1106 (Cortex-A7 + NPU + ISP) |
| `RK_APP_TYPE` | `RKIPC_RV1106` | приложение rkipc |
| `RK_BOOT_MEDIUM` | `spi_nand` | загрузка с SPI NAND |
| `RK_KERNEL_DTS` | `rv1106g-luckfox-pico-pro-max-sc230ai.dts` | device tree активной платы; `&sc3336`/`&mis5001` выключены |
| I2C3-M1 | `okay`, `100000` Гц, `pinctrl-0 = <&i2c3m1_xfer>` | включён в `rv1106g-luckfox-pico-pro-max-sc230ai.dts`; выводы `GPIO1_D2`/`GPIO1_D3`, узел `/dev/i2c-3` |
| `RK_UBOOT_DEFCONFIG` | `luckfox_rv1106_uboot_defconfig` | конфигурация U-Boot |
| `RK_UBOOT_DEFCONFIG_FRAGMENT` | `rk-sfc.config` | поддержка SPI flash controller |
| `RK_KERNEL_DEFCONFIG` | `luckfox_rv1106_linux_defconfig` | базовая конфигурация ядра |
| `RK_KERNEL_DEFCONFIG_FRAGMENT` | `rv1106-sc230ai.config` | `CONFIG_VIDEO_SC230AI=m`; SC3336/IMX415/MIS5001/MIA1321 явно выключены |
| `RK_BOOTARGS_CMA_SIZE` | `66M` | в cmdline попадает как `rk_dma_heap_cma=66M` |
| `RK_CAMERA_SENSOR_IQFILES` | `sc230ai_CMK-OT2115-PC1_30IRC-F16.json` | IQ-файл сенсора |
| `RK_ENABLE_WIFI` | `y` | WiFi-модули собираются и копируются в `/oem/usr/ko` |
| `RK_ENABLE_MOTOR` | **не задан** | `motor.ko` не собирается (см. раздел 7) |
| `RK_POST_BUILD_SCRIPT` | **не задан** | init-скрипты не переписываются пост-скриптом |

Используется автономная загрузка из SPI NAND: ядро и корневая ФС находятся
во flash, `oem` и `userdata` монтируются из соответствующих UBI-разделов.
Сетевые сервисы хоста для загрузки не используются.

После того как корневая ФС смонтирована и запущен busybox init, инициализация
выполняется по разделам 3–7.

## 2. Boot-цепочка

1. **BootROM** читает загрузчик из SPI NAND и передаёт управление TPL/SPL.
2. **U-Boot** (`luckfox_rv1106_uboot_defconfig` + фрагмент `rk-sfc.config`) инициализирует
   SFC и загружает ядро из раздела `boot`: читается FIT-образ (ядро + DTB
   `rv1106g-luckfox-pico-pro-max-sc230ai.dts`); корень — `root=ubi0:rootfs rootfstype=ubifs`.
3. **Ядро Linux 5.10.160** монтирует корень и запускает `/sbin/init` — симлинк на
   `/bin/busybox` (в текущем rootfs отдельного `/init` нет, `init=` в cmdline не задан).
4. **busybox init** читает `/etc/inittab` (раздел 3).

`start.sh` поднимает Docker-сборщик через `docker compose` (`build` + `up -d`)
и открывает shell внутри контейнера.

## 3. Инициализация busybox (inittab)

Шаблон: `sysdrv/source/buildroot/buildroot-2023.02.6/package/busybox/inittab`.
Записи `::sysinit` выполняются по порядку:

1. `mount -t proc proc /proc`
2. `mount -o remount,rw /`
3. `mkdir -p /dev/pts /dev/shm`
4. `mount -a` (монтирование по `/etc/fstab`)
5. `mkdir -p /run/lock/subsys`
6. `swapon -a`
7. симлинки `/dev/fd`, `/dev/stdin`, `/dev/stdout`, `/dev/stderr`
8. `hostname -F /etc/hostname`
9. `/etc/init.d/rcS` — запуск всех `S??*`-скриптов (раздел 4)

`getty` на `ttyS0` в шаблоне закомментирован. При выключении выполняются
`/etc/init.d/rcK`, `swapoff -a`, `umount -a -r`.

`/etc/init.d/rcS` перебирает `/etc/init.d/S??*` в лексикографическом порядке. Скрипты с
расширением `.sh` выполняются через `source` в подоболочке, остальные — как
`$i start`. «Висячие» симлинки пропускаются.

## 4. Порядок S*-скриптов

Фактический набор в собранном rootfs (`output/out/rootfs_uclibc_rv1106/etc/init.d/`)
совпадает с порядком ниже. Скрипты из оверлея
`project/cfg/BoardConfig_IPC/overlay/overlay-luckfox-buildroot-init/etc/init.d/`
добавляются в общий каталог при сборке.

| № | Скрипт | Назначение | Зависимости / примечания |
|---|---|---|---|
| 1 | `S01seedrng` | инициализация ГПСЧ | — |
| 2 | `S01syslogd` | системный логгер | — |
| 3 | `S02klogd` | логгер ядра | после `S01syslogd` |
| 4 | `S02sysctl` | применение `/etc/sysctl.conf` | `/proc` уже смонтирован |
| 5 | `S10udev` | eudev, создание узлов устройств; правило `60-oko-sdcard.rules` вызывает `oko-sdcard add/remove` и монтирует карту в `/mnt/sdcard` (состояние — `/run/oko-sd.state`) | должен пройти до монтирования по `by-name`; `&sdmmc` в DTS — съёмная карта с `sdmmc0_det` |
| 6 | `S20linkmount` | симлинки `/dev/block/by-name`, монтирование `oem`/`userdata` | ключевой этап, см. ниже |
| 7 | `S20oko-osd` | divinus по умолчанию (`/userdata/divinus.enable`, opt-out — `/userdata/rkipc.enable`), бутстрап OSD-конфига, запуск `oko-osd` | оверлей OKO; после `S20linkmount`, до `S21appinit` |
| 8 | `S20urandom` | сохранение/восстановление seed urandom | — |
| 9 | `S21appinit` | `sh /oem/usr/bin/RkLunch.sh` | **требует смонтированных `/oem` и `/userdata`** |
| 10 | `S21oko-ec` | запуск `oko-ec daemon` (телеметрия EC/INA/SHT21 в `/tmp/oko-ec.json` и `/tmp/oko-ec.osd`) | оверлей OKO; после `S20linkmount` и `S21appinit` (лексикографический порядок), требует `/dev/i2c-3` и `/oem/usr/bin/oko-ec`; opt-out — `/userdata/oko-ec.disable` |
| 11 | `S35iptables` | правила firewall | сеть ещё не обязательна |
| 12 | `S40network` | базовая настройка сети | — |
| 13 | `S41dhcpcd` | заглушка OKO; DHCP управляется `/usr/bin/oko-net` | после `S40network` |
| 14 | `S49ntp` | синхронизация времени | требует рабочей сети; `divinus` (запущен раньше, из `S21appinit`) через 10 с учитывает `ntp_enable`/`ntp_server` и может остановить этот демон |
| 15 | `S50sshd` | SSH-сервер | сеть |
| 16 | `S50telnet` | telnet-сервер | сеть |
| 17 | `S50usbdevice` | настройка USB-режима (gadget) | — |
| 18 | `S60micinit` | настройка микрофонного тракта через `amixer` | аудио-модули ядра |
| 19 | `S99_auto_reboot` | скрипты rockchip_test (auto-reboot / power-lost) | `/oem/rockchip_test`, `/data/cfg` |
| 20 | `S99hciinit` | `hciattach` для AIC8800 и поднятие `wlan0` | зависит от `aic8800_fdrv` |
| 21 | `S99luckfoxconfigload` | `luckfox-config load` — восстановление настроек | `/userdata` |
| 22 | `S99luckfoxcustomoverlay` | применение пользовательского DT-overlay из `/mnt/cfg` | `dtc`, `configfs` |
| 23 | `S99oko-test` | тестовый маркер OKO: пишет `/tmp/oko-test-ok` и строку в `/dev/kmsg` | оверлей OKO, `MARKER` в скрипте |
| 24 | `S99python` | запуск `/root/main.py` или `/root/boot.py` | `/usr/bin/python` |
| 25 | `S99rtcinit` | калибровка RTC при «нулевом» времени | — |
| 26 | `S99usb0config` | назначение IP `172.32.0.93` интерфейсу `usb0` | может перезапускать `S50usbdevice` |

`S20linkmount` генерируется в `project/build.sh` (при разборе таблицы разделов) в файл
`$RK_PROJECT_OUTPUT/S20linkmount` с `bootmedium=$RK_BOOT_MEDIUM`. Для `spi_nand` он:

- создаёт `/dev/block/by-name/{env,idblock,uboot,boot,oem,userdata,rootfs}` →
  `/dev/mtd0…mtd6`;
- `rootfs` не монтирует (корень уже поднят ядром, скрипт лишь печатает устройство);
- монтирует `oem` → `/oem` и `userdata` → `/userdata` как ubifs, при необходимости
  выполняя `ubiattach`/`ubimkvol`.

`rootfs` в этой ветке не участвует (`mountpt=IGNORE`): корень уже поднят ядром
из UBI-раздела. `oem` и `userdata` всегда монтируются из SPI NAND (ubifs).

> **Критическая зависимость:** `S20linkmount` обязан выполниться раньше `S21appinit`.
> `RkLunch.sh` читает `/oem/usr/bin`, `/oem/usr/ko`, `/oem/usr/share`, пишет в
> `/userdata`. Если монтирование не удалось, приложение не найдёт свои файлы.

## 5. Запуск приложения (`S21appinit` → `RkLunch.sh`)

`S21appinit` генерируется в `project/build.sh` (функция `__PACKAGE_OEM`) и копируется в
`/etc/init.d`:

```sh
#!/bin/sh
[ -f /etc/profile.d/RkEnv.sh ] && source /etc/profile.d/RkEnv.sh
case $1 in
	start) sh /oem/usr/bin/RkLunch.sh ;;
	stop)  sh /oem/usr/bin/RkLunch-stop.sh ;;
	*)     exit 1 ;;
esac
```

`/oem/usr/bin/RkLunch.sh` (исходник:
`project/app/rkipc/rkipc/src/rv1106_ipc/RkLunch.sh`):

1. Выполняет `rcS` по `/oem/usr/etc/init.d/S??*`. В текущей прошивке каталог
   `/oem/usr/etc/init.d/` пуст/отсутствует, поэтому цикл ничего не делает.
2. Настраивает окружение: `ulimit -c unlimited`, core-паттерн
   `/data/core-%p-%e`, `vm.overcommit_memory=1`.
3. Запускает `post_chk &` и сразу завершается — `rcS` продолжает работу, не дожидаясь
   инициализации приложения.

`post_chk` (в фоне):

1. Ожидает монтирование `/userdata` (до 30 попыток по 0.1 с, т.е. ~3 с).
2. Выбирает каталог модулей: `/oem/usr/ko`, если там есть `insmod_ko.sh`, иначе `/ko`,
   и выполняет `sh insmod_ko.sh` (раздел 6).
3. Запускает `network_init &`: если есть `/usr/bin/oko-net`, вызывает
   `oko-net apply`. MAC и режим сети (`oko_mac`, `oko_net_mode`, `oko_ipaddr`,
   `oko_netmask`, `oko_gateway`, `oko_dns`) читаются из env-раздела U-Boot
   через `fw_printenv`; MAC при первом запуске выводится из
   `/proc/device-tree/serial-number` и сохраняется в env. В режиме `dhcp`
   запускается `dhcpcd -b eth0` (client ID = MAC, без DUID), в режиме
   `static` — статический адрес/маршрут/DNS и DHCP-клиент не запускается.
   Старый vendor-путь с `/data/ethaddr.txt` и `udhcpc` остаётся только
   fallback-ом при отсутствии `oko-net`.
4. Создаёт симлинки для веб-интерфейса: `/userdata → /oem/usr/www/userdata`,
   `/media/usb0 → /oem/usr/www/usb0`, `/oem/usr/www/sdcard → /mnt/sdcard`.
   Сам `/mnt/sdcard` — точка монтирования microSD: udev-правило
   `lib/udev/rules.d/60-oko-sdcard.rules` и скрипт `usr/bin/oko-sdcard`
   (оверлей OKO) монтируют первый раздел `mmcblk*` и снимают его при
   извлечении, записывая состояние в `/run/oko-sd.state`
   (`present`, `mounted`, `device`, `error`, `fs`, `total_kb`, `free_kb`).
   Скрипт также выполняет команды WebUI:
   - `oko-sdcard mount [device]` — смонтировать карту (устройство из state,
     ровно один `mmcblk` с `device/type=SD` или явный `/dev/mmcblkX`;
     `/sys/block/*/removable` в этом ядре всегда 0, поэтому тип карты
     проверяется через `device/type`);
   - `oko-sdcard unmount` — снять `/mnt/sdcard` (lazy, если занята);
   - `oko-sdcard format [fat32] [device]` — `wipefs -a` при наличии,
     `sfdisk` (MBR, раздел `0x0c`, метка `OKO`) + `mkfs.vfat -F 32`, затем
     mount с `uid=1000,gid=1000,fmask=0133,dmask=0022`.
   Формат разрешён только для `mmcblk` с `device/type=SD` (eMMC = `MMC`);
   `/dev/mtd*`, `/dev/ubi*`, `/dev/root` и eMMC boot-разделы отвергаются.
   eudev читает только `/lib/udev/rules.d`, поэтому правило лежит там, а не в
   `/etc/udev/rules.d`. Вендорское `61-sd-cards-auto-mount.rules` заменено
   заглушкой: его безусловный `ACTION=="remove"` размонтировал только что
   отформатированную карту на stale-событиях `sfdisk`/`partprobe`. Монтажом и
   состоянием управляет только `oko-sdcard`; stale-remove для ещё существующего
   `mmcblkX` игнорируется. Все операции
   (`add`/`remove`/`mount`/`unmount`/`format`) сериализуются lock-каталогом
   `/run/oko-sd.lock`, чтобы udev не вмешался в форматирование. При активной
   записи `/api/storage` блокирует `unmount`/`format`; при потере монтирования
   recorder закрывает текущий сегмент и не пишет в NAND.
5. Проверяет маркер `/userdata/divinus.enable` — переключатель стримера OpenIPC
   `divinus` (MIT, вендорен в `project/app/divinus/`). Маркер создаётся по
   умолчанию скриптом `S20oko-osd` (оверлей OKO, выполняется до `S21appinit`);
   opt-out — файл `/userdata/rkipc.enable`: при его наличии `S20oko-osd` удаляет
   divinus-маркер, и `post_chk` запускает штатный `rkipc`. Если маркер есть:
- `S20oko-osd` заранее бутстрапит `/userdata/divinus.yaml`: при отсутствии
      файла или отсутствии в нём `reg0_text`/`reg0_size`/`reg0_color`/`reg3_text`
      (старый/ручной конфиг) файл сохраняется как `divinus.yaml.bak` и
      перезаписывается фабричным `/oem/usr/share/divinus/divinus.yaml` с тремя
      OSD-регионами (дата/время, телеметрия, внешние сообщения); в `post_chk`
      остаётся только резервное копирование фабричного конфига при отсутствии
      файла;
   - `post_chk` переходит в `/userdata` (конфиг ищется в каталоге исполняемого
     файла, затем в `./`, затем в `/etc/divinus.yaml`; при сохранении из WebUI файл
     пере-создаётся рядом с текущим каталогом, поэтому `/userdata/divinus.yaml`
     остаётся постоянным);
   - запускается `/oem/usr/bin/divinus &`, после чего `post_chk` завершается:
     секции rkipc ниже (ini, `image.bmp`, `speaker_test`, `csi_unite`, запуск
     rkipc) не выполняются. Удаление маркера возвращает штатный запуск `rkipc`.
     Если бинарь divinus отсутствует в неполной сборке, ветка не активируется и
     запускается `rkipc`.
6. Готовит `rkipc.ini`:
   - если `/oem/usr/share/rkipc.ini` отсутствует, фабричный ini выбирается по
     загруженному сенсору (`lsmod`): `mia1321` → `rkipc-mia1321-100w.ini`,
     `mis5001` → `rkipc-mis5001-500w.ini`, `sc530ai` → `rkipc-500w.ini`,
     `sc4336` → `rkipc-400w.ini`, `sc230ai` → `rkipc-300w.ini`,
     `sc3336` → `rkipc-300w.ini`, `imx415` → `rkipc-800w.ini`;
   - выбранный файл копируется в `/userdata/rkipc.ini`;
   - если ни один вариант не найден, печатается `Error: not found rkipc.ini !!!`
     и `post_chk` завершается с ошибкой.
7. Копирует `/oem/usr/share/image.bmp` в `/userdata`, при наличии проигрывает
   `speaker_test.wav` через `rk_mpi_ao_test`.
8. Вызывает `luckfox-config get_csi_unite`; для `imx415` требуется `unite=1`, для
   остальных — `0`. При расхождении значение меняется и `post_chk` возвращается без
   запуска rkipc (требуется перезагрузка).
9. Запускает `rkipc -a /oem/usr/share/iqfiles &` (или `rkipc &`, если каталога IQ нет).

> **Особенность SC230AI:** отдельной ветки выбора ini долго не было — исправлено:
> `post_chk` сопоставляет `sc230ai` шаблону `/oem/usr/share/rkipc-300w.ini`
> (сенсор 3 МП, ближайшая геометрия). Если `rkipc` стартует с некорректной геометрией,
> шаблон подбирается отдельно.

## 6. Порядок загрузки модулей ядра (`insmod_ko.sh`)

Источник: `sysdrv/drv_ko/insmod_ko.sh`; при сборке копируется в `/oem/usr/ko/` вместе с
модулями. Скрипт выполняется из своего каталога, поэтому имена `.ko` относительные.
Функция `__insmod` вставляет модуль только при наличии файла (`[ -f "$1" ]`), поэтому
отсутствующие модули молча пропускаются.

| Шаг | Действие | Модули / параметры |
|---|---|---|
| 1 | остановка обработки udev-событий | `udevadm control --stop-exec-queue` |
| 2 | DMA-буферы | `rk_dvbm.ko` |
| 3 | подсистема V4L2 | `videobuf2-memops.ko`, `videobuf2-common.ko`, `videobuf2-v4l2.ko`, `videobuf2-vmalloc.ko`, `videobuf2-cma-sg.ko` |
| 4 | драйверы сенсоров | в образе активной платы только **`sc230ai.ko`**; вендорские ветки `imx415`, `os04a10`, `sc4336`, `sc3336`, `sc530ai`, `gc2053`, `sc200ai`, `sc401ai`, `sc450ai`, `techpoint`, `mis5001`, `mia1321` остаются в общем `insmod_ko.sh`, но их `.ko` в oem отсутствуют и `__insmod` их пропускает |
| 5 | захват/обработка видео | `video_rkcif.ko`, `video_rkisp.ko` |
| 6 | MIPI CSI2 D-PHY | `phy-rockchip-csi2-dphy-hw.ko`, `phy-rockchip-csi2-dphy.ko` |
| 7 | выгрузка неиспользуемых сенсоров | `__rmmod_camera_sensor`: `rmmod` для модулей с refcount 0 |
| 8 | сброс «неготовых» устройств | `echo 1 > /sys/module/video_rkcif/parameters/clr_unready_dev` и аналогично для `video_rkisp` |
| 9 | 2D-ускоритель | `rga3.ko` |
| 10 | видео-кодек | `mpp_vcodec.ko` |
| 11 | NPU | `rknpu.ko` |
| 12 | аудио | `snd-soc-rv1106.ko` |
| 13 | мотор (условно) | `motor.ko` — в текущей прошивке отсутствует (см. раздел 7) |
| 14 | определение высоты сенсора | парсинг `Input Format` из `/proc/rkisp-vir0` или `/proc/rkisp0` в `sensor_height` |
| 15 | медиа-сервер | `rockit.ko mcu_fw_path="./hpmcu_wrap.bin" mcu_fw_addr=0xff6fe000 isp_max_h=$sensor_height` |
| 16 | видео-энкодер | `rve.ko` |
| 17 | возобновление udev | `udevadm control --start-exec-queue` |
| 18 | WiFi (фоном) | `$(pwd)/insmod_wifi.sh &` |

`insmod_wifi.sh` определяет чип по `uevent` SDIO/USB и загружает соответствующий набор:
AIC8800 (`aic_load_fw` → `aic8800_fdrv`), AP6XXX (`bcmdhd`), RTL8723BS
(`cfg80211` + `mac80211` + `r8723bs`), RTL8723DS, RTL8189FS, RTL8188FU, ATBM и др.
Загрузка WiFi не блокирует запуск `rkipc`.

## 7. Зависимости

### Разделы и файловая система

- `S20linkmount` → `S21appinit`: без смонтированных `/oem` и `/userdata` `RkLunch.sh`
  не найдёт `/oem/usr/bin/RkLunch.sh`, модули и IQ-файлы. Источник этих разделов —
  ubifs из SPI NAND.
- `/etc/iqfiles` — симлинк на `../oem/usr/share/iqfiles`, создаётся при упаковке
  (`build.sh`); `rkipc` запускается с `-a /oem/usr/share/iqfiles` напрямую.
- `rkipc.ini` хранится в `/userdata` (переживает перепрошивку), фабричный шаблон — в
  `/oem/usr/share`.

### Приложение rkipc

`rkipc` зависит от:

- `insmod_ko.sh` — весь медиастек (ISP, CIF, RGA, MPP, NPU, rockit, rve);
- IQ-файлов сенсора в `/oem/usr/share/iqfiles`;
- корректного `rkipc.ini` в `/userdata`;
- `luckfox-config` (проверка/установка `csi_unite`);
- смонтированных `/oem` и `/userdata`.

### Приложение divinus (по умолчанию, opt-out через `rkipc.enable`)

`/oem/usr/bin/divinus` запускается из `post_chk` при наличии
`/userdata/divinus.enable`; маркер создаёт `S20oko-osd`, если нет
`/userdata/rkipc.enable` (opt-out):

- зависит от тех же `insmod_ko.sh`, `/oem/usr/share/iqfiles` и смонтированных
  `/oem`/`/userdata`; отдельного набора модулей ядра не требует;
- вендорные библиотеки `librockit.so` и `librkaiq.so` загружает через
  `dlopen`/`dlsym` из `LD_LIBRARY_PATH`, который задаёт `RkEnv.sh`
  (`/oem/usr/lib`), наследуемый от `S21appinit`;
- конфиг — `/userdata/divinus.yaml` (фабричный шаблон —
  `/oem/usr/share/divinus/divinus.yaml`); при старте каталог — `/userdata`, чтобы
  сохранение из WebUI пере-создавало именно этот файл, а не симлинк; если файл
  не разбирается (ручная правка, импорт), `main.c` один раз восстанавливает
  `divinus.yaml.bak` и повторяет разбор, и только затем завершается;
- OSD-текст с датой/временем рендерится самим divinus (`reg0_text: "$t"`),
  время — в правом нижнем углу; снимок `/image.jpg` кодируется отдельным
  VENC-каналом и OSD не содержит (ограничение RGN на RV1106);
- NTP: дистрибутивный `S49ntp` запускается после `S21appinit`, поэтому divinus
  применяет `[system] ntp_enable`/`ntp_server` из своего конфига через 10 с
  после старта (`ntp_startup_schedule`): при `ntp_enable=false` останавливает
  `ntpd`, при заданном сервере перезапускает его с `/tmp/divinus-ntp.conf`, при
  пустом сервере оставляет вендорский `/usr/sbin/ntpd -g -p
  /var/run/ntpd.pid` (или поднимает его, если `S49ntp` не смог);
- `reg1` — телеметрия `T/BAT/LTE/WiFi` четырьмя строками в левом верхнем углу,
  `reg2` — GPS в правом верхнем, `reg3` — сообщения из FIFO под телеметрией,
  `reg4` — реальные CPU load, свободная память и температура CPU тремя
  строками в левом нижнем углу;
  телеметрия и GPS пишутся через `@file`-провайдер (локальный патч `region.c`):
  `/tmp/osd-dyn.txt`, `/tmp/osd-gps.txt`, `/tmp/osd-sys.txt` и
  `/tmp/osd-msg.txt` пишет демон
  `oko-osd`, который запускает `S20oko-osd` вместе с FIFO `/tmp/osd.fifo`;
  эмулированные `GPS`, температура, батарея, `LTE` и `WiFi` (dBm + полосы)
  обновляются раз в 15 с;
- размер шрифта всех OSD-регионов задан `regN_size: 24.0`, цвет текста —
  `regN_color: 0x07e0` (ярко-зелёный RGB565), контур — `regN_outl: 0x8000`
  (чёрный) толщиной `regN_thick: 1.0` в фабричном `divinus.yaml`;
  RGN использует один canvas, чтобы OSD не отставал на секунду;
- `reg5` (`@/tmp/osd-ivs.txt`) — индикатор IVS `MOTION HH:MM:SS` под
  сообщениями; файл пишет поток аналитики при `ivs.act_osd`, через 10 с без
  движения регион гаснет;
- второй H.26x-поток (`substream.enable`) публикуется как `/live/1`;
  main остаётся `/live/0`. На RV1106 четыре VPSS-канала: при включённом IVS
  заняты main/sub/JPEG, MJPEG должен быть выключен;
- запись (`record.*`) разрешена только при реально смонтированной microSD
  (`/mnt/sdcard`), пути `/oem` и `/userdata` отвергаются; `record.max_bytes`
  и `record.min_free_mb` удаляют самые старые `.mp4` перед открытием
  следующего сегмента; без карты `/api/record?enable=true` возвращает
  `blocked`;
- `divinus` использует пропатченный `compat.c` (libc `mmap` с 32-битным `off_t`),
  поэтому дополнительный `LD_PRELOAD` не нужен;
- `divinus` и `rkipc` используют один rockit/ISP-конвейер, поэтому
  взаимоисключающие: одновременно не запускаются, выбор — маркерами
  `divinus.enable`/`rkipc.enable`.

### Демон oko-ec

`/oem/usr/bin/oko-ec daemon` запускается из `S21oko-ec` (после `S20linkmount`,
`S20oko-osd` и `S21appinit`) и опрашивает три устройства на `/dev/i2c-3`:

- `SHT21 0x40` — no-hold измерения температуры и влажности;
- `INA3221 0x41` (или INA226) — bus/shunt-напряжение, ток и мощность канала
  1..3 (определяется по `Die ID`; без калибровочного регистра; шунт 100 мОм по
  умолчанию). Номер канала для демона задаёт `INA_CHANNEL` в `S21oko-ec`
  (по умолчанию 1) или переменная `OKO_EC_INA_CHANNEL`;
- `STM32 ACPI-EC 0x42` — `GET_BATTERY_DATA` (только при `device_type = 2`).

Зависимости и условия:

- `/dev/i2c-3` создаётся драйвером `i2c-rk3x` из узла `&i2c3` в DTS
  (`status = "okay"`, 100 кГц, `i2c3m1_xfer`); без узла демон не стартует;
- `/oem/usr/bin/oko-ec` собирается пакетом `project/app/oko-ec` и попадает в
  образ через `RK_BUILD_APP_TO_OEM_PARTITION=y`;
- opt-out — маркер `/userdata/oko-ec.disable` (демон не запускается, OSD
  остаётся на симуляции);
- `/userdata` должен быть смонтирован (`S21oko-ec` ждёт монтирования, как
  `S20oko-osd`);
- демон сам пишет `/tmp/oko-ec.pid`, `/tmp/oko-ec.json` и `/tmp/oko-ec.osd`;
  init-скрипт только читает PID-файл для `stop`/`status`;
- `oko-osd` использует `/tmp/oko-ec.osd`, пока он свежий (≤ 30 с) и `OK 1`:
  выводит строки SHT21 (температура/влажность), STM32 (напряжение/ток/
  температура/SOC/ячейки) и INA3221 (напряжение/ток/мощность); при ошибке
  датчиков или остановленном демоне возвращается к симуляции `T`/`BAT`.

### Модули ядра

- `video_rkisp` / `video_rkcif` зависят от `videobuf2-*` и модуля сенсора.
- `rockit.ko` зависит от результата детекции высоты сенсора (`/proc/rkisp-vir0`) и
  прошивки MCU `hpmcu_wrap.bin` (путь `./hpmcu_wrap.bin`, адрес `0xff6fe000`).
- `sc230ai.ko` — единственный сенсорный модуль в текущем образе; IQ-каталог
  `/oem/usr/share/iqfiles` содержит только
  `sc230ai_CMK-OT2115-PC1_30IRC-F16.json`. Остальные сенсорные `.ko`/IQ
  удаляются из oem при сборке (`__RUN_POST_CLEAN_FILES`), поэтому шаг 4
  `insmod_ko.sh` загружает только `sc230ai`, а шаг 7 не находит лишних
  модулей. Вендорские исходники и DTS других плат не изменяются.
- WiFi-модули зависят от `cfg80211`, а для RTL8723BS — ещё и от `mac80211` и
  крипто-модулей (`libarc4`, `aes_generic` и т.п.); состав определяется чипом.
- **`motor.ko` — условный модуль.** Флаг `RK_ENABLE_MOTOR` в активной конфигурации не
  задан, поэтому в `sysdrv/cfg/package.mk` остаётся
  `CONFIG_SYSDRV_ENABLE_MOTOR=n`, модуль не собирается и отсутствует в
  `/oem/usr/ko/`. Строка `__insmod motor.ko` в `insmod_ko.sh` пропускается проверкой
  `-f`. Для включения нужно:
  1. задать `RK_ENABLE_MOTOR=y` в board config;
  2. добавить в DTS узел `motor` с GPIO (пример —
     `sysdrv/drv_ko/motor/ReadMe`: `motorA…motorH-gpios` на `gpio1` PC0–PC7).
- `hpmcu_wrap.bin` — прошивка встроенного MCU, загружается драйвером `rockit`.

### Сеть

- `RkLunch.sh` вызывает `oko-net apply`; `S41dhcpcd` заменён заглушкой.
  MAC хранится в env (`oko_mac`), поэтому не зависит от `/userdata` и
  переживает полную прошивку; DHCP-клиент один (`dhcpcd`), его client ID —
  MAC, а не DUID из `/var/db/dhcpcd` (иначе после перепрошивки менялся бы
  адрес). Настройки статического IP также лежат в env-разделе.
- `S49ntp`, `S50sshd`, `S50telnet` рассчитывают на уже поднятую сеть.

## 8. Источники и проверка

| Этап | Исходный файл |
|---|---|
| Конфигурация платы | `project/cfg/BoardConfig_IPC/BoardConfig-SPI_NAND-Buildroot-RV1106_Luckfox_Pico_Pro_Max-SC230AI.mk` |
| Окружение сборки (Docker) | `Dockerfile`, `docker-compose.yml`, `docker-entrypoint.sh`, `start.sh` |
| U-Boot / ядро / cmdline | `project/build.sh` (генерация `S20linkmount`, `S21appinit`), `sysdrv/source/kernel/arch/arm/configs/rv1106-sc230ai.config` |
| inittab | `sysdrv/source/buildroot/buildroot-2023.02.6/package/busybox/inittab` |
| Init-скрипты (оверлей) | `project/cfg/BoardConfig_IPC/overlay/overlay-luckfox-buildroot-init/etc/init.d/` |
| Загрузка модулей | `sysdrv/drv_ko/insmod_ko.sh` |
| WiFi | `sysdrv/drv_ko/wifi/insmod_wifi.sh` |
| Приложение | `project/app/rkipc/rkipc/src/rv1106_ipc/RkLunch.sh` |
| Мотор | `sysdrv/drv_ko/motor/ReadMe`, `sysdrv/cfg/package.mk` |
| Сверка артефакта | `output/out/rootfs_uclibc_rv1106/etc/init.d/`, `output/out/oem/usr/ko/` |

Быстрая проверка после изменений:

```sh
# фактический список init-скриптов
ls output/out/rootfs_uclibc_rv1106/etc/init.d/S??*

# порядок загрузки модулей
grep -n "__insmod\|insmod_wifi" sysdrv/drv_ko/insmod_ko.sh

# наличие условных модулей
ls output/out/oem/usr/ko/ | grep -E "motor|hpmcu"

# зависимости приложения
grep -n "insmod_ko\|rkipc.ini\|iqfiles\|luckfox-config" \
  project/app/rkipc/rkipc/src/rv1106_ipc/RkLunch.sh
```