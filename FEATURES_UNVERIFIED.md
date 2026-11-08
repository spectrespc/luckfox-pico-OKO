# Неподтверждённые функции OKO

Возможности, которые проверены не полностью (**ограничено**), заблокированы
причиной (**заблокировано**) или ещё не проверялись. Подтверждённые функции —
в `FEATURES_VERIFIED.md`; паспорт платформы, общие примечания и команды
проверки — в `FEATURES.md`; открытые дефекты и обходы — в `KNOWN_ISSUES.md`.

Статус на 2026-11-07. Пометки: **ограничено** — работает с оговоркой;
**заблокировано** — отказ с причиной.

## Русский

### Сводная таблица неподтверждённого функционала

| Группа | Функционал | Описание | Управление (API, секция YAML) | Что добавлено (OKO vs вендорский V1.4) | Статус |
|---|---|---|---|---|---|
| Видео и стриминг | SmartP | Настройка GOP | `/api/venc`, `[venc]` | SmartP stage `divinus` | **ограничено** — применяется со следующего старта, не перепроверялся |
| Видео и стриминг | IVS | Детекция движения/объектов, снимок, клип, OSD | `/api/ivs`, `[ivs]` | IVS stage `divinus` + `gating.c` | **ограничено** — MD проверен; OD и событие на статичной сцене не подтверждены |
| Видео и стриминг | Runtime IQ | `switch_scene`, `updateIq` | `/api/iq`, `[iq]` | runtime IQ stage `divinus` | **ограничено** — сцена принята; `updateIq` не проверялся |
| Видео и стриминг | HDR2/HDR3 | HDR на SC230AI | `/api/hdr` | HDR API + `gating.c`; экспериментальный HDR_X2 выключен | **заблокировано** — `librkaiq` не поддерживает SC230AI |
| Видео и стриминг | OSD на всех потоках | `osd.scope=all` (main+sub+JPEG) | конфиг `[osd]`, гейт G4 | `gating.c` | **заблокировано** — multi-attach RGN не прошёл проверку |
| Видео и стриминг | YOLO `source: vi1` | VI selfpath (chn1) как источник кадров | `/api/yolo`, `[yolo]` | `gating.c`, API и WebUI отклоняют значение | **заблокировано** — включение `vi1` привело к зависанию платы; значение отклоняется с причиной |
| Видео и стриминг | Второй рантайм RKNN 2.x | Модели toolkit 2.x, `yolo.runtime: 2` | `/api/yolo` (`runtime`), `/api/models` (`runtime`) | `librknnmrt2.so` с отдельным SONAME, два handle в HAL, проверка SDK-версии | **ограничено** — AIISP на 1.6.0 + YOLO на 2.x в одном процессе, переключение runtime без рестарта и 2-часовой soak проверены; mAP 2.x-модели на размеченном наборе не измерен |
| Сеть | WiFi AIC8800 | Модули собираются и копируются в `/oem/usr/ko` | `RK_ENABLE_WIFI=y` | вендорские модули без правок OKO | **ограничено** — WiFi-стек не проверялся; сеть проверена по Ethernet |
| Телеметрия (oko-ec) | Датчики | SHT21 `0x40` — T/RH; INA3221 или INA226 `0x41` — U/I/P; STM32 EC `0x42` — U/I/T, SOC, ячейки, OTA | `/dev/i2c-3`, `i2cdetect -y 3` | `i2c-dev` без драйверов (`CONFIG_HWMON` выключен), автоопределение INA | **ограничено** — адреса видны; у INA3221 силовые входы не подключены |
| microSD | Hotplug | Извлечение и установка карты | udev, `/api/storage` | определение по `device/type=SD` | **ограничено** — проверено unbind/bind; физическое извлечение не проверялось |
| Сеть | VPN — WireGuard | Профиль `wg0` из импортированного `.conf`: адреса, маршруты по `AllowedIPs`, split-tunnel, автозапуск и watchdog | `/api/vpn`, WebUI Network → VPN, `/usr/bin/oko-vpn`, `/userdata/vpn` | встроенный `CONFIG_WIREGUARD=y`, `wireguard-tools` 1.0.20210914 в `/oem/usr/bin/wg`, `oko-vpn` + `S45oko-vpn`, API и карточка WebUI | **ограничено** — на стенде 2026-11-06 проверены: handshake с loopback-пиром (`wg show`: 8 с, transfer 92/212 B), split-маршрут, API/WebUI, autostart+watchdog после reboot, guard positive/negative (default вернулся); внешний пир и `ping` через туннель не прогонялись |
| Сеть | VPN — AmneziaWG | Профиль `awg0`, параметры AWG v3.1, модуль загружается по требованию | `/api/vpn`, WebUI Network → VPN, `/usr/bin/oko-vpn` | out-of-tree `amneziawg.ko` (`v3.1.20260906`), `awg` (`v3.1.20260812`), `RK_ENABLE_AWG=y` | **ограничено** — модуль загружается (`dmesg`: `AmneziaWG 3.1.20260906`), handshake v3.1 с loopback-пиром прошёл со всеми параметрами (`I1/I5`, `HeaderProtectionKey`, `S3/S4`, `AdvancedSecurity`), guard positive/negative проверен; внешний пир не прогонялся |
| Прошивка | Обрыв питания при обновлении | Восстановление после потери питания STM32/STM8 | `flash.c`, загрузчики | `oko-flash`, I2C-загрузчик STM8 | **ограничено** — обрыв хостового `oko-motor flash` проверен (загрузчик жив, повторная загрузка успешна); полный обрыв питания не имитировался |

### Примечания

- **Бюджет каналов VPSS — 4.** Без IVS доступны main + sub + MJPEG + JPEG; с
  включённым IVS свободных каналов меньше. Проверено 2026-11-05: IVS + MJPEG
  (субпоток выключен) работают вместе; комбинация sub + MJPEG + IVS не
  проверялась. Неподтверждённые комбинации `gating.c` отклоняет с причиной и
  подсказкой.
- **YOLO.** `source: vpss3` занимает последний VPSS-канал и потому несовместим
  с IVS/MJPEG/четвёртым потоком — санитайзер переводит на `vi0`. Точность mAP на
  валидационном наборе не измерена; для другой модели подбирать
  `conf_threshold`. Основная функция YOLO подтверждена — см.
  `FEATURES_VERIFIED.md`.
- **AIISP.** Ночная точность шумоподавки не измерялась. Совместная работа AIISP
  и YOLO на `vpss3` проверена на стенде (демо 640×640: main 30–33 fps, NPU 28%,
  без пропусков).
- **RKNN 2.x.** Модель `yolov5n-640x384-r2.rknn` собрана toolkit 2.3.2 на тех же
  100 кадрах калибровки; активация, смешанная нагрузка и переключение рантайма
  без рестарта проверены. mAP на размеченном наборе не измерялся, поэтому
  «2.x не хуже 1.6.0» по точности пока не доказано.
- **Поворот и рамки.** Канва 0/90/180/270 и совмещение NN-рамок проверены
  `OKO_GEOMETRY_TESTS=1` 2026-11-07 (IoU 0.62/0.71/0.71), OSD-текст остаётся
  в своих углах. Эталонной разметки положения рамок на повёрнутой сцене нет:
  измерена воспроизводимость маппинга, а не абсолютная точность.
- **Помощь фокусировке.** AF-мост включает `RKAIQ_AF_MODE_FIXED` и
  `manual_meascfg_v31.af_en=1`; обязательное условие ненулевой метрики —
  `contrast_af.enable=1` в IQ SC230AI (в OKO включено в IQ и в рантайме).
- **STM8.** Загрузчик и приложение собираются SDCC 4.2 (`tools/stm8-sdcc`);
  актуальная прошивка — 2.1.1 (первичная запись по ST-Link/SWIM, обновление
  `app-2.1.1.ihx` через WebUI и `oko-motor flash`). Слот загрузчика — 2.5 КиБ
  (`0x8000–0x89FF`) при измеренном boot 2355 байт. Подробности и версии — в
  `FEATURES_VERIFIED.md` и `VERIFIED_FUNCTIONS.md`.
- **Ручное управление и home/zero.** Команды `move`/`SET_POS`, ручное
  управление focus/zoom и асинхронные `home`/`zero` покрыты `api_check.py`
  (`OKO_FOCUS_MOTOR_TESTS=1`), включая гейт версии и 409 при занятом job’е.
  Открытый остаток — пропуск шагов и поведение при упоре на настоящем
  объективе.
- **Автофокус.** Поиск по AF-метрике (`focus_af.c`) и micro-fine `af=fine`
  реализованы и покрыты хостовым тестом; на стенде 2026-11-07 обе ветки
  завершились `done`, но метрика от позиции мотора не зависит — объектив
  стенда оптически не связан с приводом, поэтому сходимость на реальной сцене
  не подтверждена. Компоновка WebUI проверена headless-Chromium на
  1440×900 и 390×844, но не на физическом телефоне.
- **VPN.** Модуль `amneziawg.ko` собран под 5.10.160 и загружается по требованию
  из `oko-vpn`; `wg`/`awg` собраны app-тулчейном (ELF ARM, uClibc). Парсер
  `.conf` отклоняет `PreUp/PostUp/PreDown/PostDown`, поддерживает AWG v3.1
  (`I1…I5`, `HeaderProtectionKey`, `AdvancedSecurity`) и проверен хостовым
  тестом. Полный прогон `bench/vpn_check.py` требует пира на хосте (WireGuard
  для `wg0`, AmneziaWG из вендорных исходников для `awg0`) и ещё не выполнялся;
  после него строки переходят в `FEATURES_VERIFIED.md`.
- **Датчики.** Адреса SHT21/INA/EC видны на `/dev/i2c-3`; у INA3221 силовые
  входы не подключены, поэтому ток/мощность могут быть недостоверны.

## English

Capabilities that are only partially verified (**limited**), blocked with a
reason (**blocked**) or not yet verified. Confirmed features are in
`FEATURES_VERIFIED.md`; the platform passport, shared notes and verification
commands are in `FEATURES.md`; open defects and workarounds are in
`KNOWN_ISSUES.md`.

Status as of 2026-11-07.

| Group | Feature | Description | Control (API, YAML section) | What was added (OKO vs vendor V1.4) | Status |
|---|---|---|---|---|---|
| Video and streaming | SmartP | GOP change | `/api/venc`, `[venc]` | SmartP stage in `divinus` | **limited** — applies on next start, not re-verified |
| Video and streaming | IVS | Motion/object detection, snapshot, clip, OSD | `/api/ivs`, `[ivs]` | IVS stage in `divinus` + `gating.c` | **limited** — MD verified; OD and static-scene events unconfirmed |
| Video and streaming | Runtime IQ | `switch_scene`, `updateIq` | `/api/iq`, `[iq]` | runtime IQ stage in `divinus` | **limited** — scene switch accepted, `updateIq` untested |
| Video and streaming | HDR2/HDR3 | HDR on SC230AI | `/api/hdr` | HDR API + `gating.c`; experimental HDR_X2 disabled | **blocked** — `librkaiq` does not support SC230AI |
| Video and streaming | OSD on all streams | `osd.scope=all` (main+sub+JPEG) | `[osd]` config, gate G4 | `gating.c` | **blocked** — multi-attach RGN failed verification |
| Video and streaming | YOLO `source: vi1` | VI selfpath (chn1) as the frame source | `/api/yolo`, `[yolo]` | `gating.c`, API and WebUI reject the value | **blocked** — enabling `vi1` hung the board; the value is refused with a reason |
| Video and streaming | Second RKNN 2.x runtime | Models built by toolkit 2.x, `yolo.runtime: 2` | `/api/yolo` (`runtime`), `/api/models` (`runtime`) | `librknnmrt2.so` with a distinct SONAME, two HAL handles, SDK-version check | **limited** — AIISP on 1.6.0 plus YOLO on 2.x in one process, runtime switching without a restart and a 2-hour soak were verified; the 2.x model mAP on a labelled set is not measured |
| Network | WiFi AIC8800 | Modules are built and copied to `/oem/usr/ko` | `RK_ENABLE_WIFI=y` | vendor modules without OKO changes | **limited** — WiFi stack unverified; networking verified over Ethernet |
| Telemetry (`oko-ec`) | Sensors | SHT21 `0x40` — T/RH; INA3221 or INA226 `0x41` — U/I/P; STM32 EC `0x42` — U/I/T, SOC, cells, OTA | `/dev/i2c-3`, `i2cdetect -y 3` | `i2c-dev` without drivers (`CONFIG_HWMON` off), INA autodetection | **limited** — addresses respond; INA3221 power inputs are not wired |
| microSD | Hotplug | Card removal and insertion | udev, `/api/storage` | detection via `device/type=SD` | **limited** — verified with unbind/bind only; physical removal untested |
| Network | VPN — WireGuard | `wg0` profile imported from a `.conf`: addresses, routes from `AllowedIPs`, split tunnel, autostart and watchdog | `/api/vpn`, WebUI Network → VPN, `/usr/bin/oko-vpn`, `/userdata/vpn` | built-in `CONFIG_WIREGUARD=y`, `wireguard-tools` 1.0.20210914 as `/oem/usr/bin/wg`, `oko-vpn` + `S45oko-vpn`, API and WebUI card | **limited** — bench 2026-11-06: handshake with a loopback peer (`wg show`: 8 s, transfer 92/212 B), split route, API/WebUI, autostart+watchdog after reboot, guard positive/negative (the default route was restored); an external peer and a tunnel `ping` were not run |
| Network | VPN — AmneziaWG | `awg0` profile, AWG v3.1 parameters, module loaded on demand | `/api/vpn`, WebUI Network → VPN, `/usr/bin/oko-vpn` | out-of-tree `amneziawg.ko` (`v3.1.20260906`), `awg` (`v3.1.20260812`), `RK_ENABLE_AWG=y` | **limited** — the module loads (`dmesg`: `AmneziaWG 3.1.20260906`), the v3.1 handshake with a loopback peer passed with all parameters (`I1/I5`, `HeaderProtectionKey`, `S3/S4`, `AdvancedSecurity`), and the guard positive/negative path was verified; an external peer was not run |
| Firmware | Power loss during an update | Recovery of STM32/STM8 | `flash.c`, bootloaders | `oko-flash`, STM8 I2C bootloader | **limited** — killing the host `oko-motor flash` was tested (bootloader stays usable, the next upload recovers); a full power cut was not emulated |

### Notes

- **VPSS channel budget is 4.** Without IVS, main + sub + MJPEG + JPEG are
  available; with IVS there are fewer free channels. On 2026-11-05, IVS + MJPEG
  worked with the sub stream off; sub + MJPEG + IVS was not tested. Unsupported
  combinations are refused by `gating.c` with a reason and a hint.
- **YOLO.** `source: vpss3` takes the last VPSS channel and is therefore
  incompatible with IVS/MJPEG/a fourth stream, so the sanitizer falls back to
  `vi0`. mAP on a validation set is not measured; tune `conf_threshold` for a
  different model. The main YOLO feature is confirmed — see
  `FEATURES_VERIFIED.md`.
- **AIISP.** Night denoise accuracy is not measured. AIISP and YOLO on `vpss3`
  were verified together on the bench (640×640 demo: main 30–33 fps, NPU 28%,
  no drops).
- **Rotation and boxes.** The canvas at 0/90/180/270 and the NN box mapping
  were measured with `OKO_GEOMETRY_TESTS=1` on 2026-11-07 (IoU
  0.62/0.71/0.71), and the OSD text keeps its corners. There is no reference
  markup for box positions on a rotated scene: reproducibility of the mapping
  is measured, not absolute accuracy.
- **Focus assist.** The AF bridge sets `RKAIQ_AF_MODE_FIXED` and
  `manual_meascfg_v31.af_en=1`; a non-zero metric requires
  `contrast_af.enable=1` in the SC230AI IQ (OKO sets it in the IQ and at
  runtime).
- **STM8.** The bootloader and the application build with SDCC 4.2
  (`tools/stm8-sdcc`); the current firmware is 2.1.1 (initial write over
  ST-Link/SWIM, `app-2.1.1.ihx` updates through the WebUI and
  `oko-motor flash`). The boot slot is 2.5 KiB (`0x8000–0x89FF`) with a
  measured boot image of 2355 bytes. Details and versions are in
  `FEATURES_VERIFIED.md` and `VERIFIED_FUNCTIONS.md`.
- **Manual control and home/zero.** `move`/`SET_POS`, manual focus/zoom control
  and the asynchronous `home`/`zero` jobs are covered by `api_check.py`
  (`OKO_FOCUS_MOTOR_TESTS=1`), including the version gate and the 409 for a
  busy job. The open remainder is step loss and the stop behaviour on a real
  lens.
- **Auto-focus.** The AF-metric search (`focus_af.c`) and micro-fine `af=fine`
  are implemented and covered by a host test; on the bench (2026-11-07) both
  branches finished `done`, but the metric does not depend on the motor
  position — the bench lens is not optically coupled to the drive, so
  convergence on a real scene is not confirmed. The WebUI layout was checked
  with headless Chromium at 1440×900 and 390×844, but not on a physical phone.
- **VPN.** `amneziawg.ko` builds for 5.10.160 and is loaded on demand by
  `oko-vpn`; `wg`/`awg` are built with the app toolchain (ARM ELF, uClibc). The
  `.conf` parser rejects `PreUp/PostUp/PreDown/PostDown`, supports AWG v3.1
  (`I1…I5`, `HeaderProtectionKey`, `AdvancedSecurity`) and is covered by a host
  test. The full `bench/vpn_check.py` needs a host peer (WireGuard for `wg0`,
  AmneziaWG from the vendored sources for `awg0`) and has not been run yet;
  after it passes, the rows move to `FEATURES_VERIFIED.md`.
- **Sensors.** SHT21/INA/EC addresses respond on `/dev/i2c-3`; the INA3221 power
  inputs are not wired, so current and power readings may be inaccurate.