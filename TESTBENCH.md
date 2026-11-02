# Тестовый стенд OKO

Паспорт и рабочие параметры лабораторного стенда для отладки прошивки Luckfox Pico
Pro Max (RV1106) с автономной загрузкой из SPI NAND. Значения сверены с `bench/*.py`,
`start.sh`, `docker-compose.yml` и активным BoardConfig на дату обновления.

## 1. Паспорт стенда

| Поле | Значение |
|---|---|
| Дата обновления | `2026-11-02` |
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
| Сетевые сервисы | не запускаются; сборка и прошивка выполняются локально |
| Запуск окружения | `./start.sh` |

## 4. Подключения

| Интерфейс | Значение | Примечание |
|---|---|---|
| UART | `/dev/ttyACM0` | `bench/serial_exec.py` берёт последний найденный `/dev/ttyACM*` |
| Скорость UART | `115200 8N1` | задана в скриптах `bench/` |
| Ethernet | лабораторная сеть `192.168.52.0/24` | через порт устройства |
| USB | `<порт / хаб>` | используется для Maskrom-прошивки |

Требования к хосту: Python 3 и зависимости из `bench/requirements.txt`;
`bench/setup.sh` создаёт venv в `output/bench/venv`, запуск — `bench/run.sh`.

## 5. Сеть

| Узел | Адрес |
|---|---|
| Устройство (DUT) | `192.168.52.65` (DHCP, факт на `2026-11-01`) |
| Маска | `255.255.255.0` |
| Шлюз | `192.168.52.254` |

В автономном режиме адрес `eth0` выдаётся по DHCP (`RkLunch.sh` → `udhcpc`).
Фактический адрес смотрите на DUT (`ifconfig eth0`) или в serial-консоли.
Для стабильных проверок зарезервируйте адрес `192.168.52.150` в DHCP-сервере
лабораторной сети.

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

## 7. Прошивка SPI NAND

### A. SPI NAND (автономная загрузка)

1. В контейнере: `./build.sh firmware`.
2. Результат: `output/image/update.img`.
3. Перевести устройство в Maskrom — `<кнопка / перемычка>`.
4. Прошить: `./rkflash.sh` или `tools/linux/Linux_Upgrade_Tool/upgrade_tool`.
5. Перезагрузить; загрузка пойдёт из SPI NAND без хоста.

### B. Покомпонентная прошивка изменённых разделов

Режим для выкатки изменённых разделов в SPI NAND без полного образа: прошиваются
только разделы, содержимое которых изменилось, остальные (включая `env`
с MAC-адресом) не трогаются. Уменьшает износ NAND и исключает сброс U-Boot env.

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
   `upgrade_tool di -oem/-rootfs`, но в чистом Maskrom (до `db`) таблицы разделов
   нет, и он отвечает `parameter is invalid,please check!`. Рабочий путь —
   запись по LBA с **полным стиранием раздела** (после `db` таблицу можно
   проверить через `upgrade_tool pl`):
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
4. `boot`, `idblock`, `uboot` и `env` этой фичей не менялись. При необходимости
   прошивайте их полным `./rkflash.sh update` (§7A) или отдельной проверенной
   процедурой; `di -b`/`di -uboot` в чистом Maskrom так же требуют загруженного
   через `db` loader.
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

Чек-лист после загрузки (по SSH `root@<DHCP-адрес DUT>` или UART; фактический
адрес — в §5):

```sh
# маркер тестовой сборки (S99oko-test)
cat /tmp/oko-test-ok

# модули медиастека и сенсора
lsmod | grep -E 'sc230ai|rockit|rga3|rknpu|video_rkisp'

# сенсор определился, формат кадра читается
cat /proc/rkisp-vir0

# режим сенсора: 1920x1080@10000/300000 (30 fps)
media-ctl -p | grep -A6 'sc230ai 4-0032' | grep fmt

# проверка потока (divinus должен быть остановлен): ожидается 30.0 fps
v4l2-ctl -d /dev/video0 --stream-mmap --stream-count=60 --stream-poll

# приложение запущено: divinus по умолчанию, rkipc — только при opt-out
ps | grep -E 'divinus|rkipc'
ls -l /oem/usr/bin/divinus /userdata/divinus.enable
ls -l /userdata/divinus.yaml /userdata/rkipc.enable 2>/dev/null
# opt-out на DUT: touch /userdata/rkipc.enable && reboot
curl -s -u admin:oko-change-me http://192.168.52.65/api/status
curl -s -u admin:oko-change-me -o /tmp/snap.jpg http://192.168.52.65/image.jpg
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
bench/run.sh osd_check.py --ip 192.168.52.65 --seconds 17
# сообщение в reg2: сначала на DUT, затем повторить osd_check.py
bench/run.sh serial_exec.py "echo 'OKO MSG TEST' > /tmp/osd.fifo"
# RTSP-корень divinus — без /live/0:
# ffprobe -rtsp_transport tcp rtsp://admin:oko-change-me@192.168.52.65:554/

# сеть
ifconfig eth0
ip route

# разделы смонтированы из SPI NAND (ubifs)
mount | grep -E ' /oem | /userdata '

# логи
ls -l /tmp/*.log /data/core-* 2>/dev/null
dmesg | tail -50
```

Типовые проблемы:

| Симптом | Вероятная причина |
|---|---|
| Нет загрузки после прошивки | не прошит `rootfs`/`oem`, повреждён UBI (неполное стирание раздела), неверная таблица разделов |
| `ubi0 error: ubi_attach: bad image sequence number` | раздел записан по длине образа, а не по полному размеру (`wl` вторым аргументом передан неверный размер) |
| `rkipc` не стартует | нет `rkipc.ini` для сенсора или не смонтирован `/oem` (см. `INIT_ORDER.md`) |
| `divinus` не стартует (ожидается по умолчанию) | есть `/userdata/rkipc.enable` (opt-out), нет `/oem/usr/bin/divinus` (сборка пакета упала молча), нет IQ-файла сенсора, `dlopen` не находит `librockit.so`/`librkaiq.so` (`LD_LIBRARY_PATH` без `/oem/usr/lib`) |
| `divinus`: нет картинки, AIQ-ошибки | геометрия в `divinus.yaml` не совпадает с режимом сенсора (SC230AI — 1920x1080), IQ не найден по имени модуля из `/proc/rkisp-vir0` |
| `divinus`: `mmap part 1 failed!`, пустые JPEG/RTSP | в сборке не применён патч `compat.c` (ABI `mmap`): на uClibc hard-float не должен определяться собственный 64-битный `mmap`; см. `project/app/divinus/README.md` |
| OSD: нет телеметрии/сообщений, `@/tmp/...` виден как текст | не применён патч `region.c` (`@file`-провайдер) или не запущен `oko-osd`: `ps` не показывает процесс, `/etc/init.d/S20oko-osd status` сообщает `not running`, файлов `/tmp/osd-*` нет |
| OSD: шрифт не найден | в `regN_font` указан отсутствующий шрифт; в rootfs есть только `/oem/usr/share/simsun_en.ttf` (`regN_font: simsun_en`) |
| `/userdata/divinus.yaml` перезаписан после загрузки | ожидаемо: `S20oko-osd` заменяет конфиг без `reg0_text`/`reg0_size`/`reg0_color`/`reg3_text` фабричным (старый формат); первый прежний файл сохраняется как `divinus.yaml.bak` |
| Запущены и `divinus`, и `rkipc` | маркер появился после старта `rkipc`; перезагрузить DUT (ветки взаимоисключающие) |
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
- В автономном режиме `oem` и `userdata` монтируются из SPI NAND (ubifs).
  Изменения, сделанные на устройстве в `/userdata`, сохраняются в NAND.
- Docker использует `network_mode: host`, но собственных сетевых сервисов
  не запускает.
- `divinus` — стример OpenIPC (MIT, `project/app/divinus/`), **по умолчанию**:
  маркер `/userdata/divinus.enable` создаёт `S20oko-osd`. Opt-out —
  `/userdata/rkipc.enable` (маркер divinus удаляется, запускается `rkipc`).
  RV11xx-HAL в upstream ещё «в разработке»: RTSP/fMP4/JPEG работают,
  аудио ограничено. Web-порт divinus — `80`, RTSP — `554`; пока divinus активен,
  web/RTSP rkipc не запускаются.
- OSD divinus включён в фабричном конфиге: время — в правом нижнем углу
  (`reg0`), телеметрия `T/BAT/LTE/WiFi` — четырьмя строками в левом верхнем
  (`reg1`), GPS — в правом верхнем (`reg2`), сообщения из `/tmp/osd.fifo` —
  в левом верхнем под телеметрией (`reg3`, TTL 60 с), CPU load / свободная
  память / температура CPU — тремя строками в левом нижнем углу (`reg4`).
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

## 10. Правила ведения

1. Обновлять документ при любой смене железа, IP-адресов, путей, версии SDK или
   состава модулей.
2. При каждом обновлении менять «Дату обновления» в разделе 1.
3. Секреты не хранить: только плейсхолдеры `<...>` и публичные вендорские дефолты.
4. Расхождения между документом и скриптами трактовать в пользу скриптов
   (`bench/*.py`, `start.sh`, BoardConfig) и сразу править документ.