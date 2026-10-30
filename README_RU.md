# Luckfox Pico OKO

Русскоязычная точка входа в репозиторий проекта **OKO** — форк SDK Luckfox Pico
(Rockchip RV1103/RV1106). Доработки форка: IPC-камера на сенсоре **SC230AI**,
сборка в Docker и отладка с загрузкой по сети (**netboot**: TFTP + NFS).
Вендорский SDK уже содержит драйвер шагового двигателя **28BYJ-48**; в текущей
конфигурации он выключен (см. «Полезные команды»).

Вендорская документация SDK не изменена и лежит рядом: `README.md` (EN),
`README_CN.md` (CN), история версий SDK — `UPDATE_LOG.md` / `UPDATE_LOG_CN.md`.

## Текущая конфигурация

Активная конфигурация задаётся симлинком `.BoardConfig.mk`:

```
.BoardConfig.mk -> project/cfg/BoardConfig_IPC/BoardConfig-SPI_NAND-Buildroot-RV1106_Luckfox_Pico_Pro_Max-NETBOOT-SC230AI.mk
```

Что выбрано в этом файле:

| Параметр | Значение | Смысл |
|---|---|---|
| `RK_CHIP` | `rv1106` | SoC RV1106 |
| `RK_APP_TYPE` | `RKIPC_RV1106` | приложение `rkipc` |
| `RK_BOOT_MEDIUM` | `spi_nand` | загрузка с SPI NAND |
| `LF_TARGET_ROOTFS` | `buildroot` | корневая ФС Buildroot (busybox init) |
| Режим | **NETBOOT** | ядро по TFTP, корень по NFS (`rv1106-netboot.config`) |
| Сенсор | **SC230AI** | драйвер `sc230ai` + `rv1106-sc230ai.config` |
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
кросс-тулчейном Rockchip и TFTP-сервером.

```sh
./start.sh
```

Скрипт:

1. собирает образ `luckfox-builder:22.04` (`docker compose build`);
2. поднимает контейнер `luckfox_oko_build_env` (`docker compose up -d`,
   `network_mode: host`, рабочая папка `/workspace`, проект смонтирован туда же);
3. при наличии `.gitattributes` подтягивает Git LFS-файлы;
4. создаёт `output/image` (корень TFTP) и открывает shell внутри контейнера.

Внутри контейнера:

```sh
./build.sh            # полная сборка (all)
./build.sh firmware   # только образ прошивки
```

Результаты сборки — в `output/image/` (например, `update.img`), промежуточные
артефакты — в `output/out/`, общий лог — `output/build_all.log`.

TFTP-сервер (`tftpd-hpa`) стартует из `docker-entrypoint.sh` на порту `69/udp`
с корнем `/workspace/output/image` (`TFTP_ROOT`). NFS-сервер в образ не входит —
экспорт `/srv/nfs/rootfs` для netboot настраивается на хосте отдельно
(см. `TESTBENCH.md`).

## Сборка без Docker

Сборка на чистом Ubuntu 22.04 с установкой зависимостей и ручной настройкой
окружения описана в вендорском `README.md` (раздел про установку SDK). Требования
к пакетам те же, что перечислены в `Dockerfile`, а тулчейн берётся из
`tools/linux/toolchain/arm-rockchip830-linux-uclibcgnueabihf/bin`
(`CROSS_COMPILE=arm-rockchip830-linux-uclibcgnueabihf-`, `ARCH=arm`).

## Загрузка на устройство

Используются два пути.

**A. Прошивка SPI NAND (автономная загрузка).**

```sh
./build.sh firmware
# output/image/update.img
./rkflash.sh          # или tools/linux/Linux_Upgrade_Tool
```

Устройство переводится в режим Maskrom; затем образ пишется в SPI NAND.

**B. Netboot (текущий рабочий режим стенда).**

1. `./start.sh` — поднимает контейнер и TFTP-корень `output/image`.
2. На хосте подготовить каталог лога и запустить автоматизацию по serial:
   ```sh
   mkdir -p /tmp/kilo
   python3 netboot-catch.py
   ```
   Скрипт требует `pyserial`, открывает последний `/dev/ttyACM*` на 115200,
   пробивает U-Boot, выставляет `ipaddr`/`serverip`/`sys_bootargs`, загружает
   `boot.img` по TFTP и выполняет `boot_fit`.
3. Сбросить питание устройства (или нажать Reset) — дальше скрипт работает сам.

Подробные адреса, переменные U-Boot и порядок действий — в `TESTBENCH.md`.

## Документация проекта

| Файл | Содержание |
|---|---|
| `STRUCTURE.md` | структура репозитория: папки, ключевые файлы, назначение |
| `INIT_ORDER.md` | порядок инициализации: U-Boot → ядро → busybox init → модули → `rkipc` |
| `TESTBENCH.md` | паспорт тестового стенда: железо, сеть, учётки, прошивка, диагностика |
| `CHANGELOG.md` | история изменений OKO-форка (Keep a Changelog + SemVer) |
| `README.md`, `README_CN.md` | вендорская документация SDK (EN/CN) |
| `UPDATE_LOG.md`, `UPDATE_LOG_CN.md` | вендорская история версий SDK (EN/CN) |

## Полезные команды

```sh
# на устройстве (после netboot)
ssh root@192.168.52.150          # пароль по умолчанию — вендорский, см. TESTBENCH.md
luckfox-config                   # настройки платы/камеры
lsmod                            # загруженные модули ядра
sh /oem/usr/ko/insmod_ko.sh      # ручная перезагрузка модулей (порядок важен)
```

- Последовательный порт: `/dev/ttyACM0`, **115200 8N1** (устройство может
  определиться и как `/dev/ttyACM1` — скрипт берёт последний `ttyACM*`).
- Лог serial-сессии: `/tmp/kilo/netboot-run4.log` (задаётся в `netboot-catch.py`).
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
- Docker использует `network_mode: host`: TFTP виден из локальной сети хоста,
  но порт `69/udp` будет занят, если на хосте уже запущен другой TFTP-сервер.