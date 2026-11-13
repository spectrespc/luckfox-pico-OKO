# Неподтверждённые функции OKO

Возможности, которые проверены не полностью (**ограничено**), заблокированы
причиной (**заблокировано**) или ещё не проверялись. Подтверждённые функции —
в `FEATURES_VERIFIED.md`; паспорт платформы, общие примечания и команды
проверки — в `FEATURES.md`; открытые дефекты и обходы — в `KNOWN_ISSUES.md`.

Статус на 2026-11-12. Пометки: **ограничено** — работает с оговоркой;
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
| Видео и стриминг | Multicast как первый `dest` | `dest: - udp://239.255.255.0:5600` первым адресом | `[stream] dest` | вендорская multicast-ветка отправки | **ограничено** — пакеты уходят в группу, но `seq`/`ssrc`/`tstamp` всегда 0 (стенд 2026-11-10: 2674 пакета, 60 уникальных `seq`, `ssrc=0`), стандартный приёмник такой поток не соберёт; если multicast-адрес стоит **вторым** после unicast, пакеты валидны (2889 пакетов, `seq` 59648→62536 без разрывов) |
| Видео и стриминг | Поток H.265 в браузере | `http://<хост>:8889/cam/`, HLS `8888` | `services/stream/` | MediaMTX 1.21.3 | **ограничено** — Chromium (Playwright 1248) WebRTC закрывает с `codecs not supported by client`, HLS отдаётся как `hvc1` и в MSE не играет; H.265 смотрится в Safari или нативном плеере. С H.264 (`mp4.codec: H.264` + `rtpSDP H264/90000`) WebRTC играет 1080p30 в Chromium — проверено 2026-11-10 |
| Видео и стриминг | YOLO `source: vi1` | VI selfpath (chn1) как источник кадров | `/api/yolo`, `[yolo]` | `gating.c`, API и WebUI отклоняют значение | **заблокировано** — включение `vi1` привело к зависанию платы; значение отклоняется с причиной |
| Видео и стриминг | Второй рантайм RKNN 2.x | Модели toolkit 2.x, `yolo.runtime: 2` | `/api/yolo` (`runtime`), `/api/models` (`runtime`) | `librknnmrt2.so` с отдельным SONAME, два handle в HAL, проверка SDK-версии | **ограничено** — AIISP на 1.6.0 + YOLO на 2.x в одном процессе, переключение runtime без рестарта и 2-часовой soak проверены; mAP 2.x-модели на размеченном наборе не измерен |
| Сеть | WiFi AIC8800 (SDIO) | Модули `aic8800dc`/`aic_load_fw`/`aic8800_fdrv` собираются в `/oem/usr/ko` | `RK_ENABLE_WIFI=y` | вендорские модули без правок OKO | **ограничено** — USB RTL8821CU проверен отдельно (`FEATURES_VERIFIED.md`); SDIO-сборка и AP+STA не тестировались |

| Телеметрия (oko-ec) | Датчики | SHT21 `0x40` — T/RH; INA3221 или INA226 `0x41` — U/I/P; STM32 EC `0x42` — U/I/T, SOC, ячейки, OTA | `/dev/i2c-3`, `i2cdetect -y 3` | `i2c-dev` без драйверов (`CONFIG_HWMON` выключен), автоопределение INA | **ограничено** — адреса видны; у INA3221 силовые входы не подключены |
| microSD | Hotplug | Извлечение и установка карты | udev, `/api/storage` | определение по `device/type=SD` | **ограничено** — проверено unbind/bind; физическое извлечение не проверялось |
| USB | Host + хаб | Порт закреплён host'ом: хаб и устройства за ним; gadget/adb недоступны | `dr_mode = "host"` в активном DTS, `lsusb`, `/sys/class/udc` (пусто), `dmesg` | OKO: `dr_mode` «peripheral» → «host», фрагмент `rv1106-usb-host.config`, guard'ы в `S50usbdevice`/`S99usb0config` | **ограничено** — стенд 2026-11-11: после прошивки по TFTP xHCI поднимается сразу, хаб `1a40:0101` и `0bda:c811` за ним видны в `lsusb` без `otg_mode`; UDC/`usb0`/adbd отсутствуют. HID, USB-сеть и BT-донгл не подключались; VBUS — KI-054, недоступность gadget — KI-057 |
| USB | HID за хабом | Клавиатура/мышь | `/dev/input/event*`, `evtest` | `CONFIG_HID=y`, `CONFIG_USB_HID=y`, `CONFIG_INPUT_MOUSE=y` | **не проверено** — стенд 2026-11-10: `usbhid` и `psmouse` зарегистрированы, HID-устройство не подключалось |
| USB | USB-сеть (host) | RNDIS/CDC ECM/NCM, ASIX AX8817x/88179, RTL8150/8152 | `ip link`, `udhcpc -i usb0` | `CONFIG_USB_NET_*`, `CONFIG_USB_RTL815x` | **не проверено** — зарегистрированы `asix`, `ax88179_178a`, `cdc_ether`, `cdc_ncm`, `cdc_subset`, `rndis_host`, `r8152`, `rtl8150`; адаптер не подключался. Для RTL8153 нужна прошивка `rtl_nic`, в образ не входит |
| USB | Bluetooth | USB-BT-донгл, `bluetoothd` + `bluetoothctl` | `/etc/init.d/S30dbus-daemon`, `S40bluetoothd`, `bluetoothctl list` | `CONFIG_BT*`, bluez5_utils 5.86, dbus, libglib2, linux-firmware (RTL87xx/88xx BT, AR3012) | **ограничено** — стенд 2026-11-10: `dbus-daemon` 1.16.2 и `bluetoothd` запущены, `bluetoothctl` 5.86 работает (uclibc-патч `wordexp`), `btusb` зарегистрирован; BT-адаптер не подключался, `bluetoothctl list` без контроллера |
| Сеть | VPN — AmneziaWG | Профиль `awg0`, параметры AWG v3.1, модуль загружается по требованию | `/api/vpn`, WebUI Network → VPN, `/usr/bin/oko-vpn` | out-of-tree `amneziawg.ko` (`v3.1.20260906`), `awg` (`v3.1.20260812`), `RK_ENABLE_AWG=y` | **ограничено** — модуль загружается (`dmesg`: `AmneziaWG 3.1.20260906`), handshake v3.1 с loopback-пиром прошёл со всеми параметрами (`I1/I5`, `HeaderProtectionKey`, `S3/S4`, `AdvancedSecurity`), guard positive/negative проверен; 2026-11-08 loopback-регрессия `bench/vpn_check.py --type awg` — 19/19 (handshake 3 с, split-маршрут, `down`); внешний пир не прогонялся |
| Прошивка | Обрыв питания при обновлении | Восстановление после потери питания STM32/STM8 | `flash.c`, загрузчики | `oko-flash`, I2C-загрузчик STM8 | **ограничено** — обрыв хостового `oko-motor flash` проверен (загрузчик жив, повторная загрузка успешна); полный обрыв питания не имитировался |

### Примечания

- **rkipc и вендорские тест-утилиты вырезаны из образа.** `rkipc`,
  `rkaiq_3A_server`, `rkaiq_tool_server`, `rk_*_test`, `rk_mpi_*_test`,
  `luckfox_*_test` и лишние v4l-утилиты перечислены в `oko-prune.list` и
  отсутствуют в прошивке; диагностика через них недоступна. Сохранены
  `v4l2-ctl`, `media-ctl` и `rgaImDemo`. Стендовый цикл этой ветки —
  отложенный гейт, см. `KNOWN_ISSUES.md`.

- **USB host/BT: хаб проверен, классы за ним — нет.** Для порта собраны ядро
  с HID/USB-net/Bluetooth (`rv1106-usb-host.config`, все символы `=y` в
  итоговом `.config`), DTB содержит `dr_mode = "host"`, в rootfs вошли
  `bluetoothctl`, `dbus-daemon`, `lsusb` (busybox) и BT-прошивки
  `linux-firmware`. Стенд 2026-11-11: после TFTP-прошивки плата загрузилась с
  NAND, xHCI поднялся сразу, хаб `1a40:0101` и `0bda:c811` за ним видны в
  `lsusb` без ручного `otg_mode`; UDC нет, `usb0`/adbd не запускаются
  (`S50usbdevice`/`S99usb0config` выходят по `dr_mode`). HID, USB-сеть и
  BT-донгл не подключались, VBUS — KI-054, gadget недоступен — KI-057.

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
- **VPN.** WireGuard и OpenVPN подтверждены внешним пиром 2026-11-08 и
  перенесены в `FEATURES_VERIFIED.md` (`bench/vpn_check.py`: WG 19/19,
  OpenVPN 23/23 full+auth+reboot и 21/21 split, iperf3 21–23 Мбит/с).
  Остаётся AmneziaWG: модуль `amneziawg.ko` собран под 5.10.160 и
  загружается по требованию из `oko-vpn`; `wg`/`awg` собраны app-тулчейном
  (ELF ARM, uClibc). Парсер `.conf` отклоняет
  `PreUp/PostUp/PreDown/PostDown`, поддерживает AWG v3.1 (`I1…I5`,
  `HeaderProtectionKey`, `AdvancedSecurity`) и проверен хостовым тестом;
  handshake v3.1 с loopback-пиром и guard positive/negative пройдены, но
  внешний пир AWG не прогонялся (для хоста нужен out-of-tree модуль под
  ядро 6.12).
- **Датчики.** Адреса SHT21/INA/EC видны на `/dev/i2c-3`; у INA3221 силовые
  входы не подключены, поэтому ток/мощность могут быть недостоверны.

## English

Capabilities that are only partially verified (**limited**), blocked with a
reason (**blocked**) or not yet verified. Confirmed features are in
`FEATURES_VERIFIED.md`; the platform passport, shared notes and verification
commands are in `FEATURES.md`; open defects and workarounds are in
`KNOWN_ISSUES.md`.

Status as of 2026-11-12.

| Group | Feature | Description | Control (API, YAML section) | What was added (OKO vs vendor V1.4) | Status |
|---|---|---|---|---|---|
| Video and streaming | SmartP | GOP change | `/api/venc`, `[venc]` | SmartP stage in `divinus` | **limited** — applies on next start, not re-verified |
| Video and streaming | IVS | Motion/object detection, snapshot, clip, OSD | `/api/ivs`, `[ivs]` | IVS stage in `divinus` + `gating.c` | **limited** — MD verified; OD and static-scene events unconfirmed |
| Video and streaming | Runtime IQ | `switch_scene`, `updateIq` | `/api/iq`, `[iq]` | runtime IQ stage in `divinus` | **limited** — scene switch accepted, `updateIq` untested |
| Video and streaming | HDR2/HDR3 | HDR on SC230AI | `/api/hdr` | HDR API + `gating.c`; experimental HDR_X2 disabled | **blocked** — `librkaiq` does not support SC230AI |
| Video and streaming | OSD on all streams | `osd.scope=all` (main+sub+JPEG) | `[osd]` config, gate G4 | `gating.c` | **blocked** — multi-attach RGN failed verification |
| Video and streaming | Multicast as the first `dest` | `dest: - udp://239.255.255.0:5600` as the first entry | `[stream] dest` | vendor multicast send branch | **limited** — packets reach the group but `seq`/`ssrc`/`tstamp` stay 0 (bench 2026-11-10: 2674 packets, 60 unique `seq` values, `ssrc=0`), so a standard receiver cannot assemble the stream; with the multicast address **second** after a unicast one the packets are valid (2889 packets, `seq` 59648→62536 without gaps) |
| Video and streaming | H.265 stream in a browser | `http://<host>:8889/cam/`, HLS `8888` | `services/stream/` | MediaMTX 1.21.3 | **limited** — Chromium (Playwright 1248) closes WebRTC with `codecs not supported by client` and HLS is served as `hvc1`, which MSE rejects; H.265 needs Safari or a native player. With H.264 (`mp4.codec: H.264` + `rtpSDP H264/90000`) WebRTC plays 1080p30 in Chromium — verified 2026-11-10 |
| Video and streaming | YOLO `source: vi1` | VI selfpath (chn1) as the frame source | `/api/yolo`, `[yolo]` | `gating.c`, API and WebUI reject the value | **blocked** — enabling `vi1` hung the board; the value is refused with a reason |
| Video and streaming | Second RKNN 2.x runtime | Models built by toolkit 2.x, `yolo.runtime: 2` | `/api/yolo` (`runtime`), `/api/models` (`runtime`) | `librknnmrt2.so` with a distinct SONAME, two HAL handles, SDK-version check | **limited** — AIISP on 1.6.0 plus YOLO on 2.x in one process, runtime switching without a restart and a 2-hour soak were verified; the 2.x model mAP on a labelled set is not measured |
| Network | WiFi AIC8800 (SDIO) | `aic8800dc`/`aic_load_fw`/`aic8800_fdrv` are built into `/oem/usr/ko` | `RK_ENABLE_WIFI=y` | vendor modules without OKO changes | **limited** — USB RTL8821CU is verified separately (`FEATURES_VERIFIED.md`); the SDIO build and AP+STA are untested |

| Telemetry (`oko-ec`) | Sensors | SHT21 `0x40` — T/RH; INA3221 or INA226 `0x41` — U/I/P; STM32 EC `0x42` — U/I/T, SOC, cells, OTA | `/dev/i2c-3`, `i2cdetect -y 3` | `i2c-dev` without drivers (`CONFIG_HWMON` off), INA autodetection | **limited** — addresses respond; INA3221 power inputs are not wired |
| microSD | Hotplug | Card removal and insertion | udev, `/api/storage` | detection via `device/type=SD` | **limited** — verified with unbind/bind only; physical removal untested |
| USB | Host + hub | The port is pinned to host: a hub and the devices behind it; the gadget and adb are unavailable | `dr_mode = "host"` in the active DTS, `lsusb`, `/sys/class/udc` (empty), `dmesg` | OKO: `dr_mode` "peripheral" -> "host", `rv1106-usb-host.config` fragment, guards in `S50usbdevice`/`S99usb0config` | **limited** - bench 2026-11-11: after flashing over TFTP xHCI comes up at boot, the hub `1a40:0101` and the `0bda:c811` behind it appear in `lsusb` without `otg_mode`; there is no UDC, `usb0` or adbd. HID, USB networking and a BT dongle were not attached; VBUS - KI-054, no gadget - KI-057 |
| USB | HID behind a hub | Keyboard/mouse | `/dev/input/event*`, `evtest` | `CONFIG_HID=y`, `CONFIG_USB_HID=y`, `CONFIG_INPUT_MOUSE=y` | **unverified** — bench 2026-11-10: `usbhid` and `psmouse` are registered, no HID device was attached |
| USB | USB networking (host) | RNDIS/CDC ECM/NCM, ASIX AX8817x/88179, RTL8150/8152 | `ip link`, `udhcpc -i usb0` | `CONFIG_USB_NET_*`, `CONFIG_USB_RTL815x` | **unverified** — `asix`, `ax88179_178a`, `cdc_ether`, `cdc_ncm`, `cdc_subset`, `rndis_host`, `r8152` and `rtl8150` are registered; no adapter was attached. RTL8153 needs the `rtl_nic` firmware, which is not shipped |
| USB | Bluetooth | USB BT dongle, `bluetoothd` + `bluetoothctl` | `/etc/init.d/S30dbus-daemon`, `S40bluetoothd`, `bluetoothctl list` | `CONFIG_BT*`, bluez5_utils 5.86, dbus, libglib2, linux-firmware (RTL87xx/88xx BT, AR3012) | **limited** — bench 2026-11-10: `dbus-daemon` 1.16.2 and `bluetoothd` are running, `bluetoothctl` 5.86 works (uclibc `wordexp` patch), `btusb` is registered; no BT adapter was attached, `bluetoothctl list` shows no controller |
| Network | VPN — AmneziaWG | `awg0` profile, AWG v3.1 parameters, module loaded on demand | `/api/vpn`, WebUI Network → VPN, `/usr/bin/oko-vpn` | out-of-tree `amneziawg.ko` (`v3.1.20260906`), `awg` (`v3.1.20260812`), `RK_ENABLE_AWG=y` | **limited** — the module loads (`dmesg`: `AmneziaWG 3.1.20260906`), the v3.1 handshake with a loopback peer passed with all parameters (`I1/I5`, `HeaderProtectionKey`, `S3/S4`, `AdvancedSecurity`), and the guard positive/negative path was verified; the 2026-11-08 loopback regression `bench/vpn_check.py --type awg` passed 19/19 (3 s handshake, split route, `down`); an external peer was not run |
| Firmware | Power loss during an update | Recovery of STM32/STM8 | `flash.c`, bootloaders | `oko-flash`, STM8 I2C bootloader | **limited** — killing the host `oko-motor flash` was tested (bootloader stays usable, the next upload recovers); a full power cut was not emulated |

### Notes

- **rkipc and the vendor test utilities are pruned from the image.** `rkipc`,
  `rkaiq_3A_server`, `rkaiq_tool_server`, `rk_*_test`, `rk_mpi_*_test`,
  `luckfox_*_test` and the extra v4l utilities are listed in `oko-prune.list`
  and are absent from the firmware, so diagnostics through them are
  unavailable. `v4l2-ctl`, `media-ctl` and `rgaImDemo` are kept. The bench
  cycle for this branch is a deferred gate, see `KNOWN_ISSUES.md`.

- **USB host/BT: the hub is verified, the classes behind it are not.**
  The kernel is built with HID/USB net/Bluetooth (`rv1106-usb-host.config`,
  every symbol `=y` in the final `.config`), the DTB carries
  `dr_mode = "host"`, and the rootfs ships `bluetoothctl`, `dbus-daemon`,
  `lsusb` (busybox) and the `linux-firmware` BT blobs. Bench 2026-11-11: after
  TFTP flashing the board booted from NAND, xHCI came up at boot, and the hub
  `1a40:0101` with the `0bda:c811` behind it appeared in `lsusb` without a
  manual `otg_mode`; there is no UDC and neither `usb0` nor adbd starts
  (`S50usbdevice`/`S99usb0config` exit on `dr_mode`). HID, USB networking and a
  BT dongle were not attached; VBUS - KI-054, no gadget - KI-057.

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
- **VPN.** WireGuard and OpenVPN were confirmed against an external peer on
  2026-11-08 and moved to `FEATURES_VERIFIED.md` (`bench/vpn_check.py`: WG
  19/19, OpenVPN 23/23 full+auth+reboot and 21/21 split, iperf3 21–23 Mbit/s).
  AmneziaWG remains: `amneziawg.ko` builds for 5.10.160 and is loaded on demand
  by `oko-vpn`; `wg`/`awg` are built with the app toolchain (ARM ELF, uClibc).
  The `.conf` parser rejects `PreUp/PostUp/PreDown/PostDown`, supports AWG v3.1
  (`I1…I5`, `HeaderProtectionKey`, `AdvancedSecurity`) and is covered by a host
  test; the v3.1 loopback handshake and the guard positive/negative path pass,
  but an external AWG peer was not run (the host needs the out-of-tree module
  for kernel 6.12).
- **Sensors.** SHT21/INA/EC addresses respond on `/dev/i2c-3`; the INA3221 power
  inputs are not wired, so current and power readings may be inaccurate.