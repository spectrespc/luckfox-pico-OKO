# research — архив внешних исследований

Не-сборочная папка для сохранения полезного из внешних источников
(upstream-драйверы, документация, стеки) в виде двух частей:

1. заметка-вывод — что из внешнего материала переносимо в OKO, а что нет;
2. минимальный read-only снимок ключевых upstream-файлов с обязательным
   провенансом (источник, коммит, лицензия, хеш).

Папка **не участвует в сборке прошивки**: на неё не ссылается ни один
`Makefile`, `*.mk` или `build.sh` (прецедент — `bench/`).

## Правила

1. Одна тема = одна подпапка в kebab-case (`rockchip-npu-mainline`,
   `rga-upstream`, …).
2. `README.md` темы обязателен и содержит разделы именно в этом порядке:
   `Вопрос` → `Что это` → `Выводы для OKO` (переносимо / не переносимо) →
   `Проверенные факты` (пути и строки в нашем дереве) → `Ссылки` →
   `Провенанс снимков` (таблица) → `Как обновить снимок`.
3. `snapshot/` — только read-only копии upstream-файлов: никаких правок,
   никаких «улучшенных» версий, никакого участия в сборке. Файл вне
   upstream-пути не создаётся.
4. Таблица провенанса: upstream-путь → локальный путь → лицензия/SPDX →
   sha256 → размер → дата снятия → pin (репозиторий + полный sha коммита;
   для статических PDF — версия и дата документа).
5. Лимита на число файлов и размер снимка нет. Для крупных PDF/архивов
   фиксируйте в провенансе размер, хеш и причину хранения.
6. Не класть: датасеты, бинарники прошивок, секреты, артефакты прогонов
   (это `output/bench/`) и инструментальные скрипты (это `bench/`).
7. Любое добавление или обновление темы → запись в `CHANGELOG.md` →
   `[Не выпущено]` (категория `Добавлено`/`Изменено`) и актуализация
   индекса ниже.
8. Папка не участвует в сборке: при добавлении темы проверять, что на неё
   не ссылаются сборочные файлы.

## Индекс тем

| Тема | Что внутри |
|---|---|
| [`buildroot-2026-08/`](buildroot-2026-08/README.md) | миграция rootfs на upstream Buildroot 2026.08.1: вендорская дельта, пакетные патчи под uClibc/gcc 8, версии пакетов |
| [`rockchip-kernel-upstream/`](rockchip-kernel-upstream/README.md) | фиксы rockchip-linux/kernel `develop-5.10`/`develop-6.12` для ядра 5.10.160: SFC (KI-071), rk3x I2C (KI-073/080), rv1106_pm (KI-072/074/079), SC230AI (KI-011); что уже было в дереве и что отклонено |
| [`rockchip-npu-mainline/`](rockchip-npu-mainline/README.md) | mainline-драйвер NPU Rockchip `drivers/accel/rocket`; применимость к RV1106 и сопоставление с вендорским `rknpu` |
| [`rgn-mpp-lifecycle/`](rgn-mpp-lifecycle/README.md) | 2BPP overlay-регионы `RK_MPI_RGN_*`: вендорский цикл `GetCanvasInfo`/`UpdateCanvas`, переносимость destroy/recreate под живым энкодером, дефект KI-063 |
| [`rkadk-guide/`](rkadk-guide/README.md) | гайд Rockchip RKADK (152 с., 2024-01-08): API record/photo/stream/RTSP/RTMP/storage/OSD/параметры; что применимо к `divinus`, что не переносится (AOV — отдельный гайд) |
| [`stm8-iap-an2659/`](stm8-iap-an2659/README.md) | IAP на STM8 (KI-075): AN2659/AN2737 как архитектура загрузчика, PM0051 §4 как единственная точная процедура стирания блока, сверка с нашим кодом и SPL |
| [`rv1106-aov/`](rv1106-aov/README.md) | внешние материалы по AOV, PMU/MCU и I2C RV1106: `dehai168` (deep suspend, FE-PHY, PMU-таймер; после зачистки 2026-11-25 снимок AOV guide, design-материалов ISP-handover, storage/media/record/OSD/fMP4/RKNN и soak-скриптов, выжимка выводов в README), Luckfox #10/#99 (HPMCU/LPMCU, JTAG, `mcutool`), `luyi1888/rv1106-mcu`, `liamhays/rk3566-mcu` (SCR1, mailbox, suspend), RV1106 TRM V0.3 и даташиты Rev 1.8 и Rev 2.0, rkbin release notes, Rockchip I2C guide, `as-jackson/Rockchip-Docs` (pinctrl, IO-domain, power, camera, RGA), X5 SC230AI (sync/FPS/HDR/gain), SPI noirq patch; что переносимо в OKO и что остаётся гипотезой |