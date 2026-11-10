# Пакет divinus для OKO

Альтернативный IPC-стример [OpenIPC/divinus](https://github.com/OpenIPC/divinus)
(MIT) для RV1106. Штатным стримером остаётся `rkipc`; divinus запускается только при
наличии маркера `/userdata/divinus.enable` (см. `INIT_ORDER.md`).

## Источник и пин

| Поле | Значение |
|---|---|
| Upstream | `https://github.com/OpenIPC/divinus` |
| Закоммиченный коммит | `bca1932ed8b348af68fb33a82a67218e8f45db7a` |
| Дата вендоринга | `2026-11-01` |
| Лицензия | MIT (`source/LICENSE`, `source/COPYING`) |
| Каталог исходников | `source/` (`src/`, `res/`, `doc/`, лицензии) |

`source/src/Makefile` собирает цель `../divinus`, то есть `project/app/divinus/divinus`.
Вендорные библиотеки (`librockit.so`, `librkaiq.so`) подключаются через
`dlopen`/`dlsym` во время работы и в сборке не участвуют.

## Состав пакета

- `Makefile` — обёртка SDK: кросс-сборка через `$(RK_APP_CROSS)-gcc`, стейджинг
  `out/usr/bin/divinus` и `out/usr/share/divinus/divinus.yaml` в `RK_APP_OUTPUT`.
- `files/divinus.yaml` — фабричный конфиг для платы Pico Pro Max / SC230AI.
- `tests/bmp_align_test.c` — хостовый тест выравнивания растра OSD (не входит в
  сборку пакета, см. правило 5 ниже).
- `tests/rk_rgn_pack_test.c` — хостовый тест упаковки `ARGB1555 → BGRA5551` и
  выравнивания на 16 (не входит в сборку пакета).
- `tests/yolo_postprocess_test.c` — хостовый тест декода/NMS YOLOv5 (не входит
  в сборку пакета).
- `tests/nn_geom_test.c` — хостовый тест геометрии NN-рамок и якоря OSD
  (`source/src/analytics/nn_geom.c`; не входит в сборку пакета).
- `source/src/analytics/` — OKO-модуль NPU-аналитики (`yolo.c`,
  `yolo_postprocess.c`, `models.c`).
- `source/` — вендорские исходники без `.git`; `build.sh` и `remote.sh` upstream не
  копируются: кросс-тулчейн берётся из SDK. В `source/src/` внесены три локальных
  патча (`compat.c`, `region.c`, `hal/plus/rk_hal.c`, см. ниже).

## Патч upstream: ABI `mmap` в `compat.c`

`source/src/compat.c` для ARM определяет собственный `mmap`. Пакет собирается с
`-D_FILE_OFFSET_BITS=64` (из `RK_APP_OPTS`), а `src/Makefile` линкует бинарь с
`-rdynamic`, поэтому этот 64-битный `mmap` перехватывает `mmap` у `librockit.so`,
который собран с 32-битным `off_t`. Offset читается из неверного stack-слота, и
отображение выходного кольца VENC падает (`mmap part 1 failed!`), энкодер не отдаёт
кадры. На uClibc hard-float локальный `mmap` не нужен: libc уже даёт оба варианта
(`mmap`/`mmap64`), поэтому в `compat.c` определение `mmap` для
`__ARM_PCS_VFP && __UCLIBC__` отключено (определение `__mmap64` оставлено).

Проверено на стенде без LD_PRELOAD: `/image.jpg` — JPEG 1920x1080 (~65 КБ), RTSP
`rtsp://admin:<пароль>@<ip>:554/` — H.265, ~1 МБ RTP за 5 с, `/api/status` —
`rv11xx`/`sc230ai`.

## Патч upstream: `@file`-провайдер текста OSD

В upstream текст региона (`reg%d_text`) читается только парсером конфига при
старте, а поток `region_thread` (`source/src/region.c`) пере-рендеривает регион
лишь при `updt=1` или при наличии `$`-макроса. Поэтому внешний меняющийся текст
(телеметрия, сообщения) без правки ядра OSD не обновляется.

Локальный патч в `region.c` добавляет соглашение: если текст региона начинается
с `@` и содержит путь (`@/tmp/osd-dyn.txt`), то:

- каждую секунду читается первая строка файла (до 255 байт, `\r`/`\n`
  отбрасываются);
- при изменении содержимого относительно прошлого прохода выставляется `updt=1`,
  и регион пере-рендеривается; сам `osds[id].text` не мутируется, поэтому
  `@...` остаётся шаблоном на весь прогон и переживает сохранение из WebUI;
- если файл пуст или не читается, регион гасится (`*_region_destroy`) и
  появляется снова, когда в файле появится текст;
- `$`-макросы (`$t`, `$T`, `$C`, `$M`, `$B`) работают и внутри файла; `$$` —
  литеральный `$`.

Почему безопасно: изменение затрагивает только регионы, чей текст начинается с
`@`; прежнее поведение для обычных строк и картинок не меняется, формат конфига
и дамп из WebUI остаются прежними (`@...` хранится как opaque-строка).

Ограничения:

- пользовательский текст, начинающийся с `@`, будет воспринят как путь к файлу
  (это соглашение OSD, а не пользовательских сообщений);
- отображается только первая строка файла (до 255 байт; буфер `@file`-
  провайдера увеличен локальным патчем с 80 до 256 байт); остальные строки
  файла игнорируются, а переводы строк внутри первой строки задаются
  литеральными `\n`;
- файл перечитывается раз в секунду: обновления чаще не имеют смысла.

Потребитель патча — утилита `oko-osd` из оверлея
`overlay-luckfox-buildroot-init`: GPS пишется в `/tmp/osd-gps.txt`, остальная
телеметрия (`T/BAT/LTE/WiFi`, четыре строки) — в `/tmp/osd-dyn.txt`, сообщения
из `/tmp/osd.fifo` — в `/tmp/osd-msg.txt`, реальные CPU/память/температура —
в `/tmp/osd-sys.txt`. Время — правый нижний угол, GPS — правый верхний,
телеметрия — левый верхний, сообщения — под ней, CPU/память/температура —
левый нижний. Значения обновляются раз в 15 с (системные — раз в 5 с);
размер шрифта регионов задаётся `regN_size` в
`files/divinus.yaml` (сейчас `24.0` px), цвет — `regN_color: 0x07e0`
(ярко-зелёный RGB565), контур — `regN_outl: 0x8000` (чёрный) толщиной
`regN_thick: 1.0`.

`@file`-провайдер (`region.c`) читает только первую строку файла, а
`text.c` распознаёт перенос только как literal `\n` (backslash + n). Поэтому
многострочные файлы (`/tmp/osd-sys.txt`) должны содержать `\n` внутри строки,
а не реальные `0x0A`; иначе в кадре останется только первая строка.

OSD виден в RTSP (VENC-канал 0). Снимок `/image.jpg` кодируется отдельным
VENC-каналом JPEG; одна RGN-область на RV1106 не прикрепляется ко второму
каналу, поэтому snapshot OSD не содержит (для JPEG нужны отдельные
shadow-области — вынесено за рамки этой итерации).

## Патч upstream: RGN OSD на RV1106 (`hal/plus/rk_hal.c`)

Upstream-код OSD для RK-платформ рассчитан на другой вариант `librockit` и
не учитывает ограничения RV1106:

- оверлей принимает только `BGRA5551`, а текстовый растр создаётся в
  `ARGB1555` — упаковка каналов вынесена в `hal/plus/rk_rgn_pack.c`
  (`rk_rgn_pack_bgra5551()`), её используют и HAL, и хостовые тесты;
- позиция и размер региона должны быть выровнены на 16 пикселей —
  `rk_region_create()` выравнивает `x`/`y`/ширину/высоту, растр дополняется
  прозрачными пикселями до того же размера (`rk_rgn_pad16()`);
- структура `BITMAP_S` в используемом `librockit.so` содержит 32-битное поле
  между размером и указателем (`pData` на смещении 16); вендорный
  `rk_rgn_bmp` без этого поля передаёт `pData` не туда, и
  `RK_MPI_RGN_SetBitMap` возвращает «Bitmap data is NULL»;
- `librockit` выделяет canvas региона только при явно заданном
  `u32CanvasNum` — в патче выставляется `region.overlay.canvas = 1`: один
  canvas, как у OSD в `rkipc`; двойная буферизация может показывать текст
  предыдущей секунды до следующего обновления;
- при неизменных позиции и прозрачности повторный
  `RK_MPI_RGN_AttachToChn()` не вызывается: RV1106 отклоняет повторное
  прикрепление (и может привести к oops в `mpi_obj_ref`).

Обновление региона: если `librockit.so` экспортирует
`RK_MPI_RGN_GetCanvasInfo`/`RK_MPI_RGN_UpdateCanvas`, `rk_region_setbitmap()`
пишет растр прямо в canvas региона (`u64VirAddr`) и вызывает `UpdateCanvas` —
без `calloc` и копии на каждый кадр. Символы грузятся `dlsym` как
необязательные: если их нет, `GetCanvasInfo` вернул ошибку или размер/stride
canvas не совпал с выровненным растром, остаётся прежний путь
`RK_MPI_RGN_SetBitMap` (с одним предупреждением на handle). Ошибка canvas не
роняет приложение.

Позиция и `fgAlpha` меняются через `RK_MPI_RGN_GetDisplayAttr`/
`RK_MPI_RGN_SetDisplayAttr`; detach/attach остаётся только для первого
подключения и как fallback, если `SetDisplayAttr` вернул ошибку. Побочно
исправлено: смена `opal` через `/api/osd/<id>` при неизменной позиции раньше
игнорировалась.

Патч затрагивает только RK-ветку HAL (`hal/plus/rk_hal.c`, `rk_rgn.h`,
`rk_rgn_pack.c/.h`) и не меняет поведение других платформ.

## Медиа-фичи OKO (этап divinus-media-features)

Добавлены семь групп возможностей; каждая закрыта гейтом на стенде
(`source/doc/README.md` — таблица результатов и ограничения):

| Возможность | Состояние | API / конфиг |
|---|---|---|
| ePTZ (цифровой зум, кроп VPSS) | работает | `/api/eptz`, `[eptz]` |
| Поворот 90/180/270 | работает через VENC | `/api/rotate`, `[rotate]` |
| SmartP/SVC/motion deblur/QP | работает (SmartP — со следующего старта) | `/api/venc`, `[venc]` |
| Второй H.26x поток `/live/1` | работает | `/api/substream`, `[substream]` |
| AIISP (ночная шумоподавка) | работает при наличии модели в oem | `/api/aiisp`, `[aiisp]` |
| IVS (детекция движения/объектов) | работает (MD) | `/api/ivs`, `[ivs]` |
| HDR2/HDR3 | заблокировано: prepare падает на SC230AI | `/api/hdr`, `[hdr]` |
| OSD на main+sub+JPEG | заблокировано: multi-attach не прошёл G4 | `[osd] scope` |
| Ethernet: DHCP/static | MAC и IPv4-настройки в env-разделе U-Boot, проверено | `/api/network`, `oko-net`, `fw_setenv` |
| microSD, формат и квота записей | формат FAT32/MBR, mount/unmount, запись и hotplug проверены на стенде 2026-11-04 | `/api/storage`, `oko-sdcard`, `[record]` |

Ключевые технические решения:

- **Каналы.** На RV1106 `VPSS_MAX_CHN_NUM == 4`, а VENC-канал использует
  VPSS-канал с тем же индексом. `chnCount` уменьшен до 4; при включённом IVS
  индекс 3 занят аналитикой, поэтому VENC-слотов три (main/sub/JPEG), а MJPEG
  должен быть выключен. IVS на каналах 4/5 возвращает `ILLEGAL_PARAM`.
- **Поворот.** `RK_MPI_VPSS_SetChnRotation` возвращает успех, но не меняет
  картинку; рабочий путь — `RK_MPI_VENC_SetChnRotation` (SPS становится
  1080x1920). VPSS-поворот оставлен как fallback. Включение канала сбрасывает
  поворот, поэтому он применяется заново в `rk_channel_bind()` сразу после
  `EnableChn`, а смена на лету сохраняется и применяется graceful-рестартом
  приложения (`/api/rotate`, KI-044).
- **ePTZ.** `RK_MPI_VPSS_SetChnCrop` с абсолютными координатами; выходной
  канал сохраняет 1920x1080, кроп масштабируется обратно. Код возврата MPP
  сохраняется и отдаётся в `/api/eptz` полями `applied`/`rc`/`chn`/`crop`,
  так что «зум применился» отличим от «библиотека отказала». Включение канала
  сбрасывает кроп, поэтому он применяется в `rk_channel_bind()` сразу после
  `EnableChn` (до этого вызова `SetChnCrop` принимается, но картинку не
  меняет — проверено 2026-11-07). Смена на лету только сохраняется, затем
  сервер перезапускает приложение (`app_config_save` + SIGHUP), и новый кроп
  применяется при старте канала; повтор того же запроса рестарта не вызывает
  (KI-044 закрыт).
- **Геометрия оверлеев.** NN-рамки маппятся из нормализованных координат
  детекции через кроп и поворот (`analytics/nn_geom.c`, хост-тест
  `tests/nn_geom_test.c`) и выравниваются по 4 px, а не по сетке 16 px.
  Текстовый OSD переякоривается при 90/270 так, чтобы сохранить угол и отступ
  (`nn_map_anchor()` в `rk_region_create2`).
- **AIISP.** Модель ставится в `/oem/usr/lib` из `media_out/lib` по
  `RK_AIISP_MODEL` в BoardConfig. `pModelFilePath` в `librockit` живёт дольше
  вызова, поэтому HAL передаёт статическую строку; без этого
  `rkPostIspInit` падает с `-1`. `u32FrameBufCnt=1` на RV1106.
- **IVS.** Отдельный поток с результатами `RK_MPI_IVS_GetResults`; стек 256 КиБ
  (32 КиБ приводили к падению процесса), действия — HTTP POST снимка, клип
  `record_start_event`, индикатор `/tmp/osd-ivs.txt` (регион `reg5`).
- **RTSP.** `rtsp_stream_t` на путь, `/live/0` (main) и `/live/1` (sub);
  SDP, sprop, payload type, timestamp и `request_idr` — на каждый поток.
  Старые клиенты без пути продолжают получать main. `PLAY` читает канал
  напрямую: вызов блокирующего геттера под уже взятым мьютексом RTSP давал
  дедлок, останавливал видеопоток и приводил к `SetBitmap 0xfffffffe`.
  Неизвестный/выключенный `/live/<n>` теперь 404, а не main.
- **Второй поток и OSD.** Канал sub создаётся в порядке `main → sub → JPEG`
  (только он стабилен с RGN на RV1106). Выключение делает soft-stop и
  сохраняет канал, включение возобновляет RTSP. Если канала ещё нет или
  меняются параметры энкодера, конфиг сохраняется и приложение один раз
  перезапускается. Запись любого параметра ePTZ/VENC, как и раньше,
  не разрушает регионы.
- **Конфиг.** `MAX_SECTIONS` увеличено с 16 до 40: новый набор секций
  выходил за лимит и сохранённый конфиг выглядел «пустым». Сохранение OSD
  приведено к двум пробелам (`regX_*`), иначе после рестарта секция `osd`
  терялась.
- **Хранилище.** `oko-sdcard` (udev) монтирует карту в `/mnt/sdcard`, пишет
  `/run/oko-sd.state` и по `/api/storage?action=format` создаёт на SD-карте
  (`mmcblk` с `device/type=SD`) один MBR-раздел FAT32 с меткой `OKO`
  (`wipefs` + `sfdisk` +
  `mkfs.vfat`). Формат требует `confirm=FORMAT` и одноразовый `format_token`,
  заблокирован при активной записи и сериализован с udev через
  `/run/oko-sd.lock`. `/api/storage` также даёт `mount`/`unmount`, `device`,
  `fs`, `format_supported`, `format_busy`. Запись разрешена только ниже
  реально смонтированного `/mnt/sdcard`; `/oem`, `/userdata`, `/tmp` и `/`
  запрещены. При исчезновении карты текущий сегмент закрывается, запись
  останавливается, NAND не используется. `record.max_bytes`/`min_free_mb`
  чистят старые сегменты. DTS `&sdmmc` переведён из `non-removable`/SDIO в
  режим съёмной карты с `sdmmc0_det`. В WebUI «Система → Статус» рядом с
  «Памятью» выводятся объём и свободное место этого тома (`GiB`/`MiB`,
  прочерк при несмонтированной карте); действия с картой остаются в
  «Накопителе».
- **Ethernet.** `/api/network` и WebUI **Network → Ethernet** переключают
  `dhcp`/`static`; `oko-net` пишет `oko_net_mode`, `oko_ipaddr`,
  `oko_netmask`, `oko_gateway`, `oko_dns` и `oko_mac` в env-раздел U-Boot
  через `fw_printenv`/`fw_setenv`. MAC выводится из
  `/proc/device-tree/serial-number` и не теряется вместе с `/userdata`.
  `S41dhcpcd` заменён заглушкой; единственный DHCP-клиент — `dhcpcd` с client
  ID по MAC (без DUID), поэтому адрес не меняется после перепрошивки.
- **Образ.** Для активной SC230AI-платы собирается только `sc230ai.ko` и один
  IQ-файл; из oem удалены `sample_*`/`simple_*` и сенсорные модули других плат
  (`sc3336`, `mis5001`, `imx415`, `mia1321` и другие). Диагностические
  `rk_*_test`/`rk_mpi_*_test` сохранены.
- **Запись H.265.** Вендорский VENC-пак для HEVC отдаётся одним NAL-элементом
  на весь access unit, поэтому `record.c` теперь сам режет Annex B через
  `nal_split` и раскладывает VPS/SPS/PPS/IDR/слайсы по типам. Раньше
  H.265-сегмент получался нулевого размера: muxer не видел parameter sets.
  Квота проверяется при открытии сегмента (текущий может временно превысить
  `max_bytes` до следующей ротации).

## OKO: NPU-аналитика YOLOv5 (`analytics/`)

`[yolo] enable: true` запускает детекцию после `sdk_start()`:
`analytics/yolo.c` держит кольцо NV12-кадров, раз в `fps` готовит
NV12→RGB888, вызывает выбранный мини-рантайм через `dlopen`
(`hal/plus/rk_rknn.h`, `hal/plus/rknn_api.h`) и декодирует три выхода
`analytics/yolo_postprocess.c`. Событие = индикатор в `/tmp/osd-ivs.txt` +
`pre_seconds`/`post_seconds` кадров, которые отдельный VENC-канал **MJPG 4**
кодирует и отправляет N `PUT` с `X-Event-*` (`http_post.c`).

Два рантайма: `yolo.runtime: 1` — `librknnmrt.so` 1.6.0 (общий с
AIISP/RockIVA), `2` — `librknnmrt2.so` 2.x с отдельным SONAME. `models.json`
помнит рантайм модели, активация «чужой» модели отклоняется с причиной;
переключение runtime не требует рестарта. `yolo.convert: cpu | rga`
(по умолчанию `cpu`): RGA-путь отдаёт im2d fd кольца (MMZ) и входа
`rknn_create_mem` (dma-heap), при ошибке один раз пишет предупреждение и
навсегда откатывается на CPU. Легаси `c_RkRgaBlit` по-прежнему не
используется. Кольцо лежит в MMZ и отображается через `mmap(fd)`:
`MB_Handle2VirAddr` для MMZ-блока валиден только на первые 4 КиБ.
`yolo.source=vi1` (selfpath) вешает плату и отклоняется `gating.c`,
`/api/yolo` и WebUI; дефолт — `vpss3` (при IVS/MJPEG — `vi0`). Модели:
`/userdata/models` (`yolo.model_dir`), активация через
`POST /api/models/activate` с откатом; INT8 `.rknn` 1.6.0 или 2.x, вход
должен совпадать с `yolo.input_width/height`.

`yolo.boxes: true` (по умолчанию `false`, API `/api/yolo?boxes=…`) рисует
рамки на основном потоке — порт вендорского NN-оверлея rkipc: регион RGN
`handle 12`, `layer 6`, формат 2BPP с двумя цветами (класс 0 — красный,
остальные — синий), канва 1920×1088 ≈ 518 КиБ вместо 4.1 МиБ BGRA5551.
Перерисовка идёт в `yolo_capture_thread` после каждого инференса: канва
полностью очищается, поэтому при нуле детекций рамки исчезают; координаты
`0..10000` проходят через кроп и поворот (`nn_geom.c`), масштабируются в
размер закодированного главного потока (при повороте 90/270 — с обменом
сторон) и выравниваются по 4 px, как требует 2BPP-растр. Включение/выключение применяется на лету, регион
уничтожается при остановке детектора. Если в librockit нет
`RK_MPI_RGN_GetCanvasInfo`/`UpdateCanvas`, детектор продолжает работать, а
рамки отключаются с одним предупреждением (`gating` не даст стартовать YOLO
с `boxes=1`). Регион пересоздаётся при смене размера/поворота главного
потока и при потере привязки к пересозданному VENC-каналу.

## OKO: фокус и прошивка контроллеров

`[focus] enable: true` (или `POST /api/focus?enable=1`) запускает поток
`focus.c` после `sdk_start()`. Поток 5 Гц читает AF-статистику rkaiq через
`hal/plus/rk_af_stats.c` — единственный TU с заголовками `rkaiq` (символы
`af_GetAttrib`/`af_SetAttrib`/`get3AStatsBlk`/`release3AStatsRef` берутся
`dlsym`, поэтому сборка без AF не падает). Метрика — сумма
`fv_h1+h2+v1+v2` по 225 ячейкам с пропуском `highlit` и `luma==0`;
`metric_avg` — среднее по валидным ячейкам, `metric_smooth` — EMA.
В `mode=roi` агрегируются ячейки, центры которых попадают в ROI (координаты
в базисных пунктах 0…10000); на этой сборке RV1106 сетка AFM остаётся
полнокадровой, поэтому ROI применяется в `focus.c`, а не аппаратным окном.
В IQ SC230AI **обязательно** `af_v31.TuningPara.contrast_af.enable=1` (оба
блока, день и ночь); при `0` все ячейки AFM нулевые. `GET /api/focus?map=1`
отдаёт исходную карту 225 ячеек. Неудачное применение окна повторяется,
пока AIQ не готов; нулевая карта дважды включает `contrast_af` как fallback
гейта A6.

Карточка «Помощь фокусировке» живёт в разделе «Видео» (`sec-live`): при
входе в раздел предпросмотр запускается сам с каскадом MP4 → MJPEG → снимок
`/image.jpg` (браузер без HEVC не декодирует H.265 и через ~4 с переходит на
резервную ветку с toast-пометкой), при уходе из раздела постоянное
chunked-соединение освобождается. Каркас занимает ровно высоту окна
(`100dvh` с фолбэком `100vh`), прокручивается только `#content`; на широком
экране (≥ 60em) «Видео» — две колонки: превью слева, карточка фокуса справа
со своей прокруткой. Высота предпросмотра ограничена `calc(100dvh - 250px)`
в двух колонках и `calc(100dvh - 430px)` в одной, с сохранением пропорций;
рамка `#focus-box` позиционируется от прямоугольника медиаэлемента, поэтому
letterbox не смещает ROI. На ширине ≤ 40em включается мобильный режим:
sidebar становится нижней сеткой 4×2 с короткими подписями (`nav_short_*`),
кнопки не ниже 44 px, статус-строка скрывает uptime/load/температуру, а
тосты поднимаются над навбаром. Зона выбирается двухшагово: кнопка
«Выделить зону» включает
режим, drag запоминает `pendingRoi` (в координатах стрима) без POST, «Готово»
оставляет рамку, Esc/«Отмена» её сбрасывают, а «Применить» пересчитывает
mirror/flip и отправляет ROI вместе с `af=1`. Вне режима drag показывает
подсказку. Поля «Включить/Режим/Частота» участвуют в общей модели
Apply/dirty: правка включает «Применить» и жёлтую точку на «Видео», а
двухсекундный опрос `focusPoll` перезаписывает поля только при чистой
группе — несохранённый выбор не затирается. Изменённое состояние
принудительно принимается только по явным действиям: успешное «Применить»,
STOP и кнопка «Автофокус» (`af=fine`). Кнопка «Применить» по-прежнему всегда
шлёт `motor=1`, но сервер применяет `enable`/`mode`/`fps`/ROI до взведения
привода: при занятой или отсутствующей I2C-шине метрика включается, а в
ответе остаётся `motor.enabled=false` с причиной (KI-043 закрыт). Чистые
команды движения и `af=1|fine` по-прежнему требуют взведённого драйвера.
`[mp4] enable` в коде (`app_config.c`) по умолчанию `true`;
фабричный `files/divinus.yaml` уже включает MP4 с H.265, сохранённые
конфиги `/userdata` не мигрируются. Поворот 90°/270° при выборе ROI не
учитывается.

Ручное управление объективом живёт в той же карточке: селекторы шага
(1/10/50/200) и скорости (50–400), кнопки FOCUS/ZOOM ◀▶ (один относительный
`move` на клик), STOP всех моторов и readout позиций. Переключателя
«Мотор» нет: каждая команда движения несёт `motor=1` и взводит драйвер в
том же запросе, шкала задаётся `scale=focus|zoom|both`, а STOP отправляет
`motor=0&stop=1&af=cancel`. `GET /api/focus` опрашивает контроллер и без
взведения, чтобы UI знал о его наличии. Границы шкал —
`focus.min_pos/max_pos` (0/2935) и `focus.zoom_min_pos/zoom_max_pos`
(0/2275), скорости — `focus.speed` и `focus.zoom_speed` (обе 300), всё
переопределяется в `divinus.yaml`. Кнопка «Автофокус» рядом с jog-кнопками
запускает `enable=1&motor=1&af=fine`: локальную донастройку
±`af_coarse` (150) шагом `af_fine` (25), которая всегда завершается `done`
и не требует контрастной сцены; полный поиск остаётся за «Применить»
(`af=1`).

Принудительный сброс: `POST /api/focus?home=focus|zoom|both` прижимает шкалу
к отрицательному упору чанками по ≤32000 шагов, ждёт `IDLE` и отправляет
`SET_POS(0)`; `zero=...&value=N` только задаёт позицию. Задача выполняется в
`focus_home.c` отдельным потоком (один слот), её состояние отдаётся в
`GET /api/focus` (`home.state/travel/done/error`), повторный запуск и
прошивка контроллера во время job’а получают HTTP 409. Нужна прошивка STM8
2.0.2 или 2.1.1 (гейт по версии); позиция вне `[-1000, max_pos+1000]`
считается испорченной и homing отклоняется с советом использовать Zero.

Автофокус: `POST /api/focus?af=1` (вместе с `enable=1&motor=1`, при
необходимости `mode/roi/fps` в том же запросе) запускает в `focus_af.c`
фоновый поиск по `metric_avg`. Сначала зонд направления половинным
coarse-шагом, затем coarse-проход до `af_patience` неулучшений или края
шкалы, затем fine-проход между лучшими точками и переход к лучшей позиции.
После каждого хода выдерживается `af_settle_ms` и берётся медиана трёх
свежих кадров; сырое среднее выбрано потому, что `metric_smooth` — EMA и
отстаёт на несколько кадров. Поиск не выходит за
`[max(0, min_pos)+home_margin, max_pos-home_margin]` (KI-042), ограничен
`af_timeout_s` и прерывается по `status==error`; плоская сцена даёт ошибку
«peak not found», а не ложный успех. `af=fine` — та же задача в режиме
micro-fine: окно ±`af_coarse` вокруг текущей позиции, шаг `af_fine`,
безусловный `done` и `af.scope="fine"` в ответе. Состояние — в
`GET /api/focus`
(`af.state/scope/position/best_position/metric/best_metric/progress/done/error`);
`af=cancel` или `stop=1` останавливают job. Во время поиска home/zero,
ручные движения и `POST /api/flash` получают 409; STOP в карточке остаётся
доступен и отменяет поиск. Параметры `af_*` задаются в `[focus]`
(`app_config.c`, закомментированный блок в `files/divinus.yaml`).

`[flash] enable: true` включает карточки прошивки контроллеров.
`POST /api/flash` сохраняет multipart-тело в `/tmp/fw-<target>-<pid>.<ext>`
(расширение сохраняется, чтобы `oko-motor` отличил Intel HEX от raw-образа),
проверяет заголовок `OKO8`, CRC32 и диапазон STM8 офлайн и запускает воркер.
Воркер останавливает `S21oko-ec`, приостанавливает мотор фокуса, запускает
`/oem/usr/bin/oko-flash` и разбирает `PROGRESS nn` / `Progress: … NN%`.
`GET /api/firmware` отдаёт снимок STM32 из `/tmp/oko-ec.json` и роль/версию
STM8, которую `firmware_status.c` опрашивает с частотой 0.2 Гц; приложение
STM8 отвечает на общую команду `0x08` как `[role][major][minor][patch]`,
поэтому boot-команда `0x10` не уходит в работающее приложение. Прошивка
STM8 и загрузчик — в `project/app/stm8-motor`; первая запись — `factory.ihx`
через ST-Link, дальнейшие обновления — через `/api/flash`.

## OKO: обновление ПО из WebUI (OTA)

Карточка «Обновление ПО» в разделе «Система» готовит обновление на microSD и
передаёт саму прошивку штатному механизму U-Boot `sd_update`; ядро SPI NAND не
пишет. Локальный модуль — `source/src/update.c/.h`, оверлейный помощник —
`/usr/bin/oko-update`, пост-проверка — `/etc/init.d/S20oko-update`.

Пакет `.okp` — несжатый tar с `oko-update.conf` (манифест: версия, части,
sha256, смещения и полные размеры разделов) и образами. Хост-утилита:
`tools/native/make_oko_update.sh --version 0.3.0`. В v1 обновляются только
`boot`, `oem` и `rootfs`; `env`/`idblock`/`uboot`/`userdata` не трогаются.

API (все запросы требуют ту же авторизацию, что и остальной WebUI):

- `GET /api/update` — снимок: `enabled`, `sd_mounted`, `free_mb`,
  `max_package_mb`, `current_version` (из `/etc/oko-release`), `tool`,
  `pending`, `blocked/reason/hint`, `staged{staged,version,parts,ok,error}`,
  `last{timestamp,ok,version,expected,error}`, `job{state,progress,message,
  received,total,started,finished}` и одноразовый `apply_token`;
- `POST /api/update/upload` — тело запроса целиком является `.okp`
  (`Content-Type: application/octet-stream`, обязателен `Content-Length`).
  Это единственный эндпоинт, который обходит общий 4 МиБ буфер: `server.c`
  распознаёт его по заголовкам и пишет тело потоком прямо на карту;
- `POST /api/update/fetch?url=…` — `curl --proto =http,https` скачивает пакет
  на карту и запускает проверку; `http://` разрешён только при
  `update.allow_insecure_url: true`, адреса устройства (`localhost`, `127.*`,
  `[::1]`) отклоняются;
- `POST /api/update/apply` — требует `confirm=APPLY` и `token` из GET;
  helper ещё раз сверяет sha256, пишет `fw_setenv oko_update 1` и
  `/userdata/oko-update.pending`, после чего воркер отвечает и перезагружает
  плату; токен одноразовый;
- `POST /api/update?clear=1` — удаляет подготовленные файлы с карты;
- `POST /api/update?cancel=1` — прерывает загрузку/скачивание/воркер.

Помощник `oko-update` (`stage`) проверяет манифест, размеры (не больше полного
раздела) и sha256 каждого образа, затем кладёт образы в корень FAT и пишет
`sd_update.txt` последним. Строки скрипта повторяют проверенный в TESTBENCH §7B
формат (`mw.b` → `fatload mmc 1` → `mtd erase spi-nand0 <offset> <full size>` →
`mtd write`), но каждая обёрнута в `if env exists oko_update; then … fi`:
U-Boot вызывает `sd_update` при каждой загрузке, поэтому подготовленные файлы
сами по себе ничего не прошивают — только выставленный `apply` флаг. В хвосте
той же обёртки — `env delete oko_update; env save`, строки `reset` нет.
`S20oko-update` на следующей загрузке сравнивает `/etc/oko-release` с ожидаемой
версией, пишет результат в `/userdata/oko-update.state` и удаляет файлы с карты,
поэтому оставленная карта не перепрошивает плату повторно. Если Linux не
загрузился (обрыв питания во время прошивки), пост-проверка не выполняется,
`sd_update.txt` остаётся и U-Boot повторяет прошивку при следующей подаче
питания.

Настройки — секция `[update]` (`app_config.c`, закомментированный блок в
`files/divinus.yaml`): `enable` (по умолчанию `true`), `max_package_mb` (`200`),
`allow_insecure_url` (`true` для v1, где HTTPS-загрузка не проверена на стенде).

Полный цикл проверен на стенде 2026-11-10: upload 61,8 МБ → stage → apply →
`sd_update` стёр и записал `boot`/`oem`/`rootfs` → postcheck `ok=1`, версия
0.3.0, карта очищена. `mmc 1` — слот microSD, HTTPS работает, `/oem` после
полного стирания монтируется. Флаг `oko_update` виден U-Boot, потому что ключ
добавлен в `CONFIG_ENVF_LIST` (`oko_mac`/`oko_net_*` — тоже, чтобы `env save`
не терял сетевые настройки).

Ограничения v1: подписи пакета нет (только sha256 из манифеста и проверка
размера); A/B-разделов нет; обновление возможно только при смонтированной
FAT32 microSD.

## Правила обновления

1. Клонировать upstream с `--depth 1`, заменить `source/src`, `source/res`, `source/doc`,
   `source/COPYING`, `source/LICENSE`, удалить `.git`.
2. Записать в таблицу выше новый **полный** хеш (`git rev-parse HEAD`) и дату.
3. Обновить `DIVINUS_REV` (короткий хеш) в `Makefile`.
4. **Повторно применить все локальные патчи**:
   - `compat.c`: на uClibc hard-float не определять собственный `mmap`
     (см. раздел выше), иначе вернётся `mmap part 1 failed!`;
   - `region.c`: `@file`-провайдер текста OSD (см. раздел выше), иначе
     телеметрия и сообщения `oko-osd` не отображаются;
   - `hal/plus/rk_hal.c`, `hal/plus/rk_rgn.h`, `hal/plus/rk_rgn_pack.c/.h`:
     BGRA5551, выравнивание на 16, поле `BITMAP_S` перед указателем данных,
     canvas-обновление с fallback на `SetBitmap`, `SetDisplayAttr` для
     позиции/прозрачности и однократное прикрепление региона для RV1106
     (см. раздел выше), иначе OSD не отображается или приводит к oops.
   - `hal/types.h`, `hal/globals.h`, `hal/support.c`, `hal/plus/rk_vpss.h`,
     `hal/plus/rk_venc.h`, `hal/plus/rk_aiq.h`, `hal/plus/rk_ivs.h`,
     `hal/plus/rk_sys.h`: типы и обёртки ePTZ/поворота/VENC/AIISP/IVS; новые
     символы грузятся через `dlsym` и не ломают загрузку на старом
     `librockit`.
   - `media.c`/`media.h`: роли каналов, второй поток, применение ePTZ,
     поворота, AIISP, IQ и VENC-расширений.
   - `rtsp/`: `rtsp_stream_t` на путь, `/live/0` и `/live/1`, SDP/sprop/
     timestamp/IDR на поток.
   - `source/src/analytics/` и правки `rk_hal.c`/`http_post.*`/`server.c`/
     `gating.c`/`app_config.*`/`res/index.html`: NPU-аналитика (см. раздел
     «OKO: NPU-аналитика»), иначе сборка потеряет YOLO-модуль.
   - `app_config.*` + `hal/config.h`: новые секции и `MAX_SECTIONS=40`
     (иначе сохранённый конфиг обрезается на 16 секциях).
   - `server.c`, `gating.*`, `ivs_events.*`, `record.*`, `res/index.html`:
     API, блокировки, аналитика, квота записей и WebUI.
   - `files/divinus.yaml`: регион `reg5` для индикатора IVS и новые секции.
- `focus.c/.h`, `focus_geom.*`, `focus_proto.*`, `focus_motor.*`,
      `focus_home.*`, `firmware_status.*`, `flash.*`, `hal/plus/rk_af_stats.*`
      и `MEDIA_INCLUDE` в `Makefile`: фокус-ассистент, мотор STM8 `0x60`,
      home/zero и прошивка контроллеров (см. раздел «OKO: фокус и прошивка»).
   - `server.c`/`res/index.html`/`app_config.*`/`gating.c`: `/api/focus`,
     `/api/flash`, `/api/firmware`, карточки Image/System и секции
     `[focus]`/`[flash]`.
5. Перед коммитом пересобрать (`./build.sh` на хосте) и проверить наличие
   `output/out/oem/usr/bin/divinus` — цикл сборки приложений не прерывается на ошибке
   отдельного пакета. После применения HAL-патча прогнать хостовые тесты и
   дождаться `ok 660x46 rows placed at stride 672` и `ok rk_rgn_pack`:
   ```sh
   gcc -std=c99 -Wall -Wextra -O2 -Iproject/app/divinus/source/src/hal/plus \
       project/app/divinus/source/src/hal/plus/rk_rgn_pack.c \
       project/app/divinus/tests/bmp_align_test.c -o /tmp/bmp_align_test &&
       /tmp/bmp_align_test
   gcc -std=c99 -Wall -Wextra \
       project/app/divinus/source/src/hal/plus/rk_rgn_pack.c \
       project/app/divinus/tests/rk_rgn_pack_test.c -o /tmp/rk_rgn_pack_test &&
       /tmp/rk_rgn_pack_test
   gcc -std=c99 -Wall -Wextra -O2 \
       -Iproject/app/divinus/source/src/analytics \
       project/app/divinus/source/src/analytics/nn_geom.c \
       project/app/divinus/tests/nn_geom_test.c -o /tmp/nn_geom_test &&
       /tmp/nn_geom_test
   ```
6. Другие правки upstream-файлов — только для сборки/запуска на плате, с явным диффом
   и комментарием в коммите.