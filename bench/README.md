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
| `osd_check.py` | захват кадров RTSP, попарный диф боксов OSD-регионов, сохранение полных кадров и кропов |
| `api_check.py` | матрица HTTP API: GET всех эндпоинтов, POST no-op, экспорт конфига и журнала, негативные проверки (статика, имена файлов, NTP) |
| `ui_check.py` | WebUI в headless Chromium: восемь разделов, EN/RU, Apply/dirty-state, мобильная раскладка, скриншоты (нужен Playwright) |
| `deploy_divinus.py` | копирование свежесобранного `divinus` в `/oem/usr/bin` по SSH и graceful restart (быстрая итерация, не замена прошивке) |

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

# API: 74 проверки, код возврата 1 при ошибке
bench/run.sh api_check.py

# быстрая выкатка собранного divinus в /oem без пересборки образа
make -C project/app/divinus            # в Docker-цикле SDK
bench/run.sh deploy_divinus.py

# WebUI: отдельный venv с Playwright и Chromium
python3 -m venv /tmp/pw && /tmp/pw/bin/pip install playwright
/tmp/pw/bin/playwright install chromium
/tmp/pw/bin/python bench/ui_check.py
```

`ui_check.py` не входит в `requirements.txt`: Playwright тянет браузер (~120 МБ),
а `api_check.py` использует только стандартную библиотеку. Если Chromium не
стартует с `libasound.so.2: cannot open shared object file`, поставьте
`libasound2`/`libasound2t64` или распакуйте пакет и задайте `LD_LIBRARY_PATH`.

## Переменные окружения

| Переменная | По умолчанию | Смысл |
|---|---|---|
| `OKO_BENCH_OUT` | `output/bench` | каталог логов, кадров и кропов |
| `OKO_BENCH_VENV` | `$OKO_BENCH_OUT/venv` | путь к virtualenv |
| `OKO_SERIAL_PORT` | последний `/dev/ttyACM*` | UART-порт |
| `OKO_DUT_HOST` | `192.168.52.150` | адрес DUT по умолчанию (SSH/RTSP; текущий DHCP-адрес — `192.168.52.97`, см. TESTBENCH.md §5) |
| `OKO_DUT_PASSWORD` | `luckfox` | пароль root (публичный вендорский дефолт) |
| `OKO_RTSP_USER` / `OKO_RTSP_PASS` | `admin` / `oko-change-me` | учётка divinus (фабричный дефолт) |

Секреты в репозитории не хранятся: только публичные вендорские дефолты и
переопределение переменными окружения.

## Артефакты

- Кадры и кропы OSD: `output/bench/osd-<YYYYmmdd-HHMMSS>-fNN[-<reg>].png`.
- Скриншоты WebUI: `output/bench/ui/<раздел>.png` и `mobile-system.png`.
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