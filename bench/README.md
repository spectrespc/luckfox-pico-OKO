# Утилиты тестового стенда

Скрипты для работы с DUT (`Luckfox Pico Pro Max`, RV1106) по UART, SSH и RTSP.
Каталог не участвует в сборке прошивки; зависимости ставятся в отдельный venv
внутри `output/` (gitignore).

## Быстрый старт

```sh
bench/setup.sh                          # создать output/bench/venv и поставить зависимости
bench/run.sh serial_exec.py 'cat /proc/uptime'
bench/run.sh dut.py 'mount | grep -w " / "'
bench/run.sh netboot.py catch           # затем сбросить питание DUT
```

`bench/run.sh` сам вызывает `setup.sh`, если venv ещё нет.

## Скрипты

| Скрипт | Назначение |
|---|---|
| `serial_exec.py` | вход в консоль по UART (`/dev/ttyACM*`, 115200 8N1) и выполнение команд на DUT |
| `dut.py` | выполнение команд по SSH через `paramiko` |
| `netboot.py` | управление U-Boot: `catch` (спам Ctrl-C до `=>`, затем netboot-команды), `reboot` (перезагрузка DUT с перехватом U-Boot), `cmd '<команда>'` (одна команда в U-Boot); ядро берётся по TFTP из `output/image/boot.img`, rootfs — по NFS |
| `nfs_env.py` | переключение штатной загрузки через env U-Boot: `status` (текущий `sys_bootargs` и режим `nand`/`nfs`), `on` (NFS-root, проверка `/`, `/oem`, `/userdata`), `off` (возврат на `ubi0:rootfs`); `--no-reboot` пишет переменную без перезагрузки |
| `osd_check.py` | захват кадров RTSP (`--path`, по умолчанию `/live/0`), попарный диф боксов OSD-регионов, сохранение полных кадров и кропов |
| `geometry_check.py` | геометрия оверлеев и ePTZ: `--what zoom` — кроп на main/sub, `/mjpeg`/`/image.jpg`, критерий — нормированная корреляция с ожидаемым центром (mean\|diff\| печатается для справки); рестарт MP4 — отдельный opt-in `OKO_GEOMETRY_MP4_TESTS=1` (после смены кропа он ненадёжен); `--what boxes`/`--what osd` (opt-in `OKO_GEOMETRY_TESTS=1`) — совмещение NN-рамок и OSD-текста на 0/90/180/270; артефакты — `output/bench/geometry` |
| `api_check.py` | матрица HTTP API: GET всех эндпоинтов, POST no-op, экспорт конфига и журнала, негативные проверки (статика, имена файлов, NTP); opt-in `OKO_FOCUS_MOTOR_TESTS=1`/`OKO_FOCUS_AF_TESTS=1` — движение моторов и автофокус (`af=1` full/ROI и micro-fine `af=fine`) с записью AF-кривых |
| `ui_check.py` | WebUI в headless Chromium: восемь разделов, EN/RU, Apply/dirty-state, автозапуск предпросмотра, двухшаговый выбор ROI и строка автофокуса в «Видео», несохранённый выбор «Включить» переживает опрос, кнопка «Автофокус» (`af=fine`), сводка SD-карты в «Система → Статус» (сверка с `window.lastStatus`, работает и без карты), раскладка под окно на 1440×900 и мобильная 390×844 (нижний навбар 4×2), скриншоты (нужен Playwright) |
| `deploy_divinus.py` | копирование свежесобранного `divinus` в `/oem/usr/bin` по SSH и graceful restart (быстрая итерация, не замена прошивке) |
| `focus_check.py` | `/api/focus`: статус AF-метрики, ROI, мотор; `--enable`, `--roi x,y,w,h`, `--move`, `--seconds/--interval/--snapshot/--strict`, CSV в `--out` (по умолчанию `output/bench/focus`); код 2 — API жив, но снимок не снят, код 1 — метрика нулевая |
| `firmware_check.py` | `/api/firmware` и `/api/flash`: версии STM32/STM8, состояние задачи; `--flash --confirm FLASH` запускает прошивку, `--negative` прогоняет проверки валидации (400) |
| `vpn_check.py` | WireGuard/AmneziaWG/OpenVPN на DUT: модуль или `/dev/net/tun`, импорт профиля через API, `up`, handshake или адрес `tun0`, маршруты, ping, `down`, негатив (хуки/plugin, delete без токена, credentials required), опционально `--iperf-peer`, reboot + автозапуск; артефакты — `output/bench/vpn` |

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
bench/run.sh osd_check.py --ip 192.168.52.97 --seconds 17

# свои регионы (имя:x0,y0,x1,y1;...)
bench/run.sh osd_check.py --regions 'telemetry:0,16,760,160;gps:1500,16,1920,64'

# геометрия: кроп ePTZ и его сохранение после рестарта канала
bench/run.sh geometry_check.py --what zoom
# NN-рамки и OSD под 0/90/180/270 — только по явному разрешению:
# нужны модель, объект в кадре и неподвижная камера; код 2 — нет детекций
OKO_GEOMETRY_TESTS=1 bench/run.sh geometry_check.py --what boxes
OKO_GEOMETRY_TESTS=1 bench/run.sh geometry_check.py --what osd

# API: 97 проверок, код возврата 1 при ошибке
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
`oem`/`userdata` — из `<nfsroot>/.nfsroot/` (S20linkmount). На хосте нужны
`tftpd-hpa` с корнем `output/image` и `nfs-kernel-server`; настройка,
деплой rootfs и возврат на NAND — TESTBENCH.md §7D.

```sh
# деплой свежей rootfs в NFS-экспорт (бэкап — tar в output/bench/);
# --exclude=/.nfsroot сохраняет oem/userdata-подкаталоги экспорта
sudo rsync -a --delete --exclude=/.nfsroot output/out/rootfs_uclibc_rv1106/ /srv/nfs/rootfs/
sudo rsync -a --delete output/out/oem/ /srv/nfs/rootfs/.nfsroot/oem/
sudo rsync -a --delete output/out/userdata/ /srv/nfs/rootfs/.nfsroot/userdata/

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
# режим и адрес DUT до переключения (при DHCP это 192.168.52.97)
bench/run.sh nfs_env.py status --host 192.168.52.97

# NFS-root: reboot, ожидание SSH на 192.168.52.150, проверка /,/oem,/userdata
bench/run.sh nfs_env.py on --host 192.168.52.97

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
| `OKO_SERIAL_PORT` | последний `/dev/ttyACM*` | UART-порт |
| `OKO_DUT_HOST` | `192.168.52.150` | адрес DUT по умолчанию (SSH/RTSP; текущий DHCP-адрес — `192.168.52.97`, см. TESTBENCH.md §5) |
| `OKO_DUT_PASSWORD` | `luckfox` | пароль root (публичный вендорский дефолт) |
| `OKO_RTSP_USER` / `OKO_RTSP_PASS` | `admin` / `oko-change-me` | учётка divinus (фабричный дефолт) |
| `OKO_TFTP_HOST` | `192.168.52.200` | `serverip` для TFTP/NFS (netboot) |
| `OKO_NFS_IP` | `192.168.52.150` | статический адрес DUT в NFS-режиме (`nfs_env.py`, `ip=`) |
| `OKO_CMA_SIZE` | `66M` | `rk_dma_heap_cma` в строках `nfs_env.py`/`netboot.py` (как `RK_BOOTARGS_CMA_SIZE`) |
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
- Каталог создаётся автоматически; `output/` в `.gitignore`.

## Порядок работы и CMA

- После двух и более тяжёлых медиа-прогонов подряд (`ui_check.py`,
  `geometry_check.py`, ручная отладка ePTZ/поворота) перезагружайте DUT:
  `bench/run.sh dut.py --host 192.168.52.97 'reboot'` и подождите 50–60 с.
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