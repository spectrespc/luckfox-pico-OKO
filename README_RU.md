# Luckfox Pico OKO

Русскоязычная точка входа в репозиторий проекта **OKO** — форк SDK Luckfox Pico
(Rockchip RV1103/RV1106). Доработки форка: IPC-камера на сенсоре **SC230AI**,
сборка в Docker и автономная загрузка с **SPI NAND**.
Вендорский SDK уже содержит драйвер шагового двигателя **28BYJ-48**; в текущей
конфигурации он выключен (см. «Полезные команды»).

Вендорская документация SDK не изменена и лежит рядом: `README.md` (EN),
`README_CN.md` (CN), история версий SDK — `UPDATE_LOG.md` / `UPDATE_LOG_CN.md`.

## Текущая конфигурация

Активная конфигурация задаётся симлинком `.BoardConfig.mk`:

```
.BoardConfig.mk -> project/cfg/BoardConfig_IPC/BoardConfig-SPI_NAND-Buildroot-RV1106_Luckfox_Pico_Pro_Max-SC230AI.mk
```

Что выбрано в этом файле:

| Параметр | Значение | Смысл |
|---|---|---|
| `RK_CHIP` | `rv1106` | SoC RV1106 |
| `RK_APP_TYPE` | `RKIPC_RV1106` | приложение `rkipc` |
| `RK_BOOT_MEDIUM` | `spi_nand` | загрузка с SPI NAND |
| `RK_PARTITION_CMD_IN_ENV` | `256K(env),256K@256K(idblock),512K(uboot),6M(boot),40M(oem),10M(userdata),198M(rootfs)` | разметка 256 МБ NAND: `boot` 6 МБ, `oem` 40 МБ, `rootfs` 198 МБ |
| `LF_TARGET_ROOTFS` | `buildroot` | корневая ФС Buildroot (busybox init) |
| Режим | **SPI NAND** | автономная загрузка с flash (`root=ubi0:rootfs`) |
| Сенсор | **SC230AI** | драйвер `sc230ai` + `rv1106-sc230ai.config`; 1080p30 и 480p120, `HDR_X2` экспериментально |
| `RK_KERNEL_DTS` | `rv1106g-luckfox-pico-pro-max-sc230ai.dts` | device tree платы; `&sc3336`/`&mis5001` выключены |
| I2C3-M1 | **включён, 100 кГц** | `pinctrl-0 = <&i2c3m1_xfer>` (`GPIO1_D2`/`GPIO1_D3`), устройство `/dev/i2c-3` |
| `RK_BOOTARGS_CMA_SIZE` | `66M` | CMA для медиастека |
| `RK_ENABLE_WIFI` | `y` | WiFi-модули в `/oem/usr/ko` |
| `RK_AIISP_MODEL` | `rkmodel_1080_1920.aiisp` | в `/oem/usr/lib` ставится только эта модель AIISP |
| `RK_ENABLE_MOTOR` | **не задан** | драйвер мотора не собирается (см. ниже) |

В образ активной SC230AI-платы попадают только `sc230ai.ko` и IQ-файл
`sc230ai_CMK-OT2115-PC1_30IRC-F16.json`. Неиспользуемые сенсоры (SC3336,
MIS5001, IMX415, MIA1321) выключены в DTS и config-фрагменте, их модули и IQ
удаляются из oem. Демонстрационные `sample_*` и `simple_*` из образа удалены;
диагностические `rk_*_test`/`rk_mpi_*_test` сохранены.

Сменить конфигурацию можно интерактивным меню:

```sh
./build.sh lunch
```

## Быстрый старт (Docker)

Сборка выполняется в изолированном контейнере Ubuntu 22.04 с уже установленным
кросс-тулчейном Rockchip.

```sh
./start.sh
```

Скрипт:

1. собирает образ `luckfox-builder:22.04` (`docker compose build`);
2. поднимает контейнер `luckfox_oko_build_env` (`docker compose up -d`,
   `network_mode: host`, рабочая папка `/workspace`, проект смонтирован туда же);
3. при наличии `.gitattributes` подтягивает Git LFS-файлы;
4. создаёт `output/image` (каталог готовых образов) и открывает shell внутри контейнера.

Внутри контейнера:

```sh
./build.sh            # полная сборка (all)
./build.sh firmware   # только образ прошивки
```

Результаты сборки — в `output/image/` (например, `update.img`), промежуточные
артефакты — в `output/out/`, общий лог — `output/build_all.log`.

Контейнер не запускает сетевых сервисов: сборка и прошивка выполняются локально,
готовые образы лежат в `output/image/`.

## Сборка без Docker

Сборка на чистом Ubuntu 22.04 с установкой зависимостей и ручной настройкой
окружения описана в вендорском `README.md` (раздел про установку SDK). Требования
к пакетам те же, что перечислены в `Dockerfile`, а тулчейн берётся из
`tools/linux/toolchain/arm-rockchip830-linux-uclibcgnueabihf/bin`
(`CROSS_COMPILE=arm-rockchip830-linux-uclibcgnueabihf-`, `ARCH=arm`).

## Загрузка на устройство

Используется SPI NAND.

> **Внимание:** таблица разделов изменена (`boot` 6 МБ, `oem` 40 МБ,
> `rootfs` 198 МБ). После первой прошивки этого образа покомпонентная прошивка
> `oem`/`rootfs` со старой разметкой несовместима: нужна **полная прошивка**
> `output/image/update.img`. Старые `/userdata` и данные на NAND будут
> потеряны — сохраните нужное на microSD/хост до прошивки.

**A. Полная прошивка SPI NAND.**

```sh
./build.sh firmware
# output/image/update.img
./rkflash.sh update   # полный образ; или tools/linux/Linux_Upgrade_Tool
```

Устройство переводится в режим Maskrom; затем образ пишется в SPI NAND.

**B. Покомпонентная прошивка изменённых разделов.**

Режим для выкатки в SPI NAND без полного образа: прошиваются только изменённые
разделы `oem`/`rootfs`, остальные (включая `env` с MAC-адресом) не трогаются.
Нужны `upgrade_tool db output/image/download.bin` и запись по LBA с полным
размером раздела — см. `TESTBENCH.md` §7B. Применять только после того, как
устройство уже прошито новой таблицей разделов (полный `update.img`); иначе
LBA не совпадут.

Подробные адреса, команды и порядок действий — в `TESTBENCH.md`.

## Медиа-фичи divinus

Стример `divinus` (запускается при наличии `/userdata/divinus.enable`) получил
набор возможностей с гейтами на стенде:

| Возможность | Состояние | Включение |
|---|---|---|
| ePTZ (цифровой зум, кроп VPSS) | работает | `POST /api/eptz`, секция `[eptz]` |
| Поворот 90/180/270 | работает через VENC | `POST /api/rotate`, `[rotate]` |
| SmartP/SVC/motion deblur/QP | работает (SmartP — со следующего старта) | `POST /api/venc`, `[venc]` |
| Ethernet: DHCP/static IP | работает | WebUI `Network`, `/api/network`, env `oko_*` |
| Второй H.26x поток `/live/1` | работает | `POST /api/substream`, `[substream]` |
| AIISP (ночная шумоподавка) | работает при модели в `/oem/usr/lib` | `POST /api/aiisp`, `[aiisp]` |
| IVS (детекция движения/объектов) | работает (MD), действия: снимок, клип, OSD | `POST /api/ivs`, `[ivs]` |
| Runtime IQ (`switch_scene`/`updateIq`) | API работает, смена сцены принята | `POST /api/iq`, `[iq]` |
| HDR2/HDR3 | заблокировано: prepare падает на SC230AI | `POST /api/hdr` вернёт причину |
| OSD на main+sub+JPEG | заблокировано: multi-attach не прошёл гейт | `osd.scope` |
| microSD: mount/unmount/format | формат FAT32/MBR из WebUI, mount/unmount, запись и hotplug проверены на стенде 2026-11-04 | `/api/storage`, `oko-sdcard`, `[record]` |

Ethernet в WebUI (Network → Ethernet) переключается между DHCP и статическим
IPv4. Настройки (`mode`, `ipaddr`, `netmask`, `gateway`, `dns`) и постоянный
MAC (`oko_mac`) хранятся в env-разделе U-Boot (`fw_printenv`/`fw_setenv`,
`/etc/fw_env.config`), а не в `/userdata`, поэтому переживают reboot и
покомпонентную прошивку. Полный `update.img` перезаписывает `env.img`, но MAC
после него восстанавливается из `/proc/device-tree/serial-number` и остаётся
тем же; статические настройки после полной прошивки нужно задать заново.
DHCP-клиент один — `dhcpcd` с client ID по MAC; vendor-`S41dhcpcd` заменён
заглушкой, а DUID из `/var/db/dhcpcd` больше не используется. `oko-net apply`
применяет настройки при загрузке.

Модель AIISP ставится в образ по `RK_AIISP_MODEL` в BoardConfig. Карта microSD
монтируется udev-скриптом `oko-sdcard` в `/mnt/sdcard`; запись разрешена только
при реальном монтировании, старые сегменты удаляются по `record.max_bytes` и
`record.min_free_mb`. В WebUI (секция Storage) доступны `Mount`, `Unmount` и
`Format card`: формат создаёт один MBR-раздел FAT32 с меткой `OKO` на съёмной
`mmcblk`, требует подтверждения и одноразовый токен, блокируется во время
записи. Unmount/формат переводят recorder в отказ: файлы в `/oem` и
`/userdata` не создаются. Подробности, результаты гейтов и ограничения —
`project/app/divinus/source/doc/README.md`, архитектура и список локальных
патчей — `project/app/divinus/README.md`, реестр проверенного —
`VERIFIED_FUNCTIONS.md`.

## Утилита oko-ec

`/oem/usr/bin/oko-ec` работает с тремя устройствами на I2C3-M1 (`/dev/i2c-3`,
100 кГц): SHT21 `0x40`, INA3221/INA226 `0x41` и STM32 ACPI-EC `0x42`. Сборка —
`project/app/oko-ec` (`./build.sh app` → `output/out/app_out/usr/bin/oko-ec`);
исходники и правила обновления — `project/app/oko-ec/README.md`.

```sh
oko-ec scan                 # кого видно на шине
oko-ec info                 # тип устройства EC, протокол, версия, число ячеек
oko-ec battery              # напряжение/ток/температура/SOC/ячейки
oko-ec cells                # три ячейки, мВ
oko-ec temp                 # температура EC, 0.1 °C
oko-ec ina226               # INA3221/INA226: напряжение, ток, мощность
oko-ec --ina-channel 2 ina226   # канал 1..3 (только INA3221)
oko-ec sht21                # температура и влажность
oko-ec sensors              # все три устройства одним блоком
oko-ec --json sensors       # одна строка JSON
oko-ec flash app.bin 0x00010001   # OTA STM32 через загрузчик
oko-ec reset                # перезагрузка STM32
```

Адрес шины, адреса устройств, шунт и тайминги переопределяются опциями
`--dev`, `--ec-addr`, `--ina-addr`, `--ina-channel`, `--sht-addr`,
`--shunt-mohm`, `--delay-us`, `--retries` (шина также берётся из `OKO_EC_DEV`,
шунт — из `OKO_EC_SHUNT_MOHM`, канал INA3221 — из `OKO_EC_INA_CHANNEL`).
Для демона канал задаётся переменной `INA_CHANNEL` в `S21oko-ec`.

Демон запускается `S21oko-ec` при наличии `/dev/i2c-3` и утилиты; opt-out —
`/userdata/oko-ec.disable`. Он пишет `/tmp/oko-ec.json` (полные данные) и
`/tmp/oko-ec.osd` (`TS`/`OK`/`SHT`/`EC`/`INA`/`T`/`BAT`) каждые 2 с. `oko-osd`
из свежего файла (≤ 30 с) выводит четыре строки: SHT21 (температура и
влажность), STM32 (напряжение, ток, температура, SOC, ячейки), INA3221/INA226
(напряжение, ток, мощность) и LTE/WiFi. Без демона или при отсутствии всех
датчиков OSD работает на прежней симуляции.

## Документация проекта

| Файл | Содержание |
|---|---|
| `STRUCTURE.md` | структура репозитория: папки, ключевые файлы, назначение |
| `INIT_ORDER.md` | порядок инициализации: U-Boot → ядро → busybox init → модули → `divinus`/`rkipc` |
| `TESTBENCH.md` | паспорт тестового стенда: железо, сеть, учётки, прошивка, диагностика |
| `FEATURES.md` | поддерживаемый функционал: платформа, видео, сеть, `oko-ec`, microSD, ограничения и команды проверки |
| `VERIFIED_FUNCTIONS.md` | реестр проверенных на стенде функций: статус, доказательство, ограничения |
| `CHANGELOG.md` | история изменений OKO-форка (Keep a Changelog + SemVer) |
| `bench/README.md` | утилиты стенда: UART/SSH-команды, проверка OSD по RTSP, venv |
| `README.md`, `README_CN.md` | вендорская документация SDK (EN/CN) |
| `UPDATE_LOG.md`, `UPDATE_LOG_CN.md` | вендорская история версий SDK (EN/CN) |

## Полезные команды

```sh
# на устройстве (адрес eth0 выдаётся по DHCP — смотрите TESTBENCH.md §5)
ssh root@<IP-адрес-DUT>         # пароль по умолчанию — вендорский, см. TESTBENCH.md
luckfox-config                   # настройки платы/камеры
lsmod                            # загруженные модули ядра
sh /oem/usr/ko/insmod_ko.sh      # ручная перезагрузка модулей (порядок важен)
```

- Последовательный порт: `/dev/ttyACM0`, **115200 8N1** (устройство может
  определиться и как `/dev/ttyACM1` — `bench/` берёт последний `ttyACM*`).
- Стример по умолчанию — `divinus` с включённым OSD (время справа снизу,
  телеметрия `T/BAT/LTE/WiFi` четырьмя строками слева сверху, GPS справа
  сверху, сообщения из `/tmp/osd.fifo` слева сверху, CPU/свободная память/
  температура CPU слева снизу; ярко-зелёный текст `0x07e0` с чёрным контуром,
  30 fps); вернуть штатный `rkipc` —
  `touch /userdata/rkipc.enable && reboot`. Подробности — `INIT_ORDER.md`,
  `TESTBENCH.md` и `project/app/divinus/README.md`.
- Драйвер мотора 28BYJ-48: исходники в `sysdrv/drv_ko/motor`, строка
  `__insmod motor.ko` есть в `sysdrv/drv_ko/insmod_ko.sh`, но модуль **пока
  выключен**: `RK_ENABLE_MOTOR` не задан, `motor.ko` не собирается и в
  `/oem/usr/ko/` отсутствует. Для включения нужен `RK_ENABLE_MOTOR=y` и узел
  `motor` с GPIO в DTS (см. `sysdrv/drv_ko/motor/ReadMe` и `INIT_ORDER.md`).

## Важные примечания

- **Не копируйте дерево под Windows.** В репозитории есть симлинки
  (`.BoardConfig.mk`, `build.sh`, `rkflash.sh`, `config/*`), которые теряются
  на NTFS/FAT. Используйте Linux или WSL с ext4.
- **Git LFS.** Часть бинарных файлов хранится через LFS; перед сборкой выполните
  `git lfs pull`.
- **`IMAGE/` может содержать устаревший релиз** (`RV1103 Pico Mini`), который не
  соответствует активной конфигурации `RV1106 Pro Max`. Ориентируйтесь на
  `.BoardConfig.mk` и `output/`, а не на `IMAGE/`.
- `output/`, `IMAGE/`, `config/` и объектные файлы (`*.o`, `*.ko`) исключены из
  Git — это артефакты, а не исходники.
- Docker использует `network_mode: host`: контейнер видит сеть хоста, но не
  запускает собственных сетевых сервисов.