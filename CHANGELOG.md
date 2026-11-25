# Чанжлог

Формат — [Keep a Changelog 1.1.0](https://keepachangelog.com/ru/1.1.0/),
версионирование — [SemVer 2.0.0](https://semver.org/lang/ru/).

Проект — форк SDK **Luckfox Pico V1.4**; базовая точка отсчёта — вендорский SDK
V1.4. История V1.0–V1.4 в этот файл не переносится и находится в `UPDATE_LOG.md`
(EN) и `UPDATE_LOG_CN.md` (CN).

## Правила ведения

1. Каждое изменение (функция, исправление, удаление) добавляется в `[Не выпущено]`
   в момент изменения. При выпуске раздел превращается в версию с датой
   в формате `ГГГГ-ММ-ДД`.
2. SemVer: **major** — ломающие изменения совместимости прошивки или поддержки
   оборудования; **minor** — новые функции; **patch** — исправления.
3. Категории: `Добавлено`, `Изменено`, `Исправлено`, `Удалено`, `Устарело`,
   `Безопасность`. Пустые категории опускаются.
4. История не переписывается: исправление ошибки в записи оформляется новой
   записью, а не правкой прошлой.
5. Файл обязательно обновляется при каждом изменении поведения, сборки,
   конфигурации или документации проекта.
6. `UPDATE_LOG.md` / `UPDATE_LOG_CN.md` — неприкосновенный вендорский архив.

## [Не выпущено]

### Добавлено

- **`divinus`: метки стадий старта в `/dev/kmsg` (P4, `TO_DO.md` §14).**
  Добавлен `source/src/hal/boot_mark.c`/`.h`: `main`, `sys_init`,
  `aiq_init`, `aiq_prepare`, `aiq_start`, `vi`, `vi_bind`, `venc`, `bind`,
  `first_frame` с мкс-дельтой `CLOCK_MONOTONIC`; строки видны в `dmesg` и на
  UART-консоли. Бюджет `main → first_frame` на стенде `192.168.52.33` —
  0.764–0.811 с (`aiq_init` 450–518 мс, `sys_init` 195–216 мс, VENC+bind
  ~15 мс); запись в `VERIFIED_FUNCTIONS.md`, артефакты `output/bench/boot/`.
- **`divinus`: boot-stage markers in `/dev/kmsg` (P4, `TO_DO.md` §14).**
  New `source/src/hal/boot_mark.c`/`.h`: `main`, `sys_init`, `aiq_init`,
  `aiq_prepare`, `aiq_start`, `vi`, `vi_bind`, `venc`, `bind`,
  `first_frame` with a `CLOCK_MONOTONIC` microsecond delta; the lines are
  visible in `dmesg` and on the UART console. The `main → first_frame`
  budget on the `192.168.52.33` bench is 0.764–0.811 s (`aiq_init`
  450–518 ms, `sys_init` 195–216 ms, VENC+bind ~15 ms); recorded in
  `VERIFIED_FUNCTIONS.md`, artifacts under `output/bench/boot/`.
- **`bench/boot_probe.py`: бюджет старта `divinus` и проба вендорского
  `fastboot_client`.** Подкоманды `divinus` (последний прогон меток из
  `dmesg`), `vendor` (копирование бинаря в `/userdata/boot-probe`, запуск с
  UART-консоли при остановленном `divinus`, съём `[app-cli]`-меток и stdout,
  восстановление `divinus`), `marks`; артефакты `output/bench/boot/`;
  описание и примеры — в `bench/README.md`.
  Вендорское демо на продуктовом образе не стартует (VI dev/meta доступны
  только в TB-образе) — зафиксировано в `TO_DO.md` §14.4.
- **`bench/boot_probe.py`: divinus start-up budget and a vendor
  `fastboot_client` probe.** Subcommands: `divinus` (last marker run from
  `dmesg`), `vendor` (copy the binary to `/userdata/boot-probe`, launch it
  from the UART console with `divinus` stopped, collect the `[app-cli]`
  markers and stdout, restart `divinus`), and `marks`; artifacts under
  `output/bench/boot/`, documented with examples in `bench/README.md`.
  The vendor demo does not start on the product image
  (the VI dev/meta are TB-only) — recorded in `TO_DO.md` §14.4.
- **`research/rv1106-aov/snapshot/rockchip-rv1106-datasheet/RV1106_Datasheet_Rev2.0.pdf`
  — снимок даташита RV1106 Rev 2.0 (2026-04-02, 31 с., 2 382 782 Б, sha256
  `53476c5a…`).** Rev 1.8 оставлен рядом как предыдущая редакция (снимки
  обновляются только новой копией). В README темы добавлены разбор Rev 2.0,
  факты §2.1 order info (G2/G3 — QFN128, RV1106 — BGA313) и §2.2 top marking,
  ссылка на зеркало `rockchip.fr` и строка провенанса; шапка снимка
  пересчитана — 38 файлов, 39 799 861 Б. `research/README.md` и
  `STRUCTURE.md` обновлены под Rev 2.0.
- **`research/rv1106-aov/snapshot/rockchip-rv1106-datasheet/RV1106_Datasheet_Rev2.0.pdf`
  — RV1106 Datasheet Rev 2.0 snapshot (2026-04-02, 31 pp., 2 382 782 B,
  sha256 `53476c5a…`).** Rev 1.8 is kept alongside as the previous revision
  (snapshots are only ever added, never rewritten). The topic README now has
  a Rev 2.0 review, §2.1 order info (G2/G3 QFN128, RV1106 BGA313) and §2.2
  top marking facts, the `rockchip.fr` mirror link and a provenance row; the
  snapshot header is recomputed — 38 files, 39 799 861 B. `research/README.md`
  and `STRUCTURE.md` were updated for Rev 2.0.
- **`bench/aov_i2c_diag.py`: диагностика I2C3 на уровне регистров и падов.**
  Читает `CON/CLKDIV/IEN/IPD/FCNT/SCL_OE_DB/ST/DBGCTRL/CON1` (base `0xff460000`,
  `ST` — 0x220, `DBGCTRL` — 0x224, TRM V0.3 Part1 §15.4), уровни падов
  (`0xff530070`), iomux `0xff538018`, `/proc/interrupts` и `i2cdetect`;
  умеет `--nak-release-scl`, `--sample` (чтение при включённом pclk),
  `--bitbang` (9 тактов SCL + STOP через GPIO1_D2/D3 с masked-записями
  HIWORD_UPDATE), `--mux-loss`/`--restore-mux` (детерминированное
  воспроизведение KI-080) и `--heal` (наблюдение с восстановлением iomux).
  В `sleep_power.py` лестница STM8 теперь снимает дампы до/после unbind и
  bit-bang между unbind и rebind (`--no-ladder-bitbang` отключает), а сценарий
  `aov` пишет iomux и уровни падов на каждом пробуждении.
- **`bench/aov_i2c_diag.py`: register- and pad-level I2C3 diagnostics.**
  Reads `CON/CLKDIV/IEN/IPD/FCNT/SCL_OE_DB/ST/DBGCTRL/CON1` (base
  `0xff460000`, `ST` at 0x220, `DBGCTRL` at 0x224, TRM V0.3 Part1 §15.4), the
  pad levels (`0xff530070`), iomux `0xff538018`, `/proc/interrupts` and
  `i2cdetect`; supports `--nak-release-scl`, `--sample` (reads while pclk is
  on), `--bitbang` (9 SCL clocks + STOP over GPIO1_D2/D3 with HIWORD_UPDATE
  masked writes), `--mux-loss`/`--restore-mux` (deterministic KI-080
  reproduction) and `--heal` (watch and restore the iomux). In
  `sleep_power.py` the STM8 ladder now dumps before/after unbind and bit-bangs
  between unbind and rebind (`--no-ladder-bitbang` disables it), and the `aov`
  scenario records iomux/pad levels on every wake.
- **`bench/ina_log.py` + `bench/sleep_power.py`: замер DUT через INA3221
  ESP32-моста.** Второй INA3221 на мосту измеряет вход 5 В всего DUT (CH3;
  прошивка 1.11.1, `GET /sensor`, LSB 0.4 мА). `ina_log.py` пишет CSV с
  метками фаз и импортируемым API, `sleep_power.py` прогоняет сценарии
  `levels`/`cycle`/`soak`/`peaks`/`stm8`, берёт окна deep suspend из `dmesg`,
  считает энергию цикла и сверяется с бортовым INA3221; мост держит одну
  cookie-сессию и логинится/выходит сам. Базовая линия: `off` 0.8 мА, `idle`
  364 мА / 1.72 Вт, полка deep suspend 86 мА, 4.9 мВт·ч/цикл (медиана),
  60-мин soak 279 циклов / ≥200 heartbeat; во время restore `/userdata`
  ушёл в `ro` (KI-071), power cycle вернул `rw`.
- **`bench/ina_log.py` + `bench/sleep_power.py`: DUT power via the bridge
  INA3221.** The second INA3221 on the bridge measures the whole DUT 5 V input
  (CH3; firmware 1.11.1, `GET /sensor`, 0.4 mA LSB). `ina_log.py` writes a
  phase-labelled CSV with an importable API; `sleep_power.py` runs
  `levels`/`cycle`/`soak`/`peaks`/`stm8`, reads deep-suspend windows from
  `dmesg`, integrates cycle energy and cross-checks the onboard INA3221; the
  bridge keeps one cookie session and logs in/out itself. Baseline: `off`
  0.8 mA, `idle` 364 mA / 1.72 W, deep-suspend floor 86 mA, 4.9 mWh/cycle
  (median), 60-min soak 279 cycles / ≥200 heartbeats; `/userdata` went
  `ro` during restore (KI-071), a power cycle restored `rw`.
- **`bench/sleep_power.py`: сценарий `motion` — сон с детекцией движения.**
  Включает IVS (`min_area`, `sensitivity`), `[sleep]` с `wake_check_s`, снимает
  N циклов «сон/бодрствование», инъецирует движение зум-мотором и пишет
  IVS-событие; на стенде 2026-11-22 IVS даёт полнокадровое `area=1000‰` при
  каждом wake, камера бодрствует 24–26 с, 15.25–17.94 мВт·ч/цикл (среднее
  16.85) против 4.9 мВт·ч при `wake_check=5` (KI-079).
- **`bench/sleep_power.py`: `motion` scenario — sleep with motion detection.**
  Enables IVS (`min_area`, `sensitivity`), `[sleep]` with `wake_check_s`,
  records N sleep/awake cycles, injects motion with the zoom motor and logs the
  IVS event; on the 2026-11-22 bench IVS reports a full-frame `area=1000‰` on
  every wake, the camera stays awake for 24–26 s and a cycle costs
  15.25–17.94 mWh (mean 16.85) versus 4.9 mWh at `wake_check=5` (KI-079).
- **`bench/sleep_power.py`: сценарий `aov` — PMU/AOV-MCU и интервал
  пробуждения.** Задаёт `wake_interval_s` (по умолчанию 60 для `aov`),
  `aov_mcu`/`aov_keep_isp`, пишет переходы состояния, интервалы wake,
  `aov_state` и энергию. На стенде 2026-11-22 интервал 60 с не выдержан
  (фактически 12–18 с при deep suspend 2.4 с), MCU-детекция не подтверждена
  (`aov_state=off`, `mcu_run_count=0`, meta `ae_mode=1` не помог) — KI-072.
- **`bench/sleep_power.py`: `aov` scenario — PMU/AOV-MCU and wake interval.**
  Sets `wake_interval_s` (60 by default for `aov`), `aov_mcu`/`aov_keep_isp`,
  logs state transitions, wake intervals, `aov_state` and energy. On the
  2026-11-22 bench the 60 s interval was not honoured (actual 12–18 s with a
  2.4 s deep suspend), and MCU detection was not confirmed (`aov_state=off`,
  `mcu_run_count=0`, meta `ae_mode=1` did not help) — KI-072.
- **`research/stm8-iap-an2659/`: IAP на STM8 по AN2659/AN2737/PM0051.**
  Тема фиксирует, что именно требуют документы ST к стиранию блока
  (`PM0051` §4.2: `CR2.ERASE` + `NCR2.NERASE`, триггер записью слова в блок,
  шаги 2–5 из RAM, опрос `PUL`/`EOP`), что говорят AN2659/AN2737 (архитектура
  пользовательского загрузчика, RWW только для data EEPROM) и чего из этого нет
  в нашем коде. В `snapshot/` — дословные извлечения нужных страниц (2 файла,
  17 540 Б); полные PDF не дублируются, их хеши/размеры зафиксированы.
  Вывод: недостающего шага в нашей последовательности нет (KI-075).
- **`research/stm8-iap-an2659/`: STM8 IAP per AN2659/AN2737/PM0051.** The
  topic records what the ST documents actually require for a block erase
  (`PM0051` §4.2: `CR2.ERASE` + `NCR2.NERASE`, a word write inside the block
  as the trigger, steps 2-5 from RAM, `PUL`/`EOP` polling), what AN2659/AN2737
  say (user-bootloader architecture, RWW for data EEPROM only) and which of
  those our code lacks. `snapshot/` holds verbatim extracts of the relevant
  pages (2 files, 17 540 B); the full PDFs are not duplicated and are pinned
  by hash/size. Conclusion: no step is missing from our sequence
  (KI-075).
- **`TO_DO.md` §14: разбор вендорского `fastboot_client.c` и план переноса.**
  Зафиксированы результаты анализа файла (`project/app/fastboot_client/`):
  это демо thunder-boot медиастека, а не Android fastboot; ни один
  BoardConfig его не собирает (`RK_APP_TYPE` без `RK_FASTBOOT_CLIENT`), а
  post-скрипт TB-образа удаляет бинарник. Описаны пригодные к переносу
  механизмы (порядок VENC → VI → bind → AIQ, загрузка IQ из резервированной
  памяти по `/proc/cmdline` + `mmap`, Smart IR FSM, `klog()`-маркеры стадий,
  ленивый RTSP, лог джиттера PTS), дефекты, из-за которых файл нельзя брать
  как есть, и семь пунктов P1–P7 с зависимостями. Код и тесты не менялись.
- **`TO_DO.md` §14: analysis of the vendor `fastboot_client.c` and a port
  plan.** The section records the analysis of `project/app/fastboot_client/`:
  it is a thunder-boot media-stack demo, not Android fastboot; no BoardConfig
  builds it (`RK_APP_TYPE` never contains `RK_FASTBOOT_CLIENT`), and the TB
  image post-script deletes the binary. It lists the reusable mechanisms
  (VENC → VI → bind → AIQ order, IQ from reserved memory via `/proc/cmdline`
  plus `mmap`, the Smart IR FSM, `klog()` stage markers, lazy RTSP, the PTS
  jitter log), the defects that prevent using the file as is, and seven items
  P1–P7 with dependencies. No code or tests were changed.
- **Ветка `snapshot/main-2026-11-20` — снимок `main`.** Состояние `main` на
  2026-11-20 (коммит `a59954b1e`) зафиксировано отдельной ветвью в локальном
  репозитории и на `origin`, чтобы сохранить точку до чистки веток. Код,
  сборка и конфигурация не менялись.
- **`snapshot/main-2026-11-20` — a `main` snapshot.** The `main` state of
  2026-11-20 (commit `a59954b1e`) is preserved in a dedicated branch both
  locally and on `origin` to keep a reference point taken before the branch
  cleanup. No code, build or configuration changes.

- **`bench/sleep_power.py`: сценарий `i2c` и лестница STM8/AOV (2026-11-23).**
  `--scenario i2c` — N циклов `oko-motor sleep show` + `move` без suspend;
  `aov` снимает `i2cdetect`/`GET /api/focus`/`oko-ec battery`/`aov_*`/
  `devmem 0x01001C0C/0x01001C10` на старте и пробуждениях, при пропаже
  `0x60` выполняет лестницу (dmesg/скан, безопасный unbind/rebind,
  GPIO1_D2/D3, `oko-motor reset`, `oko-ec reset`) и пишет
  `stm8-recovery.md`; `--expect-present` возвращает код 3, `--motor-sleep`,
  `--ladder-rebind`, SSH с баннер-ретраями. Стенд: 300 циклов I2C — 0
  ошибок и `present=true`; `aov_mcu=1` даёт отказ I2C3/AP (KI-080).
- **`bench/sleep_power.py`: `i2c` scenario and the STM8/AOV ladder
  (2026-11-23).** `--scenario i2c` runs N `oko-motor sleep show` + `move`
  cycles without suspend; `aov` captures `i2cdetect`, `GET /api/focus`,
  `oko-ec battery`, `aov_*` and `devmem 0x01001C0C/0x01001C10` at start and
  on every wake, and on a missing `0x60` runs the ladder (dmesg/scan, safe
  unbind/rebind, GPIO1_D2/D3, `oko-motor reset`, `oko-ec reset`) writing
  `stm8-recovery.md`; `--expect-present` exits 3, plus `--motor-sleep`,
  `--ladder-rebind` and SSH banner retries. Bench: 300 I2C cycles — 0 errors
  and `present=true`; `aov_mcu=1` fails I2C3/AP (KI-080).
- **`research/rockchip-kernel-upstream/`: фиксы rockchip-linux/kernel для нашего
  ядра (2026-11-24).** Тема разбирает ветви `develop-5.10` и `develop-6.12`
  (`develop-6.1`/`rt53` после сверки бесполезны) и фиксирует, какие коммиты
  закрывают KI-011/KI-071/KI-072/KI-073/KI-074/KI-079/KI-080, какие уже есть в
  дереве 5.10.160 и какие отклонены (новые IP, thunder-boot, normal-memory DMA,
  10 кГц). `snapshot/` — четыре серии патчей по одному файлу ядра (4 файла,
  28 042 Б), полные SHA и sha256 в таблице провенанса; формат «серии» вместо
  копий файлов обоснован в README темы (полные коммиты сенсора — 60–111 файлов).
- **`research/rockchip-kernel-upstream/`: rockchip-linux/kernel fixes for our
  kernel (2026-11-24).** The topic reviews `develop-5.10` and `develop-6.12`
  (`develop-6.1`/`rt53` turned out to be useless after the comparison) and
  records which commits address KI-011/KI-071/KI-072/KI-073/KI-074/KI-079/
  KI-080, which are already in the 5.10.160 tree and which were rejected (new
  IPs, thunder-boot, normal-memory DMA, 10 kHz). `snapshot/` holds four
  per-file patch series (4 files, 28 042 B) with full SHAs and sha256 in the
  provenance table; the series format is justified in the topic README (the
  full sensor commits touch 60-111 files).
- **`research/rv1106-aov/`: внешние материалы по AOV и MCU RV1106
  (2026-11-24).** Тема разбирает `dehai168/rv1106-aov-ipc` (deep suspend
  требует `rockchip,rv1106`, FE-PHY off и PMU `ms×32`; MCU-детекции на MIS5001
  нет), Luckfox #10/#99 (HPMCU/LPMCU, JTAG `0xff538008`, IDCODE `0xDEB11001`,
  `mcutool`), `luyi1888/rv1106-mcu` и `liamhays/rk3566-mcu` (SCR1 RV32IMC,
  загрузка, mailbox, таймер). Вывод: HPMCU — то же семейство SCR1, но
  интеграция на RV1106 другая; LPMCU подтверждён регистрами в PMUGRF и ролью
  в TRM §7.6.2, но прошивки и отдельной документации на ISA/debug нет; тезис
  о «периодическом опросе периферии» не
  подтверждён (на RK3566 MCU выключен в suspend). `snapshot/` — 44 файла,
  37 524 270 Б (GPL + Rockchip/Horizon research copies), pin/sha256 в
  таблице; выбранные 22 файла dehai включены, остальные не копировались.
  Обновлены `research/README.md` и `STRUCTURE.md`.
- **`research/rv1106-aov/`: RV1106 AOV and MCU external material
  (2026-11-24).** The topic reviews `dehai168/rv1106-aov-ipc` (deep suspend
  needs `rockchip,rv1106`, FE-PHY off and the PMU `ms×32` counter; no MCU
  detection on MIS5001), Luckfox #10/#99 (HPMCU/LPMCU, JTAG `0xff538008`,
  IDCODE `0xDEB11001`, `mcutool`), `luyi1888/rv1106-mcu` and
  `liamhays/rk3566-mcu` (SCR1 RV32IMC, boot, mailbox, timer). Conclusion:
  HPMCU is the same SCR1 core family, but the RV1106 integration differs;
  LPMCU is confirmed by PMUGRF registers and its TRM §7.6.2 role, but has no
  firmware and no separate ISA/debug documentation; the "periodic peripheral
  polling" claim is unconfirmed (on
  RK3566 the MCU is off during suspend). `snapshot/` holds 44 files,
  37 524 270 B (GPL + Rockchip/Horizon research copies), with pins/sha256
  in the provenance table; 22 selected dehai files are included and the rest
  were not copied. `research/README.md` and `STRUCTURE.md` were updated.
- **`research/rkadk-guide/`: разбор вендорского RKADK-гайда (2026-11-25).**
  Добавлена тема с read-only снимком
  `Rockchip_Developer_Guide_Linux_Rkadk_EN.pdf` (152 с., создан 2024-01-08,
  704 558 Б, sha256 `0f06e0b4…`, pin `824b817f…`). Разобрано, что из API
  record/photo/stream/RTSP/RTMP/storage/OSD/параметров применимо к `divinus`
  как чек-лист (файловый кэш и manual split, thumbnails, storage, OSD,
  параметры VENC/муксера), и что не переносится: сам RKADK, RV1106/SC230AI-
  специфика и AOV (отдельный гайд уже в `research/rv1106-aov/`). Обновлены
  `research/README.md`, `STRUCTURE.md` и ссылка в `TO_DO.md` §15.7.
- **`research/rkadk-guide/`: vendor RKADK guide review (2026-11-25).**
  New topic with a read-only snapshot of
  `Rockchip_Developer_Guide_Linux_Rkadk_EN.pdf` (152 pp., created 2024-01-08,
  704 558 B, sha256 `0f06e0b4…`, pin `824b817f…`). It maps the
  record/photo/stream/RTSP/RTMP/storage/OSD/parameter APIs to `divinus` as a
  checklist (file cache and manual split, thumbnails, storage, OSD,
  VENC/muxer parameters) and records what is not portable: RKADK itself,
  RV1106/SC230AI specifics and AOV (a separate guide is already in
  `research/rv1106-aov/`). `research/README.md`, `STRUCTURE.md` and the
  `TO_DO.md` §15.7 link were updated.

### Изменено

- **KI-072: вендорский ISP-handover признан нереализуемым на этом железе
  (2026-11-25).** План `.kilo/plans/1795603530004-ki072-isp-handover-close.md`
  остановлен по собственному стоп-правилу: схема «AP отпускает media, MCU
  владеет ISP/SC230AI во время deep suspend» не поддерживается ни железом, ни
  вендорской прошивкой. Стенд: `aov_mcu=1` + `keep_isp=0` (60 с, ядро `#38`)
  даёт сон 60.63 с (`last_suspend_time`), но handshake детерминированно
  уходит в `-h-timeout-` — MCU не может писать регистры сенсора
  (`[SC230AI] ERR write multi-regs addr=0x0103`, 10 ретраев), burst
  заканчивается уже после resume. Руководство Rockchip AOV V1.0.4 определяет
  AOV как цикл «сон → пробуждение → один кадр → сон» (`--suspend_time 1000`
  мс), во сне питание есть только у PMUIO (базовый сон ≈5 мВт, 3V3_STB
  0.05 мА, цикл «1000 мс сна + 75 мс работы» = 39 мВт), сенсор в standby;
  обе ветки `applications/battery-ipc/stream.c` заканчиваются
  `sysTick_stop(); while (1) asm("wfi")`, а «непрерывная» demo-борда —
  не-AOV вариант. В PMU RV1106 один общий `RV1106_PMU_POWER_OFF_ENA`, per-domain
  keep нет. Итог: AOV остаётся на совместимой схеме `aov_mcu=1` +
  `keep_isp=1` + PMU-таймер; в KI-072 (RU+EN) и
  `research/rv1106-aov/README.md` записаны доказательства и цитаты, gate в
  `TO_DO.md` §8.1 переформулирован в решение владельца (политика интервала,
  выпуск без handover, отдельный проект «перенос `divinus` через suspend» для
  латентности < 2 с). Артефакты:
  `output/bench/aov-test/keepisp0-20261125b`,
  `output/bench/aov-test/quiet_keepisp0-20261125b.20261125-141406.log`.
  Решение владельца 2026-11-25: вердикт принят, AOV остаётся на схеме
  `aov_mcu=1` + `keep_isp=1` + PMU-таймер (60 с), открыт только вопрос
  выпуска из экспериментального профиля.
- **KI-072: the vendor ISP-handover is not implementable on this hardware
  (2026-11-25).** The plan `.kilo/plans/1795603530004-ki072-isp-handover-close.md`
  was stopped by its own stop rule: "the AP releases media and the MCU owns
  the ISP/SC230AI during the AP deep suspend" is supported neither by the
  hardware nor by the vendor firmware. On the bench `aov_mcu=1` +
  `keep_isp=0` (60 s, kernel `#38`) sleeps for 60.63 s
  (`last_suspend_time`) but the handshake deterministically ends in
  `-h-timeout-` — the MCU cannot write the sensor registers
  (`[SC230AI] ERR write multi-regs addr=0x0103`, 10 retries) and its burst
  only completes after the resume. The Rockchip AOV guide V1.0.4 defines AOV
  as a "sleep -> wake -> one frame -> sleep" cycle (`--suspend_time 1000`
  ms) where only PMUIO is powered during the sleep (base sleep about 5 mW,
  3V3_STB 0.05 mA, a "1000 ms asleep + 75 ms of work" cycle = 39 mW) and the
  sensor stays in standby; both `applications/battery-ipc/stream.c`
  branches end in `sysTick_stop(); while (1) asm("wfi")`, and the
  "continuous" demo board is the non-AOV variant. The RV1106 PMU has a single
  global `RV1106_PMU_POWER_OFF_ENA` and no per-domain keep. As a result AOV
  stays on the compatible `aov_mcu=1` + `keep_isp=1` + PMU timer scheme;
  KI-072 (RU+EN) and `research/rv1106-aov/README.md` now carry the evidence
  and quotes, and the gate in `TO_DO.md` §8.1 became an owner decision
  (interval policy, release without handover, a separate "carry `divinus`
  across suspend" project for a sub-2 s latency). Artifacts:
  `output/bench/aov-test/keepisp0-20261125b`,
  `output/bench/aov-test/quiet_keepisp0-20261125b.20261125-141406.log`.
  Owner decision 2026-11-25: the verdict is accepted; AOV stays on
  `aov_mcu=1` + `keep_isp=1` + the PMU timer (60 s), with only the release
  out of the experimental profile still open.
- **`TO_DO.md` §15: каталог остатков вендорского кода и аудит ядра
  (2026-11-25).** Добавлен каталог всех каталогов `project/app` (назначение,
  релевантность, статус сборки, лицензия, вердикт) и разбор
  `component/fastboot_server` (подтверждён дубль ~80 %, уникальны только
  multi-cam/AVS/luma-link), backlog `rkipc` вне AF (PWM IR-LED,
  day/night-гистерезис, IQ-резерв), вердикты `rk_smart_door` и `ipcweb`,
  integration-вердикты `research/rv1106-aov` и регресс-чеклист «медиа-граблей».
  В §14 закрыты P1/P2/P4/P6/P7 (P3/P5 отложены) с измеренным бюджетом
  `divinus` и прогоном вендорского демо; §8.1 дополнен вердиктами B5;
  P7 отражён в `STRUCTURE.md` и `INIT_ORDER.md` §1.1.
- **`TO_DO.md` §15: vendor code leftover catalogue and core audit
  (2026-11-25).** Added a catalogue of every `project/app` directory
  (purpose, relevance, build status, licence, verdict) and a review of
  `component/fastboot_server` (~80 % duplicate confirmed; only
  multi-cam/AVS/luma-link are unique), the `rkipc` backlog outside AF
  (PWM IR-LED, day/night hysteresis, IQ reservation), the `rk_smart_door`
  and `ipcweb` verdicts, the `research/rv1106-aov` integration verdicts and
  the "media pitfalls" regression checklist. §14 closes P1/P2/P4/P6/P7
  (P3/P5 deferred) with the measured `divinus` budget and the vendor demo
  run; §8.1 gained the B5 verdicts; P7 is reflected in `STRUCTURE.md` and
  `INIT_ORDER.md` §1.1.
- **Паспорта платы и стенда уточнены до заказной модели SoC RV1106G3
  (2026-11-25).** В `README_RU.md`, `FEATURES.md`, `TESTBENCH.md` и
  `INIT_ORDER.md` вместо родового «RV1106» указана модель **RV1106G3**
  (QFN128, Cortex-A7 + NPU 1 Топс + ISP, встроенные 2 Гбит DDR3L = 256 МБ
  ОЗУ) со ссылкой на RV1106 Datasheet Rev 2.0 (2026-04-02). В `FEATURES.md`
  добавлена отдельная строка «ОЗУ», чтобы не смешивать 256 МБ DDR3L с 256 МБ
  SPI NAND. `RK_CHIP` в SDK остаётся `rv1106`: G3 — маркировка чипа, отдельного
  BoardConfig для него нет. В `research/rv1106-aov/` добавлен разбор Rev 2.0.
  Косвенное подтверждение на стенде: `MemTotal` 177 176 кБ при `CmaTotal`
  67 584 кБ — это 256 МБ DDR3L, а не 128 МБ; строка «ОЗУ» добавлена и в
  `TESTBENCH.md` §2.
- **Board and bench passports refined to the orderable SoC RV1106G3
  (2026-11-25).** `README_RU.md`, `FEATURES.md`, `TESTBENCH.md` and
  `INIT_ORDER.md` now name the **RV1106G3** model (QFN128, Cortex-A7 + NPU
  1 TOPS + ISP, on-package 2 Gb DDR3L = 256 MB RAM) instead of the generic
  "RV1106", with a link to RV1106 Datasheet Rev 2.0 (2026-04-02).
  `FEATURES.md` gained a separate RAM row so the 256 MB DDR3L is not confused
  with the 256 MB SPI NAND. `RK_CHIP` in the SDK stays `rv1106`: G3 is a chip
  marking, there is no separate BoardConfig. `research/rv1106-aov/` now
  covers Rev 2.0. Bench cross-check: `MemTotal` 177 176 kB with `CmaTotal`
  67 584 kB, i.e. 256 MB DDR3L rather than 128 MB; a RAM row was added to
  `TESTBENCH.md` §2 as well.
- **KI-072: кадентность AOV перепроверена после устранения KI-080
  (2026-11-24).** V1 (`aov_mcu=1`, `keep_isp=1`, `wake_interval_s=60`,
  240 с) дал 14 пробуждений с интервалами 14.9–20.4 с и окнами deep suspend
  2.26–2.27 с, `aov_state=off`; тихий `cycle` при `wake_interval_s=10` —
  те же окна 2.26–2.30 с при счётчике `0xff300048` 320 против 1920 тиков.
  Окно не масштабируется по `wake_interval_s`, то есть AP будит не
  PMU-таймер; `last_resume_reason` пуст, `last_suspend_time` =
  `2.266882594 0.080939000`. Шина I2C3 и STM8 в этих прогонах живы
  (контроль 300 циклов — 0 ошибок). Записано в `KNOWN_ISSUES.md` (KI-072).
- **KI-072: AOV cadence re-checked after the KI-080 fix (2026-11-24).**
  V1 (`aov_mcu=1`, `keep_isp=1`, `wake_interval_s=60`, 240 s) produced
  14 wakes 14.9–20.4 s apart with 2.26–2.27 s deep-suspend windows and
  `aov_state=off`; a quiet `cycle` at `wake_interval_s=10` shows the same
  2.26–2.30 s windows while `0xff300048` holds 320 versus 1920 ticks. The
  window does not scale with `wake_interval_s`, so the AP is not woken by the
  PMU timer; `last_resume_reason` is empty and `last_suspend_time` reads
  `2.266882594 0.080939000`. I2C3 and the STM8 stayed alive (300-cycle
  control: 0 errors). Recorded in `KNOWN_ISSUES.md` (KI-072).
- **`research/rv1106-aov/`: разобраны Lanxin CameraSDK и X5 SC230AI
  (2026-11-24).** В `snapshot/drobotics-x5-libcam-sensor/` добавлены 4 файла
  (50 490 Б): `sc230ai_setting.h`, `sc230ai_utility.c`, `MaybeUsed`,
  `sc230ai_camera_helper.c`. Снимок темы — 44 файла/37 524 270 Б. Выявлены
  отличия sync master/slave, dynamic FPS через VTS, DOL2 HDR и gain LUT;
  Lanxin CameraSDK не копировался — в нём нет SC230AI/RV1106-кода. X5-код
  проприетарный (Horizon), включён по решению владельца как read-only
  research copy. Обновлены README, STRUCTURE, CHANGELOG.
- **`research/rv1106-aov/`: Lanxin CameraSDK and X5 SC230AI reviewed
  (2026-11-24).** Added 4 files (50 490 B) to
  `snapshot/drobotics-x5-libcam-sensor/`: `sc230ai_setting.h`,
  `sc230ai_utility.c`, `MaybeUsed`, `sc230ai_camera_helper.c`. The topic
  snapshot is now 44 files/37 524 270 B. Identified sync master/slave
  differences, VTS-based dynamic FPS, DOL2 HDR and gain LUT; the Lanxin
  CameraSDK was not copied because it contains no SC230AI/RV1106 code. The
  X5 code is proprietary (Horizon) and is included by the project owner's
  decision as a read-only research copy. Updated README, STRUCTURE,
  CHANGELOG.
- **`research/rv1106-aov/`: добавлены документы `as-jackson/Rockchip-Docs`
  (2026-11-24).** Из публичного зеркала Rockchip взяты 5 public-документов
  (2 829 505 Б): Pinctrl RV1106, IO-Domain, Power Analysis, Camera
  Troubleshooting Linux 5.10 (RV1106/RV1103) и RGA IM2D. Снимок темы —
  44 файла/37 524 270 Б. `SOC_Internal_MCU_CN` помечен «内部资料» и не
  копировался; его факты (только BUS MCU, PMU/DDR MCU исключены; JTAG через
  GRF) пересказаны в README. Обновлены `research/README.md`, `STRUCTURE.md`,
  `CHANGELOG.md`.
- **`research/rv1106-aov/`: `as-jackson/Rockchip-Docs` documents added
  (2026-11-24).** Five public Rockchip documents (2 829 505 B) were taken
  from the public mirror: RV1106 Pinctrl, IO-Domain, Power Analysis, Linux
  5.10 Camera Troubleshooting (RV1106/RV1103) and RGA IM2D. The topic
  snapshot is now 44 files/37 524 270 B. `SOC_Internal_MCU_CN` is marked
  "internal" and was not copied; its facts (BUS MCU only, PMU/DDR MCU
  excluded; JTAG switch via GRF) are summarised in the README. Updated
  `research/README.md`, `STRUCTURE.md`, `CHANGELOG.md`.
- **`research/rv1106-aov/`: разобран локальный dehai-репозиторий
  (2026-11-24).** В `snapshot/dehai168-rv1106-aov-ipc/` добавлены 22 файла
  (7 684 761 Б): `README`, worklog, design-доки, AOV guide PDF,
  `aov_service.c`, `network_service.c`, `storage_service.c`,
  `media_service.c`, `record_service.c`, `media_osd.c`, `fmp4_mux.c`,
  `rknn_face.c`, `detect_service.c`, `default_config.json` и скрипты.
  Снимок темы — 44 файла/37 524 270 Б. У репозитория нет `LICENSE`; файлы
  включены по решению владельца проекта как read-only research copy.
  Обновлены `research/README.md`, `STRUCTURE.md`, `CHANGELOG.md`.
- **`research/rv1106-aov/`: local dehai repository reviewed (2026-11-24).**
  Added 22 files (7 684 761 B) to
  `snapshot/dehai168-rv1106-aov-ipc/`: `README`, worklog, design docs, the
  AOV guide PDF, `aov_service.c`, `network_service.c`,
  `storage_service.c`, `media_service.c`, `record_service.c`,
  `media_osd.c`, `fmp4_mux.c`, `rknn_face.c`, `detect_service.c`,
  `default_config.json` and scripts. The topic snapshot is now 44 files/
  37 524 270 B. The repository has no `LICENSE`; the files are included by
  the project owner's decision as a read-only research copy. Updated
  `research/README.md`, `STRUCTURE.md`, `CHANGELOG.md`.
- **`research`: снят лимит на размер и число файлов; `rv1106-aov` дополнен
  (2026-11-24).** Из `research/README.md` удалён лимит ≤8 файлов/≤200 КБ.
  В `research/rv1106-aov/snapshot/` добавлены TRM V0.3 (24.6 МБ), даташит
  Rev 1.8 (1.9 МБ), I2C guide V2.2.0 (315 КБ), rkbin `RV1106_EN.md` (43 КБ)
  и письмо linux-rockchip 029415 (5 КБ); снимок — 44 файла/37 524 270 Б.
  Провенанс/хеши обновлены; 22 выбранных файла dehai включены, остальные
  не копировались. Обновлены `research/README.md`, `CHANGELOG.md` и
  формулировки о лимите в `research/rockchip-npu-mainline/`,
  `rockchip-kernel-upstream/`, `stm8-iap-an2659/`, `rgn-mpp-lifecycle/`.
- **`research`: size/file limit removed; `rv1106-aov` extended (2026-11-24).**
  The ≤8 files/≤200 KB limit was removed from `research/README.md`. Added
  TRM V0.3 (24.6 MB), datasheet Rev 1.8 (1.9 MB), I2C guide V2.2.0 (315 KB),
  rkbin `RV1106_EN.md` (43 KB) and the linux-rockchip 029415 message (5 KB)
  to `research/rv1106-aov/snapshot/`; the snapshot is now 44 files/
  37 524 270 B. Provenance/hashes updated; 22 selected dehai files are
  included and the rest were not copied. Updated `research/README.md`,
  `CHANGELOG.md` and the limit wording in `research/rockchip-npu-mainline/`,
  `rockchip-kernel-upstream/`, `stm8-iap-an2659/`, `rgn-mpp-lifecycle/`.
- **`research/rv1106-aov/`: TRM, даташит, rkbin и I2C guide (2026-11-24).**
  Тема дополнена первоисточниками: RV1106 TRM V0.3 Part1 (PMU §7, MCU §8,
  I2C §15), даташит Rev 1.8 (3 домена `VDD_ARM`/`VDD_LOGIC`/`VDD_PMU`, 8 КБ
  PMU SRAM, I2C3 M1 на GPIO1_D3/D2 с питанием `GPIO1_VCC3V3`), rkbin
  `RV1106_EN` (wakeup v1.00, DDR v1.15 suspend/resume, wrap v2.03rc4 AVS),
  Rockchip I2C guide V2.2.0 (локальная копия побайтово совпадает с зеркалом;
  добавлены NACK/timeout/`ipd`/`state` и GPIO-recovery) и linux-rockchip
  029415 (SPI noirq PM). Уточнено: deep отключает `VD_LOGIC`
  на каждом входе, а не через 60 с; 60 с — `PMU_WAKEUP_TIMEOUT_CNT`; I2C1 —
  PMU-блок, пады I2C3 — на отдельной 3.3 В линии; LPMCU — PMU-контроллер
  (TRM §7.6.2), а не второй документированный SCR1. Снимок — 44 файла/
  37 524 270 Б, крупные PDF/тексты и dehai-снимок включены в `snapshot/`.
- **`research/rv1106-aov/`: TRM, datasheet, rkbin and I2C guide
  (2026-11-24).** The topic now cites the RV1106 TRM V0.3 Part1 (PMU §7,
  MCU §8, I2C §15), the Rev 1.8 datasheet (three `VDD_ARM`/`VDD_LOGIC`/
  `VDD_PMU` domains, 8 KB PMU SRAM, I2C3 M1 on GPIO1_D3/D2 powered by
  `GPIO1_VCC3V3`), rkbin `RV1106_EN` (wakeup v1.00, DDR v1.15
  suspend/resume, wrap v2.03rc4 AVS), the Rockchip I2C guide V2.2.0 (the
  local copy is byte-identical to the mirror; NACK/timeout/`ipd`/`state` and
  GPIO recovery added) and linux-rockchip 029415 (SPI noirq PM). Corrected:
  deep powers off `VD_LOGIC` on every entry, not after 60 s;
  60 s is `PMU_WAKEUP_TIMEOUT_CNT`; I2C1 is in the PMU block and I2C3 pads
  are on a separate 3.3 V rail; LPMCU is a PMU controller (TRM §7.6.2), not
  a second documented SCR1. The snapshot is now 44 files/37 524 270 B; the
  large PDFs/texts and the dehai snapshot are included in `snapshot/`.
- **Документация: AOV-MCU и интервал 60 с (2026-11-22).**
  `TESTBENCH.md` §7-AOV, `bench/README.md`, `FEATURES_UNVERIFIED.md`,
  `KNOWN_ISSUES.md` KI-072: интервал 1 минута не выдержан (12–18 с),
  MCU-детекция не подтверждена.
- **Docs: AOV MCU and the 60 s interval (2026-11-22).**
  `TESTBENCH.md` §7-AOV, `bench/README.md`, `FEATURES_UNVERIFIED.md`,
  `KNOWN_ISSUES.md` KI-072: the 1-minute interval is not honoured (12–18 s)
  and MCU detection is not confirmed.
- **Документация: сон с детекцией движения (IVS, 2026-11-22).**
  `TESTBENCH.md` §8 (motion-сценарий, 3 цикла, IVS `area=1000‰`, 16.85 мВт·ч),
  `bench/README.md`, `FEATURES_VERIFIED.md`/`FEATURES_UNVERIFIED.md`,
  `VERIFIED_FUNCTIONS.md`, `TO_DO.md` §13, `KNOWN_ISSUES.md` KI-079.
- **Docs: sleep with motion detection (IVS, 2026-11-22).**
  `TESTBENCH.md` §8 (motion scenario, 3 cycles, IVS `area=1000‰`, 16.85 mWh),
  `bench/README.md`, `FEATURES_VERIFIED.md`/`FEATURES_UNVERIFIED.md`,
  `VERIFIED_FUNCTIONS.md`, `TO_DO.md` §13, `KNOWN_ISSUES.md` KI-079.
- **Документация: питание и сон DUT через INA3221 моста (2026-11-22).**
  `TESTBENCH.md` §4 (второй INA3221 на ESP32-мосту, CH3 — вход 5 В DUT), §8
  (поля `GET /sensor`, прошивка 1.11.1, SD-CSV `ina3221_chN.csv`, базовая
  линия `[sleep]`); `bench/README.md` (скрипты, примеры, артефакты,
  `OKO_BRIDGE*`); `FEATURES_VERIFIED.md`/`FEATURES_UNVERIFIED.md` (замер
  выполнен, сверка с эталоном), `VERIFIED_FUNCTIONS.md` (строка и команды),
  `TO_DO.md` §8/§13, `KNOWN_ISSUES.md` KI-020 (силовые входы подключены).
- **Docs: DUT power and sleep via the bridge INA3221 (2026-11-22).**
  `TESTBENCH.md` §4 (second INA3221 on the ESP32 bridge, CH3 is the DUT 5 V
  input), §8 (`GET /sensor` fields, firmware 1.11.1, `ina3221_chN.csv` SD log,
  the `[sleep]` baseline); `bench/README.md` (scripts, examples, artifacts,
  `OKO_BRIDGE*`); `FEATURES_VERIFIED.md`/`FEATURES_UNVERIFIED.md` (measurement
  done, reference cross-check), `VERIFIED_FUNCTIONS.md` (row and commands),
  `TO_DO.md` §8/§13, `KNOWN_ISSUES.md` KI-020 (power inputs are connected).
- **OSD/телеметрия: INA3221 читается с канала 3 (RV1106 + STM8).**
  `S21oko-ec`: `INA_CHANNEL=${OKO_EC_INA_CHANNEL:-3}` вместо канала 1, поэтому
  строка `INA3221` в OSD, `/tmp/oko-ec.json`, `/api/telemetry` и MQTT-телеметрия
  показывают основное потребление платы (CH3), а не малый канал 1. Утилита
  `oko-ec ina226` по-прежнему по умолчанию читает CH1 — канал задаётся
  `--ina-channel`; переопределение демона — `OKO_EC_INA_CHANNEL`. Обновлены
  `INIT_ORDER.md` (демон `oko-ec`), `TESTBENCH.md` §8 и
  `project/app/oko-ec/README.md`. Стенд 2026-11-22: после
  `/etc/init.d/S21oko-ec restart` демон запущен с `--ina-channel 3`,
  `/tmp/osd-dyn.txt` → `INA3221 4.66V 0.32A 1.51W` (было `4.71V 0.01A 0.04W`
  на CH1), в `/tmp/oko-ec.json` `"channel":3`.
- **OSD/telemetry: the INA3221 is read from channel 3 (RV1106 + STM8).**
  `S21oko-ec` now uses `INA_CHANNEL=${OKO_EC_INA_CHANNEL:-3}` instead of
  channel 1, so the `INA3221` OSD line, `/tmp/oko-ec.json`, `/api/telemetry`
  and the MQTT telemetry report the main board consumption (CH3) instead of
  the small channel 1. The `oko-ec ina226` utility still defaults to CH1 - the
  channel is selected with `--ina-channel`; the daemon override is
  `OKO_EC_INA_CHANNEL`. Updated `INIT_ORDER.md` (oko-ec daemon),
  `TESTBENCH.md` §8 and `project/app/oko-ec/README.md`. Bench 2026-11-22:
  after `/etc/init.d/S21oko-ec restart` the daemon runs with
  `--ina-channel 3`, `/tmp/osd-dyn.txt` shows `INA3221 4.66V 0.32A 1.51W`
  (was `4.71V 0.01A 0.04W` on CH1) and `/tmp/oko-ec.json` reports
  `"channel":3`.
- **Стенд: зафиксировано назначение каналов INA3221.** `TESTBENCH.md` §4/§8 и
  `project/app/oko-ec/README.md`: CH1 — DRV8833 + двигатели focus/zoom,
  CH2 — STM32 ACPI-EC, CH3 — RV1106 + STM8. Добавлены опорные значения замера
  2026-11-22 при простое (CH1 ≈ 11 мА / 51 мВт, CH2 ≈ 2 мА / 9 мВт,
  CH3 ≈ 380 мА / 1.8 Вт при шине ≈ 4.6 В) и оговорка про `--shunt-mohm`
  (по умолчанию 100 мОм). В `TESTBENCH.md` убрана устаревшая пометка «силовые
  входы пока не подключены», дата обновления — 2026-11-22;
  `FEATURES_UNVERIFIED.md` (RU+EN) исправлен: каналы подключены и размечены,
  ограничение — непроверенный номинал шунта и отсутствие калибровки по
  эталонной нагрузке. Код не менялся.
- **Bench: the INA3221 channel assignment is now documented.** `TESTBENCH.md`
  §4/§8 and `project/app/oko-ec/README.md`: CH1 is the DRV8833 plus the
  focus/zoom motors, CH2 is the STM32 ACPI-EC, CH3 is the RV1106 plus the
  STM8. The entries add reference values measured on 2026-11-22 while idle
  (CH1 ≈ 11 mA / 51 mW, CH2 ≈ 2 mA / 9 mW, CH3 ≈ 380 mA / 1.8 W at a bus of
  ≈ 4.6 V) and a note about `--shunt-mohm` (100 mΩ by default).
  `TESTBENCH.md` drops the stale "power inputs not connected" note and its
  update date is now 2026-11-22; `FEATURES_UNVERIFIED.md` (RU+EN) is fixed:
  the channels are wired and mapped, and the remaining limitation is the
  unverified shunt value plus the lack of reference-load calibration. No code
  changes.
- **KI-075: гипотезы `PUL` и `FLASH_CR1.HALT` проверены на стенде и
  отклонены.** 2026-11-22 собраны три опытных загрузчика: `1.1.1` (ожидание
  `FLASH_IAPSR.PUL` в `boot_flash_erase`, boot 2550 Б), `1.1.2` (`CR1.HALT=1`
  в RAM-трамплине, 2536 Б) и `1.1.3` (`HALT=1` + `AHALT=1`, 2544 Б). Ни один
  не изменил поведение: `0x11` принимается, ответа на чтение нет, в `dmesg`
  `SCL hold`, блок не стёрт (SWIM-ридбэк). Диагностическая сборка `1.1.4`
  (заглушка без ожидания `EOP`) тоже не ответила — управление не возвращается
  из записи-триггера. Все правки откачены, версия загрузчика снова `1.1.0`,
  трамплин — исходные 67 Б. Сверка с PM0051/SPL/AN2659/AN2737 недостающего
  шага не выявила: отказ IAP-стирания — дефект экземпляра STM8S003F3P6.
  Обновлены `KNOWN_ISSUES.md` (KI-075, RU+EN), `TESTBENCH.md` §7C,
  `TO_DO.md` §3.2 (план замены чипа с критериями), `project/app/stm8-motor/
  docs/BOOTLOADER.md` (устаревший тезис «стирание как слово из Flash» и
  размеры образа), `project/app/stm8-motor/CHANGELOG.md`. Артефакты:
  `output/bench/stm8/ki075-pul-experiment-20261122.txt`,
  `ki075-cr1-experiment-20261122.txt`.
- **KI-075: the `PUL` and `FLASH_CR1.HALT` hypotheses were tested and
  rejected.** On 2026-11-22 three experimental bootloaders were built:
  `1.1.1` (`FLASH_IAPSR.PUL` wait in `boot_flash_erase`, boot 2550 B), `1.1.2`
  (`CR1.HALT=1` in the RAM trampoline, 2536 B) and `1.1.3` (`HALT=1` +
  `AHALT=1`, 2544 B). None changed the behaviour: `0x11` is accepted, the read
  never answers, `dmesg` shows `SCL hold`, the block stays unchanged in the
  SWIM read-back. The diagnostic build `1.1.4` (stub without the `EOP` wait)
  did not answer either - control never returns from the trigger write. All
  patches were reverted, the bootloader version is `1.1.0` again and the
  trampoline is back to its 67 bytes. A cross-check against
  PM0051/SPL/AN2659/AN2737 found no missing step: the IAP block erase fails
  because of the STM8S003F3P6 sample. Updated `KNOWN_ISSUES.md` (KI-075,
  RU+EN), `TESTBENCH.md` §7C, `TO_DO.md` §3.2 (part-replacement plan with
  acceptance criteria), `project/app/stm8-motor/docs/BOOTLOADER.md` (the
  stale "erase runs from Flash like a word" claim and the image sizes) and
  `project/app/stm8-motor/CHANGELOG.md`. Artifacts:
  `output/bench/stm8/ki075-pul-experiment-20261122.txt`,
  `ki075-cr1-experiment-20261122.txt`.
- **STM8: основная линия сборки — тестовая плата (`BOARD=test`, 2.3.2).**
  `project/app/stm8-motor/Makefile.stm8`: `BOARD ?= test`; `make -f
  Makefile.stm8 all` без аргументов собирает тестовую плату, продуктовая
  карта остаётся явной сборкой `BOARD=oko` (2.2.0).  Сон привода включён по
  умолчанию только на основной/тестовой сборке.  Обновлены README (RU/EN),
  `docs/HARDWARE_TESTBOARD.md`, `project/app/stm8-motor/CHANGELOG.md`.
- **STM8: the test board is the main build line (`BOARD=test`, 2.3.2).**
  `project/app/stm8-motor/Makefile.stm8`: `BOARD ?= test`; a plain `make -f
  Makefile.stm8 all` builds the test board, while the production pin map
  stays an explicit `BOARD=oko` build (2.2.0).  The drive sleep mode is
  enabled by default only on the main/test build.  README (RU/EN),
  `docs/HARDWARE_TESTBOARD.md` and `project/app/stm8-motor/CHANGELOG.md` were
  updated.
- **STM8: сон привода переведён на Wait mode (`WFI`), прошивка тестовой
  платы `2.3.2`; линия STM8 закрыта стендовой проверкой.** Стендовая
  проверка T8 на `2.3.0` выявила дефект: после входа в Active-halt
  контроллер через ~0,5 с сбрасывался по IWDG (RAM-состояние терялось,
  `GET_SLEEP` всегда отдавал нули, в `dmesg` DUT — `rk3x-i2c ... SCL hold by
  slave`). Причина — узел AWU на стендовом STM8S003F3P6 не считает
  (`AWUEN/TBR/APR` читаются и пишутся, `AWUF` не выставляется даже через
  MSR-измерение LSI), поэтому HALT нечем разбудить (KI-077). `2.3.2`:
  `hw_sleep_enter()` выполняет `WFI`, ядро просыпается по тику TIM4 (1 мс,
  обработчик кормит IWDG), I2C-slave отвечает штатно; `system_ticks` и
  тиковые таймауты работают как в Run mode. `GET_SLEEP` 0x0A: байт 2 —
  гранулярность пробуждения в мс, байт 7 — таймерные пробуждения
  (`w_timer`), `entries` считается по эпизодам сна и закрывается наверху
  главного цикла (`sleep_note_frames`), поэтому кадр, пришедший между двумя
  `WFI`, тоже учитывается.  Хост `oko-motor sleep show` печатает
  `slice N ms` / `wake i2c N timer N`; divinus больше не умножает байт 2 на
  10 (`focus_motor.c`), т.к. поле теперь в миллисекундах. `tests/hardware_test.sh` переведён на `FW_VERSION`
  (2.3.2), сравнение статусов по коду протокола, ретраи чтений после
  пробуждения и разбор метки `timer`; секция 12 требует остановленного
  divinus. Документация: `TESTBENCH.md` §7C (`factory-2.3.2.ihx`,
  обязательный `stm8flash -R`, `-s opt -r`), `KNOWN_ISSUES.md` (KI-075
  с новым воспроизведением и проверкой option bytes, новый KI-077),
  `FEATURES_UNVERIFIED.md` (строка сна: «заблокировано» →
  «ограничено»), `VERIFIED_FUNCTIONS.md` (строка T8/T9), `TO_DO.md`
  (статус и команды), `project/app/stm8-motor/*` (CHANGELOG, PROTOCOL,
  HARDWARE, README, HARDWARE_TESTBOARD).
  Стенд 2026-11-21: T8 — 41 PASS / 0 FAIL при `SLEEP_SOAK_S=1800` (12.4:
  `entries=103 i2c=103 timer=255`; 12.3: 100 циклов пробуждения без отказов;
  12.6: `entries 104→105`, счётчики не сбрасываются;
  `dmesg` без новых `SCL hold`); T9 — `[motor] sleep_enable: true`,
  `/api/firmware.stm8.sleep_*` и последовательность `SET_SLEEP on/off` +
  чтение версии проверены, полный deep suspend не выполнялся (KI-071/
  KI-074). Артефакты: `output/bench/stm8/`.
- **STM8: motor sleep reworked to Wait mode (`WFI`), test-board firmware
  `2.3.2`; the STM8 line is closed with an on-target validation.** The T8
  bench run on `2.3.0` exposed a defect: after entering Active-halt the
  controller was reset by the IWDG after ~0.5 s (RAM state lost, `GET_SLEEP`
  always returned zeros, the DUT `dmesg` showed `rk3x-i2c ... SCL hold by
  slave`). The cause is the AWU block of the bench STM8S003F3P6 not counting
  (`AWUEN/TBR/APR` read and write correctly, `AWUF` never sets, not even
  through the MSR LSI measurement), so nothing can wake HALT (KI-077).
  `2.3.2`: `hw_sleep_enter()` executes `WFI`; the core is woken by the 1 ms
  TIM4 tick (its handler reloads the IWDG) and the I2C slave keeps answering;
  `system_ticks` and tick-based timeouts behave as in Run mode. `GET_SLEEP`
  0x0A: byte 2 is the wake granularity in ms, byte 7 counts timer wakes
  (`w_timer`), `entries` counts sleep episodes and is closed at the top of the
  main loop (`sleep_note_frames`), so a frame served between two `WFI` calls is
  accounted for too.  `oko-motor sleep show` prints `slice N ms` /
  `wake i2c N timer N`; divinus no longer multiplies byte 2 by 10
  (`focus_motor.c`) because the field is milliseconds now.
  `tests/hardware_test.sh` now uses `FW_VERSION` (2.3.2), compares status
  codes from the protocol, retries reads after a wake and parses the `timer`
  label; stage 12 requires divinus to be stopped. Documentation:
  `TESTBENCH.md` §7C (`factory-2.3.2.ihx`, mandatory `stm8flash -R`,
  `-s opt -r`), `KNOWN_ISSUES.md` (KI-075 with a fresh reproduction and the
  option-byte check, new KI-077), `FEATURES_UNVERIFIED.md` (sleep row:
  "blocked" → "limited"), `VERIFIED_FUNCTIONS.md` (T8/T9 row), `TO_DO.md`
  (status and commands), `project/app/stm8-motor/*` (CHANGELOG, PROTOCOL,
  HARDWARE, README, HARDWARE_TESTBOARD).
  Bench 2026-11-21: T8 — 41 PASS / 0 FAIL with `SLEEP_SOAK_S=1800` (12.4:
  `entries=103 i2c=103 timer=255`; 12.3: 100 wake cycles with no failures;
  12.6: `entries 104→105`, the counters never restart; no new
  `SCL hold` in `dmesg`); T9 — `[motor] sleep_enable: true`,
  `/api/firmware.stm8.sleep_*` and the `SET_SLEEP on/off` plus version-read
  sequence were verified, a full deep suspend was not run (KI-071/KI-074).
  Artifacts: `output/bench/stm8/`.
- **`STRUCTURE.md`: описание `TO_DO.md` расширено до §1–§14.** В карту
  документации добавлен этап переноса полезного из вендорского
  `fastboot_client.c` (сам план — в `TO_DO.md` §14). Код не менялся.
- **`STRUCTURE.md`: the `TO_DO.md` description now covers §1–§14.** The
  documentation map mentions the plan for porting the useful parts of the
  vendor `fastboot_client.c` (the plan itself is in `TO_DO.md` §14). No code
  changes.
- **Актуализация документации (docs-check 2026-11-20) и выпуск 0.4.0.**
  `STRUCTURE.md`: в строке `bench/` перечислены все скрипты стенда
  (`dut.py`, `geometry_check.py`, `yolo_night.py`, `vpn_check.py`,
  `aov_cadence.py`, `ec_bus_soak.py`), добавлено примечание о переключении
  экспериментальных BoardConfig, у `TO_DO.md` обновлены состав этапов
  (§1–§14) и дата. `AGENTS.md` §2: в карту документации добавлены
  `VERIFIED_FUNCTIONS.md` и `TO_DO.md`. `TO_DO.md`: шапка-статус
  актуализирована на 2026-11-20 (сон привода STM8 и heartbeat-Snapshot
  камеры; MQTT вынесен как смежная подсистема). `FEATURES.md` и
  `services/README.md`: адрес DUT в рабочих командах — `192.168.52.33`
  вместо устаревшего `192.168.52.97`. `INIT_ORDER.md` §1: продуктовый
  профиль отделён от экспериментальных. Версия `/etc/oko-release` и
  примеры сборки `.okp` (`README_RU.md`, `TESTBENCH.md`, README пакета
  `divinus`) переведены на 0.4.0.
- **Documentation actualization (docs-check 2026-11-20) and the 0.4.0
  release.** `STRUCTURE.md`: the `bench/` row now lists every bench script
  (`dut.py`, `geometry_check.py`, `yolo_night.py`, `vpn_check.py`,
  `aov_cadence.py`, `ec_bus_soak.py`) and notes how the experimental
  BoardConfigs are switched, while the `TO_DO.md` stage range (§1–§14) and
  date are updated. `AGENTS.md` §2: the documentation map gained
  `VERIFIED_FUNCTIONS.md` and `TO_DO.md`. `TO_DO.md`: the status header is
  refreshed to 2026-11-20 (STM8 motor sleep and the camera heartbeat
  snapshot; MQTT is listed as a neighbouring subsystem). `FEATURES.md` and
  `services/README.md`: the DUT address in working commands is now
  `192.168.52.33` instead of the stale `192.168.52.97`. `INIT_ORDER.md` §1:
  the product profile is separated from the experimental ones. The
  `/etc/oko-release` version and the `.okp` build examples (`README_RU.md`,
  `TESTBENCH.md`, the `divinus` package README) moved to 0.4.0.

- **Документация: KI-080 и gate AOV (2026-11-23).**
  `KNOWN_ISSUES.md` KI-072/078/080, `TESTBENCH.md` §1/§7-AOV/§8,
  `FEATURES_UNVERIFIED.md` §AOV, `VERIFIED_FUNCTIONS.md` (контроль 300
  циклов I2C), `TO_DO.md` §8.1, `bench/README.md`; AOV с `aov_mcu=1`
  признан NO-GO до вендорского ISP-handover.
- **Docs: KI-080 and the AOV gate (2026-11-23).**
  `KNOWN_ISSUES.md` KI-072/078/080, `TESTBENCH.md` §1/§7-AOV/§8,
  `FEATURES_UNVERIFIED.md` §AOV, `VERIFIED_FUNCTIONS.md` (300-cycle I2C
  control), `TO_DO.md` §8.1, `bench/README.md`; AOV with `aov_mcu=1` is
  NO-GO until a vendor ISP-handover.

- **Стенд: безопасное восстановление после зависшего `[sleep]` (2026-11-23).**
  Новый `bench/recover_dut.py`: один power cycle через мост (одна
  cookie-сессия, logout), немедленное выключение сна с
  `idle_timeout_s=3600`, `--ssh` правит YAML, `--restart` перезапускает
  divinus; при `429` не пытается логиниться снова. В `sleep_power.py`
  стартовая проба AOV больше не валит прогон при сбое SSH, а `oko-ec reset`
  в лестнице только по `--ladder-ec-reset`; `power.py` принимает `logout`
  явно. Вывод KI-080 уточнён: отказ после unbind не исключает H2 (выходной
  каскад rk3x), нужен полный reset контроллера.
- **Bench: safe recovery after a hung `[sleep]` (2026-11-23).**
  New `bench/recover_dut.py`: one bridge power cycle (one cookie session,
  logout), immediate sleep disable with `idle_timeout_s=3600`, `--ssh`
  normalises the YAML and `--restart` restarts divinus; on `429` it does not
  retry the login. In `sleep_power.py` the AOV start probe no longer aborts a
  run on an SSH failure, and `oko-ec reset` is opt-in via
  `--ladder-ec-reset`; `power.py` accepts `logout` explicitly. The KI-080
  conclusion now states that unbind alone does not rule out H2 (rk3x output
  stage); a full controller reset is required.

- **I2C3: исправлен адрес мастера и подтверждён H2 (2026-11-23).**
  Лестница `sleep_power.py` разбирала `ff470000.i2c`, но это i2c4/сенсор
  (`sc230ai`), а STM8/EC/INA/SHT сидят на `ff460000.i2c` (i2c3); ошибочный
  unbind i2c4 давал Oops `rkcif_set_default_fmt`. После исправления
  воспроизведён V1 (`aov_mcu=1`, `keep_isp=1`): `SCL=0` удерживает сам
  контроллер rk3x — unbind i2c3 меняет GPIO `0x06000800 → 0x06000C00`
  (SCL=1), bind возвращает `0x40/0x41/0x42/0x60` без Oops. H2 подтверждён;
  unbind/rebind i2c3 теперь шаг лестницы по умолчанию
  (`--no-ladder-rebind` отключает), power cycle для этого отказа не нужен.
  Затронуты `bench/sleep_power.py`, `TESTBENCH.md`, `KNOWN_ISSUES.md`
  KI-073/KI-080, `bench/README.md`.
- **I2C3: corrected the master address and confirmed H2 (2026-11-23).**
  The `sleep_power.py` ladder used `ff470000.i2c`, which is i2c4/the sensor
  (`sc230ai`), while the STM8/EC/INA/SHT sit on `ff460000.i2c` (i2c3); the
  wrong i2c4 unbind produced an `rkcif_set_default_fmt` Oops. After the fix
  V1 (`aov_mcu=1`, `keep_isp=1`) was reproduced: the rk3x controller holds
  `SCL=0` — unbinding i2c3 changes GPIO `0x06000800 → 0x06000C00` (SCL=1)
  and binding restores `0x40/0x41/0x42/0x60` without an Oops. H2 is
  confirmed; the i2c3 unbind/rebind is now a default ladder step
  (`--no-ladder-rebind` disables it) and no power cycle is needed for this
  failure. Touches `bench/sleep_power.py`, `TESTBENCH.md`,
  `KNOWN_ISSUES.md` KI-073/KI-080, `bench/README.md`.

- **Экспериментальный H2-патч rk3x (2026-11-23, не устранил отказ).**
  Для i2c3 добавлен `resets = <&cru SRST_I2C3>`, в
  `rk3x_i2c_resume` — полный reset контроллера (reset-on-resume), плюс
  предупреждение о busy-transfer на suspend. Ядро #29 собрано и прошито
  через TFTP/U-Boot. V1 (`aov_mcu=1`, `keep_isp=1`) по-прежнему теряет
  I2C3 за 60 с, `busy`-предупреждений нет; патч откатан в исходниках,
  стенд возвращён на стабильное #27. После восстановления NAND AOV на #27
  всё равно зависает на первом suspend (`wake_interval_s` 10/40/60), AOV
  остаётся NO-GO. Рабочий обход — unbind/rebind `ff460000.i2c`.
- **Experimental rk3x H2 patch (2026-11-23, did not fix the failure).**
  i2c3 now has `resets = <&cru SRST_I2C3>` and `rk3x_i2c_resume` performs a
  full controller reset (reset-on-resume), plus a busy-transfer warning on
  suspend. Kernel #29 was built and flashed over TFTP/U-Boot. V1
  (`aov_mcu=1`, `keep_isp=1`) still loses I2C3 within 60 s and there are no
  busy warnings; the patch was reverted in the source tree and the bench is
  back on the stable #27. After the NAND recovery AOV on #27 still hangs on
  the first suspend (`wake_interval_s` 10/40/60), so AOV remains NO-GO. The
  working workaround is unbind/rebind of `ff460000.i2c`.

- **Итог H2-эксперимента и состояние стенда (2026-11-24).**
  Kernel-патчи #29/#30 не устранили потерю I2C3 и откатаны; стенд возвращён
  на #27 из бэкапа. После восстановления NAND зависает не только AOV, но и
  тихий `cycle` (10/40/60 с), помогает только холодный power cycle.
  Не-suspend регрессия на #27: `api_check` 139/0, `ui_check` passed,
  300 циклов I2C — 0 ошибок. AOV остаётся NO-GO; suspend-тесты до
  стабилизации PMU/HPMCU не запускать.
- **H2 experiment result and bench state (2026-11-24).**
  Kernel patches #29/#30 did not stop the I2C3 loss and were reverted; the
  bench is back on #27 from the backup. After the NAND recovery not only AOV
  but also a quiet `cycle` hangs (10/40/60 s); only a cold power cycle
  recovers. Non-suspend regression on #27: `api_check` 139/0, `ui_check`
  passed, 300 I2C cycles — 0 errors. AOV remains NO-GO; do not run suspend
  tests until the PMU/HPMCU state is stabilised.

- **rk3x: backport из Rockchip develop-5.10, тихий цикл восстановлен
  (2026-11-24).** Атомарная запись `CON`+счётчика в `rk3x_i2c_start`
  (start вынесен из-под лока вызывающего), клампы `START/STOP_SETUP_MAX`,
  очистка `REG_INT_START` в общем IPD, `disable_irq` вокруг polled-передачи.
  Ядро #32 прошито: тихий `cycle` и AOV `aov_mcu=0` снова работают
  (2.25–2.29 с), `api_check` 139/0, 300 циклов I2C — 0 ошибок. V1 с
  `aov_mcu=1`/`keep_isp=1` всё ещё теряет I2C3; H2 не закрыт. Один
  AOV-прогон снова оставил `/userdata` в `ro` (KI-071), sweep ограничен.
- **rk3x: Rockchip develop-5.10 backport, quiet cycle restored
  (2026-11-24).** Atomic `CON`+count write in `rk3x_i2c_start` (start moved
  out of the caller lock), `START/STOP_SETUP_MAX` clamps,
  `REG_INT_START` clearing in the common IPD, and `disable_irq` around a
  polled transfer. Kernel #32 was flashed: the quiet `cycle` and AOV
  `aov_mcu=0` work again (2.25–2.29 s), `api_check` 139/0, 300 I2C cycles —
  0 errors. V1 with `aov_mcu=1`/`keep_isp=1` still loses I2C3; H2 is not
  closed. One AOV run left `/userdata` `ro` again (KI-071), so the sweep is
  limited.

- **i2c3: проверка гипотезы MCU и `-EBUSY` в suspend (2026-11-24).**
  Вендорские MCU-примеры (`oko-wakeup-sc230ai-ADC`, `battery-ipc`) используют
  только i2c4 (`RT_USING_I2C3` выключен, iomux `i2c4_m2`), поэтому MCU не
  блокирует шину STM8/EC; SC230AI работает через i2c4. В
  `rk3x_i2c_suspend_noirq` добавлен `-EBUSY` при активной транзакции; V1
  всё равно теряет I2C3, отмен suspend не наблюдалось, тихий `cycle`
  работает (2.26–2.27 с). H2 остаётся на стороне AP/AOV-fast-wake.
- **i2c3: MCU hypothesis check and suspend `-EBUSY` (2026-11-24).**
  The vendor MCU examples (`oko-wakeup-sc230ai-ADC`, `battery-ipc`) use
  i2c4 only (`RT_USING_I2C3` disabled, `i2c4_m2` iomux), so the MCU does not
  occupy the STM8/EC bus; SC230AI works through i2c4. `rk3x_i2c_suspend_noirq`
  now returns `-EBUSY` for an active transfer; V1 still loses I2C3, no
  suspend abort was observed, and the quiet `cycle` works (2.26–2.27 s).
  H2 remains on the AP/AOV-fast-wake side.

- **i2c3: удержание в reset через suspend (2026-11-24, не помогло).**
  Для i2c3 добавлен `resets` в DTS; в `rk3x` появились `.suspend`
  (assert reset) и `.resume` (deassert + `adapt_div`), плюс `-EBUSY` при
  активной транзакции. Ядро #34 прошито: тихий `cycle` работает
  (2.26–2.28 с), но V1 (`aov_mcu=1`, `keep_isp=1`) всё равно теряет I2C3.
  Удержание контроллера в reset не освобождает SCL → вероятен внешний
  slave (STM8/EC) или отказ после resume; нужен bus-recovery 9 тактами
  или hardening slave.
- **i2c3: hold in reset across suspend (2026-11-24, did not help).**
  i2c3 now has DTS `resets`; `rk3x` has `.suspend` (assert reset) and
  `.resume` (deassert + `adapt_div`), plus `-EBUSY` on an active transfer.
  Kernel #34 was flashed: the quiet `cycle` works (2.26–2.28 s), but V1
  (`aov_mcu=1`, `keep_isp=1`) still loses I2C3. Holding the controller in
  reset does not release SCL → an external slave (STM8/EC) or a
  post-resume transaction is more likely; 9-clock bus recovery or slave
  hardening is needed.

- **i2c3: gate clocks и pinctrl на resume (2026-11-24, не помогло).**
  На suspend добавлен `clk_unprepare` обоих клоков (как при unbind), на
  resume — `pinctrl_pm_select_default_state()` и повторный `clk_prepare`.
  Ядро #36 прошито: тихий `cycle` работает (2.25–2.30 с), V1
  (`aov_mcu=1`, `keep_isp=1`) всё равно теряет I2C3. Серия reset/gate/
  pinctrl/busy-abort H2 не закрывает. MCU i2c3 не включали: вендорское
  приложение его не использует, это добавило бы второго мастера на шину
  STM8/EC.
- **i2c3: gate clocks and pinctrl on resume (2026-11-24, did not help).**
  `clk_unprepare` of both clocks on suspend (as unbind does) and
  `pinctrl_pm_select_default_state()` plus `clk_prepare` on resume were
  added. Kernel #36 was flashed: the quiet `cycle` works (2.25–2.30 s),
  V1 (`aov_mcu=1`, `keep_isp=1`) still loses I2C3. The reset/gate/pinctrl/
  busy-abort series does not close H2. MCU i2c3 was not enabled: the vendor
  app has no i2c3 consumer, and it would add a second master to the
  STM8/EC bus.
- **Ядро 5.10.160: бэкпорт фиксов rockchip-linux/kernel (2026-11-24).**
  Перенесены поздние коммиты вендорского ядра (интеграционный патч по AGENTS §6,
  in-tree — у `sysdrv/source/kernel` нет git-истории):
  - `drivers/spi/spi-rockchip-sfc.c` (KI-071): `53123db` — обнуление delay lines
    перед DLL-тюнингом; `9dc03be` (кросс-порт из `develop-6.12`) — `init` и
    восстановление `dll_cells` в `rockchip_sfc_init()`/`runtime_resume`. Наша
    защита 2026-11-21 сохранена: `reset`+`init` в `rockchip_sfc_resume` остаётся,
    дублирующий цикл DLL убран. `5885601`, `924d325`, `a7c2ff4` уже были в дереве;
    в `5885601` исправлен недостижимый `goto err_dma` (upstream возвращал
    `-ENOMEM` до `goto`).
  - `drivers/i2c/busses/i2c-rk3x.c` (KI-073/KI-080): `2b42b35` — `i2c->msg`
    обнуляется только в состоянии IDLE, guard KI-073 сохранён. `d8a07ba`,
    `6272868`, `f6730fc`, `13505e1` уже были в дереве (последние два — в
    локальном коммите `9ee690f5d`).
  - `arch/arm/mach-rockchip/rv1106_pm.c` (KI-072/074/079): `fca1696` —
    TIMEOUT_WKUP включается по ненулевому `PMU_WAKEUP_TIMEOUT_CNT` (при init
    счётчик обнуляется); `856ec70` — save/restore `rtc_clamp_en` через `vi_grf`;
    `8854147` — 32k-ext только при `chip_version > 0`. AOV-хуки
    (`oko_aov_enabled`, `pmu_timeout_cnt`, `hpmcu_fast_wkup`,
    `gic_irq_clear_pending`, окно mailbox 1000 мс) не тронуты.
  - `drivers/media/i2c/sc230ai.c` (KI-011): `365e2f6` — `s_frame_interval`,
    `bus_code`, tie-break по `bus_fmt`; `34690d3` — выбор режима по
    `s_fmt`/`s_frame_interval`/`set_hdr` с подбором по FPS; `aecea3e` —
    исправление `cur_best_fit = -1`. Гейт `hdr` в divinus не снят: нужна
    поддержка в `rk_aiq`/IQ.
  Сборка `./build.sh kernel` и `./build.sh driver` при активном AOV-профиле
  проходит; `output/image/boot.img` — AOV-FIT из `boot-aov.its` (gzip, kernel
  4 782 493 Б, `load = entry = 0x00208000`), `sc230ai.ko` собран. Стенд
  2026-11-24 (netboot/TFTP + NFS root, NAND не прошивался): ядро `#37`
  загрузилось, тихие циклы `aov_mcu=0` идут (deep suspend 2.26–2.28 с,
  интервал ~12 с, десятки циклов), `dmesg` без `sfc`/`ubifs`/`oops`; I2C3 жива
  после 30 `oko-ec scan` и циклов; `api_check` 139/6 — те же 6 отказов
  `/api/mqtt` (404), что и до серии, из-за `divinus` от 2026-11-17.
  Артефакты — `output/bench/kernel-backport-20261124/`. NAND-прогон
  (ядро `#37` прошито в `boot`, root ubifs): 80 циклов deep suspend при
  непрерывной нагрузке на ubifs (897 итераций записи/чтения по 512 КБ) —
  `dmesg` без `sfc`/`ubifs` ошибок, тома `rw`, контроль чтения 2 МиБ совпал;
  обновлён `sc230ai.ko` ветки — сенсор и поток работают. AOV-MCU
  (`aov_mcu=1`+`keep_isp=1`): 7 циклов с MCU-прокси, fast-AE, детекцией
  движения, mailbox и штатным resume, но H2 (KI-080) воспроизведён — I2C3
  умирает после серии циклов, AOV остаётся NO-GO. HDR не проверялся
  (гейт KI-011); статусы KI не менялись на «исправлено». Артефакты —
  `output/bench/nand-aov-20261124/`.
- **Kernel 5.10.160: backport of rockchip-linux/kernel fixes (2026-11-24).**
  Late vendor-kernel commits were ported (integration patch under AGENTS §6,
  in-tree — `sysdrv/source/kernel` has no git history):
  - `drivers/spi/spi-rockchip-sfc.c` (KI-071): `53123db` zeroes the delay lines
    before DLL tuning; `9dc03be` (cross-port from `develop-6.12`) moves `init`
    and the `dll_cells` restore into `rockchip_sfc_init()`/`runtime_resume`. The
    local 2026-11-21 protection is kept: `reset`+`init` in
    `rockchip_sfc_resume` stays, the duplicate DLL loop is gone. `5885601`,
    `924d325`, `a7c2ff4` were already in the tree; the unreachable
    `goto err_dma` in `5885601` is fixed (upstream returns `-ENOMEM` before the
    `goto`).
  - `drivers/i2c/busses/i2c-rk3x.c` (KI-073/KI-080): `2b42b35` clears
    `i2c->msg` only in the IDLE state; the KI-073 guard is kept. `d8a07ba`,
    `6272868`, `f6730fc`, `13505e1` were already in the tree (the last two via
    the local commit `9ee690f5d`).
  - `arch/arm/mach-rockchip/rv1106_pm.c` (KI-072/074/079): `fca1696` enables
    TIMEOUT_WKUP only when `PMU_WAKEUP_TIMEOUT_CNT` is non-zero (init writes 0);
    `856ec70` saves/restores `rtc_clamp_en` via `vi_grf`; `8854147` uses 32k-ext
    only when `chip_version > 0`. The AOV hooks (`oko_aov_enabled`,
    `pmu_timeout_cnt`, `hpmcu_fast_wkup`, `gic_irq_clear_pending`, 1000 ms
    mailbox window) are untouched.
  - `drivers/media/i2c/sc230ai.c` (KI-011): `365e2f6` adds `s_frame_interval`,
    `bus_code` and the `bus_fmt` tie-break; `34690d3` selects the mode from
    `s_fmt`/`s_frame_interval`/`set_hdr` with an FPS match; `aecea3e` fixes
    `cur_best_fit = -1`. The divinus `hdr` gate stays: `rk_aiq`/IQ support is
    still required.
  `./build.sh kernel` and `./build.sh driver` pass with the active AOV profile;
  `output/image/boot.img` is the `boot-aov.its` FIT (gzip, 4 782 493 B kernel,
  `load = entry = 0x00208000`) and `sc230ai.ko` builds. Bench 2026-11-24
  (netboot/TFTP + NFS root, NAND untouched): kernel `#37` booted, quiet
  `aov_mcu=0` cycles run (deep suspend 2.26-2.28 s, ~12 s period, dozens of
  cycles),
  `dmesg` has no `sfc`/`ubifs`/`oops`; I2C3 stays alive after 30 `oko-ec scan`
  runs and the cycles; `api_check` reports 139/6 with the same six
  `/api/mqtt` 404s as before the series (the `divinus` from 2026-11-17 has no
  MQTT API). Artifacts: `output/bench/kernel-backport-20261124/`. NAND run (kernel `#37`
  flashed to `boot`, ubifs root): 80 deep-suspend cycles under continuous ubifs
  load (897 write/read iterations of 512 KB) — no `sfc`/`ubifs` errors, the
  volumes stayed `rw` and a 2 MiB read-back check matched; the branch-built
  `sc230ai.ko` was installed and the sensor/stream work. AOV-MCU
  (`aov_mcu=1`+`keep_isp=1`): 7 cycles with the MCU proxy, fast AE, motion
  detection, mailbox and a normal resume, but H2 (KI-080) reproduced — I2C3
  dies after the series, so AOV stays NO-GO. HDR was not tested (KI-011 gate);
  no KI status is claimed as fixed. Artifacts:
  `output/bench/nand-aov-20261124/`.

- **Документация AOV: KI-079/KI-080 сведены в архив, KI-072 актуализирован.**
  В `KNOWN_ISSUES.md` длинные записи KI-079 и KI-080 перенесены в «Архив
  закрытых записей» (компактные строки), у KI-072 обновлены заголовок и
  статус: handshake и кадентность работают, остатки — overshoot 68–79 с и gate
  ISP-handover; добавлен итог стенда 2026-11-25 по окну 500 мс. Синхронно
  обновлены `TESTBENCH.md` §7-AOV, `FEATURES_UNVERIFIED.md` §AOV,
  `VERIFIED_FUNCTIONS.md` и `bench/README.md`; `research/rv1106-aov/README.md`
  и индекс `research/README.md` приведены к составу снимка после зачистки.
- **AOV documentation: KI-079/KI-080 moved to the archive, KI-072
  refreshed.** In `KNOWN_ISSUES.md` the long KI-079 and KI-080 entries moved
  to the closed-entries archive (compact lines), and the KI-072 heading and
  status were refreshed: the handshake and the cadence now work, residuals are
  the 68–79 s overshoot and the ISP-handover gate; the 2026-11-25 500 ms
  bench result was added. `TESTBENCH.md` §7-AOV, `FEATURES_UNVERIFIED.md`
  §AOV, `VERIFIED_FUNCTIONS.md` and `bench/README.md` were updated in sync;
  `research/rv1106-aov/README.md` and the `research/README.md` index now match
  the snapshot after the cleanup.
- **`STRUCTURE.md`: актуальный профиль платы.** Убрана устаревшая фраза
  «на 2026-11-23 активен AOV-профиль»: после работ симлинк `.BoardConfig.mk`
  возвращён на продуктовый `...-SC230AI.mk`, AOV включается тем же симлинком.
- **`STRUCTURE.md`: current board profile.** Removed the stale "the AOV
  profile is active as of 2026-11-23" sentence: after the work the
  `.BoardConfig.mk` symlink points back at the product `...-SC230AI.mk`, and
  AOV is enabled through the same symlink.
- **`TO_DO.md`: актуализация после работ 2026-11-22…25.** Закрытые пункты
  помечены `[x]` с датой и доказательством: карта реализации `divinus` (§7),
  применение ROI на ходу (§5.1), порт MCU battery-ipc, кадентность и окно
  mailbox 500 мс (§8.1), AOV-MCU с интервалом 1 мин и закрытие KI-079 (§13).
  Из §8.1 удалён утративший смысл пункт про STM8-счётчик залипаний (причина
  потери шины — meta-пин `ircut_b`, KI-080 закрыт 2026-11-24); добавлены
  открытые остатки AOV: gate вендорского ISP-handover, overshoot 68–79 с окна
  `wake_check`, прошивка и матрица тестов Э5/Э7, решение владельца о выводе
  AOV из экспериментального профиля. В §11 отмечены ограничения ночного
  замера (KI-005) и CMA (KI-006); в §4.5 ссылка `(KI-035)` исправлена на
  KI-075. Нумерация разделов сохранена.
- **`TO_DO.md`: refreshed after the 2026-11-22…25 work.** Closed items are now
  marked `[x]` with dates and evidence: the `divinus` implementation map (§7),
  live ROI application (§5.1), the battery-ipc MCU port, the cadence and the
  500 ms mailbox window (§8.1), AOV-MCU with the 1-minute interval and the
  KI-079 closure (§13). The obsolete STM8 sticky-counter item was removed from
  §8.1 (the bus loss came from the `ircut_b` meta pin, KI-080 closed on
  2026-11-24); the open AOV residuals were added: the vendor ISP-handover gate,
  the 68–79 s `wake_check` overshoot, the E5/E7 flashing and test matrix, and
  the owner decision on moving AOV out of the experimental profile. §11 now
  notes the night-measurement (KI-005) and CMA (KI-006) limits; the `(KI-035)`
  reference in §4.5 is corrected to KI-075. Section numbering is unchanged.

### Исправлено

- **`/api/record`: `enable=false` и `stop=1` теперь останавливают активную
  запись (KI-082).** Обработчик собирает `start`/`stop` в процессе разбора
  запроса и применяет команды после всех конфиг-ключей: `stop=1` и
  `enable=false` останавливают сессию безусловно, независимо от `enable`,
  `continuous` и порядка ключей; `start=1` требует `record_enable=true` по
  итогам запроса; `stop=1&start=1` перезапускает сессию. Запрос только с
  конфиг-ключами активную сессию не трогает. `record_stop()` дополнительно
  отменяет таймер событийного клипа (`recordEventActive`/`+1`), чтобы
  осиротевший таймер IVS не закрыл следующую сессию. Стенд 2026-11-24:
  `enable=false`, отдельный `stop=1`, оба порядка `stop=1`/`enable=false`,
  stop в `continuous`, рестарт одним запросом, конфиг-запрос `max_bytes`,
  IVS-клип + stop + новый старт (сессия жива спустя таймер клипа), WebUI
  Start/Stop, `api_check.py` 138/138; артефакты — `output/bench/ki082/`.
- **`/api/record`: `enable=false` and `stop=1` now stop an active recording
  (KI-082).** The handler collects `start`/`stop` while parsing the query and
  applies the commands after every config key: `stop=1` and `enable=false`
  stop the session unconditionally, regardless of `enable`, `continuous` or
  the key order; `start=1` requires `record_enable=true` in the final request
  state; `stop=1&start=1` restarts the session. A request with only config
  keys leaves the live session alone. `record_stop()` also cancels the motion
  clip timer (`recordEventActive`/`+1`), so an orphaned IVS timer cannot close
  the next session. Bench 2026-11-24: `enable=false`, a standalone `stop=1`,
  both `stop=1`/`enable=false` orders, stop in `continuous`, a one-request
  restart, a config-only `max_bytes` request, an IVS clip + stop + fresh start
  (the session survives past the clip timer), the WebUI Start/Stop buttons and
  `api_check.py` 138/138; artifacts in `output/bench/ki082/`.
- **microSD: coredump-дампы больше не пишутся в rootfs и не забивают карту
  (KI-081).** В `project/build.sh` (`__BUILD_ENABLE_COREDUMP_SCRIPT`) handler
  `/usr/bin/coredump2sdcard.sh` теперь пишет дамп только если `/mnt/sdcard`
  действительно смонтирован (`mountpoint -q`) и свободно ≥ 128 МБ, иначе
  дамп отбрасывается. Раньше `cat > /mnt/sdcard/core-$1-$2` без карты падал
  в rootfs, а с картой копил дампы без предела (30 файлов divinus/npu_probe,
  ~1.4 ГиБ); обрыв питания во время такой записи дал порчу FAT. Стенд
  2026-11-24: карта `SC16G` восстановлена `fsck.fat -a -F 1` (два прохода),
  мусор и битые каталоги очищены, карта монтируется (15177 МБ свободно),
  запись 7.9 МБ MP4 проверена; handler проверен с картой (core создаётся) и
  без карты (дамп не создаётся).
- **microSD: crash dumps no longer land on the rootfs or fill the card
  (KI-081).** In `project/build.sh` (`__BUILD_ENABLE_COREDUMP_SCRIPT`) the
  `/usr/bin/coredump2sdcard.sh` handler now writes a dump only when
  `/mnt/sdcard` is really mounted (`mountpoint -q`) and at least 128 MB are
  free; otherwise the dump is dropped. Previously
  `cat > /mnt/sdcard/core-$1-$2` fell through to the rootfs without a card
  and accumulated dumps without limit with one (30 divinus/npu_probe files,
  ~1.4 GiB); a power loss during such a write corrupted the FAT. Bench
  2026-11-24: the `SC16G` card was recovered with `fsck.fat -a -F 1` (two
  passes), the junk and broken directories were removed, the card mounts
  again (15177 MB free) and a 7.9 MB MP4 write was verified; the handler was
  tested with the card (a core is created) and without it (no dump).
- **KI-072: интервал PMU-пробуждения теперь соблюдается.** Причина «плотного
  цикла» — единицы `PMU_WAKEUP_TIMEOUT_CNT` (`0xff300048`): счётчик тактируется
  32 кГц (32768 тиков/с, активен PVTM-путь), а divinus писал
  `wake_interval_s × 32`; `wake_interval_s=60` давал 1920 тиков = 59 мс сна.
  Прежние «окна deep suspend 2.26 с» оказались resume-путём: метки `dmesg`
  идут по `CLOCK_MONOTONIC`, которая в сне не продвигается, реальный сон
  отдаёт `last_suspend_time` (было 81 мс). Калибровка на стенде: 32768 тиков →
  1.07 с, 327680 → 10.07 с, 1920000 → 58.66 с; `count=0` выключает
  timeout-wake совсем. `pmu_timeout_hz` по умолчанию стал **32768**, старые
  конфиги со значением < 1000 мигрируют при загрузке
  (`app_config_parse`). Стенд: тихий `cycle` спит 10.08 с при интервале 10 с и
  60.08 с при 60 (ровно 1 цикл на интервал), V1 (`aov_mcu=1`, `keep_isp=1`,
  60 с) — интервалы 68–79 с, I2C3 и STM8 живы. Unbind `rk_gmac-dwmac` на сон
  не влияет, а rebind не поднимает встроенный FE-PHY, поэтому методика dehai
  для OKO не применяется.
- **KI-072: the PMU wake interval is now honoured.** The "dense cycle" cause
  was the unit of `PMU_WAKEUP_TIMEOUT_CNT` (`0xff300048`): the counter runs at
  32 kHz (32768 ticks/s, PVTM path) while divinus wrote
  `wake_interval_s × 32`, so `wake_interval_s=60` programmed 1920 ticks = a
  59 ms sleep. The earlier "2.26 s deep-suspend windows" were the resume path:
  `dmesg` timestamps use `CLOCK_MONOTONIC`, which does not advance in suspend,
  and the real sleep comes from `last_suspend_time` (81 ms before the fix).
  Bench calibration: 32768 ticks → 1.07 s, 327680 → 10.07 s, 1920000 →
  58.66 s; `count=0` disables the timeout wake entirely. The
  `pmu_timeout_hz` default is now **32768** and old configs with a value below
  1000 are migrated on load (`app_config_parse`). Bench: the quiet `cycle`
  sleeps 10.08 s at a 10 s interval and 60.08 s at 60 s (one cycle per
  interval), V1 (`aov_mcu=1`, `keep_isp=1`, 60 s) shows 68–79 s intervals with
  I2C3 and the STM8 alive. Unbinding `rk_gmac-dwmac` does not change the sleep
  and rebinding does not bring the integrated FE-PHY back, so dehai's
  technique does not apply to OKO.
- **KI-079: ложное полнокадровое IVS-событие после пробуждения убрано.**
  Первые MD-результаты после рестарта пайплайна видят переход AE/ISP и дают
  `area=1000‰` на весь кадр. В `ivs_events.c` добавлено окно устоятия
  `IVS_SETTLE_S=3` после старта аналитики: результаты с площадью ≥ 900‰ в нём
  отбрасываются, движение с меньшей площадью проходит; журнал, действия и
  отсрочка сна не затрагиваются. Стенд: `--scenario motion` 3 цикла — 0/3
  событий на пробуждении (было 3/3), контроль движения зумом вне окна даёт
  `md area=728/440/871/657‰`.
- **KI-079: the false full-frame IVS event after a wake is removed.** The
  first MD results after a pipeline restart see the AE/ISP transition and
  report `area=1000‰` over the whole frame. `ivs_events.c` now keeps a
  `IVS_SETTLE_S=3` settle window after the analytics start: results with an
  area ≥ 900‰ inside it are dropped while smaller motion passes; the journal,
  actions and the sleep postponement are untouched. Bench: `--scenario motion`,
  3 cycles — 0/3 events on wake (was 3/3); a zoom-move control outside the
  window reports `md area=728/440/871/657‰`.
- **KI-072: окно mailbox HPMCU уменьшено до 500 мс.**
  `RV1106_HPMCU_FAST_WKUP_TIMEOUT` снижено с 1000 до 500 мс: измеренное
  решение MCU ~0.2 с даёт запас 2.5×. Ядро `#38` собрано в профиле `-AOV`,
  прошито в NAND (`boot`) и проверено на стенде 2026-11-25: монитор моста
  насчитал 4 `-h-wait-`/4 `-h-mbox-`/0 `-h-timeout-`; V1 (60 с, 260 с) —
  3 пробуждения, интервалы 75.5/75.3 с, сон 60.28 с, `i2c3m1` и STM8 2.3.2
  на каждом wake, `dmesg` без `rk3x`/Oops. Фолбэк на 1000 мс не
  потребовался. Артефакты: `output/bench/power/20261125-073154-aov`,
  `output/bench/aov-test/summary_mbox500-20261125.*`.
- **KI-072: the HPMCU mailbox window is reduced to 500 ms.**
  `RV1106_HPMCU_FAST_WKUP_TIMEOUT` dropped from 1000 to 500 ms: the measured
  MCU decision takes ~0.2 s, a 2.5× margin. Kernel `#38` was built in the
  `-AOV` profile, flashed to NAND (`boot`) and verified on the bench on
  2026-11-25: the bridge monitor counted 4 `-h-wait-`/4 `-h-mbox-`/
  0 `-h-timeout-`; V1 (60 s, 260 s) — 3 wakes, intervals 75.5/75.3 s, a
  60.28 s sleep, `i2c3m1` and STM8 2.3.2 on every wake, `dmesg` free of
  `rk3x`/Oops. The 1000 ms fallback was not needed. Artifacts:
  `output/bench/power/20261125-073154-aov`,
  `output/bench/aov-test/summary_mbox500-20261125.*`.
- **`bench/sleep_power.py`: сон измеряется по `last_suspend_time`.**
  Отчёт больше не выдаёт окно `PM: suspend entry/exit` за длительность сна:
  оно подписано как resume-путь, добавлена таблица фактического сна из
  `/sys/kernel/wakeup_reasons/last_suspend_time`. Именно это расхождение
  породило неверный вывод «окно 2.26 с не масштабируется».
- **`bench/sleep_power.py`: sleep is measured from `last_suspend_time`.**
  The report no longer presents the `PM: suspend entry/exit` window as the
  sleep length: it is labelled as the resume path and a real-sleep table from
  `/sys/kernel/wakeup_reasons/last_suspend_time` was added. That mismatch had
  produced the wrong "the 2.26 s window does not scale" conclusion.
- **KI-080: I2C3 больше не умирает после AOV-пробуждения.** Причина — не
  контроллер rk3x, а конфликт пина в meta-образе AOV: sensor-init для SC230AI
  объявлял `ircut_b` на GPIO1_D3, который на этой плате занят под I2C3_M1 SCL
  (STM8/EC). MCU battery-ipc (`fast_ae.c`, `ircut_switch()`) на каждом решении
  AE/день-ночь заново применял `IOMUX_FUNC0 | OUTPUT | LOW`, переводил пад в
  GPIO и прижимал SCL к земле; GPIO-регистры с загрузки держат выход низким,
  поэтому unbind/rebind и reset контроллера не помогали. В
  `project/make_meta/sensor_init/sc230ai_init.c` `ircut_b` переведён на
  GPIO1_D1 (IR-cut в продукте нет), `meta.img` пересобран. Стенд:
  детерминированное воспроизведение через iomux (`bench/aov_i2c_diag.py
  --mux-loss --restore-mux`) и V1 на NAND #37 — 14/14 пробуждений с
  `i2c3m1`, SCL=1, STM8 `0x60` и `motor.present=true` (до правки шина мертва
  уже на первом wake). KI-080 закрыт в части шины; KI-072 (плотный цикл) и
  gate AOV остаются открытыми.
- **KI-080: I2C3 no longer dies after an AOV wake.** The cause was not the
  rk3x controller but a pin clash in the AOV meta image: the SC230AI
  sensor-init table declared `ircut_b` on GPIO1_D3, which this board uses as
  I2C3_M1 SCL (STM8/EC). The battery-ipc MCU (`fast_ae.c`, `ircut_switch()`)
  re-applied `IOMUX_FUNC0 | OUTPUT | LOW` on every AE/day-night decision,
  switched the pad to GPIO and tied SCL to ground; the GPIO registers keep the
  output low since boot, so unbind/rebind and controller reset could not help.
  `ircut_b` in `project/make_meta/sensor_init/sc230ai_init.c` now points at
  GPIO1_D1 (the product has no IR-cut) and `meta.img` was rebuilt. Bench:
  deterministic reproduction through the iomux (`bench/aov_i2c_diag.py
  --mux-loss --restore-mux`) and V1 on NAND #37 — 14/14 wakes with `i2c3m1`,
  SCL=1, STM8 `0x60` and `motor.present=true` (before the fix the bus died on
  the first wake). KI-080 is closed for the bus loss; KI-072 (dense cycle) and
  the AOV gate stay open.
- **HOME FOCUS/ZOOM: полный ход до упора вместо коррекции счётчика.**
  `POST /api/focus?home=focus|zoom` в divinus (`focus_home.c`) раньше ехал на
  `|position| + home_margin`: из нуля это 25 шагов, поэтому каждое нажатие
  кнопки в WebUI давало лишь частичное смещение (после первого HOME
  `home.travel` = 25). Теперь команда не зависит от текущего счётчика и от
  обратной связи: счётчик обнуляется, объектив идёт полным ходом
  `max_pos + home_margin` одним `MOVE` (2960 шагов focus, 2300 zoom), затем
  `SET_POS(0)` с паузой 50 мс (запись позиции в EEPROM блокирует главный цикл
  и может «съесть» кадр), проверкой чтением и до трёх повторов (потеря кадра
  на пробуждении I2C). Убраны отказ из-за `status=2` (`nFAULT`
  DRV8833 больше не подключён) и проверка «позиция испорчена» — на неё
  опираться больше не нужно. Та же логика в стендовой утилите
  `oko-motor home` (`util/motorctl.c`). Стенд 2026-11-22: HOME focus →
  `travel 2960`, HOME zoom → `travel 2300`, позиция после job’а 0, повторный
  HOME снова даёт полный ход, `dmesg` без `SCL hold`. Обновлены
  `project/app/divinus/README.md` и
  `project/app/stm8-motor/docs/HARDWARE_TESTBOARD.md`, комментарий
  `focus.home_margin`.
- **HOME FOCUS/ZOOM: full travel to the stop instead of a counter
  correction.** `POST /api/focus?home=focus|zoom` in divinus
  (`focus_home.c`) used to move `|position| + home_margin` steps: from zero
  that is 25 steps, so every WebUI button press only nudged the lens (after
  the first HOME `home.travel` was 25). The command now ignores both the
  current counter and the driver feedback: the counter is zeroed, the lens is
  driven the full `max_pos + home_margin` travel with a single `MOVE`
  (2960 steps for focus, 2300 for zoom) and then `SET_POS(0)` is sent after a
  50 ms pause (the position EEPROM write blocks the main loop and can swallow
  a frame), verified by read-back and retried up to three times (a frame can
  be lost on the I2C wake path). The `status=2` abort (the DRV8833 `nFAULT` is no longer wired) and
  the "corrupted position" guard are gone. The bench utility `oko-motor home`
  (`util/motorctl.c`) mirrors the same logic. Bench 2026-11-22: HOME focus →
  `travel 2960`, HOME zoom → `travel 2300`, position 0 after the job, a
  repeated HOME still performs the full travel, and `dmesg` shows no
  `SCL hold`. Updated `project/app/divinus/README.md`,
  `project/app/stm8-motor/docs/HARDWARE_TESTBOARD.md` and the
  `focus.home_margin` comment.
- **Удалены слитые ветки: 10 локальных и 7 на `origin`.** Локально удалены
  `temp-branch`, `OKO-v1-06.10.26`, `feature/buildroot-2026-08`,
  `feature/oem-blob-nfs-env`, `feature/slim-image`, `feature/rga-dma-buf`,
  `feature/rknn-runtime-2.x`, `feature/rknpu-0.9.8`,
  `feature/yolo-osd-boxes` и `feat/sleep-heartbeat-snapshot`; у
  `feature/OKO-v1-06.10.26` локальной копии не было, удалена только
  remote-tracking ссылка. На `origin` удалены `OKO-v1-06.10.26`,
  `feat/sleep-heartbeat-snapshot`, `feature/OKO-v1-06.10.26`,
  `feature/rga-dma-buf`, `feature/rknn-runtime-2.x`, `feature/rknpu-0.9.8`,
  `feature/yolo-osd-boxes`. Все ветки полностью содержались в `main`;
  единственный уникальный коммит `feat/sleep-heartbeat-snapshot`
  (`4842f2ac7`, MIT-лицензия `stm32_acpi-ec`) дублирует содержимое коммита
  `main` `a59954b1e` — совпадение деревьев подтверждено `git diff`.
- **Merged branches removed: 10 local and 7 on `origin`.** Locally removed
  `temp-branch`, `OKO-v1-06.10.26`, `feature/buildroot-2026-08`,
  `feature/oem-blob-nfs-env`, `feature/slim-image`, `feature/rga-dma-buf`,
  `feature/rknn-runtime-2.x`, `feature/rknpu-0.9.8`,
  `feature/yolo-osd-boxes` and `feat/sleep-heartbeat-snapshot`;
  `feature/OKO-v1-06.10.26` had no local copy, only its remote-tracking ref
  was dropped. On `origin` removed `OKO-v1-06.10.26`,
  `feat/sleep-heartbeat-snapshot`, `feature/OKO-v1-06.10.26`,
  `feature/rga-dma-buf`, `feature/rknn-runtime-2.x`, `feature/rknpu-0.9.8`,
  `feature/yolo-osd-boxes`. Every branch was fully contained in `main`; the
  only unique commit of `feat/sleep-heartbeat-snapshot` (`4842f2ac7`, the
  `stm32_acpi-ec` MIT license) duplicates the `main` commit `a59954b1e` —
  identical trees confirmed by `git diff`.

### Удалено

- **Удалены неиспользуемые вендорские каталоги (2026-11-25).**
  `project/app/component/rkadk` (111 файлов, 3.4 МБ),
  `project/app/component/lvgl` (1088 файлов, 95 МБ),
  `project/app/ipcweb` (282 файла, 5.7 МБ) и
  `project/app/component/rkfsmk` (8 файлов, 1.7 МБ): ни один BoardConfig их не
  включает, `project/app/Makefile` их не собирает, ссылок из OKO-кода нет.
  У `rkfsmk` API (`rkfsmk_*`, `repair_mp4`, `kernel_*`) никто не вызывал и
  `-lrkfsmk` не линковался; в `rkipc/Makefile` остаётся guard
  `test ! -d`, поэтому правка вендорского Makefile не потребовалась, а
  `make -C project/app/rkipc rkfsmk-build` после удаления проходит.
  Удалены и устаревшие копии `librkfsmk.so`/`rkfsmk.h` из
  `project/app/out/` и `output/out/`.
  Upstream-ссылки и отметки полезного (RKADK API/examples, LVGL для
  CVR/face-gate, REST-схемы OSD/ROI/DDNS/PPPoE, FAT/MP4-ремонт) —
  `TO_DO.md` §15.7;
  кандидаты, оставленные осознанно (`rk_smart_door`, `fastboot_server`,
  `uvc_app_tiny`, `rkipc`, `third_libs`, MCU SDK), перечислены там
  же. Проверка: `make -C project/app info` проходит, удалённых каталогов в
  `app_src` нет.
- **Unused vendor directories removed (2026-11-25).**
  `project/app/component/rkadk` (111 files, 3.4 MB),
  `project/app/component/lvgl` (1088 files, 95 MB),
  `project/app/ipcweb` (282 files, 5.7 MB) and
  `project/app/component/rkfsmk` (8 files, 1.7 MB): no BoardConfig enables
  them, `project/app/Makefile` does not build them, and OKO code has no
  references. The `rkfsmk` API (`rkfsmk_*`, `repair_mp4`, `kernel_*`) had no
  callers and nothing linked `-lrkfsmk`; the `test ! -d` guard in
  `rkipc/Makefile` keeps the vendor Makefile untouched, and
  `make -C project/app/rkipc rkfsmk-build` still succeeds after the removal.
  Stale `librkfsmk.so`/`rkfsmk.h` copies were also removed from
  `project/app/out/` and `output/out/`.
  Upstream links and notes on the useful parts (RKADK
  API/examples, LVGL for CVR/face-gate, REST OSD/ROI/DDNS/PPPoE schemas,
  FAT/MP4 repair)
  are in `TO_DO.md` §15.7; the deliberately kept candidates (`rk_smart_door`,
  `fastboot_server`, `uvc_app_tiny`, `rkipc`, `third_libs`, the
  MCU SDKs) are listed there as well. Checked with
  `make -C project/app info`: the removed directories are gone from
  `app_src`.
- **`research/rv1106-aov`: удалены 7 отработавших файлов снимка dehai.**
  Выводы применены в OKO или отвергнуты стендом, поэтому удалены
  `docs/design/aov.md`, `docs/07-todolist-aov.md`, `docs/04-worklog.md`,
  `src/aov/aov_service.c`, `src/system/network_service.c`,
  `src/detect/detect_service.c`, `scripts/test_aov_fephy_off_mem.sh`.
  Уникальные факты перенесены в `research/rv1106-aov/README.md`
  («Проверенные факты» → «Во внешних источниках», «Переносимо»); таблица
  провенанса и счётчик обновлены (37 файлов, 37 417 079 Б), индекс
  `research/README.md` синхронизирован. PDF/TRM и материалы отложенных
  направлений (JTAG, mcutool, LPMCU, ISP-handover) оставлены как справка.
- **`research/rv1106-aov`: 7 spent dehai snapshot files removed.** Their
  conclusions were either applied in OKO or refuted on the bench, so
  `docs/design/aov.md`, `docs/07-todolist-aov.md`, `docs/04-worklog.md`,
  `src/aov/aov_service.c`, `src/system/network_service.c`,
  `src/detect/detect_service.c` and `scripts/test_aov_fephy_off_mem.sh` were
  deleted. The unique facts moved to `research/rv1106-aov/README.md`
  ("Проверенные факты" → "Во внешних источниках", "Переносимо"); the
  provenance table and counter were updated (37 files, 37 417 079 B) and the
  `research/README.md` index was synced. The PDFs/TRM and the deferred-topic
  material (JTAG, mcutool, LPMCU, ISP-handover) stay as reference.

## [0.4.0] — 2026-11-20

### Добавлено

- **MQTT: события и телеметрия камеры + стендовый брокер.** Новый
  самописный клиент MQTT 3.1.1 в divinus: чистый кодек `mqtt.c`/`mqtt.h`
  (CONNECT с LWT и учёткой, PUBLISH QoS0/QoS1, PINGREQ/DISCONNECT, разбор
  CONNACK/PUBACK/PINGRESP, varint) и отправитель `mqtt_post.c`/`mqtt_post.h`
  с отдельным потоком, очередью на 64 JSON в RAM (переполнение теряет самое
  старое, счётчик `dropped`), backoff 30 с/2 мин/10 мин и телеметрией
  `oko-ec` по таймеру. Темы: `oko/<id>/event` (QoS1: motion, yolo, clip,
  heartbeat), `oko/<id>/telemetry` (QoS0 retained, `available`/`age` для
  устаревшего `/tmp/oko-ec.json`) и `oko/<id>/status` (retained
  `online:true` + LWT `online:false`); `id` по умолчанию — `oko-<4 hex MAC>`.
  Секция `[mqtt]` (enable, host, login, password, topic_prefix, client_id,
  keepalive_s, telemetry_interval_s, publish_events, publish_telemetry),
  живые `GET/POST /api/mqtt` (пароль write-only, старт/стоп по `enable`,
  переподключение при смене хоста/учётки) и карточка «MQTT (события,
  телеметрия)» в WebUI → Events. Хуки стоят параллельно HTTP-каналу и его
  не заменяют. Новый профиль `services/mqtt/`: `eclipse-mosquitto:2.0.20`
  с парольным файлом из env (стендовые `oko`/`oko`, рекордер
  `oko-rec`/`oko-rec`), ACL (устройство — write/read `oko/#`, рекордер —
  read) и рекордер на 8092 (`/api/events`, `/api/messages`, `/api/telemetry`,
  `/api/status`, retention `MQTT_REC_MAX_MB`). Проверки: хост-тест
  `tests/mqtt_test.c`, `bench/mqtt_check.py` (auth, событие, телеметрия,
  LWT через `kill -9`, рестарт), расширения `api_check.py`/`ui_check.py`.
- **MQTT: camera events and telemetry + a bench broker.** A self-contained
  MQTT 3.1.1 client inside divinus: the pure codec `mqtt.c`/`mqtt.h` (CONNECT
  with LWT and credentials, PUBLISH QoS0/QoS1, PINGREQ/DISCONNECT, CONNACK/
  PUBACK/PINGRESP parsing, varint) and the `mqtt_post.c`/`mqtt_post.h` sender
  with its own thread, a 64-message RAM queue (overflow drops the oldest and
  increments `dropped`), 30 s/2 min/10 min backoff and timer-driven `oko-ec`
  telemetry. Topics: `oko/<id>/event` (QoS1: motion, yolo, clip, heartbeat),
  `oko/<id>/telemetry` (QoS0 retained, `available`/`age` for a stale
  `/tmp/oko-ec.json`) and `oko/<id>/status` (retained `online:true` plus the
  LWT `online:false`); `id` defaults to `oko-<4 hex MAC>`. The `[mqtt]`
  section (enable, host, login, password, topic_prefix, client_id,
  keepalive_s, telemetry_interval_s, publish_events, publish_telemetry), live
  `GET/POST /api/mqtt` (write-only password, start/stop on `enable`,
  reconnect after a host/credential change) and a "MQTT (events, telemetry)"
  card in WebUI → Events. The hooks run next to the HTTP channel and do not
  replace it. New `services/mqtt/` profile: `eclipse-mosquitto:2.0.20` with a
  password file generated from env (bench defaults `oko`/`oko`, recorder
  `oko-rec`/`oko-rec`), an ACL (device writes/reads `oko/#`, recorder reads)
  and a recorder on 8092 (`/api/events`, `/api/messages`, `/api/telemetry`,
  `/api/status`, retention `MQTT_REC_MAX_MB`). Checks: the host test
  `tests/mqtt_test.c`, `bench/mqtt_check.py` (auth, event, telemetry, LWT via
  `kill -9`, restart) and extensions to `api_check.py`/`ui_check.py`.
- **Сон камеры: heartbeat-кадр (Snapshot) в `[sleep]`.** Новый модуль
  `heartbeat.c`/`heartbeat_task.c` отправляет один JPEG за тихий
  wake-цикл — в конце окна `wake_check_s`, непосредственно перед повторным
  suspend. Кадр идёт через `[http_post]` на эндпоинт, производный от
  `http_post.url` (`frame` → `heartbeat`), с заголовками `X-Heartbeat`,
  `X-Heartbeat-Ts`, `X-OKO-Idle-S`, `X-OKO-Motion-Area` (‰ последнего
  IVS-результата, `-1` — IVS выключен), `X-OKO-Sleep: quiet` и
  `X-OKO-Wake-Cycle`. Ключи `heartbeat` (bool), `heartbeat_interval_s`
  (0..86400, `0` — каждый тихий цикл) и `heartbeat_retries` (0..10)
  добавлены в `[sleep]`, `GET/POST /api/sleep` (вложенный объект
  `heartbeat` с `last_sent_s`/`fail_streak`/`wake_cycle`/`reason`) и
  карточку «Питание и сон» WebUI. Сбой отправки не задерживает сон:
  `heartbeat_fail_streak` в `/api/status.sleep` растёт и сбрасывается при
  успехе. В `services/events` добавлены `PUT /ingest/heartbeat`,
  `GET /api/heartbeats` и хранение `heartbeat/<дата>/hb-<ts>.jpg` вне
  событийного архива; `bench/heartbeat_check.py` проверяет циклы, motion и
  негативный сценарий. Хост-тест — `tests/heartbeat_test.c`.
- **Camera sleep: heartbeat snapshot in `[sleep]`.** The new
  `heartbeat.c`/`heartbeat_task.c` module sends one JPEG per quiet wake cycle
  at the end of `wake_check_s`, right before the repeated suspend. The frame
  goes through `[http_post]` to the endpoint derived from `http_post.url`
  (`frame` → `heartbeat`) with `X-Heartbeat`, `X-Heartbeat-Ts`,
  `X-OKO-Idle-S`, `X-OKO-Motion-Area` (last IVS result in permille, `-1` when
  IVS is off), `X-OKO-Sleep: quiet` and `X-OKO-Wake-Cycle` headers. The
  `heartbeat` (bool), `heartbeat_interval_s` (0..86400, `0` = every quiet
  cycle) and `heartbeat_retries` (0..10) keys were added to `[sleep]`,
  `GET/POST /api/sleep` (nested `heartbeat` object with
  `last_sent_s`/`fail_streak`/`wake_cycle`/`reason`) and the "Power and sleep"
  WebUI card. A failed send never postpones the suspend:
  `heartbeat_fail_streak` in `/api/status.sleep` grows and resets on success.
  `services/events` gained `PUT /ingest/heartbeat`, `GET /api/heartbeats` and
  `heartbeat/<date>/hb-<ts>.jpg` storage outside the event archive;
  `bench/heartbeat_check.py` covers cycles, motion and the negative case. The
  host test is `tests/heartbeat_test.c`.
- **Экспериментальный профиль `-AOV`: PMCU-прокси пробуждения (этапы 0–7
  плана AOV, NO-GO на стенде).** Добавлены профиль
  `BoardConfig-SPI_NAND-Buildroot-RV1106_Luckfox_Pico_Pro_Max-SC230AI-AOV.mk`,
  DTS `rv1106g-luckfox-pico-pro-max-sc230ai-aov.dts`, фрагмент
  `rv1106-hpmcu-aov.config`, MCU-каталог `oko-wakeup-sc230ai-ADC` (RK_AOV,
  SC230AI, meta 0x01000000, ISP 0x01060000), ini
  `RV1106MINIALL_SPI_NAND_HPMCU.ini`, sysfs-интерфейс
  `aov_wakeproxy`/`aov_state` в `rockchip-pm`, гейт и timeout в
  `rv1106_pm.c`, ключи `[sleep] pmu_timer_wake/aov_mcu`, плоский
  `GET/POST /api/sleep`, карточка «Питание и сон» в WebUI, а также
  профиль-зависимые `tftp_flash.py` (разбор mtdparts, `idblock`, SSH-бэкап)
  и `make_oko_update.sh`. Профиль **не выпускается**: Hpmcu+SPL+DDR+usbplug
  (~300 КБ) не влезают в продуктовый idblock 256K, поэтому idblock расширен
  до 1M и все разделы после него сдвинуты на 768K; rootfs уменьшен на 1M (167M); meta-раздела нет
  (256 МБ заняты), blob лежит в OEM и копируется в reserved-memory
  0x01000000. Стенд 2026-11-16/17: idblock с `Hpmcu=` и AOV-ядро загрузились,
  `/api/status` показал `aov_available:true`, но первый цикл suspend/wake
  завис, после чего пересобранное ядро перестало доходить до userspace;
  исходный продуктовый образ 09:35 восстановлен по TFTP, плата жива
  (`bad PEBs: 0`). Сдвиг на 1M проверен отдельно 2026-11-17: AOV-образ
  загрузился, `/proc/mtd` — idblock 1M, rootfs 167M (rootfs 142.8M, 104M
  свободно), `/api/status.sleep.aov_available=true`, `api_check` 133/0,
  `ui_check` зелёный (новая карточка «Питание и сон» не ломает восемь
  разделов); suspend/wake NO-GO остаётся. Причина не разделена (sleep-stack/KI-071/PMU); AOV-профиль
  помечен экспериментальным.
- **Experimental `-AOV` profile: PMCU wake proxy (AOV plan stages 0–7,
  hardware NO-GO).** Adds the
  `BoardConfig-SPI_NAND-Buildroot-RV1106_Luckfox_Pico_Pro_Max-SC230AI-AOV.mk`
  profile, the `rv1106g-luckfox-pico-pro-max-sc230ai-aov.dts` DTS, the
  `rv1106-hpmcu-aov.config` fragment, the `oko-wakeup-sc230ai-ADC` MCU
  board (RK_AOV, SC230AI, meta 0x01000000, ISP 0x01060000), the
  `RV1106MINIALL_SPI_NAND_HPMCU.ini` loader, the `aov_wakeproxy`/
  `aov_state` sysfs interface in `rockchip-pm`, the gate and timeout in
  `rv1106_pm.c`, the `[sleep] pmu_timer_wake/aov_mcu` keys, the flat
  `GET/POST /api/sleep`, the "Power and sleep" WebUI card, and the
  profile-aware `tftp_flash.py` (mtdparts parsing, `idblock`, SSH backup)
  and `make_oko_update.sh`. The profile is **not released**: Hpmcu + SPL +
  DDR + usbplug (~300 KB) do not fit the product 256K idblock, so the AOV
  idblock is 1M and every following partition moves by 768K; rootfs gives up 1M (167M); there is no
  meta partition (the 256 MB NAND is full), so the blob ships in OEM and is
  copied into reserved memory at 0x01000000. Bench 2026-11-16/17: the
  `Hpmcu=` idblock and the AOV kernel booted, `/api/status` reported
  `aov_available:true`, but the first suspend/wake cycle hung and the
  rebuilt kernel then stopped reaching userspace; the original 09:35
  product image was restored over TFTP and the board is healthy
  (`bad PEBs: 0`). The 1M shift was verified separately on 2026-11-17: the
  AOV image booted, `/proc/mtd` shows idblock 1M and rootfs 167M (rootfs
  142.8M, 104M free), `/api/status.sleep.aov_available=true`, `api_check`
  133/0 and `ui_check` green (the new "Power and sleep" card does not break
  the eight sections); the suspend/wake NO-GO remains. The root cause is not isolated (sleep stack/
  KI-071/PMU); the AOV profile is experimental.
- **divinus: политика сна камеры `[sleep]` (Д2).** Новый модуль
  `sleep_policy.{c,h}`: после `idle_timeout_s` без движения (хуки IVS с
  `min_area` и YOLO-детекций) дивинус штатно разбирает пайплайн, пишет
  `host_state=SUSPENDING` в EC, программирует `PMU_WAKEUP_TIMEOUT_CNT`
  (`0xff300048`, 32 тика/с) и уходит в deep suspend. После PMU-wake процесс
  перезапускается через существующий SIGHUP-путь (`execvp`, `FD_CLOEXEC`) с
  `OKO_SLEEP_WAKE=1`; новый экземпляр в течение `wake_check_s` ждёт движение,
  при активности остаётся в обычном режиме, иначе засыпает снова. Секция
  `[sleep]` (`enable`, `idle_timeout_s=600`, `wake_interval_s=10`,
  `wake_check_s=5`, `pmu_timeout_hz=32`) добавлена в `app_config` и
  `divinus.yaml`; `/api/status` получил поле `sleep`, причина пробуждения
  переживает exec в `/tmp/oko-sleep-wake`. Политика сама отключается, если
  ядро не предлагает deep suspend или нет `/dev/mem`. Стенд 2026-11-16:
  циклы idle→suspend→probe→suspend, два `idle_timeout_s=600`, 1-часовой
  soak (288 циклов, pid не менялся, NAND 0 ошибок, память 33→35 МБ, поток
  26–30 fps), после soak `api_check` 133/0, `ui_check` зелёный,
  поток/IVS/rga2/npu/EC живы.
- **divinus: `[sleep]` camera sleep policy (Д2).** New `sleep_policy.{c,h}`:
  after `idle_timeout_s` without motion (IVS `min_area` and YOLO detection
  hooks) divinus tears the pipeline down, writes `host_state=SUSPENDING` to
  the EC, programs `PMU_WAKEUP_TIMEOUT_CNT` (`0xff300048`, 32 ticks/s) and
  enters deep suspend. After the PMU wake the process restarts through the
  existing SIGHUP path (`execvp`, `FD_CLOEXEC`) with `OKO_SLEEP_WAKE=1`; the
  new instance waits `wake_check_s` for motion, stays awake on activity and
  suspends again when the window is quiet. The `[sleep]` section (`enable`,
  `idle_timeout_s=600`, `wake_interval_s=10`, `wake_check_s=5`,
  `pmu_timeout_hz=32`) is part of `app_config` and `divinus.yaml`;
  `/api/status` gained the `sleep` field, and the wake source survives the
  exec in `/tmp/oko-sleep-wake`. The policy disables itself when the kernel
  has no deep suspend or `/dev/mem` is missing. Bench 2026-11-16:
  idle→suspend→probe→suspend cycles, two `idle_timeout_s=600` runs and a
  1-hour soak (288 cycles, PID unchanged, zero NAND errors, memory
  33→35 MB, 26–30 fps stream); after the soak `api_check` is 133/0 and
  `ui_check` passes, the stream, IVS, rga2/npu and the EC are alive.
- **oko-ec: протокол v3 — `power` (SET_POWER/GET_POWER) и честный
  `host_state`.** `ec_proto` получил `EC_CMD_GET_POWER 0x20` /
  `EC_CMD_SET_POWER 0x21`, структуру `ec_power` и функции
  `ec_get_power()/ec_set_power()`; CLI — `oko-ec power [--state NAME]`
  (`awake`, `suspending`, `suspended`, …) с JSON/quiet-выводом, демон пишет
  `AWAKE` при старте, кладёт блок `power` (host_state, flags, power_request,
  last_event, event_count, reset_cause, last_wake_src, wake_count) в
  `/tmp/oko-ec.json` и логирует смену `last_wake_src/wake_count`. На стенде
  прошивка STM32 отвечает `proto=3`; `last_wake_src` — источник пробуждения
  самого EC (после I2C-обмена обычно 2), а не AP, поэтому PMU-timer и кнопку
  по нему не различить (зафиксировано в TESTBENCH §8).
- **oko-ec: protocol v3 `power` (SET_POWER/GET_POWER) and a real
  `host_state`.** `ec_proto` gained `EC_CMD_GET_POWER 0x20` /
  `EC_CMD_SET_POWER 0x21`, the `ec_power` structure and
  `ec_get_power()/ec_set_power()`; the CLI is `oko-ec power [--state NAME]`
  (`awake`, `suspending`, `suspended`, …) with JSON/quiet output. The daemon
  writes `AWAKE` on start, adds the `power` block (host_state, flags,
  power_request, last_event, event_count, reset_cause, last_wake_src,
  wake_count) to `/tmp/oko-ec.json` and logs `last_wake_src/wake_count`
  changes. The STM32 firmware reports `proto=3` on the bench;
  `last_wake_src` is the EC's own wake source (usually 2 after I2C traffic),
  not the AP's, so the PMU timer and the button cannot be told apart by it
  (noted in TESTBENCH §8).
- **Продуктовый образ: deep-suspend и PMU timeout wake (гейт Д1 плана сна
  камеры).** Фрагмент `rv1106-pmu-timeout.config` добавлен в продуктовый
  `BoardConfig-SPI_NAND-Buildroot-RV1106_Luckfox_Pico_Pro_Max-SC230AI.mk`
  (`CONFIG_SUSPEND`, `CONFIG_PM_SLEEP`, `CONFIG_PM_WAKELOCKS`,
  `CONFIG_ROCKCHIP_SUSPEND_MODE`, `CONFIG_RV1106_PMU_WAKEUP_TIMEOUT`,
  `CONFIG_DEVMEM`). Проверено на стенде: `echo mem` входит в **deep**, а не
  `s2idle`; PMU-timeout (`0xff300048`) будит AP и ядро возвращается в шелл,
  сеть/eth0, SoftAP `wlan0`, rga2/npu, I2C3 (`oko-ec sensors` — EC, INA3221,
  SHT21) и UBIFS/SFC после resume живые. Три обязательных условия, найденных
  на стенде: (1) в root `compatible` продукта добавлен `rockchip,rv1106`, иначе
  ядро выбирает generic машинный дескриптор и `rv1106_suspend_ops` вообще не
  регистрируются (`/sys/power/mem_sleep` = только `[s2idle]`); (2) после
  включения `CONFIG_PM_SLEEP` все внешние `.ko` обязаны быть пересобраны
  (`./build.sh driver`) — старые модули вешают загрузку на `video_rkisp`;
  (3) PMU timeout считает **32 тика в секунду** (вендорский
  `rkadk_aov.c`: `io -4 0xff300048 seconds*32`), а не 32 кГц: 320 = 10 с.
  С активным CSI/ISP-пайплайном resume даёт шторм `mipi-csi2-hw ERR1:0x10`
  и AP фактически зависает, поэтому перед suspend пайплайн надо остановить,
  а после wake — поднять заново (перезапуск divinus проверен: поток, IVS и
  RTSP восстанавливаются). Решение Д1: классический mem-suspend с остановкой
  и повторным стартом пайплайна, без fallback-цепочек.
- **Product image: deep suspend and PMU timeout wake (plan gate Д1).** The
  `rv1106-pmu-timeout.config` fragment is now part of the product
  `BoardConfig-SPI_NAND-Buildroot-RV1106_Luckfox_Pico_Pro_Max-SC230AI.mk`
  (`CONFIG_SUSPEND`, `CONFIG_PM_SLEEP`, `CONFIG_PM_WAKELOCKS`,
  `CONFIG_ROCKCHIP_SUSPEND_MODE`, `CONFIG_RV1106_PMU_WAKEUP_TIMEOUT`,
  `CONFIG_DEVMEM`). Bench result: `echo mem` enters **deep**, not `s2idle`;
  the PMU timeout (`0xff300048`) wakes the AP and the kernel returns to a
  shell with eth0, the `wlan0` SoftAP, rga2/npu, I2C3 (`oko-ec sensors` — EC,
  INA3221, SHT21) and UBIFS/SFC alive after resume. Three hard requirements
  found on the bench: (1) the product root `compatible` needs
  `rockchip,rv1106`, otherwise the kernel picks the generic machine descriptor
  and `rv1106_suspend_ops` are never registered (`/sys/power/mem_sleep` shows
  only `[s2idle]`); (2) after enabling `CONFIG_PM_SLEEP` every external `.ko`
  must be rebuilt (`./build.sh driver`) — stale modules hang the boot in
  `video_rkisp`; (3) the PMU timeout counts **32 ticks per second** (vendor
  `rkadk_aov.c`: `io -4 0xff300048 seconds*32`), not 32 kHz: 320 means 10 s.
  With the CSI/ISP pipeline running, resume produces a `mipi-csi2-hw
  ERR1:0x10` storm and the AP effectively wedges, so the pipeline must be
  stopped before suspend and restarted after wake (a divinus restart was
  verified to restore the stream, IVS and RTSP). Д1 decision: classic
  mem-suspend with pipeline stop/restart, no fallback chain.
- **Экспериментальная wakeup-IPC линия: MCU + TB-образ под SC230AI и сон
  камеры по отсутствию движения.** MCU-борд `battery_ipc_demo_v10-SC230AI-ADC`
  портирован под Luckfox (`i2c_addr = 0x32`, шина i2c4/m2, reset GPIO3_C5 без
  изменений) и воспроизводимо собирается `./build.sh mcu
  battery_ipc_demo_v10-SC230AI-ADC`. Добавлены TB-BoardConfig
  `BoardConfig-SPI_NAND-Busybox-RV1106_Luckfox_Pico_Pro_Max-SC230AI-IPC_FASTBOOT.mk`,
  DTS `rv1106g-luckfox-pico-pro-max-sc230ai-fastboot.dts`, фрагмент ядра
  `rv1106-pmu-timeout.config` (suspend + PMU timeout wake) и post-скрипт
  `oko-rv1106-tb-sc230ai-wakeup-post.sh` (rcS с `rkipc`, prune Wi-Fi/демо,
  strip, IQ json). AP-политика в `rv1106_wakeup_ipc`: новая секция `[aov]`
  (`sleep_timeout_s`, `wake_interval_s`, `wake_check_s`, `pmu_timeout_hz`),
  `ivs:md_area_percent` вместо жёстких 30 % и модуль `video/aov_sleep.c`:
  после 600 с без движения rkipc программирует `PMU_WAKEUP_TIMEOUT_CNT` и
  уводит AP в suspend, после timeout-wake даёт окно MD и либо продолжает
  стрим, либо засыпает снова. Статус — bench-only: прошивка и матрица
  тестов (Э5/Э7 плана) не выполнены, см. `FEATURES_UNVERIFIED.md`.
- **Experimental wakeup-IPC line: MCU + SC230AI thunder-boot image with
  idle-based camera sleep.** The `battery_ipc_demo_v10-SC230AI-ADC` MCU board
  is ported to the Luckfox hardware (`i2c_addr = 0x32`, i2c4/m2 bus, GPIO3_C5
  reset unchanged) and builds reproducibly with `./build.sh mcu
  battery_ipc_demo_v10-SC230AI-ADC`. Added the thunder-boot BoardConfig
  `BoardConfig-SPI_NAND-Busybox-RV1106_Luckfox_Pico_Pro_Max-SC230AI-IPC_FASTBOOT.mk`,
  the DTS `rv1106g-luckfox-pico-pro-max-sc230ai-fastboot.dts`, the kernel
  fragment `rv1106-pmu-timeout.config` (suspend + PMU timeout wake) and the
  post-build script `oko-rv1106-tb-sc230ai-wakeup-post.sh` (rcS with `rkipc`,
  Wi-Fi/demo pruning, strip, IQ json). The AP-side policy in
  `rv1106_wakeup_ipc` gains an `[aov]` section (`sleep_timeout_s`,
  `wake_interval_s`, `wake_check_s`, `pmu_timeout_hz`), `ivs:md_area_percent`
  instead of the hardcoded 30 % and the `video/aov_sleep.c` module: after
  600 s without motion rkipc programs `PMU_WAKEUP_TIMEOUT_CNT` and suspends
  the AP, then after a timeout wake it runs one motion-detection window and
  either keeps streaming or suspends again. Status: bench-only, the flashing
  and test matrix (plan E5/E7) are not executed, see
  `FEATURES_UNVERIFIED.md`.
- **`bench/rgn_boxes_check.py` — soak 2BPP-рамок (KI-063).** Скрипт снимает
  fd, `VmRSS`, число потоков и счётчики ядра (`rockchip-sfc`, `UBIFS error`,
  `dup mClientFd`/`CHECK_GE`, oops, `rga2_reg`) каждые 10 с и пишет JSON в
  `output/bench/rgn/`. Режимы: `observe` (baseline), `cycle` (A/B тумблер
  рамок через API), `restart` (SIGHUP-рестарты с привязанным регионом),
  `config` (правка `boxes` в `/userdata/divinus.yaml` с бэкапом и
  рестартом), `steady`. Код 1 — рост fd/VmRSS или новый счётчик ядра.
- **`bench/rgn_boxes_check.py` — 2BPP box soak (KI-063).** The script samples
  fd, `VmRSS`, thread count and kernel counters (`rockchip-sfc`,
  `UBIFS error`, `dup mClientFd`/`CHECK_GE`, oops, `rga2_reg`) every 10 s and
  writes JSON to `output/bench/rgn/`. Modes: `observe` (baseline), `cycle`
  (A/B box toggling via the API), `restart` (SIGHUP restarts with the region
  attached), `config` (edit `boxes` in `/userdata/divinus.yaml` with a backup
  and a restart) and `steady`. Exit code 1 means fd/VmRSS growth or a new
  kernel counter.
- **Тема `research/rgn-mpp-lifecycle/`.** Разбор вендорского цикла
  `RK_MPI_RGN_*` для 2BPP-рамок: пример `test_mpi_rgn.cpp`, реальный
  потребитель `rkipc`, переносимость destroy/recreate под живым энкодером и
  отсутствие вендорского освобождения `canvasBlk`. Снимки не снимаются —
  источники уже в дереве; фиксируются пути, sha256 и pin.
- **Topic `research/rgn-mpp-lifecycle/`.** Analysis of the vendor
  `RK_MPI_RGN_*` lifecycle for 2BPP boxes: the `test_mpi_rgn.cpp` sample, the
  real `rkipc` consumer, portability of destroy/recreate under a running
  encoder, and the missing vendor release of `canvasBlk`. No snapshots are
  taken because the sources are already in the tree; paths, sha256 and the pin
  are recorded instead.
- **mDNS-публикация `oko-<MAC>.local`.** `divinus` публикует имя
  `oko-<4 hex MAC eth0>.local` встроенным `tinysvcmdns` (без Avahi, новых
  пакетов и демонов) и отвечает A-записями по всем не-loopback IPv4 (до трёх).
  Управление живое: `GET /api/mdns` (`enable`, `running`, `name`,
  `addresses`) и `POST /api/mdns?enable=0|1` (400 на неверное значение,
  сохранение в `divinus.yaml`), карточка **Сеть → mDNS discovery** в WebUI с
  именем, списком адресов и переключателем. `[mdns] enable` по умолчанию
  теперь `true` — на всех устройствах после OTA открывается UDP 5353 и идёт
  фоновый multicast-трафик. Имя выводится из MAC `eth0` (`wlan0` — только
  fallback), поэтому две платы в одной сети не конфликтуют, а имя переживает
  обновление прошивки и замену USB Wi-Fi. Responder стартует только при
  наличии IPv4 (на boot-race `divinus` поднимается в `S21appinit` раньше
  DHCP), набор адресов сверяется раз в 10 с и при изменении пересоздаётся
  (`mdnsd_set_hostname()` нельзя звать дважды на живом инстансе); без
  адреса публикация останавливается, занятый порт 5353 не роняет процесс.
  Неудачный старт повторяется до пяти раз с паузой 60 с, затем попытка ждёт
  нового набора адресов или переключения; на error-путях `tinysvcmdns`
  (`create_recv_sock`/`mdnsd_start`) закрываются сокет и pipe, поэтому серия
  неудач не утекает дескрипторами (проверено: 15 неудачных стартов при занятом
  порту, fd 111→111).
  Исправлены порядок байт в A-записях (`ip_to_int()` давал обратный порядок —
  ответы были неверными) и переполнение буфера имени. Проверено на стенде
  2026-11-15: `bench/mdns_check.py` получает A `192.168.52.33` и
  `192.168.43.1`, `enable=0` → ответов нет, reconcile нового адреса ≤13 с,
  20 циклов stop/start без роста RSS, `api_check` 133/0, `ui_check` зелёный.
  Ограничения — KI-067; после OTA старый `/userdata/divinus.yaml` с
  `enable: false` оставляет публикацию выключенной до первого переключения
  (заводской шаблон `true` применяется только при создании файла).
- **mDNS publication `oko-<MAC>.local`.** `divinus` publishes
  `oko-<last four hex of the eth0 MAC>.local` with the built-in
  `tinysvcmdns` (no avahi, no new packages or daemons) and answers A records
  for every non-loopback IPv4 address (up to three). Control is live:
  `GET /api/mdns` (`enable`, `running`, `name`, `addresses`) and
  `POST /api/mdns?enable=0|1` (400 on a bad value, saved in `divinus.yaml`),
  plus a **Network → mDNS discovery** WebUI card with the name, the published
  addresses and a switch. `[mdns] enable` now defaults to `true`, so every
  device opens UDP 5353 and sends background multicast traffic after an OTA.
  The name comes from the `eth0` MAC (`wlan0` is a fallback only), so two
  boards on one LAN do not collide and the name survives a firmware update or
  a USB Wi-Fi swap. The responder starts only when an IPv4 address exists
  (`divinus` runs in `S21appinit`, before DHCP), the address set is reconciled
  every 10 s and recreated on a change (`mdnsd_set_hostname()` must not be
  called twice on a live instance); without an address publication stops, and
  a busy port 5353 does not kill the process. A failed start is retried up to
  five times with a 60 s pause, then the next attempt waits for a new address
  set or a switch change; the `tinysvcmdns` error paths
  (`create_recv_sock`/`mdnsd_start`) now close the socket and the pipe, so a
  series of failures does not leak descriptors (verified: 15 failed starts with
  the port held, fd 111→111). Fixed the A-record byte order
  (`ip_to_int()` returned the opposite order, so answers were wrong) and a
  hostname buffer overflow. Bench-verified on 2026-11-15: `bench/mdns_check.py`
  returns A `192.168.52.33` and `192.168.43.1`, `enable=0` gives no answers, a
  new address is reconciled within 13 s, 20 stop/start cycles keep RSS flat,
  `api_check` 133/0, `ui_check` passed. Limitations — KI-067; after an OTA an
  old `/userdata/divinus.yaml` with `enable: false` keeps publication off
  until the first toggle (the factory `true` applies only when the file is
  created).

- **Порог площади движения IVS `ivs.min_area`.** Ключ `[ivs]` в процентах
  кадра, `0`–`50`, по умолчанию `2`; `0` отключает фильтр. Применяется живым:
  `POST /api/ivs?min_area=N` не перезапускает IVS-канал, значение читается на
  каждое событие. В WebUI добавлено поле «Порог площади движения (%)», в
  `GET /api/ivs` — ключ `min_area`, в ответе `events` — `area`/`cause`, в
  строке журнала — причина и площадь. `bench/api_check.py` проверяет наличие
  `min_area` в GET, эхо `POST min_area=3` и поля `area`/`cause` у событий;
  прогон на стенде 2026-11-15 — 127 проверок, 0 ошибок. Живое применение
  подтверждено: `min_area` меняется без пар `IVS disabled`/`enabled` в консоли;
  порог проверен дифференциально: `min_area=50` — 10 событий за 60 с (все
  `area` ≥ 644‰), `min_area=0` — 276 событий за 60 с (включая 40–80‰).
- **IVS motion-area threshold `ivs.min_area`.** An `[ivs]` key in percent of the
  frame, `0`–`50`, default `2`; `0` disables the filter. Applied live:
  `POST /api/ivs?min_area=N` does not restart the IVS channel and the value is
  read per event. The WebUI has a "Motion area threshold (%)" field,
  `GET /api/ivs` returns `min_area`, every `events` entry has `area`/`cause`,
  and the journal row shows the cause and the area. `bench/api_check.py` now
  checks `min_area` in GET, the `POST min_area=3` echo and the `area`/`cause`
  event fields; the bench run on 2026-11-15 reported 127 checks, 0 failures.
  Live application is confirmed: `min_area` changes with no `IVS disabled`/
  `enabled` pairs on the console; the threshold was verified differentially:
  `min_area=50` — 10 events in 60 s (all `area` ≥ 644‰), `min_area=0` — 276
  events in 60 s (including 40–80‰).

- **Web-конвертер RKNN (`services/converter/`).** Отдельный профиль
  `oko-converter` (порт 8091, host-сеть) с браузерным UI и FastAPI: принимает
  ONNX с статическим входом 640×384 (NCHW `[1,3,384,640]` или NHWC
  `[1,384,640,3]`) и 20–200 калибровочных JPEG, собирает `.rknn` для `rv1106`
  официальным `rknn-toolkit2` **2.5.0** (INT8 `asymmetric_quantized-8`).
  Вендорский toolkit для rv1106 отклоняет `do_quantization=False` и
  квантование без dataset, поэтому FP16-режим не предлагается: `dtype=fp16`
  возвращает `422` с объяснением. Параметры `rknn.config` (`mean 0`,
  `std 255`) совпадают с `tools/rknn-toolkit2/convert.py`. Конверсия идёт в
  дочернем процессе одним слотом (таймаут 15 мин, отмена SIGKILL, лимит
  очереди 10), состояние задач — `output/srv/converter/jobs/<id>/` (`meta.json`,
  `model.onnx`, `calib/`, `model.rknn`, `log.txt`), после рестарта активные
  задачи помечаются `interrupted`; retention — `CONVERTER_MAX_GB` (2 ГБ),
  загрузка ONNX — `CONVERTER_MAX_UPLOAD_MIB` (256 МиБ), опциональный токен
  `OKO_CONVERTER_TOKEN` (`X-OKO-Token`). Динамический вход, размер не 640×384,
  INT8 без 20 JPEG и превышение лимита отклоняются с читаемым текстом (`422`/
  `413`). Модели 2.5.0 требуют рантайм 2.5.0 на борту (текущий — 1.6.0/2.3.2);
  проверка на rv1106 в этом изменении не выполнялась, операции с DUT не
  проводились; на хосте проверены fixture (19 098 Б) и реальный
  `yolo11n-640x384.onnx` (3 905 265 Б), включая целостность скачивания.
  Добавлен `tools_fixture/make_fixture.py` для тестового ONNX и синтетических
  кадров.
- **RKNN web converter (`services/converter/`).** A separate `oko-converter`
  profile (port 8091, host network) with a browser UI and FastAPI: accepts an
  ONNX model with a static 640×384 input (NCHW `[1,3,384,640]` or NHWC
  `[1,384,640,3]`) plus 20–200 JPEG calibration frames and produces an `.rknn`
  for `rv1106` with the official `rknn-toolkit2` **2.5.0** (INT8
  `asymmetric_quantized-8`). The vendor toolkit rejects
  `do_quantization=False` and dataset-less quantization for rv1106, so no FP16
  mode is offered: `dtype=fp16` returns `422` with an explanation. The
  `rknn.config` parameters (`mean 0`, `std 255`) match
  `tools/rknn-toolkit2/convert.py`. Conversion runs in one child-process slot
  (15 min timeout, SIGKILL cancel, queue limit 10); job state lives in
  `output/srv/converter/jobs/<id>/` (`meta.json`, `model.onnx`, `calib/`,
  `model.rknn`, `log.txt`), active jobs become `interrupted` after a restart;
  retention is `CONVERTER_MAX_GB` (2 GB), the ONNX upload cap is
  `CONVERTER_MAX_UPLOAD_MIB` (256 MiB), and an optional `OKO_CONVERTER_TOKEN`
  guards the API with `X-OKO-Token`. Dynamic inputs, non-640×384 sizes, INT8
  without 20 JPEGs and oversized uploads are rejected with a readable message
  (`422`/`413`). Models built with 2.5.0 need a 2.5.0 runtime on the board
  (current board has 1.6.0/2.3.2); no rv1106 verification or DUT operations
  were performed, while the host run covered the fixture (19 098 B) and a real
  `yolo11n-640x384.onnx` (3 905 265 B) including download integrity.
  `tools_fixture/make_fixture.py` generates a test ONNX and synthetic frames.
- **WebUI для UDP/RTP-трансляции, рамки IVS и карточка отправки событий.**
  Новый `GET/POST /api/stream` (`enable`, `udp_srcport`, скалярные
  `dest0..dest3`) с живым применением: включение/выключение — `media_start()`/
  `media_stop()`, смена адреса или порта при активном потоке — пересоздание
  клиентов; пустой слот сжимается, проверяются схемы `udp://`/`rtmp://`, а
  multicast первым при наличии второго адреса отклоняется с 400 (KI-052).
  В разделе Streams появилась карточка «UDP/RTP-трансляция». Аналитика получила
  ключ `ivs.boxes` и отдельный 2BPP-регион (handle 13, слой 5): синие рамки
  движения рисуются поверх главного потока без перезапуска IVS, для OD без
  прямоугольников показывается зелёный маркер; регион YOLO не затронут.
  Ключ `ivs.boxes` принудительно отключён: на стенде включение 2BPP-региона
  вешает MPP/VENC и всю плату, в том числе без YOLO (`dup mClientFd failed`
  → SIGSEGV, KI-063); API возвращает 400, гейт не стартует IVS с
  `boxes: true`, поток IVS всегда выключает рамки. IVS-детекция без рамок и
  `act_osd` остаются доступны.
  В разделе Events — карточка «Отправка событий (HTTP)»: `[http_post]`
  (`enable`, `host`, `url`, `login`, `password`, `width`, `height`, `interval`,
  `qfactor`, новый `receiver_url`), живой `GET/POST /api/http_post`, ссылка на
  веб-приёмник (из `receiver_url` или `host:8090`), раскрывающееся описание
  исходящего API кадров/роликов и эндпоинтов `services/events`; карточка
  «Ролики событий» управляет `enable`/`pre_seconds`/`post_seconds`/`max_bytes`
  через расширенный `POST /api/clip` и показывает счётчики доставки.
- **WebUI for UDP/RTP streaming, IVS boxes and the event-upload card.**
  New `GET/POST /api/stream` (`enable`, `udp_srcport`, scalar
  `dest0..dest3`) applied live: enabling/disabling calls `media_start()`/
  `media_stop()`, an address or port change with an active stream recreates the
  clients; empty slots are compacted, `udp://`/`rtmp://` schemes are validated,
  and a multicast first destination with a second address is rejected with 400
  (KI-052). The Streams section has a new "UDP/RTP streaming" card. Analytics
  gained `ivs.boxes` and a separate 2BPP region (handle 13, layer 5): blue
  motion rectangles are drawn over the main stream without restarting IVS, and
  OD without rectangles shows a green marker; the YOLO region is untouched.
  The `ivs.boxes` key is hard-disabled: on the bench attaching the 2BPP region
  hangs the MPP/VENC path and the whole board, including without YOLO (`dup
  mClientFd failed` → SIGSEGV, KI-063); the API returns 400, the gate refuses
  to start IVS with `boxes: true`, and the IVS thread always disables the
  overlay. IVS detection without boxes and `act_osd` remain available. The
  Events section has an "Event upload (HTTP)" card for `[http_post]` (`enable`,
  `host`, `url`, `login`, `password`, `width`, `height`, `interval`, `qfactor`
  and the new `receiver_url`), a live `GET/POST /api/http_post`, a receiver link
  (from `receiver_url` or `host:8090`), a collapsible description of the
  outgoing frame/clip API and the `services/events` endpoints; the "Event clips"
  card drives `enable`/`pre_seconds`/`post_seconds`/`max_bytes` through the
  extended `POST /api/clip` and shows delivery counters.
- **`KI-070` — устаревший учёт VPSS-канала 3 после переноса IVS на VI chn2.**
  `gating.c:116` отказывает IVS при занятом chn3, `media.c:462-467` резервирует
  chn3 при `ivs.enable`, а при полном пуле `take_next_free_channel()`
  возвращает −1, и `media.c:750-754`/`jpeg.c:24-29` не выходят, а пишут в
  `chnState[-1]`. IVS при этом работает на VI chn2 (`rk_hal.c:2082`), поэтому
  резерв и гейт относятся к старой топологии. Обход — не включать все четыре
  VENC-потока вместе с IVS; лечение — правка кода отдельной задачей.
- **`KI-070` — stale VPSS channel 3 accounting after IVS moved to VI chn2.**
  `gating.c:116` refuses IVS when chn3 is taken, `media.c:462-467` reserves chn3
  with `ivs.enable`, and with a full pool `take_next_free_channel()` returns −1
  while `media.c:750-754`/`jpeg.c:24-29` do not bail out and write to
  `chnState[-1]`. IVS itself runs on VI chn2 (`rk_hal.c:2082`), so the
  reservation and the gate follow the old topology. Workaround: do not run all
  four VENC streams together with IVS; the fix is a separate code change.

- **Сон привода STM8 по тишине I2C (Active-halt + AWU).** Прошивка
  `stm8-motor` 2.2.0 (тестовая плата 2.3.0) после 3 с без обращений к
  `0x60` уходит в цепочку слайсов Active-halt; ядро будят совпадение
  адреса I2C и AWU, `main` кормит IWDG на каждом пробуждении. Слайс —
  51200 периодов LSI (400 мс при 128 кГц) = 80 % окна IWDG `250*256`,
  поэтому отношение не зависит от разброса LSI; TIM4 перед HALT
  останавливается и его `UIF` сбрасывается (Table 11 паспорта STM8S003F3:
  TIM4 не будит ни из Halt, ни из Active-halt). Перед первым слайсом
  позиции принудительно сохраняются в EEPROM (не во время RUNNING), LED
  гаснет. `system_ticks` во сне не идёт — 2-секундный self-reset I2C и
  прочие тиковые таймауты замирают. Чистая логика —
  `src/motor/sleep_logic.h`, хост-тест `tests/test_sleep_logic.c`.
  Новые команды: `0x0A GET_SLEEP` (8 байт: режим, состояние, слайс/10 мс,
  резерв, входы BE, пробуждения I2C/AWU) и `0x0B SET_SLEEP`
  (`[cmd][mode][idle_s]`, 0 — дефолт 3 с). `oko-motor sleep on|off [S]` /
  `sleep show` с проверкой версии 2.2.0+; в продуктовой сборке режим
  выключен по умолчанию, на тестовой — включён (`-DSLEEP_DEFAULT_ON`).
  `tests/hardware_test.sh`: секция 12 (сон/пробуждение, 100 циклов,
  счётчики, движение после пробуждения, 30-минутный soak).
- **STM8 focus-motor sleep over I2C silence (Active-halt + AWU).**
  `stm8-motor` firmware 2.2.0 (test board 2.3.0) enters a chain of
  Active-halt slices after 3 s without traffic to `0x60`; the I2C address
  match and the AWU wake the core, and `main` reloads the IWDG on every
  wake-up. A slice is 51200 LSI periods (400 ms at 128 kHz) = 80 % of the
  `250*256` IWDG window, so the ratio is independent of the LSI tolerance.
  TIM4 is stopped and its `UIF` cleared before HALT (STM8S003F3 datasheet
  Table 11: TIM4 wakes from neither Halt nor Active-halt). Before the first
  slice the positions are force-saved to EEPROM (never while RUNNING) and
  the LED is turned off. `system_ticks` freezes during Halt, so the 2 s I2C
  self-reset and every tick-based timeout freeze with it. Pure decision
  logic is `src/motor/sleep_logic.h` with the `tests/test_sleep_logic.c`
  host test. New commands: `0x0A GET_SLEEP` (8 bytes: mode, state, slice in
  10 ms units, reserved, entries BE, I2C/AWU wake counters) and `0x0B
  SET_SLEEP` (`[cmd][mode][idle_s]`, 0 = default 3 s). `oko-motor sleep
  on|off [S]` / `sleep show` require 2.2.0+; the production build keeps the
  mode off, the test board defaults it on (`-DSLEEP_DEFAULT_ON`).
  `tests/hardware_test.sh` gained section 12 (sleep/wake, 100 wake cycles,
  counters, motion after wake, a 30-minute soak).
- **`[motor] sleep_enable`/`sleep_idle_s` и поля сна в `/api/firmware`.**
  `divinus` включает сон STM8 перед `write_power_state()` (suspend) и
  выключает после resume с одним контрольным чтением версии; в
  `/api/firmware.stm8` добавлены `sleep_mode`, `sleep_state`,
  `sleep_entries`, `sleep_wake_i2c`, `sleep_wake_awu`. Дефолт
  `sleep_enable: false`.
- **`[motor] sleep_enable`/`sleep_idle_s` and sleep fields in `/api/firmware`.**
  `divinus` enables the STM8 sleep before `write_power_state()` (suspend)
  and disables it after resume with one version read; `/api/firmware.stm8`
  gained `sleep_mode`, `sleep_state`, `sleep_entries`, `sleep_wake_i2c` and
  `sleep_wake_awu`. Default `sleep_enable: false`.

### Исправлено

- **SFC (KI-071): устойчивость к таймауту DMA и к suspend/resume.**
  `drivers/spi/spi-rockchip-sfc.c`: при таймауте DMA контроллер
  сбрасывается и инициализируется заново (раньше оставался взведённым, и
  все последующие операции падали с `wait sfc idle timeout`, уводя UBIFS в
  read-only); при resume сброс выполняется до инициализации; при suspend
  маскируются прерывания и блок выключается, чтобы DMA не оставался
  активным на момент отключения тактов.  Ядро собирается
  (`./build.sh kernel`), стендовая проверка отложена: UART-мост не отдаёт
  вывод DUT (KI-078), netboot недоступен.  KI-074 (gic400_restore)
  подтверждён в дереве без изменений.
- **SFC (KI-071): DMA-timeout and suspend/resume robustness.**
  `drivers/spi/spi-rockchip-sfc.c`: a DMA timeout now soft-resets and
  re-initialises the controller (it used to stay armed, so every later
  transfer failed with `wait sfc idle timeout` and drove UBIFS read-only); a
  system resume resets the block before initialising it; suspend masks the
  interrupts and disables the block so no DMA is left running when the clocks
  are gated.  The kernel builds (`./build.sh kernel`); bench validation is
  deferred because the UART bridge no longer delivers DUT output (KI-078) and
  netboot is unavailable.  KI-074 (gic400_restore) is confirmed in the tree
  unchanged.
- **STM8: I2C-обновление больше не «проходит» вхолостую; загрузчик 1.1.0.**
  KI-075 разобран до причины: IAP-стирание блока на стендовом STM8S003F3P6
  не завершается вовсе (ядро стопорится, при выключенном IWDG контроллер
  молчит >16 с), при этом word-программирование и SWIM/ICP работают —
  дефект экземпляра (второй отказ того же чипа — неработающий AWU, KI-077).
  Загрузчик: стирание вынесено в RAM-заглушку (`src/boot/erase_stub.asm`),
  EOP сбрасывается перед каждой операцией, `WR_PG_DIS`/таймаут возвращаются
  как `BOOT_ST_FLASH_ERR`, ответ на write-команды стал двухбайтным
  `[status][эхо команды]`.  `oko-motor flash` сверяет эхо и считает
  расхождение версии ошибкой (`RC=1`) с явным сообщением.  Документация:
  `KNOWN_ISSUES.md` (KI-075 переписан), `PROTOCOL.md`/`PROTOCOL.ru.md`,
  `TESTBENCH.md` §7C, `project/app/stm8-motor/CHANGELOG.md`.
- **STM8: the I2C update no longer "succeeds" without writing; bootloader
  1.1.0.** KI-075 is explained down to the cause: the IAP block erase of the
  bench STM8S003F3P6 never completes (the core stalls; with the IWDG disabled
  the controller stays silent for more than 16 s), while word programming and
  SWIM/ICP programming work - a sample defect (the same chip also has a dead
  AWU, KI-077).  Bootloader: the erase now runs from a RAM stub
  (`src/boot/erase_stub.asm`), EOP is cleared before every operation,
  `WR_PG_DIS`/timeouts are returned as `BOOT_ST_FLASH_ERR`, and write-command
  answers are two bytes, `[status][command echo]`.  `oko-motor flash` checks
  the echo and treats a version mismatch as an error (`RC=1`) with an explicit
  message.  Documentation: `KNOWN_ISSUES.md` (KI-075 rewritten),
  `PROTOCOL.md`/`PROTOCOL.ru.md`, `TESTBENCH.md` §7C,
  `project/app/stm8-motor/CHANGELOG.md`.
- **Диагностика resume: `pm_test` и многострочный `--uboot-command`.**
  Добавлен невключаемый по умолчанию фрагмент ядра
  `rv1106-pm-debug.config` (`CONFIG_PM_DEBUG`/`CONFIG_PM_TEST_SUSPEND`) — он
  даёт `/sys/power/pm_test` для лестницы
  freezer→devices→platform→processors→core. `bench/netboot.py` принимает
  повторяемый `--uboot-command` и выполняет команды в одной консольной
  сессии (нужно для `setenv`+`saveenv`+`boot` подряд). Стенд 2026-11-17:
  лестница проходит все уровни, в том числе с политикой divinus
  (`pm_test=core`: циклы идут, плата жива), значит стоп KI-074 — в реальном
  входе в сон, а не в PM-ядре; детали в `KNOWN_ISSUES.md` KI-074.
- **Resume diagnosis: `pm_test` and a multi-command `--uboot-command`.**
  New opt-in kernel fragment `rv1106-pm-debug.config`
  (`CONFIG_PM_DEBUG`/`CONFIG_PM_TEST_SUSPEND`) that provides
  `/sys/power/pm_test` for the freezer→devices→platform→processors→core
  ladder. `bench/netboot.py` now accepts a repeated `--uboot-command` and
  runs the commands in one console session (needed for
  `setenv`+`saveenv`+`boot` in a row). Bench 2026-11-17: every ladder level
  passes, including with the divinus policy (`pm_test=core`: the cycles run
  and the board stays alive), so the KI-074 stop is in the real sleep entry,
  not in the PM core; see `KNOWN_ISSUES.md` KI-074.
- **AOV: управляемое решение MCU (`wakeup_param`) и сброс pending IRQ.**
  Сборка инициализирует сегмент `wakeup_param` в `meta.img`
  (`RK_META_AOV_AE_WAKEUP_MODE`, `RK_META_AOV_MCU_MAX_RUN_COUNT`,
  `tools/native/patch_meta_aov.py`): 0 — решение по AE, 1 — принудительное
  движение, 2 — принудительная тишина. Стенд 2026-11-17: `ae_mode=2` —
  `md_flag is 0`, AP_SUSPEND, 0 resume (AP спит); `ae_mode=1` —
  `md_flag is 1`, `aov motion`, resume. В ядре добавлен
  `gic_irq_clear_pending()` после ack mailbox: pending IRQ будил AP сразу и
  quiet-цикл превращался в плотный цикл ~370 мс.
- **AOV: controllable MCU decision (`wakeup_param`) and IRQ pending clear.**
  The build now initializes the `wakeup_param` segment in `meta.img`
  (`RK_META_AOV_AE_WAKEUP_MODE`, `RK_META_AOV_MCU_MAX_RUN_COUNT`,
  `tools/native/patch_meta_aov.py`): 0 — AE decides, 1 — force motion, 2 —
  force quiet. Bench 2026-11-17: `ae_mode=2` — `md_flag is 0`, AP_SUSPEND,
  zero resumes (the AP stays asleep); `ae_mode=1` — `md_flag is 1`,
  `aov motion`, resume. The kernel now calls `gic_irq_clear_pending()` after
  the mailbox ack: the pending IRQ woke the AP immediately and turned the
  quiet cycle into a tight ~370 ms loop.
- **AOV: handshake MCU заработал (KI-072).** Две правки: в divinus —
  режим `[sleep] aov_keep_isp` (AOV-профиль не вызывает
  `sdk_stop()`/`media_stop()`, CSI/ISP остаются поднятыми для MCU, есть в
  API и yaml); в ядре — `hpmcu_fast_wkup()` больше не перечитывает статус
  mailbox после ack (флаг `got`), из-за чего любой здоровый ответ
  трактовался как `-h-timeout-`. Стенд 2026-11-17: 11 полных циклов —
  `[STREAM]: … md_flag is 1, tick:172..204` → `-h-mbox-` →
  `[sleep] resumed (… aov motion)`. Остаётся тюнинг MCU (`md_flag` в
  статичной сцене, `wakeup_param` в meta) и проверка стрима после
  AOV-resume.
- **AOV: the MCU handshake now works (KI-072).** Two changes: divinus gets a
  `[sleep] aov_keep_isp` mode (the AOV profile skips `sdk_stop()`/
  `media_stop()` so the CSI/ISP stay up for the MCU; exposed in the API and
  the yaml); the kernel `hpmcu_fast_wkup()` no longer re-reads the mailbox
  status after acking it (a `got` flag), which used to report every healthy
  answer as `-h-timeout-`. Bench 2026-11-17: 11 complete cycles —
  `[STREAM]: … md_flag is 1, tick:172..204` → `-h-mbox-` →
  `[sleep] resumed (… aov motion)`. Still open: MCU tuning (`md_flag` in a
  static scene, `wakeup_param` in the meta) and streaming after an AOV
  resume.
- **AOV DTS: `aov-reserved` на `&rkcif_mipi_lvds`.** Свойство взято из
  вендорского AOV-эталона `rv1106g-evb2-v12-aov-spi-nor.dts` (резерв
  CIF/ISP под MCU на время AP-suspend). На стенде 2026-11-17 поведение не
  изменилось: MCU находит `isp_0`, но кадров в окне mailbox нет, потому что
  divinus останавливает ISP/CSI перед `mem`; вывод записан в KI-072.
- **AOV DTS: `aov-reserved` on `&rkcif_mipi_lvds`.** Taken from the vendor
  AOV reference `rv1106g-evb2-v12-aov-spi-nor.dts` (keeps the CIF/ISP
  reserved for the MCU during AP suspend). On the bench 2026-11-17 the
  behaviour did not change: the MCU finds `isp_0` but gets no frames in the
  mailbox window because divinus stops the ISP/CSI before `mem`; the
  conclusion is recorded in KI-072.
- **MCU: консоль uart2 и окно mailbox 6 с (диагностика AOV).** В
  `oko-wakeup-sc230ai-ADC/defconfig` включены `CONFIG_RT_USING_UART` и
  `CONFIG_RT_USING_UART2` (общий с AP отладочный uart2) — по логу видно,
  что MCU стартует и доходит до `[STREAM]: the device to be found:isp_0`,
  но кадр/решение получает только после resume AP (его ISP). Окно
  `RV1106_HPMCU_FAST_WKUP_TIMEOUT` увеличено с 2000 до 6000 мс по
  измеренному времени до решения MCU (~4.2 с); handshake это не
  исправляет — MCU-прошивке нужен порт battery-ipc AOV (свой подъём
  ISP/сенсора).
- **MCU: uart2 console and a 6 s mailbox window (AOV diagnosis).** The
  `oko-wakeup-sc230ai-ADC/defconfig` now enables `CONFIG_RT_USING_UART` and
  `CONFIG_RT_USING_UART2` (the shared AP debug uart2), which shows the MCU
  booting and reaching `[STREAM]: the device to be found:isp_0`, but it only
  gets a frame and decides after the AP resumed (its ISP). The
  `RV1106_HPMCU_FAST_WKUP_TIMEOUT` window is raised from 2000 to 6000 ms
  based on the measured time to the MCU decision (~4.2 s); this does not fix
  the handshake — the MCU firmware needs the battery-ipc AOV port (its own
  ISP/sensor bring-up).
- **divinus: результат AOV-handshake в журнале resume.** После resume
  политика читает `aov_state` до разоружения прокси и пишет его в строку
  `[sleep] resumed (EC wake src …, last event …, aov …)`. Стенд
  2026-11-17: `aov timeout` — подтверждает 2-секундный таймаут mailbox
  (маркеры `-h-wait-`/`-h-mbox-`/`-h-timeout-` печатаются мимо printk и
  видны только на живом UART). Это даёт наблюдаемость handshake без
  консоли.
- **divinus: AOV handshake outcome in the resume log.** After a resume the
  policy reads `aov_state` before disarming the proxy and prints it in
  `[sleep] resumed (EC wake src …, last event …, aov …)`. Bench
  2026-11-17: `aov timeout`, confirming the 2 s mailbox timeout (the
  `-h-wait-`/`-h-mbox-`/`-h-timeout-` markers bypass printk and are only
  visible on a live UART). This makes the handshake observable without a
  console.
- **PM: восстановление GIC-distributor на resume (KI-074).** В
  `arch/arm/mach-rockchip/rv1106_pm.c` функция `gic400_restore()` при
  `CONFIG_RV1106_HPMCU_FAST_WAKEUP` писала только `GIC_DIST_CTRL` и
  оставляла контекст distributor'а AOV-MCU; если MCU простаивает (`aov off`)
  или не отвечает (`-h-timeout-`), прерывания после resume не доставляются и
  AP виснет в `suspend_thaw_processes()` (последний вывод —
  `OOM killer enabled.`). Теперь контекст восстанавливается всегда, как это
  уже делает вендорский timeout-путь. Стенд 2026-11-17: AOV-ядро (0x208000,
  gzip-FIT) с `aov_mcu=0` прошло ~15 циклов
  (`[sleep] resumed (EC wake src 4)`, `Restarting tasks ... done.`); до
  патча первый же resume зависал. С `aov_mcu=1` политика пишет
  `aov armed`, и ядро дважды проходит `PM: suspend entry` →
  `Restarting tasks ... done.` → `PM: suspend exit` — AP не виснет; ответ
  MCU по mailbox не подтверждён (`-h-*` печатаются прямо в UART).
  Последующее зависание после ~15 циклов — это SFC/NAND (KI-071), не PM. Артефакты: `output/bench/aov-test/console-gicfix-run1.log`,
  `uar0-gicfix-death.log`.
- **PM: restore the GIC distributor on resume (KI-074).** In
  `arch/arm/mach-rockchip/rv1106_pm.c` the `gic400_restore()` function wrote
  only `GIC_DIST_CTRL` when `CONFIG_RV1106_HPMCU_FAST_WAKEUP` was set and
  left the distributor context to the AOV MCU; with the MCU idle (`aov off`)
  or not answering (`-h-timeout-`) no interrupts are delivered after resume
  and the AP hangs in `suspend_thaw_processes()` (the last output is
  `OOM killer enabled.`). The context is now always restored, exactly like
  the vendor timeout path already does. Bench 2026-11-17: the AOV kernel
  (0x208000, gzip FIT) with `aov_mcu=0` ran ~15 cycles
  (`[sleep] resumed (EC wake src 4)`, `Restarting tasks ... done.`); before
  the patch the first resume already hung. With `aov_mcu=1` the policy logs
  `aov armed` and the kernel completes `PM: suspend entry` →
  `Restarting tasks ... done.` → `PM: suspend exit` twice, so the AP no
  longer hangs; the MCU mailbox answer is not confirmed (the `-h-*` strings
  go straight to the UART). The later hang after ~15 cycles is SFC/NAND
  (KI-071), not PM. Artifacts: `output/bench/aov-test/console-gicfix-run1.log`,
  `uar0-gicfix-death.log`.
- **I2C3: паника `rk3x_i2c_irq` при циклах `[sleep]` (KI-073) — патч.**
  В вендорский `drivers/i2c/busses/i2c-rk3x.c` добавлен guard после ack
  NAK: `rk3x_i2c_stop()` обнуляет `i2c->msg` для последнего сообщения, но
  контроллер продолжает присылать NAK, и обработчик разыменовывал
  `i2c->msg->flags` (NULL + 2) — `Oops - BUG: 17` в прерывании и
  `Kernel panic` на I2C3 (`ff470000.i2c`, STM32 EC). Теперь поздний NAK
  подтверждается и передача завершается через STOP-путь. Стенд
  2026-11-17: три прогона suspend без паники (до патча — на первом-втором
  цикле). Остаточное зависание resume — отдельная KI-074; на прошивке без
  патча оно тоже воспроизводится (проверено контрольной прошивкой
  10:02). Артефакты: `output/bench/aov-test/console-nakguard-run1.log`,
  `console-prepatch-control.log`.
- **I2C3: `rk3x_i2c_irq` panic during `[sleep]` cycles (KI-073) — patched.**
  The vendor `drivers/i2c/busses/i2c-rk3x.c` now has a guard after the NAK
  ack: `rk3x_i2c_stop()` clears `i2c->msg` for the final message while the
  controller keeps sending NAKs, and the handler dereferenced
  `i2c->msg->flags` (NULL + 2) — `Oops - BUG: 17` in interrupt context and a
  `Kernel panic` on I2C3 (`ff470000.i2c`, STM32 EC). A late NAK is now
  acknowledged and the transfer finishes through the STOP path. Bench
  2026-11-17: three suspend runs without a panic (it hit on the first or
  second cycle before). The remaining resume hang is a separate KI-074 and
  reproduces on the build without the patch (checked with the 10:02 control
  image). Artifacts: `output/bench/aov-test/console-nakguard-run1.log`,
  `console-prepatch-control.log`.
- **AOV: окно ядра/MCU исправлено FIT-ом `0x208000`, загрузка проверена.**
  Вендорский `textofs-$(CONFIG_RV1106_HPMCU_FAST_WAKEUP) := 0x00208000` в
  `arch/arm/Makefile` восстановлен (был закомментирован ночью 2026-11-17), а
  `boot.img` профиля `-AOV` теперь собирается из `Image.gz` новым
  `sysdrv/source/kernel/boot-aov.its` (`compression = "gzip"`,
  `load = entry = 0x208000`) в `__BUILD_KERNEL_FIT` (`project/build.sh`):
  U-Boot распаковывает ядро сразу в 0x208000, как в вендорском
  thunder-boot-образе. Связка «zImage + загрузка по 0x8000» с этим textofs
  до userspace не доходит — проверено на стенде. Стенд 2026-11-17:
  `Uncompressing GZIP Kernel Image ... to 0x00208000`, `_text`/`_etext`
  = 0xB0208000/0xB06CDD70, предупреждение об окне `rtos@40000` исчезло,
  1M-раскладка и `aov_available=true` подтверждены (`/proc/mtd`, `/api/status`).
  Suspend с `aov_mcu=1` доходит до `-h-wait-` → `-h-timeout-` (2000 мс):
  MCU не отвечает по mailbox и AP возвращается по таймауту; `aov_mcu=0`
  (PMU-only) циклится штатно. Остаются KI-072 (handshake MCU) и KI-073
  (паника `rk3x_i2c` на I2C3). Устаревшие комментарии про «meta at 0x800000»
  в `sleep_policy.c` и `build.sh` исправлены на 0x1000000.
- **AOV: kernel/MCU window fixed with the `0x208000` FIT, boot verified.**
  The vendor `textofs-$(CONFIG_RV1106_HPMCU_FAST_WAKEUP) := 0x00208000` in
  `arch/arm/Makefile` is restored (it was commented out overnight on
  2026-11-17), and the `-AOV` profile `boot.img` is now built from `Image.gz`
  with the new `sysdrv/source/kernel/boot-aov.its`
  (`compression = "gzip"`, `load = entry = 0x208000`) by
  `__BUILD_KERNEL_FIT` (`project/build.sh`): U-Boot unpacks the kernel
  straight into 0x208000, exactly like the vendor thunder-boot image. The
  stock "zImage loaded at 0x8000" combination does not reach userspace with
  this textofs — verified on the bench. Bench 2026-11-17:
  `Uncompressing GZIP Kernel Image ... to 0x00208000`, `_text`/`_etext`
  = 0xB0208000/0xB06CDD70, the `rtos@40000` overlap warning is gone, and the
  1M layout plus `aov_available=true` are confirmed (`/proc/mtd`,
  `/api/status`). A suspend with `aov_mcu=1` reaches `-h-wait-` →
  `-h-timeout-` (2000 ms): the MCU does not answer the mailbox and the AP
  returns on the timeout; `aov_mcu=0` (PMU-only) cycles cleanly. KI-072 (MCU
  handshake) and KI-073 (`rk3x_i2c` panic on I2C3) remain open. Stale
  "meta at 0x800000" comments in `sleep_policy.c` and `build.sh` are fixed to
  0x1000000.
- **Bench: управление питанием и надёжный вход в U-Boot.** Новый
  `bench/power.py` (`off`/`on`/`cycle`/`reset` через ESP32-мост, вход по
  cookie, обязательный logout: мост держит не больше трёх сессий).
  `tftp_flash.py` получил `--power-cycle` (холодный цикл питания перед
  прошивкой — нужен после зависшего HPMCU) и SSH-перезагрузку как запасной
  путь, когда консольный логин недоступен; `netboot.py` ждёт приглашение
  `Password:` перед вводом пароля (раньше пароль уходил слишком рано), а
  `serial_exec.py --listen N` зеркалит консоль N секунд без логина — так
  ловится окно простоя перед suspend.
- **Bench: power control and a reliable way into U-Boot.** New
  `bench/power.py` (`off`/`on`/`cycle`/`reset` through the ESP32 bridge,
  cookie login and a mandatory logout because the bridge keeps at most three
  sessions). `tftp_flash.py` gained `--power-cycle` (a cold power cycle
  before flashing, required after a stuck HPMCU) and an SSH reboot fallback
  for when the console login is unusable; `netboot.py` now waits for the
  `Password:` prompt before typing the password (it used to be sent too
  early), and `serial_exec.py --listen N` mirrors the console for N seconds
  without logging in, which captures the idle window before a suspend.

- **Ревью сна камеры: состояние EC при сбое suspend и единицы TB-таймера.**
  Если запись `mem` в `/sys/power/state` не удалась, `sleep_policy` теперь
  возвращает EC в `AWAKE`, а не оставляет `SUSPENDING`. Демон `oko-ec`
  выставляет признак отправленного `AWAKE` только после успешной записи
  (до 5 попыток), чтобы сбой шины не оставлял `host_state` спящим. В
  замороженной TB-линии `pmu_timeout_hz` исправлен с 32000 на измеренные
  **32 тика/с** (`rkipc-300w.ini` и `aov_sleep.c`), иначе `wake_interval_s=10`
  давал бы ~10000 с. Стендовый `POST /api/sleep?motion=1` теперь доступен
  только при `OKO_BENCH_API=1`; GET-статус сна работает всегда.
- **Camera-sleep review: EC state on suspend failure and TB timer units.**
  When the `mem` write to `/sys/power/state` fails, `sleep_policy` now puts
  the EC back to `AWAKE` instead of leaving `SUSPENDING`. The `oko-ec` daemon
  marks the `AWAKE` write as done only after success (up to 5 attempts), so
  a bus failure no longer leaves `host_state` sleeping. In the frozen TB line
  `pmu_timeout_hz` is corrected from 32000 to the measured **32 ticks/s**
  (`rkipc-300w.ini` and `aov_sleep.c`); otherwise `wake_interval_s=10` meant
  ~10000 s. The bench `POST /api/sleep?motion=1` is now gated by
  `OKO_BENCH_API=1`; the GET sleep status is always available.
- **IVS: ложные события при отсутствии движения.** HAL не пробрасывал площадь
  движения, поэтому любой прямоугольник MD или флаг OD давал событие, и шум,
  пыль и мелкая разница пикселей запускали журнал, снимок, клип и OSD.
  `hal_ivs_result` получил `mdSquare`/`odFlag`, а `ivs_events` — порог
  `ivs.min_area`: MD-результаты ниже него не записываются и не запускают
  действия, OD-события порог не скрывает. В событие добавлены `area`
  (промилле кадра) и `cause` (`md`/`od`); `HAL_INFO` выводит в консоль `area`
  и сырой `mdSquare` (KI-065). Стенд 2026-11-15: `min_area=50` — 10 событий за
  60 с, все `area` ≥ 644‰; `min_area=0` — 276 событий, включая 40–80‰; смена
  `min_area` не рестартит канал. Гарантированно статичной сцены на стенде нет,
  порог проверен дифференциально. OD отдельно не проверялся.
- **IVS: false events without motion.** The HAL did not propagate the motion
  area, so any MD rectangle or OD flag counted as an event and noise, dust and
  small pixel differences triggered the journal, snapshot, clip and OSD.
  `hal_ivs_result` gained `mdSquare`/`odFlag` and `ivs_events` gained the
  `ivs.min_area` threshold: MD results below it are neither recorded nor acted
  on, while OD hits are never hidden by it. Events now carry `area` (permille of
  the frame) and `cause` (`md`/`od`); `HAL_INFO` prints `area` and the raw
  `mdSquare` to the console (KI-065). Bench on 2026-11-15: `min_area=50` — 10
  events in 60 s, all `area` ≥ 644‰; `min_area=0` — 276 events including
  40–80‰; changing `min_area` does not restart the channel. The bench has no
  guaranteed static scene, so the threshold was verified differentially. OD was
  not verified separately.

- **IVS получал кадры через VPSS, а результат без движения не освобождался.**
  Привязка `VPSS chn3 → IVS` создавала канал, но `RK_MPI_IVS_GetResults` всегда
  возвращал таймаут, поэтому событий не было вообще. IVS переведён на
  выделенный VI-канал 2 (`ivs.width`×`ivs.height`, как `video.2` в rkipc),
  `rk_ivs_get_results` отдаёт реальный `mdSquare`, рестарт канала занимает
  0,2 с. Второй дефект: результат с `rectNum == 0` и без `od.flag` не
  освобождался, и один MD-буфер на результат утекал — процесс доходил до OOM
  (153 МиБ) за минуты; освобождение добавлено в ветку «нет события», RSS
  стабилен 12,4 МиБ (91 событие за 2 мин). KI-065 закрыт.
  Проверено дополнительно: статичная сцена 45 с при `min_area=2` — 0 событий;
  `act_http_post` — `PUT /ingest/frame` 200; `act_record` — клип 15,03 с HEVC
  1920×1080; 15-мин soak без роста памяти (RSS 12,4→15,8 МиБ за 40 мин);
  `min_area` переживает `POST /api/cmd?save` и SIGHUP; UI-поле 0–50
  проверяется в `bench/ui_check.py`; IVS и YOLO `vi0` работают вместе
  (RSS 23,6 МиБ). KI-066 после смены топологии не воспроизводится; OD не
  проверен — в кадре не было объекта.
- **IVS received frames through VPSS and never released motion-less results.**
  The `VPSS chn3 → IVS` bind created the channel but `RK_MPI_IVS_GetResults`
  always timed out, so no events were produced at all. IVS now uses a dedicated
  VI channel 2 (`ivs.width`×`ivs.height`, like rkipc's `video.2`),
  `rk_ivs_get_results` reports a real `mdSquare`, and a channel restart takes
  0.2 s. A second defect: a result with `rectNum == 0` and no `od.flag` was
  never released, leaking one MD buffer per result and driving the process to
  OOM (153 MiB) within minutes; the release was added to the "no event" branch
  and RSS now stays at 12.4 MiB (91 events over 2 min). KI-065 is closed.
  Verified additionally: a 45 s static scene with `min_area=2` produced
  0 events; `act_http_post` — `PUT /ingest/frame` 200; `act_record` — a
  15.03 s HEVC 1920×1080 clip; a 15 min soak without memory growth (RSS
  12.4→15.8 MiB over 40 min); `min_area` survives `POST /api/cmd?save` and
  SIGHUP; the 0–50 UI field is checked by `bench/ui_check.py`; IVS and YOLO
  `vi0` run together (RSS 23.6 MiB). KI-066 is not reproducible after the
  topology change; OD is unverified — no object was in the frame.

- **SIGHUP-рестарт больше не утекает дескрипторами и CMA.** Вендорский
  медиастек не закрывает часть буферов к моменту `execvp()`, а `execvp`
  наследует дескрипторы без `FD_CLOEXEC`: каждый рестарт оставлял ~5
  `/dmabuf:vmpi` и 1–2 `/dev/rga` (при включённом IVS), ~2 без IVS. За ~40
  рестартов накопилось 235 `dmabuf`-дескрипторов, CMA выросла с 44,2 до
  57,7 МиБ — путь к исчерпанию fd/CMA и вероятная причина исторических
  отказов MPP после серии перезапусков. Перед `execvp()` все дескрипторы с 3
  помечаются `FD_CLOEXEC`, в консоль печатается их число (KI-069). Стенд:
  5 рестартов — fd 116→115, CMA без изменений; 20 рестартов — без роста fd и
  ошибок ядра.
- **A SIGHUP restart no longer leaks descriptors or CMA.** The vendor media
  stack does not close some buffers before `execvp()`, and `execvp` inherits
  descriptors without `FD_CLOEXEC`: every restart left about five
  `/dmabuf:vmpi` and one or two `/dev/rga` descriptors with IVS enabled (about
  two without IVS). About 40 restarts accumulated 235 dmabuf descriptors and
  CMA grew from 44.2 to 57.7 MiB — a path to fd/CMA exhaustion and the likely
  cause of the historical MPP failures after a series of application restarts.
  Before `execvp()` every descriptor from 3 is marked `FD_CLOEXEC` and the
  count is printed to the console (KI-069). Bench: 5 restarts — fd 116→115,
  CMA unchanged; 20 restarts — no fd growth and no kernel errors.
- **Живое выключение `http_post` больше не вешает сервер.** Поток отправки
  крутился на `while (keepRunning)`, а `http_post_stop()` делал `join`, поэтому
  `enable=false` из WebUI блокировал обработчик навсегда; добавлен флаг
  `httpPostRun`, старт стал идемпотентным, остановка будит цикл не позднее
  одной секунды. Поведение при загрузке и завершении не изменилось.
- **Live `http_post` disable no longer hangs the web server.** The sender
  thread looped on `while (keepRunning)` while `http_post_stop()` joined it, so
  `enable=false` from the WebUI blocked the request handler forever; a
  `httpPostRun` flag was added, start is idempotent, and stop wakes the loop
  within one second. Boot and shutdown behaviour is unchanged.
- **Стек HTTP-сервера: новые обработчики переполняли 64 КиБ.** Кадр
  `respond_request()` вырос до ~31 КиБ (локальные буферы `/api/stream` и
  `/api/http_post`), и при `web_server_thread_stack_size: 65536` из yaml
  `send_file()`/`send_stream_file()` уходили в SIGSEGV: статика
  `//etc/passwd` и `/api/cmd?export` роняли процесс. Сервер теперь поднимает
  стек до 128 КиБ и пишет предупреждение о слишком малом значении.
- **HTTP server stack: the new handlers overflowed 64 KiB.** The
  `respond_request()` frame grew to ~31 KiB (local buffers of `/api/stream`
  and `/api/http_post`), and with `web_server_thread_stack_size: 65536` from
  the yaml `send_file()`/`send_stream_file()` hit SIGSEGV: the static
  `//etc/passwd` path and `/api/cmd?export` killed the process. The server now
  raises the stack to 128 KiB and warns about a too-small value.

- **Хостовый STM32-gate приведён к слоту C011.** `flash.c` ограничивает
  образ STM32 20 КиБ (было 120 КиБ для CBT6); обновлены комментарий и
  текст ошибки. Документация стенда: `TESTBENCH.md` §2 описывает EC
  STM32C011F6P6 (`0x42`, proto v3) и дату §1; `FEATURES_VERIFIED.md`,
  `FEATURES.md` и `TO_DO.md` сохраняют исторические записи 2026-11-07 и
  помечают актуальный EC.
- **Host STM32 gate matches the C011 slot.** `flash.c` limits the STM32
  image to 20 KiB (was 120 KiB for the CBT6); the comment and error text
  were updated. Bench docs: `TESTBENCH.md` §2 now lists the EC as an
  STM32C011F6P6 (`0x42`, proto v3) and §1 carries the new date;
  `FEATURES_VERIFIED.md`, `FEATURES.md` and `TO_DO.md` keep the historical
  2026-11-07 records and annotate the current EC.
- **I2C-транспорт хостов: ретрай записи и таймаут адаптера.** `oko-motor`
  и `focus_motor.c` повторяют только отказ фазы адреса (`I2C_WRITE_RETRIES`
  2 / `FM_WRITE_RETRIES` 2), не переигрывая частичный кадр, и выставляют
  `I2C_TIMEOUT` 1 с: STM8 удерживает SCL во время программирования Flash.
- **Host I2C transport: write retry and adapter timeout.** `oko-motor` and
  `focus_motor.c` retry an address-phase failure only (`I2C_WRITE_RETRIES`
  2 / `FM_WRITE_RETRIES` 2) and never replay a partial frame, and set
  `I2C_TIMEOUT` to 1 s because the STM8 stretches SCL while programming
  Flash.

### Изменено

- **`[http_post]`: стартовый снимок не дублирует heartbeat в wake-цикле.**
  Если `sleep.heartbeat=1` и процесс стартовал после probe-wake, поток
  периодической отправки пропускает свой первый снимок: единственный кадр
  цикла отправляет heartbeat. При обычной загрузке поведение прежнее.
- **`[http_post]`: the startup snapshot no longer duplicates the heartbeat in
  a wake cycle.** With `sleep.heartbeat=1` and a process started after a
  probe wake, the periodic sender skips its first snapshot: the heartbeat is
  the only frame of the cycle. A normal boot keeps the previous behaviour.
- **AOV: окно ожидания mailbox сокращено до 1000 мс (KI-072).**
  `RV1106_HPMCU_FAST_WKUP_TIMEOUT` 6000 → 1000 мс: при поднятом конвейере MCU
  отвечает `-h-mbox-` через ~0.2 с (прогон 2026-11-17), а 6 с только
  задерживали возврат AP по таймауту, когда кадров нет (`aov_keep_isp=0`).
  Стенд (ядро #26, окно 1000 мс, motion-мета): 6 из 6 `-h-wait-`
  получили `-h-mbox-`, ни одного `-h-timeout-`, в консоли
  `[sleep] resumed (… aov motion)` (артефакт
  `output/bench/aov-test/quiet_motion1000.20261118-094013.log`); в тихих
  прогонах #24/#25 MCU тоже отвечал в окне (десятки `-h-mbox-`). `-h-timeout-`
  появляется только на здоровом MCU без кадров (`aov_keep_isp=0`).
  Диагностические печати и эксперименты с PM/MCU из этого цикла откатаны;
  длинный тихий сон записан в KI-072 как NO-GO на стороне AP.
- **AOV: the mailbox wait window is cut to 1000 ms (KI-072).**
  `RV1106_HPMCU_FAST_WKUP_TIMEOUT` 6000 → 1000 ms: with the pipeline kept up
  the MCU answers `-h-mbox-` after ~0.2 s (2026-11-17 run), while 6 s only
  delayed the AP timeout return when there are no frames (`aov_keep_isp=0`).
  Bench (kernel #26, 1000 ms window, motion meta): 6 of 6 `-h-wait-` got an
  `-h-mbox-`, no `-h-timeout-`, and the console logs
  `[sleep] resumed (… aov motion)` (artifact
  `output/bench/aov-test/quiet_motion1000.20261118-094013.log`); the quiet runs
  #24/#25 also answered inside the window (dozens of `-h-mbox-`). `-h-timeout-`
  appears only on a healthy MCU without frames (`aov_keep_isp=0`). The
  diagnostic prints and the PM/MCU experiments from this cycle are reverted;
  the long quiet sleep is recorded in KI-072 as NO-GO on the AP side.
- **Сборка ядра: пустой `Image.gz` больше не попадает в `boot.img`.**
  Правило `%.img` в `arch/arm` собирает `zImage` и `Image.gz` одним
  параллельным `make`; один раз это дало 20-байтный `Image.gz` (cat ещё не
  дописанного `Image`), FIT собрался с пустым ядром и U-Boot ушёл в
  `### ERROR ###`. `__BUILD_KERNEL_FIT` теперь при размере меньше 1 МиБ
  пересобирает payload последовательно и отказывается упаковывать FIT, если
  размер всё ещё подозрительный.
- **Kernel build: an empty `Image.gz` never reaches `boot.img`.**
  The `%.img` rule in `arch/arm` builds `zImage` and `Image.gz` in one parallel
  `make`; once that produced a 20-byte `Image.gz` (cat of an `Image` that was
  still being written), the FIT was packed with an empty kernel and U-Boot
  stopped at `### ERROR ###`. `__BUILD_KERNEL_FIT` now rebuilds the payload
  serially when it is smaller than 1 MiB and refuses to pack the FIT if the
  size is still suspicious.
- **Стенд: захват U-Boot больше не путает вывод MCU с приглашением.**
  HPMCU делит UART2 с консолью AP и печатает строки вида
  `[AELIB]: ... env_lux ==> 0.0000`; поиск подстроки `=>` принимал их за
  приглашение U-Boot, команды `mtd list` уходили в чужой контекст и прошивка
  падала на `no MTD device in 'mtd list'`. `netboot.wait_prompt()`,
  `tftp_flash.at_uboot_prompt()`/`uboot_cmd()` теперь ищут `=>` только в
  начале строки (и предварительно сливают остатки ввода).
- **Bench: catching U-Boot no longer mistakes MCU output for the prompt.**
  The HPMCU shares UART2 with the AP console and prints lines such as
  `[AELIB]: ... env_lux ==> 0.0000`; a plain `=>` substring search took them
  for the U-Boot prompt, `mtd list` went to the wrong context and the flash
  failed with `no MTD device in 'mtd list'`. `netboot.wait_prompt()`,
  `tftp_flash.at_uboot_prompt()`/`uboot_cmd()` now match `=>` only at the
  start of a line (and drain pending input first).
- **Стенд: `bench/aov_cadence.py` — замер кадентности AOV по монитору UART.**
  Скрипт пишет поток read-only монитора моста (`:2324`) в
  `output/bench/aov-test/`, считает `-h-wait-`/`-h-mbox-`/`-h-timeout-` и
  печатает дельты и темп (JSON-сводка рядом с захватом); умеет разбирать
  готовый файл (`--file`). Сводка учитывает потери монитора
  (`dropped_bytes`/`gap_notices`, `complete=false`) и склеивает маркеры,
  разорванные границей чанка, поэтому прогон с потерями не выдаётся за полный
  замер. Использован для дифференциальных прогонов KI-072.
- **Bench: `bench/aov_cadence.py` measures AOV cadence from the UART monitor.**
  The script records the bridge read-only monitor stream (`:2324`) into
  `output/bench/aov-test/`, counts `-h-wait-`/`-h-mbox-`/`-h-timeout-` and
  prints deltas and the rate (a JSON summary next to the capture); it can also
  parse an existing file (`--file`). The summary accounts for monitor drops
  (`dropped_bytes`/`gap_notices`, `complete=false`) and joins markers split by
  a chunk boundary, so a run that lost data is not reported as a full
  measurement. Used for the KI-072 differential runs.
- **Документация: REST API ESP32-моста (ESP32-OKO-KVM) в инструкциях агента.**
  `AGENTS.md §7` превращён из пары curl-команд в роутер по мосту: прошивка и
  upstream-репозиторий, каналы (telnet `:23` — один управляющий клиент,
  read-only мониторы UART `:2324`/`:24`, HTTP `:80`), группы API (`/config`,
  `/sd/*`, `/logs`, `/ext/status|power|reset`, `/system/time`, `/ota/*`),
  гигиена сессий (login → работа → обязательный logout, пул 3 сессии, `429`);
  в §2 (`TESTBENCH.md`) добавлено упоминание REST API моста. В `TESTBENCH.md`
  по пробам живого моста (прошивка `1.10.0`, слот `ota_1`) описаны проверенные
  эндпоинты с форматами ответов, коды `401`/`429`/`MON_DISABLED`, распиновка
  (`ext_pwr` GPIO20, `ext_rst` GPIO19, UART0 GPIO16/17), формат text
  SD-лога; исправлено «файл кольцевой» на ротацию 1 МиБ × 10 копий и
  зафиксировано, что на 1.10.0 запись идёт без подключённого telnet-клиента
  (upstream-описание «только при клиенте» помечено как неподтверждённое);
  уточнены §2/§4/§5/§6/§7-AOV и добавлена особенность моста как общего
  ресурса стенда в §9.
- **Docs: ESP32 bridge REST API (ESP32-OKO-KVM) in the agent instructions.**
  `AGENTS.md §7` is now a router for the bridge instead of a pair of curl
  commands: firmware and upstream repository, channels (telnet `:23` with a
  single control client, read-only UART monitors `:2324`/`:24`, HTTP `:80`),
  API groups (`/config`, `/sd/*`, `/logs`, `/ext/status|power|reset`,
  `/system/time`, `/ota/*`) and session hygiene (login → work → mandatory
  logout, a 3-session pool, `429`); §2 (the `TESTBENCH.md` row) mentions the
  bridge REST API. `TESTBENCH.md` documents the endpoints verified on the live
  bridge (firmware `1.10.0`, slot `ota_1`) with response formats, the
  `401`/`429`/`MON_DISABLED` codes, pinout (`ext_pwr` GPIO20, `ext_rst`
  GPIO19, UART0 GPIO16/17) and the text SD-log format; the "ring file" wording
  is replaced by 1 MiB × 10-copy rotation, and it is recorded that on 1.10.0
  the log is written without a connected telnet client (the upstream "only
  while a client is attached" statement is marked unconfirmed); §2/§4/§5/§6/
  §7-AOV are refined and §9 gains the bridge-as-shared-resource note.
- **Правило проекта: вендорский код — только справочник (AGENTS.md §6).**
  Зафиксировано, что `project/app/rkipc`, `rk_smart_door`,
  `fastboot_client`, `component/rkadk`, `sysdrv/source/mcu` (RT-Thread) и
  прочие унаследованные каталоги SDK Luckfox/Rockchip читаются, но не
  правятся и не служат основой продуктовых фич. Продуктовые фичи делаются в
  `divinus`, `oko-*`, `tools/native`, `bench/`, OKO-файлах `project/cfg`,
  фрагментах ядра/DTS и склейках `drv_ko`. Интеграционный патч вендора
  допускается только с обоснованием и записью в CHANGELOG; замороженные
  экспериментальные ветки (TB/rkipc) не расширяются. Это рамка для
  divinus-трека сна камеры (задачи Д0–Д6 плана).
- **Project rule: vendor code is reference only (AGENTS.md §6).** It is now
  explicit that `project/app/rkipc`, `rk_smart_door`, `fastboot_client`,
  `component/rkadk`, `sysdrv/source/mcu` (RT-Thread) and other inherited
  Luckfox/Rockchip SDK directories may be read but not modified and not used
  as a base for product features. Product features live in `divinus`,
  `oko-*`, `tools/native`, `bench/`, OKO `project/cfg` files, kernel/DTS
  fragments and `drv_ko` glue. A vendor integration patch is allowed only with
  a justification and a CHANGELOG entry; frozen experimental branches
  (TB/rkipc) are not extended. This frames the divinus camera-sleep track
  (plan tasks Д0–Д6).
- **Сборочные послабления для wakeup-IPC линии.** `project/app/Makefile`
  собирает vendor `rkipc` только при `RK_APP_TYPE=RKIPC_RV1106_WAKEUP_IPC`
  (продуктовые сборки OKO по-прежнему без него), `project/build.sh` уважает
  `RK_SKIP_OKO_PRUNE=y` для линий, которым нужны файлы из `oko-prune.list`,
  а host-утилиты `make_meta` (`sensor_ae_awb_table`, `sensor_init`) собираются
  без `-m32`: на Debian 13 32-битный бинарник получает мусорный `argv` и
  падает; структуры в них — только фиксированной ширины.
- **Build allowances for the wakeup-IPC line.** `project/app/Makefile` builds
  the vendor `rkipc` only for `RK_APP_TYPE=RKIPC_RV1106_WAKEUP_IPC` (OKO
  product builds still exclude it), `project/build.sh` honours
  `RK_SKIP_OKO_PRUNE=y` for lines that need files from `oko-prune.list`, and
  the `make_meta` host tools (`sensor_ae_awb_table`, `sensor_init`) are built
  without `-m32`: on Debian 13 the 32-bit binary gets a garbage `argv` and
  crashes; their structures are fixed-width only.
- **Рамки IVS возвращены: регион 2BPP создаётся один раз за процесс.**
  Переследование KI-063 на восстановленном NAND показало, что destroy/recreate
  2BPP-региона сам по себе не вешает MPP: YOLO-рамки (тот же код
  `rk_region_box_*`) выдержали 200 A/B-циклов (fd 116↔117, VmRSS 20,0 МиБ,
  0 ошибок ядра), а IVS-рамки в режиме create-once стабильны 30 мин на NAND и
  30 мин на NFS (fd 90→90 и 104→104, 0 счётчиков ядра). Hard block снят из
  `gating.c`, `ivs_events.c` (`ivs_boxes_request(0)` больше не форсируется),
  `/api/ivs` и WebUI; регион уничтожается в `ivs_events_stop()` до `sdk_stop()`,
  поэтому выход процесса и SIGHUP больше не дают oops `dma_buf_release`
  (20 рестартов с привязанным регионом 13 — без oops). Новый ключ
  `ivs.boxes_mode` (`restart` по умолчанию, `live` — opt-in): в `restart`
  режиме `POST /api/ivs?boxes=0|1` сохраняет конфиг, отвечает
  `"applied":false` с `note` и делает SIGHUP; WebUI показывает это в
  примечании. Если конфиг сохранить не удалось, тумблер не меняется и
  приложение не перезапускается (`applied:false` + примечание); `boxes` и
  `boxes_mode` в одном запросе применяются согласованно. Заодно сняты устаревшие проверки «vpss3 занят IVS» в
  `gating.c` и `main.c`: IVS работает на своём VI-канале 2, поэтому
  IVS-рамки и YOLO `vpss3` живут вместе (30 мин, fd 132–134). Документация —
  `KNOWN_ISSUES.md` KI-063/KI-068, `FEATURES_VERIFIED.md`,
  `TESTBENCH.md §8`, `research/rgn-mpp-lifecycle/`.
- **IVS boxes are back: the 2BPP region is created once per process.** The
  KI-063 re-investigation on the restored NAND showed that destroy/recreate of
  a 2BPP region does not hang MPP by itself: YOLO boxes (the same
  `rk_region_box_*` code) survived 200 A/B cycles (fd 116↔117, VmRSS steady at
  20.0 MiB, no kernel errors), and IVS boxes in create-once mode were stable
  for 30 min on NAND and 30 min on NFS (fd 90→90 and 104→104, zero kernel
  counters). The hard block is removed from `gating.c`, `ivs_events.c`
  (`ivs_boxes_request(0)` is no longer forced), `/api/ivs` and the WebUI; the
  region is destroyed in `ivs_events_stop()` before `sdk_stop()`, so a process
  exit or SIGHUP no longer oopses in `dma_buf_release` (20 restarts with
  region 13 attached produced no oops). A new `ivs.boxes_mode` key (`restart`
  by default, `live` opt-in): in `restart` mode `POST /api/ivs?boxes=0|1`
  saves the configuration, replies `"applied":false` with a `note` and sends
  SIGHUP; the WebUI note explains it. When the configuration cannot be saved
  the switch is left unchanged and nothing restarts (`applied:false` plus a
  note); `boxes` and `boxes_mode` in one request are applied together. The stale "vpss3 is taken by IVS"
  checks were removed from `gating.c` and `main.c`: IVS uses its own VI
  channel 2, so IVS boxes and YOLO `vpss3` run together (30 min, fd 132–134).
  Docs — `KNOWN_ISSUES.md` KI-063/KI-068, `FEATURES_VERIFIED.md`,
  `TESTBENCH.md §8`, `research/rgn-mpp-lifecycle/`.
- **`bench/ui_check.py` проверяет поле порога площади IVS.** Добавлена проверка
  `#ivs_min_area` в карточке Events: поле есть, `min=0`, `max=50`, значение
  приходит из `GET /api/ivs`; в выводе печатаются границы и текущее значение.
- **`bench/ui_check.py` checks the IVS motion-area field.** Added an
  `#ivs_min_area` check in the Events card: the field exists, `min=0`,
  `max=50`, and the value comes from `GET /api/ivs`; the report prints the
  bounds and the current value.

- **Адрес DUT в стендовых скриптах и TESTBENCH.** DHCP-адрес камеры в режиме
  NAND — `192.168.52.33` вместо `192.168.52.97`: обновлены значения по
  умолчанию `OKO_DUT_HOST`/`OKO_NAND_HOST` в `bench/`, примеры команд в
  `bench/README.md` и `TESTBENCH.md §4–§5`.
- **DUT address in the bench scripts and TESTBENCH.** The camera's DHCP address
  in NAND mode is `192.168.52.33` instead of `192.168.52.97`: the
  `OKO_DUT_HOST`/`OKO_NAND_HOST` defaults in `bench/`, the command examples in
  `bench/README.md` and `TESTBENCH.md §4-§5` were updated.
- **Документация: лог UART RV1106 через ESP32.** В `AGENTS.md §6` и
  `TESTBENCH.md §8` добавлены команды входа на `192.168.52.17` и скачивания
  `uar0.log` через `/sd/download`; лог сохраняется в `output/bench/`, cookie —
  временный `/tmp/c`.
- **Docs: RV1106 UART log through the ESP32 bridge.** `AGENTS.md §6` and
  `TESTBENCH.md §8` now document logging in to `192.168.52.17` and downloading
  `uar0.log` via `/sd/download`; the log is kept in `output/bench/`, the cookie
  is the session-scoped `/tmp/c`.
- **Чистка устаревших утверждений в документации.** `KNOWN_ISSUES.md`:
  14 закрытых записей (KI-021, KI-023, KI-036, KI-043, KI-044, KI-049, KI-050,
  KI-055, KI-056, KI-061, KI-063, KI-065, KI-066, KI-069) перенесены в
  компактный архив в конце RU/EN-секций с сохранением остатков; KI-002/010/015
  переформулированы, KI-001 получил актуальный обход, заведена KI-070, даты
  шапок RU/EN синхронизированы (2026-11-15). `FEATURES_VERIFIED.md` и
  `FEATURES_UNVERIFIED.md`: рамки на main-потоке, `vpss3` конфликтует только с
  MJPEG/четвёртым потоком (KI-068), бюджет VPSS и `vi0`-совет приведены к
  текущей топологии. `project/app/divinus/README.md` (раздел «Каналы»),
  `source/doc/README.md` (аддендум к G7/G11 и «Channel budget»),
  `README_RU.md` (актуальный список открытых записей темы STM8/STM32),
  `STRUCTURE.md` (в списке корневых файлов появился `TO_DO.md`).
- **Stale claims removed from the docs.** `KNOWN_ISSUES.md`: 14 closed entries
  (KI-021, KI-023, KI-036, KI-043, KI-044, KI-049, KI-050, KI-055, KI-056,
  KI-061, KI-063, KI-065, KI-066, KI-069) moved to a compact archive at the end
  of the RU/EN sections with their residuals; KI-002/010/015 reworded, KI-001
  gained the current workaround, KI-070 filed, and the RU/EN header dates
  synchronised (2026-11-15). `FEATURES_VERIFIED.md` and
  `FEATURES_UNVERIFIED.md`: boxes on the main stream, `vpss3` conflicting only
  with MJPEG/a fourth stream (KI-068), and the VPSS budget/`vi0` advice brought
  to the current topology. `project/app/divinus/README.md` (the "Channels"
  section), `source/doc/README.md` (an addendum to G7/G11 and "Channel budget"),
  `README_RU.md` (the current list of open STM8/STM32 entries) and
  `STRUCTURE.md` (the root file list now includes `TO_DO.md`).

- **Прошивка STM8: версии 2.2.0 (продукт) и 2.3.0 (тестовая плата),
  тестовая секция 12 и документация.** `Makefile.stm8` задаёт версии по
  `BOARD` и `SLEEP_DEFAULT_ON`; обновлены `PROTOCOL*`, `HARDWARE*`,
  `HARDWARE_TESTBOARD.md`, README утилиты и subtree-CHANGELOG. Стендовая
  проверка сна заблокирована KI-075 (I2C-обновление STM8 не стирает
  Flash на текущем экземпляре); продуктовый дефолт сна остаётся OFF.
- **STM8 firmware: versions 2.2.0 (product) and 2.3.0 (test board), test
  section 12 and documentation.** `Makefile.stm8` sets the versions per
  `BOARD` and `SLEEP_DEFAULT_ON`; `PROTOCOL*`, `HARDWARE*`,
  `HARDWARE_TESTBOARD.md`, the utility READMEs and the subtree CHANGELOG
  were updated. On-target sleep validation is blocked by KI-075 (the STM8
  I2C update does not erase Flash on the current sample); the product sleep
  default stays OFF.

## [0.3.0] — 2026-11-14

### Добавлено

- **YOLO11, рантайм RKNN 2.5.0 и ролики событий.** Второй мини-рантайм
  `librknnmrt2.so` обновлён до **2.5.0** (`Dockerfile.runtime`: тег, коммит
  `2c723177…`, проверка SHA-256 оригинала `632d0e50…`, patched `6f1d83dd…`);
  образ `oko-rknn-toolkit2:2.5.0` собран на колесе 2.5.0 с пинами
  `protobuf 4.25.4`, `onnx 1.17.0`, `onnxruntime 1.17.3`, `numpy 1.24.4`,
  `torch 1.10.1+cpu`; заголовок `hal/plus/rknn_api.h` синхронизирован с
  2.5.0 (`user_key`, `-14..-16`, `rknn_create_mem2`). Новые
  `Dockerfile.export11` и `export_yolo11.sh` экспортируют YOLO11n в RKOPT
  (`format: rknn`, 9 выходов) на 640×384; модель INT8 3 905 345 Б,
  архитектура определяется по числу выходов (`3` → yolov5, `9` → yolo11),
  пишется в `models.json` (`arch`) и `/api/yolo` (`arch`), YOLO11 требует
  `yolo.runtime=2`. Добавлен DFL-постпроцесс (порт Apache-2.0 из
  `rknn_model_zoo`, score-sum фильтр, общий NMS) и хост-тест
  `tests/yolo11_postprocess_test.c` с golden-рамками bus.jpg (3 person + bus,
  ±2 px). Квота моделей поднята с 3 МиБ до 4386 КиБ (измеренный размер
  +15 %), HTTP-буфер — до 10 МиБ, чтобы превышение квоты давало читаемую
  ошибку. Добавлены ролики событий: RAM-кольцо AU main-потока (обрезка по
  ключевому кадру), независимый fMP4-муксер `fmt/mp4_clip.c`, `record_clip`
  с pre/post (`[event_clip]`), отправкой `PUT` на `http_post.url` с
  `frame`→`clip` и заголовками `X-Event-Id/X-Cause/X-Model/X-Duration-Ms/X-Ts`,
  повторами 30 с/2 мин/10 мин, outbox на microSD (256 МиБ, FIFO) и
  `GET/POST /api/clip` (`test=1` — синтетическое событие с общим id для
  JPEG-пачки и ролика). Сервис `services/events` (`oko-events`, порт 8090,
  FastAPI + ffmpeg) принимает кадры и клип, хранит
  `output/srv/events/<дата>/<event_id>/`, делает H.264-копию и JPEG-превью,
  отдаёт `/api/events`, `/media/...`, HTML-список, retention
  `EVENTS_MAX_GB`. `bench/event_check.py` проверяет сквозной сценарий,
  длительность `pre+post ±2 с`, кодеки hevc→h264 и повторную доставку.
  Проверено на DUT `192.168.52.33`: YOLO11n 640×384, 5 fps — NPU 28 %,
  CPU `divinus` ~12 %; событие 14,4–14,8 с, 16–20 JPEG, `clip.mp4` hevc
  1920×1080, `clip-h264.mp4` — h264; `api_check.py` 113 проверок без ошибок,
  `event_check.py` зелёный (включая `--retry`), `osd_check.py` и `ui_check.py`
  без регрессии; `MemFree` 79 МиБ при кольце 9,8 МиБ и активном YOLO11.
  Полный `./build.sh all` прошёл, собран `output/image/update.img`
  (71 936 586 Б); в образе `librknnmrt2.so` 2.5.0 после `strip --strip-debug`
  (310 492 Б, sha `4b48d519…`), `divinus` с `[event_clip]` и YAML по умолчанию.
  Soak `bench/clip_soak.py`: 10- и 5-минутные прогоны — 0 отказов камеры,
  0 потерянных событий, outbox 0, `MemFree` стабилен (73–84 МиБ), кольцо
  сбрасывало 7–25 AU при переполнении во время записи. Кольцо роликов
  переведено на отдельный `malloc` на AU (без байтового wrap, который мог
  затереть AU активного клипа) и 4 слота вместо 2: при детекции каждые 5 с
  и клипе 15 с два слота теряли ~15 % событий, четыре покрывают три
  перекрывающихся клипа; арена — `pre+post+5` с (11,5 МиБ при 4096 кбит/с).
  Сервис: 4 воркера транскода, повторная постановка `pending` при старте,
  защита `meta.json` от удалённого retention каталога и retention по
  отдельным событиям с 10-минутной защитой (раньше удалялась целая дата и
  свежие события). `event_check.py` ждёт транскод конкретного события, а не
  всего списка. Outbox разгружается пачкой до 8 клипов за проход (раз в 15 с),
  иначе после двухчасового простоя доставка растягивалась на часы.
  Итоговый 2-часовой soak (2026-11-14, 09:34–11:35 UTC): 0 отказов камеры,
  0 потерянных событий, outbox пуст, `sent` 1355, `ring_dropped` +805,
  `retries_failed` 1, `MemFree` 75,8 → 69,6 МиБ (минимум), события 43 → 218.
  Сценарий outbox с SD: спул и разгрузка — ok. Без SD (размонтирована):
  после трёх ретраев `lost` 21, outbox 0, `/tmp` занят 26 МиБ из 90.
  Dry-cycle: перезагрузка, автозапуск `divinus` (pid 410), YOLO11 активна,
  `event_clip` работает; после ребута `api_check` 113/113, `event_check` и
  `osd_check` зелёные. mAP YOLO11 и загрузка
  RK3576-модели Hanzo не проверялись;
  прямая копия Hanzo-модели в `/userdata/models` и активация дают
  `rknn_init failed (-1)` с `E RKNN: incompatible model version: 6`
  (ожидавшийся `-13` не воспроизводится: модель отклоняется по версии формата);
  рамки YOLO11 проверены: регион 12 создан, в кадре есть красные/синие пиксели
  оверлея (класс 25).
- **YOLO11, RKNN 2.5.0 runtime and event clips.** The second mini-runtime
  `librknnmrt2.so` is upgraded to **2.5.0** (`Dockerfile.runtime` pins the
  tag, commit `2c723177…`, the original SHA-256 `632d0e50…` and the patched
  `6f1d83dd…`); `oko-rknn-toolkit2:2.5.0` is built from the 2.5.0 wheel with
  `protobuf 4.25.4`, `onnx 1.17.0`, `onnxruntime 1.17.3`, `numpy 1.24.4`,
  `torch 1.10.1+cpu`; `hal/plus/rknn_api.h` is synced with 2.5.0 (`user_key`,
  `-14..-16`, `rknn_create_mem2`). The new `Dockerfile.export11` and
  `export_yolo11.sh` export YOLO11n in the RKOPT layout (`format: rknn`, nine
  outputs) at 640×384; the INT8 model is 3,905,345 bytes, the architecture is
  detected from the output count (`3` → yolov5, `9` → yolo11), stored in
  `models.json` (`arch`) and exposed by `/api/yolo` (`arch`), and YOLO11
  requires `yolo.runtime=2`. Added the DFL decoder (Apache-2.0 port from
  `rknn_model_zoo`, score-sum filter, shared NMS) and the host test
  `tests/yolo11_postprocess_test.c` with the bus.jpg golden boxes (3 person +
  bus, ±2 px). The model quota grows from 3 MiB to 4386 KiB (measured size
  +15 %) and the HTTP buffer to 10 MiB so an oversized upload gets a readable
  error. Added event clips: a RAM ring of main-stream AUs (trimmed to the key
  frame), an independent fMP4 muxer `fmt/mp4_clip.c`, `record_clip` with
  pre/post (`[event_clip]`), `PUT` to `http_post.url` with `frame`→`clip` and
  `X-Event-Id/X-Cause/X-Model/X-Duration-Ms/X-Ts`, retries at 30 s/2 min/
  10 min, a 256 MiB FIFO microSD outbox and `GET/POST /api/clip` (`test=1` is
  a synthetic event sharing its id with the JPEG package). The
  `services/events` receiver (`oko-events`, port 8090, FastAPI + ffmpeg)
  stores `output/srv/events/<date>/<event_id>/`, produces the H.264 copy and
  JPEG preview, serves `/api/events`, `/media/...`, an HTML list and
  `EVENTS_MAX_GB` retention. `bench/event_check.py` covers the end-to-end
  flow, the `pre+post ±2 s` duration, hevc→h264 codecs and retry delivery.
  Verified on the DUT `192.168.52.33`: YOLO11n 640×384 at 5 fps — NPU 28 %,
  `divinus` CPU ~12 %; a 14.4–14.8 s event with 16–20 JPEG frames,
  a hevc 1920×1080 `clip.mp4` and an h264 `clip-h264.mp4`; the
  `bench/clip_soak.py` soak polls `/api/clip` and the receiver, sends a test
  event while idle and records `sent/lost/outbox/ring_dropped/MemFree`; `api_check.py`
  113 checks with no failures, `event_check.py` green (including `--retry`),
  `osd_check.py` and `ui_check.py` regression-clean; `MemFree` 79 MiB with a
  9.8 MiB ring and YOLO11 active. A full `./build.sh all` succeeded and produced
  `output/image/update.img` (71,936,586 bytes); the image carries the 2.5.0
  `librknnmrt2.so` after `strip --strip-debug` (310,492 bytes, sha
  `4b48d519…`) and `divinus` with `[event_clip]`. The
  `bench/clip_soak.py` 10- and 5-minute runs had zero camera failures, zero
  lost events, an empty outbox and stable `MemFree` (73–84 MiB); the ring
  dropped 7–25 AUs when it filled during an active clip. The clip ring now
  allocates one buffer per AU (no byte-ring wrap that could overwrite an AU
  held by an active clip) and uses four slots instead of two: at a detection
  every 5 s and a 15 s clip, two slots lost ~15 % of the events, four cover
  three overlapping clips; the arena is `pre+post+5` seconds (11.5 MiB at
  4096 kbit/s). The service now runs four transcode workers, requeues
  `pending` clips at startup, tolerates a `meta.json` removed by retention and
  evicts individual events with a 10-minute protection window (it used to
  remove the whole date, including fresh events). `event_check.py` waits for
  the transcoded copy of its own event instead of the whole list. YOLO11 mAP and the
  Hanzo RK3576 upload remain unverified; a direct copy of the Hanzo model into
  `/userdata/models` plus activation returns `rknn_init failed (-1)` with
  `E RKNN: incompatible model version: 6` (the expected `-13` does not
  reproduce: the model is rejected by its format version); YOLO11 boxes were
  checked — region 12 is created and the frame contains red/blue overlay
  pixels (class 25).

- **История инференсов YOLO и стенд ночного замера (KI-005).**
  `GET /api/yolo?detections=1&since=<seq>` — read-only выдача последних 64
  инференсов (до 8 боксов в нормировке 0..10000, `seq/ts/source/aiisp`);
  статическое кольцо 7,5 КиБ, сброс при старте детектора, ответ ≤8 КиБ с
  курсором `last`, `since` без значения — 200 с пустым списком. Добавлены
  `bench/yolo_night.py` (матрица C1–C4 `{vpss3,vi0}×{AIISP off,on}`,
  day-control C1/C4, `--phase ambient` для плат без ИК-железа, правка
  `/userdata/divinus.yaml` + graceful restart, восстановление конфига,
  recall / detection rate / mean score / FP per frame по `--gt`, артефакты в
  `output/bench/yolo_night`) и хост-тест
  `tests/yolo_history_test.c`; обновлены `endpoints.md` и `bench/README.md`.
  Проверено на DUT `192.168.52.33`: сборка divinus, хост-тесты, `api_check.py`
  108 проверок без ошибок. Модель `yolov5n-640x384.rknn` (2,66 МБ) собрана
  через `tools/rknn-toolkit2` по 60 кадрам калибровки с DUT, загружена и
  активирована; детектор ловит `person` со score 0.35–0.69. Матрица C1–C4
  прогнана (`--phase ambient`, 4×40 с), но числа не опубликованы: три повтора
  одной C1 дали detection rate 0.04/0.22/0.31 — сцена нестабильна; ИК-подсветки
  и управляемого IR-cut на плате нет. KI-005 остаётся открытым.
- **YOLO inference history and the night-measurement bench (KI-005).**
  `GET /api/yolo?detections=1&since=<seq>` is a read-only view of the last 64
  inferences (up to 8 boxes normalised to 0..10000, `seq/ts/source/aiisp`);
  a static 7.5 KiB ring, reset when the detector starts, an answer capped at
  8 KiB with a `last` cursor; `since` without a value returns HTTP 200 with an
  empty list. Added `bench/yolo_night.py` (C1–C4
  `{vpss3,vi0}×{AIISP off,on}`, day control C1/C4, `--phase ambient` for
  boards without IR hardware, `/userdata/divinus.yaml` edit plus a graceful
  restart, config restore, recall / detection rate / mean score / FP per frame
  from `--gt`, artifacts in `output/bench/yolo_night`) and the host test
  `tests/yolo_history_test.c`; updated `endpoints.md` and `bench/README.md`.
  Verified on the DUT `192.168.52.33`: divinus build, host tests,
  `api_check.py` 108 checks with no failures. The `yolov5n-640x384.rknn` model
  (2.66 MB) was built with `tools/rknn-toolkit2` from 60 calibration frames
  captured on the DUT, uploaded and activated; the detector finds `person`
  with score 0.35–0.69. The C1–C4 matrix ran (`--phase ambient`, 4×40 s) but
  its numbers are not published: three repeats of the same C1 gave detection
  rate 0.04/0.22/0.31, i.e. the scene is unstable, and the board has no IR
  illuminator or switchable IR-cut. KI-005 stays open.

- **STM32 EC: таймерное освобождение прижатой SCL и отладочный слой.**
  В прошивку C011/C031 добавлены аппаратный SCL-low timeout `TIMEOUTR`
  (~10 мс) с обработкой `I2C_ISR_TIMEOUT`, программный bus-watchdog
  (`POWER_BUS_HANG_MS=15`), счётчики/снимки I2C с зеркалом в
  `PWR->BKP2R/BKP3R` (16-битные, упакованы) и команды `0x28`/`0x29` (только
  `-DEC_DEBUG_BUILD=ON`). Если мастер исчезает посреди чтения, EC
  отпускает линию сам, без NRST. Добавлен `bench/ec_bus_soak.py` —
  матрица reboot/read-window/detect/hold-scl. Проверено на стенде C011
  2026-11-13: hold-scl отпускается за ~10 мс, Stop→wake после 5 с, 40
  ребутов (30 read-window) без `SCL hold by slave`.

- **SoftAP всегда WPA2-PSK с заводским паролем (OKO).** `oko-wifi` больше не
  поднимает открытую точку: пустой `oko_wlan_ap_psk` в `load_settings`
  заменяется заводским `ybrjveytljdthzq`, а `start_ap` всегда пишет `wpa=2`,
  `wpa_passphrase`, `wpa_key_mgmt=WPA-PSK` и `rsn_pairwise=CCMP`. Пароль
  виден оператору в `oko-wifi env`, `GET /api/wifi` и `/api/wifi/apinfo`
  (назначение последнего — показать пароль точки с телефона). Явно заданный
  пароль (`oko-wifi set`, `/api/wifi`, SD-конфиг) переопределяет заводский;
  вернуть заводский — `fw_setenv oko_wlan_ap_psk` (удаление переменной).
  Пароль публичный, уровня `admin/oko-change-me`, и обязателен к смене при
  вводе в эксплуатацию (KI-030). Стенд 2026-11-12 (DUT `192.168.52.33`,
  фабричное env, рабочее дерево): `oko-wifi env` и `apinfo` →
  `ybrjveytljdthzq`, hostapd → `wpa=2`/CCMP, OSD `wifi ap`, адрес
  `192.168.43.1/24`; клиент с хоста (`wpa_supplicant`, `iw`) ассоциировался
  по `WPA2-PSK`/`CCMP`, получил DHCP `192.168.43.17`, ping и HTTP к
  `192.168.43.1` отвечают; неверный пароль → `4-Way Handshake failed` /
  `WRONG_KEY` без ассоциации, DUT остаётся в AP; кастомный
  `custom-hotspot-pass` ассоциируется и виден в конфиге и `apinfo`, после
  `fw_setenv oko_wlan_ap_psk` снова заводский.
- **SoftAP is always WPA2-PSK with a factory password (OKO).** `oko-wifi` no
  longer brings up an open access point: an empty `oko_wlan_ap_psk` is
  replaced by the factory `ybrjveytljdthzq` in `load_settings`, and
  `start_ap` always writes `wpa=2`, `wpa_passphrase`, `wpa_key_mgmt=WPA-PSK`
  and `rsn_pairwise=CCMP`. The password is visible in `oko-wifi env`,
  `GET /api/wifi` and `/api/wifi/apinfo` (the latter exists to show the AP
  password from a phone). An explicitly set password (`oko-wifi set`,
  `/api/wifi`, SD config) overrides the factory one; restoring it is
  `fw_setenv oko_wlan_ap_psk` (delete the variable). The password is public,
  on the level of `admin/oko-change-me`, and must be changed before
  deployment (KI-030). Bench 2026-11-12 (DUT `192.168.52.33`, factory env,
  working tree): `oko-wifi env` and `apinfo` → `ybrjveytljdthzq`, hostapd →
  `wpa=2`/CCMP, OSD `wifi ap`, address `192.168.43.1/24`; a host client
  (`wpa_supplicant`, `iw`) associated over `WPA2-PSK`/`CCMP`, got DHCP
  `192.168.43.17`, ping and HTTP to `192.168.43.1` answer; a wrong password
  gives `4-Way Handshake failed` / `WRONG_KEY` with no association while the
  DUT stays in AP; the custom `custom-hotspot-pass` associates and shows up
  in the config and `apinfo`, and `fw_setenv oko_wlan_ap_psk` restores the
  factory one.
- **OSD: реальный Wi-Fi RSSI и режим SoftAP вместо симуляции (OKO).**
  `oko-osd` больше не рисует случайный RSSI: поле `WiFi` в `/tmp/osd-dyn.txt`
  (`reg1`) берётся из `wpa_cli -i wlan0 status` и
  `wpa_cli -i wlan0 signal_poll` (nl80211, dBm) и имеет четыре состояния —
  `WiFi <полоски>/5 <dbm>dBm` при `wpa_state=COMPLETED` и RSSI в диапазоне
  −120…−1 dBm, `wifi ap` при работающем SoftAP (`hostapd` с pid-файлом
  `/run/hostapd-wlan0.pid`, живым процессом с именем `hostapd` и IPv4 на
  `wlan0`), `wifi off` при наличии адаптера без ассоциации и без AP, и
  полное отсутствие поля, когда `/sys/class/net/wlan0` нет: пустой
  `@file`-файл заставляет divinus убрать регион. Проверка AP не даёт ложного
  `wifi ap` после переподключения USB-донгла (hostapd жив на старом netdev,
  адреса нет) и при переиспользовании pid из устаревшего pid-файла. В
  real-ветке Wi-Fi делит последнюю строку с LTE, в fallback-ветке занимает
  четвёртую; без адаптера не остаётся ни пустой строки, ни хвостового
  пробела. LTE и GPS по-прежнему декоративная симуляция. Стенд 2026-11-12:
  SoftAP → `wifi ap` (в т.ч. после boot), STA к `OKO-BENCH` →
  `WiFi 4/5 -55dBm` (совпало с `signal_poll`), `disconnect` → `wifi off`,
  переподключение USB при живом hostapd → `wifi off`, `authorized=0` → три
  строки без Wi-Fi; файл 31–99 байт (<255), в кадре четыре строки
  (`output/bench/osd-20261112-220334-*`); `api_check` 105/0, `ui_check` и
  `osd_check` зелёные, образ прошит по TFTP.
- **OSD: real Wi-Fi RSSI and SoftAP state instead of simulation (OKO).**
  `oko-osd` no longer draws a random RSSI: the `WiFi` field in
  `/tmp/osd-dyn.txt` (`reg1`) comes from `wpa_cli -i wlan0 status` and
  `wpa_cli -i wlan0 signal_poll` (nl80211, dBm) and has four states —
  `WiFi <bars>/5 <dbm>dBm` with `wpa_state=COMPLETED` and an RSSI in
  −120…−1 dBm, `wifi ap` while the SoftAP runs (`hostapd` with the
  `/run/hostapd-wlan0.pid` pid file, a live process named `hostapd` and an
  IPv4 address on `wlan0`), `wifi off` when the adapter is present but not
  associated and no AP runs, and no field at all when
  `/sys/class/net/wlan0` is missing: an empty `@file` makes divinus drop the
  region. The AP check avoids a phantom `wifi ap` after a USB dongle
  re-plug (hostapd still alive on the old netdev, no address) and after a
  recycled pid from a stale pid file. In the real branch Wi-Fi shares the
  last line with LTE, in the fallback branch it takes the fourth line;
  without an adapter neither an empty line nor a trailing space remains.
  LTE and GPS stay decorative simulation. Bench 2026-11-12: SoftAP →
  `wifi ap` (also after boot), STA to `OKO-BENCH` → `WiFi 4/5 -55dBm`
  (matched `signal_poll`), `disconnect` → `wifi off`, USB re-plug with a
  live hostapd → `wifi off`, `authorized=0` → three lines without Wi-Fi;
  the file is 31–99 bytes (<255), the frame shows four lines
  (`output/bench/osd-20261112-220334-*`); `api_check` 105/0, `ui_check` and
  `osd_check` passed, and the image was flashed over TFTP.
- **Два интерфейса в одной сети: weak-host защита (OKO).**
  `oko-route` (вызывается из `once` и каждого витка `monitor`) теперь
  выставляет `arp_ignore=1` и `arp_announce=2` на `all`/`default`, а при
  строгом `rp_filter=1` переводит его в loose-режим `2`; ARP-ответы больше
  не уходят с чужого интерфейса. Для eth0 и wlan0 создаются постоянные
  таблицы 100/101 (`<prefix> dev <if> src <ip>` и `default via <gw>`), к ним
  добавляются правила `from <ip>/32` (100/200) и `iif <if>` (110/210);
  занятые чужие приоритеты обходятся выбором свободного номера, выбор
  хранится в `/run/oko-route/policy.rules`. Пересборка идёт только при
  смене штампа адрес/шлюз, поэтому DHCP renew не вызывает flap. `show`
  дополнен `eth_addr`, `wlan_addr`, `multihome`, `arp_tuning`, `rp_filter`,
  `policy_tables`; `/api/network` отдаёт `multihome`, `arp_tuning`,
  `policy_tables`, `eth_addr`, `wlan_addr`; в WebUI Network появилась строка
  «Защита двух интерфейсов» (EN/RU). Стенд 2026-11-12 (eth0 DHCP
  `192.168.52.33/24`, wlan0 static `192.168.52.44/24` в той же подсети):
  параллельный ping 20/20 на каждый адрес — 0% потерь; `ip route get
  <host> from <addr>` уходит в eth0/wlan0 соответственно; ARP на PC —
  `.33` → MAC eth0, `.44` → MAC wlan0, flap нет; HTTP-снимок и RTSP
  DESCRIBE — 200 на обоих адресах; dead-шлюз eth (iptables DROP ICMP) →
  `eth_demoted=1`, default на wlan0, возврат после снятия DROP; AP-фолбэк
  `192.168.43.1` работает; static eth+static wlan в одной подсети —
  10/10 пингов на каждый адрес; 3 цикла eth link down/up — оба адреса
  живы; netboot-ветка (подмена `/proc/cmdline`) не меняет eth-default и
  resolv. Найден и исправлен дефект: `ip addr flush` из `oko-net apply`
  очищал таблицу 100 при неизменном штампе — теперь пустая таблица
  форсирует пересборку. `api_check` 105/0, `ui_check` зелёный;
  `check_image_contents` 30 required / 63 forbidden; образ прошит по
  TFTP. Не прогонялись: VPN поверх деградированного линка, реальный
  NFS-boot, 10 циклов линка (сделано 3), ассоциация клиента к SoftAP.
- **Two interfaces in one subnet: weak-host protection (OKO).**
  `oko-route` (called from `once` and every `monitor` pass) now sets
  `arp_ignore=1`/`arp_announce=2` on `all`/`default` and switches a strict
  `rp_filter=1` to the loose mode `2`, so ARP replies no longer leave through
  the wrong interface. Permanent tables 100/101 are built for eth0 and wlan0
  (`<prefix> dev <if> src <ip>` plus `default via <gw>`), with `from <ip>/32`
  rules (100/200) and `iif <if>` rules (110/210); occupied foreign priorities
  are skipped by picking a free number and remembering it in
  `/run/oko-route/policy.rules`. Rebuilds happen only when the address/gateway
  stamp changes, so a DHCP renewal does not flap the tables. `show` reports
  `eth_addr`, `wlan_addr`, `multihome`, `arp_tuning`, `rp_filter`,
  `policy_tables`; `/api/network` exposes `multihome`, `arp_tuning`,
  `policy_tables`, `eth_addr`, `wlan_addr`; the Network WebUI has a
  "Dual-interface protection" line (EN/RU). Bench 2026-11-12 (eth0 DHCP
  `192.168.52.33/24`, wlan0 static `192.168.52.44/24` in the same subnet):
  parallel ping 20/20 to each address, 0% loss; `ip route get <host> from
  <addr>` leaves through eth0/wlan0 respectively; on the PC `.33` resolves
  to the eth0 MAC and `.44` to the wlan0 MAC with no flap; the HTTP snapshot
  and RTSP DESCRIBE return 200 on both addresses; a dead eth gateway
  (iptables ICMP DROP) sets `eth_demoted=1` and moves the default to wlan0,
  and it returns when the DROP is removed; the `192.168.43.1` AP fallback
  works; static eth + static wlan in one subnet passes 10/10 pings to each
  address; three eth link down/up cycles leave both addresses alive; the
  netboot branch (faked `/proc/cmdline`) keeps the eth default and resolv
  untouched. Fixed a defect found here: `ip addr flush` in `oko-net apply`
  emptied table 100 while the stamp stayed the same — an empty table now
  forces a rebuild. `api_check` 105/0, `ui_check` passed,
  `check_image_contents` 30 required / 63 forbidden, and the image was
  flashed over TFTP. Not run: VPN over a degraded link, a real NFS boot,
  10 link cycles (3 were done), SoftAP client association.
- **Два аплинка: eth0 + Wi-Fi STA, приоритеты и failover (OKO).**
  Новый `/usr/bin/oko-route` — единый владелец маршрутов по умолчанию и
  `/etc/resolv.conf`. Приоритеты `oko_net_metric` (100) и `oko_wlan_metric`
  (600) хранятся в env U-Boot; демон из `S97okoroute` каждые 5 с
  пере-выравнивает метрики после renew DHCP, проверяет шлюз через
  `ping -I` (3 отказа → metric+10000, 2 успеха → возврат, cooldown 30 с) и
  при `root=/dev/nfs` не трогает eth-маршрут и resolv.conf. `/etc/resolv.conf`
  теперь пишет только `oko-route`: dhcpcd получил `nohook resolv.conf`, его
  резолверы забирает хук `/lib/dhcpcd/dhcpcd-hooks/20-oko-route`, резолверы
  wlan — из `/tmp/wlan.dns`; до трёх серверов, dedup, порядок по эффективным
  метрикам. Равные метрики автоматически разводятся (Wi-Fi = +1), потому что
  ядро держит один default на metric. В `/api/network` добавлены `metric`,
  `active_default`, `wlan_active`, `demoted`, `netboot`, в `/api/wifi` —
  `metric`; POST `metric=` пишет только приоритет. WebUI: поля
  «Приоритет (metric)» в карточках Ethernet и Wi-Fi и строка «Активный
  аплинк» с признаком failover. Стенд 2026-11-12: два default (100/600),
  WebUI с обоих адресов; падение eth-шлюза → demote eth до 10100 и трафик
  через wlan за ≤30 с, возврат без вмешательства; падение и возврат шлюза
  wlan; выдёргивание eth-линка; equal-metric 100/100 разводится до 100/101;
  netboot-ветка не меняет eth-метрику и resolv; 20-минутный soak с
  чередованием линков — без сбоев.
- **Dual uplink: eth0 + Wi-Fi STA, priorities and failover (OKO).**
  New `/usr/bin/oko-route` is the single owner of the default routes and
  `/etc/resolv.conf`. Priorities `oko_net_metric` (100) and
  `oko_wlan_metric` (600) live in the U-Boot env; the `S97okoroute` daemon
  re-aligns the metrics every 5 s after a DHCP renewal, probes the gateway
  with `ping -I` (three failures → metric+10000, two successes → restore,
  30 s cooldown) and leaves the Ethernet route and resolv.conf alone when
  `root=/dev/nfs`. `/etc/resolv.conf` is written only by `oko-route`: dhcpcd
  uses `nohook resolv.conf`, its resolvers are saved by
  `/lib/dhcpcd/dhcpcd-hooks/20-oko-route`, and the Wi-Fi resolvers come from
  `/tmp/wlan.dns`; up to three servers, de-duplicated, ordered by the
  effective metrics. Equal metrics are separated automatically (Wi-Fi = +1)
  because the kernel keeps one default per metric. `/api/network` now
  returns `metric`, `active_default`, `wlan_active`, `demoted` and `netboot`;
  `/api/wifi` returns `metric`; POST `metric=` stores only the priority. The
  WebUI has "Priority (metric)" fields in the Ethernet and Wi-Fi cards and an
  "Active uplink" line with the failover hint. Bench 2026-11-12: two defaults
  (100/600) and the WebUI on both addresses; a dead Ethernet gateway demotes
  eth to 10100 and traffic moves to wlan within 30 s, then returns on its
  own; the Wi-Fi gateway was dropped and restored; the eth link was
  pulled; equal metrics 100/100 become 100/101; the netboot branch keeps the
  eth metric and resolv untouched; a 20-minute soak with alternating links
  passed.

- **USB Wi-Fi: RTL8821CU, STA с SoftAP-фолбэком, WebUI и SD-конфиг.**
  В `sysdrv/drv_ko/wifi/rtl8821cu/` добавлен драйвер RTL8821CU/RTL8811CU
  (morrownr `8821cu-20210916`, GPL-2.0), он собирается для ядра SDK и
  копируется в `/oem/usr/ko/8821cu.ko`; `insmod_wifi.sh` распознаёт
  `0bda:c811` и родственные USB ID. Новый `/usr/bin/oko-wifi` хранит
  настройки в env U-Boot (`oko_wlan_*`), поднимает STA через
  `wpa_supplicant -D nl80211`, при неудаче — SoftAP `OKO-XXXX` на
  `192.168.43.1/24` с `dnsmasq`, умеет скан (`wpa_cli scan_results`) и
  однократный импорт `/mnt/sdcard/oko-wifi.conf` с переименованием в
  `.applied-<hash>`. Автозапуск — `S98okowifi`; из вендорского `S99hciinit`
  убрана ветка `ifconfig wlan0 up && udhcpc`. В divinus добавлены
  `/api/wifi`, `/api/wifi/scan`, `/api/wifi/apinfo`, в WebUI — карточка
  **Сеть → Wi-Fi** (EN/RU); в образ включён `wireless-regdb`. AP+STA
  отклоняется для этого драйвера (`ap_st_unsupported`). Проверено на стенде
  с `0bda:c811`: скан, STA (WPA2, DHCP, ping/HTTP/RTSP по wlan0),
  AP-фолбэк, статический IP, сохранение настроек после ребута, SD-импорт и
  20-минутный soak (20/20 проверок без потерь).
- **USB Wi-Fi: RTL8821CU, STA with SoftAP fallback, WebUI and SD config.**
  Added the RTL8821CU/RTL8811CU driver (morrownr `8821cu-20210916`,
  GPL-2.0) under `sysdrv/drv_ko/wifi/rtl8821cu/`; it builds against the SDK
  kernel and is installed as `/oem/usr/ko/8821cu.ko`. `insmod_wifi.sh` now
  detects `0bda:c811` and related USB IDs. The new `/usr/bin/oko-wifi` stores
  settings in the U-Boot env (`oko_wlan_*`), runs STA through
  `wpa_supplicant -D nl80211`, falls back to a SoftAP `OKO-XXXX` on
  `192.168.43.1/24` with `dnsmasq`, scans through `wpa_cli scan_results` and
  imports `/mnt/sdcard/oko-wifi.conf` once, renaming it to
  `.applied-<hash>`. Boot integration is `S98okowifi`; the vendor
  `S99hciinit` branch `ifconfig wlan0 up && udhcpc` was removed. divinus
  gained `/api/wifi`, `/api/wifi/scan` and `/api/wifi/apinfo`; the WebUI has
  a **Network → Wi-Fi** card (EN/RU), and `wireless-regdb` is part of the
  image. AP+STA is refused for this driver (`ap_st_unsupported`). Verified on
  the bench with `0bda:c811`: scan, STA (WPA2, DHCP, ping/HTTP/RTSP over
  wlan0), AP fallback, static IP, persistence across reboot, SD import and a 20-minute
  soak (20/20 checks without losses).

- **Прошивка EC: C0-цели STM32C011F6P6 (стенд) и STM32C031F6P6 (OKO).**
  В `project/app/stm32_acpi-ec/` добавлены `MCU=C011x6` (TSSOP20, 32 КиБ
  Flash, 6 КиБ SRAM) и `MCU=C031x6` (32 КиБ Flash, 12 КиБ SRAM):
  загрузчик 8 КиБ + приложение 20 КиБ, карта пинов и обвязка в
  `docs/hardware.md`, `cmake/FindSTM32CubeC0.cmake` и
  `platform/stm32c0/`. На стенде `st-info` определяет `STM32C011xx`
  (flash 32768, pagesize 2048, sram 6144). Приложение управляет питанием хоста
  (`PWR_EN`/`PWR_GOOD`/`HOST_WAKE`), кнопкой power и сном Stop;
  FSM `common/power_fsm.c` (short/long/very-long/double, graceful
  SLEEP/SHUTDOWN/REBOOT, power-cycle, boot-fail, LED-коды) покрыт
  `tests/test_power_fsm.c`. Протокол обновлён до **v3**:
  `GET_POWER 0x20`/`SET_POWER 0x21`, команды `0x01…0x0E`/`0x10…0x12`
  совместимы с proto-2. `hal/` разделён на `hal/include`, `hal/f1`,
  `hal/c0`; флаг OTA — линкер-символ `__ec_update_flag`; для C0 flash
  пишется double-word по 8 байт. Фактические размеры: boot 3 996 Б,
  app 7 628 Б (gcc 14.2.1). В C0 нет LPTIM, а RTC не имеет
  wakeup-таймера, поэтому Stop покидается только кнопкой или адресом I2C
  (отличие от первоначального плана зафиксировано в
  `docs/architecture.md`). Живая проверка на стенде: SWD-доступ получен,
  bootloader и app прошиты и верифицированы, валидная `MetaConfig`
  записана, приложение запускается из загрузчика, FSM в OFF,
  `PWR_EN=0`, I2C `PE=0`, backup-регистры сохраняются. Вход в Stop
  подтверждён (SCB->SCR.SLEEPDEEP=1, PC на WFI); попутно исправлен баг:
  pending-прерывание SysTick немедленно выводило ядро из Stop — SysTick
  теперь останавливается и сбрасывается перед WFI. Найден и исправлен
  дефекты C0 I2C: `TXIE` был активен до адреса (TXDR предзагружался
  `0xFF`, ответ начинался с лишнего байта), а выключение `TXIE` после
  последнего байта при `NOSTRETCH=0` удерживало SCL при чтении хостом
  большего буфера. Теперь `TXIE` включается только на ADDR чтения,
  отдаёт `0xFF`-filler до STOP, а на STOP/NACK/ошибке периферия
  переконфигурируется (PE=0/1), очищая TXDR и отпуская линию. Отдельно
  найдено, что `WUPEN` в рабочем режиме прижимал SCL на C011 — теперь он
  ставится только перед `WFI`. Живые проверки через I2C-мастер DUT
  пройдены: `info` (proto 3, 7628 Б), `battery`, `GET_POWER`, wake по
  адресу после Stop; физическая кнопка и ток Stop не проверялись.
- **EC firmware: C0 targets STM32C011F6P6 (bench) and STM32C031F6P6
  (OKO).** Added `MCU=C011x6` (TSSOP20, 32 KiB flash, 6 KiB SRAM) and
  `MCU=C031x6` (32 KiB flash, 12 KiB SRAM) to
  `project/app/stm32_acpi-ec/`: an 8 KiB bootloader plus a 20 KiB
  application, pin/wiring map in `docs/hardware.md`,
  `cmake/FindSTM32CubeC0.cmake` and `platform/stm32c0/`. The application
  drives host power (`PWR_EN`/`PWR_GOOD`/`HOST_WAKE`), the power button and
  Stop; `common/power_fsm.c` (short/long/very-long/double, graceful
  SLEEP/SHUTDOWN/REBOOT, power-cycle, boot-fail, LED codes) is covered by
  `tests/test_power_fsm.c`. The protocol is now **v3**:
  `GET_POWER 0x20`/`SET_POWER 0x21`; `0x01…0x0E`/`0x10…0x12` remain
  compatible with proto-2 hosts. `hal/` is split into `hal/include`,
  `hal/f1`, `hal/c0`; the OTA flag is the `__ec_update_flag` linker symbol;
  C0 flash is programmed in 8-byte double words. Measured sizes: boot
  3,996 B, app 7,628 B (gcc 14.2.1). The bench `st-info` reports
  `STM32C011xx` (flash 32768, pagesize 2048, sram 6144). C0 has no LPTIM
  and its RTC has no wakeup timer, so Stop is left only by the button or
  the I2C address (recorded as a deviation from the initial plan in
  `docs/architecture.md`). Live bench verification: SWD access obtained,
  bootloader and app flashed and verified, a valid `MetaConfig` was
  written, the application boots from the bootloader, the FSM is in OFF,
  `PWR_EN=0`, I2C `PE=0`, and the backup registers persist. Stop entry is
  confirmed (SCB->SCR.SLEEPDEEP=1, PC at WFI); a bug was fixed where a
  pending SysTick interrupt immediately woke the core — SysTick is now
  stopped and its pending flag cleared before WFI. A C0 I2C defect was
  found and fixed: `TXIE` was enabled before the address, so TXDR was
  preloaded with `0xFF` and every response had a leading byte; disabling
  `TXIE` after the last byte also let the slave hold SCL forever when the
  host read more bytes than the frame (`NOSTRETCH=0`). `TXIE` is now
  enabled only for a read after ADDR, stays on with `0xFF` filler until
  STOP, and STOP/NACK/error reconfigure the peripheral (PE=0/1) to clear
  TXDR and release the bus. `WUPEN` left set in run mode pulled SCL low on
  the C011 and killed the bus; it is now set only just before `WFI` and
  cleared after wake. Bench-verified: `oko-ec info` (proto 3, 7628 B),
  `battery`, `GET_POWER`, address wake after Stop. The physical button and
  Stop current were not tested.

- **Прошивка SPI NAND по TFTP — режим по умолчанию (OKO).**
  Новый `bench/tftp_flash.py`: входит в U-Boot по консоли (telnet-мост или
  `/dev/ttyACM*`), грузит `output/image/*.img` на `ramdisk_addr_r`, сверяет
  `Bytes transferred` с размером файла и пишет разделы `mtd erase`/`mtd write`
  по таблице offset/size (в этом U-Boot `CONFIG_CMD_MTDPARTS` выключен, `mtd`
  работает с целым `spi-nand0`), затем восстанавливает NAND-`sys_bootargs`,
  `saveenv` и `reset`. Флаги: `--components`, `--userdata`, `--no-reset`.
  Стенд 2026-11-11: записаны `boot`, `oem`, `rootfs`, плата загрузилась с
  `root=ubi0:rootfs`, TFTP-сеанс восстановил env после NFS-режима.
  USB и Maskrom не нужны; описано в `TESTBENCH.md` §7-TFTP, `README_RU.md`,
  `bench/README.md`, `.kilo/command/bench.md`.
- **TFTP flashing of SPI NAND is the default mode (OKO).**
  New `bench/tftp_flash.py`: enters U-Boot over the console (telnet bridge or
  `/dev/ttyACM*`), loads `output/image/*.img` to `ramdisk_addr_r`, checks
  `Bytes transferred` against the file size and writes the partitions with
  `mtd erase`/`mtd write` using the offset/size table (this U-Boot has
  `CONFIG_CMD_MTDPARTS` off, so `mtd` works on the whole `spi-nand0`), then
  restores the NAND `sys_bootargs`, runs `saveenv` and resets. Flags:
  `--components`, `--userdata`, `--no-reset`. Bench 2026-11-11: `boot`, `oem`
  and `rootfs` were written, the board booted with `root=ubi0:rootfs`, and the
  env was restored after an NFS session. USB and Maskrom are not needed; see
  `TESTBENCH.md` §7-TFTP, `README_RU.md`, `bench/README.md`,
  `.kilo/command/bench.md`.

- **USB host за хабом: HID, USB-сеть и Bluetooth (OKO).**
  Порт RV1106 переведён в dual-role (позже закреплён в host, см. «Изменено»): с OTG-переходником и внешним питанием он
  работает как host (хаб и устройства за ним), с кабелем к ПК остаётся device
  (gadget, `usb0`/adb). Новый фрагмент ядра `rv1106-usb-host.config` включает
  встроенными HID (`CONFIG_HID`, `CONFIG_USB_HID`), USB-сеть
  (`USB_NET_DRIVERS`, `USB_USBNET`, CDC ECM/NCM, RNDIS host, ASIX
  AX8817x/88179, RTL8150/8152) и Bluetooth (`BT`, `BT_LE`, `BT_HCIBTUSB` +
  RTL); в вендорском defconfig эти классы выключены, а драйвер хаба входит в
  usbcore и отдельного символа Kconfig не имеет. В rootfs добавлены `dbus`,
  `libglib2`, `bluez5_utils 5.86` (`bluetoothctl`, `S40bluetoothd`,
  `S30dbus-daemon`) и `linux-firmware` (RTL87xx/88xx BT, AR3012); `lsusb`
  уже даёт busybox. Для uclibc-ng без `wordexp` добавлен патч
  `patches-2026.08/bluez5_utils/0001-build-provide-wordexp-fallback-...`,
  неприменимые вендорские патчи bluez удалены из дерева. VBUS 5 В SoC не
  управляет (KI-054). Стенд 2026-11-10: device-роль и gadget (`usb0`
  `172.32.0.93`, перечисление на ПК), принудительный host и автоматический
  возврат gadget после host-режима проверены. 2026-11-11: с хабом
  `1a40:0101` автоопределение остаётся device, а в принудительном host хаб и
  устройство за ним (`0bda:c811`) перечисляются; HID/USB-сеть/BT не
  подключались, `bluetoothd`+`dbus-daemon`+`bluetoothctl 5.86` работают.
- **USB host behind a hub: HID, USB networking and Bluetooth (OKO).**
  The RV1106 port now runs in dual-role (later pinned to host, see "Changed"): with an OTG adapter and external power
  it is a host (a hub and the devices behind it), and with a cable to a PC it
  stays a device (gadget, `usb0`/adb). The new kernel fragment
  `rv1106-usb-host.config` builds in HID (`CONFIG_HID`, `CONFIG_USB_HID`), USB
  networking (`USB_NET_DRIVERS`, `USB_USBNET`, CDC ECM/NCM, RNDIS host, ASIX
  AX8817x/88179, RTL8150/8152) and Bluetooth (`BT`, `BT_LE`, `BT_HCIBTUSB` +
  RTL); the vendor defconfig disables these classes, and hub support lives in
  usbcore with no separate Kconfig symbol. The rootfs gains `dbus`,
  `libglib2`, `bluez5_utils 5.86` (`bluetoothctl`, `S40bluetoothd`,
  `S30dbus-daemon`) and `linux-firmware` (RTL87xx/88xx BT, AR3012); `lsusb`
  already comes from busybox. For uclibc-ng without `wordexp`, a
  `patches-2026.08/bluez5_utils/0001-build-provide-wordexp-fallback-...` patch
  was added and the inapplicable vendor bluez patches were dropped from the
  tree. The SoC does not control VBUS 5 V (KI-054). Bench 2026-11-10: the device
  role and gadget (`usb0` `172.32.0.93`, enumeration on a PC), forced host and
  the automatic gadget re-bind after a host excursion were verified. On
  2026-11-11 the hub `1a40:0101` keeps autodetection in the device role, while
  in forced host the hub and its downstream device (`0bda:c811`) enumerate; HID,
  USB networking and BT were not attached, while `bluetoothd` +
  `dbus-daemon` + `bluetoothctl 5.86` run.

- **Стенд: приёмник UDP/RTP MediaMTX и сквозная проверка раздачи (OKO).**
  `services/stream/` — отдельный профиль compose с закреплённым образом
  `bluenviron/mediamtx:1.21.3-ffmpeg` (host-сеть): путь `cam` слушает «сырой»
  RTP камеры на `5600/udp` (`rtpSDP` с `H265/90000`) и переиздаёт поток по
  RTSP `8554`, HLS `8888` и WebRTC `8889`; control API — только
  `127.0.0.1:9997`. Аутентификация стоковая (анонимные `publish/read/playback`),
  поэтому RTSP играется без пароля — только для лабораторного стенда.
  Новый `bench/stream_check.py` читает `stream`/`mp4` из
  `/userdata/divinus.yaml` по SSH, опрашивает API приёмника, декодирует
  `rtsp://<хост>:8554/cam` через PyAV, сверяет fps с `mp4.fps`, а с
  `--compare-dut` — с собственным RTSP камеры и оценивает задержку
  ретрансляции по совпадению кадров (единицы миллисекунд); артефакты — в
  `output/bench/stream`, учётные данные в них не попадают.
  Браузерный просмотр: с H.264 (`mp4.codec: H.264` + `rtpSDP H264/90000`)
  WebRTC-страница играет 1080p30 в Chromium, с H.265 (фабричный) — нет:
  WebRTC закрывается с `codecs not supported by client`, HLS отдаётся как
  `hvc1` и в MSE не воспроизводится (KI-053).
- **Bench: MediaMTX UDP/RTP receiver and end-to-end push check (OKO).**
  `services/stream/` is a separate compose profile with the pinned
  `bluenviron/mediamtx:1.21.3-ffmpeg` image (host networking): the `cam` path
  listens for the camera's raw RTP on `5600/udp` (`rtpSDP` with `H265/90000`)
  and re-publishes it over RTSP `8554`, HLS `8888` and WebRTC `8889`; the
  control API stays on `127.0.0.1:9997`. Authorization is the stock policy
  (anonymous `publish/read/playback`), so RTSP plays without credentials — a
  test-stand-only setting. The new `bench/stream_check.py` reads `stream`/`mp4`
  from `/userdata/divinus.yaml` over SSH, polls the receiver API, decodes
  `rtsp://<host>:8554/cam` with PyAV, compares fps with `mp4.fps` and, with
  `--compare-dut`, with the camera's own RTSP while estimating the relay delay
  by frame matching (a few milliseconds); artifacts go to
  `output/bench/stream` and never contain credentials. Browser playback:
  with H.264 (`mp4.codec: H.264` + `rtpSDP H264/90000`) the WebRTC page plays
  1080p30 in Chromium, with H.265 (factory default) it does not — WebRTC closes
  with `codecs not supported by client` and HLS is served as `hvc1`, which MSE
  rejects (KI-053).

### Изменено

- **Модели, события и второй рантайм в API.** `/api/yolo` отдаёт `arch`,
  `/api/models` — поле `arch` у каждой записи, `/api/status` — `features.clip`;
  `/api/models` принимает файлы до 4386 КиБ. `media/rknn2/README.md`,
  `tools/rknn-toolkit2/README.md`, `services/README.md`, `bench/README.md`,
  `STRUCTURE.md` и `endpoints.md` описывают 2.5.0, YOLO11 и сервис событий;
  в `divinus.yaml` добавлена секция `[event_clip]` с комментариями.
- **Models, events and the second runtime in the API.** `/api/yolo` reports
  `arch`, `/api/models` carries `arch` per record, `/api/status` reports
  `features.clip`, and `/api/models` accepts files up to 4386 KiB.
  `media/rknn2/README.md`, `tools/rknn-toolkit2/README.md`,
  `services/README.md`, `bench/README.md`, `STRUCTURE.md` and `endpoints.md`
  describe 2.5.0, YOLO11 and the events service; `divinus.yaml` gets a
  commented `[event_clip]` section.

- **DNS и default-маршрут: единый владелец (OKO).**
  `oko-wifi` больше не пишет `/etc/resolv.conf` и не хардкодит metric 600:
  default через wlan ставится с `oko_wlan_metric`, а DNS складывается в
  `/tmp/wlan.dns`. `oko-net` в static-режиме ставит default через
  `ip route replace ... metric oko_net_metric`, не перезаписывает
  resolv.conf и при apply останавливает только udhcpc, привязанный к eth0
  (lease wlan переживает смену Ethernet). Оба apply вызывают
  `oko-route once`; `S97okoroute` стартует до `S98okowifi`. В `INIT_ORDER.md`
  §4 таблица приведена к фактическому набору S*-скриптов (`S11modules`,
  `S30dbus-daemon`, `S40bluetoothd`, `S50crond`, `S97okoroute`).
- **DNS and default route: a single owner (OKO).**
  `oko-wifi` no longer writes `/etc/resolv.conf` and no longer hardcodes
  metric 600: the wlan default uses `oko_wlan_metric`, and its resolvers go
  to `/tmp/wlan.dns`. In static mode `oko-net` installs the default with
  `ip route replace ... metric oko_net_metric`, stops overwriting
  resolv.conf and kills only the udhcpc bound to eth0, so the wlan lease
  survives an Ethernet change. Both apply paths run `oko-route once`;
  `S97okoroute` starts before `S98okowifi`. The `INIT_ORDER.md` §4 table now
  matches the actual S* set (`S11modules`, `S30dbus-daemon`, `S40bluetoothd`,
  `S50crond`, `S97okoroute`).

- **USB-порт закреплён в host-режиме (`otg` → `host`) (OKO).**
  В активном DTS `dr_mode = "host"`: xHCI поднимается при загрузке, хаб
  `1a40:0101` и устройство за ним видны в `lsusb` без `otg_mode`. Gadget
  функций больше нет — UDC не регистрируется, `usb0`/RNDIS и adb недоступны;
  `S50usbdevice` получил guard по `dr_mode` (`usb gadget: dr_mode=host,
  skipping`), `S99usb0config` при `host` выходит сразу, а логика ролей
  сохранена для возврата к `otg`. Maskrom по USB виден только при прямом
  кабеле к хосту, поэтому прошивка по умолчанию переведена на TFTP (§7-TFTP).
  Стенд 2026-11-11: прошивка по TFTP, загрузка с `root=ubi0:rootfs`,
  `dr_mode = "host"`, хаб и `0bda:c811` в `lsusb`, UDC/`usb0`/adbd отсутствуют
  (KI-057). Обновлены `INIT_ORDER.md`, `FEATURES*.md`, `KNOWN_ISSUES.md`,
  `TESTBENCH.md`, `README_RU.md`, `STRUCTURE.md`.
- **USB port pinned to host mode (`otg` → `host`) (OKO).**
  The active DTS now has `dr_mode = "host"`: xHCI comes up at boot and the hub
  `1a40:0101` with its downstream device appear in `lsusb` without `otg_mode`.
  The gadget is gone — no UDC, no `usb0`/RNDIS and no adb; `S50usbdevice` got a
  `dr_mode` guard (`usb gadget: dr_mode=host, skipping`) and `S99usb0config`
  exits immediately on `host`, while the role logic is kept for returning to
  `otg`. Maskrom over USB is only visible with a direct cable to the host, so
  the default flashing mode moved to TFTP (§7-TFTP). Bench 2026-11-11: flashed
  over TFTP, booted with `root=ubi0:rootfs`, `dr_mode = "host"`, hub and
  `0bda:c811` in `lsusb`, no UDC/`usb0`/adbd (KI-057). Updated `INIT_ORDER.md`,
  `FEATURES*.md`, `KNOWN_ISSUES.md`, `TESTBENCH.md`, `README_RU.md`,
  `STRUCTURE.md`.

- **Стенд: консоль DUT через ESP32 telnet-UART bridge (OKO).**
  UART RV1106 теперь доступен по telnet `192.168.52.17:23` (115200 8N1).
  Новый `bench/uart.py` — минимальный telnet-клиент: отвечает на
  IAC-согласование (WILL ECHO/SGA → DONT/WONT), вырезает IAC/SB из потока и
  отдаёт pyserial-совместимый интерфейс; `serial_exec.py` и `netboot.py`
  используют мост по умолчанию, локальный `/dev/ttyACM*` включается через
  `--port /dev/ttyACM0` или `OKO_SERIAL_PORT`; мост обслуживает одного
  клиента, поэтому перед запуском скриптов нужно закрыть другие telnet-сессии.
  Обновлены `TESTBENCH.md`
  (§1/§2/§4/§5), `README_RU.md`, `AGENTS.md` и `bench/README.md`; в §4
  зафиксировано, что USB-хаб подключён. Проверено на стенде 2026-11-11:
  `bench/run.sh serial_exec.py 'uname -a'` логинится через мост и выполняет
  команду (kernel `#12`).
- **Bench: the DUT console moved to the ESP32 telnet-UART bridge (OKO).**
  The RV1106 UART is now reachable over telnet at `192.168.52.17:23`
  (115200 8N1). The new `bench/uart.py` is a minimal telnet client: it answers
  IAC negotiation (WILL ECHO/SGA → DONT/WONT), strips IAC/SB from the stream
  and exposes a pyserial-compatible interface; `serial_exec.py` and
  `netboot.py` use the bridge by default, while a local `/dev/ttyACM*` is
  selected with `--port /dev/ttyACM0` or `OKO_SERIAL_PORT`; the bridge serves a
  single client, so other telnet sessions must be closed before running the
  scripts. `TESTBENCH.md`
  (§1/§2/§4/§5), `README_RU.md`, `AGENTS.md` and `bench/README.md` were
  updated, and §4 now records the connected USB hub. Bench 2026-11-11:
  `bench/run.sh serial_exec.py 'uname -a'` logs in through the bridge and runs
  the command (kernel `#12`).

- **`S99usb0config` определяет USB-роль по extcon (OKO).**
  Скрипт больше не проверяет строку `dr_mode` в DTS: роль читается из
  `/sys/class/extcon/*/state`, в device-роли назначается `172.32.0.93`, а при
  возврате из host-режима заново биндится configfs-gadget (DWC3 снимает UDC и
  сам его не возвращает). Прежние повторы установки IP сохранены, watchdog
  опрашивает роль раз в 5 с, а при устойчивой ошибке биндинга пауза между
  перезапусками растёт 5→60 с. Проверено на стенде: два цикла host→device —
  UDC `configured`, gadget снова виден на ПК.
- **`S99usb0config` derives the USB role from extcon (OKO).**
  The script no longer reads `dr_mode` from the DTS: the role comes from
  `/sys/class/extcon/*/state`, the device role gets `172.32.0.93`, and after a
  host excursion the configfs gadget is bound again (DWC3 drops its UDC and
  does not restore it). The previous IP retries are kept, the watchdog polls the
  role every 5 s, and a persistent bind failure backs off from 5 to 60 s.
  Bench: two host→device cycles — UDC `configured` and the gadget visible on
  the PC again.

- **Активный DTS и BoardConfig: USB-порт в `otg` (OKO).**
  В `rv1106g-luckfox-pico-pro-max-sc230ai.dts` у `&usbdrd_dwc3` значение
  `dr_mode` изменено с `"peripheral"` на `"otg"` с комментарием OKO;
  `&u2phy`, `&u2phy_otg` и `&usbdrd` не менялись. Список
  `RK_KERNEL_DEFCONFIG_FRAGMENT` активного BoardConfig дополнен
  `rv1106-usb-host.config` (после sensor-фрагмента); прочие DTS и BoardConfig
  не затронуты. Роль в `S99usb0config` определяется по extcon (см. запись
  выше), `S50usbdevice` продолжает биндить gadget.
- **Active DTS and BoardConfig: USB port in `otg` mode (OKO).**
  In `rv1106g-luckfox-pico-pro-max-sc230ai.dts` the `&usbdrd_dwc3` node now
  has `dr_mode = "otg"` instead of `"peripheral"`, with an OKO comment;
  `&u2phy`, `&u2phy_otg` and `&usbdrd` are unchanged. The active BoardConfig
  `RK_KERNEL_DEFCONFIG_FRAGMENT` list gains `rv1106-usb-host.config` (after the
  sensor fragment); other DTS files and BoardConfig profiles are untouched.
  `S99usb0config` now derives the role from extcon (see the entry above), and
  `S50usbdevice` keeps binding the gadget.

- **Собственный лаунчер `oko-lunch` (OKO).** Новый пакет `project/app/oko-lunch`
  ставит в `/oem/usr/bin` скрипты `oko-lunch.sh` и `oko-lunch-stop.sh`; тело
  перенесено из вендорского `RkLunch.sh` с OKO-правками (`divinus` как
  единственный стример, `oko-net apply`, пропуск DHCP в netboot, запуск из
  `/userdata`). Пакет собирается общим циклом `project/app/Makefile`.
- **Standalone `oko-lunch` launcher (OKO).** The new `project/app/oko-lunch`
  package installs `oko-lunch.sh` and `oko-lunch-stop.sh` into
  `/oem/usr/bin`; the body is carried over from the vendor `RkLunch.sh` with
  the OKO changes (`divinus` as the only streamer, `oko-net apply`, DHCP skip
  in netboot, start from `/userdata`). The package is built by the shared
  `project/app/Makefile` loop.

- **Обновление ПО через WebUI (OTA, OKO).** В разделе «Система» появилась
  карточка «Обновление ПО»: загрузка `.okp` файлом (потоковый приём тела в
  `server.c` в обход буфера 4 МиБ) или скачивание по `http(s)://`
  (`curl`, адреса устройства отклоняются), проверка манифеста, размеров и
  sha256, подготовка `sd_update.txt` и образов на microSD, одноразовый токен и
  `confirm=APPLY`. Прошивку `boot`/`oem`/`rootfs` выполняет штатный U-Boot
  `sd_update` после перезагрузки; каждая строка скрипта обёрнута в
  `if env exists oko_update; then … fi`, поэтому подготовленный, но не
  подтверждённый пакет при обычной перезагрузке не прошивается; `env`/`idblock`/`uboot`/`userdata` не
  затрагиваются. Новые `update.c/.h`, `/usr/bin/oko-update`,
  `/etc/init.d/S20oko-update`, `/etc/oko-release`, секция `[update]`
  (`enable`, `max_package_mb`, `allow_insecure_url`), гейт `update` и хостовая
  утилита `tools/native/make_oko_update.sh`. `S20oko-update` сохраняет
  результат в `/userdata/oko-update.state` и очищает карту, поэтому оставленная
  карта не перепрошивает плату повторно. Гейтовые эксперименты (env-флаг,
  `mmc 1`, HTTPS, перемонтирование `/oem`) и полный цикл на стенде **не
  выполнены** — DUT не подключён; статус в `FEATURES_UNVERIFIED.md` (KI-049).
- **Software update from the WebUI (OTA, OKO).** The System section gained a
  "Software update" card: upload an `.okp` file (the body is streamed in
  `server.c`, bypassing the 4 MiB request buffer) or download it over
  `http(s)://` (`curl`; URLs pointing at the device are refused), verify the
  manifest, sizes and sha256, stage `sd_update.txt` and the images on the
  microSD card, with a one-time token and `confirm=APPLY`. The stock U-Boot
  `sd_update` flashes `boot`/`oem`/`rootfs` after the reboot; every script line
  is wrapped in `if env exists oko_update; then … fi`, so a staged but
  unconfirmed package is not flashed by an ordinary reboot; `env`/`idblock`/
  `uboot`/`userdata` are untouched. New `update.c/.h`, `/usr/bin/oko-update`,
  `/etc/init.d/S20oko-update`, `/etc/oko-release`, the `[update]` section
  (`enable`, `max_package_mb`, `allow_insecure_url`), the `update` gate and the
  host tool `tools/native/make_oko_update.sh`. `S20oko-update` stores the result
  in `/userdata/oko-update.state` and clears the card, so a card left in the
  slot cannot reflash the board. The gating experiments (env flag, `mmc 1`,
  HTTPS, `/oem` remount) and a full bench cycle were **not run** — the DUT was
  disconnected; status in `FEATURES_UNVERIFIED.md` (KI-049).

- **Поддержка часовых поясов (OKO).** Образ buildroot ставит набор зон
  (`BR2_TARGET_TZ_INFO`, `BR2_TARGET_TZ_ZONELIST="default"`,
  `BR2_TARGET_LOCALTIME="Europe/Moscow"`) в `/usr/share/zoneinfo/uclibc`, а
  `/etc/TZ` — относительный симлинк на выбранную зону. Новый ключ `[system] tz`
  в `divinus.yaml` (дефолт `Europe/Moscow`) и параметр `tz=` в
  `GET/POST /api/time`: имя валидируется (IANA, без `..` и ведущего `/`, зона
  должна существовать), при смене пояс применяется к `/etc/TZ`, конфигурация
  автосохраняется и приложение перезапускается graceful SIGHUP (uClibc кэширует
  зону на процесс). Новый init-скрипт `S20okotz` применяет сохранённый `tz:` при
  загрузке. WebUI «Синхронизация времени» получил список зон и подсказку о
  рестарте; `bench/api_check.py` — проверки tz, `TESTBENCH.md` §8 — read-only
  диагностику пояса. `GET /api/time` дополнительно отдаёт `tz_current` —
  фактическую зону из `/etc/TZ`, WebUI показывает её в карточке NTP рядом с
  выбором; страница WebUI отдаётся с `Cache-Control: no-cache`, чтобы новая
  версия интерфейса подхватывалась без жёсткого обновления.
- **Time zone support (OKO).** The buildroot image now installs the zone set
  (`BR2_TARGET_TZ_INFO`, `BR2_TARGET_TZ_ZONELIST="default"`,
  `BR2_TARGET_LOCALTIME="Europe/Moscow"`) under `/usr/share/zoneinfo/uclibc`
  with `/etc/TZ` as a relative symlink. New `[system] tz` key in
  `divinus.yaml` (default `Europe/Moscow`) and `tz=` parameter in
  `GET/POST /api/time`: the name is validated (IANA, no `..`, no leading `/`,
  the zone must exist); on change the zone is applied to `/etc/TZ`, the config
  is auto-saved and the application restarts with a graceful SIGHUP (uClibc
  caches the zone per process). New `S20okotz` init script applies the saved
  `tz:` at boot. The WebUI "Time synchronization" card gained a zone list and a
  restart hint; `bench/api_check.py` covers tz, and `TESTBENCH.md` §8 has
  read-only zone diagnostics. `GET /api/time` also returns `tz_current`, the
  effective zone read from `/etc/TZ`; the WebUI shows it in the NTP card next
  to the selector, and the page is served with `Cache-Control: no-cache` so a
  new UI version is picked up without a hard refresh.

- **`bench/nfs_env.py` — переключение NAND ↔ NFS через env U-Boot (OKO).**
  Режимы `status` (текущий `sys_bootargs`, распознанный `nand`/`nfs` и
  сверка с `output/image/.env.txt`), `on` (`fw_setenv sys_bootargs` со
  строкой `root=/dev/nfs … ip=…`, перезагрузка, ожидание SSH и проверка
  `/`, `/oem`, `/userdata` как nfs3 с адреса NFS-сервера), `off` (возврат
  на `ubi.mtd=6 root=ubi0:rootfs …`, проверка cmdline и ubifs). Флаги
  `--host`, `--no-reboot`, `--timeout`; адреса и CMA — из тех же
  переменных, что у `netboot.py` (`OKO_NFS_IP`, `OKO_TFTP_HOST`,
  `OKO_GATEWAY`, `OKO_NFSROOT`, `OKO_CMA_SIZE`). Логи —
  `output/bench/nfs-env-<stamp>.log`.
- **`bench/nfs_env.py` switches NAND ↔ NFS through the U-Boot env (OKO).**
  Modes: `status` (current `sys_bootargs`, recognised `nand`/`nfs` and a
  comparison with `output/image/.env.txt`), `on` (`fw_setenv sys_bootargs`
  with `root=/dev/nfs … ip=…`, reboot, wait for SSH and verify `/`, `/oem`,
  `/userdata` as nfs3 from the NFS server), `off` (back to
  `ubi.mtd=6 root=ubi0:rootfs …`, verify the cmdline and ubifs). Flags
  `--host`, `--no-reboot`, `--timeout`; addresses and CMA come from the
  same variables as `netboot.py` (`OKO_NFS_IP`, `OKO_TFTP_HOST`,
  `OKO_GATEWAY`, `OKO_NFSROOT`, `OKO_CMA_SIZE`). Logs go to
  `output/bench/nfs-env-<stamp>.log`.

- **Возвращён netboot-контур TFTP/NFS (OKO).** `bench/netboot.py`
  (режимы `catch`/`reboot`/`cmd`, определение shell-приглашения
  `root@luckfox`), фрагмент ядра `rv1106-netboot.config` (IP_PNP,
  ROOT_NFS), конфиг
  `BoardConfig-SPI_NAND-Buildroot-RV1106_Luckfox_Pico_Pro_Max-SC230AI-NETBOOT.mk`
  (оба фрагмента: netboot и SC230AI), NFS-ветки `oem`/`userdata` в
  S20linkmount (`project/build.sh`) и пропуск DHCP в `RkLunch.sh` при
  `nfsroot=` в cmdline. Хостовые сервисы настраивает
  `tools/native/setup_netboot.sh`: `tftpd-hpa` с корнем
  `<repo>/output/image` (chroot, `ProtectHome=read-only`, ACL на
  обход домашнего каталога) и NFS-экспорт `/srv/nfs/rootfs` для
  `192.168.52.0/24`. Первый прогон `netboot.py reboot`: TFTP boot.img,
  NFS-root и статический `192.168.52.150` подтверждены на DUT.
- **The TFTP/NFS netboot loop is back (OKO).** `bench/netboot.py`
  (`catch`/`reboot`/`cmd` modes, detects the `root@luckfox` shell
  prompt), the `rv1106-netboot.config` kernel fragment (IP_PNP,
  ROOT_NFS), the
  `BoardConfig-SPI_NAND-Buildroot-RV1106_Luckfox_Pico_Pro_Max-SC230AI-NETBOOT.mk`
  board config (both the netboot and SC230AI fragments), the
  `oem`/`userdata` NFS branches in the S20linkmount generator
  (`project/build.sh`) and the DHCP skip in `RkLunch.sh` when
  `nfsroot=` is present in the cmdline. Host services are configured by
  `tools/native/setup_netboot.sh`: `tftpd-hpa` rooted at
  `<repo>/output/image` (chroot, `ProtectHome=read-only`, ACLs for the
  home directory traversal) and the `/srv/nfs/rootfs` export for
  `192.168.52.0/24`. The first `netboot.py reboot` run confirmed TFTP
  boot.img, the NFS root and the static `192.168.52.150` on the DUT.

- **Нативная сборка SDK на Debian 13 (OKO).** Добавлены
  `tools/native/setup_host.sh` (apt-чек-лист бывшего контейнера с именами
  пакетов trixie: `libncurses-dev`, `u-boot-tools`, `ninja-build`,
  `squashfs-tools`, `mtd-utils`; проверка `ldd`/запуска вендорского
  кросс-тулчейна), `tools/native/env.sh` (`PATH`, `CROSS_COMPILE`, `ARCH`,
  `LANG/LC_ALL=C.UTF-8`, `sbin` в `PATH`) и `tools/native/smoke.sh` (отчёт
  `output/bench/native/smoke.log`: host-утилиты, кросс-gcc 8.3.0,
  `upgrade_tool ld`). Хост стенда — Debian 13 trixie, gcc 14.2; тулчейн
  `arm-rockchip830` запускается без библиотек совместимости.
- **Native SDK build on Debian 13 (OKO).** Added
  `tools/native/setup_host.sh` (the former container apt checklist with
  trixie package names: `libncurses-dev`, `u-boot-tools`, `ninja-build`,
  `squashfs-tools`, `mtd-utils`; verifies the vendor cross toolchain with
  `ldd` and a test run), `tools/native/env.sh` (`PATH`, `CROSS_COMPILE`,
  `ARCH`, `LANG/LC_ALL=C.UTF-8`, `sbin` in `PATH`) and
  `tools/native/smoke.sh` (report in `output/bench/native/smoke.log`: host
  tools, cross gcc 8.3.0, `upgrade_tool ld`). The bench host is Debian 13
  trixie with gcc 14.2; the `arm-rockchip830` toolchain runs without
  compatibility libraries.

- **OpenVPN-клиент с управлением из WebUI (OKO).** Третий тип профиля
  `ovpn`: `oko-vpn` разбирает клиентский конфиг OpenVPN, отклоняет
  `dev tap`, `plugin`, `management`, скриптовые директивы, `askpass`,
  `config` и `auth-user-pass <файл>`, принудительно ставит
  `redirect-gateway def1`, запускает демон `/usr/sbin/openvpn` с
  pid/status-файлами, ждёт `Initialization Sequence Completed` и адрес
  `tun0`, а для full tunnel выполняет тот же 15-секундный guard с откатом.
  Профили и учётные данные — `/userdata/vpn/<name>.conf` и
  `<name>.pass` (0600); `S45oko-vpn` подхватывает `tun0` в autostart и
  watchdog по pid. Импорт через `/api/vpn/import` принимает поля
  `user`/`pass` (multipart и form), WebUI показывает их только для типа
  `ovpn`; `GET /api/vpn` отдаёт `ovpn_bin`, `tunip`, `rx/tx` и
  `handshake_age: null`. Сборка: `BR2_PACKAGE_OPENVPN=y` (openvpn 2.5.7,
  OpenSSL, LZO/LZ4) в `luckfox_pico_defconfig` и `CONFIG_TUN=y` в
  `luckfox_rv1106_linux_defconfig`. Обновлены `FEATURES*.md`,
  `INIT_ORDER.md` (§4: `S60openvpn` неактивен), `bench/README.md` и
  хостовый тест `vpn_conf_test.c` (детект, отказы, `def1`). Стенд
  2026-11-08: внешний сервер OpenVPN (`192.168.52.200:1194`), inline
  сертификаты + `tls-crypt` + `auth-user-pass`, `tun0` 10.8.0.3/24, ping
  через туннель, iperf3 21–23 Мбит/с, full tunnel `def1` с guard, `down`
  без маршрутов, autostart после reboot; `vpn_check.py` 23/23 и 21/21,
  регрессия WG 19/19, `api_check` 98/0, `ui_check` зелёный.
- **OpenVPN client managed from the WebUI (OKO).** A third profile type
  `ovpn`: `oko-vpn` parses an OpenVPN client configuration, rejects
  `dev tap`, `plugin`, `management`, script directives, `askpass`,
  `config` and `auth-user-pass <file>`, forces `redirect-gateway def1`,
  starts the `/usr/sbin/openvpn` daemon with pid/status files, waits for
  `Initialization Sequence Completed` and a `tun0` address, and runs the
  same 15-second full-tunnel guard with a rollback. Profiles and
  credentials live in `/userdata/vpn/<name>.conf` and `<name>.pass`
  (0600); `S45oko-vpn` covers `tun0` in autostart and the pid-based
  watchdog. `/api/vpn/import` accepts `user`/`pass` (multipart and form),
  the WebUI shows them only for `ovpn`, and `GET /api/vpn` exposes
  `ovpn_bin`, `tunip`, `rx/tx` and `handshake_age: null`. Build:
  `BR2_PACKAGE_OPENVPN=y` (openvpn 2.5.7, OpenSSL, LZO/LZ4) in
  `luckfox_pico_defconfig` and `CONFIG_TUN=y` in
  `luckfox_rv1106_linux_defconfig`. `FEATURES*.md`, `INIT_ORDER.md`
  (§4: inactive `S60openvpn`), `bench/README.md` and the host test
  `vpn_conf_test.c` (detection, rejects, `def1`) are updated. Bench
  2026-11-08: an external OpenVPN server (`192.168.52.200:1194`) with inline
  certificates, `tls-crypt` and `auth-user-pass`, `tun0` 10.8.0.3/24, a
  tunnel ping, iperf3 21–23 Mbit/s, full tunnel `def1` with the guard,
  route-free `down` and autostart after a reboot; `vpn_check.py` 23/23 and
  21/21, the WG regression 19/19, `api_check` 98/0 and `ui_check` passed.
- **Сводка microSD в OSD (OKO).** `oko-osd` дописывает
  ` SD FREE <free>/<cap>GiB` в строку `MEM FREE` региона `reg4` (свободно
  первым, ёмкость второй, один знак после точки — как в WebUI); без
  смонтированной карты выводится `SD FREE -`. Источник — `df -k /mnt/sdcard`
  с проверкой монтирования по `/proc/mounts`, тот же паттерн, что у NPU:
  строк остаётся три, регион не обрезается. Стенд: `MEM FREE 77% SD FREE
  14.8/14.8GiB` совпало с `/api/status` (`15171/15177 MiB`), `umount` дал
  `SD FREE -`, после `mount` числа вернулись; `ui_check` и `api_check`
  (97/0) зелёные.
- **microSD summary in OSD (OKO).** `oko-osd` appends
  ` SD FREE <free>/<cap>GiB` to the `MEM FREE` line of `reg4` (free first,
  capacity second, one decimal, as in the WebUI); without a mounted card it
  reports `SD FREE -`. The source is `df -k /mnt/sdcard` guarded by
  `/proc/mounts`, the same trick as the NPU suffix: the file keeps three lines
  and the region does not clip. Bench result: `MEM FREE 77% SD FREE
  14.8/14.8GiB` matched `/api/status` (`15171/15177 MiB`), `umount` produced
  `SD FREE -`, and the numbers returned after `mount`; `ui_check` and
  `api_check` (97/0) passed.
- **Наблюдаемость ePTZ и геометрия оверлеев (OKO).** `rk_vpss_set_chn_crop`
  возвращает числовой код MPP, печатает его один раз и сохраняется в
  `media.c` под мьютексом (`media_eptz_last`); `/api/eptz` отдаёт фактические
  `applied`, `rc`, `chn` и `crop` и `note` при отказе. Кроп и поворот
  применяются в `rk_channel_bind()` сразу после `EnableChn` — при каждом
  старте канала (включение канала сбрасывает их). Новый модуль
  `analytics/nn_geom.c` (хост-тест `tests/nn_geom_test.c`) пересчитывает
  координаты детекций с учётом кропа и поворота 0/90/180/270; рамки
  выравниваются по 4 px вместо сетки 16 px. Текстовый OSD переякоривается при
  90/270 (`nn_map_anchor` в `rk_region_create2`), сохраняя угол и отступ.
  Новый `bench/geometry_check.py` (`--what zoom|boxes|osd`) и opt-in
  `OKO_GEOMETRY_TESTS=1`; в предпросмотре WebUI — бейдж `#preview-mode`
  («MP4 · main», «MJPEG · отдельный канал», «стоп-кадр · JPEG»,
  «остановлено») и подсказка, что зум/поворот применяются к выбранному потоку.
- **ePTZ observability and overlay geometry (OKO).**
  `rk_vpss_set_chn_crop` returns the numeric MPP code, logs it once and stores
  it in `media.c` under a mutex (`media_eptz_last`); `/api/eptz` now reports
  the actual `applied`, `rc`, `chn` and `crop` plus a `note` on failure. The
  crop and the rotation are applied in `rk_channel_bind()` right after
  `EnableChn` — on every channel start (the enable resets them). The new
  `analytics/nn_geom.c` module (host test `tests/nn_geom_test.c`) maps
  detection coordinates through the crop and the 0/90/180/270 rotation; boxes
  are aligned to 4 px instead of the 16 px grid. OSD text is re-anchored at
  90/270 (`nn_map_anchor` in `rk_region_create2`) and keeps its corner and
  margin. New `bench/geometry_check.py` (`--what zoom|boxes|osd`) and the
  opt-in `OKO_GEOMETRY_TESTS=1`; the WebUI preview gained the `#preview-mode`
  badge ("MP4 · main", "MJPEG · separate channel", "still · JPEG", "stopped")
  and a note that zoom/rotation apply to the selected stream.
- **Автофокус по AF-метрике (OKO).** Новый модуль `focus_af.c` выполняет
  поиск фокуса по статистике `metric_avg` (вариант B из `TO_DO.md` §5):
  зонд направления половинным шагом, coarse-проход с остановкой после
  `af_patience` неулучшений или у границы, fine-проход между лучшими
  точками и переход к лучшей позиции. После каждого хода — пауза
  `af_settle_ms` и медиана трёх свежих кадров; сырое среднее используется
  потому, что `metric_smooth` — EMA и отстаёт на несколько кадров. Поиск
  ограничен `[max(0, min_pos)+home_margin, max_pos-home_margin]` (KI-042:
  отрицательные позиции не командуются), `af_timeout_s` и проверкой
  `status==error`; на плоской сцене возвращается ошибка «peak not found».
  `POST /api/focus?af=1` запускает job в том же запросе, что и
  `enable/mode/roi/fps/motor=1`, `af=cancel` и `stop=1` отменяют его,
  `GET /api/focus` отдаёт блок `af` (`state`, `position`, `best_position`,
  `metric`, `best_metric`, `progress`, `done`, `error`). Взаимные
  блокировки: AF не стартует при home/zero или прошивке контроллера,
  home/zero/ручные движения и `POST /api/flash` получают 409 при активном
  поиске. Параметры `[focus]`: `af_coarse=150`, `af_fine=25`,
  `af_patience=3`, `af_settle_ms=600`, `af_timeout_s=60`,
  `af_min_gain_pct=2`. Чистые решающие правила вынесены в
  `focus_af_algo.c` и покрыты `tests/focus_af_test.c`; обновлены
  `endpoints.md`, `README.md` divinus, `TO_DO.md`, `STRUCTURE.md` и
  `FEATURES_UNVERIFIED.md`. На стенде 2026-11-07 обе ветки (`full`/`roi`)
  завершились `done`, но метрика не зависит от позиции мотора (объектив
  стенда не связан с приводом), поэтому сходимость не подтверждена;
  кривые — в `output/bench/af_curve_*.csv`.
- **Auto-focus over the AF metric (OKO).** The new `focus_af.c` searches for
  focus using the `metric_avg` statistics (variant B in `TO_DO.md` §5): a
  half-step direction probe, a coarse pass that stops after `af_patience`
  non-improvements or at a travel limit, a fine pass between the best points
  and a move to the best position. Each move is followed by an
  `af_settle_ms` pause and the median of three fresh frames; the raw average
  is used because `metric_smooth` is an EMA that lags several frames. The
  search is bounded by `[max(0, min_pos)+home_margin, max_pos-home_margin]`
  (KI-042: negative positions are never commanded), `af_timeout_s` and a
  `status==error` check; a flat scene returns "peak not found".
  `POST /api/focus?af=1` starts the job in the same request as
  `enable/mode/roi/fps/motor=1`, `af=cancel` and `stop=1` cancel it, and
  `GET /api/focus` exposes an `af` block (`state`, `position`,
  `best_position`, `metric`, `best_metric`, `progress`, `done`, `error`).
  Mutual exclusions: AF does not start during home/zero or a controller
  flash, and home/zero/manual moves and `POST /api/flash` return 409 while a
  search runs. `[focus]` keys: `af_coarse=150`, `af_fine=25`,
  `af_patience=3`, `af_settle_ms=600`, `af_timeout_s=60`,
  `af_min_gain_pct=2`. The pure decision rules live in `focus_af_algo.c` and
  are covered by `tests/focus_af_test.c`; `endpoints.md`, divinus
  `README.md`, `TO_DO.md`, `STRUCTURE.md` and `FEATURES_UNVERIFIED.md` are
  updated. On the bench (2026-11-07) both `full` and `roi` finished `done`,
  but the metric does not depend on the motor position (the bench lens is not
  coupled to the drive), so convergence is not confirmed; the curves are in
  `output/bench/af_curve_*.csv`.
- **Micro-fine автофокус и мобильный режим WebUI (OKO).** `POST
  /api/focus?af=fine` запускает локальную донастройку: окно
  `[pos−af_coarse, pos+af_coarse]` с клампом по безопасным границам, шаг
  `af_fine`, безусловный `done` без проверки `af_min_gain_pct`; в
  `GET /api/focus` добавлено поле `af.scope` (`full`/`fine`). В карточке
  Live появилась кнопка «Автофокус» рядом с jog-кнопками; на время job'а
  она блокируется вместе с остальными ручными кнопками, STOP по-прежнему
  отменяет поиск. Чистая функция `af_window()` покрыта
  `tests/focus_af_test.c`. На ширине ≤ 40em WebUI автоматически включает
  мобильный режим: нижняя навигация 4×2 с короткими подписями
  (`nav_short_*`), кнопки не ниже 44 px, компактная статус-строка (только
  IP и SD), тосты над навбаром и `font-size: 16px` у полей (анти-zoom iOS).
  Обновлены `bench/api_check.py` (`af=fine`, кривая
  `output/bench/af_curve_fine_*.csv`), `bench/ui_check.py` (раскладка всех
  вкладок на 1440×900 и 390×844, две колонки Live, нижний навбар, кнопка
  «Автофокус»), `bench/README.md`, `endpoints.md`, divinus `README.md`,
  `WEBUI_REORG.md`, `TO_DO.md`, `FEATURES_UNVERIFIED.md` и `TESTBENCH.md`.
- **Micro-fine auto-focus and the mobile WebUI (OKO).** `POST
  /api/focus?af=fine` runs a local correction: the window
  `[pos−af_coarse, pos+af_coarse]` clamped to the safe travel bounds, the
  `af_fine` step and an unconditional `done` without the `af_min_gain_pct`
  check; `GET /api/focus` gained `af.scope` (`full`/`fine`). The Live card
  has an Autofocus button next to the jog buttons; it is locked together with
  the other manual controls while a job runs, and STOP still cancels the
  search. The pure `af_window()` helper is covered by
  `tests/focus_af_test.c`. At ≤ 40em the WebUI switches to the mobile mode
  automatically: a bottom 4×2 navbar with short labels (`nav_short_*`),
  targets of at least 44 px, a trimmed status line (IP and SD only), toasts
  above the navbar and 16 px form fields (anti-zoom on iOS). Updated
  `bench/api_check.py` (`af=fine`, `output/bench/af_curve_fine_*.csv`),
  `bench/ui_check.py` (all tabs fit at 1440×900 and 390×844, two-column
  Live, bottom navbar, Autofocus button), `bench/README.md`, `endpoints.md`,
  divinus `README.md`, `WEBUI_REORG.md`, `TO_DO.md`,
  `FEATURES_UNVERIFIED.md` and `TESTBENCH.md`.
- **WebUI: объём и свободное место SD-карты в «Система → Статус» (OKO).**
  Рядом со строкой «Память» добавлены две read-only строки,
  `#status_sd_total_mb`/`#status_sd_free_mb`, заполняемые из уже
  существующих полей `/api/status` (`sd_total_mb`/`sd_free_mb`, MiB от
  `statvfs`). Хук `fmtMiB` печатает `GiB`/`MiB` и прочерк, когда карта
  отсутствует или не смонтирована (сервер отдаёт `0`); строки не входят в
  dirty-state и не добавляют кнопок `Apply`. Под карточкой — пояснение, что
  объём относится к смонтированному тому FAT32, а не к паспортной ёмкости
  карты. Статус-строка `#sb_sd` не менялась. Обновлены `endpoints.md`,
  divinus `README.md`, `WEBUI_REORG.md` и `bench/README.md`;
  `bench/ui_check.py` сверяет тексты с `window.lastStatus` и проверяет
  формат, отсутствие карты не требуется.
- **WebUI: SD card capacity and free space in System → Status (OKO).** Two
  read-only rows, `#status_sd_total_mb`/`#status_sd_free_mb`, sit next to
  Memory and are filled from the existing `/api/status` fields
  (`sd_total_mb`/`sd_free_mb`, MiB from `statvfs`). The `fmtMiB` read hook
  prints `GiB`/`MiB` and a dash when the card is absent or unmounted (the
  server reports `0`); the rows take no part in the dirty state and add no
  `Apply` buttons. A note under the card states that the size is the mounted
  FAT32 volume, not the card's advertised capacity. The `#sb_sd` status line
  is unchanged. Updated `endpoints.md`, divinus `README.md`, `WEBUI_REORG.md`
  and `bench/README.md`; `bench/ui_check.py` compares the rendered texts with
  `window.lastStatus` and checks the format, so no card is required.

- **Сервисы стенда в контейнере `services/` (OKO).** Новый корневой каталог
  `services/`: `Dockerfile` (Debian trixie, `supervisor`, `tftpd-hpa`,
  `nfs-kernel-server`, `wireguard-tools`, `openvpn`), `docker-compose.yml`
  (`network_mode: host`, `privileged`, `restart: unless-stopped`) и
  `entrypoint.sh`, который монтирует `nfsd`, поднимает `rpcbind` и
  `rpc.nfsd` (со сбросом счётчика потоков, чтобы регистрация NFS переживала
  restart контейнера), применяет `exportfs -ra` и запускает `supervisord`.
  TFTP-корень — `output/image` (`/srv/tftp`, ro), NFS-экспорт —
  `output/srv/nfs` (`/srv/nfs`, rw), конфигурация — `services/conf` (ro),
  секреты — `output/srv/vpn/` (`/etc/oko-secrets`, ro).
  Программы `wg`, `awg`, `openvpn` включены неактивными шаблонами
  (`autostart=false`). `tools/native/setup_netboot.sh` переписан в тонкую
  обёртку без `sudo`: проверка Docker, `mkdir -p output/srv/nfs/rootfs` и
  `output/srv/vpn`, `docker compose up -d --build`, диагностика
  `showmount`/`ss`. NFS-хранилище перенесено в `output/srv/nfs/rootfs`
  (rsync с сохранением владельцев, ACL и xattr); путь экспорта внутри
  контейнера остался `/srv/nfs/rootfs`, поэтому строки `nfsroot=` на DUT и
  скрипты `bench/` не менялись. Матрица `nfs_env.py on/off` на DUT пройдена
  до и после purge; затем удалены хостовые `tftpd-hpa`, `nfs-kernel-server`,
  `nfs-common` и `rpcbind`, снят экспорт `/srv/nfs` и ACL `u:tftp`.
  Подробности — `services/README.md`.
- **Stand services run in the `services/` container (OKO).** New root
  directory `services/`: a `Dockerfile` (Debian trixie, `supervisor`,
  `tftpd-hpa`, `nfs-kernel-server`, `wireguard-tools`, `openvpn`), a
  `docker-compose.yml` (`network_mode: host`, `privileged`,
  `restart: unless-stopped`) and an `entrypoint.sh` that mounts `nfsd`,
  starts `rpcbind` and `rpc.nfsd` (resetting the thread count so the NFS
  registration survives a container restart), applies `exportfs -ra` and
  runs `supervisord`. The TFTP root is `output/image` (`/srv/tftp`, ro), the NFS
  export is `output/srv/nfs` (`/srv/nfs`, rw), configuration lives in
  `services/conf` (ro), and secrets live in `output/srv/vpn`
  (`/etc/oko-secrets`, ro). The `wg`, `awg` and `openvpn` programs
  are shipped as inactive templates (`autostart=false`).
  `tools/native/setup_netboot.sh` is now a thin wrapper without `sudo`: a
  Docker check, `mkdir -p output/srv/nfs/rootfs` and `output/srv/vpn`,
  `docker compose up -d --build`, and `showmount`/`ss` diagnostics. The NFS
  storage moved to `output/srv/nfs/rootfs` (rsync preserving owners, ACLs
  and xattrs); the export path inside the container stays
  `/srv/nfs/rootfs`, so the DUT `nfsroot=` strings and the `bench/` scripts
  are unchanged. The `nfs_env.py on/off` matrix passed on the DUT both before
  and after the purge; the host `tftpd-hpa`, `nfs-kernel-server`,
  `nfs-common` and `rpcbind` packages were then removed, and the `/srv/nfs`
  export plus the `u:tftp` ACLs were cleaned up. See `services/README.md`.

### Изменено

- **Документация: `stream.enable` больше не выдаётся за условие RTSP (OKO).**
  В `TESTBENCH.md` (§8 и §9) и `bench/README.md` было записано, что RTSP отдаёт
  кадры только при `stream.enable=true`. Код это опровергает: RTSP управляется
  `rtsp.enable`, кадры приходят с VENC-канала (`mp4.enable`/`substream.enable`),
  а `stream.enable` включает отдельный UDP/RTP-пуш. Формулировки исправлены,
  в `TESTBENCH.md` §5 добавлен приёмник MediaMTX с портами и §8 — подраздел
  диагностики «UDP/RTP-раздача камеры», `services/README.md` и `STRUCTURE.md`
  описывают `services/stream/`.
- **Docs: `stream.enable` is no longer presented as an RTSP prerequisite
  (OKO).** `TESTBENCH.md` (§8 and §9) and `bench/README.md` claimed that RTSP
  emits frames only with `stream.enable=true`. The code says otherwise: RTSP is
  controlled by `rtsp.enable`, frames come from the VENC channel
  (`mp4.enable`/`substream.enable`), and `stream.enable` turns on the separate
  UDP/RTP push. The wording is fixed, `TESTBENCH.md` §5 gained the MediaMTX
  receiver with its ports, §8 gained the "camera UDP/RTP push" diagnostic
  subsection, and `services/README.md` plus `STRUCTURE.md` describe
  `services/stream/`.

- **`S21appinit` переключён на `oko-lunch`, сборка `rkipc` отключена (OKO).**
  Heredoc `S21appinit` в `project/build.sh` вызывает
  `/oem/usr/bin/oko-lunch.sh`/`oko-lunch-stop.sh`; `rkipc` исключён из
  `app_src` в `project/app/Makefile` (исходники остаются в дереве как
  справочные). `oko-prune.list` и `tools/native/check_image_contents.sh`
  обновлены: `RkLunch*.sh` — запрещённые пути, `oko-lunch*.sh` — обязательные.
  Останов больше не висит в `while [ 1 ]`: ожидание выхода `divinus` ограничено
  10 с, затем `killall -9 divinus`; после ожидания `udhcpc`/`dhcpcd` снимаются
  повторно, чтобы поздний `oko-net apply` не вернул сеть. Документация (`INIT_ORDER.md` §4/§5/§7/§8,
  `README_RU.md`, `STRUCTURE.md`, `TESTBENCH.md`, `VERIFIED_FUNCTIONS.md`,
  `FEATURES_VERIFIED.md`)
  приведена к новым именам. Проверено на DUT 2026-11-10: прошиты `oem` и
  `rootfs`, `S21appinit start/stop` работает, `api_check` — 105 проверок без
  ошибок, RTSP/OSD живы; netboot-прогон не выполнялся (NFS-экспорт содержит
  прошлую сборку).
- **`S21appinit` switched to `oko-lunch`, the `rkipc` build is disabled (OKO).**
  The `S21appinit` heredoc in `project/build.sh` calls
  `/oem/usr/bin/oko-lunch.sh`/`oko-lunch-stop.sh`; `rkipc` is filtered out of
  `app_src` in `project/app/Makefile` (its sources stay in the tree for
  reference). `oko-prune.list` and `tools/native/check_image_contents.sh` were
  updated: `RkLunch*.sh` are forbidden paths and `oko-lunch*.sh` are required.
  Stop no longer loops in `while [ 1 ]`: the `divinus` exit wait is capped at
  10 s, then `killall -9 divinus` runs; `udhcpc`/`dhcpcd` are stopped again
  after the wait so a late `oko-net apply` cannot bring the network back. The documentation (`INIT_ORDER.md`
  §4/§5/§7/§8, `README_RU.md`, `STRUCTURE.md`, `TESTBENCH.md`,
  `VERIFIED_FUNCTIONS.md`, `FEATURES_VERIFIED.md`) follows the new names. Verified on the DUT
  2026-11-10: `oem` and `rootfs` were flashed, `S21appinit start/stop` works,
  `api_check` passed 105 checks and RTSP/OSD stayed alive; the netboot run was
  not repeated (the NFS export still holds the previous build).

- **Документация приведена к составу slim-образа (OKO).** `INIT_ORDER.md`
  описывает `RkLunch.sh` без rkipc-ветки и обновлённую таблицу init-скриптов
  (без `S99python`/`S99_auto_reboot`); `TESTBENCH.md` — divinus как
  единственный стример, сохранённую диагностику `v4l2-ctl`/`media-ctl`/
  `rgaImDemo` и проверку `check_image_contents.sh`; `README_RU.md`,
  `STRUCTURE.md`, `FEATURES*.md`, `KNOWN_ISSUES.md` (KI-045, KI-050) — тот же
  состав.
- **Documentation aligned with the slim image contents (OKO).** `INIT_ORDER.md`
  describes `RkLunch.sh` without the rkipc branch and the updated init-script
  table (no `S99python`/`S99_auto_reboot`); `TESTBENCH.md` covers divinus as
  the only streamer, the retained `v4l2-ctl`/`media-ctl`/`rgaImDemo`
  diagnostics and the `check_image_contents.sh` check; `README_RU.md`,
  `STRUCTURE.md`, `FEATURES*.md`, `KNOWN_ISSUES.md` (KI-045, KI-050) follow
  the same layout.

- **Запуск OKO без rkipc-ветки (OKO).** `RkLunch.sh` больше не выбирает
  `rkipc-*.ini`, не копирует `image.bmp`, не запускает `rk_mpi_ao_test` и не
  переключает `csi_unite`; единственный стример — `divinus`, при отсутствии
  маркера или бинарника в консоль выводится явное сообщение. `RkLunch-stop.sh`
  останавливает `divinus`, а `S20oko-osd` всегда создаёт
  `/userdata/divinus.enable` (opt-out через `/userdata/rkipc.enable` убран).
- **OKO boot without the rkipc branch (OKO).** `RkLunch.sh` no longer selects
  an `rkipc-*.ini`, copies `image.bmp`, runs `rk_mpi_ao_test` or flips
  `csi_unite`; `divinus` is the only streamer, and a missing marker or binary
  prints an explicit console message. `RkLunch-stop.sh` stops `divinus`, and
  `S20oko-osd` always creates `/userdata/divinus.enable` (the
  `/userdata/rkipc.enable` opt-out is gone).

- **`S99rtcinit` пишет RTC в UTC (OKO).** busybox `hwclock` по умолчанию
  работает с локальным временем: при не-UTC `/etc/TZ` калибровка при
  «нулевом» RTC сдвигала системное время на смещение пояса после каждой
  перезагрузки. Проверка и запись теперь идут с `-u`, а `date -s` заменён на
  `date -u -s`; RTC хранит UTC независимо от выбранного пояса.
- **`S99rtcinit` keeps the RTC in UTC (OKO).** busybox `hwclock` defaults to
  local time, so with a non-UTC `/etc/TZ` the "empty RTC" calibration shifted
  the system clock by the zone offset on every boot. The probe and the write
  now pass `-u`, and `date -s` became `date -u -s`; the RTC holds UTC
  regardless of the selected zone.

- **Тест-матрикс `/oem` на BusyBox 1.38 при NFS-root пройден (OKO).**
  Группа 0: `nfs_env.py on` → `root=/dev/nfs`, `ip=192.168.52.150`,
  `/`, `/oem`, `/userdata` — nfs3 с `192.168.52.200`, `eth0` статический
  без DHCP-демона. Группа 1: `sc230ai`, `rockit`, `rknpu`, `rve`,
  `rk_dvbm`, `mpp_vcodec`, `rga3` загружены из NFS, ошибок загрузки
  модулей нет. Группа 2: `api_check` 98/98, `osd_check` — кадры
  1080p30 с UTF-8-текстом, `ui_check` зелёный, `/api/telemetry`
  `available=true`, `oko_mac` не изменился. Группа 3: сообщение
  `oko-osd` через `/tmp/osd.fifo` отрисовано в кадре (5000 пикселей
  региона msg), `wg`/`awg` dry-cycle, `/dev/net/tun`, microSD и
  `/api/storage`. Группа 4: `passwd` пишет `$5$`-хэш, init-скрипты без
  `ash`-ошибок, обычный `reboot` переживает NFS-корень. Группа 5:
  `off` → `root=ubi0:rootfs` и ubifs, `/userdata` 29.1 МиБ,
  `api_check` 98/98 повторно; soak 10/10 on/off; 2 холодных старта.
  Аварийный drill выполнен с недоступным NFS-экспортом и `panic=5`:
  panic и восстановление env по UART (`setenv`/`saveenv`/`reset`
  по одной команде) подтверждены; остановка `nfs-kernel-server`
  требует физического сброса питания и остаётся ручной процедурой.
- **The `/oem` test matrix on BusyBox 1.38 over NFS root passed (OKO).**
  Group 0: `nfs_env.py on` → `root=/dev/nfs`, `ip=192.168.52.150`,
  `/`, `/oem`, `/userdata` are nfs3 from `192.168.52.200`, `eth0` is
  static with no DHCP daemon. Group 1: `sc230ai`, `rockit`, `rknpu`,
  `rve`, `rk_dvbm`, `mpp_vcodec`, `rga3` loaded from NFS, no module
  load errors. Group 2: `api_check` 98/98, `osd_check` 1080p30 frames
  with UTF-8 text, `ui_check` passed, `/api/telemetry` `available=true`,
  `oko_mac` unchanged. Group 3: an `oko-osd` `/tmp/osd.fifo` message
  rendered in the frame (5000 pixels in the msg region), `wg`/`awg`
  dry cycle, `/dev/net/tun`, microSD and `/api/storage`. Group 4:
  `passwd` writes a `$5$` hash, init scripts have no `ash` errors, a
  normal `reboot` survives the NFS root. Group 5: `off` →
  `root=ubi0:rootfs` and ubifs, `/userdata` 29.1 MiB, `api_check`
  98/98 again; soak 10/10 on/off cycles; 2 cold starts. The emergency
  drill used an unreachable NFS export with `panic=5`: the panic and
  the UART env recovery (`setenv`/`saveenv`/`reset` as separate
  commands) are confirmed; stopping `nfs-kernel-server` needs a
  physical power reset and remains a manual procedure.

- **Документация переключения NAND ↔ NFS обновлена (OKO).**
  `TESTBENCH.md` §7D переписан под `nfs_env.py` (без смены BoardConfig
  и пересборки), §7B получил LBA `boot` (`0x800`, `0x3000`), §4/§5 —
  фактический UART (`/dev/ttyACM1`) и адреса NAND/NFS. `INIT_ORDER.md`
  §1/§2: netboot-фрагмент в основном конфиге, режим — переменная env.
  `STRUCTURE.md`, `TO_DO.md` и `FEATURES_VERIFIED.md` — `nfs_env.py`
  и подтверждённый матрикс без `*-NETBOOT.mk`.
- **NAND ↔ NFS switching documentation updated (OKO).**
  `TESTBENCH.md` §7D now describes `nfs_env.py` (no BoardConfig switch
  or rebuild), §7B gained the `boot` LBA (`0x800`, `0x3000`), and §4/§5
  reflect the actual UART (`/dev/ttyACM1`) and NAND/NFS addresses.
  `INIT_ORDER.md` §1/§2: the netboot fragment is in the main config and
  the mode is an env variable. `STRUCTURE.md`, `TO_DO.md` and
  `FEATURES_VERIFIED.md` list `nfs_env.py` and the verified matrix
  without `*-NETBOOT.mk`.

- **NFS-root включён в штатное ядро SC230AI, дубль BoardConfig удалён
  (OKO).** `RK_KERNEL_DEFCONFIG_FRAGMENT` основного конфига теперь
  `"rv1106-netboot.config rv1106-sc230ai.config"`, поэтому `boot.img`
  штатной NAND-сборки содержит `CONFIG_IP_PNP`, `IP_PNP_DHCP`,
  `IP_PNP_BOOTP` и `CONFIG_ROOT_NFS`. Режим загрузки переключается только
  переменной env U-Boot `sys_bootargs`, без перепрошивки ядра. Дубль
  `BoardConfig-...-SC230AI-NETBOOT.mk` удалён. Гейты: diff `.config` —
  только эти символы и их select'ы (`IP_PNP_RARP`), `boot.img`
  3 740 672 байт при разделе 6 МиБ, `./build.sh firmware` собирает
  `update.img`.
- **NFS root is built into the stock SC230AI kernel and the duplicate
  board config is gone (OKO).** `RK_KERNEL_DEFCONFIG_FRAGMENT` of the main
  config is now `"rv1106-netboot.config rv1106-sc230ai.config"`, so the
  stock NAND `boot.img` carries `CONFIG_IP_PNP`, `IP_PNP_DHCP`,
  `IP_PNP_BOOTP` and `CONFIG_ROOT_NFS`. The boot mode is switched solely
  through the U-Boot env variable `sys_bootargs`, with no kernel reflash.
  The duplicate `BoardConfig-...-SC230AI-NETBOOT.mk` was removed. Gates:
  the `.config` diff contains only those symbols and their selects
  (`IP_PNP_RARP`), `boot.img` is 3 740 672 bytes against a 6 MiB
  partition, and `./build.sh firmware` produces `update.img`.

- **Rootfs 2026.08.1 проверена на DUT и прошита в SPI NAND (OKO).** По NFS:
  `api_check` 98/98, `osd_check` 5/5 кадров, RTSP 1920×1080, Python 3.14.8 +
  OpenSSL 3.6.5, `oem`/`userdata` из `<nfsroot>/.nfsroot/`. После возврата
  `.BoardConfig.mk` на SC230AI и полной пересборки `update.img`
  (boot 6 МБ/oem 40 МБ/userdata 40 МБ/rootfs 168 МБ) прошит через Maskrom
  (`download` → `upgrade_tool uf`); DUT грузится с `root=ubi0:rootfs`,
  `/userdata` ≈29.1 МиБ, `api_check` 98/98, RTSP/OSD без регрессии,
  `/dev/net/tun` и OpenVPN 2.7.8 на месте. ABI-риск для сторонних blob'ов
  зафиксирован в KI-045, w-вариант — в KI-046.
- **The 2026.08.1 rootfs is verified on the DUT and flashed to SPI NAND
  (OKO).** Over NFS: `api_check` 98/98, `osd_check` 5/5 frames, RTSP at
  1920×1080, Python 3.14.8 + OpenSSL 3.6.5, `oem`/`userdata` mounted from
  `<nfsroot>/.nfsroot/`. After switching `.BoardConfig.mk` back to SC230AI
  and rebuilding `update.img` (boot 6 MB/oem 40 MB/userdata 40 MB/rootfs
  168 MB) it was flashed through Maskrom (`download` → `upgrade_tool uf`);
  the DUT boots with `root=ubi0:rootfs`, `/userdata` ≈29.1 MiB,
  `api_check` 98/98, RTSP/OSD clean, `/dev/net/tun` and OpenVPN 2.7.8
  present. The third-party blob ABI risk is tracked in KI-045 and the w
  variant in KI-046.

- **Buildroot обновлён до upstream 2026.08.1 (OKO).** Вместо вендорского
  `buildroot-2023.02.6` используется официальный релиз 2026.08.1
  (tarball с закреплённым sha256, `make_tarball.sh`), вендорская дельта
  перенесена внешними файлами: `luckfox_pico_defconfig`
  (`BR2_GLOBAL_PATCH_DIR`), `busybox.config`, патчи
  `patches-2026.08/{gnutls,libv4l}`; копирование `hcitool_patch`/`mpv_patch`
  в `buildroot_create`/`buildroot` убрано (пакеты не входят в активный
  defconfig). Гейт внешнего тулчейна
  (`arm-rockchip830`, gcc 8.3.0, uClibc-ng, headers 5.10) пройден без
  ослабления `BR2_TOOLCHAIN_*`; состав `BR2_PACKAGE_*` defconfig
  сохранён. Пакетные правки: GnuTLS 3.8.13 не собирает gnulib
  self-тесты (`pthread_t` в uClibc `bits/sigthread.h`), v4l-utils 1.32.0
  (`edid-decode`) использует `i2c_msg{...}` вместо C++20 designated
  initializer, busybox 1.38.0 собирается с отключённым yescrypt (нет
  `explicit_bzero` в uClibc-ng). Вендорские busybox-патчи перенумерованы
  в `0100`/`0101`. Версии: OpenSSL 3.x (было 1.1.1), Python 3.14.8
  (было 3.11.6), busybox 1.38.0 (было 1.36.1). `BUILDROOT_VER` и
  `BUILDROOT_PATH` обновлены; `rootfs.tar` собран нативно
  (`./build.sh rootfs`). Вендорская дельта и версии пакетов —
  `research/buildroot-2026-08/`.
- **Buildroot updated to upstream 2026.08.1 (OKO).** The vendor
  `buildroot-2023.02.6` is replaced by the official 2026.08.1 release
  (sha256-pinned tarball, `make_tarball.sh`), with the vendor delta kept
  outside the tree: `luckfox_pico_defconfig` (`BR2_GLOBAL_PATCH_DIR`),
  `busybox.config`, `patches-2026.08/{gnutls,libv4l}`; the
  `hcitool_patch`/`mpv_patch` copies in `buildroot_create`/`buildroot`
  are removed (the packages are not part of the active defconfig). The external
  toolchain gate (`arm-rockchip830`, gcc 8.3.0, uClibc-ng, headers 5.10)
  passes without relaxing `BR2_TOOLCHAIN_*`; the defconfig
  `BR2_PACKAGE_*` set is unchanged. Package fixes: GnuTLS 3.8.13 skips
  the gnulib self-tests (`pthread_t` in the uClibc `bits/sigthread.h`),
  v4l-utils 1.32.0 (`edid-decode`) uses `i2c_msg{...}` instead of a
  C++20 designated initializer, and busybox 1.38.0 builds with yescrypt
  disabled (no `explicit_bzero` in uClibc-ng). The vendor busybox
  patches are renumbered to `0100`/`0101`. Versions: OpenSSL 3.x (was
  1.1.1), Python 3.14.8 (was 3.11.6), busybox 1.38.0 (was 1.36.1).
  `BUILDROOT_VER` and `BUILDROOT_PATH` are updated; `rootfs.tar` is built
  natively (`./build.sh rootfs`). The vendor delta and package versions
  are recorded in `research/buildroot-2026-08/`.

- **Сборка U-Boot, ядра, media и app подтверждена на хосте без контейнера
  (OKO).** `./build.sh uboot|kernel|media|app` на Debian 13 с gcc 14.2 и
  вендорским кросс-gcc 8.3.0 прошли без правок исходников: host-tools
  U-Boot 2017 (`scripts/dtc`, `tools/`) и ядра 5.10 собраны gcc-14 только
  с предупреждениями, целевой код по-прежнему собирает кросс-gcc 8.
  Изменения сборки не потребовались.
- **U-Boot, kernel, media and app build on the container-free host (OKO).**
  `./build.sh uboot|kernel|media|app` on Debian 13 with gcc 14.2 and the
  vendor cross gcc 8.3.0 passed without source changes: the U-Boot 2017
  host tools (`scripts/dtc`, `tools/`) and the 5.10 kernel host tools build
  with gcc-14 warnings only, while target code still uses cross gcc 8. No
  build changes were required.

- **Итоговая регрессия KI-043/KI-044 и устойчивость bench-скриптов (OKO).**
  2026-11-08 на свежей загрузке: `nn_geom` 52/52, `api_check.py` 97/0
  (`OKO_FOCUS_MOTOR_TESTS=1` — 107/0), `ui_check.py` в обычном режиме и с
  `OKO_FOCUS_AF_TESTS=1` — passed, `geometry_check.py --what zoom` — 6
  проверок (кроп 4.28 против 53.02 к полному кадру, корреляция 0.985/0.112;
  возврат к 1x — 4.24, корреляция 1.000). `geometry_check.py`:
  `stable_capture()` отбрасывает тёмные первые кадры после рестарта, критерий
  кропа — нормированная корреляция вместо `mean|diff|` (её сбивает общий сдвиг
  яркости/AWB), рестарт MP4 вынесен в opt-in `OKO_GEOMETRY_MP4_TESTS=1`
  (после смены кропа он клинит RTSP). `api_check.py` опрашивает
  `focus.status` до 20 с: после нескольких циклов `enable=1/0` AFM может
  отдавать `idle`/`no_stats` до перезагрузки — это записано в
  `bench/README.md` и `TESTBENCH.md` §9, как и уточнённые признаки CMA
  (`cma_alloc -12`/503/нет кадров; высокий `loadavg` и `DW`-потоки сами по
  себе не признак).
- **Final KI-043/KI-044 regression and bench-script robustness (OKO).**
  2026-11-08 on a fresh boot: `nn_geom` 52/52, `api_check.py` 97/0
  (`OKO_FOCUS_MOTOR_TESTS=1` — 107/0), `ui_check.py` both in the default mode
  and with `OKO_FOCUS_AF_TESTS=1` passed, `geometry_check.py --what zoom` — 6
  checks (crop 4.28 versus 53.02 to the full frame, correlation 0.985/0.112;
  back to 1x — 4.24, correlation 1.000). `geometry_check.py`:
  `stable_capture()` drops the dark first frames after a restart, the crop
  criterion is normalised correlation instead of `mean|diff|` (a global
  brightness/AWB shift skews it), and the MP4 restart is opt-in via
  `OKO_GEOMETRY_MP4_TESTS=1` (after a crop change it wedges RTSP).
  `api_check.py` polls `focus.status` for up to 20 s: after several
  `enable=1/0` cycles the AFM can report `idle`/`no_stats` until a reboot —
  recorded in `bench/README.md` and `TESTBENCH.md` §9 together with the
  refined CMA signs (`cma_alloc -12`/503/no frames; high `loadavg` and `DW`
  threads are not a sign by themselves).
- **Проверены сборки `sysdrv`/`app` и уточнена команда обновления `oem`
  (OKO).** 2026-11-08: `./build.sh sysdrv` и `./build.sh app` проходят в
  Docker-контейнере; `app` кладёт бинарник в `output/out/app_out/usr/bin`
  (sha256 `ddbe6aa2…`, совпадает с `project/app/divinus/source/divinus`), но
  `output/out/oem` не обновляет. Стейджинг `oem` пересобирает
  `./build.sh firmware` (через `__PACKAGE_OEM`): после него
  `output/out/oem/usr/bin/divinus` имеет тот же sha256, `output/image/update.img`
  пересобран. В `FEATURES_VERIFIED.md` команда `make oem` заменена на фактические
  `./build.sh app` / `./build.sh firmware` (цели `make oem` в репозитории нет).
  В `bench/README.md` добавлен раздел «Порядок работы и CMA», в `TESTBENCH.md`
  §9 — признаки исчерпания CMA и правило перезагрузки; дата §1 обновлена.
- **`sysdrv`/`app` builds verified and the `oem` refresh command corrected
  (OKO).** 2026-11-08: `./build.sh sysdrv` and `./build.sh app` pass in the
  Docker container; `app` installs the binary into `output/out/app_out/usr/bin`
  (sha256 `ddbe6aa2…`, identical to `project/app/divinus/source/divinus`) but
  does not update `output/out/oem`. The `oem` staging is rebuilt by
  `./build.sh firmware` (via `__PACKAGE_OEM`): afterwards
  `output/out/oem/usr/bin/divinus` has the same sha256 and
  `output/image/update.img` is regenerated. `FEATURES_VERIFIED.md` now names
  the real `./build.sh app` / `./build.sh firmware` commands instead of
  `make oem` (there is no `make oem` target). `bench/README.md` gained a
  "Workflow and CMA" section, `TESTBENCH.md` §9 the CMA exhaustion signs and the
  reboot rule, and the §1 date is updated.
- **Измерена геометрия NN-рамок и OSD под поворотом (OKO).** Прогон
  `OKO_GEOMETRY_TESTS=1 geometry_check.py --what boxes|osd` 2026-11-07:
  рамки совпали с повёрнутой маской 0° — IoU 0.624 (90°), 0.713 (180°),
  0.711 (270°); OSD-текст остаётся в правом нижнем и левом верхнем углах на
  всех четырёх углах (12 проверок). Канва: 1080x1920 на 90/270, 1920x1080
  на 180. `geometry_check.py` после рестарта ждёт 6 с и берёт 20-й кадр:
  первые кадры после подъёма сервера бывают тёмными и давали ложные провалы.
  Рамки и поворот переведены в `FEATURES_VERIFIED.md`, в `TO_DO.md` §11.5
  закрыты 180/270; эталонная разметка положения рамок остаётся открытой;
  числа — в `VERIFIED_FUNCTIONS.md`.
- **NN box and OSD geometry under rotation measured (OKO).** The 2026-11-07
  `OKO_GEOMETRY_TESTS=1 geometry_check.py --what boxes|osd` run: the boxes
  matched the rotated 0° mask — IoU 0.624 (90°), 0.713 (180°), 0.711 (270°);
  the OSD text keeps the right-bottom and left-top corners at all four angles
  (12 checks). Canvas: 1080x1920 at 90/270, 1920x1080 at 180.
  `geometry_check.py` now waits 6 s after a restart and takes the 20th frame:
  the first frames after the server comes back are sometimes dark and caused
  false failures. Boxes and rotation moved to `FEATURES_VERIFIED.md`, the
  180/270 item in `TO_DO.md` §11.5 is closed; the reference box markup stays
  open; the numbers are in `VERIFIED_FUNCTIONS.md`.
- **ePTZ: смена зума/поворота применяется graceful-рестартом (OKO).**
  `/api/eptz` и `/api/rotate` больше не пытаются менять работающий канал:
  новое значение сохраняется, ответ содержит `applied:false` и
  `note:"settings saved; restarting the application"`, после `send_and_close`
  вызываются `app_config_save()` и `kill(getpid(), SIGHUP)` — тот же путь,
  что у `/api/substream`. `applied:true` отдаётся только когда живой канал
  совпадает с запросом; `crop` — фактический кроп канала (полный кадр при
  выключенном ePTZ), `rc` — код MPP последнего старта, `note` объясняет
  «stream is off»; повтор одинакового запроса рестарта не вызывает. Пока идёт
  поиск автофокуса, `home`/`zero` или прошивка, запрос отклоняется с
  `409 busy`. Кроп и поворот теперь применяются в `rk_channel_bind()` сразу
  после `EnableChn` — включение канала сбрасывает их (замеры 2026-11-07:
  mean|diff| 4.26 к центру 1x против 53.85 к полному кадру). KI-044 закрыт;
  `bench/geometry_check.py` ждёт подъём `/api/status` после POST и проверяет
  кроп после рестарта. Обновлены `doc/endpoints.md`, `divinus/README.md`,
  `WEBUI_REORG.md`, `KNOWN_ISSUES.md`, `FEATURES_VERIFIED.md` и
  `VERIFIED_FUNCTIONS.md`.
- **ePTZ: a zoom/rotation change is applied by a graceful restart (OKO).**
  `/api/eptz` and `/api/rotate` no longer touch a running channel: the new
  value is saved, the response carries `applied:false` and
  `note:"settings saved; restarting the application"`, and after
  `send_and_close` the handler calls `app_config_save()` and
  `kill(getpid(), SIGHUP)` — the `/api/substream` path. `applied:true` is
  returned only when the live channel matches the request; `crop` is the real
  channel rectangle (full frame when ePTZ is off), `rc` is the MPP code of the
  last start, and `note` explains "stream is off"; repeating the same request
  does not restart again. While an autofocus search, a `home`/`zero` job or a
  firmware write runs, the request is refused with `409 busy`. The crop and the
  rotation are now applied in `rk_channel_bind()` right after `EnableChn` —
  the enable resets them (2026-11-07: mean|diff| 4.26 to the centre of the 1x
  frame versus 53.85 to the full frame). KI-044 is closed;
  `bench/geometry_check.py` waits for `/api/status` after a POST and verifies
  the crop after the restart. Updated `doc/endpoints.md`,
  `divinus/README.md`, `WEBUI_REORG.md`, `KNOWN_ISSUES.md`,
  `FEATURES_VERIFIED.md` and `VERIFIED_FUNCTIONS.md`.
- **Документация: TO_DO и FEATURES приведены к индексу (OKO).** `TO_DO.md`
  сокращён до открытых работ: убраны закрытые чек-листы фаз 0–4 и вариантов A/B,
  механика объектива зафиксирована текстом, обновлены §6 («Открытые работы по
  фокусу») и §11.5. `FEATURES_UNVERIFIED.md` вычищен от закрытых примечаний и
  дублей строк таблицы, версии STM8 актуализированы. `FEATURES.md` оставлен
  тонким индексом: паспорт платформы, 5 команд проверки и ссылки на
  `TESTBENCH.md` §8 / `VERIFIED_FUNCTIONS.md` §3; дублирующие примечания
  удалены. Обновлены `AGENTS.md`, `STRUCTURE.md`, `README_RU.md` и
  `.kilo/command/docs-check.md` / `.kilo/skills/oko-docs/SKILL.md`.
- **Docs: TO_DO and FEATURES reduced to an index (OKO).** `TO_DO.md` is cut
  down to open work: the closed checklists of phases 0–4 and variants A/B are
  gone, the lens mechanics are recorded as text, and §6 ("Open focus work") and
  §11.5 are updated. `FEATURES_UNVERIFIED.md` is cleaned of closed notes and
  table-row duplicates, and the STM8 versions are brought up to date.
  `FEATURES.md` is now a thin index: the platform passport, five verification
  commands and links to `TESTBENCH.md` §8 / `VERIFIED_FUNCTIONS.md` §3; the
  duplicated notes are removed. `AGENTS.md`, `STRUCTURE.md`, `README_RU.md` and
  `.kilo/command/docs-check.md` / `.kilo/skills/oko-docs/SKILL.md` are updated.
- **Подтверждение SD-инкремента прогонами стенда (OKO).** Прогоны 2026-11-07
  подтвердили заявленное: `bench/ui_check.py` печатает
  `SD summary: 14.8 GiB / 14.8 GiB (raw 15177/15171 MiB)` и завершается
  `UI check passed` с кодом 0; тот же прогон с `OKO_FOCUS_AF_TESTS=1`
  (кнопка «Автофокус», `af=fine`) тоже даёт `UI check passed`, код 0;
  `bench/api_check.py` — `96 checks, 0 failures`, код 0. Счётчики в
  `bench/README.md`, `FEATURES_VERIFIED.md` и `VERIFIED_FUNCTIONS.md`
  приведены к фактическим 96 проверкам и дате прогона. Staged-копия
  `output/out/oem/usr/bin/divinus` намеренно не пересобиралась: перепрошивка
  в этом цикле не планируется.
- **SD increment confirmed by bench runs (OKO).** The 2026-11-07 runs
  confirmed the claims: `bench/ui_check.py` prints
  `SD summary: 14.8 GiB / 14.8 GiB (raw 15177/15171 MiB)` and ends with
  `UI check passed`, exit code 0; the same run with
  `OKO_FOCUS_AF_TESTS=1` (the Autofocus button, `af=fine`) also returns
  `UI check passed`, exit code 0; `bench/api_check.py` reports
  `96 checks, 0 failures`, exit code 0. The counters in `bench/README.md`,
  `FEATURES_VERIFIED.md` and `VERIFIED_FUNCTIONS.md` are brought to the
  measured 96 checks and the run date. The staged copy
  `output/out/oem/usr/bin/divinus` was deliberately not rebuilt: no reflash
  is planned in this cycle.
- **WebUI: каркас под высоту окна и две колонки Live (OKO).** Страница
  больше не прокручивается целиком: `body` — flex-колонка высотой
  `100vh`/`100dvh` с `overflow: hidden`, `#layout` растягивается
  (`flex: 1 1 auto; min-height: 0`), а прокручиваются только `#sidebar` и
  `#content`; широкие таблицы получили `.card table { display: block;
  overflow-x: auto }`. На ≥ 60em «Видео» — две колонки: превью слева,
  `#focus-card` справа со своей прокруткой (`max-height:
  calc(100dvh - 130px)`). Высота превью изменена с `100vh - 520px` на
  `100dvh - 250px` в двух колонках и `100dvh - 430px` в одной (фолбэк
  `100vh`); рамка ROI по-прежнему позиционируется от медиаэлемента.
  Обновлены `bench/ui_check.py` (проверка отсутствия скролла страницы на
  всех вкладках, две колонки, нижний навбар) и `bench/README.md`.
- **WebUI: viewport shell and two-column Live (OKO).** The page no longer
  scrolls as a whole: `body` is a `100vh`/`100dvh` flex column with
  `overflow: hidden`, `#layout` stretches (`flex: 1 1 auto; min-height: 0`)
  and only `#sidebar` and `#content` scroll; wide tables use
  `.card table { display: block; overflow-x: auto }`. At ≥ 60em Live is two
  columns: the preview on the left and `#focus-card` on the right with its
  own scroll (`max-height: calc(100dvh - 130px)`). The preview height changed
  from `100vh - 520px` to `100dvh - 250px` in two columns and
  `100dvh - 430px` in one column (`100vh` fallback); the ROI box is still
  positioned from the media element. Updated `bench/ui_check.py` (no page
  scroll on any tab, two columns, bottom navbar) and `bench/README.md`.

- **WebUI «Видео»: размер предпросмотра, автовзведение привода и выбор
  зоны (OKO).** `#preview` и его `video`/`img` ограничены по высоте
  (`calc(100vh - 520px)`) с сохранением пропорций; рамка ROI
  позиционируется от прямоугольника медиаэлемента, поэтому letterbox
  больше не смещает её. Переключатель `focus_motor` удалён: каждая команда
  движения (`move`, `speed`, `home`, `zero`, `af`, «Применить») несёт
  `motor=1` и взводит драйвер атомарно, ручной STOP отправляет
  `motor=0&stop=1&af=cancel`, а `GET /api/focus` теперь опрашивает
  контроллер и без взведения, чтобы UI знал о его наличии. Для выбора
  шкалы добавлен параметр `scale=focus|zoom|both` (старый
  `motor=focus|zoom|both` оставлен для скриптов). ROI выбирается
  двухшагово: «Выделить зону» включает режим, drag только запоминает
  `pendingRoi` без POST, «Готово»/Esc завершают или отменяют выбор, а
  «Применить» отправляет зону с `af=1`; вне режима drag показывает
  подсказку. В карточке появилась строка `#af-state` (прогресс, позиция,
  лучший FV) и блокировка ручных/home/zero кнопок на время поиска;
  STOP остаётся доступен для отмены. Обновлены `bench/api_check.py`
  (`af=bogus`→400, busy-матрица, opt-in `OKO_FOCUS_AF_TESTS=1` с записью
  `output/bench/af_curve_*.csv`), `bench/ui_check.py` (двухшаговый выбор
  зоны, отсутствие `#focus_motor`, `#af-state`) и `bench/README.md`.
- **WebUI Live: preview sizing, automatic driver arming and zone selection
  (OKO).** `#preview` and its `video`/`img` are height-limited
  (`calc(100vh - 520px)`) while keeping the aspect ratio; the ROI box is
  positioned from the media element rectangle, so letterboxing no longer
  shifts it. The `focus_motor` selector is gone: every motion command
  (`move`, `speed`, `home`, `zero`, `af`, Apply) carries `motor=1` and arms
  the driver atomically, the manual STOP sends
  `motor=0&stop=1&af=cancel`, and `GET /api/focus` now probes the controller
  even while it is disarmed so the UI knows it is present. The new
  `scale=focus|zoom|both` parameter selects the scale (the legacy
  `motor=focus|zoom|both` stays for scripts). ROI selection is two-step:
  "Select zone" enters the mode, a drag only stores `pendingRoi` without a
  POST, "Done"/Esc finish or cancel, and Apply sends the zone with `af=1`;
  outside the mode a drag shows a hint. The card gained an `#af-state` line
  (progress, position, best FV) and the manual/home/zero buttons are locked
  during a search; STOP stays available to cancel it. Updated
  `bench/api_check.py` (`af=bogus`→400, busy matrix, opt-in
  `OKO_FOCUS_AF_TESTS=1` recording `output/bench/af_curve_*.csv`),
  `bench/ui_check.py` (two-step zone selection, no `#focus_motor`,
  `#af-state`) and `bench/README.md`.

### Исправлено

- **Ревью этапа 0.3.0: ёмкость кольца, outbox, рестарт и конкурентность.**
  `record_clip_start` больше не урезает кольцо молча: при `pre+post`,
  не помещающихся в 12 МиБ, функция отключается с причиной в `/api/clip`
  (проверено: `post_seconds: 30` → «need a 23000 KiB ring … 12288 KiB limit»).
  `clip_outbox_scan` суммирует все `.mp4`, а не первые 256 (проверено на 300
  файлах: `outbox_count` 307), и `PATH_MAX`-буфер в скане заменён на 512 —
  иначе серверный поток падал на большом outbox. Outbox разгружается
  пачками с повторным сканом. Штатный `record_clip_stop` (SIGHUP/TERM)
  теперь спулит активные и ожидающие клипы в outbox и считает потерю только
  при отсутствии SD (проверено: 4,5 и 3,7 МБ клипы попали в outbox).
  Добавлен счётчик `skipped` для событий, не получивших слот, очистка
  осиротевших `/tmp/oko-clip-*.mp4` при старте (на стенде они занимали
  90 МиБ tmpfs), защита от нулевых файлов в outbox.
  В `services/events`: лимит тела кадра 8 МиБ (9 МиБ → HTTP 413), дата
  события фиксируется по первому каталогу (кадры и клип через полночь UTC
  не расходятся), `_requeue_pending` ставит только свежие `pending`, а не
  весь архив и `failed`; retention уже удаляет события, а не дату.
  `yolo_event_test` и capture-поток сериализованы `eventMtx` (общий
  `activeEvent`/`eventSequence`). `bench/clip_soak.py` переиспользует SSH-
  соединение, берёт SSH-пароль из `OKO_DUT_PASSWORD`, а пароль WebUI — из
  `OKO_WEB_PASSWORD` (раньше один пароль ломал один из двух доступов).
  Отправка JPEG-пачки проверяет `senderRunning` между кадрами: при выключенном
  приёмнике SIGTERM завершается за 3 с (раньше минуты), клипы уходят в outbox.
  После правок: 5-минутный
  soak зелёный (0 lost, outbox 0), `event_check` и `api_check` 113/113.
- **Review of 0.3.0: ring capacity, outbox, restart and concurrency.**
  `record_clip_start` no longer silently caps the ring: when `pre+post` does
  not fit in 12 MiB, the feature is disabled with a reason in `/api/clip`
  (verified: `post_seconds: 30` → "need a 23000 KiB ring … 12288 KiB
  limit"). `clip_outbox_scan` sums every `.mp4`, not the first 256 (verified
  with 300 files: `outbox_count` 307), and its `PATH_MAX` buffer became 512 —
  the 4 KiB frame overflowed the server-thread stack with a large outbox.
  The outbox drains in batches with re-scans. A graceful `record_clip_stop`
  (SIGHUP/TERM) now spools active and retrying clips to the outbox and counts
  a loss only without an SD card (verified: 4.5 MB and 3.7 MB clips were
  spooled). Added a `skipped` counter for events that got no clip slot,
  startup cleanup of orphaned `/tmp/oko-clip-*.mp4` (they filled the 90 MiB
  tmpfs on the bench), and a zero-size outbox guard. In `services/events`:
  an 8 MiB frame-body limit (9 MiB → HTTP 413), the event date is pinned by
  the first directory (frames and clip no longer split across UTC midnight),
  `_requeue_pending` only queues recent `pending` clips instead of the whole
  archive and `failed` entries; retention already evicts events, not a date.
  `yolo_event_test` and the capture thread are serialised with `eventMtx`
  (shared `activeEvent`/`eventSequence`). `bench/clip_soak.py` reuses one SSH
  connection, reads the SSH password from `OKO_DUT_PASSWORD` and the WebUI
  password from `OKO_WEB_PASSWORD` (a single shared value broke one of the two
  logins). The JPEG package sender checks `senderRunning` between frames, so a
  SIGTERM with the receiver down finishes in 3 s (previously minutes) and the
  clips go to the outbox. After the fixes: the 5-minute soak
  is green (0 lost, empty outbox), `event_check` and `api_check` 113/113.

- **Нули в выключенном `[http_post]` и перепутанные рамки в NMS.**
  `app_config_parse` теперь задаёт `http_post.width/height/interval/qfactor`
  до разбора, поэтому сохранённый с `enable: false` файл больше не содержит
  нулей и включение секции вручную не ломает следующий старт (ошибка
  `CONFIG_PARAM_ISNT_IN_RANGE`). В YOLOv5-постпроцессе координаты
  кандидатов теперь сортируются вместе со счётом (индексный массив), раньше
  после сортировки рамка могла браться от другого кандидата; общий NMS
  вынесен в `yolo_finish_candidates`.
- **Zeros in a disabled `[http_post]` and mismatched NMS boxes.**
  `app_config_parse` now seeds `http_post.width/height/interval/qfactor`
  before parsing, so a file saved with `enable: false` no longer carries
  zeros and enabling the section by hand does not break the next start
  (`CONFIG_PARAM_ISNT_IN_RANGE`). The YOLOv5 decoder now sorts the candidate
  coordinates together with their scores (index array); previously a box
  could be paired with another candidate after sorting. The shared NMS lives
  in `yolo_finish_candidates`.

- **divinus: SIGSEGV при `/api/cmd?save` и активации модели (OKO).**
  `app_config_open`, `app_config_restore` и `app_config_path` держали на
  стеке по два-три буфера `PATH_MAX` (4 КиБ каждый). При
  `web_server_thread_stack_size=65536` release-сборка (`-Os`) переполняла
  стек HTTP-потока: падение в `readlink` на `GET /api/cmd?save` и
  `POST /api/models/activate`. Буферы перенесены в кучу; циклы
  `web_whitelist` ограничены размером массива (при четырёх записях
  читался `web_whitelist[4]` за границей). Проверено на DUT: save и
  активация модели проходят при 64 КиБ, `api_check.py` 108/108.
- **divinus: SIGSEGV on `/api/cmd?save` and model activation (OKO).**
  `app_config_open`, `app_config_restore` and `app_config_path` kept two to
  three `PATH_MAX` buffers (4 KiB each) on the stack. With
  `web_server_thread_stack_size=65536` the `-Os` release build overflowed the
  HTTP thread stack and crashed in `readlink` on `GET /api/cmd?save` and
  `POST /api/models/activate`. The buffers are now heap-allocated, and the
  `web_whitelist` loops are bounded by the array size (with four entries they
  read `web_whitelist[4]` out of bounds). Verified on the DUT: save and model
  activation work with a 64 KiB stack, `api_check.py` 108/108.
- **tools/rknn-toolkit2: образ не собирался из-за переименованного wheel.**
  `Dockerfile.toolkit` скачивал wheel как `/tmp/rknn-toolkit2.whl`, и pip 23
  отказывался его ставить без канонического имени файла. Wheel сохраняется
  под оригинальным именем; модель `yolov5n-640x384.rknn` собрана и проверена.
- **tools/rknn-toolkit2: the image failed on a renamed wheel.**
  `Dockerfile.toolkit` downloaded the wheel as `/tmp/rknn-toolkit2.whl`, which
  pip 23 refuses to install without the canonical filename. The wheel now
  keeps its original name; `yolov5n-640x384.rknn` was built and verified.

- **STM32 EC: I2C1 тактировался от PCLK, из-за чего не работало
  пробуждение из Stop по адресу.** `RCC->CCIPR.I2C1SEL` было `00`
  (PCLK): в Stop периферия не тактируется, `WUPEN` не будит ядро, а
  `TIMINGR`/`tTIMEOUT` считаются от 12 МГц вместо 48 МГц. Исправлено на
  `10` (HSIKER). Также: `i2c_pause()`/`i2c_resume()` теперь реально
  работают в приложении; Stop не удерживает линию под маской
  прерываний; транзакция, разбудившая EC, не обрывается сбросом `TXIE`;
  `flash-app` для C011 использует конфиг OpenOCD C0; хост повторяет
  неудачную запись кадра (`I2C_WRITE_RETRIES`).

- **divinus: переполнение стека при экспорте конфигурации и лога (OKO).**
  `GET /api/cmd?export` и `?log` завершались SIGSEGV: кадр
  `respond_request` (8 КиБ HTTP-ответа и 8 КиБ JSON Wi-Fi) вместе с 8,8 КиБ
  потоковой отдачей не помещался в стек веб-сервера (64 КиБ по factory-YAML).
  Буферы `/api/wifi` и `popen()` вынесены в static, фабричный и дефолтный
  `web_server_thread_stack_size` подняты до 128 КиБ, а лимит PNG-чанков
  spng вынесен в отдельную константу 64 КиБ (раньше он наследовал размер
  стека). Проверено на стенде: export 4236 Б и log 200 OK, divinus не
  падает, `api_check` — 105/0.
- **divinus: stack exhaustion when exporting the configuration and log (OKO).**
  `GET /api/cmd?export` and `?log` ended with SIGSEGV: the
  `respond_request` frame (the 8 KiB HTTP response plus the 8 KiB Wi-Fi JSON)
  with the 8.8 KiB streaming function did not fit the web-server stack
  (64 KiB in the factory YAML). The `/api/wifi` and `popen()` buffers are now
  static, the factory/default `web_server_thread_stack_size` is 128 KiB, and
  the spng PNG chunk limit is now a separate 64 KiB constant instead of
  following the stack size. Bench-verified: export 4236 B and log 200 OK,
  divinus stays up, and `api_check` is 105/0.

- **Legacy `g_mass_storage` срывал инициализацию USB-gadget (OKO).**
  В вендорском defconfig был `CONFIG_USB_MASS_STORAGE=y`: legacy-драйвер
  захватывал UDC при старте, падал с `-EINVAL` (нет backing-файла) и обрывал
  инициализацию peripheral у DWC3 (`failed to add gadget`,
  `failed to initialize peripheral`), поэтому `/sys/class/udc` оставался
  пустым и adb/RNDIS не поднимались. Символ выключен во фрагменте
  `rv1106-usb-host.config`; функции gadget управляются через configfs.
  Стенд 2026-11-10: UDC `ffb00000.usb` в состоянии `configured`, gadget
  перечисляется на ПК, `usb0` получает `172.32.0.93` (KI-056).
- **Legacy `g_mass_storage` aborted the USB gadget init (OKO).**
  The vendor defconfig had `CONFIG_USB_MASS_STORAGE=y`: the legacy driver
  grabbed the UDC at boot, failed with `-EINVAL` (no backing file) and aborted
  the DWC3 peripheral init (`failed to add gadget`,
  `failed to initialize peripheral`), leaving `/sys/class/udc` empty so
  adb/RNDIS never started. The symbol is disabled in
  `rv1106-usb-host.config`; gadget functions are managed through configfs.
  Bench 2026-11-10: UDC `ffb00000.usb` reports `configured`, the gadget
  enumerates on the PC and `usb0` gets `172.32.0.93` (KI-056).

- **divinus: UDP/RTP-раздача (`[stream]`) не работала (OKO).** Шесть дефектов
  вендорского кода, каждый из которых по отдельности делал функцию мёртвой:
  (1) `media_start()` возвращал `EXIT_FAILURE` при успешной инициализации
  (инвертированная проверка `udp_stream_init`) и не имел `return` вовсе;
  (2) `strrchr()` даёт `NULL`, если в `dest` нет порта, — `strtol` по `NULL`;
  (3) ранний `return` после инициализации не давал зарегистрировать второй и
  последующие адреса; (4) клиенты из конфига удалялись через 60 с простоя, хотя
  RTP-приёмник ничего не отправляет; (5) `hal/config.c: parse_list()` собирал
  шаблоны с `\n` и `\s`, а POSIX ERE в uClibc-ng трактует их буквально —
  `stream.dest` не разбирался никогда (`[config] Parameter 'dest' not found`);
  (6) в один RTP-пакет уходил весь pack с Annex-B старткодами и несколькими
  NAL, а H.265-фрагментация использовала двухбайтовый FU-заголовок и
  `nal_type = b0 & 0x3F` — такие пакеты не разбирает ни один приёмник.
  Теперь: один NAL на пакет, FU по RFC 6184/7798, `udp_stream_frame_end()`
  двигает RTP-метку времени на кадр (не на NAL), marker — на последнем пакете
  access unit, `stream_dests` увеличен до 5 слотов (запись терминатора при
  четырёх адресах была за границей массива), у `udp_stream_init` появились
  проверки `calloc`/`mutex`. Стенд 2026-11-10: камера шлёт 1080p30 H.265 на
  `192.168.52.200:5600`, MediaMTX отдаёт `hevc Main 1920x1080 30/1` без
  пароля, поток совпадает с RTSP камеры кадр-в-кадр.
- **divinus: the UDP/RTP push (`[stream]`) never worked (OKO).** Six defects in
  vendor code, each of which alone made the feature dead: (1) `media_start()`
  returned `EXIT_FAILURE` on success (inverted `udp_stream_init` check) and had
  no `return` at all; (2) `strrchr()` yields `NULL` when `dest` has no port, so
  `strtol` ran on `NULL`; (3) an early `return` skipped registration of the
  second and later destinations; (4) configured clients were expired after 60 s
  of silence although a plain RTP receiver never sends anything back;
  (5) `hal/config.c: parse_list()` built its patterns with `\n` and `\s`, which
  uClibc-ng's POSIX ERE takes literally, so `stream.dest` never parsed
  (`[config] Parameter 'dest' not found`); (6) a whole pack with Annex-B start
  codes and several NAL units went into one RTP packet, and H.265 fragmentation
  used a two-byte FU header and `nal_type = b0 & 0x3F`, which no receiver can
  depacketize. Now: one NAL per packet, RFC 6184/7798 fragmentation,
  `udp_stream_frame_end()` advances the RTP timestamp per frame (not per NAL),
  the marker sits on the last packet of the access unit, `stream_dests` grew to
  five slots (the terminator write at four destinations was out of bounds), and
  `udp_stream_init` checks `calloc`/`mutex`. Bench 2026-11-10: the camera pushes
  1080p30 H.265 to `192.168.52.200:5600`, MediaMTX serves `hevc Main 1920x1080
  30/1` without credentials, and the stream matches the camera RTSP frame for
  frame.

- **OTA: токен применения после `token_generate` (OKO).** `update_apply_start`
  генерировал новый одноразовый токен до запуска worker, но worker получал
  старый: `oko-update apply` сравнивал его с уже перезаписанным
  `/run/oko-update.token` и отвечал `invalid apply token`. Теперь worker
  получает новый токен. Стенд 2026-11-10: apply → прошивка `boot`/`oem`/
  `rootfs` из U-Boot `sd_update` → postcheck `ok=1`, версия 0.3.0.
- **OTA: apply token after `token_generate` (OKO).** `update_apply_start`
  generated a fresh one-time token before starting the worker, but the worker
  received the old one: `oko-update apply` compared it with the already
  rewritten `/run/oko-update.token` and answered `invalid apply token`. The
  worker now receives the new token. Bench 2026-11-10: apply → `boot`/`oem`/
  `rootfs` flashed by U-Boot `sd_update` → postcheck `ok=1`, version 0.3.0.

- **U-Boot: env-флаг OTA виден загрузчику (OKO).** `CONFIG_ENVF_LIST` не
  содержал `oko_update`, поэтому Rockchip `ENVF` импортировал из env только
  белый список и `fw_setenv` из Linux был невидим U-Boot. В список добавлены
  `oko_update` и ключи `oko_mac`/`oko_net_*`, чтобы `env save` не терял
  сетевые настройки. Стенд 2026-11-10: `printenv oko_update` в U-Boot
  показывает 1, `sd_update` прошивает разделы, флаг удаляется.
- **U-Boot: the OTA env flag is visible to the bootloader (OKO).**
  `CONFIG_ENVF_LIST` did not contain `oko_update`, so Rockchip `ENVF` imported
  only its allowlist from the env and the Linux-written `fw_setenv` stayed
  invisible to U-Boot. The list now includes `oko_update` plus the
  `oko_mac`/`oko_net_*` keys so `env save` does not drop the network settings.
  Bench 2026-11-10: `printenv oko_update` in U-Boot shows 1, `sd_update`
  flashes the partitions and the flag is deleted.

- **`oko-update`: проверка размеров без `stat` (OKO).** В образе нет
  `stat`, поэтому staging на DUT падал с `size-mismatch` (пустой вывод
  `stat -c %s`). Размеры частей теперь считаются `wc -c`; staging `.okp` на
  стенде проходит, полный цикл OTA проверен (KI-049 закрыт).
- **`oko-update`: size checks without `stat` (OKO).** The image has no `stat`,
  so staging on the DUT failed with `size-mismatch` (empty `stat -c %s`
  output). Part sizes now use `wc -c`, `.okp` staging passes on the bench and
  the full OTA cycle is verified (KI-049 closed).

- **ONVIF: падение `divinus` при включении (OKO).** Поток ONVIF создавался
  со стеком 16 КиБ, но использует два 4-КиБ SOAP-буфера и кадры
  `snprintf`: при `onvif.enable=true` процесс падал с `SIGSEGV` ещё до старта
  медиастека. Стек увеличен до 256 КиБ; `GetDeviceInformation` на
  `/onvif/device_service` отвечает, WebUI/RTSP работают. Проверено на стенде
  2026-11-09.
- **ONVIF: `divinus` crashed when ONVIF was enabled (OKO).** The ONVIF thread
  was created with a 16 KiB stack but uses two 4 KiB SOAP buffers and
  `snprintf` frames, so `onvif.enable=true` made the process die with `SIGSEGV`
  before the media stack started. The stack is now 256 KiB;
  `GetDeviceInformation` on `/onvif/device_service` responds and WebUI/RTSP
  keep working. Verified on the bench on 2026-11-09.

- **Долги ревью базы netboot (OKO).** `tools/native/setup_netboot.sh`
  перезапускает `tftpd-hpa` после правки `/etc/default` и drop-in
  (`systemctl enable` + `restart` вместо `enable --now`), поэтому
  повторный запуск применяет конфигурацию. `KNOWN_ISSUES.md` (KI-047)
  относит сборки «до миграции» к последнему коммиту Docker-эпохи
  `9718258b0` (тег `OKO-v1-06.10.26`) вместо ошибочного `d8ba46293`.
  Из `sysdrv/Makefile` удалены неиспользуемые `HCITOOL_TOOL_PATH` и
  `MPV_PATCH_PATH`. В `bench/netboot.py` CMA вынесена в переменную
  окружения `OKO_CMA_SIZE` (по умолчанию `66M`) — хардкод убран.
- **Base netboot review debts (OKO).** `tools/native/setup_netboot.sh`
  restarts `tftpd-hpa` after rewriting `/etc/default` and the drop-in
  (`systemctl enable` + `restart` instead of `enable --now`), so a second
  run applies the configuration. `KNOWN_ISSUES.md` (KI-047) now points
  pre-migration builds at the last Docker-era commit `9718258b0` (tag
  `OKO-v1-06.10.26`) instead of the wrong `d8ba46293`. Unused
  `HCITOOL_TOOL_PATH` and `MPV_PATCH_PATH` were removed from
  `sysdrv/Makefile`. In `bench/netboot.py` the CMA size is now the
  `OKO_CMA_SIZE` environment variable (default `66M`) instead of a
  hardcoded value.

- **Сборка драйвера atbm на GNU make 4.4 (OKO).** В
  `sysdrv/drv_ko/wifi/atbm/Makefile.build.customer` переменные
  `SDIO_HOST`…`MODPMSTAYAWK` объявлены через `:=` вместо `?=` с
  `$(shell echo …)`, а `DRIVER_PATH` в `atbm/Makefile` — через `:=`.
  Причина: make 4.4 при голом `export` рекурсивно экспортирует такие
  переменные и зависает на предупреждениях «не раскрывается рекурсивно
  для экспорта в функцию оболочки»; на Ubuntu 22.04 (make 4.3) сборка
  проходила. После правки `make -C sysdrv drv` завершается.
- **The atbm driver builds with GNU make 4.4 (OKO).** In
  `sysdrv/drv_ko/wifi/atbm/Makefile.build.customer` the
  `SDIO_HOST`…`MODPMSTAYAWK` variables use `:=` instead of `?=` with
  `$(shell echo …)`, and `DRIVER_PATH` in `atbm/Makefile` is also
  simply expanded. With make 4.4 a bare `export` recursively exports
  such variables and hangs on “recursive variable … references itself
  for export to shell function” warnings; make 4.3 on Ubuntu 22.04
  completed the build. `make -C sysdrv drv` now finishes.

- **`parser-conf.sh` драйвера ssv6x5x сделан исполняемым (OKO).** Файл
  `sysdrv/drv_ko/wifi/ssv6x5x/parser-conf.sh` вызывается через `env` из
  Makefile, но лежал в git с режимом `100644`, из-за чего при каждой
  сборке печаталось `Permission denied` и заголовок парсера не
  обновлялся. Режим исправлен на `100755`.
- **The ssv6x5x `parser-conf.sh` is now executable (OKO).** The driver
  Makefile calls `sysdrv/drv_ko/wifi/ssv6x5x/parser-conf.sh` through
  `env`, but the file was tracked as `100644`, so every build printed
  `Permission denied` and the parser header was never regenerated. The
  mode is fixed to `100755`.

- **Фокус: AF-метрика включается независимо от STM8 (OKO).** `POST
  /api/focus` применяет `enable`/`mode`/`fps`/ROI до взведения привода:
  блок `motor` перенесён после `focus_start`/`focus_configure`, поэтому
  отсутствующий, занятый или находящийся в бутлоадере контроллер больше не
  блокирует измерение. Ошибки `blocked`/409/502 сохранены для чистых моторных
  запросов, а `af=1|fine` по-прежнему требуют взведённого драйвера.
  `api_check.py` получил две проверки `enable=1` без `motor` и `enable=0`;
  обновлены `doc/endpoints.md`, `divinus/README.md`, `WEBUI_REORG.md` §8
  (обход через curl снят) и `KNOWN_ISSUES.md` (KI-043 закрыт). Счётчики в
  `bench/README.md`, `FEATURES_VERIFIED.md` и `VERIFIED_FUNCTIONS.md`
  приведены к фактическому прогону.
- **Focus: the AF metric starts regardless of the STM8 (OKO).** `POST
  /api/focus` applies `enable`/`mode`/`fps`/ROI before arming the driver: the
  `motor` block moved after `focus_start`/`focus_configure`, so a missing, busy
  or bootloader-stuck controller no longer blocks the measurement. The
  `blocked`/409/502 responses remain for pure motor requests, and `af=1|fine`
  still require the armed driver. `api_check.py` gained two checks (`enable=1`
  without `motor`, then `enable=0`); `doc/endpoints.md`, `divinus/README.md`,
  `WEBUI_REORG.md` §8 (the curl workaround is gone) and `KNOWN_ISSUES.md`
  (KI-043 closed) are updated. The counters in `bench/README.md`,
  `FEATURES_VERIFIED.md` and `VERIFIED_FUNCTIONS.md` follow the actual run.
- **ePTZ: кроп/поворот при старте канала и геометрия оверлеев (OKO).**
  Кроп и поворот теперь применяются в `rk_channel_bind()` сразу после
  `EnableChn` (через `rk_apply_stream_settings`), поэтому каждый старт канала
  получает сохранённые настройки, а не теряет их молча: включение канала
  сбрасывает `SetChnCrop`/`SetChnRotation`, а вызов после него работает
  (замеры 2026-11-07). Смена на работающем канале только сохраняется, и
  `/api/eptz` отдаёт `applied:false` и `note` (KI-044).
  NN-рамки поворачиваются вместе с
  картинкой: координаты детекций проходят через кроп и поворот
  (`nn_geom.c`), а не только масштабируются по неповёрнутому кадру.
  Систематический сдвиг рамки до 15 px вверх/влево от квантования на сетку
  16 px убран (выравнивание 4 px). Текстовый OSD при 90/270 переезжает к тому
  же углу кадра, а не остаётся за пределами portrait-картинки.
- **ePTZ: crop/rotation at channel start and overlay geometry (OKO).** The
  crop and rotation are now applied in `rk_channel_bind()` right after
  `EnableChn` (via `rk_apply_stream_settings`), so every channel start receives
  the stored settings instead of silently dropping them: the enable resets
  `SetChnCrop`/`SetChnRotation`, while a call after it works (2026-11-07
  measurements). A change on a running channel is only stored and `/api/eptz`
  reports `applied:false` plus a `note` (KI-044). NN boxes rotate with the
  picture:
  detection coordinates go through the crop and the rotation (`nn_geom.c`)
  instead of being scaled on the unrotated frame only. The systematic
  up-to-15-px up/left shift from the 16-px grid quantization is gone (4-px
  alignment). At 90/270 the OSD text moves to the same picture corner instead
  of staying outside the portrait picture.
- **Документация: ePTZ понижен до «ограничено», версии STM8 — 2.1.1 (OKO).**
  Строка ePTZ убрана из `FEATURES_VERIFIED.md` и добавлена в
  `FEATURES_UNVERIFIED.md` как **ограничено**: API принимает кроп и отдаёт
  запрошенные `x/y/w/h`, но влияние на main в сборке 2026-11-07 не замерялось;
  заведена KI-044 (кроп не наблюдается, HAL теряет код возврата MPP, кроп
  сбрасывается при рестарте канала). `VERIFIED_FUNCTIONS.md` помечает ePTZ как
  «ограничено» с честным доказательством (echo-проверка 2026-11-05). Версии
  STM8 в `FEATURES_VERIFIED.md` / `VERIFIED_FUNCTIONS.md` обновлены с
  2.0.1/2.0.2 до 2.1.1 (релиз `7969e922f`), перекрёстная ссылка на
  «непроверенные» `home`/`zero` убрана — они покрыты `api_check.py`.
- **Docs: ePTZ lowered to "limited", STM8 versions to 2.1.1 (OKO).** The ePTZ
  row is removed from `FEATURES_VERIFIED.md` and added to
  `FEATURES_UNVERIFIED.md` as **limited**: the API accepts the crop and returns
  the requested `x/y/w/h`, but its effect on main was not measured on the
  2026-11-07 build; KI-044 records that the crop is not observable, the HAL
  drops the MPP return code and a channel restart resets the crop.
  `VERIFIED_FUNCTIONS.md` marks ePTZ as "limited" with the honest evidence (a
  2026-11-05 echo check). STM8 versions in `FEATURES_VERIFIED.md` /
  `VERIFIED_FUNCTIONS.md` are updated from 2.0.1/2.0.2 to 2.1.1 (release
  `7969e922f`), and the cross-reference to "unverified" `home`/`zero` is gone
  because `api_check.py` covers them.
- **WebUI: опрос больше не сбрасывает «Включить» в «Помощи фокусировке» (OKO).**
  Поля `#focus_enable`/`#focus_mode`/`#focus_fps` подключены к общей модели
  `groups`/`snapshots` (`data-group="focus"`), кнопка «Применить» помечена
  `data-apply="focus"`/`data-ep="focus"`, снапшот снимается в `init()`.
  `focusApplyValues(dec, force)` пишет поля только при отсутствии
  несохранённой правки или по `force`; `focusPoll` раз в 2 с больше не
  затирает выбор, жёлтая точка и доступность «Применить» считаются через
  `groupDirty`, причём кнопка активна и при ожидающей выделенной зоне
  (`pendingRoi`). Явные действия (`focusStop`, «Автофокус» `af=fine`,
  успешное «Применить») применяют состояние сервера принудительно; при
  отказе сервера правка сохраняется и точку можно применить повторно.
  `bench/ui_check.py` получил регресс-проверку «несохранённый выбор
  переживает цикл опроса» без POST и без движения объектива. Добавлена
  запись KI-043 (включение метрики из WebUI зависит от STM8: `Apply` шлёт
  `motor=1`; обход — `POST /api/focus?enable=1&mode=full` без `motor`).
  Обновлены `KNOWN_ISSUES.md`, `bench/README.md`, `project/app/divinus/README.md`
  и `WEBUI_REORG.md` (RU+EN).
- **WebUI: the poll no longer resets "Enable" in Focus assist (OKO).** The
  `#focus_enable`/`#focus_mode`/`#focus_fps` fields now belong to the shared
  `groups`/`snapshots` model (`data-group="focus"`), the Apply button carries
  `data-apply="focus"`/`data-ep="focus"`, and the baseline snapshot is taken in
  `init()`. `focusApplyValues(dec, force)` writes the fields only when there is
  no unsaved edit or when `force` is set; the 2 s `focusPoll` no longer
  overwrites the selection, the navigation dot and Apply state come from
  `groupDirty`, and Apply stays enabled while a zone selection is pending
  (`pendingRoi`). Explicit actions (`focusStop`, the `af=fine` Autofocus
  button, a successful Apply) accept the server state forcibly; on a server
  rejection the edit is kept and can be applied again. `bench/ui_check.py`
  gained a regression check "an unsaved value survives a poll cycle" that
  sends no POST and does not move the lens. New KI-043: enabling the metric
  from the WebUI depends on the STM8 because Apply sends `motor=1`
  (workaround: `POST /api/focus?enable=1&mode=full` without `motor`).
  `KNOWN_ISSUES.md`, `bench/README.md`, `project/app/divinus/README.md` and
  `WEBUI_REORG.md` (RU+EN) are updated.
- **I2C STM8: сериализация транзакций (OKO).** Каждая I2C-транзакция
  в `focus_motor.c` теперь берёт внутрипроцессный мьютекс, помимо
  advisory-flock. Раньше опрос версии (`firmware_status`, shared-блок) и
  чтение позиции из другого потока могли перемешать фазы записи/чтения:
  `GET_POS` получал ответ предыдущего `GET_STATE` (позиция
  `0x00020101` = «2.1.1»). Ожидание занятой шины ограничено ~100 мс;
  нулевой ответ версии сразу после `SET_POS` перепроверяется, а
  автофокус повторяет чтение позиции перед тем, как счесть шкалу
  испорченной.
- **STM8 I2C: transaction serialisation (OKO).** Every I2C transaction in
  `focus_motor.c` now takes an in-process mutex in addition to the advisory
  flock. Previously the version poll (`firmware_status`, shared lock) and a
  position read from another thread could interleave their write/read phases:
  `GET_POS` received the previous `GET_STATE` reply (position `0x00020101` =
  "2.1.1"). Bus contention now waits for at most ~100 ms; a zero version reply
  right after `SET_POS` is re-read, and the auto-focus retries the position
  read before treating the scale as corrupted.

### Удалено

- **Вендорские ветки из лаунчера и `RkLunch*.sh` из образа (OKO).** Из нового
  лаунчера убраны циклы `rcS`/`rcK` по отсутствующему `/oem/usr/etc/init.d`,
  fallback `/data/ethaddr.txt` + `udhcpc` (без `/usr/bin/oko-net` сеть не
  поднимается, в консоль выводится явное сообщение) и настройка core dump
  (`ulimit -c unlimited`, `/data/core-%p-%e`): её владелец —
  `/etc/profile.d/enable_coredump.sh` из `project/build.sh`, ядро падает в
  `/mnt/sdcard/core-<pid>-<exe>`. `RkLunch.sh`/`RkLunch-stop.sh` больше не
  попадают в образ.
- **Vendor branches dropped from the launcher and `RkLunch*.sh` removed from
  the image (OKO).** The new launcher no longer walks `rcS`/`rcK` over the
  absent `/oem/usr/etc/init.d`, has no `/data/ethaddr.txt` + `udhcpc` fallback
  (without `/usr/bin/oko-net` the network stays down and an explicit message
  is printed) and sets up no core dumps (`ulimit -c unlimited`,
  `/data/core-%p-%e`): that belongs to `/etc/profile.d/enable_coredump.sh`
  from `project/build.sh`, and kernels land in `/mnt/sdcard/core-<pid>-<exe>`.
  `RkLunch.sh`/`RkLunch-stop.sh` are no longer part of the image.

- **Итоговый размер slim-образа (OKO, 2026-11-10).** До ветки: `oem`
  26 574 КиБ, rootfs 99 461 КиБ, `oem.img` 19 136 512 Б, `rootfs.img`
  63 307 776 Б, `update.img` 89 408 074 Б. После: `oem` 24 442 КиБ,
  rootfs 59 304 КиБ, `oem.img` 17 956 864 Б, `rootfs.img` 40 108 032 Б,
  `update.img` 65 028 682 Б. Экономия: 2 132 КиБ в `oem`, 40 157 КиБ в
  rootfs, 1,1 МиБ в `oem.img`, 22,1 МиБ в `rootfs.img` и 23,3 МиБ в
  `update.img`.
- **Final slim image size (OKO, 2026-11-10).** Before: `oem` 26,574 KiB,
  rootfs 99,461 KiB, `oem.img` 19,136,512 B, `rootfs.img` 63,307,776 B,
  `update.img` 89,408,074 B. After: `oem` 24,442 KiB, rootfs 59,304 KiB,
  `oem.img` 17,956,864 B, `rootfs.img` 40,108,032 B, `update.img`
  65,028,682 B. Saved: 2,132 KiB in `oem`, 40,157 KiB in the rootfs tree,
  1.1 MiB in `oem.img`, 22.1 MiB in `rootfs.img` and 23.3 MiB in
  `update.img`.

- **Лишние v4l-утилиты и `rockchip_test` (OKO).** Из rootfs вырезаны
  `usr/bin/v4l2-compliance`, `usr/bin/v4l2-sysfs-path`, `usr/sbin/v4l2-dbg`,
  каталог `rockchip_test` и `etc/init.d/S99_auto_reboot`; `v4l2-ctl` и
  `media-ctl` оставлены для диагностики по `TESTBENCH.md`.
- **Extra v4l utilities and `rockchip_test` (OKO).** The rootfs no longer
  contains `usr/bin/v4l2-compliance`, `usr/bin/v4l2-sysfs-path`,
  `usr/sbin/v4l2-dbg`, the `rockchip_test` tree or
  `etc/init.d/S99_auto_reboot`; `v4l2-ctl` and `media-ctl` stay for the
  `TESTBENCH.md` diagnostics.

- **Python 3.14 удалён из rootfs (OKO).** Из
  `sysdrv/tools/board/buildroot/luckfox_pico_defconfig` и рабочей копии
  `buildroot-2026.08.1/configs/luckfox_pico_defconfig` убраны `PYTHON3`,
  `PYTHON3_SSL` и все Python-пакеты (aiohttp, click, itsdangerous, jinja2,
  periphery, pillow, pyserial, setuptools, smbus-cffi, spidev, werkzeug).
  В `oko-prune.list` добавлены `usr/lib/python3.14`,
  `libpython3.14.so.1.0`, `python*`, `pyserial-*` и `S99python` — страховка
  для инкрементальной сборки. Освобождается около 34 МБ в дереве rootfs.
- **Python 3.14 is removed from the rootfs (OKO).**
  `sysdrv/tools/board/buildroot/luckfox_pico_defconfig` and the working copy
  `buildroot-2026.08.1/configs/luckfox_pico_defconfig` no longer select
  `PYTHON3`, `PYTHON3_SSL` or any Python package (aiohttp, click,
  itsdangerous, jinja2, periphery, pillow, pyserial, setuptools, smbus-cffi,
  spidev, werkzeug). `oko-prune.list` gained `usr/lib/python3.14`,
  `libpython3.14.so.1.0`, `python*`, `pyserial-*` and `S99python` as an
  incremental-build safety net. The rootfs tree shrinks by about 34 MB.

- **rkipc, серверы rkaiq и вендорские тесты вырезаны из образа (OKO).**
  `usr/bin/rkipc`, шесть `rkipc-*.ini`, `image.bmp`, `speaker_test.wav`,
  `rkaiq_3A_server`, `rkaiq_tool_server`, все `rk_mpi_*_test`/`mpi_enc_test`/
  `mpp_info_test`/`vpu_api_test` и `rk_*_test`/`luckfox_*_test` добавлены в
  `oko-prune.list` и не попадают в `oem.img`. Пакет rkipc по-прежнему
  собирается: он устанавливает `RkLunch.sh`/`RkLunch-stop.sh`. Divinus грузит
  `librkaiq.so` в процессе, поэтому библиотеки и `iqfiles` сохранены.
- **rkipc, rkaiq servers and vendor tests are pruned (OKO).**
  `usr/bin/rkipc`, the six `rkipc-*.ini` files, `image.bmp`,
  `speaker_test.wav`, `rkaiq_3A_server`, `rkaiq_tool_server`, every
  `rk_mpi_*_test`/`mpi_enc_test`/`mpp_info_test`/`vpu_api_test` and
  `rk_*_test`/`luckfox_*_test` entry joined `oko-prune.list` and no longer
  reach `oem.img`. The rkipc package is still built because it installs
  `RkLunch.sh`/`RkLunch-stop.sh`. Divinus loads `librkaiq.so` in-process, so
  the libraries and `iqfiles` are kept.

- **Декларативное вырезание из образа, первые позиции (OKO).** Добавлен
  `project/cfg/BoardConfig_IPC/oko-prune.list` — единый список путей
  относительно rootfs и `oem` (комментарии и glob разрешены), который
  применяет `__RUN_POST_CLEAN_FILES` в `project/build.sh` (`Prune: <путь>`,
  отсутствие пути — предупреждение). Добавлен
  `tools/native/check_image_contents.sh`: проверяет, что обязательные файлы
  OKO на месте, а позиции из prune-списка в образе отсутствуют. Первыми
  вырезаны неиспользуемые вендорские бинарники `usr/bin/modetest` и
  `usr/bin/dumpsys`.
- **Declarative image pruning, first entries (OKO).**
  `project/cfg/BoardConfig_IPC/oko-prune.list` is a single list of paths
  relative to both the rootfs and `oem` (comments and globs allowed), applied
  by `__RUN_POST_CLEAN_FILES` in `project/build.sh` (`Prune: <path>`, a missing
  path is only a warning). `tools/native/check_image_contents.sh` verifies that
  the required OKO files are present and that no prune-list entry survives.
  The first removed items are the unused vendor binaries `usr/bin/modetest`
  and `usr/bin/dumpsys`.

- **Docker-окружение сборки прошивки удалено (OKO).** Удалены `Dockerfile`,
  `docker-compose.yml`, `docker-entrypoint.sh`, `start.sh` и контейнер
  `luckfox_oko_build_env`; сборка, прошивка и сетевые сервисы стенда
  выполняются на хосте Debian 13 (`tools/native/`, systemd
  `tftpd-hpa`/`nfs-kernel-server`). Из документации и правил агента убраны
  ссылки на контейнер. Отдельные образы `tools/rknn-toolkit2/` и
  `tools/stm8-sdcc/` не затронуты.
- **The firmware build Docker environment is removed (OKO).** `Dockerfile`,
  `docker-compose.yml`, `docker-entrypoint.sh`, `start.sh` and the
  `luckfox_oko_build_env` container are gone; builds, flashing and the bench
  network services now run on the Debian 13 host (`tools/native/`, systemd
  `tftpd-hpa`/`nfs-kernel-server`). Container references were removed from
  the docs and agent rules. The separate `tools/rknn-toolkit2/` and
  `tools/stm8-sdcc/` images are unaffected.

- **Драйвер `motor.ko` (28BYJ-48, pan/tilt) удалён из форка (OKO).** Удалён
  каталог `sysdrv/drv_ko/motor` (исходники, `sample`, `src`), `motor` убран из
  `M_DIRS` в `sysdrv/drv_ko/Makefile`, из `sysdrv/drv_ko/insmod_ko.sh` убрана
  строка `__insmod motor.ko`, из `sysdrv/cfg/package.mk` — блок
  `CONFIG_SYSDRV_ENABLE_MOTOR`/`RK_ENABLE_MOTOR`. В образ модуль никогда не
  попадал; привод фокуса/зума — STM8 по I2C. KI-021 закрыт (2026-11-07).
  Обновлены `STRUCTURE.md`, `INIT_ORDER.md` (§1, §6, §7, §8), `TESTBENCH.md`
  (§1, §8, §9), `README_RU.md`, `KNOWN_ISSUES.md`, `AGENTS.md` и файлы в
  `.kilo/`.
- **The `motor.ko` driver (28BYJ-48, pan/tilt) is removed from the fork
  (OKO).** The `sysdrv/drv_ko/motor` tree (sources, `sample`, `src`) is gone,
  `motor` is dropped from `M_DIRS` in `sysdrv/drv_ko/Makefile`, the
  `__insmod motor.ko` line is removed from `sysdrv/drv_ko/insmod_ko.sh`, and
  the `CONFIG_SYSDRV_ENABLE_MOTOR`/`RK_ENABLE_MOTOR` block is removed from
  `sysdrv/cfg/package.mk`. The module never reached the image; focus/zoom is
  driven by the STM8 over I2C. KI-021 is closed (2026-11-07). `STRUCTURE.md`,
  `INIT_ORDER.md` (§1, §6, §7, §8), `TESTBENCH.md` (§1, §8, §9),
  `README_RU.md`, `KNOWN_ISSUES.md`, `AGENTS.md` and the files under `.kilo/`
  are updated.

- **Очистка устаревших артефактов и git-мусора (OKO).** Удалены 21 из 22
  релизных каталогов `IMAGE/` (~6.4 ГБ); оставлен
  `SC230AI_SPI_NAND_BUILDROOT_RV1106_LUCKFOX_PICO_PRO_MAX_20261108.0748_RELEASE_TEST`.
  Вместе с промежуточными сборками 20261104–20261106 удалены все
  `..._NETBOOT_...` и артефакт RV1103 Pico Mini — KI-023 закрыт. В
  `output/bench/` удалены промежуточные OSD-кадры (кроме
  `osd-20261108-100849-*`), каталоги `osd-*` (кроме `osd-fix2`),
  `rknpu-{A,B,C,baseline}-*` (кроме `rknpu-O-20261107`), черновики
  `stm8-*.ihx`, образы и логи U-Boot, `serial-capture.py`, `sha_test.c`;
  `userdata-backup-20261106.tar` оставлен, потому что его содержимое не
  дублируется в `rknpu-O-20261107/userdata-backup/`. Удалены `output/*.log`,
  кроме `build_all.log`, и каталоги `__pycache__`. В git удалены
  `sysdrv/drv_ko/wifi/atbm/hal_apollo/main.c.bak` и
  `sysdrv/drv_ko/wifi/atbm/.config.old` (ссылок в сборке нет). Завершённые
  планы `.kilo/plans/*.md` удалены, каталог сохранён. Обновлены
  `STRUCTURE.md`, `TESTBENCH.md`, `README_RU.md`, `KNOWN_ISSUES.md`,
  `bench/README.md` и `.kilo/skills/oko-docs/SKILL.md`.
- **Obsolete artifacts and git junk removed (OKO).** 21 of the 22 `IMAGE/`
  release directories (~6.4 GB) are gone; only
  `SC230AI_SPI_NAND_BUILDROOT_RV1106_LUCKFOX_PICO_PRO_MAX_20261108.0748_RELEASE_TEST`
  remains. The intermediate 20261104–20261106 builds went together with all
  `..._NETBOOT_...` directories and the RV1103 Pico Mini artifact, closing
  KI-023. Under `output/bench/` the intermediate OSD frames (except
  `osd-20261108-100849-*`), the `osd-*` directories (except `osd-fix2`),
  `rknpu-{A,B,C,baseline}-*` (except `rknpu-O-20261107`), the `stm8-*.ihx`
  drafts, the U-Boot images and logs, `serial-capture.py` and `sha_test.c`
  are removed; `userdata-backup-20261106.tar` is kept because its contents
  are not duplicated in `rknpu-O-20261107/userdata-backup/`. `output/*.log`
  (except `build_all.log`) and the `__pycache__` directories are removed.
  Git no longer tracks `sysdrv/drv_ko/wifi/atbm/hal_apollo/main.c.bak` or
  `sysdrv/drv_ko/wifi/atbm/.config.old` (nothing in the build referenced
  them). The completed `.kilo/plans/*.md` files are deleted and the
  directory is kept. `STRUCTURE.md`, `TESTBENCH.md`, `README_RU.md`,
  `KNOWN_ISSUES.md`, `bench/README.md` and
  `.kilo/skills/oko-docs/SKILL.md` are updated.

## [0.2.0] — 2026-11-07

- `KNOWN_ISSUES.md` — реестр открытых ошибок и ограничений: критичные
  (`vi1`, librga, отсутствие модели 640×384, лицензия YOLOv5), функциональные
  (ночная точность, CMA, OSD, IVS, HDR, VENC), прошивка/конфигурация
  (`update.img` и env, `reboot loader`, `ubinfo`, старый YAML), безопасность и
  инструменты. Для каждой проблемы указаны проявление, обход и статус;
  ссылки добавлены в `STRUCTURE.md`, `README_RU.md`, `FEATURES.md` и `AGENTS.md`.
- `KNOWN_ISSUES.md` — a register of open defects and limitations: critical
  (`vi1`, librga, missing 640×384 model, YOLOv5 licence), functional (night
  accuracy, CMA, OSD, IVS, HDR, VENC), flashing/configuration (`update.img`
  and env, `reboot loader`, `ubinfo`, old YAML), security and tooling. Each
  issue lists its symptom, workaround and status; links added to
  `STRUCTURE.md`, `README_RU.md`, `FEATURES.md` and `AGENTS.md`.

### Изменено

- **WebUI: фокус в «Видео», автозапуск MP4 и ROI поверх потока (OKO).**
  Карточка «Помощь фокусировке» перенесена из «Изображения» в «Видео»
  (`sec-live`). Предпросмотр запускается сам при входе в раздел с каскадом
  MP4 → MJPEG → снимок `/image.jpg` (браузер без HEVC не декодирует H.265 и
  через ~4 с переходит на резервную ветку), при уходе из раздела поток
  освобождается. Зона фокуса выбирается перетаскиванием прямо поверх живого
  видео; рамка `#focus-box` находится в `#preview`, ROI по-прежнему не
  учитывает поворот 90°/270°. Дефолт `app_config.mp4_enable` изменён на
  `true` (фабричный `divinus.yaml` уже включал MP4/H.265), сохранённые
  конфиги `/userdata` не мигрируются. Обновлены `bench/ui_check.py`,
  `project/app/divinus/README.md`, `source/doc/endpoints.md`,
  `source/doc/WEBUI_REORG.md` и `FEATURES_VERIFIED.md`.
- **WebUI: focus card in Live, MP4 auto-start and ROI over the stream (OKO).**
  The Focus assist card moved from Image to Live (`sec-live`). The preview now
  starts by itself when the section opens, with an MP4 → MJPEG → `/image.jpg`
  cascade (a browser without HEVC cannot decode H.265 and switches to the
  fallback after ~4 s); leaving the section releases the stream. The focus area
  is selected by dragging directly over the live video; `#focus-box` lives
  inside `#preview`, and ROI still ignores 90°/270° rotation.
  `app_config.mp4_enable` now defaults to `true` (the shipped `divinus.yaml`
  already enabled MP4/H.265); saved `/userdata` configs are not migrated.
  Updated `bench/ui_check.py`, `project/app/divinus/README.md`,
  `source/doc/endpoints.md`, `source/doc/WEBUI_REORG.md` and
  `FEATURES_VERIFIED.md`.
- **NPU: драйвер `rknpu` 0.9.2 → 0.9.8 (OKO).** Импортирован upstream
  `drivers/rknpu` из `rockchip-linux/kernel`, ветка `develop-5.10`, коммит
  `276af44f9a7be47a2804ecabf37f8898541d244e` (0.9.8, `20240828`). Сохранено
  локальное отличие — enum `MONITOR_TPYE_DEV` в `rknpu_devfreq.c`; DT и
  non-IOMMU режим не менялись. При `CONFIG_PM_DEVFREQ=y` модуль линкует
  `rknpu_devfreq.o` (в 0.9.2 этот код был внутри `rknpu_drv.c`); `FENCE`,
  `SRAM` и `DRM_GEM` остаются выключены. Рантайм `librknnmrt.so` 1.6.0 не
  менялся. Проверено на DUT hot-swap: `v0.9.8`, YOLO и AIISP работают, три
  перезагрузки и 2-часовой soak без деградации; обновлены `FEATURES.md`,
  `FEATURES_VERIFIED.md`, `VERIFIED_FUNCTIONS.md` и `TESTBENCH.md` (§8).
- **NPU: `rknpu` driver 0.9.2 → 0.9.8 (OKO).** Imported the upstream
  `drivers/rknpu` from `rockchip-linux/kernel`, branch `develop-5.10`, commit
  `276af44f9a7be47a2804ecabf37f8898541d244e` (0.9.8, `20240828`). The local
  `MONITOR_TPYE_DEV` enum change in `rknpu_devfreq.c` is preserved; DT and
  non-IOMMU mode are unchanged. With `CONFIG_PM_DEVFREQ=y` the module now links
  `rknpu_devfreq.o` (0.9.2 kept that code inside `rknpu_drv.c`); `FENCE`,
  `SRAM` and `DRM_GEM` stay disabled. The `librknnmrt.so` 1.6.0 runtime is
  untouched. Verified on the DUT via hot-swap: `v0.9.8`, YOLO and AIISP work,
  three reboots and a 2-hour soak show no degradation; updated `FEATURES.md`,
  `FEATURES_VERIFIED.md`, `VERIFIED_FUNCTIONS.md` and `TESTBENCH.md` (§8).
- **VPN: флаги сборки и статус (OKO).** `sysdrv/drv_ko/Makefile` собирает
  `amneziawg` (`M_DIRS`), `sysdrv/cfg/package.mk` получил
  `CONFIG_SYSDRV_ENABLE_AWG=n` + `RK_ENABLE_AWG`, активный board config —
  `RK_ENABLE_AWG=y`; встроенный `CONFIG_WIREGUARD=y` не менялся.
  `/api/status` отдаёт `features.vpn` (хелпер и хотя бы один бинарь `wg`/`awg`).
  Обновлены `STRUCTURE.md`, `INIT_ORDER.md` (S45oko-vpn, порядок модулей),
  `FEATURES.md`, `FEATURES_UNVERIFIED.md` (KI-037…041), `TESTBENCH.md`,
  `bench/README.md`.
- **VPN: build flags and status (OKO).** `sysdrv/drv_ko/Makefile` builds
  `amneziawg` (`M_DIRS`), `sysdrv/cfg/package.mk` gained
  `CONFIG_SYSDRV_ENABLE_AWG=n` + `RK_ENABLE_AWG`, and the active board config
  sets `RK_ENABLE_AWG=y`; the built-in `CONFIG_WIREGUARD=y` is untouched.
  `/api/status` exposes `features.vpn` (helper plus at least one `wg`/`awg`
  binary). Updated `STRUCTURE.md`, `INIT_ORDER.md` (S45oko-vpn, module order),
  `FEATURES.md`, `FEATURES_UNVERIFIED.md` (KI-037…041), `TESTBENCH.md` and
  `bench/README.md`.
- **Версия прошивки из имени файла (OKO).** `/api/flash` больше не использует
  поле `version`: версия берётся из имени файла (`app-1.0.1.bin`,
  `app-2.0.1.ihx`; ровно одна тройка `major.minor.patch`, каждый компонент
  ≤ 255, иначе 400). Для STM32 она упаковывается в метаданные загрузчика
  (`0x00010001`), для STM8 сервер сверяет её с заголовком OKO8 через
  `oko-motor check FILE X.Y.Z` и отклоняет несовпадение до создания job.
  `/api/firmware` показывает STM32 точечно (`1.0.0`) вместо `0x00010000`.
  `Makefile.stm8` и CMake STM32 кладут версию в имена артефактов
  (`app-2.0.1.ihx`, `factory-2.0.1.ihx`, `app-1.0.1.bin`), сохраняя
  `app.ihx`/`app.bin` копиями для ST-Link; в WebUI поле версии удалено.
- **Firmware version from the file name (OKO).** `/api/flash` no longer uses
  the `version` field: the version comes from the file name
  (`app-1.0.1.bin`, `app-2.0.1.ihx`; exactly one `major.minor.patch` triple,
  each component ≤ 255, otherwise 400). STM32 packs it into the bootloader
  metadata (`0x00010001`); STM8 compares it with the OKO8 header through
  `oko-motor check FILE X.Y.Z` and rejects a mismatch before creating a job.
  `/api/firmware` shows STM32 as `1.0.0` instead of `0x00010000`.
  `Makefile.stm8` and the STM32 CMake place the version in artifact names
  (`app-2.0.1.ihx`, `factory-2.0.1.ihx`, `app-1.0.1.bin`) while keeping
  `app.ihx`/`app.bin` copies for ST-Link; the WebUI version field is gone.
- **Итоговый `update.img`.** Пересобран после изменения артефактов: sha256
  `ad0b07ca19e32462540187e770af2a3f1f8bb3b099491a217ded29df8ce625e4`
  (`output/bench/update-img-sha256.txt`); хеш `a4d93f3f…` выше относится к
  сборке до этой правки. `build.sh app`/`firmware`, `api_check.py` (84/84) и
  `ui_check.py` проходят на стенде.
- **Final `update.img`.** Rebuilt after the artifact change: sha256
  `ad0b07ca19e32462540187e770af2a3f1f8bb3b099491a217ded29df8ce625e4`
  (`output/bench/update-img-sha256.txt`); the `a4d93f3f…` hash above belongs
  to the build before this change. `build.sh app`/`firmware`,
  `api_check.py` (84/84) and `ui_check.py` pass on the bench.

### Добавлено

- **STM8: `SET_POS 0x09` и принудительный сброс позиций (2.0.2/2.1.1).**
  Прошивка получила команду `SET_POS` (`[09][motor][int32 BE]`): позиция и
  цель задаются без движения, команда применяется только вне `RUNNING` и
  помечает позицию для сохранения в EEPROM. Версии подняты до **2.0.2**
  (продукт) и **2.1.1** (тестовая плата); `oko-motor` получил `set-pos
  MOTOR VALUE` с проверкой readback и `home MOTOR [SPEED]`. `/api/focus`
  принимает `home=focus|zoom|both` и `zero=focus|zoom|both&value=N`:
  асинхронный job прижимает шкалу к отрицательному упору чанками, ждёт
  `IDLE` и обнуляет позицию; повторный запуск и прошивка контроллера во
  время job’а возвращают HTTP 409, а прошивка ниже 2.0.2/2.1.1 отклоняется
  с явной ошибкой. `GET /api/focus` отдаёт блок `home` и позиции/статусы
  обоих моторов. Homing считает шаги в open-loop и обнуляет шкалу через
  `SET_POS`, не читая отрицательную позицию (KI-042).
- **Ручное управление фокусом и зумом в WebUI (OKO).** `/api/focus`
  понимает `motor=focus|zoom` для `speed`/`move`/`position` (clamp по
  границам выбранной шкалы) и `motor=both` для скорости; в карточке Focus
  assist появились шаг 1/10/50/200, скорость 50–400, кнопки FOCUS/ZOOM ◀▶,
  STOP и readout позиций, а также кнопки Home/Zero с индикатором задачи.
  Дефолты границ заменены на даташитные: focus 0…2935, zoom 0…2275; новые
  ключи `focus.home_margin`, `focus.zoom_speed`, `focus.zoom_min_pos`,
  `focus.zoom_max_pos`. Старые конфиги с неизменённой парой `-100000`/
  `100000` автоматически переводятся на 0/2935 при загрузке.
  `bench/api_check.py` и `bench/ui_check.py`
  дополнены проверками новых параметров и панели.
- **STM8: `SET_POS 0x09` and forced position reset (2.0.2/2.1.1).** The
  firmware gained `SET_POS` (`[09][motor][int32 BE]`): position and target
  are set without motion, the command is applied only outside `RUNNING`, and
  the position is marked for EEPROM persistence. Versions were bumped to
  **2.0.2** (product) and **2.1.1** (test board); `oko-motor` gained
  `set-pos MOTOR VALUE` with a readback check and `home MOTOR [SPEED]`.
  `/api/focus` accepts `home=focus|zoom|both` and
  `zero=focus|zoom|both&value=N`: a background job drives the scale to the
  negative stop in chunks, waits for `IDLE`, then zeroes the position; a
  second job or a controller flash returns HTTP 409, and firmware older than
  2.0.2/2.1.1 is refused with an explicit error. `GET /api/focus` exposes a
  `home` block and both motor positions/statuses. Homing counts steps
  open-loop and zeroes the scale through `SET_POS` without reading the
  negative position (KI-042).
- **Manual focus and zoom control in the WebUI (OKO).** `/api/focus`
  understands `motor=focus|zoom` for `speed`/`move`/`position` (clamped by
  that scale's bounds) and `motor=both` for speed; the Focus assist card now
  has step 1/10/50/200, speed 50–400, FOCUS/ZOOM ◀▶ buttons, STOP and a
  position readout, plus Home/Zero buttons with a job indicator. The default
  bounds now match the datasheet: focus 0…2935, zoom 0…2275; new keys are
  `focus.home_margin`, `focus.zoom_speed`, `focus.zoom_min_pos` and
  `focus.zoom_max_pos`. Old configs that still carry the untouched
  `-100000`/`100000` pair are migrated to 0/2935 on load.
  `bench/api_check.py` and `bench/ui_check.py` cover
  the new parameters and panel.
- **STM8: прошивка тестовой платы `BOARD=test` (2.1.1).** В
  `project/app/stm8-motor` добавлен вариант сборки под тестовую плату на
  STM8S003F3P6 с отдельной картой пинов (`PD5/PC7/PD3/PD2` + `PA1/PA2` для
  focus, `PC4/PC3/PC6/PC5` + `PA3/PD6` для zoom). Карта выбирается на этапе
  компиляции (`-DBOARD_TEST`) в `stepper.c`, `hw_config.c` и `boot_hw.c`;
  продуктовая сборка по умолчанию остаётся 2.0.2. Смена `BOARD`
  отслеживается маркером `build/.board` и автоматически перекомпилирует все
  исходники, поэтому смешанная сборка с чужой картой не возникает.
  `stm8flash` использует профиль `stm8s003f3` (`FLASH_DEV`);
  документация — `docs/HARDWARE_TESTBOARD.md`, модель MCU и размер EEPROM
  уточнены в `TO_DO.md`, `docs/HARDWARE.md/.ru` и `eeprom.h`. Исправлено
  ошибочное утверждение, что пропуск шагов ловится по `nFAULT`: при 5 В и
  обмотках 40 Ом ток заклинившего мотора равен рабочему и OCP не
  срабатывает.
- **STM8: test-board firmware `BOARD=test` (2.1.1).** `project/app/stm8-motor`
  gained a compile-time variant for the STM8S003F3P6 test board with its own
  pin map (`PD5/PC7/PD3/PD2` + `PA1/PA2` for focus, `PC4/PC3/PC6/PC5` +
  `PA3/PD6` for zoom). The map is selected with `-DBOARD_TEST` in
  `stepper.c`, `hw_config.c` and `boot_hw.c`; the default production build
  stays 2.0.2. A `build/.board` marker makes a `BOARD` switch recompile every
  source, so a mixed build with the wrong pin map cannot occur.
  `stm8flash` now uses the `stm8s003f3` profile (`FLASH_DEV`); documented in
  `docs/HARDWARE_TESTBOARD.md`, with the MCU model and EEPROM size corrected
  in `TO_DO.md`, `docs/HARDWARE.md/.ru` and `eeprom.h`. Also corrected the
  claim that missed steps are caught by `nFAULT`: at 5 V and 40 Ω coils a
  stalled motor draws the same current as a running one, so OCP never trips.
- **Архив внешних исследований `research/` (OKO).** Создана не-сборочная
  папка `research/` с правилами оформления тем: заметка о переносимости
  upstream-кода, read-only снимки с провенансом (путь, лицензия, sha256,
  размер, дата, пин) и лимит на тему. Первая тема —
  `research/rockchip-npu-mainline/`: разбор mainline-драйвера NPU
  `drivers/accel/rocket` (RK3588, четыре ioctl, job → tasks, отдельный
  IOMMU-домен на fd) и вывод, что прямой перенос на RV1106 невозможен;
  снимок шести upstream-файлов (включая `rocket_registers.h`) на пине
  `d48723711bae9b8cdc56719c448ba42fef28cbee`, с обязанностью обновлять
  `CHANGELOG.md` при каждой правке темы. Обновлены `STRUCTURE.md`,
  `AGENTS.md` (карта документации) и `README_RU.md`.
- **External research archive `research/` (OKO).** Added the non-build
  `research/` folder with topic rules: a portability note, read-only upstream
  snapshots with provenance (path, licence, sha256, size, date, pin) and a
  per-topic limit. The first topic is `research/rockchip-npu-mainline/`: an
  analysis of the mainline NPU driver `drivers/accel/rocket` (RK3588, four
  ioctls, job → tasks, a separate IOMMU domain per fd) concluding that a
  direct port to RV1106 is not possible; six upstream files (including
  `rocket_registers.h`) are pinned at
  `d48723711bae9b8cdc56719c448ba42fef28cbee`, and every topic change must
  update `CHANGELOG.md`. Updated `STRUCTURE.md`, `AGENTS.md` (documentation
  map) and `README_RU.md`.
- **Рамки детекции YOLO на видеопотоке (OKO).** Порт вендорского NN-оверлея
  rkipc: регион RGN `12` (`layer 6`) формата 2BPP с двумя цветами (класс 0 —
  красный, прочие — синий), канва главного потока `1920×1088` ≈ 518 КиБ
  (BGRA5551 заняла бы ~4.1 МиБ). Перерисовка в `yolo_capture_thread` после
  каждого инференса (5 Hz): канва полностью очищается, поэтому при нуле
  детекций рамки исчезают; координаты `0..10000` масштабируются в размер
  закодированного потока (90/270 — с обменом сторон) и выравниваются по сетке
  16 px. Добавлены `yolo.boxes` (по умолчанию `false`),
  `POST /api/yolo?boxes=0|1`, живое включение/выключение, поле WebUI EN/RU и
  проверка в `gating` при старте YOLO. Регион пересоздаётся при смене размера
  или поворота главного потока и при потере привязки к VENC; без
  `RK_MPI_RGN_GetCanvasInfo`/`UpdateCanvas` детектор продолжает работать, а
  рамки отключаются с одним предупреждением. DUT: CMA +512 КиБ
  (53 824→54 336), CPU `divinus` 29% при 5 fps без роста, `boxes=2|bogus` →
  HTTP 400, поворот 90° пересоздаёт канву `1088×1920`. Обновлены
  `endpoints.md`, `README` divinus, `FEATURES*.md`, `VERIFIED_FUNCTIONS.md`,
  `TESTBENCH.md` и `TO_DO.md`. Ограничения: только главный поток, два цвета,
  180/270 не проверялись.
- **YOLO detection boxes on the video stream (OKO).** Port of the vendor rkipc
  NN overlay: RGN region `12` (`layer 6`) in 2BPP with two colours (class 0 is
  red, everything else is blue) and a `1920×1088` main-stream canvas of about
  518 KiB (BGRA5551 would take ~4.1 MiB). `yolo_capture_thread` repaints it
  after every inference (5 Hz) and clears the whole canvas, so the boxes vanish
  on zero detections; `0..10000` coordinates are scaled to the encoded stream
  (swapped for 90/270) and snapped to the 16-pixel grid. Added `yolo.boxes`
  (default `false`), `POST /api/yolo?boxes=0|1`, live switching, an EN/RU WebUI
  field and a `gating` check when YOLO starts. The region is recreated when the
  main-stream size or rotation changes and when the overlay is no longer
  attached to the VENC channel; without
  `RK_MPI_RGN_GetCanvasInfo`/`UpdateCanvas` the detector keeps running and the
  boxes are disabled with one warning. DUT: CMA +512 KiB (53,824→54,336),
  `divinus` CPU 29% at 5 fps with no increase, `boxes=2|bogus` → HTTP 400, 90°
  rotation recreates the canvas as `1088×1920`. Updated `endpoints.md`, the
  divinus `README`, `FEATURES*.md`, `VERIFIED_FUNCTIONS.md`, `TESTBENCH.md` and
  `TO_DO.md`. Limitations: main stream only, two colours, 180/270 untested.
- **RGA-конверсия NV12→RGB888 для YOLO (OKO).** На `rknpu` 0.9.8 im2d
  `importbuffer_fd` импортирует MMZ- и dma-heap-дескрипторы; добавлен
  опциональный `yolo.convert: cpu | rga` (по умолчанию `cpu`) и HAL
  `hal/plus/rk_rga.c` (dlopen `librga.so`). RGA-путь отдаёт im2d fd кольца и
  входа `rknn_create_mem`, при ошибке один раз логирует и навсегда
  откатывается на CPU. Замер: CPU `divinus` ~35.6% → ~29.4% при 5 fps
  640×384 без потери fps, 2-часовой soak без зависаний. Легаси
  `c_RkRgaBlit` не используется. Обновлены `KNOWN_ISSUES.md` (KI-002),
  `endpoints.md`, `FEATURES*.md`, `VERIFIED_FUNCTIONS.md`, `TESTBENCH.md`.
- **RGA NV12→RGB888 conversion for YOLO (OKO).** On `rknpu` 0.9.8 im2d
  `importbuffer_fd` imports MMZ and dma-heap descriptors, so YOLO gained the
  optional `yolo.convert: cpu | rga` setting (default `cpu`) and an
  `hal/plus/rk_rga.c` HAL (dlopen of `librga.so`). The RGA path feeds im2d
  with the ring and `rknn_create_mem` fds; on an error it logs once and
  permanently falls back to the CPU converter. Measured `divinus` CPU
  ~35.6% → ~29.4% at 5 fps 640×384 with no fps loss and a 2-hour soak without
  hangs. Legacy `c_RkRgaBlit` is not used. Updated `KNOWN_ISSUES.md`
  (KI-002), `endpoints.md`, `FEATURES*.md`, `VERIFIED_FUNCTIONS.md` and
  `TESTBENCH.md`.
- **Второй рантайм RKNN 2.x для YOLO (OKO).** В образ добавлен
  `librknnmrt2.so` (2.3.2, SONAME изменён на `librknnmrt2.so`, см.
  `media/rknn2/README.md`); AIISP и RockIVA продолжают использовать
  `librknnmrt.so` 1.6.0 в том же процессе. `divinus` получил `yolo.runtime:
  1 | 2` и `yolo.model_dir`; HAL держит два handle (2.x — абсолютный путь,
  `RTLD_LOCAL`), после `rknn_init` проверяется major версии SDK.
  `models.json` хранит `runtime` модели, активация «чужого» рантайма
  отклоняется с причиной, переключение 1↔2 не требует рестарта. Сборка модели
  2.x — `tools/rknn-toolkit2/Dockerfile.toolkit2` (вендорское колесо только
  cp38). Обновлены `endpoints.md`, `FEATURES.md`, `FEATURES_VERIFIED.md`,
  `FEATURES_UNVERIFIED.md`, `VERIFIED_FUNCTIONS.md`. Точность (mAP)
  2.x-модели на размеченном наборе не измерялась.
- **Second RKNN 2.x runtime for YOLO (OKO).** The image now ships
  `librknnmrt2.so` (2.3.2, SONAME changed to `librknnmrt2.so`, see
  `media/rknn2/README.md`); AIISP and RockIVA keep using the 1.6.0
  `librknnmrt.so` in the same process. `divinus` gained `yolo.runtime: 1 | 2`
  and `yolo.model_dir`; the HAL keeps two handles (2.x by absolute path,
  `RTLD_LOCAL`) and checks the SDK major version after `rknn_init`.
  `models.json` records the model runtime, activation of a model from the
  other runtime is refused with a reason, and switching 1↔2 needs no restart.
  The 2.x model is built by `tools/rknn-toolkit2/Dockerfile.toolkit2` (the
  vendor wheel is cp38 only). Updated `endpoints.md`, `FEATURES.md`,
  `FEATURES_VERIFIED.md`, `FEATURES_UNVERIFIED.md`, `VERIFIED_FUNCTIONS.md`.
  The 2.x model accuracy (mAP) on a labelled set is not measured.
- **VPN: WireGuard и AmneziaWG (OKO).** Out-of-tree модуль `amneziawg.ko`
  (вендорский слепок `amneziawg-linux-kernel-module` `v3.1.20260906`, без
  правок) в `sysdrv/drv_ko/amneziawg`; вендорские `wireguard-tools`
  1.0.20210914 и `amneziawg-tools` 3.1.20260812 в `project/app/vpn-tools`
  собираются app-тулчейном в `/oem/usr/bin/{wg,awg}` (`wg-quick` отключён).
  POSIX-хелпер `/usr/bin/oko-vpn` (validate/up/down/status/autostart/watchdog/
  delete/set-auto) применяет `.conf` через `syncconf`, ведёт split-маршруты,
  делает guard с откатом default route для `0.0.0.0/0`, хранит профили в
  `/userdata/vpn`; `S45oko-vpn` запускает autostart и watchdog.
  `divinus` получил `vpn_conf.c` (парсер `.conf`, отклоняет
  `PreUp/PostUp/PreDown/PostDown`, знает AWG v3.1), `/api/vpn`,
  `/api/vpn/import`, `/api/vpn/export`, токен на delete, `features.vpn` и
  карточку Network → VPN (EN/RU). Добавлены `bench/vpn_check.py` и хостовый
  тест `tests/vpn_conf_test.c`.
- **VPN: проверено на стенде (OKO).** 2026-11-06 на DUT .97: модуль
  `amneziawg` загружается (`dmesg`: `AmneziaWG 3.1.20260906`), `wg`/`awg`
  версий 1.0.20210914/3.1.20260812; handshake WireGuard и AmneziaWG v3.1 с
  loopback-пиром, split-маршруты, API `import/up/down/delete/export`, отказ
  хукам и delete без токена, autostart+watchdog после reboot, guard полного
  туннеля (positive и auto-revert с восстановлением default). `api_check.py`
  — 88/88, `ui_check.py` (карточка Network → VPN, EN/RU) — без ошибок.
  `./build.sh sysdrv`, `app`, `firmware` проходят: `amneziawg.ko` в
  `/oem/usr/ko` (69 688 Б после strip, vermagic 5.10.160), `oem.img`
  19 005 440 Б, `rootfs.img` 57 016 320 Б, `update.img` 82 971 210 Б
  (sha256 `4e6dbc4d…`). Внешний VPN-пир и `ping` через туннель не
  прогонялись; статус в `FEATURES_UNVERIFIED.md`.
- **VPN: verified on the bench (OKO).** 2026-11-06 on DUT .97: the
  `amneziawg` module loads (`dmesg`: `AmneziaWG 3.1.20260906`), `wg`/`awg` are
  1.0.20210914/3.1.20260812; WireGuard and AmneziaWG v3.1 handshakes with a
  loopback peer, split routes, API `import/up/down/delete/export`, rejection of
  hooks and delete without a token, autostart+watchdog after reboot, and the
  full-tunnel guard (positive and auto-revert restoring the default route).
  `api_check.py` — 88/88, `ui_check.py` (Network → VPN card, EN/RU) — clean.
  `./build.sh sysdrv`, `app` and `firmware` pass: `amneziawg.ko` lands in
  `/oem/usr/ko` (69,688 B after stripping, vermagic 5.10.160), `oem.img` is
  19,005,440 B, `rootfs.img` 57,016,320 B and `update.img` 82,971,210 B
  (sha256 `4e6dbc4d…`). An external VPN peer and a tunnel `ping` were not run;
  status in `FEATURES_UNVERIFIED.md`.
- **VPN: WireGuard and AmneziaWG (OKO).** An out-of-tree `amneziawg.ko`
  (vendored `amneziawg-linux-kernel-module` `v3.1.20260906`, unmodified) in
  `sysdrv/drv_ko/amneziawg`; vendored `wireguard-tools` 1.0.20210914 and
  `amneziawg-tools` 3.1.20260812 in `project/app/vpn-tools` built by the app
  toolchain into `/oem/usr/bin/{wg,awg}` (`wg-quick` disabled). The POSIX
  helper `/usr/bin/oko-vpn` (validate/up/down/status/autostart/watchdog/
  delete/set-auto) applies a `.conf` through `syncconf`, manages split routes,
  guards and rolls back the default route for `0.0.0.0/0`, and keeps profiles
  in `/userdata/vpn`; `S45oko-vpn` starts autostart and the watchdog.
  `divinus` gained `vpn_conf.c` (`.conf` parser, rejects
  `PreUp/PostUp/PreDown/PostDown`, supports AWG v3.1), `/api/vpn`,
  `/api/vpn/import`, `/api/vpn/export`, a delete token, `features.vpn` and the
  Network → VPN card (EN/RU). Added `bench/vpn_check.py` and the host test
  `tests/vpn_conf_test.c`.
- **Помощь фокусировке и прошивка контроллеров (OKO).** `divinus` получил
  тред AF-статистики (`focus.c`, `hal/plus/rk_af_stats.c` — единственный TU с
  заголовками rkaiq), геометрию ROI в базисных пунктах (`focus_geom.c`),
  API `/api/focus` и карточку в Image; мотор STM8 `0x60` на `/dev/i2c-3`
  (`focus_motor.c`, `focus_proto.c`). Добавлены `/api/flash`, `/api/firmware`,
  фоновый воркер прошивки (`flash.c`, стоп `S21oko-ec`, приостановка мотора,
  `oko-flash`), кэш версий STM32/STM8 (`firmware_status.c`) и загрузка NPU в
  строку CPU OSD. Новые пакеты: `project/app/stm8-motor` (прошивка STM8,
  загрузчик I2C, `oko-motor`) и `project/app/oko-flash`; отдельный образ
  `tools/stm8-sdcc`. Разметка STM8: boot `0x8000–0x89FF`, заголовок `0x8A00`,
  приложение `0x8A10–0x9FFF` (2.5 КиБ / 5.5 КиБ; измерено 2412/4279 байт).
  Хостовые тесты: `focus_geom_test.c`, `focus_proto_test.c`,
  `stm8-motor/tests` (CRC, заголовок, кадры, ihx). Стенд:
  `bench/focus_check.py`, `bench/firmware_check.py`.
- **Focus assist and controller firmware (OKO).** `divinus` gained the AF
  statistics thread (`focus.c`, `hal/plus/rk_af_stats.c` is the only TU with
  rkaiq headers), ROI geometry in basis points (`focus_geom.c`), `/api/focus`
  and an Image card; the STM8 motor at `0x60` on `/dev/i2c-3`
  (`focus_motor.c`, `focus_proto.c`). Added `/api/flash`, `/api/firmware`, a
  background flash worker (`flash.c`, stops `S21oko-ec`, suspends the motor,
  runs `oko-flash`), a STM32/STM8 version cache (`firmware_status.c`) and NPU
  load on the OSD CPU line. New packages: `project/app/stm8-motor` (STM8
  firmware, I2C bootloader, `oko-motor`) and `project/app/oko-flash`; a
  separate `tools/stm8-sdcc` image. STM8 layout: boot `0x8000–0x89FF`, header
  `0x8A00`, app `0x8A10–0x9FFF` (2.5 KiB / 5.5 KiB; measured 2412/4279 bytes).
  Host tests: `focus_geom_test.c`, `focus_proto_test.c`, `stm8-motor/tests`
  (CRC, header, frames, ihx). Bench: `bench/focus_check.py`,
  `bench/firmware_check.py`.
- **Исходники прошивки STM32.** Полный репозиторий
  `spectrespc/stm32_acpi-ec` на пине
  `41728ddaf6936d49a5fb65676ca5203dd631b4f6` добавлен как git subtree в
  `project/app/stm32_acpi-ec/` (загрузчик, приложение, HAL, CMake, тесты,
  хостовый инструмент). `tools/stm32_push.sh` коммитит только этот каталог и
  отправляет изменения в `stm32/main`, `tools/stm32_pull.sh` подтягивает
  upstream; сборка STM32 — отдельная CMake-сборка и в `./build.sh` не входит.
  **STM32 firmware sources.** The complete `spectrespc/stm32_acpi-ec`
  repository at commit `41728ddaf6936d49a5fb65676ca5203dd631b4f6` is now a
  git subtree in `project/app/stm32_acpi-ec/` (bootloader, application, HAL,
  CMake, tests, host tool). `tools/stm32_push.sh` commits only that directory
  and pushes to `stm32/main`, `tools/stm32_pull.sh` pulls upstream; the STM32
  build is a separate CMake build and is not part of `./build.sh`.

- **Исходники прошивки STM8.** Локальные изменения (I2C-загрузчик, команда
  `0x08 GET_STATE`, `oko-motor check/boot-reset`, тесты и dual-mode
  `Makefile`) отправлены в `spectrespc/stm8s103_i2c_motor` коммитом
  `ef106e6513afaf15db71c2cbc57628a9dfb932c3`; репозиторий добавлен как git
  subtree в `project/app/stm8-motor/`. `tools/stm8_push.sh` коммитит только
  этот каталог и отправляет изменения в `stm8/main`, `tools/stm8_pull.sh`
  подтягивает upstream; сборка прошивки — SDCC, `oko-motor` собирается в SDK.
  **STM8 firmware sources.** The local changes (I2C bootloader, `0x08
  GET_STATE`, `oko-motor check/boot-reset`, tests and the dual-mode
  `Makefile`) were pushed to `spectrespc/stm8s103_i2c_motor` as commit
  `ef106e6513afaf15db71c2cbc57628a9dfb932c3`; the repository is now a git
  subtree at `project/app/stm8-motor/`. `tools/stm8_push.sh` commits only that
  directory and pushes to `stm8/main`, `tools/stm8_pull.sh` pulls upstream;
  firmware uses SDCC while `oko-motor` is built in the SDK.

- **Статусы задач.** В плане и `TO_DO.md` отмечены выполненные пункты;
  на стенде проверены A6, мотор STM8, загрузчик/обновление STM8, версии,
  NPU OSD и реальная прошивка STM32 (CLI 1.0.1 → WebUI 1.0.0). Открыты
  обрыв питания при стирании и оптическая связь объектива с мотором.
  **Task status.** Completed plan/TO_DO items are marked; the bench verified
  A6, the STM8 motor, the STM8 bootloader/update, versions, NPU OSD and a real
  STM32 update (CLI 1.0.1 → WebUI 1.0.0). Still open: power loss during erase
  and the lens-to-motor optical coupling.

### Исправлено

- **STM8: ложный `STATUS_ERROR` при пробуждении драйвера.** `stepper_tick()`
  опрашивал nFAULT сразу после подъёма nSLEEP, до выдержки `wake_ticks`, и
  DRV8833 в окне tWAKE иногда держал nFAULT низким: 15–25% первых шагов после
  простоя обрывались ошибкой (на стенде 3 из 20 реверсов). Проверка nFAULT
  перенесена за блок `wake_ticks`; на исправленной прошивке 40 движений и
  sweep 100–800 шаг/с — 0 ложных ERROR. **STM8: spurious `STATUS_ERROR` on
  driver wake.** `stepper_tick()` sampled nFAULT immediately after nSLEEP
  rose, before the `wake_ticks` delay; the DRV8833 can hold nFAULT low during
  its tWAKE window, so 15–25% of the first steps after idle aborted with an
  error (3 of 20 reverse cycles on the bench). The nFAULT check now runs
  after the `wake_ticks` block; with the fix, 40 moves and the 100–800 pps
  sweep produced no spurious errors.
- **Сборка STM8 и негативные тесты.** Цели `app`/`factory`/`bin` в
  `Makefile.stm8` сериализованы через `.NOTPARALLEL`, чтобы `make -j` не
  запускал несколько `mkapp.py` над одними файлами; проверено чистым
  `make -j8 all`. `firmware_check.py --negative` теперь завершается ошибкой,
  если `app-2.0.1.ihx` отсутствует, вместо тихого пропуска кейсов.
- **STM8 build and negative tests.** The `app`/`factory`/`bin` targets in
  `Makefile.stm8` are serialized with `.NOTPARALLEL` so `make -j` cannot run
  several `mkapp.py` instances over the same files (verified with a clean
  `make -j8 all`). `firmware_check.py --negative` now fails when
  `app-2.0.1.ihx` is missing instead of silently skipping cases.

- **OSD: три строки снова рендерятся.** `oko-osd` снова пишет literal
  `\n` (backslash + n) в `/tmp/osd-sys.txt`: `region_read_text_file()`
  читает только первую строку файла, а `text.c` распознаёт только эту
  последовательность. Вариант с реальными `0x0A` оставлял в кадре лишь
  `CPU … NPU …`. Проверено `od -c` и тремя текст-полосами в вырезке
  sys-региона (`output/bench/osd-fix2/`).
- **OSD: three lines render again.** `oko-osd` writes the literal `\n`
  (backslash + n) sequence to `/tmp/osd-sys.txt` again:
  `region_read_text_file()` reads only the first line of the file and
  `text.c` recognizes only that sequence. Real `0x0A` bytes left only
  `CPU … NPU …` in the frame. Verified with `od -c` and three text bands in
  the sys crop (`output/bench/osd-fix2/`).
- **STM32: слот CBT6 и защита от переразмера.** Установленный загрузчик —
  CBT6 (128 КиБ, слот 120 КиБ), а не C8T6: он принимает 60 КиБ, а
  C8T6-сборка приложения даёт `img_size=0` (метаданные читаются из страниц
  C8T6). Образы пересобраны с `-DMCU=CBT6`; `/api/flash` отклоняет
  STM32-файлы >120 КиБ до создания job; `oko-ec flash` при ошибке записи
  отправляет `RESET` и возвращает приложение (проверено: 130 КиБ →
  `ERR_PARAM` → `RESET` → app 1.0.0).
- **STM32: CBT6 slot and oversize guard.** The installed bootloader is CBT6
  (128 KiB, 120 KiB app slot), not C8T6: it accepts a 60 KiB image, while a
  C8T6 application build reports `img_size=0` (it reads metadata from the
  C8T6 pages). Images are rebuilt with `-DMCU=CBT6`; `/api/flash` rejects
  STM32 files >120 KiB before creating a job; `oko-ec flash` sends `RESET`
  on a write error and returns to the application (verified: 130 KiB →
  `ERR_PARAM` → `RESET` → app 1.0.0).
- **STM32: реальная прошивка проверена.** CLI `1.0.1` (`0x00010001`) →
  `Application started`, `img_size=2324`; затем WebUI `/api/flash` `1.0.0`
  (`0x00010000`) → job `done`, телеметрия и `flags=0`; после reboot —
  app 1.0.0. KI-036 закрыт. Финальный `update.img` — sha256 `a4d93f3f…`.
- **STM32: real flashing verified.** CLI `1.0.1` (`0x00010001`) →
  `Application started`, `img_size=2324`; then the WebUI `/api/flash`
  `1.0.0` (`0x00010000`) → job `done`, telemetry and `flags=0`; after reboot
  the app runs 1.0.0. KI-036 is closed. Final `update.img` sha256
  `a4d93f3f…`.
- **STM32: вход в загрузчик и запуск после прошивки.** `oko-ec flash` теперь
  проверяет роль через `GET_INFO`, при `device_type=2` отправляет
  `ENTER_UPDATE` (0x0E) и ждёт загрузчик до 5 с, а после `COMMIT` отправляет
  `RESET` (0x08) и ждёт приложение до 3 с; при неподтверждённом старте
  печатается предупреждение. Раньше `BEGIN_UPDATE` уходил в работающее
  приложение (`ERR_CMD`), а после `COMMIT` загрузчик оставался активным.
  Переход проверен на стенде без записи: `info` → `enter-update` →
  `info` (bootloader) → `reset` → `info` (app 1.0.0), телеметрия жива.
  Реальная запись и закрытие KI-036 — в записи выше; финальный `update.img`
  — sha256 `a4d93f3f…`.
- **STM32: версия через CMake cache.** `FW_VERSION_STRING` переведён в
  `CACHE STRING`, поэтому `-DFW_VERSION_STRING=1.0.1` даёт
  `FW_VERSION=0x00010001`, а не перекрывается значением по умолчанию.
  Собраны `output/bench/stm32/app-1.0.0.bin` (sha256 `4bb4f433…`) и
  `app-1.0.1.bin` (sha256 `cb1f8e6e…`) для CBT6.
- **STM32: bootloader entry and application start after a flash.**
  `oko-ec flash` now checks the device role via `GET_INFO`, sends
  `ENTER_UPDATE` (0x0E) when `device_type=2` and waits up to 5 s for the
  bootloader, then sends `RESET` (0x08) after `COMMIT` and waits up to 3 s for
  the application; an unconfirmed start produces a warning. Before this,
  `BEGIN_UPDATE` went to the running application (`ERR_CMD`) and the
  bootloader stayed active after `COMMIT`. The transition is verified on the
  bench without writing: `info` → `enter-update` → `info` (bootloader) →
  `reset` → `info` (app 1.0.0), telemetry still alive. The real write and
  KI-036 closure are in the entry above; the final `update.img` sha256 is
  `a4d93f3f…`.
- **STM32: firmware version through the CMake cache.**
  `FW_VERSION_STRING` is now a `CACHE STRING`, so
  `-DFW_VERSION_STRING=1.0.1` yields `FW_VERSION=0x00010001` instead of being
  overridden by the default. Built `output/bench/stm32/app-1.0.0.bin`
  (sha256 `4bb4f433…`) and `app-1.0.1.bin` (sha256 `cb1f8e6e…`) for CBT6.
- **AF-статистика SC230AI.** В IQ включён
  `af_v31.TuningPara.contrast_af.enable=1` (оба блока, день/ночь); без него
  AFM отдавал 225 нулевых ячеек. A6 пройден: `status=running`,
  `cells_used=225`, `metric_avg` 527k–530k за 20 с. ROI на этой сборке
  применяется выбором ячеек полнокадровой карты (аппаратное окно AFM карту не
  меняет), поэтому `mode=roi` теперь меняет метрику. Неудачное применение окна
  повторяется, а `SetAttrib` до 5 раз; нулевая карта один раз включает
  `contrast_af` как fallback.
- **SC230AI AF statistics.** The IQ now has
  `af_v31.TuningPara.contrast_af.enable=1` (both day/night blocks); without it
  the AFM returned 225 zero cells. A6 passes: `status=running`,
  `cells_used=225`, `metric_avg` 527k–530k over 20 s. On this build the ROI is
  applied by selecting cells of the full-frame map (the hardware AFM window
  does not move the map), so `mode=roi` now changes the metric. A failed window
  apply is retried, `SetAttrib` is retried up to 5 times, and a zero map enables
  `contrast_af` once as a fallback.
- **STM8: роль и версия без boot-команды.** Добавлена общая команда `0x08`
  (`[role][major][minor][patch]`); `oko-motor`, `focus_motor.c` и
  `firmware_status.c` сначала читают роль, поэтому `0x10` больше не уходит в
  работающее приложение, а `info` в загрузчике не принимает `06 00 00` за
  версию `6.0.0`. Прошивка STM8 пересобрана: boot 2412 Б, app 4279 Б.
- **STM8: role and version without a boot command.** Added the shared `0x08`
  command (`[role][major][minor][patch]`); `oko-motor`, `focus_motor.c` and
  `firmware_status.c` read the role first, so `0x10` never reaches a running
  application and `info` in the bootloader no longer mistakes `06 00 00` for
  version `6.0.0`. STM8 firmware rebuilt: boot 2412 B, app 4279 B.
- **STM8: загрузчик и прошивка по I2C.** Исправлен порядок инициализации I2C
  в загрузчике (`PE` до `ACK`), IWDG кормится в циклах стирания/записи, а
  `oko-motor` выдерживает паузу перед чтением статуса (`erase` 500 мс,
  `write` 40 мс, `finish` 150 мс). `enter-boot`/`boot-reset`/`info` проверены;
  `app.ihx` заливается через `/api/flash`; `oko-motor move 0 -10` больше не
  воспринимает `-10` как опцию (`+` в `getopt`). Первичная запись —
  `factory.ihx` через ST-Link.
- **STM8: bootloader and I2C flashing.** Fixed the bootloader I2C init order
  (`PE` before `ACK`), the IWDG is fed in erase/write loops, and `oko-motor`
  waits before reading a status (`erase` 500 ms, `write` 40 ms, `finish`
  150 ms). `enter-boot`/`boot-reset`/`info` are verified; `app.ihx` uploads via
  `/api/flash`; `oko-motor move 0 -10` no longer treats `-10` as an option
  (`+` in `getopt`). The first write is `factory.ihx` over ST-Link.
- **Фокус и прошивка: безопасность и устойчивость.** `GET /api/focus`
  теперь только читает (`map=1`), все изменения (`enable`, `mode`, ROI,
  `motor`, `move`, `stop`, `reset`) требуют `POST`; WebUI и `focus_check.py`
  отправляют их POST-запросом с query. Временный файл прошивки создаётся
  через `mkstemp` с расширением и открывается с `O_NOFOLLOW`; `sha256sum`,
  `oko-motor check`, `oko-flash` и init-скрипты запускаются через
  `fork`+`execv` с абсолютными путями (без `popen`/`system`/`execvp`), дочерние
  процессы не наследуют сокеты divinus. `S21oko-ec` перезапускается только
  если демон работал до задачи (по `/tmp/oko-ec.pid`). Ошибка
  `focus_motor_stop_all()` при `motor=0` возвращает 502 вместо ложного
  «disabled». Строка `/tmp/osd-sys.txt` снова содержит три строки
  (`\n` исправлен).
- **Focus and flashing safety.** `GET /api/focus` is read-only (`map=1`); all
  changes (`enable`, `mode`, ROI, `motor`, `move`, `stop`, `reset`) require
  `POST`; the WebUI and `focus_check.py` send them as POST queries. The
  firmware temp file is created with `mkstemp` plus the extension and opened
  with `O_NOFOLLOW`; `sha256sum`, `oko-motor check`, `oko-flash` and init
  scripts run via `fork`+`execv` with absolute paths (no
  `popen`/`system`/`execvp`), and children do not inherit divinus sockets.
  `S21oko-ec` is restarted only if the daemon was running before the job
  (checked through `/tmp/oko-ec.pid`). A failed `focus_motor_stop_all()` on
  `motor=0` returns 502 instead of reporting disabled. `/tmp/osd-sys.txt` is
  back to three lines (the `\n` escaping is fixed).

- **Прошивка через WebUI.** Присутствие контроллера берётся из кэша
  `firmware_status`, а не сырой записью в I2C; временный файл сохраняет
  расширение (`.ihx/.hex/.bin`), поэтому HEX не разбирается как BIN; перед
  заданием STM8 проверяется `oko-motor check` (магия/CRC/диапазон) и плохой
  образ отдаёт 400; SNMP-таймстамп STM32 (`ts`) старше 15 с считается
  отсутствием. Добавлены негативные проверки `firmware_check.py --negative`.
- **WebUI flashing.** Controller presence comes from the `firmware_status`
  cache instead of a raw I2C write; the temporary file keeps its extension
  (`.ihx/.hex/.bin`), so HEX is not parsed as BIN; `oko-motor check`
  (magic/CRC/range) runs before an STM8 job and a bad image returns 400; an
  STM32 snapshot (`ts`) older than 15 s counts as absent. Added
  `firmware_check.py --negative`.

- `tools/rknn-toolkit2/` — Docker-пайплайн YOLOv5n для `rv1106`: образ
  `rknn-toolkit2` 1.6.0 (колесо с `--no-deps`), отдельный образ для экспорта
  `airockchip/yolov5`, `export_yolov5.sh`, `convert_rknn.sh`, `convert.py`.
  Собрана модель 640×384 INT8 (2.6 МиБ, sha256 `8285e1bd…`), активирована на
  DUT: `input 640x384 NHWC stride 640, 3 4D outputs`; 5 fps — NPU 17%, CPU
  `divinus` ~28.5%, main 30.3 fps (640×640 для сравнения: NPU 28%, CPU ~66%).
  Симулятор toolkit на `bus.jpg` даёт 3 person + 1 truck; mAP не измерен.
  Обновлены `TO_DO.md`, `KNOWN_ISSUES.md`, `FEATURES.md`,
  `VERIFIED_FUNCTIONS.md` и `STRUCTURE.md`.
- `tools/rknn-toolkit2/` — Docker pipeline for YOLOv5n on `rv1106`: an
  `rknn-toolkit2` 1.6.0 image (wheel installed with `--no-deps`), a separate
  `airockchip/yolov5` export image, `export_yolov5.sh`, `convert_rknn.sh` and
  `convert.py`. A 640×384 INT8 model was built (2.6 MiB, sha256 `8285e1bd…`)
  and activated on the DUT: `input 640x384 NHWC stride 640, 3 4D outputs`;
  at 5 fps — NPU 17%, `divinus` CPU ~28.5%, main 30.3 fps (640×640 control:
  NPU 28%, CPU ~66%). The toolkit simulator detects 3 persons + 1 truck on
  `bus.jpg`; mAP is not measured. `TO_DO.md`, `KNOWN_ISSUES.md`,
  `FEATURES.md`, `VERIFIED_FUNCTIONS.md` and `STRUCTURE.md` were updated.
- `FEATURES.md` в корне: сводка поддерживаемого функционала — платформа и состав
  образа, видео/стриминг и статусы гейтов, WebUI/API, сеть, `oko-ec`, microSD,
  известные ограничения и команды проверки; продублировано краткой
  English-сводкой. Ссылки добавлены в `README_RU.md`, `STRUCTURE.md` и
  `AGENTS.md`.
- `FEATURES.md` in the repository root: a summary of supported functionality —
  platform and image contents, video/streaming and gate statuses, WebUI/API,
  networking, `oko-ec`, microSD, known limitations and verification commands,
  with a short English digest. Links added to `README_RU.md`, `STRUCTURE.md`
  and `AGENTS.md`.
- I2C3-M1 переведён на 100 кГц (`clock-frequency = <100000>`) в активном
  `rv1106g-luckfox-pico-pro-max-sc230ai.dts`; устройства SHT21/INA3221/
  STM32 ACPI-EC на `/dev/i2c-3` работают на этой скорости.
- I2C3-M1 runs at 100 kHz (`clock-frequency = <100000>`) in the active
  `rv1106g-luckfox-pico-pro-max-sc230ai.dts`; the SHT21/INA3221/STM32
  ACPI-EC devices on `/dev/i2c-3` use that bus speed.
- WebUI/API: `/api/network` и секция **Network → Ethernet** переключают
  DHCP/static IPv4. Настройки `oko_net_mode`, `oko_ipaddr`, `oko_netmask`,
  `oko_gateway`, `oko_dns` и постоянный `oko_mac` хранятся в env-разделе
  U-Boot (`fw_printenv`/`fw_setenv`, `/etc/fw_env.config`), а не в
  `/userdata`, и переживают reboot и покомпонентную прошивку. Полный
  `update.img` перезаписывает `env.img`, но MAC восстанавливается из serial;
  статические настройки после полной прошивки задаются заново.
- WebUI/API: `/api/network` and **Network → Ethernet** switch DHCP/static
  IPv4. `oko_net_mode`, `oko_ipaddr`, `oko_netmask`, `oko_gateway`,
  `oko_dns` and the persistent `oko_mac` live in the U-Boot env partition
  (`fw_printenv`/`fw_setenv`, `/etc/fw_env.config`), not in `/userdata`, so
  they survive a reboot and component flashing. A full `update.img` rewrites
  `env.img`, but the MAC is re-derived from the serial number; static
  settings must be re-applied after a full flash.
- Buildroot: включены `BR2_PACKAGE_UBOOT_TOOLS=y` и
  `BR2_PACKAGE_UBOOT_TOOLS_FWPRINTENV=y` (`/usr/sbin/fw_printenv`,
  `fw_setenv`); добавлен `/etc/fw_env.config` для `/dev/mtd0`.
- Buildroot: enabled `BR2_PACKAGE_UBOOT_TOOLS=y` and
  `BR2_PACKAGE_UBOOT_TOOLS_FWPRINTENV=y` (`/usr/sbin/fw_printenv`,
  `fw_setenv`); added `/etc/fw_env.config` for `/dev/mtd0`.
- `divinus`: медиа-фичи этапа divinus-media-features — ePTZ (кроп VPSS),
  поворот 90/180/270 (VENC), SmartP/SVC/motion deblur/QP, второй поток
  `/live/1` (RTSP на путь), AIISP (`/api/aiisp`, модель
  `rkmodel_1080_1920.aiisp`), IVS (`/api/ivs`: MD/OD, снимок, клип, OSD),
  runtime IQ и HDR API; непроверенные комбинации блокируются `gating.c`
  с причиной. WebUI получил секции Geometry, Encoding, Night image, Image,
  Analytics, Sub stream, Streams и Storage. Результаты гейтов —
  `project/app/divinus/source/doc/README.md`.
- `divinus`: media feature set from the divinus-media-features stage — ePTZ
  (VPSS crop), 90/180/270 rotation (VENC), SmartP/SVC/motion deblur/QP, a
  second `/live/1` stream (per-path RTSP), AIISP (`/api/aiisp`, model
  `rkmodel_1080_1920.aiisp`), IVS (`/api/ivs`: MD/OD, snapshot, clip, OSD),
  runtime IQ and HDR APIs; unverified combinations are refused by `gating.c`
  with a reason. The WebUI gained Geometry, Encoding, Night image, Image,
  Analytics, Sub stream, Streams and Storage sections. Gate results are in
  `project/app/divinus/source/doc/README.md`.
- microSD: udev-скрипт `oko-sdcard` монтирует карту в `/mnt/sdcard` и пишет
  `/run/oko-sd.state`; `record.source`, `record.max_bytes` и
  `record.min_free_mb` выбирают поток и чистят старые сегменты. Запись в `/oem`
  и `/userdata` запрещена, без реально смонтированной карты — отказ.
  DTS `&sdmmc` переведён из `non-removable`/SDIO в режим съёмной карты.
- microSD: the `oko-sdcard` udev helper mounts the card at `/mnt/sdcard` and
  writes `/run/oko-sd.state`; `record.source`, `record.max_bytes` and
  `record.min_free_mb` select the feed and prune the oldest segments. Writing
  to `/oem` or `/userdata` is refused, and recording without a real mount is
  blocked. The `&sdmmc` DTS node no longer uses `non-removable`/SDIO flags.
- Сборка: `RK_AIISP_MODEL=rkmodel_1080_1920.aiisp` в BoardConfig копирует
  выбранную модель в `/oem/usr/lib`; из oem удаляются стресс/AVS-демо
  (`sample_*_stresstest`, `sample_*avs*`, `rk_mpi_avs_test`, `rkisp_demo`,
  `j2s4b_dev`), чтобы уменьшить размер oem.
- Build: `RK_AIISP_MODEL=rkmodel_1080_1920.aiisp` in the board config installs
  the selected model in `/oem/usr/lib`; stress/AVS demos
  (`sample_*_stresstest`, `sample_*avs*`, `rk_mpi_avs_test`, `rkisp_demo`,
  `j2s4b_dev`) are pruned from oem to reduce its size.
- `oko-osd`: в эмуляцию телеметрии добавлены LTE (RSRP, dBm + 1–5 полос) и
  WiFi (RSSI, dBm + 1–5 полос); значения меняются раз в 15 с вместе с
  GPS/температурой/батареей.
- `oko-osd`: added simulated LTE (RSRP in dBm + 1–5 bars) and WiFi (RSSI in dBm
  + 1–5 bars) to the telemetry; values change every 15 s together with
  GPS/temperature/battery.
- `project/app/divinus/`: вендоренный OpenIPC/divinus (MIT, upstream-коммит
  `bca1932ed8b348af68fb33a82a67218e8f45db7a`), Makefile-обёртка для сборки в общем
  цикле SDK (кросс-компилятор `arm-rockchip830-linux-uclibcgnueabihf`, hard-float)
  со стейджингом `/oem/usr/bin/divinus` и `/oem/usr/share/divinus/divinus.yaml`,
  фабричный конфиг под SC230AI (H.265 1920x1080/25, JPEG, web 80 и RTSP 554
  с обязательной аутентификацией, watchdog 0) и `README.md` с пином коммита
  и правилами обновления.
- `RkLunch.sh`: переключатель `/userdata/divinus.enable` — opt-in запуск `divinus`
  вместо `rkipc`; без маркера порядок инициализации и запуск `rkipc` не меняются.
- `project/app/divinus/`: vendored OpenIPC/divinus (MIT, upstream commit
  `bca1932ed8b348af68fb33a82a67218e8f45db7a`), a Makefile wrapper for the SDK build
  loop (cross-compiler `arm-rockchip830-linux-uclibcgnueabihf`, hard-float) that
  stages `/oem/usr/bin/divinus` and `/oem/usr/share/divinus/divinus.yaml`, a
  SC230AI factory config (H.265 1920x1080/25, JPEG, web 80 and RTSP 554 with
  authentication enabled, watchdog 0), and a `README.md` with the pinned commit
  and update rules.
- `RkLunch.sh`: `/userdata/divinus.enable` toggle for opt-in `divinus` startup
  instead of `rkipc`; without the marker the init order and `rkipc` startup are
  unchanged.
- `project/app/divinus/source/src/compat.c`: локальный патч ABI `mmap` для
  uClibc hard-float — собственный 64-битный `mmap` (из-за
  `_FILE_OFFSET_BITS=64` и `-rdynamic`) перехватывал вызовы `librockit.so`
  с 32-битным `off_t`, offset читался из неверного stack-слота, и кольцо VENC
  не отображалось (`mmap part 1 failed!`). Для `__ARM_PCS_VFP && __UCLIBC__`
  используется libc-вариант `mmap`/`mmap64`; `LD_PRELOAD` не нужен.
- `project/app/divinus/source/src/compat.c`: local ABI `mmap` patch for uClibc
  hard-float — the package's own 64-bit `mmap` (caused by
  `_FILE_OFFSET_BITS=64` plus `-rdynamic`) interposed the 32-bit `off_t`
  `mmap` calls of `librockit.so`, the offset was read from the wrong stack slot
  and the VENC ring could not be mapped (`mmap part 1 failed!`). On
  `__ARM_PCS_VFP && __UCLIBC__` the libc `mmap`/`mmap64` is used; no
  `LD_PRELOAD` is required.
- Тестовый маркер `S99oko-test` в оверлее `overlay-luckfox-buildroot-init`:
  скрипт пишет `/tmp/oko-test-ok` (`marker=OKO-TEST-2026.10-01`, uptime, версия
  ядра) и строку `OKO-TEST: …` в `/dev/kmsg`; доказывает, что загружена сборка
  после этой правки.
- `project/cfg/BoardConfig_IPC/overlay/overlay-luckfox-buildroot-init/`:
  `etc/init.d/S20oko-osd` (активация `divinus` по умолчанию при отсутствии
  `/userdata/rkipc.enable`, бутстрап OSD-конфига, создание `/tmp/osd.fifo` и
  запуск демона) и `usr/bin/oko-osd` (эмуляция GPS/температуры/АКБ в
  `/tmp/osd-dyn.txt` каждые 15 с; сообщения из FIFO в `/tmp/osd-msg.txt`
  с TTL 60 с). UART/ADC-оборудование не используется.
- `project/app/divinus/`: фабричный `divinus.yaml` включает OSD (`reg0` —
  дата/время `$t`, `reg1` — телеметрия, `reg2` — внешние сообщения) и локальный
  патч `source/src/region.c`: текст региона `@<path>` рендерится из первой строки
  файла, перечитывается раз в секунду, обновляет кадр при изменении и гасит
  регион при пустом/нечитаемом файле.
- `project/app/divinus/`: локальный патч `source/src/hal/plus/rk_hal.c` и
  `rk_rgn.h` для OSD на RV1106: преобразование растра `ARGB1555` в `BGRA5551`,
  выравнивание позиции и размера региона на 16 пикселей, поле `BITMAP_S`
  перед указателем данных (`pData` на смещении 16 в используемом
  `librockit.so`), выделение canvas региона и отказ от повторного прикрепления
  уже прикреплённого региона (иначе `RK_MPI_RGN_SetBitMap` возвращает ошибку,
  а повторный attach приводит к oops).
- `project/app/divinus/`: local `source/src/hal/plus/rk_hal.c` and `rk_rgn.h`
  patch for RV1106 OSD: converts `ARGB1555` text bitmaps to `BGRA5551`, aligns
  the region position and size to 16 pixels, adds the `BITMAP_S` field before
  the data pointer (`pData` at offset 16 in the shipped `librockit.so`),
  allocates the region canvas and skips re-attaching an already attached region
  (otherwise `RK_MPI_RGN_SetBitMap` fails and a repeated attach leads to an
  oops).
- Overlay `overlay-luckfox-buildroot-init`: `etc/init.d/S20oko-osd` (enables
  `divinus` by default unless `/userdata/rkipc.enable` exists, bootstraps the OSD
  config, creates `/tmp/osd.fifo`, starts the daemon) and `usr/bin/oko-osd`
  (simulated GPS/temperature/battery written to `/tmp/osd-dyn.txt` every 15 s;
  FIFO messages shown in `/tmp/osd-msg.txt` for 60 s). No UART/ADC hardware is
  used.
- `project/app/divinus/`: the factory `divinus.yaml` enables OSD (`reg0` —
  date/time `$t`, `reg1` — telemetry, `reg2` — external messages) and a local
  `source/src/region.c` patch renders `@<path>` region text from the first line
  of a file, re-reads it once per second, refreshes the frame on change and
  hides the region while the file is empty or unreadable.
- Netboot-монтирование `oem` и `userdata` с хоста: `mount_part()` в генерируемом
  `project/build.sh` `S20linkmount` при `root=/dev/nfs` монтирует
  `<host>:<nfsroot>/.nfsroot/<раздел>` по NFS (сервер и путь парсятся из
  `nfsroot=` в `/proc/cmdline`, в том числе без завершающих опций; при неудачном
  разборе печатается причина); при ошибке монтирования — откат на ubifs из SPI NAND.
- `RkLunch.sh`: ветка выбора `/oem/usr/share/rkipc-300w.ini` для сенсора
  `sc230ai` (раньше `post_chk` завершался с `Error: not found rkipc.ini !!!`).
- `RkLunch.sh`: в netboot (`nfsroot=` в cmdline) `network_init` не меняет MAC и
  не запускает `udhcpc` — статический адрес из `ip=` сохраняется, NFS-соединения
  корня и разделов не разрываются.
- `bench/`: утилиты тестового стенда в репозитории вместо `/tmp` —
  `serial_exec.py` (UART), `dut.py` (SSH), `netboot.py` (`catch`/`reboot`/`cmd`
  для U-Boot), `osd_check.py` (попарный диф OSD-регионов по RTSP), а также
  `setup.sh`, `run.sh`, `requirements.txt` и `README.md`; venv, логи и кадры —
  в `output/bench/` (gitignore).
- `bench/`: bench utilities in the repository instead of `/tmp` —
  `serial_exec.py` (UART), `dut.py` (SSH), `netboot.py` (`catch`/`reboot`/`cmd`
  for U-Boot), `osd_check.py` (pairwise OSD region diff over RTSP), plus
  `setup.sh`, `run.sh`, `requirements.txt` and `README.md`; the venv, logs and
  frames live in `output/bench/` (gitignore).
- `project/app/divinus/tests/bmp_align_test.c`: хостовый тест выравнивания
  растра OSD (660x46 → 672x48, stride 672), не входит в Makefile пакета;
  правило ре-вендоринга требует прогонять его после HAL-патча.
- `project/app/divinus/tests/bmp_align_test.c`: host test for the OSD raster
  alignment (660x46 → 672x48, stride 672), not part of the package Makefile;
  the re-vendoring rules require running it after the HAL patch.
- I2C3-M1 включён по умолчанию на 100 кГц:
  `sysdrv/source/kernel/arch/arm/boot/dts/rv1106g-luckfox-pico-pro-max.dts`,
  блок `&i2c3` — `status = "okay"`, `clock-frequency = <100000>`,
  `pinctrl-0 = <&i2c3m1_xfer>` (выводы `GPIO1_D2`/`GPIO1_D3`). Устройство
  `/dev/i2c-3` появляется при загрузке без вызова `luckfox-config`; группы
  I2C3-M0 из общего dtsi больше не мультиплексируются.
- I2C3-M1 enabled by default at 100 kHz:
  `sysdrv/source/kernel/arch/arm/boot/dts/rv1106g-luckfox-pico-pro-max.dts`,
  the `&i2c3` node — `status = "okay"`, `clock-frequency = <100000>`,
  `pinctrl-0 = <&i2c3m1_xfer>` (`GPIO1_D2`/`GPIO1_D3` pins). `/dev/i2c-3` is
  available at boot without running `luckfox-config`; the I2C3-M0 groups from
  the shared dtsi are no longer muxed.
- `project/app/oko-ec/`: утилита `/oem/usr/bin/oko-ec` для устройств на
  `/dev/i2c-3` — SHT21 `0x40`, INA226 `0x41`, STM32 ACPI-EC `0x42`. Команды
  `scan`, `info`, `status`, `battery`, `cells`, `temp`, `ina226`, `sht21`,
  `sensors`, OTA STM32 (`flash`/`enter-update`/`reset`) и демон
  `oko-ec daemon` (интервал 2 с, `/tmp/oko-ec.json`, `/tmp/oko-ec.osd`,
  `/tmp/oko-ec.pid`). `oko-osd` берёт реальные `T`/`BAT` из свежего
  `/tmp/oko-ec.osd` (≤ 30 с) и иначе работает на симуляции. Датчики читаются
  через `i2c-dev` без драйверов ядра; INA226 — без калибровочного регистра
  (шунт 100 мОм по умолчанию), SHT21 — no-hold с CRC8. Опция `--json`/`--quiet`,
  адреса/шунт/тайминги переопределяются. Вендорен хостовый инструмент upstream
  `spectrespc/stm32_acpi-ec` на коммите
  `41728ddaf6936d49a5fb65676ca5203dd631b4f6` (файла `LICENSE` в upstream нет);
  `S21oko-ec` запускает демон при наличии `/dev/i2c-3` и `/oem/usr/bin/oko-ec`,
  opt-out — `/userdata/oko-ec.disable`.
- `project/app/oko-ec/`: the `/oem/usr/bin/oko-ec` utility for devices on
  `/dev/i2c-3` — SHT21 `0x40`, INA226 `0x41`, STM32 ACPI-EC `0x42`. Commands:
  `scan`, `info`, `status`, `battery`, `cells`, `temp`, `ina226`, `sht21`,
  `sensors`, STM32 OTA (`flash`/`enter-update`/`reset`) and the
  `oko-ec daemon` loop (2 s interval, `/tmp/oko-ec.json`, `/tmp/oko-ec.osd`,
  `/tmp/oko-ec.pid`). `oko-osd` uses real `T`/`BAT` from a fresh
  `/tmp/oko-ec.osd` (≤ 30 s) and otherwise keeps simulating them. Sensors are
  read through `i2c-dev` without kernel drivers; INA226 uses no calibration
  register (100 mOhm shunt by default), SHT21 is no-hold with CRC8.
  `--json`/`--quiet` are supported and addresses/shunt/timings are overridable.
  The upstream host tool `spectrespc/stm32_acpi-ec` is vendored at commit
  `41728ddaf6936d49a5fb65676ca5203dd631b4f6` (no `LICENSE` file in upstream);
  `S21oko-ec` starts the daemon when `/dev/i2c-3` and `/oem/usr/bin/oko-ec`
  exist, with `/userdata/oko-ec.disable` as opt-out.
- `oko-ec`: поддержка INA3221 (die ID `0x3220`, три канала, 13-битные значения
  `D15:D3`, LSB 40 мкВ/8 мВ) с автоопределением чипа по `0xFF` и выбором канала
  `--ina-channel 1..3` (`OKO_EC_INA_CHANNEL`); INA226 (`0x2260`) продолжает
  поддерживаться. OSD (reg1) выводит четыре строки: SHT21 (температура и
  влажность), STM32 (напряжение/ток/температура/SOC/ячейки), INA3221
  (напряжение/ток/мощность) и LTE/WiFi; при отсутствии источника — `--`, без
  свежих данных — прежняя симуляция `T`/`BAT`. На стенде на `0x41` определён
  INA3221 (силовые входы пока не подключены, показания 0).
- `oko-ec`: INA3221 support (die ID `0x3220`, three channels, 13-bit values in
  `D15:D3`, 40 uV/8 mV LSB) with chip auto-detection via register `0xFF` and
  channel selection `--ina-channel 1..3` (`OKO_EC_INA_CHANNEL`); INA226
  (`0x2260`) remains supported. The OSD region reg1 now shows four lines:
  SHT21 (temperature and humidity), STM32 (voltage/current/temperature/SOC/
  cells), INA3221 (voltage/current/power) and LTE/WiFi; a missing source is
  shown as `--`, and stale data falls back to the previous `T`/`BAT`
  simulation. The testbench device at `0x41` is an INA3221 (power inputs are
  not connected yet, so the readings are zero).

- Разметка SPI NAND: `boot` увеличен с 4 до 6 МБ, `oem` — с 30 до 40 МБ,
  `rootfs` уменьшен с 210 до 198 МБ
  (`256K(env),256K@256K(idblock),512K(uboot),6M(boot),40M(oem),10M(userdata),198M(rootfs)`,
  всего 255 МБ). После этой сборки нужна **полная прошивка** `update.img`:
  покомпонентная выкатка со старой таблицей несовместима, `/userdata` и данные
  на NAND теряются.
- SPI NAND layout: `boot` grows from 4 to 6 MiB, `oem` from 30 to 40 MiB and
  `rootfs` shrinks from 210 to 198 MiB
  (`256K(env),256K@256K(idblock),512K(uboot),6M(boot),40M(oem),10M(userdata),198M(rootfs)`,
  255 MiB total). This build requires a **full `update.img` flash**: component
  flashing with the old table is incompatible, and `/userdata` plus the data on
  NAND are lost.
- WebUI/API storage: `/api/storage` получил `mount`, `unmount` и `format`
  (FAT32/MBR, метка `OKO`, один раздел), поля `device`, `fs`,
  `format_supported`, `format_busy` и одноразовый `format_token`; формат
  требует `confirm=FORMAT`, блокируется при записи и сериализован с udev через
  `/run/oko-sd.lock`. Секция Storage получила `Mount`, `Unmount`, `Format card`
  и скрытый токен.
- WebUI/API storage: `/api/storage` gained `mount`, `unmount` and `format`
  (FAT32/MBR, label `OKO`, one partition), plus `device`, `fs`,
  `format_supported`, `format_busy` and a one-shot `format_token`; formatting
  requires `confirm=FORMAT`, is refused while recording and is serialized with
  udev through `/run/oko-sd.lock`. The Storage section gained `Mount`,
  `Unmount`, `Format card` and a hidden token field.
- `VERIFIED_FUNCTIONS.md`: реестр проверенных на стенде функций (дата, плата,
  стенд `192.168.52.97`, доказательства, ограничения) и правила его ведения;
  ссылки добавлены в `README_RU.md` и `STRUCTURE.md`. После прогона 2026-11-04
  обновлены статусы разметки, формата, hotplug, AIISP, RTSP и записи.
- `VERIFIED_FUNCTIONS.md`: a registry of functions verified on the bench (date,
  board, `192.168.52.97` test stand, evidence and limitations) with maintenance
  rules; linked from `README_RU.md` and `STRUCTURE.md`. The 2026-11-04 run
  updated the statuses for the NAND layout, format, hotplug, AIISP, RTSP and
  recording.
- Buildroot: `sysdrv/tools/board/buildroot/luckfox_pico_defconfig` включает
  `BR2_PACKAGE_UTIL_LINUX_BINARIES=y` и `BR2_PACKAGE_UTIL_LINUX_WIPEFS=y`,
  поэтому в rootfs появляются `sfdisk`, `wipefs`, `fdisk` и другие утилиты
  (нужны для форматирования microSD).
- Buildroot: `sysdrv/tools/board/buildroot/luckfox_pico_defconfig` enables
  `BR2_PACKAGE_UTIL_LINUX_BINARIES=y` and `BR2_PACKAGE_UTIL_LINUX_WIPEFS=y`, so
  the rootfs contains `sfdisk`, `wipefs`, `fdisk` and the rest of the basic
  util-linux set (needed to format the microSD card).
- WebUI: страница `/` переработана в восемь разделов — **Live, Image, Streams,
  Events, Storage, Network, OSD, System** — с боковым меню, липкой статус-строкой
  (IP, аптайм, load, температура, microSD), переключателем **EN/RU** (по
  умолчанию `en`, выбор в `localStorage`), тостами ошибок (`reason`/`hint`) и
  индикацией неприменённых изменений; описание — в
  `project/app/divinus/source/doc/WEBUI_REORG.md`.
- WebUI: the `/` page is reworked into eight sections — **Live, Image, Streams,
  Events, Storage, Network, OSD, System** — with a sidebar, a sticky status bar
  (IP, uptime, load, SoC temperature, microSD), an **EN/RU** switch (default
  `en`, choice kept in `localStorage`), error toasts built from `reason`/`hint`,
  and unsaved-change indicators; see
  `project/app/divinus/source/doc/WEBUI_REORG.md`.
- API: `/api/cmd` получил `reboot`, `export` (in-memory YAML как вложение) и
  `log` (хвост `/var/log/messages`, до 256 КиБ); добавлены `POST
  /api/cmd/import` (multipart, замена `divinus.yaml` с ротацией `.bak`),
  `POST /api/web-auth` (смена пароля WebUI с проверкой текущего),
  `/api/recordings` (+ `/file`, `/delete`), `/api/telemetry`; `/api/status`
  теперь отдаёт `ip`, `sd_free_mb`, `sd_total_mb`.
- API: `/api/cmd` gained `reboot`, `export` (in-memory YAML as an attachment) and
  `log` (tail of `/var/log/messages`, up to 256 KiB); added `POST
  /api/cmd/import` (multipart, replaces `divinus.yaml` and rotates `.bak`),
  `POST /api/web-auth` (WebUI password change with current-password check),
  `/api/recordings` (+ `/file`, `/delete`) and `/api/telemetry`; `/api/status`
  now returns `ip`, `sd_free_mb` and `sd_total_mb`.
- NTP: `/api/time` принимает `ntp=on|off` и `ntp_server`; ключи
  `ntp_enable`/`ntp_server` пишутся в секцию `system` YAML. `ntp.c` на старте
  учитывает их через 10 с (после `S49ntp`): при выключенном NTP останавливает
  демон, при заданном сервере поднимает `ntpd -c /tmp/divinus-ntp.conf`, при
  пустом сервере не трогает вендорский `/etc/init.d/S49ntp` (или поднимает его,
  если тот не стартовал). Проверено тремя перезагрузками стенда.
- NTP: `/api/time` accepts `ntp=on|off` and `ntp_server`; the `ntp_enable` /
  `ntp_server` keys are stored in the YAML `system` section. Ten seconds after
  startup (after `S49ntp`) `ntp.c` applies them: a disabled client stops the
  daemon, a configured server starts `ntpd -c /tmp/divinus-ntp.conf`, and an
  empty server leaves the vendor `/etc/init.d/S49ntp` daemon alone (or starts it
  if it did not come up). Verified with three bench reboots.
- Стенд: `bench/api_check.py` (матрица GET/POST и негативных проверок API),
  `bench/ui_check.py` (headless Chromium: разделы, EN/RU, dirty-state,
  отсутствие горизонтальной прокрутки на 1440 и 390 px, скриншоты в
  `output/bench/ui/`) и `bench/deploy_divinus.py` (выкатка собранного бинарника
  в `/oem` по SSH для быстрой итерации).
- Bench: `bench/api_check.py` (GET/POST and negative API matrix),
  `bench/ui_check.py` (headless Chromium: sections, EN/RU, dirty state, no
  horizontal scroll at 1440 and 390 px, screenshots in `output/bench/ui/`) and
  `bench/deploy_divinus.py` (installs the built binary into `/oem` over SSH for
  fast iteration).
- Документация OKO: `project/app/divinus/source/doc/WEBUI_REORG.md` (разбор
  «до/после», карта разделов, матрица функций ipcweb, backlog), обновлены
  `endpoints.md`, `INIT_ORDER.md` (порядок NTP и откат конфига),
  `TESTBENCH.md` (§1, §7B, §8), `STRUCTURE.md`, `bench/README.md`,
  `FEATURES.md`, `VERIFIED_FUNCTIONS.md`; `doc/webui.png` заменён, прежний
  экран сохранён как `doc/webui-before.png`.
- OKO documentation: `project/app/divinus/source/doc/WEBUI_REORG.md` (before and
  after, section map, ipcweb feature matrix, backlog), updated
  `endpoints.md`, `INIT_ORDER.md` (NTP ordering and config rollback),
  `TESTBENCH.md` (§1, §7B, §8), `STRUCTURE.md`, `bench/README.md`,
  `FEATURES.md`, `VERIFIED_FUNCTIONS.md`; `doc/webui.png` is replaced and the
  previous screen is kept as `doc/webui-before.png`.
- `divinus`: NPU-аналитика YOLOv5n. RKNN подключается через `dlopen`
  `librknnmrt.so` (`hal/plus/rk_rknn.h`, копия `rknn_api.h` 1.6.0), конверсия
  NV12→RGB888 — на CPU (`yolo_convert_nv12_rgb`): librga на этом образе
  непригодна, легаси `c_RkRgaBlit` падает с SIGSEGV, а im2d
  `importbuffer_fd` не импортирует ни MMZ-, ни dma-heap-дескрипторы; эта же
  RGA-операция на куче и вешала плату. Новые модули:
  `analytics/yolo.c` (тред захвата, кольцо пре-ролла, событие, отправитель),
  `analytics/yolo_postprocess.c` (декод трёх ветвей и NMS),
  `analytics/models.c` (каталог `/userdata/models`, атомарная загрузка,
  активация с откатом). Секция `[yolo]` (`enable`, `source`, `model`,
  `input_width/height`, `fps`, `conf_threshold`, `nms_threshold`, `classes`,
  `pre_seconds`, `post_seconds`, `max_frames`) добавлена в разбор и печать
  конфига; API — `GET/POST /api/yolo`, `GET /api/models`,
  `POST /api/models`, `/api/models/activate`, `/api/models/delete`; WebUI —
  карточки «Object detection (NPU)» и «Models» в разделе Events.
  Событие отправляется N последовательными `PUT` с `X-Event-Id`,
  `X-Frame-Index`, `X-Frame-Total`, `X-Frame-Ts`, `X-Event-Cause`, `X-Model`;
  `http_post.host` теперь принимает `host:port`. Гейт `yolo` в `gating.c`
  проверяет runtime, модель и занятость канала VPSS 3; RGA не используется
  (препроцессинг на CPU). `yolo.source` по умолчанию `vpss3`, при IVS/MJPEG
  санитайзер переводит на `vi0`.
  Замер M0 на стенде: `librknnmrt.so` — **1.6.0** (не 1.5.2), драйвер 0.9.2
  совместим, `/proc/rknpu/load` = 77% на YOLOv5n 640×640, ~9 кадр/с,
  `MemAvailable` −8.7 МБ; включение VI selfpath (chn1) в этой сборке
  привело к зависанию платы, поэтому `source: vi1` заблокирован.
  Проверено на стенде (`source: vpss3`, YOLOv5n 640×640, 3 fps): детекция
  идёт, NPU 28–33%, событие отдаёт 6 JPEG 640×640 с заголовками
  `X-Event-*`, модель из `/tmp`; `api_check.py` — 78/78, RTSP/OSD — 20
  кадров без регрессии; загрузка/активация/удаление модели и откат при
  `rknn_init` проверены. CPU-конверсия добавляет ~28% одного ядра на 3 fps
  при 640×640 (для целевых 640×384 дешевле). Загрузка модели 3 МиБ на стенде
  не проходит по свободному месту `/userdata` (~1.5 МиБ).
- `divinus`: YOLOv5n NPU analytics. The RKNN runtime is loaded with
  `dlopen("librknnmrt.so")` (`hal/plus/rk_rknn.h`, a copy of the 1.6.0
  `rknn_api.h`), and NV12→RGB888 conversion runs on the CPU
  (`yolo_convert_nv12_rgb`): librga is unusable on this image, the legacy
  `c_RkRgaBlit` segfaults and im2d `importbuffer_fd` imports neither MMZ nor
  dma-heap descriptors; the same RGA operation on heap memory also hung the
  board. New modules: `analytics/yolo.c` (capture thread,
  pre-roll ring, event assembly, sender), `analytics/yolo_postprocess.c`
  (three-branch decode and NMS), `analytics/models.c` (`/userdata/models`,
  atomic upload, activation with rollback). The `[yolo]` section (`enable`,
  `source`, `model`, `input_width/height`, `fps`, `conf_threshold`,
  `nms_threshold`, `classes`, `pre_seconds`, `post_seconds`, `max_frames`) is
  parsed and dumped; the API adds `GET/POST /api/yolo`, `GET /api/models`,
  `POST /api/models`, `/api/models/activate`, `/api/models/delete`; the WebUI
  adds the "Object detection (NPU)" and "Models" cards to Events. An event is
  sent as N `PUT` requests carrying `X-Event-Id`, `X-Frame-Index`,
  `X-Frame-Total`, `X-Frame-Ts`, `X-Event-Cause`, `X-Model`;
  `http_post.host` now accepts `host:port`. The `yolo` gate in `gating.c`
  checks the runtime, the model file and VPSS channel 3; RGA is not used
  (pre-processing runs on the CPU). `yolo.source` defaults to `vpss3`, and the
  sanitiser switches to `vi0` when IVS or MJPEG owns the channel. M0 bench measurements: `librknnmrt.so` is **1.6.0** (not
  1.5.2), driver 0.9.2 is compatible, `/proc/rknpu/load` is 77% on YOLOv5n
  640×640 at ~9 fps with `MemAvailable` down 8.7 MB; enabling VI selfpath
  (chn1) hung this board, so `source: vi1` is blocked.
  Verified on the bench (`source: vpss3`, YOLOv5n 640×640, 3 fps): detection
  runs, NPU 28–33%, an event delivers 6 JPEGs at 640×640 with the
  `X-Event-*` headers from a model in `/tmp`; `api_check.py` is 78/78 and
  RTSP/OSD captured 20 frames without regressions; model upload, activation,
  deletion and the `rknn_init` rollback were exercised. The CPU conversion
  costs about 28% of one core at 3 fps for 640×640 (cheaper at the target
  640×384). A 3 MiB model upload does not fit the ~1.5 MiB free in
  `/userdata` on this bench.


### Изменено

- `FEATURES.md` разделён на три файла: `FEATURES.md` — паспорт платформы,
  разделение по статусу проверки, общие примечания и команды; новый
  `FEATURES_VERIFIED.md` — только подтверждённые функции и возможности со
  статусом «работает»; новый `FEATURES_UNVERIFIED.md` — ограниченные и
  заблокированные возможности, непроверенные комбинации потоков, датчиков и
  microSD. Ссылки обновлены в `AGENTS.md`, `STRUCTURE.md`, `README_RU.md`,
  `KNOWN_ISSUES.md` и `TO_DO.md`.
- `FEATURES.md` is split into three files: `FEATURES.md` now holds the platform
  passport, the split by verification status, shared notes and commands; the new
  `FEATURES_VERIFIED.md` lists only confirmed features with the **works** status;
  the new `FEATURES_UNVERIFIED.md` lists limited and blocked capabilities and
  unverified stream, sensor and microSD combinations. Links were updated in
  `AGENTS.md`, `STRUCTURE.md`, `README_RU.md`, `KNOWN_ISSUES.md` and `TO_DO.md`.
- `FEATURES.md` переработан: вместо разнородных списков и прозы — короткий
  паспорт платформы и одна сводная таблица функционала с колонками «Группа»,
  «Функционал», «Описание», «Управление (API, секция YAML)», «Что добавлено
  (OKO vs вендорский V1.4)» и «Проверено и работает». English-раздел сжат до
  зеркальной таблицы `Feature | What was added | Verified & works`; ограничения
  привязаны к строкам таблицы или вынесены в «Примечания». Строка `FEATURES.md`
  в карте документации `AGENTS.md` приведена к новой структуре.
- `FEATURES.md` reworked: the mixed lists and prose are replaced by a short
  platform passport and one consolidated feature table with `Group`, `Feature`,
  `Description`, `Control (API, YAML section)`, `What was added (OKO vs vendor
  V1.4)` and `Verified & works` columns. The English section is reduced to a
  mirrored `Feature | What was added | Verified & works` table; limitations are
  attached to table rows or collected under "Notes". The `FEATURES.md` row in
  the `AGENTS.md` documentation map now matches the new structure.
- `project/app/divinus/source/src/record.c`: запись разрешена только ниже
  реально смонтированного `/mnt/sdcard`; `/oem`, `/userdata`, `/tmp` и `/`
  отвергаются. Добавлен `storageBusy`: во время формата/размонтирования новый
  сегмент не открывается. Раз в секунду и перед ротацией проверяется
  монтирование: при извлечении карты файл закрывается, запись останавливается,
  NAND не используется. `start=1` при отказе возвращает `blocked` с причиной.
- `project/app/divinus/source/src/record.c`: recording is allowed only below a
  really mounted `/mnt/sdcard`; `/oem`, `/userdata`, `/tmp` and `/` are refused.
  `storageBusy` prevents a new segment from opening during format/unmount. The
  mount is checked once per second and before rotation: if the card disappears,
  the file is closed, recording stops and SPI NAND is never used. A failed
  `start=1` now returns `blocked` with a reason.
- SC230AI DTS: новый `rv1106g-luckfox-pico-pro-max-sc230ai.dts` (копия Pro Max
  DTS) выключает неиспользуемые `&sc3336` и `&mis5001`; активный BoardConfig
  использует этот файл. Общий вендорский dtsi и DTS других плат не изменены.
- SC230AI DTS: the new `rv1106g-luckfox-pico-pro-max-sc230ai.dts` (a copy of the
  Pro Max DTS) disables the unused `&sc3336` and `&mis5001`; the active board
  config points at this file. The shared vendor dtsi and other board DTS files
  are unchanged.
- `rv1106-sc230ai.config`: явно выключены `CONFIG_VIDEO_SC3336`,
  `CONFIG_VIDEO_IMX415`, `CONFIG_VIDEO_MIS5001`, `CONFIG_VIDEO_MIA1321`;
  собирается только `CONFIG_VIDEO_SC230AI=m`.
- `rv1106-sc230ai.config`: `CONFIG_VIDEO_SC3336`, `CONFIG_VIDEO_IMX415`,
  `CONFIG_VIDEO_MIS5001` and `CONFIG_VIDEO_MIA1321` are explicitly disabled;
  only `CONFIG_VIDEO_SC230AI=m` is built.
- `project/app/divinus/source/src/region.c`: буфер локального `@file`-провайдера
  текста OSD увеличен с 80 до 256 байт (и `region_file_prev`), чтобы строка
  телеметрии могла содержать SHT21/STM32/INA3221/LTE/WiFi; поведение для
  коротких строк не меняется.
- `project/app/divinus/source/src/region.c`: the local OSD `@file` text provider
  buffer is increased from 80 to 256 bytes (including `region_file_prev`), so the
  telemetry line can carry SHT21/STM32/INA3221/LTE/WiFi; short strings behave as
  before.
- `sc230ai`: режим 1920×1080 переведён с 25 на **30 fps** (`VTS = 0x0465`,
  HTS остаётся 2200, MIPI link 371.25 Мбит/с не менялся). Проверено на DUT:
  `v4l2-ctl` даёт стабильные **30.01–30.02 fps** на 300 кадрах, ошибок
  CSI/MIPI нет. Модуль собран отдельно и загружен через
  `/oem/usr/ko/sc230ai.ko` без полной перепрошивки.
- `sc230ai`: в проект перенесена запись internal sync master/slave регистров
  при старте и остановке потока (из upstream 6.1 / X5): `0x300a`, `0x3032`,
  `0x3222`, `0x3224`, `0x3614`. При `NO_SYNC` поведение не меняется; проверены
  загрузка модуля, 30 fps и OSD.
- `sc230ai`: ported internal sync master/slave register writes at stream
  start/stop from upstream 6.1 / X5: `0x300a`, `0x3032`, `0x3222`, `0x3224`,
  `0x3614`. With `NO_SYNC` behavior is unchanged; module load, 30 fps and OSD
  were verified.
- `sc230ai`: добавлен экспериментальный **HDR_X2** (SHDR/DCG, 2 lane,
  742.5 Мбит/с, 1920×1080@30): таблица из SigmaStar, link frequency
  371.25 МГц, VTS 2250, запись long/short exposure и gain в `set_hdrae()`.
  По умолчанию не включается (`rockchip,camera-hdr-mode = <1>`); HDR IQ для
  SC230AI на RV1106 не проверялся. Linear 30 fps работает без регрессий.
- `sc230ai`: added experimental **HDR_X2** (SHDR/DCG, 2 lanes,
  742.5 Mbps, 1920×1080@30): SigmaStar register table, 371.25 MHz link
  frequency, VTS 2250, and long/short exposure + gain writes in
  `set_hdrae()`. Disabled by default (`rockchip,camera-hdr-mode = <1>`);
  SC230AI HDR IQ on RV1106 has not been validated. Linear 30 fps is unaffected.
- `divinus`: основной поток (`mp4`) и `mjpeg` переведены на **30 fps**
  (`fps: 30`), битрейт `mp4` остаётся 4096 кбит/с. На DUT измерено
  **30.08 fps** за 20 с и ≈4.73 Мбит/с; OSD работает.
- `sc230ai`: the 1920×1080 mode is switched from 25 to **30 fps**
  (`VTS = 0x0465`, HTS stays 2200, MIPI link 371.25 Mbps unchanged). Verified
  on the DUT: `v4l2-ctl` reports stable **30.01–30.02 fps** over 300 frames with
  no CSI/MIPI errors. The module was built separately and loaded through
  `/oem/usr/ko/sc230ai.ko` without a full reflash.
- `divinus`: the main stream (`mp4`) and `mjpeg` are switched to **30 fps**
  (`fps: 30`); the `mp4` bitrate remains 4096 kbps. Measured on the DUT:
  **30.08 fps** over 20 s and ≈4.73 Mbps; OSD works.
- OSD: добавлен чёрный контур текста — `regN_outl: 0x8000`,
  `regN_thick: 1.0` для всех регионов. Конфиг применён на DUT и
  сохранён в `/userdata/divinus.yaml`; контур проверен по кадрам RTSP.
- OSD: added a black text outline — `regN_outl: 0x8000`, `regN_thick: 1.0`
  for all regions. Applied on the DUT and persisted in
  `/userdata/divinus.yaml`; verified from RTSP frames.
- OSD: добавлен `reg4` в левом нижнем углу — реальные загрузка CPU,
  процент свободной памяти (`MemAvailable/MemTotal`) и температура
  `soc-thermal`; файл `/tmp/osd-sys.txt`, обновление раз в 5 с.
- OSD: added `reg4` in the bottom-left corner — real CPU load, free memory
  percentage (`MemAvailable/MemTotal`) and `soc-thermal` temperature; source
  `/tmp/osd-sys.txt`, updated every 5 s.
- `divinus`: битрейт основного RTSP-потока (`mp4.bitrate`, H.265 CBR)
  увеличен вдвое — `2048` → `4096` кбит/с; измеренный поток вырос
  с ≈2.28 до ≈4.5–4.9 Мбит/с. На DUT значение применено и сохранено в
  `/userdata/divinus.yaml`.
- `divinus`: main RTSP stream bitrate (`mp4.bitrate`, H.265 CBR) doubled —
  `2048` → `4096` kbps; the measured stream grew from ≈2.28 to ≈4.5–4.9 Mbps.
  Applied on the DUT and persisted in `/userdata/divinus.yaml`.
- OSD: размер шрифта всех регионов — `24.0` px; время перенесено в правый
  нижний угол, GPS — в правый верхний, телеметрия `T/BAT/LTE/WiFi` разбита на
  четыре строки в левом верхнем углу, сообщения — под ней в `reg3`. Цвет —
  ярко-зелёный `regN_color: 0x07e0` (RGB565). `S20oko-osd` мигрирует старый
  `/userdata/divinus.yaml` (без `reg0_size`/`reg0_color`/`reg3_text`) с
  сохранением первого бэкапа `/userdata/divinus.yaml.bak`.
- OSD: font size for all regions is `24.0` px; time moved to bottom-right, GPS
  to top-right, `T/BAT/LTE/WiFi` split into four lines in the top-left, and
  messages placed below them in `reg3`. Color is bright green
  `regN_color: 0x07e0` (RGB565). `S20oko-osd` migrates an old
  `/userdata/divinus.yaml` (without `reg0_size`/`reg0_color`/`reg3_text`) and
  keeps the first backup `/userdata/divinus.yaml.bak`.
- `divinus` RGN (`rk_hal.c`): в конверсии BGRA5551 выставляются оба бита
  альфы (бит 0 и бит 15); сборки librockit для RV1106 различаются, без этого
  не белый цвет OSD (зелёный/красный) рендерится полностью прозрачным.
- `divinus` RGN (`rk_hal.c`): the BGRA5551 conversion now sets both alpha bits
  (bit 0 and bit 15); RV1106 librockit builds differ, and otherwise a non-white
  OSD color (green/red) is rendered fully transparent.
- Активная конфигурация переключена на
  `project/cfg/BoardConfig_IPC/BoardConfig-SPI_NAND-Buildroot-RV1106_Luckfox_Pico_Pro_Max-SC230AI.mk`:
  SPI NAND, Buildroot, SC230AI, фрагмент ядра только `rv1106-sc230ai.config`.
  Сборка, прошивка и проверки выполняются в автономном режиме, без TFTP/NFS.
- `Dockerfile`, `docker-entrypoint.sh`, `docker-compose.yml`, `start.sh`: убраны
  TFTP-сервер, пакеты `tftpd-hpa`/`atftp` и соответствующие сообщения; контейнер
  больше не запускает сетевых сервисов.
- `AGENTS.md`, `README_RU.md`, `TESTBENCH.md`, `INIT_ORDER.md`, `STRUCTURE.md`,
  `bench/README.md`, `.kilo/command/bench.md`, `.kilo/command/docs-check.md`,
  `.kilo/skills/oko-docs/SKILL.md`: netboot удалён из описания стенда, прошивки,
  диагностики и утилит; основной режим — SPI NAND.
- После перехода на автономную загрузку DUT получает адрес по DHCP; фактический
  адрес на `2026-11-01` — `192.168.52.65`. `TESTBENCH.md` §5 и примеры в
  `README_RU.md` / `bench/README.md` / `/bench` обновлены; `192.168.52.150`
  остаётся резервным адресом для DHCP-резервирования.
- Active configuration switched to
  `project/cfg/BoardConfig_IPC/BoardConfig-SPI_NAND-Buildroot-RV1106_Luckfox_Pico_Pro_Max-SC230AI.mk`:
  SPI NAND, Buildroot, SC230AI, kernel fragment `rv1106-sc230ai.config` only.
  Build, flashing and checks run standalone without TFTP/NFS.
- After the switch to standalone boot the DUT obtains its address via DHCP; the
  actual address on `2026-11-01` is `192.168.52.65`. `TESTBENCH.md` §5 and the
  examples in `README_RU.md` / `bench/README.md` / `/bench` were updated;
  `192.168.52.150` remains a reserved address for a DHCP reservation.
- `Dockerfile`, `docker-entrypoint.sh`, `docker-compose.yml`, `start.sh`: removed
  the TFTP server, the `tftpd-hpa`/`atftp` packages and related messages; the
  container no longer starts network services.
- `AGENTS.md`, `README_RU.md`, `TESTBENCH.md`, `INIT_ORDER.md`, `STRUCTURE.md`,
  `bench/README.md`, `.kilo/command/bench.md`, `.kilo/command/docs-check.md`,
  `.kilo/skills/oko-docs/SKILL.md`: netboot removed from the bench, flashing,
  diagnostics and tooling docs; SPI NAND is the primary mode.
- Поведение запуска стримера: `divinus` стартует по умолчанию — маркер
  `/userdata/divinus.enable` создаёт `S20oko-osd`; штатный `rkipc` запускается
  только при opt-out `/userdata/rkipc.enable`. Логика `RkLunch.sh` не менялась.
- `S20oko-osd` перезаписывает `/userdata/divinus.yaml` фабричным, если в нём нет
  `reg0_text`/`reg0_size`/`reg0_color` (старый или ручной конфиг); первый прежний
  файл сохраняется как `divinus.yaml.bak`, ручные правки такого файла не
  переживают перезагрузку.
- `INIT_ORDER.md`, `TESTBENCH.md`, `STRUCTURE.md`, `README_RU.md`: divinus по
  умолчанию, строка `S20oko-osd` в порядке init, OSD-чек-лист и файлы оверлея.
- Streamer startup: `divinus` now starts by default — `S20oko-osd` creates
  `/userdata/divinus.enable`; the stock `rkipc` runs only when the
  `/userdata/rkipc.enable` opt-out marker exists. `RkLunch.sh` itself is
  unchanged.
- `S20oko-osd` replaces `/userdata/divinus.yaml` with the factory file when it
  lacks `reg0_text` (old or hand-edited config); such a file does not survive a
  reboot.
- `INIT_ORDER.md`, `TESTBENCH.md`, `STRUCTURE.md`, `README_RU.md`: default
  divinus startup, the `S20oko-osd` init step, the OSD checklist and the new
  overlay files.
- `STRUCTURE.md`: добавлен пакет `project/app/divinus/` (вендорские исходники,
  фабричный конфиг, Makefile) и уточнён состав `project/app/`.
- `INIT_ORDER.md`: ветка `divinus.enable` в `post_chk` и зависимости `divinus`
  (общий rockit/ISP-конвейер с `rkipc`, `dlopen` вендорных библиотек,
  патч ABI `mmap` в `compat.c`).
- `TESTBENCH.md`: чек-лист divinus (`/api/status`, `/image.jpg`, RTSP-корень),
  диагностика сбоев и особенности портов/конфига.
- `STRUCTURE.md`: added the `project/app/divinus/` package (vendored sources,
  factory config, Makefile) and clarified the `project/app/` contents.
- `INIT_ORDER.md`: documented the `divinus.enable` branch in `post_chk` and the
  `divinus` dependencies (shared rockit/ISP pipeline with `rkipc`, `dlopen` of
  vendor libraries, the `compat.c` `mmap` ABI patch).
- `TESTBENCH.md`: added the divinus checklist (`/api/status`, `/image.jpg`, RTSP
  root) plus failure diagnostics and port/config notes.
- `INIT_ORDER.md`: источник `oem`/`userdata` при netboot, ветка `S20linkmount`,
  строка `S99oko-test` в порядке init-скриптов, поведение сети при netboot.
- `TESTBENCH.md`: `.nfsroot` внутри экспорта `/srv/nfs/rootfs`, деплой rsync с
  бэкапом, проверки маркера и NFS-монтирований, статический IP в netboot.
- `INIT_ORDER.md`: зафиксировано условие, что сборка прошивки OKO выполняется в
  Docker-контейнере `luckfox_oko_build_env` (`docker compose build` / `up -d`,
  запуск через `start.sh`); в разделе источников сборки добавлена строка про
  Docker-окружение. Сборка без Docker остаётся вендорским fallback в `README_RU.md`.
- `INIT_ORDER.md`: recorded that the OKO firmware build runs in the
  `luckfox_oko_build_env` Docker container (`docker compose build` / `up -d`, started via
  `start.sh`), and added a Docker environment row to the build sources table. Building
  without Docker remains a vendor fallback in `README_RU.md`.
- Уточнены правила делегирования субагентам: промпт самодостаточный, с выжимкой фактов,
  без путей и номеров разделов документации и без имён skills; зафиксирована область
  применения `AGENTS.md` (исполняет только агент `code`, правила дополняют базовые
  инструкции) и привязка команд `/bench`, `/docs-check` к `code`.
- Refined sub-agent delegation rules: the prompt is self-contained with distilled facts,
  without documentation paths, section numbers, or skill names; recorded that `AGENTS.md`
  applies only to the `code` agent and supplements the base instructions, and that
  `/bench` and `/docs-check` are bound to `code`.
- `STRUCTURE.md`: описание `.kilo/` приведено к фактическому составу (команды, skill,
  планы), в список корневых файлов добавлены документы OKO и `AGENTS.md`;
  `.kilo/.gitignore` больше не игнорирует сам себя.
- `STRUCTURE.md`: the `.kilo/` description now matches the actual layout (commands, skill,
  plans), OKO documents and `AGENTS.md` were added to the root files list, and
  `.kilo/.gitignore` no longer ignores itself.
- `TESTBENCH.md`: новый режим «C. Покомпонентная прошивка изменённых разделов» —
  маппинг исходников на разделы SPI NAND (`oem`, `rootfs`, `userdata`, `boot`),
  команды `./rkflash.sh oem|rootfs|userdata|rd` из Maskrom, правило «полный
  `update.img` только при первичной прошивке или смене таблицы разделов, loader,
  U-Boot env», сверка хешей образов с `.flashed.sha256` и альтернатива сырыми
  `mtd erase/write` из `sd_update.txt`.
- `README_RU.md`: в быстром старте прошивка полным образом — `./rkflash.sh update`
  (без аргумента скрипт ничего не прошивает), добавлена ссылка на покомпонентный
  режим; команда `/bench` дополнена режимами прошивки и `divinus` в чек-листе.
- `TESTBENCH.md`: new "C. Per-partition flashing of changed sections" mode — a
  source-to-SPI-NAND-partition map (`oem`, `rootfs`, `userdata`, `boot`), the
  `./rkflash.sh oem|rootfs|userdata|rd` commands from Maskrom, the rule that a full
  `update.img` is only for the first flash or a partition table, loader, or U-Boot
  env change, image hash comparison against `.flashed.sha256`, and the raw
  `mtd erase/write` alternative from `sd_update.txt`.
- `README_RU.md`: the quick start now flashes the full image with
  `./rkflash.sh update` (the script flashes nothing without an argument) and links
  to the per-partition mode; the `/bench` command now lists the flashing modes and
  `divinus` in its checklist.
- `TESTBENCH.md`: зафиксировано покрытие шрифта `simsun_en` — ASCII `0x21–0x7e`
  и несколько CJK-иероглифов, кириллицы нет; текст OSD пишется латиницей.
- `TESTBENCH.md`: documented the `simsun_en` font coverage — ASCII `0x21–0x7e`
  plus a few CJK characters, no Cyrillic; OSD text must be Latin.
- Логи netboot и OSD-проверок перенесены из `/tmp/kilo` в `output/bench/`
  (`netboot-<ts>.log`, `osd-<ts>-*.png`); `netboot-catch.py` читает
  `OKO_NETBOOT_LOG` и создаёт каталог сам. Обновлены `TESTBENCH.md` (§4, §7B,
  §8, §10), `README_RU.md`, `STRUCTURE.md` (папка `bench/`, `output/bench/`),
  команда `/bench` и правило в `AGENTS.md` §6.
- Netboot and OSD-check artifacts moved from `/tmp/kilo` to `output/bench/`
  (`netboot-<ts>.log`, `osd-<ts>-*.png`); `netboot-catch.py` honours
  `OKO_NETBOOT_LOG` and creates the directory itself. Updated `TESTBENCH.md`
  (§4, §7B, §8, §10), `README_RU.md`, `STRUCTURE.md` (`bench/`, `output/bench/`),
  the `/bench` command and the rule in `AGENTS.md` §6.
- WebUI: запись настроек переведена на `POST application/x-www-form-urlencoded`
  (сервер подставляет тело в `req->query`, обработчики не менялись); `GET`
  оставлен для совместимости и помечен устаревшим в `endpoints.md`. Кнопка
  `Apply` активна только при отличиях от последнего прочитанного состояния,
  ответ сервера разбирается вместо безусловного «success».
- WebUI: writes now use `POST application/x-www-form-urlencoded` (the server
  maps the body to `req->query`, so the handlers are unchanged); `GET` is kept
  for compatibility and marked deprecated in `endpoints.md`. `Apply` is enabled
  only when a field differs from the last read state, and the server response is
  parsed instead of assuming success.
- `/api/osd/{id}`: `color` и `outl` отдаются как `#RRGGBB` с ведущими нулями,
  `text`/`img`/`font` экранируются в JSON; пустые `text`/`img` очищают значение;
  PNG-загрузка прописывает `img=/tmp/osd{id}.png`, поэтому картинка реально
  отображается.
- `/api/osd/{id}`: `color` and `outl` are returned as zero-padded `#RRGGBB`, and
  `text`/`img`/`font` are JSON-escaped; empty `text`/`img` clear the value; a PNG
  upload sets `img=/tmp/osd{id}.png`, so the image is actually displayed.
- `divinus.yaml` (фабричный и рабочий): в `system` добавлены `ntp_enable` и
  `ntp_server`; существующие файлы без этих ключей читаются со значениями по
  умолчанию.
- `divinus.yaml` (factory and working copies): `system` gains `ntp_enable` and
  `ntp_server`; existing files without the keys parse with defaults.
- `unescape_uri()` вызывается после разделения `key=value`, поэтому `%3D` в
  значении параметра больше не влияет на разбор, а `NULL` для параметра без
  значения безопасен.
- `unescape_uri()` now runs after the `key=value` split, so an encoded `%3D` in
  a value no longer affects parsing, and `NULL` for a parameter without a value
  is safe.
- `divinus`: OSD на RV1106 обновляется через canvas
  (`RK_MPI_RGN_GetCanvasInfo`/`RK_MPI_RGN_UpdateCanvas`): растр пишется прямо
  в память региона, без `calloc` и копии на каждый кадр. Если символы
  отсутствуют, `GetCanvasInfo` вернул ошибку или размер/stride canvas не
  совпал, остаётся прежний путь `RK_MPI_RGN_SetBitMap` с однократным
  предупреждением на handle. Позиция и `fgAlpha` меняются через
  `RK_MPI_RGN_SetDisplayAttr` вместо detach/attach, поэтому `opal` из
  `/api/osd/<id>` применяется и при неизменной позиции. Упаковка
  `ARGB1555 → BGRA5551` вынесена в `hal/plus/rk_rgn_pack.c`, добавлен хостовый
  тест `tests/rk_rgn_pack_test.c`, `tests/bmp_align_test.c` переведён на общий
  helper. `u32CanvasNum` остаётся равным 1.
- `divinus`: the RV1106 OSD is updated through the canvas interface
  (`RK_MPI_RGN_GetCanvasInfo`/`RK_MPI_RGN_UpdateCanvas`) — the raster is
  written directly into the region memory, without a `calloc` and a copy per
  frame. If the symbols are missing, `GetCanvasInfo` fails or the canvas
  size/stride does not match, the previous `RK_MPI_RGN_SetBitMap` path is used
  with a single warning per handle. Position and `fgAlpha` are changed with
  `RK_MPI_RGN_SetDisplayAttr` instead of detach/attach, so `opal` from
  `/api/osd/<id>` is applied even when the position is unchanged. The
  `ARGB1555 → BGRA5551` packing moved to `hal/plus/rk_rgn_pack.c`, a host test
  `tests/rk_rgn_pack_test.c` was added, and `tests/bmp_align_test.c` now uses
  the shared helper. `u32CanvasNum` stays at 1.

- Разметка SPI NAND: `userdata` расширен с 10 до 40 МБ за счёт `rootfs`
  (198→168 МБ), сумма `userdata`+`rootfs` сохранена (208 МБ). После прошивки
  полным `update.img` том `ubi5_0` — 29.1 МиБ (свободно ~28 МиБ), загрузка
  модели 3 МиБ через `/api/models` проверена; `rootfs` занимает ~54 МиБ из
  168 МБ. Обновлены `BoardConfig`, `TESTBENCH.md` (LBA/размеры),
  `INIT_ORDER.md` и `FEATURES.md`. Если `reboot loader` не переводит плату в
  loader-режим, U-Boot на UART останавливается `<Ctrl+C>` и команда
  `download` уводит её в `Maskrom`.
- SPI NAND layout: `userdata` grew from 10 to 40 MB by shrinking `rootfs`
  (198→168 MB); the combined size stays 208 MB. After flashing the full
  `update.img` the `ubi5_0` volume is 29.1 MiB (~28 MiB free), and a 3 MiB
  model upload through `/api/models` was verified; `rootfs` uses ~54 MiB of
  its 168 MB. `BoardConfig`, `TESTBENCH.md` (LBA/sizes), `INIT_ORDER.md` and
  `FEATURES.md` were updated. When `reboot loader` does not enter loader mode,
  stopping U-Boot on UART with `<Ctrl+C>` and issuing `download` switches the
  board to `Maskrom`.

- `bench/ui_check.py`: добавлена браузерная проверка раздела Events — список
  источников YOLO не содержит `vi1`, модель загружается через карточку
  Models, ошибка `rknn_init` показывается в тосте, удаление убирает строку.
  Прогон выполнен в headless Chromium (без root `libasound.so.2` распакован
  из `libasound2t64`), итог — `UI check passed`, скриншоты в
  `output/bench/ui/`; способ запуска описан в `bench/README.md`.
- `bench/ui_check.py`: added a browser check of the Events section — the YOLO
  source list has no `vi1`, a model is uploaded from the Models card, the
  `rknn_init` error appears in a toast, and deleting the row removes it. The
  run used headless Chromium (without root, `libasound.so.2` was unpacked from
  `libasound2t64`) and ended with `UI check passed`; screenshots are in
  `output/bench/ui/`, and the command is documented in `bench/README.md`.

- Зафиксирована проверка AIISP + YOLO на `vpss3` (демо-модель 640×640,
  3 fps): AIISP on/off не роняет детекцию, main 30.2–32.8 fps, NPU 28%,
  `librkpostisp.so` загружен, пропусков в `/live/0` не видно; результаты — в
  `VERIFIED_FUNCTIONS.md`. Ночная точность и CMA не измерялись (нет
  цели/разметки), целевая модель 640×384 ещё не собрана.
- Recorded the AIISP + YOLO check on `vpss3` (640×640 demo model, 3 fps):
  toggling AIISP does not break detection, main runs at 30.2–32.8 fps with 28%
  NPU, `librkpostisp.so` is loaded and `/live/0` shows no gaps; results are in
  `VERIFIED_FUNCTIONS.md`. Night accuracy and CMA were not measured (no
  target/labels), and the target 640×384 model is not built yet.

- `TO_DO.md` §11 сокращён до незакрытых задач: удалены реализованные фазы
  NPU-аналитики, исходное состояние, принятые решения и статус; остались модель
  640×384 с точностью, ночная точность/CMA, лицензия YOLOv5 и вопрос
  `.aiisp`-моделей через WebUI.
- `TO_DO.md` §11 is reduced to open work: the implemented NPU phases, initial
  state, decisions and status were removed; it now lists only the 640×384 model
  plus accuracy, night accuracy/CMA, the YOLOv5 licence and the `.aiisp` model
  question for the WebUI.

### Удалено

- Из oem активной платы удалены `sample_*` и `simple_*` (включая прежние
  стресс/AVS-демо); диагностические `rk_*_test`/`rk_mpi_*_test`, `rkipc`,
  `rkaiq_*`, `divinus` и `oko-*` сохранены.
- `sample_*` and `simple_*` (including the old stress/AVS demos) are removed
  from the active board oem; diagnostic `rk_*_test`/`rk_mpi_*_test`, `rkipc`,
  `rkaiq_*`, `divinus` and `oko-*` are kept.
- Из oem удалены сенсорные модули и IQ других плат (`sc3336`, `mis5001`,
  `imx415`, `mia1321` и другие сенсорные `.ko`); остаётся только `sc230ai.ko`
  и `sc230ai_CMK-OT2115-PC1_30IRC-F16.json`. Вендорские исходники и DTS других
  плат не тронуты.
- Sensor modules and IQ files of the other boards (`sc3336`, `mis5001`,
  `imx415`, `mia1321` and other sensor `.ko` files) are removed from oem; only
  `sc230ai.ko` and `sc230ai_CMK-OT2115-PC1_30IRC-F16.json` remain. Vendor
  sources and DTS files of the other boards are untouched.
- Netboot-режим: файлы `BoardConfig-SPI_NAND-Buildroot-RV1106_Luckfox_Pico_Pro_Max-NETBOOT.mk`,
  `BoardConfig-SPI_NAND-Buildroot-RV1106_Luckfox_Pico_Pro_Max-NETBOOT-SC230AI.mk`,
  фрагмент ядра `sysdrv/source/kernel/arch/arm/configs/rv1106-netboot.config`,
  утилиты `bench/netboot.py` и `netboot-catch.py`.
- NFS-root в `S20linkmount` (`project/build.sh`): ветка монтирования
  `oem`/`userdata` с хоста и сообщения `netboot: …`; разделы всегда берутся из
  SPI NAND (ubifs).
- Netboot-ветка в `RkLunch.sh`: статическая сеть из `ip=` больше не
  поддерживается, `network_init` всегда настраивает MAC и запускает DHCP.
- Netboot mode: the
  `BoardConfig-SPI_NAND-Buildroot-RV1106_Luckfox_Pico_Pro_Max-NETBOOT.mk` and
  `BoardConfig-SPI_NAND-Buildroot-RV1106_Luckfox_Pico_Pro_Max-NETBOOT-SC230AI.mk`
  files, the `sysdrv/source/kernel/arch/arm/configs/rv1106-netboot.config`
  kernel fragment, and the `bench/netboot.py` / `netboot-catch.py` tools.
- NFS root in `S20linkmount` (`project/build.sh`): the host-side
  `oem`/`userdata` mount branch and `netboot: …` messages; partitions always come
  from SPI NAND (ubifs).
- Netboot branch in `RkLunch.sh`: static networking from `ip=` is no longer
  supported; `network_init` always configures the MAC and starts DHCP.
- `divinus`: приведены к вендорскому ABI структуры HAL, которые передаются в
  librockit: `VIDEO_FRAME_S` (`pVirAddr` — 2 элемента, а не 3) и хвост
  `VI_CHN_ATTR_S` (`stShareBufChn` — три `int`, а не один). Прежние размеры
  приводили к чтению за границей структуры при `RK_MPI_VI_SetChnAttr` и
  сдвигу `u64PTS`. Проверено `_Static_assert` по заголовкам SDK для
  VI/VPSS/VENC/VIDEO_FRAME.
- `divinus`: HAL structures passed to librockit now match the vendor ABI:
  `VIDEO_FRAME_S` (`pVirAddr` holds two elements, not three) and the
  `VI_CHN_ATTR_S` tail (`stShareBufChn` is three `int`s, not one). The old
  sizes made `RK_MPI_VI_SetChnAttr` read past the object and shifted
  `u64PTS`. Verified with `_Static_assert` against the SDK headers for
  VI/VPSS/VENC/VIDEO_FRAME.


### Исправлено

- `divinus`: `yolo.source=vi1` закрыт на всех уровнях — `gating.c` блокирует
  его с причиной, `GET`/`POST /api/yolo?source=vi1` и неизвестные значения
  возвращают HTTP 400 `{"error":"invalid"}`, из WebUI опция удалена, а
  стартовый санитайзер переводит сохранённый `vi1` в `vpss3`. В zero-copy
  входе учитывается `w_stride` (строки пишутся с шагом тензора), при
  активации проверяются 4D-NHWC выходы и одинаковая размерность каналов всех
  ветвей, атрибуты входа/выходов пишутся в лог. Хостовый
  `yolo_postprocess_test` покрывает 80 классов (C = 255, `C % 16 != 0`) и
  проверяет, что каналы NHWC не выравниваются до 16; `bench/api_check.py`
  проверяет отказ для `vi1` и неизвестного значения.
- `divinus`: `yolo.source=vi1` is closed at every level — `gating.c` blocks it
  with a reason, `GET`/`POST /api/yolo?source=vi1` and unknown values return
  HTTP 400 `{"error":"invalid"}`, the WebUI option is removed, and the startup
  sanitiser turns a saved `vi1` into `vpss3`. The zero-copy input honours
  `w_stride` (rows are written at the tensor stride), activation validates
  4D-NHWC outputs and identical channel counts in all branches, and the input
  and output attributes are logged. The host `yolo_postprocess_test` covers 80
  classes (C = 255, `C % 16 != 0`) and verifies that NHWC channels are not
  padded to 16; `bench/api_check.py` checks the rejection of `vi1` and of an
  unknown value.
- DHCP: причина смены IP — MAC хранился в `/data/ethaddr.txt` (раздел
  `/userdata`), а `dhcpcd` использовал DUID из `/var/db/dhcpcd`; оба
  сбрасываются при полной прошивке. Дополнительно на `eth0` работали два
  клиента (`S41dhcpcd` и `udhcpc` из `RkLunch.sh`). Теперь MAC выводится из
  `/proc/device-tree/serial-number` и хранится в env (`oko_mac`), DHCP-клиент
  один — `dhcpcd` с client ID по MAC (`clientid`, без `duid`),
  `S41dhcpcd` заменён заглушкой, а `RkLunch.sh` вызывает `oko-net apply`.
  Проверено: после reboot MAC `02:47:8d:05:67:56` и IP `192.168.52.97`
  сохраняются; static `.220` также переживает reboot.
- DHCP: the changing IP was caused by the MAC living in `/data/ethaddr.txt`
  (`/userdata`) and `dhcpcd` using a DUID from `/var/db/dhcpcd`; both are
  recreated by a full firmware update. Two clients also ran on `eth0`
  (`S41dhcpcd` and `udhcpc` from `RkLunch.sh`). Now the MAC is derived from
  `/proc/device-tree/serial-number` and stored in env (`oko_mac`), there is
  one DHCP client (`dhcpcd` with `clientid` by MAC, no `duid`), `S41dhcpcd`
  is a stub, and `RkLunch.sh` calls `oko-net apply`. Verified: after reboot
  MAC `02:47:8d:05:67:56` and IP `192.168.52.97` are preserved; static
  `.220` also survives a reboot.
- WebUI/MP4: HTTP-поток `/video.mp4` больше не отдаёт пустой chunk и не рвёт
  соединение. `send_mp4_to_client()` теперь, как и recorder, режет Annex B
  через `nal_split()`: VPS/SPS/PPS HEVC приходят одним access unit, и старый
  обход `pack->nalu[]` не находил их, поэтому `moov` не строился. Проверено:
  PyAV декодирует 30 кадров hevc 1920x1080 с `http://<DUT>/video.mp4`.
- WebUI/MP4: the `/video.mp4` HTTP stream no longer sends an empty chunk and
  closes. `send_mp4_to_client()` now splits Annex B with `nal_split()` like the
  recorder: HEVC VPS/SPS/PPS arrive in one access unit, and the old
  `pack->nalu[]` walk missed them, so `moov` was never built. Verified: PyAV
  decodes 30 hevc 1920x1080 frames from `http://<DUT>/video.mp4`.
- WebUI: вкладки MP4/MJPEG показывают понятное сообщение, если поток выключен,
  а для MP4 добавлена подсказка про H.265 и MJPEG. MJPEG при
  `mjpeg.enable=true` отдаёт `multipart/x-mixed-replace` (проверено).
- WebUI: the MP4/MJPEG tabs show a clear message when the stream is disabled,
  and the MP4 tab hints at H.265 vs MJPEG. With `mjpeg.enable=true` the MJPEG
  endpoint returns a `multipart/x-mixed-replace` stream (verified).
- microSD/udev: eudev в образе читает только `/lib/udev/rules.d`, поэтому
  правило `60-oko-sdcard.rules` перенесено из `/etc/udev/rules.d` в
  `/lib/udev/rules.d`; теперь `oko-sdcard add/remove` действительно вызывается
  и пишет `/run/oko-sd.state`. Вендорское `61-sd-cards-auto-mount.rules`
  заменено заглушкой: его безусловный remove-`umount` снимал только что
  отформатированную карту на stale-событиях `sfdisk`/`partprobe`; stale-remove
  для ещё существующего `mmcblkX` теперь игнорируется. Проверено: format
  создаёт один FAT32-раздел `OKO`, а unbind/bind `mmc1:aaaa` даёт `present=0`
  → `present=1` и автомонтирование.
- microSD/udev: eudev in this image reads only `/lib/udev/rules.d`, so
  `60-oko-sdcard.rules` moved from `/etc/udev/rules.d` to `/lib/udev/rules.d`;
  `oko-sdcard add/remove` is now actually invoked and writes
  `/run/oko-sd.state`. The vendor `61-sd-cards-auto-mount.rules` is replaced by
  a stub: its unconditional remove-`umount` detached the freshly formatted card
  on stale `sfdisk`/`partprobe` events; stale remove events for an existing
  `mmcblkX` are now ignored. Verified: format creates one FAT32 `OKO` partition
  and unbinding/binding `mmc1:aaaa` changes `present=0` to `present=1` with
  automount.
- `oko-sdcard`: определение карты переведено с `/sys/block/*/removable`
  (в этом 5.10-ядре всегда `0`) на `device/type=SD`; `unmount` снимает все
  наложения `/mnt/sdcard`, включая старые p1+p2. VFAT монтируется с
  `uid=1000,gid=1000,fmask=0133,dmask=0022`.
- `oko-sdcard`: card detection now uses `device/type=SD` because
  `/sys/block/*/removable` is always `0` in this 5.10 kernel; `unmount` drops
  all stacked `/mnt/sdcard` mounts, including legacy p1+p2. VFAT mounts with
  `uid=1000,gid=1000,fmask=0133,dmask=0022`.
- Сборка: выбранная AIISP-модель копируется в oem **до** `build_mkimg oem`;
  раньше `cp` из post-clean выполнялся после создания `oem.img`, и
  `rkmodel_1080_1920.aiisp` отсутствовал в прошивке.
- Build: the selected AIISP model is copied into oem **before**
  `build_mkimg oem`; previously the post-clean `cp` ran after `oem.img` was
  created, so `rkmodel_1080_1920.aiisp` was missing from the firmware.
- `divinus/record`: H.265-запись больше не создаёт пустые `.mp4`. VENC-пак
  для HEVC приходит одним NAL-элементом на весь access unit; `record.c` теперь
  сам режет Annex B через `nal_split` и передаёт VPS/SPS/PPS/IDR/слайсы в
  muxer. Проверено: 8-секундный клип декодируется как 222 кадра 1920x1080.
  Заодно исправлено удаление старых сегментов: `recordPath` больше не
  исключается из квоты, когда файл уже закрыт.
- `divinus/record`: H.265 recording no longer produces empty `.mp4` files. The
  VENC pack for HEVC arrives as one NAL entry for the whole access unit, so
  `record.c` now splits Annex B with `nal_split` and passes
  VPS/SPS/PPS/IDR/slices to the muxer. Verified: an 8 s clip decodes as 222
  frames of 1920x1080. The quota prune was also fixed: a closed `recordPath`
  is no longer skipped, so old segments are actually removed.
- `divinus/rtsp`: `PLAY` больше не вызывает блокирующий геттер канала под уже
  взятым мьютексом RTSP. Дедлок останавливал видео-поток, из-за чего
  `RK_MPI_RGN_SetBitMap` начинал возвращать `0xfffffffe` и OSD пропадал.
  Неизвестный или выключенный `/live/<n>` теперь возвращает 404, а не main.
- `divinus/rtsp`: `PLAY` no longer calls the locking channel getter while the
  RTSP mutex is already held. The deadlock stalled the video thread, which made
  `RK_MPI_RGN_SetBitMap` return `0xfffffffe` and dropped the OSD. An unknown or
  disabled `/live/<n>` now returns 404 instead of falling back to main.
- `divinus`: второй поток при выключении делает soft-stop и оставляет канал
  выделенным (повторное включение мгновенное); создание канала и смена
  параметров энкодера применяются перезапуском в безопасном порядке
  `main → sub → JPEG`. Это устраняет разрушение OSD на RV1106.
- `divinus`: disabling the sub stream is now a soft stop that keeps the channel
  allocated (re-enabling is immediate); creating the channel or changing
  encoder parameters is applied by a restart in the safe
  `main -> sub -> JPEG` order. This removes the RV1106 OSD corruption.
- `divinus/hal/config.h`: `MAX_SECTIONS` увеличено с 16 до 40. Новые секции
  (eptz/rotate/venc/substream/aiisp/ivs/iq/hdr) выводили файл за лимит
  `find_sections()`, и после `app_config_save()` конфиг читался частично
  (`Section 'osd' not found`, `Configuration error code: 1`).
- `divinus/hal/config.h`: `MAX_SECTIONS` raised from 16 to 40. The new
  sections (eptz/rotate/venc/substream/aiisp/ivs/iq/hdr) exceeded
  `find_sections()`'s table, so after `app_config_save()` the configuration
  was read partially (`Section 'osd' not found`, `Configuration error code: 1`).
- `divinus/app_config.c`: сохранение OSD пишет `regX_*` с отступом в два
  пробела, как фабричный YAML; раньше отступ 4 пробела ломал разбор `osd`
  после рестарта и HDR-перезапуск терял регионы.
- `divinus/app_config.c`: OSD save emits `regX_*` with the factory two-space
  indentation; the previous four-space indent broke the `osd` section after a
  restart, so the HDR restart lost the overlays.
- `divinus/hal/plus/rk_hal.c`: `rk_video_destroy()` больше не прерывает
  teardown при ошибке `UnBind` — вызывающий код уже отвязал пару, и повторная
  отвязка оставляла VENC/VPSS канал неразрушенным.
- `divinus/hal/plus/rk_hal.c`: `rk_video_destroy()` no longer aborts teardown
  on an `UnBind` error; callers already unbound the pair, and the second
  unbind left the VENC/VPSS channel alive.
- OSD: RGN переведён на один canvas (`region.overlay.canvas = 1`). При двух
  canvas драйвер периодически отдавал кадр с OSD предыдущей секунды — время на
  видео шло не по порядку. Проверено 60 с: кадров со «старым» временем нет.
- OSD: the RGN now uses a single canvas (`region.overlay.canvas = 1`). With two
  canvases the driver periodically emitted a frame with the previous second's
  OSD, so the displayed time was not sequential. Verified for 60 s with no
  stale-time frames.
- `netboot-catch.py`: после появления login-приглашения (или по таймауту 180 с)
  скрипт закрывает `/dev/ttyACM*` и завершается, больше не держит serial и не
  спамит `^C` в консоль; окно ожидания U-Boot остаётся 900 с.
- `docker-entrypoint.sh`: TFTP-корень больше не world-writable (`0755`), сервер
  запускается без `--create` — загрузка файлов на хост отключена; скачивание
  `boot.img` для netboot работает как прежде.
- `netboot-catch.py`: after detecting the login prompt (or after a 180 s timeout)
  the script closes `/dev/ttyACM*` and exits, so it no longer holds the serial
  port or floods the console with `^C`; the U-Boot catch window stays at 900 s.
- `docker-entrypoint.sh`: the TFTP root is no longer world-writable (`0755`) and
  the server runs without `--create`, disabling uploads; downloading `boot.img`
  for netboot keeps working.
- `TESTBENCH.md` §7C, `README_RU.md`, команда `/bench`: исправлена процедура
  покомпонентной прошивки. `rkflash.sh oem/rootfs` (`upgrade_tool di`) в чистом
  Maskrom без загруженного loader отвечает `parameter is invalid`; нужен
  `db download.bin` + `wl <LBA> <размер всего раздела в секторах>` (`oem`
  0x2800/0xF000, `rootfs` 0x16800/0x69000). `wl` с длиной образа оставляет в
  хвосте старые UBI-PEB → `ubi_attach: bad image sequence number` и panic.
- `TESTBENCH.md` §7C, `README_RU.md`, the `/bench` command: fixed the
  per-partition flashing procedure. `rkflash.sh oem/rootfs` (`upgrade_tool di`)
  in bare Maskrom without a loaded loader reports `parameter is invalid`; use
  `db download.bin` plus `wl <LBA> <full partition size in sectors>` (`oem`
  0x2800/0xF000, `rootfs` 0x16800/0x69000). Passing the image length to `wl`
  leaves stale UBI PEBs → `ubi_attach: bad image sequence number` and panic.
- `netboot-catch.py`: `OKO_NETBOOT_LOG` без каталога (например, `netboot.log`)
  больше не падает на `os.makedirs('')` — родительский каталог нормализуется
  в `.`.
- `netboot-catch.py`: `OKO_NETBOOT_LOG` without a directory component (for
  example, `netboot.log`) no longer fails on `os.makedirs('')`; the parent
  directory is normalized to `.`.
- `INIT_ORDER.md`: убрано дублирование условия бутстрапа `divinus.yaml` —
  замена при отсутствии файла или `reg0_text` описана один раз, резервное
  копирование в `post_chk` отмечено отдельно.
- `INIT_ORDER.md`: removed the duplicated `divinus.yaml` bootstrap condition —
  replacement when the file or `reg0_text` is missing is described once, and
  the `post_chk` fallback is called out separately.
- `divinus.yaml`, который не разбирается (например, после ручной правки или
  импорта), больше не оставляет устройство без стримера: `main.c` при ошибке
  разбора один раз восстанавливает `divinus.yaml.bak` и повторяет разбор,
  и только затем завершается. Проверено на стенде: битый `web_port` →
  восстановление, `/api/status` отвечает.
- A `divinus.yaml` that no longer parses (after a manual edit or an import) no
  longer leaves the device without a streamer: on a parse error `main.c`
  restores `divinus.yaml.bak` once and retries, exiting only if that also
  fails. Verified on the bench: a broken `web_port` is rolled back and
  `/api/status` keeps answering.
- `/api/time?ts=`: отметка времени читалась в `short`, из-за чего эпоха
  обрезалась и на устройство выставлялось случайное время; теперь `long` с
  проверкой нижней границы.
- `/api/time?ts=`: the timestamp was read into a `short`, so the epoch was
  truncated and the device got a random time; it is a `long` with a lower-bound
  check now.
- Проверки символов: `compat.c` определяет функцию `__ctype_b`, а `-rdynamic`
  экспортирует её поверх таблицы ctype uClibc, поэтому любой макрос
  `isalnum()`/`isspace()` разыменовывал код как таблицу и ронял процесс
  (`SIGSEGV`). В `escape_url()`, разборе ONVIF и проверке имени NTP-сервера
  заменено на явные ASCII-сравнения.
- Character checks: `compat.c` defines a function named `__ctype_b` and
  `-rdynamic` exports it over the uClibc ctype table, so every
  `isalnum()`/`isspace()` macro dereferenced code as a table and crashed the
  process (`SIGSEGV`). `escape_url()`, the ONVIF parser and the NTP server-name
  check now use explicit ASCII comparisons.
- `send_file()` отдаёт только файлы под `/mnt/sdcard/` и резолвит симлинки
  (`realpath`). Раньше любой несовпавший URI трактовался как абсолютный путь от
  `/`, а MIME-тип был равен самому URI, поэтому `GET //etc/passwd` отдавал файл
  системы. **Ломающее изменение** для сторонних скриптов, которые скачивали
  файлы вне карты; для архива записей используйте `/api/recordings/file`.
- `send_file()` serves only files under `/mnt/sdcard/` and resolves symlinks
  (`realpath`). Previously any unmatched URI was treated as an absolute path
  from `/` and the MIME type was the URI itself, so `GET //etc/passwd` leaked the
  system file. **Breaking change** for third-party scripts that downloaded files
  outside the card; use `/api/recordings/file` for the recording archive.

### Безопасность

- `project/app/divinus/files/divinus.yaml`: фабричный конфиг включает
  аутентификацию web и RTSP (`admin` / `oko-change-me`), `web_auth_skiplocal`
  выключен; пароль — публичный фабричный дефолт, его нужно сменить до подключения
  камеры к недоверенной сети (см. `TESTBENCH.md` §6).
- `project/app/divinus/files/divinus.yaml`: the factory config now enables web and
  RTSP authentication (`admin` / `oko-change-me`) and disables
  `web_auth_skiplocal`; the password is a public factory default and must be
  changed before the camera is connected to an untrusted network (see
  `TESTBENCH.md` §6).
- `POST /api/cmd/import` принимает только `multipart/form-data`, ограничен
  128 КиБ, требует подстроки `system:` и `web_port:` и пишет файл только после
  этих проверок; предыдущий конфиг сохраняется как `.bak` до перезаписи.
- `POST /api/cmd/import` accepts `multipart/form-data` only, is limited to
  128 KiB, requires the `system:` and `web_port:` markers, and writes the file
  only after those checks; the previous config is kept as `.bak` before the
  overwrite.
- Смена пароля WebUI (`/api/web-auth`) требует текущий пароль и выполняется
  только методом `POST`; пароли по-прежнему не читаются ни одним GET-эндпоинтом.
- The WebUI password change (`/api/web-auth`) requires the current password and
  is `POST`-only; passwords are still never returned by any GET endpoint.

## [0.1.0] — 2026-10-29

Первая версия OKO-форка. База — Luckfox SDK V1.4.

### Добавлено

- Docker-окружение сборки: `Dockerfile` (Ubuntu 22.04, `luckfox-builder:22.04`,
  пользователь `builder` с UID/GID хоста, кросс-тулчейн Rockchip в `PATH`,
  `tftpd-hpa`), `docker-compose.yml` (контейнер `luckfox_oko_build_env`,
  `network_mode: host`, `TFTP_ROOT=/workspace/output/image`),
  `docker-entrypoint.sh` (запуск TFTP-сервера на `69/udp`) и `start.sh`
  (сборка образа, запуск контейнера, Git LFS, вход в shell).
- Конфигурация платы
  `project/cfg/BoardConfig_IPC/BoardConfig-SPI_NAND-Buildroot-RV1106_Luckfox_Pico_Pro_Max-NETBOOT-SC230AI.mk`
  (RV1106 Pro Max, SPI NAND, Buildroot, NETBOOT, SC230AI, `RKIPC_RV1106`,
  CMA 66M, `RK_ENABLE_WIFI=y`) и локальный симлинк `.BoardConfig.mk` на неё
  (симлинк не версионируется — `.gitignore` его исключает, создаётся
  `./build.sh lunch`). Рядом присутствует промежуточный вариант `…-NETBOOT.mk`
  без SC230AI.
- Фрагменты конфигурации ядра `rv1106-netboot.config` (`CONFIG_IP_PNP`,
  `CONFIG_ROOT_NFS`) и `rv1106-sc230ai.config`; подключены через
  `RK_KERNEL_DEFCONFIG_FRAGMENT`.
- Локальные симлинки быстрого доступа к конфигурации (не версионируются,
  каталог `config/` в `.gitignore`): `config/buildroot_defconfig`,
  `config/kernel_defconfig`, `config/dts_config`.
- Автоматизация netboot по последовательному порту: `netboot-catch.py`
  (последний `/dev/ttyACM*`, 115200, настройка `ipaddr`/`netmask`/`serverip`/
  `gatewayip`/`sys_bootargs`, `tftp` → `boot_fit`, лог `/tmp/kilo/netboot-run4.log`).
- Документация проекта: `README_RU.md`, `CHANGELOG.md`, `TESTBENCH.md`,
  `STRUCTURE.md`, `INIT_ORDER.md`.
- `.gitignore` для артефактов сборки (`output/`, `IMAGE/`, `config/`, `*.o`,
  `*.ko`, `build/`), core-дампов и Python-кэша.

### Изменено

- `sysdrv/drv_ko/insmod_ko.sh`: добавлена загрузка `sc230ai.ko` и `sc230ai`
  в список выгрузки неиспользуемых сенсоров (`__rmmod_camera_sensor`).
- Драйвер шагового двигателя 28BYJ-48 из вендорского SDK V1.4
  (`sysdrv/drv_ko/motor`, строка `__insmod motor.ko`) остаётся **выключенным**:
  `RK_ENABLE_MOTOR` не задан, модуль не собирается, а загрузка пропускается
  проверкой `-f`.
- Device tree `sysdrv/source/kernel/arch/arm/boot/dts/rv1106-luckfox-pico-pro-max-ipc.dtsi`
  адаптирован под текущую плату и сенсор: добавлены регуляторы и узел SC230AI,
  endpoint CSI D-PHY, mux линии сброса.
- Драйвер `sysdrv/source/kernel/drivers/media/i2c/sc230ai.c`: для не-thunderboot
  сборки линии `reset`/`pwdn` запрашиваются в состоянии `GPIOD_OUT_LOW`.