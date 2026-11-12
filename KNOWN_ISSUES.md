# Известные ошибки и ограничения OKO

Обновлено: `2026-11-12`. В файле — открытые проблемы, подтверждённые
ограничения и недавно закрытые записи (KI-036). Проверенные функции и доказательства — в `VERIFIED_FUNCTIONS.md`,
состав и статус функционала — в `FEATURES_VERIFIED.md` / `FEATURES_UNVERIFIED.md`,
процедуры стенда — в `TESTBENCH.md`.

## Русский

### Критические

- **KI-001. `yolo.source=vi1` вешает плату.**
  Включение VI selfpath (chn1) через librockit приводит к зависанию RV1106 —
  нужна перезагрузка или перепрошивка. Значение заблокировано в `gating.c`,
  `/api/yolo` (HTTP 400) и WebUI, сохранённый `vi1` при старте заменяется на
  `vpss3`. Обход: `vpss3`, при конфликте с IVS/MJPEG — `vi0`.
  Статус: **заблокировано по дизайну**.

- **KI-002. RGA: im2d fd-путь работает, легаси blit — нет.**
  На `rknpu` 0.9.8 im2d `importbuffer_fd` импортирует MMZ- и dma-heap-
  дескрипторы (проверено `rgaImDemo --copy` и отдельным тестом MMZ
  NV12→RGB888). `yolo.convert: rga` снижает CPU `divinus` с ~35.6% до ~29.4%
  при 5 fps 640×384 без потери fps; при ошибке RGA один раз пишет
  предупреждение и откатывается на CPU. Легаси `c_RkRgaBlit` и блит по
  `malloc`-памяти по-прежнему не используются (SIGSEGV/зависания).
  Статус: **закрыто для im2d fd-пути**, легаси API — принятое ограничение.

- **KI-003. Модель 640×384 не входит в образ.**
  Модель собрана Docker-пайплайном (`tools/rknn-toolkit2/`, 2.6 МиБ) и
  активируется на DUT, но в репозиторий и `oem` не кладётся: после прошивки
  её нужно загрузить через WebUI/`/api/models`. Без загрузки YOLO не стартует.
  Обход: загрузить `yolov5n-640x384.rknn` (или собрать свою модель с тем же
  входом). Точность на валидационном наборе не измерена.
  Статус: **частично закрыто (P2: остался mAP)**.

- **KI-004. Лицензия YOLOv5.**
  Ultralytics YOLOv5 распространяется под GPL-3.0; для закрытого продукта нужна
  коммерческая лицензия или Apache-2.0-альтернатива (YOLOX, NanoDet).
  Статус: **открыто, юридический блокер**.

### Функциональные ограничения

- **KI-005. Ночная точность YOLO не измерена.**
  AIISP обрабатывает только кадр `vpss3`; при `vi0` детектор работает по
  необработанному кадру. Днём AIISP + YOLO работают одновременно (main
  30.2–32.8 fps, NPU 28%), но точность на ИК-сцене после AIISP и до него не
  проверялась. Статус: **открыто (P5)**.

- **KI-006. CMA/RSS под кольцом кадров не измерены.**
  `max_frames=20` при 640×640 — примерно 12 МБ MMZ; поведение при серии
  событий и рост памяти не зафиксированы. Обход: ограничивать события
  кулдауном и `max_frames`. Статус: **открыто (P5)**.

- **KI-007. Порог уверенности зависит от модели.**
  В текущей 640×384 модели sigmoid уже в графе: выход 0…1, фон около 0.5 и
  ниже, `conf_threshold=0.35` даёт корректные детекции (проверено на
  `bus.jpg`). Для другой модели порог подбирать заново по распределению
  objectness, а не копировать 0.35.
  Статус: **проверено для текущей модели**; для новых — при сборке.

- **KI-008. Квота загрузки модели — 3 МиБ.**
  Целевая 640×384 INT8 весит 2.6 МиБ и загружается; демо-модель YOLOv5n
  640×640 (~7.2 МиБ) через WebUI не пройдёт. Квоту менять только вместе с
  перерасчётом лимитов. Статус: **по дизайну; текущая модель проходит**.

- **KI-009. Принимаются только 4D-NHWC выходы.**
  3D-выход при zero-copy на RV1106 заполняется только наполовину; модель
  отклоняется при активации. Обход: reshape выходов в 4D в toolkit.
  Статус: **по дизайну**.

- **KI-010. Рамки детекции не рисуются.**
  YOLO не наносит bounding boxes на RTSP/JPEG; событие — это индикатор OSD и
  пачка чистых JPEG. Рисование рамок — вне объёма текущей реализации.
  Статус: **вне объёма**.

- **KI-011. HDR2/HDR3 не поддерживается на SC230AI.**
  `rk_aiq_uapi_sysctl_prepare(mode=HDR2/HDR3)` завершается ошибкой, приложение
  откатывается в SDR. Гейт `hdr` возвращает `blocked`.
  Статус: **заблокировано по дизайну**.

- **KI-012. AIISP несовместим с HDR.**
  При `aiisp.enable != off` и `hdr.mode != normal` AIISP принудительно
  выключается. AIISP также снижает fps, пока модель активна.
  Статус: **принятое ограничение**.

- **KI-013. OSD только на VENC main.**
  `osd.scope=all` и регионы на JPEG-канале заблокированы гейтом G4; `/api/jpeg`
  и снимки не содержат OSD. Один canvas на регион (double buffering даёт лаг).
  Статус: **по дизайну**.

- **KI-014. В шрифте OSD нет кириллицы.**
  `/oem/usr/share/simsun_en.ttf` покрывает ASCII и несколько CJK-иероглифов;
  сообщения и телеметрию нужно писать латиницей.
  Статус: **принятое ограничение**.

- **KI-015. IVS подтверждён частично.**
  Проверена детекция движения (MD); OD и событие на статичной сцене не
  подтверждены. Комбинация `sub + MJPEG + IVS` не проверялась, каналов VPSS
  всего четыре. Неподтверждённые комбинации отклоняет `gating.c`.
  Статус: **ограничено**.

- **KI-016. Часть настроек VENC проверена не полностью.**
  Поворот 90° проверен, 180°/270° — без отдельных прогонов. SmartP применяется
  только со следующего старта. `updateIq` не проверялся.
  Статус: **ограничено**.

- **KI-017. Сегмент записи может превысить квоту.**
  `record.max_bytes` контролируется при ротации: текущий сегмент успевает
  вырасти выше лимита до следующей проверки.
  Статус: **принятое ограничение**.

- **KI-018. Физический hotplug microSD не проверялся.**
  Проверены unbind/bind и API `mount`/`unmount`/`format`; извлечение и установка
  карты руками — нет. Статус: **ограничено**.

- **KI-019. Wi-Fi: USB RTL8821CU проверен, AIC8800 SDIO — нет.**
  Вендорские модули собираются в `/oem/usr/ko`; USB-адаптер `0bda:c811`
  (RTL8821CU) проверен на стенде 2026-11-12: скан, STA WPA2 с DHCP,
  ping/HTTP/RTSP через wlan0, AP-фолбэк и SD-импорт. SDIO-сборка
  (`aic8800dc`/`aic8800_fdrv`) и одновременный AP+STA не тестировались.
  Статус: **ограничено** (SDIO и AP+STA).

- **KI-020. У INA3221 не подключены силовые входы.**
  Обмен по I2C работает, но `VIN+`/`VIN-`/`VBUS` не подключены к нагрузке,
  поэтому ток/мощность могут быть нулевыми. Канал выбирается `--ina-channel`.
  Статус: **особенность стенда**.

- **KI-021. `motor.ko` удалён из форка (2026-11-07).**
  Драйвер 28BYJ-48 для pan/tilt не использовался; удалены каталог
  `sysdrv/drv_ko/motor`, строка `__insmod motor.ko`, `M_DIRS` и
  `CONFIG_SYSDRV_ENABLE_MOTOR`. Привод фокуса/зума — STM8 по I2C.
  Статус: **закрыто (2026-11-07)**.

- **KI-022. `post_chk` использует `rkipc-300w.ini` для SC230AI.**
  Шаблон рассчитан на сенсор 3 МП; при некорректной геометрии кадра ini нужно
  подбирать отдельно. Статус: **риск, проверить при смене сценария**.

- **KI-023. `IMAGE/` — устаревший артефакт RV1103 Pico Mini.**
  Артефакт не соответствовал активной конфигурации RV1106 Pro Max + SC230AI;
  удалён 2026-11-08, в `IMAGE/` остаётся только релиз под активную плату.
  Для прошивки использовать `output/image/update.img`.
  Статус: **закрыто (2026-11-08)**.

- **KI-048. Время ONVIF/ivs с суффиксом `Z` при поясе ≠ UTC.**
  Строки `"%Y-%m-%dT%H:%M:%SZ"` формируются через `localtime` (`server.c`,
  `ivs_events.c`), поэтому при не-UTC `/etc/TZ` они печатают локальное время с
  суффиксом UTC (`Z`). Это ошибка метки, а не самих событий/записей. Обход: для
  интеграций, где важна абсолютная метка, использовать `Etc/UTC` или добавлять
  смещение на стороне потребителя. Переход на UTC-формат — отдельная задача.
  Статус: **открыто (P4)**.

- **KI-051. UDP/RTP-раздача без FEC: потеря пакета портит кадр.**
  `[stream]` отправляет «сырой» RTP без ретрансляции и без FEC, поэтому любой
  потерянный пакет обрезает кадр (приёмник пишет `RTP packet lost`, затем
  `invalid fragmentation unit`), а при `keyint` до 50 артефакт держится до
  следующего IDR. На стенде 2026-11-10 потерь почти нет: за 60 с — 1 пакет из
  ~22 000. Отдельно: сам H.265-поток камеры уже содержит ошибки RPS/POC —
  `ffmpeg` пишет `Could not find ref with POC N` и на прямом
  `rtsp://<DUT>/live/0` (24 ошибки за 30 с), и на переизданном потоке
  (37 за 30 с), т.е. это свойство кодировщика, а не раздачи. Обход: TCP-транспорт
  RTSP `8554` у приёмника (`ffmpeg -rtsp_transport tcp`) и меньший `gop`.
  Статус: **принятое ограничение (P4)**.

- **KI-052. Multicast первым `dest` даёт невалидный RTP.**
  Флаг multicast берётся из первого адреса `dest`. При `is_mcast=1` вендорская
  ветка отправки пишет `seq=0`, `ssrc=0`, `tstamp=0` во все пакеты, поэтому
  стандартный приёмник поток не соберёт. Стенд 2026-11-10: 2674 пакета,
  60 уникальных `seq`, `ssrc=0`. Если multicast-адрес стоит вторым после
  unicast, отправка идёт по unicast-ветке и поток валиден (2889 пакетов,
  `seq` 59648→62536 без разрывов). Обход: unicast первым, multicast вторым.
  Статус: **принятое ограничение (P4)**.

- **KI-053. H.265 не играет в браузере.**
  Chromium не декодирует HEVC: страница `http://<хост>:8889/cam/` открывается,
  но WebRTC-сессия закрывается с `codecs not supported by client`, а HLS идёт
  как `hvc1` и в MSE не воспроизводится. H.265 смотрится в Safari (macOS) или
  нативном плеере (VLC/ffplay). Для браузера переключить камеру на H.264 и
  `rtpSDP` на `H264/90000` — проверено 2026-11-10: WebRTC играет 1080p30.
  Статус: **принятое ограничение (P4)**.

- **KI-054. USB-host за хабом: питание шины и физический OTG-переходник не проверены.**
  Порт переведён в dual-role (`dr_mode = "otg"`), классы HID/USB-net/BT включены
  в ядро. VBUS 5 В SoC не управляет: PHY запрашивает регулятор `vbus` из
  `vbus-supply` otg-port, в RV1106 DTS его нет, записи разрешения VBUS в
  `rv1106_phy_cfgs` тоже нет. Поэтому устройства за хабом питаются только от
  схемы платы/внешнего 5 В, а bus-powered хаб без внешнего питания может не
  подняться. На стенде 2026-11-10 проверено: роль device определяется по
  `bvalid` (extcon `USB=1`, UDC `configured`, gadget перечисляется на ПК), а
  принудительный host через
  `/sys/devices/platform/ff3e0000.usb2-phy/otg_mode` (`echo host`, обратно
  `otg`) поднимает xHCI с корневыми хабами. Сам OTG-переходник с хабом,
  HID-устройством, USB-сетью и BT-донглом на железе не подключался. Обход
  (только после подтверждения проблемы): тот же `otg_mode` или возврат к
  `dr_mode = "host"`; молча не включается.
  Статус: **частично проверено, хаб/HID/сеть/BT — untested**.

- **KI-055. `usb0` в режиме `otg` — закрыто (2026-11-10).**
  Раньше `S99usb0config` смотрел строку `dr_mode` в DTS и при `"otg"` не
  назначал `172.32.0.93`. Скрипт переписан на определение роли через
  `/sys/class/extcon/*/state`; в device-роли он ставит адрес и следит за
  сменой роли, а при возврате в device заново биндит configfs-gadget
  (после host-режима DWC3 снимает UDC и сам его не возвращает). Проверено на
  стенде 2026-11-10: два цикла host→device — UDC `configured`, `usb0`
  `172.32.0.93`, gadget снова виден на ПК. RNDIS-обмен данными не измерялся
  (на хосте нет прав на настройку `usb0`).
  Статус: **закрыто**.

- **KI-056. Legacy `g_mass_storage` ломал gadget — закрыто (2026-11-10).**
  Вендорский defconfig собирал `CONFIG_USB_MASS_STORAGE=y` (legacy
  `g_mass_storage`). Драйвер захватывал UDC при старте, падал с `-EINVAL`
  (нет backing-файла) и обрывал инициализацию peripheral у DWC3:
  `failed to add gadget` / `failed to initialize peripheral`,
  `/sys/class/udc` оставался пустым, adb/RNDIS не поднимались. Символ выключен
  во фрагменте `rv1106-usb-host.config`; gadget управляется через configfs
  (`USB_CONFIGFS_MASS_STORAGE` при необходимости включается в `S50usbdevice`).
  Проверено: UDC `ffb00000.usb` присутствует, state `configured`, gadget
  перечисляется.
  Статус: **закрыто**.
- **KI-059. Два аплинка: ограничения failover и приоритетов.**
  Активен всегда один default-маршрут (меньшая метрика), балансировки и
  агрегации нет. Равные `oko_net_metric`/`oko_wlan_metric` ядро не хранит в
  одной таблице, поэтому `oko-route` автоматически даёт Wi-Fi метрику +1
  (Ethernet выигрывает); для предсказуемости задавайте разные значения.
  wlan0 считается аплинком только при `mode=sta` и `wpa_state=COMPLETED` —
  SoftAP `192.168.43.1/24` управляющая сеть, не аплинк. VPN-туннель при
  failover рвётся и переподнимается на новом default; per-VPN pin по
  интерфейсу не поддерживается (`FEATURES.md`).
  Статус: **принятое ограничение (by design)**.

- **KI-060. При netboot (`root=/dev/nfs`) `oko-route` не управляет eth и resolv.**
  Демон намеренно не подменяет Ethernet-default и не переписывает
  `/etc/resolv.conf`, чтобы не сломать NFS-загрузку стенда; Wi-Fi-метрики при
  этом выравниваются. Следствие: в NFS-режиме DNS Wi-Fi-аплинка в
  resolv.conf не попадает, а eth-метрика остаётся такой, какой её поставил
  kernel/`ip=`. Это защита стенда, а не режим продукта.
  Статус: **принятое ограничение (by design)**.

### Прошивка и конфигурация

- **KI-024. Полный `update.img` затирает U-Boot env.**
  MAC восстанавливается из serial, но статические сетевые настройки
  (`oko_net_mode`, `oko_ipaddr`, `oko_netmask`, `oko_gateway`, `oko_dns`)
  нужно задать заново. Покомпонентная прошивка `oem`/`rootfs` env не трогает.
  Статус: **по процедуре**.

- **KI-025. `reboot loader` не переводит плату в loader-режим.**
  Проверено 2026-11-05: плата просто перезагружается, `upgrade_tool ld` пуст.
  Обход без кнопки: остановить U-Boot по UART (`Ctrl+C` в окне
  `Hit key to stop autoboot`) и выполнить `download`; команда отвечает
  `fallback to bootrom`, после чего `upgrade_tool` видит `Maskrom`.
  Статус: **известная особенность, есть обход**.

- **KI-026. Утилита `ubinfo` отсутствует в образе.**
  Геометрию UBI смотреть через
  `/sys/class/ubi/ubi5/{eraseblock_size,total_eraseblocks,avail_eraseblocks}`,
  `/sys/class/ubi/ubi5_0/{data_bytes,reserved_ebs,usable_eb_size}` и
  `df -h /userdata`. Статус: **ограничение инструментов**.

- **KI-027. Старый формат `divinus.yaml` перезаписывается.**
  `S20oko-osd` заменяет конфиг фабричным, если в нём нет `reg0_text`,
  `reg0_size`, `reg0_color` или `reg3_text`; прежний файл сохраняется как
  `divinus.yaml.bak`. Ручные правки без этих ключей не переживают перезагрузку.
  Статус: **по дизайну, потеря ручных правок возможна**.

- **KI-028. Неразбираемый конфиг откатывается только один раз.**
  При ошибке старта приложение восстанавливает `divinus.yaml.bak`; если и он
  битый — `divinus` не стартует. Статус: **принятое ограничение**.

- **KI-032. STM8: загрузчик проверен на одном экземпляре, риск раскладки
  остаётся.**
  На стенде был upstream `2.0.0` без OKO-загрузчика; `factory-2.0.1.ihx`
  записан через ST-Link, после чего работают `enter-boot`/`info`/`boot-reset`,
  обновление `app-2.0.1.ihx` через `/api/flash` (версия из имени сверяется с
  заголовком OKO8) и восстановление после обрыва
  хостового процесса. Итоговые размеры: boot 2412 Б, app 4279 Б (слот
  0x8000–0x89FF / 0x8A10–0x9FFF). Статус: **проверено на одном экземпляре**;
  риск — другая ревизия STM8S103 или обрыв питания во время стирания.

- **KI-033. AF-статистика нулевая без `contrast_af` в IQ SC230AI.**
  При `af_v31.TuningPara.contrast_af.enable=0` (оба блока IQ) AFM отдаёт
  нулевые ячейки, хотя `frame_id` растёт. После установки `1` A6 пройден:
  `status=running`, `cells_used=225`, `metric_avg` 527k–530k за 20 с.
  ROI на этой сборке применяется выбором ячеек полнокадровой карты, потому
  что аппаратное окно AFM не меняет карту. Метрика не реагирует на малые
  ходы мотора: оптическая связь объектива с STM8 не подтверждена; это
  ограничение стенда, а не API. Статус: **A6 пройден, требуется IQ с
  `contrast_af.enable=1`**.

- **KI-034. Прошивка контроллера не прерывается безопасно.**
  Воркер останавливает `S21oko-ec` и мотор; потеря питания посередине оставляет
  STM32/STM8 без приложения. STM8 остаётся в загрузчике (адрес `0x60`,
  `BOOT_INFO`), STM32 — в вендорском updater. Восстановление требует
  ST-Link/`oko-ec flash` с хоста. Прерывание хостового `oko-motor flash`
  проверено: загрузчик остаётся доступным, повторная загрузка восстанавливает
  приложение. Статус: **принятое ограничение** (полный обрыв питания не
  имитировался).

- **KI-035. NPU в OSD зависит от `rknpu.ko`.**
  Строка `NPU` добавляется к `CPU` только при наличии `/proc/rknpu/load`
  (`CONFIG_ROCKCHIP_RKNPU_PROC_FS=y`). Четвёртая строка OSD не добавляется:
  регион `reg4_posy=960` обрезается. Статус: **по дизайну**.

- **KI-036. Реальная прошивка STM32 через WebUI — закрыто (2026-11-06).**
  Установленный MCU — CBT6 (128 КиБ, слот 120 КиБ), а не C8T6: загрузчик
  принимает 60 КиБ, а C8T6-сборка приложения даёт `img_size=0` (метаданные
  читаются из страниц C8T6). Рабочие образы собраны с `-DMCU=CBT6`:
  `output/bench/stm32/app-1.0.0.bin` (`4bb4f433…`) и `app-1.0.1.bin`
  (`cb1f8e6e…`). Проверено: CLI `1.0.1` → `Application started`,
  `version=0x00010001`, `img_size=2324`; WebUI `/api/flash`
  `app-1.0.1.bin` → job `done`, `/api/firmware` показывает `1.0.1` точечно;
  затем rollback `app-1.0.0.bin` → `1.0.0`; телеметрия и `flags=0`; после
  power-cycle — app 1.0.0. Негативные проверки — 9/9. Осталось непроверенным
  только полное отключение питания в момент стирания. Статус: **закрыто**.

- **KI-049. Обновление ПО через WebUI (OTA) проверено на стенде (закрыто).**
  На стенде 2026-11-10 пройден полный цикл: `.okp` собран из
  `boot`/`oem`/`rootfs`, загружен через `/api/update/upload`, подготовлен на
  microSD (`oko-update stage`, размеры через `wc -c`), применён с токеном.
  U-Boot увидел `oko_update` (ключ добавлен в `CONFIG_ENVF_LIST`, вместе с
  `oko_mac`/`oko_net_*`), `sd_update` прочитал `sd_update.txt` с `mmc 1`,
  стёр и записал `boot`/`oem`/`rootfs`, снял флаг и сохранил env.
  `S20oko-update` записал `ok=1`, версия и ожидаемая 0.3.0 совпали, карта
  очищена; `/oem` после полного стирания смонтировался, divinus стартовал.
  HTTPS (`curl https://example.com`) работает. Найденные и исправленные по
  пути дефекты: отсутствие `stat`, неверный порядок выдачи apply-токена,
  белый список `ENVF`. Статус: **закрыто (2026-11-10)**.

- **KI-050. Ветка `feature/slim-image`: гейт мержа пройден.**
  Пройдено на DUT 2026-11-09–10: полная прошивка `update.img`, старт
  `divinus`, OSD, RTSP, ONVIF, `api_check` 105/105, `ui_check`, `osd_check`,
  `check_image_contents.sh` (22 обязательных / 61 запрещённый путь) и полный
  OTA-цикл с прошивкой из U-Boot (KI-049). Блокеров мержа нет; слияние в
  `main` не выполнялось и остаётся отдельным действием.
  Статус: **гейт пройден, мерж не выполнен**.

### VPN

- **KI-037. `resolvconf` в образе нет.**
  `wg-quick`-логика с `DNS=` не может обновлять системный resolver через
  `resolvconf`. В `oko-vpn` поддержаны два режима: `dns=keep` (по умолчанию,
  `/etc/resolv.conf` не трогается) и `dns=tunnel` (хелпер подменяет файл и
  восстанавливает его при `down`). Обход: `dns=tunnel` в метаданных профиля.
  Статус: **принятое ограничение**.

- **KI-038. `CONFIG_TUN` выключен, `amneziawg-go` невозможен.**
  В ядре `# CONFIG_TUN is not set`, поэтому userspace-реализация AmneziaWG
  (`amneziawg-go`) не может создать TUN-интерфейс. OKO использует только
  kernel-модуль `amneziawg.ko`. Включение TUN требует пересборки ядра и
  `boot`-раздела. Статус: **ограничение образа**.

- **KI-039. Версии модуля, `awg` и пира должны совпадать.**
  В образе `amneziawg-linux-kernel-module` `v3.1.20260906` и `amneziawg-tools`
  `v3.1.20260812`. Параметры v3.1 (`HeaderProtectionKey`, `I1…I5`,
  `RandomTrailers`, `AdvancedSecurity`) с пиром 1.x не согласуются; handshake
  молча не проходит. Обход: пир/сервер того же поколения 3.1; при
  несовместимости откат модуля и `awg` на `v1.0.20260725`.
  Статус: **требование совместимости**.

- **KI-040. Полный `update.img` стирает профили VPN.**
  Профили и состояние лежат в `/userdata/vpn`; полная прошивка перезаписывает
  `userdata`, поэтому профили (включая приватные ключи) нужно импортировать
  заново. Покомпонентная прошивка `oem`/`rootfs` их сохраняет.
  Статус: **принятое ограничение**.

- **KI-041. `CONFIG_NET_NS` выключен.**
  Нельзя поднять профиль в отдельном network namespace и нельзя иметь два
  независимых default route. Одновременно активны максимум `wg0` и `awg0`,
  маршруты общие; при двух full-tunnel профилях default route один.
  Статус: **ограничение ядра**.

- **KI-042. STM8: старший байт отрицательной позиции искажается.**
  На прошивке 2.0.2/2.1.1 после завершения движения в отрицательную область
  `GET_POS` возвращает значение с испорченным старшим байтом
  (`-1` → `0xFBFFFFFF`), хотя мотор проходит ровно заданное число шагов и
  статус возвращается в `IDLE`; искажённое значение может попасть в EEPROM.
  Причина локализована в записи 32-битной позиции из ISR (SDCC 4.2, STM8);
  `SET_POS` и положительные позиции работают корректно. Обход: divinus и
  `oko-motor home` считают шаги в open-loop и после довига отправляют
  `SET_POS(0)`, не читая отрицательную позицию; ручное управление ограничено
  `min_pos = 0`, а `home`/`zero` отклоняют заведомо битые значения. Статус:
  **открыто, обход встроен**.

- **KI-043. Включение AF-метрики больше не зависит от STM8 (закрыто).**
  Кнопка «Применить» по-прежнему отправляет `motor=1`, но сервер применяет
  `enable`/`mode`/`fps`/ROI **до** взведения привода (`server.c`, блок
  `motor` перенесён после `focus_start`/`focus_configure`). При отсутствующем,
  занятом или находящемся в бутлоадере контроллере метрика включается, ответ
  остаётся 200, а `motor.enabled=false`/`motor.status` показывают причину;
  ошибки `blocked`/409/502 остаются только у чистых моторных запросов и
  `af=1|fine` (им по-прежнему нужен взведённый драйвер). Проверено на стенде
  2026-11-07: `POST /api/focus?enable=1` без `motor` → `status=running`,
  `enable=0` → `idle`. Статус: **закрыто (2026-11-07)**.

- **KI-044. ePTZ: кроп/поворот применяются при старте канала (закрыто).**
  Замеры 2026-11-07: включение канала сбрасывает `SetChnCrop` и
  `SetChnRotation` — вызов до `RK_MPI_VPSS_EnableChn` возвращает `0`, но
  картинку не меняет; вызов сразу после `EnableChn` работает (кроп 960x540:
  mean|diff| 4.26 к центру кадра 1x против 53.85 к полному кадру). Поэтому
  `rk_channel_bind()` применяет сохранённые настройки после `EnableChn`, а
  смена зума/поворота на работающем канале только сохраняется и применяется
  graceful-рестартом приложения (`app_config_save` + SIGHUP, как у
  `/api/substream`; ответ `applied:false` и `note`). Повтор того же запроса
  рестарта не вызывает, а во время AF/`home`/`zero`/прошивки запрос
  отклоняется с `409`. После смены кропа обычный рестарт MP4 остаётся
  ненадёжным (нет кадров) — это ограничение стенда, обходится рестартом
  приложения. Предпросмотр Live может показывать `/mjpeg` или `/image.jpg`,
  где кроп main не действует. Статус: **закрыто (2026-11-07)**.

- **KI-045. Смена ABI rootfs: OpenSSL 3 (Python 3.14 удалён).**
  После перехода на Buildroot 2026.08.1 в rootfs нет
  `libssl.so.1.1`/`libcrypto.so.1.1`; вместо них `libssl.so.3`/`libcrypto.so.3`.
  Штатные `divinus`, `oko-*`, `wg`/`awg`, OpenVPN 2.7.8 пересобраны и проверены
  (`api_check` 98/98, RTSP/OSD), но сторонние сборки в `/oem`, слинкованные
  со старым ABI, не запустятся. Python 3.14 с 2026-11-09 в образ не входит
  (`oko-prune.list`, defconfig), поэтому `libpython3.14` и Python-пакеты
  недоступны. Обход: пересобрать из исходников SDK; для готовых blob'ов —
  совместимые копии библиотек рядом (не рекомендуется).
  Статус: **принятое ограничение миграции (обновлено 2026-11-09)**.

- **KI-046. `luckfox_pico_w_defconfig` не мигрирован.**
  Wi-Fi/audio-вариант (`bluez5_utils`, `mpv`) остался на старых патчах
  `hcitool_patch`/`mpv_patch`; на Buildroot 2026.08.1 их применение к новым
  версиям пакетов не проверялось. В базовом `luckfox_pico_defconfig` bluez
  теперь есть: пакет `bluez5_utils 5.86` собирается с uclibc-ng через
  `patches-2026.08/bluez5_utils/0001-build-provide-wordexp-fallback-...`
  (в uclibc выключен `wordexp`), устаревшие вендорские патчи
  `0001-fixed-header-file-errors`/`0002-fixed-wordexp`/
  `0003-fixed-compat-wordexp` из дерева удалены — они не входят в tarball
  2026.08.1 и не применялись к 5.86. w-вариант по-прежнему не проверен.
  Обход: не переключать `.BoardConfig.mk` на w-вариант до ребейса патчей;
  при необходимости — отключить пакеты или обновить патчи.
  Статус: **открыто для w-варианта, untested**.

### Безопасность

- **KI-029. WebUI работает по HTTP Basic без HTTPS.**
  Сессий и Logout нет (`/exit` останавливает сервер), пароли WebUI/RTSP/ONVIF
  хранятся в `divinus.yaml` открытым текстом и не читаются через API.
  Статус: **принятое ограничение**.

- **KI-030. Фабричный пароль нужно сменить.**
  WebUI/RTSP: `admin` / `oko-change-me`; SoftAP (`OKO-XXXX`): WPA2-PSK
  `ybrjveytljdthzq`, пока `oko_wlan_ap_psk` не задан. До подключения камеры к
  недоверенной сети сменить оба: SoftAP — `oko-wifi set` или `/api/wifi`,
  возврат заводского — `fw_setenv oko_wlan_ap_psk`. Статус: **требование
  эксплуатации**.

### Инструменты разработки

- **KI-047. Старые сборки (Docker/Ubuntu 22.04) не воспроизводятся.**
  Контейнер `luckfox_oko_build_env` и `Dockerfile`/`start.sh` удалены;
  Buildroot 2023.02.6 заменён на 2026.08.1. Собрать точную прошивку до
  миграции можно только из артефактов `output/` или из истории git
  (коммиты до `9718258b0`). Для повторной проверки старого поведения
  используйте `git worktree` на коммите до миграции и вендорский
  rootfs из `IMAGE/`. Статус: **принятое ограничение**.

- **KI-058. Wi-Fi PSK хранится открытым текстом в env U-Boot.**
  `oko_wlan_psk` и `oko_wlan_ap_psk` лежат в `/dev/mtd0` без шифрования,
  как и настройки Ethernet. API не возвращает пароль станции (`psk_set`),
  но пароль точки доступа доступен в `/api/wifi/apinfo` для подключения.
  Перенос в защищённое хранилище — отдельное решение.
  Статус: **принятое ограничение**.

- **KI-031. Headless Chromium требует `libasound.so.2`.**
  В хостовом окружении библиотеки нет. Обход без root:
  `apt-get download libasound2t64`, распаковать `dpkg-deb -x` и задать
  `LD_LIBRARY_PATH` (см. `bench/README.md`). Статус: **особенность хоста**.

## English

Updated: `2026-11-12`. This file lists open problems, confirmed limitations and
recently closed entries (KI-036).
Verified functions are in `VERIFIED_FUNCTIONS.md`, feature status in
`FEATURES_VERIFIED.md` / `FEATURES_UNVERIFIED.md`, bench procedures in
`TESTBENCH.md`.

### Critical

- **KI-001. `yolo.source=vi1` hangs the board.** Enabling VI selfpath (chn1)
  locks RV1106 until a power cycle or reflash. The value is blocked in
  `gating.c`, `/api/yolo` (HTTP 400) and the WebUI; a saved `vi1` is replaced
  with `vpss3` at startup. Workaround: `vpss3`, or `vi0` when IVS/MJPEG owns
  channel 3. Status: **blocked by design**.

- **KI-002. RGA: the im2d fd path works, the legacy blit does not.** On
  `rknpu` 0.9.8 im2d `importbuffer_fd` imports MMZ and dma-heap descriptors
  (verified with `rgaImDemo --copy` and a dedicated MMZ NV12→RGB888 test).
  `yolo.convert: rga` lowers `divinus` CPU from ~35.6% to ~29.4% at 5 fps
  640×384 without losing fps; on an RGA error it logs once and falls back to
  the CPU path. Legacy `c_RkRgaBlit` and blits on `malloc` memory are still
  not used (SIGSEGV/hangs). Status: **closed for the im2d fd path**; the
  legacy API remains an accepted limitation.

- **KI-003. The 640×384 model is not part of the image.** It is built by the
  Docker pipeline (`tools/rknn-toolkit2/`, 2.6 MiB) and activates on the DUT,
  but is not stored in the repository or `oem`: after flashing, upload it via
  the WebUI/`/api/models`. Without an upload YOLO does not start. Workaround:
  upload `yolov5n-640x384.rknn` (or build a model with the same input).
  Validation mAP is not measured. Status: **partially closed (P2: mAP left)**.

- **KI-004. YOLOv5 licence.** Ultralytics YOLOv5 is GPL-3.0; a closed product
  needs a commercial licence or an Apache-2.0 alternative (YOLOX, NanoDet).
  Status: **open, legal blocker**.

### Functional limitations

- **KI-005. Night accuracy is not measured.** AIISP only processes `vpss3`
  frames; `vi0` is unprocessed. Daytime AIISP + YOLO works (main 30.2–32.8 fps,
  NPU 28%), but IR accuracy with and without AIISP is unknown.
  Status: **open (P5)**.

- **KI-006. CMA/RSS under the frame ring is not measured.** `max_frames=20` at
  640×640 is about 12 MB of MMZ; behaviour under event bursts is not recorded.
  Workaround: cooldown and `max_frames`. Status: **open (P5)**.

- **KI-007. The confidence threshold depends on the model.** In the current
  640×384 model sigmoid is fused: outputs are 0…1, background sits near 0.5 and
  below, and `conf_threshold=0.35` gives correct detections (`bus.jpg` check).
  For another model, tune the threshold from the objectness distribution
  instead of copying 0.35. Status: **verified for the current model**; recheck
  for new ones.

- **KI-008. Model upload quota is 3 MiB.** The target 640×384 INT8 model is
  2.6 MiB and uploads fine; the 640×640 YOLOv5n demo (~7.2 MiB) does not fit
  through the WebUI. Status: **by design; the current model fits**.

- **KI-009. Only 4D-NHWC outputs are accepted.** A 3D zero-copy output is
  filled only partially on RV1106 and is rejected at activation. Reshape the
  outputs to 4D in the toolkit. Status: **by design**.

- **KI-010. Detection boxes are not drawn.** YOLO does not render bounding
  boxes on RTSP/JPEG; an event is an OSD indicator plus clean JPEGs.
  Status: **out of scope**.

- **KI-011. HDR2/HDR3 is unsupported on SC230AI.** `rk_aiq_uapi_sysctl_prepare`
  fails and the stream falls back to SDR; the `hdr` gate returns `blocked`.
  Status: **blocked by design**.

- **KI-012. AIISP cannot run with HDR.** AIISP is forced off when
  `hdr.mode != normal`; it also lowers fps while its model is active.
  Status: **accepted limitation**.

- **KI-013. OSD is available only on VENC main.** `osd.scope=all` and regions
  on the JPEG channel are blocked by gate G4; `/api/jpeg` and snapshots have no
  OSD. One canvas per region (double buffering causes lag).
  Status: **by design**.

- **KI-014. The OSD font has no Cyrillic.** `simsun_en.ttf` covers ASCII and a
  few CJK glyphs; OSD text must be Latin. Status: **accepted limitation**.

- **KI-015. IVS is only partially verified.** Motion detection is verified;
  object detection and a static-scene event are not. `sub + MJPEG + IVS` is
  untested, and VPSS has only four channels. `gating.c` refuses unconfirmed
  combinations. Status: **limited**.

- **KI-016. Some VENC settings are not fully verified.** Rotation 90° is
  verified, 180°/270° are not. SmartP applies on the next start. `updateIq` is
  untested. Status: **limited**.

- **KI-017. A recording segment can exceed its quota.** `record.max_bytes` is
  enforced at rotation, so the current segment may grow past the limit first.
  Status: **accepted limitation**.

- **KI-018. Physical microSD hotplug is untested.** Unbind/bind and
  `mount`/`unmount`/`format` are verified; manual card removal/insertion is not.
  Status: **limited**.

- **KI-019. Wi-Fi: USB RTL8821CU is verified, AIC8800 SDIO is not.** The
  vendor modules are built into `/oem/usr/ko`. The `0bda:c811` (RTL8821CU)
  USB adapter was verified on the bench on 2026-11-12: scan, WPA2 STA with
  DHCP, ping/HTTP/RTSP over wlan0, AP fallback and SD import. The SDIO build
  (`aic8800dc`/`aic8800_fdrv`) and concurrent AP+STA were not tested. Status:
  **limited** (SDIO and AP+STA).

- **KI-020. INA3221 power inputs are not connected.** I2C works, but
  `VIN+`/`VIN-`/`VBUS` are not wired to a load, so current/power may read zero.
  Status: **bench limitation**.

- **KI-021. `motor.ko` was removed from the fork (2026-11-07).**
  The 28BYJ-48 pan/tilt driver was unused; the `sysdrv/drv_ko/motor` tree, the
  `__insmod motor.ko` line, `M_DIRS` and `CONFIG_SYSDRV_ENABLE_MOTOR` are gone.
  Focus/zoom is driven by the STM8 over I2C. Status: **closed (2026-11-07)**.

- **KI-022. `post_chk` uses `rkipc-300w.ini` for SC230AI.** The template
  targets a 3 MP sensor; choose another ini if the frame geometry is wrong.
  Status: **risk, verify when the scenario changes**.

- **KI-023. `IMAGE/` is an outdated RV1103 Pico Mini artifact.** The artifact
  did not match RV1106 Pro Max + SC230AI; it was removed on 2026-11-08 and
  `IMAGE/` now holds only the release for the active board. Flash
  `output/image/update.img` instead. Status: **closed (2026-11-08)**.

- **KI-048. ONVIF/ivs timestamps carry a `Z` suffix in a non-UTC zone.**
  The `"%Y-%m-%dT%H:%M:%SZ"` strings are built with `localtime` (`server.c`,
  `ivs_events.c`), so with a non-UTC `/etc/TZ` they print local time with a UTC
  (`Z`) suffix. Only the label is wrong; the events and recordings are intact.
  Workaround: use `Etc/UTC` where an absolute timestamp matters, or add the
  offset on the consumer side. Switching to a real UTC format is a separate
  task. Status: **open (P4)**.

- **KI-051. The UDP/RTP push has no FEC, so a lost packet damages a frame.**
  `[stream]` sends raw RTP without retransmission or FEC, so any lost packet
  truncates a frame (the receiver logs `RTP packet lost`, then `invalid
  fragmentation unit`), and with `keyint` up to 50 the artifact lasts until the
  next IDR. On the 2026-11-10 bench losses are negligible: one packet out of
  ~22,000 in 60 s. Separately, the camera's own H.265 stream already carries
  RPS/POC errors — `ffmpeg` prints `Could not find ref with POC N` both on the
  direct `rtsp://<DUT>/live/0` (24 errors in 30 s) and on the re-published
  stream (37 in 30 s), so it is an encoder property, not a push problem.
  Workarounds: read the receiver over RTSP/TCP (`ffmpeg -rtsp_transport tcp`)
  and use a smaller `gop`. Status: **accepted limitation (P4)**.

- **KI-052. Multicast as the first `dest` produces invalid RTP.**
  The multicast flag comes from the first `dest` entry. With `is_mcast=1` the
  vendor send branch writes `seq=0`, `ssrc=0` and `tstamp=0` into every packet,
  so a standard receiver cannot assemble the stream. Bench 2026-11-10: 2674
  packets, 60 unique `seq` values, `ssrc=0`. With the multicast address second
  after a unicast one the unicast branch is used and the stream is valid (2889
  packets, `seq` 59648→62536 without gaps). Workaround: unicast first,
  multicast second. Status: **accepted limitation (P4)**.

- **KI-053. H.265 does not play in a browser.**
  Chromium has no HEVC decoder: `http://<host>:8889/cam/` opens, but the WebRTC
  session closes with `codecs not supported by client`, and HLS is offered as
  `hvc1`, which MSE rejects. H.265 plays in Safari (macOS) or a native player
  (VLC/ffplay). For browser playback switch the camera to H.264 and `rtpSDP` to
  `H264/90000` — verified 2026-11-10: WebRTC plays 1080p30.
  Status: **accepted limitation (P4)**.

- **KI-054. USB host behind a hub: bus power and a physical OTG adapter are unverified.**
  The port runs in dual-role mode (`dr_mode = "otg"`) with the HID/USB-net/BT
  classes built in. The SoC does not control VBUS 5 V: the PHY requests a `vbus`
  regulator from `vbus-supply` on the otg-port (absent from the RV1106 DTS), and
  `rv1106_phy_cfgs` has no VBUS enable register. Devices behind a hub are
  therefore powered only by the board supply or an external 5 V, and a
  bus-powered hub may not come up. Bench 2026-11-10: the device role is detected
  via `bvalid` (extcon `USB=1`, UDC `configured`, the gadget enumerates on a
  PC), and forcing host through
  `/sys/devices/platform/ff3e0000.usb2-phy/otg_mode` (`echo host`, back with
  `otg`) brings up xHCI with its root hubs. An OTG adapter with a hub, a HID
  device, USB networking and a BT dongle were not attached to the hardware.
  Workaround (only after the problem is confirmed): the same `otg_mode`, or go
  back to `dr_mode = "host"`; it is not enabled silently.
  Status: **partially verified, hub/HID/net/BT untested**.

- **KI-055. `usb0` in `otg` mode — closed (2026-11-10).**
  `S99usb0config` used to read `dr_mode` from the DTS and skipped
  `172.32.0.93` when it was `"otg"`. The script now derives the role from
  `/sys/class/extcon/*/state`; in the device role it assigns the address and
  watches for role changes, re-binding the configfs gadget when the port
  becomes a device again (after a host excursion DWC3 drops its UDC and does
  not restore it by itself). Bench 2026-11-10: two host→device cycles — UDC
  `configured`, `usb0` `172.32.0.93`, the gadget visible on the PC again. RNDIS
  data throughput was not measured (no host permission to configure `usb0`).
  Status: **closed**.

- **KI-056. The legacy `g_mass_storage` driver broke the gadget — closed (2026-11-10).**
  The vendor defconfig built `CONFIG_USB_MASS_STORAGE=y` (legacy
  `g_mass_storage`). The driver grabbed the UDC at boot, failed with `-EINVAL`
  (no backing file) and aborted the DWC3 peripheral init:
  `failed to add gadget` / `failed to initialize peripheral`, leaving
  `/sys/class/udc` empty so adb/RNDIS could not start. The symbol is disabled in
  `rv1106-usb-host.config`; gadget functions are managed through configfs
  (`USB_CONFIGFS_MASS_STORAGE` can be enabled in `S50usbdevice` when needed).
  Verified: UDC `ffb00000.usb` present, state `configured`, gadget enumerated.
  Status: **closed**.
- **KI-059. Dual uplink: failover and priority limitations.**
  Only one default route is active at a time (the lower metric); there is no
  balancing or aggregation. The kernel cannot keep two equal
  `oko_net_metric`/`oko_wlan_metric` values in one table, so `oko-route`
  automatically adds +1 to Wi-Fi (Ethernet wins); use different values for
  predictable behaviour. `wlan0` is an uplink only with `mode=sta` and
  `wpa_state=COMPLETED` - the SoftAP `192.168.43.1/24` is a management
  network, not an uplink. A VPN tunnel breaks on failover and is re-established
  on the new default; per-VPN interface pinning is not supported
  (`FEATURES.md`). Status: **accepted limitation (by design)**.

- **KI-060. Under netboot (`root=/dev/nfs`) `oko-route` does not manage eth or resolv.**
  The daemon deliberately leaves the Ethernet default route and
  `/etc/resolv.conf` alone so the bench NFS boot is not broken; the Wi-Fi
  metrics are still reconciled. Consequence: in NFS mode the Wi-Fi resolver is
  not added to resolv.conf, and the eth metric stays whatever the kernel/`ip=`
  installed. This is a bench safeguard, not a product mode.
  Status: **accepted limitation (by design)**.

### Flashing and configuration

- **KI-024. A full `update.img` wipes the U-Boot env.** The MAC is re-derived
  from the serial number, but static network settings
  (`oko_net_mode`, `oko_ipaddr`, `oko_netmask`, `oko_gateway`, `oko_dns`) must
  be re-applied. Component `oem`/`rootfs` flashing keeps env.
  Status: **procedure**.

- **KI-025. `reboot loader` does not enter loader mode.** On 2026-11-05 the
  board simply rebooted. Workaround without a button: stop U-Boot over UART
  (`Ctrl+C` at `Hit key to stop autoboot`) and run `download`; it falls back to
  bootrom and `upgrade_tool` sees `Maskrom`. Status: **known, workaround exists**.

- **KI-026. `ubinfo` is missing from the image.** Read UBI geometry from
  `/sys/class/ubi/ubi5/*`, `/sys/class/ubi/ubi5_0/*` and `df -h /userdata`.
  Status: **tooling limitation**.

- **KI-027. Old-format `divinus.yaml` is overwritten.** `S20oko-osd` replaces
  the config with the factory one when `reg0_text`, `reg0_size`, `reg0_color`
  or `reg3_text` is missing; the old file is kept as `divinus.yaml.bak`.
  Status: **by design, manual edits may be lost**.

- **KI-028. A broken config is rolled back only once.** The app restores
  `divinus.yaml.bak`; if the backup is broken too, `divinus` does not start.
  Status: **accepted limitation**.

- **KI-032. STM8: the bootloader is verified on one sample; layout risk remains.**
  The bench board had upstream `2.0.0` without the OKO bootloader;
  `factory-2.0.1.ihx` was written over ST-Link, after which
  `enter-boot`/`info`/`boot-reset`, the `/api/flash` `app-2.0.1.ihx` update
  (the name version is checked against the OKO8 header) and recovery after
  killing the host tool all work. Final sizes: boot 2412 B,
  app 4279 B (slots 0x8000–0x89FF / 0x8A10–0x9FFF). Status: **verified on one
  sample**; another STM8S103 revision or a power cut during erase is the risk.

- **KI-033. AF statistics are zero without `contrast_af` in the SC230AI IQ.**
  With `af_v31.TuningPara.contrast_af.enable=0` (both IQ blocks) the AFM
  returns zero cells while `frame_id` still advances. With `1`, A6 passes:
  `status=running`, `cells_used=225`, `metric_avg` 527k–530k over 20 s. On this
  build the ROI is applied by selecting cells of the full-frame map because the
  hardware AFM window does not move the map. The metric does not react to small
  motor moves: the lens-to-STM8 optical coupling is not confirmed. Status:
  **A6 passed; the IQ must keep `contrast_af.enable=1`**.

- **KI-034. Controller flashing is not power-safe.**
  The worker stops `S21oko-ec` and the motor; a power loss in the middle leaves
  the STM32/STM8 without an application. The STM8 stays in the bootloader
  (`0x60`, `BOOT_INFO`); the STM32 stays in the vendor updater. Recovery needs
  ST-Link or `oko-ec flash` from a host. Status: **accepted limitation**.

- **KI-035. NPU on the OSD depends on `rknpu.ko`.**
  The `NPU` suffix is appended to the `CPU` line only when
  `/proc/rknpu/load` exists (`CONFIG_ROCKCHIP_RKNPU_PROC_FS=y`). A fourth OSD
  line is not added: the region at `reg4_posy=960` clips. Status: **by design**.

- **KI-036. A real STM32 update through the WebUI — closed (2026-11-06).**
  The installed MCU is CBT6 (128 KiB, 120 KiB app slot), not C8T6: the
  bootloader accepts a 60 KiB image, while a C8T6 application build reports
  `img_size=0` (it reads metadata from the C8T6 pages). Working images are
  built with `-DMCU=CBT6`: `output/bench/stm32/app-1.0.0.bin`
  (`4bb4f433…`) and `app-1.0.1.bin` (`cb1f8e6e…`). Verified: CLI `1.0.1` →
  `Application started`, `version=0x00010001`, `img_size=2324`; WebUI
  `/api/flash` `app-1.0.1.bin` → job `done`, `/api/firmware` reports `1.0.1`,
  then rollback `app-1.0.0.bin` → `1.0.0`; telemetry and `flags=0`; after a
  power cycle the app runs 1.0.0. Negative checks 9/9. Only a full power cut
  during erase remains untested. Status: **closed**.

- **KI-049. Software update from the WebUI (OTA) verified on the bench (closed).**
  The full cycle passed on 2026-11-10: the `.okp` package built from
  `boot`/`oem`/`rootfs` was uploaded through `/api/update/upload`, staged on
  the microSD card (`oko-update stage`, sizes via `wc -c`) and applied with a
  token. U-Boot saw `oko_update` (the key was added to `CONFIG_ENVF_LIST`
  together with `oko_mac`/`oko_net_*`), `sd_update` read `sd_update.txt` from
  `mmc 1`, erased and wrote `boot`/`oem`/`rootfs`, cleared the flag and saved
  the env. `S20oko-update` recorded `ok=1` with version and expected 0.3.0
  matching, and the card was cleaned; `/oem` mounted after the full erase and
  divinus started. HTTPS (`curl https://example.com`) works. Defects found and
  fixed on the way: missing `stat`, wrong apply-token issuance order, and the
  `ENVF` allowlist. Status: **closed (2026-11-10)**.

- **KI-050. `feature/slim-image` branch: merge gate passed.**
  Passed on the DUT on 2026-11-09–10: full `update.img` flash, `divinus`
  start, OSD, RTSP, ONVIF, `api_check` 105/105, `ui_check`, `osd_check`,
  `check_image_contents.sh` (22 required / 61 forbidden paths) and the full
  OTA cycle with a U-Boot flash (KI-049). There are no merge blockers; the
  merge into `main` was not performed and remains a separate action.
  Status: **gate passed, not merged**.

### VPN

- **KI-037. `resolvconf` is not in the image.** The wg-quick `DNS=` path cannot
  update the system resolver through `resolvconf`. `oko-vpn` supports two
  modes: `dns=keep` (default, `/etc/resolv.conf` is untouched) and `dns=tunnel`
  (the helper replaces the file and restores it on `down`). Workaround:
  `dns=tunnel` in the profile metadata. Status: **accepted limitation**.

- **KI-038. `CONFIG_TUN` is off, so `amneziawg-go` is impossible.** The kernel
  has `# CONFIG_TUN is not set`, so the userspace AmneziaWG implementation
  cannot create a TUN device. OKO uses only the `amneziawg.ko` kernel module.
  Enabling TUN needs a kernel and `boot` partition rebuild. Status: **image
  limitation**.

- **KI-039. Module, `awg` and peer versions must match.** The image ships
  `amneziawg-linux-kernel-module` `v3.1.20260906` and `amneziawg-tools`
  `v3.1.20260812`. The v3.1 parameters (`HeaderProtectionKey`, `I1…I5`,
  `RandomTrailers`, `AdvancedSecurity`) do not interoperate with a 1.x peer and
  the handshake fails silently. Workaround: use a 3.1 peer/server; otherwise
  downgrade the module and `awg` to `v1.0.20260725`. Status: **compatibility
  requirement**.

- **KI-040. A full `update.img` erases the VPN profiles.** Profiles and state
  live in `/userdata/vpn`; a full flash rewrites `userdata`, so the profiles
  (including private keys) must be imported again. Component flashing of
  `oem`/`rootfs` keeps them. Status: **accepted limitation**.

- **KI-041. `CONFIG_NET_NS` is off.** A profile cannot run in its own network
  namespace and two independent default routes are impossible. At most `wg0`
  and `awg0` are active together, the route table is shared, and two
  full-tunnel profiles share one default route. Status: **kernel limitation**.

- **KI-042. STM8: the top byte of a negative position is corrupted.** On
  firmware 2.0.2/2.1.1, after a move into the negative range `GET_POS` returns
  a value with a wrong top byte (`-1` → `0xFBFFFFFF`), even though the motor
  travels exactly the commanded number of steps and returns to `IDLE`; the
  corrupted value can reach the EEPROM. The fault is in the 32-bit position
  store from the ISR (SDCC 4.2, STM8); `SET_POS` and positive positions are
  correct. Workaround: divinus and `oko-motor home` count steps open-loop and
  send `SET_POS(0)` after the overshoot without reading the negative position;
  manual control is bounded by `min_pos = 0`, and `home`/`zero` reject values
  that are visibly corrupted. Status: **open, workaround in place**.

- **KI-043. Enabling the AF metric no longer depends on the STM8 (closed).**
  The "Apply" button still sends `motor=1`, but the server now applies
  `enable`/`mode`/`fps`/ROI **before** arming the driver (`server.c`, the
  `motor` block moved after `focus_start`/`focus_configure`). With a missing,
  busy or bootloader-stuck controller the metric still starts, the response
  stays HTTP 200 and `motor.enabled=false`/`motor.status` show the reason;
  `blocked`/409/502 remain only for pure motor requests and `af=1|fine`
  (those still require the armed driver). Verified on the bench 2026-11-07:
  `POST /api/focus?enable=1` without `motor` → `status=running`, `enable=0` →
  `idle`. Status: **closed (2026-11-07)**.

- **KI-044. ePTZ: crop/rotation are applied when a channel starts (closed).**
  Measurements on 2026-11-07: enabling a channel resets `SetChnCrop` and
  `SetChnRotation` — a call before `RK_MPI_VPSS_EnableChn` returns `0` but
  leaves the picture unchanged, while a call right after `EnableChn` works (a
  960x540 crop: mean|diff| 4.26 to the centre of the 1x frame versus 53.85 to
  the full frame). `rk_channel_bind()` therefore applies the stored settings
  after `EnableChn`, and a runtime zoom/rotation change is only stored and
  applied by a graceful application restart (`app_config_save` + SIGHUP, the
  `/api/substream` pattern; the response carries `applied:false` and a `note`).
  Repeating the same request does not restart again, and a request during an
  AF/`home`/`zero`/flash job is refused with `409`. After a crop change a plain
  MP4 restart is still unreliable (no frames) — a bench limitation worked
  around by the application restart. The Live preview may show `/mjpeg` or
  `/image.jpg`, where the main crop has no effect.
  Status: **closed (2026-11-07)**.

- **KI-045. Rootfs ABI change: OpenSSL 3 (Python 3.14 removed).**
  After the Buildroot 2026.08.1 migration the rootfs no longer ships
  `libssl.so.1.1`/`libcrypto.so.1.1`; it provides `libssl.so.3`/`libcrypto.so.3`
  instead. The bundled `divinus`, `oko-*`, `wg`/`awg` and OpenVPN 2.7.8 are
  rebuilt and verified (`api_check` 98/98, RTSP/OSD), but third-party `/oem`
  binaries linked against the old ABI will not start. Since 2026-11-09 Python
  3.14 is not part of the image (`oko-prune.list`, defconfig), so
  `libpython3.14` and the Python packages are unavailable. Workaround: rebuild
  them from the SDK sources; for prebuilt blobs, ship compatibility libraries
  alongside (not recommended). Status: **accepted migration limitation
  (updated 2026-11-09)**.

- **KI-046. `luckfox_pico_w_defconfig` is not migrated.**
  The Wi-Fi/audio variant (`bluez5_utils`, `mpv`) still relies on the old
  `hcitool_patch`/`mpv_patch`; whether they apply to the newer package
  versions on Buildroot 2026.08.1 is untested. The base
  `luckfox_pico_defconfig` now includes BlueZ: `bluez5_utils 5.86` builds
  against uclibc-ng through
  `patches-2026.08/bluez5_utils/0001-build-provide-wordexp-fallback-...`
  (uClibc is built without `wordexp`), and the stale vendor patches
  `0001-fixed-header-file-errors`/`0002-fixed-wordexp`/
  `0003-fixed-compat-wordexp` were dropped from the tree — they are not part
  of the 2026.08.1 tarball and do not apply to 5.86. The w variant remains
  unverified. Workaround: do not switch `.BoardConfig.mk` to the w variant
  before rebasing the patches; otherwise disable the packages or update the
  patches. Status: **open for the w variant, untested**.

### Security

- **KI-029. The WebUI uses HTTP Basic without HTTPS.** There are no sessions or
  logout (`/exit` stops the server), and WebUI/RTSP/ONVIF passwords are stored
  in plaintext in `divinus.yaml`. Status: **accepted limitation**.

- **KI-030. The factory password must be changed.** WebUI/RTSP use `admin` /
  `oko-change-me`; the SoftAP (`OKO-XXXX`) uses the WPA2-PSK
  `ybrjveytljdthzq` while `oko_wlan_ap_psk` is unset. Change both before
  exposing the camera to an untrusted network: the SoftAP one via
  `oko-wifi set` or `/api/wifi`, and restore the factory one with
  `fw_setenv oko_wlan_ap_psk`. Status: **operational requirement**.

### Development tools

- **KI-047. Pre-migration builds (Docker/Ubuntu 22.04) are not reproducible.**
  The `luckfox_oko_build_env` container and `Dockerfile`/`start.sh` are gone,
  and Buildroot 2023.02.6 was replaced by 2026.08.1. The exact pre-migration
  image can only be rebuilt from `output/` artifacts or git history (commits
  before `9718258b0`). To retest the old behavior, use a `git worktree` at a
  pre-migration commit and the vendor rootfs from `IMAGE/`. Status:
  **accepted limitation**.

- **KI-058. The Wi-Fi PSK is stored in plain text in the U-Boot env.**
  `oko_wlan_psk` and `oko_wlan_ap_psk` live in `/dev/mtd0` unencrypted, like
  the Ethernet settings. The API does not return the station passphrase
  (`psk_set`), but the hotspot passphrase is available in `/api/wifi/apinfo`
  so a client can join it. Moving the secrets to protected storage is a
  separate decision. Status: **accepted limitation**.

- **KI-031. Headless Chromium needs `libasound.so.2`.** The host image lacks
  it. Without root: `apt-get download libasound2t64`, unpack with `dpkg-deb -x`
  and set `LD_LIBRARY_PATH` (see `bench/README.md`).
  Status: **host environment**.