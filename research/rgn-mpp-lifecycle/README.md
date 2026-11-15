# RGN 2BPP overlay: вендорный жизненный цикл и применимость к OKO

## Вопрос

Можно ли на RV1106 держать 2BPP overlay-регионы (рамки YOLO и IVS) на одном
VENC-канале и **пересоздавать** регион на лету, или destroy/recreate под
работающим энкодером — дефект вендорского стека MPP/rockit? От ответа зависит,
какой UX переключения `ivs.boxes` можно обещать: живой тумблер или
«применяется после рестарта».

## Что это

`RK_MPI_RGN_*` — регионы OSD/overlay в MPP. Для рамок используется тип
`OVERLAY` в формате `2BPP`: один байт на четыре пикселя и две записи палитры,
поэтому канва 1920×1088 занимает ~518 КиБ вместо ~4,1 МиБ BGRA5551.
Отрисовка идёт не через `SetBitmap`, а через указатель канвы:
`GetCanvasInfo` → `memset`/рисование → `UpdateCanvas`.

В дереве есть три вендорских источника истины:

- `media/rockit/rockit/mpi/example/mod/test_mpi_rgn.cpp` — пример MPI:
  create → attach → SetBitmap → цикл GetCanvasInfo/UpdateCanvas → смена
  позиции → show/hide → detach → destroy;
- `project/app/rkipc/rkipc/src/rv1106_ipc/video/video.c` — реальный продукт
  вендора (`rkipc`): поток `RkipcNpuOsd` каждые ~40 мс делает
  `GetCanvasInfo` → `memset` → рисование → `UpdateCanvas` для региона
  `DRAW_NN_OSD_ID` и никогда его не пересоздаёт;
- заголовки `rk_comm_rgn.h`, `rk_mpi_rgn.h` — структура `RGN_CANVAS_INFO_S`
  (в ней `MB_BLK canvasBlk`, `RK_U64 u64VirAddr`, `u32VirWidth/Height`).

## Выводы для OKO

**Переносимо:**

- create → attach к VENC → цикл `GetCanvasInfo`/`UpdateCanvas` на каждый кадр
  отрисовки. Именно так работает и вендорский `rkipc`, и наши YOLO/IVS-рамки;
  отдельного освобождения `canvasBlk` вендор не делает — `UpdateCanvas`
  закрывает цикл, поэтому «утечка MB_BLK на каждый кадр» гипотезой остаётся,
  а не фактом.
- Один регион на один слой; слой и LUT задаются в `RGN_CHN_ATTR_S` при attach.
- `RK_MPI_RGN_GetChannelConfig` **не** входит в вендорский цикл отрисовки:
  в примерах его нет. Наш `rk_region_box_begin()` вызывает его на каждый
  кадр для проверки, что регион ещё привязан к каналу, — это наша добавка,
  а не вендорный паттерн.

**Не переносимо / не подтверждено вендором:**

- **destroy/recreate региона при живом энкодере.** Ни `rkipc`, ни пример MPI
  так не делают: пример создаёт регионы один раз и в конце уничтожает,
  `rkipc` держит регион до остановки потока. Наш живой тумблер
  (`ivs.boxes` 0→1) как раз пересоздаёт регион под работающим VENC.
- **рамки IVS.** Вендорский `rkipc` IVS-рамки не рисует вообще: поток
  `rkipc_ivs_get_results` только журналирует результаты, регион 13 и слой 5 —
  наше изобретение. Поэтому «вендорский баг» для IVS-рамок не доказан: у
  вендора нет этой комбинации, и сравнивать не с чем.
- **два 2BPP-региона одновременно** (YOLO + IVS на одном канале). Вендор
  держит один регион на канал; сериализация вызовов у нас своя
  (`rk_region_overlay_lock`), вендор её не документирует.
- **освобождение `canvasBlk` вручную.** В API нет парной «release canvas»;
  `RK_MPI_MB_ReleaseBlock` для блока, полученного из `GetCanvasInfo`, в
  примерах не вызывается. Любая попытка освободить его — эксперимент, а не
  перенос вендорского кода.

**Отсюда стратегия OKO:** регион создаётся один раз за процесс, живой
destroy/recreate не используется; переключение `ivs.boxes` применяется
рестартом приложения (create-once), отрисовка — по вендорскому циклу.
Живой тумблер возможен только если измерения на стенде докажут, что
destroy/recreate безопасен; до этого он не предлагается.

## Проверенные факты

- `project/app/divinus/source/src/hal/plus/rk_hal.c:890-938` —
  `rk_region_box_begin()`: `GetChannelConfig` по каждому VENC-таргету на
  каждый кадр, затем `GetCanvasInfo`; указатель не кэшируется.
- `project/app/divinus/source/src/hal/plus/rk_hal.c:774-796` —
  destroy: `DetachChannel` по всем каналам + `DestroyRegion` под
  `rk_box_mtx`; у YOLO и IVS один и тот же код, отличаются handle
  (12/13) и слой (6/5), см. `rk_hal.h:89-103`.
- `project/app/divinus/source/src/analytics/yolo.c:794-901` и
  `project/app/divinus/source/src/ivs_events.c:194-351` — одинаковые
  create/render/destroy последовательности.
- `media/rockit/rockit/mpi/example/mod/test_mpi_rgn.cpp:304-325` —
  вендорский цикл `GetCanvasInfo`/`UpdateCanvas` без освобождения блока;
  `test_mpi_rgn.cpp:224-263` — create и attach один раз,
  `test_mpi_rgn.cpp:374-392` — detach и destroy один раз в конце.
- `grep -rn GetChannelConfig` по `media/rockit/rockit/mpi/example/`,
  `project/app/rkipc/rkipc/src/rv1106_ipc/`, `media/samples/`,
  `project/app/uvc_app_tiny/` не даёт совпадений: в вендорском цикле
  отрисовки этой проверки нет.
- `project/app/rkipc/rkipc/src/rv1106_ipc/video/video.c:1889-1999` —
  поток NN-рамок `rkipc`: `GetCanvasInfo` → `memset` → `draw_rect_2bpp` →
  `UpdateCanvas` без пересоздания региона.
- `project/app/rkipc/rkipc/src/rv1106_ipc/video/video.c:609-641` —
  IVS-результаты только журналируются, регион для них не создаётся.
- `media/rockit/rockit/mpi/sdk/include/rk_comm_rgn.h:182` — `MB_BLK
  canvasBlk` в `RGN_CANVAS_INFO_S`.
- Строки отказов из вендорских бинарников (`grep -a` по файлам):
  `dup mClientFd` есть только в `librockchip_mpp.so*`,
  `ma_encode_get_fd`, `getFd`, `CHECK_GE` — в `librockit*.so`.
  Исходников этих библиотек в дереве нет, поэтому точную причину
  `dup`/`CHECK_GE`/SIGSEGV по коду проследить нельзя — только по стенду.
- Жизненный цикл по `DetachChannel`/`DestroyRegion` не считается закрытым:
  на выходе процесса вендорский стек оставляет ~5 дескрипторов
  `/dmabuf:vmpi` и 1–2 `/dev/rga` (при включённом IVS) и ~2 без него
  (KI-069, 2026-11-15). Поэтому переходы проверяются по
  `/proc/<pid>/fd` и `Cma*` в `/proc/meminfo`, а `execvp` перед рестартом
  обязан закрыть дескрипторы (`FD_CLOEXEC`).

## Ссылки

- `media/rockit/rockit/mpi/sdk/include/rk_mpi_rgn.h`,
  `media/rockit/rockit/mpi/sdk/include/rk_comm_rgn.h` — API регионов.
- `media/rockit/rockit/mpi/example/mod/test_mpi_rgn.cpp` — пример MPI.
- `project/app/rkipc/rkipc/src/rv1106_ipc/video/video.c` — вендорский
  продукт-потребитель того же API.
- `KNOWN_ISSUES.md` KI-063 — стендовые симптомы MPP/VENC.

## Провенанс снимков

Снимки в `snapshot/` для этой темы **не снимаются**: все источники — файлы
вендорского SDK, которые уже лежат в дереве и являются read-only
(вендорский архив не редактируется). Копия в `snapshot/` дублировала бы
мегабайты vendor-кода и нарушала лимит темы. Ниже — фиксация версий.

| Upstream-путь (в дереве) | Лицензия | sha256 | Размер | Дата снятия | Pin |
|---|---|---|---|---|---|
| `media/rockit/rockit/mpi/example/mod/test_mpi_rgn.cpp` | vendor SDK, проприетарная | `747ea1f36cf5a3e7409c80ef255afd4d178d691937e6d3844af5cae1d36b0745` | 27 КиБ | 2026-11-15 | репозиторий OKO, `d871ce9ce` |
| `media/rockit/rockit/mpi/sdk/include/rk_comm_rgn.h` | vendor SDK, проприетарная | `0ae5bbb6a9ff68fccb1b6de2192c073ef742c680e38f523463c1e154e6e3a8f4` | 13 КиБ | 2026-11-15 | репозиторий OKO, `d871ce9ce` |
| `media/rockit/rockit/mpi/sdk/include/rk_mpi_rgn.h` | vendor SDK, проприетарная | `4e1e01df6604c1498d664ec4655ddf0bb48e609f74c42eb78ffb04e8fe5ad5ab` | 5 КиБ | 2026-11-15 | репозиторий OKO, `d871ce9ce` |
| `project/app/rkipc/rkipc/src/rv1106_ipc/video/video.c` | vendor SDK, проприетарная | `eba534e462d01a5307b61bfeb70299fa5f9173eb0ac5bc40fed1ef99e4a43968` | 138 КиБ | 2026-11-15 | репозиторий OKO, `d871ce9ce` |
| `media/rockit/out/lib/librockit.so` (бинарник) | vendor SDK, проприетарная | `3f92f8c41ffe9ad72e407b68750906fcff89ea06758f14a3fc2a3d87061e3d0f` | — | 2026-11-15 | репозиторий OKO, `d871ce9ce` |
| `media/mpp/out/lib/librockchip_mpp.so.0` (бинарник) | vendor SDK, проприетарная | `e8183339fff1dd466adc9567be5c4c98239c567157eaebefe4a2fe50f793fec8` | — | 2026-11-15 | репозиторий OKO, `d871ce9ce` |

## Как обновить снимок

Снимков нет. При обновлении вендорского SDK перепроверить:

```sh
sha256sum media/rockit/rockit/mpi/example/mod/test_mpi_rgn.cpp \
  media/rockit/rockit/mpi/sdk/include/rk_comm_rgn.h \
  media/rockit/rockit/mpi/sdk/include/rk_mpi_rgn.h \
  project/app/rkipc/rkipc/src/rv1106_ipc/video/video.c
grep -a -o 'dup mClientFd' media/mpp/out/lib/librockchip_mpp.so.0
grep -a -o 'ma_encode_get_fd' media/rockit/out/lib/librockit.so
```

и обновить таблицу выше; выводы темы пересмотреть, если в новых примерах
появится destroy/recreate региона на живом канале или освобождение
`canvasBlk`.