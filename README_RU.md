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
| `LF_TARGET_ROOTFS` | `buildroot` | корневая ФС Buildroot (busybox init) |
| Режим | **SPI NAND** | автономная загрузка с flash (`root=ubi0:rootfs`) |
| Сенсор | **SC230AI** | драйвер `sc230ai` + `rv1106-sc230ai.config`; 1080p30 и 480p120, `HDR_X2` экспериментально |
| `RK_KERNEL_DTS` | `rv1106g-luckfox-pico-pro-max.dts` | device tree платы |
| `RK_BOOTARGS_CMA_SIZE` | `66M` | CMA для медиастека |
| `RK_ENABLE_WIFI` | `y` | WiFi-модули в `/oem/usr/ko` |
| `RK_ENABLE_MOTOR` | **не задан** | драйвер мотора не собирается (см. ниже) |

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
размером раздела — см. `TESTBENCH.md` §7B.

Подробные адреса, команды и порядок действий — в `TESTBENCH.md`.

## Документация проекта

| Файл | Содержание |
|---|---|
| `STRUCTURE.md` | структура репозитория: папки, ключевые файлы, назначение |
| `INIT_ORDER.md` | порядок инициализации: U-Boot → ядро → busybox init → модули → `divinus`/`rkipc` |
| `TESTBENCH.md` | паспорт тестового стенда: железо, сеть, учётки, прошивка, диагностика |
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