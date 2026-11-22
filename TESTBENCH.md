# Тестовый стенд OKO

Паспорт и рабочие параметры лабораторного стенда для отладки прошивки Luckfox Pico
Pro Max (RV1106) с автономной загрузкой из SPI NAND. Значения сверены с `bench/*.py`,
`tools/native/*.sh` и активным BoardConfig на дату обновления.

## 1. Паспорт стенда

| Поле | Значение |
|---|---|
| Дата обновления | `2026-11-22` |
| Ответственный | `<ФИО>` |
| Статус | активен |
| Назначение | сборка прошивки и прошивка SPI NAND для отладки IPC |
| Расположение | `<лаборатория / рабочее место>` |

## 2. Подопытное устройство (DUT)

| Параметр | Значение |
|---|---|
| Плата | Luckfox Pico Pro Max |
| SoC | Rockchip RV1106 (Cortex-A7 + NPU + ISP) |
| Носитель загрузки | SPI NAND (`RK_BOOT_MEDIUM=spi_nand`) |
| Корневая ФС | Buildroot, busybox init |
| Сенсор | SC230AI (MIPI CSI, драйвер `sc230ai`, 1080p30 / 480p120; `HDR_X2` экспериментально; `divinus` кодирует 30 fps) |
| Двигатель | `STM8S003F3P6` на I2C (адрес `0x60`, `/dev/i2c-3`, 100 кГц) — привод фокуса/зума |
| EC | STM32C011F6P6 (`st-info`: 32 КиБ Flash, 2 КиБ страница), адрес `0x42`, proto v3, приложение 20 КиБ |
| Питание | `<источник, напряжение, ток>` |
| Подключение к ПК | USB host: хаб `1a40:0101` на порту DUT (gadget/adb недоступны, KI-057); UART через ESP32-C6-мост `192.168.52.17` — прошивка [ESP32-OKO-KVM](https://github.com/spectrespc/ESP32-OKO-KVM) (на стенде `1.10.0`): telnet `:23` (консоль) и REST API HTTP `:80` (§8); Maskrom — `<кнопка / перемычка>` при прямом USB |
| Серийный номер / метка | `<S/N>` |

## 3. Хост

| Параметр | Значение |
|---|---|
| ОС | Debian 13 trixie |
| Компилятор хоста | gcc/g++ 14.2 (`host-tools` U-Boot/ядра собираются им) |
| Целевой тулчейн | `arm-rockchip830-linux-uclibcgnueabihf` (gcc 8.3.0, uClibc-ng, `tools/linux/toolchain/`) |
| Сборка | нативная, без контейнера: `tools/native/setup_host.sh`, `source tools/native/env.sh`, `./build.sh` |
| Рабочая папка | корень репозитория на хосте |
| Docker | `docker.io` + плагин `docker compose`, пользователь в группе `docker`; контейнер `services/` (`network_mode: host`, `privileged`) |
| Сетевые сервисы | контейнер `services/`: `tftpd-hpa` (корень `output/image`, chroot) и `nfs-kernel-server` (экспорт `output/srv/nfs/rootfs` → `/srv/nfs/rootfs`) |
| Запуск окружения | `source tools/native/env.sh` (проверка — `tools/native/smoke.sh`) |

## 4. Подключения

| Интерфейс | Значение | Примечание |
|---|---|---|
| UART | **telnet `192.168.52.17:23`** (ESP32-C6-мост, прошивка ESP32-OKO-KVM) | основной путь: `telnet 192.168.52.17` или `bench/run.sh serial_exec.py …`; `bench/uart.py` сам отвечает на IAC-согласование и вырезает его из потока. Мост обслуживает **одного управляющего клиента**: перед запуском скриптов закройте другие telnet-сессии, иначе connect завершится `timed out`. Параллельный съём лога — read-only мониторы UART (`:2324`/`:24`, по умолчанию выключены, §8). Помимо telnet мост отдаёт REST API по HTTP `:80` (конфиг, SD-логи, питание/сброс DUT, логи ESP) — команды и гигиена сессий в §8 |
| UART, резерв | последний `/dev/ttyACM*` | локальный USB-UART, если мост недоступен: `--port /dev/ttyACM0` или `OKO_SERIAL_PORT=/dev/ttyACM0` |
| Скорость UART | `115200 8N1` | мост настроен на ту же скорость; задана в скриптах `bench/` |
| Ethernet | лабораторная сеть `192.168.52.0/24` | через порт устройства |
| USB | DUT — host: хаб `1a40:0101` (4 порта), питание платы отдельное 5 В | хаб подключён (2026-11-11), за ним — `0bda:c811` (Realtek 802.11ac NIC, RTL8821CU); с 2026-11-12 драйвер `8821cu.ko` входит в образ. Порт закреплён в host-режиме (`dr_mode = "host"`), устройство за хабом видно сразу после загрузки; Maskrom-прошивка по этому порту возможна только при прямом кабеле к хосту (хаб скрывает загрузчик), по умолчанию прошивка — TFTP (§7-TFTP). VBUS SoC не управляет |
| I2C3-M1 | `/dev/i2c-3`, 100 кГц, `GPIO1_D2`/`GPIO1_D3` | SHT21 `0x40`, INA3221 `0x41` (`A0` = VS; die ID `0x3220`), STM32 ACPI-EC `0x42`; нужны внешние pull-up 2.2–4.7 кОм к 3.3 В (в DTS `pcfg_pull_none_smt`). Каналы INA3221: **CH1 — DRV8833 + двигатели focus/zoom, CH2 — STM32 ACPI-EC, CH3 — RV1106 + STM8** (замер 2026-11-22: CH1 ≈ 11 мА / 51 мВт, CH2 ≈ 2 мА / 9 мВт, CH3 ≈ 380 мА / 1.8 Вт при шине ≈ 4.6 В; ток считается по шунту `--shunt-mohm`, по умолчанию 100 мОм) |

Требования к хосту: Python 3 и зависимости из `bench/requirements.txt`;
`bench/setup.sh` создаёт venv в `output/bench/venv`, запуск — `bench/run.sh`.

## 5. Сеть

| Узел | Адрес |
|---|---|
| Устройство (DUT), NAND | `192.168.52.33` (DHCP; адрес может смениться — сверяйте `ifconfig eth0` или `/api/status`) |
| Устройство (DUT), NFS | `192.168.52.150` (статический из `ip=` в `sys_bootargs`) |
| Хост TFTP/NFS | `192.168.52.200` |
| UART-мост (ESP32-C6, ESP32-OKO-KVM) | `192.168.52.17`: TCP `23` (telnet, консоль DUT, 115200 8N1), TCP `2323` (telnet UART1); HTTP `:80` — REST API (конфиг, SD-логи, питание/сброс DUT, логи ESP); мониторы UART `:2324`/`:24` (read-only, выключены по умолчанию) |
| Хост, приёмник UDP/RTP (MediaMTX) | `192.168.52.200`: `5600/udp` (вход от камеры), `8554/tcp` (RTSP), `8000–8001/udp` (RTP/RTCP для RTSP-читателей), `8888/tcp` (HLS), `8889/tcp` (WebRTC), `8189/udp` (WebRTC ICE), `9997/tcp` — control API только на `127.0.0.1` |
| Хост, MQTT | `192.168.52.200`: `1883/tcp` — брокер `oko-mqtt-broker` (логин обязателен), `8092/tcp` — HTTP-API рекордера `oko-mqtt-recorder` |
| Маска | `255.255.255.0` |
| Шлюз | `192.168.52.254` |

В автономном режиме адрес `eth0` выдаётся по DHCP (`oko-lunch.sh` → `oko-net apply`).
В NFS-режиме адрес задаётся в cmdline (`ip=192.168.52.150::…:off`), а `oko-lunch.sh`
пропускает DHCP. Фактический адрес смотрите на DUT (`ifconfig eth0`) или в
консоли UART (telnet-мост, §4). Для стабильных проверок зарезервируйте адрес `192.168.52.150`
в DHCP-сервере лабораторной сети.

## 6. Учётные данные

> **Внимание.** В файл запрещено записывать личные, рабочие или production-пароли.
> Допустимы только публичные вендорские значения по умолчанию и плейсхолдеры
> вида `<...>`. Для реальных секретов используйте менеджер паролей или переменные
> окружения вне репозитория.

| Система | Логин | Пароль |
|---|---|---|
| Устройство (SSH / Samba / console) | `root` | `luckfox` — публичный вендорский дефолт (`BR2_TARGET_GENERIC_ROOT_PASSWD` в `luckfox_pico_defconfig`) |
| WiFi (`LF_WIFI_SSID`) | — | `<SSID стенда>` |
| WiFi (`LF_WIFI_PSK`) | — | `<пароль WiFi — заполнять только при необходимости>` |
| SoftAP устройства (`OKO-XXXX`, SSID из MAC) | — | `ybrjveytljdthzq` — публичный фабричный дефолт WPA2-PSK, пока `oko_wlan_ap_psk` не задан; сменить до ввода в эксплуатацию (KI-030) |
| divinus Web/RTSP (фабричный) | `admin` | `oko-change-me` — публичный фабричный дефолт, сменить до подключения к недоверенной сети |
| ESP32-мост (Web UI / REST API) | `admin` | `admin` — вендорский дефолт прошивок до 1.4.2; в свежей прошивке при первой загрузке генерируется случайный пароль (печатается в USB-Serial/JTAG консоль моста). Переопределение в скриптах — `OKO_BRIDGE`, `OKO_BRIDGE_USER`, `OKO_BRIDGE_PASSWORD` |
| MQTT-брокер стенда (`services/mqtt`) | `oko` | `oko` — публичный стендовый дефолт (`OKO_MQTT_USER`/`OKO_MQTT_PASS`) |
| MQTT-рекордер стенда | `oko-rec` | `oko-rec` — публичный стендовый дефолт (`OKO_MQTT_REC_USER`/`OKO_MQTT_REC_PASS`), подписка только на чтение |
| Хост | `spectre` | sudo по паролю; в репозитории и скриптах не хранится |

Перед выдачей стенда наружу вендорский пароль `luckfox` необходимо сменить.

## 7. Прошивка SPI NAND

### 7-TFTP. Прошивка по TFTP (по умолчанию)

Основной режим стенда с 2026-11-11: USB-порт DUT занят хабом, Maskrom по USB
хост не видит, а TFTP доступен по Ethernet. USB и кнопки Maskrom не нужны.
Перед запуском закройте свои telnet-сессии к мосту (§4) — он обслуживает
одного клиента, иначе скрипт не подключится (`timed out`).

```sh
./build.sh firmware                 # output/image/{boot,oem,rootfs,userdata}.img
tools/native/setup_netboot.sh       # TFTP/NFS-контейнер services/ (если ещё не поднят)
bench/run.sh tftp_flash.py          # boot + oem + rootfs, затем reset
bench/run.sh tftp_flash.py --components boot          # только ядро и DTB
bench/run.sh tftp_flash.py --userdata                 # плюс factory-userdata (стирает раздел)
bench/run.sh tftp_flash.py --no-reset                 # остаться в U-Boot для проверок
```

Что делает скрипт:

1. По консоли (§4) входит в U-Boot: если Linux загружен — перезагружает его и
   ловит `Hit any key to stop autoboot`, иначе ждёт prompt `=>`.
   Плата в Maskrom/RKUART по консоли не восстанавливается — нужен power cycle.
2. Проверяет разделы через `mtd list` и сеть (`ipaddr`/`serverip`/`gatewayip`,
   `ping 192.168.52.200`).
3. Для каждого компонента: `tftp ${ramdisk_addr_r} <образ>` (адрес
   `0x00e00000`, RAM от 14 МиБ), сверяет `Bytes transferred` с размером файла,
   затем `mtd erase <dev> <offset> <size>` и
   `mtd write <dev> ${ramdisk_addr_r} <offset> <size>`. В этом U-Boot
   `CONFIG_CMD_MTDPARTS` выключен, поэтому `mtd` работает с целым устройством
   (`spi-nand0` из `mtd list`) и смещение/размер задаются явно по таблице:

   | Раздел | Offset | Size |
   |---|---|---|
   | boot | `0x00100000` | `0x00600000` |
   | oem | `0x00700000` | `0x02800000` |
   | userdata | `0x02f00000` | `0x02800000` |
   | rootfs | `0x05700000` | `0x0a800000` |

   Размер образа передаётся в `mtd write` явно: по умолчанию команда пишет
   весь раздел (168 МиБ для rootfs) из RAM.
4. Восстанавливает `sys_bootargs` на NAND-загрузку (`ubi.mtd=6 root=ubi0:rootfs
   rootfstype=ubifs rk_dma_heap_cma=66M`), `saveenv` и `reset`.

Проверка после загрузки: `cat /proc/cmdline` (`root=ubi0:rootfs`), `cat /proc/mtd`
(разметка `boot 6M / oem 40M / userdata 40M / rootfs 168M`), `uname -a`,
`df -h /userdata`. Логи — `output/bench/netboot-<дата>.log`. Проверено на стенде
2026-11-11: записаны `boot`, `oem`, `rootfs`, плата загрузилась с
`root=ubi0:rootfs`, `dr_mode = "host"`, хаб `1a40:0101` и устройство за ним
видны в `lsusb`.

Ограничения: стирание `rootfs` (168 МиБ) в U-Boot занимает несколько минут;
`env`, `idblock` и `uboot` не трогаются (их меняет только полный `update.img`
через Maskrom, §7A); при обрыве питания во время `mtd write` прошивка
повторяется тем же способом (U-Boot остаётся доступен по UART).

### 7-AOV. Экспериментальный профиль `-AOV` (NO-GO, не для выпуска)

Профиль `BoardConfig ...-SC230AI-AOV.mk` (KI-072) меняет idblock и таблицу
разделов: Hpmcu+SPL+DDR+usbplug ~300 КБ, поэтому idblock 1M, а `uboot`,
`boot`, `oem`, `userdata`, `rootfs` сдвинуты на 768K (rootfs 167M). Ядро
линкуется по 0x208000, и `boot.img` собирается FIT-ом `boot-aov.its` из
`Image.gz` (`load = entry = 0x208000`, `compression = "gzip"`) — U-Boot
распаковывает его сразу в 0x208000; обычный zImage по 0x8000 с этим
textofs не грузится. Скрипт `tftp_flash.py` читает раскладку из
`output/image/.env.txt`, поэтому вручную смещения не задаются.

```sh
# собрать профиль (idblock/uboot/boot с FIT/oem/rootfs/meta/update.img)
ln -sfn project/cfg/BoardConfig_IPC/BoardConfig-SPI_NAND-Buildroot-RV1106_Luckfox_Pico_Pro_Max-SC230AI-AOV.mk .BoardConfig.mk
./build.sh kernel && ./build.sh uboot && ./build.sh firmware
# прошить минимум компонентов, дождавшись холодного цикла питания
bench/run.sh tftp_flash.py --components boot --power-cycle
# возврат на продукт: symlink на ...-SC230AI.mk, ./build.sh, затем
# bench/run.sh tftp_flash.py --components env,idblock,boot,oem --power-cycle
```

`--power-cycle` поднимает питание через мост и ловит U-Boot по UART: после
зависшего HPMCU тёплый `reboot` оставляет MCU в неизвестном состоянии.
Если консольный логин недоступен, `tftp_flash.py` сам перезагружает плату
по SSH (`--dut-host`, вендорский пароль). Перед записью `idblock` скрипт
требует SSH-бэкап `/dev/mtdblock1` в `output/bench/backup/`; из
U-Boot-промпта SSH недоступен — тогда только `--skip-backup` и внешний
бэкап/Maskrom. После загрузки проверка: `cat /proc/mtd` (idblock 1M,
rootfs 167M), `grep -o 'mtdparts=[^ ]*' /proc/cmdline`,
`/api/status.sleep.aov_available`,
`cat /sys/devices/platform/rockchip-suspend/aov_state`, в UART —
`Uncompressing GZIP Kernel Image ... to 0x00208000`.

Стенд 2026-11-17 (артефакты `output/bench/aov-image-1m/`,
`output/bench/uar0-aov-fit-boot-20261117.log`): FIT c ядром по 0x208000
загружается с NAND, `/proc/mtd` — idblock 1M / rootfs 167M,
`aov_available=true`, окно `rtos@40000` больше не пересекается с ядром.
Suspend с `aov_mcu=1` доходит до `-h-wait-` → `-h-timeout-` (2000 мс):
MCU не отвечает по mailbox, AP возвращается по таймауту; `aov_mcu=0`
(PMU-only) циклится штатно (`rv1106 enter/exit sleep`). В этих же прогонах
шина I2C3 падает в панику `rk3x_i2c_irq` (KI-073) — до её разбора
`sleep enable` держать `false`, иначе плата уходит в reset на каждом
цикле. Профиль остаётся NO-GO (KI-072).

Перед прогоном sleep на стенде проверьте конфиг приложения: если включены
одновременно `mjpeg`, `jpeg` и `ivs`, старт падает (`[jpeg] No free
channel for JPEG!` → `[hal] Failed to start SDK!`, это KI-069); помогает
`mjpeg.enable: false`. Ложное движение (`min_area` по умолчанию 2) не даёт
таймеру простоя дойти до нуля — `ivs.min_area` допустим в диапазоне [0; 50],
ставьте 50. Отказ `RKAiChn ... ai set sound card failed` → `[hal] Failed to
start SDK!` встречается на этом стенде при `audio.enable: true` (лечится
`audio.enable: false`). Проверка циклов: `/api/sleep` (`idle_s` растёт),
`dmesg | grep -E 'PM: suspend entry|Restarting tasks'`, `aov_state` в
момент простоя должен быть `off`, а строки `-h-wait-`/`-h-timeout-` видны
только на живом UART (печатаются мимо printk).

Наблюдаемость: на прогонах 2026-11-14 мост ESP32 переставал отдавать вывод DUT
живому зеркалу примерно через 7 с после старта ядра, поэтому окно suspend/resume
снималось зеркалом `bench/run.sh serial_exec.py --listen 200`, а не выгрузкой
`uar0.log`. Запись на microSD при этом не прекращается (`uart0_sd_log=1`, на
1.10.0 проверено при отсутствии подключённого клиента), но файл ротируемый, а не
кольцевой: 1 МиБ × 10 копий `uar0.log.1`…`uar0.log.10`, старые редакции теряются —
забирать лог сразу после события и при необходимости склеивать с копиями (§8).
Пул ограничен тремя HTTP-сессиями авторизации (не telnet-клиентами), при
переполнении — `429 Too many active sessions`: `power.py` логинится сам и всегда
делает logout, а `--cookie-file` переиспользует готовую сессию (после
`tftp_flash --power-cycle` cookie гаснет, нужен новый логин).

Кадентность AOV-цикла (KI-072) измеряется без занятия консоли:
`bench/run.sh aov_cadence.py --label keepisp1 --seconds 90` — read-only монитор
моста `:2324` (нужен `telnet0_mon_enable=1` + перезагрузка моста, §8) пишет
поток в `output/bench/aov-test/` и считает `-h-wait-`/`-h-mbox-`/`-h-timeout-`
с дельтами. Монитор нужен потому, что HPMCU делит UART2 с консолью AP.
Состояние на 2026-11-18: при `aov_keep_isp=1` и тихой мете `deep suspend`
не удерживается (≈2.6–2.9 цикла/с, `PMU_WAKEUP_INT_ST=0`, media-IRQ на
границе WFI выключены), при `aov_keep_isp=0` — один вход в сон на
PMU-таймаут (~16 с при окне mailbox 6000 мс; с окном 1000 мс ожидается ~11 с,
отдельно не измерялось). Длинный тихий сон — NO-GO на стороне AP,
AOV = motion-resume; подробности и артефакты — KI-072.

Диагностика resume (KI-074): в `-AOV` можно собрать ядро с фрагментом
`rv1106-pm-debug.config` (по умолчанию он не включён) — появляется
`/sys/power/pm_test`. Загрузка для диагностики:
`setenv sys_bootargs '... no_console_suspend initcall_debug ignore_loglevel'`
в U-Boot, затем лестница
`echo {freezer|devices|platform|processors|core} > /sys/power/pm_test` и
`echo mem > /sys/power/state` (каждый уровень возвращает управление).
Живость проверять по SSH/`/proc/uptime`: во время цикла HTTP divinus
закрыт. После диагностики вернуть штатный `sys_bootargs`
(`ubi.mtd=6 root=ubi0:rootfs rootfstype=ubifs rk_dma_heap_cma=66M`).

Патч I2C (KI-073) входит в ядро этого профиля: в `rk3x_i2c_irq()`
поздний NAK при `i2c->msg == NULL` подтверждается и не разыменовывается.
Паника на I2C3 после патча не воспроизводится (3 прогона), но остаётся
KI-074 — AP зависает на resume сразу после `rv1106 exit sleep`, поэтому до
её разбора `[sleep] enable` держать `false`. Проверка патча — только на
профиле `-AOV` (ядро по 0x208000) и только с холодным циклом питания между
попытками; для контроля можно прошить `output/bench/aov-image-1m/boot.img`
(сборка 10:02, без патча).

### 7-TB. Экспериментальная thunder-boot (wakeup-IPC) линия

Отдельная линия для MCU-порта `battery_ipc_demo_v10-SC230AI-ADC`: busybox
rootfs, разметка `env/idblock/uboot/meta/boot/userdata` (без `oem`/`rootfs`),
MCU в `idblock` (SPL `Hpmcu`), IQ в разделе `meta`.

1. Переключить активный BoardConfig:
   `ln -sfn project/cfg/BoardConfig_IPC/BoardConfig-SPI_NAND-Busybox-RV1106_Luckfox_Pico_Pro_Max-SC230AI-IPC_FASTBOOT.mk .BoardConfig.mk`.
2. `source tools/native/env.sh && ./build.sh` → `output/image/update.img`
   (~15,6 МБ). Проверить: `grep -ac sc230ai output/image/idblock.img` ≠ 0 и
   `dumpimage -l output/image/boot.img` (ramdisk ≤ 20 МБ `ramdisk_r`).
3. **`bench/tftp_flash.py` для этой линии не подходит**: он пишет только
   продуктовые разделы `boot/oem/userdata/rootfs` и не умеет
   `idblock/meta`, а TB-таблица несовместима с продуктовой. Прошивка —
   только полный `update.img` (`upgrade_tool uf`/Maskrom), т.е. по §7A;
   на стенде USB занят хабом, поэтому сначала нужно решение по доступу
   (см. §7A и `KNOWN_ISSUES.md`).
4. Откат: продуктовый `update.img`, собранный до переключения
   (`output/bench/product-update.img`, 72,6 МБ), ставится тем же
   полным способом; после загрузки — `bench/run.sh api_check` и `ui_check`.
5. TB-образ и компоненты текущей сборки сохранены в
   `output/bench/tb-sc230ai-wakeup/` (gitignore).

Ограничение: запись полного `update.img` пересоздаёт `userdata` и `env`
(MAC/статический IP — сохранить заранее, §7-D).

### A. SPI NAND (автономная загрузка)

1. На хосте: `source tools/native/env.sh && ./build.sh firmware`.
2. Результат: `output/image/update.img`.
3. Перевести устройство в Maskrom — `<кнопка / перемычка>`; USB-порт DUT
   подключить к хосту **напрямую** (через хаб из §4 Maskrom не виден).
4. Прошить: `./rkflash.sh` или `tools/linux/Linux_Upgrade_Tool/upgrade_tool`.
5. Перезагрузить; загрузка пойдёт из SPI NAND без хоста.

> **С 2026-11-05 разметка изменена** (`boot` 6 МБ, `oem` 40 МБ, `userdata`
> 40 МБ, `rootfs` 168 МБ). Первая прошивка такой сборки — **только полный
> `update.img`**; покомпонентная выкатка со старой таблицей несовместима.
> Сохраните нужное из `/userdata` и с microSD: раздел `userdata` будет
> пересоздан. Проверка после загрузки: `cat /proc/cmdline` и `cat /proc/mtd`
> (см. §8); том `ubi5_0` должен быть около 29 МиБ (`df -h /userdata`).
>
> Если `reboot loader` на DUT не переводит плату в loader-режим (проверено
> 2026-11-05: обычная перезагрузка), остановите U-Boot по UART
> (`<Ctrl+C>` в окне `Hit key to stop autoboot`; консоль — telnet-мост §4
> или `bench/run.sh netboot.py catch`) и выполните `download`:
> команда отвечает `Enter rockusb failed, fallback to bootrom...`, после чего
> `upgrade_tool ld` видит устройство в режиме `Maskrom`. Дальше — обычный
> `./rkflash.sh update`; кнопка/перемычка Maskrom не нужна.

> **С 2026-11-08 OpenVPN добавляет `CONFIG_TUN=y` (boot) и `openvpn 2.5.7`
> (rootfs).** Изменяются `boot`, `rootfs` и `oem` (divinus), поэтому нужен
> полный `update.img`, а не покомпонентная выкатка. После загрузки проверьте
> `ls -l /dev/net/tun` и `openvpn --version`.

### B. Покомпонентная прошивка изменённых разделов

Режим для выкатки изменённых разделов в SPI NAND без полного образа: прошиваются
только разделы, содержимое которых изменилось, остальные (включая `env`
с MAC-адресом) не трогаются. Уменьшает износ NAND и исключает сброс U-Boot env.

> **Только после переразметки.** Текущая таблица (`boot` 6 МБ, `oem` 40 МБ,
> `userdata` 40 МБ, `rootfs` 168 МБ) сначала выкатывается полным
> `./rkflash.sh update` / `upgrade_tool uf update.img`. Покомпонентная запись
> ниже применима только к устройству, уже прошитому этой таблицей; LBA и
> размеры из старых заметок (`oem 0x2800`, `userdata 0x5000`,
> `rootfs 0x16800`/`0x63000`/`0x69000`) недействительны.

**Маппинг исходников на разделы** (`output/image/*.img` собирает
`./build.sh firmware`):

| Раздел | Что содержит | Источник в сборке |
|---|---|---|
| `env` | U-Boot env (MAC и пр.) | `output/image/env.img` (этой фичей не изменялся) |
| `idblock`, `uboot` | SPL/DDR, U-Boot | `output/image/idblock.img`, `uboot.img` |
| `boot` | ядро + DTB | `output/image/boot.img` |
| `oem` | `/oem/usr/bin/divinus`, `/oem/usr/bin/wg`, `/oem/usr/bin/awg`, `/oem/usr/ko/amneziawg.ko`, `/oem/usr/share/divinus/divinus.yaml`, `/oem/usr/share/iqfiles`, библиотеки | `output/out/oem/` |
| `userdata` | фабричное содержимое раздела; рабочие маркеры и конфиг живут на устройстве | `output/out/userdata/` |
| `rootfs` | корневая ФС, включая `/etc/init.d/S20oko-osd`, `/usr/bin/oko-osd`, `/usr/bin/oko-vpn` и `/etc/init.d/S45oko-vpn` | `output/out/rootfs_uclibc_rv1106/` |

**Порядок:**

1. Перевести устройство в Maskrom (`<кнопка / перемычка>`) либо выполнить на DUT
   `reboot loader` (Rockchip RESTART2); наличие устройства проверить
   `upgrade_tool ld`. На прошивке 2026-11-05 `reboot loader` завершился обычной
   перезагрузкой (`upgrade_tool ld` → `List of rockusb connected(0)`), поэтому
   без кнопки Maskrom покомпонентная запись недоступна.
2. Загрузить loader в RAM и проверить таблицу разделов:
   ```sh
   ./tools/linux/Linux_Upgrade_Tool/upgrade_tool db output/image/download.bin
   ./tools/linux/Linux_Upgrade_Tool/upgrade_tool pl   # LBA и размеры в секторах 512 B
   ```
3. Прошить только изменённые разделы. `./rkflash.sh oem` / `rootfs` использует
   `upgrade_tool di -oem/-rootfs`, но в чистом Maskrom (до `db`) таблицы разделов
   нет, и он отвечает `parameter is invalid,please check!`. Рабочий путь —
   запись по LBA с **полным стиранием раздела** (после `db` таблицу можно
   проверить через `upgrade_tool pl`):
```sh
    ./tools/linux/Linux_Upgrade_Tool/upgrade_tool wl 0x800  0x3000 output/image/boot.img
    ./tools/linux/Linux_Upgrade_Tool/upgrade_tool wl 0x3800 0x14000 output/image/oem.img
    ./tools/linux/Linux_Upgrade_Tool/upgrade_tool wl 0x2B800 0x54000 output/image/rootfs.img
    ./tools/linux/Linux_Upgrade_Tool/upgrade_tool rd
    ```
    Второй аргумент `wl` — размер **всего раздела** в секторах (boot 6M = `0x3000`,
    oem 40M = `0x14000`, rootfs 168M = `0x54000`), а не длина файла: tool стирает
    этот диапазон и пишет образ. Если передать длину образа (boot `0x391400/512`
    ≈ `0x1C8A`, oem `0x9D00`, rootfs `0x1A500`), в хвосте останутся старые
    UBI-PEB с другим `image_seq`, и ядро упадёт с
    `ubi0 error: ubi_attach: bad image sequence number …` (panic).
    Maskrom нужен один раз: после `db` устройство остаётся в loader-режиме,
    поэтому следующие разделы прошиваются без повторного входа.
    `userdata` (LBA `0x17800`, 40M = `0x14000`) — только если менялось фабричное
    содержимое раздела.
4. `idblock`, `uboot` и `env` этой фичей не менялись. `boot` перепрошивается, когда
   меняются ядро, DTB или его конфиг (например, `rv1106-netboot.config`). При
   необходимости прошивайте `idblock`/`uboot` полным `./rkflash.sh update` (§7A)
   или отдельной проверенной процедурой; `di -b`/`di -uboot` в чистом Maskrom так
   же требуют загруженного через `db` loader.
5. Полный `./rkflash.sh update` (`upgrade_tool uf update.img`) — **только** при
   первичной прошивке, смене таблицы разделов (`parameter.txt`), loader
   (`idblock`/`uboot`) или U-Boot env. Он затирает и неизменённые разделы.
6. Проверить после загрузки: чек-лист §8 (`/oem/usr/bin/divinus`,
   `/etc/init.d/S20oko-osd`, OSD в RTSP).

После перезаписи разделов при необходимости отключите и подключите USB-OTG
на 5–10 с: `rd` не всегда выводит плату из loader-состояния, а BootROM при
повторном подключении снова показывает устройство как Maskrom.

**Как определить, что изменилось:** сравнить хеши собранных образов с последним
выкатанным комплектом (вести на хосте файл `.flashed.sha256`):
```sh
sha256sum output/image/*.img > /tmp/now.sha256
diff /tmp/now.sha256 .flashed.sha256   # прошивать только различающиеся разделы
```
Альтернатива без Maskrom — сырые `mtd erase/write` из `output/image/sd_update.txt`
(смещения и размеры по разделам, выполняется из U-Boot). Файл и образы кладутся
в FAT-раздел карты; `sd_update.txt` должен оставаться **без** строки `reset`:
после записи U-Boot вернётся в консоль, и карту/файл нужно убрать до следующей
загрузки. Если оставить `sd_update.txt` (или добавить `reset`), U-Boot будет
перепрошивать NAND при каждом старте.

### C. STM8: первичная прошивка через ST-Link

STM8S103F3P6 на плате не имеет USB; первая запись OKO-загрузчика и приложения
делается ST-Link/V2 по SWIM (PD1), который подключён к USB хоста. Питание
платы при этом должно быть включено (общий GND), I2C на DUT можно не
останавливать: в худшем случае `firmware_status` временно покажет `absent`.
`stm8flash` не входит в дистрибутив и требует libusb; проверенный способ без
root — собрать и запустить его в привилегированном контейнере с пробросом USB.
Готовые образы: `factory-<ver>.ihx` — загрузчик + приложение (первичная
запись), `app-<ver>.ihx` — только приложение (для `/api/flash` и
`oko-motor flash`). На стенде 2026-11-21 используется тестовая сборка
`factory-2.3.2.ihx` (продуктовая — `factory-2.2.0.ihx`).

```sh
git clone --depth 1 https://github.com/vdudouyt/stm8flash /tmp/kilo/stm8flash
docker run --rm --privileged \
  -v /dev/bus/usb:/dev/bus/usb \
  -v /tmp/kilo/stm8flash:/src -v "$PWD:/work" -w /src \
  debian:bookworm-slim bash -lc '
    apt-get update -qq &&
    apt-get install -y -qq build-essential libusb-1.0-0-dev pkg-config &&
    make clean && make RELEASE=1 &&
    ./stm8flash -c stlinkv2 -p stm8s103f3 -s flash \
      -w /work/project/app/stm8-motor/build/factory-2.3.2.ihx &&
    ./stm8flash -c stlinkv2 -p stm8s103f3 -s flash \
      -v /work/project/app/stm8-motor/build/factory-2.3.2.ihx &&
    ./stm8flash -c stlinkv2 -p stm8s103f3 -R'
```

`factory-<ver>.ihx` содержит загрузчик `0x8000–0x89FF` и приложение
`0x8A10–0x9FFF`; EEPROM (позиции моторов) не стирается. **После записи
обязателен `stm8flash -R`**: без явного сброса ядро остаётся остановленным
(stm8flash оставляет SWIM-stall) и плата не отвечает по I2C до сброса.
Проверка после записи — `oko-motor -b 3 -a 0x60 version` (ожидается `2.3.2`
для тестовой платы, `2.2.0` для продуктовой), `sleep show` (тестовая сборка:
`sleep on, slice 1 ms`), затем `enter-boot` → `info` → `boot-reset`.
Восстановление при неудачной записи — тем же `factory-<ver>.ihx` (худший
случай — откат на `factory-2.1.1.ihx`). Диагностика: `-s opt -r` читает
option bytes (см. KI-075 — защита записи не выставляется).
Обновление приложения по I2C (`/api/flash` или `oko-motor flash app-<ver>.ihx`)
на стендовом экземпляре не работает: его IAP-стирание блока не завершается
(KI-075, дефект чипа — там же второй отказ, AWU/KI-077; ожидание `PUL` и
`FLASH_CR1.HALT`/`AHALT` проверены и отклонены 2026-11-22, см. артефакты
`output/bench/stm8/ki075-*experiment-20261122.txt`). Загрузчик `1.1.0`
больше не маскирует отказ: `oko-motor flash` возвращает `RC=1` и печатает
`bootloader did not complete command 0x11…`, версия не меняется.
Единственный путь обновления на этой плате — ST-Link/SWIM.

### D. Переключение NAND ↔ NFS через env U-Boot

Штатный `boot.img` (SC230AI) уже содержит фрагмент `rv1106-netboot.config`
(`CONFIG_IP_PNP`, `IP_PNP_DHCP`, `IP_PNP_BOOTP`, `CONFIG_ROOT_NFS`), поэтому
режим корня переключается только переменной U-Boot `sys_bootargs` — без
перепрошивки ядра и без отдельного BoardConfig. В NFS-режиме `oem`/`userdata`
монтируются из `<nfsroot>/.nfsroot/`, DHCP в `oko-lunch.sh` пропускается.

1. Поднять сервисы (Sudo не нужен; нужны Docker и членство в группе `docker`):
   ```sh
   tools/native/setup_netboot.sh
   ```
   Скрипт собирает и запускает контейнер `services/` с `network_mode: host`:
   TFTP отдаёт `output/image` (`/srv/tftp`, chroot, `--foreground --secure`),
   NFS-сервер экспортирует `output/srv/nfs/rootfs` для `192.168.52.0/24`.
   Внутри контейнера путь экспорта — `/srv/nfs/rootfs`, поэтому строки
   `nfsroot=` на DUT не меняются. Логи — `docker logs -f oko-services`;
   раскладка и оговорки — `services/README.md`. Если на хосте ещё работают
   пакетные `tftpd-hpa`/`nfs-kernel-server`, сначала освободите порты
   (раздел «Переход с хостовых сервисов» в `services/README.md`), иначе
   контейнер не сможет занять 69/111/2049/20048.
2. Разложить rootfs и разделы в экспорт (бэкап — tar в `output/bench/nfs/`):
   ```sh
   rsync -a --delete --exclude=/.nfsroot output/out/rootfs_uclibc_rv1106/ output/srv/nfs/rootfs/
   rsync -a --delete output/out/oem/ output/srv/nfs/rootfs/.nfsroot/oem/
   rsync -a --delete output/out/userdata/ output/srv/nfs/rootfs/.nfsroot/userdata/
   ```
   Если rsync упирается в root-only каталоги экспорта (`var/lib/seedrng`),
   выполните те же команды через `sudo rsync` от root.
3. Переключить корень и проверить после перезагрузки:
   ```sh
   bench/run.sh nfs_env.py status --host 192.168.52.33   # режим и адрес до переключения
   bench/run.sh nfs_env.py on  --host 192.168.52.33      # NFS-root
   bench/run.sh nfs_env.py off --host 192.168.52.150     # возврат на ubi0:rootfs
   ```
   `on` пишет `sys_bootargs` с `root=/dev/nfs … ip=192.168.52.150:…:off`,
   перезагружает плату и проверяет `/`, `/oem`, `/userdata` (nfs3 с
   `192.168.52.200`); `off` пишет строку
   `ubi.mtd=6 root=ubi0:rootfs rootfstype=ubifs rk_dma_heap_cma=66M` и проверяет
   `root=ubi0:rootfs` и `ubifs`. Логи — `output/bench/nfs-env-<дата>.log`.
   Строки и адреса — константы `bench/nfs_env.py` (`OKO_NFS_IP`,
   `OKO_NAND_HOST`, `OKO_TFTP_HOST`, `OKO_GATEWAY`, `OKO_NFSROOT`,
   `OKO_CMA_SIZE`); при смене разметки или CMA обновите их.
4. Итерации ядра без прошивки (`netboot.py`) по-прежнему возможны: скрипт кладёт
   `output/image/boot.img` по TFTP и задаёт `sys_bootargs` только в RAM U-Boot,
   не сохраняя его в env.

**Контейнер и NFS-root.** Останавливать контейнер `services/` при смонтированном
на DUT NFS-root нельзя: клиент теряет `/` и зависает. Если контейнер уже
остановлен или пересобирается, сначала верните DUT на NAND по процедуре ниже
(через UART), затем поднимайте сервисы. Порты 69/111/2049/20048 занимает
контейнер; хостовые `tftpd-hpa`, `nfs-kernel-server` и `rpcbind` должны быть
остановлены и удалены.

**Аварийное восстановление.** Неверная строка `sys_bootargs` (`nfsroot`
недоступен) приводит к `VFS: Unable to mount root fs via NFS` и panic. Если в
строке нет `panic=`, ядро зависает и нужен сброс питания. Проверенная процедура
(2026-11-08, воспроизведено с несуществующим экспортом и `panic=5`):

```sh
# 1. поймать U-Boot по UART (Ctrl-C), 2. вернуть NAND-строку и сохранить env:
bench/run.sh netboot.py cmd "setenv sys_bootargs 'ubi.mtd=6 root=ubi0:rootfs rootfstype=ubifs rk_dma_heap_cma=66M'"
bench/run.sh netboot.py cmd 'printenv sys_bootargs'
bench/run.sh netboot.py cmd 'saveenv'
bench/run.sh netboot.py cmd 'reset'
```

Команды `setenv`/`saveenv`/`reset` отправляйте по одной: длинная строка в одной
команде с `;` обрезается буфером U-Boot. Если плата зависла (panic без
`panic=N`), сначала снимите и подайте питание, затем ловите U-Boot.

Логи — `output/bench/netboot-<дата>.log`. Скорость NFS в этом режиме ограничена
`rsize/wsize=4096` для `/` (параметры `nfsroot`), `oem`/`userdata` монтируются
с `rsize/wsize=1048576`; для замеров производительности используйте SPI NAND.

### E. Обновление через WebUI (OTA)

Пакет `.okp` собирается на хосте из образов последней сборки и обновляет
`boot`, `oem`, `rootfs` через microSD; SPI NAND из Linux не пишется.

1. Хост: `tools/native/make_oko_update.sh --version 0.4.0`. Версия должна
   совпадать с `/etc/oko-release` собираемого образа (сейчас `0.4.0`),
   иначе пост-проверка после прошивки сообщит `version-mismatch`.
2. WebUI → «Система» → «Обновление ПО»: выбрать файл или указать URL
   (`http://` или `https://`), «Загрузить пакет» / «Скачать и подготовить».
   Прогресс — в карточке; `GET /api/update` отдаёт состояние и хеши.
3. Убедиться, что пакет проверен (`staged.ok=true`). До подтверждения пакет
   на карте безопасен: каждая строка `sd_update.txt` обёрнута в
   `if env exists oko_update; then … fi`, и флаг выставляет только `apply`.
   Ввести `APPLY` и нажать
   «Прошить и перезагрузить». Устройство уйдёт в перезагрузку, U-Boot
   прошьёт разделы (около 2–3 минут), затем загрузится Linux.
4. После возврата `GET /api/update` должен показать новую версию в
   `current_version` и `last.ok=true`; карта очищена, повторной прошивки при
   следующей загрузке нет. Проверить `df -h /userdata` — раздел не менялся.

Отрицательные проверки: apply без токена или без `confirm=APPLY` — 400/409;
повторный POST во время работы — 409; повреждённый пакет — `staged.ok=false`,
apply отклоняется; карта без пакета или без `sd_update.txt` — прошивки нет;
обрыв питания во время прошивки — SD остаётся в слоте, U-Boot повторяет
запись при следующей подаче питания.

> **Проверено на стенде 2026-11-10.** Полный цикл: upload 61,8 МБ →
> `staged.ok=true` → `APPLY` → перезагрузка → U-Boot `sd_update` стёр и
> записал `boot`/`oem`/`rootfs` → Linux загрузился, `S20oko-update` записал
> `ok=1`, карта очищена. `mmc 1` — слот microSD (`mmc list`: `1 (SD)`),
> `fatload` из `sd_update` читает FAT; `/oem` после полного `mtd erase`
> монтируется штатным `S20linkmount`; HTTPS (`curl https://example.com`)
> работает. Флаг `oko_update` виден U-Boot, потому что ключ добавлен в
> `CONFIG_ENVF_LIST` (`oko_mac`/`oko_net_*` — тоже, чтобы `env save` не терял
> сетевые настройки); `S20oko-update` снимает флаг после загрузки. Остальные
> негативные проверки — как описано выше.

## 8. Диагностика

Чек-лист после загрузки (по SSH `root@<DHCP-адрес DUT>` или UART; фактический
адрес — в §5):

```sh
# маркер тестовой сборки (S99oko-test)
cat /tmp/oko-test-ok

# модули медиастека и сенсора
lsmod | grep -E 'sc230ai|rockit|rga3|rknpu|video_rkisp'

# замена драйвера NPU без перепрошивки (этап A: 0.9.2 -> 0.9.8)
# baseline сохранён в /userdata/rknpu-baseline-0.9.2.ko
killall divinus
rmmod rknpu
insmod /userdata/rknpu-0.9.8.ko
dmesg | grep -i rknpu | tail -3      # ожидается v0.9.8 и non-iommu mode
cd /userdata && LD_LIBRARY_PATH=/oem/usr/lib setsid /oem/usr/bin/divinus \
  >/tmp/divinus.log 2>&1 < /dev/null &
curl -s -u admin:oko-change-me http://127.0.0.1/api/status
# killall/stop = не graceful: divinus вернёт divinus.yaml.bak (откат настроек).
# Мягкий рестарт без отката: kill -HUP $(pidof divinus)
# откат модуля: killall divinus; rmmod rknpu; insmod /userdata/rknpu-baseline-0.9.2.ko
# штатная загрузка берёт /oem/usr/ko/rknpu.ko; кандидат ставится туда же
# только после soak (cp /userdata/rknpu-0.9.8.ko /oem/usr/ko/rknpu.ko)

# сенсор определился, формат кадра читается
cat /proc/rkisp-vir0

# режим сенсора: 1920x1080@10000/300000 (30 fps)
media-ctl -p | grep -A6 'sc230ai 4-0032' | grep fmt

# STM32 EC: ошибки шины I2C3 и форензика (после загрузки)
dmesg | grep -c 'SCL hold by slave'          # ожидается 0
dmesg | grep 'rk3x-i2c' | tail -5            # timeout, ipd: 0x80 => slave держал SCL
/usr/sbin/i2cdetect -y -r 3 | grep 42        # 0x42 отвечает
/oem/usr/bin/oko-ec info                     # device_type 2, proto 3

# AOV-профиль (экспериментальный): интерфейс, meta, состояние
ls /sys/devices/platform/rockchip-suspend/aov_*
cat /sys/devices/platform/rockchip-suspend/aov_wakeproxy
cat /sys/devices/platform/rockchip-suspend/aov_state
ls -l /oem/usr/share/oko/meta.img            # 393216 Б, RKMT, load 0x01000000
curl -s -u admin:oko-change-me http://127.0.0.1/api/sleep
# перед AOV-циклом: dmesg -c; после resume: dmesg | grep -iE 'sfc|ubi.*error'
# KI-071: при sfc timeout/ro-rootfs — power cycle, при повторе — перепрошивка

# Счётчики DEBUG_BUILD (-DEC_DEBUG_BUILD=ON): 60 байт ответа 0x28
/usr/sbin/i2ctransfer -y 3 w3@0x42 0x01 0x00 0x28
sleep 0.2
/usr/sbin/i2ctransfer -y 3 r63@0x42          # timeout_fires/bus_hang_events в payload

# Воспроизведение H1 и проверка таймерного отпускания линии
/usr/sbin/i2ctransfer -y 3 w4@0x42 0x02 0x00 0x29 0x01
/usr/sbin/i2ctransfer -y 3 r63@0x42 || true  # ожидается один timeout, затем шина жива
/usr/sbin/i2ctransfer -y 3 w4@0x42 0x02 0x00 0x29 0x00

# Soak-матрица (отчёт в output/bench/ec_bus_soak-*.json)
bench/run.sh ec_bus_soak.py --mode all --iterations 20
# Проверено 2026-11-13 на C011 (debug 9172 Б): hold-scl ~10 мс,
# 40 ребутов (30 read-window 0…150 мс) — 0 'SCL hold by slave'.

# Если EC завис в Stop и SWD не подключается (init mode failed):
# нужен NRST/снятие always-on 3.3 В; см. docs/hardware.md §8.

# проверка потока (divinus должен быть остановлен): ожидается 30.0 fps
v4l2-ctl -d /dev/video0 --stream-mmap --stream-count=60 --stream-poll

# WebUI и API после прошивки: 103 проверки API и headless-проверка разделов
bench/run.sh api_check.py
LD_LIBRARY_PATH=/tmp/kilo/libasound/usr/lib/x86_64-linux-gnu \
  output/bench/venv/bin/python bench/ui_check.py   # Playwright, см. VERIFIED_FUNCTIONS.md §3

# часовой пояс (только чтение): /etc/TZ — относительный симлинк на зону uclibc,
# его ставит образ или S20okotz из system.tz в /userdata/divinus.yaml
readlink /etc/TZ                     # ожидается ../usr/share/zoneinfo/uclibc/<зона>
date                                 # локальное время выбранной зоны
date -u                              # UTC: разница равна смещению зоны
ls /usr/share/zoneinfo/uclibc        # установленный набор зон
hwclock -u                           # RTC идёт в UTC; вывод локальный, совпадает с date

# контроллеры: версии, роль STM8, загрузчик
curl -s -u admin:oko-change-me http://192.168.52.33/api/firmware
/oem/usr/bin/oko-motor -b 3 -a 0x60 version
/oem/usr/bin/oko-motor -b 3 -a 0x60 info
/oem/usr/bin/oko-ec info

# AF-метрика: 20 отсчётов; для SC230AI нужен contrast_af.enable=1 в IQ
curl -s -X POST -u admin:oko-change-me 'http://192.168.52.33/api/focus?enable=1&motor=1'
bench/run.sh focus_check.py --seconds 20 --interval 1 --snapshot
# автофокус по контрастной сцене (opt-in): full + ROI + micro-fine (af=fine),
# кривые в output/bench/
OKO_FOCUS_MOTOR_TESTS=1 OKO_FOCUS_AF_TESTS=1 bench/run.sh api_check.py

# VPN: модуль по требованию, утилиты и профили
lsmod | grep amneziawg
/oem/usr/bin/awg version
/usr/bin/oko-vpn status
curl -s -u admin:oko-change-me http://192.168.52.33/api/vpn
# полный прогон с пиром на хосте (см. bench/README.md, «VPN-пир на хосте»)
bench/run.sh vpn_check.py --peer-host 192.168.52.1 \
  --profile output/bench/vpn/awg0.conf

# прошивка через WebUI: негативные проверки (400) и реальный STM8
bench/run.sh firmware_check.py --negative
bench/run.sh firmware_check.py --flash --target stm8 \
  --file project/app/stm8-motor/build/app-2.1.1.ihx --confirm FLASH

# NPU в OSD: строка должна появиться после старта YOLO
# ожидаемый файл: CPU 20% NPU 17%\nMEM FREE 77% SD FREE 14.8/14.8GiB\nT 65.3C
cat /tmp/osd-sys.txt; cat /proc/rknpu/load

# Wi-Fi в OSD: реальный RSSI, wifi ap (SoftAP), wifi off или скрытое поле
wpa_cli -i wlan0 status | grep wpa_state
wpa_cli -i wlan0 signal_poll | grep RSSI
ps w | grep "[h]ostapd"; ip -4 addr show dev wlan0 | grep inet
cat /tmp/osd-dyn.txt; wc -c /tmp/osd-dyn.txt
#   ожидается: WiFi <n>/5 <dBm>dBm при COMPLETED; wifi ap при работающем
#   SoftAP (hostapd + IPv4); wifi off без ассоциации и без AP; после
#   переподключения USB без re-apply — wifi off, не wifi ap; без
#   /sys/class/net/wlan0 — три строки без Wi-Fi, <255 байт

# RGA im2d fd-путь: копирование через CMA-буфер (rc=1 — нет /data/in0...bin,
# это нормально; зависание/чёрный экран — откат на CPU и power cycle)
LD_LIBRARY_PATH=/oem/usr/lib /oem/usr/bin/rgaImDemo --copy
# конверсия YOLO на RGA: в логе не должно быть RGA_BLIT/falling back
curl -s -u admin:oko-change-me -X POST 'http://192.168.52.33/api/yolo?convert=rga'
grep -aE 'rk_rga|RGA_BLIT|falling back' /tmp/divinus.log | tail

# рамки детекции на потоке (2BPP RGN 12, слой 6): включение на лету
curl -s -u admin:oko-change-me -X POST 'http://192.168.52.33/api/yolo?boxes=1'
grep -aE 'NN overlay region 12' /tmp/divinus.log | tail -1
#   ожидается: "1920x1088 2BPP on 1 channel(s), layer 6"
# для гарантированных детекций на статичной сцене: conf_threshold=0.05,
# затем кадры RTSP и проверка красных/синих линий по краям рамок
curl -s -u admin:oko-change-me -X POST 'http://192.168.52.33/api/yolo?conf_threshold=0.05'
bench/run.sh osd_check.py --ip 192.168.52.33 --frames 3 --interval 1 --seconds 4
# выключение: регион уничтожается, кадры остаются чистыми
curl -s -u admin:oko-change-me -X POST 'http://192.168.52.33/api/yolo?boxes=0'
# восстановить: conf_threshold=0.35, boxes=1, /api/cmd?save=1

# RTSP отдаёт кадры при rtsp.enable=true и работающем VENC-канале
# (mp4.enable или substream.enable). stream.enable к RTSP отношения не имеет:
# он включает только UDP/RTP-раздачу.
grep -A2 '^stream:' /userdata/divinus.yaml
grep -A2 '^rtsp:' /userdata/divinus.yaml

# новая разметка: boot 0x600000, oem 0x2800000, userdata 0x2800000, rootfs 0xA800000
cat /proc/cmdline
cat /proc/mtd

# microSD: состояние, mount/unmount и формат (формат уничтожает данные карты)
curl -s -u admin:oko-change-me http://192.168.52.33/api/storage
curl -s -u admin:oko-change-me 'http://192.168.52.33/api/storage?action=unmount&token=<token>'
# без карты start=1 должен вернуть blocked и не создать файл в /oem или /userdata
curl -s -u admin:oko-change-me 'http://192.168.52.33/api/record?enable=true&start=1'
# с картой: format -> один раздел FAT32 OKO, затем запись 5 с и проверка MP4
# token берётся из ответа /api/storage (поле format_token)
curl -s -u admin:oko-change-me \
  'http://192.168.52.33/api/storage?action=format&fs=fat32&confirm=FORMAT&token=<token>'
ls -l /mnt/sdcard
ls /oem/usr/ko | grep -E 'sc3336|mis5001|imx415|mia1321'   # пусто
ls /oem/usr/share/iqfiles                                   # только sc230ai_*.json
ls /oem/usr/bin/sample_* /oem/usr/bin/simple_* 2>/dev/null  # пусто

# I2C3-M1: 100 kHz = 0x000186a0
hexdump -C /sys/class/i2c-adapter/i2c-3/of_node/clock-frequency

# помощь фокусировке: AF-статистика (A6), мотор 0x60 и NPU в OSD
bench/run.sh focus_check.py
bench/run.sh focus_check.py --enable --polls 10
curl -s -u admin:oko-change-me 'http://192.168.52.33/api/focus?map=1'
curl -s -u admin:oko-change-me 'http://192.168.52.33/api/focus' | grep -o '"af":{[^}]*}'
i2cdetect -y 3 | grep -E ' 42 | 60 '
# вторая строка несёт сводку SD: MEM FREE 77% SD FREE 14.8/14.8GiB
cat /tmp/osd-sys.txt
oko-motor info
# прошивки контроллеров (без --flash — только версии)
bench/run.sh firmware_check.py
# STM32: проверка перехода в загрузчик без записи (образ не меняется)
/etc/init.d/S21oko-ec stop
/oem/usr/bin/oko-ec info            # device_type=2 (application)
/oem/usr/bin/oko-ec enter-update    # 0x0E, пауза 3 с
/oem/usr/bin/oko-ec info            # device_type=1 (bootloader)
/oem/usr/bin/oko-ec reset
sleep 2
/oem/usr/bin/oko-ec info            # снова device_type=2, версия/ячейки
/etc/init.d/S21oko-ec start
# STM32: реальная прошивка (CBT6, слот 120 КиБ, версия 1.0.0 = rollback)
bench/run.sh firmware_check.py --flash --target stm32 \
  --file output/bench/stm32/app-1.0.0.bin --confirm FLASH
# ожидается job done; затем /api/firmware, oko-ec info/battery, power-cycle
# STM8: первая запись factory-2.1.1.ihx по ST-Link, далее через WebUI/oko-motor
ls -l project/app/stm8-motor/build/app-2.1.1.ihx \
      project/app/stm8-motor/build/factory-2.1.1.ihx

# Ethernet: MAC и настройки в env-разделе U-Boot
fw_printenv oko_mac oko_net_mode oko_ipaddr oko_netmask oko_gateway oko_dns
oko-net env
curl -s -u admin:oko-change-me http://192.168.52.33/api/network
# статический режим (сменить адрес на свободный; страница отключится)
curl -s -u admin:oko-change-me \
  'http://192.168.52.33/api/network?mode=static&ipaddr=192.168.52.220&netmask=255.255.255.0&gateway=192.168.52.254&dns=192.168.52.254'
# после переподключения на .220 вернуть DHCP:
curl -s -u admin:oko-change-me \
  'http://192.168.52.220/api/network?mode=dhcp&ipaddr=192.168.52.220&netmask=255.255.255.0&gateway=192.168.52.254&dns=192.168.52.254'
# MAC/IP должны сохраняться после reboot; DHCP-клиент — только dhcpcd.
# Полный update.img перезаписывает env: static-настройки задать заново,
# MAC восстановится из serial.
ps | grep dhcpcd | grep -v grep

# приложение запущено: divinus — единственный стример (rkipc вырезан из образа)
ps | grep -E 'divinus|rkipc'
ls -l /oem/usr/bin/divinus /userdata/divinus.enable
ls -l /userdata/divinus.yaml
# проверка вырезанного состава (на хосте, после сборки)
tools/native/check_image_contents.sh
curl -s -u admin:oko-change-me http://192.168.52.33/api/status
curl -s -u admin:oko-change-me -o /tmp/snap.jpg http://192.168.52.33/image.jpg
# OSD: time — bottom-right, telemetry (4 lines) — top-left, GPS — top-right,
# msg — top-left below telemetry.
# Смотреть в RTSP-потоке (кадры `/image.jpg` кодируются отдельным VENC-каналом
# и OSD не содержат — ограничение RGN на RV1106).
# Два кадра с интервалом >1 с: меняется время; >16 с: телеметрия и GPS.
# На DUT (не с хоста): сервис сообщений — локальный FIFO.
echo 'OKO MESSAGE 123' > /tmp/osd.fifo
# через 60+ с reg3 гаснет: /tmp/osd-msg.txt пуст, /tmp/osd-msg.ts удалён
ls -l /tmp/osd-dyn.txt /tmp/osd-msg.txt /tmp/osd.fifo
# OSD с хоста: попарный диф регионов по RTSP, кадры и кропы в output/bench/
bench/run.sh osd_check.py --ip 192.168.52.33 --seconds 17
# геометрия оверлеев и ePTZ: кроп, /mjpeg и /image.jpg, рестарт MP4
bench/run.sh geometry_check.py --what zoom
# NN-рамки и OSD под 0/90/180/270 — opt-in: модель, объект в кадре, штатив
OKO_GEOMETRY_TESTS=1 bench/run.sh geometry_check.py --what boxes
OKO_GEOMETRY_TESTS=1 bench/run.sh geometry_check.py --what osd
# сообщение в reg2: сначала на DUT, затем повторить osd_check.py
bench/run.sh serial_exec.py "echo 'OKO MSG TEST' > /tmp/osd.fifo"
# RTSP-корень divinus — без /live/0:
# ffprobe -rtsp_transport tcp rtsp://admin:oko-change-me@192.168.52.33:554/

# I2C3-M1 (GPIO1_D2/GPIO1_D3, 100 кГц) включён по умолчанию
ls -l /dev/i2c-3
# шина должна быть доступна; отсутствие устройств на ней — норма
# pinmux: оба вывода принадлежат ff460000.i2c, группа i2c3m1-xfer;
# GPIO2_A6/GPIO2_A7 (I2C3-M0) в I2C3 не переведены
mount -t debugfs none /sys/kernel/debug 2>/dev/null
grep "function i2c3" /sys/kernel/debug/pinctrl/*/pinmux-pins

# oko-ec: шина и три устройства (0x40 SHT21, 0x41 INA3221, 0x42 STM32 EC)
# i2c-tools в образ не входит, список устройств даёт scan
oko-ec scan
oko-ec info          # device_type: 2, proto 2, cells 3
oko-ec battery
oko-ec ina226        # INA3221: канал 1 по умолчанию — DRV8833 + двигатели
oko-ec --ina-channel 2 ina226   # CH2 — STM32 ACPI-EC
oko-ec --ina-channel 3 ina226   # CH3 — RV1106 + STM8 (основное потребление)
oko-ec sht21         # температура и влажность
oko-ec sensors
# демон и файлы состояния (OSD берёт из них реальные T/BAT)
ps | grep oko-ec
cat /tmp/oko-ec.json
cat /tmp/oko-ec.osd
/etc/init.d/S21oko-ec status
# opt-out: touch /userdata/oko-ec.disable && reboot

# сеть
ifconfig eth0
ip route

# разделы смонтированы из SPI NAND (ubifs)
mount | grep -E ' /oem | /userdata '

# логи
ls -l /tmp/*.log /mnt/sdcard/core-* 2>/dev/null
dmesg | tail -50
```

Типовые проблемы:

| Симптом | Вероятная причина |
|---|---|
| Нет загрузки после прошивки | не прошит `rootfs`/`oem`, повреждён UBI (неполное стирание раздела), неверная таблица разделов |
| `ubi0 error: ubi_attach: bad image sequence number` | раздел записан по длине образа, а не по полному размеру (`wl` вторым аргументом передан неверный размер) |
| `divinus` не стартует (единственный стример) | нет `/userdata/divinus.enable` (не выполнился `S20oko-osd`), нет `/oem/usr/bin/divinus` (сборка пакета упала молча), нет IQ-файла сенсора, `dlopen` не находит `librockit.so`/`librkaiq.so` (`LD_LIBRARY_PATH` без `/oem/usr/lib`) |
| `divinus`: нет картинки, AIQ-ошибки | геометрия в `divinus.yaml` не совпадает с режимом сенсора (SC230AI — 1920x1080), IQ не найден по имени модуля из `/proc/rkisp-vir0` |
| `divinus`: `mmap part 1 failed!`, пустые JPEG/RTSP | в сборке не применён патч `compat.c` (ABI `mmap`): на uClibc hard-float не должен определяться собственный 64-битный `mmap`; см. `project/app/divinus/README.md` |
| OSD: нет телеметрии/сообщений, `@/tmp/...` виден как текст | не применён патч `region.c` (`@file`-провайдер) или не запущен `oko-osd`: `ps` не показывает процесс, `/etc/init.d/S20oko-osd status` сообщает `not running`, файлов `/tmp/osd-*` нет |
| OSD: шрифт не найден | в `regN_font` указан отсутствующий шрифт; в rootfs есть только `/oem/usr/share/simsun_en.ttf` (`regN_font: simsun_en`) |
| `/userdata/divinus.yaml` перезаписан после загрузки | ожидаемо: `S20oko-osd` заменяет конфиг без `reg0_text`/`reg0_size`/`reg0_color`/`reg3_text` фабричным (старый формат); первый прежний файл сохраняется как `divinus.yaml.bak` |
| `check_image_contents.sh` сообщает об уцелевшем пути | инкрементальная сборка не применила prune-список: пересобрать `./build.sh firmware` (или полную сборку) и повторить проверку |
| Нет `/dev/video*` | `insmod_ko.sh` не выполнился или `sc230ai.ko` отсутствует |
| `oko-ec scan` не находит `0x40`/`0x41`/`0x42` | нет внешних pull-up на `GPIO1_D2`/`GPIO1_D3`, неверный pinmux (I2C3-M1 вместо M0) или INA3221 с другой перемычкой `A0` (ожидается `0x41`) |
| `oko-ec ina226` показывает нули | силовые входы INA3221 (`VIN+`/`VIN-`, `VBUS`) не подключены к нагрузке/шине; обмен по I2C при этом работает. Нули на CH1/CH2 при простое — норма (двигатели обесточены, STM32 в малом потреблении), рабочие значения смотреть на CH3 |
| `oko-ec ina226` показывает не тот канал | каналы INA3221: CH1 — DRV8833 + двигатели, CH2 — STM32, CH3 — RV1106 + STM8 (см. §4); выбор — `--ina-channel 1..3` (утилита по умолчанию 1, демон/OSD — 3, `INA_CHANNEL` в `S21oko-ec` или `OKO_EC_INA_CHANNEL`) |
| `oko-ec battery` пишет, что устройство в загрузчике | STM32 в режиме загрузчика (`device_type = 1`); выполнить `oko-ec reset` и повторить |
| `/tmp/oko-ec.osd` не появляется | не запущен `S21oko-ec` (нет `/dev/i2c-3` или `/oem/usr/bin/oko-ec`), есть `/userdata/oko-ec.disable`, либо демон уже работает (см. `/tmp/oko-ec.log`) |

### USB host и устройства за хабом

Порт закреплён в host-режиме (`dr_mode = "host"`): UDC и gadget не
регистрируются, `usb0`/adb отсутствуют, роль не переключается.

```sh
# режим из DTB и отсутствие gadget
cat /proc/device-tree/usbdrd/usb@ffb00000/dr_mode       # host
ls /sys/class/udc                                       # пусто
ifconfig usb0                                           # нет интерфейса

# хаб и устройства за ним
lsusb
dmesg | grep -iE 'usb|dwc3|xhci' | tail -30

# HID (клавиатура/мышь) за хабом
ls /dev/input/event*
evtest /dev/input/event0

# USB-сеть (RNDIS-телефон, CDC ECM/NCM, ASIX/Realtek)
ip -br link
udhcpc -i usb0

# Bluetooth-донгл
pidof dbus-daemon bluetoothd            # оба должны быть живы
bluetoothctl --version                  # 5.86
bluetoothctl list                       # адаптер после подключения донгла
```

Возврат к gadget требует вернуть `dr_mode = "otg"` в DTS и пересобрать
`boot` (KI-057). Maskrom-загрузчик от `dr_mode` не зависит, но по USB виден
только при прямом подключении к хосту — хаб на порту DUT его скрывает;
поэтому прошивка по умолчанию идёт по TFTP (§7-TFTP).

### UDP/RTP-раздача камеры (divinus → MediaMTX → RTSP)

Камера шлёт «сырой» RTP (PT 96, H.265) на `192.168.52.200:5600` со своего
порта `stream.udp_srcport`; приёмник — контейнер `oko-mediamtx`
(`services/stream/`). Учётные данные RTSP не нужны: путь `cam` доступен
анонимно (стенд).

```sh
# 1. конфигурация и живой процесс на DUT
grep -A5 '^stream:' /userdata/divinus.yaml     # enable: true, dest: - udp://192.168.52.200:5600
grep -A2 '^mp4:' /userdata/divinus.yaml        # enable: true, codec: H.265
pidof divinus
# в консоли UART после kill -HUP $(pidof divinus) ожидается:
#   [stream] UDP streaming initialized on port 5600
#   [stream] Added UDP client 192.168.52.200:5600 (ID 0)
#   [media]  Starting streaming to udp://192.168.52.200:5600...

# 2. приёмник на хосте
docker compose -f services/stream/docker-compose.yml ps
docker logs --tail 20 oko-mediamtx              # INF [path cam] stream is available ... (H265)
curl -s http://127.0.0.1:9997/v3/paths/list     # ready/online true, tracks ["H265"]
ss -lun 'sport = :5600'

# 3. поток целиком: fps, сверка с RTSP камеры, задержка ретрансляции
bench/run.sh stream_check.py --seconds 60 --latency --compare-dut
docker exec oko-mediamtx ffprobe -v error -show_streams rtsp://127.0.0.1:8554/cam
```

Типовые проблемы:

| Симптом | Вероятная причина |
|---|---|
| Камера не отправляет пакеты (`INIT` в логе есть, `Added UDP client` нет) | `stream.dest` не разобран: список читается только при `stream.enable: true`, ошибка видна как `[config] Parameter 'dest' not found in section 'stream'`; проверить отступы (`- udp://...` внутри `dest:`) |
| `Added UDP client` есть, приёмника нет | другой адрес в `dest` (сверьте `--server-host`), занят `5600/udp` на хосте (`ss -lun`), либо пакеты режет ufw |
| MediaMTX: `invalid fragmentation unit` | повреждённая H.265-фрагментация в отправителе (FU-заголовок) или потерян один RTP-пакет: `. 1 RTP packet lost` рядом в логе означает второе |
| `codecProps.width/height` = 0, `ffprobe` не даёт разрешение | параметры VPS/SPS/PPS идут не отдельными NAL-юнитами (склеены в один RTP-пакет) — проверить сборку `divinus` |
| Поток рвётся раз в несколько минут | перезапуск `divinus` (`kill -HUP`) рвёт последовательность RTP: приёмник пересобирает путь за 1–2 с |

### 2026-11-14: ESP32-мост ESP32-OKO-KVM — REST API и лог UART (сверено 2026-11-17)

К UART0 RV1106 подключён ESP32-C6-мост `192.168.52.17`; прошивка —
[ESP32-OKO-KVM](https://github.com/spectrespc/ESP32-OKO-KVM) (на стенде
`1.10.0`, слот `ota_1`; в `ota_0` лежит `1.9.0`). Документация API upstream —
`docs/api.md` того же репозитория; ниже — только то, что проверено на стенде.
Мост отдаёт консоль DUT по telnet `:23` и REST API по HTTP `:80` с cookie-сессией:

```bash
curl -s -c /tmp/c -X POST http://192.168.52.17/auth/login \
  -H 'Content-Type: application/json' \
  -d '{"username":"admin","password":"admin"}'
curl -s -b /tmp/c -o output/bench/uar0.log \
  'http://192.168.52.17/sd/download?path=uar0.log'
curl -s -b /tmp/c -X POST http://192.168.52.17/auth/logout
```

Коды, которые надо различать: `401 {"error":"Authentication required"}` —
нет/просрочена сессия; `401 {"authenticated":false,"error":"Invalid credentials"}`
— неверный логин; `429 Too many active sessions` — исчерпан пул сессий.
Пул ограничен (на стенде наблюдалось 3 сессии; `429` уже блокировал прогоны —
KI, 2026-11-17), сессия живёт 24 ч; по upstream после 5 неудачных логинов вход
блокируется на 60 с (на стенде не проверялось). Logout обязателен сразу после
работы. `bench/run.sh power.py on|off|cycle|reset` (`--cookie-file`) сам
логинится, делает logout и переиспользует сохранённую сессию; прямые
`curl`-сессии — только в простое стенда и с logout в той же серии команд.
Destructive-запросы (`/system/*`, `/ext/*`, `/ota/*`) сверяют `Origin` с `Host`
(по докладу upstream; проверено, что `curl` без `Origin` проходит): запрос из
браузера с чужого origin отклоняется, `curl` и скрипты `bench/` работают.

Эндпоинты, проверенные GET-запросом на 1.10.0 (все — с cookie, кроме
`/auth/login`; `POST /config` описан по upstream и не проверялся):

| Запрос | Назначение |
|---|---|
| `GET /config` | действующий конфиг: `baud_rate`, `telnet0/1_enable/port`, `telnetN_mon_*`, `uart0_sd_log(_fmt)`, `ext_*`, `ntp_*`, `pins` |
| `GET /config/export` | конфиг + `firmware_version` (быстрый способ узнать версию моста) |
| `POST /config` | частичное изменение (JSON с нужными ключами), применяется после перезагрузки моста |
| `GET /ota/status` | `current_version`, `available_version`, `last_result`, интервал проверки |
| `GET /ota/slots` | слоты `ota_0`/`ota_1` с версиями и адресами |
| `GET /ext/status` | `power_state`, `power_pin`, `reset_pin`, полярности, `reset_pulse_ms` |
| `POST /ext/power {"on":true\|false}` | реле 5 В питания DUT; `ext_pwr_state` персистентен в NVS |
| `POST /ext/reset` | импульс сброса DUT (`ext_rst_pulse_ms`, троттлинг 2 с) |
| `GET /system/time` | `epoch_ms`, `uptime_ms`, `synced`, NTP-сервер, `tz_offset_min` (`180` — метки логов MSK) |
| `GET /logs` | кольцевой буфер лога самого ESP32 (128 строк): диагностика «мост не отдаёт консоль» |
| `GET /sd/list` | файлы microSD: `name`, `path`, `is_dir`, `size` |
| `GET /sd/download?path=<name>` | скачать файл с microSD (в т.ч. ротированные `uar0.log.N`) |
| `GET /uart/monitor/status` | состояние read-only мониторов: `enabled`, `port`, `fmt`, `max_clients`, `clients`, `dropped` |
| `GET /uart/monitor?uart=0&fmt=1&cursor=N` | чанк лога с курсором (только при включённом мониторе) |

Danger: `POST /ext/power {"on":false}` **снимает питание DUT**, а состояние
хранится в NVS — после перезагрузки моста DUT останется обесточенным, пока не
вернуть `{"on":true}`. Распиновка на стенде (`GET /ext/status` и `pins` в
`/config`): питание DUT — `ext_pwr` GPIO20 (`active-low`), сброс — `ext_rst`
GPIO19 (`active-high`), UART0 TX/RX — GPIO16/17, microSD по SPI —
CS18/MOSI1/MISO3/SCLK0.

Лог UART на microSD: при `uart0_sd_log=1` пишется `uar0.log` (UART1 —
`uar1.log`), формат по умолчанию text (`uart0_sd_log_fmt=1`): каждый чанк
начинается строкой `[2026-11-17 21:01:35.294] RX: ` (время локальное по
`tz_offset_min`), байты DUT внутри чанка идут без префикса. Файл **не
кольцевой, а ротируемый**: при 1 МиБ (`1048576` байт) происходит сдвиг
`uar0.log` → `uar0.log.1` → … → `uar0.log.10`; хранится до 10 копий, более
старые удаляются. Смотреть — `GET /sd/list`, забирать — `GET /sd/download`
(скачивание занимает HTTP-сессию, но не telnet-клиент и не мешает зеркалу
`serial_exec.py`). Проверено на 1.10.0: файл растёт и ротируется **без
подключённого telnet-клиента**, т.е. `uart0_sd_log` включает запись постоянно;
upstream-архитектура описывает запись только при подключённом клиенте моста —
при обновлении прошивки моста это утверждение перепроверить.

Read-only мониторы UART (прошивка ≥ 1.9.0; на 1.10.0 эндпоинты есть): telnet
`:2324` (UART0) и `:24` (UART1), до `telnetN_mon_clients` клиентов (на стенде
`2`), форматы raw/text/hexdump (`telnetN_mon_fmt`). На стенде выключены
(`telnet0_mon_enable=0`), поэтому веб-читалка отвечает
`400 {"error":"MON_DISABLED"}`. Включение — `POST /config` с
`{"telnet0_mon_enable":1}` и перезагрузка моста (на стенде не выполнялось,
чтобы не ронять прогон); по upstream мониторы не требуют включённого
telnet-моста и работают при подключённом управляющем клиенте `:23` — это
замена «второму клиенту моста» (на стенде не проверено).

Живая консоль и команды — через `bench/serial_exec.py`
(`telnet://192.168.52.17:23`). Лог полезен, когда плата уже не отвечает по
SSH/HTTP: в нём остаются kernel panic/oops, `sfc`/UBIFS и сообщения `divinus`
до зависания. `uar0.log` — артефакт в `output/bench/` (не в `/tmp`);
cookie-файл `/tmp/c` живёт одну сессию.

### 2026-11-14: деградация SPI NAND после серии power cycle

После нескольких аварийных снятий питания на DUT (`192.168.52.33`) появились
ошибки SPI NAND и UBIFS:

```
rockchip-sfc ffac0000.spi: DMA wait for transfer finish timeout
rockchip-sfc ffac0000.spi: wait sfc idle timeout
UBIFS error (ubi0:0): fallible_read_node: ... error -5
UBIFS error (ubi4:0): make_reservation: cannot reserve ... error -30
Fatal exception in interrupt
```

`ubi0` — rootfs, `ubi4` — `/oem`. `error -5` — это `EIO` (чтение не
завершилось), `error -30` — `EROFS`: UBIFS после ошибок перевёл том в
read-only, поэтому `make_reservation`/`do_writepage` не пишут. Причина
выше — таймауты `rockchip-sfc`, а не флаг write-protect. Проявления:
SSH/HTTP/ARP пропадают, shell не может выполнить `echo`, в консоли kernel
panic при `sync`. Это состояние платы/флеша, а не `divinus`: до
перепрошивки (TFTP/Maskrom) или замены платы DUT-проверки остановлены.
После холодной загрузки 2026-11-14 recovery завершился, `ubi0`/`ubi4`/`ubi5`
смонтированы `rw`, `touch /oem/probe` + `sync` прошли — сбой был
транзиентным после kernel panic, а не постоянной защитой записи. При
повторении `sfc ... timeout` проверять питание/пайку NAND. Если `sfc ... timeout` повторяется
и после перепрошивки — проверять питание/пайку SPI NAND, а не образ.

### 2026-11-14: kernel oops `dma_buf_release` после MPP/IVS

При завершении `divinus` после работы с MPP/IVS ядро падает в
`dma_buf_release`:

```
Internal error: Oops - BUG: 0 [#1] THUMB2
PC is at dma_buf_release+0xa/0x58
... __dentry_kill / __fput / do_exit
Fixing recursive fault but reboot is needed!
```

Триггер на стенде 2026-11-14: destroy/recreate 2BPP-региона IVS
(`ivs.boxes` 1→0→1) — сначала `mpp: dup mClientFd failed`,
`getFd CHECK_GE`, SIGSEGV, затем oops при выходе. Это вендорный баг
жизненного цикла MPP/DMA-buf, не UBIFS. После него
устройство может остаться с зависшими процессами `divinus` в D-state;
`killall -9` их не убирает. Восстановление — `reboot -f` по SSH или
power cycle; затем убедиться, что остался ровно один `divinus`
(`pidof divinus`) и что `/live/0` отдаёт кадры. Не запускать второй
экземпляр вручную при живом init-процессе.

### 2026-11-15: panic `rga2_reg: irq handler err` после остановки YOLO и старта IVS

Остановили YOLO через `POST /api/yolo?enable=false` (каналы MPP 4/1/0
уничтожились штатно), затем `POST /api/ivs?enable=true` — через ~0.1 с ядро
падает:

```
rga_mm: Only get buffer 348160 byte from dma_buf_ptr = ..., but current image required 368640 byte
rga_mm: rga_mm_map_buffer map dma_buf error!
rga2_reg: irq handler err! INTR[0x701], HW_STATUS[0x0], CMD_STATUS[0x1]
rga2_reg: RGA bus error intr, please check your configuration and buffer.
rga: rga_mpi_commit, commit mpi job failed
Unable to handle kernel paging request at virtual address 8e828e82
Kernel panic - not syncing: Fatal exception in interrupt
```

Буфер 640×384 — вход YOLO: в момент сноса каналов остался незавершённый
RGA/DVBM-буфер, и следующий старт IVS добивает его. Это вендорный баг
жизненного цикла MPP/RGA (родственник `dma_buf_release`, KI-063), не правка
порога IVS. После panic DUT не отвечает по сети и по UART; импульс
`POST /ext/reset` моста не помогает, помогает только полный цикл питания
`POST /ext/power {"on":false}`, пауза 3–4 с, `{"on":true}` (восстановление
~40–60 с).

IVS с 2026-11-15 берёт кадры с отдельного VI-канала 2; привязка к выходу
VPSS давала непрерывные `RKMpiIvsChannel ... {getResults :718} get result
timeout` каждые 500 мс и ни одного события. Если таймауты вернулись — проверять
привязку `VI chn2 → IVS` и размер `ivs.width`×`ivs.height`, а не VPSS.

Повторная проверка KI-066 на новой топологии (2026-11-15): `yolo.source:
vpss3` работает, затем `POST /api/yolo?enable=false` и сразу
`POST /api/ivs?enable=true` — паники нет, IVS поднимается на VI chn2, события
идут, RSS 12,4 МиБ. Проверка действий: `act_http_post` виден в приёмнике
(`PUT /ingest/frame` 200), `act_record` кладёт клип в `/mnt/sdcard/recordings`
(`ffprobe`: hevc 1920×1080, `ivs.record_seconds`). Память: `grep VmRSS
/proc/$(pidof divinus)/status` — 12–16 МиБ с IVS, ~23 МиБ с YOLO `vi0`.

Симптом утечки IVS в консоли и `dmesg`: `ANALYZER:E:no free item for sofInfo!`,
затем OOM (`Killed process ... anon-rss:153236kB`) за минуты; RSS divinus рос
~340 КБ/с. Причина — результат без `rectNum`/`od.flag` не освобождался
(исправлено в `rk_ivs_get_results`). Для проверки: `grep VmRSS
/proc/$(pidof divinus)/status` должен стоять на ~12,4 МиБ.

`logread` в rootfs нет — сообщения `HAL_INFO`/вендорных библиотек смотреть на
консоли (`bench/run.sh serial_exec.py`, telnet-мост); `divinus.log` может
остаться от прошлой загрузки. Ручной запуск для отладки требует
`LD_LIBRARY_PATH=/oem/usr/lib` и cwd `/userdata`, иначе `[rk_sys] Failed to
load library!`.

Если после цикла питания мост перестал принимать telnet (`:23` закрыт),
перезагрузить ESP32 (`POST /system/reboot`) и повторить логин.

### 2026-11-15: 2BPP-рамки IVS (KI-063) — процедура и результаты

Рамки YOLO (handle 12, layer 6) и IVS (handle 13, layer 5) идут через один код
`rk_region_box_create/begin/commit/destroy` в `hal/plus/rk_hal.c`. Наблюдение
ведёт `bench/rgn_boxes_check.py`: сэмплы fd (`/proc/<pid>/fd`), `VmRSS` и
счётчиков ядра (`rockchip-sfc`, `UBIFS error`, `dup mClientFd`/`CHECK_GE`,
oops, `rga2_reg`) каждые 10 с, JSON в `output/bench/rgn/`.

```sh
# здоровье NAND перед экспериментом (гейт: 0 sfc timeout, 0 UBIFS error)
bench/run.sh dut.py --host 192.168.52.33 'dmesg -l err -l warn' 'uptime'
bench/run.sh dut.py --host 192.168.52.33 'touch /oem/probe' 'sync'

# baseline 30 мин: main + OSD + IVS без рамок
bench/run.sh rgn_boxes_check.py --mode observe --target ivs --seconds 1800

# A/B-цикл рамок YOLO (живой тумблер, 200 раз)
bench/run.sh rgn_boxes_check.py --mode cycle --target yolo --count 200

# IVS-рамки: create-once, применяется рестартом приложения
bench/run.sh rgn_boxes_check.py --mode config --target ivs --value true
curl -s -u admin:oko-change-me -X POST 'http://<dut>/api/ivs?enable=1'
bench/run.sh rgn_boxes_check.py --mode observe --target ivs --seconds 1800

# 20 graceful-рестартов с привязанным регионом 13
bench/run.sh rgn_boxes_check.py --mode restart --target ivs --count 20
```

Что смотреть на консоли (мост ESP32, `output/bench/uart-*.log`):

- `[rk_rgn] 2BPP overlay region 13 created ... layer 5` — регион создан **один
  раз за процесс**; повторные `created` при неизменном конфиге — дефект
  (пересоздание региона);
- `[rk_rgn] 2BPP overlay region 13 destroyed (layer 5)` — destroy: только при
  выходе/остановке IVS;
- `runtimeGetCanvasIn ... sync failed`, `[rk_venc] Main stream loop timed out!`,
  `dup mClientFd`, `getFd: CHECK_GE` — отказ RGN/MPP, немедленно снимать
  конфигурацию и разбирать;
- `[ivs] Motion event: ... area ... mdSquare ...` — результаты IVS идут.

Результаты 2026-11-15 (NAND восстановлен, 33-мин baseline чистый):

| Прогон | Длительность | fd | VmRSS | Ядро | Итог |
|---|---|---|---|---|---|
| baseline IVS без рамок | 33 мин | 79→79 | 12,9 МиБ | 0 | ок |
| YOLO-рамки, 200 A/B | 3,3 мин | 116↔117 | 20,0 МиБ | 0 | ок |
| IVS-рамки create-once | 30 мин | 90→90 | 12,58→12,59 МиБ | 0 | ок |
| IVS+YOLO (`vpss3`) | 30 мин | 132–134 | 19,9 МиБ | 0 | ок |
| NFS: IVS+YOLO | 30 мин | 104→104 | 21,0 МиБ | 0 | ок |
| 20 SIGHUP с регионами 12/13 | 7,2 мин | 113–116 | 12,7/19,9 МиБ (модель) | 0 | ок, без oops |

Ловушки:

- `yolo.source: vi0` с включёнными YOLO-рамками пересоздаёт регион 12 каждую
  секунду и ломает OSD (`Main stream loop timed out!`); для рамок держать
  `vpss3` (KI-068). IVS теперь на VI chn2 и с `vpss3` не конфликтует.
- Регион создаётся на **первый результат IVS** после включения, а статичная
  сцена даёт результаты редкими пачками. Для живого тумблера это значит:
  `boxes=1` может не дать рамок до первого события; для проверки живого
  режима надо двигать сцену или включать `md_interval=1`/OD.
- Смена `ivs.mode`/`sensitivity`/`md_interval`/размера перезапускает IVS
  внутри процесса; после такого перезапуска события на стенде 2026-11-15
  шли пачкой и замирали — для чистого замера делать SIGHUP приложения.
- До 2026-11-15 каждый SIGHUP-рестарт оставлял ~5 `dmabuf`-дескрипторов
  (`/dmabuf:vmpi`) и 1–2 `/dev/rga` в новом процессе: fd рос на 6–7 за
  рестарт, CMA — на 0,3–0,7 МиБ (KI-069). Исправлено пометкой всех дескрипторов
  `FD_CLOEXEC` перед `execvp()`; в консоли `[main] restart: N descriptor(s)
  closed on exec`. Контроль: `ls /proc/$(pidof divinus)/fd | wc -l` до и
  после серии рестартов должен стоять на 113–116.

### Сон камеры `[sleep]` (deep suspend + PMU timeout) — смоук-тест

Проверено 2026-11-16 на продуктовом образе; heartbeat-Snapshot — 2026-11-18
на AOV-образе (PMU-only, `aov_mcu=0`). Условие: ядро собрано с
`rv1106-pmu-timeout.config` (продуктовый BoardConfig), в root `compatible`
DTS есть `rockchip,rv1106`, все `.ko` пересобраны `./build.sh driver`.

1. Убедиться в готовности платформы (иначе политика сама откажется спать):
   ```sh
   cat /sys/power/mem_sleep        # должно быть "s2idle [deep]"
   devmem 0xff3000e0 32            # 0x000007FF после инициализации PMU
   /oem/usr/bin/oko-ec --json power # proto 3, host_state=2 (awake)
   ```
2. Тихое окно: отключить IVS/YOLO в `/userdata/divinus.yaml`
   (`ivs.enable: false`, `yolo.enable: false`), иначе лабораторная сцена
   даёт постоянное движение ~1000 ‰ и таймер простоя не истекает.
3. В `[sleep]` поставить `enable: true`, `idle_timeout_s: 30`,
   `wake_interval_s: 10`, `wake_check_s: 5`, `pmu_timeout_hz: 32`; SIGHUP
   divinus (`kill -HUP $(pidof divinus)`).
4. Наблюдение: `GET /api/status` → `sleep.state/idle_s`. Ожидаемый цикл:
   30 с `awake` → `suspending` → ~10 с сеть/DUT недоступны (deep suspend) →
   `probe` ~5 с → снова suspend. На консоли — `[sleep] suspended`,
   `[sleep] resumed`, `[sleep] wake probe`.
5. Движение: включить IVS (`ivs.enable: true`) → в окне probe событие выше
   `ivs.min_area` отменяет сон (`[sleep] motion in the wake window`), поток
   и RTSP продолжают работать, idle-таймер сбрасывается. Для стенда без
   реального движения инъектор `POST /api/sleep?motion=1` работает только
   при запуске divinus с `OKO_BENCH_API=1` (в боевом образе выключен).
6. После серии циклов: `bench/run.sh api_check.py`, `ui_check.py`,
   `dmesg | grep -iE "ubi.*err|sfc.*(err|time)"`, `oko-ec sensors` — должны
   быть без ошибок; `ls /proc/$(pidof divinus)/fd | wc -l` — 113–116.
7. Откат/выключение: `[sleep] enable: false` + SIGHUP; сон полностью
   перестаёт вмешиваться, остальные подсистемы не меняются.
8. Калибровка: PMU timeout считает **32 тика в секунду** (вендорский
   `rkadk_aov.c`: `io -4 0xff300048 seconds*32`). Значение 320 = ~10 с;
   при замене `pmu_timeout_hz` замерять фактический период по логу
   `[sleep] suspending ... (N s)` и возврату сети.
9. Heartbeat-кадр (Snapshot): в `[sleep]` добавить `heartbeat: true`,
   `heartbeat_interval_s: 0`, `heartbeat_retries: 3`, включить `[http_post]`
   с `host: 192.168.52.200:8090` и `url: /ingest/frame`; SIGHUP. На тихом
   цикле после `wake_check_s` в консоли `[heartbeat] sent (wake cycle N, …)`,
   в приёмнике — `PUT /ingest/heartbeat` (JPEG + `X-Heartbeat*`/`X-OKO-*`;
   `X-OKO-Motion-Area: -1` при выключенном IVS). Тихий цикл без карточки
   движения может не наступить: в лаборатории IVS даёт ~1000 ‰, поэтому
   поднять `ivs.min_area` (до 50) или выключить IVS/YOLO.
10. Сквозная проверка heartbeat (приёмник `services/events`, `docker compose
    -f services/events/docker-compose.yml up -d`):
    `bench/run.sh heartbeat_check.py` — ≥3 тихих цикла, сверка заголовков и
    `wake_cycle`, `fail_streak=0`; `--neg` (приёмник выключен) — сон всё
    равно наступает, `heartbeat_fail_streak` растёт; motion-фаза требует
    `OKO_BENCH_API=1` у divinus (иначе `SKIP`). Артефакты —
    `output/bench/heartbeat/`. После серии — `api_check.py`, `ui_check.py`,
    `dmesg | grep -iE "sfc|ubi.*err"`.
    Heartbeat добавляет к циклу до `heartbeat_retries × (3 с connect + 1 с
    пауза)` при недоступном приёмнике; это задерживает вход в suspend, но не
    отменяет его.
    Результат 2026-11-18 (`heartbeat_check.py`, AOV-образ): 40 heartbeat за
    746 с (циклы 2→62) и 12 за 195 с на финальной сборке, `area=-1`,
    `fail_streak` сброшен, `--neg` — рост `fail_streak` и продолжение
    циклов. Motion-фаза проверена с `OKO_BENCH_API=1` и `--wake-check 30`:
    инъекция в probe переводит состояние в `awake`, этот цикл heartbeat не
    отправляет, следующий тихий — отправляет (цикл 188). 1-часовой soak:
    200 heartbeat за 61,8 мин (циклы 192→502), `dmesg` без SFC/UBI,
    `rootfs`/`userdata` в `rw`. В окне 5 с сеть после resume готова только к
    концу: первые 1–2 попытки часто не соединяются, третий retry доставляет
    кадр, часть циклов остаётся без heartbeat (видно по `missed`). Артефакты —
    `output/bench/heartbeat/`.
11. Динамическая сцена (реальный MD, не инъектор): включить IVS
    (`POST /api/ivs?enable=true&min_area=30`), sleep с `idle_timeout_s=30`,
    `wake_interval_s=10`, `wake_check_s=30`; после deep suspend в окне
    `probe` сцена даёт `area 1000‰` → `[ivs] Motion event` (см. консоль) и
    `[sleep] motion in the wake window; stream stays on`, AP остаётся
    бодрствовать, heartbeat в этом цикле не уходит; после 30 с без движения
    — снова suspend. Проверено 2026-11-18 дважды (wake_cycle 1 и 2),
    артефакт `output/bench/heartbeat/dynamic-scene.txt`.
    Ловушка стенда: `/api/ivs` меняет `enable`/`min_area` живьём, но не
    сохраняет конфиг — после правки нужен `POST /api/cmd?save`, иначе
    reboot вернёт прежние значения.

Не держать активный CSI/ISP-пайплайн через `echo mem`: на этой плате resume
даёт шторм `mipi-csi2-hw ERR1:0x10` и AP зависает. Политика сама разбирает
пайплайн перед сном и перезапускает divinus после wake.

После длинной серии циклов (2026-11-16: ~4,5 ч, 288 циклов + серия power
cycle и один неудачный `echo mem`) наблюдался `rockchip-sfc ... DMA wait for
transfer finish timeout`, затем `UBIFS ... error -5` и перевод `/` и
`/userdata` в `ro` (KI-071). Power cycle через ESP32 восстановил оба тома.
При длительных прогонах сна проверять `dmesg | grep -i sfc` после каждого
resume и держать резервный `output/image/update.img` для TFTP.

### 2026-11-19: MQTT — события и телеметрия камеры (`services/mqtt`)

Профиль `oko-mqtt` (`network_mode: host`): брокер `eclipse-mosquitto:2.0.20`
на `1883` с обязательной аутентификацией и рекордер на `8092`, который
складывает JSON в `output/srv/mqtt/recorder/` (`events/<дата>.jsonl`,
`telemetry/<id>.json`, `status/<id>.json`, `messages.log.jsonl`, retention
`MQTT_REC_MAX_MB`, по умолчанию 16 МиБ). TLS нет;
пароли — переменные `OKO_MQTT_*` (стендовые дефолты в §6).

```sh
# порт должен быть свободен (иначе compose не стартует listener)
ss -ltn 'sport = :1883 or sport = :8092'
docker compose -f services/mqtt/docker-compose.yml up -d --build
docker compose -f services/mqtt/docker-compose.yml ps
docker logs --tail 20 oko-mqtt-broker
curl -fsS http://127.0.0.1:8092/health
# {"ok":true,"broker":"127.0.0.1:1883","topic":"oko/#",...}

# аутентификация: верный пароль проходит, неверный — отказ (rc 5)
docker exec oko-mqtt-broker mosquitto_pub -h 127.0.0.1 -u oko -P oko \
  -t oko/bench/probe -m '{"type":"probe"}'
docker exec oko-mqtt-broker mosquitto_pub -h 127.0.0.1 -u oko -P bad \
  -t oko/bench/probe -m x        # Connection Refused: not authorised

# подписка на всё дерево (до 10 с ждём одно сообщение)
docker exec oko-mqtt-broker mosquitto_sub -h 127.0.0.1 -u oko-rec -P oko-rec \
  -t 'oko/#' -W 10 -C 1
```

Настройка камеры — живьём, без рестарта и без обрыва потока:

```sh
curl -u admin:oko-change-me -X POST \
 'http://192.168.52.33/api/mqtt?enable=1&host=192.168.52.200%3A1883&login=oko&password=oko&keepalive_s=60&telemetry_interval_s=30&publish_events=1&publish_telemetry=1'
curl -u admin:oko-change-me http://192.168.52.33/api/mqtt
# enable/host/.../connected/sent/failed/queued/dropped/last_error,
# resolved_client_id и полные темы event_topic/telemetry_topic/status_topic
```

Темы: `oko/<id>/event` (QoS1, JSON на событие motion/yolo/clip/heartbeat;
`id` — `resolved_client_id`, по умолчанию `oko-<4 hex MAC>`),
`oko/<id>/telemetry` (QoS0 retained, `{ts, available, data}` из
`/tmp/oko-ec.json`; устаревший файл → `available:false`,`age`) и
`oko/<id>/status` (retained `{online:true, fw}`; LWT брокера
`{online:false}`). Сквозная проверка:

```sh
bench/run.sh mqtt_check.py           # положительный прогон (профиль поднят)
bench/run.sh mqtt_check.py --deploy  # сначала bench/deploy_divinus.py
bench/run.sh mqtt_check.py --neg     # профиль выключен: connected=false,
                                     # last_error непустой, ретраи 30с/2мин/10мин
docker compose -f services/mqtt/docker-compose.yml down
```

`mqtt_check.py` проверяет аутентификацию брокера, `POST /api/mqtt`,
retained `online:true`, телеметрию, затем `kill -9` divinus → LWT
`online:false`. Дальше скрипт поднимает divinus заново с `OKO_BENCH_API=1`
(`cd /userdata; LD_LIBRARY_PATH=/oem/usr/lib …`), проверяет событие
`POST /api/mqtt?test=1` (тип `test`, `cause bench`) и в конце перезапускает
процесс уже без `OKO_BENCH_API`, чтобы bench-инъектор не остался включённым;
отчёт — `output/bench/mqtt/report.json`. Важно: divinus запускает
`oko-lunch.sh`, а
`/etc/init.d/S20oko-osd` управляет только OSD-сайдкаром — для рестарта
одного divinus используйте `LD_LIBRARY_PATH=/oem/usr/lib` и cwd `/userdata`.
Очередь событий на камере — 64 JSON в RAM,
переполнение теряет самое старое (`dropped`); отправка не блокирует потоки
аналитики, PUBACK ждёт только mqtt-поток. События, возникшие во время
suspend, теряются (память только RAM). Пароль лежит открытым в
`/userdata/divinus.yaml` — как у `[http_post]`.

## 9. Известные особенности

- **USB-порт закреплён в host-режиме (`dr_mode = "host"`).** С 2026-11-11
  порт не переключается: при загрузке xHCI поднимается сразу, хаб `1a40:0101`
  и устройство за ним (`0bda:c811`) видны в `lsusb` без `otg_mode`. Gadget
  функций нет: UDC не регистрируется, `usb0`/RNDIS и adb отсутствуют,
  `S50usbdevice` и `S99usb0config` сами выходят по `dr_mode` (KI-057).
  VBUS 5 В SoC не управляет — хаб и устройства питаются от платы/внешнего
  источника; bus-powered хаб без питания может не определиться. Maskrom по
  USB виден только при прямом кабеле к хосту, поэтому прошивка по умолчанию —
  TFTP (§7-TFTP). Legacy `g_mass_storage` выключен (KI-056).
  Ограничения — KI-054, KI-057.
- **SoftAP всегда WPA2-PSK.** В AP-фолбэке (`OKO-XXXX`, `192.168.43.1/24`)
  точка защищена WPA2-PSK; если `oko_wlan_ap_psk` не задан, используется
  заводский пароль `ybrjveytljdthzq` (публичный дефолт KI-030; смена —
  `oko-wifi set` или `/api/wifi`, возврат — `fw_setenv oko_wlan_ap_psk`).
  Открытой точки нет. `ap_psk` виден в `oko-wifi env`, `GET /api/wifi` и
  `GET /api/wifi/apinfo`. Проверено 2026-11-12: клиент с хоста
  (`wpa_supplicant` + `iw`) ассоциируется по WPA2-PSK/CCMP, получает DHCP
  `192.168.43.1x`, ping и HTTP к `192.168.43.1` отвечают; неверный пароль —
  без ассоциации, DUT остаётся в AP.
- **CMA накапливается после тяжёлых медиа-прогонов.** После двух и более
  подряд прогонов `ui_check.py`/`geometry_check.py` или ручной отладки
  ePTZ/поворота перезагружайте DUT. Решающие признаки: `cma_alloc ... ret:
  -12` в `dmesg -l err`, `/image.jpg` отвечает 503, RTSP не отдаёт кадры.
  Высокий `loadavg` и потоки ядра в `DW` (`vlog`, `valloc`, `vpss`, `venc`, …)
  сами по себе не признак — они висят в `DW` и на здоровой плате. Лечение —
  только перезагрузка (`bench/run.sh dut.py --host 192.168.52.33 'reboot'`,
  ожидание 50–60 с); перед замерами геометрии проверяйте, что `/image.jpg`
  отвечает 200 и RTSP отдаёт кадры. После нескольких циклов
  `focus?enable=1/0` AF-метрика может оставаться `idle`/`no_stats` до
  перезагрузки.
- `IMAGE/` содержит актуальную релизную сборку под `RV1106 Pro Max` + SC230AI;
  устаревший артефакт `RV1103 Pico Mini` удалён 2026-11-08.
- Драйвер `motor_24byj48` (`motor.ko`, шаговик 28BYJ-48) удалён из форка
  2026-11-07: в истории прошивок его не было, привод фокуса — STM8 по I2C.
- `insmod_ko.sh` вызывается из `oko-lunch.sh`, который запускает `S21appinit`;
  лаунчер (`project/app/oko-lunch`) сразу уходит в фон (`post_chk`), вся логика
  приложения зашита в этой функции. Подробнее — `INIT_ORDER.md`.
- `post_chk` больше не выбирает `rkipc-*.ini` и не переключает `csi_unite`:
  `rkipc` не собирается, его исходники и старые `RkLunch*.sh` вырезаны из образа
  вместе с ini, `image.bmp` и `speaker_test.wav` (`oko-prune.list`). Диагностика
  через `rkipc`, `rk_mpi_*_test`, `rkaiq_3A_server` и `rkaiq_tool_server`
  недоступна; остаются `v4l2-ctl`, `media-ctl` и `rgaImDemo`.
- В автономном режиме `oem` и `userdata` монтируются из SPI NAND (ubifs).
  Изменения, сделанные на устройстве в `/userdata`, сохраняются в NAND.
- Сборка идёт на хосте; сетевые сервисы стенда (`tftpd-hpa`,
  `nfs-kernel-server`, `rpcbind`) запускает контейнер `services/` через
  `tools/native/setup_netboot.sh`. Старый контейнер сборки не используется.
- `divinus` — единственный стример OpenIPC (MIT, `project/app/divinus/`):
  маркер `/userdata/divinus.enable` всегда создаёт `S20oko-osd`; `rkipc`
  вырезан из образа, opt-out больше нет.
  RV11xx-HAL в upstream ещё «в разработке»: RTSP/fMP4/JPEG работают,
  аудио ограничено. Web-порт divinus — `80`, RTSP — `554`.
- OSD divinus включён в фабричном конфиге: время — в правом нижнем углу
  (`reg0`), телеметрия `T/BAT/LTE/WiFi` — четырьмя строками в левом верхнем
  (`reg1`), GPS — в правом верхнем (`reg2`), сообщения из `/tmp/osd.fifo` —
  в левом верхнем под телеметрией (`reg3`, TTL 60 с), CPU load / свободная
  память / температура CPU — тремя строками в левом нижнем углу (`reg4`).
  Wi-Fi в `reg1` — реальный RSSI STA (`wpa_cli status`/`signal_poll`):
  `WiFi <n>/5 <dBm>dBm` при `wpa_state=COMPLETED`, `wifi ap` при
  работающем SoftAP (`hostapd` с pid-файлом `/run/hostapd-wlan0.pid`,
  живым процессом `hostapd` и IPv4 на `wlan0`), `wifi off` без ассоциации
  и без AP (скан, `mode=off`), а без `/sys/class/net/wlan0` поле и его
  строка исчезают; LTE и GPS — симуляция.
  Провайдер `@file` — локальный патч `region.c`; данные пишут `oko-osd` и
  `reg0_text: "$t"`.
  Размер шрифта всех регионов — `24` px, цвет — ярко-зелёный `0x07e0`
  (RGB565), чёрный контур `regN_outl: 0x8000` толщиной `1.0`. RGN использует один canvas (`canvas=1`): с двумя canvas драйвер
  иногда показывал OSD предыдущей секунды. Шрифт — `/oem/usr/share/simsun_en.ttf` (`regN_font: simsun_en`):
  покрытие — ASCII `0x21–0x7e` плюс несколько CJK-иероглифов, кириллицы в шрифте
  нет, поэтому сообщения и телеметрию пишите латиницей.
  OSD виден в RTSP (VENC-канал 0); snapshots `/image.jpg` кодируются отдельным
  VENC-каналом и OSD не содержат — ограничение RGN на RV1106.
- `S20oko-osd` перезаписывает `/userdata/divinus.yaml` фабричным, если в нём нет
  `reg0_text`, `reg0_size`, `reg0_color` или `reg3_text` (конфиг старого формата);
  первый прежний файл сохраняется как `/userdata/divinus.yaml.bak`. Ручные правки
  такого файла не переживают перезагрузку; добавьте эти параметры или отключите
  OSD в файле.
- divinus ищет конфиг рядом с исполняемым файлом, затем в текущем каталоге, затем
  `/etc/divinus.yaml`. `post_chk` запускает его из `/userdata`, поэтому сохранение
  конфига из WebUI (`/api/cmd?save`) остаётся в `/userdata` (файл `.bak` — там же).
- в `compat.c` divinus пропатчен ABI `mmap` (libc-вариант с 32-битным `off_t`),
  поэтому `LD_PRELOAD` не нужен. Если после обновления upstream в логе снова
  появился `mmap part 1 failed!`, значит патч `compat.c` не применён; см.
  `project/app/divinus/README.md`. Второй патч (`region.c`, `@file`-провайдер OSD)
  повторяется тем же порядком.

- STM8S103F3P6 подключён к I2C3 по адресу `0x60` и к ST-Link/V2 по SWIM;
  первая прошивка — `factory-2.1.1.ihx` (§7C), далее `oko-motor flash` или
  `/api/flash`. Версия обеих прошивок берётся из имени файла
  (`app-1.0.1.bin`, `app-2.1.1.ihx`); безымянный `app.bin`/`app.ihx`
  отклоняется, а несовпадение имени с заголовком OKO8 — тоже 400.
  EEPROM-позиции моторов при прошивке flash не стираются.
- AF-статистика SC230AI требует `af_v31.TuningPara.contrast_af.enable=1`
  в IQ (оба блока). Метрика в `/api/focus` — по 225 ячейкам; ROI применяется
  выбором ячеек, AFM-карта полнокадровая. Малые ходы мотора метрику не
  меняют: оптическая связь объектива на стенде не подтверждена.
- RTSP молчит (OPTIONS без ответа), если `rtsp.enable=false` в
  `/userdata/divinus.yaml`; кадров не будет и без активного VENC-канала
  (`mp4.enable`/`substream.enable`). `stream.enable` к RTSP отношения не имеет:
  это отдельный UDP/RTP-пуш.
- USB Wi-Fi собран на `0bda:c811` (RTL8821CU) за хабом `1a40:0101`. После
  загрузки `insmod_wifi.sh` видит `0bda/c811`, грузит `cfg80211.ko` и
  `8821cu.ko`; `wlan0` появляется через несколько секунд. Настройка — WebUI
  **Сеть → Wi-Fi** или `/api/wifi`; настройки лежат в env U-Boot
  (`oko_wlan_*`), поэтому переживают прошивку `oem`/`rootfs`. Без рабочего
  STA поднимается SoftAP `OKO-XXXX` на `192.168.43.1/24` с DHCP; веб-интерфейс
  доступен с телефона по этому адресу. AP+STA для этого драйвера не
  поддерживается (только STA или AP по отдельности). Скан через
  `/api/wifi?scan=1` + `/api/wifi/scan`; при активном SoftAP скан отклоняется.
  SD-конфиг: файл `oko-wifi.conf` в корне первого раздела карты, импорт один
  раз при монтировании, затем файл переименовывается в
  `oko-wifi.conf.applied-<sha1-8>`.
- Два аплинка: eth0 и wlan0 (STA) поднимаются одновременно, приоритет —
  `oko_net_metric` (100) и `oko_wlan_metric` (600) в env. `S97okoroute`
  держит по одному default-маршруту, переключает трафик при недоступности
  шлюза (3 отказа `ping -I` → metric+10000, 2 успеха → возврат, cooldown
  30 с) и единолично пишет `/etc/resolv.conf`. Проверка:
  ```sh
  curl -s -u admin:oko-change-me http://<DUT>/api/network | grep -o '"active_default":{[^}]*}'
  bench/run.sh dut.py --host <DUT> '/usr/bin/oko-route show | grep -E "demoted|active_default"'
  bench/run.sh dut.py --host <DUT> 'ip route get 8.8.8.8'
  ```
  Для стендовой проверки Wi-Fi-аплинка на хосте в контейнере `services`
  поднимается временная открытая точка доступа (hostapd на host `wlan0`,
  `192.168.44.1/24`), DUT подключается статикой `192.168.44.50/24`, шлюз
  `192.168.44.1`; затем шлюз снимается/возвращается и проверяются demote и
  возврат. AP — только тестовый инструмент, в образ не входит. Пакеты в
  контейнере не предустановлены:
  ```sh
  docker exec oko-services sh -c 'apt-get install -y hostapd iptables'
  docker exec -d oko-services hostapd -B -P /tmp/hostapd-bench.pid /tmp/hostapd-bench.conf
  docker exec oko-services ip addr add 192.168.44.1/24 dev wlan0
  # «уронить» только ping DUT к шлюзу, не трогая SSH:
  docker exec oko-services iptables -I INPUT -i wlan0 -s 192.168.44.50 \
      -d 192.168.44.1 -p icmp --icmp-type echo-request -j DROP
  ```
  Конфиг hostapd — open network `OKO-BENCH`, `hw_mode=g`, `channel=6`.
  Автоматизация — `bench/run.sh uplink_soak.py`.
- При `root=/dev/nfs` `oko-route` не подменяет eth-default и не пишет
  resolv.conf (KI-060): это защищает NFS-загрузку стенда (`nfs_env.py on`).
- **Weak-host защита eth0+wlan0 в одной подсети.** Ядро собрано с
  `CONFIG_IP_ADVANCED_ROUTER=y` и `CONFIG_IP_MULTIPLE_TABLES=y`; busybox
  1.38.0 в образе содержит `CONFIG_IPRULE=y` и `CONFIG_FEATURE_IP_RULE=y`,
  поэтому `ip rule add from/iif ... lookup <num> priority <n>` и
  `ip route ... table <n>` работают (проверено на DUT). Исходные значения:
  `rp_filter` all/default/eth0 = 0, `arp_ignore`/`arp_announce` = 0;
  после `oko-route once` — `arp_ignore=1`, `arp_announce=2` (all+default),
  `rp_filter` остаётся 0, потому что строгий режим не включён.
  Стендовый прогон 2026-11-12: eth0 DHCP `192.168.52.33/24` + wlan0
  static `192.168.52.44/24` в одной подсети через тестовую AP
  (`192.168.52.201`, `OKO-BENCH`). Параллельный ping 20/20 на каждый адрес —
  0% потерь; `ip route get 192.168.52.200 from .33` → `eth0`,
  `... .201 from .44` → `wlan0`; на PC `.33` привязан к MAC eth0,
  `.44` — к MAC wlan0, flap нет; HTTP-снимок и RTSP DESCRIBE — 200 на
  обоих адресах; dead-шлюз eth (iptables DROP ICMP) → `eth_demoted=1`,
  default на wlan0, возврат после снятия DROP; AP-фолбэк `192.168.43.1`
  с таблицей 101 без default; static eth + static wlan — 10/10 на каждый
  адрес; 3 цикла `ip link set eth0 down/up` — оба адреса живы; netboot
  (подмена `/proc/cmdline`) не меняет eth-default и resolv.
  Состояние — `oko-route show` (`multihome`, `arp_tuning`, `rp_filter`,
  `policy_tables`, `eth_addr`, `wlan_addr`). После прошивки по TFTP
  настройки `oko_*` переживают reboot; если U-Boot при загрузке напечатал
  `Using default environment`, `saveenv` в конце прошивки затрёт `oko_*` —
  их нужно задать заново.
- Обновление ПО из WebUI: пакет готовится на microSD, прошивку выполняет
  U-Boot `sd_update` при следующей загрузке, но только при выставленном
  `oko_update` в U-Boot env (`apply`), поэтому перезагрузка до подтверждения
  ничего не прошивает. Оставленная в слоте карта после успешной загрузки
  безопасна: `S20oko-update` удаляет `sd_update.txt` и образы. Если Linux не загрузился (обрыв питания во время `mtd erase/write`),
  карта сохраняет пакет и U-Boot повторяет прошивку; иначе плату
  восстанавливают через USB (`upgrade_tool`, Maskrom). Раздел `/userdata`
  обновлением не затрагивается.
- Прошивка через WebUI идёт в фоне; `POST /api/flash` без `confirm=FLASH`,
  без версии в имени, с чужим расширением, битым `OKO8`, несовпадением
  имени/заголовка или большим файлом отдаёт 400, повторный
  POST во время работы — 409. Демон `S21oko-ec` на время задачи
  останавливается; при обрыве хостового процесса STM8 остаётся в загрузчике
  и принимает повторную загрузку.
- **ESP32-мост (ESP32-OKO-KVM 1.10.0) — общий ресурс стенда.** Консоль DUT на
  `:23` обслуживает **один** управляющий клиент: второй telnet не подключится,
  пока первый держит сессию (`timed out`), поэтому перед `bench/run.sh …`
  закрывайте свои telnet-сессии. Параллельно читать лог можно только через
  read-only мониторы `:2324` (UART0) / `:24` (UART1), а они по умолчанию
  выключены (`telnetN_mon_enable=0`, ответ `MON_DISABLED`) — включение по
  upstream требует `POST /config` + перезагрузка моста, т.е. обрыва консоли.
  HTTP-сессии авторизации — отдельный пул на 3 штуки
  (`429 Too many active sessions`), живут 24 ч: логиниться точечно и сразу
  делать `/auth/logout`, иначе следующие прогоны (в т.ч. `power.py`) получат
  429. SD-лог `uar0.log` пишется непрерывно при `uart0_sd_log=1`, ротируется
  по 1 МиБ и хранит 10 копий: после долгого простоя интересующие строки могут
  уйти в `uar0.log.9/10`. Подробности и команды — §8.

## 10. Правила ведения

1. Обновлять документ при любой смене железа, IP-адресов, путей, версии SDK или
   состава модулей.
2. При каждом обновлении менять «Дату обновления» в разделе 1.
3. Секреты не хранить: только плейсхолдеры `<...>` и публичные вендорские дефолты.
4. Расхождения между документом и скриптами трактовать в пользу скриптов
   (`bench/*.py`, `tools/native/*.sh`, BoardConfig) и сразу править документ.