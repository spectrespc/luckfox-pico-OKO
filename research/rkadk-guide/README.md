# RKADK: вендорский API-гайд и его применимость к OKO

## Вопрос

Чем `Rockchip_Developer_Guide_Linux_Rkadk_EN.pdf` полезен OKO после удаления
`project/app/component/rkadk`: какие контракты записи/стриминга стоит взять
как чек-лист для `divinus`, а что не переносится и требует других документов
(прежде всего AOV и raw MPI/rockit).

## Что это

`Rockchip RKADK Development Guide` — 152-страничный PDF (создан 2024-01-08,
экспорт Chromium/Skia), документирующий публичный API RKADK — прикладной
обёртки Rockchip над MPI/rockit для типовых IPC-сценариев. Оглавление:

- `Overview`, `Version Introduction`;
- `Video Recording` (`RKADK_RECORD_Create/Destroy/Start/Stop/Reset/ManualSplit`,
  `GetAencChn`, `GetThmInMp4`, `ThmBufFree`, `SetRotation`,
  `ToggleMirror/Flip`, `FileCacheInit/DeInit`);
- `Taking Photos` (`RKADK_PHOTO_*`, `GetThmInJpg`, `ThumbBufFree`);
- `Remote Preview` (`RKADK_STREAM_VideoInit/VencStart/...`);
- `Live Streaming` (`RKADK_RTSP_Init/Start/Stop`, `RKADK_RTMP_Init/DeInit`);
- `Storage` (`GetMountStatus/GetCapacity/GetFileList/Format`);
- `Local preview` (`RKADK_DISP_*`), `Watermark` (`RKADK_OSD_*`),
  `UI Overlay` (`RKADK_UI_*`);
- `Parameter Settings` (`RKADK_PARAM_*`: bitrate, GOP, `RecTime`, VQE,
  muxer file type, pre-record) и `INI File Introduction` (global + sensor INI);
- `Examples`: `rkadk_record_test`, `rkadk_photo_test`, `rkadk_stream_test`,
  `rkadk_player_test`, `rkadk_thumb_test`, `rkadk_rtsp_test`,
  `rkadk_rtmp_test`, `rkadk_storage_test`, `rkadk_disp_test`, `rkadk_ui_test`.

В оглавлении нет разделов про AOV, suspend/low-power, ISP-handover и MCU/PMU.
AOV-гайд Rockchip — отдельный документ, он уже лежит в
`research/rv1106-aov/snapshot/dehai168-rv1106-aov-ipc/docs/`.

В OKO-дереве самого RKADK больше нет: `project/app/component/rkadk` удалён
2026-11-25 как неиспользуемый (`TO_DO.md` §15.7). PDF остаётся внешним
справочником, а не частью сборки.

## Выводы для OKO

**Переносимо:**

- **Контракты и состояния, а не код.** Гайд — удобный чек-лист для сверки
  нашего media-стека (`record.c`, `record_clip.c`, `rtsp/`, `rtmp.c`,
  `jpeg.c`, `region.c`, `server.c`): какие состояния записи, события файла,
  параметры VENC/муксера и операции storage вендор считает частью API.
- **Файловый кэш и ручной split.** `RKADK_RECORD_FileCacheInit/DeInit` и
  `ManualSplit` описывают вендорскую модель «кэш → сегмент»; это референс для
  усиления нашей записи, где `record.c` пишет файл напрямую и есть риск
  обрезанного MP4 при сбое (`TO_DO.md` §15.6). Модель полезна как идея, но не
  доказывает crash-safety.
- **Thumbnails.** `GetThmInMp4`/`GetThmInJpg`/`ThumbBufFree` — перечень
  операций для превью; сверить, нужны ли они WebUI/событиям.
- **Storage API.** `GetMountStatus`, `GetCapacity`, `GetFileList`,
  `GetFileNum`, `Format` — чек-лист полноты нашего storage-слоя.
- **OSD/watermark/UI overlay.** `RKADK_OSD_AttachToStream/UpdateBitmap`,
  `RKADK_UI_Update` — сверка с нашими `region.c`/`text.c` по составу
  операций (не по реализации).
- **Параметры и INI.** Список `bitrate`, `GOP`, `RecTime`, `VQE_MODE`,
  `MuxerFileType`, pre-record — полезен как аудит defaults в
  `app_config.c`; формат INI не переносим, у нас своя схема.
- **RTSP/RTMP.** `RKADK_RTSP_Init/Start/Stop` и `RKADK_RTMP_Init/DeInit`
  вместе с моделью info/колбэков — только сверка семантики с нашим `rtsp/` и
  `rtmp.c`.

**Не переносимо / не подтверждено:**

- **Сам RKADK и его сборка.** Ни один BoardConfig его не включает, в
  `app_src` его нет; вендорский код по `AGENTS.md` §6 — только справочник.
  Возвращать каталог ради документа не нужно.
- **RV1106/SC230AI-специфика.** В PDF нет порядка AIQ/VI/VENC,
  `meta_load_addr`, особенностей SC230AI, suspend/FE-PHY, KI-072/KI-073. Для
  этого — наши стендовые документы и `research/rv1106-aov/`.
- **AOV.** В PDF нет AOV/low-power раздела; `rkadk_aov.c` — исходник RKADK,
  а не этот гайд. AOV-архитектура OKO (`sleep_policy.c`, HPMCU/PMU) по этому
  документу не проверяется.
- **Raw MPI/rockit.** Гайд описывает обёртку RKADK, а не `RK_MPI_*`. Для
  прямых вызовов источник истины — заголовки/examples в `media/rockit`.
- **Examples как код.** Тесты завязаны на RKADK-библиотеку и её build; в
  `divinus` их нельзя перенести без переписывания.
- **Player/Local preview.** Воспроизведение и VO/локальный вывод не входят в
  текущий продуктовый media-path; разделы оставлены для полноты.

## Проверенные факты

- `TO_DO.md:953` — `component/rkadk`: RKADK API/examples/PDF, 111 файлов,
  3.4 МБ, ни одна цель `project/app` его не вызывает; удалён 2026-11-25.
- `TO_DO.md:943-957` — §15.7: удалённые каталоги, upstream-ссылки и проверка
  после удаления (`make -C project/app info`, отсутствие в `app_src`).
- `CHANGELOG.md` → `[Не выпущено]` → `Удалено`, запись «Удалены
  неиспользуемые вендорские каталоги» (`rkadk`/`lvgl`/`ipcweb`).
- `STRUCTURE.md:77` — каталоги `component/rkadk`, `component/lvgl`, `ipcweb`
  удалены 2026-11-25.
- `TO_DO.md:933` — «stage MP4 на TF + `rename`»: у нас `record.c` пишет файл
  напрямую, риск обрезанного файла при сбое.
- `project/app/divinus/source/src/record.c:265,388,537-558` — ротация
  фрагментов и split Annex B; `record_clip.c:146` — `nal_split`.
- `project/app/divinus/source/src/rtsp/`, `rtmp.c`, `jpeg.c`, `region.c`,
  `server.c` — наши RTSP/RTMP/JPEG/OSD/HTTP-модули, с которыми сверяется
  оглавление гайда; `fmt/moof.c` — свой fMP4-writer (`TO_DO.md:935`).
- `FEATURES.md:31-32` — медиастек `divinus` (H.265/H.264, JPEG, RTSP, WebUI),
  event-клипы `record_clip` (fMP4 → `services/events`).
- `research/rv1106-aov/README.md:582-600` — AOV-схема dehai использует
  `rkadk_aov.c`/`rkadk_muxer.c`, RKADK по умолчанию `-DENABLE_AOV=OFF`;
  AOV-гайд Rockchip — отдельный документ.
- `research/rv1106-aov/README.md:774` — снимок
  `Rockchip_RV1106_Developer_Guide_Linux_AOV_CN.pdf` (7 377 217 Б).
- Снимок PDF: `file` — PDF 1.4, 152 страницы; `sha256sum` —
  `0f06e0b4396ee7636dc7eea65c7616f23c5be81048c294d67bd6af874f7391d5`;
  размер 704 558 Б; в оглавлении (`grep -a '/Title'`) нет AOV/suspend/low-power.

## Ссылки

- PDF (raw):
  https://raw.githubusercontent.com/LuckfoxTECH/luckfox-pico/main/project/app/component/rkadk/rkadk/docs/Rockchip_Developer_Guide_Linux_Rkadk_EN.pdf
- PDF (raw, pinned commit):
  https://raw.githubusercontent.com/LuckfoxTECH/luckfox-pico/824b817f889c2cbff1d48fcdb18ab494a68f69d1/project/app/component/rkadk/rkadk/docs/Rockchip_Developer_Guide_Linux_Rkadk_EN.pdf
- PDF (GitHub):
  https://github.com/LuckfoxTECH/luckfox-pico/blob/main/project/app/component/rkadk/rkadk/docs/Rockchip_Developer_Guide_Linux_Rkadk_EN.pdf
- Каталог docs (EN + CN):
  https://github.com/LuckfoxTECH/luckfox-pico/tree/main/project/app/component/rkadk/rkadk/docs
- Вендорский репозиторий:
  https://github.com/LuckfoxTECH/luckfox-pico
- AOV-материалы OKO: `research/rv1106-aov/README.md`
- История удаления: `TO_DO.md` §15.7, `CHANGELOG.md` `[Не выпущено]` → `Удалено`

## Провенанс снимков

Снимок: 1 файл, 704 558 Б. Лимита на число файлов и размер нет. Дата снятия —
2026-11-25. Файл не редактировался; PDF — статический документ Rockchip,
включён как read-only research copy.

| Upstream path | Локальный путь | Лицензия / SPDX | sha256 | Размер | Дата | Pin |
|---|---|---|---|---|---|---|
| `project/app/component/rkadk/rkadk/docs/Rockchip_Developer_Guide_Linux_Rkadk_EN.pdf` | `snapshot/Rockchip_Developer_Guide_Linux_Rkadk_EN.pdf` | Rockchip doc; all rights reserved (research copy) | `0f06e0b4396ee7636dc7eea65c7616f23c5be81048c294d67bd6af874f7391d5` | 704 558 | 2026-11-25 | `LuckfoxTECH/luckfox-pico@824b817f889c2cbff1d48fcdb18ab494a68f69d1`; PDF 152 с., создан 2024-01-08; версия в метаданных не указана |

Не снято:

- `Rockchip_Developer_Guide_Linux_RKADK_CN.pdf` — китайская версия того же
  гайда; не копировалась. Нужна для сверки терминов; ссылка в разделе
  «Ссылки».
- Исходники/examples `rkadk` — не копировались: каталог удалён из OKO, а
  upstream-ссылка сохранена в `TO_DO.md` §15.7. При возврате к задаче
  скачивать только нужный файл, а не весь каталог.

## Как обновить снимок

```sh
mkdir -p /tmp/kilo/rkadk-guide
curl -sS -L -o /tmp/kilo/rkadk-guide/Rockchip_Developer_Guide_Linux_Rkadk_EN.pdf \
  'https://raw.githubusercontent.com/LuckfoxTECH/luckfox-pico/824b817f889c2cbff1d48fcdb18ab494a68f69d1/project/app/component/rkadk/rkadk/docs/Rockchip_Developer_Guide_Linux_Rkadk_EN.pdf'
sha256sum /tmp/kilo/rkadk-guide/Rockchip_Developer_Guide_Linux_Rkadk_EN.pdf
file /tmp/kilo/rkadk-guide/Rockchip_Developer_Guide_Linux_Rkadk_EN.pdf
cp /tmp/kilo/rkadk-guide/Rockchip_Developer_Guide_Linux_Rkadk_EN.pdf \
  research/rkadk-guide/snapshot/
git ls-remote https://github.com/LuckfoxTECH/luckfox-pico.git refs/heads/main
```

Проверить, что PDF по-прежнему 152 страницы и что в оглавлении не появились
AOV/suspend-разделы:

```sh
grep -a '/Title' research/rkadk-guide/snapshot/Rockchip_Developer_Guide_Linux_Rkadk_EN.pdf
```

Затем обновить sha256/размер/дату и pin в таблице, пересмотреть выводы темы и
внести запись в `CHANGELOG.md` → `[Не выпущено]`. Править файл в `snapshot/`
нельзя: обновление — только новой копией с новым пином.