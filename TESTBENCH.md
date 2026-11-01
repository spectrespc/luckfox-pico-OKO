# Тестовый стенд OKO

Паспорт и рабочие параметры лабораторного стенда для отладки прошивки Luckfox Pico
Pro Max (RV1106) в режиме netboot. Значения сверены с `netboot-catch.py`,
`start.sh`, `docker-compose.yml` и активным BoardConfig на дату обновления.

## 1. Паспорт стенда

| Поле | Значение |
|---|---|
| Дата обновления | `2026-11-01` |
| Ответственный | `<ФИО>` |
| Статус | активен |
| Назначение | сборка прошивки и загрузка по сети (TFTP + NFS) для отладки IPC |
| Расположение | `<лаборатория / рабочее место>` |

## 2. Подопытное устройство (DUT)

| Параметр | Значение |
|---|---|
| Плата | Luckfox Pico Pro Max |
| SoC | Rockchip RV1106 (Cortex-A7 + NPU + ISP) |
| Носитель загрузки | SPI NAND (`RK_BOOT_MEDIUM=spi_nand`) |
| Корневая ФС | Buildroot, busybox init |
| Сенсор | SC230AI (MIPI CSI, драйвер `sc230ai`, активен) |
| Двигатель | шаговый 28BYJ-48 через GPIO-драйвер `motor_24byj48` — **выключен** (`RK_ENABLE_MOTOR` не задан) |
| Питание | `<источник, напряжение, ток>` |
| Подключение к ПК | USB (OTG) + UART; Maskrom — `<кнопка / перемычка>` |
| Серийный номер / метка | `<S/N>` |

## 3. Хост

| Параметр | Значение |
|---|---|
| ОС | Ubuntu 22.04 |
| Сборка | Docker, образ `luckfox-builder:22.04` |
| Контейнер | `luckfox_oko_build_env` |
| Сеть контейнера | `network_mode: host` (общий сетевой стек с хостом) |
| Рабочая папка | `/workspace` (корень репозитория смонтирован туда) |
| TFTP-сервер | `tftpd-hpa`, порт `69/udp`, корень `./output/image` (`/workspace/output/image`) |
| NFS-расшарка | `/srv/nfs/rootfs` (настраивается на хосте, в репозиторий не входит); внутри — `.nfsroot/{oem,userdata}` |
| Запуск окружения | `./start.sh` |

## 4. Подключения

| Интерфейс | Значение | Примечание |
|---|---|---|
| UART | `/dev/ttyACM0` | `bench/serial_exec.py` и `bench/netboot.py` берут последний найденный `/dev/ttyACM*` |
| Скорость UART | `115200 8N1` | задана в скриптах `bench/` |
| Лог serial-сессии | `output/bench/netboot-<ts>.log` | каталог создаётся скриптом автоматически |
| Ethernet | лабораторная сеть `192.168.52.0/24` | через порт устройства |
| USB | `<порт / хаб>` | используется для Maskrom-прошивки |

Требования к хосту: Python 3 и зависимости из `bench/requirements.txt`;
`bench/setup.sh` создаёт venv в `output/bench/venv`, запуск — `bench/run.sh`.
Legacy-скрипт `netboot-catch.py` в корне требует только `pyserial`.

## 5. Сеть

| Узел | Адрес |
|---|---|
| Хост (TFTP + NFS) | `192.168.52.200` |
| Устройство (DUT) | `192.168.52.150` |
| Маска | `255.255.255.0` |
| Шлюз | `192.168.52.254` |

Переменные U-Boot, которые выставляет `netboot-catch.py`:

```
ipaddr     = 192.168.52.150
netmask    = 255.255.255.0
serverip   = 192.168.52.200
gatewayip  = 192.168.52.254
sys_bootargs = root=/dev/nfs rw \
               nfsroot=192.168.52.200:/srv/nfs/rootfs,vers=3,proto=tcp,nolock \
               ip=192.168.52.150:192.168.52.200:192.168.52.254:255.255.255.0:luckfox:eth0:off \
               rk_dma_heap_cma=66M
```

Загрузка по TFTP:

```
tftp ${ramdisk_addr_r} boot.img
boot_fit ${ramdisk_addr_r}
```

- TFTP-корень отдаёт `output/image`, поэтому `boot.img` должен быть результатом
  свежей сборки (`./build.sh` внутри контейнера).
- NFS-сервер должен экспортировать `/srv/nfs/rootfs` с поддержкой NFSv3 и TCP.
- Разделы `oem` и `userdata` при netboot монтируются по NFS из
  `<host>:<nfsroot>/.nfsroot/{oem,userdata}`; при ошибке NFS — откат на ubifs из SPI NAND.
- Экспорт хоста: `/srv/nfs/rootfs 192.168.52.0/24(rw,sync,no_subtree_check,no_root_squash,insecure)`.
  `no_root_squash` позволяет устройству писать в `/userdata` без правки прав.

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
| divinus Web/RTSP (фабричный) | `admin` | `oko-change-me` — публичный фабричный дефолт, сменить до подключения к недоверенной сети |
| Хост / Docker | `builder` | без пароля внутри контейнера (`NOPASSWD` sudo) |

Перед выдачей стенда наружу вендорский пароль `luckfox` необходимо сменить.

## 7. Прошивка и загрузка

### A. SPI NAND (автономная загрузка)

1. В контейнере: `./build.sh firmware`.
2. Результат: `output/image/update.img`.
3. Перевести устройство в Maskrom — `<кнопка / перемычка>`.
4. Прошить: `./rkflash.sh` или `tools/linux/Linux_Upgrade_Tool/upgrade_tool`.
5. Перезагрузить; загрузка пойдёт из SPI NAND без хоста.

### B. Netboot (текущий режим отладки)

1. `./start.sh` — собрать/запустить контейнер, поднять TFTP на `69/udp`.
2. Убедиться, что NFS экспортирует `/srv/nfs/rootfs` и в `output/image` есть
   актуальный `boot.img`.
3. Выложить свежую сборку на хост (корень, `oem`, `userdata`). Экспорт идёт с
   `no_root_squash`, поэтому устройство оставляет в дереве файлы `root:root`
   (`/var/lib/seedrng`, `/userdata/rkipc.ini`, `/userdata/video*`); синхронизацию
   выполняйте от root — например, из того же контейнера:
   ```sh
   docker run --rm -u 0 -v /srv/nfs:/srv/nfs \
     -v "$PWD/output/out:/src:ro" --entrypoint bash luckfox-builder:22.04 -c '
     ts=$(date +%Y%m%d-%H%M)
     tar -C /srv/nfs -czf /srv/nfs/rootfs-backup-$ts.tar.gz rootfs
     rsync -a --delete --exclude=.nfsroot /src/rootfs_uclibc_rv1106/ /srv/nfs/rootfs/
     mkdir -p /srv/nfs/rootfs/.nfsroot
     rsync -a --delete /src/oem/ /srv/nfs/rootfs/.nfsroot/oem/
     rsync -a --delete /src/userdata/ /srv/nfs/rootfs/.nfsroot/userdata/
     mkdir -p /srv/nfs/rootfs/.nfsroot/userdata/data
     chmod -R 777 /srv/nfs/rootfs/.nfsroot/userdata
     chmod -R a+rX /srv/nfs/rootfs/.nfsroot/oem'
   ```
   `/.nfsroot/` находится внутри того же экспорта, поэтому правка `/etc/exports`
   не нужна. `--exclude=.nfsroot` не даёт удалить зеркала разделов при синхронизации.
   Эквивалентный вариант — те же команды через `sudo` от пользователя `builder`.
4. На хосте запустить перехват U-Boot (скрипт сам откроет последний
   `/dev/ttyACM*` и создаст лог в `output/bench/`):
   ```sh
   bench/run.sh netboot.py catch
   ```
   Альтернатива без ручного сброса питания — `bench/run.sh netboot.py reboot`:
   скрипт войдёт в систему, выполнит `reboot` и перехватит U-Boot.
5. Сбросить питание устройства (для режима `catch`). Скрипт пробивает U-Boot,
   отправляет команды из `NETBOOT_CMDS`, грузит `boot.img` по TFTP и выполняет
   `boot_fit`.
6. Смотреть лог `output/bench/netboot-<ts>.log` (точный путь печатает скрипт).
   Таймауты — `--prompt-timeout` и `--boot-timeout`; у legacy
   `netboot-catch.py` лимит сессии 900 секунд.

Ручной вариант в консоли U-Boot — те же команды, что в `NETBOOT_CMDS` файла
`bench/netboot.py` (`setenv …`, `ping 192.168.52.200`, `tftp`, `boot_fit`).

### C. Покомпонентная прошивка изменённых разделов

Режим для выкатки в SPI NAND после проверки по netboot: прошиваются только
разделы, содержимое которых изменилось, остальные (включая `env` с MAC-адресом)
не трогаются. Уменьшает износ NAND и исключает сброс U-Boot env.

**Маппинг исходников на разделы** (`output/image/*.img` собирает
`./build.sh firmware`):

| Раздел | Что содержит | Источник в сборке |
|---|---|---|
| `env` | U-Boot env (MAC и пр.) | `output/image/env.img` (этой фичей не изменялся) |
| `idblock`, `uboot` | SPL/DDR, U-Boot | `output/image/idblock.img`, `uboot.img` |
| `boot` | ядро + DTB | `output/image/boot.img` |
| `oem` | `/oem/usr/bin/divinus`, `/oem/usr/share/divinus/divinus.yaml`, `/oem/usr/share/rkipc*`, библиотеки | `output/out/oem/` |
| `userdata` | фабричное содержимое раздела; рабочие маркеры и конфиг живут на устройстве | `output/out/userdata/` |
| `rootfs` | корневая ФС, включая `/etc/init.d/S20oko-osd` и `/usr/bin/oko-osd` | `output/out/rootfs_uclibc_rv1106/` |

**Порядок:**

1. Перевести устройство в Maskrom (`<кнопка / перемычка>`) либо выполнить на DUT
   `reboot loader` (Rockchip RESTART2); наличие устройства проверить
   `upgrade_tool ld`.
2. Загрузить loader в RAM и проверить таблицу разделов:
   ```sh
   ./tools/linux/Linux_Upgrade_Tool/upgrade_tool db output/image/download.bin
   ./tools/linux/Linux_Upgrade_Tool/upgrade_tool pl   # LBA и размеры в секторах 512 B
   ```
3. Прошить только изменённые разделы. `./rkflash.sh oem` / `rootfs` использует
   `upgrade_tool di -oem/-rootfs`, но в текущей сборке нет `parameter.txt`
   (образы упаковываются в `update.img`), поэтому в Maskrom он отвечает
   `parameter is invalid,please check!`. Рабочий путь — запись по LBA с
   **полным стиранием раздела**:
   ```sh
   ./tools/linux/Linux_Upgrade_Tool/upgrade_tool wl 0x2800 0xF000 output/image/oem.img
   ./tools/linux/Linux_Upgrade_Tool/upgrade_tool wl 0x16800 0x69000 output/image/rootfs.img
   ./tools/linux/Linux_Upgrade_Tool/upgrade_tool rd
   ```
   Второй аргумент `wl` — размер **всего раздела** в секторах (oem 30M =
   `0xF000`, rootfs 210M = `0x69000`), а не длина файла: tool стирает этот
   диапазон и пишет образ. Если передать длину образа (oem `0x9D00`, rootfs
   `0x1A500`), в хвосте останутся старые UBI-PEB с другим `image_seq`, и ядро
   упадёт с `ubi0 error: ubi_attach: bad image sequence number …` (panic).
   Maskrom нужен один раз: после `db` устройство остаётся в loader-режиме,
   поэтому следующие разделы прошиваются без повторного входа.
   `userdata` (LBA `0x11800`, 10M = `0x5000`) — только если менялось фабричное
   содержимое раздела.
4. `./rkflash.sh boot` (`di -b` + `rd`) прошивает ядро/DTB и сам перезагружает
   устройство — вызывать его последним, вместо отдельного `rd`.
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
(смещения и размеры по разделам, выполняется из U-Boot).

## 8. Диагностика

Чек-лист после загрузки (по SSH `root@192.168.52.150` или UART):

```sh
# маркер тестовой сборки (S99oko-test)
cat /tmp/oko-test-ok

# модули медиастека и сенсора
lsmod | grep -E 'sc230ai|rockit|rga3|rknpu|video_rkisp'

# сенсор определился, формат кадра читается
cat /proc/rkisp-vir0

# приложение запущено: divinus по умолчанию, rkipc — только при opt-out
ps | grep -E 'divinus|rkipc'
ls -l /oem/usr/bin/divinus /userdata/divinus.enable
ls -l /userdata/divinus.yaml /userdata/rkipc.enable 2>/dev/null
# opt-out на DUT: touch /userdata/rkipc.enable && reboot
curl -s -u admin:oko-change-me http://192.168.52.150/api/status
curl -s -u admin:oko-change-me -o /tmp/snap.jpg http://192.168.52.150/image.jpg
# OSD: reg0 — дата/время, reg1 — телеметрия, reg2 — сообщение из FIFO.
# Смотреть в RTSP-потоке (кадры `/image.jpg` кодируются отдельным VENC-каналом
# и OSD не содержат — ограничение RGN на RV1106).
# Два кадра с интервалом >1 с: меняются секунды; >16 с: GPS/T/BAT.
# На DUT (не с хоста): сервис сообщений — локальный FIFO.
echo 'OKO MESSAGE 123' > /tmp/osd.fifo
# через 60+ с reg2 гаснет: /tmp/osd-msg.txt пуст, /tmp/osd-msg.ts удалён
ls -l /tmp/osd-dyn.txt /tmp/osd-msg.txt /tmp/osd.fifo
# OSD с хоста: попарный диф регионов по RTSP, кадры и кропы в output/bench/
bench/run.sh osd_check.py --ip 192.168.52.150 --seconds 17
# сообщение в reg2: сначала на DUT, затем повторить osd_check.py
bench/run.sh serial_exec.py "echo 'OKO MSG TEST' > /tmp/osd.fifo"
# RTSP-корень divinus — без /live/0:
# ffprobe -rtsp_transport tcp rtsp://admin:oko-change-me@192.168.52.150:554/

# сеть
ifconfig eth0
ip route

# разделы смонтированы: type nfs, источник 192.168.52.200:/srv/nfs/rootfs/.nfsroot/*
# в консоли: netboot: oem from nfs … / netboot: userdata from nfs …
mount | grep -E ' /oem | /userdata '

# логи
ls -l /tmp/*.log /data/core-* 2>/dev/null
dmesg | tail -50
```

Типовые проблемы:

| Симптом | Вероятная причина |
|---|---|
| Нет загрузки по TFTP | не запущен контейнер/TFTP, неверный `serverip`, `boot.img` отсутствует в `output/image` |
| Ядро загрузилось, нет корня | не экспортирован `/srv/nfs/rootfs`, firewall, несовпадение `nfsroot` |
| `rkipc` не стартует | нет `rkipc.ini` для сенсора или не смонтирован `/oem` (см. `INIT_ORDER.md`) |
| `divinus` не стартует (ожидается по умолчанию) | есть `/userdata/rkipc.enable` (opt-out), нет `/oem/usr/bin/divinus` (сборка пакета упала молча), нет IQ-файла сенсора, `dlopen` не находит `librockit.so`/`librkaiq.so` (`LD_LIBRARY_PATH` без `/oem/usr/lib`) |
| `divinus`: нет картинки, AIQ-ошибки | геометрия в `divinus.yaml` не совпадает с режимом сенсора (SC230AI — 1920x1080), IQ не найден по имени модуля из `/proc/rkisp-vir0` |
| `divinus`: `mmap part 1 failed!`, пустые JPEG/RTSP | в сборке не применён патч `compat.c` (ABI `mmap`): на uClibc hard-float не должен определяться собственный 64-битный `mmap`; см. `project/app/divinus/README.md` |
| OSD: нет телеметрии/сообщений, `@/tmp/...` виден как текст | не применён патч `region.c` (`@file`-провайдер) или не запущен `oko-osd`: `ps` не показывает процесс, `/etc/init.d/S20oko-osd status` сообщает `not running`, файлов `/tmp/osd-*` нет |
| OSD: шрифт не найден | в `regN_font` указан отсутствующий шрифт; в rootfs есть только `/oem/usr/share/simsun_en.ttf` (`regN_font: simsun_en`) |
| `/userdata/divinus.yaml` перезаписан после загрузки | ожидаемо: `S20oko-osd` заменяет конфиг без `reg0_text` фабричным (старый формат) |
| Запущены и `divinus`, и `rkipc` | маркер появился после старта `rkipc`; перезагрузить DUT (ветки взаимоисключающие) |
| `netboot: nfs mount … failed, fallback` | `.nfsroot` не создан/не синхронизирован, нет доступа к TCP 2049, неверный `nfsroot` в cmdline |
| Нет `/dev/video*` | `insmod_ko.sh` не выполнился или `sc230ai.ko` отсутствует |
| `motor.ko` не грузится | ожидаемо: драйвер выключен в конфигурации |

## 9. Известные особенности

- `IMAGE/` содержит релиз для **RV1103 Pico Mini**; он не соответствует активной
  конфигурации `RV1106 Pro Max` и является устаревшим артефактом.
- Драйвер мотора `motor.ko` в прошивке отсутствует: `RK_ENABLE_MOTOR` не задан,
  строка `__insmod motor.ko` в `insmod_ko.sh` пропускается проверкой `-f`.
- `insmod_ko.sh` вызывается из `RkLunch.sh`, который запускает `S21appinit`;
  каталог `/oem/usr/etc/init.d/` пуст, вся логика приложения зашита в `post_chk`.
  Подробнее — `INIT_ORDER.md`.
- `post_chk` выбирает для `sc230ai` шаблон `/oem/usr/share/rkipc-300w.ini`
  (сенсор 3 МП). Если геометрия кадра окажется некорректной, ini подбирается отдельно.
- При netboot `oem` и `userdata` берутся с хоста из `/srv/nfs/rootfs/.nfsroot/`, а не из
  SPI NAND. Изменения, сделанные на устройстве в `/userdata`, попадают в это NFS-дерево;
  NAND-разделы не затрагиваются. При недоступности NFS скрипт откатывается на NAND.
- В netboot сеть статическая: адрес задаётся `ip=` в `sys_bootargs`, а
  `network_init` в `RkLunch.sh` не запускает `udhcpc` и не меняет MAC. Иначе смена
  адреса рвёт TCP-соединения NFS-корня и система зависает на жёстком NFS-монтировании.
  Штатный `S41dhcpcd` при этом продолжает работать и может добавить вторичный
  DHCP-адрес (`ip route` показывает его как `src`); статический `.150` и NFS-монтирования
  сохраняются.
- Docker использует `network_mode: host`, поэтому порт `69/udp` на хосте должен
  быть свободен.
- `divinus` — стример OpenIPC (MIT, `project/app/divinus/`), **по умолчанию**:
  маркер `/userdata/divinus.enable` создаёт `S20oko-osd`. Opt-out —
  `/userdata/rkipc.enable` (маркер divinus удаляется, запускается `rkipc`).
  RV11xx-HAL в upstream ещё «в разработке»: RTSP/fMP4/JPEG работают,
  аудио ограничено. Web-порт divinus — `80`, RTSP — `554`; пока divinus активен,
  web/RTSP rkipc не запускаются.
- OSD divinus включён в фабричном конфиге: `reg0` — дата/время (`$t`),
  `reg1` — эмуляция телеметрии (GPS/температура/батарея) раз в 15 с,
  `reg2` — внешний текст из `/tmp/osd.fifo` с TTL 60 с. Провайдер `@file` —
  локальный патч `region.c`; данные пишет демон `oko-osd`, сеть в канале не
  участвует. Шрифт — `/oem/usr/share/simsun_en.ttf` (`regN_font: simsun_en`):
  покрытие — ASCII `0x21–0x7e` плюс несколько CJK-иероглифов, кириллицы в шрифте
  нет, поэтому сообщения и телеметрию пишите латиницей.
  OSD виден в RTSP (VENC-канал 0); snapshots `/image.jpg` кодируются отдельным
  VENC-каналом и OSD не содержат — ограничение RGN на RV1106.
- `S20oko-osd` перезаписывает `/userdata/divinus.yaml` фабричным, если в нём нет
  `reg0_text` (конфиг старого формата). Ручные правки такого файла не переживают
  перезагрузку; добавьте `reg0_text` или отключите OSD в файле.
- divinus ищет конфиг рядом с исполняемым файлом, затем в текущем каталоге, затем
  `/etc/divinus.yaml`. `post_chk` запускает его из `/userdata`, поэтому сохранение
  конфига из WebUI (`/api/cmd?save`) остаётся в `/userdata` (файл `.bak` — там же).
- в `compat.c` divinus пропатчен ABI `mmap` (libc-вариант с 32-битным `off_t`),
  поэтому `LD_PRELOAD` не нужен. Если после обновления upstream в логе снова
  появился `mmap part 1 failed!`, значит патч `compat.c` не применён; см.
  `project/app/divinus/README.md`. Второй патч (`region.c`, `@file`-провайдер OSD)
  повторяется тем же порядком.

## 10. Правила ведения

1. Обновлять документ при любой смене железа, IP-адресов, путей, версии SDK или
   состава модулей.
2. При каждом обновлении менять «Дату обновления» в разделе 1.
3. Секреты не хранить: только плейсхолдеры `<...>` и публичные вендорские дефолты.
4. Расхождения между документом и скриптами трактовать в пользу скриптов
   (`bench/*.py`, `netboot-catch.py`, `start.sh`, BoardConfig) и сразу править документ.