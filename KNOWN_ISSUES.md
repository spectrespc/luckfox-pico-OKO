# Известные ошибки и ограничения OKO

Обновлено: `2026-11-17`. В файле — открытые проблемы, подтверждённые
ограничения и архив закрытых записей (§Архив). Проверенные функции и доказательства — в `VERIFIED_FUNCTIONS.md`,
состав и статус функционала — в `FEATURES_VERIFIED.md` / `FEATURES_UNVERIFIED.md`,
процедуры стенда — в `TESTBENCH.md`.

## Русский

### Критические

- **KI-001. `yolo.source=vi1` вешает плату.**
  Включение VI selfpath (chn1) через librockit приводит к зависанию RV1106 —
  нужна перезагрузка или перепрошивка. Значение заблокировано в `gating.c`,
  `/api/yolo` (HTTP 400) и WebUI, сохранённый `vi1` при старте заменяется на
  `vpss3`. Обход: `vpss3`; конфликт теперь только с MJPEG или четвёртым
  потоком. Переход на `vi0` при включённых рамках ведёт к KI-068.
  Статус: **заблокировано по дизайну**.

- **KI-002. RGA: im2d fd-путь работает, легаси blit — нет.**
  im2d `importbuffer_fd` импортирует MMZ- и dma-heap-дескрипторы (`rknpu`
  0.9.8, проверено `rgaImDemo --copy` и тестом MMZ NV12→RGB888),
  `yolo.convert: rga` снижает CPU `divinus` с ~35,6% до ~29,4% при 5 fps
  640×384 без потери fps, при ошибке RGA откатывается на CPU. Легаси
  `c_RkRgaBlit` и блит по `malloc`-памяти не используются
  (SIGSEGV/зависания) — принятая легаси-API; im2d fd-путь подтверждён в
  `FEATURES_VERIFIED.md`.
  Статус: **принятое ограничение (легаси-API), im2d fd-путь работает**.

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
  AIISP обрабатывает только кадр `vpss3`; `vi0` — `rkisp_mainpath`, кадр
  после 3A ISP **без** AIISP-постобработки (не «необработанный кадр»).
  Модель `yolov5n-640x384.rknn` собрана (`tools/rknn-toolkit2`, 60 кадров
  калибровки с DUT, 2,66 МБ), загружена и активирована на DUT: детектор
  работает, `person` ловится со score 0.35–0.69. Матрица C1–C4 прогнана в
  ambient-свете (`bench/yolo_night.py --phase ambient`, 4×40 с): лучший score
  0.69 / 0.64 / 0.53 / 0.47 и доля кадров с `person` 0.32 / 0.76 / 0.13 / 0.06
  (C1 `vpss3` off, C2 `vpss3` on, C3 `vi0` on, C4 `vi0` off). Числа **не
  опубликованы**: три повтора одной и той же C1 дали detection rate
  0.04 / 0.22 / 0.31 и score 0.51 / 0.74 / 0.61 — сцена (человек в кадре,
  работа 3A/экспозиции) нестабильна, критерий |Δscore| ≤ 0.05 не выполняется.
  На этой плате также нет ИК-подсветки и управляемого IR-cut: при штатном
  конфиге `/api/night` отдаёт `ir_cut_pin1=999`, `irled_pin=999`, узла ircut
  в DTS Pro Max нет, линза фиксированная IR-корректированная, поэтому «ночь»
  с ИК физически недоступна. Статус: **открыто (P5); модель и стенд готовы,
  замер ждёт неподвижную сцену и ИК-железо**.

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

- **KI-010. Рамки детекции рисуются только на main-потоке.**
  С 2026-11-15 `yolo.boxes` и `ivs.boxes` рисуют 2BPP-регионы на VENC main;
  на JPEG-канале и субпотоке рамок нет (гейт G4, KI-013), снимки событий —
  чистые JPEG. Статус: **принятое ограничение (топология main)**.

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
  подтверждены. IVS работает на VI chn2 (KI-065); комбинация всех четырёх
  потоков вместе с IVS не проверялась — KI-070.
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

- **KI-022. `post_chk` использует `rkipc-300w.ini` для SC230AI.**
  Шаблон рассчитан на сенсор 3 МП; при некорректной геометрии кадра ini нужно
  подбирать отдельно. Статус: **риск, проверить при смене сценария**.

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
  С 2026-11-14 `POST /api/stream` и карточка WebUI отклоняют multicast первым
  при наличии второго адреса (400, `dest` не меняется); уже записанный yaml
  API не переписывает. Статус: **принятое ограничение (P4)**.

- **KI-053. H.265 не играет в браузере.**
  Chromium не декодирует HEVC: страница `http://<хост>:8889/cam/` открывается,
  но WebRTC-сессия закрывается с `codecs not supported by client`, а HLS идёт
  как `hvc1` и в MSE не воспроизводится. H.265 смотрится в Safari (macOS) или
  нативном плеере (VLC/ffplay). Для браузера переключить камеру на H.264 и
  `rtpSDP` на `H264/90000` — проверено 2026-11-10: WebRTC играет 1080p30.
  Статус: **принятое ограничение (P4)**.

- **KI-054. USB-host: питание шины и классы за хабом не проверены.**
  Порт закреплён в host-режиме (`dr_mode = "host"`), классы HID/USB-net/BT
  включены в ядро. VBUS 5 В SoC не управляет: PHY запрашивает регулятор `vbus`
  из `vbus-supply` otg-port, в RV1106 DTS его нет, записи разрешения VBUS в
  `rv1106_phy_cfgs` тоже нет. Поэтому устройства за хабом питаются только от
  схемы платы/внешнего 5 В, а bus-powered хаб без внешнего питания может не
  подняться. Стенд 2026-11-11: после TFTP-прошивки xHCI поднимается при
  загрузке, хаб `1a40:0101` и устройство за ним (`0bda:c811`, Realtek
  802.11ac NIC) перечисляются без ручного вмешательства (`lsusb`, дерево
  `/sys/bus/usb/devices`). HID-устройство, USB-сеть и BT-донгл за хабом не
  подключались.
  Статус: **частично проверено; хаб — работает, HID/USB-сеть/BT — untested**.

- **KI-057. В host-режиме недоступны gadget, `usb0`/RNDIS и adb.**
  Порт закреплён в `dr_mode = "host"`: DWC3 не регистрирует UDC, поэтому
  `S50usbdevice` и `S99usb0config` выходят по `dr_mode` (иначе они писали бы
  в несуществующий UDC и запускали adbd без функции). Maskrom по USB виден
  только при прямом кабеле к хосту — установленный хаб его скрывает, поэтому
  прошивка по умолчанию переведена на TFTP (`TESTBENCH.md` §7-TFTP).
  Обход/возврат: `dr_mode = "otg"` в активном DTS, пересборка `boot` и
  прошивка (§7-TFTP или Maskrom напрямую). Молча не переключается.
  Статус: **принятое ограничение (by design)**.

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

- **KI-068. YOLO-рамки при `yolo.source: vi0` пересоздают 2BPP-регион
  каждую секунду.**
  2026-11-15, NAND, `ivs.boxes=1` + `yolo.boxes=1`, `yolo.source: vi0`:
  регион 12 создаётся заново ~18 раз за 60 с (`2BPP overlay region 12
  created` в консоли), на каждый кадр `runtimeGetCanvasIn:954 rgn [12]
  sync failed`, `[rk_venc] Main stream loop timed out!` и отказы
  `SetBitmap`/канвы у текстовых регионов 0–5. Причина: vi0 — selfpath,
  общий с main-потоком; после старта YOLO на vi0 канал пересобирается,
  `RK_MPI_RGN_GetCanvasInfo` для региона 12 возвращает ошибку,
  `yolo_boxes_render()` уничтожает регион, а `yolo_boxes_sync()` создаёт
  его снова — цикл. Отключение `yolo.boxes` останавливает цикл сразу.
  С `yolo.source: vpss3` тот же набор (IVS на VI chn2 + YOLO на vpss3 +
  оба 2BPP-региона) стабилен 30 мин, 90 с без единого пересоздания.
  Обход: держать `yolo.source: vpss3` (заводской дефолт); IVS больше не
  занимает VPSS-канал, поэтому конфликта нет — устаревшие проверки
  «vpss3 занят IVS» убраны из `gating.c` и `main.c`.
  Дополнительно: если во время такого цикла приходит SIGHUP, на выходе
  процесса возможно ядро-oops `valloc_release [rockit]` из `__fput`
  (наблюдалось 2026-11-15); процесс остаётся зомби, divinus не
  поднимается, нужен `reboot`.
  Статус: **открыто, обход — `yolo.source: vpss3`**.

- **KI-070. Учёт VPSS-канала 3 не обновлён после переноса IVS на VI chn2.**
  IVS больше не занимает VPSS-канал 3 (`RK_IVS_VI_CHN 2`, `rk_hal.c:2082`), но
  гейты и аллокатор остались на старой топологии: `gating.c:116` отказывает
  IVS, если поток уже занял chn3, а `media.c:462-467` продолжает резервировать
  слот 3 при `ivs.enable`. При полном пуле (main + sub + MJPEG + JPEG вместе с
  IVS) `take_next_free_channel()` возвращает −1, и `media.c:750-754` (MJPEG) и
  `jpeg.c:24-29` (JPEG) не выходят, а пишут в `chnState[-1]` — запись за
  границу массива вместо отказа. Обход: не включать все четыре VENC-потока
  вместе с IVS. Статус: **открыто (P2)**; лечение — правка кода отдельной
  задачей.

- **KI-071. SFC/UBIFS: DMA timeout и переход rootfs/userdata в read-only.**
  2026-11-16 после ~4,5 ч работы и 288 циклов suspend/resume (плюс серия
  power cycle и один неудачный `echo mem` с `EINVAL`) на стенде появились
  `rockchip-sfc ffac0000.spi: DMA wait for transfer finish timeout` и
  `wait sfc idle timeout`, затем `ubi_io_write/read: error -5` на ubi4/ubi5 и
  `UBIFS: make_reservation ... error -5`; `/` и `/userdata` перешли в `ro`,
  `sync`/`reboot` отвечали `Input/output error`. Power cycle через ESP32
  восстановил оба тома (`rw`) и обнулил счётчик ошибок; 1-часовой soak до
  этого прошёл с 0 ошибок NAND. Причина не разделена: известная деградация
  SPI NAND (TESTBENCH §8, 2026-11-14), незавершённый resume SFC или
  сочетание. Обход: power cycle; при повторении — прошивка по TFTP и
  проверка bad blocks. Статус: **открыто, требует наблюдения**; для `[sleep]`
  рекомендуется длительный прогон с мониторингом `dmesg | grep -i sfc` после
  каждого resume. 2026-11-16/17 AOV-эксперимент: после первого цикла
  suspend с AOV-ядром (затем и с пересобранным продуктовым ядром) DUT
  перестал доходить до userspace — `Starting kernel ...` и зависание около
  probe `rockchip-pm`; исходный продуктовый образ 09:35 восстановлен по
  TFTP, `bad PEBs: 0`, rootfs/userdata `rw`. Причина не разделена, но
  AOV-циклы усиливают риск: до выяснения AOV-профиль не выпускать. 2026-11-17
  14:47 повтор: после ~15 циклов suspend/resume появились
  `rockchip-sfc ffac0000.spi: DMA wait for transfer finish timeout` /
  `xfer data failed ret -110 dir 1`, UBIFS (`ubi0:0`) ушёл в read-only
  (`ubifs_leb_write ... error -5`, `make_reservation ... error -30`), а
  ближайший `PM: suspend entry` после этого не завершился пробуждением (AP
  остался спать, UART молчит). Power cycle через мост восстановил
  rootfs/userdata в `rw`, `dmesg` без SFC/UBI ошибок — сценарий KI-071
  повторяем. Уточнение
  2026-11-17: «зависание после `Starting kernel`» воспроизведено только на
  ядре со сдвинутым textofs и без `0x208000`-FIT (см. KI-072); «пересобранное
  продуктовое ядро» на стенде после этого **не перепроверялось** — адрес
  линковки сверен (`_text = 0x8000`), загрузка — нет.

- **KI-072. Профиль `-AOV`: 1M-раскладка и окно ядра/MCU исправлены,
  handshake MCU не отвечает.** Hpmcu (117 КБ) + SPL + DDR + usbplug дают
  idblock ~300 КБ; продуктовый idblock 256K расширен до 1M, все разделы
  после него сдвинуты на 768K (rootfs уменьшен до 167M, сумма 255.25 МБ).
  Отдельного meta-раздела нет — 256 МБ NAND заняты, поэтому blob лежит в
  `/oem/usr/share/oko/meta.img` и копируется в reserved-memory 0x01000000
  (ISP — 0x01060000). `CONFIG_RV1106_HPMCU_FAST_WAKEUP` поднимает `textofs`
  до 0x208000; чтобы ядро реально оказалось по этому адресу, `boot.img`
  профиля собирается из `Image.gz` FIT-ом `boot-aov.its`
  (`compression = "gzip"`, `load = entry = 0x208000`), и U-Boot распаковывает
  ядро сразу в 0x208000 — так же устроен вендорский thunder-boot
  (`output/bench/tb-sc230ai-wakeup/boot.img`). Обычная связка «zImage +
  загрузка по 0x8000» с этим textofs до userspace не доходит.
  2026-11-17 на стенде проверено: `/proc/mtd` idblock 1M / rootfs 167M,
  `/api/status.sleep.aov_available=true`, в консоли
  `Uncompressing GZIP Kernel Image ... to 0x00208000` и предупреждение об
  окне `rtos@40000` исчезло. Suspend с `aov_mcu=1` доходит до `-h-wait-` →
  `-h-timeout-` (2000 мс): MCU не отвечает по mailbox, AP возвращается по
  таймауту, цикл повторяется. Отрицательный прогон `aov_mcu=0` (PMU-only)
  циклится штатно (`rv1106 enter/exit sleep`). После патча KI-074
  (`gic400_restore`) прогон 2026-11-17 15:24 с `aov_mcu=1`: политика пишет
  `aov armed`, ядро дважды проходит `PM: suspend entry` →
  `Restarting tasks ... done.` → `PM: suspend exit`, AP не виснет. Ответ
  MCU по mailbox при этом не подтверждён: строки
  `-h-wait-`/`-h-mbox-`/`-h-timeout-` печатаются `rkpm_printstr` прямо в
  UART и в лог моста не попали. Статус: **экспериментальный, не для
  выпуска**; блокеры — handshake MCU, KI-073 (патч применён) и
  KI-074 (исправлено, проверено для PMU-only и AOV-armed циклов). Артефакты: `output/bench/aov-image-1m/`,
  `output/bench/uar0-aov-fit-boot-20261117.log`,
  `output/bench/aov-test/uar0-m2-pmu-only.log`.

- **KI-073. Паника ядра в `rk3x_i2c_irq` на I2C3 при циклах `[sleep]`.**
  2026-11-17 в AOV-прогоне (idle 30 s, `aov_mcu=1`) шина I2C3
  (`ff470000.i2c`, STM32 EC) выдала серию `rk3x-i2c ... irq in STATE_IDLE,
  ipd = 0x10` и `unexpected irq in STOP: 0x10`, затем
  `Unable to handle kernel NULL pointer dereference at virtual address
  00000002`, `PC is at rk3x_i2c_irq+0x4a/0x1be`, `Internal error: Oops - BUG:
  17 [#1] THUMB2`, `Kernel panic - not syncing: Fatal exception in interrupt`;
  плата ушла в reset. В том же прогоне `oko-ec power --state
  suspending/awake` отвечал `failed` — шина уже была в ошибке. Падает
  вендорский обработчик `rk3x_i2c` (не разбирает состояние после
  `unexpected irq`), это не suspend-путь: риск есть и в PMU-only циклах.
  Обход на время: `[sleep] enable: false`. Статус: **патч применён
  2026-11-17** — в `rk3x_i2c_irq()` после ack NAK добавлен guard
  `if (!i2c->msg) goto out;` (интеграционный патч
  `drivers/i2c/busses/i2c-rk3x.c`, см. CHANGELOG). После патча паника не
  воспроизводится в трёх прогонах suspend (до патча — на первом-втором
  цикле); без патча цикл на том же стенде теперь доходит до зависания
  resume (KI-074), поэтому остаточный блокер — KI-074. Артефакт паники:
  `output/bench/aov-test/console-m3-panic-attempt3.log`.

- **KI-074. Зависание AP на resume сразу после `rv1106 exit sleep`.**
  2026-11-17, AOV-образ (ядро по 0x208000), `[sleep]` с `aov_mcu=0`, idle
  30 s, `wake_interval_s=10`: первый suspend проходит штатно
  (`PM: suspend entry` → `rv1106 enter sleep` → DDR re-init →
  `wake up status:0x00000000` → `rv1106 exit sleep`), после чего AP
  замолкает: нет ни `[sleep] resumed`, ни `Restarting...`, SSH/HTTP не
  поднимаются, UART не отвечает на ввод. В одном прогоне (2026-11-17
  10:36) тот же сценарий повторялся штатно (`resumed (EC wake src -1)` →
  `wake probe` → следующий suspend), поэтому это гонка в resume, а не
  детерминированный дефект. Воспроизводится и на прошивке **без** патча
  I2C (KI-073), то есть к патчу отношения не имеет. Последние сообщения —
  `rk_gmac-dwmac ... Link is Down` / `init for RMII`; при опущенном `eth0`
  их нет, а зависание то же, значит стоп происходит в device-resume, а не
  только в GMAC/PHY. Статус: **причина найдена и исправлена 2026-11-17**:
  при `CONFIG_RV1106_HPMCU_FAST_WAKEUP=y` вендорский `gic400_restore()`
  писал только `GIC_DIST_CTRL`, оставляя восстановление контекста
  GIC-distributor AOV-MCU; когда MCU простаивает (`aov off`) или не
  отвечает (`-h-timeout-`), distributor остаётся в сброшенном состоянии,
  прерывания не доставляются и resume умирает в
  `suspend_thaw_processes()`. Патч `rv1106_pm.c`: контекст
  восстанавливается всегда (`rkpm_gicv2_dist_restore`), как и в
  вендорском timeout-пути. Стенд: AOV-ядро (0x208000, gzip-FIT) с
  `aov_mcu=0` прошло ~15 циклов (`[sleep] resumed (EC wake src 4)`,
  `Restarting tasks ... done.`); продуктовое ядро (флаг off) циклы
  проходило и раньше; путь `aov_mcu=1` после патча проверен 2026-11-17
  15:24 (два полных цикла в `dmesg`, см. KI-072). Артефакты:
  `output/bench/aov-test/console-nakguard-run1.log`,
  `console-ethdown-patched-run.log`, `console-prepatch-control.log`.
  Дополнение 2026-11-17 (диагностика `pm_test`): ядро собрано с
  диагностическим фрагментом `rv1106-pm-debug.config`
  (`CONFIG_PM_DEBUG`/`CONFIG_PM_TEST_SUSPEND`), загрузка — с
  `no_console_suspend initcall_debug ignore_loglevel`. Лестница
  `echo {freezer|devices|platform|processors|core} > /sys/power/pm_test` +
  `echo mem > /sys/power/state` проходит **все** уровни (RC=0, uptime
  растёт), в том числе с политикой divinus (`pm_test=core`: циклы идут,
  плата жива). Значит PM-ядро, teardown divinus и thaw в тестовом режиме
  работают, а стоп даёт именно реальный вход в сон (PMU SRAM/DDR:
  `rv1106 enter sleep` → WFI → `exit sleep`), после которого
  `suspend_thaw_processes()` не проходит (последний вывод —
  `OOM killer enabled.`). Замечание по методике: при `pm_test` HTTP-сервер
  divinus закрыт во время цикла, поэтому живость проверять SSH/uptime, а не
  `/api/sleep`. Следующие шаги: сравнить поведение на вендорском TB-ядре,
  разобрать DDR/PMU-stub (`rv1106_pm.c`, `rkpm_*`, `Jump to PMU_SRAM`) и
  проверить гипотезу порчи/несовместимости контекста после DDR re-init.

- **KI-064. `GET /api/update` роняет/подвешивает divinus.**
  2026-11-14: и старая сборка 0.3.0 (`/userdata/divinus.old`), и новая
  отвечают на запрос зависанием: в логе `New request: (GET) /api/update`,
  ответа нет, процесс завершается без `Graceful shutdown`/`SIGSEGV`, после
  чего HTTP недоступен. Повторяется после холодной загрузки и не связано с
  SPI NAND (запись/чтение разделов в этот момент работают). Обход: не
  опрашивать `/api/update`; `bench/api_check.py` исключает его из smoke-теста
  до разбора `update_json()`/`gating_sd_space()`. Статус: **открыто**.

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

- **KI-067. mDNS: announce-only без probing и один multicast-интерфейс.**
  `tinysvcmdns` не реализует probing/protection по RFC 6762 §8.1: responder
  только анонсирует записи, конфликт имён не разрешается. Коллизий не ждём —
  имя строится из MAC `eth0` (`oko-<4 hex>`), а не из общего `/etc/hostname`.
  Ответы идут одним сокетом с `IP_ADD_MEMBERSHIP(INADDR_ANY)` на интерфейс
  multicast-маршрута: discovery работает в сети основного аплинка, но SoftAP
  `192.168.43.0/24` и VPN-туннели не обслуживаются. Адрес SoftAP
  (`wlan0` `192.168.43.1`) при этом попадает в A-записи как обычный локальный
  IPv4, хотя из основной сети он недоступен: клиент должен выбирать адрес по
  маршруту, а не первый из ответа. Набор адресов сверяется
  раз в 10 с, поэтому после DHCP-renew или failover eth↔wlan клиент может
  получить старый адрес до ~11 с (запись в его кэше живёт до TTL 120 с).
  Смена адресов пересоздаёт responder: `mdnsd_set_hostname()` нельзя звать
  дважды на живом инстансе (`assert`). После OTA старый
  `/userdata/divinus.yaml` с `mdns.enable: false` оставляет публикацию
  выключенной до первого переключения в WebUI/`/api/mdns`; заводской шаблон
  `true` действует только при создании файла. Обходов нет; будущая опция —
  сокет на каждый интерфейс и service-записи (`_http._tcp`, `_rtsp._tcp`).
  Статус: **принятое ограничение**.

### VPN

- **KI-062. `oko-ec flash`: COMMIT метаданных не применяется.**
  Образ записывается, `VERIFY` возвращает OK, `COMMIT` печатает
  `0x00 (OK)`, но MetaConfig не меняется: после RESET загрузчик видит
  старые `img_size`/`seq`. Ручная последовательность
  `BEGIN`+`VERIFY`+`COMMIT` (без `WRITE_DATA`) записывает мета корректно
  (проверено SWD-дампом page0 seq15). Похоже на гонку: хост читает
  устаревший ответ `VERIFY`, пока загрузчик ещё выполняет erase/program
  метастраницы, и сразу шлёт RESET. Обход: прошить app/meta через SWD
  (`program` + `flash write_image erase meta.bin 0x08007800`) или
  отправить `COMMIT` вручную после `BEGIN`+`VERIFY`. Статус: **открыто,
  требует отдельного разбора `oko-ec`/тайминга COMMIT**.

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

### Архив закрытых записей

Компактный список закрытых проблем; действующие остатки указаны в строке
записи, номера не переиспользуются.

- KI-021. `motor.ko` удалён из форка: драйвер 28BYJ-48 не использовался, привод фокуса/зума — STM8 по I2C. — закрыто (2026-11-07).
- KI-023. `IMAGE/` — устаревший артефакт RV1103 Pico Mini, удалён 2026-11-08; для прошивки — `output/image/update.img`. — закрыто (2026-11-08).
- KI-036. Прошивка STM32 через WebUI: установлен CBT6 (128 КиБ), образы собраны с `-DMCU=CBT6`, CLI/WebUI/rollback и 9/9 негативных проверок пройдены. — закрыто (2026-11-06).
- KI-043. Включение AF-метрики больше не зависит от STM8: сервер применяет параметры до взведения привода, `motor.status` показывает причину. — закрыто (2026-11-07).
- KI-044. ePTZ: кроп/поворот применяются после `RK_MPI_VPSS_EnableChn`, смена на живом канале — через SIGHUP (`applied:false` + `note`). — закрыто (2026-11-07).
- KI-049. OTA из WebUI пройдено на стенде: `.okp` собран, применён из U-Boot `sd_update`, версия 0.3.0 совпала. — закрыто (2026-11-10).
- KI-050. Ветка `feature/slim-image` смержена в `main` (0.3.0). — закрыто (2026-11-14); гейт мержа пройден 2026-11-09–10.
- KI-055. `usb0` в режиме `otg`: `S99usb0config` определяет роль через `/sys/class/extcon/*/state`; в host-режиме не применимо. — закрыто (2026-11-10).
- KI-056. Legacy `g_mass_storage` ломал gadget: символ выключен в `rv1106-usb-host.config`, gadget — через configfs. — закрыто (2026-11-10).
- KI-061. STM32 EC удерживал SCL при рестарте RV1106: `I2C1SEL=10` (HSIKER), `TIMEOUTR` ~10 мс, bus-watchdog и `i2c_pause()`; 40 ребутов без повтора. — исправлено (2026-11-13); остаток: полный soak 300 ребутов не прогонялся (40/300).
- KI-063. Рамки IVS на 2BPP-регионе (handle 13): регион создаётся один раз за процесс, hard block снят, `ivs.boxes_mode: restart` по умолчанию. — закрыто (2026-11-15); остаток: `boxes_mode: live` — opt-in без soak 200 циклов; актуальный источник — `FEATURES_UNVERIFIED.md` (строка «Рамки IVS: живое переключение»), отдельная открытая запись не заводится.
- KI-065. IVS: ложные события без движения — введён `ivs.min_area` (площадь кадра в %), в событии `area`/`cause`, исправлена утечка MD-буфера. — исправлено (2026-11-15); остаток: OD не проверен — закрыт ссылкой на KI-015.
- KI-066. Паника `rga2_reg: irq handler err` при старте IVS сразу после остановки YOLO: после перевода IVS на VI chn2 не воспроизводится. — закрыто (2026-11-15); regression-watch: запись в архиве, не воспроизводится на VI chn2.
- KI-069. SIGHUP-рестарт утекал дескрипторами и CMA: перед `execvp()` все дескрипторы с 3 помечаются `FD_CLOEXEC`, 20 рестартов без роста fd. — закрыто (2026-11-15).

## English

Updated: `2026-11-16`. This file lists open problems, confirmed limitations and
an archive of closed entries (§Archive).
Verified functions are in `VERIFIED_FUNCTIONS.md`, feature status in
`FEATURES_VERIFIED.md` / `FEATURES_UNVERIFIED.md`, bench procedures in
`TESTBENCH.md`.

### Critical

- **KI-001. `yolo.source=vi1` hangs the board.** Enabling VI selfpath (chn1)
  locks RV1106 until a power cycle or reflash. The value is blocked in
  `gating.c`, `/api/yolo` (HTTP 400) and the WebUI; a saved `vi1` is replaced
  with `vpss3` at startup. Workaround: `vpss3`; the conflict is now only with
  MJPEG or the fourth stream. Switching to `vi0` with boxes enabled leads to
  KI-068. Status: **blocked by design**.

- **KI-002. RGA: the im2d fd path works, the legacy blit does not.**
  im2d `importbuffer_fd` imports MMZ and dma-heap descriptors (`rknpu` 0.9.8,
  verified with `rgaImDemo --copy` and an MMZ NV12→RGB888 test).
  `yolo.convert: rga` lowers `divinus` CPU from ~35.6% to ~29.4% at 5 fps
  640×384 without losing fps and falls back to the CPU path on an RGA error.
  Legacy `c_RkRgaBlit` and blits on `malloc` memory are not used
  (SIGSEGV/hangs) — an accepted legacy API; the im2d fd path is confirmed in
  `FEATURES_VERIFIED.md`.
  Status: **accepted limitation (legacy API), the im2d fd path works**.

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
  frames; `vi0` is `rkisp_mainpath`, the frame after 3A ISP **without** AIISP
  post-processing (it is not an "unprocessed" frame). The model
  `yolov5n-640x384.rknn` was built with `tools/rknn-toolkit2` (60 calibration
  frames from the DUT, 2.66 MB), uploaded and activated; the detector runs and
  finds `person` with score 0.35–0.69. The C1–C4 matrix was executed in
  ambient light (`bench/yolo_night.py --phase ambient`, 4×40 s): best score
  0.69 / 0.64 / 0.53 / 0.47 and person frame rate 0.32 / 0.76 / 0.13 / 0.06
  (C1 `vpss3` off, C2 `vpss3` on, C3 `vi0` on, C4 `vi0` off). The numbers are
  **not published**: three repeats of the same C1 gave detection rate
  0.04 / 0.22 / 0.31 and score 0.51 / 0.74 / 0.61 — the scene (a person in
  front of the camera, 3A/exposure changes) is unstable and the |Δscore| ≤
  0.05 criterion fails. The board also has no IR illuminator or switchable
  IR-cut: with the stock config `/api/night` reports `ir_cut_pin1=999`,
  `irled_pin=999`, the Pro Max DTS has no ircut node and the lens is fixed
  IR-corrected, so a physical IR night scene is not available.
  Status: **open (P5); the model and bench are ready, the run waits for a
  static scene and IR hardware**.

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

- **KI-010. Detection boxes are drawn on the main stream only.** Since
  2026-11-15 `yolo.boxes` and `ivs.boxes` paint 2BPP regions on VENC main; the
  JPEG channel and the substream have no boxes (gate G4, KI-013) and event
  snapshots stay clean JPEG. Status: **accepted limitation (main topology)**.

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
  object detection and a static-scene event are not. IVS runs on VI chn2
  (KI-065); all four streams together with IVS were not tested — KI-070.
  Status: **limited**.

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

- **KI-022. `post_chk` uses `rkipc-300w.ini` for SC230AI.** The template
  targets a 3 MP sensor; choose another ini if the frame geometry is wrong.
  Status: **risk, verify when the scenario changes**.

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
  multicast second. Since 2026-11-14 `POST /api/stream` and the WebUI card
  reject a multicast first entry when a second address exists (400, `dest` is
  left unchanged); the API does not rewrite an existing yaml. Status:
  **accepted limitation (P4)**.

- **KI-053. H.265 does not play in a browser.**
  Chromium has no HEVC decoder: `http://<host>:8889/cam/` opens, but the WebRTC
  session closes with `codecs not supported by client`, and HLS is offered as
  `hvc1`, which MSE rejects. H.265 plays in Safari (macOS) or a native player
  (VLC/ffplay). For browser playback switch the camera to H.264 and `rtpSDP` to
  `H264/90000` — verified 2026-11-10: WebRTC plays 1080p30.
  Status: **accepted limitation (P4)**.

- **KI-054. USB host: bus power and the classes behind a hub are unverified.**
  The port is pinned to host mode (`dr_mode = "host"`) with the HID/USB-net/BT
  classes built in. The SoC does not control VBUS 5 V: the PHY requests a `vbus`
  regulator from `vbus-supply` on the otg-port (absent from the RV1106 DTS), and
  `rv1106_phy_cfgs` has no VBUS enable register. Devices behind a hub are
  therefore powered only by the board supply or an external 5 V, and a
  bus-powered hub may not come up. Bench 2026-11-11: after flashing over TFTP
  xHCI comes up at boot, and the hub `1a40:0101` with the device behind it
  (`0bda:c811`, Realtek 802.11ac NIC) enumerates without manual steps (`lsusb`,
  `/sys/bus/usb/devices`). A HID device, USB networking and a BT dongle were not
  attached.
  Status: **partially verified; the hub works, HID/net/BT untested**.

- **KI-057. Host mode disables the gadget, `usb0`/RNDIS and adb.**
  The port is pinned to `dr_mode = "host"`: DWC3 registers no UDC, so
  `S50usbdevice` and `S99usb0config` exit on `dr_mode` instead of writing to a
  missing UDC and starting adbd without a function. Maskrom over USB is only
  visible with a direct cable to the host - the attached hub hides it - so the
  default flashing mode moved to TFTP (`TESTBENCH.md` §7-TFTP).
  Workaround/rollback: set `dr_mode = "otg"` in the active DTS, rebuild `boot`
  and flash (§7-TFTP or Maskrom with a direct cable). Not switched silently.
  Status: **accepted limitation (by design)**.

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

- **KI-068. YOLO boxes with `yolo.source: vi0` rebuild the 2BPP region every
  second.**
  2026-11-15, NAND, `ivs.boxes=1` + `yolo.boxes=1`, `yolo.source: vi0`:
  region 12 is recreated about 18 times per 60 s (`2BPP overlay region 12
  created` on the console), every frame logs `runtimeGetCanvasIn:954 rgn [12]
  sync failed`, followed by `[rk_venc] Main stream loop timed out!` and failed
  `SetBitmap`/canvas updates for text regions 0–5. Cause: vi0 is the selfpath
  shared with the main stream; after YOLO starts on vi0 the channel is
  rebuilt, `RK_MPI_RGN_GetCanvasInfo` fails for region 12,
  `yolo_boxes_render()` destroys the region and `yolo_boxes_sync()` creates it
  again — a loop. Turning `yolo.boxes` off stops it immediately. With
  `yolo.source: vpss3` the same set (IVS on VI chn2 + YOLO on vpss3 + both
  2BPP regions) is stable for 30 min and shows no re-creation in 90 s.
  Workaround: keep `yolo.source: vpss3` (factory default); IVS no longer
  occupies a VPSS channel, so the stale "vpss3 is taken by IVS" checks were
  removed from `gating.c` and `main.c`.
  Note: if a SIGHUP arrives during that loop, the process exit can hit a
  kernel oops `valloc_release [rockit]` from `__fput` (observed 2026-11-15);
  the process stays a zombie, divinus is not respawned and a `reboot` is
  required.
  Status: **open, workaround is `yolo.source: vpss3`**.

- **KI-070. VPSS channel 3 accounting is stale after IVS moved to VI chn2.**
  IVS no longer occupies VPSS channel 3 (`RK_IVS_VI_CHN 2`, `rk_hal.c:2082`),
  but the gates and the allocator still follow the old topology:
  `gating.c:116` refuses IVS when a stream already owns chn3, and
  `media.c:462-467` keeps reserving slot 3 while `ivs.enable` is set. With a
  full pool (main + sub + MJPEG + JPEG together with IVS)
  `take_next_free_channel()` returns −1 and `media.c:750-754` (MJPEG) and
  `jpeg.c:24-29` (JPEG) do not bail out but write to `chnState[-1]` — an
  out-of-bounds write instead of a refusal. Workaround: do not run all four
  VENC streams together with IVS. Status: **open (P2)**; the fix is a separate
  code change.

- **KI-071. SFC/UBIFS: DMA timeout and rootfs/userdata switch to read-only.**
  On 2026-11-16, after ~4.5 h and 288 suspend/resume cycles (plus a series of
  power cycles and one failed `echo mem` returning `EINVAL`), the bench showed
  `rockchip-sfc ffac0000.spi: DMA wait for transfer finish timeout` and
  `wait sfc idle timeout`, then `ubi_io_write/read: error -5` on ubi4/ubi5 and
  `UBIFS: make_reservation ... error -5`; `/` and `/userdata` became `ro` and
  `sync`/`reboot` returned `Input/output error`. A power cycle through the
  ESP32 restored both volumes (`rw`) and cleared the error counter; the
  1-hour soak before that had zero NAND errors. The cause is not separated:
  the known SPI NAND degradation (TESTBENCH §8, 2026-11-14), an incomplete
  SFC resume or a combination. Workaround: power cycle; on recurrence reflash
  over TFTP and check bad blocks. Status: **open, needs observation**; for
  `[sleep]` a longer run with `dmesg | grep -i sfc` after every resume is
  recommended. 2026-11-16/17 AOV experiment: after the first suspend cycle
  with the AOV kernel (and then with the rebuilt product kernel) the DUT
  stopped reaching userspace — `Starting kernel ...` followed by a hang
  near the `rockchip-pm` probe; the original 09:35 product image was
  restored over TFTP, `bad PEBs: 0`, rootfs/userdata `rw`. The cause is not
  separated, but AOV cycles raise the risk: do not release the AOV profile
  until this is understood. 2026-11-17 14:47 recurrence: after ~15
  suspend/resume cycles the log showed
  `rockchip-sfc ffac0000.spi: DMA wait for transfer finish timeout` /
  `xfer data failed ret -110 dir 1`, UBIFS (`ubi0:0`) switched to read-only
  (`ubifs_leb_write ... error -5`, `make_reservation ... error -30`) and the
  next `PM: suspend entry` never woke (the AP stayed asleep and the UART was
  silent). A bridge power cycle restored rootfs/userdata to `rw` with no
  SFC/UBI errors in `dmesg`, so the KI-071 scenario reproduces. Update
  2026-11-17: the "hang after `Starting
  kernel`" reproduces only with a moved textofs and no 0x208000 FIT (see
  KI-072); the "rebuilt product kernel" has **not** been retested on the
  bench — only its link address was checked (`_text = 0x8000`).

- **KI-072. `-AOV` profile: 1M layout and the kernel/MCU window are fixed,
  the MCU handshake does not answer.** Hpmcu (117 KB) + SPL + DDR + usbplug
  make a ~300 KB idblock; the product 256K idblock is enlarged to 1M and every
  following partition moves by 768K (rootfs shrinks to 167M, total 255.25 MB).
  There is no meta partition — the 256 MB NAND is full — so the blob lives in
  `/oem/usr/share/oko/meta.img` and is copied into reserved memory at
  0x01000000 (ISP at 0x01060000). `CONFIG_RV1106_HPMCU_FAST_WAKEUP` raises
  `textofs` to 0x208000; for the kernel to actually land there the profile's
  `boot.img` is built from `Image.gz` with `boot-aov.its`
  (`compression = "gzip"`, `load = entry = 0x208000`) and U-Boot unpacks the
  kernel straight into 0x208000 — the same layout as the vendor thunder-boot
  image (`output/bench/tb-sc230ai-wakeup/boot.img`). The stock "zImage loaded
  at 0x8000" combination does not reach userspace with this textofs.
  Verified on the bench 2026-11-17: `/proc/mtd` idblock 1M / rootfs 167M,
  `/api/status.sleep.aov_available=true`, the console shows
  `Uncompressing GZIP Kernel Image ... to 0x00208000` and the `rtos@40000`
  overlap warning is gone. A suspend with `aov_mcu=1` reaches `-h-wait-` →
  `-h-timeout-` (2000 ms): the MCU does not answer the mailbox, the AP
  returns on the timeout and the cycle repeats. The `aov_mcu=0` (PMU-only)
  run cycles cleanly (`rv1106 enter/exit sleep`). After the KI-074 patch
  (`gic400_restore`) the 2026-11-17 15:24 run with `aov_mcu=1` shows the
  policy arming AOV (`aov armed`) and the kernel completing
  `PM: suspend entry` → `Restarting tasks ... done.` → `PM: suspend exit`
  twice, so the AP no longer hangs. The MCU mailbox answer is not confirmed:
  the `-h-wait-`/`-h-mbox-`/`-h-timeout-` strings are printed by
  `rkpm_printstr` straight to the UART and never reached the bridge log.
  Status: **experimental, not for release**; blockers are the MCU handshake,
  KI-073 (patched) and KI-074 (fixed, verified for PMU-only and AOV-armed
  cycles). Artifacts: `output/bench/aov-image-1m/`,
  `output/bench/uar0-aov-fit-boot-20261117.log`,
  `output/bench/aov-test/uar0-m2-pmu-only.log`.

- **KI-073. Kernel panic in `rk3x_i2c_irq` on I2C3 during `[sleep]` cycles.**
  On 2026-11-17 an AOV run (idle 30 s, `aov_mcu=1`) got a burst of
  `rk3x-i2c ... irq in STATE_IDLE, ipd = 0x10` and
  `unexpected irq in STOP: 0x10` on I2C3 (`ff470000.i2c`, STM32 EC),
  followed by `Unable to handle kernel NULL pointer dereference at virtual
  address 00000002`, `PC is at rk3x_i2c_irq+0x4a/0x1be`,
  `Internal error: Oops - BUG: 17 [#1] THUMB2` and
  `Kernel panic - not syncing: Fatal exception in interrupt`; the board reset.
  In the same run `oko-ec power --state suspending/awake` answered `failed` —
  the bus was already in an error state. The vendor `rk3x_i2c` handler
  (it does not handle the state after `unexpected irq`) is what crashes, not
  the suspend path, so PMU-only cycles carry the same risk. Workaround for
  now: `[sleep] enable: false`. Status: **patched 2026-11-17** —
  `rk3x_i2c_irq()` has an `if (!i2c->msg) goto out;` guard after the NAK ack
  (integration patch in `drivers/i2c/busses/i2c-rk3x.c`, see CHANGELOG). The
  panic no longer reproduces in three suspend runs (before the patch it hit
  on the first or second cycle); without the patch the same bench now stops
  at the resume hang (KI-074), so the remaining blocker is KI-074. Panic
  artifact: `output/bench/aov-test/console-m3-panic-attempt3.log`.

- **KI-074. AP hangs on resume right after `rv1106 exit sleep`.**
  2026-11-17, AOV image (kernel at 0x208000), `[sleep]` with `aov_mcu=0`,
  idle 30 s, `wake_interval_s=10`: the first suspend completes normally
  (`PM: suspend entry` → `rv1106 enter sleep` → DDR re-init →
  `wake up status:0x00000000` → `rv1106 exit sleep`), then the AP goes
  silent: no `[sleep] resumed`, no `Restarting...`, SSH/HTTP never come back
  and the UART does not answer input. In one run (2026-11-17 10:36) the same
  scenario cycled normally (`resumed (EC wake src -1)` → `wake probe` → next
  suspend), so this is a resume race rather than a deterministic defect. It
  reproduces on the build **without** the I2C patch (KI-073), so it is not
  caused by that patch. The last messages are `rk_gmac-dwmac ... Link is
  Down` / `init for RMII`; with `eth0` down they disappear but the hang stays,
  so the stop is in device resume, not only in GMAC/PHY. Status: **root
  cause found and fixed on 2026-11-17**: with
  `CONFIG_RV1106_HPMCU_FAST_WAKEUP=y` the vendor `gic400_restore()` wrote
  only `GIC_DIST_CTRL` and left the distributor context to the AOV MCU; when
  the MCU is idle (`aov off`) or does not answer (`-h-timeout-`) the
  distributor stays reset, interrupts are no longer delivered and the resume
  dies in `suspend_thaw_processes()`. The `rv1106_pm.c` patch always
  restores the context (`rkpm_gicv2_dist_restore`), matching the vendor
  timeout path. Bench: the AOV kernel (0x208000, gzip FIT) with `aov_mcu=0`
  ran ~15 cycles (`[sleep] resumed (EC wake src 4)`, `Restarting tasks ...
  done.`); the product kernel (flag off) cycled before; the `aov_mcu=1` path
  after the fix was verified on 2026-11-17 15:24 (two full cycles in
  `dmesg`, see KI-072). Artifacts:
  `output/bench/aov-test/console-nakguard-run1.log`,
  `console-ethdown-patched-run.log`, `console-prepatch-control.log`.
  Update 2026-11-17 (`pm_test` diagnosis): the kernel is built with the
  diagnostic fragment `rv1106-pm-debug.config` (`CONFIG_PM_DEBUG`/
  `CONFIG_PM_TEST_SUSPEND`) and booted with
  `no_console_suspend initcall_debug ignore_loglevel`. The ladder
  `echo {freezer|devices|platform|processors|core} > /sys/power/pm_test` +
  `echo mem > /sys/power/state` passes **every** level (RC=0, uptime keeps
  growing), including with the divinus policy (`pm_test=core`: the cycles
  run and the board stays alive). So the PM core, the divinus teardown and
  the thaw all work in test mode, and the stop comes from the real sleep
  entry (PMU SRAM/DDR: `rv1106 enter sleep` → WFI → `exit sleep`), after
  which `suspend_thaw_processes()` does not complete (the last output is
  `OOM killer enabled.`). Methodology note: with `pm_test` the divinus HTTP
  server is closed during a cycle, so liveness must be checked over SSH or
  `/proc/uptime`, not `/api/sleep`. Next steps: compare with the vendor TB
  kernel, inspect the DDR/PMU stub (`rv1106_pm.c`, `rkpm_*`,
  `Jump to PMU_SRAM`) and test the context-corruption hypothesis after the
  DDR re-init.

- **KI-064. `GET /api/update` hangs/kills divinus.**
  On 2026-11-14 both the previous 0.3.0 binary (`/userdata/divinus.old`) and
  the new build hang on the request: the log shows
  `New request: (GET) /api/update`, no reply follows, and the process exits
  without `Graceful shutdown`/`SIGSEGV`, leaving HTTP unavailable. It
  reproduces after a cold boot and is not related to the SPI NAND (partition
  reads/writes work at that moment). Workaround: do not poll `/api/update`;
  `bench/api_check.py` excludes it from the smoke test until
  `update_json()`/`gating_sd_space()` is investigated. Status: **open**.

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

- **KI-067. mDNS: announce-only without probing, one multicast interface.**
  `tinysvcmdns` does not implement RFC 6762 §8.1 probing/protection: the
  responder only announces records, so a name conflict is not resolved.
  Conflicts are not expected — the name comes from the `eth0` MAC
  (`oko-<4 hex>`), not from the shared `/etc/hostname`. Replies go through one
  socket with `IP_ADD_MEMBERSHIP(INADDR_ANY)` on the multicast-route interface:
  discovery works on the main uplink, but the SoftAP subnet
  `192.168.43.0/24` and VPN tunnels are not served. The SoftAP address
  (`wlan0` `192.168.43.1`) is still advertised as an ordinary local IPv4 even
  though it is unreachable from the main LAN: a client must pick the address by
  route, not the first answer. The address set is
  reconciled every 10 s, so after a DHCP renew or an eth↔wlan failover a client
  may see the old address for up to ~11 s (its cache keeps the record up to the
  120 s TTL). An address change recreates the responder because
  `mdnsd_set_hostname()` must not be called twice on a live instance
  (`assert`). After an OTA an old `/userdata/divinus.yaml` with
  `mdns.enable: false` keeps publication off until the first toggle in the
  WebUI/`/api/mdns`; the factory `true` applies only when the file is created.
  No workaround; future options are a per-interface socket and service records
  (`_http._tcp`, `_rtsp._tcp`). Status: **accepted limitation**.

### VPN

- **KI-062. `oko-ec flash`: the metadata COMMIT is not applied.**
  The image is written and `VERIFY` returns OK, and `COMMIT` prints
  `0x00 (OK)`, but MetaConfig does not change: after RESET the bootloader
  still reports the old `img_size`/`seq`. A manual
  `BEGIN`+`VERIFY`+`COMMIT` sequence (without `WRITE_DATA`) writes the
  metadata correctly (verified with an SWD dump of page0 seq15). This
  looks like a race: the host reads a stale `VERIFY` reply while the
  bootloader is still erasing/programming the metadata page, then sends
  RESET immediately. Workaround: flash app/meta over SWD (`program` plus
  `flash write_image erase meta.bin 0x08007800`) or send `COMMIT` manually
  after `BEGIN`+`VERIFY`. Status: **open, needs a separate `oko-ec`/COMMIT
  timing investigation**.

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

### Closed entries archive

A compact list of closed problems; live residuals are named inside the entry,
and numbers are never reused.

- KI-021. `motor.ko` was removed from the fork: the 28BYJ-48 driver was unused; focus/zoom is driven by the STM8 over I2C. — closed (2026-11-07).
- KI-023. `IMAGE/` held an outdated RV1103 Pico Mini artifact, removed on 2026-11-08; flash `output/image/update.img`. — closed (2026-11-08).
- KI-036. STM32 update through the WebUI: the installed MCU is CBT6 (128 KiB), images are built with `-DMCU=CBT6`, and the CLI/WebUI/rollback plus 9/9 negative checks passed. — closed (2026-11-06).
- KI-043. Enabling the AF metric no longer depends on the STM8: the server applies the parameters before arming the motor and `motor.status` reports the cause. — closed (2026-11-07).
- KI-044. ePTZ: crop/rotation are applied after `RK_MPI_VPSS_EnableChn`; a change on a live channel goes through SIGHUP (`applied:false` + `note`). — closed (2026-11-07).
- KI-049. WebUI OTA passed on the bench: the `.okp` was built, applied by U-Boot `sd_update`, and the version matched 0.3.0. — closed (2026-11-10).
- KI-050. The `feature/slim-image` branch was merged into `main` (0.3.0). — closed (2026-11-14); the merge gate passed on 2026-11-09–10.
- KI-055. `usb0` in `otg` mode: `S99usb0config` detects the role through `/sys/class/extcon/*/state`; not applicable in host mode. — closed (2026-11-10).
- KI-056. The legacy `g_mass_storage` driver broke the gadget: the symbol is off in `rv1106-usb-host.config` and the gadget uses configfs. — closed (2026-11-10).
- KI-061. The STM32 EC held SCL when the RV1106 rebooted: `I2C1SEL=10` (HSIKER), `TIMEOUTR` ~10 ms, a bus watchdog and `i2c_pause()`; 40 reboots with no repeat. — fixed (2026-11-13); residual: the full 300-reboot soak was not run (40/300).
- KI-063. IVS boxes on the 2BPP region (handle 13): the region is created once per process, the hard block is removed, and `ivs.boxes_mode: restart` is the default. — closed (2026-11-15); residual: `boxes_mode: live` is opt-in without a 200-cycle soak; the current source is `FEATURES_UNVERIFIED.md` (the "IVS boxes: live switching" row), and no separate open entry is filed.
- KI-065. IVS: false events without motion — `ivs.min_area` (percent of the frame) and `area`/`cause` in the event were added, and the MD buffer leak was fixed. — fixed (2026-11-15); residual: OD is unverified and covered by KI-015.
- KI-066. Kernel panic `rga2_reg: irq handler err` when IVS starts right after YOLO stops: not reproducible after IVS moved to VI chn2. — closed (2026-11-15); regression-watch: the entry stays in the archive and is not reproducible on VI chn2.
- KI-069. A SIGHUP restart leaked descriptors and CMA: before `execvp()` every descriptor from 3 is marked `FD_CLOEXEC`, and 20 restarts show no fd growth. — closed (2026-11-15).
