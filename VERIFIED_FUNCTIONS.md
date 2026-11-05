# Реестр проверенных функций OKO

Реестр фиксирует только то, что проверено на реальном стенде. Непроверенная
комбинация не считается работающей и либо отсутствует в таблице, либо явно
помечена как «ожидает проверки».

## 1. Паспорт проверки

| Поле | Значение |
|---|---|
| Дата | `2026-11-05` |
| Плата | Luckfox Pico Pro Max (RV1106), сенсор SC230AI |
| Прошивка | `output/image/update.img`, полная прошивка SPI NAND |
| Стенд | `192.168.52.97` (DHCP, SSH), UART `/dev/ttyACM0` 115200 |
| Методы | SSH, RTSP (PyAV), HTTP API (`curl`, `bench/api_check.py`), WebUI (`bench/ui_check.py`, headless Chromium), скриншоты в `output/bench/ui/` |
| Примечание | 2026-11-05 выполнен полный `./rkflash.sh update` образа с разметкой `userdata` 40 МБ / `rootfs` 168 МБ; вход в Maskrom — `download` из U-Boot по UART (см. `TESTBENCH.md` §7A) |
| Сборка | Docker, активный BoardConfig `BoardConfig-SPI_NAND-Buildroot-RV1106_Luckfox_Pico_Pro_Max-SC230AI.mk` |

## 2. Функции

| Функция | Статус | Доказательство | Ограничения |
|---|---|---|---|
| RTSP `/live/0`, H.265 1920x1080 | работает | PyAV: `codec=hevc`, 1920x1080, 60 кадров за 2.5 с; запись 6 с декодируется | Требуется включённая аутентификация RTSP |
| RTSP `/live/1`, H.265 640x360 | работает | PyAV: `hevc`, 640x360, 30 кадров; после `substream.enable=false` — `HTTPNotFoundError` 404, после `enable=true` — 10 кадров | При включённом IVS свободных VENC-каналов меньше; MJPEG должен быть выключен |
| ePTZ 1x/2x | работает | `GET /api/eptz?stream=main&enable=true&zoom=2` → `x=480,y=270,w=960,h=540`, поток декодируется 1920x1080; `reset=1` → полный кадр | Только кроп VPSS, без оптического зума |
| Поворот 90° | работает | `GET /api/rotate?stream=main&enable=true&angle=90` → PyAV видит 1080x1920; `enable=false&angle=0` → 1920x1080 | Реализован через VENC, не через VPSS |
| SVC, motion deblur, QP live | работает | `GET /api/venc?svc=true&motion_deblur=true&min_qp=20&max_qp=45`; поток hevc 1920x1080, 30 кадров без рестарта | Параметры действуют на работающий канал |
| SmartP после рестарта | не перепроверялся в этой прошивке | Ранее проверялось: `GET /api/venc?smartp=true`, рестарт, меняется GOP | Применяется только при следующем старте |
| AIISP с моделью | работает | В образе есть `rkmodel_1080_1920.aiisp` и `librkpostisp.so`; `GET /api/aiisp?enable=on` → `enable=on`, в `/proc/<pid>/maps` загружен `librkpostisp.so`, открыт `/dev/mpi/aiisp`, поток hevc 1920x1080 декодируется | Нужен `rkmodel_1080_1920.aiisp`; несовместим с HDR |
| YOLO (NPU) | работает (`vpss3`; `vi1` отклоняется) | 2026-11-05, YOLOv5n 640×640, 3 fps: NPU 28–33%; событие = 6 JPEG 640×640 с `X-Event-*` (42 кадра на приёмнике), `/api/yolo` live enable/disable (NPU 0↔33%), `/api/models` upload/hash/activate-rollback/delete, `api_check.py` 80/80 (включая HTTP 400 для `source=vi1` и `bogus`), RTSP/OSD 20 кадров без регрессии; лог атрибутов: `input 640x640 NHWC stride 640 px, 3 4D outputs` | `vi1` (selfpath) вешает плату и отклоняется `gating.c`/API/UI; модель обязана быть INT8 `.rknn` 1.6.0, 4D-NHWC и совпадать с `yolo.input_*`; librga непригодна — CPU-конверсия ~28% ядра на 3 fps; браузерный `ui_check` проходит |
| Разметка `userdata`/`rootfs` | работает | 2026-11-05: `/proc/cmdline` и `/proc/mtd` показывают `40M(userdata),168M(rootfs)`; `df -h /userdata` — 29.1 МиБ, `ubi5_0/data_bytes` = 35045376; загрузка `.rknn` ровно 3 МиБ через `/api/models` → HTTP 200, затем delete | Нужна полная прошивка `update.img`; `userdata` пересоздаётся |
| IVS MD: старт/стоп | работает | `GET /api/ivs?enable=true&mode=md`, затем `enable=false`; `/api/ivs` отвечает, процесс не падает | Событие движения на статичной сцене не наблюдалось — детекция ограничена сценой/настройками |
| OSD main + IVS + AIISP | работает (ранее) | OSD-регионы видны в RTSP main; в текущей сессии `SetBitmap`-ошибок в `logread` не найдено | `osd.scope=all` заблокирован; JPEG-снимок OSD не содержит |
| Запись H.265 + квота | работает | `GET /api/record?enable=true&start=1`, 6 с записи; файл в `/mnt/sdcard/recordings` содержит 150 кадров, 5.0 с, hevc 1920x1080, успешно декодируется | Нужна смонтированная microSD; текущий сегмент может превысить `max_bytes` до ротации |
| microSD: mount/read, `/api/storage` | работает | `GET /api/storage` → `sd_mounted=true`, `device=/dev/mmcblk1p1`, `fs=vfat`, `total_mb=15177`; `/run/oko-sd.state` создан udev-правилом | Запись разрешена только ниже `/mnt/sdcard` |
| WebUI: модели через браузер | работает | `bench/ui_check.py` с новым `oem`: список источников YOLO — только `vpss3`/`vi0`; файл `ui-test.rknn` загружается из карточки Models, активация показывает `rknn_init failed (-1): the model was probably built by an incompatible rknn-toolkit2`, удаление убирает строку; скриншоты в `output/bench/ui/` | Chromium требует `libasound.so.2`; без root он распаковывается из `apt-get download libasound2t64` |
| WebUI: разделы и API | работает | `GET /` → HTTP 200 (`<title>Divinus</title>`); `/api/status`, `/api/streams`, `/api/record`, `/api/storage` отвечают | Аутентификация — по настройкам `web_*` |
| WebUI 2026-11-05: 8 разделов, EN/RU, dirty-state | работает | `bench/ui_check.py`: все восемь разделов открываются, `#isp_mirror` включает `Apply` и точку в меню, после применения кнопка снова выключена, переключение на RU даёт «Видео», 9 плиток телеметрии, 10 OSD-регионов, нет горизонтальной прокрутки на 1440 и 390 px, нет ошибок консоли | Playwright + Chromium; установка браузера — см. `bench/README.md` |
| API-матрица 2026-11-05 | работает | `bench/api_check.py`: 80 проверок — 34 GET, 30 POST no-op, export/log, три 404 на статику, HTTP 400 для `yolo.source=vi1` и `bogus`, отказы delete (`..`, `x.txt`, пусто) и NTP (`bad..host`); 0 ошибок | Нужны учётные данные стенда |
| NTP через API | работает | `POST /api/time` с `ntp=off` останавливает `ntpd`; `ntp=on&ntp_server=pool.ntp.org` запускает `ntpd -g -c /tmp/divinus-ntp.conf -p /tmp/divinus-ntpd.pid`; пустой `ntp_server` возвращает вендорский `/usr/sbin/ntpd -g -p /var/run/ntpd.pid`; `bad..host` → `{"error":"blocked"}` | Не сохраняется без `/api/cmd?save` |
| NTP после перезагрузки | работает | Три reboot с сохранённым конфигом: `ntp_enable=false` → `pidof ntpd` пуст и `ntp_running=false`; `ntp_enable=true` + пустой сервер → `ntpd -g -p /var/run/ntpd.pid`; `ntp_server=pool.ntp.org` → `ntpd -g -c /tmp/divinus-ntp.conf -p /tmp/divinus-ntpd.pid`, файл содержит `server pool.ntp.org iburst` | Применяется через 10 с после старта (после `S49ntp`); `/api/time` и `date +%s` совпадают после `ts=` |
| Смена пароля спецсимволами | работает | Пароль `P@ss w=rd%1` (пробел, `=`, `%`, `@`): `POST /api/web-auth` → `ok`, старый 401, новый 200, в YAML `web_auth_pass: P@ss w=rd%1`; возврат к `oko-change-me` → 200 | Проверяет порядок `url_decode` после `split` |
| Запись и архив без карты | работает | `action=unmount` → `sd_mounted=false`; `/api/record?enable=true&start=1` → `blocked` «recording requires a mounted microSD card»; `/api/recordings` → `unavailable`; `action=mount` → `sd_mounted=true`, 15175 МБ свободно | Карта возвращена в строй без перезагрузки |
| Формат с неверным токеном | работает | `action=format&fs=fat32&confirm=FORMAT&token=deadbeef…` → `{"error":"blocked","reason":"format confirmation or token is invalid"}` | Разрушающий успешный прогон выполнялся ранее (см. строку «Формат microSD») |
| Телеметрия offline/online | работает | `S21oko-ec stop`, через 45 с `/api/telemetry` → `{"available":false,"age":45}`; `S21oko-ec start` → `available=true, age=1`, 9 плиток в UI | Порог — 30 с |
| MJPEG-предпросмотр | работает | `POST /api/mjpeg enable=true`, в headless Chromium клик MJPEG → `#preview img.naturalWidth > 0`; `bench/ui_check.py` сообщает `mjpeg preview painted: True` | При `enable=false` кнопка показывает сообщение; после теста MJPEG возвращён в `false` |
| MJPEG вместе с IVS | работает (частный случай) | `POST /api/ivs enable=true&mode=md` и `POST /api/mjpeg enable=true`, субпоток выключен: `/mjpeg` отдал 693 КБ за 4 с, предпросмотр в Chromium окрашен | Комбинация sub + MJPEG + IVS не проверялась; после теста IVS и MJPEG выключены |
| Архив записей и удаление | работает | `GET /api/recordings` → 1 файл; удаление тестового `zz_test_01.mp4` → `deleted`; повтор → `notfound`; `../../etc/passwd`, `x.txt`, пустое имя → `blocked`; активный сегмент → `blocked` | Активный сегмент защищён `record_active_path()` |
| Экспорт/импорт конфигурации | работает | `GET /api/cmd?export` → YAML 3399 байт с `ntp_*`; повторный импорт → `imported`; после `restart` файл на месте; импорт `web_port: bogus` → при старте `Configuration error 3; restoring the backup`, `web_port` снова 80, `/api/status` 200 | Откат одноразовый; битый `.bak` тоже приведёт к остановке |
| Смена пароля WebUI | работает | `POST /api/web-auth` со старым паролем → `ok`, старый пароль после этого 401, новый 200; `grep web_auth_pass /userdata/divinus.yaml` показывает новый; обратная смена возвращает 200 | HTTP Basic, сессии нет; страницу нужно перезагрузить |
| Телеметрия через API/WebUI | работает | `GET /api/telemetry` → `available=true`, `age=1`, поля `ec`/`ina226`/`sht21`; в UI 9 плиток | Данные старше 30 с → `available=false` |
| Статика только с microSD | работает | `GET //etc/passwd` → 404, `/tmp/divinus.yaml` → 404, `/mnt/sdcard/recordings/recording_20261104-153241.mp4` → 200 (2014174 байта), отсутствующий файл → 404 | **Ломающее** ограничение для сторонних скриптов |
| WebUI: MP4 preview `/video.mp4` | работает | `mp4.enable=true`; raw-ответ начинается с `ftyp`/`moov`, PyAV декодирует 30 кадров hevc 1920x1080 через `http://<DUT>/video.mp4` | Браузер должен поддерживать HEVC; иначе использовать MJPEG |
| WebUI: MJPEG preview `/mjpeg` | работает | `mjpeg.enable=true`; ответ `multipart/x-mixed-replace; boundary=boundarydonotcross`, 1.3 МБ JPEG-кадров за 4 с | При выключенном MJPEG endpoint отдаёт 404, вкладка показывает сообщение |
| HDR2/HDR3 | заблокировано | `GET /api/hdr?mode=hdr2` → `{"error":"blocked","reason":"HDR is not supported by the SC230AI build"}` | Не включать вручную в конфиге |
| `osd.scope=all` | заблокировано | Гейт G4 в `gating.c`; конфиг с `scope=all` возвращает причину | multi-attach RGN на RV1106 не прошёл проверку |
| Формат microSD из WebUI/API | работает | `GET /api/storage?action=format&fs=fat32&confirm=FORMAT&token=...` → `formatted=true`, `total_mb=15177`, `format_token` сменился; `fdisk -l` — один раздел `c W95 FAT32 (LBA)` 2048..31116287 | Разрушающая операция; блокируется при записи и без верного токена |
| microSD hotplug | работает (моделирование) | `echo mmc1:aaaa > .../mmcblk/unbind` → `present=0`, `not_mounted`; `bind` → `present=1`, автомонтирование `/dev/mmcblk1p1`; физическое извлечение не проверялось | udev-правило в `/lib/udev/rules.d`; vendor `61-sd-cards-auto-mount.rules` заменён заглушкой |
| I2C3-M1 100 кГц | работает | `hexdump -C /sys/class/i2c-adapter/i2c-3/of_node/clock-frequency` → `00 01 86 a0` (100000); `i2cdetect -y 3` видит `0x40`, `0x41`, `0x42`, `0x60`; `oko-ec` запущен | Устройства SHT21/INA3221/STM32 EC рассчитаны на 100 кГц |
| Ethernet: стабильный MAC | работает | MAC `02:47:8d:05:67:56` выводится из serial и хранится в env (`fw_printenv oko_mac`); после полной прошивки и reboot не меняется | `/userdata` и rootfs при полной прошивке пересоздаются, env — нет |
| Ethernet: DHCP | работает | `dhcpcd` (один клиент, client ID = MAC, без DUID), IP `192.168.52.97` сохраняется после reboot; `S41dhcpcd` — заглушка, `udhcpc` не запускается | При смене MAC/сети адрес может измениться один раз |
| Ethernet: static через WebUI/API | работает | `GET /api/network?mode=static&ipaddr=192.168.52.220&netmask=255.255.255.0&gateway=192.168.52.254&dns=192.168.52.254` → `.220`, маршрут/DNS применяются, `dhcpcd` остановлен; после reboot `.220` сохраняется; возврат `mode=dhcp` → `.97` | Ответ приходит до применения; страница может отключиться, нужен reconnect |
| Переразметка NAND 6M/40M/198M | работает | `cat /proc/mtd`: `boot 00600000`, `oem 02800000`, `userdata 00a00000`, `rootfs 0c600000`; `cmdline` содержит новую `mtdparts` | Требуется полная прошивка `update.img`; старые `/userdata` и таблица разделов несовместимы |
| Сенсоры SC230AI-only | работает | `ls /oem/usr/ko` → только `sc230ai.ko`; `lsmod` → только `sc230ai`; IQ → только `sc230ai_CMK-OT2115-PC1_30IRC-F16.json`; `sample_*`/`simple_*` отсутствуют | Вендорские DTS/исходники других плат не тронуты |

### Причина смены DHCP-адреса

До исправления MAC хранился в `/data/ethaddr.txt` (симлинк на `/userdata`), а
`dhcpcd` использовал DUID из `/var/db/dhcpcd`. Полная прошивка пересоздаёт и
`/userdata`, и rootfs, поэтому после каждой прошивки менялись и MAC, и DUID, и
DHCP-сервер выдавал новый адрес. Дополнительно на `eth0` работали два клиента:
`S41dhcpcd` и `udhcpc` из `RkLunch.sh`.

Исправление: MAC выводится из `/proc/device-tree/serial-number` и хранится в
env (`oko_mac`); DHCP-клиент один — `dhcpcd` с `clientid` по MAC (без `duid`);
`S41dhcpcd` заменён заглушкой; `RkLunch.sh` вызывает `oko-net apply`.

## 3. Как проверить

### RTSP

```sh
ffprobe -v error -rtsp_transport tcp \
  -show_entries stream=codec_name,width,height \
  rtsp://<user>:<pass>@<DUT_IP>/live/0
ffprobe -v error -rtsp_transport tcp \
  -show_entries stream=codec_name,width,height \
  rtsp://<user>:<pass>@<DUT_IP>/live/1
```

Краткая запись 5–10 с и проверка декодирования:

```sh
ffmpeg -v error -rtsp_transport tcp -i rtsp://<user>:<pass>@<DUT_IP>/live/0 \
  -t 8 -c copy /tmp/live0.mkv
ffprobe -v error -count_frames -show_entries stream=nb_read_frames /tmp/live0.mkv
```

### API

```sh
curl -s 'http://<DUT_IP>/api/status'
curl -s 'http://<DUT_IP>/api/streams'
curl -s 'http://<DUT_IP>/api/storage'
curl -s 'http://<DUT_IP>/api/record?enable=true&start=1'
curl -s 'http://<DUT_IP>/api/record?enable=true&stop=1'
```

Формат карты (разрушающая операция, только после подтверждения):

```sh
STATUS=$(curl -s 'http://<DUT_IP>/api/storage')
TOKEN=$(printf '%s' "$STATUS" | sed -n 's/.*"format_token":"\([0-9a-f]*\)".*/\1/p')
curl -s "http://<DUT_IP>/api/storage?action=format&fs=fat32&confirm=FORMAT&token=$TOKEN"
```

### WebUI и API-матрица (2026-11-05)

```sh
# 74 проверки: GET, POST no-op, вложения, негативные случаи
bench/run.sh api_check.py

# WebUI в headless Chromium: разделы, EN/RU, Apply/dirty, мобильная раскладка
python3 -m venv /tmp/pw && /tmp/pw/bin/pip install playwright
/tmp/pw/bin/playwright install chromium
LD_LIBRARY_PATH=/tmp/kilo/libasound/usr/lib/x86_64-linux-gnu bench/run.sh ui_check.py

# скриншоты разделов и мобильной версии
ls output/bench/ui/
```

Если Chromium не стартует с `libasound.so.2: cannot open shared object file`,
поставьте пакет `libasound2t64`/`libasound2` или распакуйте его в каталог и
запустите с `LD_LIBRARY_PATH`.

### Образ и разделы

```sh
strings output/image/env.img | grep mtdparts
# ожидается: 6M(boot),40M(oem),...,198M(rootfs)
```

На DUT после полной прошивки:

```sh
cat /proc/cmdline
cat /proc/mtd
# boot 0x600000, oem 0x02800000, rootfs 0x0c600000 (размеры в hex)
df -h /oem /mnt/sdcard
ls /oem/usr/ko | grep -E 'sc3336|mis5001|imx415|mia1321'   # пусто
ls /oem/usr/share/iqfiles                                   # только sc230ai_*.json
ls /oem/usr/bin/sample_* /oem/usr/bin/simple_* 2>/dev/null  # пусто
dmesg | grep -i sc230ai
lsmod | grep -E 'sc3336|mis5001|imx415|mia1321'             # пусто
```

### Сеть и I2C

```sh
hexdump -C /sys/class/i2c-adapter/i2c-3/of_node/clock-frequency
# ожидается: 00 01 86 a0  (100000 Гц)
i2cdetect -y 3

fw_printenv oko_mac oko_net_mode oko_ipaddr oko_netmask oko_gateway oko_dns
oko-net env
ps | grep -E 'dhcpcd|udhcpc' | grep -v grep

# GET и static
curl -s -u admin:oko-change-me http://<DUT_IP>/api/network
curl -s -u admin:oko-change-me \
  'http://<DUT_IP>/api/network?mode=static&ipaddr=192.168.52.220&netmask=255.255.255.0&gateway=192.168.52.254&dns=192.168.52.254'
# после reconnect на .220 вернуть DHCP; параметры IP можно оставить
curl -s -u admin:oko-change-me \
  'http://192.168.52.220/api/network?mode=dhcp&ipaddr=192.168.52.220&netmask=255.255.255.0&gateway=192.168.52.254&dns=192.168.52.254'
```

### Запись и защита от NAND

```sh
# без карты
curl -s 'http://<DUT_IP>/api/storage?action=unmount'
curl -s 'http://<DUT_IP>/api/record?enable=true&start=1'
# ожидается blocked и сообщение про microSD

# с картой
curl -s 'http://<DUT_IP>/api/storage?action=mount'
curl -s 'http://<DUT_IP>/api/record?enable=true&start=1'
sleep 5
curl -s 'http://<DUT_IP>/api/record?enable=true&stop=1'
ls -l /mnt/sdcard/recordings
ffprobe -v error -show_entries format=duration /mnt/sdcard/recordings/*.mp4
```

При извлечении карты во время записи файл должен закрыться, а новых файлов в
`/oem` и `/userdata` быть не должно.

## 4. Правило ведения

- Строка «работает» допустима только после проверки на стенде с указанной
  прошивкой; в доказательстве — команда, лог или артефакт.
- При изменении функциональности обновляйте строку, дату в §1 и доказательство.
- Непроверенное оформляйте как «ожидает проверки» или «заблокировано», а не как
  «работает».
- Ограничения и известные дефекты не удаляйте при обновлении: заменяйте на
  актуальные.