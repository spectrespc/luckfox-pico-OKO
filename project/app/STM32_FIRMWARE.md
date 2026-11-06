# STM32 firmware sources

## Русский

Каталог `project/app/stm32_acpi-ec/` — полный репозиторий прошивки STM32
ACPI-EC, импортированный как **git subtree**:

| Поле | Значение |
|---|---|
| Upstream | `https://github.com/spectrespc/stm32_acpi-ec` |
| Ветка | `main` |
| Импортированный коммит | `41728ddaf6936d49a5fb65676ca5203dd631b4f6` |
| Дата импорта | `2026-11-06` |
| Способ | `git subtree`, история upstream сохранена |

Здесь лежат загрузчик (`boot/`), приложение (`app/`), HAL (`hal/`), общие
модули (`common/`), CMake-сборка, тесты и хостовый инструмент
`tools/bootloader_tool`. Это отдельная CMake-сборка: `./build.sh` OKO её не
собирает и в образ не кладёт. `project/app/oko-ec/source/` по-прежнему
содержит только вендоренный хостовый инструмент, который используется в
`/oem/usr/bin/oko-ec`.

### Сборка

Нужны CMake, `arm-none-eabi-gcc` и SDK STM32CubeF1. Upstream ожидает SDK в
`../STM32CubeF1`, то есть:

```sh
git clone https://github.com/STMicroelectronics/STM32CubeF1.git \
    project/app/STM32CubeF1
cd project/app/stm32_acpi-ec
cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake -DMCU=C8T6
cmake --build build
```

`-DMCU=CBT6` выбирает 128-КиБ вариант. Результаты: `build/bootloader.*` и
`build/app.*`. Хостовые тесты:

```sh
cmake -B build_host
cmake --build build_host --target check
```

`project/app/STM32CubeF1/` — локальная зависимость, в git не входит.

### Отправка изменений в STM32-репозиторий

Правки делаются прямо в `project/app/stm32_acpi-ec/`, затем:

```sh
tools/stm32_push.sh "app: краткое описание изменения"
```

Скрипт коммитит **только** этот каталог, оставляя остальные изменения OKO
нетронутыми, и выполняет `git subtree push --prefix=project/app/stm32_acpi-ec
stm32 main`. Ветка `stm32` в OKO настроена на fetch по HTTPS и push по SSH;
если remote отсутствует, скрипт создаёт его сам.

Если upstream ушёл вперёд:

```sh
tools/stm32_pull.sh
```

`git subtree pull` создаёт merge-коммит и при конфликтах требует обычного
разрешения. Перед pull закоммитьте или спрячьте локальные правки.

### Обновление пина

После `tools/stm32_pull.sh` обновите строку «Импортированный коммит» в этом
файле и запись в `CHANGELOG.md`; сам gitlink/поддерево уже фиксирует новый
коммит в истории OKO.

## English

`project/app/stm32_acpi-ec/` is the complete STM32 ACPI-EC firmware repository,
imported as a **git subtree**:

| Field | Value |
|---|---|
| Upstream | `https://github.com/spectrespc/stm32_acpi-ec` |
| Branch | `main` |
| Imported commit | `41728ddaf6936d49a5fb65676ca5203dd631b4f6` |
| Import date | `2026-11-06` |
| Method | `git subtree`; upstream history is preserved |

It contains the bootloader (`boot/`), application (`app/`), HAL (`hal/`),
common modules (`common/`), the CMake build, tests and the host tool
`tools/bootloader_tool`. It is a separate CMake build: the OKO `./build.sh`
does not build it and does not place it in the image.
`project/app/oko-ec/source/` still contains only the vendored host tool used as
`/oem/usr/bin/oko-ec`.

### Build

CMake, `arm-none-eabi-gcc` and the STM32CubeF1 SDK are required. Upstream
expects the SDK at `../STM32CubeF1`:

```sh
git clone https://github.com/STMicroelectronics/STM32CubeF1.git \
    project/app/STM32CubeF1
cd project/app/stm32_acpi-ec
cmake -B build -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake -DMCU=C8T6
cmake --build build
```

Use `-DMCU=CBT6` for the 128-KiB part. Outputs are `build/bootloader.*` and
`build/app.*`. Host tests:

```sh
cmake -B build_host
cmake --build build_host --target check
```

`project/app/STM32CubeF1/` is a local dependency and is not tracked.

### Sending changes to the STM32 repository

Edit files directly under `project/app/stm32_acpi-ec/`, then run:

```sh
tools/stm32_push.sh "app: short change description"
```

The script commits **only** that directory, leaves other OKO changes alone and
runs `git subtree push --prefix=project/app/stm32_acpi-ec stm32 main`. The OKO
remote `stm32` fetches over HTTPS and pushes over SSH; the script creates it if
it is missing.

If upstream has moved:

```sh
tools/stm32_pull.sh
```

`git subtree pull` creates a merge commit and reports conflicts for normal
resolution. Commit or stash local changes before pulling.

### Updating the pin

After `tools/stm32_pull.sh`, update the “Imported commit” row above and the
`CHANGELOG.md` entry; the subtree history already records the new commit.