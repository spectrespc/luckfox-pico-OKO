# rockchip-linux/kernel: фиксы develop-5.10 для нашего ядра 5.10.160

## Вопрос

Какие коммиты ветвей `develop-5.10` и (для SFC) `develop-6.12` репозитория
`rockchip-linux/kernel` закрывают открытые KI проекта — SFC/UBIFS `ro`
(KI-071), oops в `rk3x_i2c_irq` (KI-073), SCL-hold и мёртвую шину I2C3
(KI-080), wake/PMU (KI-072/074/079), HDR на SC230AI (KI-011) — и переносимы
в локальное дерево без git-истории?

## Что это

Наше ядро — вендорский форк **5.10.160** без `.git`
(`sysdrv/source/kernel/Makefile`), правки ведутся in-tree. Точка синхронизации
Luckfox ≈ март–июль 2024, поэтому большинство коммитов Rockchip за 2023 —
начало 2024 в дереве уже есть, а полезны только поздние (2024–2026) и один
кросс-порт из `develop-6.12`.

В `snapshot/` лежат четыре серии — по одной на изменяемый файл:

| Серия | Файл ядра | Коммитов |
|---|---|---|
| `spi-rockchip-sfc-5.10.series.patch` | `drivers/spi/spi-rockchip-sfc.c` | 5 (один из `develop-6.12`) |
| `i2c-rk3x-5.10.series.patch` | `drivers/i2c/busses/i2c-rk3x.c` | 5 |
| `rv1106_pm-5.10.series.patch` | `arch/arm/mach-rockchip/rv1106_pm.c` | 3 |
| `sc230ai-5.10.series.patch` | `drivers/media/i2c/sc230ai.c` | 3 |

Решение по формату (правило 5 `research/README.md`): снимок сделан **сериями
патчей**, а не копиями upstream-файлов. Полные файлы `sc230ai.c` и
`spi-rockchip-sfc.c` из `develop-5.10` — это уже другая кодовая база
(другие IP-версии, другой набор режимов), они не являются «эталоном» для
нашего форка и вместе с полными коммитами сенсора (60–111 файлов, до 781 КБ)
не влезают в лимит. Серии сохраняют ровно то, что переносилось. Для
`sc230ai.c` из каждого коммита сохранена шапка (From/Subject/date/
Signed-off-by) и только хунки этого файла; остальные файлы тех коммитов
(прочие сенсоры) в снимок не входят.

## Выводы для OKO

**Переносимо (применено в этой задаче):**

| Upstream | Дата | Что даёт | Состояние |
|---|---|---|---|
| `53123db8345873e478b23a7739703efdb5376dcc` | 2026-07-23 | обнуление delay lines перед DLL-тюнингом | применено |
| `9dc03be23fb536b6a6fb861c4a3272fe0925a7c9` | 2025-11-10 (`develop-6.12`) | инициализация контроллера и восстановление DLL в `runtime_resume` | кросс-порт в 5.10 |
| `2b42b3527ea03e894d33dc6d878c27e41f022588` | 2023-12-22 | `i2c->msg` обнуляется только в состоянии IDLE | применено |
| `fca16960f80380d7e585b1b04ecfd7f08bf3079b` | 2024-05-07 | TIMEOUT_WKUP только при ненулевом `PMU_WAKEUP_TIMEOUT_CNT` | применено |
| `856ec70f8bb014b4aa892db413dbc907fe4c7057` | 2024-05-29 | save/restore `rtc_clamp_en` (`vi_grf`) | применено |
| `8854147ef26e6b1c6db008765a582a9d485a3c46` | 2024-10-25 | 32k-ext только при `chip_version > 0` | применено |
| `365e2f63c957bc21ca7eaef89c37cded1ae299ec` | 2024-04-03 | `s_frame_interval`, `bus_code`, tie-break по `bus_fmt` | применено |
| `34690d3be73e98c6b037e24c76b3200fb22b9e79` | 2024-09-04 | выбор режима по `s_fmt`/`s_frame_interval`/`set_hdr` | применено |
| `aecea3e48f45e519d2da3be181f28bb819fdb6cd` | 2024-09-29 | исправление `cur_best_fit` в `SET_HDR_CFG` | применено |

**Уже было в дереве (переносить нечего):**

| Upstream | Дата | Маркер в нашем дереве |
|---|---|---|
| `a7c2ff483d857f192fa3b49d0c64bd24283ab0c5` | 2024-02-29 | `spi-rockchip-sfc.c:197` (`struct spi_master *master`), `:1021` (`sfc->master` в remove) |
| `58856019542ef88a5797c5e8ed0fa7ec810cadef` | 2024-02-29 | метка `err_dma` — `spi-rockchip-sfc.c:1006` |
| `924d32547cd5e78a02bf859688f313358ac4b26e` | 2024-04-23 | `spi-rockchip-sfc.c:964` (`5000 * USEC_PER_MSEC`) |
| `d8a07ba130da09763442adb598d6c881fa777cc3` | 2023-12-07 | `i2c-rk3x.c:264` (`int irq`), `:1724` (`i2c->irq = irq`), `:1301` (`disable_irq`) |
| `6272868cd8921f94a0e82997c36e6a49abe6a080` | 2023-12-19 | `i2c-rk3x.c:641` (`REG_INT_START` в маске неинтересных бит) |
| `f6730fc3b48ad20efb580cf22b687dccfa85fccf` + `13505e1858afef433c91ec59309300b6829552b8` | 2025-10-29 | `i2c-rk3x.c:397-400` — атомарная запись CON+CNT под `i2c->lock` (наложено локальным коммитом `9ee690f5d`) |

**Не переносимо / отклонено:**

- `563fd7672f0a` (normal memory for DMA) — меняет тип буферов обмена с
  embedded DMA; без стендовой проверки риск регресса SFC на всех операциях,
  а KI-071 закрывается таймаутом/сбросом.
- thunder-boot-специфичные `1c56c45293a5`, `5284f3e91eed`, `f70babda2a22` —
  ждут статуса DMA от thunder-boot загрузчика; OKO грузит AOV-ядро через
  FIT/`boot.its`, этого состояния в дереве нет, код был бы мёртвым.
- Новые IP/режимы: `32d6069a2adf` (`rv1103b-fspi`), `cbc4d51a2dd3` /
  `a9d53bdef7cd` (`sclk_x2`), `b54436c2dc91` (`auto_merge`), `784e52a797a9`
  (`sfc-cs-gpio`), `5e2441babf22` / `534b4d00d940` (новые fspi-версии) —
  других IP у OKO нет, проверять нечем.
- `147348c8fc14` (10 кГц data update point) — режим 10 кГц на шине не
  используется (SC230AI/STM8/INA3221 работают на 100–400 кГц).
- `5ccac5354513` (`rv1103b` GRF в I2C) — другой SoC.
- `02fc73a44beb`, `fba2070492fc` (divisor/version после MCU) — thunder-boot
  путь, в OKO не используется.

**Гейт HDR не снимается.** Сенсорные коммиты `365e2f6`/`34690d3`/`aecea3e`
дают ядру возможность переключать HDR-режимы и корректно отдавать интервал
кадра, но `rk_aiq`/IQ-часть для HDR2/HDR3 в проекте отсутствует, поэтому
KI-011 остаётся заблокированным: «сенсорная часть обновлена, проверка не
выполнена».

**Отклонение от upstream (обосновано):** в `5885601` опубликованный код
содержит `return -ENOMEM;` перед `goto err_dma;` — `goto` недостижим, коды
возврата/очистка не работают. В нашем дереве заменено на
`ret = -ENOMEM; goto err_dma;`.

**Совместимость с локальными правками OKO:**

- SFC: собственная защита KI-071 (`reset`+`init` в `rockchip_sfc_resume`)
  сохранена; `9dc03be` переносит восстановление DLL в `rockchip_sfc_init()`,
  поэтому цикл в `resume` убран — двойного reset нет, init вызывается один
  раз на каждом пути пробуждения.
- I2C: guard `if (!i2c->msg)` в NAK-ветке (KI-073, коммит проекта) сохранён;
  `2b42b35` делает его срабатывание более редким (msg живёт до IDLE), но не
  отменяет.
- PM: AOV-хуки (`oko_aov_enabled`, `pmu_timeout_cnt`, `hpmcu_fast_wkup`,
  `gic_irq_clear_pending`, окно mailbox 1000 мс) не тронуты. `fca1696`
  делает таймаут зависимым от значения счётчика: divinus обязан программировать
  `PMU_WAKEUP_TIMEOUT_CNT` перед `echo mem`, иначе TIMEOUT_WKUP выключен.

## Проверенные факты

До правок (grep-маркеры отсутствовали): в `rv1106_pm.c` не было
`rtc_clamp_en`/`chip_version`/проверки `TIMEOUT_CNT`; в
`spi-rockchip-sfc.c` не было обнуления delay lines и `init` в
`runtime_resume`; в `sc230ai.c` не было `s_frame_interval`/`bus_code` и
выбора режима по FPS.

После правок (пути и строки в этом дереве):

- `sysdrv/source/kernel/drivers/spi/spi-rockchip-sfc.c`
  - `:298` — `rockchip_sfc_init()` с циклом восстановления `dll_cells`;
  - `:659` — `rockchip_sfc_set_delay_lines(sfc, 0, cs)` перед тюнингом;
  - `:985-990` — исправленный путь OOM (`ret = -ENOMEM; goto err_dma;`);
  - `:1042` — `runtime_resume` вызывает `rockchip_sfc_init()`;
  - `:1078-1104` — `resume`: `reset` + `init` без дублирующего цикла DLL.
- `sysdrv/source/kernel/drivers/i2c/busses/i2c-rk3x.c`
  - `:408-441` — `rk3x_i2c_stop()`: `i2c->msg = NULL` только после
    `STATE_IDLE`;
  - `:615` — `rk3x_i2c_handle_stop()`: обнуление msg после IDLE;
  - `:654` — сохранён guard KI-073.
- `sysdrv/source/kernel/arch/arm/mach-rockchip/rv1106_pm.c`
  - `:87` — `rv1106_chip_version`;
  - `:216` — регион `vi_grf` `rtc_clamp_en` (маска `0x00400000`);
  - `:712` — 32k-ext только при `chip_version > 0`;
  - `:847` — TIMEOUT_WKUP по значению `RV1106_PMU_WAKEUP_TIMEOUT_CNT`;
  - `:1402` — чтение версии чипа из `PMUGRF_OS_REG(1)`;
  - `:1423` — обнуление `PMU_WAKEUP_TIMEOUT_CNT` при init.
- `sysdrv/source/kernel/drivers/media/i2c/sc230ai.c`
  - `:797` — `bus_code[]`;
  - `:1173` — `sc230ai_find_mode()`;
  - `:1196` — `sc230ai_s_frame_interval()`;
  - `:1279-1281` — `cur_best_fit`/`dst_fps` в ioctl;
  - `:1820` — `.s_frame_interval` в `video_ops`.

Проверка сборки: `./build.sh kernel` и `./build.sh driver` (модули) прошли —
профиль `BoardConfig-SPI_NAND-Buildroot-RV1106_Luckfox_Pico_Pro_Max-SC230AI-AOV.mk`,
DTS `rv1106g-luckfox-pico-pro-max-sc230ai-aov.dts`. `output/image/boot.img` —
AOV-FIT из `boot-aov.its` (`mkimage -l`: `compression = gzip`, kernel
4 782 493 Б, `load = entry = 0x00208000`), `sc230ai.ko` собран и установлен в
`sysdrv/out/kernel_drv_ko/`.

Стендовая проверка не выполнялась. На 2026-11-24 сеть стенда жива
(`192.168.52.17/config` и `192.168.52.33/api/status` отвечают `401`), но
прогон требует netboot/консоли: вывод DUT мост не отдаёт (KI-078), NAND
деградирует от перепрошивок (KI-071), а suspend-путь самого стенда на
2026-11-24 нестабилен даже без новых патчей (KI-080) — результат был бы
неотличим от уже известных отказов. Поэтому статусы KI не менялись на
«исправлено».

## Ссылки

- Репозиторий: `https://github.com/rockchip-linux/kernel`
- История по файлу (HTML, без query-string):
  `https://github.com/rockchip-linux/kernel/commits/develop-5.10/drivers/spi/spi-rockchip-sfc.c`
  (аналогично для остальных трёх путей и для `develop-6.12` у SFC)
- Отдельный коммит: `https://github.com/rockchip-linux/kernel/commit/<full-sha>.patch`
- Список коммитов по пути через API:
  `https://api.github.com/repos/rockchip-linux/kernel/commits?sha=<branch>&path=<file>`

## Провенанс снимков

Лицензия всех файлов ядра — `GPL-2.0` (SPDX в исходниках Rockchip); патчи
содержат только контекст и изменения upstream-кода.

| upstream | локальный файл | лицензия | sha256 | размер | дата снятия | pin (полные SHA) |
|---|---|---|---|---|---|---|
| `drivers/spi/spi-rockchip-sfc.c` | `snapshot/spi-rockchip-sfc-5.10.series.patch` | GPL-2.0 | `3e5335fcf165a92a5565d972af939e3ac0f6051999dfae8685ef0217cb7d43b8` | 7 407 Б | 2026-11-24 | `a7c2ff483d857f192fa3b49d0c64bd24283ab0c5`, `58856019542ef88a5797c5e8ed0fa7ec810cadef`, `924d32547cd5e78a02bf859688f313358ac4b26e`, `9dc03be23fb536b6a6fb861c4a3272fe0925a7c9` (develop-6.12), `53123db8345873e478b23a7739703efdb5376dcc` |
| `drivers/i2c/busses/i2c-rk3x.c` | `snapshot/i2c-rk3x-5.10.series.patch` | GPL-2.0 | `0909e4819bd90d68ff07a7d7840dd997be5a092244dc9ecc0ddbb336d98b2ec1` | 8 321 Б | 2026-11-24 | `d8a07ba130da09763442adb598d6c881fa777cc3`, `6272868cd8921f94a0e82997c36e6a49abe6a080`, `2b42b3527ea03e894d33dc6d878c27e41f022588`, `f6730fc3b48ad20efb580cf22b687dccfa85fccf`, `13505e1858afef433c91ec59309300b6829552b8` |
| `arch/arm/mach-rockchip/rv1106_pm.c` | `snapshot/rv1106_pm-5.10.series.patch` | GPL-2.0 | `f84b90b3ab2d39723ccc220f0bf68a984a4ed989d929b01052d59ba30f2e1d2b` | 4 214 Б | 2026-11-24 | `fca16960f80380d7e585b1b04ecfd7f08bf3079b`, `856ec70f8bb014b4aa892db413dbc907fe4c7057`, `8854147ef26e6b1c6db008765a582a9d485a3c46` |
| `drivers/media/i2c/sc230ai.c` | `snapshot/sc230ai-5.10.series.patch` | GPL-2.0 | `c9f9bdb05b8afe5b3c1052a43dd8ecde6084c0765db818159f7675587900ed62` | 8 100 Б | 2026-11-24 | `365e2f63c957bc21ca7eaef89c37cded1ae299ec`, `34690d3be73e98c6b037e24c76b3200fb22b9e79`, `aecea3e48f45e519d2da3be181f28bb819fdb6cd` |

Итого 4 файла / 28 042 Б — в пределах лимита темы.

## Как обновить снимок

1. Запросить список коммитов по пути:
   `curl -sL -G https://api.github.com/repos/rockchip-linux/kernel/commits -d sha=develop-5.10 -d path=<file>`
   (без query-string в URL: символы `&`/`|`/`;` запрещены политикой команд).
2. Скачать патчи по полному SHA: `curl -sL -O https://github.com/rockchip-linux/kernel/commit/<full-sha>.patch`
   (короткие SHA вне default-ветки по этому URL отдают 404).
3. Для `sc230ai.c` вырезать из патча шапку коммита и только
   `diff --git a/drivers/media/i2c/sc230ai.c ...`-блок; для остальных файлов
   серия собирается конкатенацией патчей в хронологическом порядке.
4. Пересчитать `sha256sum` и размеры, обновить таблицу выше, строку в индексе
   `research/README.md` и запись в `CHANGELOG.md` (`[Не выпущено]`, RU+EN).