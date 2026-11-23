# Неподтверждённые функции OKO

Возможности, которые проверены не полностью (**ограничено**), заблокированы
причиной (**заблокировано**) или ещё не проверялись. Подтверждённые функции —
в `FEATURES_VERIFIED.md`; паспорт платформы, общие примечания и команды
проверки — в `FEATURES.md`; открытые дефекты и обходы — в `KNOWN_ISSUES.md`.

Статус на 2026-11-19. Пометки: **ограничено** — работает с оговоркой;
**заблокировано** — отказ с причиной.

## Русский

### Сводная таблица неподтверждённого функционала

| Группа | Функционал | Описание | Управление (API, секция YAML) | Что добавлено (OKO vs вендорский V1.4) | Статус |
|---|---|---|---|---|---|
| Видео и стриминг | SmartP | Настройка GOP | `/api/venc`, `[venc]` | SmartP stage `divinus` | **ограничено** — применяется со следующего старта, не перепроверялся |
| Видео и стриминг | IVS | Детекция движения/объектов, снимок, клип, OSD | `/api/ivs`, `[ivs]` | IVS stage `divinus` + `gating.c`; выделенный VI-канал 2 и `ivs.min_area` (2026-11-15) | **ограничено** — MD, `min_area`, OSD, снимок/клип и 15-мин soak подтверждены 2026-11-15 (`FEATURES_VERIFIED.md`); статичная сцена 45 с при `min_area=2` — 0 событий; OD не проверен: в кадре не было объекта (пустая сцена), `cause:"od"` на стенде не наблюдался |
| Видео и стриминг | Рамки IVS: живое переключение | `ivs.boxes_mode: live` — регион 13 уничтожается и создаётся в работающем IVS-потоке без рестарта приложения | `/api/ivs?boxes=0\|1` при `boxes_mode: live`, `[ivs] boxes_mode` | `ivs_events.c` (`ivs_boxes_request`, create/destroy в потоке) | **ограничено (2026-11-15)** — create и destroy подтверждены в консоли (`2BPP overlay region 13 destroyed/created`), падений и ошибок ядра нет; полные 200 циклов не прогнаны: регион создаётся только на первый результат IVS после включения, а статичная сцена даёт результаты редкими пачками (3 реальные пары за 51 окно). По умолчанию режим `restart`; историю SIGSEGV см. KI-063 |
| Видео и стриминг | Runtime IQ | `switch_scene`, `updateIq` | `/api/iq`, `[iq]` | runtime IQ stage `divinus` | **ограничено** — сцена принята; `updateIq` не проверялся |
| Видео и стриминг | HDR2/HDR3 | HDR на SC230AI | `/api/hdr` | HDR API + `gating.c`; экспериментальный HDR_X2 выключен | **заблокировано** — `librkaiq` не поддерживает SC230AI |
| Видео и стриминг | OSD на всех потоках | `osd.scope=all` (main+sub+JPEG) | конфиг `[osd]`, гейт G4 | `gating.c` | **заблокировано** — multi-attach RGN не прошёл проверку |
| Видео и стриминг | Multicast как первый `dest` | `dest: - udp://239.255.255.0:5600` первым адресом | `[stream] dest` | вендорская multicast-ветка отправки | **ограничено** — пакеты уходят в группу, но `seq`/`ssrc`/`tstamp` всегда 0 (стенд 2026-11-10: 2674 пакета, 60 уникальных `seq`, `ssrc=0`), стандартный приёмник такой поток не соберёт; если multicast-адрес стоит **вторым** после unicast, пакеты валидны (2889 пакетов, `seq` 59648→62536 без разрывов) |
| Видео и стриминг | Поток H.265 в браузере | `http://<хост>:8889/cam/`, HLS `8888` | `services/stream/` | MediaMTX 1.21.3 | **ограничено** — Chromium (Playwright 1248) WebRTC закрывает с `codecs not supported by client`, HLS отдаётся как `hvc1` и в MSE не играет; H.265 смотрится в Safari или нативном плеере. С H.264 (`mp4.codec: H.264` + `rtpSDP H264/90000`) WebRTC играет 1080p30 в Chromium — проверено 2026-11-10 |
| Видео и стриминг | YOLO `source: vi1` | VI selfpath (chn1) как источник кадров | `/api/yolo`, `[yolo]` | `gating.c`, API и WebUI отклоняют значение | **заблокировано** — включение `vi1` привело к зависанию платы; значение отклоняется с причиной |
| Видео и стриминг | Второй рантайм RKNN 2.x | Модели toolkit 2.x, `yolo.runtime: 2` | `/api/yolo` (`runtime`, `arch`), `/api/models` (`runtime`, `arch`) | `librknnmrt2.so` **2.5.0** с отдельным SONAME, два handle в HAL, проверка SDK-версии | **ограничено** — YOLOv5n и YOLO11n (RKOPT, 9 выходов) собраны toolkit 2.5.0, грузятся и работают на runtime 2.5.0; модель, собранная toolkit 2.3.2, на 2.5.0 не проверялась (в дереве её не было); mAP YOLO11 на размеченном наборе не измерен; 2-часовой soak пройден (0 lost, outbox 0, память стабильна) |
| Видео и стриминг | YOLO11/события: негативные и долгие сценарии | Модель RK3576 от Hanzo, mAP YOLO11, outbox без microSD, 2-часовой soak | `/api/models`, `/api/clip` | `models.c` (квота), `record_clip.c` | **ограничено** — `Hanzo-Huang/rk3576-ai-demos/models/yolo11n.rknn` (7,26 МБ, RK3576, без лицензии) отклоняется квотой 4386 КиБ; прямая копия в `/userdata/models` даёт `rknn_init failed (-1)` и `incompatible model version: 6` (не `-13`); потеря без SD проверена размонтированием карты (`lost` 21), outbox с картой — спул и разгрузка; mAP YOLO11 и поведение на другой сцене не измерены |
| Сеть | WiFi AIC8800 (SDIO) | Модули `aic8800dc`/`aic_load_fw`/`aic8800_fdrv` собираются в `/oem/usr/ko` | `RK_ENABLE_WIFI=y` | вендорские модули без правок OKO | **ограничено** — USB RTL8821CU проверен отдельно (`FEATURES_VERIFIED.md`); SDIO-сборка и AP+STA не тестировались |

| Питание и камера | Wakeup-IPC: сон камеры при отсутствии движения (TB-линия) | MCU-борд `battery_ipc_demo_v10-SC230AI-ADC` + TB-образ SC230AI; AP-политика спит после 600 с без MD, просыпается по PMU timeout, окно MD после resume | `RK_APP_TYPE=RKIPC_RV1106_WAKEUP_IPC`, секция `[aov]` в `rkipc-300w.ini`, BoardConfig `...-SC230AI-IPC_FASTBOOT.mk` | MCU-порт (`i2c_addr 0x32`), TB-DTS, `rv1106-pmu-timeout.config`, `video/aov_sleep.c`, `ivs:md_area_percent` | **заморожено** — активен divinus-трек (см. следующую строку); правки TB/rkipc-линии не обслуживаются, существующий код остаётся справочником (AGENTS.md §6). Пакет собирается (`update.img` 15,6 МБ, MCU в idblock, erofs 19,2 МБ в FIT), но прошивка Э5/Э7 не выполнялась: `bench/tftp_flash.py` не пишет `idblock/meta`, нужен полный `update.img` через Maskrom, USB-порт занят хабом; bench-only |
| Питание и камера | Сон камеры `[sleep]` (divinus) | AP уходит в deep suspend после `idle_timeout_s` без движения, PMU timeout (`0xff300048`, 32 тика/с) будит AP, после resume пайплайн поднимается и проверяет движение `wake_check_s`; нет движения — снова сон | `/api/status` поле `sleep`, `[sleep]` в `divinus.yaml`, `/api/sleep?motion=1` (стенд, только при `OKO_BENCH_API=1`) | `sleep_policy.c/h`, хук IVS `min_area` и YOLO-детекций, `rv1106-pmu-timeout.config` в продуктовом BoardConfig, `rockchip,rv1106` в root compatible продукта, `oko-ec power` v3 | **ограничено (стенд 2026-11-16)** — deep suspend, PMU wake и повторный сон в «тихом» окне подтверждены: idle 30 с → ~10 с suspend → probe 5 с → снова suspend; 1-часовой soak выдержал 288 циклов без ребутов (pid не менялся), NAND 0 ошибок, память 33→35 МБ, поток 26–30 fps после серии; два полных `idle_timeout_s=600` цикла (второй — после инъекции движения `/api/sleep?motion=1`) прошли, `api_check` 133/0 и `ui_check` зелёные после soak. **Реальный MD-триггер на динамической сцене (2026-11-18)**: IVS MD с `min_area=30`, после deep suspend AP поднялся в `probe`, в окне IVS дал `area 1000‰` → `[sleep] motion in the wake window; stream stays on`, AP остался бодрствовать (дважды, `wake_cycle` 1 и 2), heartbeat в этих циклах не отправлялся; после 30 с без движения — снова suspend (артефакт `output/bench/heartbeat/dynamic-scene.txt`). Замер энергопотребления внешним INA3221 (2026-11-22): полка deep suspend ≈86 мА (min 47, p05 124), окно 2.4 с за цикл ≈13 с, 4.9 мВт·ч/цикл (медиана), 60-мин soak 279 циклов и ≥200 heartbeat, `fail_streak=0`; методика — `TESTBENCH.md` §8. Сон с включённой IVS (2026-11-22, `motion`): IVS фиксирует полнокадровое движение (`area=1000‰`) при каждом пробуждении, политика уходит `probe → awake`, камера бодрствует 24–26 с за цикл; энергия 15.25–17.94 мВт·ч (среднее 16.85) против 4.9 мВт·ч при `wake_check=5`. Основной вклад — длинное окно проверки; при `wake_check=30` без IVS энергия сопоставима (KI-079). Открытый риск NAND: KI-071 (`sfc` DMA timeout и `ro` для rootfs/userdata после длинной серии циклов, лечится power cycle) |
| Питание и камера | Профиль `-AOV`: AOV-MCU wake proxy | MCU `oko-wakeup-sc230ai-ADC` в idblock (`LOADER2=Hpmcu`), `aov_wakeproxy`/`aov_state`, meta в reserved-memory, `[sleep] aov_mcu` | BoardConfig `...-SC230AI-AOV.mk`, `/api/sleep`, `[sleep] aov_mcu`, sysfs `rockchip-suspend/aov_*` | OKO-каталог MCU, AOV-DTS, `rv1106-hpmcu-aov.config`, ini `..._HPMCU.ini`, `build.sh`-хук, gate/timeout в `rv1106_pm.c`, OEM-загрузчик meta | **ограничено (стенд 2026-11-18)** — 1M-раскладка, FIT с ядром по 0x208000 и `aov_available:true` проверены (`/proc/mtd`, `Uncompressing GZIP Kernel Image ... to 0x00208000`). Handshake работает: `-h-wait-` → `-h-mbox-` за ~0.2 с (окно mailbox 1000 мс), motion-resume даёт `[sleep] resumed (… aov motion)`, после resume `/api/status` = `probe`, `/image.jpg` 1920×1080 и RTSP `DESCRIBE /live/0` = 200 без ошибок CSI/сенсора. **Длинный тихий сон — NO-GO на стороне AP**: при `aov_keep_isp=1` deep suspend не завершается (AP возвращается через ~20 мс, 2.6–2.9 цикла/с, `PMU_WAKEUP_INT_ST=0`; media-IRQ на границе WFI выключены, MCU после `AP_SUSPEND` уходит в WFI, снятие PMU GPIO-wake и `IDLE_REQ_VI` не помогает) — вендорский AOV передаёт ISP MCU. AOV в текущем виде = motion-resume. Артефакты: `output/bench/aov-test/quiet-differential-20261117.md`, `gicdiag-wfi-20261117.md`, `mcu-reset-experiment-20261117.md`. Детали и история — KI-072. **2026-11-22, интервал 60 с:** `wake_interval_s=60` (`aov_mcu=0` и `aov_mcu=1 + keep_isp`) даёт PMU-счётчик `0x780` (1920 = 60 с), но фактический deep suspend 2.4 с и цикл 12–18 с; `aov_state` после resume = `off`, `mcu_run_count=0`; форсирование meta `ae_mode=1` не изменило поведение. Интервал 1 минута и MCU-детекция на этом образе не подтверждены. **2026-11-23, локализация (KI-080):** при `aov_mcu=1` один 60-с цикл оставляет I2C3 мёртвой (`keep_isp=1`: нет 0x40/41/42/60, SCL=0) или AP не выходит из suspend (`keep_isp=0`, >8 мин); `aov_mcu=0` даёт тихий цикл 2.26–2.40 с, STM8 на месте. **Gate: AOV не используется, пока нет вендорского ISP-handover; quiet-тесты только с `aov_mcu=0`.** |
| События | MQTT: реальные события и устойчивость | Публикация motion/yolo/clip/heartbeat, поведение при обрыве брокера и в цикле сна | `[mqtt]`, `GET/POST /api/mqtt`, `POST /api/mqtt?test=1` (стенд, `OKO_BENCH_API=1`) | `mqtt_post.c` (очередь, backoff), хуки `yolo.c`/`ivs_events.c`/`record_clip.c`/`heartbeat_task.c` | **ограничено (2026-11-19)** — базовая цепочка подтверждена (`FEATURES_VERIFIED.md`), но сквозная доставка проверена bench-инъектором `test`, а не реальными motion/yolo/clip (на статичной сцене не было детектора/движения); нет TLS, входящих команд (subscribe на устройстве) и моста MQTT↔HTTP; события, возникшие во время suspend (AOV), теряются — очередь только в RAM (64 сообщения, переполнение теряет старое, `dropped`); длительный soak при недоступном брокере и восстановление после переполнения очереди не прогонялись; пароль брокера лежит в `/userdata/divinus.yaml` открытым текстом (как `[http_post]`) |
| Телеметрия (oko-ec) | Датчики | SHT21 `0x40` — T/RH; INA3221 или INA226 `0x41` — U/I/P; STM32 EC `0x42` — U/I/T, SOC, ячейки, OTA | `/dev/i2c-3`, `i2cdetect -y 3`, `oko-ec ina226 --ina-channel 1..3` | `i2c-dev` без драйверов (`CONFIG_HWMON` выключен), автоопределение INA | **ограничено (стенд 2026-11-22)** — адреса видны, каналы INA3221 подключены и размечены: CH1 — DRV8833 + двигатели, CH2 — STM32, CH3 — RV1106 + STM8; при простое CH1 ≈ 11 мА / 51 мВт, CH2 ≈ 2 мА / 9 мВт, CH3 ≈ 380 мА / 1.8 Вт (`TESTBENCH.md` §4). Ток считается по шунту `--shunt-mohm` (по умолчанию 100 мОм). Сверка с внешним эталоном выполнена 2026-11-22: второй INA3221 на ESP32-мосту, CH3 — вход 5 В DUT; на STM8-soak бортовой CH3 232.0 мА против 225.4 мА у ESP32 (−6.7 % тока, −5.4 % мощности). Замер в циклах сна `[sleep]` выполнен: полка ≈86 мА, 4.9 мВт·ч/цикл (медиана), 60-мин soak — `TESTBENCH.md` §8 |
| microSD | Hotplug | Извлечение и установка карты | udev, `/api/storage` | определение по `device/type=SD` | **ограничено** — проверено unbind/bind; физическое извлечение не проверялось |
| USB | Host + хаб | Порт закреплён host'ом: хаб и устройства за ним; gadget/adb недоступны | `dr_mode = "host"` в активном DTS, `lsusb`, `/sys/class/udc` (пусто), `dmesg` | OKO: `dr_mode` «peripheral» → «host», фрагмент `rv1106-usb-host.config`, guard'ы в `S50usbdevice`/`S99usb0config` | **ограничено** — стенд 2026-11-11: после прошивки по TFTP xHCI поднимается сразу, хаб `1a40:0101` и `0bda:c811` за ним видны в `lsusb` без `otg_mode`; UDC/`usb0`/adbd отсутствуют. HID, USB-сеть и BT-донгл не подключались; VBUS — KI-054, недоступность gadget — KI-057 |
| USB | HID за хабом | Клавиатура/мышь | `/dev/input/event*`, `evtest` | `CONFIG_HID=y`, `CONFIG_USB_HID=y`, `CONFIG_INPUT_MOUSE=y` | **не проверено** — стенд 2026-11-10: `usbhid` и `psmouse` зарегистрированы, HID-устройство не подключалось |
| USB | USB-сеть (host) | RNDIS/CDC ECM/NCM, ASIX AX8817x/88179, RTL8150/8152 | `ip link`, `udhcpc -i usb0` | `CONFIG_USB_NET_*`, `CONFIG_USB_RTL815x` | **не проверено** — зарегистрированы `asix`, `ax88179_178a`, `cdc_ether`, `cdc_ncm`, `cdc_subset`, `rndis_host`, `r8152`, `rtl8150`; адаптер не подключался. Для RTL8153 нужна прошивка `rtl_nic`, в образ не входит |
| USB | Bluetooth | USB-BT-донгл, `bluetoothd` + `bluetoothctl` | `/etc/init.d/S30dbus-daemon`, `S40bluetoothd`, `bluetoothctl list` | `CONFIG_BT*`, bluez5_utils 5.86, dbus, libglib2, linux-firmware (RTL87xx/88xx BT, AR3012) | **ограничено** — стенд 2026-11-10: `dbus-daemon` 1.16.2 и `bluetoothd` запущены, `bluetoothctl` 5.86 работает (uclibc-патч `wordexp`), `btusb` зарегистрирован; BT-адаптер не подключался, `bluetoothctl list` без контроллера |
| Сеть | VPN — AmneziaWG | Профиль `awg0`, параметры AWG v3.1, модуль загружается по требованию | `/api/vpn`, WebUI Network → VPN, `/usr/bin/oko-vpn` | out-of-tree `amneziawg.ko` (`v3.1.20260906`), `awg` (`v3.1.20260812`), `RK_ENABLE_AWG=y` | **ограничено** — модуль загружается (`dmesg`: `AmneziaWG 3.1.20260906`), handshake v3.1 с loopback-пиром прошёл со всеми параметрами (`I1/I5`, `HeaderProtectionKey`, `S3/S4`, `AdvancedSecurity`), guard positive/negative проверен; 2026-11-08 loopback-регрессия `bench/vpn_check.py --type awg` — 19/19 (handshake 3 с, split-маршрут, `down`); внешний пир не прогонялся |
| Прошивка | Обрыв питания при обновлении | Восстановление после потери питания STM32/STM8 | `flash.c`, загрузчики | `oko-flash`, I2C-загрузчик STM8 | **ограничено** — обрыв хостового `oko-motor flash` проверен (загрузчик жив, повторная загрузка успешна); полный обрыв питания не имитировался |
| Прошивка | Сон привода STM8 | Сон по тишине I2C: Wait mode (`WFI`) с пробуждением по тику TIM4, тишина после `idle_s` без RUNNING-мотора и грязного EEPROM, эпизодические счётчики и состояние; включается хостом перед suspend | `oko-motor sleep on\|off [S]`, `/api/firmware.stm8.sleep_*`, `[motor] sleep_enable/sleep_idle_s` | `src/motor/sleep_logic.h`, `hw_sleep_enter` (`WFI`), команды `0x0A`/`0x0B`, `sleep_policy.c` | **ограничено (стенд 2026-11-21)** — T8 на тестовой плате с прошивкой `2.3.2` пройден полностью: `tests/hardware_test.sh` (`BUS=3`, soak 1800 с) 41 PASS / 0 FAIL, 100 циклов пробуждения без отказов, `entries/wake_i2c/timer` растут, позиция стабильна под WDT со сном, `dmesg` без `SCL hold`. T9 проверен на уровне API/логики: `[motor] sleep_enable: true` + HUP принимаются, `/api/firmware.stm8.sleep_*` читаются (`2.3.2`), последовательность divinus (`SET_SLEEP on 3` → тишина → `SET_SLEEP off` + чтение версии) проходит; полный deep suspend/resume с включённым сном не выполнялся (нестабильность KI-071/KI-074). Ограничения: Active-halt непригоден на стендовом STM8S003F3P6 (AWU не считает, KI-077), поэтому выигрыш по току — уровень Wait, а не Halt; пока AP опрашивает контроллер чаще `idle_s` (divinus ~2 с при пороге 3 с), сон не наступает; I2C-обновление прошивки по-прежнему требует обхода ST-Link (KI-075); продуктовая сборка оставляет режим выключенным |

### Примечания

- **rkipc и вендорские тест-утилиты вырезаны из образа.** `rkipc`,
  `rkaiq_3A_server`, `rkaiq_tool_server`, `rk_*_test`, `rk_mpi_*_test`,
  `luckfox_*_test` и лишние v4l-утилиты перечислены в `oko-prune.list` и
  отсутствуют в прошивке; диагностика через них недоступна. Сохранены
  `v4l2-ctl`, `media-ctl` и `rgaImDemo`. Исключение — экспериментальная
  wakeup-IPC линия: её BoardConfig выставляет `RK_SKIP_OKO_PRUNE=y` и содержит
  `rkipc`, потому что приложение этой линии запускается через него. Стендовый цикл этой ветки —
  отложенный гейт, см. `KNOWN_ISSUES.md`.

- **USB host/BT: хаб проверен, классы за ним — нет.** Для порта собраны ядро
  с HID/USB-net/Bluetooth (`rv1106-usb-host.config`, все символы `=y` в
  итоговом `.config`), DTB содержит `dr_mode = "host"`, в rootfs вошли
  `bluetoothctl`, `dbus-daemon`, `lsusb` (busybox) и BT-прошивки
  `linux-firmware`. Стенд 2026-11-11: после TFTP-прошивки плата загрузилась с
  NAND, xHCI поднялся сразу, хаб `1a40:0101` и `0bda:c811` за ним видны в
  `lsusb` без ручного `otg_mode`; UDC нет, `usb0`/adbd не запускаются
  (`S50usbdevice`/`S99usb0config` выходят по `dr_mode`). HID, USB-сеть и
  BT-донгл не подключались, VBUS — KI-054, gadget недоступен — KI-057.

- **Бюджет каналов VPSS — 4.** `VPSS_MAX_CHN_NUM == 4`: без IVS доступны
  main + sub + MJPEG + JPEG. IVS работает на VI chn2, но аллокатор
  по-прежнему резервирует chn3 — KI-070; прежние прогоны IVS + MJPEG
  (2026-11-05) относятся к старой топологии. Комбинации с неподтверждённой
  топологией `gating.c` отклоняет с причиной и подсказкой.
- **YOLO.** `source: vpss3` занимает последний VPSS-канал и потому несовместим
  с MJPEG/четвёртым потоком — санитайзер переводит на `vi0` (рамки на `vi0`
  пересоздаются — KI-068). Точность mAP на валидационном наборе не измерена;
  для другой модели подбирать `conf_threshold`. Основная функция YOLO
  подтверждена — см. `FEATURES_VERIFIED.md`.
- **AIISP.** Ночная точность шумоподавки не измерялась. Совместная работа AIISP
  и YOLO на `vpss3` проверена на стенде (демо 640×640: main 30–33 fps, NPU 28%,
  без пропусков). Модель `.rknn` собрана и активирована, матрица C1–C4 в
  ambient-свете прогнана (`bench/yolo_night.py --phase ambient`), но числа не
  опубликованы: повторы одной конфигурации расходятся (detection rate
  0.04/0.22/0.31), сцена нестабильна. На этой плате нет ИК-подсветки и
  управляемого IR-cut (`/api/night` отдаёт пины 999), поэтому ночной замер с
  ИК физически недоступен; KI-005 открыт.
- **RKNN 2.x / YOLO11.** Модели `yolov5n-640x384-r2.rknn` и
  `yolo11n-640x384-r2.rknn` собраны toolkit 2.5.0 на тех же 60 кадрах
  калибровки; активация, инференс и `arch=yolo11` проверены. Модель,
  собранная toolkit 2.3.2, на рантайме 2.5.0 не проверялась; mAP YOLO11 на
  размеченном наборе не измерялся, поэтому «YOLO11 не хуже YOLOv5» по
  точности пока не доказано. 2-часовой soak с включённым кольцом роликов
  пройден 2026-11-14: 0 отказов камеры, 0 потерянных событий, outbox пуст,
  `MemFree` 75,8→69,6 МиБ.
- **Поворот и рамки.** Канва 0/90/180/270 и совмещение NN-рамок проверены
  `OKO_GEOMETRY_TESTS=1` 2026-11-07 (IoU 0.62/0.71/0.71), OSD-текст остаётся
  в своих углах. Эталонной разметки положения рамок на повёрнутой сцене нет:
  измерена воспроизводимость маппинга, а не абсолютная точность.
- **Помощь фокусировке.** AF-мост включает `RKAIQ_AF_MODE_FIXED` и
  `manual_meascfg_v31.af_en=1`; обязательное условие ненулевой метрики —
  `contrast_af.enable=1` в IQ SC230AI (в OKO включено в IQ и в рантайме).
- **STM8.** Загрузчик и приложение собираются SDCC 4.2 (`tools/stm8-sdcc`);
  актуальная прошивка — 2.1.1 (первичная запись по ST-Link/SWIM, обновление
  `app-2.1.1.ihx` через WebUI и `oko-motor flash`). Слот загрузчика — 2.5 КиБ
  (`0x8000–0x89FF`) при измеренном boot 2355 байт. Подробности и версии — в
  `FEATURES_VERIFIED.md` и `VERIFIED_FUNCTIONS.md`.
- **Ручное управление и home/zero.** Команды `move`/`SET_POS`, ручное
  управление focus/zoom и асинхронные `home`/`zero` покрыты `api_check.py`
  (`OKO_FOCUS_MOTOR_TESTS=1`), включая гейт версии и 409 при занятом job’е.
  Открытый остаток — пропуск шагов и поведение при упоре на настоящем
  объективе.
- **Автофокус.** Поиск по AF-метрике (`focus_af.c`) и micro-fine `af=fine`
  реализованы и покрыты хостовым тестом; на стенде 2026-11-07 обе ветки
  завершились `done`, но метрика от позиции мотора не зависит — объектив
  стенда оптически не связан с приводом, поэтому сходимость на реальной сцене
  не подтверждена. Компоновка WebUI проверена headless-Chromium на
  1440×900 и 390×844, но не на физическом телефоне.
- **VPN.** WireGuard и OpenVPN подтверждены внешним пиром 2026-11-08 и
  перенесены в `FEATURES_VERIFIED.md` (`bench/vpn_check.py`: WG 19/19,
  OpenVPN 23/23 full+auth+reboot и 21/21 split, iperf3 21–23 Мбит/с).
  Остаётся AmneziaWG: модуль `amneziawg.ko` собран под 5.10.160 и
  загружается по требованию из `oko-vpn`; `wg`/`awg` собраны app-тулчейном
  (ELF ARM, uClibc). Парсер `.conf` отклоняет
  `PreUp/PostUp/PreDown/PostDown`, поддерживает AWG v3.1 (`I1…I5`,
  `HeaderProtectionKey`, `AdvancedSecurity`) и проверен хостовым тестом;
  handshake v3.1 с loopback-пиром и guard positive/negative пройдены, но
  внешний пир AWG не прогонялся (для хоста нужен out-of-tree модуль под
  ядро 6.12).
- **Датчики.** Адреса SHT21/INA/EC видны на `/dev/i2c-3`; силовые входы
  INA3221 подключены и размечены (CH1 — DRV8833 + двигатели, CH2 — STM32,
  CH3 — RV1106 + STM8), ток/мощность читаются на всех каналах. Ограничение:
  абсолютные значения зависят от номинала шунта (`--shunt-mohm`, по умолчанию
  100 мОм) и не сверялись с эталонной нагрузкой.

## English

Capabilities that are only partially verified (**limited**), blocked with a
reason (**blocked**) or not yet verified. Confirmed features are in
`FEATURES_VERIFIED.md`; the platform passport, shared notes and verification
commands are in `FEATURES.md`; open defects and workarounds are in
`KNOWN_ISSUES.md`.

Status as of 2026-11-19.

| Group | Feature | Description | Control (API, YAML section) | What was added (OKO vs vendor V1.4) | Status |
|---|---|---|---|---|---|
| Video and streaming | SmartP | GOP change | `/api/venc`, `[venc]` | SmartP stage in `divinus` | **limited** — applies on next start, not re-verified |
| Video and streaming | IVS | Motion/object detection, snapshot, clip, OSD | `/api/ivs`, `[ivs]` | IVS stage in `divinus` + `gating.c`; dedicated VI channel 2 and `ivs.min_area` (2026-11-15) | **limited** — MD, `min_area`, OSD, snapshot/clip actions and a 15 min soak verified on 2026-11-15 (`FEATURES_VERIFIED.md`); a 45 s static scene with `min_area=2` produced 0 events; OD not verified: no object was in the frame (empty scene), `cause:"od"` was never observed on the bench |
| Video and streaming | IVS boxes: live switch | `ivs.boxes_mode: live` — region 13 is destroyed and created inside the running IVS thread without an application restart | `/api/ivs?boxes=0\|1` with `boxes_mode: live`, `[ivs] boxes_mode` | `ivs_events.c` (`ivs_boxes_request`, create/destroy in the thread) | **limited (2026-11-15)** — create and destroy are confirmed on the console (`2BPP overlay region 13 destroyed/created`), no crashes and no kernel errors; the full 200 cycles were not run: the region is only created on the first IVS result after enabling, and a static scene produces results in rare bursts (3 real pairs over 51 windows). The default mode is `restart`; for the SIGSEGV history see KI-063 |
| Video and streaming | Runtime IQ | `switch_scene`, `updateIq` | `/api/iq`, `[iq]` | runtime IQ stage in `divinus` | **limited** — scene switch accepted, `updateIq` untested |
| Video and streaming | HDR2/HDR3 | HDR on SC230AI | `/api/hdr` | HDR API + `gating.c`; experimental HDR_X2 disabled | **blocked** — `librkaiq` does not support SC230AI |
| Video and streaming | OSD on all streams | `osd.scope=all` (main+sub+JPEG) | `[osd]` config, gate G4 | `gating.c` | **blocked** — multi-attach RGN failed verification |
| Video and streaming | Multicast as the first `dest` | `dest: - udp://239.255.255.0:5600` as the first entry | `[stream] dest` | vendor multicast send branch | **limited** — packets reach the group but `seq`/`ssrc`/`tstamp` stay 0 (bench 2026-11-10: 2674 packets, 60 unique `seq` values, `ssrc=0`), so a standard receiver cannot assemble the stream; with the multicast address **second** after a unicast one the packets are valid (2889 packets, `seq` 59648→62536 without gaps) |
| Video and streaming | H.265 stream in a browser | `http://<host>:8889/cam/`, HLS `8888` | `services/stream/` | MediaMTX 1.21.3 | **limited** — Chromium (Playwright 1248) closes WebRTC with `codecs not supported by client` and HLS is served as `hvc1`, which MSE rejects; H.265 needs Safari or a native player. With H.264 (`mp4.codec: H.264` + `rtpSDP H264/90000`) WebRTC plays 1080p30 in Chromium — verified 2026-11-10 |
| Video and streaming | YOLO `source: vi1` | VI selfpath (chn1) as the frame source | `/api/yolo`, `[yolo]` | `gating.c`, API and WebUI reject the value | **blocked** — enabling `vi1` hung the board; the value is refused with a reason |
| Video and streaming | Second RKNN 2.x runtime | Models built by toolkit 2.x, `yolo.runtime: 2` | `/api/yolo` (`runtime`, `arch`), `/api/models` (`runtime`, `arch`) | `librknnmrt2.so` **2.5.0** with a distinct SONAME, two HAL handles, SDK-version check | **limited** — YOLOv5n and YOLO11n (RKOPT, nine outputs) are built by toolkit 2.5.0 and load/run on runtime 2.5.0; a model built by toolkit 2.3.2 was not tested on 2.5.0 (none was left in the tree); the YOLO11 mAP on a labelled set is not measured; the 2-hour event-clip soak passed (0 lost, empty outbox, stable memory) |
| Video and streaming | YOLO11/events: negative and long scenarios | Hanzo RK3576 model, YOLO11 mAP, outbox without a microSD, 2-hour soak | `/api/models`, `/api/clip` | `models.c` (quota), `record_clip.c` | **limited** — `Hanzo-Huang/rk3576-ai-demos/models/yolo11n.rknn` (7.26 MB, RK3576, no licence) is rejected by the 4386 KiB quota; a direct copy into `/userdata/models` yields `rknn_init failed (-1)` with `incompatible model version: 6` (not `-13`); the no-SD loss was tested by unmounting the card (`lost` 21) and the SD outbox spool/drain passed; the YOLO11 mAP and other scenes are not measured |
| Network | WiFi AIC8800 (SDIO) | `aic8800dc`/`aic_load_fw`/`aic8800_fdrv` are built into `/oem/usr/ko` | `RK_ENABLE_WIFI=y` | vendor modules without OKO changes | **limited** — USB RTL8821CU is verified separately (`FEATURES_VERIFIED.md`); the SDIO build and AP+STA are untested |

| Power and camera | Wakeup-IPC: idle camera sleep (TB line) | MCU board `battery_ipc_demo_v10-SC230AI-ADC` + SC230AI thunder-boot image; the AP policy suspends after 600 s without MD, wakes on the PMU timeout and runs one MD window after resume | `RK_APP_TYPE=RKIPC_RV1106_WAKEUP_IPC`, `[aov]` section in `rkipc-300w.ini`, BoardConfig `...-SC230AI-IPC_FASTBOOT.mk` | MCU port (`i2c_addr 0x32`), TB DTS, `rv1106-pmu-timeout.config`, `video/aov_sleep.c`, `ivs:md_area_percent` | **frozen** — the divinus track below is active; the TB/rkipc line is no longer maintained and remains reference code only (AGENTS.md §6). The image builds (`update.img` 15.6 MB, MCU in the idblock, 19.2 MB erofs inside the FIT), but the E5/E7 flashing and test matrix were never executed: `bench/tftp_flash.py` does not write `idblock/meta`, a full `update.img` via Maskrom is required, and the bench USB port is occupied by a hub; bench-only |
| Power and camera | Camera sleep `[sleep]` (divinus) | The AP enters deep suspend after `idle_timeout_s` without motion, the PMU timeout (`0xff300048`, 32 ticks/s) wakes it, the pipeline is restarted and the policy waits `wake_check_s`; quiet → suspend again | `/api/status` field `sleep`, `[sleep]` in `divinus.yaml`, `/api/sleep?motion=1` (bench, only with `OKO_BENCH_API=1`) | `sleep_policy.c/h`, IVS `min_area` and YOLO detection hooks, `rv1106-pmu-timeout.config` in the product BoardConfig, `rockchip,rv1106` in the product root compatible, `oko-ec power` v3 | **limited (bench 2026-11-16)** — deep suspend, PMU wake and the repeat-sleep quiet window were verified: idle 30 s → ~10 s suspended → 5 s probe → suspended again; a 1-hour soak survived 288 cycles with no reboot (the PID never changed), zero NAND errors, memory 33→35 MB and a 26–30 fps stream after the series; two full `idle_timeout_s=600` cycles (the second after a `/api/sleep?motion=1` injection) passed, `api_check` 133/0 and `ui_check` pass after the soak. **Real MD trigger on a dynamic scene (2026-11-18)**: IVS MD with `min_area=30`, after a deep suspend the AP came up in `probe`, IVS reported `area 1000‰` inside the window → `[sleep] motion in the wake window; stream stays on`, and the AP stayed awake (twice, `wake_cycle` 1 and 2) while no heartbeat was sent in those cycles; after 30 s without motion it suspended again (artifact `output/bench/heartbeat/dynamic-scene.txt`). Not verified: power measurements. Open NAND risk: KI-071 (`sfc` DMA timeout and rootfs/userdata going `ro` after a long cycle series, recovered by a power cycle) |
| Power and camera | `-AOV` profile: AOV MCU wake proxy | MCU `oko-wakeup-sc230ai-ADC` in the idblock (`LOADER2=Hpmcu`), `aov_wakeproxy`/`aov_state`, meta in reserved memory, `[sleep] aov_mcu` | BoardConfig `...-SC230AI-AOV.mk`, `/api/sleep`, `[sleep] aov_mcu`, sysfs `rockchip-suspend/aov_*` | OKO MCU board, AOV DTS, `rv1106-hpmcu-aov.config`, `..._HPMCU.ini`, `build.sh` hook, gate/timeout in `rv1106_pm.c`, OEM meta loader | **limited (bench 2026-11-18)** — the 1M layout, the FIT that loads the kernel at 0x208000 and `aov_available:true` are verified (`/proc/mtd`, `Uncompressing GZIP Kernel Image ... to 0x00208000`). The handshake works: `-h-wait-` → `-h-mbox-` in ~0.2 s (1000 ms mailbox window), a motion resume logs `[sleep] resumed (… aov motion)`, and after the resume `/api/status` is `probe`, `/image.jpg` is 1920×1080 and RTSP `DESCRIBE /live/0` returns 200 with no CSI/sensor errors. **Long quiet sleep is NO-GO on the AP side**: with `aov_keep_isp=1` deep suspend does not complete (the AP returns after ~20 ms at 2.6–2.9 cycles/s, `PMU_WAKEUP_INT_ST=0`; the media IRQs are disabled at the WFI boundary, the MCU goes to WFI after `AP_SUSPEND`, and masking the PMU GPIO wake or dropping `IDLE_REQ_VI` does not help) — the vendor AOV hands the ISP to the MCU. AOV as it stands is motion-resume. Artifacts: `output/bench/aov-test/quiet-differential-20261117.md`, `gicdiag-wfi-20261117.md`, `mcu-reset-experiment-20261117.md`. Details and history — KI-072. **2026-11-22, 60 s interval:** `wake_interval_s=60` (`aov_mcu=0` and `aov_mcu=1 + keep_isp`) programs the PMU counter as `0x780` (1920 = 60 s), but the actual deep suspend lasts 2.4 s and the cycle is 12–18 s; `aov_state` after resume is `off`, `mcu_run_count=0`; forcing meta `ae_mode=1` did not change the behaviour. The 1-minute interval and MCU detection are not confirmed on this image. **2026-11-23, localisation (KI-080):** with `aov_mcu=1` a single 60 s cycle leaves I2C3 dead (`keep_isp=1`: no 0x40/41/42/60, SCL=0) or the AP never leaves suspend (`keep_isp=0`, >8 min); `aov_mcu=0` gives a quiet 2.26–2.40 s cycle and the STM8 stays present. **Gate: AOV is not used until a vendor ISP-handover exists; quiet tests only with `aov_mcu=0`.** |
| Events | MQTT: real events and robustness | Publishing motion/yolo/clip/heartbeat, behaviour when the broker is down and during the sleep cycle | `[mqtt]`, `GET/POST /api/mqtt`, `POST /api/mqtt?test=1` (bench, `OKO_BENCH_API=1`) | `mqtt_post.c` (queue, backoff), hooks in `yolo.c`/`ivs_events.c`/`record_clip.c`/`heartbeat_task.c` | **limited (2026-11-19)** — the base chain is verified (`FEATURES_VERIFIED.md`), but the end-to-end delivery used the `test` bench injector rather than real motion/yolo/clip events (the static scene had no detector or motion); there is no TLS, no inbound commands (device-side subscribe) and no MQTT↔HTTP bridge; events produced during suspend (AOV) are lost because the queue lives in RAM only (64 messages, overflow drops the oldest, `dropped`); a long soak with the broker down and recovery after queue overflow were not run; the broker password is stored in plaintext in `/userdata/divinus.yaml` (same as `[http_post]`) |
| Telemetry (`oko-ec`) | Sensors | SHT21 `0x40` — T/RH; INA3221 or INA226 `0x41` — U/I/P; STM32 EC `0x42` — U/I/T, SOC, cells, OTA | `/dev/i2c-3`, `i2cdetect -y 3`, `oko-ec ina226 --ina-channel 1..3` | `i2c-dev` without drivers (`CONFIG_HWMON` off), INA autodetection | **limited (bench 2026-11-22)** — addresses respond and the INA3221 channels are wired and mapped: CH1 is the DRV8833 plus the motors, CH2 is the STM32, CH3 is the RV1106 plus the STM8; while idle CH1 ≈ 11 mA / 51 mW, CH2 ≈ 2 mA / 9 mW, CH3 ≈ 380 mA / 1.8 W (`TESTBENCH.md` §4). Current is derived from `--shunt-mohm` (100 mΩ by default); no reference-load cross-check or calibration was done, and no power measurement was taken across `[sleep]` cycles |
| microSD | Hotplug | Card removal and insertion | udev, `/api/storage` | detection via `device/type=SD` | **limited** — verified with unbind/bind only; physical removal untested |
| USB | Host + hub | The port is pinned to host: a hub and the devices behind it; the gadget and adb are unavailable | `dr_mode = "host"` in the active DTS, `lsusb`, `/sys/class/udc` (empty), `dmesg` | OKO: `dr_mode` "peripheral" -> "host", `rv1106-usb-host.config` fragment, guards in `S50usbdevice`/`S99usb0config` | **limited** - bench 2026-11-11: after flashing over TFTP xHCI comes up at boot, the hub `1a40:0101` and the `0bda:c811` behind it appear in `lsusb` without `otg_mode`; there is no UDC, `usb0` or adbd. HID, USB networking and a BT dongle were not attached; VBUS - KI-054, no gadget - KI-057 |
| USB | HID behind a hub | Keyboard/mouse | `/dev/input/event*`, `evtest` | `CONFIG_HID=y`, `CONFIG_USB_HID=y`, `CONFIG_INPUT_MOUSE=y` | **unverified** — bench 2026-11-10: `usbhid` and `psmouse` are registered, no HID device was attached |
| USB | USB networking (host) | RNDIS/CDC ECM/NCM, ASIX AX8817x/88179, RTL8150/8152 | `ip link`, `udhcpc -i usb0` | `CONFIG_USB_NET_*`, `CONFIG_USB_RTL815x` | **unverified** — `asix`, `ax88179_178a`, `cdc_ether`, `cdc_ncm`, `cdc_subset`, `rndis_host`, `r8152` and `rtl8150` are registered; no adapter was attached. RTL8153 needs the `rtl_nic` firmware, which is not shipped |
| USB | Bluetooth | USB BT dongle, `bluetoothd` + `bluetoothctl` | `/etc/init.d/S30dbus-daemon`, `S40bluetoothd`, `bluetoothctl list` | `CONFIG_BT*`, bluez5_utils 5.86, dbus, libglib2, linux-firmware (RTL87xx/88xx BT, AR3012) | **limited** — bench 2026-11-10: `dbus-daemon` 1.16.2 and `bluetoothd` are running, `bluetoothctl` 5.86 works (uclibc `wordexp` patch), `btusb` is registered; no BT adapter was attached, `bluetoothctl list` shows no controller |
| Network | VPN — AmneziaWG | `awg0` profile, AWG v3.1 parameters, module loaded on demand | `/api/vpn`, WebUI Network → VPN, `/usr/bin/oko-vpn` | out-of-tree `amneziawg.ko` (`v3.1.20260906`), `awg` (`v3.1.20260812`), `RK_ENABLE_AWG=y` | **limited** — the module loads (`dmesg`: `AmneziaWG 3.1.20260906`), the v3.1 handshake with a loopback peer passed with all parameters (`I1/I5`, `HeaderProtectionKey`, `S3/S4`, `AdvancedSecurity`), and the guard positive/negative path was verified; the 2026-11-08 loopback regression `bench/vpn_check.py --type awg` passed 19/19 (3 s handshake, split route, `down`); an external peer was not run |
| Firmware | Power loss during an update | Recovery of STM32/STM8 | `flash.c`, bootloaders | `oko-flash`, STM8 I2C bootloader | **limited** — killing the host `oko-motor flash` was tested (bootloader stays usable, the next upload recovers); a full power cut was not emulated |
| Firmware | STM8 motor sleep | I2C-idle sleep: Wait mode (`WFI`) woken by the TIM4 tick, entered after `idle_s` of silence with no motor RUNNING and a clean EEPROM, with per-episode counters and state; enabled by the host before suspend | `oko-motor sleep on\|off [S]`, `/api/firmware.stm8.sleep_*`, `[motor] sleep_enable/sleep_idle_s` | `src/motor/sleep_logic.h`, `hw_sleep_enter` (`WFI`), commands `0x0A`/`0x0B`, `sleep_policy.c` | **limited (bench 2026-11-21)** — T8 on the test board with firmware `2.3.2` passes completely: `tests/hardware_test.sh` (`BUS=3`, 1800 s soak) 41 PASS / 0 FAIL, 100 wake cycles with no failures, `entries/wake_i2c/timer` grow, the position is stable under the WDT with sleep on, and `dmesg` shows no `SCL hold`. T9 was verified at the API/logic level: `[motor] sleep_enable: true` plus HUP are accepted, `/api/firmware.stm8.sleep_*` reads work (`2.3.2`), and the divinus sequence (`SET_SLEEP on 3` → silence → `SET_SLEEP off` plus a version read) passes; a full deep suspend/resume with sleep enabled was not run (KI-071/KI-074 instability). Limitations: Active-halt is unusable on the bench STM8S003F3P6 (the AWU does not count, KI-077), so the saving is Wait-level, not Halt-level; while the AP polls the controller more often than `idle_s` (divinus ~2 s against a 3 s threshold) the sleep does not engage; I2C firmware updates still need the ST-Link workaround (KI-075); the production build keeps the mode off |

### Notes

- **rkipc and the vendor test utilities are pruned from the image.** `rkipc`,
  `rkaiq_3A_server`, `rkaiq_tool_server`, `rk_*_test`, `rk_mpi_*_test`,
  `luckfox_*_test` and the extra v4l utilities are listed in `oko-prune.list`
  and are absent from the firmware, so diagnostics through them are
  unavailable. `v4l2-ctl`, `media-ctl` and `rgaImDemo` are kept. The bench
  cycle for this branch is a deferred gate, see `KNOWN_ISSUES.md`.
  Exception: the experimental wakeup-IPC line sets `RK_SKIP_OKO_PRUNE=y` in its
  BoardConfig and does ship `rkipc`, because that is the application it runs.

- **USB host/BT: the hub is verified, the classes behind it are not.**
  The kernel is built with HID/USB net/Bluetooth (`rv1106-usb-host.config`,
  every symbol `=y` in the final `.config`), the DTB carries
  `dr_mode = "host"`, and the rootfs ships `bluetoothctl`, `dbus-daemon`,
  `lsusb` (busybox) and the `linux-firmware` BT blobs. Bench 2026-11-11: after
  TFTP flashing the board booted from NAND, xHCI came up at boot, and the hub
  `1a40:0101` with the `0bda:c811` behind it appeared in `lsusb` without a
  manual `otg_mode`; there is no UDC and neither `usb0` nor adbd starts
  (`S50usbdevice`/`S99usb0config` exit on `dr_mode`). HID, USB networking and a
  BT dongle were not attached; VBUS - KI-054, no gadget - KI-057.

- **VPSS channel budget is 4.** `VPSS_MAX_CHN_NUM == 4`: without IVS, main +
  sub + MJPEG + JPEG are available. IVS runs on VI chn2, but the allocator
  still reserves chn3 — KI-070; the earlier IVS + MJPEG runs (2026-11-05)
  belong to the old topology. Combinations with an unverified topology are
  refused by `gating.c` with a reason and a hint.
- **YOLO.** `source: vpss3` takes the last VPSS channel and is therefore
  incompatible with MJPEG/a fourth stream, so the sanitizer falls back to
  `vi0` (boxes on `vi0` are rebuilt — KI-068). mAP on a validation set is not
  measured; tune `conf_threshold` for a different model. The main YOLO feature
  is confirmed — see `FEATURES_VERIFIED.md`.
- **AIISP.** Night denoise accuracy is not measured. AIISP and YOLO on `vpss3`
  were verified together on the bench (640×640 demo: main 30–33 fps, NPU 28%,
  no drops). The `.rknn` model is built and active and the C1–C4 matrix was run
  in ambient light (`bench/yolo_night.py --phase ambient`), but its numbers are
  not published: repeats of one configuration disagree (detection rate
  0.04/0.22/0.31), i.e. the scene is unstable. The board has no IR illuminator
  or switchable IR-cut (`/api/night` reports pins 999), so an IR night
  measurement is physically unavailable; KI-005 stays open.
- **Rotation and boxes.** The canvas at 0/90/180/270 and the NN box mapping
  were measured with `OKO_GEOMETRY_TESTS=1` on 2026-11-07 (IoU
  0.62/0.71/0.71), and the OSD text keeps its corners. There is no reference
  markup for box positions on a rotated scene: reproducibility of the mapping
  is measured, not absolute accuracy.
- **Focus assist.** The AF bridge sets `RKAIQ_AF_MODE_FIXED` and
  `manual_meascfg_v31.af_en=1`; a non-zero metric requires
  `contrast_af.enable=1` in the SC230AI IQ (OKO sets it in the IQ and at
  runtime).
- **STM8.** The bootloader and the application build with SDCC 4.2
  (`tools/stm8-sdcc`); the current firmware is 2.1.1 (initial write over
  ST-Link/SWIM, `app-2.1.1.ihx` updates through the WebUI and
  `oko-motor flash`). The boot slot is 2.5 KiB (`0x8000–0x89FF`) with a
  measured boot image of 2355 bytes. Details and versions are in
  `FEATURES_VERIFIED.md` and `VERIFIED_FUNCTIONS.md`.
- **Manual control and home/zero.** `move`/`SET_POS`, manual focus/zoom control
  and the asynchronous `home`/`zero` jobs are covered by `api_check.py`
  (`OKO_FOCUS_MOTOR_TESTS=1`), including the version gate and the 409 for a
  busy job. The open remainder is step loss and the stop behaviour on a real
  lens.
- **Auto-focus.** The AF-metric search (`focus_af.c`) and micro-fine `af=fine`
  are implemented and covered by a host test; on the bench (2026-11-07) both
  branches finished `done`, but the metric does not depend on the motor
  position — the bench lens is not optically coupled to the drive, so
  convergence on a real scene is not confirmed. The WebUI layout was checked
  with headless Chromium at 1440×900 and 390×844, but not on a physical phone.
- **VPN.** WireGuard and OpenVPN were confirmed against an external peer on
  2026-11-08 and moved to `FEATURES_VERIFIED.md` (`bench/vpn_check.py`: WG
  19/19, OpenVPN 23/23 full+auth+reboot and 21/21 split, iperf3 21–23 Mbit/s).
  AmneziaWG remains: `amneziawg.ko` builds for 5.10.160 and is loaded on demand
  by `oko-vpn`; `wg`/`awg` are built with the app toolchain (ARM ELF, uClibc).
  The `.conf` parser rejects `PreUp/PostUp/PreDown/PostDown`, supports AWG v3.1
  (`I1…I5`, `HeaderProtectionKey`, `AdvancedSecurity`) and is covered by a host
  test; the v3.1 loopback handshake and the guard positive/negative path pass,
  but an external AWG peer was not run (the host needs the out-of-tree module
  for kernel 6.12).
- **Sensors.** SHT21/INA/EC addresses respond on `/dev/i2c-3`; the INA3221
  power inputs are wired and mapped (CH1 - DRV8833 plus the motors, CH2 - the
  STM32, CH3 - the RV1106 plus the STM8) and current/power read on all
  channels. Limitation: absolute values depend on the shunt value
  (`--shunt-mohm`, 100 mΩ by default) and were not cross-checked against a
  reference load.