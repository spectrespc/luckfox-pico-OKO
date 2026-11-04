# Поддерживаемый функционал OKO

Сводка возможностей прошивки OKO для Luckfox Pico Pro Max (RV1106, SPI NAND,
Buildroot) с сенсором SC230AI. Файл описывает, что поддерживается и как это
включить; доказательства и команды проверки — в `VERIFIED_FUNCTIONS.md`,
результаты гейтов медиастека — в `project/app/divinus/source/doc/README.md`.

Статус на 2026-11-04. Пометки: **работает** — проверено на стенде;
**ограничено** — работает с оговоркой; **заблокировано** — отказ с причиной.

## Русский

### 1. Платформа и образ

| Параметр | Значение |
|---|---|
| Плата / SoC | Luckfox Pico Pro Max, Rockchip RV1106 (Cortex-A7 + NPU + ISP) |
| Сенсор | SC230AI, режимы 1080p30 и 480p120 |
| Загрузка | SPI NAND 256 МБ, U-Boot → ядро 5.10.160 → busybox init |
| Корневая ФС | Buildroot |
| Разделы | `env` 256K, `idblock` 256K, `uboot` 512K, `boot` 6M, `oem` 40M, `userdata` 10M, `rootfs` 198M |
| Активный DTS | `rv1106g-luckfox-pico-pro-max-sc230ai.dts` |
| Медиастек | `divinus` (OpenIPC), H.265/H.264, JPEG, RTSP, WebUI |
| Сборка | Docker (`./start.sh`), результат — `output/image/update.img` |

В образ попадают только драйвер сенсора `sc230ai.ko` и IQ-файл
`sc230ai_CMK-OT2115-PC1_30IRC-F16.json`; модули и IQ остальных сенсоров
удаляются из `oem`. Демонстрационные `sample_*`/`simple_*` также удалены,
диагностические `rk_*_test`/`rk_mpi_*_test` сохранены. В `/oem/usr/lib`
устанавливается только модель AIISP, выбранная `RK_AIISP_MODEL`
(`rkmodel_1080_1920.aiisp`). В Buildroot включены `fw_printenv`/`fw_setenv`
(`uboot-tools`) и `util-linux` (`mount`, `wipefs`).

### 2. Видео и стриминг

| Возможность | Статус | Управление |
|---|---|---|
| RTSP основной поток `/live/0`, H.265 1920x1080 | работает | RTSP 554, обязательная аутентификация |
| RTSP второй поток `/live/1`, H.265 640x360 | работает | `/api/substream`, секция `[substream]` |
| MJPEG `/mjpeg` | работает | `/api/mjpeg` |
| MP4-превью `/video.mp4` | работает | `/api/mp4`; браузер должен поддерживать HEVC |
| JPEG-снимок | работает | `/api/jpeg` |
| ePTZ (цифровой зум 1x/2x, кроп VPSS) | работает | `/api/eptz`, секция `[eptz]` |
| Поворот 90/180/270 | работает через VENC | `/api/rotate`, секция `[rotate]` |
| SmartP, SVC, motion deblur, QP | работает; SmartP — со следующего старта | `/api/venc`, секция `[venc]` |
| AIISP (ночная шумоподавка) | работает при наличии модели в `/oem/usr/lib` | `/api/aiisp`, секция `[aiisp]` |
| IVS: детекция движения/объектов, снимок, клип, OSD | ограничено: MD проверен, OD реализован | `/api/ivs`, секция `[ivs]` |
| Runtime IQ: `switch_scene`, `updateIq` | ограничено: смена сцены принята, перезагрузка файла не проверялась | `/api/iq`, секция `[iq]` |
| Запись H.265 на microSD с квотой | работает | `/api/record`, секция `[record]` |
| OSD: время, GPS, температура, батарея, LTE, WiFi | работает для основного потока | `/api/osd/{id}`, `oko-osd` |
| HDR2/HDR3 | заблокировано на SC230AI | `/api/hdr` возвращает причину |
| OSD одновременно на main+sub+JPEG (`osd.scope=all`) | заблокировано | гейт G4 в `gating.c` |

Бюджет каналов VPSS — 4. Без IVS доступны main + sub + MJPEG + JPEG, с
включённым IVS — main + sub + JPEG, а MJPEG нужно выключить. Неподтверждённые
комбинации `gating.c` отклоняет с причиной и подсказкой; ручная правка YAML
санитизируется при старте (HDR откатывается, `osd.scope=all` приводится к
`main`). Шрифт OSD — ASCII без кириллицы.

### 3. WebUI и HTTP API

WebUI (`http://<DUT>/`) даёт доступ к секциям: Geometry, Encoding, Night image,
Image, Analytics, Sub stream, Streams, Storage и Network. API включает
`/api/status`, `/api/isp`, `/api/time`, `/api/night`, `/api/streams`,
`/api/storage`, `/api/network` и перечисленные выше медиаэндпоинты. Полный
список параметров — `project/app/divinus/source/doc/endpoints.md`.

Аутентификация WebUI и RTSP включена. Фабричные учётные данные — `admin` /
`oko-change-me`; пароль нужно сменить до подключения камеры к недоверенной сети
(см. `TESTBENCH.md` §6).

### 4. Сеть

| Возможность | Статус | Детали |
|---|---|---|
| DHCP для Ethernet | работает | один клиент `dhcpcd`, client ID по MAC, без DUID |
| Статический IPv4 | работает | WebUI Network → Ethernet, `/api/network` |
| Постоянный MAC | работает | `oko_mac` в env-разделе U-Boot, при первом старте выводится из serial SoC |
| Сохранение настроек между прошивками | работает | `oko_net_mode`, `oko_ipaddr`, `oko_netmask`, `oko_gateway`, `oko_dns` в U-Boot env |
| WiFi-модули AIC8800 | собираются и копируются в `/oem/usr/ko` | сетевой стек проверен по Ethernet |

Настройки применяет `/usr/bin/oko-net apply` при загрузке; vendor-скрипт
`S41dhcpcd` заменён заглушкой, чтобы DHCP-клиент был один. Полный `update.img`
перезаписывает `env.img`: MAC восстанавливается из serial, статические
настройки нужно задать заново. Покомпонентная прошивка `oem`/`rootfs` env не
трогает.

### 5. Питание и телеметрия (`oko-ec`)

`/oem/usr/bin/oko-ec` работает с тремя устройствами на I2C3-M1
(`/dev/i2c-3`, 100 кГц, `GPIO1_D2`/`GPIO1_D3`):

| Устройство | Адрес | Данные |
|---|---|---|
| SHT21 | `0x40` | температура, влажность |
| INA3221 или INA226 | `0x41` | напряжение, ток, мощность (INA3221 — 3 канала) |
| STM32 ACPI-EC | `0x42` | напряжение, ток, температура, SOC, ячейки, OTA |

Поддерживаются команды `scan`, `info`, `battery`, `cells`, `temp`, `ina226`,
`sht21`, `sensors`, `flash`, `reset` и режим `daemon`. Демон `S21oko-ec`
обновляет `/tmp/oko-ec.json` и `/tmp/oko-ec.osd` каждые 2 с; `oko-osd` выводит
реальные строки SHT21/STM32/INA3221 и переключается на симуляцию, если данных
нет дольше 30 с. Opt-out — файл `/userdata/oko-ec.disable`. Датчики читаются
через `i2c-dev`: `CONFIG_HWMON` в ядре выключен, отдельные драйверы не нужны.

### 6. microSD и запись

| Возможность | Статус | Детали |
|---|---|---|
| Автомонтирование | работает | udev-правило `60-oko-sdcard.rules`, точка `/mnt/sdcard`, состояние `/run/oko-sd.state` |
| Hotplug | ограничено | проверено программным unbind/bind `mmc1:aaaa`; физическое извлечение не проверялось |
| Формат карты FAT32/MBR | работает | `/api/storage?action=format`, подтверждение + одноразовый токен, метка `OKO` |
| Unmount | работает | `/api/storage`, recorder переходит в отказ |
| Запись с квотой | работает | `record.max_bytes`, `record.min_free_mb`, чистка старых сегментов |

Запись разрешена только при реально смонтированной карте и только ниже
`/mnt/sdcard`; файлы в `/oem`, `/userdata`, `/tmp` и `/` не создаются. Текущий
сегмент может превысить `max_bytes` до ротации. Unmount и формат блокируются
во время записи.

### 7. Ограничения

- HDR2/HDR3 не поддерживаются `librkaiq` на SC230AI — API возвращает
  `{"error":"blocked","reason":...}`; включать HDR вручную в YAML нельзя.
- OSD нельзя одновременно привязать к main, sub и JPEG (`osd.scope=all`).
- При включённом IVS MJPEG должен быть выключен: не хватает каналов VPSS.
- SmartP применяется только при следующем старте канала.
- Детекция движения зависит от сцены; на статичной сцене событие не возникло.
  OD-события на стенде не подтверждены.
- Запись и формат требуют физически установленной microSD.
- Статические сетевые настройки после полной прошивки задаются заново; MAC
  сохраняется.
- I2C3-M1 работает на 100 кГц — это требование SHT21/INA3221/STM32 EC.
- OSD-шрифт не содержит кириллицы.
- Фабричный пароль WebUI/RTSP необходимо сменить.

### 8. Как проверить

```sh
# статус и потоки
curl -u admin:oko-change-me http://192.168.52.97/api/status
curl -u admin:oko-change-me http://192.168.52.97/api/streams

# сеть и накопитель
curl -u admin:oko-change-me http://192.168.52.97/api/network
curl -u admin:oko-change-me http://192.168.52.97/api/storage

# RTSP (пути /live/0 и /live/1)
ffprobe -v error -rtsp_transport tcp \
  rtsp://admin:oko-change-me@192.168.52.97:554/live/0

# I2C и телеметрия на устройстве
i2cdetect -y 3
oko-ec sensors
cat /tmp/oko-ec.osd
```

Подробные доказательства, снимки экрана и команды воспроизведения — в
`VERIFIED_FUNCTIONS.md`; процедура прошивки и адреса стенда — в `TESTBENCH.md`.

## English

Summary of the supported functionality. Status as of 2026-11-04: **works** —
verified on the bench, **limited** — works with a caveat, **blocked** — refused
with a reason. Evidence: `VERIFIED_FUNCTIONS.md`.

- **Platform**: Luckfox Pico Pro Max (RV1106), SC230AI, 256 MB SPI NAND,
  Buildroot, kernel 5.10.160; partitions `boot` 6M, `oem` 40M, `userdata` 10M,
  `rootfs` 198M; Docker build produces `output/image/update.img`. Only the
  SC230AI driver and IQ file are shipped, and only the selected AIISP model is
  installed.
- **Video**: RTSP `/live/0` H.265 1920x1080 and `/live/1` H.265 640x360 (both
  with authentication), MJPEG `/mjpeg`, MP4 preview `/video.mp4`, JPEG snapshot,
  ePTZ 1x/2x, 90/180/270 rotation via VENC, SmartP/SVC/motion deblur/QP
  (SmartP applies on the next start), AIISP with `rkmodel_1080_1920.aiisp`, IVS
  (MD verified, OD implemented, not confirmed), runtime IQ scene switch,
  H.265 recording to microSD with quota, telemetry OSD.
- **Blocked**: HDR2/HDR3 on SC230AI and OSD on main+sub+JPEG
  (`osd.scope=all`); the API returns a reason and `gating.c` re-checks it.
  VPSS has four channels, so MJPEG must be off while IVS is active.
- **WebUI/API**: Geometry, Encoding, Night image, Image, Analytics, Sub stream,
  Streams, Storage and Network sections; `/api/status`, `/api/isp`, `/api/time`,
  `/api/night`, `/api/streams`, `/api/storage`, `/api/network`, media endpoints.
  Authentication is enabled; the factory `admin` / `oko-change-me` password must
  be changed.
- **Network**: DHCP or static IPv4, one `dhcpcd` client (client ID by MAC, no
  DUID), persistent MAC and network settings in the U-Boot env. A full flash
  rewrites `env.img`: the MAC is restored from the SoC serial, static settings
  must be re-applied. AIC8800 WiFi modules are built; networking is verified
  over Ethernet.
- **Telemetry (`oko-ec`)**: SHT21 `0x40`, INA3221/INA226 `0x41`, STM32 ACPI-EC
  `0x42` on I2C3-M1 at 100 kHz; CLI commands and a 2 s daemon feeding
  `/tmp/oko-ec.osd`; opt-out `/userdata/oko-ec.disable`.
- **microSD**: udev automount at `/mnt/sdcard`, FAT32/MBR format with a
  one-time token, unmount, recording quota and old-segment pruning. Writes are
  allowed only below `/mnt/sdcard`; recording and format require a mounted card.
- **Limitations**: no HDR on SC230AI, no multi-target OSD, SmartP applies on
  restart, motion events depend on the scene, OD events are unconfirmed,
  physical card extraction was not tested, and the OSD font has no Cyrillic.