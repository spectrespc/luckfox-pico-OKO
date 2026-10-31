# Тестовый стенд OKO

Паспорт и рабочие параметры лабораторного стенда для отладки прошивки Luckfox Pico
Pro Max (RV1106) в режиме netboot. Значения сверены с `netboot-catch.py`,
`start.sh`, `docker-compose.yml` и активным BoardConfig на дату обновления.

## 1. Паспорт стенда

| Поле | Значение |
|---|---|
| Дата обновления | `2026-10-31` |
| Ответственный | `<ФИО>` |
| Статус | активен |
| Назначение | сборка прошивки и загрузка по сети (TFTP + NFS) для отладки IPC |
| Расположение | `<лаборатория / рабочее место>` |

## 2. Подопытное устройство (DUT)

| Параметр | Значение |
|---|---|
| Плата | Luckfox Pico Pro Max |
| SoC | Rockchip RV1106 (Cortex-A7 + NPU + ISP) |
| Носитель загрузки | SPI NAND (`RK_BOOT_MEDIUM=spi_nand`) |
| Корневая ФС | Buildroot, busybox init |
| Сенсор | SC230AI (MIPI CSI, драйвер `sc230ai`, активен) |
| Двигатель | шаговый 28BYJ-48 через GPIO-драйвер `motor_24byj48` — **выключен** (`RK_ENABLE_MOTOR` не задан) |
| Питание | `<источник, напряжение, ток>` |
| Подключение к ПК | USB (OTG) + UART; Maskrom — `<кнопка / перемычка>` |
| Серийный номер / метка | `<S/N>` |

## 3. Хост

| Параметр | Значение |
|---|---|
| ОС | Ubuntu 22.04 |
| Сборка | Docker, образ `luckfox-builder:22.04` |
| Контейнер | `luckfox_oko_build_env` |
| Сеть контейнера | `network_mode: host` (общий сетевой стек с хостом) |
| Рабочая папка | `/workspace` (корень репозитория смонтирован туда) |
| TFTP-сервер | `tftpd-hpa`, порт `69/udp`, корень `./output/image` (`/workspace/output/image`) |
| NFS-расшарка | `/srv/nfs/rootfs` (настраивается на хосте, в репозиторий не входит); внутри — `.nfsroot/{oem,userdata}` |
| Запуск окружения | `./start.sh` |

## 4. Подключения

| Интерфейс | Значение | Примечание |
|---|---|---|
| UART | `/dev/ttyACM0` | `netboot-catch.py` берёт последний найденный `/dev/ttyACM*` |
| Скорость UART | `115200 8N1` | задана в скрипте |
| Лог serial-сессии | `/tmp/kilo/netboot-run4.log` | каталог `/tmp/kilo` должен существовать |
| Ethernet | лабораторная сеть `192.168.52.0/24` | через порт устройства |
| USB | `<порт / хаб>` | используется для Maskrom-прошивки |

Требования к хосту для `netboot-catch.py`: Python 3 и модуль `pyserial`.

## 5. Сеть

| Узел | Адрес |
|---|---|
| Хост (TFTP + NFS) | `192.168.52.200` |
| Устройство (DUT) | `192.168.52.150` |
| Маска | `255.255.255.0` |
| Шлюз | `192.168.52.254` |

Переменные U-Boot, которые выставляет `netboot-catch.py`:

```
ipaddr     = 192.168.52.150
netmask    = 255.255.255.0
serverip   = 192.168.52.200
gatewayip  = 192.168.52.254
sys_bootargs = root=/dev/nfs rw \
               nfsroot=192.168.52.200:/srv/nfs/rootfs,vers=3,proto=tcp,nolock \
               ip=192.168.52.150:192.168.52.200:192.168.52.254:255.255.255.0:luckfox:eth0:off \
               rk_dma_heap_cma=66M
```

Загрузка по TFTP:

```
tftp ${ramdisk_addr_r} boot.img
boot_fit ${ramdisk_addr_r}
```

- TFTP-корень отдаёт `output/image`, поэтому `boot.img` должен быть результатом
  свежей сборки (`./build.sh` внутри контейнера).
- NFS-сервер должен экспортировать `/srv/nfs/rootfs` с поддержкой NFSv3 и TCP.
- Разделы `oem` и `userdata` при netboot монтируются по NFS из
  `<host>:<nfsroot>/.nfsroot/{oem,userdata}`; при ошибке NFS — откат на ubifs из SPI NAND.
- Экспорт хоста: `/srv/nfs/rootfs 192.168.52.0/24(rw,sync,no_subtree_check,no_root_squash,insecure)`.
  `no_root_squash` позволяет устройству писать в `/userdata` без правки прав.

## 6. Учётные данные

> **Внимание.** В файл запрещено записывать личные, рабочие или production-пароли.
> Допустимы только публичные вендорские значения по умолчанию и плейсхолдеры
> вида `<...>`. Для реальных секретов используйте менеджер паролей или переменные
> окружения вне репозитория.

| Система | Логин | Пароль |
|---|---|---|
| Устройство (SSH / Samba / console) | `root` | `luckfox` — публичный вендорский дефолт (`BR2_TARGET_GENERIC_ROOT_PASSWD` в `luckfox_pico_defconfig`) |
| WiFi (`LF_WIFI_SSID`) | — | `<SSID стенда>` |
| WiFi (`LF_WIFI_PSK`) | — | `<пароль WiFi — заполнять только при необходимости>` |
| Хост / Docker | `builder` | без пароля внутри контейнера (`NOPASSWD` sudo) |

Перед выдачей стенда наружу вендорский пароль `luckfox` необходимо сменить.

## 7. Прошивка и загрузка

### A. SPI NAND (автономная загрузка)

1. В контейнере: `./build.sh firmware`.
2. Результат: `output/image/update.img`.
3. Перевести устройство в Maskrom — `<кнопка / перемычка>`.
4. Прошить: `./rkflash.sh` или `tools/linux/Linux_Upgrade_Tool/upgrade_tool`.
5. Перезагрузить; загрузка пойдёт из SPI NAND без хоста.

### B. Netboot (текущий режим отладки)

1. `./start.sh` — собрать/запустить контейнер, поднять TFTP на `69/udp`.
2. Убедиться, что NFS экспортирует `/srv/nfs/rootfs` и в `output/image` есть
   актуальный `boot.img`.
3. Выложить свежую сборку на хост (корень, `oem`, `userdata`):
   ```sh
   tar -C /srv/nfs -czf /srv/nfs/rootfs-backup-$(date +%Y%m%d-%H%M).tar.gz rootfs
   rsync -a --delete --exclude='.nfsroot' output/out/rootfs_uclibc_rv1106/ /srv/nfs/rootfs/
   mkdir -p /srv/nfs/rootfs/.nfsroot
   rsync -a --delete output/out/oem/      /srv/nfs/rootfs/.nfsroot/oem/
   rsync -a --delete output/out/userdata/ /srv/nfs/rootfs/.nfsroot/userdata/
   mkdir -p /srv/nfs/rootfs/.nfsroot/userdata/data
   chmod -R 777 /srv/nfs/rootfs/.nfsroot/userdata
   chmod -R a+rX /srv/nfs/rootfs/.nfsroot/oem
   ```
   `/.nfsroot/` находится внутри того же экспорта, поэтому правка `/etc/exports`
   не нужна. `--exclude='.nfsroot'` не даёт удалить зеркала разделов при синхронизации.
4. На хосте:
   ```sh
   mkdir -p /tmp/kilo
   python3 netboot-catch.py
   ```
5. Сбросить питание устройства. Скрипт пробивает U-Boot, отправляет команды из
   `CMDS`, грузит `boot.img` по TFTP и выполняет `boot_fit`.
6. Смотреть лог `/tmp/kilo/netboot-run4.log`. Лимит сессии — 900 секунд.

Ручной вариант в консоли U-Boot — те же команды, что в `CMDS` файла
`netboot-catch.py` (`setenv …`, `ping 192.168.52.200`, `tftp`, `boot_fit`).

## 8. Диагностика

Чек-лист после загрузки (по SSH `root@192.168.52.150` или UART):

```sh
# маркер тестовой сборки (S99oko-test)
cat /tmp/oko-test-ok

# модули медиастека и сенсора
lsmod | grep -E 'sc230ai|rockit|rga3|rknpu|video_rkisp'

# сенсор определился, формат кадра читается
cat /proc/rkisp-vir0

# приложение запущено
ps | grep rkipc

# сеть
ifconfig eth0
ip route

# разделы смонтированы: type nfs, источник 192.168.52.200:/srv/nfs/rootfs/.nfsroot/*
# в консоли: netboot: oem from nfs … / netboot: userdata from nfs …
mount | grep -E ' /oem | /userdata '

# логи
ls -l /tmp/*.log /data/core-* 2>/dev/null
dmesg | tail -50
```

Типовые проблемы:

| Симптом | Вероятная причина |
|---|---|
| Нет загрузки по TFTP | не запущен контейнер/TFTP, неверный `serverip`, `boot.img` отсутствует в `output/image` |
| Ядро загрузилось, нет корня | не экспортирован `/srv/nfs/rootfs`, firewall, несовпадение `nfsroot` |
| `rkipc` не стартует | нет `rkipc.ini` для сенсора или не смонтирован `/oem` (см. `INIT_ORDER.md`) |
| `netboot: nfs mount … failed, fallback` | `.nfsroot` не создан/не синхронизирован, нет доступа к TCP 2049, неверный `nfsroot` в cmdline |
| Нет `/dev/video*` | `insmod_ko.sh` не выполнился или `sc230ai.ko` отсутствует |
| `motor.ko` не грузится | ожидаемо: драйвер выключен в конфигурации |

## 9. Известные особенности

- `IMAGE/` содержит релиз для **RV1103 Pico Mini**; он не соответствует активной
  конфигурации `RV1106 Pro Max` и является устаревшим артефактом.
- Драйвер мотора `motor.ko` в прошивке отсутствует: `RK_ENABLE_MOTOR` не задан,
  строка `__insmod motor.ko` в `insmod_ko.sh` пропускается проверкой `-f`.
- `insmod_ko.sh` вызывается из `RkLunch.sh`, который запускает `S21appinit`;
  каталог `/oem/usr/etc/init.d/` пуст, вся логика приложения зашита в `post_chk`.
  Подробнее — `INIT_ORDER.md`.
- `post_chk` выбирает для `sc230ai` шаблон `/oem/usr/share/rkipc-300w.ini`
  (сенсор 3 МП). Если геометрия кадра окажется некорректной, ini подбирается отдельно.
- При netboot `oem` и `userdata` берутся с хоста из `/srv/nfs/rootfs/.nfsroot/`, а не из
  SPI NAND. Изменения, сделанные на устройстве в `/userdata`, попадают в это NFS-дерево;
  NAND-разделы не затрагиваются. При недоступности NFS скрипт откатывается на NAND.
- В netboot сеть статическая: адрес задаётся `ip=` в `sys_bootargs`, а
  `network_init` в `RkLunch.sh` не запускает `udhcpc` и не меняет MAC. Иначе смена
  адреса рвёт TCP-соединения NFS-корня и система зависает на жёстком NFS-монтировании.
  Штатный `S41dhcpcd` при этом продолжает работать и может добавить вторичный
  DHCP-адрес (`ip route` показывает его как `src`); статический `.150` и NFS-монтирования
  сохраняются.
- Docker использует `network_mode: host`, поэтому порт `69/udp` на хосте должен
  быть свободен.

## 10. Правила ведения

1. Обновлять документ при любой смене железа, IP-адресов, путей, версии SDK или
   состава модулей.
2. При каждом обновлении менять «Дату обновления» в разделе 1.
3. Секреты не хранить: только плейсхолдеры `<...>` и публичные вендорские дефолты.
4. Расхождения между документом и скриптами трактовать в пользу скриптов
   (`netboot-catch.py`, `start.sh`, BoardConfig) и сразу править документ.