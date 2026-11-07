# rockchip-npu-mainline — mainline-драйвер NPU Rockchip (`drivers/accel/rocket`)

## Вопрос

Можно ли использовать или перенести mainline-драйвер NPU Rockchip
`drivers/accel/rocket` на RV1106 в ядре 5.10.160 OKO и что из него полезно,
даже если прямой перенос невозможен.

## Что это

`drivers/accel/rocket` — upstream-драйвер подсистемы DRM accel для NPU
Rockchip (Rockchip называет блок RKNN или RKNPU). Автор — Tomeu Vizoso.
Модель разделения: «тонкое ядро + Gallium-драйвер в Mesa».

Ядро делает только следующее:

- включает и выключает питание/клоки и управляет runtime-PM;
- выделяет буферы (GEM) и отображает их в адресное пространство NPU;
- принимает задания и ставит их в очередь DRM scheduler.

Всё остальное — разбор сети, планирование слоёв, генерация регистровых команд
(regcmd) — выполняет userspace-драйвер `rocket` в Mesa3D. Аппаратура описана
в главе 36 RK3588 TRM.

Ключевые свойства по снимку:

- единственный `of_device_id` — `rockchip,rk3588-rknn-core`; для userspace
  создаётся одно facade-устройство `rknn`, задания распределяются по ядрам;
- поддерживается только RK3588 (`Documentation/accel/rocket/index.rst`);
- сборка требует `DRM_ACCEL`, `(ARCH_ROCKCHIP && ARM64) || COMPILE_TEST`,
  `ROCKCHIP_IOMMU || COMPILE_TEST`, `MMU`; выбираются `DRM_SCHED` и
  `DRM_GEM_SHMEM_HELPER`.

### UAPI

Четыре ioctl (`include/uapi/drm/rocket_accel.h`):

| ioctl | Назначение |
|---|---|
| `CREATE_BO` | создать GEM-буфер; вернуть handle, `dma_address` в адресном пространстве NPU и `offset` для `mmap` |
| `SUBMIT` | передать массив job'ов; ядро планирует их с учётом зависимостей |
| `PREP_BO` | начать владение буфером со стороны CPU: дождаться завершения job'ов NPU и синхронизировать кэши |
| `FINI_BO` | завершить владение CPU: синхронизировать кэши для доступа NPU |

Модель работы:

- **job** — единица планирования; содержит массив **task**;
- **task** — минимальная единица работы: DMA-адрес regcmd-буфера и число
  команд в нём;
- все task одного job выполняются последовательно на одном ядре, чтобы
  использовать резидентность данных в SRAM;
- `in_bo_handles` / `out_bo_handles` описывают зависимости job'а: ядро
  упорядочивает job'ы по чтению/записи буферов;
- `PREP_BO`/`FINI_BO` — явный протокол передачи владения буфером между CPU и
  NPU с синхронизацией кэшей, вместо неявных предположений;
- на каждый открытый fd создаётся отдельный `iommu_paging_domain`; выданный
  `dma_address` приватен для fd, то есть клиенты изолированы друг от друга.

## Выводы для OKO

### Не переносимо (код и UAPI)

- Драйвер поддерживает только RK3588; RV1106 в `of_device_id` нет.
- `Kconfig` требует `ARCH_ROCKCHIP && ARM64`, а OKO — ARM32 (`arm`).
- Нужен `ROCKCHIP_IOMMU`; в нашей конфигурации NPU работает без отдельного
  IOMMU-домена на клиента.
- Нужна подсистема `DRM_ACCEL`, появившаяся в ядре 6.2; в нашем 5.10.160
  каталога `drivers/accel` нет.
- Userspace-часть (Gallium `rocket` в Mesa) знает регистровую модель RK3588 и
  не умеет RV1106.

Практичный путь OKO остаётся вендорским: драйвер `rknpu` 0.9.8,
`librknnmrt.so` 1.6.0 и RKNN-Toolkit2 для конвертации моделей. Обновлять его
на mainline-стек смысла нет.

### Переносимо (как идея и справочник)

- **Явный BO-ownership протокол** (`PREP_BO`/`FINI_BO`): полезен как образец,
  если когда-нибудь появится своя обвязка буферов; сейчас вендорский `rknpu`
  закрывает это своими ioctl.
- **IOMMU-изоляция клиентов**: отдельный домен на fd — хорошая модель
  безопасности для многопроцессного использования NPU.
- **Regcmd-модель задач**: task = пачка регистровых команд; job = набор task
  с зависимостями по буферам. Помогает понимать, как устроен вендорский
  submission.
- **Карта регистров** `rocket_registers.h` — офлайн-справочник по блокам
  PC/CNA/CORE/DPU/DPU_RDMA/PPU/PPU_RDMA (автоген из Mesa `registers.xml`).
- **Границы mainline-стека**: снимок фиксирует, что upstream поддерживает
  только RK3588 и насколько «тонким» остаётся ядро.

### Кросс-проверка PC-блока

Offset статуса задачи `0x3c` совпадает у mainline RK3588
(`rocket_registers.h:212`, `REG_PC_TASK_STATUS`) и у нашего вендорского
`rv1106_rknpu_config` (`rknpu_drv.c:232`, `pc_task_status_offset = 0x3c`).
Ширина поля номера задачи разная: в mainline-регистре `TASK_NUMBER` — 12 бит
(`0xfff`, `rocket_registers.h:191`), в конфиге RV1106 — 16 бит
(`0xffff`, `rknpu_drv.c:230-231`). Вывод: RV1106 и RK3588 используют
родственную модель PC-юнита, но не идентичную; копировать регистровые
структуры RK3588 в RV1106 нельзя.

### Что осталось непроверенным

- Точный релиз ядра, в котором появился `rocket`. По внешним источникам —
  6.18; проверка по тегам upstream не выполнялась.
- Числа производительности Mesa/Teflon (MobileNetV1/V2, SSDLite MobileDet) —
  по внешним данным, на стенде OKO не проверялись.
- Статус поддержки RK3576 в upstream — по внешним данным, находится в
  списках рассылки и не влит.

## Проверенные факты

Наше дерево:

- `sysdrv/source/kernel/Makefile:2-4` — ядро `5.10.160`.
- `sysdrv/source/kernel/drivers/accel` — отсутствует; подсистемы DRM accel в
  нашем ядре нет.
- `sysdrv/source/kernel/drivers/rknpu/rknpu_drv.c:225-243` — конфигурация
  `rv1106_rknpu_config`: `dma_mask = DMA_BIT_MASK(32)`, `core_mask = 0x1`,
  `pc_task_number_bits = 16`, `pc_task_number_mask = 0xffff`,
  `pc_task_status_offset = 0x3c`, `pc_data_amount_scale = 2`.
- `sysdrv/source/kernel/drivers/rknpu/rknpu_drv.c:324-325` — compatible
  `rockchip,rv1106-rknpu` → `rv1106_rknpu_config`.
- `sysdrv/source/kernel/arch/arm/boot/dts/rv1106.dtsi:1127-1138` — узел
  `npu: npu@ff660000`, `status = "disabled"` по умолчанию.
- `project/cfg/BoardConfig_IPC/luckfox-rv1106-tb-spi_nand-post.sh:134` и
  `luckfox-rv1106-tb-emmc-post.sh:52` — загрузка `rknpu.ko`.
- `project/cfg/BoardConfig_IPC/overlay/overlay-luckfox-buildroot-init/usr/bin/oko-osd:215-222` —
  OSD читает `/proc/rknpu/load`.

Снимок mainline (пин ниже):

- `snapshot/rocket_Kconfig` — зависимости и `select` для `DRM_ACCEL_ROCKET`.
- `snapshot/rocket_Makefile` — объекты `rocket_core.o`, `rocket_device.o`,
  `rocket_drv.o`, `rocket_gem.o`, `rocket_job.o`.
- `snapshot/rocket_drv.c` — facade-устройство `rknn`, один
  `of_device_id`, четыре ioctl, отдельный IOMMU-домен на fd.
- `snapshot/rocket_registers.h:50` — `REG_PC_BASE_ADDRESS 0x10`
  (`PC_SOURCE_ADDR` маска `0xfffffff0`, shift 4; бит 0 — `PC_SEL`).
- `snapshot/rocket_registers.h:70` — `REG_PC_REGISTER_AMOUNTS 0x14`,
  поле `PC_DATA_AMOUNT` — 16 бит `0xffff`.
- `snapshot/rocket_registers.h:172,191` — `REG_PC_TASK_CON 0x30`, поле
  `TASK_NUMBER` — 12 бит `0xfff`.
- `snapshot/rocket_registers.h:212` — `REG_PC_TASK_STATUS 0x3c`.
- `snapshot/rocket_registers.h` — блоки: CNA `0x1000`, CORE `0x3000`,
  DPU `0x4000`, DPU_RDMA `0x5000`, PPU `0x6000`, PPU_RDMA `0x7000`.
- `snapshot/rocket_accel.h` — UAPI: структуры `create_bo`, `prep_bo`,
  `fini_bo`, `job`, `task`, `submit`.
- `snapshot/index.rst` — «Hardware currently supported: RK3588».

## Ссылки

- Upstream-код (пин снимка):
  <https://github.com/torvalds/linux/tree/d48723711bae9b8cdc56719c448ba42fef28cbee/drivers/accel/rocket>
- Документация ядра: <https://docs.kernel.org/accel/rocket/index.html>
- Userspace-драйвер Mesa:
  <https://gitlab.freedesktop.org/mesa/mesa/-/tree/main/src/gallium/drivers/rocket>
- RK3588 TRM, глава 36 — регистровое описание NPU (публичной стабильной
  ссылки нет; документ распространяется Rockchip).

Внешние утверждения (не проверялись на стенде OKO, дата снятия — 2026-11-07):

- Mesa/Teflon ускоряет свёртки с per-tensor квантизацией без дилатации и
  поддерживает fused add.
- Ориентиры времени на RK3588: MobileNetV1 ~18 мс, MobileNetV2 ~21 мс,
  SSDLite MobileDet ~48 мс.
- Поддержка RK3576 в upstream — в списках рассылки, не влита.
- `rocket` появился в ядре 6.18 — по внешним источникам.

## Провенанс снимков

Все файлы сняты с одного пина `https://github.com/torvalds/linux.git`
(HEAD на момент снятия, подтверждён `git ls-remote`).

| Upstream-путь | Локальный путь | Лицензия/SPDX | sha256 | Размер, байт | Дата снятия | Pin (полный sha) |
|---|---|---|---|---|---|---|
| `drivers/accel/rocket/Kconfig` | `snapshot/rocket_Kconfig` | `GPL-2.0-only` | `7aa71225c3b8bf3b5ede368e7d8a927b123e8ec0e2b7d840e9ce4664300e69f8` | 781 | 2026-11-07 | `d48723711bae9b8cdc56719c448ba42fef28cbee` |
| `drivers/accel/rocket/Makefile` | `snapshot/rocket_Makefile` | `GPL-2.0-only` | `23a43c8dd8e8b6ce05935a476235d41a0532f7fe2accfa92203389852da32449` | 181 | 2026-11-07 | `d48723711bae9b8cdc56719c448ba42fef28cbee` |
| `drivers/accel/rocket/rocket_drv.c` | `snapshot/rocket_drv.c` | `GPL-2.0-only` | `082f7db21fd8bb99450d3ded5c2371f26a4ca64ed5d5efd6ebc2ae1d56324e0e` | 7046 | 2026-11-07 | `d48723711bae9b8cdc56719c448ba42fef28cbee` |
| `drivers/accel/rocket/rocket_registers.h` | `snapshot/rocket_registers.h` | `GPL-2.0-only OR MIT` | `7214696ed897f1afe416aadacc6414597ba62aab8204d154fbde43f32bb25dbe` | 193636 | 2026-11-07 | `d48723711bae9b8cdc56719c448ba42fef28cbee` |
| `include/uapi/drm/rocket_accel.h` | `snapshot/rocket_accel.h` | `MIT` | `aea392c2a755ea119cecc8b0185627c1335814130f8eb56369e5ab491a230f04` | 4281 | 2026-11-07 | `d48723711bae9b8cdc56719c448ba42fef28cbee` |
| `Documentation/accel/rocket/index.rst` | `snapshot/index.rst` | `GPL-2.0-only` | `c83a9ec5bd35ad05cf61f893af72f3ac04e08c5537c94633f211daf1037f3f8c` | 657 | 2026-11-07 | `d48723711bae9b8cdc56719c448ba42fef28cbee` |

Итого: 6 файлов, 206 582 байта. Это формально больше мягкого лимита
`200 КБ` из `research/README.md`, потому что `rocket_registers.h` в текущем
upstream занимает 193 636 байт (в плане ожидалось ~142 КБ). Решение: файл
**включён** — это основная справочная ценность темы для офлайн-чтения; лимит
превышен осознанно и зафиксирован здесь.

## Как обновить снимок

1. Получить актуальный HEAD: `git ls-remote https://github.com/torvalds/linux.git HEAD`.
2. Скачать файлы с новым пином:

   ```sh
   PIN=<новый-полный-sha>
   BASE=https://raw.githubusercontent.com/torvalds/linux/$PIN
   base=research/rockchip-npu-mainline/snapshot
   curl -fsS -o "$base/rocket_Kconfig" "$BASE/drivers/accel/rocket/Kconfig"
   curl -fsS -o "$base/rocket_Makefile" "$BASE/drivers/accel/rocket/Makefile"
   curl -fsS -o "$base/rocket_drv.c" "$BASE/drivers/accel/rocket/rocket_drv.c"
   curl -fsS -o "$base/rocket_registers.h" "$BASE/drivers/accel/rocket/rocket_registers.h"
   curl -fsS -o "$base/rocket_accel.h" "$BASE/include/uapi/drm/rocket_accel.h"
   curl -fsS -o "$base/index.rst" "$BASE/Documentation/accel/rocket/index.rst"
   ```

3. Проверить, что ни один файл не пуст: `wc -c "$base"/*`.
4. Пересчитать хеши: `sha256sum "$base"/*` и обновить таблицу провенанса,
   дату снятия и пин.
5. Просмотреть `head -3` каждого файла — SPDX-заголовок должен остаться.
6. Проверить, не появились ли новые `of_device_id`, ioctl или зависимости в
   `rocket_drv.c`, `rocket_accel.h`, `rocket_Kconfig`; при изменениях обновить
   разделы «Что это», «Выводы для OKO» и «Проверенные факты».
7. Добавить запись в `CHANGELOG.md` → `[Не выпущено]`.