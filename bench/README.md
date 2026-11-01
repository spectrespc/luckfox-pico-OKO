# Утилиты тестового стенда

Скрипты для работы с DUT (`Luckfox Pico Pro Max`, RV1106) по UART, SSH и RTSP.
Каталог не участвует в сборке прошивки; зависимости ставятся в отдельный venv
внутри `output/` (gitignore).

## Быстрый старт

```sh
bench/setup.sh                          # создать output/bench/venv и поставить зависимости
bench/run.sh serial_exec.py 'cat /proc/uptime'
bench/run.sh netboot.py catch           # затем сбросить питание DUT
```

`bench/run.sh` сам вызывает `setup.sh`, если venv ещё нет.

## Скрипты

| Скрипт | Назначение |
|---|---|
| `serial_exec.py` | вход в консоль по UART (`/dev/ttyACM*`, 115200 8N1) и выполнение команд на DUT |
| `dut.py` | выполнение команд по SSH через `paramiko` |
| `netboot.py` | управление U-Boot: `catch` (спам Ctrl-C до `=>`, затем netboot-команды), `reboot` (перезагрузка DUT с перехватом U-Boot), `cmd '<команда>'` (одна команда в U-Boot) |
| `osd_check.py` | захват кадров RTSP, попарный диф боксов OSD-регионов, сохранение полных кадров и кропов |

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

# OSD: 17 с наблюдения, reg0 меняется за 1 с, reg1 — за 16 с
bench/run.sh osd_check.py --ip 192.168.52.150 --seconds 17

# свои регионы (имя:x0,y0,x1,y1;...)
bench/run.sh osd_check.py --regions 'top:0,16,1200,64;msg:0,112,1200,160'
```

## Переменные окружения

| Переменная | По умолчанию | Смысл |
|---|---|---|
| `OKO_BENCH_OUT` | `output/bench` | каталог логов, кадров и кропов |
| `OKO_BENCH_VENV` | `$OKO_BENCH_OUT/venv` | путь к virtualenv |
| `OKO_SERIAL_PORT` | последний `/dev/ttyACM*` | UART-порт |
| `OKO_DUT_HOST` | `192.168.52.150` | адрес DUT (SSH/RTSP, `ipaddr` в netboot) |
| `OKO_DUT_PASSWORD` | `luckfox` | пароль root (публичный вендорский дефолт) |
| `OKO_RTSP_USER` / `OKO_RTSP_PASS` | `admin` / `oko-change-me` | учётка divinus (фабричный дефолт) |
| `OKO_TFTP_HOST` | `192.168.52.200` | `serverip` для TFTP/NFS |
| `OKO_GATEWAY` | `192.168.52.254` | `gatewayip` |
| `OKO_NFSROOT` | `192.168.52.200:/srv/nfs/rootfs` | NFS-корень для `sys_bootargs` |
| `OKO_NETBOOT_LOG` | `output/bench/netboot-<ts>.log` | лог legacy-скрипта `netboot-catch.py` в корне |

Секреты в репозитории не хранятся: только публичные вендорские дефолты и
переопределение переменными окружения.

## Артефакты

- Логи U-Boot/netboot: `output/bench/netboot-<YYYYmmdd-HHMMSS>.log`.
- Кадры и кропы OSD: `output/bench/osd-<YYYYmmdd-HHMMSS>-fNN[-<reg>].png`.
- Каталог создаётся автоматически; `output/` в `.gitignore`.

## Типичные ошибки

- `ModuleNotFoundError: serial/av/paramiko` — не создан venv: выполните
  `bench/setup.sh` (нужны `python3-venv` и доступ к PyPI).
- `Permission denied: '/dev/ttyACM0'` — добавьте пользователя в группу `dialout`
  и перелогиньтесь: `sudo usermod -aG dialout "$USER"`.
- `device or resource busy` — порт держит другой процесс (`fuser /dev/ttyACM0`);
  `netboot.py` и `serial_exec.py` запускайте последовательно.
- `U-Boot prompt not found` — устройство уже загрузило Linux (используйте
  `netboot.py reboot`) либо автозагрузка не прерывается (проверьте `bootdelay`).
- PyAV не ставится из-за отсутствия колеса под версию Python — поставьте
  системный пакет (`sudo apt install python3-av`) и запускайте `osd_check.py`
  системным Python; остальные скрипты от этого не зависят.
- RTSP пуст или нет OSD — проверьте `divinus`/`oko-osd` в `ps`, конфиг
  `/userdata/divinus.yaml` и что кадры снимаются с RTSP, а не с `/image.jpg`
  (JPEG-снимок кодируется отдельным VENC-каналом без RGN).

## Регионы OSD

Боксы по умолчанию — `0,16,1200,64`, `0,64,1200,112`, `0,112,1200,160`
(`reg0..reg2`); они соответствуют `regN_posy` в
`project/app/divinus/files/divinus.yaml`. При смене раскладки OSD правьте
значения в обоих местах.

## Legacy

`netboot-catch.py` в корне репозитория оставлен для совместимости и пишет лог в
`output/bench/` (переопределение — `OKO_NETBOOT_LOG`). Для новых сценариев
используйте `bench/run.sh netboot.py catch`.