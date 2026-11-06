# WebUI rework (OKO)

## Русский

### 1. Что было до переработки

Одна страница `project/app/divinus/source/res/index.html` (1448 строк), вшитая
в бинарник через `IMPORT_STR`, с пятью модальными окнами на скрытых checkbox:
`#live-modal`, `#media-modal`, `#network-modal`, `#osd-modal`, `#system-modal`
(меню — строки 1428–1432). Каждое окно — длинный список полей без разделения
на задачи; `Media` смешивал кодировщик, RTSP, OSD-геометрию и запись.

Проблемы, найденные при разборе (ссылки — на ревизию до переработки):

1. **Применение без проверки.** `apiApply()` (`index.html:170`) собирал все
   элементы с id-префиксом `endpoint_` и отправлял их `GET`-ом. Ответ
   разбирался только ради `dec.error`, а обратная связь — `alert()`
   (`index.html:186`, ещё 4 вызова). Успех печатался безусловно.
2. **Состояние после Apply не перечитывалось.** `apiRead()`
   (`index.html:230`) заполнял поля при загрузке, но после применения сервер
   мог изменить значения (гейты HDR/`osd.scope`, clamp’ы `record`), и UI
   показывал не то, что в конфиге.
3. **`send_file()` отдавал всю ФС** (`server.c:586`): любой несовпавший URI
   трактовался как абсолютный путь от `/`, проверка `..` (`server.c:703`) от
   этого не защищала, а в `Content-Type` подставлялся сам URI. `GET
   //etc/passwd` возвращал файл.
4. **URL-декодирование стояло до разделения `key=value`**
   (`unescape_uri`, 22 вызова, например `server.c:1046`): значение, которое
   начинается с закодированного `=`, терялось, а `+` в пароле становился
   пробелом.
5. **Пароли.** `rtsp_auth_pass`/`onvif_auth_pass` не отдаются в GET — это
   правильно, но старый UI после «применения» оставлял введённое значение в
   поле, создавая иллюзию сохранённого пароля.
6. **Не было эксплуатационных функций:** reboot, экспорт/импорт конфигурации,
   экспорт журнала, архив записей, телеметрия `oko-ec`, NTP, смена пароля
   WebUI, dirty-state.
7. **Выход из WebUI.** `/exit` останавливает сервер (`endpoints.md`), поэтому
   кнопку Logout делать нельзя; у Basic-auth нет сессии — это ограничение
   зафиксировано в §8.

Снимок экрана: `webui.png` в этой папке заменён на новый, а кадр до
переработки сохранён как `webui-before.png` (остальные четыре модалки
снимались в `output/bench/ui/before-*.png`).

### 2. Целевая структура

```
+--------------------------------------------------------------------------------+
| Divinus | IP 192.168.52.97 | Uptime 20:04 | Load 0.42 | 59.5 C | SD mounted | EN RU |
+----------+---------------------------------------------------------------------+
| Live     | Preview: [MP4] [MJPEG] [Stop] [Snapshot]                            |
| Image    | +---------------------+  +---------------------+                     |
| Streams  | | Recording           |  | Zoom / rotation     |                     |
| Events   | +---------------------+  +---------------------+                     |
| Storage  | +---------------------+  +---------------------+                     |
| Network  | | Event log (IVS)     |  | Telemetry (oko-ec)  |                     |
| OSD      | +---------------------+  +---------------------+                     |
| System   |                                                                     |
+----------+---------------------------------------------------------------------+
```

Восемь разделов: **Live, Image, Streams, Events, Storage, Network, OSD,
System**. Мобильная раскладка (<40em) превращает sidebar в горизонтальные
табы, статус-строка переносится, карточки идут в одну колонку.

### 3. Модель Apply / Save / Restart

1. При открытии раздела страница делает `GET` для каждого endpoint'а и
   запоминает значения (`snapshots`).
2. Правка любого поля включает `Apply` этой карточки и жёлтую точку в меню.
3. `Apply` отправляет `POST` только с полями своей карточки.
4. Ответ применяется к полям, `Apply` снова выключается. `note` со словом
   «restart» показывает баннер «требуется перезапуск».
5. `Save` (`/api/cmd?save`) пишет YAML, `Restart` (`/api/cmd?restart`) делает
   graceful `execvp`, `Reboot` (`/api/cmd?reboot`) перезагружает плату.

### 4. Разделы и используемые endpoint'ы

| Раздел | Endpoint'ы (GET для чтения, POST для применения) |
|---|---|
| Live | `/api/record` (пуск/стоп), `/api/eptz`, `/api/rotate`, `/api/ivs` (журнал), `/api/telemetry` |
| Image | `/api/isp`, `/api/night`, `/api/aiisp`, `/api/hdr`, `/api/iq`, `/api/focus` |
| Streams | `/api/mp4`, `/api/substream`, `/api/mjpeg`, `/api/jpeg`, `/api/audio`, `/api/venc`, `/api/rtsp`, `/api/onvif`, `/api/streams` |
| Events | `/api/ivs` (настройки и журнал), `/api/yolo` (NPU-детекция), `/api/models` (список, загрузка, активация, удаление) |
| Storage | `/api/storage`, `/api/record`, `/api/recordings`, `/api/recordings/file`, `/api/recordings/delete` |
| Network | `/api/network`, `/api/time` (`ntp`, `ntp_server`), `/api/vpn`, `/api/vpn/import`, `/api/vpn/export` |
| OSD | `/api/osd/{0..9}` (текст или картинка) |
| System | `/api/status`, `/api/time`, `/api/web-auth`, `/api/cmd` (`save`, `restart`, `reboot`, `export`, `log`), `/api/cmd/import`, `/api/flash`, `/api/firmware` |

Карточки OKO: **Network → VPN** (профиль `wg0`/`awg0`, бейдж типа, живое
состояние, импорт файлом или текстом, Connect/Disconnect/Autostart/Delete,
скачивание профиля) и **Image → Focus assist** (`enable`, `mode`, `fps`, ROI и
предпросмотр с рамкой, живой `metric_avg`/`cells_used`/`status`); также
**System → Controller firmware / Firmware versions** (target, файл,
`confirm=FLASH`, прогресс и журнал; строки STM32/STM8 с `state`, `version`,
`boot_version`, Refresh и автоопрос раз в 10 с). Прошивка выполняется
воркером, HTTP-поток не блокируется; при `busy` форма отключается.

### 5. Каталог функций ipcweb и матрица решений

Источник — исходники `project/app/ipcweb/ipcweb-backend` (`rest_api.cpp`,
регистрация по префиксам имён API) и `doc/IPC_RESTful_API_Guide.md`. Код не
переносился: бэкенд ipcweb привязан к `dbserver`/`mediaserver` и rockface.

| Функция ipcweb | Решение |
|---|---|
| Разделы WebUI, навигация, карточки настроек | **перенесено** — 8 разделов |
| `take-photo`, `start-record`, `stop-record` | **перенесено** — Snapshot и Live → Recording |
| Список записей, скачивание | **перенесено** — `/api/recordings`, `/file` |
| Export log | **перенесено** — `/api/cmd?log` |
| Export/import DB | **перенесено** как export/import YAML-конфигурации |
| Reboot | **перенесено** — `/api/cmd?reboot` |
| NTP | **перенесено** — `/api/time` + `ntp.c` |
| JWT-логин, уровни доступа, пользователи | **backlog** — вне плана; сейчас Basic-auth |
| Firmware upgrade через UI | **перенесено для контроллеров** — `/api/flash` (STM32/STM8), карточки Controller firmware и Firmware versions; полный `update.img` — backlog |
| Factory reset | **backlog** — нет безопасного сброса без физической кнопки |
| Планировщики (motion/intrusion/video-plan/screenshot) | **backlog** |
| Поиск архива по типам (black/white-list, snapshot) | **backlog** — сейчас список `.mp4`/`.jpg` |
| Privacy mask / ROI | **неприменимо** в этом образе — в divinus/rockface нет API |
| DDNS, PPPoE, WiFi-scan | **backlog** — WiFi-стек не проверялся |
| WASM-плеер wxplayer (~220 КБ gzip) | **неприменимо** — превью MP4/MJPEG без WebRTC |
| Журнал событий | **перенесено** — RAM-кольцо `/api/ivs`; файловый журнал — backlog |

### 6. Backlog

JWT и роли вместо Basic-auth, HTTPS, обновление прошивки из UI, factory reset,
планировщики, поиск по архиву и фильтры, DDNS/PPPoE/WiFi, privacy mask/ROI,
двусторонний звук, ONVIF PTZ, файловый журнал событий, экспорт диагностики
одним архивом.

### 7. Что изменено в `divinus`

- `POST` с `application/x-www-form-urlencoded` подставляется в `req->query`.
- `send_file()` отдаёт только файлы под `/mnt/sdcard/` (с `realpath`).
- `unescape_uri()` вызывается после разделения `key=value`.
- `/api/osd/{id}`: `color`/`outl` как `#RRGGBB`, JSON-экранирование
  `text`/`img`/`font`, пустые значения очищают поле, PNG-загрузка указывает
  регион на `/tmp/osdN.png`.
- `/api/status` дополнен `ip`, `sd_free_mb`, `sd_total_mb`.
- `/api/time?ts=` читает `long` (раньше `short` обрезал эпоху).
- Новые: `/api/cmd?reboot|export|log`, `/api/cmd/import`, `/api/web-auth`,
  `/api/recordings*`, `/api/telemetry`; NTP-ключи в YAML и `ntp.c`.
- Битый конфиг при старте откатывается из `divinus.yaml.bak` (один раз).
- В `compat.c` определена функция `__ctype_b`, которая через `-rdynamic`
  перекрывает таблицу ctype uClibc. Поэтому в новом коде (`valid_hostname`,
  `escape_url`, `onvif.c`) проверки символов сделаны сравнениями ASCII, а не
  `isalnum()`/`isspace()`: макрос ctype разыменовывает код как таблицу и падает.

### 8. Ограничения

- Basic-auth: нет выхода из сессии и HTTPS; пароль уходит с каждым запросом и
  хранится в `divinus.yaml` открытым текстом (модель upstream). `/exit`
  останавливает сервер — кнопки выхода нет.
- Пароли RTSP/ONVIF/WebUI не читаются через API: поля пустые, пустое значение
  не отправляется.
- RTSP-порт, аудиокодек, ONVIF и SmartP применяются после перезапуска.
- OSD-текст — только латиница (`simsun_en.ttf`), кириллицы в шрифте нет.
- NTP: при пустом `ntp_server` divinus не трогает демон из
  `/etc/init.d/S49ntp`; при заданном сервере останавливает его и запускает
  `ntpd -c /tmp/divinus-ntp.conf`; при `ntp_enable=false` останавливает. На
  старте это применяется через 10 с после запуска divinus — `S49ntp`
  выполняется позже `S21appinit`.
- `osd.scope=all` и HDR по-прежнему блокируются гейтами; UI показывает причину.
- Импорт конфигурации пишет файл и сохраняет предыдущий как `.bak`; битый файл
  откатывается при следующем старте.

## English

The page used to be a single document with five checkbox modals
(`#live-modal` … `#system-modal`, menu at lines 1428–1432 of the old revision).
`apiApply()` (`index.html:170`) collected every `endpoint_*` field into a `GET`,
ignored the outcome and used `alert()` for feedback; `apiRead()`
(`index.html:230`) was not re-run after an apply, so gated or clamped values
were shown incorrectly. `send_file()` (`server.c:586`) served any absolute path
and echoed the URI as the content type; `unescape_uri()` ran before the
`key=value` split; passwords kept the typed value after "apply"; and there were
no operational features (reboot, export/import, log, archive, telemetry, NTP,
password change).

The rework is an eight-section layout (**Live, Image, Streams, Events, Storage,
Network, OSD, System**) with a sticky status bar, an EN/RU switch (default
`en`), per-card `Apply` buttons enabled only when a field differs from the last
read state, toast feedback built from `reason`/`hint`, and `POST` form bodies for
every write. `GET` remains supported. New endpoints: `/api/cmd?reboot|export|log`,
`/api/cmd/import` (multipart), `/api/web-auth`, `/api/recordings` (+ `/file`,
`/delete`), `/api/telemetry`, and `ntp`/`ntp_server` on `/api/time`;
`/api/status` gained `ip`, `sd_free_mb`, `sd_total_mb`. Static serving is
restricted to `/mnt/sdcard/`, and an unparsable configuration is rolled back from
`divinus.yaml.bak` at startup. OKO cards: **Image → Focus assist** (enable, mode,
fps, ROI preview/overlay, live metric) and **System → Controller firmware /
Firmware versions** (STM32/STM8 upload with `confirm=FLASH`, progress and log;
cached versions with a 10 s refresh). Firmware runs in a worker so the HTTP
thread never blocks; the form is disabled while a job is busy.

The ipcweb catalog was used as a reference: take-photo/record control, archive
listing, log export, config export/import, reboot and NTP are ported; JWT and
user management, firmware upgrade, factory reset, schedulers, archive type
filters, DDNS/PPPoE/WiFi, two-way audio, ONVIF PTZ and a file-backed event log
are backlog; privacy mask/ROI and the WASM player do not apply to this image.

Limitations: Basic-auth has no logout and no HTTPS (passwords travel with every
request and are stored as plain text in `divinus.yaml`), RTSP/ONVIF/WebUI
passwords are never read back, RTSP port/audio codec/ONVIF/SmartP need a
restart, OSD text is Latin-only, NTP takes over ten seconds after boot (after
`S49ntp`), and HDR/`osd.scope=all` remain gated with the reason shown in the UI.