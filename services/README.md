# Сервисы стенда OKO

Один контейнер для сетевых сервисов стенда:

- **TFTP** — отдаёт `output/image` (там лежат `boot.img` и `update.img`);
- **NFS** — экспортирует `output/srv/nfs/rootfs` для netboot-цикла;
- **опционально** — WireGuard, AmneziaWG, OpenVPN по готовому паттерну.

Отдельно живут профили `services/stream/` (приёмник UDP/RTP MediaMTX),
`services/events/` (приём событий камеры), `services/mqtt/` (брокер MQTT и
JSON-рекордер) и `services/converter/`
(web-конвертер ONNX → RKNN); они не входят в этот контейнер и поднимаются
своими профилями compose.

Контейнер работает с `network_mode: host` и `privileged: true`: он использует
общий с хостом модуль ядра `nfsd` и устройство `/dev/net/tun`. Путь экспорта
внутри контейнера — `/srv/nfs/rootfs`, поэтому строки `nfsroot=` на DUT и
скрипты `bench/` не меняются; на хосте данные лежат в `output/srv/nfs/`.

## Быстрый старт

Требования: `docker.io` с плагином `docker compose`, пользователь в группе
`docker`. `sudo` для запуска не нужен.

```sh
tools/native/setup_netboot.sh          # сборка + запуск
docker compose -f services/docker-compose.yml ps
docker logs -f oko-services            # логи всех программ
```

Скрипт создаёт `output/srv/nfs/rootfs` и `output/srv/vpn`, собирает образ и
поднимает контейнер. Пересборка после правок `Dockerfile`/`conf/`:

```sh
docker compose -f services/docker-compose.yml up -d --build
```

## Проверка

```sh
docker exec oko-services exportfs -v
docker exec oko-services rpcinfo -p localhost
docker exec oko-services showmount -e localhost
ss -lun 'sport = :69'
ss -tln 'sport = :2049'
curl tftp://192.168.52.200/boot.img -o /tmp/boot.img
cmp /tmp/boot.img output/image/boot.img
```

Полная матрица NAND ↔ NFS — `bench/run.sh nfs_env.py on|off` и
`bench/run.sh netboot.py catch` (см. `TESTBENCH.md` §7D).

## Раскладка данных

| Хост | В контейнере | Режим |
|---|---|---|
| `output/image` | `/srv/tftp` | ro |
| `output/srv/nfs` | `/srv/nfs` | rw |
| `output/srv/vpn` | `/etc/oko-secrets` | ro |
| `services/conf` | `/etc/oko-services` | ro |
| `services/conf/exports` | `/etc/exports` | ro |

Rootfs, `oem` и `userdata` в NFS-экспорте раскладываются так же, как раньше:
`output/srv/nfs/rootfs/` и `output/srv/nfs/rootfs/.nfsroot/{oem,userdata}/`.

## Добавление сервиса (паттерн)

1. Положить профиль в `conf/<service>/`.
2. Добавить `conf/programs/<service>.ini`: `autostart=true`, команда в
   foreground, `stdout_logfile=/dev/stdout`, `redirect_stderr=true`.
3. Перезапустить: `docker compose -f services/docker-compose.yml restart`.

Готовые неактивные профили: `programs/wg.ini`, `programs/awg.ini`,
`programs/openvpn.ini` (`autostart=false`). Включение — `autostart=true` или
`docker exec oko-services supervisorctl start <program>`.

Секреты (ключи, сертификаты, CRL) в git не хранятся: только
`output/srv/vpn/` (gitignore), в контейнере — `/etc/oko-secrets/`.
Публичные вендорские дефолты стенда допустимы.

## Приёмник UDP/RTP: `services/stream/`

Отдельный профиль, а не программа внутри `oko-services`: это сторонний образ
MediaMTX с host-сетью, которому не нужны ни TFTP, ни NFS, ни privileged-режим.
Так `oko-services` остаётся одним самодостаточным контейнером, а приёмник
поднимается и гасится независимо от netboot-цикла.

```sh
docker compose -f services/stream/docker-compose.yml up -d
docker compose -f services/stream/docker-compose.yml logs -f
docker compose -f services/stream/docker-compose.yml down
```

- Образ закреплён: `bluenviron/mediamtx:1.21.3-ffmpeg`. Вариант `-ffmpeg` нужен,
  потому что на хосте нет `ffprobe`/`ffmpeg`, а контейнер используется и как
  инструмент анализа (`docker exec oko-mediamtx ffprobe ...`).
- Сеть `host`: камера шлёт RTP на `192.168.52.200:5600`, читатели идут на
  `8554` (RTSP), `8888` (HLS), `8889` (WebRTC). Control API — только
  `127.0.0.1:9997`.
- Путь `cam` описан в `services/stream/mediamtx.yml`: `source: udp+rtp://:5600`
  и `rtpSDP` с `H265/90000`, потому что divinus отправляет «сырой» RTP без SDP.
- Аутентификация — стоковая: пользователь `any` имеет `publish/read/playback`,
  `api/metrics/pprof` ограничены localhost. Это допустимо только для
  лабораторного стенда: RTSP отдаётся без пароля.
- Проверка цепочки: `bench/run.sh stream_check.py --latency --compare-dut`
  (см. `bench/README.md`).
- **Браузер и H.265.** Chromium не декодирует HEVC: WebRTC-сессия закрывается
  с `codecs not supported by client`, а HLS приходит как `hvc1` и в MSE не
  играет. H.265 играется в Safari или нативном плеере. Для браузерного
  просмотра переключите камеру на H.264 (`mp4.codec: H.264` в
  `/userdata/divinus.yaml`, затем `kill -HUP $(pidof divinus)`) и замените
  `a=rtpmap:96 H265/90000` на `H264/90000` в `services/stream/mediamtx.yml`
  (`docker compose -f services/stream/docker-compose.yml restart`). Проверено
  2026-11-10: WebRTC играет 1080p30 в Chromium, HLS — H.264.

## Сервис событий: `services/events/`

Отдельный профиль `oko-events` (порт **8090**, `network_mode: host`) принимает
пакеты событий камеры и хранит их в `output/srv/events`:

- `PUT /ingest/frame` — JPEG-кадр с заголовками `X-Event-Id`,
  `X-Frame-Index`, `X-Frame-Total`, `X-Frame-Ts`, `X-Event-Cause`, `X-Model`;
- `PUT /ingest/clip` — fMP4 H.265 с `X-Event-Id`, `X-Cause`, `X-Model`,
  `X-Duration-Ms`, `X-Ts`;
- `PUT /ingest/heartbeat` — proof-of-life JPEG из тихого цикла сна с
  `X-Heartbeat`, `X-Heartbeat-Ts`, `X-OKO-Idle-S`, `X-OKO-Motion-Area`,
  `X-OKO-Sleep`, `X-OKO-Wake-Cycle`; файл и запись складываются в
  `heartbeat/<ГГГГ-ММ-ДД>/` (`hb-<ts>.jpg`, `heartbeat.json`), событийный
  архив и очередь ffmpeg не затрагиваются;
- файлы события: `<ГГГГ-ММ-ДД>/<event_id>/` (`meta.json`, `frame-NNN.jpg`,
  `clip.mp4`), плюс `clip-h264.mp4` и `clip.jpg` после транскодинга
  (ffmpeg, `libx264 veryfast`, `+faststart`);
- `GET /api/events?limit=`, `GET /api/events/<id>`, `GET /media/...`,
  `GET /api/stats`, минимальный HTML-список на `/`;
- `GET /api/heartbeats?limit=&expected_s=` — последние heartbeat-записи
  (новые первыми) и число пропущенных интервалов `missed` относительно
  `expected_s` (0 — не считать); ссылка на JPEG — `/media/heartbeat/<дата>/<файл>`;
- retention: `EVENTS_MAX_GB` (по умолчанию 2 ГБ), удаляются старые даты;
- опциональный `OKO_EVENTS_TOKEN`: при непустом значении требуется заголовок
  `X-OKO-Token`. На стенде токен не задан.

```sh
docker compose -f services/events/docker-compose.yml up -d --build
docker compose -f services/events/docker-compose.yml logs -f
curl -fsS http://127.0.0.1:8090/health
curl -fsS http://127.0.0.1:8090/api/events | head
docker compose -f services/events/docker-compose.yml down
```

Камера настраивается в `/userdata/divinus.yaml`:

```yaml
http_post:
  enable: true
  host: 192.168.52.200:8090
  url: /ingest/frame
event_clip:
  enable: true
  pre_seconds: 5
  post_seconds: 10
  max_bytes: 16777216
```

Те же ключи настраиваются из WebUI камеры: Events → «Отправка событий
(HTTP)» (живые `GET/POST /api/http_post`) и «Ролики событий» (`POST /api/clip`).
Новый ключ `http_post.receiver_url` задаёт ссылку на этот сервис для карточки;
при пустом значении ссылка строится из `host` с портом 8090. В карточке также
раскрывается встроенное описание протокола (`<details>`): заголовки кадров и
клипов, повторы, outbox и эндпоинты приёмника. `enable` в карточке управляет
только периодическими снимками — событийные пачки и ролики уходят при заданном
`host` по решениям IVS/YOLO/`event_clip`.

Клип уходит на `/ingest/clip` (последний сегмент `frame` заменяется на
`clip`); JPEG-пачка не меняется. Для клипа путь берётся из `url` без
`strftime`-подстановок, поэтому держите его литеральным (`/ingest/frame`). Для браузера сервис отдаёт H.264-копию,
потому что Chromium не играет H.265. Сквозная проверка —
`bench/run.sh event_check.py` (`--retry`, `--outbox`, `--require-detection`).
Если сервис недоступен, камера повторяет отправку (30 с / 2 мин / 10 мин),
затем кладёт клип в outbox на microSD (`/mnt/sdcard/oko-outbox`, квота
256 МиБ); без карты событие теряется и учитывается в `/api/clip` (`lost`).

Heartbeat сна (`[sleep] heartbeat: true`) идёт на тот же `host` и путь,
производный от `http_post.url` (`/ingest/frame` → `/ingest/heartbeat`), но
мимо событийного архива. Последние записи и разрывы:

```sh
curl -fsS 'http://127.0.0.1:8090/api/heartbeats?limit=20&expected_s=60' | head
# {"heartbeats":[{"ts":1763456789000,"date":"2026-11-18","idle_s":12,...}],
#  "count":20,"expected_s":60,"missed":1}
```

Сквозная проверка — `bench/run.sh heartbeat_check.py` (тихие циклы, motion,
`--neg` при выключенном приёмнике).

## Брокер MQTT и рекордер: `services/mqtt/`

Профиль `oko-mqtt` поднимает два контейнера с `network_mode: host`:

- `oko-mqtt-broker` — `eclipse-mosquitto:2.0.20` на **1883** с обязательной
  аутентификацией. Пароли задаются переменными окружения, в git попадают
  только стендовые дефолты: устройство `oko`/`oko`, рекордер `oko-rec`/`oko-rec`
  (`OKO_MQTT_USER`, `OKO_MQTT_PASS`, `OKO_MQTT_REC_USER`, `OKO_MQTT_REC_PASS`).
  `conf/entrypoint.sh` генерирует `mosquitto.passwd` в `output/srv/mqtt/broker/`,
  ACL (`conf/acl.conf`): устройство пишет и читает `oko/#`, рекордер — только
  читает. TLS нет.
- `oko-mqtt-recorder` — HTTP-API на **8092** (`python:3.12-slim`, пины
  `fastapi==0.115.6`, `uvicorn==0.34.0`, `paho-mqtt==2.1.0`), подписка на
  `oko/#`, архив в `output/srv/mqtt/recorder/`:
  `events/<ГГГГ-ММ-ДД>.jsonl`, `telemetry/<id>.json`, `status/<id>.json`,
  `messages.log.jsonl`; retention `MQTT_REC_MAX_MB` (по умолчанию 16 МиБ).

Темы и payload (JSON, без картинок):

- `oko/<id>/event` — QoS1, по одному сообщению на событие:
  `{"type":"motion|yolo|clip|heartbeat","event_id":"...","cause":"...",`
  `"model":"...","ts_ms":N,...}`; у `clip` есть `duration_ms`/`bytes`, у
  `motion` — `area_permille`, у `heartbeat` — `idle_s`,
  `motion_area_permille`, `sleep`, `wake_cycle`;
- `oko/<id>/telemetry` — QoS0, retained, `{"ts":N,"available":bool,"data":…}`
  из `/tmp/oko-ec.json` (устаревший файл — `available:false`,`age`);
- `oko/<id>/status` — retained `{"online":true,"fw":"…"}`, LWT брокера —
  `{"online":false}`.

```sh
docker compose -f services/mqtt/docker-compose.yml up -d --build
docker compose -f services/mqtt/docker-compose.yml logs -f
curl -fsS http://127.0.0.1:8092/health
curl -fsS 'http://127.0.0.1:8092/api/events?limit=20'
# брокер видит сообщения только с паролем (без пароля — отказ):
docker exec oko-mqtt-broker mosquitto_sub -h 127.0.0.1 -u oko -P oko \
  -t 'oko/#' -W 10 -C 1
docker compose -f services/mqtt/docker-compose.yml down
```

Рекордер отдаёт `/health`, `/api/messages?topic=&limit=`, `/api/events?limit=`,
`/api/telemetry`, `/api/status` и минимальную HTML-страницу на `/`.
Камера настраивается в `[mqtt]` (`host: 192.168.52.200:1883`, `enable: true`)
или живьём через WebUI → Events → «MQTT (события, телеметрия)»
(`GET/POST /api/mqtt`). Сквозная проверка — `bench/run.sh mqtt_check.py`
(`--neg` при выключенном профиле; bench-событие `POST /api/mqtt?test=1`
требует запуска divinus с `OKO_BENCH_API=1`).

## Web-конвертер RKNN: `services/converter/`

Отдельный профиль `oko-converter` (порт **8091**, `network_mode: host`):
браузерная форма принимает ONNX-модель и 20–200 калибровочных JPEG, проверяет
статический вход **640×384** (NCHW `[1,3,384,640]` или NHWC `[1,384,640,3]`) и
конвертирует модель официальным `rknn-toolkit2` **2.5.0** для `rv1106` в INT8
с калибровкой. FP16 и квантование без dataset вендорский toolkit для rv1106
отклоняет (`not support do_quantization = False`; `dataset ... is None`),
поэтому INT8 с 20–200 JPEG — единственный собираемый режим. Параметры
`rknn.config` совпадают с `tools/rknn-toolkit2/convert.py`, поэтому web- и
CLI-путь дают модели с одинаковыми параметрами и размером (побайтового
совпадения нет: toolkit пишет в шапку адреса/указатели).

```sh
docker compose -f services/converter/docker-compose.yml up -d --build
docker compose -f services/converter/docker-compose.yml logs -f
curl -fsS http://127.0.0.1:8091/healthz
# браузер: http://192.168.52.200:8091/
```

- **Один слот конверсии.** Каждая задача — дочерний процесс
  `worker/convert.py` с таймаутом 15 мин; вторая задача ждёт в очереди
  (лимит `queued` — 10, ответ `409`). Квантование пиково ест 2–3 ГБ RAM,
  поэтому два процесса одновременно не запускаются.
- **Состояние на диске.** Задачи лежат в `output/srv/converter/jobs/<id>/`
  (`meta.json`, `model.onnx`, `calib/`, `model.rknn`, `log.txt`). После
  рестарта контейнера задачи в статусе `queued`/`running` становятся
  `interrupted`, файлы сохраняются. Завершённые задачи вытесняются по
  `CONVERTER_MAX_GB` (по умолчанию 2; для теста можно задать
  `CONVERTER_MAX_MB`).
- **API** (все ответы — JSON, кроме `/` и скачивания): `POST /api/jobs`
  (multipart `model`, `images[]`, `dtype`), `GET /api/jobs`,
  `GET /api/jobs/<id>` (meta + хвост лога), `POST /api/jobs/<id>/cancel`,
  `GET /api/jobs/<id>/download`, `DELETE /api/jobs/<id>` (только завершённые),
  `GET /healthz`.
- **Лимиты и допуски.** Загрузка ONNX — до `CONVERTER_MAX_UPLOAD_MIB`
  (256 МиБ, ответ `413`); требуется 20–200 JPEG (без них rv1106-квантование
  невозможно), `dtype=fp16` отклоняется с объяснением; динамический вход и
  размер не 640×384 отклоняются с текстом-подсказкой (`422`). Размер файла
  результата не ограничен квотой борта: загрузка на DUT — отдельная операция.
- **Токен.** При непустом `OKO_CONVERTER_TOKEN` все `/api/*` и `/healthz`
  требуют заголовок `X-OKO-Token`; UI хранит его в localStorage. На стенде
  токен по умолчанию не задан. Секреты в git не кладутся — только env.
- **Проверочный fixture.** `tools_fixture/make_fixture.py` собирает
  двухслойный ONNX 640×384 и 25 JPEG. Запуск без установки Python-зависимостей
  на хост:

  ```sh
  docker run --rm -v "$PWD/output/srv/converter:/out" \
      --entrypoint python oko-converter:latest \
      /app/tools_fixture/make_fixture.py --out /out/fixtures
  ```

- **Проверено на реальной модели.** `output/rknn/yolo11n-640x384.onnx`
  (11 МБ, вход `images` `[1,3,384,640]`, 9 выходов) собран в `.rknn`
  3 905 265 Б с 25 синтетическими JPEG; скачивание совпадает по sha256.
- **Совместимость.** Модель собрана toolkit **2.5.0** и рассчитана на
  мини-рантайм `librknnmrt.so` **2.5.0** на борту; текущая плата несёт
  1.6.0/2.3.2, поэтому такая модель на ней не загрузится. Проверка на
  rv1106 этим сервисом **не выполняется** — только конверсия и скачивание.
- Образ большой (~2.5–3 ГБ: Torch и зависимости toolkit), собирается один раз
  и кешируется. Вендорское колесо ставится с `--no-deps` и закреплённым
  sha256 — как в `tools/rknn-toolkit2/Dockerfile.toolkit2`.

## Оговорки

- **AmneziaWG** требует модуль ядра хоста (`modprobe amneziawg`, DKMS вне
  контейнера) и `amneziawg-tools`, которых нет в Debian trixie — их нужно
  добавить в `Dockerfile`. Для DUT модуль собирается из
  `sysdrv/drv_ko/amneziawg`.
- **WireGuard** встроен в ядро Debian, `wireguard-tools` уже в образе;
  `wg-quick` настраивает интерфейс и завершается, поэтому программа остаётся в
  состоянии `EXITED` (это нормально). Остановка — `wg-quick down wg0`.
- **OpenVPN** работает в userspace, отдельный модуль ядра не нужен.
- **Остановка контейнера при смонтированном на DUT NFS-root** вешает клиента
  (`/` перестаёт отвечать). Восстановление — по разделу «Аварийное
  восстановление» в `TESTBENCH.md`: поймать U-Boot по UART и выполнить
  `bench/run.sh nfs_env.py off` либо вернуть NAND-строку `netboot.py cmd`.
- **Порты 69/111/2049** занимает контейнер. Хостовые `tftpd-hpa`,
  `nfs-kernel-server` и `rpcbind` должны быть остановлены и удалены, иначе
  контейнер не сможет занять порты (см. ниже).
- **ufw** хоста не трогаем: контейнер слушает те же порты в host-сети. Если
  ufw включён, нужны правила `69/udp`, `111/tcp+udp`, `2049/tcp+udp`,
  `20048/tcp+udp`.

## Переход с хостовых сервисов

Выполняется один раз, требует `sudo` и доступного DUT (гейт — зелёная матрица
`nfs_env.py on/off`). На текущем стенде переход выполнен 2026-11-08; ниже —
воспроизводимый порядок для нового хоста или отката. Данные сначала
копируются, хост-сервисы удаляются только после успешной проверки контейнера:

```sh
# 1. копия экспорта с сохранением владельцев/ACL/xattr (root внутри контейнера)
docker compose -f services/docker-compose.yml build
docker run --rm --entrypoint rsync \
  -v /srv/nfs:/src:ro -v "$PWD/output/srv:/dst" oko-services:local \
  -aHAX /src/rootfs/ /dst/rootfs/

# 2. освободить порты
sudo systemctl stop tftpd-hpa nfs-server nfs-mountd rpcbind.socket rpcbind

# 3. поднять контейнер и проверить TFTP/NFS + матрицу стенда
tools/native/setup_netboot.sh
bench/run.sh nfs_env.py on --host 192.168.52.97
bench/run.sh nfs_env.py off --host 192.168.52.150

# 4. только после зелёной матрицы — удалить хостовый стек
sudo apt-get purge --autoremove tftpd-hpa nfs-kernel-server nfs-common
sudo rm -f /etc/systemd/system/tftpd-hpa.service.d/oko.conf
sudo rm -rf /etc/systemd/system/tftpd-hpa.service.d
sudo setfacl -b output/image
sudo rm -rf /srv/nfs
```

Откат: `sudo apt-get install tftpd-hpa nfs-kernel-server` и прежний скрипт из
git (`git show <коммит-до-контейнера>:tools/native/setup_netboot.sh`); данные в
`output/srv/` при этом целы.