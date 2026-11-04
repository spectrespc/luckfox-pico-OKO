# Пакет oko-ec для OKO

Утилита для трёх устройств на шине I2C3-M1 (`/dev/i2c-3`, 100 кГц,
`GPIO1_D2`/`GPIO1_D3`):

| Устройство | 7-бит адрес | Назначение |
|---|---|---|
| SHT21 | `0x40` | температура/влажность |
| INA3221 (или INA226) | `0x41` (`A0` = VS) | напряжение/ток/мощность шунта |
| STM32 ACPI-EC | `0x42` | протокол v2, OTA-прошивка |

Один бинарь `/oem/usr/bin/oko-ec`: подкоманды чтения, OTA-прошивка STM32 и
режим демона `oko-ec daemon`, который пишет `/tmp/oko-ec.json` и
`/tmp/oko-ec.osd`. `oko-osd` использует свежий `.osd` (≤ 30 с) и выводит
реальные строки `SHT21` (температура + влажность), `STM32` (напряжение, ток,
температура, SOC, ячейки) и `INA3221`/`INA226` (напряжение, ток, мощность);
без данных остаётся прежняя симуляция `T`/`BAT`.

Датчики читаются напрямую через `i2c-dev`: `CONFIG_HWMON` в ядре выключен,
поэтому драйверов `ina2xx`/`sht21` нет. Правки ядра и DTS под датчики не нужны —
только включённый узел `&i2c3` (`clock-frequency = <100000>`,
`pinctrl-0 = <&i2c3m1_xfer>`).

## Источник и пин

| Поле | Значение |
|---|---|
| Upstream | `https://github.com/spectrespc/stm32_acpi-ec` |
| Закоммиченный коммит | `41728ddaf6936d49a5fb65676ca5203dd631b4f6` |
| Дата вендоринга | `2026-11-04` |
| Лицензия | файла `LICENSE` в upstream нет; источник — репозиторий владельца проекта |
| Каталог исходников | `source/` |

Вендорены только файлы хостового инструмента
`tools/bootloader_tool/{main.c,crc_host.c,crc_host.h,i2c_transport.c,i2c_transport.h}`
(пин протокола — `docs/protocol.md`, v2). Остальной репозиторий STM32 (прошивка,
HAL, сборка) не копируется.

## Состав пакета

- `Makefile` — кросс-сборка через `$(RK_APP_CROSS)-gcc`, стейджинг
  `out/usr/bin/oko-ec` в `RK_APP_OUTPUT` (попадает в `/oem/usr/bin`).
- `source/main.c` — вендоренный CLI: `info`/`status`/`flash`/`enter-update`/
  `reset`, цель `motor` (включая `motor stlink`); локально добавлены команды
  `battery`/`cells`/`temp`/`ina226`/`sht21`/`sensors`/`scan`/`daemon` и
  глобальные опции (`--dev`, адреса, `--ina-channel`, `--shunt-mohm`,
  `--delay-us`, `--retries`, `--json`, `--quiet`).
- `source/crc_host.c/.h`, `source/i2c_transport.c/.h` — вендоренные; в транспорт
  добавлены `i2c_transport_write_read_timed()` (явные задержка и число попыток),
  `i2c_transport_probe()` (scan) и `i2c_transport_set_verbose()` (демон и scan не
  печатают ошибки каждого опроса). Старая `i2c_transport_write_read()` оставлена
  обёрткой 100 мс × 5 для совместимости.
- `source/ec_proto.c/.h` — новый слой кадров протокола v2: проверка `STATUS` и
  строгих длин ответов (`battery` — 15, `cells` — 7, `temp` — 3).
- `source/sensors_math.c/.h` — чистые целочисленные преобразования
  INA226/INA3221/SHT21 и CRC8 (полином `0x31`), без I/O и без `float`.
- `source/sensors.c/.h` — чтение INA226/INA3221 (big-endian, без записи
  конфигурации/калибровки; чип определяется по `Die ID`: `0x2260` — INA226,
  `0x3220` — INA3221; для INA3221 каналы 1–3 выбираются `--ina-channel`) и
  SHT21 (no-hold `0xE3`/`0xE5`, пауза 90/40 мс, CRC8, 2 повтора при ошибке CRC).
- `source/daemon.c/.h` — цикл (по умолчанию 2 с), атомарная запись
  `/tmp/oko-ec.json` и `/tmp/oko-ec.osd` через `.tmp` + `rename`, PID-файл
  `/tmp/oko-ec.pid`, лог только при смене состояния устройства.
- `tests/sensors_math_test.c` — хостовый тест, в сборку пакета не входит.

## Протокол и адреса

- `0x42`: кадры `[LEN_LO][LEN_HI][CMD][PAYLOAD]` → `[LEN_LO][LEN_HI][STATUS]
  [PAYLOAD]`. Протокол описан для 100 кГц; это текущая скорость шины в
  `rv1106g-luckfox-pico-pro-max-sc230ai.dts`.
- `0x41`: INA3221 или INA226, определяется по `Die ID` (`0xFF`):
  - INA3221 (`0x3220`): shunt/bus канала N — регистры `0x01/0x02`, `0x03/0x04`,
    `0x05/0x06`; 13-битные знаковые значения в `D15:D3`, LSB 40 мкВ и 8 мВ;
    канал задаётся `--ina-channel 1..3` (по умолчанию 1), env
    `OKO_EC_INA_CHANNEL`;
  - INA226 (`0x2260`): shunt `0x01`, bus `0x02`, LSB 2.5 мкВ и 1.25 мВ, канал
    только 1.
  Калибровочный регистр не используется: ток считается по shunt-напряжению и
  сопротивлению шунта (по умолчанию 100 мОм, `--shunt-mohm` /
  `OKO_EC_SHUNT_MOHM`). В конфигурацию чипа утилита не пишет.
- `0x40`: SHT21, no-hold измерения; единственные записи на шине — команды
  измерения SHT21 и кадры STM32. В INA и пользовательский регистр SHT21
  утилита не пишет.
- Без внешних pull-up (типично 2.2–4.7 кОм к 3.3 В) шина не поднимется:
  в DTS `pcfg_pull_none_smt`.

## Правила обновления

1. Клонировать upstream, перейти на новый коммит и скопировать пять файлов
   `tools/bootloader_tool/` в `source/` (без `CMakeLists.txt`).
2. Записать в таблицу выше новый **полный** хеш (`git rev-parse HEAD`) и дату.
3. **Повторно применить локальные изменения**:
   - `i2c_transport.c/.h` — timed/probe/verbose, обёртка старой функции;
   - `main.c` — новые команды, опции, целочисленный вывод размеров/прогресса
     OTA, совместимость `ec <i2c-dev> <cmd>` и `motor`;
   - `ec_proto.*`, `sensors_math.*`, `sensors.*`, `daemon.*` — локальные файлы,
     upstream их не содержит; при смене протокола сверить длины и поля с
     `docs/protocol.md` upstream.
4. Собрать `./build.sh app` и проверить `output/out/app_out/usr/bin/oko-ec`
   (`file` → ARM EABI, hard-float), а также `output/out/oem/usr/bin/oko-ec`.
5. Прогнать хостовый тест:
   ```sh
   gcc -std=c99 -Wall -Wextra -Iproject/app/oko-ec/source \
       project/app/oko-ec/source/sensors_math.c \
       project/app/oko-ec/tests/sensors_math_test.c -o /tmp/oko-ec-test
   /tmp/oko-ec-test
   ```
6. На стенде: `oko-ec scan`, `info`, `battery`, `sensors`; проверить
   `/tmp/oko-ec.json` и `/tmp/oko-ec.osd`, затем OSD.