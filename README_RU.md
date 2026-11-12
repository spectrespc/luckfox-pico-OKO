# Luckfox Pico OKO

Русскоязычная точка входа в репозиторий проекта **OKO** — форк SDK Luckfox Pico
(Rockchip RV1103/RV1106). Доработки форка: IPC-камера на сенсоре **SC230AI**,
нативная сборка на Debian 13 и автономная загрузка с **SPI NAND**.

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
| `RK_APP_TYPE` | `RKIPC_RV1106` | тип приложения SDK; сборка `rkipc` отключена, лаунчер — `oko-lunch.sh` |
| `RK_BOOT_MEDIUM` | `spi_nand` | загрузка с SPI NAND |
| `RK_PARTITION_CMD_IN_ENV` | `256K(env),256K@256K(idblock),512K(uboot),6M(boot),40M(oem),40M(userdata),168M(rootfs)` | разметка 256 МБ NAND: `boot` 6 МБ, `oem` 40 МБ, `userdata` 40 МБ, `rootfs` 168 МБ |
| `LF_TARGET_ROOTFS` | `buildroot` | корневая ФС Buildroot (busybox init) |
| Режим | **SPI NAND** | автономная загрузка с flash (`root=ubi0:rootfs`) |
| Сенсор | **SC230AI** | драйвер `sc230ai` + `rv1106-sc230ai.config`; 1080p30 и 480p120, `HDR_X2` экспериментально |
| `RK_KERNEL_DTS` | `rv1106g-luckfox-pico-pro-max-sc230ai.dts` | device tree платы; `&sc3336`/`&mis5001` выключены |
| USB | **dual-role (`otg`)** | фрагмент `rv1106-usb-host.config` включает HID/USB-сеть/Bluetooth; host — с OTG-переходником и хабом, gadget — при подключении к ПК |
| I2C3-M1 | **включён, 100 кГц** | `pinctrl-0 = <&i2c3m1_xfer>` (`GPIO1_D2`/`GPIO1_D3`), устройство `/dev/i2c-3` |
| `RK_BOOTARGS_CMA_SIZE` | `66M` | CMA для медиастека |
| `RK_ENABLE_WIFI` | `y` | WiFi-модули в `/oem/usr/ko` |
| `RK_AIISP_MODEL` | `rkmodel_1080_1920.aiisp` | в `/oem/usr/lib` ставится только эта модель AIISP |

В образ активной SC230AI-платы попадают только `sc230ai.ko` и IQ-файл
`sc230ai_CMK-OT2115-PC1_30IRC-F16.json`. Неиспользуемые сенсоры (SC3336,
MIS5001, IMX415, MIA1321) выключены в DTS и config-фрагменте, их модули и IQ
удаляются из oem. Демонстрационные `sample_*` и `simple_*` из образа удалены;
диагностические `rk_*_test`/`rk_mpi_*_test` сохранены.

Сменить конфигурацию можно интерактивным меню:

```sh
./build.sh lunch
```

## Быстрый старт (Debian 13, без контейнера)

Сборка выполняется на хосте Debian 13 (trixie) с системным gcc 14 и вендорским
кросс-тулчейном Rockchip из `tools/linux/toolchain/`. Один раз установите
зависимости и проверьте окружение:

```sh
tools/native/setup_host.sh    # apt-пакеты trixie + проверка кросс-тулчейна
tools/native/smoke.sh         # отчёт output/bench/native/smoke.log
```

Перед сборкой в каждой новой оболочке:

```sh
source tools/native/env.sh
./build.sh            # полная сборка (all)
./build.sh firmware   # только образ прошивки
```

`env.sh` добавляет кросс-тулчейн и `sbin` в `PATH`, выставляет
`CROSS_COMPILE=arm-rockchip830-linux-uclibcgnueabihf-`, `ARCH=arm` и
`LANG/LC_ALL=C.UTF-8`. Результаты сборки — в `output/image/` (например,
`update.img`), промежуточные артефакты — в `output/out/`, общий лог —
`output/build_all.log`.

Сетевые сервисы стенда (TFTP/NFS для netboot) поднимает контейнер `services/`:
`tools/native/setup_netboot.sh` (нужны `docker.io` с плагином `docker compose`
и членство в группе `docker`; см. `TESTBENCH.md` §7D и `services/README.md`).
Отдельные Docker-пайплайны остались только для инструментов, не входящих в
сборку прошивки: `tools/rknn-toolkit2/` (конвертация YOLO) и
`tools/stm8-sdcc/` (прошивка STM8).

## Загрузка на устройство

Используется SPI NAND.

> **Внимание:** таблица разделов изменена (`boot` 6 МБ, `oem` 40 МБ,
> `userdata` 40 МБ, `rootfs` 168 МБ). После первой прошивки этого образа
> покомпонентная прошивка
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

**C. Обновление через WebUI (OTA).**

Пакет `.okp` собирается на хосте (`tools/native/make_oko_update.sh
--version 0.3.0`) и загружается в WebUI → «Система» → «Обновление ПО»
(файлом или по `http(s)://`). Плата проверяет sha256 и размеры, готовит
`sd_update.txt` и образы на microSD, а после подтверждения `APPLY` и
перезагрузки U-Boot прошивает `boot`, `oem`, `rootfs`. Раздел `/userdata` и
`env` не затрагиваются; оставленная в слоте карта после успешной загрузки не
перепрошивает плату. Полный цикл проверен на стенде 2026-11-10; подробности —
`TESTBENCH.md` §7E и `project/app/divinus/README.md`.

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
| VPN: WireGuard / AmneziaWG | собрано (модуль, `wg`/`awg`, API, WebUI); полный прогон с пиром — в `bench/vpn_check.py`, статус «ограничено» | WebUI `Network` → VPN, `/api/vpn`, `/usr/bin/oko-vpn`, профили в `/userdata/vpn` |

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

VPN (Network → VPN) импортирует готовый `.conf` файлом или текстом и хранит
профили в `/userdata/vpn` (не более двух: `wg0` для WireGuard, `awg0` для
AmneziaWG). Конфигурацию применяет `/usr/bin/oko-vpn`, а не `wg-quick`:
скриптовые хуки `PreUp`/`PostUp`/`PreDown`/`PostDown` отклоняются, маршруты
строятся по `AllowedIPs`, для `0.0.0.0/0` включается guard с откатом default
route через 15 с, автозапуск выполняет `S45oko-vpn` с watchdog. Модуль
`amneziawg.ko` (AWG v3.1) грузится по требованию; встроенный WireGuard ядра
не менялся. Полная прошивка `update.img` стирает профили вместе с `/userdata`.

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
oko-ec flash app-1.0.1.bin 0x00010001   # OTA STM32 через загрузчик
oko-ec reset                # перезагрузка STM32
```

Адрес шины, адреса устройств, шунт и тайминги переопределяются опциями
`--dev`, `--ec-addr`, `--ina-addr`, `--ina-channel`, `--sht-addr`,
`--shunt-mohm`, `--delay-us`, `--retries` (шина также берётся из `OKO_EC_DEV`,
шунт — из `OKO_EC_SHUNT_MOHM`, канал INA3221 — из `OKO_EC_INA_CHANNEL`).
Для демона канал задаётся переменной `INA_CHANNEL` в `S21oko-ec`.

Исходники прошивок контроллеров лежат в git subtree:
`project/app/stm32_acpi-ec/` (`spectrespc/stm32_acpi-ec`, отдельная
CMake-сборка) и `project/app/stm8-motor/` (`spectrespc/stm8s103_i2c_motor`,
SDCC). Правки отправляются `tools/stm32_push.sh "сообщение"` /
`tools/stm8_push.sh "сообщение"`, обновление — `tools/stm32_pull.sh` /
`tools/stm8_pull.sh`; подробности в `project/app/STM32_FIRMWARE.md` и
`project/app/STM8_FIRMWARE.md`.

Демон запускается `S21oko-ec` при наличии `/dev/i2c-3` и утилиты; opt-out —
`/userdata/oko-ec.disable`. Он пишет `/tmp/oko-ec.json` (полные данные) и
`/tmp/oko-ec.osd` (`TS`/`OK`/`SHT`/`EC`/`INA`/`T`/`BAT`) каждые 2 с. `oko-osd`
из свежего файла (≤ 30 с) выводит четыре строки: SHT21 (температура и
влажность), STM32 (напряжение, ток, температура, SOC, ячейки), INA3221/INA226
(напряжение, ток, мощность) и LTE/WiFi. Поле Wi-Fi не зависит от демона: это
реальный RSSI STA из `wpa_cli` (`WiFi <n>/5 <dBm>dBm`), `wifi ap` при
работающем SoftAP, `wifi off` без ассоциации и без AP или полностью скрытая
строка, если `wlan0` отсутствует; LTE — декоративная симуляция. Без демона
или при отсутствии всех датчиков OSD работает на прежней симуляции T/BAT.


## Фокус и прошивка контроллеров

`divinus` получил помощь фокусировке (AF-статистика `librkaiq`, ROI в базисных
пунктах, мотор STM8 на `0x60`) и фоновую прошивку контроллеров STM32/STM8 с
WebUI. API: `/api/focus`, `/api/flash`, `/api/firmware`; конфиг — секции
`[focus]` и `[flash]` в `divinus.yaml`. Команды стенда:

```sh
bench/run.sh focus_check.py --enable --seconds 20 --snapshot
bench/run.sh firmware_check.py
bench/run.sh firmware_check.py --negative
```

Прошивка STM8 собирается отдельным образом (SDCC нет в основном builder):

```sh
docker build -t oko-stm8-sdcc tools/stm8-sdcc
docker run --rm -v "$PWD:/work" -w /work/project/app/stm8-motor \
  oko-stm8-sdcc make -f Makefile.stm8 all
```

Разметка STM8: boot `0x8000–0x89FF`, заголовок `0x8A00`, приложение
`0x8A10–0x9FFF`; первая запись — `factory-2.1.1.ihx` по ST-Link, дальше
`oko-motor flash`/`/api/flash`. Версия прошивки берётся из имени файла
(`app-1.0.1.bin`, `app-2.1.1.ihx`); безымянные `app.ihx`/`app.bin` — только
для ST-Link.

Проверено на стенде 2026-11-06: A6 пройден (`status=running`,
`cells_used=225`, `metric_avg` 527k–530k за 20 с) при
`af_v31.TuningPara.contrast_af.enable=1` в IQ SC230AI; ROI меняет метрику.
STM8: upstream `2.0.0` заменён `factory-2.1.1.ihx` через ST-Link, загрузчик
I2C, `enter-boot`/`info`/`boot-reset` и обновление `app-2.1.1.ihx` через
`/api/flash` работают (версия из имени сверяется с заголовком OKO8),
приложение стартует после power-cycle. STM32 (CBT6): реальная прошивка
проверена — WebUI `app-1.0.1.bin` → `/api/firmware` `1.0.1`, rollback
`app-1.0.0.bin` → `1.0.0` (точечно), `img_size=2324`, `flags=0`, телеметрия
и reboot. Открытые пункты — `KNOWN_ISSUES.md` (KI-032…035) и
`FEATURES_UNVERIFIED.md`.

## Документация проекта

| Файл | Содержание |
|---|---|
| `STRUCTURE.md` | структура репозитория: папки, ключевые файлы, назначение |
| `INIT_ORDER.md` | порядок инициализации: U-Boot → ядро → busybox init → модули → `divinus` |
| `TESTBENCH.md` | паспорт тестового стенда: железо, сеть, учётки, прошивка, диагностика |
| `FEATURES.md` | точка входа в описание функционала: паспорт платформы, индекс по файлам статусов и краткие команды проверки |
| `FEATURES_VERIFIED.md` | подтверждённые функции: видео, сеть, `oko-ec`, WebUI/API, microSD |
| `FEATURES_UNVERIFIED.md` | ограниченные и заблокированные возможности, непроверенные комбинации |
| `VERIFIED_FUNCTIONS.md` | реестр проверенных на стенде функций: статус, доказательство, ограничения |
| `KNOWN_ISSUES.md` | известные ошибки и ограничения: критичные, функциональные, прошивка, безопасность, инструменты |
| `CHANGELOG.md` | история изменений OKO-форка (Keep a Changelog + SemVer) |
| `bench/README.md` | утилиты стенда: UART/SSH-команды, проверка OSD по RTSP, фокус и прошивка контроллеров, venv |
| `research/README.md` | архив внешних исследований: правила оформления тем, индекс и read-only снимки upstream-файлов с провенансом |
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
  30 fps); Wi-Fi в телеметрии — реальный RSSI STA (`wpa_cli`) или `wifi ap`
  при работающем SoftAP, `wifi off` без подключения и без AP, без адаптера
  поле скрыто; LTE и GPS — симуляция.
  `rkipc` вырезан из образа (`oko-prune.list`), поэтому стример
  один. Подробности — `INIT_ORDER.md`, `TESTBENCH.md` и
  `project/app/divinus/README.md`.

## Важные примечания

- **Не копируйте дерево под Windows.** В репозитории есть симлинки
  (`.BoardConfig.mk`, `build.sh`, `rkflash.sh`, `config/*`), которые теряются
  на NTFS/FAT. Используйте Linux или WSL с ext4.
- **`IMAGE/` содержит актуальный релиз** под активную конфигурацию
  `RV1106 Pro Max` + SC230AI; устаревший артефакт `RV1103 Pico Mini` удалён
  2026-11-08. Источник истины — `.BoardConfig.mk` и `output/`.
- `output/`, `IMAGE/`, `config/` и объектные файлы (`*.o`, `*.ko`) исключены из
  Git — это артефакты, а не исходники.
- Сборка идёт на хосте. Сетевые сервисы TFTP/NFS для netboot запускает
  контейнер `services/` (`docker compose`, host network); контейнер сборки
  прошивки больше не используется.