# Известные ошибки и ограничения OKO

Обновлено: `2026-11-08`. В файле — открытые проблемы, подтверждённые
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

- **KI-019. WiFi AIC8800 не проверялся.**
  Модули собираются и копируются в `/oem/usr/ko`, но стек WiFi на стенде не
  тестировался; сетевые проверки выполнены по Ethernet.
  Статус: **ограничено**.

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

- **KI-045. Смена ABI rootfs: OpenSSL 3 и Python 3.14.**
  После перехода на Buildroot 2026.08.1 в rootfs нет
  `libssl.so.1.1`/`libcrypto.so.1.1` и `libpython3.11`; вместо них
  `libssl.so.3`/`libcrypto.so.3` и `libpython3.14`. Штатные `divinus`,
  `oko-*`, `wg`/`awg`, OpenVPN 2.7.8 пересобраны и проверены
  (`api_check` 98/98, RTSP/OSD), но сторонние сборки в `/oem`, слинкованные
  со старым ABI, не запустятся. Обход: пересобрать из исходников SDK; для
  готовых blob'ов — совместимые копии библиотек рядом (не рекомендуется).
  Статус: **принятое ограничение миграции**.

- **KI-046. `luckfox_pico_w_defconfig` не мигрирован.**
  Wi-Fi/audio-вариант (`bluez5_utils`, `mpv`) остался на старых патчах
  `hcitool_patch`/`mpv_patch`; на Buildroot 2026.08.1 их применение к новым
  версиям пакетов не проверялось. Активный `luckfox_pico_defconfig` эти
  пакеты не использует — сборка образа OKO не затронута. Обход: не
  переключать `.BoardConfig.mk` на w-вариант до ребейса патчей; при
  необходимости — отключить пакеты или обновить патчи.
  Статус: **открыто, untested**.

### Безопасность

- **KI-029. WebUI работает по HTTP Basic без HTTPS.**
  Сессий и Logout нет (`/exit` останавливает сервер), пароли WebUI/RTSP/ONVIF
  хранятся в `divinus.yaml` открытым текстом и не читаются через API.
  Статус: **принятое ограничение**.

- **KI-030. Фабричный пароль нужно сменить.**
  WebUI/RTSP: `admin` / `oko-change-me`. До подключения камеры к недоверенной
  сети пароль сменить. Статус: **требование эксплуатации**.

### Инструменты разработки

- **KI-047. Старые сборки (Docker/Ubuntu 22.04) не воспроизводятся.**
  Контейнер `luckfox_oko_build_env` и `Dockerfile`/`start.sh` удалены;
  Buildroot 2023.02.6 заменён на 2026.08.1. Собрать точную прошивку до
  миграции можно только из артефактов `output/` или из истории git
  (коммиты до `9718258b0`). Для повторной проверки старого поведения
  используйте `git worktree` на коммите до миграции и вендорский
  rootfs из `IMAGE/`. Статус: **принятое ограничение**.

- **KI-031. Headless Chromium требует `libasound.so.2`.**
  В хостовом окружении библиотеки нет. Обход без root:
  `apt-get download libasound2t64`, распаковать `dpkg-deb -x` и задать
  `LD_LIBRARY_PATH` (см. `bench/README.md`). Статус: **особенность хоста**.

## English

Updated: `2026-11-08`. This file lists open problems, confirmed limitations and
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

- **KI-019. WiFi AIC8800 is untested.** Modules are built and copied to
  `/oem/usr/ko`, but the WiFi stack was not exercised; networking was verified
  over Ethernet. Status: **limited**.

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

- **KI-045. Rootfs ABI change: OpenSSL 3 and Python 3.14.**
  After the Buildroot 2026.08.1 migration the rootfs no longer ships
  `libssl.so.1.1`/`libcrypto.so.1.1` or `libpython3.11`; it provides
  `libssl.so.3`/`libcrypto.so.3` and `libpython3.14` instead. The bundled
  `divinus`, `oko-*`, `wg`/`awg` and OpenVPN 2.7.8 are rebuilt and verified
  (`api_check` 98/98, RTSP/OSD), but third-party `/oem` binaries linked
  against the old ABI will not start. Workaround: rebuild them from the SDK
  sources; for prebuilt blobs, ship compatibility libraries alongside (not
  recommended). Status: **accepted migration limitation**.

- **KI-046. `luckfox_pico_w_defconfig` is not migrated.**
  The Wi-Fi/audio variant (`bluez5_utils`, `mpv`) still relies on the old
  `hcitool_patch`/`mpv_patch`; whether they apply to the newer package
  versions on Buildroot 2026.08.1 is untested. The active
  `luckfox_pico_defconfig` does not use these packages, so the OKO image
  build is unaffected. Workaround: do not switch `.BoardConfig.mk` to the
  w variant before rebasing the patches; otherwise disable the packages or
  update the patches. Status: **open, untested**.

### Security

- **KI-029. The WebUI uses HTTP Basic without HTTPS.** There are no sessions or
  logout (`/exit` stops the server), and WebUI/RTSP/ONVIF passwords are stored
  in plaintext in `divinus.yaml`. Status: **accepted limitation**.

- **KI-030. The factory password must be changed.** WebUI/RTSP use `admin` /
  `oko-change-me`; change it before exposing the camera to an untrusted
  network. Status: **operational requirement**.

### Development tools

- **KI-047. Pre-migration builds (Docker/Ubuntu 22.04) are not reproducible.**
  The `luckfox_oko_build_env` container and `Dockerfile`/`start.sh` are gone,
  and Buildroot 2023.02.6 was replaced by 2026.08.1. The exact pre-migration
  image can only be rebuilt from `output/` artifacts or git history (commits
  before `9718258b0`). To retest the old behavior, use a `git worktree` at a
  pre-migration commit and the vendor rootfs from `IMAGE/`. Status:
  **accepted limitation**.

- **KI-031. Headless Chromium needs `libasound.so.2`.** The host image lacks
  it. Without root: `apt-get download libasound2t64`, unpack with `dpkg-deb -x`
  and set `LD_LIBRARY_PATH` (see `bench/README.md`).
  Status: **host environment**.