# rv1106-aov-vendor-sample — вендорский AP-side AOV-сэмпл Rockchip

## Вопрос

Где лежит вендорская AP-сторонняя реализация AOV для RV1106, которую мы не
нашли в Luckfox SDK, и можно ли по ней закрыть провал `keep_process` /
`park_process` (шторм `mipi-csi2-hw` и потерю потоков) в `divinus`?

## Что это

Полный Rockchip AOV-сэмпл `project/app/aov_sample` из SDK платы LubanCat RV06
(форк Rockchip SDK для RV1106/RV1103). В Luckfox-дереве этого каталога нет;
в `jetkvm/rv1106-system` он присутствует и **побайтово совпадает** с
LubanCat-версией (одинаковые blob-SHA), поэтому снимок один.

Уникальные части JetKVM (KVM/UVC-приложение, MAC из I2C, свой
`RkLunch.sh`) к AOV/сну не относятся и в снимок не брались — по AOV там
лежит тот же Rockchip-сэмпл.

Ключевые файлы снимка:

- `common/aov/sample_comm_aov_rv1106.c` — `SetSuspendTime` (PMU
  `ms*32.768`), `EnterSleep` (`echo mem`), `Bind/Unbind` SD/Ethernet/audio,
  meta/wakeup-параметры MCU.
- `common/isp2.x/sample_comm_isp.c` — `SAMPLE_COMM_ISP_SingleFrame()` /
  `MultiFrame()`.
- `sample_aov_vi.c` — минимальный цикл «один кадр → сон».
- `sample_aov_vi_iva_venc.c` — цикл с IVA/NPU, VENC, MP4-записью и
  переключением single/multi-frame.
- `resume_suspend_test.sh`, `resume_suspend_test_2.sh` — вендорские
  стресс-скрипты (unbind `dwmmc`, `sample_aov_* ... --suspend_time`,
  `echo mem`).
- `RkLunch.sh`, `Makefile` — запуск/сборка.

### Ревизия AIGLASS (SDK `RV1106B_RV1103B_Linux_IPC_SDK_Release_V1.0.0_20241012`)

Второй источник темы — `makevary/AIGLASS_DEV_ENV`: вендорский SDK уже под
RV1106B/RV1103B, дефолт `.BoardConfig.mk` →
`project/cfg/BoardConfig_BatteryIPC/BoardConfig-EMMC-NONE-RV1103B_EVB1_V10_V11-IPC.mk`,
rockit KO только `release_rockit-ko_rv1103b_arm` (`1c41df6`, 2024-10-12,
«mcu: fix mcu auto start in suspend/resume»). Снимок лежит отдельным корнем
`snapshot/aiglass-rv1106b/`, upstream-путь сохранён.

Отличия AOV-сэмпла от снимка LubanCat:

- структура: общий `common/aov/sample_comm_aov.c` (27 767 Б) вынесен из
  per-chip файлов, которые разделены на `sample_comm_aov_rv1106.c`
  (24 394 Б), `_rv1106b.c` (28 007 Б) и `_rv1126.c` (21 986 Б);
- `rv1106`-файл содержит `SAMPLE_COMM_AOV_BindUsb/UnbindUsb`
  (`xhci-hcd.0.auto`, uevent-строки
  `bind@/devices/platform/usbdrd/ffb00000.usb/xhci-hcd.0.auto`), которых в
  нашем LubanCat-снимке (45 270 Б, merged-версия) **нет**;
- появились `sample_aov_bind_test.c` (стресс bind/unbind всех периферий:
  `--enable_sdcard`, `--enable_usb`, `--enable_ethernet`, `--enable_audio`,
  `--enable_sdio`), `sample_aov_npu_test.c`, `sample_aov_aiisp_iva_venc.c`,
  `sample_aov_multi_vi*.c`;
- `resume_suspend_test.sh` вырос до 50 425 Б (у нас 12 630 Б) — это отдельный
  сценарий, в снимок не взят;
- BoardConfig-образцы: `BoardConfig-SPI_NOR-NONE-RV1106B_EVB2_V10_V11-BAT_IPC_SC200AI_AOV.mk`
  и `...-BAT_IPC_FASTBOOT.mk` (SPI NOR, батарейный IPC).

## Выводы для OKO

### Переносимо (как алгоритм, не как код)

1. **Процесс живёт через `mem`; `RK_MPI_SYS_Exit`/`Init` не вызывается.**
   Вендор не делает полный stop/start и не перезапускает приложение.
2. **«Single-frame» — это пауза AIQ, а не остановка VI.** Точные вызовы
   (`sample_comm_isp.c:1127-1146`):
   - `SAMPLE_COMM_ISP_SingleFrame(cam)` =
     `rk_aiq_uapi_sysctl_stopMotionDetect(ctx)` +
     `rk_aiq_uapi_sysctl_pause(ctx, true)`;
   - `SAMPLE_COMM_ISP_MultiFrame(cam)` =
     `rk_aiq_uapi_sysctl_resume(ctx)` +
     `rk_aiq_uapi_sysctl_startMotionDetect(ctx)`.
3. **Перед сном**: `SingleFrame` → `DisableNonBootCPUs` (в файле заглушка,
   `:1455`) → **вычитать остатки кадров** циклом
   `RK_MPI_VI_GetChnFrame`/`ReleaseChnFrame` (оба канала) → при
   необходимости `UnbindSdcard`/`UnbindSoundcard`/`UnbindEthernet` →
   `SetSuspendTime(ms)` → `EnterSleep` (`echo mem`).
4. **После пробуждения** в single-frame режиме кадр добывается через
   `RK_MPI_VI_DevEnableSinglelFrame(dev, 1)` (+ dummy-кадры); если IVA/NPU,
   GPIO-прерывание или большое изменение экспозиции — `MultiFrame`, IVA в
   `VIDEO`-режим и старт MP4; когда движения нет / счётчик кадров исчерпан —
   `StopRecordMp4` → `SingleFrame` → drain → сон. Базовый `sample_aov_vi.c`
   делает то же самое без IVA/записи.
5. `SAMPLE_COMM_AOV_SetSuspendTime` пишет `ms * 32.768` в
   `SUSPEND_TIME_REG` — совпадает с нашим PMU-счётчиком 32 кГц.
6. Наш `[sleep] usb_on_motion` — аналог вендорского `UnbindEthernet` для USB:
   убрать периферию из resume-пути. FE-PHY на нашем борту так отвязывать
   нельзя (rebind не поднимает PHY, вердикт `rv1106-aov/README.md`).

### Ревизия AIGLASS: что даёт сверх снимка LubanCat

1. **Детерминированное ожидание bind/unbind.** Вендор пишет `bind`/`unbind`
   в sysfs и ждёт не «на глаз», а события `NETLINK_KOBJECT_UEVENT`:
   `SAMPLE_COMM_AOV_BindUsb` (`sample_comm_aov_rv1106.c:723`) и
   `SAMPLE_COMM_AOV_UnbindUsb` (`:777`) сравнивают прочитанную строку с
   `USB_BIND_DONE`/`USB_UNBIND_DONE` (`:720-721`), отбрасывают прочие
   сообщения и ограничены `MAX_SELECT_TIMEOUT = 2 * 1000 * 1000` мкс (`:24`).
   Перед этим проверяется текущее состояние
   `device_driver_is_bound()`; повторный bind/unbind не пишется.
2. **То же для SD/eMMC.** `SAMPLE_COMM_AOV_UnbindSdcard` (`:385`) после
   `device_detach_driver("ffaa0000.mmc")` ждёт
   `unbind@/devices/platform/ffaa0000.mmc`; bind дополнительно ждёт
   `bind@/devices/platform/ffaa0000.mmc/mmc_host/mmc1/mmc1` (`:300-301`) и
   проверяет `detect_sdcard_is_enable()` по регистру `0xffaa0050` (`:304`).
3. **Ethernet-ветка использует `NETLINK_ROUTE`/`RTM_NEWLINK`** (`:116-190`) —
   это нужно только для их `UnbindEthernet`; на нашем борту Ethernet не
   отвязывается (см. `rv1106-aov/README.md`).
4. **`sample_aov_bind_test` как референс gate-условий**: перебирает все
   периферии и печатает success/failure по каждой, то есть вендор тоже
   считает bind/unbind отдельной операцией со своим кодом возврата.

Для `divinus` это подтверждает уже выбранный подход: писать в sysfs и
**проверять предикат готовности с таймаутом**, а не считать, что запись в
`bind` синхронна. Реализация в OKO — poll по sysfs (без netlink-сокета):
`usb_lock` уже сериализует `report_motion()`/`oko-wifi apply`, а лишний
NETLINK-сокет в event-пути дороже 10-мс опроса.

### Сверка API RV1106 ↔ RV1106B/RV1103B: решение по переносимости

Гейт — `Rockchip_RV1106_RV1103B_Instructions_API_Differential_EN.pdf`
(V1.0.0, 2024-09-01, 7 с.). Он **не описывает AOV-MPI и PMU-регистры**:
весь документ — это toolchain (`time_t` 8 байт, `input_event` без `timeval`),
VI `SetModParam`/`GetModParam`, новые поля `IVS_OD_INFO_S`/`IVS_OD_ATTR_S`,
уменьшение лимита MB 10240 → 1024, TDE alpha-blending, RGN 1BPP OSD, очередь
VPSS, channel map/VQE у AI. Вывод: отличия AOV-сэмплов — это **другая
версия media и другой чип**, а не задокументированная AOV-дельта API.

| Что использует AIGLASS-сэмпл | Есть в нашем `media/out` | Решение |
|---|---|---|
| `RK_MPI_VI_DevEnableSinglelFrame` | да, `rk_mpi_vi.h:69` (опечатка вендора с двумя `l`) | совместимо |
| `rk_aiq_uapi2_sysctl_pause/resume` | да, `rk_aiq_user_api2_sysctl.h:550,557` | совместимо, уже используется в `fast_resume=aiq` |
| `rk_aiq_uapi_sysctl_start/stopMotionDetect` | **нет** ни в uAPI, ни в uAPI2 | не переносимо |
| `RK_MPI_VI_SetModParam`/`GetModParam` (`sample_aov_multi_vi_venc.c:465-475`) | **нет** в `rk_mpi_vi.h` | не переносимо как код |
| AIISP: `rk_comm_aiisp.h`, `rk_aiq_uapi2_sysctl_getAinrParams`, `.aiisp`-модели | да | только как идея, сэмпл не собирается |
| ROCKIVA (`rockiva_*`, `librockiva`) | да | не нужно: у нас свой IVS/YOLO-путь |
| RKNN в `sample_aov_npu_test.c` | `librknnmrt` есть, `rknn_api.h` в `media/out/include` **нет** | не переносимо |
| `SUSPEND_TIME_REG 0xff300048`, `ms * 32.768` | наш PMU 32 кГц уже подтверждён | совместимо, уже есть |
| `detect_sdcard_is_enable()` через `0xffaa0050` | регистр не проверялся | не тащим: проверяем наличие `/dev/mmcblk*` |

Итог по сэмплам: как код не переносится ни один файл (другой чип, другая
версия media, чужие `sample_comm`/`RkLunch.sh`). В `divinus` идут только
**техники**: ожидание bind/unbind с таймаутом (Задача 3 плана) и отвязка SD
перед `mem` за фича-флагом (Задача 4). `pre-record` и MCU-порт ISP — отдельные
задачи после этой ревизии, в этом плане не реализуются.

### Отложено (вне этого плана)

- RV1106-бинарь `rockit.ko` с фиксом «mcu: fix mcu auto start in
  suspend/resume» (`1c41df6`, AIGLASS собирает его только для rv1103b) —
  запрос релиза у Luckfox/Rockchip; подмена rv1103b-файлом запрещена.
- Полноценный AOV с MCU-портом ISP: `sysdrv/source/mcu/rt-thread/applications/battery-ipc`
  и отсутствующий у нас `pre-record` (есть у AIGLASS; в upstream Luckfox MCU
  tree его тоже нет) — после решения ISP-handover (KI-072, на этом железе
  признан нереализуемым). В дереве уже лежит вендорский референс постоянного
  владения ISP: AP-DTS `rv1103g-battery-ipc-v10/v11.dts`
  (`rkisp_thunderboot`, `amp-shared` i2c4) + MCU-борда
  `battery_ipc_demo_v10-SC230AI-ADC`; это не динамическая передача на
  suspend, а другая архитектура (общий буфер кадров).
- Диагностика MCU: `cmbacktrace`, `coredump`, `rpmsg_cmd`, `jpeg_enc`,
  `ota`, `aupipe` в дереве есть, но выключены; `libfastawb_gcc.a`
  слинкован без AWB-пути. Включать только отдельной задачей — дерево
  вендорское.
- Сверка экспериментальных профилей `-FASTBOOT`/`-AOV` с
  `BoardConfig_BatteryIPC` AIGLASS — только после приёмки API-Differential
  (в этом плане гейт закрыт: отличий AOV-API в документе нет).
- CI/build-tooling из `pennycoders/rv1106-system` (devcontainer, GitHub
  Actions, `make_buildkit.sh`) — отдельное решение.
- `OpenHD/rockchip-sdk` (в `.chips` нет RV1106), `YuWanCCC/app_demo`
  (RV1126B, aarch64-rockchip1240), `sysdrv/drv_ko/motor/motor_24byj48.c`
  (униполярный 24BYJ48, у нас биполярные DRV8833 + STM8) — не применять.

### Не переносимо

- **Код как есть**: файлы Rockchip под BSD-style/Apache-2.0, корневого
  `LICENSE` в репозиториях нет; держим только read-only снимок, в сборку
  OKO не тянем.
- **Другой сенсор/борд**: сэмпл собран под сенсоры Rockchip EVB; поддержка
  `rk_aiq_uapi_sysctl_pause/resume` и single-frame для SC230AI не проверена.
- **MCU/meta-путь**: `RK_ENABLE_RTT`, `rtos@40000`, `wakeup_param`,
  `rk_meta_wakeup_param.h` — своя прошивка/мета, не наши `divinus`.
- **Скрипты**: используют `rk_mpi_venc_test`, `sample_aov_*` и пути
  `/userdata`, `/etc/iqfiles`; напрямую не применимы.

### Что это меняет для наших экспериментов

- `keep_process` (живой AIQ, без drain) дал шторм `mipi-csi2-hw`; вендор
  перед `mem` **паузит AIQ и вычитывает кадры**.
- `park_process` (полный `sdk_stop`/`sdk_start`) шторм снял, но потерял
  сервер/RTSP/IVS-потоки; вендор SDK и потоки сохраняет.
- Остаётся ровно вендорский вариант: **не трогать SDK/потоки, добавить в
  `divinus` паузу/резюм AIQ + остановку/запуск motion-detect + drain VI**.
  В нашем HAL `rk_aiq` есть только `fnStart`/`fnStop`; нужны обёртки
  `rk_aiq_uapi_sysctl_pause/resume` и
  `rk_aiq_uapi_sysctl_start/stopMotionDetect` (dlsym в `rk_hal.c`).
- `VI_PauseChn` из `common/isp/rv1106/isp.c` — это HDR-путь, **не** AOV:
  вендорский AOV паузит AIQ, а не VI.

## Проверенные факты

- `sample_comm_isp.c:1127-1146` — single/multi frame = AIQ pause/resume +
  motion-detect stop/start.
- `sample_aov_vi.c:218-240,260-275,329-340` — переход single→multi,
  drain 60 кадров, `SingleFrame`+drain перед `EnterSleep`.
- `sample_aov_vi_iva_venc.c:420-445,606-662` — single/multi переходы,
  `StartRecordMp4`/`StopRecordMp4`, drain и сон.
- `sample_comm_aov_rv1106.c:366-372` — `EnterSleep` = `echo mem`;
  `:429-436` — `ms*32.768`; `:516-868` — bind/unbind Ethernet/SD;
  `:1337-1450` — sound; `:1453-1456` — emmc/CPU-заглушки.
- `resume_suspend_test.sh:25,34` — unbind `ffaa0000.mmc` и `echo mem`;
  `:231` — `sample_aov_vi ... --suspend_time 100 --vi_frame_mode 1`.
- AIGLASS `common/aov/sample_comm_aov_rv1106.c`: `:21` `SUSPEND_TIME_REG`,
  `:24` `MAX_SELECT_TIMEOUT` (2 с), `:298-302` строки uevent и путь драйвера
  SD, `:304-312` `detect_sdcard_is_enable` (`0xffaa0050`), `:314-383`
  `BindSdcard`, `:385-439` `UnbindSdcard`, `:719-722` строки и путь драйвера
  USB, `:723-776` `BindUsb`, `:777-831` `UnbindUsb`.
- AIGLASS `sample_aov_bind_test.c:502-664` — bind/unbind SD/eMMC, Ethernet,
  audio, USB и SDIO по кругу с подсчётом успехов.
- AIGLASS `sample_aov_multi_vi_venc.c:465-475` — единственное место с
  `RK_MPI_VI_SetModParam`, которого нет в нашем media.
- Наш media: `media/out/include/rk_mpi_vi.h:69` —
  `RK_MPI_VI_DevEnableSinglelFrame`; `media/out/include/rkaiq/uAPI2/rk_aiq_user_api2_sysctl.h:550,557` —
  `pause/resume`; `grep -rn MotionDetect media/out/include` — пусто;
  `grep -rn "SetModParam" media/out/include` — пусто; `media/out/include/rk_comm_aiisp.h`
  и `media/out/include/rockiva/` присутствуют.
- AIGLASS `sysdrv/drv_ko/rockit/release_rockit-ko_rv1103b_arm/release_version.txt`
  — `commit 1c41df617a42efb36cf235869f2aae11b4b63343`, 2024-10-12 (наш
  `release_rockit-ko_rv1106_arm` — `28ee389`, 2024-04-20).
- Наш контекст: `sysdrv/source/kernel/arch/arm/mach-rockchip/rv1106_pm.c`
  (`hpmcu_fast_wkup`), `project/app/divinus/source/src/sleep_policy.c`,
  KI-072 (2026-11-25), отчёты `output/bench/aov-test/two-tier-20261125/`.

## Ссылки

- LubanCat RV06 SDK: <https://github.com/LubanCat/RV06_03_Linux_SDK>
  (`project/app/aov_sample`, ветка `main`,
  pin `826f267850709889eaff128e40c5c79f4ae63a25`).
- JetKVM RV1106 system: <https://github.com/jetkvm/rv1106-system>
  (`project/app/aov_sample`, ветка `dev`,
  pin `ccd1c03732055518f85d7ccb1d3200056aad21ff`) — тот же сэмпл,
  blob-SHA совпадают; уникален только `RkLunch.sh`.
- Гайд Rockchip AOV V1.0.4 и выжимка dehai —
  `research/rv1106-aov/README.md`.
- AIGLASS_DEV_ENV (RV1106B/RV1103B IPC SDK V1.0.0, 2024-10-12):
  <https://github.com/makevary/AIGLASS_DEV_ENV> (`project/app/aov_sample`,
  `rk_docs/en`, ветка `main`,
  pin `1baceb45c22deb0a3d086ad5c128aeb18c6fae6f`). Чип и media другие —
  источник только для техник, не для кода.
- PDF-снимок документации AIGLASS — `research/rv1106-aov/README.md`
  (раздел `makevary/AIGLASS_DEV_ENV` в провенансе).

## Провенанс снимков

Дата снятия: 2026-11-25. Источник: LubanCat `main`
`826f267850709889eaff128e40c5c79f4ae63a25` (GitHub API `contents`/`raw`).
Лицензия репозитория: GitHub `license: null`; ниже — по заголовкам файлов.

| upstream-путь | локальный путь | лицензия/SPDX | sha256 | размер | дата | pin |
|---|---|---|---|---|---|---|
| `project/app/aov_sample/common/aov/sample_comm_aov.h` | `snapshot/project/app/aov_sample/common/aov/sample_comm_aov.h` | BSD-style (в шапке) | `508fd26a6fb93b1a6ee22c1725436c2f92abe89db35a1be81e1309e689765eec` | 2033 | 2026-11-25 | `826f2678` |
| `project/app/aov_sample/common/aov/sample_comm_aov_rv1106.c` | `snapshot/.../sample_comm_aov_rv1106.c` | BSD-style (в шапке) | `6a0fd3440e7563637463673facdef4b1a0de583693b1dd894856bcd18b3f78e7` | 45270 | 2026-11-25 | `826f2678` |
| `project/app/aov_sample/common/isp2.x/sample_comm_isp.c` | `snapshot/.../common/isp2.x/sample_comm_isp.c` | BSD-style (в шапке) | `210a521a6d0fd777be7ac41c767661dd1a14bf8d8f6476cf8144c08823cc80e3` | 40331 | 2026-11-25 | `826f2678` |
| `project/app/aov_sample/sample_aov_vi.c` | `snapshot/.../sample_aov_vi.c` | Apache-2.0 | `df6487bab3e55fc7439ebe4f2ec11f3c76af65396a0d5fc7c4446849e5f5a7ef` | 18332 | 2026-11-25 | `826f2678` |
| `project/app/aov_sample/sample_aov_vi_iva_venc.c` | `snapshot/.../sample_aov_vi_iva_venc.c` | Apache-2.0 | `167447e90bac5c421b3894645bb86788cd562e77086e166fc632b90a6060dc2a` | 42774 | 2026-11-25 | `826f2678` |
| `project/app/aov_sample/resume_suspend_test.sh` | `snapshot/.../resume_suspend_test.sh` | не указана | `52f70f176b990fa34d0f6ecdd9990d982c081e495ce15af31708899d2da043fd` | 12630 | 2026-11-25 | `826f2678` |
| `project/app/aov_sample/resume_suspend_test_2.sh` | `snapshot/.../resume_suspend_test_2.sh` | не указана | `6cb6ff7de700e6490316c349b3b3c2ecb5b1b302d5f5289ec6bbaa246c28811f` | 18959 | 2026-11-25 | `826f2678` |
| `project/app/aov_sample/RkLunch.sh` | `snapshot/.../RkLunch.sh` | не указана | `6483dee9940a85807c38a20086014dcaf6df9eca0a78e7ad935dd4d769d434e3` | 2717 | 2026-11-25 | `826f2678` |
| `project/app/aov_sample/Makefile` | `snapshot/.../Makefile` | не указана | `c631752905af121a75b33488ba0bf148e499857599ce107d0316628f1656ecc1` | 5814 | 2026-11-25 | `826f2678` |

### makevary/AIGLASS_DEV_ENV (pin `1baceb45…`)

Дата снятия: 2026-11-26. Источник: `main`
`1baceb45c22deb0a3d086ad5c128aeb18c6fae6f` (GitHub API `contents`/`raw`).
Корень репозитория — Apache-2.0 (`LICENSE`), но AOV-файлы несут собственную
BSD-style шапку (`sample_comm_aov*`), сэмплы — Apache-2.0, скрипты и
Makefile — без лицензии; SPDX ниже указан по шапке файла. Снимок read-only,
в сборку не входит.

| upstream-путь | локальный путь | лицензия/SPDX | sha256 | размер | дата | pin |
|---|---|---|---|---|---|---|
| `project/app/aov_sample/common/aov/sample_comm_aov.h` | `snapshot/aiglass-rv1106b/project/app/aov_sample/common/aov/sample_comm_aov.h` | BSD-style (в шапке) | `3fcb2046f6e35fb76a2401fd579be4922acb4bc67b6f5d64d5053bfc9f1fe05b` | 2296 | 2026-11-26 | `1baceb45` |
| `project/app/aov_sample/common/aov/sample_comm_aov.c` | `snapshot/aiglass-rv1106b/.../sample_comm_aov.c` | BSD-style (в шапке) | `6b0eef1987d5afaa40486e62a53b63742790cccbac54f6d8391512ca3e44ed0c` | 27767 | 2026-11-26 | `1baceb45` |
| `project/app/aov_sample/common/aov/sample_comm_aov_rv1106.c` | `snapshot/aiglass-rv1106b/.../sample_comm_aov_rv1106.c` | BSD-style (в шапке) | `7e7c92e2e99a56242e9a170016dc38a548b3e0f22327fc763f607c5d84967d8d` | 24394 | 2026-11-26 | `1baceb45` |
| `project/app/aov_sample/common/aov/sample_comm_aov_rv1106b.c` | `snapshot/aiglass-rv1106b/.../sample_comm_aov_rv1106b.c` | BSD-style (в шапке) | `62214255ec25aea5b4fdeb806f735182de2aa011412b57898c959dc103fb86fa` | 28007 | 2026-11-26 | `1baceb45` |
| `project/app/aov_sample/common/aov/sample_comm_aov_rv1126.c` | `snapshot/aiglass-rv1106b/.../sample_comm_aov_rv1126.c` | BSD-style (в шапке) | `d1136eccd36b5e1935d10be1aeeb5c59f49c806ee9630b0320e42db209787d13` | 21986 | 2026-11-26 | `1baceb45` |
| `project/app/aov_sample/common/aov/Makefile` | `snapshot/aiglass-rv1106b/.../common/aov/Makefile` | не указана | `1c4c7d6f24e5e97cce5b3845d47bc22965203c6e888ff7c0c46832e7968db2aa` | 118 | 2026-11-26 | `1baceb45` |
| `project/app/aov_sample/sample_aov_npu_test.c` | `snapshot/aiglass-rv1106b/.../sample_aov_npu_test.c` | Apache-2.0 | `b4133a4721e599612dbfbb00c6d7721a2008cb0aee429d4197e2554d8a6bebf3` | 9544 | 2026-11-26 | `1baceb45` |
| `project/app/aov_sample/sample_aov_aiisp_iva_venc.c` | `snapshot/aiglass-rv1106b/.../sample_aov_aiisp_iva_venc.c` | Apache-2.0 | `0e8f6367541c708034a35af88d13342818f4a07a65ebe50cf487b9e47380a6df` | 44983 | 2026-11-26 | `1baceb45` |
| `project/app/aov_sample/sample_aov_multi_vi_venc.c` | `snapshot/aiglass-rv1106b/.../sample_aov_multi_vi_venc.c` | Apache-2.0 | `f477802c3107fa335fcc718661a3d38a9ca01f5da26992187647e28d373613f4` | 33500 | 2026-11-26 | `1baceb45` |
| `project/app/aov_sample/sample_aov_bind_test.c` | `snapshot/aiglass-rv1106b/.../sample_aov_bind_test.c` | не указана | `2f20bfbbae61891ea0814775085467382e523868cae6e4788b05cb9269dab344` | 19639 | 2026-11-26 | `1baceb45` |
| `project/app/aov_sample/sample_aov_vi.c` | `snapshot/aiglass-rv1106b/.../sample_aov_vi.c` | Apache-2.0 | `0092a7f29bc939bbe9cd2406de917591226913a3ec435870a3b5429fcc8f6b49` | 25626 | 2026-11-26 | `1baceb45` |
| `project/app/aov_sample/Makefile` | `snapshot/aiglass-rv1106b/.../Makefile` | не указана | `107a9c255f7498f8636249b979d945c2168a1803eb233d015b9e8b439478bdc5` | 6915 | 2026-11-26 | `1baceb45` |
| `project/app/aov_sample/RkLunch.sh` | `snapshot/aiglass-rv1106b/.../RkLunch.sh` | не указана | `8b4d2a59b32c5e76e04bdb5561d9e53ac716fae39e27826289a9a8860058ea62` | 2569 | 2026-11-26 | `1baceb45` |
| `sysdrv/drv_ko/rockit/release_rockit-ko_rv1103b_arm/release_version.txt` | `snapshot/aiglass-rv1106b/sysdrv/drv_ko/rockit/release_rockit-ko_rv1103b_arm/release_version.txt` | не указана | `621e13376d1f2ad93fa84a82ae8cc66189caab42b375934f486a82ac7eb850c1` | 301 | 2026-11-26 | `1baceb45` |
| `project/cfg/BoardConfig_BatteryIPC/BoardConfig-SPI_NOR-NONE-RV1106B_EVB2_V10_V11-BAT_IPC_SC200AI_AOV.mk` | `snapshot/aiglass-rv1106b/project/cfg/BoardConfig_BatteryIPC/...SC200AI_AOV.mk` | не указана | `5f9129ddba88cb28b8cd047518391522c9b4201dff91065617f769858b92c9d0` | 3604 | 2026-11-26 | `1baceb45` |
| `project/cfg/BoardConfig_BatteryIPC/BoardConfig-SPI_NOR-NONE-RV1106B_EVB2_V10_V11-BAT_IPC_FASTBOOT.mk` | `snapshot/aiglass-rv1106b/project/cfg/BoardConfig_BatteryIPC/...BAT_IPC_FASTBOOT.mk` | не указана | `09274014c4f30557c33e321eaac68b81d96e7230f542f853e75ee10f4d82a3eb` | 3215 | 2026-11-26 | `1baceb45` |

## Как обновить снимок

```bash
BASE=https://raw.githubusercontent.com/LubanCat/RV06_03_Linux_SDK/826f267850709889eaff128e40c5c79f4ae63a25
# для каждого файла из таблицы:
curl -fsSL -o <локальный путь> "$BASE/<upstream-путь>"
sha256sum <локальный путь>   # сверить с таблицей
```

При смене pin обновить таблицу, дату и проверить, что blob-SHA JetKVM
по-прежнему совпадают (иначе снять отдельный снимок JetKVM).

AIGLASS снимается тем же способом из своего pin:

```bash
AIGLASS=https://raw.githubusercontent.com/makevary/AIGLASS_DEV_ENV/1baceb45c22deb0a3d086ad5c128aeb18c6fae6f
curl -fsSL -o <локальный путь> "$AIGLASS/<upstream-путь>"   # пути — во второй таблице
sha256sum <локальный путь>
```

Проверка после обновления: `git grep -l aiglass -- '*.mk' 'Makefile' '*/Makefile'`
должен быть пустым — снимки не участвуют в сборке. PDF-часть AIGLASS
(`rk_docs/en`) лежит в теме `research/rv1106-aov/`, чтобы не смешивать
документацию и код.