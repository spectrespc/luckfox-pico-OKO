# Порядок инициализации прошивки

Документ описывает полную цепочку запуска прошивки Luckfox Pico Pro Max (RV1106):
BootROM → U-Boot → ядро Linux → busybox init → init-скрипты → загрузка модулей ядра →
запуск приложения `rkipc`, а также зависимости между разделами, модулями и сервисами.

Активная конфигурация определяется симлинком `.BoardConfig.mk`, который указывает на
`project/cfg/BoardConfig_IPC/BoardConfig-SPI_NAND-Buildroot-RV1106_Luckfox_Pico_Pro_Max-NETBOOT-SC230AI.mk`.

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
| `RK_KERNEL_DTS` | `rv1106g-luckfox-pico-pro-max.dts` | device tree платы |
| `RK_UBOOT_DEFCONFIG` | `luckfox_rv1106_uboot_defconfig` | конфигурация U-Boot |
| `RK_UBOOT_DEFCONFIG_FRAGMENT` | `rk-sfc.config` | поддержка SPI flash controller |
| `RK_KERNEL_DEFCONFIG` | `luckfox_rv1106_linux_defconfig` | базовая конфигурация ядра |
| `RK_KERNEL_DEFCONFIG_FRAGMENT` | `rv1106-netboot.config rv1106-sc230ai.config` | NFS-root + сенсор SC230AI |
| `RK_BOOTARGS_CMA_SIZE` | `66M` | в cmdline попадает как `rk_dma_heap_cma=66M` |
| `RK_CAMERA_SENSOR_IQFILES` | `sc230ai_CMK-OT2115-PC1_30IRC-F16.json` | IQ-файл сенсора |
| `RK_ENABLE_WIFI` | `y` | WiFi-модули собираются и копируются в `/oem/usr/ko` |
| `RK_ENABLE_MOTOR` | **не задан** | `motor.ko` не собирается (см. раздел 7) |
| `RK_POST_BUILD_SCRIPT` | **не задан** | init-скрипты не переписываются пост-скриптом |

Используются два пути загрузки:

1. **Прошивка SPI NAND** — самодостаточный вариант: ядро и корневая ФС находятся во flash.
2. **Netboot** — рабочий вариант текущей конфигурации: ядро (`boot.img`) загружается по
   TFTP, корневая ФС монтируется по NFS с хоста. Разделы `oem` и `userdata` в netboot
   также берутся с хоста: `S20linkmount` монтирует их по NFS из
   `<host>:<nfsroot>/.nfsroot/{oem,userdata}` (см. раздел 4). Если NFS-монтирование
   раздела не удалось, скрипт откатывается на прежнее поведение — ubifs из SPI NAND.

После того как корневая ФС смонтирована и запущен busybox init, инициализация обоих
вариантов идентична.

## 2. Boot-цепочка

1. **BootROM** читает загрузчик из SPI NAND и передаёт управление TPL/SPL.
2. **U-Boot** (`luckfox_rv1106_uboot_defconfig` + фрагмент `rk-sfc.config`) инициализирует
   SFC и загружает ядро:
   - **SPI NAND:** из раздела `boot` читается FIT-образ (ядро + DTB
     `rv1106g-luckfox-pico-pro-max.dts`); корень — `root=ubi0:rootfs rootfstype=ubifs`.
   - **Netboot:** по TFTP загружается `boot.img`, затем выполняется `boot_fit`; переменная
     `sys_bootargs` содержит NFS-root, например:
     ```
     root=/dev/nfs rw nfsroot=<host>:/srv/nfs/rootfs,vers=3,proto=tcp,nolock \
     ip=<ip>:<server>:<gw>:<mask>:luckfox:eth0:off rk_dma_heap_cma=66M
     ```
     Поддержка обеспечивается фрагментом `rv1106-netboot.config`
     (`CONFIG_IP_PNP`, `CONFIG_ROOT_NFS`).
3. **Ядро Linux 5.10.160** монтирует корень и запускает `/sbin/init` — симлинк на
   `/bin/busybox` (в текущем rootfs отдельного `/init` нет, `init=` в cmdline не задан).
4. **busybox init** читает `/etc/inittab` (раздел 3).

Вспомогательные средства netboot в корне репозитория: `start.sh` (поднимает
Docker-сборщик через `docker compose` — `build` + `up -d` — и открывает shell внутри
контейнера; TFTP-корень `output/image`) и `netboot-catch.py` (автоматизация загрузки по serial:
`setenv sys_bootargs`, `tftp ${ramdisk_addr_r} boot.img`, `boot_fit`).

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
| 5 | `S10udev` | eudev, создание узлов устройств | должен пройти до монтирования по `by-name` |
| 6 | `S20linkmount` | симлинки `/dev/block/by-name`, монтирование `oem`/`userdata` | ключевой этап, см. ниже |
| 7 | `S20urandom` | сохранение/восстановление seed urandom | — |
| 8 | `S21appinit` | `sh /oem/usr/bin/RkLunch.sh` | **требует смонтированных `/oem` и `/userdata`** |
| 9 | `S35iptables` | правила firewall | сеть ещё не обязательна |
| 10 | `S40network` | базовая настройка сети | — |
| 11 | `S41dhcpcd` | DHCP-клиент | после `S40network` |
| 12 | `S49ntp` | синхронизация времени | требует рабочей сети |
| 13 | `S50sshd` | SSH-сервер | сеть |
| 14 | `S50telnet` | telnet-сервер | сеть |
| 15 | `S50usbdevice` | настройка USB-режима (gadget) | — |
| 16 | `S60micinit` | настройка микрофонного тракта через `amixer` | аудио-модули ядра |
| 17 | `S99_auto_reboot` | скрипты rockchip_test (auto-reboot / power-lost) | `/oem/rockchip_test`, `/data/cfg` |
| 18 | `S99hciinit` | `hciattach` для AIC8800 и поднятие `wlan0` | зависит от `aic8800_fdrv` |
| 19 | `S99luckfoxconfigload` | `luckfox-config load` — восстановление настроек | `/userdata` |
| 20 | `S99luckfoxcustomoverlay` | применение пользовательского DT-overlay из `/mnt/cfg` | `dtc`, `configfs` |
| 21 | `S99oko-test` | тестовый маркер OKO: пишет `/tmp/oko-test-ok` и строку в `/dev/kmsg` | оверлей OKO, `MARKER` в скрипте |
| 22 | `S99python` | запуск `/root/main.py` или `/root/boot.py` | `/usr/bin/python` |
| 23 | `S99rtcinit` | калибровка RTC при «нулевом» времени | — |
| 24 | `S99usb0config` | назначение IP `172.32.0.93` интерфейсу `usb0` | может перезапускать `S50usbdevice` |

`S20linkmount` генерируется в `project/build.sh` (при разборе таблицы разделов) в файл
`$RK_PROJECT_OUTPUT/S20linkmount` с `bootmedium=$RK_BOOT_MEDIUM`. Для `spi_nand` он:

- создаёт `/dev/block/by-name/{env,idblock,uboot,boot,oem,userdata,rootfs}` →
  `/dev/mtd0…mtd6`;
- `rootfs` не монтирует (корень уже поднят ядром, скрипт лишь печатает устройство);
- монтирует `oem` → `/oem` и `userdata` → `/userdata` как ubifs, при необходимости
  выполняя `ubiattach`/`ubimkvol`.

В netboot-режиме (`root=/dev/nfs` в `/proc/cmdline`) `mount_part()` сначала пытается
смонтировать `oem` и `userdata` по NFS:

- сервер и путь берутся из `nfsroot=<host>:<path>` в `/proc/cmdline`, хардкода нет;
- монтируется `<host>:<path>/.nfsroot/<partname>` с опциями
  `vers=3,proto=tcp,nolock,soft,timeo=50,retrans=3`;
- успешное монтирование печатает `netboot: <partname> from nfs <host>:<path>/.nfsroot`;
- при ошибке NFS печатается `netboot: nfs mount <partname> failed, fallback to spi_nand`
  и выполняется прежняя ветка — ubifs с SPI NAND.

`rootfs` в этой ветке не участвует (`mountpt=IGNORE`), NAND при удачном NFS-монтировании
не трогается. Зеркала разделов на хосте — `<nfsroot>/.nfsroot/{oem,userdata}`; их
содержимое обновляется из `output/out/oem` и `output/out/userdata` после сборки.

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
3. Запускает `network_init &`: берёт MAC из `/data/ethaddr.txt` (при отсутствии —
   сохраняет текущий), поднимает `eth0`, запускает `udhcpc -i eth0`. При netboot
   (`nfsroot=` в `/proc/cmdline`) шаг пропускается: адрес уже задан параметром `ip=`,
   а смена MAC или DHCP-адреса разрывает NFS-соединения корня и `/oem`/`/userdata`.
4. Создаёт симлинки для веб-интерфейса: `/userdata → /oem/usr/www/userdata`,
   `/media/usb0 → /oem/usr/www/usb0`, `/mnt/sdcard → /oem/usr/www/sdcard`.
5. Проверяет маркер `/userdata/divinus.enable` — переключатель альтернативного
   стримера OpenIPC `divinus` (MIT, вендорен в `project/app/divinus/`). Если маркер
   есть:
   - при отсутствии `/userdata/divinus.yaml` копируется фабричный конфиг
     `/oem/usr/share/divinus/divinus.yaml`;
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
| 4 | драйверы сенсоров | `imx415`, `os04a10`, `sc4336`, `sc3336`, `sc530ai`, **`sc230ai`**, `gc2053`, `sc200ai`, `sc401ai`, `sc450ai`, `techpoint`, `mis5001`, `mia1321` |
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
  не найдёт `/oem/usr/bin/RkLunch.sh`, модули и IQ-файлы. Источник этих разделов зависит
  от загрузки: SPI NAND → ubifs с flash, netboot → NFS `<host>:<nfsroot>/.nfsroot/*`
  (fallback — снова NAND).
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

### Приложение divinus (альтернатива rkipc, opt-in)

`/oem/usr/bin/divinus` запускается из `post_chk` только при наличии
`/userdata/divinus.enable`:

- зависит от тех же `insmod_ko.sh`, `/oem/usr/share/iqfiles` и смонтированных
  `/oem`/`/userdata`; отдельного набора модулей ядра не требует;
- вендорные библиотеки `librockit.so` и `librkaiq.so` загружает через
  `dlopen`/`dlsym` из `LD_LIBRARY_PATH`, который задаёт `RkEnv.sh`
  (`/oem/usr/lib`), наследуемый от `S21appinit`;
- конфиг — `/userdata/divinus.yaml` (фабричный шаблон —
  `/oem/usr/share/divinus/divinus.yaml`); при старте каталог — `/userdata`, чтобы
  сохранение из WebUI пере-создавало именно этот файл, а не симлинк;
- `divinus` использует пропатченный `compat.c` (libc `mmap` с 32-битным `off_t`),
  поэтому дополнительный `LD_PRELOAD` не нужен;
- `divinus` и `rkipc` используют один rockit/ISP-конвейер, поэтому
  взаимоисключающие: одновременно не запускаются, выбор — маркером.

### Модули ядра

- `video_rkisp` / `video_rkcif` зависят от `videobuf2-*` и модуля сенсора.
- `rockit.ko` зависит от результата детекции высоты сенсора (`/proc/rkisp-vir0`) и
  прошивки MCU `hpmcu_wrap.bin` (путь `./hpmcu_wrap.bin`, адрес `0xff6fe000`).
- `sc230ai.ko` активен в текущей конфигурации; остальные сенсорные модули
  выгружаются на шаге 7, если их refcount равен 0.
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

- `RkLunch.sh` поднимает `eth0` и `udhcpc` в фоне параллельно с `S40network` /
  `S41dhcpcd`. MAC-адрес фиксируется в `/data/ethaddr.txt`. В netboot DHCP не
  запускается: адрес берётся из `ip=` в cmdline (иначе смена IP рвёт NFS-корень).
- `S49ntp`, `S50sshd`, `S50telnet` рассчитывают на уже поднятую сеть.
- В netboot-режиме сеть нужна ещё до старта userspace — на этапе U-Boot (TFTP) и ядра
  (NFS-root).

## 8. Источники и проверка

| Этап | Исходный файл |
|---|---|
| Конфигурация платы | `project/cfg/BoardConfig_IPC/BoardConfig-SPI_NAND-Buildroot-RV1106_Luckfox_Pico_Pro_Max-NETBOOT-SC230AI.mk` |
| Окружение сборки (Docker) | `Dockerfile`, `docker-compose.yml`, `docker-entrypoint.sh`, `start.sh` |
| U-Boot / ядро / cmdline | `project/build.sh` (генерация `S20linkmount`, `S21appinit`), `sysdrv/source/kernel/arch/arm/configs/rv1106-netboot.config` |
| inittab | `sysdrv/source/buildroot/buildroot-2023.02.6/package/busybox/inittab` |
| Init-скрипты (оверлей) | `project/cfg/BoardConfig_IPC/overlay/overlay-luckfox-buildroot-init/etc/init.d/` |
| Загрузка модулей | `sysdrv/drv_ko/insmod_ko.sh` |
| WiFi | `sysdrv/drv_ko/wifi/insmod_wifi.sh` |
| Приложение | `project/app/rkipc/rkipc/src/rv1106_ipc/RkLunch.sh` |
| Мотор | `sysdrv/drv_ko/motor/ReadMe`, `sysdrv/cfg/package.mk` |
| Netboot-утилиты | `start.sh`, `netboot-catch.py` |
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