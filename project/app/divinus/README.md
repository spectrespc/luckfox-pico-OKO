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
  `out/usr/bin/divinus`, `out/usr/share/divinus/divinus.yaml` и
  `out/usr/lib/libdivinus-mmap.so` в `RK_APP_OUTPUT`.
- `files/divinus.yaml` — фабричный конфиг для платы Pico Pro Max / SC230AI.
- `shim/rk_mmap_offset.c` — LD_PRELOAD-обход бага rockit (см. ниже).
- `source/` — вендорские исходники без `.git`; `build.sh` и `remote.sh` upstream не
  копируются: кросс-тулчейн берётся из SDK.

## Обход бага rockit (`libdivinus-mmap.so`)

ROCKIT из этого SDK (git-4720dc10a) в `MpiAdapterEncode::ma_encode_get_pack`
отображает выходное кольцо VENC двумя частями через `mmap2` с
`MAP_SHARED|MAP_FIXED`. Для нашей сборки вызов приходит без корректного page offset,
mmap падает (`mmap part 1 failed!`) и энкодер не отдаёт кадры: RTSP/web работают, но
видео и JPEG пустые.

`shim/rk_mmap_offset.c` — минимальный `LD_PRELOAD`-перехватчик: у больших
`MAP_SHARED|MAP_FIXED` mmap2 (кольца VENC) принудительно выставляет offset 0, все
остальные syscall пропускает без изменений. Включается только для процесса divinus
в `RkLunch.sh` (`LD_PRELOAD=/oem/usr/lib/libdivinus-mmap.so`), rkipc не затрагивает.

Проверено на стенде: после обхода `/image.jpg` отдаёт JPEG 1920x1080 (~65 КБ), RTSP
`rtsp://<ip>:554/` — H.265, ~1,3 МБ RTP за 5 с, `/api/status` — `rv11xx`/`sc230ai`.
Шим можно удалить, когда rockit или divinus исправят offset в upstream.

## Правила обновления

1. Клонировать upstream с `--depth 1`, заменить `source/src`, `source/res`, `source/doc`,
   `source/COPYING`, `source/LICENSE`, удалить `.git`.
2. Записать в таблицу выше новый **полный** хеш (`git rev-parse HEAD`) и дату.
3. Обновить `DIVINUS_REV` (короткий хеш) в `Makefile`.
4. Перед коммитом пересобрать (`./build.sh` в Docker) и проверить наличие
   `output/out/oem/usr/bin/divinus` — цикл сборки приложений не прерывается на ошибке
   отдельного пакета.
5. Правки upstream-файлов — только для сборки/запуска на плате, с явным диффом и
   комментарием в коммите. Отдельного каталога `patches/` нет, пока правок upstream нет.