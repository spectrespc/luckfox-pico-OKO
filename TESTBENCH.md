# Тестовый стенд OKO

Паспорт и рабочие параметры лабораторного стенда для отладки прошивки Luckfox Pico
Pro Max (RV1106) с автономной загрузкой из SPI NAND. Значения сверены с `bench/*.py`,
`tools/native/*.sh` и активным BoardConfig на дату обновления.

## 1. Паспорт стенда

| Поле | Значение |
|---|---|
| Дата обновления | `2026-11-09` |
| Ответственный | `<ФИО>` |
| Статус | активен |
| Назначение | сборка прошивки и прошивка SPI NAND для отладки IPC |
| Расположение | `<лаборатория / рабочее место>` |

## 2. Подопытное устройство (DUT)

| Параметр | Значение |
|---|---|
| Плата | Luckfox Pico Pro Max |
| SoC | Rockchip RV1106 (Cortex-A7 + NPU + ISP) |
| Носитель загрузки | SPI NAND (`RK_BOOT_MEDIUM=spi_nand`) |
| Корневая ФС | Buildroot, busybox init |
| Сенсор | SC230AI (MIPI CSI, драйвер `sc230ai`, 1080p30 / 480p120; `HDR_X2` экспериментально; `divinus` кодирует 30 fps) |
| Двигатель | `STM8S003F3P6` на I2C (адрес `0x60`, `/dev/i2c-3`, 100 кГц) — привод фокуса/зума |
| Питание | `<источник, напряжение, ток>` |
| Подключение к ПК | USB (OTG) + UART; Maskrom — `<кнопка / перемычка>` |
| Серийный номер / метка | `<S/N>` |

## 3. Хост

| Параметр | Значение |
|---|---|
| ОС | Debian 13 trixie |
| Компилятор хоста | gcc/g++ 14.2 (`host-tools` U-Boot/ядра собираются им) |
| Целевой тулчейн | `arm-rockchip830-linux-uclibcgnueabihf` (gcc 8.3.0, uClibc-ng, `tools/linux/toolchain/`) |
| Сборка | нативная, без контейнера: `tools/native/setup_host.sh`, `source tools/native/env.sh`, `./build.sh` |
| Рабочая папка | корень репозитория на хосте |
| Docker | `docker.io` + плагин `docker compose`, пользователь в группе `docker`; контейнер `services/` (`network_mode: host`, `privileged`) |
| Сетевые сервисы | контейнер `services/`: `tftpd-hpa` (корень `output/image`, chroot) и `nfs-kernel-server` (экспорт `output/srv/nfs/rootfs` → `/srv/nfs/rootfs`) |
| Запуск окружения | `source tools/native/env.sh` (проверка — `tools/native/smoke.sh`) |

## 4. Подключения

| Интерфейс | Значение | Примечание |
|---|---|---|
| UART | последний `/dev/ttyACM*` | `bench/serial_exec.py` берёт последний найденный; сейчас это `/dev/ttyACM1` (CH340), `/dev/ttyACM0` — CMSIS-DAP |
| Скорость UART | `115200 8N1` | задана в скриптах `bench/` |
| Ethernet | лабораторная сеть `192.168.52.0/24` | через порт устройства |
| USB | `<порт / хаб>` | используется для Maskrom-прошивки |
| I2C3-M1 | `/dev/i2c-3`, 100 кГц, `GPIO1_D2`/`GPIO1_D3` | SHT21 `0x40`, INA3221 `0x41` (`A0` = VS; die ID `0x3220`; силовые входы пока не подключены), STM32 ACPI-EC `0x42`; нужны внешние pull-up 2.2–4.7 кОм к 3.3 В (в DTS `pcfg_pull_none_smt`) |

Требования к хосту: Python 3 и зависимости из `bench/requirements.txt`;
`bench/setup.sh` создаёт venv в `output/bench/venv`, запуск — `bench/run.sh`.

## 5. Сеть

| Узел | Адрес |
|---|---|
| Устройство (DUT), NAND | `192.168.52.97` (DHCP, факт на `2026-11-04`) |
| Устройство (DUT), NFS | `192.168.52.150` (статический из `ip=` в `sys_bootargs`) |
| Хост TFTP/NFS | `192.168.52.200` |
| Маска | `255.255.255.0` |
| Шлюз | `192.168.52.254` |

В автономном режиме адрес `eth0` выдаётся по DHCP (`RkLunch.sh` → `oko-net apply`).
В NFS-режиме адрес задаётся в cmdline (`ip=192.168.52.150::…:off`), а `RkLunch.sh`
пропускает DHCP. Фактический адрес смотрите на DUT (`ifconfig eth0`) или в
serial-консоли. Для стабильных проверок зарезервируйте адрес `192.168.52.150`
в DHCP-сервере лабораторной сети.

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
| divinus Web/RTSP (фабричный) | `admin` | `oko-change-me` — публичный фабричный дефолт, сменить до подключения к недоверенной сети |
| Хост | `spectre` | sudo по паролю; в репозитории и скриптах не хранится |

Перед выдачей стенда наружу вендорский пароль `luckfox` необходимо сменить.

## 7. Прошивка SPI NAND

### A. SPI NAND (автономная загрузка)

1. На хосте: `source tools/native/env.sh && ./build.sh firmware`.
2. Результат: `output/image/update.img`.
3. Перевести устройство в Maskrom — `<кнопка / перемычка>`.
4. Прошить: `./rkflash.sh` или `tools/linux/Linux_Upgrade_Tool/upgrade_tool`.
5. Перезагрузить; загрузка пойдёт из SPI NAND без хоста.

> **С 2026-11-05 разметка изменена** (`boot` 6 МБ, `oem` 40 МБ, `userdata`
> 40 МБ, `rootfs` 168 МБ). Первая прошивка такой сборки — **только полный
> `update.img`**; покомпонентная выкатка со старой таблицей несовместима.
> Сохраните нужное из `/userdata` и с microSD: раздел `userdata` будет
> пересоздан. Проверка после загрузки: `cat /proc/cmdline` и `cat /proc/mtd`
> (см. §8); том `ubi5_0` должен быть около 29 МиБ (`df -h /userdata`).
>
> Если `reboot loader` на DUT не переводит плату в loader-режим (проверено
> 2026-11-05: обычная перезагрузка), остановите U-Boot по UART
> (`<Ctrl+C>` в окне `Hit key to stop autoboot`) и выполните `download`:
> команда отвечает `Enter rockusb failed, fallback to bootrom...`, после чего
> `upgrade_tool ld` видит устройство в режиме `Maskrom`. Дальше — обычный
> `./rkflash.sh update`; кнопка/перемычка Maskrom не нужна.

> **С 2026-11-08 OpenVPN добавляет `CONFIG_TUN=y` (boot) и `openvpn 2.5.7`
> (rootfs).** Изменяются `boot`, `rootfs` и `oem` (divinus), поэтому нужен
> полный `update.img`, а не покомпонентная выкатка. После загрузки проверьте
> `ls -l /dev/net/tun` и `openvpn --version`.

### B. Покомпонентная прошивка изменённых разделов

Режим для выкатки изменённых разделов в SPI NAND без полного образа: прошиваются
только разделы, содержимое которых изменилось, остальные (включая `env`
с MAC-адресом) не трогаются. Уменьшает износ NAND и исключает сброс U-Boot env.

> **Только после переразметки.** Текущая таблица (`boot` 6 МБ, `oem` 40 МБ,
> `userdata` 40 МБ, `rootfs` 168 МБ) сначала выкатывается полным
> `./rkflash.sh update` / `upgrade_tool uf update.img`. Покомпонентная запись
> ниже применима только к устройству, уже прошитому этой таблицей; LBA и
> размеры из старых заметок (`oem 0x2800`, `userdata 0x5000`,
> `rootfs 0x16800`/`0x63000`/`0x69000`) недействительны.

**Маппинг исходников на разделы** (`output/image/*.img` собирает
`./build.sh firmware`):

| Раздел | Что содержит | Источник в сборке |
|---|---|---|
| `env` | U-Boot env (MAC и пр.) | `output/image/env.img` (этой фичей не изменялся) |
| `idblock`, `uboot` | SPL/DDR, U-Boot | `output/image/idblock.img`, `uboot.img` |
| `boot` | ядро + DTB | `output/image/boot.img` |
| `oem` | `/oem/usr/bin/divinus`, `/oem/usr/bin/wg`, `/oem/usr/bin/awg`, `/oem/usr/ko/amneziawg.ko`, `/oem/usr/share/divinus/divinus.yaml`, `/oem/usr/share/rkipc*`, библиотеки | `output/out/oem/` |
| `userdata` | фабричное содержимое раздела; рабочие маркеры и конфиг живут на устройстве | `output/out/userdata/` |
| `rootfs` | корневая ФС, включая `/etc/init.d/S20oko-osd`, `/usr/bin/oko-osd`, `/usr/bin/oko-vpn` и `/etc/init.d/S45oko-vpn` | `output/out/rootfs_uclibc_rv1106/` |

**Порядок:**

1. Перевести устройство в Maskrom (`<кнопка / перемычка>`) либо выполнить на DUT
   `reboot loader` (Rockchip RESTART2); наличие устройства проверить
   `upgrade_tool ld`. На прошивке 2026-11-05 `reboot loader` завершился обычной
   перезагрузкой (`upgrade_tool ld` → `List of rockusb connected(0)`), поэтому
   без кнопки Maskrom покомпонентная запись недоступна.
2. Загрузить loader в RAM и проверить таблицу разделов:
   ```sh
   ./tools/linux/Linux_Upgrade_Tool/upgrade_tool db output/image/download.bin
   ./tools/linux/Linux_Upgrade_Tool/upgrade_tool pl   # LBA и размеры в секторах 512 B
   ```
3. Прошить только изменённые разделы. `./rkflash.sh oem` / `rootfs` использует
   `upgrade_tool di -oem/-rootfs`, но в чистом Maskrom (до `db`) таблицы разделов
   нет, и он отвечает `parameter is invalid,please check!`. Рабочий путь —
   запись по LBA с **полным стиранием раздела** (после `db` таблицу можно
   проверить через `upgrade_tool pl`):
```sh
    ./tools/linux/Linux_Upgrade_Tool/upgrade_tool wl 0x800  0x3000 output/image/boot.img
    ./tools/linux/Linux_Upgrade_Tool/upgrade_tool wl 0x3800 0x14000 output/image/oem.img
    ./tools/linux/Linux_Upgrade_Tool/upgrade_tool wl 0x2B800 0x54000 output/image/rootfs.img
    ./tools/linux/Linux_Upgrade_Tool/upgrade_tool rd
    ```
    Второй аргумент `wl` — размер **всего раздела** в секторах (boot 6M = `0x3000`,
    oem 40M = `0x14000`, rootfs 168M = `0x54000`), а не длина файла: tool стирает
    этот диапазон и пишет образ. Если передать длину образа (boot `0x391400/512`
    ≈ `0x1C8A`, oem `0x9D00`, rootfs `0x1A500`), в хвосте останутся старые
    UBI-PEB с другим `image_seq`, и ядро упадёт с
    `ubi0 error: ubi_attach: bad image sequence number …` (panic).
    Maskrom нужен один раз: после `db` устройство остаётся в loader-режиме,
    поэтому следующие разделы прошиваются без повторного входа.
    `userdata` (LBA `0x17800`, 40M = `0x14000`) — только если менялось фабричное
    содержимое раздела.
4. `idblock`, `uboot` и `env` этой фичей не менялись. `boot` перепрошивается, когда
   меняются ядро, DTB или его конфиг (например, `rv1106-netboot.config`). При
   необходимости прошивайте `idblock`/`uboot` полным `./rkflash.sh update` (§7A)
   или отдельной проверенной процедурой; `di -b`/`di -uboot` в чистом Maskrom так
   же требуют загруженного через `db` loader.
5. Полный `./rkflash.sh update` (`upgrade_tool uf update.img`) — **только** при
   первичной прошивке, смене таблицы разделов (`parameter.txt`), loader
   (`idblock`/`uboot`) или U-Boot env. Он затирает и неизменённые разделы.
6. Проверить после загрузки: чек-лист §8 (`/oem/usr/bin/divinus`,
   `/etc/init.d/S20oko-osd`, OSD в RTSP).

После перезаписи разделов при необходимости отключите и подключите USB-OTG
на 5–10 с: `rd` не всегда выводит плату из loader-состояния, а BootROM при
повторном подключении снова показывает устройство как Maskrom.

**Как определить, что изменилось:** сравнить хеши собранных образов с последним
выкатанным комплектом (вести на хосте файл `.flashed.sha256`):
```sh
sha256sum output/image/*.img > /tmp/now.sha256
diff /tmp/now.sha256 .flashed.sha256   # прошивать только различающиеся разделы
```
Альтернатива без Maskrom — сырые `mtd erase/write` из `output/image/sd_update.txt`
(смещения и размеры по разделам, выполняется из U-Boot). Файл и образы кладутся
в FAT-раздел карты; `sd_update.txt` должен оставаться **без** строки `reset`:
после записи U-Boot вернётся в консоль, и карту/файл нужно убрать до следующей
загрузки. Если оставить `sd_update.txt` (или добавить `reset`), U-Boot будет
перепрошивать NAND при каждом старте.

### C. STM8: первичная прошивка через ST-Link

STM8S103F3P6 на плате не имеет USB; первая запись OKO-загрузчика и приложения
делается ST-Link/V2 по SWIM (PD1), который подключён к USB хоста. Питание
платы при этом должно быть включено (общий GND), I2C на DUT можно не
останавливать: в худшем случае `firmware_status` временно покажет `absent`.
`stm8flash` не входит в дистрибутив и требует libusb; проверенный способ без
root — собрать и запустить его в привилегированном контейнере с пробросом USB:

```sh
git clone --depth 1 https://github.com/vdudouyt/stm8flash /tmp/kilo/stm8flash
docker run --rm --privileged \
  -v /dev/bus/usb:/dev/bus/usb \
  -v /tmp/kilo/stm8flash:/src -v "$PWD:/work" -w /src \
  debian:bookworm-slim bash -lc '
    apt-get update -qq &&
    apt-get install -y -qq build-essential libusb-1.0-0-dev pkg-config &&
    make clean && make RELEASE=1 &&
    ./stm8flash -c stlinkv2 -p stm8s103f3 -s flash \
      -w /work/project/app/stm8-motor/build/factory-2.1.1.ihx'
```

`factory-2.1.1.ihx` содержит загрузчик `0x8000–0x89FF` и приложение
`0x8A10–0x9FFF`; EEPROM (позиции моторов) не стирается. Проверка после
записи — `oko-motor -b 3 -a 0x60 version` (ожидается `2.1.1`), затем
`enter-boot` → `info` → `boot-reset`. Восстановление при неудачной записи —
тем же `factory-2.1.1.ihx`; приложение можно обновлять без ST-Link через
`/api/flash` (имя с версией) или `oko-motor flash app-2.1.1.ihx`.

### D. Переключение NAND ↔ NFS через env U-Boot

Штатный `boot.img` (SC230AI) уже содержит фрагмент `rv1106-netboot.config`
(`CONFIG_IP_PNP`, `IP_PNP_DHCP`, `IP_PNP_BOOTP`, `CONFIG_ROOT_NFS`), поэтому
режим корня переключается только переменной U-Boot `sys_bootargs` — без
перепрошивки ядра и без отдельного BoardConfig. В NFS-режиме `oem`/`userdata`
монтируются из `<nfsroot>/.nfsroot/`, DHCP в `RkLunch.sh` пропускается.

1. Поднять сервисы (Sudo не нужен; нужны Docker и членство в группе `docker`):
   ```sh
   tools/native/setup_netboot.sh
   ```
   Скрипт собирает и запускает контейнер `services/` с `network_mode: host`:
   TFTP отдаёт `output/image` (`/srv/tftp`, chroot, `--foreground --secure`),
   NFS-сервер экспортирует `output/srv/nfs/rootfs` для `192.168.52.0/24`.
   Внутри контейнера путь экспорта — `/srv/nfs/rootfs`, поэтому строки
   `nfsroot=` на DUT не меняются. Логи — `docker logs -f oko-services`;
   раскладка и оговорки — `services/README.md`. Если на хосте ещё работают
   пакетные `tftpd-hpa`/`nfs-kernel-server`, сначала освободите порты
   (раздел «Переход с хостовых сервисов» в `services/README.md`), иначе
   контейнер не сможет занять 69/111/2049/20048.
2. Разложить rootfs и разделы в экспорт (бэкап — tar в `output/bench/nfs/`):
   ```sh
   rsync -a --delete --exclude=/.nfsroot output/out/rootfs_uclibc_rv1106/ output/srv/nfs/rootfs/
   rsync -a --delete output/out/oem/ output/srv/nfs/rootfs/.nfsroot/oem/
   rsync -a --delete output/out/userdata/ output/srv/nfs/rootfs/.nfsroot/userdata/
   ```
   Если rsync упирается в root-only каталоги экспорта (`var/lib/seedrng`),
   выполните те же команды через `sudo rsync` от root.
3. Переключить корень и проверить после перезагрузки:
   ```sh
   bench/run.sh nfs_env.py status --host 192.168.52.97   # режим и адрес до переключения
   bench/run.sh nfs_env.py on  --host 192.168.52.97      # NFS-root
   bench/run.sh nfs_env.py off --host 192.168.52.150     # возврат на ubi0:rootfs
   ```
   `on` пишет `sys_bootargs` с `root=/dev/nfs … ip=192.168.52.150:…:off`,
   перезагружает плату и проверяет `/`, `/oem`, `/userdata` (nfs3 с
   `192.168.52.200`); `off` пишет строку
   `ubi.mtd=6 root=ubi0:rootfs rootfstype=ubifs rk_dma_heap_cma=66M` и проверяет
   `root=ubi0:rootfs` и `ubifs`. Логи — `output/bench/nfs-env-<дата>.log`.
   Строки и адреса — константы `bench/nfs_env.py` (`OKO_NFS_IP`,
   `OKO_NAND_HOST`, `OKO_TFTP_HOST`, `OKO_GATEWAY`, `OKO_NFSROOT`,
   `OKO_CMA_SIZE`); при смене разметки или CMA обновите их.
4. Итерации ядра без прошивки (`netboot.py`) по-прежнему возможны: скрипт кладёт
   `output/image/boot.img` по TFTP и задаёт `sys_bootargs` только в RAM U-Boot,
   не сохраняя его в env.

**Контейнер и NFS-root.** Останавливать контейнер `services/` при смонтированном
на DUT NFS-root нельзя: клиент теряет `/` и зависает. Если контейнер уже
остановлен или пересобирается, сначала верните DUT на NAND по процедуре ниже
(через UART), затем поднимайте сервисы. Порты 69/111/2049/20048 занимает
контейнер; хостовые `tftpd-hpa`, `nfs-kernel-server` и `rpcbind` должны быть
остановлены и удалены.

**Аварийное восстановление.** Неверная строка `sys_bootargs` (`nfsroot`
недоступен) приводит к `VFS: Unable to mount root fs via NFS` и panic. Если в
строке нет `panic=`, ядро зависает и нужен сброс питания. Проверенная процедура
(2026-11-08, воспроизведено с несуществующим экспортом и `panic=5`):

```sh
# 1. поймать U-Boot по UART (Ctrl-C), 2. вернуть NAND-строку и сохранить env:
bench/run.sh netboot.py cmd "setenv sys_bootargs 'ubi.mtd=6 root=ubi0:rootfs rootfstype=ubifs rk_dma_heap_cma=66M'"
bench/run.sh netboot.py cmd 'printenv sys_bootargs'
bench/run.sh netboot.py cmd 'saveenv'
bench/run.sh netboot.py cmd 'reset'
```

Команды `setenv`/`saveenv`/`reset` отправляйте по одной: длинная строка в одной
команде с `;` обрезается буфером U-Boot. Если плата зависла (panic без
`panic=N`), сначала снимите и подайте питание, затем ловите U-Boot.

Логи — `output/bench/netboot-<дата>.log`. Скорость NFS в этом режиме ограничена
`rsize/wsize=4096` для `/` (параметры `nfsroot`), `oem`/`userdata` монтируются
с `rsize/wsize=1048576`; для замеров производительности используйте SPI NAND.

### E. Обновление через WebUI (OTA)

Пакет `.okp` собирается на хосте из образов последней сборки и обновляет
`boot`, `oem`, `rootfs` через microSD; SPI NAND из Linux не пишется.

1. Хост: `tools/native/make_oko_update.sh --version 0.3.0`. Версия должна
   совпадать с `/etc/oko-release` собираемого образа (сейчас `0.3.0`),
   иначе пост-проверка после прошивки сообщит `version-mismatch`.
2. WebUI → «Система» → «Обновление ПО»: выбрать файл или указать URL
   (`http://` или `https://`), «Загрузить пакет» / «Скачать и подготовить».
   Прогресс — в карточке; `GET /api/update` отдаёт состояние и хеши.
3. Убедиться, что пакет проверен (`staged.ok=true`). До подтверждения пакет
   на карте безопасен: каждая строка `sd_update.txt` обёрнута в
   `if env exists oko_update; then … fi`, и флаг выставляет только `apply`.
   Ввести `APPLY` и нажать
   «Прошить и перезагрузить». Устройство уйдёт в перезагрузку, U-Boot
   прошьёт разделы (около 2–3 минут), затем загрузится Linux.
4. После возврата `GET /api/update` должен показать новую версию в
   `current_version` и `last.ok=true`; карта очищена, повторной прошивки при
   следующей загрузке нет. Проверить `df -h /userdata` — раздел не менялся.

Отрицательные проверки: apply без токена или без `confirm=APPLY` — 400/409;
повторный POST во время работы — 409; повреждённый пакет — `staged.ok=false`,
apply отклоняется; карта без пакета или без `sd_update.txt` — прошивки нет;
обрыв питания во время прошивки — SD остаётся в слоте, U-Boot повторяет
запись при следующей подаче питания.

> **Гейтовые эксперименты (выполнить на подключённом DUT до выкатки):**
> видимость `fw_setenv oko_update` в U-Boot, номер `mmc 1` для слота карты,
> поведение `ubimkvol`/перемонтирования `/oem` после полного `mtd erase`,
> работоспособность HTTPS (`curl` собран с OpenSSL). До их закрытия
> механизм считается непроверенным (см. `FEATURES_UNVERIFIED.md`).

## 8. Диагностика

Чек-лист после загрузки (по SSH `root@<DHCP-адрес DUT>` или UART; фактический
адрес — в §5):

```sh
# маркер тестовой сборки (S99oko-test)
cat /tmp/oko-test-ok

# модули медиастека и сенсора
lsmod | grep -E 'sc230ai|rockit|rga3|rknpu|video_rkisp'

# замена драйвера NPU без перепрошивки (этап A: 0.9.2 -> 0.9.8)
# baseline сохранён в /userdata/rknpu-baseline-0.9.2.ko
killall divinus
rmmod rknpu
insmod /userdata/rknpu-0.9.8.ko
dmesg | grep -i rknpu | tail -3      # ожидается v0.9.8 и non-iommu mode
cd /userdata && LD_LIBRARY_PATH=/oem/usr/lib setsid /oem/usr/bin/divinus \
  >/tmp/divinus.log 2>&1 < /dev/null &
curl -s -u admin:oko-change-me http://127.0.0.1/api/status
# killall/stop = не graceful: divinus вернёт divinus.yaml.bak (откат настроек).
# Мягкий рестарт без отката: kill -HUP $(pidof divinus)
# откат модуля: killall divinus; rmmod rknpu; insmod /userdata/rknpu-baseline-0.9.2.ko
# штатная загрузка берёт /oem/usr/ko/rknpu.ko; кандидат ставится туда же
# только после soak (cp /userdata/rknpu-0.9.8.ko /oem/usr/ko/rknpu.ko)

# сенсор определился, формат кадра читается
cat /proc/rkisp-vir0

# режим сенсора: 1920x1080@10000/300000 (30 fps)
media-ctl -p | grep -A6 'sc230ai 4-0032' | grep fmt

# проверка потока (divinus должен быть остановлен): ожидается 30.0 fps
v4l2-ctl -d /dev/video0 --stream-mmap --stream-count=60 --stream-poll

# WebUI и API после прошивки: 103 проверки API и headless-проверка разделов
bench/run.sh api_check.py
LD_LIBRARY_PATH=/tmp/kilo/libasound/usr/lib/x86_64-linux-gnu \
  output/bench/venv/bin/python bench/ui_check.py   # Playwright, см. VERIFIED_FUNCTIONS.md §3

# часовой пояс (только чтение): /etc/TZ — относительный симлинк на зону uclibc,
# его ставит образ или S20okotz из system.tz в /userdata/divinus.yaml
readlink /etc/TZ                     # ожидается ../usr/share/zoneinfo/uclibc/<зона>
date                                 # локальное время выбранной зоны
date -u                              # UTC: разница равна смещению зоны
ls /usr/share/zoneinfo/uclibc        # установленный набор зон
hwclock -u                           # RTC идёт в UTC; вывод локальный, совпадает с date

# контроллеры: версии, роль STM8, загрузчик
curl -s -u admin:oko-change-me http://192.168.52.97/api/firmware
/oem/usr/bin/oko-motor -b 3 -a 0x60 version
/oem/usr/bin/oko-motor -b 3 -a 0x60 info
/oem/usr/bin/oko-ec info

# AF-метрика: 20 отсчётов; для SC230AI нужен contrast_af.enable=1 в IQ
curl -s -X POST -u admin:oko-change-me 'http://192.168.52.97/api/focus?enable=1&motor=1'
bench/run.sh focus_check.py --seconds 20 --interval 1 --snapshot
# автофокус по контрастной сцене (opt-in): full + ROI + micro-fine (af=fine),
# кривые в output/bench/
OKO_FOCUS_MOTOR_TESTS=1 OKO_FOCUS_AF_TESTS=1 bench/run.sh api_check.py

# VPN: модуль по требованию, утилиты и профили
lsmod | grep amneziawg
/oem/usr/bin/awg version
/usr/bin/oko-vpn status
curl -s -u admin:oko-change-me http://192.168.52.97/api/vpn
# полный прогон с пиром на хосте (см. bench/README.md, «VPN-пир на хосте»)
bench/run.sh vpn_check.py --peer-host 192.168.52.1 \
  --profile output/bench/vpn/awg0.conf

# прошивка через WebUI: негативные проверки (400) и реальный STM8
bench/run.sh firmware_check.py --negative
bench/run.sh firmware_check.py --flash --target stm8 \
  --file project/app/stm8-motor/build/app-2.1.1.ihx --confirm FLASH

# NPU в OSD: строка должна появиться после старта YOLO
# ожидаемый файл: CPU 20% NPU 17%\nMEM FREE 77% SD FREE 14.8/14.8GiB\nT 65.3C
cat /tmp/osd-sys.txt; cat /proc/rknpu/load

# RGA im2d fd-путь: копирование через CMA-буфер (rc=1 — нет /data/in0...bin,
# это нормально; зависание/чёрный экран — откат на CPU и power cycle)
LD_LIBRARY_PATH=/oem/usr/lib /oem/usr/bin/rgaImDemo --copy
# конверсия YOLO на RGA: в логе не должно быть RGA_BLIT/falling back
curl -s -u admin:oko-change-me -X POST 'http://192.168.52.97/api/yolo?convert=rga'
grep -aE 'rk_rga|RGA_BLIT|falling back' /tmp/divinus.log | tail

# рамки детекции на потоке (2BPP RGN 12, слой 6): включение на лету
curl -s -u admin:oko-change-me -X POST 'http://192.168.52.97/api/yolo?boxes=1'
grep -aE 'NN overlay region 12' /tmp/divinus.log | tail -1
#   ожидается: "1920x1088 2BPP on 1 channel(s), layer 6"
# для гарантированных детекций на статичной сцене: conf_threshold=0.05,
# затем кадры RTSP и проверка красных/синих линий по краям рамок
curl -s -u admin:oko-change-me -X POST 'http://192.168.52.97/api/yolo?conf_threshold=0.05'
bench/run.sh osd_check.py --ip 192.168.52.97 --frames 3 --interval 1 --seconds 4
# выключение: регион уничтожается, кадры остаются чистыми
curl -s -u admin:oko-change-me -X POST 'http://192.168.52.97/api/yolo?boxes=0'
# восстановить: conf_threshold=0.35, boxes=1, /api/cmd?save=1

# RTSP отдаёт кадры только при stream.enable=true в /userdata/divinus.yaml
grep -A2 '^stream:' /userdata/divinus.yaml

# новая разметка: boot 0x600000, oem 0x2800000, userdata 0x2800000, rootfs 0xA800000
cat /proc/cmdline
cat /proc/mtd

# microSD: состояние, mount/unmount и формат (формат уничтожает данные карты)
curl -s -u admin:oko-change-me http://192.168.52.97/api/storage
curl -s -u admin:oko-change-me 'http://192.168.52.97/api/storage?action=unmount&token=<token>'
# без карты start=1 должен вернуть blocked и не создать файл в /oem или /userdata
curl -s -u admin:oko-change-me 'http://192.168.52.97/api/record?enable=true&start=1'
# с картой: format -> один раздел FAT32 OKO, затем запись 5 с и проверка MP4
# token берётся из ответа /api/storage (поле format_token)
curl -s -u admin:oko-change-me \
  'http://192.168.52.97/api/storage?action=format&fs=fat32&confirm=FORMAT&token=<token>'
ls -l /mnt/sdcard
ls /oem/usr/ko | grep -E 'sc3336|mis5001|imx415|mia1321'   # пусто
ls /oem/usr/share/iqfiles                                   # только sc230ai_*.json
ls /oem/usr/bin/sample_* /oem/usr/bin/simple_* 2>/dev/null  # пусто

# I2C3-M1: 100 kHz = 0x000186a0
hexdump -C /sys/class/i2c-adapter/i2c-3/of_node/clock-frequency

# помощь фокусировке: AF-статистика (A6), мотор 0x60 и NPU в OSD
bench/run.sh focus_check.py
bench/run.sh focus_check.py --enable --polls 10
curl -s -u admin:oko-change-me 'http://192.168.52.97/api/focus?map=1'
curl -s -u admin:oko-change-me 'http://192.168.52.97/api/focus' | grep -o '"af":{[^}]*}'
i2cdetect -y 3 | grep -E ' 42 | 60 '
# вторая строка несёт сводку SD: MEM FREE 77% SD FREE 14.8/14.8GiB
cat /tmp/osd-sys.txt
oko-motor info
# прошивки контроллеров (без --flash — только версии)
bench/run.sh firmware_check.py
# STM32: проверка перехода в загрузчик без записи (образ не меняется)
/etc/init.d/S21oko-ec stop
/oem/usr/bin/oko-ec info            # device_type=2 (application)
/oem/usr/bin/oko-ec enter-update    # 0x0E, пауза 3 с
/oem/usr/bin/oko-ec info            # device_type=1 (bootloader)
/oem/usr/bin/oko-ec reset
sleep 2
/oem/usr/bin/oko-ec info            # снова device_type=2, версия/ячейки
/etc/init.d/S21oko-ec start
# STM32: реальная прошивка (CBT6, слот 120 КиБ, версия 1.0.0 = rollback)
bench/run.sh firmware_check.py --flash --target stm32 \
  --file output/bench/stm32/app-1.0.0.bin --confirm FLASH
# ожидается job done; затем /api/firmware, oko-ec info/battery, power-cycle
# STM8: первая запись factory-2.1.1.ihx по ST-Link, далее через WebUI/oko-motor
ls -l project/app/stm8-motor/build/app-2.1.1.ihx \
      project/app/stm8-motor/build/factory-2.1.1.ihx

# Ethernet: MAC и настройки в env-разделе U-Boot
fw_printenv oko_mac oko_net_mode oko_ipaddr oko_netmask oko_gateway oko_dns
oko-net env
curl -s -u admin:oko-change-me http://192.168.52.97/api/network
# статический режим (сменить адрес на свободный; страница отключится)
curl -s -u admin:oko-change-me \
  'http://192.168.52.97/api/network?mode=static&ipaddr=192.168.52.220&netmask=255.255.255.0&gateway=192.168.52.254&dns=192.168.52.254'
# после переподключения на .220 вернуть DHCP:
curl -s -u admin:oko-change-me \
  'http://192.168.52.220/api/network?mode=dhcp&ipaddr=192.168.52.220&netmask=255.255.255.0&gateway=192.168.52.254&dns=192.168.52.254'
# MAC/IP должны сохраняться после reboot; DHCP-клиент — только dhcpcd.
# Полный update.img перезаписывает env: static-настройки задать заново,
# MAC восстановится из serial.
ps | grep dhcpcd | grep -v grep

# приложение запущено: divinus по умолчанию, rkipc — только при opt-out
ps | grep -E 'divinus|rkipc'
ls -l /oem/usr/bin/divinus /userdata/divinus.enable
ls -l /userdata/divinus.yaml /userdata/rkipc.enable 2>/dev/null
# opt-out на DUT: touch /userdata/rkipc.enable && reboot
curl -s -u admin:oko-change-me http://192.168.52.97/api/status
curl -s -u admin:oko-change-me -o /tmp/snap.jpg http://192.168.52.97/image.jpg
# OSD: time — bottom-right, telemetry (4 lines) — top-left, GPS — top-right,
# msg — top-left below telemetry.
# Смотреть в RTSP-потоке (кадры `/image.jpg` кодируются отдельным VENC-каналом
# и OSD не содержат — ограничение RGN на RV1106).
# Два кадра с интервалом >1 с: меняется время; >16 с: телеметрия и GPS.
# На DUT (не с хоста): сервис сообщений — локальный FIFO.
echo 'OKO MESSAGE 123' > /tmp/osd.fifo
# через 60+ с reg3 гаснет: /tmp/osd-msg.txt пуст, /tmp/osd-msg.ts удалён
ls -l /tmp/osd-dyn.txt /tmp/osd-msg.txt /tmp/osd.fifo
# OSD с хоста: попарный диф регионов по RTSP, кадры и кропы в output/bench/
bench/run.sh osd_check.py --ip 192.168.52.97 --seconds 17
# геометрия оверлеев и ePTZ: кроп, /mjpeg и /image.jpg, рестарт MP4
bench/run.sh geometry_check.py --what zoom
# NN-рамки и OSD под 0/90/180/270 — opt-in: модель, объект в кадре, штатив
OKO_GEOMETRY_TESTS=1 bench/run.sh geometry_check.py --what boxes
OKO_GEOMETRY_TESTS=1 bench/run.sh geometry_check.py --what osd
# сообщение в reg2: сначала на DUT, затем повторить osd_check.py
bench/run.sh serial_exec.py "echo 'OKO MSG TEST' > /tmp/osd.fifo"
# RTSP-корень divinus — без /live/0:
# ffprobe -rtsp_transport tcp rtsp://admin:oko-change-me@192.168.52.97:554/

# I2C3-M1 (GPIO1_D2/GPIO1_D3, 100 кГц) включён по умолчанию
ls -l /dev/i2c-3
# шина должна быть доступна; отсутствие устройств на ней — норма
# pinmux: оба вывода принадлежат ff460000.i2c, группа i2c3m1-xfer;
# GPIO2_A6/GPIO2_A7 (I2C3-M0) в I2C3 не переведены
mount -t debugfs none /sys/kernel/debug 2>/dev/null
grep "function i2c3" /sys/kernel/debug/pinctrl/*/pinmux-pins

# oko-ec: шина и три устройства (0x40 SHT21, 0x41 INA3221, 0x42 STM32 EC)
# i2c-tools в образ не входит, список устройств даёт scan
oko-ec scan
oko-ec info          # device_type: 2, proto 2, cells 3
oko-ec battery
oko-ec ina226        # INA3221: показывает chip и канал 1 по умолчанию
oko-ec --ina-channel 2 ina226   # при подключении нагрузки к каналу 2
oko-ec sht21         # температура и влажность
oko-ec sensors
# демон и файлы состояния (OSD берёт из них реальные T/BAT)
ps | grep oko-ec
cat /tmp/oko-ec.json
cat /tmp/oko-ec.osd
/etc/init.d/S21oko-ec status
# opt-out: touch /userdata/oko-ec.disable && reboot

# сеть
ifconfig eth0
ip route

# разделы смонтированы из SPI NAND (ubifs)
mount | grep -E ' /oem | /userdata '

# логи
ls -l /tmp/*.log /data/core-* 2>/dev/null
dmesg | tail -50
```

Типовые проблемы:

| Симптом | Вероятная причина |
|---|---|
| Нет загрузки после прошивки | не прошит `rootfs`/`oem`, повреждён UBI (неполное стирание раздела), неверная таблица разделов |
| `ubi0 error: ubi_attach: bad image sequence number` | раздел записан по длине образа, а не по полному размеру (`wl` вторым аргументом передан неверный размер) |
| `rkipc` не стартует | нет `rkipc.ini` для сенсора или не смонтирован `/oem` (см. `INIT_ORDER.md`) |
| `divinus` не стартует (ожидается по умолчанию) | есть `/userdata/rkipc.enable` (opt-out), нет `/oem/usr/bin/divinus` (сборка пакета упала молча), нет IQ-файла сенсора, `dlopen` не находит `librockit.so`/`librkaiq.so` (`LD_LIBRARY_PATH` без `/oem/usr/lib`) |
| `divinus`: нет картинки, AIQ-ошибки | геометрия в `divinus.yaml` не совпадает с режимом сенсора (SC230AI — 1920x1080), IQ не найден по имени модуля из `/proc/rkisp-vir0` |
| `divinus`: `mmap part 1 failed!`, пустые JPEG/RTSP | в сборке не применён патч `compat.c` (ABI `mmap`): на uClibc hard-float не должен определяться собственный 64-битный `mmap`; см. `project/app/divinus/README.md` |
| OSD: нет телеметрии/сообщений, `@/tmp/...` виден как текст | не применён патч `region.c` (`@file`-провайдер) или не запущен `oko-osd`: `ps` не показывает процесс, `/etc/init.d/S20oko-osd status` сообщает `not running`, файлов `/tmp/osd-*` нет |
| OSD: шрифт не найден | в `regN_font` указан отсутствующий шрифт; в rootfs есть только `/oem/usr/share/simsun_en.ttf` (`regN_font: simsun_en`) |
| `/userdata/divinus.yaml` перезаписан после загрузки | ожидаемо: `S20oko-osd` заменяет конфиг без `reg0_text`/`reg0_size`/`reg0_color`/`reg3_text` фабричным (старый формат); первый прежний файл сохраняется как `divinus.yaml.bak` |
| Запущены и `divinus`, и `rkipc` | маркер появился после старта `rkipc`; перезагрузить DUT (ветки взаимоисключающие) |
| Нет `/dev/video*` | `insmod_ko.sh` не выполнился или `sc230ai.ko` отсутствует |
| `oko-ec scan` не находит `0x40`/`0x41`/`0x42` | нет внешних pull-up на `GPIO1_D2`/`GPIO1_D3`, неверный pinmux (I2C3-M1 вместо M0) или INA3221 с другой перемычкой `A0` (ожидается `0x41`) |
| `oko-ec ina226` показывает нули | силовые входы INA3221 (`VIN+`/`VIN-`, `VBUS`) не подключены к нагрузке/шине; обмен по I2C при этом работает |
| `oko-ec ina226` показывает не тот канал | нагрузка подключена к другому каналу INA3221: `--ina-channel 1..3` (по умолчанию 1) |
| `oko-ec battery` пишет, что устройство в загрузчике | STM32 в режиме загрузчика (`device_type = 1`); выполнить `oko-ec reset` и повторить |
| `/tmp/oko-ec.osd` не появляется | не запущен `S21oko-ec` (нет `/dev/i2c-3` или `/oem/usr/bin/oko-ec`), есть `/userdata/oko-ec.disable`, либо демон уже работает (см. `/tmp/oko-ec.log`) |

## 9. Известные особенности

- **CMA накапливается после тяжёлых медиа-прогонов.** После двух и более
  подряд прогонов `ui_check.py`/`geometry_check.py` или ручной отладки
  ePTZ/поворота перезагружайте DUT. Решающие признаки: `cma_alloc ... ret:
  -12` в `dmesg -l err`, `/image.jpg` отвечает 503, RTSP не отдаёт кадры.
  Высокий `loadavg` и потоки ядра в `DW` (`vlog`, `valloc`, `vpss`, `venc`, …)
  сами по себе не признак — они висят в `DW` и на здоровой плате. Лечение —
  только перезагрузка (`bench/run.sh dut.py --host 192.168.52.97 'reboot'`,
  ожидание 50–60 с); перед замерами геометрии проверяйте, что `/image.jpg`
  отвечает 200 и RTSP отдаёт кадры. После нескольких циклов
  `focus?enable=1/0` AF-метрика может оставаться `idle`/`no_stats` до
  перезагрузки.
- `IMAGE/` содержит актуальную релизную сборку под `RV1106 Pro Max` + SC230AI;
  устаревший артефакт `RV1103 Pico Mini` удалён 2026-11-08.
- Драйвер `motor_24byj48` (`motor.ko`, шаговик 28BYJ-48) удалён из форка
  2026-11-07: в истории прошивок его не было, привод фокуса — STM8 по I2C.
- `insmod_ko.sh` вызывается из `RkLunch.sh`, который запускает `S21appinit`;
  каталог `/oem/usr/etc/init.d/` пуст, вся логика приложения зашита в `post_chk`.
  Подробнее — `INIT_ORDER.md`.
- `post_chk` выбирает для `sc230ai` шаблон `/oem/usr/share/rkipc-300w.ini`
  (сенсор 3 МП). Если геометрия кадра окажется некорректной, ini подбирается отдельно.
- В автономном режиме `oem` и `userdata` монтируются из SPI NAND (ubifs).
  Изменения, сделанные на устройстве в `/userdata`, сохраняются в NAND.
- Сборка идёт на хосте; сетевые сервисы стенда (`tftpd-hpa`,
  `nfs-kernel-server`, `rpcbind`) запускает контейнер `services/` через
  `tools/native/setup_netboot.sh`. Старый контейнер сборки не используется.
- `divinus` — стример OpenIPC (MIT, `project/app/divinus/`), **по умолчанию**:
  маркер `/userdata/divinus.enable` создаёт `S20oko-osd`. Opt-out —
  `/userdata/rkipc.enable` (маркер divinus удаляется, запускается `rkipc`).
  RV11xx-HAL в upstream ещё «в разработке»: RTSP/fMP4/JPEG работают,
  аудио ограничено. Web-порт divinus — `80`, RTSP — `554`; пока divinus активен,
  web/RTSP rkipc не запускаются.
- OSD divinus включён в фабричном конфиге: время — в правом нижнем углу
  (`reg0`), телеметрия `T/BAT/LTE/WiFi` — четырьмя строками в левом верхнем
  (`reg1`), GPS — в правом верхнем (`reg2`), сообщения из `/tmp/osd.fifo` —
  в левом верхнем под телеметрией (`reg3`, TTL 60 с), CPU load / свободная
  память / температура CPU — тремя строками в левом нижнем углу (`reg4`).
  Провайдер `@file` — локальный патч `region.c`; данные пишут `oko-osd` и
  `reg0_text: "$t"`.
  Размер шрифта всех регионов — `24` px, цвет — ярко-зелёный `0x07e0`
  (RGB565), чёрный контур `regN_outl: 0x8000` толщиной `1.0`. RGN использует один canvas (`canvas=1`): с двумя canvas драйвер
  иногда показывал OSD предыдущей секунды. Шрифт — `/oem/usr/share/simsun_en.ttf` (`regN_font: simsun_en`):
  покрытие — ASCII `0x21–0x7e` плюс несколько CJK-иероглифов, кириллицы в шрифте
  нет, поэтому сообщения и телеметрию пишите латиницей.
  OSD виден в RTSP (VENC-канал 0); snapshots `/image.jpg` кодируются отдельным
  VENC-каналом и OSD не содержат — ограничение RGN на RV1106.
- `S20oko-osd` перезаписывает `/userdata/divinus.yaml` фабричным, если в нём нет
  `reg0_text`, `reg0_size`, `reg0_color` или `reg3_text` (конфиг старого формата);
  первый прежний файл сохраняется как `/userdata/divinus.yaml.bak`. Ручные правки
  такого файла не переживают перезагрузку; добавьте эти параметры или отключите
  OSD в файле.
- divinus ищет конфиг рядом с исполняемым файлом, затем в текущем каталоге, затем
  `/etc/divinus.yaml`. `post_chk` запускает его из `/userdata`, поэтому сохранение
  конфига из WebUI (`/api/cmd?save`) остаётся в `/userdata` (файл `.bak` — там же).
- в `compat.c` divinus пропатчен ABI `mmap` (libc-вариант с 32-битным `off_t`),
  поэтому `LD_PRELOAD` не нужен. Если после обновления upstream в логе снова
  появился `mmap part 1 failed!`, значит патч `compat.c` не применён; см.
  `project/app/divinus/README.md`. Второй патч (`region.c`, `@file`-провайдер OSD)
  повторяется тем же порядком.

- STM8S103F3P6 подключён к I2C3 по адресу `0x60` и к ST-Link/V2 по SWIM;
  первая прошивка — `factory-2.1.1.ihx` (§7C), далее `oko-motor flash` или
  `/api/flash`. Версия обеих прошивок берётся из имени файла
  (`app-1.0.1.bin`, `app-2.1.1.ihx`); безымянный `app.bin`/`app.ihx`
  отклоняется, а несовпадение имени с заголовком OKO8 — тоже 400.
  EEPROM-позиции моторов при прошивке flash не стираются.
- AF-статистика SC230AI требует `af_v31.TuningPara.contrast_af.enable=1`
  в IQ (оба блока). Метрика в `/api/focus` — по 225 ячейкам; ROI применяется
  выбором ячеек, AFM-карта полнокадровая. Малые ходы мотора метрику не
  меняют: оптическая связь объектива на стенде не подтверждена.
- RTSP молчит (OPTIONS без ответа), если `stream.enable=false` в
  `/userdata/divinus.yaml`; для проверки OSD/потока включить и перезагрузить.
- Обновление ПО из WebUI: пакет готовится на microSD, прошивку выполняет
  U-Boot `sd_update` при следующей загрузке, но только при выставленном
  `oko_update` в U-Boot env (`apply`), поэтому перезагрузка до подтверждения
  ничего не прошивает. Оставленная в слоте карта после успешной загрузки
  безопасна: `S20oko-update` удаляет `sd_update.txt` и образы. Если Linux не загрузился (обрыв питания во время `mtd erase/write`),
  карта сохраняет пакет и U-Boot повторяет прошивку; иначе плату
  восстанавливают через USB (`upgrade_tool`, Maskrom). Раздел `/userdata`
  обновлением не затрагивается.
- Прошивка через WebUI идёт в фоне; `POST /api/flash` без `confirm=FLASH`,
  без версии в имени, с чужим расширением, битым `OKO8`, несовпадением
  имени/заголовка или большим файлом отдаёт 400, повторный
  POST во время работы — 409. Демон `S21oko-ec` на время задачи
  останавливается; при обрыве хостового процесса STM8 остаётся в загрузчике
  и принимает повторную загрузку.

## 10. Правила ведения

1. Обновлять документ при любой смене железа, IP-адресов, путей, версии SDK или
   состава модулей.
2. При каждом обновлении менять «Дату обновления» в разделе 1.
3. Секреты не хранить: только плейсхолдеры `<...>` и публичные вендорские дефолты.
4. Расхождения между документом и скриптами трактовать в пользу скриптов
   (`bench/*.py`, `tools/native/*.sh`, BoardConfig) и сразу править документ.