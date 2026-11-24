# Утилиты тестового стенда

Скрипты для работы с DUT (`Luckfox Pico Pro Max`, RV1106) по UART, SSH и RTSP.
Консоль по умолчанию — ESP32 telnet-UART bridge `telnet://192.168.52.17:23`
(`bench/uart.py`: минимальный telnet-клиент, отвечает на IAC и вырезает его
из потока); локальный USB-UART `/dev/ttyACM*` остаётся резервом.
Каталог не участвует в сборке прошивки; зависимости ставятся в отдельный venv
внутри `output/` (gitignore).

## Быстрый старт

```sh
bench/setup.sh                          # создать output/bench/venv и поставить зависимости
bench/run.sh serial_exec.py 'cat /proc/uptime'
bench/run.sh dut.py 'mount | grep -w " / "'
bench/run.sh netboot.py catch           # затем сбросить питание DUT
bench/run.sh tftp_flash.py              # прошивка SPI NAND по TFTP (по умолчанию)
bench/run.sh ec_bus_soak.py --mode reboot --iterations 20
bench/run.sh ec_bus_soak.py --mode read-window --iterations 50
bench/run.sh ec_bus_soak.py --mode hold-scl   # нужна сборка -DEC_DEBUG_BUILD=ON
```

`bench/run.sh` сам вызывает `setup.sh`, если venv ещё нет.

Профиль `-AOV` (экспериментальный, KI-072) меняет таблицу разделов: idblock
1M, все разделы после него сдвинуты на 768K (rootfs 167M), а `boot.img` —
это FIT с `Image.gz`, который U-Boot распаковывает в 0x208000. Для
minimal-прошивки достаточно `--components boot --power-cycle`; полный набор
(`idblock,uboot,boot,oem,rootfs,userdata`) нужен только при смене раскладки
и делается из U-Boot-промпта после бэкапа (скрипт снимает бэкап сам при
наличии SSH). Возврат на продукт — тем же `tftp_flash.py` с продуктовым
`output/image` (256K idblock, `--components env,idblock,boot,oem`), затем
`setenv mtdparts`/`saveenv` скрипт делает автоматически. Полный откат без
U-Boot — Maskrom, USB-порт занят хабом (TESTBENCH §7).

## Скрипты

| Скрипт | Назначение |
|---|---|
| `serial_exec.py` | вход в консоль по UART (telnet-мост `192.168.52.17:23` или `/dev/ttyACM*`, 115200 8N1) и выполнение команд на DUT; `--listen N` — зеркало консоли без логина на N секунд (окно простоя перед suspend/resume, когда вывод после приглашения `login:` уже не пишется в `uar0.log`) |
| `power.py` | питание DUT через ESP32-мост: `off`, `on`, `cycle --off-seconds N`, `reset`; вход `admin`/`admin` (env `OKO_BRIDGE*`) и обязательный logout — мост держит не больше трёх сессий, иначе логин отвечает `429 Too many active sessions`; `--cookie-file` переиспользует готовую сессию |
| `recover_dut.py` | аварийное восстановление DUT после неудачного `[sleep]`: ровно один power cycle через мост (одна cookie-сессия, logout), затем немедленное `POST /api/sleep?enable=false&idle_timeout_s=3600`, чтобы камера не уснула снова; `--ssh` правит `[sleep] enable:false` и `idle_timeout_s:15` в `/userdata/divinus.yaml`, `--restart` перезапускает divinus; при `429` не логинится повторно, а просит ручной ребут моста |
| `ina_log.py` | опрос `GET /sensor` ESP32-моста и запись INA3221 CH1..CH3 в CSV: `ts_host_epoch_ms`, `ts_bridge_uptime_ms`, `phase`, `ch`, `bus_mv`, `shunt_uv`, `cur_ma`, `power_mw`, `err`; keep-alive, `--hz` (по умолчанию 2), `--duration`, `--until-marker`, `--channel`, `--summary-json`; импортируемый API `start_logging()`/`set_phase()`/`stop()`, logout по умолчанию; артефакты в `output/bench/power/` |
| `sleep_power.py` | матрица замеров сна и потребления: `--scenario levels` (off/boot/idle/полка), `cycle` (один цикл), `motion` (сон с включённой IVS), `aov` (PMU/AOV-MCU: интервал пробуждения, `aov_state`, meta, проба STM8/I2C/EC на старте и каждом пробуждении), `i2c` (T1.1: N циклов `oko-motor sleep show` + `move` без suspend), `soak` (heartbeat + энергия на цикл + пост-проверки), `peaks` (YOLO/мотор/поток), `stm8` (T8, сверка с бортовым INA3221); окна deep suspend — из `dmesg`, границы циклов — по провалам тока; поднимает и восстанавливает `[sleep]`/`[http_post]`/аналитику, одна cookie-сессия моста; `--expect-present` — код 3 и запись `stm8-recovery.md` при пропаже `0x60`, `--motor-sleep keep|on|off` — правка `[motor] sleep_enable` с перезапуском divinus, unbind/rebind i2c3 (`ff460000.i2c`) в лестнице включён по умолчанию, между unbind и rebind — дампы регистров/падов и bit-bang 9 тактов SCL (`--no-ladder-bitbang` отключает, `--no-ladder-rebind` убирает весь шаг), сценарий `aov` пишет iomux и уровни падов на каждом пробуждении (артефакты `i2c_regs-*.txt`); SSH с баннер-ретраями; артефакты — `output/bench/power/<stamp>-<scenario>/` |
| `dut.py` | выполнение команд по SSH через `paramiko` |
| `netboot.py` | управление U-Boot (та же консоль: telnet-мост или `/dev/ttyACM*`): `catch` (спам Ctrl-C до `=>`, затем netboot-команды), `reboot` (перезагрузка DUT с перехватом U-Boot), `cmd '<команда>'` (одна команда в U-Boot); ядро берётся по TFTP из `output/image/boot.img`, rootfs — по NFS |
| `tftp_flash.py` | прошивка SPI NAND по TFTP из U-Boot (режим по умолчанию): входит в prompt, грузит `output/image/*.img` на `ramdisk_addr_r`, `mtd erase` + `mtd write` с явным размером, восстанавливает NAND-`sys_bootargs` и `mtdparts`, `saveenv` и `reset`; раскладка читается из `output/image/.env.txt` (работают и продуктовый 256K idblock, и `-AOV` 1M); `--components` (`env,idblock,uboot,boot,oem,rootfs,userdata`, по умолчанию `boot,oem,rootfs`), `--userdata`, `--no-reset`; при `idblock` обязателен SSH-бэкап `/dev/mtdblock1` в `output/bench/backup/` (`--dut-host`, `--skip-backup` — только для восстановления из U-Boot); `--power-cycle` — холодный цикл питания через мост и перехват U-Boot на подъёме (нужно после зависшего HPMCU); если консольный логин недоступен, плата перезагружается по SSH и Ctrl-C начинает бить сразу |
| `uart.py` | транспорт консоли для `serial_exec.py`/`netboot.py`: telnet-клиент ESP32-моста (отвечает на IAC и вырезает его) или локальный pyserial по `--port /dev/ttyACM*` |
| `aov_cadence.py` | кадентность AOV-цикла по read-only монитору UART моста (`:2324`, включён `telnet0_mon_enable`): захват N секунд в `output/bench/aov-test/`, подсчёт `-h-wait-`/`-h-mbox-`/`-h-timeout-`, дельты и темп, JSON-сводка; `--file` разбирает готовый захват; монитор нужен потому, что HPMCU делит UART2 с консолью AP |
| `nfs_env.py` | переключение штатной загрузки через env U-Boot: `status` (текущий `sys_bootargs` и режим `nand`/`nfs`), `on` (NFS-root, проверка `/`, `/oem`, `/userdata`), `off` (возврат на `ubi0:rootfs`); `--no-reboot` пишет переменную без перезагрузки |
| `osd_check.py` | захват кадров RTSP (`--path`, по умолчанию `/live/0`), попарный диф боксов OSD-регионов, сохранение полных кадров и кропов |
| `stream_check.py` | сквозная проверка UDP/RTP-раздачи камеры и приёмника MediaMTX: чтение `stream`/`mp4` из `/userdata/divinus.yaml` по SSH, опрос `/v3/paths/list`, декодирование `rtsp://<host>:8554/cam` через PyAV, fps и отклонение от `mp4.fps`, `--compare-dut` — сверка с собственным RTSP камеры, `--latency` — задержка ретрансляции по совпадению кадров; при `--compare-dut` эталоном fps служит сама камера, иначе `mp4.fps`; JSON и CSV в `output/bench/stream` (учётные данные в артефакты не пишутся), код 1 — отклонение, код 2 — метрика не снята |
| `geometry_check.py` | геометрия оверлеев и ePTZ: `--what zoom` — кроп на main/sub, `/mjpeg`/`/image.jpg`, критерий — нормированная корреляция с ожидаемым центром (mean\|diff\| печатается для справки); рестарт MP4 — отдельный opt-in `OKO_GEOMETRY_MP4_TESTS=1` (после смены кропа он ненадёжен); `--what boxes`/`--what osd` (opt-in `OKO_GEOMETRY_TESTS=1`) — совмещение NN-рамок и OSD-текста на 0/90/180/270; артефакты — `output/bench/geometry` |
| `api_check.py` | матрица HTTP API: GET всех эндпоинтов, POST no-op, экспорт конфига и журнала, негативные проверки (статика, имена файлов, NTP), переключение mDNS `enable=0/1` и 400 на неверное значение; opt-in `OKO_FOCUS_MOTOR_TESTS=1`/`OKO_FOCUS_AF_TESTS=1` — движение моторов и автофокус (`af=1` full/ROI и micro-fine `af=fine`) с записью AF-кривых |
| `ui_check.py` | WebUI в headless Chromium: восемь разделов, EN/RU, Apply/dirty-state, автозапуск предпросмотра, двухшаговый выбор ROI и строка автофокуса в «Видео», несохранённый выбор «Включить» переживает опрос, кнопка «Автофокус» (`af=fine`), сводка SD-карты в «Система → Статус» (сверка с `window.lastStatus`, работает и без карты), карточка mDNS в «Сети» (имя `oko-xxxx.local` и живое переключение), раскладка под окно на 1440×900 и мобильная 390×844 (нижний навбар 4×2), скриншоты (нужен Playwright) |
| `mdns_check.py` | проверка mDNS-публикации: запрос A-записи `<имя>.local` на `224.0.0.251:5353` сырым UDP-сокетом, разбор ответа (включая компрессию имён) и сверка адреса с `GET /api/mdns`; `--absent` — публикации быть не должно (после `POST ?enable=0`); avahi/nss-mdns на хосте не нужны |
| `event_check.py` | сквозная проверка событий: поднимает `services/events`, `POST /api/clip?test=1` на DUT, ждёт JPEG-пачку и клип, сверяет длительность `pre+post ±2 с` и кодеки (hevc → h264-копия) через ffprobe; `--retry` — остановка/запуск приёмника и доставка повтором, `--outbox` — вытеснение в outbox на microSD, `--require-detection` — живая детекция YOLO11; отчёт `output/bench/events/report.json` |
| `clip_soak.py` | soak детектора и событий: опрос `/api/clip` и `services/events`, тестовое событие при простое, отчёт с `sent/lost/outbox/ring_dropped` и `MemFree`; `--minutes` (по умолчанию 120), `--interval`, `--event-every`; JSON — `output/bench/events/soak-*.json` |
| `heartbeat_check.py` | heartbeat-Snapshot сна: настраивает `[http_post]` и `[sleep]` (`enable=1`, `heartbeat=1`, `idle=30`, `wake_interval=10`, `wake_check=5`, `heartbeat_interval_s=0`), перезапускает divinus и ждёт ≥3 тихих цикла; проверяет JPEG+заголовки `X-Heartbeat-*`/`X-OKO-*` в `services/events`, рост `wake_cycle`, `fail_streak=0`; `--neg` — приёмник выключен и `fail_streak` растёт, циклы продолжаются; `--no-motion` — пропустить инъекцию `?motion=1` (нужен `OKO_BENCH_API=1`); отчёт и кадры — `output/bench/heartbeat` |
| `mqtt_check.py` | сквозная проверка MQTT: поднимает `services/mqtt`, проверяет аутентификацию брокера (верный/неверный пароль), настраивает `[mqtt]` живьём через `POST /api/mqtt`, ждёт retained `online:true` и телеметрию в рекордере, затем `SIGKILL` divinus → LWT `online:false`, перезапускает divinus с `OKO_BENCH_API=1` → `online:true` и bench-событие `POST /api/mqtt?test=1`, в конце перезапускает без `OKO_BENCH_API`; `--neg` — профиль выключен и публикатор остаётся disconnected; `--deploy`, `--keep-config`; отчёт — `output/bench/mqtt/report.json` |
| `deploy_divinus.py` | копирование свежесобранного `divinus` в `/oem/usr/bin` по SSH и graceful restart (быстрая итерация, не замена прошивке) |
| `rgn_boxes_check.py` | soak 2BPP-рамок (KI-063): `--mode observe` — сэмплы fd/VmRSS/потоков и счётчиков ядра (`sfc`, `UBIFS`, `dup mClientFd`/`CHECK_GE`, oops, `rga2_reg`) каждые 10 с; `--mode cycle --target yolo\|ivs` — A/B тумблер через API; `--mode restart` — SIGHUP-рестарты с привязанным регионом (`--settle` секунд перед сэмплом, критерий — стабильный fd); `--mode config --target ivs --value true` — правка `boxes` в `/userdata/divinus.yaml` (бэкап `.oko-rgn-boxes.bak`) с рестартом; `--mode steady` — включить рамки и держать; JSON-отчёт в `output/bench/rgn`, код 1 при росте fd, VmRSS или счётчика ядра |
| `yolo_night.py` | KI-005: ночная точность YOLO по матрице `{vpss3, vi0} × {AIISP off, on}` (C1–C4). Правка `/userdata/divinus.yaml` по SSH + graceful restart на каждую конфигурацию (vi0 иначе не применяется), `night`-сцена вручную через `/api/night`, сбор истории `/api/yolo?detections=1`, метрики recall / mean best score / FP per frame по `--gt` (IoU ≥ `--iou`), день C1/C4 — контроль методики; `--repeat`, `--seconds`, `--snapshots`, `--capture-gt`, `--deploy`, `--dry-run`, `--keep-config`; `report.json`/`report.csv` и `raw-*.json` в `output/bench/yolo_night` |
| `uplink_soak.py` | 20-минутный soak двух аплинков: проверяет `active_default`, HTTP на eth/wlan-адресах и живой `divinus`, по фазам выдёргивает eth-линк и роняет шлюз временной Wi-Fi-точки (iptables в контейнере `oko-services`); `--seconds`, `--interval`, `--no-eth-toggle`, `--no-ap-toggle`; подготовка AP — `TESTBENCH.md` §9 |
| `focus_check.py` | `/api/focus`: статус AF-метрики, ROI, мотор; `--enable`, `--roi x,y,w,h`, `--move`, `--seconds/--interval/--snapshot/--strict`, CSV в `--out` (по умолчанию `output/bench/focus`); код 2 — API жив, но снимок не снят, код 1 — метрика нулевая |
| `firmware_check.py` | `/api/firmware` и `/api/flash`: версии STM32/STM8, состояние задачи; `--flash --confirm FLASH` запускает прошивку, `--negative` прогоняет проверки валидации (400) |
| `ec_bus_soak.py` | soak шины I2C3 и STM32 EC: `reboot` (счёт `SCL hold by slave` после перезагрузки), `read-window` (reboot в окне чтения EC), `detect` (i2cdetect во время reboot), `hold-scl` (команда `0x29` в `DEBUG_BUILD`, проверка отпускания линии и `0x28`-счётчиков); отчёт и лог — `output/bench/ec_bus_soak-*.json/.log` |
| `vpn_check.py` | WireGuard/AmneziaWG/OpenVPN на DUT: модуль или `/dev/net/tun`, импорт профиля через API, `up`, handshake или адрес `tun0`, маршруты, ping, `down`, негатив (хуки/plugin, delete без токена, credentials required), опционально `--iperf-peer`, reboot + автозапуск; артефакты — `output/bench/vpn` |
| `aov_i2c_diag.py` | регистровая диагностика I2C3 (STM8/EC): `--dump` — `CON/CLKDIV/IEN/IPD/FCNT/SCL_OE_DB/ST/DBGCTRL/CON1` по `0xff460000`, пады `0xff530070`, iomux `0xff538018`, `/proc/interrupts` и `i2cdetect -r -y 3` (чтение с фильтром «шинного мусора» `0x000512xx`, пока pclk закрыт); `--sample N` — чтение во время скана; `--nak-release-scl {0,1}` — `RKI2C_DBGCTRL[13]` (TRM §15.4, 0x224); `--unbind/--rebind` rk3x-i2c; `--bitbang` — 9 тактов SCL + STOP через GPIO1_D2/D3 (masked-записи HIWORD_UPDATE, iomux в GPIO и обратно); `--mux-loss`/`--restore-mux` — детерминированное воспроизведение KI-080 (сброс поля iomux SCL при выходе GPIO low) и мгновенное восстановление; `--heal N` — наблюдение с восстановлением iomux; артефакты — `output/bench/i2c-diag/<stamp>/` |

## Примеры

```sh
# консоль DUT: несколько команд подряд, окно вывода 5 с на команду
bench/run.sh serial_exec.py --wait 5 'ps | grep -E "divinus|oko-osd|rkipc"' 'cat /proc/cmdline'

# SSH без пароля в командной строке (пароль — вендорский дефолт из TESTBENCH.md §6)
bench/run.sh dut.py 'mount | grep -w " / "' 'cat /userdata/divinus.yaml'

# обычный цикл netboot: запустить захват, затем сбросить питание вручную
bench/run.sh netboot.py catch

# перезагрузка из работающей системы: скрипт сам войдёт, перезагрузит и поймает U-Boot
bench/run.sh netboot.py reboot

# одна команда в уже открытом U-Boot
bench/run.sh netboot.py cmd 'printenv ipaddr'

# OSD: 17 с наблюдения; время меняется за 1 с, телеметрия и GPS — за 16 с
bench/run.sh osd_check.py --ip 192.168.52.33 --seconds 17

# свои регионы (имя:x0,y0,x1,y1;...)
bench/run.sh osd_check.py --regions 'telemetry:0,16,760,160;gps:1500,16,1920,64'

# геометрия: кроп ePTZ и его сохранение после рестарта канала
bench/run.sh geometry_check.py --what zoom
# NN-рамки и OSD под 0/90/180/270 — только по явному разрешению:
# нужны модель, объект в кадре и неподвижная камера; код 2 — нет детекций
OKO_GEOMETRY_TESTS=1 bench/run.sh geometry_check.py --what boxes
OKO_GEOMETRY_TESTS=1 bench/run.sh geometry_check.py --what osd

# API: 133 проверки, код возврата 1 при ошибке
bench/run.sh api_check.py
# движение моторов и реальный автофокус — только по явному разрешению:
# OKO_FOCUS_MOTOR_TESTS=1 (home/zero, шаги, скорости)
# OKO_FOCUS_AF_TESTS=1 (AF full + ROI + micro-fine af=fine; кривые в
# output/bench/af_curve_*.csv; для full/ROI нужна контрастная сцена, иначе
# job честно вернёт "peak not found"; af=fine всегда завершается done)
OKO_FOCUS_MOTOR_TESTS=1 OKO_FOCUS_AF_TESTS=1 bench/run.sh api_check.py

# mDNS: имя и адрес берутся из GET /api/mdns; ответ ждём 5 с
bench/run.sh mdns_check.py
bench/run.sh mdns_check.py oko-e80e --ip 192.168.52.33 --timeout 10
# после POST /api/mdns?enable=0 ответов быть не должно (код 0)
bench/run.sh mdns_check.py --absent

# помощь фокусировке: 20 отсчётов, CSV и снимок; --strict падает на статичной метрике
bench/run.sh focus_check.py --enable --seconds 20 --interval 1 --snapshot
bench/run.sh focus_check.py --roi 2500,2500,5000,5000 --seconds 5

# версии контроллеров, негативные проверки и реальная прошивка STM8
bench/run.sh firmware_check.py
# --negative требует project/app/stm8-motor/build/app-2.1.1.ihx
# (make -f Makefile.stm8 BOARD=test; версия в имени файла обязательна)
bench/run.sh firmware_check.py --negative
bench/run.sh firmware_check.py --flash --target stm8 \
  --file project/app/stm8-motor/build/app-2.1.1.ihx --confirm FLASH
# STM32: реальная прошивка CBT6 (слот 120 КиБ); 1.0.0 = rollback.
# Версия берётся из имени файла, отдельного --version нет.
bench/run.sh firmware_check.py --flash --target stm32 \
  --file output/bench/stm32/app-1.0.0.bin --confirm FLASH

# RTSP/OSD: RTSP включается `rtsp.enable` (нужен работающий VENC-канал,
# т.е. `mp4.enable` или `substream.enable`); `stream.enable` к RTSP отношения
# не имеет — он только добавляет UDP/RTP-раздачу
bench/run.sh osd_check.py --path /live/0 --seconds 5 --frames 2

# UDP/RTP → MediaMTX → RTSP: 20 с, сверка с RTSP самой камеры и задержка
bench/run.sh stream_check.py --seconds 20 --latency --compare-dut
# 10-минутный прогон на стабильность (артефакты — output/bench/stream)
bench/run.sh stream_check.py --seconds 600 --latency --compare-dut --interval 5
# прошивка STM8 (после сборки app-2.1.1.ihx); требует --confirm FLASH
bench/run.sh firmware_check.py --flash --target stm8 \
  --file project/app/stm8-motor/build/app-2.1.1.ihx --confirm FLASH

# STM32: безопасная проверка перехода app → bootloader → app без записи
bench/run.sh dut.py '/etc/init.d/S21oko-ec stop' \
  '/oem/usr/bin/oko-ec info' '/oem/usr/bin/oko-ec enter-update' \
  '/oem/usr/bin/oko-ec info' '/oem/usr/bin/oko-ec reset' 'sleep 2' \
  '/oem/usr/bin/oko-ec info' '/etc/init.d/S21oko-ec start'
# ожидается device_type 2 → 1 → 2, версия и ячейки не меняются

# быстрая выкатка собранного divinus в /oem без пересборки образа
make -C project/app/divinus            # в цикле сборки SDK
bench/run.sh deploy_divinus.py

# WebUI: отдельный venv с Playwright и Chromium
python3 -m venv /tmp/pw && /tmp/pw/bin/pip install playwright
/tmp/pw/bin/playwright install chromium
/tmp/pw/bin/python bench/ui_check.py
# проверяет раскладку: страница не скроллится ни на одной из 8 вкладок
# (1440×900 и 390×844), Live в две колонки на десктопе, нижний навбар 4×2
# на мобильном, короткие подписи, скрытые uptime/load/SoC и кнопки ≥ 40 px;
# с OKO_FOCUS_AF_TESTS=1 дополнительно жмёт «Автофокус» и ждёт scope=fine
OKO_FOCUS_AF_TESTS=1 bench/run.sh ui_check.py
```

`ui_check.py` не входит в `requirements.txt`: Playwright тянет браузер (~120 МБ),
а `api_check.py` использует только стандартную библиотеку. Если Chromium не
стартует с `libasound.so.2: cannot open shared object file`, поставьте
`libasound2`/`libasound2t64` либо без root распакуйте пакет:

```sh
cd /tmp/kilo && apt-get download libasound2t64
mkdir -p libasound && dpkg-deb -x libasound2t64_*.deb libasound
LD_LIBRARY_PATH=/tmp/kilo/libasound/usr/lib/x86_64-linux-gnu bench/run.sh ui_check.py
```

## Soak 2BPP-рамок (KI-063)

`rgn_boxes_check.py` проверяет жизненный цикл overlay-регионов: YOLO (handle
12, слой 6) и IVS (handle 13, слой 5). Раньше включение IVS-рамок вешало
MPP/VENC, поэтому скрипт снимает fd, `VmRSS`, число потоков и счётчики
`dmesg` (`rockchip-sfc`, `UBIFS error`, `dup mClientFd`/`CHECK_GE`, oops,
`rga2_reg`) и печатает отчёт с пределом роста fd (+8) и VmRSS (±2 МиБ).

```sh
# 30-минутный baseline: IVS без рамок, main RTSP + OSD
bench/run.sh rgn_boxes_check.py --mode observe --target ivs --seconds 1800

# A/B-цикл 200 раз: включить/выключить рамки YOLO через API
bench/run.sh rgn_boxes_check.py --mode cycle --target yolo --count 200

# 20 graceful-рестартов с привязанным регионом (утечки fd/CMA не должно быть)
bench/run.sh rgn_boxes_check.py --mode restart --target ivs --count 20

# включить ivs.boxes в конфиге и перезапустить приложение (режим restart)
bench/run.sh rgn_boxes_check.py --mode config --target ivs --value true
bench/run.sh rgn_boxes_check.py --mode steady --target ivs --seconds 1800
```

`--mode config` правит ключ `boxes` в секции цели, сохраняет копию в
`/userdata/divinus.yaml.oko-rgn-boxes.bak`, затем делает SIGHUP; `--restore`
возвращает копию. Живой тумблер IVS (`--mode cycle --target ivs`) допустим
только при `ivs.boxes_mode: live` и только на NFS-netboot: ошибка MPP
уводит плату в D-state и требует `reboot -f`. Отчёты — `output/bench/rgn/`,
консоль UART (с сообщениями MPP) снимать отдельно.

## Ночная точность YOLO (KI-005)

`yolo_night.py` снимает A/B-метрики детектора на неподвижной сцене. Матрица:
C1 `vpss3`+AIISP off, C2 `vpss3`+on, C3 `vi0`+on, C4 `vi0`+off. Ночь — все
четыре конфигурации по `--repeat` прогонов (по умолчанию 3 × 60 с при
`yolo.fps=5`), день — контроль C1/C4: расхождение среднего score больше 0.05
означает нестабильную сцену, и числа публиковать нельзя.

Подготовка сцены: 3–5 объектов COCO на штативе, неподвижно; днём
`/api/night` ставит `ircut=on, irled=off`, ночью `ircut=off, irled=on,
grayscale=on`, внешний свет выключен. IVS и MJPEG скрипт принудительно
выключает: при них `vpss3` молча падает на `vi0` (`main.c`). Перед
`/api/night` скрипт проверяет пины: если `ir_cut_pin1`/`irled_pin` равны 999
(штатный конфиг на плате без ИК-железа), день/ночь не запускаются.

Для платы без ИК-подсветки и управляемого IR-cut есть `--phase ambient`:
C1–C4 без `/api/night`, числа трактуются как дневные и **не** являются
ночной точностью. Контроль стабильности C1/C4 в этом режиме пишется в
`ambient_control`.

Модель не входит в образ. Сборка: `tools/rknn-toolkit2/README.md`
(Docker-образы toolkit/export, калибровочные кадры с DUT), затем
`POST /api/models` (multipart `file`) и `POST /api/models/activate?name=…`;
активация проверяет `rknn_init`, формы тензоров и пробный инференс, после
чего сохраняет `yolo.model`. Если в `/userdata/models` ровно один `.rknn`,
`yolo_night.py` активирует его сам на preflight.

Каждая конфигурация пишется в `/userdata/divinus.yaml` и применяется
`kill -HUP` (vi0 требует пересоздания пайплайна). Исходный файл сохраняется в
`/userdata/divinus.yaml.oko-yolo-night.bak` и восстанавливается в конце, если
не указан `--keep-config`. Секреты — только через `OKO_DUT_PASSWORD`,
`OKO_RTSP_USER`/`OKO_RTSP_PASS`.

```sh
# 1) снять кадр и шаблон разметки (сцена уже собрана, день)
bench/run.sh yolo_night.py --capture-gt
#    оператор размечает output/bench/yolo_night/gt.json: cls и x,y,w,h в 0..10000
# 2) полный прогон: день (C1, C4) → ночь (C1–C4 × 3)
bench/run.sh yolo_night.py --gt output/bench/yolo_night/gt.json --snapshots
# 3) только ночь, две конфигурации, один повтор
bench/run.sh yolo_night.py --phase night --configs C2,C4 --repeat 1 \
  --gt output/bench/yolo_night/gt.json
# 4) плата без ИК: C1–C4 в ambient-свете (дневные числа, не «ночь»)
bench/run.sh yolo_night.py --phase ambient --configs C1,C2,C3,C4 \
  --repeat 1 --seconds 40 --gt output/bench/yolo_night/gt.json
```

Метрики на конфигурацию: `recall` (доля GT-объектов, пойманных хотя бы одним
кадром), `detection_rate` (средняя доля кадров с попаданием по объекту),
`mean_best_score` (средний максимум score по пойманным объектам), `fp_frame`
(доля инференсов с несопоставленной детекцией), плюс `detections_per_frame` и
`empty_frames`. Результаты: `report.json` (сырые
прогоны, сводка, `day_control`), `report.csv`, `raw-<фаза>-<конфиг>-rN.json`.
Код возврата: 0 — измерение валидно, 1 — контроль дня не прошёл, 2 — сбой
стенда/SSH/API.

Тяжёлые медиа-прогоны исчерпывают CMA: после двух полных матриц перезагрузите
DUT и проверьте `dmesg -l err` (ошибки `-12`). Сцена обесценивает GT при любом
сдвиге камеры или объектов; повтор одной конфигурации должен давать
|Δscore| ≤ 0.05, иначе числа не публикуются. После reboot конфиг откатывается
к `.bak`, поэтому скрипт вызывает `/api/cmd?save` после применения и после
восстановления.

## VPN-пир на хосте

Для `wg0` пир поднимается штатным WireGuard ядра хоста:

```sh
sudo modprobe wireguard
sudo ip link add wgpeer0 type wireguard
# ключи: wg genkey | tee peer.key | wg pubkey > peer.pub (x25519 совместим с awg)
sudo ip addr add 10.8.0.1/24 dev wgpeer0
sudo wg set wgpeer0 private-key peer.key listen-port 51820 peer <DUT_PUB>   allowed-ips 10.8.0.2/32
sudo ip link set wgpeer0 up
```

Для `awg0` используется модуль из того же вендорного слепка, что и в прошивке:

```sh
make -C sysdrv/drv_ko/amneziawg/src KERNELDIR=/lib/modules/$(uname -r)/build   WIREGUARD_VERSION=3.1.20260906
sudo insmod sysdrv/drv_ko/amneziawg/src/amneziawg.ko
make -C project/app/vpn-tools/vendor/amneziawg-tools-3.1.20260812/src   PLATFORM=linux wg
sudo install -m 755 project/app/vpn-tools/vendor/amneziawg-tools-3.1.20260812/src/wg   /usr/local/bin/awg
```

Хостовый `awg`-конфиг (`/etc/amnezia/amneziawg/host.conf`) повторяет параметры
профиля DUT (`Jc`, `Jmin`, `Jmax`, `S1…S4`, `H1…H4`, `I1…I5`,
`HeaderProtectionKey`, `ContentPaddingAddition`); ключи генерируются
`wg genkey`/`awg genkey`. Для маршрута в туннель и полного туннеля:

```sh
sudo sysctl -w net.ipv4.ip_forward=1
sudo iptables -t nat -A POSTROUTING -o <uplink> -j MASQUERADE
sudo awg setconf awgpeer0 /etc/amnezia/amneziawg/host.conf
sudo ip link set awgpeer0 up
```

Затем на DUT импортируется профиль (Network → VPN или API) и запускается
`bench/run.sh vpn_check.py --peer-host 192.168.52.1 --profile output/bench/vpn/awg0.conf`.
Для reboot-проверки автозапуска добавьте `--reboot`; профиль удаляется в конце,
если не указан `--keep`. Артефакты: `output/bench/vpn/report.json`, `ssh.log`,
`profile.conf`.

## OpenVPN-пир на хосте

`vpn_check.py` принимает `.ovpn`-профиль с `--type ovpn`; при
`auth-user-pass` логин и пароль передаются через `--creds-user/--creds-pass`
(или `OKO_VPN_USER`/`OKO_VPN_PASS`), на DUT они сохраняются в
`/userdata/vpn/tun0.pass` с правами 0600. Для полного туннеля и проброса в LAN
на хосте:

```sh
sudo sysctl -w net.ipv4.ip_forward=1
sudo iptables -t nat -A POSTROUTING -o <uplink> -j MASQUERADE
```

Пример прогона: `bench/run.sh vpn_check.py --type ovpn
--profile output/bench/vpn/client.ovpn --creds-user oko --creds-pass secret
--ping-target 10.8.0.1 --iperf-peer 192.168.52.1 --reboot`.

## Netboot (TFTP/NFS)

Итерационный контур без прошивки SPI NAND: ядро `output/image/boot.img`
отдаётся по TFTP, rootfs монтируется по NFS с `/srv/nfs/rootfs`, а
`oem`/`userdata` — из `<nfsroot>/.nfsroot/` (S20linkmount). Сервисы
(TFTP с корнем `output/image` и NFS-экспорт) поднимает контейнер `services/`
через `tools/native/setup_netboot.sh`; на хосте данные лежат в
`output/srv/nfs/rootfs`, внутри контейнера путь остаётся `/srv/nfs/rootfs`.
Настройка, деплой rootfs и возврат на NAND — TESTBENCH.md §7D.

```sh
# деплой свежей rootfs в NFS-экспорт (бэкап — tar в output/bench/);
# --exclude=/.nfsroot сохраняет oem/userdata-подкаталоги экспорта
rsync -a --delete --exclude=/.nfsroot output/out/rootfs_uclibc_rv1106/ output/srv/nfs/rootfs/
rsync -a --delete output/out/oem/ output/srv/nfs/rootfs/.nfsroot/oem/
rsync -a --delete output/out/userdata/ output/srv/nfs/rootfs/.nfsroot/userdata/

# загрузка: штатный boot.img уже содержит rv1106-netboot.config
bench/run.sh netboot.py reboot
bench/run.sh dut.py 'cat /proc/cmdline' 'mount | grep nfs'

# вход в rockusb без кнопки Maskrom (затем upgrade_tool ld / uf)
bench/run.sh netboot.py reboot --uboot-command download
```

### Переключение NAND ↔ NFS через env U-Boot

`nfs_env.py` пишет `sys_bootargs` в env-раздел (`fw_setenv`) и проверяет
результат после перезагрузки. Перепрошивка ядра не нужна: NFS-root включён
в штатный образ.

```sh
# режим и адрес DUT до переключения (при DHCP это 192.168.52.33)
bench/run.sh nfs_env.py status --host 192.168.52.33

# NFS-root: reboot, ожидание SSH на 192.168.52.150, проверка /,/oem,/userdata
bench/run.sh nfs_env.py on --host 192.168.52.33

# возврат на ubifs: проверка root=ubi0:rootfs и ubifs в mount
bench/run.sh nfs_env.py off --host 192.168.52.150

# только записать переменную без перезагрузки
bench/run.sh nfs_env.py on --no-reboot --host 192.168.52.150
```

Строки `on`/`off` повторяют `netboot.py` и дефолт `output/image/.env.txt`;
при смене разметки или `RK_BOOTARGS_CMA_SIZE` обновите `OKO_CMA_SIZE`
и константу `NAND_BOOTARGS`. Аварийное восстановление из U-Boot по UART —
TESTBENCH.md §7D.

## Переменные окружения

| Переменная | По умолчанию | Смысл |
|---|---|---|
| `OKO_BENCH_OUT` | `output/bench` | каталог логов, кадров и кропов |
| `OKO_BENCH_VENV` | `$OKO_BENCH_OUT/venv` | путь к virtualenv |
| `OKO_SERIAL_PORT` | `telnet://192.168.52.17:23` | консоль: `telnet://host:port` (ESP32-мост) или `/dev/ttyACM*` |
| `OKO_BRIDGE` | `192.168.52.17` | адрес ESP32-моста (REST, `/sensor`, питание DUT) |
| `OKO_BRIDGE_USER` / `OKO_BRIDGE_PASSWORD` | `admin` / `admin` | учётка моста; в свежей прошивке пароль может быть сгенерирован при первом старте |
| `OKO_DUT_HOST` | `192.168.52.150` | адрес DUT по умолчанию (SSH/RTSP; текущий DHCP-адрес — `192.168.52.33`, см. TESTBENCH.md §5) |
| `OKO_DUT_PASSWORD` | `oko-change-me` | пароль HTTP Basic/WebUI (как в `api_check.py`) |
| `OKO_DUT_SSH_PASSWORD` | `luckfox` | пароль root для SSH (публичный вендорский дефолт) |
| `OKO_RTSP_USER` / `OKO_RTSP_PASS` | `admin` / `oko-change-me` | учётка divinus (фабричный дефолт) |
| `OKO_EVENTS_URL` | `http://127.0.0.1:8090` | адрес `services/events` с хоста (проверки в `event_check.py`/`heartbeat_check.py`) |
| `OKO_EVENTS_ADDR` | `192.168.52.200:8090` | адрес приёмника, который прописывается камере в `http_post.host` для heartbeat |
| `OKO_MQTT_URL` | `http://127.0.0.1:8092` | HTTP-API рекордера `services/mqtt` с хоста (`mqtt_check.py`) |
| `OKO_MQTT_ADDR` | `192.168.52.200:1883` | адрес брокера, который прописывается камере в `mqtt.host` |
| `OKO_MQTT_USER` / `OKO_MQTT_PASS` | `oko` / `oko` | стендовая учётка устройства у брокера |
| `OKO_MQTT_REC_USER` / `OKO_MQTT_REC_PASS` | `oko-rec` / `oko-rec` | учётка рекордера |
| `OKO_WIFI_SSID` / `OKO_WIFI_PSK` | — | стендовая точка доступа для ручных STA-проверок; в репозиторий не пишутся |
| `OKO_UPLINK_DUT` | `192.168.44.50` | адрес DUT по Wi-Fi для `uplink_soak.py` (управление и проверка wlan-аплинка) |
| `OKO_UPLINK_ETH` | `192.168.52.33` | адрес DUT по Ethernet для `uplink_soak.py` |
| `OKO_UPLINK_AP_IF` / `OKO_UPLINK_AP_IP` | `wlan0` / `192.168.44.1` | интерфейс и адрес временной точки доступа на хосте (контейнер `oko-services`) |
| `OKO_TFTP_HOST` | `192.168.52.200` | `serverip` для TFTP/NFS (netboot) |
| `OKO_NFS_IP` | `192.168.52.150` | статический адрес DUT в NFS-режиме (`nfs_env.py`, `ip=`) |
| `OKO_NAND_HOST` | `192.168.52.33` | DHCP-адрес DUT в NAND-режиме (проверка после `nfs_env.py off`) |
| `OKO_CMA_SIZE` | `66M` | `rk_dma_heap_cma` в строках `nfs_env.py`/`netboot.py` (как `RK_BOOTARGS_CMA_SIZE`) |
| `OKO_EC_BUS` | `3` | номер шины i2c-tools (`3` = `/dev/i2c-3`) для `ec_bus_soak.py` |
| `OKO_GATEWAY` | `192.168.52.254` | `gatewayip` (netboot) |
| `OKO_NFSROOT` | `192.168.52.200:/srv/nfs/rootfs` | NFS-корень для `sys_bootargs` |
| `OKO_VPN_PEER` | — | адрес VPN-пира на хосте для `vpn_check.py` (`--peer-host` важнее) |
| `OKO_VPN_USER` / `OKO_VPN_PASS` | — | учётка `auth-user-pass` для OpenVPN-профиля (`--creds-user/--creds-pass`) |

Секреты в репозитории не хранятся: только публичные вендорские дефолты и
переопределение переменными окружения.

## Артефакты

- Логи U-Boot/netboot: `output/bench/netboot-<YYYYmmdd-HHMMSS>.log`.
- Логи переключения NAND/NFS: `output/bench/nfs-env-<YYYYmmdd-HHMMSS>.log`.
- Кадры и кропы OSD: `output/bench/osd-<YYYYmmdd-HHMMSS>-fNN[-<reg>].png`.
- Скриншоты WebUI: `output/bench/ui/<раздел>.png` и `mobile-system.png`.
- VPN: `output/bench/vpn/report.json`, `ssh.log`, `profile.conf` (тестовый
  профиль; содержит ключи — не коммитить).
- Soak шины EC: `output/bench/ec_bus_soak-<YYYYmmdd-HHMMSS>.json` и `.log`.
- Heartbeat сна: `output/bench/heartbeat/report.json` и полученные `hb-<ts>.jpg`.
- MQTT: `output/bench/mqtt/report.json`.
- Мощность и сон: `output/bench/power/<stamp>-<scenario>/` (`raw.csv`,
  `events.log`, `summary.json`, `report.md`); одиночный `ina_log.py` —
  `output/bench/power/<stamp>-raw.csv` и `<stamp>-raw.summary.json`.
- Каталог создаётся автоматически; `output/` в `.gitignore`.

## Heartbeat сна (Snapshot)

`heartbeat_check.py` проверяет proof-of-life кадр из `[sleep]`: один JPEG за
тихий wake-цикл, отправленный перед повторным suspend.

Лабораторная сцена постоянно даёт IVS ~1000 ‰, поэтому перед прогоном
поднимите `ivs.min_area` (или выключите IVS/YOLO) — иначе тихих циклов не
будет и heartbeat не появится.

```sh
docker compose -f services/events/docker-compose.yml up -d
bench/run.sh heartbeat_check.py                 # ≥3 тихих цикла + motion-фаза
bench/run.sh heartbeat_check.py --no-motion     # без инъектора
bench/run.sh heartbeat_check.py --neg           # приёмник выключен: fail_streak растёт
```

- Скрипт сам включает `[http_post]` (`host=$OKO_EVENTS_ADDR`,
  `url=/ingest/frame`) и `[sleep]` (`heartbeat=1`, `idle=30`,
  `wake_interval=10`, `wake_check=5`, `heartbeat_interval_s=0`), затем делает
  `POST /api/cmd?restart`; в конце возвращает прежние настройки
  (`--keep-config` — не возвращать).
- Приёмник принимает кадр на `PUT /ingest/heartbeat` и отдаёт записи через
  `GET /api/heartbeats?limit=`. Заголовки: `X-Heartbeat`, `X-Heartbeat-Ts`,
  `X-OKO-Idle-S`, `X-OKO-Motion-Area` (−1 — IVS выключен), `X-OKO-Sleep`,
  `X-OKO-Wake-Cycle`.
- Motion-фаза использует `POST /api/sleep?motion=1`, который доступен только
  при запуске divinus с `OKO_BENCH_API=1`. На стенде сеть после resume
  поднимается к концу 5-секундного окна, поэтому инъектору нужно больше
  времени: `--wake-check 30` (проверено 2026-11-18: `probe` → `awake`, цикл
  без heartbeat, следующий тихий — с кадром). Без env или при слишком
  коротком окне проверка помечается `SKIP`, а не падает.
- `--neg` останавливает приёмник: камера всё равно засыпает, а
  `heartbeat_fail_streak` в `/api/status.sleep` растёт.

## Мощность и сон (INA3221 ESP32-моста)

Второй INA3221 на мосту измеряет вход 5 В всего DUT (CH3; CH2 и CH1 —
вспомогательные ветви, их ток тоже зависит от питания DUT). Прошивка моста 1.11.1 отдаёт показания в
`GET /sensor`; собственный SD-лог `ina3221_chN.csv` пишет раз в ≈10.2 с, поэтому
профили сна снимаются хост-опросом.

`ina_log.py` пишет CSV с метками фаз; `sleep_power.py` управляет сценарием и
сводит отчёт. Окна глубокого сна берутся из `dmesg` (`PM: suspend entry/exit`),
потому что по HTTP DUT недоступен за секунды до и после реального suspend.

```sh
bench/run.sh ina_log.py --duration 30 --label idle
bench/run.sh sleep_power.py --scenario levels --hz 5   # off/boot/idle/полка
bench/run.sh sleep_power.py --scenario cycle --hz 5    # один цикл сна
bench/run.sh sleep_power.py --scenario motion          # сон с IVS (движение)
bench/run.sh sleep_power.py --scenario aov --aov-seconds 150  # PMU, интервал 60 с
bench/run.sh sleep_power.py --scenario aov --aov-mcu --aov-keep-isp  # AOV MCU
bench/run.sh recover_dut.py --ssh --restart             # после зависшего сна
bench/run.sh sleep_power.py --scenario i2c --i2c-cycles 300 --expect-present
bench/run.sh sleep_power.py --scenario aov --aov-seconds 60 --expect-present
bench/run.sh sleep_power.py --scenario aov --aov-mcu --motor-sleep on
bench/run.sh sleep_power.py --scenario peaks           # YOLO/мотор/поток
bench/run.sh sleep_power.py --scenario stm8 --stm8-soak 300
docker compose -f services/events/docker-compose.yml up -d
bench/run.sh sleep_power.py --scenario soak --soak-minutes 60
```

Базовая линия 2026-11-22: `off` 0.8 мА, `idle` 364 мА / 1.72 Вт, полка deep
suspend 86 мА (min 47, p05 124), окно 2.4 с за цикл ≈13 с, 4.9 мВт·ч/цикл
(медиана), 60-мин soak — 279 циклов и ≥200 heartbeat; подробности и ограничения
— `TESTBENCH.md` §8.

2026-11-23: сценарий `i2c` — 300 циклов к STM8 без suspend, 0 ошибок и
`present=true`; `aov` с `--expect-present` при `aov_mcu=1` фиксирует отказ
(I2C3 мертва при `keep_isp=1`, AP не просыпается при `keep_isp=0`, код 3,
`stm8-recovery.md`); `aov_mcu=0` даёт тихий цикл. Для `keep_isp=1` подтверждён
H2: `SCL=0` держит контроллер `rk3x`; unbind/rebind `ff460000.i2c` (i2c3)
отпускает линию и возвращает 0x40/41/42/60 без power cycle. Гейт и артефакты —
KI-080, KI-072, `TESTBENCH.md` §7-AOV. Сценарий `motion` (IVS включена) на стенде 2026-11-22:
3 цикла, каждый wake → `awake` из-за полнокадрового IVS-события (area 1000‰),
бодрствование 24–26 с и энергия цикла 15.3–17.9 мВт·ч (среднее 16.85) против
4.9 мВт·ч при `wake_check=5`; парный цикл без IVS при `wake_check=30` — того же
порядка, т.е. основной вклад даёт длинное окно проверки. Сценарий `aov`
(2026-11-22): при `wake_interval_s=60` PMU-счётчик `0x780`, но deep suspend
2.4 с и цикл 12–18 с; AOV MCU (`--aov-mcu --aov-keep-isp`) даёт `aov_state=off`
после resume и `mcu_run_count=0`, форсирование meta `ae_mode=1` не помогает
(KI-072). Мост держит не больше трёх сессий: `sleep_power.py` держит
одну cookie-сессию на логгер и питание и делает logout в конце; параллельные
ручные `curl`-сессии дают `429`.

## Wi-Fi на стенде

USB-адаптер RTL8821CU (`0bda:c811`) настраивается через WebUI **Сеть → Wi-Fi**
или `/api/wifi`; на стенде удобны прямые запросы:

```sh
# состояние и скан
curl -u admin:oko-change-me http://192.168.52.15/api/wifi
curl -u admin:oko-change-me 'http://192.168.52.15/api/wifi?scan=1'
sleep 14; curl -u admin:oko-change-me http://192.168.52.15/api/wifi/scan

# STA (пароль не попадает в историю: читайте из переменной окружения)
curl -u admin:oko-change-me -X POST \
  --data-urlencode 'mode=sta' --data-urlencode "ssid=$OKO_WIFI_SSID" \
  --data-urlencode "psk=$OKO_WIFI_PSK" --data-urlencode 'dhcp=on' \
  http://192.168.52.15/api/wifi

# SoftAP-фолбэк
curl -u admin:oko-change-me -X POST \
  --data-urlencode 'mode=ap' --data-urlencode 'ap_ssid=OKO-TEST' \
  http://192.168.52.15/api/wifi
```

`/api/wifi/apinfo` отдаёт SSID/пароль/адрес точки доступа, в том числе
заводский пароль `ybrjveytljdthzq`, пока `oko_wlan_ap_psk` не задан.
Пароль станции в ответах не возвращается (`psk_set`). Настройки лежат в
env U-Boot (`oko_wlan_*`), поэтому их можно посмотреть
`fw_printenv | grep oko_wlan`.

### SD-конфиг `oko-wifi.conf`

Файл кладётся в корень первого раздела microSD. При монтировании карты
(udev `add` или `oko-sdcard mount`) `oko-wifi sdimport` проверяет файл, пишет
настройки в env, перезапускает Wi-Fi и переименовывает файл в
`oko-wifi.conf.applied-<sha1-8>`. Повторный импорт того же содержимого
пропускается.

```ini
# комментарии и пустые строки игнорируются
# mode: sta | ap | both | off
mode=sta
ssid=lab-wifi
# 8..63 символа; пусто — открытая сеть
pass=change-me-123
# ip: dhcp | static
ip=dhcp
# для static:
ipaddr=192.168.52.60
netmask=255.255.255.0
gateway=192.168.52.254
dns=192.168.52.254
# пусто — OKO-XXXX из MAC
ap_ssid=OKO-LAB
# пусто — заводский ybrjveytljdthzq; точка всегда WPA2-PSK, не открытая
ap_psk=hotspot-pass-1
```

Проверка без карты: создать файл в `/mnt/sdcard/oko-wifi.conf` на смонтированной
карте и вызвать `/usr/bin/oko-wifi sdimport`.

## Порядок работы и CMA

- После двух и более тяжёлых медиа-прогонов подряд (`ui_check.py`,
  `geometry_check.py`, ручная отладка ePTZ/поворота) перезагружайте DUT:
  `bench/run.sh dut.py --host 192.168.52.33 'reboot'` и подождите 50–60 с.
- Признаки исчерпания CMA (решающие): `dmesg -l err` с
  `cma_alloc ... ret: -12`, `/image.jpg` отвечает 503, RTSP не отдаёт кадры.
  Высокий `loadavg` и потоки ядра в состоянии `DW` сами по себе не признак:
  на здоровой плате MPP-потоки (`vlog`, `valloc`, `vpss`, `venc`, …) тоже
  висят в `DW`, а `loadavg1` бывает 7–10. Лечение подтверждённого клина —
  только перезагрузка.
- Перед замерами геометрии убедитесь, что `/image.jpg` отвечает 200 и RTSP
  отдаёт кадры; иначе результаты диффов недостоверны.
- AF-метрика после нескольких циклов `enable=1/0` без перезагрузки может
  оставаться `idle`/`no_stats` (AFM не отдаёт ячейки). `api_check.py`
  опрашивает `status` до 20 с; если метрика так и не поднялась — перезагрузите
  DUT и повторите на свежей загрузке.

## Типичные ошибки

- `ModuleNotFoundError: serial/av/paramiko` — не создан venv: выполните
  `bench/setup.sh` (нужны `python3-venv` и доступ к PyPI).
- `Permission denied: '/dev/ttyACM0'` — добавьте пользователя в группу `dialout`
  и перелогиньтесь: `sudo usermod -aG dialout "$USER"`.
- `device or resource busy` — порт держит другой процесс (`fuser /dev/ttyACM0`);
  `netboot.py` и `serial_exec.py` запускайте последовательно.
- `cannot open telnet://192.168.52.17:23: timed out` — мост выключен или занят
  другим telnet-клиентом; закройте его и повторите. Если мост недоступен,
  переключитесь на USB-UART: `OKO_SERIAL_PORT=/dev/ttyACM0`.
- `U-Boot prompt not found` — устройство уже загрузило Linux (используйте
  `netboot.py reboot`) либо автозагрузка не прерывается (проверьте `bootdelay`).
- PyAV не ставится из-за отсутствия колеса под версию Python — поставьте
  системный пакет (`sudo apt install python3-av`) и запускайте `osd_check.py`
  системным Python; остальные скрипты от этого не зависят.
- RTSP пуст или нет OSD — проверьте `divinus`/`oko-osd` в `ps`, конфиг
  `/userdata/divinus.yaml` и что кадры снимаются с RTSP, а не с `/image.jpg`
  (JPEG-снимок кодируется отдельным VENC-каналом без RGN).

## Регионы OSD

Боксы по умолчанию:
- `time` — `1600,1024,1920,1080` (правый нижний угол);
- `telemetry` — `0,16,760,160` (четыре строки слева сверху);
- `gps` — `1500,16,1920,64` (справа сверху);
- `msg` — `0,160,1200,224` (слева сверху под телеметрией);
- `sys` — `0,960,700,1080` (CPU/память/температура слева снизу).

Они соответствуют `regN_posx/posy` в `project/app/divinus/files/divinus.yaml`.
При смене раскладки OSD правьте значения в обоих местах.