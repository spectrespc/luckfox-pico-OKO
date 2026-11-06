# STM8 firmware sources

## Русский

Каталог `project/app/stm8-motor/` — полный репозиторий прошивки STM8
(моторы фокуса/зума, I2C-загрузчик, `oko-motor`, тесты и документация),
импортированный как **git subtree**:

| Поле | Значение |
|---|---|
| Upstream | `https://github.com/spectrespc/stm8s103_i2c_motor` |
| Ветка | `main` |
| Импортированный коммит | `ef106e6513afaf15db71c2cbc57628a9dfb932c3` |
| Дата импорта | `2026-11-06` |
| Способ | `git subtree`, история upstream сохранена |

Перед импортом локальные изменения (загрузчик, `0x08 GET_STATE`,
`oko-motor check/boot-reset`, тесты, документация и dual-mode `Makefile`)
отправлены в upstream коммитом `ef106e6`.

### Сборка

Standalone (корень `project/app/stm8-motor`):

```sh
make            # build/app.ihx, build/factory.ihx, build/app.bin
make flash      # ST-Link + factory.ihx
make -C util    # util/motorctl
make -C tests test
```

`Makefile` dual-mode: при наличии `../Makefile.param` (SDK OKO) он собирает
только `oko-motor`; иначе включает `Makefile.stm8` и собирает прошивку.

В OKO SDK:

```sh
./build.sh app   # собирает /oem/usr/bin/oko-motor
docker run --rm -v "$PWD:/work" -w /work/project/app/stm8-motor \
    oko-stm8-sdcc make -f Makefile.stm8 all
```

Первая запись — `build/factory.ihx` по ST-Link/SWIM; обновление —
`oko-motor flash build/app.ihx` или `/api/flash` в WebUI. EEPROM-позиции
моторов при обновлении flash не стираются.

### Отправка изменений в STM8-репозиторий

```sh
tools/stm8_push.sh "boot: краткое описание изменения"
```

Скрипт коммитит только `project/app/stm8-motor` и выполняет
`git subtree push --prefix=project/app/stm8-motor stm8 main`. Remote `stm8`
настроен на fetch по HTTPS и push по SSH; если его нет, скрипт создаст его.

Обновление из upstream:

```sh
tools/stm8_pull.sh
```

`git subtree pull` создаёт merge-коммит; локальные правки нужно заранее
закоммитить или спрятать. После pull обновите строку «Импортированный
коммит» выше и запись в `CHANGELOG.md`.

## English

`project/app/stm8-motor/` is the complete STM8 firmware repository (focus/zoom
motors, I2C bootloader, `oko-motor`, tests and documentation), imported as a
**git subtree**:

| Field | Value |
|---|---|
| Upstream | `https://github.com/spectrespc/stm8s103_i2c_motor` |
| Branch | `main` |
| Imported commit | `ef106e6513afaf15db71c2cbc57628a9dfb932c3` |
| Import date | `2026-11-06` |
| Method | `git subtree`; upstream history is preserved |

Before the import, the local changes (bootloader, `0x08 GET_STATE`,
`oko-motor check/boot-reset`, tests, documentation and the dual-mode
`Makefile`) were pushed upstream as commit `ef106e6`.

### Build

Standalone (repository root `project/app/stm8-motor`):

```sh
make            # build/app.ihx, build/factory.ihx, build/app.bin
make flash      # ST-Link + factory.ihx
make -C util    # util/motorctl
make -C tests test
```

`Makefile` is dual-mode: when `../Makefile.param` exists (OKO SDK) it builds
only `oko-motor`; otherwise it includes `Makefile.stm8` and builds the
firmware.

In the OKO SDK:

```sh
./build.sh app   # builds /oem/usr/bin/oko-motor
docker run --rm -v "$PWD:/work" -w /work/project/app/stm8-motor \
    oko-stm8-sdcc make -f Makefile.stm8 all
```

The first write is `build/factory.ihx` over ST-Link/SWIM; updates use
`oko-motor flash build/app.ihx` or `/api/flash` in the WebUI. Motor EEPROM
positions survive a flash update.

### Sending changes to the STM8 repository

```sh
tools/stm8_push.sh "boot: short change description"
```

The script commits only `project/app/stm8-motor` and runs
`git subtree push --prefix=project/app/stm8-motor stm8 main`. The `stm8`
remote fetches over HTTPS and pushes over SSH; the script creates it when
missing.

Pull upstream changes:

```sh
tools/stm8_pull.sh
```

`git subtree pull` creates a merge commit; commit or stash local changes
first. After a pull, update the “Imported commit” row above and the
`CHANGELOG.md` entry.