# Поддерживаемый функционал OKO

Точка входа в описание возможностей прошивки OKO для Luckfox Pico Pro Max
(RV1106, SPI NAND, Buildroot) с сенсором SC230AI: паспорт платформы, разделение
функционала по статусу проверки, общие примечания и команды проверки.

Статус на 2026-11-06. Пометки: **работает** — проверено на стенде;
**ограничено** — работает с оговоркой; **заблокировано** — отказ с причиной.

| Файл | Содержание |
|---|---|
| `FEATURES_VERIFIED.md` | только подтверждённые функции и возможности (**работает**) |
| `FEATURES_UNVERIFIED.md` | проверенные не полностью (**ограничено**) и непроверенные или заблокированные (**заблокировано**) |
| `VERIFIED_FUNCTIONS.md` | подробные доказательства, ограничения и команды воспроизведения |
| `project/app/divinus/source/doc/README.md` | результаты гейтов медиастека |

## Русский

### Паспорт платформы

| Параметр | Значение |
|---|---|
| Плата / SoC | Luckfox Pico Pro Max, Rockchip RV1106 (Cortex-A7 + NPU + ISP) |
| Сенсор | SC230AI, режимы 1080p30 и 480p120 |
| Загрузка | SPI NAND 256 МБ, U-Boot → ядро 5.10.160 → busybox init |
| Корневая ФС | Buildroot |
| Разделы | `env` 256K, `idblock` 256K, `uboot` 512K, `boot` 6M, `oem` 40M, `userdata` 40M, `rootfs` 168M |
| Активный DTS | `rv1106g-luckfox-pico-pro-max-sc230ai.dts` |
| Медиастек | `divinus` (OpenIPC), H.265/H.264, JPEG, RTSP, WebUI |
| Сборка | Docker (`./start.sh`), результат — `output/image/update.img` |

### Как проверить

```sh
# матрица API и WebUI на стенде
bench/run.sh api_check.py
bench/run.sh ui_check.py            # нужен Playwright + Chromium

# статус и потоки
curl -u admin:oko-change-me http://192.168.52.97/api/status
curl -u admin:oko-change-me http://192.168.52.97/api/streams

# сеть и накопитель
curl -u admin:oko-change-me http://192.168.52.97/api/network
curl -u admin:oko-change-me http://192.168.52.97/api/storage

# VPN: профили, живое состояние, импорт из .conf (Network → VPN)
curl -u admin:oko-change-me http://192.168.52.97/api/vpn
# полный прогон с пиром на хосте: bench/README.md, раздел «VPN-пир на хосте»
bench/run.sh vpn_check.py --peer-host 192.168.52.1 \
  --profile output/bench/vpn/awg0.conf

# архив, телеметрия и NTP
curl -u admin:oko-change-me http://192.168.52.97/api/recordings
curl -u admin:oko-change-me http://192.168.52.97/api/telemetry
curl -u admin:oko-change-me http://192.168.52.97/api/time

# RTSP (пути /live/0 и /live/1)
ffprobe -v error -rtsp_transport tcp \
  rtsp://admin:oko-change-me@192.168.52.97:554/live/0

# I2C и телеметрия на устройстве
i2cdetect -y 3
oko-ec sensors
cat /tmp/oko-ec.osd

# NPU-аналитика
curl -u admin:oko-change-me http://192.168.52.97/api/yolo
curl -u admin:oko-change-me http://192.168.52.97/api/models
cat /proc/rknpu/load
df -h /userdata

# помощь фокусировке и прошивка контроллеров (проверено на стенде 2026-11-06)
bench/run.sh focus_check.py --enable --seconds 20 --snapshot --out output/bench/focus
bench/run.sh firmware_check.py
bench/run.sh firmware_check.py --negative
curl -u admin:oko-change-me 'http://192.168.52.97/api/focus?map=1'
curl -u admin:oko-change-me http://192.168.52.97/api/flash
curl -u admin:oko-change-me http://192.168.52.97/api/firmware

# STM8: сборка прошивки и образы (SDCC в отдельном контейнере)
docker build -t oko-stm8-sdcc tools/stm8-sdcc
docker run --rm -v "$PWD:/work" -w /work/project/app/stm8-motor \
  oko-stm8-sdcc make -f Makefile.stm8 all
```

Процедура прошивки и адреса стенда — в `TESTBENCH.md`; подробные доказательства,
снимки экрана и команды воспроизведения — в `VERIFIED_FUNCTIONS.md`.

### Общие примечания

- Открытые дефекты, ограничения и обходы — `KNOWN_ISSUES.md`.
- `/userdata` после переразметки — UBIFS-том `ubi5_0` около 29 МиБ (свободно
  ~28 МиБ): квота загрузки 3 МиБ на модель достижима, помещается несколько
  моделей. Раздел `userdata` расширен с 10 до 40 МБ за счёт `rootfs`
  (198→168 МБ); прошивка — только полным `update.img`.
- Ручная правка YAML санитизируется при старте: HDR откатывается,
  `osd.scope=all` приводится к `main`.
- Шрифт OSD — ASCII без кириллицы; текст OSD пишется латиницей.
- Полный `update.img` перезаписывает `env.img`: MAC восстанавливается из serial,
  статические настройки нужно задать заново. Покомпонентная прошивка
  `oem`/`rootfs` env не трогает.
- I2C3-M1 работает на 100 кГц — это требование SHT21/INA3221/STM32 EC.
- Фабричные учётные данные — `admin` / `oko-change-me`; пароль нужно сменить до
  подключения камеры к недоверенной сети (см. `TESTBENCH.md` §6).
- WebUI использует HTTP Basic: сессии и Logout нет (`/exit` останавливает
  сервер), HTTPS не поддерживается, пароли хранятся в `divinus.yaml` открытым
  текстом. Пароли RTSP/ONVIF/WebUI не читаются через API.
- Конфиг, который не разбирается при старте, откатывается из
  `divinus.yaml.bak` (один раз); импорт из WebUI сохраняет предыдущий файл как
  `.bak`.
- Статика отдаётся только с `/mnt/sdcard/`; для файлов вне карты сервер
  возвращает 404 (раньше отдавал любой абсолютный путь).
- В `divinus` нельзя использовать макросы ctype (`isalnum`, `isspace`):
  `compat.c` определяет `__ctype_b` и перекрывает таблицу uClibc — процесс
  падает. Проверки символов делаются ASCII-сравнениями.
- VPN: WireGuard (`type=wg`, встроенный модуль ядра) и AmneziaWG (`type=awg`,
  out-of-tree `amneziawg.ko`, `RK_ENABLE_AWG=y`) применяются своим POSIX-хелпером
  `/usr/bin/oko-vpn`; `wg-quick`/`awg-quick` и скриптовые хуки не используются.
  Профили — `/userdata/vpn/*.conf` (chmod 600), не более двух (`wg0`/`awg0`),
  autostart — `S45oko-vpn` + watchdog. `AllowedIPs` дают split-tunnel; для
  `0.0.0.0/0`/`::/0` включается guard с откатом default route через 15 с.
  Приватные ключи не отдаются в `GET /api/vpn`; `GET /api/vpn/export` скачивает
  файл целиком (как `/api/log`). Статус проверки — в
  `FEATURES_UNVERIFIED.md`.
- Ограничения и непроверенные комбинации потоков, YOLO, датчиков и microSD
  собраны в `FEATURES_UNVERIFIED.md`.
- Помощь фокусировке (`/api/focus`, `[focus]`) проверена на стенде: A6
  пройден, `metric_avg` 527k–530k за 20 с, ROI меняет метрику. Для SC230AI
  обязателен `af_v31.TuningPara.contrast_af.enable=1` в IQ. STM8: upstream
  `2.0.0` заменён `factory-2.0.1.ihx` через ST-Link, загрузчик I2C, обновление
  `app-2.0.1.ihx` через WebUI (версия из имени сверяется с заголовком OKO8) и
  восстановление после обрыва проверены. STM32: реальная прошивка проверена
  (CBT6, WebUI `app-1.0.1.bin` → rollback `app-1.0.0.bin`; `/api/firmware`
  показывает точечно `1.0.0`; `img_size=2324`, `flags=0`, телеметрия и
  reboot). См. `FEATURES_VERIFIED.md`,
  `FEATURES_UNVERIFIED.md` и `KNOWN_ISSUES.md` (KI-032…036).

## English

Entry point for OKO capabilities on the Luckfox Pico Pro Max (RV1106, SPI NAND,
Buildroot) with the SC230AI sensor: platform passport, feature split by
verification status, shared notes and verification commands.

Status as of 2026-11-06. Markers: **works**, **limited**, **blocked**.

| File | Content |
|---|---|
| `FEATURES_VERIFIED.md` | confirmed features only (**works**) |
| `FEATURES_UNVERIFIED.md` | partially verified (**limited**) and unverified or blocked (**blocked**) features |
| `VERIFIED_FUNCTIONS.md` | detailed evidence, limitations and reproduction commands |
| `project/app/divinus/source/doc/README.md` | media stack gate results |

Platform: Luckfox Pico Pro Max (RV1106), SC230AI, 256 MB SPI NAND, Buildroot,
kernel 5.10.160. Partitions: `env` 256K, `idblock` 256K, `uboot` 512K, `boot` 6M,
`oem` 40M, `userdata` 40M, `rootfs` 168M. Active DTS:
`rv1106g-luckfox-pico-pro-max-sc230ai.dts`. Media stack: `divinus` (OpenIPC),
H.265/H.264, JPEG, RTSP, WebUI. Build: Docker (`./start.sh`), output
`output/image/update.img`.

Shared notes: see the Russian section above; open defects are in
`KNOWN_ISSUES.md`, and unverified stream combinations, YOLO, sensors and
microSD limits are in `FEATURES_UNVERIFIED.md`. A real STM32 update (CBT6,
WebUI `app-1.0.1.bin` → rollback `app-1.0.0.bin`, dotted `1.0.0` in
`/api/firmware`) is verified. Flashing procedure and bench addresses are in
`TESTBENCH.md`; evidence is in `VERIFIED_FUNCTIONS.md`.