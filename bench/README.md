# Утилиты тестового стенда

Скрипты для работы с DUT (`Luckfox Pico Pro Max`, RV1106) по UART, SSH и RTSP.
Каталог не участвует в сборке прошивки; зависимости ставятся в отдельный venv
внутри `output/` (gitignore).

## Быстрый старт

```sh
bench/setup.sh                          # создать output/bench/venv и поставить зависимости
bench/run.sh serial_exec.py 'cat /proc/uptime'
bench/run.sh dut.py 'mount | grep -w " / "'
```

`bench/run.sh` сам вызывает `setup.sh`, если venv ещё нет.

## Скрипты

| Скрипт | Назначение |
|---|---|
| `serial_exec.py` | вход в консоль по UART (`/dev/ttyACM*`, 115200 8N1) и выполнение команд на DUT |
| `dut.py` | выполнение команд по SSH через `paramiko` |
| `osd_check.py` | захват кадров RTSP (`--path`, по умолчанию `/live/0`), попарный диф боксов OSD-регионов, сохранение полных кадров и кропов |
| `geometry_check.py` | геометрия оверлеев и ePTZ: `--what zoom` — кроп на main/sub, `/mjpeg`/`/image.jpg` и переживание рестарта MP4; `--what boxes`/`--what osd` (opt-in `OKO_GEOMETRY_TESTS=1`) — совмещение NN-рамок и OSD-текста на 0/90/180/270; артефакты — `output/bench/geometry` |
| `api_check.py` | матрица HTTP API: GET всех эндпоинтов, POST no-op, экспорт конфига и журнала, негативные проверки (статика, имена файлов, NTP); opt-in `OKO_FOCUS_MOTOR_TESTS=1`/`OKO_FOCUS_AF_TESTS=1` — движение моторов и автофокус (`af=1` full/ROI и micro-fine `af=fine`) с записью AF-кривых |
| `ui_check.py` | WebUI в headless Chromium: восемь разделов, EN/RU, Apply/dirty-state, автозапуск предпросмотра, двухшаговый выбор ROI и строка автофокуса в «Видео», несохранённый выбор «Включить» переживает опрос, кнопка «Автофокус» (`af=fine`), сводка SD-карты в «Система → Статус» (сверка с `window.lastStatus`, работает и без карты), раскладка под окно на 1440×900 и мобильная 390×844 (нижний навбар 4×2), скриншоты (нужен Playwright) |
| `deploy_divinus.py` | копирование свежесобранного `divinus` в `/oem/usr/bin` по SSH и graceful restart (быстрая итерация, не замена прошивке) |
| `focus_check.py` | `/api/focus`: статус AF-метрики, ROI, мотор; `--enable`, `--roi x,y,w,h`, `--move`, `--seconds/--interval/--snapshot/--strict`, CSV в `--out` (по умолчанию `output/bench/focus`); код 2 — API жив, но снимок не снят, код 1 — метрика нулевая |
| `firmware_check.py` | `/api/firmware` и `/api/flash`: версии STM32/STM8, состояние задачи; `--flash --confirm FLASH` запускает прошивку, `--negative` прогоняет проверки валидации (400) |
| `vpn_check.py` | WireGuard/AmneziaWG на DUT: модуль, `awg version`, импорт `.conf` через API, `up`, handshake, маршруты, ping, `down`, негатив (хуки, delete без токена), опционально reboot + автозапуск; артефакты — `output/bench/vpn` |

## Примеры

```sh
# консоль DUT: несколько команд подряд, окно вывода 5 с на команду
bench/run.sh serial_exec.py --wait 5 'ps | grep -E "divinus|oko-osd|rkipc"' 'cat /proc/cmdline'

# SSH без пароля в командной строке (пароль — вендорский дефолт из TESTBENCH.md §6)
bench/run.sh dut.py 'mount | grep -w " / "' 'cat /userdata/divinus.yaml'

# OSD: 17 с наблюдения; время меняется за 1 с, телеметрия и GPS — за 16 с
bench/run.sh osd_check.py --ip 192.168.52.97 --seconds 17

# свои регионы (имя:x0,y0,x1,y1;...)
bench/run.sh osd_check.py --regions 'telemetry:0,16,760,160;gps:1500,16,1920,64'

# геометрия: кроп ePTZ и его сохранение после рестарта канала
bench/run.sh geometry_check.py --what zoom
# NN-рамки и OSD под 0/90/180/270 — только по явному разрешению:
# нужны модель, объект в кадре и неподвижная камера; код 2 — нет детекций
OKO_GEOMETRY_TESTS=1 bench/run.sh geometry_check.py --what boxes
OKO_GEOMETRY_TESTS=1 bench/run.sh geometry_check.py --what osd

# API: 96 проверок, код возврата 1 при ошибке
bench/run.sh api_check.py
# движение моторов и реальный автофокус — только по явному разрешению:
# OKO_FOCUS_MOTOR_TESTS=1 (home/zero, шаги, скорости)
# OKO_FOCUS_AF_TESTS=1 (AF full + ROI + micro-fine af=fine; кривые в
# output/bench/af_curve_*.csv; для full/ROI нужна контрастная сцена, иначе
# job честно вернёт "peak not found"; af=fine всегда завершается done)
OKO_FOCUS_MOTOR_TESTS=1 OKO_FOCUS_AF_TESTS=1 bench/run.sh api_check.py

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

# RTSP/OSD: если OPTIONS молчит, включите stream.enable=true и перезагрузите DUT
bench/run.sh osd_check.py --path /live/0 --seconds 5 --frames 2
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
make -C project/app/divinus            # в Docker-цикле SDK
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

## Переменные окружения

| Переменная | По умолчанию | Смысл |
|---|---|---|
| `OKO_BENCH_OUT` | `output/bench` | каталог логов, кадров и кропов |
| `OKO_BENCH_VENV` | `$OKO_BENCH_OUT/venv` | путь к virtualenv |
| `OKO_SERIAL_PORT` | последний `/dev/ttyACM*` | UART-порт |
| `OKO_DUT_HOST` | `192.168.52.150` | адрес DUT по умолчанию (SSH/RTSP; текущий DHCP-адрес — `192.168.52.97`, см. TESTBENCH.md §5) |
| `OKO_DUT_PASSWORD` | `luckfox` | пароль root (публичный вендорский дефолт) |
| `OKO_RTSP_USER` / `OKO_RTSP_PASS` | `admin` / `oko-change-me` | учётка divinus (фабричный дефолт) |
| `OKO_VPN_PEER` | — | адрес VPN-пира на хосте для `vpn_check.py` (`--peer-host` важнее) |

Секреты в репозитории не хранятся: только публичные вендорские дефолты и
переопределение переменными окружения.

## Артефакты

- Кадры и кропы OSD: `output/bench/osd-<YYYYmmdd-HHMMSS>-fNN[-<reg>].png`.
- Скриншоты WebUI: `output/bench/ui/<раздел>.png` и `mobile-system.png`.
- VPN: `output/bench/vpn/report.json`, `ssh.log`, `profile.conf` (тестовый
  профиль; содержит ключи — не коммитить).
- Каталог создаётся автоматически; `output/` в `.gitignore`.

## Типичные ошибки

- `ModuleNotFoundError: serial/av/paramiko` — не создан venv: выполните
  `bench/setup.sh` (нужны `python3-venv` и доступ к PyPI).
- `Permission denied: '/dev/ttyACM0'` — добавьте пользователя в группу `dialout`
  и перелогиньтесь: `sudo usermod -aG dialout "$USER"`.
- `device or resource busy` — порт держит другой процесс (`fuser /dev/ttyACM0`);
  `serial_exec.py` запускайте после завершения других UART-сессий.
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