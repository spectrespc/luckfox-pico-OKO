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
- `source/` — вендорские исходники без `.git`; `build.sh` и `remote.sh` upstream не
  копируются: кросс-тулчейн берётся из SDK. В `source/src/compat.c` внесён один
  локальный патч (см. ниже).

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

## Правила обновления

1. Клонировать upstream с `--depth 1`, заменить `source/src`, `source/res`, `source/doc`,
   `source/COPYING`, `source/LICENSE`, удалить `.git`.
2. Записать в таблицу выше новый **полный** хеш (`git rev-parse HEAD`) и дату.
3. Обновить `DIVINUS_REV` (короткий хеш) в `Makefile`.
4. **Повторно применить патч `compat.c`**: на uClibc hard-float не определять
   собственный `mmap` (см. раздел выше), иначе вернётся `mmap part 1 failed!`.
5. Перед коммитом пересобрать (`./build.sh` в Docker) и проверить наличие
   `output/out/oem/usr/bin/divinus` — цикл сборки приложений не прерывается на ошибке
   отдельного пакета.
6. Другие правки upstream-файлов — только для сборки/запуска на плате, с явным диффом
   и комментарием в коммите.