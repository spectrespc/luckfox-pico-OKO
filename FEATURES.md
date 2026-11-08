# Поддерживаемый функционал OKO

Точка входа в описание возможностей прошивки OKO для Luckfox Pico Pro Max
(RV1106, SPI NAND, Buildroot) с сенсором SC230AI: паспорт платформы, индекс по
файлам статусов проверки и краткие команды проверки.

Статус на 2026-11-07. Пометки: **работает** — проверено на стенде;
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
| NPU-стек | драйвер `rknpu` 0.9.8 (`20240828`, non-IOMMU), мини-рантайм `librknnmrt.so` 1.6.0; опционально второй `librknnmrt2.so` 2.3.2 (`yolo.runtime: 2`), RGA-конверсия NV12→RGB888 по fd (`yolo.convert: rga`) и рамки детекции на видеопотоке (`yolo.boxes`, 2BPP RGN, два цвета) |
| Сборка | нативная на Debian 13 (`tools/native/setup_host.sh`, `./build.sh firmware`), результат — `output/image/update.img` |

### Как проверить

```sh
# матрица API и WebUI на стенде
bench/run.sh api_check.py
bench/run.sh ui_check.py            # нужен Playwright + Chromium

# помощь фокусировке и прошивка контроллеров
bench/run.sh focus_check.py --enable --seconds 20 --snapshot
bench/run.sh firmware_check.py --negative

# статус платы
curl -u admin:oko-change-me http://192.168.52.97/api/status
```

Полный набор команд — `TESTBENCH.md` §8 и `VERIFIED_FUNCTIONS.md` §3; процедура
прошивки и адреса стенда — `TESTBENCH.md`.

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
- VPN: WireGuard (`type=wg`, встроенный модуль ядра), AmneziaWG (`type=awg`,
  out-of-tree `amneziawg.ko`, `RK_ENABLE_AWG=y`) и OpenVPN-клиент (`type=ovpn`,
  пакет `openvpn 2.5.7` + `CONFIG_TUN=y`) применяются своим POSIX-хелпером
  `/usr/bin/oko-vpn`; `wg-quick`/`awg-quick` и скриптовые хуки не используются.
  Профили — `/userdata/vpn/*.conf` (chmod 600), по одному на интерфейс
  (`wg0`, `awg0`, `tun0`), autostart — `S45oko-vpn` + watchdog. OpenVPN
  клиентский: `dev tap`, `plugin`, `management`, внешние скрипты и
  `auth-user-pass <файл>` отклоняются; `auth-user-pass` через WebUI
  сохраняется в `/userdata/vpn/<name>.pass` (0600) и не отдаётся API.
  `redirect-gateway` принудительно получает `def1`; `AllowedIPs` дают
  split-tunnel, для `0.0.0.0/0`/`::/0` (и OpenVPN full tunnel) включается
  guard с откатом default route через 15 с. Приватные ключи не отдаются в
  `GET /api/vpn`; `GET /api/vpn/export` скачивает файл целиком (как
  `/api/log`). Статус проверки — в `FEATURES_VERIFIED.md` (WG/OpenVPN) и
  `FEATURES_UNVERIFIED.md` (AWG).
- Ограничения и непроверенные комбинации потоков, YOLO, датчиков и microSD
  собраны в `FEATURES_UNVERIFIED.md`.
- Помощь фокусировке, STM8/STM32, WebUI и прочие подтверждённые функции —
  в `FEATURES_VERIFIED.md`; ограничения — в `FEATURES_UNVERIFIED.md`; открытые
  дефекты и обходы — в `KNOWN_ISSUES.md`.

## English

Entry point for OKO capabilities on the Luckfox Pico Pro Max (RV1106, SPI NAND,
Buildroot) with the SC230AI sensor: platform passport, an index of the
verification-status files and short verification commands.

Status as of 2026-11-07. Markers: **works**, **limited**, **blocked**.

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
H.265/H.264, JPEG, RTSP, WebUI. NPU stack: `rknpu` driver 0.9.8 (`20240828`,
non-IOMMU) with the `librknnmrt.so` 1.6.0 mini-runtime and an optional second
`librknnmrt2.so` 2.3.2 (`yolo.runtime: 2`), optional RGA NV12→RGB888
over dma-buf fds (`yolo.convert: rga`) and detection boxes on the stream
(`yolo.boxes`, 2BPP RGN, two colours). Build: native on Debian 13
(`./build.sh firmware`), output `output/image/update.img`.

Shared notes: see the Russian section above; open defects are in
`KNOWN_ISSUES.md`, and unverified stream combinations, YOLO, sensors and
microSD limits are in `FEATURES_UNVERIFIED.md`. A real STM32 update (CBT6,
WebUI `app-1.0.1.bin` → rollback `app-1.0.0.bin`, dotted `1.0.0` in
`/api/firmware`) is verified. Flashing procedure and bench addresses are in
`TESTBENCH.md`; evidence is in `VERIFIED_FUNCTIONS.md`.