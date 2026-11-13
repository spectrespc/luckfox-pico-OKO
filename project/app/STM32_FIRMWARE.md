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

### STM32C011F6P6 / STM32C031F6P6 (порт EC)

Цели `-DMCU=C011x6` (6 КиБ SRAM, стенд) и `-DMCU=C031x6` (12 КиБ SRAM),
TSSOP20, 32 КиБ Flash:
загрузчик 8 КиБ + приложение 20 КиБ, метаданные — две страницы по 2 КиБ.
Приложение управляет питанием хоста (`PWR_EN`, `PWR_GOOD`, `HOST_WAKE`),
кнопкой power и сном Stop; протокол — v3 (`GET_POWER`/`SET_POWER`).
Карта пинов и обвязка — `project/app/stm32_acpi-ec/docs/hardware.md`.

SDK клонируется с `sparse-checkout` и подмодулями: в STM32CubeC0 каталоги
`Drivers/CMSIS/Device/ST/STM32C0xx` и `Drivers/STM32C0xx_HAL_Driver` —
подмодули, без `submodule update` они пустые. Путь
`project/app/STM32CubeC0` виден `cmake/FindSTM32CubeC0.cmake`; эталон —
`.github/actions/setup-sdk/action.yml`.

```sh
cd project/app
git clone --depth 1 --filter=blob:none --sparse \
    https://github.com/STMicroelectronics/STM32CubeC0.git STM32CubeC0
git -C STM32CubeC0 sparse-checkout set Drivers
git -C STM32CubeC0 submodule update --init --depth 1 \
    Drivers/CMSIS/Device/ST/STM32C0xx Drivers/STM32C0xx_HAL_Driver
cd stm32_acpi-ec
cmake -B build-c011 -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake -DMCU=C011x6
cmake --build build-c011
cmake --build build-c011 --target flash-boot   # OpenOCD master
cmake --build build-c011 --target flash-app
```

Нужен `arm-none-eabi-gcc` с newlib (`libnewlib-arm-none-eabi`,
`libnewlib-dev`) и **OpenOCD из git master** (`scripts/openocd_c0.cfg`):
в OpenOCD 0.12 из Debian нет `stm32c0x.cfg`. Фактические размеры на
gcc 14.2.1: boot 4 232 Б, app 8 276 Б (debug 9 112 Б). На стенде `st-info` определяет
STM32C011xx (sram 6144). В C0 нет LPTIM, а RTC не имеет wakeup-таймера, поэтому сон прерывается только кнопкой или адресом
I2C; LSE не используется (отличие от первоначального плана).
Короткое нажатие в AWAKE требует хоста с протоколом v3.

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

### STM32C011F6P6 / STM32C031F6P6 (EC port)

Targets `-DMCU=C011x6` (6 KiB SRAM, bench) and `-DMCU=C031x6` (12 KiB
SRAM), TSSOP20, 32 KiB flash: an 8 KiB
bootloader plus a 20 KiB application, with two 2 KiB metadata pages. The
application drives host power (`PWR_EN`, `PWR_GOOD`, `HOST_WAKE`), the power
button and Stop mode; the protocol is v3 (`GET_POWER`/`SET_POWER`). The pin
map and wiring requirements are in
`project/app/stm32_acpi-ec/docs/hardware.md`.

SDK клонируется с `sparse-checkout` и подмодулями: в STM32CubeC0 каталоги
`Drivers/CMSIS/Device/ST/STM32C0xx` и `Drivers/STM32C0xx_HAL_Driver` —
подмодули, без `submodule update` они пустые. Путь
`project/app/STM32CubeC0` виден `cmake/FindSTM32CubeC0.cmake`; эталон —
`.github/actions/setup-sdk/action.yml`.

```sh
cd project/app
git clone --depth 1 --filter=blob:none --sparse \
    https://github.com/STMicroelectronics/STM32CubeC0.git STM32CubeC0
git -C STM32CubeC0 sparse-checkout set Drivers
git -C STM32CubeC0 submodule update --init --depth 1 \
    Drivers/CMSIS/Device/ST/STM32C0xx Drivers/STM32C0xx_HAL_Driver
cd stm32_acpi-ec
cmake -B build-c011 -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi.cmake -DMCU=C011x6
cmake --build build-c011
cmake --build build-c011 --target flash-boot   # OpenOCD master
cmake --build build-c011 --target flash-app
```

Requires `arm-none-eabi-gcc` with newlib (`libnewlib-arm-none-eabi`,
`libnewlib-dev`) and **OpenOCD from git master**
(`scripts/openocd_c0.cfg`): Debian OpenOCD 0.12 has no `stm32c0x.cfg`.
Measured with gcc 14.2.1: boot 4,232 B, app 8,276 B (debug 9,112 B). On the bench `st-info` reports STM32C011xx (sram 6144). C0 has no LPTIM
and its RTC has no wakeup timer, so Stop is left only by the button or by
the I2C address; LSE is unused (a deviation from the initial plan). A short
press in AWAKE requires a v3 host.

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