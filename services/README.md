# Сервисы стенда OKO

Один контейнер для сетевых сервисов стенда:

- **TFTP** — отдаёт `output/image` (там лежат `boot.img` и `update.img`);
- **NFS** — экспортирует `output/srv/nfs/rootfs` для netboot-цикла;
- **опционально** — WireGuard, AmneziaWG, OpenVPN по готовому паттерну.

Отдельно, в `services/stream/`, живёт приёмник UDP/RTP (MediaMTX) для проверки
раздачи камеры — он не входит в этот контейнер и поднимается своим профилем
compose (см. раздел «Приёмник UDP/RTP»).

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