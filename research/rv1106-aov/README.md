# RV1106 AOV, HPMCU/LPMCU и JTAG: внешние материалы

## Вопрос

Что из внешних материалов по RV1106 AOV и MCU применимо к OKO:

- независимый AOV-проект `dehai168/rv1106-aov-ipc` (Pico Max + MIS5001);
- обсуждения Luckfox `#10` и `#99` (HPMCU/LPMCU, JTAG, toolchain, загрузка FW);
- `luyi1888/rv1106-mcu` (userspace-загрузчик MCU и blinky без idblock);
- `liamhays/rk3566-mcu` (устройство SCR1, загрузка, mailbox, таймер, поведение в suspend);

и подтверждается ли тезис, что HPMCU/LPMCU — «те же», что в RK3566, а для
корректного сна нужно периодически опрашивать периферию через MCU.

## Что это

**`dehai168/rv1106-aov-ipc`** — userspace-приложение `ipc_app` (~30 файлов C)
для Luckfox Pico Max (RV1106G3, 256 МБ, MIS5001, SPI NAND + TF), не форк SDK.
24 коммита, `main` = `07a2f553bc83eae8ccb3fa7a805403da6aef6dd7`, `LICENSE` в
корне **нет**. Правки ядра/DTS/U-Boot/Trust в репозитории не лежат — только
описаны в `docs/04-worklog.md` (75 сессий) и `docs/design/aov*.md`
(эти файлы удалены из снимка 2026-11-25 после переноса выводов).
Их «AOV» — это PMU-timer 1 fps + пользовательский `echo mem`; MCU-детекция
движения не сделана (для MIS5001 нет wakeup-прошивки), в soak'ах движение
эмулируется `debug_motion_interval`.

Локальная копия `../rv1106-aov-ipc` (pin тот же, `git status` чистый)
разобрана; после зачистки 2026-11-25 в `snapshot/dehai168-rv1106-aov-ipc/`
осталось 15 файлов (7 577 570 Б): AOV guide PDF, design-материалы
ISP-handover, storage/media/record/OSD/fMP4/RKNN и soak-скрипты.
Отработавшие `worklog`/`todolist`/`aov.md`, `aov_service.c`,
`network_service.c`, `detect_service.c` и `test_aov_fephy_off_mem.sh`
удалены — их выводы перенесены в этот README (см. «Проверенные факты»).
Лицензии нет; снимок включён по решению владельца проекта как read-only
research copy, не для переиспользования кода.

**Luckfox issues.** `#10` — про RISC-V MCU в RV1106: Rockchip не публикует
документацию на MCU; JTAG — IEEE 1149.1, двухпроводный SWD/cJTAG; в `PMU_SGRF`
есть `LPMCU_BOOT_ADDR`, но все известные бинарники Rockchip грузятся в HPMCU.
`#99` — практика: `mcuload` грузит blinky в `0xff6c0000`; найден HPMCU JTAG
(`GPIO1_B2/B3`, `devmem 0xff538008 32 0xff003300`, IDCODE `0xDEB11001`);
toolchain — xPack `riscv-none-embed-gcc 10.2.0-1.2`; HPMCU и ARM имеют общую
карту памяти; отдельно упомянут `RK3566_AMP_SDK` как источник blinky.

**`luyi1888/rv1106-mcu`** — минимальный `mcutool` (GPL-3.0) и blinky-бинарь.
Грузит FW в SRAM `0xff6c0000` через `/dev/mem`, без idblock: сброс
(`0xff3b8a04`), cache-окно (`0xff040024/28`), boot address (`0xff076044`),
release. Последовательность повторяет
`u-boot/arch/arm/mach-rockchip/rv1106/rv1106.c`.

**`liamhays/rk3566-mcu`** — самые подробные публичные заметки (GPL-3.0) по
SCR1 в Rockchip: RV32IMC, 3 стадии, только machine mode, 16-линейный IPIC,
загрузка по 64-КБ границе, `SYSTEM_SRAM` 64 КБ, `PMU_SRAM` 8 КБ только secure,
DDR — только данные, таймер `0xFE7F0000`, INTMUX `0xFE790000`, mailbox
`0xFE780000`, PMU-биты пробуждения MCU. Там же важный отрицательный результат:
на RK3566 MCU **выключается** при suspend (TF-A запрашивает bus idle для
`BIU_BUS` и `BIU_SECURE_FLASH`), поэтому использовать MCU во сне нельзя.

Лицензии: `rv1106-mcu` и `rk3566-mcu` — GPL-3.0; конфиги OpenOCD —
`GPL-2.0-or-later` (SPDX в файле); у `rv1106-aov-ipc` лицензии нет, поэтому
его 15 выбранных файлов включены в `snapshot/` по решению владельца проекта
как read-only research copy, не для переиспользования кода.

**`RV1106 TRM V0.3 Part1`** — 877 страниц, 2022-03-24, основной первоисточник
по PMU/CRU/GRF/MCU/I2C/SPI. Для темы важны: PMU (гл. 7, с. 258–280), MCU
Subsystem (гл. 8, с. 281–298), I2C (гл. 15, с. 444–460), SPI (гл. 23,
с. 747–762), PMU_GRF (с. 160–169). TRM прямо говорит о трёх доменах
`VD_CORE`/`VD_LOGIC`/`VD_PMU` и о clamp `VD_PMU` перед power off `VD_LOGIC`.

**`RV1106 Datasheet Rev 1.8`** (2025-03-12, 25 страниц, зеркало
`atta.szlcsc.com`) — 3 домена `VDD_ARM`/`VDD_LOGIC`/`VDD_PMU`, 8 КБ PMU SRAM,
таблица питания и распиновка. Ключевое для H2: I2C3 M1 — GPIO1_D3/D2
(pins 85/86), их IO-питание `GPIO1_VCC3V3` (pin 81), отдельное от
`DVDD_i`/`VDD_LOGIC`; I2C1 — в PMU-блоке, на GPIO0_A5/A6.

**`RV1106 Datasheet Rev 2.0`** (2026-04-02, 31 страница, зеркало `rockchip.fr`)
— редакция, где в §2.1 order info сведены все три заказные версии:
`RV1106G2` (QFN128, `Cortex A7 + MCU + 1.0 TOPS NPU + 1Gb DDR3L + 5 МП@30`),
`RV1106G3` (QFN128, `Cortex A7 + MCU + 1.0 TOPS NPU + 2Gb DDR3L + 8 МП@15`) и
`RV1106` (BGA313, внешняя DDR2/DDR3/DDR3L/DDR4). Там же §2.2 top marking,
§2.3 package dimension и §2.6 pin number list для G2/G3. Относительно Rev 1.8
исправлена оценка NPU: у обеих SIP-версий 1.0 TOPS (в Rev 1.8 у G2 было
0.5 TOPS). Наш DUT (Luckfox Pico Max) несёт именно `RV1106G3`: 2 Гбит
DDR3L в корпусе, то есть 256 МБ ОЗУ.

**`rkbin/doc/release/RV1106_EN.md`** — журнал версий RV1106-бинарей. Для темы:
`hpmcu_wakeup_v1.00` (первый HPMCU sleep wake-up), `ddr_*_v1.15`
(конфигурация таймеров под suspend/resume), `ddr_924MHz_tb_v1.13`
(DDR sleep/resume), `hpmcu_wrap_v2.03rc4` (AVS-флаг не восстанавливался после
resume). Локальный `rkbin` уже содержит DDR v1.15, wakeup v1.00 и
`hpmcu_tb_sc230ai_v1.90`; upstream новее по wrap, DDR v1.16 (OTP) и TEE/SPL,
но принципиально новых записей по PMU-сну нет.

**`Rockchip I2C Developer Guide V2.2.0`** (2021-12-29, 14 страниц, public).
Даёт диагностическую карту `ipd`/`state`, NACK/timeout cases и требования к
pull-up/rise/fall. Исходный URL отдаёт 403; копия пользователя
`/home/spectre/Rockchip_Developer_Guide_I2C_EN.pdf` (315 278 Б, sha256
`c3f463…`) включена в `snapshot/rockchip-i2c-guide/` и **побайтово
совпадает** с зеркалом `as-jackson/Rockchip-Docs` (pin `a1717bfc…`).

**`linux-rockchip 029415`** (2022-02-10, Jon Lin) — патч
`spi: rockchip: Suspend and resume the bus during NOIRQ_SYSTEM_SLEEP_PM ops`.
Обоснование: wakeup-обработчик не должен трогать SPI, пока шина в suspend;
поэтому `SET_NOIRQ_SYSTEM_SLEEP_PM_OPS` и прямые clk disable/enable.

**`as-jackson/Rockchip-Docs`** — публичное зеркало документации Rockchip
(`en/` + `cn/`, pin `a1717bfc…`). Для темы отобраны 5 public-документов:
Pinctrl V1.0.0 (RV1106 Linux 5.10), IO-Domain V1.0.1, Power Analysis V1.0.1,
Camera Troubleshooting Linux 5.10 V1.0.1 (RV1106/RV1103) и RGA IM2D V2.2.3.
`SOC_Internal_MCU_CN` (V1.0.0, 2024-05-27) просмотрен, но **не копировался**:
помечен «内部资料» и прямо исключает PMU/DDR MCU из публичного описания; его
факты пересказаны ниже.

**`D-Robotics/x5-libcam-sensor`** — userspace sensor-модули для Horizon X5
(pin `8c41592…`). Для SC230AI полезны `sc230ai/inc/sc230ai_setting.h`,
`sc230ai/sc230ai_utility.c`, `sc230ai/MaybeUsed` и
`sc230ai_v4l2/sc230ai_camera_helper.c`: frame-sync master/slave, динамическая
смена FPS через VTS, DOL2-таблица и gain LUT. Код проприетарный (Horizon,
all rights reserved), включён по решению владельца как read-only research copy.

**`Lanxin-MRDVS/CameraSDK`** — SDK для RGBD/dToF-камер Lanxin (pin `e021eba…`),
без SC230AI/RV1106-кода: только готовые библиотеки, C/C++/Python/ROS-обёртки.
Для OKO не используется.

## Выводы для OKO

### Переносимо

| Что | Зачем OKO | Ограничение |
|---|---|---|
| **HPMCU JTAG** | Сейчас MCU отлаживается только по UART2 и косвенным признакам; JTAG даёт halt/step/регистры | Нужен cJTAG-адаптер (DirtyJTAG/J-Link) и освобождение UART2; `devmem 0xff538008 32 0xff003300`, IDCODE `0xDEB11001`; в U-Boot дополнительно `mw.l 0xff076040 0xffff0002` |
| **Загрузка FW через `mcutool`** | Позволяет гонять тестовую прошивку в SRAM без пересборки idblock и без NAND | Только на debug-образе: адрес `0xff6c0000` и сброс HPMCU конфликтуют с AOV-раскладкой OKO (`HPMCU` из idblock, `rtos@40000`) |
| **SCR1/toolchain** | Подтверждает, что OKO использует правильное семейство: RISC-V, xPack 10.2.0, `-march=rv32imc -mabi=ilp32 -misa-spec=2.2` | Флаги из RK3566-заметок; наш `defconfig` уже `CONFIG_ARCH_RISCV=y`, toolchain тот же |
| **Mailbox/PMU wake** | Концептуальная схема «MCU → PMU → AP»: `PMU_INT_MASK_CON`/`PMU_WAKEUP_INT_CON`/`PMU_WAKEUP_INT_ST`, soft IRQ | Адреса RK3566 (`0xFDD9xxxx`, `0xFE780000`) к RV1106 напрямую не применимы; на RV1106 у OKO свой mailbox-протокол (`-h-wait-`/`-h-mbox-`) |
| **LPMCU register map** | В вендорском HAL OKO уже есть `LPMCU_BOOT_ADDR`, soft IRQ, reset-hold и `WFI_HALTED` — это готовая отправная точка для эксперимента | TRM p279 §7.6.2 подтверждает роль LPMCU в software low-power procedure; ISA/debug не описаны, прошивки/драйвера нет; формат `BOOT_ADDR` (20 бит) не подтверждён |
| **AOV-чеклист dehai** | Независимо подтверждает обязательные шаги: deep + `rockchip,rv1106`, FE-PHY off, PMU `ms×32`, `last_resume_reason`. Итог 2026-11-24/25: «PMU `ms×32`» перенесён в divinus (счётчик 32 кГц), кадентность и motion-resume подтверждены; FE-PHY и `last_resume_reason` стендом отвергнуты; deep+compatible и LPMCU изложены в разделах «Переносимо» и «Про периодический опрос» | Код не переносить: лицензии нет, другой сенсор, MCU-детекции нет; снимок удалённых частей заменён выжимкой фактов в этом README |
| **Медиа-грабли (вне AOV)** | Полезный regression-чеклист: VI NV12 через `GetChnFrame` ненадёжен, RGA-цель только MMZ, `MmzFlushCache(RK_FALSE)`, stride/UV по `vir_height`, не блокировать `GetStream`, stage MP4 на TF, OSD `pos` кратен 16, fMP4 `trun=0x701`, RKNN грузить лениво | Это их выводы для MIS5001/их сборки; в OKO часть уже решена иначе |

### HPMCU/LPMCU vs RK3566

| Параметр | RK3566 (заметки) | RV1106 |
|---|---|---|
| Ядро | SCR1, RV32IMC, 3 стадии | HPMCU — RV32I+M+C, 3 стадии, 16 КБ cache, INTMUX (TRM p281); LPMCU — отдельного ISA/debug-раздела в TRM нет |
| Загрузка | `GRF_SOC_CON4`, старшие 16 бит, граница 64 КБ | `SCR1_BOOT_ADDR` в SGRF, default 0, trap `0x001001C0` (TRM p298); OKO: `CORE_SGRF+0x44`, `0x40000` |
| Reset/release | `CRU_SOFTRST_CON26` | `CORECRU_SOFTRST_CON01` (`0xff3b8a04`) |
| Память | `SYSTEM_SRAM` 64 КБ, `PMU_SRAM` 8 КБ secure | `SYSTEM_SRAM` 256 КБ @`0xFF6C0000`, `PMU_SRAM` 8 КБ @`0xFF670000` (TRM p12–13); окно `rtos@40000` (OKO) |
| Mailbox | `0xFE780000` | свой протокол OKO (`-h-wait-`/`-h-mbox-`); PMU_MAILBOX @`0xFF378000` (TRM p12–13) |
| Suspend | MCU выключен (TF-A bus idle) | HPMCU живёт, владеет ISP/sensor |
| JTAG | GPIO0 (на Quartz64 не выведен) | HPMCU: m0 GPIO1_B2/B3, m1 GPIO3_A6/A7; LPMCU: m0 GPIO1_B2/B3, m1 GPIO3_A4/A5; для HPMCU включить `CORE_SGRF CPU_CTRL_CON[1]` (TRM p296) |

TRM (гл. 7–8) уточняет: в RV1106 документирован **один MCU Subsystem —
HP_MCU** (HPMCU, RV32I+M+C, 16 КБ cache, INTMUX). LPMCU нарисован внутри
PMU-блока и имеет `LPMCU_BOOT_ADDR`, soft IRQ, reset-hold и `WFI_HALTED`, но
отдельной главы об его ISA/debug нет. Поэтому «HPMCU/LPMCU — те же SCR1, что в
RK3566» верно только для HPMCU; LPMCU — контроллер в PMU-блоке, а не второй
документированный SCR1.

### TRM/даташит: что подтвердилось и что опровергнуто

- **Deep действительно отключает логику.** PMU поддерживает `VD_CORE`,
  `VD_LOGIC`, `VD_PMU` (TRM p258); `PMU_CRU_PWR_CON0` содержит
  `power_off_ena`, `input_clamp_ena`, `osc_dis_ena`, `alive_32k_ena`
  (TRM p274–275). OKO ставит `POWER_OFF | INPUT_CLAMP | OSC_DIS` в
  `pmu_sleep_config()` — это отключение/изоляция логики.
- **Но это происходит на каждом deep-входе, а не через 60 с.** PMU FSM
  запускается по `PMU_PWR_CON[0]` + WFI и проходит состояния SCU → bus → DDR →
  power gating → clock/reset → sleep (TRM p258–262). «60 с» — это
  `PMU_WAKEUP_TIMEOUT_CNT` (offset `0x48`, reset `0x5DC0`), который divinus
  перепрограммирует в `wake_interval_s × 32`; это таймер **пробуждения**, а не
  порог углубления сна.
- **I2C1 — особый случай.** На блок-диаграмме PMU (TRM p258) `I2C1` находится
  внутри PMU-блока вместе с LPMCU, HPTIMER, PMU_WDT и PMU_MAILBOX. У I2C1 есть
  отдельные PMU hold/reset-биты (`PMUGRF_SOC_CON4/5`). Поэтому «все I2C0–I2C4
  в VD_LOGIC» неверно: I2C1 — PMU-блок, I2C3 — вне его (логика).
- **Пины I2C3 — не VD_LOGIC.** Даташит Rev 1.8 (p17, p20): I2C3 M1 — это
  GPIO1_D3 (SCL, pin 85) и GPIO1_D2 (SDA, pin 86), их IO-питание —
  `GPIO1_VCC3V3` (pin 81), отдельное 3.3 В от `DVDD_i`/`VDD_LOGIC`
  (0.81–0.95 В). Контроллер I2C3 — вне PMU-блока (логика), но пады — на
  отдельной 3.3 В линии; теряются ли они одновременно, зависит от платы, а не от PMU
  power-off. IOMUX для I2C3 M1 — `VENCIOC_GPIO1D_IOMUX_SEL_L` (TRM p457),
  домен этих регистров даташит не уточняет.
- **SPI0/SPI1 к H2 не относятся.** На этой плате `&spi0` disabled, `spi1`
  disabled, NAND — на `&sfc`. Отдельный патч linux-rockchip 029415 объясняет,
  зачем SPI-драйверу `SET_NOIRQ_SYSTEM_SLEEP_PM_OPS`: wakeup-обработчик не
  должен трогать шину, пока она в suspend. Наш `spi-rockchip.c` уже содержит
  эту схему.

### I2C3/H2: диагностика по TRM и I2C guide

TRM (гл. 15, p444–457) и Rockchip I2C Developer Guide (V2.2.0) дают более
точную карту, чем «VD_LOGIC/60 с»:

- `RKI2C_DBGCTRL` (offset `0x224`, TRM p455) имеет `nak_release_scl`:
  `0` — **удерживать SCL низким после NACK**, `1` — отпускать. В
  `i2c-rk3x.c` обращений к `0x224` нет; драйвер использует
  `REG_CON1_NACK_AUTO_STOP` (offset `0x228`) при `autostop_supported`, иначе
  `rk3x_i2c_stop()`. Если NACK пришёл без auto-stop, SCL может остаться
  низким — это проверяемая гипотеза H2, а не установленный факт.
- `ipd` в логе i2c-rk3x (I2C guide §6.1.2):
  `0x00` — IOMUX/pull-up/clock/периферия; `0x10` — CPU не отвечает на IRQ;
  `0x80` — SCL удерживает slave. `state=1` — SDA low, `state=2` — SCL low,
  `state=3` — обе. В KI-080 зафиксировано `SCL=0` и освобождение линии при
  unbind **мастера**, то есть мастер/пад, а не подтверждённый slave.
- `RKI2C_DBGCTRL` также даёт `h0_check_scl`, `flt_en`, `slv_hold_scl_th`
  (TRM p455); `RKI2C_SCL_OE_DB` — debounce slave hold (TRM p452).
- I2C guide требует pull-up и контроля `i2c-scl-rising-time-ns`/
  `i2c-scl-falling-time-ns` (p2, §2.1). В нашем `&i2c3` задан только
  `clock-frequency = <100000>`; rise/fall не измерены.
Полная карта отказов из guide (полезно для H2):

- NACK (`-ENXIO`): неверный адрес; slave без питания или с неверной
  последовательностью питания; несовместимая тайминг-схема (slave ждёт STOP,
  а получает repeated START); внешний сигнал/помеха — смотреть осциллографом.
- Timeout `ipd=0x00`: ошибка IOMUX SCL/SDA; неверное или отсутствующее pull-up;
  линию держит периферия; не включён или слишком медленный `clk_i2c`;
  одновременно выставлены `CON_START` и `CON_STOP`.
- Timeout `ipd=0x10`: контроллер исправен, но CPU не обслуживает IRQ — cpu0
  заблокирован или IRQ замаскирован; смотреть `/proc/interrupts`.
- Timeout `ipd=0x80`: SCL удерживает slave; искать исключением или
  последовательным резистором (≥1/20 от pull-up) + осциллограф; часто
  просаживается SDA.
- `state=1` — SDA low, `state=2` — SCL low, `state=3` — обе.
- Режимы: TX `I2C_CON[1:0]=00`, mix `01/11`, RX `10`; максимум 32 байта за
  транзакцию; `SCL = clk_i2c / (8*(CLKDIVL+1+CLKDIVH+1))`, `clk_i2c`
  100–200 МГц (TRM p444–450).
- `i2c-gpio` (GPIO-эмуляция) в guide считается медленной и не рекомендуется,
  но именно ей можно сделать bit-bang recovery (стандартные 9 тактов SCL),
  если контроллер залип.
- `i2c-tools` (`i2cdetect`, `i2cdump`, `i2cset`, `i2cget`, `i2ctransfer`) —
  штатная проверка после resume.

- Следующий диагностический набор: прочитать `RKI2C_IPD`, `RKI2C_STATUS`,
  `RKI2C_DBGCTRL` сразу после отказа; сравнить с `ipd`/`state`; проверить
  `nak_release_scl=1`; снять SCL/SDA/3V3 осциллографом; только потом делать
  вывод о slave/питании.

### rkbin: что применимо к AOV

- `rv1106_hpmcu_wakeup_v1.00` (2023-08-12) — первый вендорский HPMCU
  sleep wake-up; это базовая линия для наших wakeup-образов.
- `rv1106_ddr_*_v1.15` (2023-12-21) — «Modify timer configuration to fit
  suspend & resume requirements»; локальный `rkbin` уже содержит DDR v1.15.
- `rv1106_ddr_924MHz_tb_v1.13` (2023-07-10) — «Supports DDR sleep and resume»;
  тоже уже покрыто v1.15.
- `rv1106_hpmcu_wrap_v2.03rc4` (2026-03-12) — «Fix avs flag not resumed after
  suspend»; относится к wrap-образу, не к нашему wakeup/tb. Если будем
  использовать wrap, версию стоит обновить.
- `rv1106_hpmcu_tb_sc230ai_v1.90` (2023-12-15) — наш текущий TB-образ;
  upstream-запись про fastae V2.3.2-rc1.

### Что взять из кода и скриптов dehai

Ссылки на файлы и номера строк ниже — из снимка до зачистки 2026-11-25:
сами `aov_service.c`, `network_service.c`, `detect_service.c`, `aov.md`,
`worklog` и `todolist` удалены, факты сохранены в этом разделе и в
«Проверенные факты».

- **Порядок `prepare_sleep`** (`src/aov/aov_service.c:350-390`): остановить
  preview → завершить event-запись → оставить motion armed (ring пополняется
  через VENC) → FE-PHY off → `SingleFrame` без media-loop / `MultiFrame` с
  media-loop → `storage_prepare_suspend`; при ошибке storage — восстановить
  сеть и уйти в FAULT.
- **PMU timeout** (`aov_service.c:332-348`): `io -4 0xff300048 ms*32`;
  `write_mem_suspend()` пишет ровно 3 байта `"mem"` в `/sys/power/state`
  (`:393-410`). `allow_mem_suspend=0` — dry-run без `mem` (`:431-436`).
- **FE-PHY** (`src/system/network_service.c:489-526`): `wakeup=disabled` →
  `ip link set eth0 down` → unbind `ffa80000.ethernet` из
  `rk_gmac-dwmac`; на пробуждении GMAC **остаётся unbound** на все циклы
  (`:528-536`), rebind + `wakeup=enabled` + link up только при выходе из AOV,
  DHCP отложен (`:538-575`).
- **SD/TF** (`src/system/storage_service.c:663-748`): `umount`, при busy —
  `umount -l`; найти `mmcblkXpN` через `/proc/mounts`/sysfs, unbind
  `dwmmc_rockchip`/`rockchip-dw-mshc`; на resume — bind и до 30 попыток
  `mount` с шагом 100 мс.
- **Медиа**:
  - JPEG combo (`src/media/media_service.c:430-495`): slave JPEG VENC
    привязывается к master H.264/H.265 через `VENC_SetComboAttr`, порядок
    `StartRecvFrame` → `SetComboAttr` → `StopRecvFrame`; `VI_GetChnFrame` не
    нужен.
  - `VENC_GetStream`-поток (`:674-734`) копирует пакет и вызывает listeners
    **до** `ReleaseStream`; поэтому listener обязан не блокироваться.
  - NV12 для NPU (`:1636-1759`): `GetHorStride(vir_width)`, RGA
    `importbuffer_fd`/`physical`, цель — `MmzAlloc_Cached`, `imcopy`,
    `MmzFlushCache(RK_FALSE)`, проверка средней Y.
  - `media_isp_single_frame`/`multi_frame` — это AIQ pause/resume
    (`:1814-1836`), а не VI stop/start.
- **Запись** (`src/record/record_service.c`): stage в
  `<records>/.stage`, финальное имя через `rename` (атомарно на TF)
  (`:232-282`); writer thread и очередь с drop при переполнении
  (`:301-364`, `:539-541`); ждать keyframe; пейсинг под `main_fps`, потому
  что `rkmuxer` штампует 1/fps независимо от PTS (`:505-556`); пустой
  prerecord не должен сбрасывать `waiting_key` (`:558-605`).
- **OSD** (`src/media/media_osd.c:163-183`): `pos_x/pos_y` выравниваются
  `& ~15`; комментарий ссылается на `rk_comm_rgn.h` (OVERLAY stPoint X/Y
  должны быть 16-aligned на VENC).
- **fMP4** (`src/web/fmp4_mux.c:499-586`): `trun` flags `0x00000701`
  (data-offset + duration + size + flags), key sample `0x02000000`,
  non-key `0x01010000`, `tfdt` v1, duration `90000/15`; 15 fps зашито.
- **IVS** (`src/detect/detect_service.c:306-380`): `IVS_MODE_MD_OD`,
  `MDInterval=5`, `MDNightMode`, `ODPercent=7`, пороги
  `ThreshSad 96/72/48/40/24` по чувствительности; bind VI2→IVS
  (`:383-391`).
- **RKNN** (`src/face/rknn_face.c:144-229`): `rknn_init` из файла,
  `RKNN_QUERY_NATIVE_OUTPUT_ATTR` + `rknn_create_mem`/`rknn_set_io_mem` для
  всех выходов, `size_with_stride`; модель `yunet_320.rknn`.
- **Watchdog** (`scripts/ipc_watchdog.sh`):
  `LD_LIBRARY_PATH=/oem/usr/lib:/oem/lib`, `flock /var/run/ipc_app.lock`,
  остановка конфликтующего `rkipc`, лог-ротация 32 КБ; `/userdata` — 2.2 МБ
  UBIFS.
- **AOV defaults** (`config/default_config.json:147-157`): `suspend_ms=1000`,
  `event_fps=25`, `quiet_sec=30`, `event_record_sec=10`,
  `max_cycles_before_reinit=40`, `down_eth=true`,
  `allow_mem_suspend=false`, `debug_motion_interval=4`.
- **Реальный wake reason**: `aov_poll_wake_reason()` возвращает
  `AOV_WAKE_MOTION` только при `debug_motion_interval`, иначе `UNKNOWN`
  (`aov_service.c:277-286`) — MCU `shared_data.md_flag` без MIS5001 FW не
  читается.

### Rockchip-Docs: что ещё пригодилось

- **`SOC_Internal_MCU_CN` (V1.0.0, 2024-05-27, «内部资料»).** Гайд описывает
  только открытый **BUS MCU**; PMU MCU и DDR MCU явно исключены как
  «特定功能专用的 MCU». Это ещё одно подтверждение, что LPMCU/HPMCU RV1106
  нельзя считать документированными по этому гайду. Полезное: AP и MCU часто
  делят один JTAG, переключаемый через GRF (пример RK3576:
  `SYS_GRF_SOC_CON7`, `GPIO2A_IOMUX_SEL_L`), но для RV1106 значения нужно
  брать из TRM, а не из примера.
- **Pinctrl (V1.0.0, 2022-05-10; документ покрывает RV1106 Linux 5.10,
  пример PD — RK3588).** GPIO0 — в `PD_PMU`, GPIO1–GPIO4 — в `PD_BUS`; SMT
  для SDA/SCL I2C улучшает сигнал; states
  `default/init/idle/sleep`; есть userland `iomux` для проверки/установки
  mux. Контроллеры GPIO1–4 в logic domain, но пады I2C3 дополнительно
  питаются `GPIO1_VCC3V3` — это разные вещи.
- **IO-Domain (V1.0.1, 2021-05-28).** `vsel` в GRF/PMUGRF: 1 = 1.8 В
  (1.62–1.98), 0 = 3.3 В (3.00–3.60); драйвер
  `drivers/power/avs/rockchip-io-domain.c` привязывает supply к regulator и
  обновляет `vsel` по notify. Для I2C3 проверить `GPIO1_VCC3V3` и
  соответствующий `vsel`, а не только IOMUX. Список SoC в документе старый
  (RV1106 там нет), поэтому это методика, а не RV1106-специфика.
- **Power Analysis (V1.0.1, 2021-04-29).** Методика: VD/PD, static/dynamic,
  последовательный резистор 0.01 Ом, многоканальный PowerMeterage (20
  каналов), обязательная запись температуры; пример `VD_LOGIC` (I2C, SPI,
  GMAC, USB, EMMC...) и `VD_PMU` (PMU, SRAM, GPIO, PVTM) — это пример RK3399,
  не RV1106, но структура подходит для таблицы AOV-потребления.
- **Camera Troubleshooting Linux 5.10 (V1.0.1, 2024-05-14;
  RV1106/RV1103).** Чеклист sensor I2C: 7-bit адрес, 24 МГц mclk, VDD и
  порядок питания; `i2ctransfer`/`media-ctl`; `select timeout` на MIPI/ISP;
  увеличение ISP frequency; fast boot; режимы sensor sleep/reset/standby и
  `rockchip,camera-module-stb`. Прямо применимо к OKO при отладке SC230AI и
  AOV-пайплайна.
- **RGA IM2D (V2.2.3, 2024-03-06).** Таблицы alignment/stride по версиям
  RGA; API `importbuffer_T`/`wrapbuffer_handle`/`imcopy`. Для RV1106
  проверить версию RGA и требования к NV12 stride; в коде dehai используется
  `GetHorStride` и MMZ-цель.

### SC230AI: что даёт X5 libcam

| Параметр | X5 `sc230ai` | OKO `sc230ai.c` |
|---|---|---|
| Linear init | 170 регистров; явно HTS `0x320c/d=0x0960`, MIPI `0x3018/19`, начальные gain `0x3e06/07/09`, sync-дефолты | 143 регистра; HTS/MIPI/начальные gain не пишутся, полагается на default/контролы |
| Sync master | `0x300a=0x24`, `0x3032=0xa0`, `0x3225=0x30`; edges `0x3216/17`, `0x322e/2f` | только `0x300a/0x3032/0x3222`; нет `0x3225` и программируемых edges |
| Sync slave | `0x300a=0x20`, `0x3032=0x20`, `0x3222=0x01`, `0x3230=0x00`, `0x3231=0x04`, `0x3224=0x82/0x92`, `0x320f=0x64` | `0x300a/0x3222/0x3224/0x3614`; нет `0x320f/0x3230/0x3231` |
| HDR | DOL2: `0x301f=0x29`, VTS `0x092e`, 2 lane 10-bit | SHDR/DCG: `0x301f=0x06`, VTS `0x08ca` |
| FPS | `vts=33750/fps`, `0x320e=(vts>>8)&0x7f`, `0x320f=(vts&0xff)-2` | V4L2 vblank/exposure |
| Gain | LUT + helper 1/2/3.1/6.2/12.4/24.8/49.6/99.2 | `sc230ai_get_gain_reg()` — та же лестница до ~1587.2 |
| Exposure | `lines_per_second=33750`, `VTS=1125`, max 1012 (10 мс) для 30 fps; DOL2 `2*VTS-8` | V4L2 max `vts-5` |
| Power | GPIO toggle + 100 мс | regulator/clock/reset |

Что взять:

- **Frame-sync master/slave.** В `sc230ai_setting.h:26-34` описан FSIN:
  `0x300a=0x24` (output), `0x3032=0xa0` (vsync_tc_en), `0x3225=0x30`
  (vsync_end_man_en), `0x3216/17` — rising edge, `0x322e/2f` — falling edge.
  При 30 fps строка ≈29.6 мкс; высокий уровень FSIN по умолчанию ≈118 мкс,
  `0x322f=0x03` → ≈148 мкс, `0x322f=0x04` → ≈178 мкс. В slave-таблице
  (`:214-398`) есть `0x320f=0x64` («синхронизировать внутреннюю частоту с
  trigger»), `0x3230=0x00`, `0x3231=0x04`, `0x3224=0x82` (efsync) / `0x92`
  (fsync).
- **OKO:** `sc230ai.c:389-410` master ставит только `0x300a/0x3032/0x3222`,
  slave — `0x300a/0x3222/0x3224/0x3614`. Если включать
  `RKMODULE_SET_SYNC_MODE`/`rockchip,camera-module-sync`, эти регистры —
  первый кандидат на стендовую проверку. Сейчас DTS sync-режим не задаёт,
  поэтому путь не активен.
- **Dynamic FPS.** `sc230ai_utility.c:549-591`: `vts=33750/fps`,
  `0x320e=(vts>>8)&0x7f`, `0x320f=(vts&0xff)-2`. Может быть проще V4L2
  vblank для 1/5/10 fps, но проверить взаимодействие с AOV/ISP/VENC.
- **Exposure/gain.** `sc230ai_utility.c:253-277` задаёт `lines_per_second`,
  VTS, max exposure 1012 (10 мс) и DOL2 long max. 10 мс — ограничение
  заказчика X5, не SC230AI; для AOV в темноте OKO может хотеть длиннее.
  `MaybeUsed` и `sc230ai_camera_helper.c:16-86` дают piecewise gain; OKO уже
  реализует ту же лестницу — полезно для сверки границ/округления.
- **DOL2 не смешивать с SHDR/DCG.** X5-таблица — DOL2 (`0x301f=0x29`,
  VTS `0x092e`), OKO — SHDR/DCG (`0x301f=0x06`, VTS `0x08ca`). Для DOL2
  нужна поддержка в rkisp/AIQ; просто подставить таблицу нельзя.
- **Init-таблицу** менять только после дампа регистров на стенде: X5 явно
  пишет HTS/MIPI/начальные gain, OKO полагается на default. Если появятся
  редкие сбои старта — сравнить `0x3018/19`, `0x320c/d`, `0x3e06/07/09`,
  `0x3614`.

### Не переносимо / ограничено

- **«HPMCU/LPMCU — те же, что в RK3566».** Общее — только ядро **SCR1
  (RV32IMC)**: это подтверждают и `luyi1888` («same RISC-V core exists in
  RK3568/RV1126/RV1106»), и RK3566-заметки. Интеграция другая: загрузка,
  адреса, mailbox, PMU и power domains. Для **LPMCU** TRM подтверждает только
  роль в software low-power procedure (p279 §7.6.2); ISA/ядро/debug не
  описаны, в issue #10 это лишь догадка («same as HPMCU at lower frequency or
  completely different»).
- **«MCU выключается в suspend» (RK3566).** К RV1106 это не переносится:
  вендорский AOV на RV1106 специально держит HPMCU живым, пока AP в `mem`, и
  передаёт ему ISP/sensor. OKO KI-072 воспроизводит ту же модель.
- **RK3566-примеры.** Адреса `0xFDC60000`, `0xFDCC0000`, `0xFE780000`,
  `GRF_SOC_CON4` — это RK3566; на RV1106 другие регистры (`CORE_SGRF+0x44`,
  `CORECRU+0x...`, `0xff076040`).
- **`mcu1` в вендорском FIT — не LPMCU.** В `rv1106.c`
  `spl_fit_standalone_release()` для `mcu0` делает reset/release, а для `mcu1`
  только пишет адрес — и **оба** пишут `CORE_SGRF_HPMCU_BOOT_ADDR`. То есть
  второй образ (wakeup) — тоже HPMCU, а не LPMCU.
- **Код dehai.** Нет `LICENSE` (по умолчанию all rights reserved) — копировать
  нельзя; кроме того, MIS5001 и PMU-only путь не доказывают поведение SC230AI.

### Про «периодический опрос периферии через HPMCU/LPMCU»

В доступных первоисточниках это **не подтверждено**:

- официальный AOV-гайд Rockchip (V1.0.4) не говорит об опросе: он требует
  вычитать остатки кадров, остановить субпоток, выгрузить SD/Ethernet/audio,
  затем отдать ISP MCU на один/несколько кадров и уйти в `mem`;
- вендорский AOV на RV1106 — **не** событийная схема: это цикл «сон →
  пробуждение по PMU-таймеру → один кадр → сон», MCU считает MD только в окне
  пробуждения и сам глушится (`stream.c:640-643`); «HPMCU владеет ISP во
  время сна и будит AP» — гипотеза, опровергнутая 2026-11-25 (KI-072);
- RK3566-заметки дают **обратный** пример: MCU выключен при suspend;
- LPMCU по регистрам (`PMUSGRF+0x20`, soft IRQ, reset-hold, `WFI_HALTED`)
  действительно живёт в PMU-домене; TRM p279 §7.6.2 прямо говорит, что LPMCU
  может выставлять PMU software power control registers для low-power
  procedure. Но это управление PMU, а не «опрос периферии»; прошивок LPMCU нет
  ни в `rkbin`, ни в board-конфигах OKO.

Поэтому «опрос периферии» — это **гипотеза для эксперимента**, а не требование
вендора. Проверять её надо отдельно от задачи владения ISP: сначала измерить,
живёт ли LPMCU в deep suspend (`WFI_HALTED`/`RST_OUT`), затем — минимальная
прошивка, которая дёргает GPIO/I2C и будит PMU. Проблему «AP не выходит из
suspend, потому что MCU ждёт ISP от AP» это не решает.

### Что это меняет для KI-072/KI-080

- dehai независимо подтверждает, что **AP-side deep suspend на Pico Max
  возможен**: после deep + FE-PHY off у них 12/12, затем 5/5 и 2000/2000
  циклов; `wake_ms≈3071` при `suspend_ms=3000` (PMU-таймер точный).
- Но их путь **не требует гасить AIQ**: без media они делают `SingleFrame`,
  а в media-режиме перед сном вызывают `MultiFrame`; по их логу media+motion
  soak 2000/2000 прошёл. Это расходится с наблюдением OKO (`keep_isp=1` даёт
  шторм `mipi-csi2-hw ERR1:0x10` и AP возвращается через ~20 мс). Скорее всего,
  разница в сенсоре/драйвере (MIS5001 vs SC230AI) — проверять, не принимать
  на веру.
- Для KI-080 (мёртвая I2C3) новые материалы дают конкретную диагностику:
  `ipd`/`state`, `RKI2C_DBGCTRL.nak_release_scl`, pull-up/rise time, отдельное
  питание падов `GPIO1_VCC3V3`; см. раздел «I2C3/H2» выше.
- Порядок проверок, который следует из материалов:
  1. прочитать `/sys/kernel/wakeup_reasons/last_resume_reason` после «60-с»
     цикла — сейчас OKO его не читает, а ядро собирает `wakeup_reason.o` при
     `CONFIG_SUSPEND`;
  2. отключать FE-PHY перед `mem` (wakeup off + `eth0 down` + unbind
     `rk_gmac-dwmac`) — в `divinus` этого нет;
  3. проверить источник часов PMU: PVTM (по умолчанию) vs
     `RKPM_SLP_32K_EXT` + `rockchip,rtc-suspend-bypass`;
  4. поднять HPMCU JTAG и снять реальные состояния MCU/PMU вместо догадок по
     UART;
  5. на debug-образе прогнать `mcutool`/свою прошивку в SRAM, чтобы отделить
     idblock/meta от логики пробуждения;
  6. только после этого — эксперимент с LPMCU.

**Итог 2026-11-24 по KI-080 (материалы темы не подтвердились как причина).**
Диагностика `ipd`/`state`/`nak_release_scl` показала, что мёртвая шина — это
не состояние контроллера: I2C3_H2 — конфликт пина в meta. Sensor-init SC230AI
объявлял `ircut_b` на GPIO1_D3 (= I2C3_M1 SCL), и MCU battery-ipc
(`fast_ae.c`, `ircut_switch()`) на каждом решении AE/день-ночь переводил пад
в GPIO-output-low; `i2cdetect` пуст, unbind/rebind не помогает. Проверено
детерминированно (`bench/aov_i2c_diag.py --mux-loss --restore-mux`) и
исправлено переносом `ircut_b` на GPIO1_D1 в
`project/make_meta/sensor_init/sc230ai_init.c`; V1 на NAND #37 — 14/14
пробуждений с живой шиной. Из snapshot темы для этого не понадобился ни один
файл (PDF остаются справкой). Кадентность KI-072 материалами темы на
2026-11-24 не закрывалась: окно deep suspend 2.26–2.27 с не зависит от
`wake_interval_s` (10/60), `last_resume_reason` пуст; 2026-11-25 она
закрыта своей правкой в divinus (счётчик PMU в тиках 32 кГц).

**Итог 2026-11-24 по KI-072/KI-079 (чеклист dehai подтвердился частично).**
Пункт чеклиста **«PMU ms×32»** (`dehai168…/docs/design/aov.md:65`, файл
удалён из снимка 2026-11-25, факт сохранён здесь:
`io -4 0xff300048 $((ms*32))`) оказался верным и **перенесён в divinus**:
счётчик `PMU_WAKEUP_TIMEOUT_CNT` тактируется 32 кГц (32768 тиков/с), а
прежняя формула `wake_interval_s × 32` давала 59 мс вместо 60 с. Замеры
стенда: 32768 → 1.07 с, 327680 → 10.07 с, 1920000 → 58.66 с; после правки
тихий цикл и AOV V1 держат интервал (`KNOWN_ISSUES.md`, KI-072). Пункт
**«FE-PHY off»** для OKO **не подтвердился**: unbind `rk_gmac-dwmac`
не меняет сон, а rebind не поднимает встроенный FE-PHY (`phy_poll_reset
failed`). Пункт **«last_resume_reason»** на этом ядре пуст, источник
пробуждения пришлось мерить через `last_suspend_time` и метки моста.
Медиа-чеклист дал для KI-079 только идею «окна устоятия» — реализовано
своей правкой в `ivs_events.c` (3 с, ≥ 900‰). Неприменённые выводы `aov.md`
(deep+compatible, LPMCU) изложены выше, PDF/гайды остаются справкой.

**Зачистка `snapshot/` 2026-11-25.** После переноса выводов удалены 7
отработавших файлов dehai (выводы применены или отвергнуты стендом):
`docs/design/aov.md`, `docs/07-todolist-aov.md`, `docs/04-worklog.md`,
`src/aov/aov_service.c`, `src/system/network_service.c`,
`src/detect/detect_service.c`, `scripts/test_aov_fephy_off_mem.sh`.
Уникальные факты этих файлов сведены в «Проверенные факты» → «Во внешних
источниках» и в разделы «Переносимо» и «Что взять из кода и скриптов
dehai». Оставлены PDF/TRM, материалы отложенного ISP-handover (JTAG,
mcutool, LPMCU), медиа-грабли и soak-инструментация. Повторное снятие
удалённых файлов не требуется; при необходимости — «Как обновить снимок».

## Проверенные факты

### В дереве OKO

- `sysdrv/source/kernel/arch/arm/boot/dts/rv1106g-luckfox-pico-pro-max-sc230ai.dts:19`
  — root compatible содержит `"rockchip,rv1106"` (условие deep suspend уже
  выполнено).
- `sysdrv/source/kernel/arch/arm/mach-rockchip/rv1106_pm.h:127,214` —
  `RV1106_CORESGRF_HPMCU_BOOTADDR 0x44`, `RV1106_HPMCU_BOOT_ADDR 0x40000`.
- `sysdrv/source/kernel/arch/arm/mach-rockchip/rv1106_pm.c:531-552`
  — `hpmcu_start()`: mailbox IRQ, `SYS_IS_WKUP`, cache-окно, reset/release,
  запись boot address в `CORE_SGRF+0x44`.
- `sysdrv/source/uboot/u-boot/arch/arm/mach-rockchip/rv1106/rv1106.c:548-566`
  — `spl_fit_standalone_release()`: `mcu0` — reset/release, `mcu1` — только
  адрес; оба пишут `CORE_SGRF_HPMCU_BOOT_ADDR`.
- `sysdrv/source/mcu/rt-thread/bsp/rockchip/common/hal/lib/CMSIS/Device/RV1106/Include/rv1106.h`
  - `:52` — `LPMCU_BOOT_ADDR` (offset `0x20` в `PMUSGRF`);
  - `:970-971` — `LPMCU_SOFT_IRQ` (bit 9 `PMUGRF_SOC_CON1`);
  - `:1028-1046` — `RESETN_LPMCU_*`/`TRESETN_LPMCU_*` и
    `LPMCU_WFI_HALTED`/`LPMCU_RST_OUT` в `PMUGRF_SOC_STATUS`;
  - `:1268-1269` — `LPMCU_HPROT_SECURE_CTRL`;
  - `:1290-1293` — поле `LPMCU_BOOT_ADDR`, маска 20 бит (`0xFFFFF`).
- `sysdrv/source/kernel/arch/arm/boot/dts/rv1106-pinctrl.dtsi:351-369`
  — `lpmcu` JTAG: m0 на GPIO1_B2/B3, m1 на GPIO3_A4/A5, func 4.
- `sysdrv/source/mcu/rt-thread/bsp/rockchip/rv1106-mcu/board/` — wakeup-борды
  только `rv1106_evb2_wakeup-SC200AI-ADC`, `rv1106_evb2_wakeup-SC3338-ADC` и
  OKO `oko-wakeup-sc230ai-ADC`; LPMCU-борда нет.
- `sysdrv/source/mcu/rt-thread/bsp/rockchip/rv1106-mcu/board/oko-wakeup-sc230ai-ADC/defconfig:57,59,341-342`
  — консоль UART2, `CONFIG_ARCH_RISCV=y`, UART включён для отладки handshake.
- `sysdrv/source/mcu/prebuilts/gcc/linux-x86/riscv64/xpack-riscv-none-embed-gcc-10.2.0-1.2/`
  — тот же toolchain, что рекомендует issue #99.
- `sysdrv/source/uboot/rkbin/bin/rv11/` — только `*hpmcu_*`
  (`hpmcu_wakeup_v1.00`, `hpmcu_tb_sc230ai_v1.90`, `hpmcu_wrap_v1.70`), ни
  одного `*lpmcu*`.
- `project/cfg/BoardConfig_IPC/BoardConfig-SPI_NAND-Buildroot-RV1106_Luckfox_Pico_Pro_Max-SC230AI-AOV.mk:106,116-117,150-155`
  — фрагменты ядра, `boot-aov.its`/`Image.gz`, `RK_UBOOT_RKBIN_MCU_CFG=oko-wakeup-sc230ai-ADC`,
  `RK_UBOOT_RKBIN_INI_OVERLAY=...HPMCU.ini`.
- `sysdrv/source/kernel/arch/arm/configs/rv1106-pmu-timeout.config:1-5`
  — `PM_SLEEP`, `SUSPEND`, `PM_WAKELOCKS`, `ROCKCHIP_SUSPEND_MODE`,
  `RV1106_PMU_WAKEUP_TIMEOUT`; `CONFIG_ROCKCHIP_GRF` здесь не задан.
- `sysdrv/source/kernel/arch/arm/configs/luckfox_rv1106_linux_defconfig:443`
  — `CONFIG_RK_CONSOLE_THREAD=y` (гайд Rockchip для AOV-таймингов советует
  выключить).
- `sysdrv/source/kernel/kernel/power/Makefile:20` — `wakeup_reason.o`
  собирается при `CONFIG_SUSPEND`, то есть `last_resume_reason` на AOV-образе
  должен быть доступен.
- `project/app/divinus/source/src/sleep_policy.c:713,731,843-847`
  — перед suspend только `sync()`, счётчик PMU `interval × hz`,
  `aov_keep_isp` включает сохранение CSI/ISP.
- `project/app/divinus/source/src/app_config.c:699` — `pmu_timeout_hz=32`.
- `project/app/divinus/source/src` — нет `ip link`/`eth0 down`/unbind
  `ffa80000.ethernet` (grep находит только MAC и `ifr_name`), то есть FE-PHY
  перед сном не отключается.

### Во внешних источниках

- `dehai168/rv1106-aov-ipc` (pin `07a2f55`):
  - deep suspend заработал только после добавления `"rockchip,rv1106"` в
    root compatible; до этого `/sys/power/mem_sleep` был только `s2idle`;
  - перед `mem` обязательны `wakeup=disabled`, `eth0 down` и unbind
    `rk_gmac-dwmac` (`network_prepare_aov_suspend()`); одного `ip link down`
    недостаточно;
  - PMU-таймер: `io -4 0xff300048 ms*32`; soak 12/12, затем 2000/2000,
    `wake_ms≈3071` при 3000 мс;
  - для MIS5001 нет wakeup-прошивки MCU: готовые board-конфиги только
    SC200AI/SC3338; MCU-детекция отложена;
  - `rmmod rockit` в рантайме вешает систему (refcnt −1);
  - `/userdata` у них ~2 МБ, YUV/записи только на TF.
  - Официальная схема AOV (из `docs/design/aov.md`, файл удалён из снимка
    2026-11-25; **сверена с руководством 2026-11-25 — правка ниже**):
    `SetSuspendTime(1000)` → ISP single frame → umount TF → `echo mem`;
    HPMCU-wakeup переживает цикл и по PMU-таймеру отдаёт ~1 fps lapse
    (`AOV_LAPSE`, `rkadk_aov.c`/`rkadk_muxer.c`), а MCU FastAeMd/mailbox
    переключает пайплайн на `MultiFrame` 20–30 fps.
    Trust-образ — `RV1106TOS_WAKEUP.ini` + `rv1106_hpmcu_wakeup_*.bin@0x40000`.
    MVP-состояния: `ALWAYS_ON ↔ AOV_IDLE → PREPARE_SLEEP → SLEEPING →
    POST_WAKE → LAPSE_1FPS|FULL_RECORD`, API `/api/v1/aov/*`.
  - **Уточнение по руководству V1.0.4 (2026-11-25), важно для OKO:**
    `snapshot/dehai168-rv1106-aov-ipc/docs/design/aov-guide-extract.txt`
    описывает AOV как **цикл «сон → пробуждение → один кадр → сон»**
    (`--suspend_time 1000` мс, стр. 15), а не как непрерывное владение ISP.
    Во сне «питание есть только у PMUIO, остальные IO обесточены» (`:197`),
    базовый сон ≈5 мВт (`:609`), 3V3_STB 0.05 мА (`:642`), весь цикл
    «1000 мс сна + 75 мс работы» = **39 мВт** (`:646`); сенсор в это время в
    standby либо обесточен (§3.2.3). Источники пробуждения — PMU-таймер,
    PIR/GPIO-кнопка, WiFi/4G (§1.1, §3.2.2), MCU-прерывания «по движению»
    в схеме нет: `battery-ipc/stream.c` в обеих ветках заканчивается
    `sysTick_stop(); while (1) asm("wfi")` (`:556-559`, `:640-643`), а
    «непрерывная» demo-борда `battery_ipc_demo_v10-SC230AI-ADC` — это
    не-AOV вариант (без `RT_USING_RK_AOV`). Вывод для OKO: «MCU держит
    ISP/SC230AI при мёртвом AP» не переносимо и не поддерживается железом
    (в PMU один общий `RV1106_PMU_POWER_OFF_ENA`, `rv1106_pm.c:880-887`);
    совместимая схема — `aov_mcu=1` + `keep_isp=1` + PMU-таймер (KI-072).
  - Их собственные результаты (`docs/07-todolist-aov.md`,
    `docs/04-worklog.md`; удалены 2026-11-25; MIS5001, на SC230AI не
    переносятся): `lapse_loop` 5/5, `wake_ms≈3071` при 3000 мс;
    media+motion soak2000 — `done ok=2000 fail=0`, `FULL_RECORD`×500,
    12 непустых MP4 по 190–240 кадров; periodic reinit 24/50 восстановился;
    отключение AOV возвращает Phase 1 (`--record 8` frames=230,
    `--detect 25` PASSED). Их firmware gap: для MIS5001 нет wakeup-прошивки
    MCU (только SC200AI/SC3338), RKADK по умолчанию `-DENABLE_AOV=OFF`;
    до `deep-compatible` + FE-PHY off `mem` не резюмился (TF-проба видела
    `before_mem*`, но не `RESUME_OK`).
- Luckfox `#10`:
  - документации на MCU Rockchip не даёт;
  - JTAG — IEEE 1149.1, двухпроводный SWD/cJTAG;
  - `LPMCU_BOOT_ADDR` находится в `PMU_SGRF`;
  - все известные MCU-бинарники Rockchip загружаются в HPMCU;
  - тип LPMCU не подтверждён.
- Luckfox `#99`:
  - `mcuload execute 0xff6c0000 ...` запускает blinky на RV1106 (и RV1103);
  - HPMCU JTAG: `devmem 0xff538008 32 0xff003300`, IDCODE `0xDEB11001`;
  - ARM JTAG: `0xff538008 = 0xff001100`, SWCLK/SWDIO на GPIO1_B2/B3;
  - в U-Boot: `mw.l 0xff076040 0xffff0002`;
  - toolchain — xPack `riscv-none-embed-gcc 10.2.0-1.2`;
  - HPMCU и ARM используют общую карту памяти;
  - `RK3566_AMP_SDK` содержит RV1106 MCU support;
  - позже — HAL с CRU-фиксом (`Sekilsgs2/external_hal`, ветка `CRU_FIX`).
- `luyi1888/rv1106-mcu`:
  - MCU — Syntacore SCR1; то же ядро в RK3568/RV1126/RV1106;
  - `mcutool` грузит FW в `0xff6c0000`, boot address — `0xff076044`,
    reset — `0xff3b8a04`, cache-окно — `0xff040024/0xff040028`.
- `liamhays/rk3566-mcu`:
  - SCR1: RV32IMC, 3 стадии, только machine mode, 16-линейный IPIC;
  - boot address — старшие 16 бит в `GRF_SOC_CON4`, код на границе 64 КБ;
  - `SYSTEM_SRAM` 64 КБ (`0xFDCC0000`), `PMU_SRAM` 8 КБ только secure,
    DDR — только данные;
  - таймер `0xFE7F0000`, INTMUX `0xFE790000`, mailbox `0xFE780000`;
  - PMU: `wakeup_mcu_sft`/`wakeup_mcu_sft_en`/`wakeup_sys_int_st`;
  - **на RK3566 MCU выключается при suspend** из-за TF-A bus idle;
  - сборка: xPack, `-march=rv32imc -mabi=ilp32 -misa-spec=2.2`,
    `-static -nostdlib`, не использовать `-linux` toolchain.

### TRM, даташит, rkbin и I2C guide

- RV1106 TRM V0.3 Part1 (2022-03-24, 877 с.):
  - p258 — PMU: `VD_CORE`, `VD_LOGIC`, `VD_PMU`; clamp `VD_PMU` input before
    power off `VD_LOGIC`; low-frequency clock PVTM/RTC; LPMCU can write
    `PMU_BIU_IDLE_SFTCON`.
  - p261–262 — `PMU_PWR_CON`/FSM; p266 — `PMU_WAKEUP_TIMEOUT_CNT` offset
    `0x48`, reset `0x5DC0`.
  - p274–275 — `PMU_CRU_PWR_CON0`: `power_off_ena`, `input_clamp_ena`,
    `osc_dis_ena`, `alive_32k_ena`.
  - p279 — `PMU_SYS_REG0–7` always on; §7.6.2 — LPMCU can execute software
    low-power procedure.
  - p281–298 — HP_MCU: RV32I+M+C, 3-stage, 16 КБ cache, INTMUX; boot via
    `SCR1_BOOT_ADDR` in SGRF, trap `0x001001C0`; JTAG m0 GPIO1_B2/B3,
    m1 GPIO3_A6/A7, `CORE_SGRF CPU_CTRL_CON[1]`.
  - p444–457 — I2C0–4, 400 kbps, clock stretching, glitch filter;
    `CLKDIV` formula; `RKI2C_DBGCTRL` `nak_release_scl`/`h0_check_scl`/
    `flt_en`; I2C3 M1 on GPIO1_D3/D2, `VENCIOC_GPIO1D_IOMUX_SEL_L`.
- RV1106 Datasheet Rev 1.8 (2025-03-12, 25 с.):
  - p6 — 8 КБ PMU SRAM;
  - p7 — 3 домена `VDD_ARM`/`VDD_LOGIC`/`VDD_PMU`;
  - p17 — I2C3 M1 GPIO1_D3/D2 (pins 85/86), `GPIO1_VCC3V3` pin 81;
    HPMCU/LPMCU JTAG on GPIO1_B2/B3 и GPIO3_A4–A7;
  - p20 — `DVDD_i` (LOGIC) 0.81–0.95 В; `GPIO1_VCC3V3`/`PMU_VCC3V3` 3.3 В.
- RV1106 Datasheet Rev 2.0 (2026-04-02, 31 с.):
  - §2.1 (p16) — order information: `RV1106G2`/`RV1106G3` — QFN128,
    `RV1106G3` = 1.0 TOPS NPU + 2Gb DDR3L + 8 МП@15, `RV1106G2` = 1.0 TOPS
    NPU + 1Gb DDR3L + 5 МП@30, `RV1106` — BGA313 без SIP-памяти;
  - §2.2 (p16) — top marking: `RVXXXX` chip name, subcontractor code,
    die lot, date code; отдельные POD-рисунки G2/G3 и RV1106;
  - §1.2.2 — встроенная память `RV1106G3` (DDR3L SIP) описана явно;
  - revision history (p2) — 2.0 от 2026-04-02, среди правок «Correct NPU
    performance as 1 TOPS» (в Rev 1.8 у G2 стояло 0.5 TOPS).
- rkbin release notes (pin `3e288fe8…`): wakeup v1.00, DDR v1.15 timer for
  suspend/resume, DDR v1.13 sleep/resume, wrap v2.03rc4 AVS resume fix,
  tb_sc230ai v1.90. Локальный `rkbin` имеет DDR v1.15/wakeup v1.00/tb v1.90.
- I2C guide V2.2.0 (2021-12-29, 14 с.): p2 — pull-ups/rise/fall;
  §6.1.2.3 (p12) — `ipd=0x00/0x10/0x80`; §6.3 (p14) — `state=1/2/3`;
  §2.1 — rise/fall DTS.
- linux-rockchip 029415 (2022-02-10): SPI noirq patch; rationale — wakeup
  handler must not access a suspended bus.

### X5 SC230AI

- `sc230ai/inc/sc230ai_setting.h:26-34` — FSIN master: `0x300a=0x24`,
  `0x3032=0xa0`, `0x3225=0x30`, edges `0x3216/17` и `0x322e/2f`; pulse
  118/148/178 мкс.
- `:214-398` — slave: `0x320f=0x64`, `0x3230=0x00`, `0x3231=0x04`,
  `0x3224=0x82/0x92`.
- `:402-700` — DOL2 2-lane 10-bit 1920×1080 30 fps (`0x301f=0x29`,
  VTS `0x092e`).
- `sc230ai_utility.c:253-277` — `lines_per_second=33750`, VTS 1125,
  exposure max 1012; `:549-591` — dynamic FPS.
- `MaybeUsed` и `sc230ai_v4l2/sc230ai_camera_helper.c:16-86` — gain LUT и
  формулы.
- OKO: `sc230ai.c:389-410` — sync master/slave; `:417-568` — linear;
  `:568-743` — SHDR/DCG HDR; `:892-945` — gain.

## Ссылки

- AOV-проект: https://github.com/dehai168/rv1106-aov-ipc
- Обзор AOV-гайда Rockchip (PDF + текстовый экстракт):
  https://github.com/dehai168/rv1106-aov-ipc/tree/main/docs
  (`docs/Rockchip_RV1106_Developer_Guide_Linux_AOV_CN.pdf`,
  `docs/design/aov-guide-extract.txt`)
- Luckfox #10: https://github.com/LuckfoxTECH/luckfox-pico/issues/10
- Luckfox #99: https://github.com/LuckfoxTECH/luckfox-pico/issues/99
- Luckfox #112 (JTAG): https://github.com/LuckfoxTECH/luckfox-pico/issues/112
- `luyi1888/rv1106-mcu`: https://github.com/luyi1888/rv1106-mcu
- `luyi1888/openocd` (target/interface configs):
  https://github.com/luyi1888/openocd
- `liamhays/rk3566-mcu`: https://github.com/liamhays/rk3566-mcu
- `D-Robotics/x5-libcam-sensor`:
  https://github.com/D-Robotics/x5-libcam-sensor
- `Lanxin-MRDVS/CameraSDK`:
  https://github.com/Lanxin-MRDVS/CameraSDK
- RK3568 TRM (основа RK3566-заметок):
  https://opensource.rock-chips.com/images/2/26/Rockchip_RK3568_TRM_Part1_V1.3-20220930P.PDF
- Rockchip `rkbin` (HPMCU-бинарники):
  https://github.com/rockchip-linux/rkbin/tree/master/bin/rv11
- Сторонний HAL с CRU-фиксом (упомянут в #99):
  https://github.com/Sekilsgs2/external_hal/tree/CRU_FIX
- RV1106 TRM V0.3 Part1:
  https://rockchip.fr/Rockchip%20RV1106%20TRM%20V0.3%20Part1.pdf
- RV1106 Datasheet Rev 1.8 (зеркало):
  https://atta.szlcsc.com/upload/public/pdf/source/20250728/0F25565987B346BAFAE15AD3F3BED3E9.pdf
- RV1106 Datasheet Rev 2.0:
  https://rockchip.fr/RV1106%20datasheet%20V2.0.pdf
- rkbin RV1106 release notes:
  https://github.com/rockchip-linux/rkbin/blob/master/doc/release/RV1106_EN.md
- Rockchip I2C Developer Guide V2.2.0 (original; 403 на 2026-11-24):
  http://pro934ad4.pic3.ysjianzhan.cn/upload/Rockchip_Developer_Guide_I2C_EN.pdf
  Локальная копия: `/home/spectre/Rockchip_Developer_Guide_I2C_EN.pdf`
  (sha256 `c3f463…`, идентична зеркалу); снимок —
  `snapshot/rockchip-i2c-guide/Rockchip_Developer_Guide_I2C_EN.pdf`.
  Зеркало:
  https://github.com/as-jackson/Rockchip-Docs/blob/main/en/Common/I2C/Rockchip_Developer_Guide_I2C_EN.pdf
- linux-rockchip 029415:
  https://lists.infradead.org/pipermail/linux-rockchip/2022-February/029415.html
- Зеркало документации Rockchip:
  https://github.com/as-jackson/Rockchip-Docs
- `SOC_Internal_MCU_CN` (internal, не копировался):
  https://github.com/as-jackson/Rockchip-Docs/blob/main/cn/Common/MCU/Rockchip_Developer_Guide_SOC_Internal_MCU_CN.pdf

## Провенанс снимков

Снимок: 38 файлов, 39 799 861 Б. Лимита на число файлов и размер нет.
Дата снятия — 2026-11-24 (Rev 2.0 даташита добавлен 2026-11-25); зачистка
отработавших файлов — 2026-11-25
(удалено 7 файлов dehai, 107 191 Б; повторное снятие — «Как обновить
снимок»). Файлы не редактировались.

| Upstream path | Локальный путь | Лицензия / SPDX | sha256 | Размер | Дата | Pin |
|---|---|---|---|---|---|---|
| `rv1106-mcu/README.md` | `snapshot/luyi1888-rv1106-mcu/README.md` | GPL-3.0 (repo `LICENSE`) | `52d7f9599279eff37d5d8f95301fd0b343a27d47131219131c9082c295abf07b` | 621 | 2026-11-24 | `a72209af76abc32f5d88e1292cbb023a6b0ada9b` |
| `rv1106-mcu/mcutool.c` | `snapshot/luyi1888-rv1106-mcu/mcutool.c` | GPL-3.0 (repo `LICENSE`) | `13ad0b276f66f32c49ca2e512025a81d0788c01f35b5d5e0b25f1c9edca86cb8` | 4213 | 2026-11-24 | `a72209af76abc32f5d88e1292cbb023a6b0ada9b` |
| `openocd/tcl/target/rv1106-mcu.cfg` | `snapshot/luyi1888-openocd/tcl/target/rv1106-mcu.cfg` | GPL-2.0-or-later (SPDX) | `e05011574f2cfde72db051abee13ff74972d8c1bdb6b276391f7cef4b3c567b0` | 606 | 2026-11-24 | `0678540e99f708e06d09b34f23f9d1acaf4530c6` |
| `openocd/tcl/interface/dirtyjtag-cjtag.cfg` | `snapshot/luyi1888-openocd/tcl/interface/dirtyjtag-cjtag.cfg` | GPL-2.0-or-later (SPDX) | `ce9c0e26e98dbbcd8fae59a768b8faceec7bb2f5591e998a0217254293e18b6b` | 202 | 2026-11-24 | `0678540e99f708e06d09b34f23f9d1acaf4530c6` |
| `rk3566-mcu/rk3566_mcu_notes.md` | `snapshot/liamhays-rk3566-mcu/rk3566_mcu_notes.md` | GPL-3.0 (repo `LICENSE`) | `4619da10f29113d36cf9d13c86416505c07bf9b98891221db4b9c42eedf0f265` | 37422 | 2026-11-24 | `16e975a9a405dc4d79ab510bdbf37a1d5bb1a060` |
| `rk3566-mcu/examples/mcu_wakeup/mcu_wakeup.c` | `snapshot/liamhays-rk3566-mcu/examples/mcu_wakeup/mcu_wakeup.c` | GPL-3.0 (repo `LICENSE`) | `7daf55de3986611f05ff2330405be1a4b7fae056bffd841c82190ce040b7093c` | 7046 | 2026-11-24 | `16e975a9a405dc4d79ab510bdbf37a1d5bb1a060` |
| `rk3566-mcu/examples/hello_mailbox/hello_mailbox.c` | `snapshot/liamhays-rk3566-mcu/examples/hello_mailbox/hello_mailbox.c` | GPL-3.0 (repo `LICENSE`) | `ccdcf4049e1b50d46df8392da38bff82e98794cfd1f44cf1b88da77cbc16b79c` | 4205 | 2026-11-24 | `16e975a9a405dc4d79ab510bdbf37a1d5bb1a060` |
| `rk3566-mcu/rk3566_mbox.dts` | `snapshot/liamhays-rk3566-mcu/rk3566_mbox.dts` | GPL-2.0 (Rockchip BSP, per header); repo GPL-3.0 | `8142d059872b0dce3876beb25a8e133a4e17349fa74ef2d476f75003f6178cd7` | 735 | 2026-11-24 | `16e975a9a405dc4d79ab510bdbf37a1d5bb1a060` |
| `rockchip-rv1106-trm/Rockchip_RV1106_TRM_V0.3_Part1.pdf` | `snapshot/rockchip-rv1106-trm/Rockchip_RV1106_TRM_V0.3_Part1.pdf` | Rockchip doc; all rights reserved (research copy) | `dc0121f7b67611af35c84b7a49e4a0c33585999059ad5a9bbf5b331c15ac9184` | 24 621 576 | 2026-11-24 | V0.3, 2022-03-24, 877 с.; static PDF |
| `rockchip-rv1106-datasheet/RV1106_Datasheet_Rev1.8.pdf` | `snapshot/rockchip-rv1106-datasheet/RV1106_Datasheet_Rev1.8.pdf` | Rockchip datasheet; all rights reserved (research copy) | `2cac8eded045e404dc67d4d4662684cbef48ffb1737e3533f52e798e5ba4a017` | 1 919 244 | 2026-11-24 | Rev 1.8, 2025-03-12, 25 с.; static PDF |
| `rockchip-rv1106-datasheet/RV1106_Datasheet_Rev2.0.pdf` | `snapshot/rockchip-rv1106-datasheet/RV1106_Datasheet_Rev2.0.pdf` | Rockchip datasheet; all rights reserved (research copy) | `53476c5ad66bc0cb8da5d7c5c4e72cd6f3365c9ad3209a36855a523c7b453284` | 2 382 782 | 2026-11-25 | Rev 2.0, 2026-04-02, 31 с.; static PDF |
| `rockchip-i2c-guide/Rockchip_Developer_Guide_I2C_EN.pdf` | `snapshot/rockchip-i2c-guide/Rockchip_Developer_Guide_I2C_EN.pdf` | Rockchip public doc; all rights reserved (research copy) | `c3f463921e446e7afd70b87f9a77dd9fc32f3568728c3ecf5a42f1a7981a98cb` | 315 278 | 2026-11-24 | local = mirror `a1717bfc5f2412d7b1c06633debb896154699807`; original URL 403 |
| `rockchip-linux-rkbin/doc/release/RV1106_EN.md` | `snapshot/rockchip-linux-rkbin/doc/release/RV1106_EN.md` | Rockchip release notes (repo) | `cde13f19c93dce5a15dd806582f494a1f3f22f3dcfde76476fcdbf856c6f6b36` | 43 323 | 2026-11-24 | `3e288fe814e059dd06833495f845cab04ac20a5c` |
| `linux-rockchip/2022-February/029415.html` | `snapshot/linux-rockchip/2022-February/029415.html` | public mailing list; kernel patch GPL-2.0-or-later | `686d5cbdd6ee292d6dde8a9dea330cdb96c1999d9a4c4bf6ecd3d8db15387080` | 5 043 | 2026-11-24 | 2022-02-10, no pin |

### dehai168/rv1106-aov-ipc (pin `07a2f553…`)

Репозиторий без `LICENSE`; после зачистки 2026-11-25 осталось 15 выбранных файлов, они включены по решению владельца
проекта как read-only research copy, не для переиспользования кода. Полный
репозиторий не копировался: UI, `www/`, `third_party/`, `models/`, `assets/`
и остальные файлы остались в локальной копии `../rv1106-aov-ipc`.

| Upstream path | Локальный путь | Лицензия | sha256 | Размер | Дата | Pin |
|---|---|---|---|---|---|---|
| `README.md` | `snapshot/dehai168-rv1106-aov-ipc/README.md` | no LICENSE (all rights reserved; owner-approved research copy) | `6ada9d1a3eb75c6a740eecaf101f5a76d2f7d6bc831e075e767c1fd239f4d213` | 3514 | 2026-11-24 | `07a2f553bc83eae8ccb3fa7a805403da6aef6dd7` |
| `docs/design/aov-probe-notes.md` | `snapshot/dehai168-rv1106-aov-ipc/docs/design/aov-probe-notes.md` | no LICENSE (all rights reserved; owner-approved research copy) | `bd2ec08787a23655f4e29b54eb835e36b17d2d61614b4c135bce7d7717f0cfd7` | 4377 | 2026-11-24 | `07a2f553bc83eae8ccb3fa7a805403da6aef6dd7` |
| `docs/design/aov-resume-blockers.md` | `snapshot/dehai168-rv1106-aov-ipc/docs/design/aov-resume-blockers.md` | no LICENSE (all rights reserved; owner-approved research copy) | `b1a663704c16fd2dc89ea57836f93005c0dded260a7f086c1520ef347e6e5d5c` | 3810 | 2026-11-24 | `07a2f553bc83eae8ccb3fa7a805403da6aef6dd7` |
| `docs/design/aov-hpmcu-wakeup-plan.md` | `snapshot/dehai168-rv1106-aov-ipc/docs/design/aov-hpmcu-wakeup-plan.md` | no LICENSE (all rights reserved; owner-approved research copy) | `438b11bd880d0cbbefbfc6de842fa3e6214ccfab9304c50bfd96f316b73e4fb1` | 3584 | 2026-11-24 | `07a2f553bc83eae8ccb3fa7a805403da6aef6dd7` |
| `docs/design/aov-guide-extract.txt` | `snapshot/dehai168-rv1106-aov-ipc/docs/design/aov-guide-extract.txt` | no LICENSE (all rights reserved; owner-approved research copy) | `d25de2ea56e5ecd9b8f785f7b3e43a9335b5578bbe36900b2a2d0dfb94d927bd` | 30853 | 2026-11-24 | `07a2f553bc83eae8ccb3fa7a805403da6aef6dd7` |
| `docs/Rockchip_RV1106_Developer_Guide_Linux_AOV_CN.pdf` | `snapshot/dehai168-rv1106-aov-ipc/docs/Rockchip_RV1106_Developer_Guide_Linux_AOV_CN.pdf` | Rockchip doc, no LICENSE in repo (owner-approved research copy) | `e51a74d6e8fc90684d9148e5dd6732f61eae04bbf584e50613a83c6705c0da57` | 7 377 217 | 2026-11-24 | `07a2f553bc83eae8ccb3fa7a805403da6aef6dd7` |
| `config/default_config.json` | `snapshot/dehai168-rv1106-aov-ipc/config/default_config.json` | no LICENSE (all rights reserved; owner-approved research copy) | `292e0230eeb53dc305587f11db3f63b3d04a9da6434941fb6bfa1472ec5e503a` | 4139 | 2026-11-24 | `07a2f553bc83eae8ccb3fa7a805403da6aef6dd7` |
| `src/system/storage_service.c` | `snapshot/dehai168-rv1106-aov-ipc/src/system/storage_service.c` | no LICENSE (all rights reserved; owner-approved research copy) | `1db825e13f8332900db86aa611aee45b8460d8bc3ae7c89a2ef4a8c25d371f66` | 21252 | 2026-11-24 | `07a2f553bc83eae8ccb3fa7a805403da6aef6dd7` |
| `src/media/media_service.c` | `snapshot/dehai168-rv1106-aov-ipc/src/media/media_service.c` | no LICENSE (all rights reserved; owner-approved research copy) | `e4c2d40354f670b1febec54774c76ea84e278443bb78a9de441f5d0017ab67e1` | 52322 | 2026-11-24 | `07a2f553bc83eae8ccb3fa7a805403da6aef6dd7` |
| `src/media/media_osd.c` | `snapshot/dehai168-rv1106-aov-ipc/src/media/media_osd.c` | no LICENSE (all rights reserved; owner-approved research copy) | `ef874308fc0845e78ee15ef2053b4de974354b8b3a3ffc17b9a2947a738d2230` | 21070 | 2026-11-24 | `07a2f553bc83eae8ccb3fa7a805403da6aef6dd7` |
| `src/record/record_service.c` | `snapshot/dehai168-rv1106-aov-ipc/src/record/record_service.c` | no LICENSE (all rights reserved; owner-approved research copy) | `b3124f574e20a77d4d91086bcacc519e2aa76503d94fc6cab67934bad42c94d9` | 29492 | 2026-11-24 | `07a2f553bc83eae8ccb3fa7a805403da6aef6dd7` |
| `src/web/fmp4_mux.c` | `snapshot/dehai168-rv1106-aov-ipc/src/web/fmp4_mux.c` | no LICENSE (all rights reserved; owner-approved research copy) | `3ce0f3bafb936ee31a68c2a4cfbe6527bb61d86406103e6df9a6490c96d53c32` | 12874 | 2026-11-24 | `07a2f553bc83eae8ccb3fa7a805403da6aef6dd7` |
| `src/face/rknn_face.c` | `snapshot/dehai168-rv1106-aov-ipc/src/face/rknn_face.c` | no LICENSE (all rights reserved; owner-approved research copy) | `237759b70797c20592c2583a5fc127d21488cbc7bd6dc3bc42bb1cae80af7c14` | 10414 | 2026-11-24 | `07a2f553bc83eae8ccb3fa7a805403da6aef6dd7` |
| `scripts/run_aov_soak.sh` | `snapshot/dehai168-rv1106-aov-ipc/scripts/run_aov_soak.sh` | no LICENSE (all rights reserved; owner-approved research copy) | `b6422a1109252353bf958be58ce89aedb01979f6b828b0e4a765d46a80040ffe` | 975 | 2026-11-24 | `07a2f553bc83eae8ccb3fa7a805403da6aef6dd7` |
| `scripts/ipc_watchdog.sh` | `snapshot/dehai168-rv1106-aov-ipc/scripts/ipc_watchdog.sh` | no LICENSE (all rights reserved; owner-approved research copy) | `80e8ee9226b2ca0cf8c3f36b0d38f99614525d399431795e1331bf202eebdc79` | 1677 | 2026-11-24 | `07a2f553bc83eae8ccb3fa7a805403da6aef6dd7` |

### as-jackson/Rockchip-Docs (pin `a1717bfc…`)

5 public-документов Rockchip. `SOC_Internal_MCU_CN` не копировался: помечен
«内部资料»; его факты пересказаны в разделе выше.

| Upstream path | Локальный путь | Лицензия | sha256 | Размер | Дата | Pin |
|---|---|---|---|---|---|---|
| `Common/IO-DOMAIN/Rockchip_Developer_Guide_Linux_IO_DOMAIN_CN.pdf` | `snapshot/rockchip-docs/Common/IO-DOMAIN/Rockchip_Developer_Guide_Linux_IO_DOMAIN_CN.pdf` | Rockchip public doc; all rights reserved (research copy) | `18181dd8abc73802b4518d886ab870f571e4a5bb3c40fcc05079c48c720c0a87` | 687 874 | 2026-11-24 | `a1717bfc5f2412d7b1c06633debb896154699807` |
| `Common/PINCTRL/Rockchip_Developer_Guide_Linux_Pinctrl_EN.pdf` | `snapshot/rockchip-docs/Common/PINCTRL/Rockchip_Developer_Guide_Linux_Pinctrl_EN.pdf` | Rockchip public doc; all rights reserved (research copy) | `538f09c11e85cae62cd80c41260f6373f76cdd0597f8815f650b5dead36f0335` | 104 099 | 2026-11-24 | `a1717bfc5f2412d7b1c06633debb896154699807` |
| `Common/POWER/Rockchip_Developer_Guide_Power_Analysis_EN.pdf` | `snapshot/rockchip-docs/Common/POWER/Rockchip_Developer_Guide_Power_Analysis_EN.pdf` | Rockchip public doc; all rights reserved (research copy) | `0e38ef1ccd4c04aa33050ec2d9591d278856938835457ca57b6d78b2215aa1ff` | 1 139 838 | 2026-11-24 | `a1717bfc5f2412d7b1c06633debb896154699807` |
| `Common/RGA/Rockchip_Developer_Guide_RGA_EN.pdf` | `snapshot/rockchip-docs/Common/RGA/Rockchip_Developer_Guide_RGA_EN.pdf` | Rockchip public doc; all rights reserved (research copy) | `4b5845e3093bfc4d2611352307ac14a73c9d3dd5ff69fc3c5f2443c5d3fd0999` | 494 388 | 2026-11-24 | `a1717bfc5f2412d7b1c06633debb896154699807` |
| `Linux/Camera/Rockchip_Trouble_Shooting_Linux5.10_Camera_EN.pdf` | `snapshot/rockchip-docs/Linux/Camera/Rockchip_Trouble_Shooting_Linux5.10_Camera_EN.pdf` | Rockchip public doc; all rights reserved (research copy) | `222405ebd79df4045ddededd8bcf56d1be81225f15eb43984ef7b7a72275b355` | 403 306 | 2026-11-24 | `a1717bfc5f2412d7b1c06633debb896154699807` |

### D-Robotics/x5-libcam-sensor (pin `8c41592…`)

Код проприетарный (Horizon, all rights reserved, без `LICENSE`); 4 файла
включены по решению владельца как read-only research copy. `Lanxin CameraSDK`
не копировался: в нём нет SC230AI/RV1106-кода.

| Upstream path | Локальный путь | Лицензия | sha256 | Размер | Дата | Pin |
|---|---|---|---|---|---|---|
| `sc230ai/inc/sc230ai_setting.h` | `snapshot/drobotics-x5-libcam-sensor/sc230ai/inc/sc230ai_setting.h` | no LICENSE (Horizon all rights reserved; owner-approved research copy) | `d4eabdc27c308e40620aa1c28b45da43e5b511df72947789c921a1468e7e0eae` | 18 681 | 2026-11-24 | `8c4159238e5a0db768d273853e4dce0039522cba` |
| `sc230ai/sc230ai_utility.c` | `snapshot/drobotics-x5-libcam-sensor/sc230ai/sc230ai_utility.c` | no LICENSE (Horizon all rights reserved; owner-approved research copy) | `3ad03e6920c5dc5d440e20d907e6dfb9b59cb7ba4e127bec71da174505ee8c74` | 25 721 | 2026-11-24 | `8c4159238e5a0db768d273853e4dce0039522cba` |
| `sc230ai/MaybeUsed` | `snapshot/drobotics-x5-libcam-sensor/sc230ai/MaybeUsed` | no LICENSE (Horizon all rights reserved; owner-approved research copy) | `061183ab36e7b67e92095aa62bfeba939d1f37b7b8643eaf8f7a9980c275d3ee` | 3 753 | 2026-11-24 | `8c4159238e5a0db768d273853e4dce0039522cba` |
| `sc230ai_v4l2/sc230ai_camera_helper.c` | `snapshot/drobotics-x5-libcam-sensor/sc230ai_v4l2/sc230ai_camera_helper.c` | no LICENSE (Horizon all rights reserved; owner-approved research copy) | `3da75b352b026c6415dcea276d812d0febece0059c8cec5d36dc0dd9e091e408` | 2 335 | 2026-11-24 | `8c4159238e5a0db768d273853e4dce0039522cba` |

Не снято:

- `dehai168/rv1106-aov-ipc` — остальные файлы репозитория (UI, `www/`,
  `third_party/`, `models/`, `assets/` и не выбранные docs/src) не
  копировались; 15 оставшихся файлов — в таблице выше (7 отработавших удалены 2026-11-25). У репозитория нет
  `LICENSE`; снимок включён по решению владельца проекта.
- `Rockchip_RV1106_Developer_Guide_Linux_AOV_CN.pdf` — включён в снимок dehai
  (таблица выше) вместе с текстовым экстрактом.
- `cn/Common/MCU/Rockchip_Developer_Guide_SOC_Internal_MCU_CN.pdf` — помечен
  «内部资料»; не копировался. Факты: гайд покрывает только открытый BUS MCU,
  PMU/DDR MCU исключены; AP/MCU JTAG переключается через GRF (пример RK3576,
  не RV1106).
- `Lanxin-MRDVS/CameraSDK` — не копировался: в дереве нет SC230AI/RV1106-кода,
  только проприетарные библиотеки/API для RGBD/dToF-камер Lanxin.
- Остальные файлы X5 (`camera_sensor_*`, другие сенсоры, Makefile) не
  копировались; выбраны 4 файла SC230AI.
- `luyi1888/rv1106-mcu/rv1106-mcu-led-blinky.bin` — бинарник, не текст; не
  входит в снимок, ссылка и команда запуска выше.

### Крупные источники

TRM, datasheet, I2C guide, AOV guide PDF и 5 документов Rockchip-Docs —
проприетарные документы Rockchip; они включены в `snapshot/` как read-only
research copy. `rkbin` release notes и письмо linux-rockchip — обычные
текстовые снимки. Остальные файлы dehai, X5 и Lanxin — см. «Не снято».

## Как обновить снимок

```bash
git clone --depth 1 https://github.com/luyi1888/rv1106-mcu.git /tmp/kilo/research-src/rv1106-mcu
git clone --depth 1 https://github.com/liamhays/rk3566-mcu.git /tmp/kilo/research-src/rk3566-mcu
git clone --depth 1 https://github.com/luyi1888/openocd.git /tmp/kilo/research-src/openocd
git clone https://github.com/dehai168/rv1106-aov-ipc.git /tmp/kilo/research-src/rv1106-aov-ipc
git clone --depth 1 https://github.com/as-jackson/Rockchip-Docs.git /tmp/kilo/research-src/Rockchip-Docs
git clone --depth 1 https://github.com/D-Robotics/x5-libcam-sensor.git /tmp/kilo/research-src/x5-libcam-sensor
git -C /tmp/kilo/research-src/rv1106-mcu rev-parse HEAD
git -C /tmp/kilo/research-src/rk3566-mcu rev-parse HEAD
git -C /tmp/kilo/research-src/openocd rev-parse HEAD
git -C /tmp/kilo/research-src/rv1106-aov-ipc rev-parse HEAD
git -C /tmp/kilo/research-src/Rockchip-Docs rev-parse HEAD
git -C /tmp/kilo/research-src/x5-libcam-sensor rev-parse HEAD
```

Скачивание и проверка крупных источников:

```bash
curl -sS -L -o /tmp/kilo/rv1106-src/rv1106-trm-v0.3-part1.pdf 'https://rockchip.fr/Rockchip%20RV1106%20TRM%20V0.3%20Part1.pdf'
curl -sS -L -o /tmp/kilo/rv1106-src/atta.pdf 'https://atta.szlcsc.com/upload/public/pdf/source/20250728/0F25565987B346BAFAE15AD3F3BED3E9.pdf'
curl -sS -L -o /tmp/kilo/rv1106-src/RV1106_Datasheet_Rev2.0.pdf 'https://rockchip.fr/RV1106%20datasheet%20V2.0.pdf'
curl -sS -L -o /tmp/kilo/rv1106-src/rkbin-RV1106_EN.md 'https://raw.githubusercontent.com/rockchip-linux/rkbin/master/doc/release/RV1106_EN.md'
curl -sS -L -o /tmp/kilo/rv1106-src/rockchip-i2c-guide-en.pdf 'https://raw.githubusercontent.com/as-jackson/Rockchip-Docs/main/en/Common/I2C/Rockchip_Developer_Guide_I2C_EN.pdf'
curl -sS -L -o /tmp/kilo/rv1106-src/linux-rockchip-029415.html 'https://lists.infradead.org/pipermail/linux-rockchip/2022-February/029415.html'
sha256sum /tmp/kilo/rv1106-src/rv1106-trm-v0.3-part1.pdf /tmp/kilo/rv1106-src/atta.pdf /tmp/kilo/rv1106-src/RV1106_Datasheet_Rev2.0.pdf /tmp/kilo/rv1106-src/rkbin-RV1106_EN.md /tmp/kilo/rv1106-src/rockchip-i2c-guide-en.pdf /tmp/kilo/rv1106-src/linux-rockchip-029415.html
cp /tmp/kilo/rv1106-src/rv1106-trm-v0.3-part1.pdf research/rv1106-aov/snapshot/rockchip-rv1106-trm/Rockchip_RV1106_TRM_V0.3_Part1.pdf
cp /tmp/kilo/rv1106-src/atta.pdf research/rv1106-aov/snapshot/rockchip-rv1106-datasheet/RV1106_Datasheet_Rev1.8.pdf
cp /tmp/kilo/rv1106-src/RV1106_Datasheet_Rev2.0.pdf research/rv1106-aov/snapshot/rockchip-rv1106-datasheet/RV1106_Datasheet_Rev2.0.pdf
cp /tmp/kilo/rv1106-src/rockchip-i2c-guide-en.pdf research/rv1106-aov/snapshot/rockchip-i2c-guide/Rockchip_Developer_Guide_I2C_EN.pdf
cp /tmp/kilo/rv1106-src/rkbin-RV1106_EN.md research/rv1106-aov/snapshot/rockchip-linux-rkbin/doc/release/RV1106_EN.md
cp /tmp/kilo/rv1106-src/linux-rockchip-029415.html research/rv1106-aov/snapshot/linux-rockchip/2022-February/029415.html
```

Исходный URL I2C guide (`http://pro934ad4.pic3.ysjianzhan.cn/...`) отдаёт
403; для проверки используется зеркало `as-jackson/Rockchip-Docs`.

Для dehai скопировать 15 путей из таблицы `dehai168/rv1106-aov-ipc`, для
Rockchip-Docs — 5 путей, для X5 — 4 пути из соответствующих таблиц; для
остальных репозиторных снимков — перечисленные файлы в те же пути
`snapshot/`. Затем пересчитать `sha256sum`, обновить таблицу провенанса, дату
снятия и запись в `CHANGELOG.md` → `[Не выпущено]`. Править файлы внутри `snapshot/` нельзя:
обновление — только новой копией с новым пином.