# Поддерживаемый функционал OKO

Сводка возможностей прошивки OKO для Luckfox Pico Pro Max (RV1106, SPI NAND,
Buildroot) с сенсором SC230AI: паспорт платформы и одна таблица «функционал —
управление — вклад OKO — статус проверки». Доказательства и команды — в
`VERIFIED_FUNCTIONS.md`, результаты гейтов медиастека — в
`project/app/divinus/source/doc/README.md`.

Статус на 2026-11-05. Пометки: **работает** — проверено на стенде;
**ограничено** — работает с оговоркой; **заблокировано** — отказ с причиной.

## Русский

### Паспорт платформы

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

### Сводная таблица функционала

| Группа | Функционал | Описание | Управление (API, секция YAML) | Что добавлено (OKO vs вендорский V1.4) | Проверено и работает |
|---|---|---|---|---|---|
| Платформа и образ | Разметка SPI NAND | `boot` 6M, `oem` 40M, `userdata` 10M, `rootfs` 198M; нужна полная прошивка `update.img` | `update.img`, BoardConfig | `boot` 4→6M, `oem` 30→40M, `rootfs` 210→198M | **работает** — `/proc/mtd`: `00600000`, `02800000`, `0c600000` |
| Платформа и образ | Сенсоры SC230AI-only | В образе только `sc230ai.ko` и IQ SC230AI; DTS выключает `sc3336`/`mis5001` | BoardConfig, `rv1106-sc230ai.config`, активный DTS | DTS `rv1106g-...-sc230ai.dts`, фрагмент ядра, `insmod_ko.sh` | **работает** — в `/oem/usr/ko` только `sc230ai.ko` |
| Платформа и образ | Состав `oem` | Удалены `sample_*`/`simple_*`; диагностические `rk_*_test`/`rk_mpi_*_test` сохранены | `make oem`, BoardConfig | список удаления в сборке oem | **работает** — `sample_*`/`simple_*` отсутствуют |
| Платформа и образ | AIISP-модель | В `/oem/usr/lib` только модель из `RK_AIISP_MODEL` | `RK_AIISP_MODEL=rkmodel_1080_1920.aiisp` | копирование модели до `build_mkimg oem` | **работает** — модель и `librkpostisp.so` в образе |
| Платформа и образ | Buildroot-утилиты | `fw_printenv`/`fw_setenv`, `mount`/`fdisk`/`sfdisk`/`wipefs` | `fw_printenv`, `/etc/fw_env.config` | `BR2_PACKAGE_UBOOT_TOOLS*`, `UTIL_LINUX_WIPEFS` | **работает** — env читается, карта форматируется |
| Платформа и образ | Сборка образа | Docker-сборка, результат `output/image/update.img` | `./start.sh`, `.BoardConfig.mk` | Docker-окружение: `Dockerfile`, compose, entrypoint | **работает** — образ собран и прошит |
| Видео и стриминг | RTSP основной поток | `/live/0`, H.265 1920x1080 30 fps, ~4.7 Мбит/с | RTSP 554, `[mp4]` | `divinus` вместо `rkipc`; 1080p30, битрейт 4096 кбит/с | **работает** — PyAV: hevc 1920x1080 |
| Видео и стриминг | RTSP второй поток | `/live/1`, H.265 640x360; выключение отдаёт 404 | `/api/substream`, `[substream]` | второй поток, soft-stop, рестарт `main → sub → JPEG` | **работает** — 30 кадров; 404 после disable |
| Видео и стриминг | MJPEG | Поток для браузеров без HEVC | `/api/mjpeg` | 30 fps в `divinus` | **работает** — `multipart`, 1.3 МБ за 4 с |
| Видео и стриминг | MP4-превью | `/video.mp4`; браузер должен поддерживать HEVC | `/api/mp4` | исправлен пустой chunk, подсказка HEVC | **работает** — 30 кадров hevc 1920x1080 |
| Видео и стриминг | JPEG-снимок | Кадр по запросу, без OSD | `/api/jpeg` | `divinus` вместо `rkipc` | **работает** — endpoint отвечает |
| Видео и стриминг | ePTZ | Цифровой зум 1x/2x кропом VPSS | `/api/eptz`, `[eptz]` | ePTZ stage `divinus` | **работает** — `zoom=2` даёт кроп 960x540 |
| Видео и стриминг | Поворот | 90/180/270 через VENC | `/api/rotate`, `[rotate]` | поворот VENC stage `divinus` | **ограничено** — проверен 90°; 180/270 без отдельных прогонов |
| Видео и стриминг | Кодировщик | SVC, motion deblur, QP на живом канале | `/api/venc`, `[venc]` | SVC/deblur/QP stage `divinus` | **работает** — поток без рестарта |
| Видео и стриминг | SmartP | Настройка GOP | `/api/venc`, `[venc]` | SmartP stage `divinus` | **ограничено** — применяется со следующего старта, не перепроверялся |
| Видео и стриминг | AIISP | Ночная шумоподавка по модели в `/oem/usr/lib` | `/api/aiisp`, `[aiisp]` | модель в oem, API stage `divinus` | **работает** — `librkpostisp.so` загружен, поток идёт |
| Видео и стриминг | IVS | Детекция движения/объектов, снимок, клип, OSD | `/api/ivs`, `[ivs]` | IVS stage `divinus` + `gating.c` | **ограничено** — MD проверен; OD и событие на статичной сцене не подтверждены |
| Видео и стриминг | Runtime IQ | `switch_scene`, `updateIq` | `/api/iq`, `[iq]` | runtime IQ stage `divinus` | **ограничено** — сцена принята; `updateIq` не проверялся |
| Видео и стриминг | Запись | H.265 на смонтированную microSD с квотой и чисткой сегментов; сегмент может превысить квоту до ротации | `/api/record`, `[record]`, `record.max_bytes`, `record.min_free_mb` | `record.c`: `nal_split`, квота, `storageBusy` | **работает** — 150 кадров hevc 1080p декодируются |
| Видео и стриминг | OSD | Время, GPS, T/BAT/LTE/WiFi, система, сообщения | `/api/osd/{id}`, `[osd]`, `oko-osd`, `/tmp/osd.fifo` | `reg0`–`reg4`, `region.c` `@file`, HAL-патч RV1106, `S20oko-osd` | **работает** — регионы видны в RTSP main |
| Видео и стриминг | HDR2/HDR3 | HDR на SC230AI | `/api/hdr` | HDR API + `gating.c`; экспериментальный HDR_X2 выключен | **заблокировано** — `librkaiq` не поддерживает SC230AI |
| Видео и стриминг | OSD на всех потоках | `osd.scope=all` (main+sub+JPEG) | конфиг `[osd]`, гейт G4 | `gating.c` | **заблокировано** — multi-attach RGN не прошёл проверку |
| WebUI/API | WebUI | Geometry, Encoding, Night image, Image, Analytics, Sub stream, Streams, Storage, Network | `http://<DUT>/` | WebUI `divinus`; Storage и Network расширены OKO | **работает** — `GET /` → HTTP 200 |
| WebUI/API | HTTP API | status, isp, time, night, streams, storage, network + медиаэндпоинты | `/api/*`, `project/app/divinus/source/doc/endpoints.md` | сети/хранилище: mount, unmount, format, record, osd | **работает** — основные endpoint'ы отвечают |
| WebUI/API | Аутентификация | WebUI и RTSP, смена фабричного пароля | `[web]`, `[rtsp]`, `web_auth_skiplocal=off` | включена в фабричном `divinus.yaml` | **работает** — без учётных данных запрос отклоняется |
| Сеть | DHCP | Один клиент `dhcpcd`, client ID по MAC, без DUID | `/api/network?mode=dhcp` | `S41dhcpcd` — заглушка, `oko-net apply` | **работает** — IP держится после reboot |
| Сеть | Статический IPv4 | Адрес, маска, шлюз, DNS | WebUI Network → Ethernet, `/api/network?mode=static` | `/api/network` и `oko-net` | **работает** — `.220` применяется и переживает reboot |
| Сеть | Постоянный MAC | Выводится из serial SoC и хранится в env | `fw_printenv oko_mac` | `oko_mac`, восстановление из serial | **работает** — MAC не меняется после прошивки |
| Сеть | Сохранение настроек | `oko_net_mode`, `oko_ipaddr`, `oko_netmask`, `oko_gateway`, `oko_dns` в U-Boot env | `fw_setenv`, `/etc/fw_env.config` | перенос настроек из `/userdata` в env | **работает** — переживает reboot и прошивку `oem`/`rootfs` |
| Сеть | WiFi AIC8800 | Модули собираются и копируются в `/oem/usr/ko` | `RK_ENABLE_WIFI=y` | вендорские модули без правок OKO | **ограничено** — WiFi-стек не проверялся; сеть проверена по Ethernet |
| Телеметрия (oko-ec) | `oko-ec` CLI и демон | Опрос датчиков раз в 2 с, JSON и строки OSD | `oko-ec`, `scan`/`info`/`battery`/`cells`/`temp`/`ina226`/`sht21`/`sensors`/`flash`/`reset`/`daemon`, `S21oko-ec` | пакет `project/app/oko-ec`, демон и opt-out | **работает** — демон запущен, `/tmp/oko-ec.json` обновляется |
| Телеметрия (oko-ec) | Датчики | SHT21 `0x40` — T/RH; INA3221 или INA226 `0x41` — U/I/P; STM32 EC `0x42` — U/I/T, SOC, ячейки, OTA | `/dev/i2c-3`, `i2cdetect -y 3` | `i2c-dev` без драйверов (`CONFIG_HWMON` выключен), автоопределение INA | **ограничено** — адреса видны; у INA3221 силовые входы не подключены |
| Телеметрия (oko-ec) | OSD-телеметрия | Реальные строки, иначе симуляция после 30 с без данных | `/tmp/oko-ec.osd`, `/userdata/oko-ec.disable` | `oko-osd` + `reg1` | **работает** — T/BAT из свежего файла |
| Телеметрия (oko-ec) | I2C3-M1 | Шина 100 кГц на `GPIO1_D2`/`GPIO1_D3` | `/dev/i2c-3`, DTS `&i2c3` | `status = "okay"`, `clock-frequency = <100000>`, `i2c3m1_xfer` | **работает** — `0x186a0`, 4 адреса на шине |
| microSD | Автомонтирование | Точка `/mnt/sdcard`, состояние `/run/oko-sd.state` | udev `60-oko-sdcard.rules` | `oko-sdcard`, DTS `&sdmmc` как съёмная карта | **работает** — `/api/storage`: `sd_mounted=true` |
| microSD | Hotplug | Извлечение и установка карты | udev, `/api/storage` | определение по `device/type=SD` | **ограничено** — проверено unbind/bind; физическое извлечение не проверялось |
| microSD | Формат карты | FAT32/MBR, метка `OKO`, одноразовый токен; блокируется при записи | `/api/storage?action=format`, `confirm=FORMAT`, `token` | mount/unmount/format в API и разделе Storage | **работает** — один раздел FAT32 |
| microSD | Unmount | Снятие `/mnt/sdcard`, запись блокируется | `/api/storage?action=unmount` | `storageBusy`, отказ recorder | **работает** — запись переходит в `blocked` |
| microSD | Запись только на карту | Только ниже `/mnt/sdcard`, SPI NAND не используется | `[record]`, `/api/record` | проверка монтирования раз в секунду и перед ротацией | **работает** — `/oem`, `/userdata`, `/tmp` и `/` не задействованы |

### Примечания

- Бюджет каналов VPSS — 4. Без IVS доступны main + sub + MJPEG + JPEG; с
  включённым IVS — main + sub + JPEG, MJPEG нужно выключить. Неподтверждённые
  комбинации `gating.c` отклоняет с причиной и подсказкой.
- Ручная правка YAML санитизируется при старте: HDR откатывается,
  `osd.scope=all` приводится к `main`.
- Шрифт OSD — ASCII без кириллицы; текст OSD пишется латиницей.
- Полный `update.img` перезаписывает `env.img`: MAC восстанавливается из serial,
  статические настройки нужно задать заново. Покомпонентная прошивка
  `oem`/`rootfs` env не трогает.
- I2C3-M1 работает на 100 кГц — это требование SHT21/INA3221/STM32 EC.
- Фабричные учётные данные — `admin` / `oko-change-me`; пароль нужно сменить до
  подключения камеры к недоверенной сети (см. `TESTBENCH.md` §6).

### Как проверить

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

Platform: Luckfox Pico Pro Max (RV1106), SC230AI, 256 MB SPI NAND, Buildroot,
kernel 5.10.160. Status as of 2026-11-05. Markers: **works**, **limited**,
**blocked**. Full evidence and commands: `VERIFIED_FUNCTIONS.md`.

| Feature | What was added | Verified & works |
|---|---|---|
| Platform, image and build | Partitions `boot` 6M, `oem` 40M, `userdata` 10M, `rootfs` 198M; SC230AI-only driver/IQ, demos pruned, one AIISP model, `fw_printenv`/util-linux; Docker build | **works** — `/proc/mtd` and `/oem` checks |
| Video streams | `divinus` instead of `rkipc`: RTSP `/live/0` 1080p H.265 and `/live/1` 640x360, MJPEG `/mjpeg`, MP4 `/video.mp4`, JPEG snapshot | **works** — PyAV and HTTP checks |
| ePTZ and rotation | VPSS crop 1x/2x; 90/180/270 rotation via VENC | **limited** — 90° verified, 180/270 not separately |
| Encoder tuning | SVC, motion deblur, QP on a live channel | **works** — stream keeps running |
| SmartP | GOP change | **limited** — applies on next start, not re-verified |
| AIISP | Night denoise with `rkmodel_1080_1920.aiisp` | **works** — `librkpostisp.so` loaded |
| IVS | MD/OD, snapshot, clip, OSD | **limited** — MD verified, OD and static-scene events unconfirmed |
| Runtime IQ | `switch_scene`, `updateIq` | **limited** — scene switch accepted, `updateIq` untested |
| OSD | Time, GPS, T/BAT/LTE/WiFi, system, messages; ASCII font only, no Cyrillic | **works** — regions visible in RTSP main |
| Recording | H.265 to microSD with quota and old-segment pruning | **works** — 150 frames decode as hevc 1080p |
| Blocked features | HDR2/HDR3 on SC230AI and `osd.scope=all`; VPSS has four channels, so MJPEG must be off while IVS is active | **blocked** — refused with a reason |
| WebUI and API | Geometry, Encoding, Night image, Image, Analytics, Sub stream, Streams, Storage, Network; `/api/*` endpoints; WebUI and RTSP authentication, change the factory password | **works** — `GET /` returns 200 |
| Network (Ethernet) | DHCP or static IPv4, one `dhcpcd` client (client ID by MAC, no DUID), persistent `oko_mac` and settings in U-Boot env; AIC8800 modules built | **works** — survives reboot; WiFi stack unverified |
| Telemetry (`oko-ec`) | SHT21 `0x40`, INA3221/INA226 `0x41`, STM32 ACPI-EC `0x42` on I2C3-M1 at 100 kHz; CLI, 2 s daemon, opt-out | **works** — daemon and `/tmp/oko-ec.osd` |
| microSD | udev automount at `/mnt/sdcard`, FAT32/MBR format with a one-time token, unmount, mount-only recording with quota and old-segment pruning | **limited** — hotplug verified by unbind/bind only |