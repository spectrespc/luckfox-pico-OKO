# Неподтверждённые функции OKO

Возможности, которые проверены не полностью (**ограничено**), заблокированы
причиной (**заблокировано**) или ещё не проверялись. Подтверждённые функции —
в `FEATURES_VERIFIED.md`; паспорт платформы, общие примечания и команды
проверки — в `FEATURES.md`; открытые дефекты и обходы — в `KNOWN_ISSUES.md`.

Статус на 2026-11-06. Пометки: **ограничено** — работает с оговоркой;
**заблокировано** — отказ с причиной.

## Русский

### Сводная таблица неподтверждённого функционала

| Группа | Функционал | Описание | Управление (API, секция YAML) | Что добавлено (OKO vs вендорский V1.4) | Статус |
|---|---|---|---|---|---|
| Видео и стриминг | Поворот | 90/180/270 через VENC | `/api/rotate`, `[rotate]` | поворот VENC stage `divinus` | **ограничено** — проверен 90°; 180/270 без отдельных прогонов |
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
- **Поворот.** Отдельных прогонов для 180° и 270° нет; проверен только 90°.
- **Runtime IQ.** `switch_scene` принимается; `updateIq` на стенде не
  проверялся.
- **Hotplug.** Проверено программное извлечение/установка через unbind/bind;
  физическое извлечение карты не проверялось.
- **Помощь фокусировке.** AF-мост включает `RKAIQ_AF_MODE_FIXED` и
  `manual_meascfg_v31.af_en=1`; в IQ-файле SC230AI остаётся
  `contrast_af.enable=0`, поэтому после 20 пустых чтений тред один раз
  включает `contrast_af=true` (решающий тест A6). Главный открытый вопрос —
  выдаёт ли AFM ненулевые статистики на этом сенсоре.
- **Прошивка STM8.** Загрузчик и приложение собираются SDCC 4.2
  (`tools/stm8-sdcc`); `factory-2.0.1.ihx` прошит через ST-Link/SWIM,
  обновления `app-2.0.1.ihx` через WebUI проверены (версия из имени
  сверяется с заголовком OKO8). Слот загрузчика — 2.5 КиБ
  (`0x8000–0x89FF`), потому что измеренный boot занимает 2355 байт.
- **Прошивка контроллеров.** Фоновый воркер останавливает `S21oko-ec`,
  приостанавливает мотор, запускает `oko-flash` и перезапускает демон.
  Реальная прошивка STM32 (CBT6, WebUI `app-1.0.1.bin` → `/api/firmware`
  `1.0.1`, rollback `1.0.0`) и STM8 проверена; потеря питания именно в момент
  стирания не имитировалась.
- **WiFi AIC8800.** Модули собираются и попадают в `/oem/usr/ko`, но WiFi-стек
  (загрузка модулей, `wpa_supplicant`, режимы) не проверялся; сетевые функции
  подтверждены только по Ethernet.
- **VPN.** Модуль `amneziawg.ko` собран под 5.10.160 и загружается по требованию
  из `oko-vpn`; `wg`/`awg` собраны app-тулчейном (ELF ARM, uClibc). Парсер
  `.conf` отклоняет `PreUp/PostUp/PreDown/PostDown`, поддерживает AWG v3.1
  (`I1…I5`, `HeaderProtectionKey`, `AdvancedSecurity`) и проверен хостовым
  тестом. Полный прогон `bench/vpn_check.py` требует пира на хосте (WireGuard
  для `wg0`, AmneziaWG из вендорных исходников для `awg0`) и ещё не выполнялся;
  после него строки переходят в `FEATURES_VERIFIED.md`.
- **Датчики.** Адреса SHT21/INA/EC видны на `/dev/i2c-3`; у INA3221 силовые
  входы не подключены, поэтому ток/мощность могут быть недостоверны.
- **HDR2/HDR3.** Заблокированы: `librkaiq` не поддерживает SC230AI; ручная
  правка YAML откатывается при старте.
- **OSD на всех потоках.** `osd.scope=all` заблокирован: multi-attach RGN не
  прошёл проверку; при старте значение приводится к `main`.

## English

Capabilities that are only partially verified (**limited**), blocked with a
reason (**blocked**) or not yet verified. Confirmed features are in
`FEATURES_VERIFIED.md`; the platform passport, shared notes and verification
commands are in `FEATURES.md`; open defects and workarounds are in
`KNOWN_ISSUES.md`.

Status as of 2026-11-06.

| Group | Feature | Description | Control (API, YAML section) | What was added (OKO vs vendor V1.4) | Status |
|---|---|---|---|---|---|
| Video and streaming | Rotation | 90/180/270 via VENC | `/api/rotate`, `[rotate]` | VENC rotation stage in `divinus` | **limited** — 90° verified; 180/270 not separately |
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
- **Rotation.** There are no separate runs for 180° and 270°; only 90° was
  verified.
- **Runtime IQ.** `switch_scene` is accepted; `updateIq` was not verified on the
  bench.
- **Hotplug.** Programmatic removal/insertion via unbind/bind was verified;
  physical card removal was not.
- **Focus assist.** The AF bridge sets `RKAIQ_AF_MODE_FIXED` and
  `manual_meascfg_v31.af_en=1`. The SC230AI IQ file still has
  `contrast_af.enable=0`; after 20 empty reads the thread sets
  `contrast_af=true` once (decision gate A6). Whether AFM produces non-zero
  statistics on this sensor is the main open hardware question.
- **STM8 firmware.** The I2C bootloader and the application build cleanly with
  SDCC 4.2 (`tools/stm8-sdcc`); `factory-2.0.1.ihx` was written over
  ST-Link/SWIM and `app-2.0.1.ihx` updates through the WebUI are verified (the
  name version is checked against the OKO8 header). The boot slot is 2.5 KiB
  (0x8000–0x89FF) because the measured boot image is 2355 bytes.
- **Controller flashing.** The background worker stops `S21oko-ec`, suspends the
  focus motor, runs `oko-flash` and restarts the daemon. Real STM32 (CBT6,
  WebUI `app-1.0.1.bin` → `/api/firmware` `1.0.1`, rollback `1.0.0`) and STM8
  updates are verified; a power cut exactly during erase was not emulated.
- **WiFi AIC8800.** The modules are built and shipped in `/oem/usr/ko`, but the
  WiFi stack (module loading, `wpa_supplicant`, modes) is unverified; network
  features are confirmed over Ethernet only.
- **VPN.** `amneziawg.ko` builds for 5.10.160 and is loaded on demand by
  `oko-vpn`; `wg`/`awg` are built with the app toolchain (ARM ELF, uClibc). The
  `.conf` parser rejects `PreUp/PostUp/PreDown/PostDown`, supports AWG v3.1
  (`I1…I5`, `HeaderProtectionKey`, `AdvancedSecurity`) and is covered by a host
  test. The full `bench/vpn_check.py` needs a host peer (WireGuard for `wg0`,
  AmneziaWG from the vendored sources for `awg0`) and has not been run yet;
  after it passes, the rows move to `FEATURES_VERIFIED.md`.
- **Sensors.** SHT21/INA/EC addresses respond on `/dev/i2c-3`; the INA3221 power
  inputs are not wired, so current and power readings may be inaccurate.
- **HDR2/HDR3.** Blocked: `librkaiq` does not support SC230AI; manual YAML edits
  are rolled back at startup.
- **OSD on all streams.** `osd.scope=all` is blocked because multi-attach RGN
  failed verification; the value is reduced to `main` at startup.