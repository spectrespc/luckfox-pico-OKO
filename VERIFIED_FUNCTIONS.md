# Реестр проверенных функций OKO

Реестр фиксирует только то, что проверено на реальном стенде. Непроверенная
комбинация не считается работающей и либо отсутствует в таблице, либо явно
помечена как «ожидает проверки».

## 1. Паспорт проверки

| Поле | Значение |
|---|---|
| Дата | `2026-11-07` |
| Плата | Luckfox Pico Pro Max (RV1106), сенсор SC230AI |
| Прошивка | `output/image/update.img` (sha256 `33a86d50…`); полная прошивка SPI NAND 2026-11-07 из слитого дерева, после загрузки восстановлены модели через `/api/models` |
| Стенд | `192.168.52.97` (DHCP, SSH), UART `/dev/ttyACM0` 115200 |
| Методы | SSH, RTSP (PyAV), HTTP API (`curl`, `bench/api_check.py`), WebUI (`bench/ui_check.py`, headless Chromium), скриншоты в `output/bench/ui/` |
| Примечание | 2026-11-05 выполнен полный `./rkflash.sh update` образа с разметкой `userdata` 40 МБ / `rootfs` 168 МБ; вход в Maskrom — `download` из U-Boot по UART (см. `TESTBENCH.md` §7A). 2026-11-06 полный `update.img` не заливался: USB OTG не подключён, компоненты `oem`/`rootfs` обновлены по SSH; образ собран из этих же исходников (`output/image/update.img`) |
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
| Драйвер NPU `rknpu` 0.9.8 | работает | 2026-11-06: импорт upstream `276af44f` (0.9.8/20240828), hot-swap на DUT, `dmesg` — `v0.9.8`, non-IOMMU; YOLO 640×384 @5 fps — main 30–31 fps, NPU 17%, AIISP on — main 30.1 fps, NPU 17%, RSS 19.5 МБ, `CmaAllocated` 52 МБ; три перезагрузки с загрузкой 0.9.8 из `/oem`; 2-часовой soak с YOLO и AIISP без утечек и зависаний; `api_check.py` 88/88, `osd_check.py` 17 с | Рантайм `librknnmrt.so` 1.6.0 не менялся; модель 2.x — отдельный этап; ночная AIISP-сцена на стенде не проверялась |
| Второй рантайм RKNN 2.3.2 | работает (mAP не измерен) | 2026-11-07: `librknnmrt2.so` (SONAME изменён) загружается рядом с `librknnmrt.so` 1.6.0; модель `yolov5n-640x384-r2.rknn` (toolkit 2.3.2) активирована на `yolo.runtime: 2` — `running:true`, NPU 17%, main 30.1 fps, AIISP on; переключение `runtime=1` ↔ `runtime=2` без рестарта; активация модели «чужого» рантайма отклонена с причиной; 2-часовой soak с AIISP 1.6.0 + YOLO 2.x | mAP 2.x-модели на размеченном наборе не измерялся; вендорское колесо только cp38; ночная сцена не проверялась |
| RGA NV12→RGB888 (`yolo.convert: rga`) | работает | 2026-11-07: `rgaImDemo --copy` (CMA fd) и отдельный тест MMZ fd → `imcvtcolor` NV12→RGB888 (`IM_STATUS_SUCCESS`); `divinus` CPU 35.6% → 29.4% при 5 fps 640×384, main 32.1 fps (CPU-путь 30.1), NPU 17%, AIISP on; 2-часовой soak без зависаний: RSS вырос разово на первом событии (12.9→20.1 МБ) и последние 45 мин не менялся | Легаси `c_RkRgaBlit` и блит по `malloc` не используются; RGA-путь требует `/oem/usr/lib/librga.so`, иначе автоматический откат на CPU |
| YOLO (NPU) | работает (`vpss3`; `vi1` отклоняется) | 2026-11-05, модель 640×384 INT8 (2.6 МиБ) активирована: `input 640x384 NHWC stride 640 px, 3 4D outputs (48x80x255, 24x40x255, 12x20x255)`; 5 fps — NPU 17%, CPU `divinus` ~28.5% (без YOLO ~7%), main 30.3 fps; 640×640 @5 fps — NPU 28%, CPU ~66%; событие проверено на демо 640×640 (6 JPEG с `X-Event-*`), `/api/yolo` live enable/disable, `/api/models` upload/hash/activate/delete, `api_check.py` 80/80, RTSP/OSD 20 кадров без регрессии, браузерный `ui_check` проходит; симулятор на `bus.jpg` — 3 person + 1 truck | `vi1` (selfpath) вешает плату; модель не входит в образ, после прошивки загружается через `/api/models`; mAP на валидационном наборе не измерен; для другой модели подбирать `conf_threshold` |
| Рамки детекции на потоке (`yolo.boxes`) | работает (ограничения) | 2026-11-07: `GetCanvasInfo`/`UpdateCanvas` доступны в librockit, регион `12` создан как `1920×1088 2BPP` (layer 6) и виден в `/live/0`; синтетические боксы подтвердили геометрию и оба цвета (класс 0 — красный, прочие — синий), реальные детекции при `conf_threshold=0.05` дали рамки на кадрах; при `0` детекций канва полностью очищается; `boxes=0` и `enable=0` уничтожают регион, повторное включение создаёт его снова; `/api/yolo?boxes=2|bogus` → HTTP 400; поворот 90° на живом канале пересоздал канву `1088×1920`, после отключения — `1920×1088`; CMA +512 КиБ (53 824→54 336), `MemAvailable` 137.6 МБ, CPU `divinus` 29% при 5 fps (без роста); `api_check.py` 88/88 | Только главный поток (по `osd.scope`); два цвета; сетка 16 px и толщина 2 px; 180/270 и точное совпадение рамок на повёрнутой сцене не измерялись; при отсутствии canvas-символов фича отключается с одним предупреждением |
| Полная прошивка из слитого дерева | работает | 2026-11-07: `update.img` `33a86d50…` прошит через Maskrom (U-Boot `download` → bootrom) и `upgrade_tool uf`; после загрузки `marker=OKO-TEST-2026.10-01`, разметка `boot/oem/userdata` 6M/40M/40M и `rootfs` 168M, `/userdata` 29.1 МиБ пересоздан; `/oem/usr/bin/divinus` sha256 `f7a985f9…` совпадает со staged; модель загружена через `/api/models` (2 660 586 Б, `runtime=2`, 640×384, 80 классов) и активирована; `api_check.py` 88/88, `osd_check.py` 17 с, рамки видны на `/live/0` (синяя рамка 896×32), AIISP on, NPU 17% | `/userdata` стирается полной прошивкой; модели и `divinus.yaml` восстанавливаются из бэкапа (`output/bench/rknpu-O-20261107/userdata-backup/`) |
| Модель YOLOv5n 640×384 (Docker) | работает | `tools/rknn-toolkit2/export_yolov5.sh 384 640` → ONNX 7.2 МиБ; `convert_rknn.sh` на 101 кадре SC230AI → `.rknn` 2.6 МиБ, sha256 `8285e1bd…`; `target_platform=rv1106`, INT8; активация на DUT заполнила `models.json` (640×384, 80 классов, runtime 1.6.0) | Образы `oko-rknn-toolkit2:1.6.0` и `oko-yolov5-export` ~2.5 ГБ; `.rknn` в git не коммитится; mAP не измерен |
| Разметка `userdata`/`rootfs` | работает | 2026-11-05: `/proc/cmdline` и `/proc/mtd` показывают `40M(userdata),168M(rootfs)`; `df -h /userdata` — 29.1 МиБ, `ubi5_0/data_bytes` = 35045376; загрузка `.rknn` ровно 3 МиБ через `/api/models` → HTTP 200, затем delete | Нужна полная прошивка `update.img`; `userdata` пересоздаётся |
| AIISP + YOLO одновременно | работает (640×640 демо) | 2026-11-05, `vpss3`, 3 fps: AIISP off — main 30.25 fps, NPU 28%, max gap 44 мс; AIISP on — `librkpostisp.so` в `/proc/<pid>/maps`, main 32.79 fps, NPU 28%, max gap 41 мс; контроль без YOLO — 30.32 fps, NPU 0%. Плата жива, пропусков в `/live/0` не видно | Ночная точность и CMA под кольцом не измерялись: на стенде нет цели/разметки; проверка на демо-модели 640×640, не на целевой 640×384 |
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

| Фокус: AF-метрика (A6) | работает | 2026-11-06, после `POST /api/focus?enable=1`: `status=running`, `af_hw_ver=2`, `cells_used=225`, 20 отсчётов `metric_avg` 527427–530023 (std 1124) в `output/bench/focus/focus-20261106-111557.csv` + снимок. ROI top-left 0,0,2000,2000 → `metric_avg=2635599` (9 ячеек), центр 2500,2500,5000,5000 → 0 (плоская область). Без `af_v31.TuningPara.contrast_af.enable=1` в IQ все ячейки нулевые | AFM-карта остаётся полнокадровой, ROI применяется выбором ячеек в `focus.c`; малые ходы мотора метрику не меняют — оптическая связь объектива не подтверждена |
| Фокус: мотор STM8 | работает | `oko-motor -b 3 -a 0x60 speed 0 300`, `move 0 10` → `position 5012`, `move 0 -10` → `5002`, `status 0`; `move ±200` и `stop` без ошибок | Позиция — счётчик шагов; фактическое движение объектива не подтверждено |
| STM8: загрузчик и обновление | работает | Исходный upstream `2.0.0` заменён `factory.ihx` по ST-Link; `enter-boot` → `info` = `bootloader: 1.0.0, application header: 2.0.1`, `version` в загрузчике → ошибка с подсказкой, `boot-reset` → `app 2.0.1`; `POST /api/flash` (target=stm8, `app-2.0.1.ihx`, confirm=FLASH) → 202, `state=done`, `oko-motor version` = `2.0.1`; сервер сверил версию из имени с заголовком OKO8 (`oko-motor check FILE 2.0.1`); power-cycle сохраняет `2.0.1`; kill -9 хостового `oko-motor flash` оставляет загрузчик, повторная загрузка успешна | Первая запись — только ST-Link/SWIM; обрыв питания при стирании не имитировался (KI-032) |
| Прошивка: валидация и блокировки | работает | `/api/flash`: без `confirm`, неизвестный target, `.txt`, битый `OKO8`, 300 КиБ, без версии в имени (`app.bin`), несовпадение имени с заголовком STM8 (`app-9.9.9.ihx`), `.ihx` для STM32 и >120 КиБ → HTTP 400; второй POST во время работы → 409 `{"error":"busy"}`; `firmware_check.py --negative` — 9/9 | Реальная запись STM32 проверена (см. ниже) |
| Прошивка: реальная прошивка STM32 | работает | 2026-11-06, CBT6: CLI `app-1.0.1.bin` (`0x00010001`) → `Application started`; WebUI `/api/flash` `app-1.0.1.bin` (версия из имени, без поля version) → job `done`, `/api/firmware` = `1.0.1`; rollback `app-1.0.0.bin` → `1.0.0` (progress 0→100); `oko-ec info` `img_size=2324`, `flags=0`; `battery/cells/temp` живы; после reboot — app 1.0.0; oversize 130 КиБ → `ERR_PARAM`, автоматический `RESET`, app 1.0.0 | Установленный MCU — CBT6 (128 КиБ, слот 120 КиБ); C8T6-сборка даёт `img_size=0`, использовать `-DMCU=CBT6`. Обрыв питания в момент стирания не имитировался |
| Прошивка: версии контроллеров | работает | `GET /api/firmware` → STM32 `1.0.0`, STM8 `2.0.1` (точечно, без `0x`); сырой hex STM32 остаётся в `oko-ec info`; после flash и power-cycle значения сохраняются; карточка System в `ui_check.py` | STM32 `state` берётся из `/tmp/oko-ec.json` и считается устаревшим через 15 с |
| OSD: NPU в строке CPU | работает | `/tmp/osd-sys.txt` — literal `\n` (backslash + n) и реальный перевод строки в конце: `CPU 20% NPU 0%\nMEM FREE 81%\nT 61.8C` (`od -c`); три текст-полосы в вырезке sys-региона `output/bench/osd-fix2/`; `GET /api/yolo?enable=1` → `NPU 17%`, после disable — 0%; регион 4 `text=@/tmp/osd-sys.txt` | Текст в кадре не распознавался OCR; проверка по байтам файла и полосам яркости |

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

### Фокус, прошивка и NPU (2026-11-06)

```sh
# AF-метрика: 20 отсчётов + CSV + снимок
G=$(curl -s -X POST -u admin:oko-change-me 'http://<DUT_IP>/api/focus?enable=1')
bench/run.sh focus_check.py --seconds 20 --interval 1 --snapshot     --out output/bench/focus
curl -s -u admin:oko-change-me 'http://<DUT_IP>/api/focus?map=1'

# STM8: версия/роль, загрузчик, обновление через WebUI
/oem/usr/bin/oko-motor -b 3 -a 0x60 version
/oem/usr/bin/oko-motor -b 3 -a 0x60 enter-boot
/oem/usr/bin/oko-motor -b 3 -a 0x60 info
/oem/usr/bin/oko-motor -b 3 -a 0x60 boot-reset
bench/run.sh firmware_check.py --flash --target stm8     --file project/app/stm8-motor/build/app-2.0.1.ihx --confirm FLASH
/oem/usr/bin/oko-motor check /tmp/app-2.0.1.ihx 2.0.1      # rc 0
/oem/usr/bin/oko-motor check /tmp/app-2.0.1.ihx 9.9.9      # rc 2

# STM32 (CBT6): CLI 1.0.1, затем WebUI 1.0.0 (rollback)
/oem/usr/bin/oko-ec flash /tmp/app-1.0.1.bin 0x00010001
bench/run.sh firmware_check.py --flash --target stm32 \
  --file output/bench/stm32/app-1.0.0.bin --confirm FLASH
/oem/usr/bin/oko-ec info; /oem/usr/bin/oko-ec battery

# негативные проверки прошивки (400/409)
bench/run.sh firmware_check.py --negative

# версии и NPU
curl -s -u admin:oko-change-me http://<DUT_IP>/api/firmware
cat /tmp/osd-sys.txt; cat /proc/rknpu/load
```

### WebUI и API-матрица (2026-11-05)

```sh
# 80 проверок: GET, POST no-op, вложения, негативные случаи
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