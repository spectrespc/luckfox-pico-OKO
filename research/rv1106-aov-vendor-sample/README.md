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

## Как обновить снимок

```bash
BASE=https://raw.githubusercontent.com/LubanCat/RV06_03_Linux_SDK/826f267850709889eaff128e40c5c79f4ae63a25
# для каждого файла из таблицы:
curl -fsSL -o <локальный путь> "$BASE/<upstream-путь>"
sha256sum <локальный путь>   # сверить с таблицей
```

При смене pin обновить таблицу, дату и проверить, что blob-SHA JetKVM
по-прежнему совпадают (иначе снять отдельный снимок JetKVM).