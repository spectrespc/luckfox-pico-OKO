# STM8 IAP: AN2659, AN2737 и PM0051 против нашего загрузчика

## Вопрос

Почему IAP-стирание блока на STM8S003F3P6 из нашего загрузчика не
завершается (KI-075), и есть ли в документации ST шаг регистровой
последовательности, которого у нас нет: ожидание `PUL`, `FLASH_CR1.HALT`,
требование к триггеру, защита UBC?

## Что это

- **AN2659** (Doc ID 14153 Rev 4, 2012) — «STM8 in-application programming
  (IAP) using a customized user-bootloader». Описывает архитектуру
  пользовательского загрузчика (активация, протокол, команды) и требования
  к работе с памятью: блочные операции только из RAM, RWW только для data
  EEPROM. Регистровой последовательности стирания не приводит — отсылает к
  SPL.
- **AN2737** (Rev 3, 2009) — «Basic in-application programming example using
  the STM8 I2C and SPI peripherals». Прикладной пример: STM8 ведёт себя как
  I2C/SPI-EEPROM, прошивка на SPL, блочное программирование копируется в RAM
  через `_fctcpy` (Cosmic). Регистровых деталей нет.
- **PM0051** (Doc ID 14614 Rev 3, 2011) — «How to program STM8S and STM8A
  Flash program memory and data EEPROM». Единственный из трёх документов с
  точной последовательностью: unlock → `CR2.ERASE` + `NCR2.NERASE` → запись
  `0x00000000` по слову в блок → опрос `EOP`; шаги 2–5 обязаны исполняться
  из RAM, `PUL`/`DUL` надо опрашивать до первой записи, `CR2`/`NCR2` должны
  писаться подряд.

## Выводы для OKO

**Переносимо:**

- Требование «блочная операция — из RAM» у нас уже выполнено: стирание идёт
  RAM-заглушкой (`src/boot/erase_stub.asm`), а не из Flash.
- Требование PM0051 опрашивать `PUL` до первой записи в программируемую
  область — проверено на стенде как отдельная гипотеза (E0) и **не помогло**;
  в SPL `FLASH_Unlock` этот бит тоже не опрашивает, то есть «эталонный» код
  ST работает без него.
- Требование PM0051 к триггеру — запись слова `0x00000000` в блок. У нас
  байтовый триггер; вариант со словом проверялся отдельно
  (`output/bench/stm8/ki075-word.ihx`) и даёт тот же отказ.

**Не переносимо:**

- Код примеров AN2659/AN2737 целиком: он на Cosmic/SPL и с их протоколом
  команд; наш I2C-протокол (CRC16 кадра, эхо команды, `STOPF`) полнее, менять
  его на пример — регресс.
- `FLASH_CR1.HALT/AHALT` как средство «дождаться стирания»: по PM0051/SPL
  ожидание `EOP` после триггера и так формальность, потому что для program
  memory RWW нет и ядро стоит в самой записи-триггере. Проверено на стенде
  (E1): и `HALT=1`, и `HALT=1 + AHALT=1` дают тот же отказ, потому что
  управление не возвращается из записи-триггера.

**Итог по KI-075:** ни один из трёх документов не описывает шага, которого
нет в нашем коде; последовательность совпадает с SPL, а SWIM-прошивка
(которая стирает те же блоки) работает. Отказ IAP-стирания на стендовом
экземпляре — дефект чипа, а не расхождение с документацией.

## Проверенные факты

Наш код (пути и строки в этом дереве):

- `project/app/stm8-motor/src/boot/erase_stub.asm` — RAM-заглушка стирания,
  67 байт: `bres 0x505f,#2` (EOP), `bset 0x505b,#5` (CR2.ERASE),
  `bres 0x505c,#5` (NCR2.NERASE), `ldw x,0x0200` + `ld (x),a` (триггер),
  цикл опроса `IAPSR` с ограничением и возврат результата.
- `project/app/stm8-motor/src/boot/boot_flash.c:102–128` —
  `boot_flash_erase()`: проверки адреса/длины, ключи `PUKR`, вызов заглушки на
  каждый 64-байтовый блок, снятие `PUL`.
- `project/app/stm8-motor/src/boot/boot_flash.c:130–174` —
  `boot_flash_write()`: `CR2.WPRG`/`NCR2.NWPRG`, запись 4 байт, ожидание
  `EOP`/`WR_PG_DIS`.
- `project/app/stm8-motor/src/boot/boot_flash.c:232–247` —
  `boot_clear_magic()`: для data EEPROM ключи `DUKR` (в обратном порядке) и
  ожидание `DUL` — тот же паттерн, которого не было в путях program memory.
- `project/app/stm8-motor/src/common/oko_boot.h` — `OKO_BLOCK_SIZE 64`,
  границы `OKO_HDR_BASE`/`OKO_APP_END`, версия загрузчика.
- `project/app/stm8-motor/src/boot/boot_hw.c:78–95` — IWDG ~500 мс
  (`PR=0x06`, `RLR=250`) и инициализация I2C-слейва `0x60`.
- `project/app/stm8-motor/util/motorctl.c:36` — `ERASE_SETTLE_US = 500000`;
  `:519` — один вызов `boot_erase()` на весь образ (≈84 блока), отсюда
  требование чанкования на хосте при починке.

SPL (не в репозитории; внутри образа `oko-stm8-sdcc`,
`/root/STM8-SPL-SDCC/Libraries/STM8S_StdPeriph_Driver/src/`):

- `stm8s_flash_private.c:169–215` — `FLASH_EraseBlock()`: `CR2 |= ERASE`,
  `NCR2 &= ~NERASE`, `*pwFlash = (uint32_t)0` (32-битный триггер), без
  ожидания `EOP` внутри.
- `stm8s_flash_Unlock.c:48–65` — `FLASH_Unlock()`: только запись ключей в
  `PUKR`/`DUKR`, `PUL`/`DUL` не опрашиваются.
- `stm8s_flash_private.c:120–166` — `FLASH_WaitForLastOperation()`: опрос
  `EOP | WR_PG_DIS` с `OPERATION_TIMEOUT`; помечена `IN_RAM`.

Стенд (2026-11-22, `output/bench/stm8/`):

- `ki075-pul-experiment-20261122.txt` — ожидание `PUL` (E0): эффекта нет.
- `ki075-cr1-experiment-20261122.txt` — `CR1.HALT`, `HALT+AHALT`,
  диагностическая сборка (E1): эффекта нет, управление не возвращается из
  записи-триггера.

## Ссылки

- AN2659: `https://www.st.com/resource/en/application_note/cd00176595.pdf`
  (локально скачано с зеркала
  `https://raw.githubusercontent.com/ccyinlu/stm8l-bootloader/master/doc/AN2659_Stm8_IAP_using_a_customized_user_bootloader.pdf`;
  `st.com` из этой сети недоступен).
- AN2737: `https://www.st.com/resource/en/application_note/an2737.pdf`
  (локально скачано с `https://archive.org/download/manuallib-id-2709231/2709231.pdf`,
  OCR-скан ManualLib, 190 305 Б; в `snapshot/` не хранится — это не
  upstream-файл ST).
- PM0051: `https://www.st.com/resource/en/programming_manual/cd00191343.pdf`
  (локально скачано с
  `https://archive.org/download/manualzilla-id-5670440/5670440.pdf`).
- RM0016 (STM8S reference manual) — регистровая карта `FLASH_CR1/CR2/NCR2/
  IAPSR/PUKR/DUKR`; используется в комментариях кода.

## Провенанс снимков

| upstream | локальный файл | лицензия | sha256 | размер | дата снятия | pin |
|---|---|---|---|---|---|---|
| AN2659 §4.2–4.3, Doc ID 14153 Rev 4, стр. 15–16 | `snapshot/AN2659-block-ops.txt` | MCD-ST Liberty SW License Agreement (документ ST; текст извлечён из PDF, правок нет) | `2f15bb14a2fea70b4281552a96f393c72685b49f096a3fcc6539f9b70f1203d8` | 6 128 Б | 2026-11-22 | источник — PDF `05dacc332e62c3aa9b3a59ee313147ca72afe352e919e0ef3b4fdf8c350aa297` (287 477 Б) |
| PM0051 стр. 16–20 (block programming, MASS/unlock, word и block erase), Doc ID 14614 Rev 3 | `snapshot/PM0051-block-erase.txt` | MCD-ST Liberty SW License Agreement (текст извлечён из PDF, правок нет) | `c04c124287a784a5248b89ca3ff419938f8697318724d88bb247553e1bff80f8` | 11 412 Б | 2026-11-22 | источник — PDF `ab5d9dbf7f57c5fb435d7f9455c20c3f0cb9c656d45c3f3f5b7ec743683b1c85` (197 136 Б) |

Решение по снимку: полные PDF не дублируются; в `snapshot/` лежат дословные
текстовые извлечения нужных страниц (pypdf 7.0.1, только пробелы текстового
слоя отличаются от PDF), а исходные PDF AN2659 (287 477 Б) и PM0051
(197 136 Б) зафиксированы хешем и размером в таблице выше. Итого 2 файла /
17 540 Б.

## Как обновить снимок

1. Скачать PDF по ссылке из раздела «Ссылки» (или с зеркала, если `st.com`
   недоступен), сверить `sha256` с таблицей — при расхождении это другая
   ревизия документа, обновить и таблицу, и текст.
2. Извлечь нужные страницы без правок:
   `python3 -c "from pypdf import PdfReader; ..."` (pypdf ≥ 7), либо
   `pdftotext -f 15 -l 16` для AN2659 и `-f 16 -l 20` для PM0051.
3. Заменить `snapshot/*.txt`, сохранив шапку «Verbatim text extract» с новыми
   хешем, размером и датой; пересчитать `sha256sum`.
4. Обновить таблицу провенанса, строку в индексе `research/README.md` и
   запись в `CHANGELOG.md` (`[Не выпущено]`, пара RU+EN).