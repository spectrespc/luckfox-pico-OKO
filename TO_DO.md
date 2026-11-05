# TO_DO: фокус/автофокус и видеоаналитика на NPU (RV1106 + SC230AI)

Статус: **только план, код и конфигурация не изменялись.**
Дата: 2026-11-05.

Состав файла: §1–§10 — фокус и автофокус; §11 — видеоаналитика на NPU
(RKNN + YOLOv5n), события с кадрами и управление моделями через WebUI.

## 1. Цель

Добавить в OKO управление фокусировкой и автофокусом для установленного
моторизованного объектива. Объектив будет подключён к внешнему контроллеру
**STM8S103F3P6** (I2C-слейв), который управляет шаговыми приводами фокуса и
зума. Наработки по прошивке STM8 находятся в репозитории:
<https://github.com/spectrespc/stm8s103_i2c_motor>.

Сейчас объектив физически установлен, но к STM8/плате ещё не подключён;
драйвера и DTS-описания линзы в Linux-части нет.

Отдельно нужно попробовать **два режима фокусировки**:

1. **по всему кадру** — критерий резкости агрегируется по всей площади кадра;
2. **по выделенному участку (ROI)** — критерий считается только по заданному
   прямоугольнику (например, по объекту интереса или по области, выбранной
   пользователем в WebUI).

## 2. Исходное состояние (проверено по репозиторию)

| Факт | Где смотреть |
|---|---|
| Активная плата: Pico Pro Max, RV1106, SPI NAND, SC230AI, приложение `divinus` | `.BoardConfig.mk`, `TESTBENCH.md` |
| В DTS сенсор описан как модуль `CMK-OT2115-PC1` с объективом `30IRC-F16`, узла VCM и `lens-focus` нет | `sysdrv/source/kernel/arch/arm/boot/dts/rv1106-luckfox-pico-pro-max-ipc.dtsi:228-255` |
| Драйвер `sc230ai` не умеет управлять линзой: только exposure/gain/vblank/flip/test-pattern | `sysdrv/source/kernel/drivers/media/i2c/sc230ai.c:1883-1926` |
| В IQ `af_v31` есть, но `contrast_af.enable = 0` (оба сенсорных режима) | `media/isp/release_camera_engine_rkaiq_rv1106_*/isp_iqfiles/sc230ai_CMK-OT2115-PC1_30IRC-F16.json:7905-7906, 15741-15742` |
| AF включён в сборку rkaiq | `media/isp/Makefile:107` (`-DRKAIQ_ENABLE_AF=ON`) |
| `rkipc` уже вызывает AF-функции AIQ (значит, символы в `librkaiq.so` есть) | `project/app/rkipc/rkipc/common/isp/rv1106/isp.c:1568-1688` |
| `divinus` (приложение по умолчанию) AF не умеет; его AIQ-обёртка — таблица `dlsym` | `project/app/divinus/source/src/hal/plus/rk_aiq.h:50-89`; `doc/endpoints.md:39-56` |
| `motor.ko` — шаговик 28BYJ-48 для pan/tilt, к фокусу не относится и выключен | `sysdrv/drv_ko/motor/ReadMe`, `TESTBENCH.md:26,325` |
| Есть внешний контроллер STM8S103F3P6 «motor»; в текущем `oko-ec` он описан на I2C `0x39` и прошивается через `oko-ec motor` | `project/app/oko-ec/README.md`, `project/app/oko-ec/source/main.c:8,978-981` |
| Объектив будет подключён к STM8 (два шаговых мотора: focus + zoom, драйверы DRV8833) | этот файл, §3, §4.5 |
| Прошивка STM8: `https://github.com/spectrespc/stm8s103_i2c_motor`, I2C-адрес `0x60` (см. §4.5) | внешний репозиторий |

## 3. Привод: STM8S103F3P6 + шаговые моторы

Архитектура уже выбрана: объектив подключается к STM8S103F3P6, а RV1106
управляет им по I2C. Это снимает вопрос о VCM/DC-приводе — используется
**вариант B** (свой AF по статистике rkaiq).

Для Linux-части не нужны ни VCM-драйвер, ни DTS-узел линзы: достаточно
рабочей I2C-шины и клиента STM8 в приложении. AF-модуль rkaiq используется
только как поставщик статистики (`RKAIQ_AF_MODE_FIXED` + `manual_meascfg_v31`),
а не как драйвер привода.

Что известно из репозитория прошивки
(`https://github.com/spectrespc/stm8s103_i2c_motor`, MIT):

- два биполярных шаговых мотора — **focus и zoom**, каждый через свой
  DRV8833 H-bridge, full-step с включёнными обеими обмотками;
- I2C-слейв, адрес **`0x60`**, 100 кГц;
- генерация шагов — 1 мс DDA в прерывании TIM4, диапазон 1–5000 шагов/с;
- IWDG ~500 мс, сохранение позиции в EEPROM с CRC16;
- контроль `nFAULT` DRV8833 (перегрузка/перегрев/UVLO) с защёлкой ERROR и
  отпусканием мотора;
- HSI 16 МГц, без внешнего кварца;
- прошивка: SDCC ≥ 4.0 + STM8-SPL-SDCC, сборка `make` → `build/firmware.ihx`,
  прошивка через ST-LINK/v2 (`make flash`).

Что осталось уточнить:

- [ ] какой из двух моторов — фокус, какой — зум (идентификаторы в протоколе);
- [ ] число шагов на полный ход объектива, направление, наличие упоров;
- [ ] требуется ли калибровка нуля/диапазона при первом включении;
- [ ] адрес на шине OKO: в прошивке `0x60`, в текущем `oko-ec` описан `0x39` —
      проверить и привести к одному значению;
- [ ] шина I2C на OKO: по `oko-ec/README.md` это `/dev/i2c-3` (100 кГц);
- [ ] подтяжки PB4/PB5 по 4.7 кОм к VDD — обязательны;
- [ ] питание DRV8833 (`VM` 2.7–10.8 В) и общий GND с RV1106;
- [ ] прошивка: новый репозиторий использует SWIM/ST-LINK; I2C-загрузчик
      старого `oko-ec motor` в нём не описан — уточнить способ обновления.

## 3.1. Протокол STM8 (что понадобится приложению)

Полное описание — в репозитории: `docs/PROTOCOL.md`, карта выводов —
`docs/HARDWARE.md`. Проверенные примеры из README:

```sh
i2cdetect -y 7
i2ctransfer -y 7 w1@0x60 0xFF r3              # версия прошивки
i2ctransfer -y 7 w4@0x60 0x02 0x00 0x01 0xF4  # скорость
i2ctransfer -y 7 w4@0x60 0x01 0x00 0x00 0x64  # move focus +100
i2ctransfer -y 7 w2@0x60 0x04 0x00 r4         # позиция focus
i2ctransfer -y 7 w2@0x60 0x05 0x00 r1         # статус focus
```

Номер шины `7` — из примера репозитория; на OKO, вероятно, `/dev/i2c-3`
(уточнить по DTS/`i2cdetect`). Байтовый порядок и точные размеры ответов —
только по `docs/PROTOCOL.md`.

Ограничения прошивки (важно для алгоритма AF):

- **open-loop**: позиция только программная, энкодера нет; пропуск шагов
  ловится лишь по `nFAULT`, а не по потере позиции;
- нет разгона/торможения (acceleration ramp);
- движение игнорируется, пока не задана скорость (сначала команда скорости);
- запись EEPROM throttled ≤ 1 Гц и не выполняется во время движения.

Из этого следует: AF-алгоритм должен работать малыми шагами, с паузами на
стабилизацию и повторной проверкой FV, а не одним длинным перемещением.

## 4. Что уже готово в SDK и можно использовать

### 4.1. Штатный AF-модуль rkaiq (uAPI2)

Заголовки: `media/isp/release_camera_engine_rkaiq_rv1106_arm-rockchip830-linux-uclibcgnueabihf/include/rkaiq/uAPI2/`.

| Возможность | API | Файл/строки |
|---|---|---|
| Режим AF: auto / semi-auto / manual | `rk_aiq_uapi2_setFocusMode/getFocusMode`, `opMode_t` (`OP_AUTO`, `OP_MANUAL`, `OP_SEMI_AUTO`) | `rk_aiq_user_api2_imgproc.h:575-591`, `rk_aiq_user_api_common.h:10-16` |
| Ручная позиция фокуса | `rk_aiq_uapi2_setFocusPosition/getFocusPosition` | `rk_aiq_user_api2_imgproc.h:601-602` |
| Окно фокуса | `rk_aiq_uapi2_setFocusWin/getFocusWin` | `:612-613` |
| Lock/unlock AF | `rk_aiq_uapi2_lockFocus/unlockFocus` | `:624-625` |
| Oneshot / ручной триггер / tracking | `oneshotFocus`, `manualTrigerFocus`, `trackingFocus` | `:636-658` |
| Траектория и результат поиска | `getSearchPath`, `getSearchResult` | `:669-680` |
| Диапазоны фокуса и зума | `getFocusRange`, `getZoomRange` | `rk_aiq_user_api2_af.h:62-64` |
| Полный AF-attrib | `rk_aiq_user_api2_af_SetAttrib/GetAttrib` | `rk_aiq_user_api2_af.h:29-32` |
| Текущая позиция VCM | `rk_aiq_user_api2_af_GetFocusPos` | `:70` |
| Конфиг VCM (токи, шаг) | `rk_aiq_user_api2_af_SetVcmCfg/GetVcmCfg` | `:54-56` |
| Ручная настройка измерения AFM | поле `manual_meascfg_v31` в `rk_aiq_af_attrib_t` | `algos/af/rk_aiq_types_af_algo_int.h:49-74` |
| Режимы AF | `RKAIQ_AF_MODE_*` (`AUTO`, `MACRO`, `INFINITY`, `FIXED`, `CONTINUOUS_VIDEO`, `CONTINUOUS_PICTURE`, …) | `algos/af/rk_aiq_types_af_algo_int.h:27-38` |

### 4.2. AF-статистика для собственного алгоритма

| Возможность | API |
|---|---|
| Получить 3A-статистику (неблокирующе/блокирующе) | `rk_aiq_uapi2_sysctl_get3AStats`, `get3AStatsBlk`, `release3AStatsRef` (`rk_aiq_user_api2_sysctl.h:390-426`) |
| Структура кадра | `rk_aiq_isp_stats_t` (`common/rk_aiq_types.h:475-490`) |
| Поля AF для RV1106 (AF HW V31) | `af_stats_v3x`: `wnda_fv_h1/h2`, `wnda_fv_v1/v2`, `wnda_luma`, `wina_highlit_cnt`, `wndb_luma`, `wndb_sharpness`, `sof_tim`, `focusCode`, `zoomCode` (`algos/af/rk_aiq_types_af_algo.h:117-139`) |
| Размер карты | 15×15 = 225 (`RKAIQ_RAWAF_SUMDATA_NUM`, `:15`) |

### 4.3. Готовые примеры использования

| Пример | Что показывает |
|---|---|
| `.../rkisp_demo/demo/sample/sample_af_module.cpp:20-140` | полный сценарий: режимы, окно, lock/unlock, oneshot, фокус/зум, search result |
| `.../rkisp_demo/demo/af_algo_demo/third_party_af_algo.cpp:198-343` | настройка `manual_meascfg_v31` и `AfMode = RKAIQ_AF_MODE_FIXED` |
| `.../third_party_af_algo.cpp:497-627` | разбор AF-статистики (карта 15×15) |
| `.../third_party_af_algo.cpp:640-680` | поток: `get3AStatsBlk` → свой алгоритм → «move focus motor» |

### 4.4. Готовые внешние интерфейсы (можно переиспользовать)

| Место | Что уже есть |
|---|---|
| `rkipc` `common/isp/rv1106/isp.c:1568-1688` | `rk_isp_get/set_af_mode`, `rk_isp_get_focus_level`, `rk_isp_af_focus_in/out`, `af_zoom_in/out` |
| `rkipc` docs `docs/Rockchip_Developer_Guide_Linux_RKIPC_EN.md:929-936` | список IPC-команд AF |
| `ipcweb` `src/image_api.cpp:107-113,335` | REST-поля `sAFMode`, `iZoomLevel`, `iFocusLevel` |
| `divinus` `src/hal/plus/rk_aiq.h` | сюда добавляются `dlsym`-обёртки AF |
| `oko-ec` | I2C-доступ к STM8 (в текущих доках `0x39`) и цель `motor` для прошивки; требует сверки с новой прошивкой `0x60` |

### 4.5. Прошивка STM8 (внешний репозиторий)

- Репозиторий: <https://github.com/spectrespc/stm8s103_i2c_motor> (MIT).
- README: <https://github.com/spectrespc/stm8s103_i2c_motor/blob/main/README.md>,
  русская версия — `README.ru.md`.
- Протокол: <https://github.com/spectrespc/stm8s103_i2c_motor/blob/main/docs/PROTOCOL.md>.
- Железо/распиновка: <https://github.com/spectrespc/stm8s103_i2c_motor/blob/main/docs/HARDWARE.md>.
- Сборка: `make clean && make` → `build/firmware.ihx`; прошивка `make flash`
  (STM8-SPL-SDCC, SDCC ≥ 4.0, ST-LINK/v2).
- Хостовые тесты протокола: `make -C tests test`.
- В OKO пока не вендорен: прошивка и её тесты лежат отдельно; при интеграции
  нужно решить, копировать ли исходники в `project/app/` или оставить внешнюю
  зависимость с фиксацией коммита.

## 5. Варианты реализации

### Вариант A. VCM на I2C → штатный автофокус rkaiq

На текущем железе **не применяется**: привод шаговый, через STM8.
Оставлен как справка на случай замены объектива на VCM-модуль.

Когда подходит: объектив с VCM-приводом (dw9714/ak7375/vm149c/dw9768/…).

- [ ] включить драйвер VCM во фрагменте ядра `rv1106-sc230ai.config`
      (`CONFIG_VIDEO_DW9714=m` и т.п.);
- [ ] добавить в активный DTS узел VCM на свободный I2C и связать с сенсором
      через `lens-focus = <&vcm>` (пример: `rk3288-evb-android-rk808-edp-avb.dts:63-88`);
- [ ] сохранить имя IQ-файла/модуля: от `rockchip,camera-module-name` и
      `rockchip,camera-module-lens-name` зависит поиск IQ (сейчас
      `BoardConfig...SC230AI.mk:93` ставит `sc230ai_CMK-OT2115-PC1_30IRC-F16.json`);
- [ ] в IQ для SC230AI включить `af_v31.TuningPara.contrast_af.enable = 1`
      и проверить `fixed/macro/infinity` коды;
- [ ] проверить `getFocusRange`, `oneshotFocus`, `getSearchResult` на стенде;
- [ ] настроить токи через `SetVcmCfg`, если потребуется.

Плюсы: минимум своего кода, штатные режимы auto/semi-auto/manual, окно, lock,
oneshot, tracking. Минусы: требуется именно VCM и правка IQ.

### Вариант B. STM8 + шаговые моторы → свой AF (выбранный)

Объектив будет подключён к STM8S103F3P6 (`0x60`); Linux-сторона общается с
ним по I2C и не управляет обмотками напрямую.

- [ ] подключить STM8 к I2C OKO (`/dev/i2c-3`, 100 кГц), проверить
      `i2cdetect` по адресу `0x60` (или `0x39`, см. §3);
- [ ] зафиксировать протокол в приложении: `move`, `set speed`, `get position`,
      `get status`, `version` (`docs/PROTOCOL.md` репозитория);
- [ ] прогнать оба мотора по краям, определить направление и полный ход,
      зафиксировать безопасные границы;
- [ ] в приложении включить AFM-измерение:
      `rk_aiq_user_api2_af_SetAttrib` с `AfMode = RKAIQ_AF_MODE_FIXED` и
      заполненным `manual_meascfg_v31` (по образцу `third_party_af_algo.cpp`);
- [ ] реализовать поиск по `wnda_fv_*` (hill-climbing/adaptive) **малыми
      шагами** с паузами: прошивка open-loop, без разгона и энкодера;
- [ ] критерий остановки — `StableThers/StableFrames` или свои пороги,
      плюс таймаут и проверка `nFAULT`/статуса;
- [ ] хранить позицию в приложении и/или читать из EEPROM STM8;
- [ ] опционально логировать `getSearchPath`, чтобы снимать AF-кривые.

Плюсы: готовый привод фокуса и зума, гальванически развязанная механика,
не требует VCM и правок IQ. Минусы: open-loop без обратной связи, нет
разгона, EEPROM-запись ≤ 1 Гц, адрес/протокол надо сверить с текущим
`oko-ec`, а безопасность упоров и калибровка — на приложении.

### Вариант C. Без привода → только метрика резкости

Использовать AF-статистику как индикатор качества фокуса: focus assist,
контроль сборки/юстировки, выбор лучшего кадра. Полезно как промежуточный шаг
и для отладки варианта B.

### 5.1. Два режима фокусировки: весь кадр и ROI (эксперимент)

Нужно реализовать и сравнить оба режима; ROI — обязательная функция, а не
замена полному кадру.

**Общая идея**

- AFM считает карту резкости 15×15 (`wnda_fv_h1/h2/v1/v2`) по окну измерения.
  Критерий фокуса — агрегат по выбранной области: сумма/среднее/медиана FV,
  при необходимости с весами и отбрасыванием пересвеченных окон
  (`wina_highlit_cnt`).
- Весь кадр: окно измерения = весь сенсорный кадр (или агрегат по всем 225
  окнам).
- ROI: окно измерения = прямоугольник ROI; агрегат только по окнам,
  попавшим в ROI (или по всей карте, если окно уже выставлено в ROI).

**Что использовать**

| Задача | Инструмент |
|---|---|
| Задать окно для встроенного AF | `rk_aiq_uapi2_setFocusWin/getFocusWin` (`paRect_t`) |
| Задать окно для своего AFM | `manual_meascfg_v31.wina_h_offs/v_offs/h_size/v_size` (и `winb_*` при двух окнах) |
| Получить карту резкости | `get3AStatsBlk` → `af_stats_v3x.wnda_fv_*`, `wnda_luma` |
| Ограничить поиск/сбросить окно | `setFocusWin` с нулевым прямоугольником = default |
| Ограничить диапазон хода | свои границы из §3 + `getFocusRange` (если появится VCM) |

**Что учесть**

- [ ] система координат: окно AFM задаётся в координатах сенсорного кадра,
      а ROI из WebUI/ONVIF — в координатах стрима; нужен пересчёт с учётом
      crop/ePTZ, масштаба и зеркала/флипа (`setMirrorFlip`);
- [ ] ROI меньше одного окна 15×15: агрегат вырождается в 1–2 окна, FV шумный —
      задать минимальный размер ROI и/или расширять его;
- [ ] ROI у края кадра: проверить, что окно не выходит за границы сенсора;
- [ ] при смене сценария/разрешения окно нужно пересчитывать;
- [ ] для «всего кадра» проверить, что края не портят FV (виньетка, IR-засветка,
      OSD/маска) — при необходимости отбрасывать краевые окна;
- [ ] сравнить оба режима на одном сюжете: точность, число шагов, время
      поиска, устойчивость к шуму и подсветке.

**Критерии приёмки эксперимента**

- [ ] полный кадр: AF сходится на типовых сценах (лицо/текст/пейзаж);
- [ ] ROI: AF сходится на объекте в ROI, даже если фон резче объекта;
- [ ] смена ROI не требует перезапуска приложения;
- [ ] оба режима укладываются в заданный таймаут и не гоняют мотор в упор;
- [ ] результаты и AF-кривые сохранены в `output/bench/` для сравнения.

## 6. План по фазам

### Фаза 0. Исследование (без изменений в коде)

- [ ] идентифицировать объектив и подключение к STM8 (см. §3);
- [ ] снять `media-ctl -p`, `v4l2-ctl --list-ctrls`, `ls /dev/v4l-subdev*`;
- [ ] убедиться, что в прошивке есть символы AF:
      `nm -D /oem/usr/lib/librkaiq.so | grep -E 'user_api2_af_|Focus'`;
- [ ] проверить шину и адрес STM8: `i2cdetect -y 3` (ожидается `0x60`,
      в старых доках `oko-ec` — `0x39`);
- [ ] прошить/проверить версию STM8: `i2ctransfer ... 0xFF r3`.

### Фаза 1. Прототип управления приводом

- [ ] собрать прошивку STM8 из внешнего репозитория (`make`, `make flash`);
- [ ] стенд: обмен по I2C из `i2ctransfer`, затем своя обёртка в приложении;
- [ ] проверить `set speed` + `move`, `get position`, `get status`, `nFAULT`;
- [ ] прогнать оба мотора по краям, измерить ход, скорость, ток;
- [ ] зафиксировать безопасные границы и реализовать защиту/таймауты.

### Фаза 2. AF-статистика

- [ ] включить AFM через `af_SetAttrib` (`RKAIQ_AF_MODE_FIXED`);
- [ ] снять зависимость `FV` от позиции привода (AF-кривая);
- [ ] проверить поведение при разной освещённости и с IR-подсветкой;
- [ ] реализовать агрегат FV по всему кадру;
- [ ] реализовать агрегат FV по ROI (окно `wina_*` и/или фильтрация карты);
- [ ] проверить пересчёт координат ROI: стрим → сенсор, crop/ePTZ, mirror/flip.

### Фаза 3. Алгоритм

- [ ] поиск (coarse + fine), окно фокуса, гистерезис, таймауты;
- [ ] режимы: ручной, oneshot, continuous (по необходимости);
- [ ] режим «весь кадр» и режим «ROI» с переключением на лету;
- [ ] валидация ROI: минимальный размер, границы, пересчёт при смене
      разрешения/сценария;
- [ ] хранение последней позиции: EEPROM STM8 (CRC16, запись ≤ 1 Гц) +
      дублирование в конфиге приложения;
- [ ] восстановление после перезагрузки и после `nFAULT`/WDT-сброса STM8.

### Фаза 4. Интеграция и API

- [ ] добавить AF-функции в `divinus` (`src/hal/plus/rk_aiq.h` + `rk_hal.c`);
- [ ] добавить endpoint(ы) в `divinus` (рядом с `/api/isp`, см. `doc/endpoints.md`);
- [ ] обновить `FEATURES.md`, `TESTBENCH.md` (если меняется стенд), `CHANGELOG.md`;
- [ ] прогнать проверки на стенде и зафиксировать статус.

## 7. Интеграция в приложение (`divinus`)

Точки расширения:

| Файл | Что менять |
|---|---|
| `project/app/divinus/source/src/hal/plus/rk_aiq.h` | добавить указатели `fnSetFocusMode`, `fnGetFocusMode`, `fnSetFocusPosition`, `fnGetFocusPos`, `fnOneshotFocus`, `fnGetAfStats` и `dlsym`-загрузку |
| `project/app/divinus/source/src/hal/plus/rk_hal.c` | реализовать обёртки над `_rk_aiq_ctx`, проверять `_rk_aiq_ctx` перед вызовом |
| `project/app/divinus/source/src/...` (I2C) | клиент STM8: `/dev/i2c-3`, команды `set speed` / `move` / `get position` / `get status` / `version`, проверка `nFAULT` |
| `project/app/divinus/source/src/...` (HTTP) | новый endpoint, например `/api/focus` с `mode`, `position`, `oneshot` и `roi` (`x,y,w,h`); `roi=null` — весь кадр |
| `project/app/divinus/source/doc/endpoints.md` | описать новый endpoint и коды ошибок |

Замечания:

- `divinus` использует `dlsym`, поэтому отсутствие AF-символов в конкретной
  сборке `librkaiq.so` не должно ронять приложение — функция опциональна.
- В `divinus` нет UI/логики AF; ONVIF `videosources.xml` лишь статически
  объявляет `AutoFocusMode=AUTO`.

## 8. Проверки на стенде

```sh
# сенсор и субдевайсы
media-ctl -p
v4l2-ctl --list-ctrls --device /dev/v4l-subdev0   # и остальные subdev

# символы AF в библиотеке
nm -D /oem/usr/lib/librkaiq.so | grep -E 'user_api2_af_|uapi2_(set|get)Focus|oneshotFocus'

# текущее состояние (для rkipc-ветки)
#   rk_isp_get_af_mode / rk_isp_set_af_mode / rk_isp_get_focus_level
#   через socket-server; для divinus — после добавления endpoint

# IQ и модуль
cat /proc/rkisp-vir0
ls /oem/usr/share/iqfiles

# STM8 (шина/адрес уточнить: 0x60 в прошивке, 0x39 в старом oko-ec)
i2cdetect -y 3
i2ctransfer -y 3 w1@0x60 0xFF r3              # версия прошивки
i2ctransfer -y 3 w2@0x60 0x05 0x00 r1         # статус focus
```

Ожидаемое поведение до подключения привода: AF-вызовы не падают, но позиция
фокуса не меняется, `getFocusRange` вырожден, `GetFocusPos` бесполезен.

## 9. Риски и ограничения

- **IQ привязан к имени модуля/объектива.** Смена `camera-module-lens-name`
  без переименования IQ или без нового IQ ломает загрузку тюнинга
  (`BoardConfig...SC230AI.mk:93`, `/proc/rkisp-vir0`).
- **Встроенный contrast AF выключен** в IQ SC230AI; для варианта A его нужно
  включить и проверить на реальном объективе.
- **PDAF нет**, HDR2/HDR3 на SC230AI заблокированы (`FEATURES.md:51`) —
  AF-статистика в HDR не проверена.
- **ROI и координаты.** Окно AFM задаётся в координатах сенсорного кадра, а
  ROI из WebUI/ONVIF — в координатах стрима; без пересчёта (crop/ePTZ,
  масштаб, mirror/flip) фокус будет искать не в той области.
- **Малый ROI.** При размере меньше окна 15×15 FV становится шумным; нужен
  минимальный размер ROI и/или расширение окна с весами.
- **`motor.ko` — не фокус.** Это pan/tilt 28BYJ-48; переиспользовать можно
  только как пример GPIO-драйвера шаговика, но не как готовый фокус-драйвер.
- **STM8: open-loop без энкодера.** Позиция программная; пропуск шагов
  ловится только по `nFAULT`, реального положения нет. AF должен проверять FV
  после каждого перемещения, а не доверять счётчику шагов.
- **STM8: нет разгона.** Длинные перемещения на большой скорости могут
  срываться; для AF использовать малые шаги и умеренную скорость.
- **STM8: порядок команд.** `move` игнорируется, пока не задана скорость
  (сначала `set speed`), см. README репозитория.
- **STM8: EEPROM.** Запись ≤ 1 Гц и только в покое; не сохранять позицию на
  каждый шаг.
- **Адрес и протокол.** Прошивка использует `0x60`, текущие доки `oko-ec` —
  `0x39`; до интеграции привести к одному значению и обновить `oko-ec`.
- **Питание и механика.** Обмотки идут через DRV8833 (`VM` 2.7–10.8 В);
  нужны общий GND с RV1106, подтяжки I2C 4.7 кОм на PB4/PB5 и защита
  механических упоров (программные границы + `nFAULT`).
- **Ресурсы RV1106.** При выбранной схеме моторы висят на STM8, поэтому
  PWM/GPIO для них не нужны. Если всё же пойдём по прямому GPIO/PWM-пути:
  PWM10/PWM11 заняты IR/белой подсветкой (`sc230ai_init.c:41-64`), свободные
  каналы проверять в DTS.
- **Стенд.** Все работы с железом — по `TESTBENCH.md`; артефакты — в
  `output/bench/`, скрипты — в `bench/`.

## 10. Справочные пути

- AF API: `media/isp/release_camera_engine_rkaiq_rv1106_*/include/rkaiq/uAPI2/rk_aiq_user_api2_af.h`,
  `rk_aiq_user_api2_imgproc.h`, `rk_aiq_user_api2_sysctl.h`
- Типы AF: `.../include/rkaiq/algos/af/rk_aiq_types_af_algo.h`,
  `rk_aiq_types_af_algo_int.h`, `.../include/rkaiq/common/rk_aiq_types.h`
- Демо: `.../rkisp_demo/demo/sample/sample_af_module.cpp`,
  `.../rkisp_demo/demo/af_algo_demo/third_party_af_algo.cpp`
- Драйверы VCM: `sysdrv/source/kernel/drivers/media/i2c/{dw9714,vm149c,ak7375,dw9768,dw9763,gt9760s,cn3927v,fp5510,aw8601}.c`
- Драйвер сенсора: `sysdrv/source/kernel/drivers/media/i2c/sc230ai.c`
- DTS: `sysdrv/source/kernel/arch/arm/boot/dts/rv1106-luckfox-pico-pro-max-ipc.dtsi`
- IQ SC230AI: `media/isp/release_camera_engine_rkaiq_rv1106_*/isp_iqfiles/sc230ai_CMK-OT2115-PC1_30IRC-F16.json`
- Приложение по умолчанию: `project/app/divinus/source/`
- Шаговик/STM8: `sysdrv/drv_ko/motor/`, `project/app/oko-ec/`
- Прошивка STM8 (внешняя): <https://github.com/spectrespc/stm8s103_i2c_motor>
  - README: `README.md` / `README.ru.md`
  - протокол: `docs/PROTOCOL.md`
  - железо/распиновка: `docs/HARDWARE.md`
  - сборка/прошивка: `make`, `make flash` (SDCC ≥ 4.0, ST-LINK/v2)
- Документация проекта: `STRUCTURE.md`, `INIT_ORDER.md`, `TESTBENCH.md`, `FEATURES.md`

## 11. Видеоаналитика на NPU (RKNN + YOLOv5n)

Статус: только план, код не изменялся. Задача: детекция объектов YOLOv5n на NPU,
события с несколькими кадрами до/после детекции, сохранение текущего OSD в RTSP,
чистый JPEG в событиях и управление моделями из WebUI.

### 11.1. Исходное состояние (проверено по репозиторию и стенду)

| Факт | Где смотреть |
|---|---|
| NPU включён в DTS: `&npu { status = "okay"; }` | `sysdrv/source/kernel/arch/arm/boot/dts/rv1106-evb.dtsi:55-57` (подключается активным DTS) |
| Оверрайд частоты NPU 700 МГц закомментирован → работает 420 МГц | `sysdrv/source/kernel/arch/arm/boot/dts/rv1106g-luckfox-pico-pro-max-sc230ai.dts:35-38` |
| Драйвер: `CONFIG_ROCKCHIP_RKNPU=m`, `rknpu.ko` в образе, грузится при старте | `sysdrv/source/kernel/arch/arm/configs/luckfox_rv1106_linux_defconfig:459-462`, `sysdrv/drv_ko/insmod_ko.sh:77` |
| Версия драйвера — **0.9.2 от 20230825**; NPU работает **без IOMMU** | `drivers/rknpu/include/rknpu_drv.h:33-36`; лог стенда: `RKNPU driver: v0.9.2 for 20230825`, `iommu device-tree entry not found!, using non-iommu mode` (`output/bench/boot-20261104-174307.log:591-592`) |
| Runtime: `librknnmrt.so` (mini) уже в образе | `/oem/usr/lib`; источник `media/iva/iva/librockiva/rockiva-rv1106-Linux/lib/` |
| AIISP использует rknn и висит на VPSS-группе | `librkpostisp.so` + `rkmodel_1080_1920.aiisp`; `rk_hal.c:1198-1222`, троттлинг fps `rk_hal.c:1169-1196` |
| Приложение по умолчанию — `divinus` (rkipc — фолбэк) | `output/out/oem/usr/bin/RkLunch.sh:80-88`; лог стенда: `Divinus (rev bca1932e)` |
| Divinus использует только VI chn0 (`rkisp_mainpath`); selfpath свободен | `hal/plus/rk_hal.c:295-313`; ISP32 регистрирует mainpath+selfpath (`capture_v32.c:2189-2243`) |
| VPSS-каналов всего 4; IVS занимает последний | `media/rockit/rockit/mpi/sdk/include/rk_defines.h:77`; `doc/config.md` (divinus) |
| RGN-overlay можно вешать только на VENC/VPSS; на VI — только COVER на весь pipe | `test_mpi_rgn.cpp:808-809, 854-863`; `rkipc .../rv1106_ipc/video/video.c:2877-2896` |
| OSD сейчас только на VENC main; multi-attach заблокирован гейтом G4 | `files/divinus.yaml:29`; `rk_hal.c:376-396, 484-486`; `gating.c:139-144`; `media.c:1145-1151` |
| События: одна JPEG на событие + OSD-индикатор + запись клипа | `ivs_events.c:53-75`, кулдаун 5 с (`:17`) |
| Снимок — полноразмерный JPEG через отдельный VENC-канал (1920×1080), регионов на нём нет | `jpeg.c:8-81`; `rk_hal.c:796-865`; `jpeg_get()` → `GetStream` |
| Отправка события: один `PUT` с `Content-Type: image/jpeg` | `http_post.c:5-57` |
| WebUI уже умеет multipart-загрузку файла (`/api/osd/<id>`) | `server.c:1519-1555`; лимит буфера запроса 4 MB (`server.c:10`) |
| Память: DDR3 256 MB, ядру 184 MB, CMA 67.5 MB | лог стенда `boot-20261104-174307.log:225` |
| `/userdata` — 10 MB, rw UBIFS; rootfs 210 MB только на чтение | cmdline `10M(userdata)` (`boot log:221`); монтирование (`boot log:486`) |
| rkipc содержит готовый NPU-путь (RockIVA) как референс: VI-канал, `npu_fps`, OSD боксов | `project/app/rkipc/rkipc/src/rv1106_ipc/video/video.c:537-566, 1889-1998, 2057-2068` |
| `rk_smart_door` (RKNN-демо лиц) не собирается при `RK_APP_TYPE=RKIPC_RV1106` | `project/app/rk_smart_door/Makefile:17-25` |

Проверено также, что `rockchip-linux/rknpu` (старый репозиторий) к RV1106 не
относится: он для RK1808/RV1126 (galcore), для RV1106 нужен `rknpu2`.

### 11.2. Принятые решения

- Версию драйвера/runtime **не меняем** (0.9.2 ↔ runtime 1.5.2): AIISP и RockIVA
  — предсобранные библиотеки, согласованные с этой парой.
- Детекция YOLO — на **VI chn1 (selfpath)** в `divinus`; IVS остаётся на VPSS.
- Событийный кадр — из **JPEG VENC-канала** (полный размер, без OSD).
- OSD остаётся **только на VENC main**; гейт G4 не открывать, регионы на JPEG
  не вешать — иначе событийный JPEG станет грязным.
- Пре-ролл/пост-ролл — кольцо кадров в RAM; кодирование буферизованных кадров —
  `RK_MPI_VENC_SendFrame` в несвязанный VENC-канал, один MMZ-блок.
- Модели — только предкомпилированные `.rknn` под runtime 1.5.2; хранение в
  `/userdata/models` (2–3 модели), управление через WebUI.

### 11.3. Открытые вопросы (требуют решения или замера)

- [ ] AIISP + YOLO: делёж NPU (0.5 TOPS). Согласовать `aiisp.fps_enabled`
      (сейчас 30) и `npu_fps` YOLO; замерить фактический fps и загрузку NPU.
- [ ] YOLO на chn1 **не видит AIISP-кадры** (обработка в VPSS-группе). Оценить
      ночную точность; альтернатива — YOLO на VPSS chn3 с отключением IVS.
- [ ] Лицензия YOLOv5 (GPL-3.0): нужна коммерческая лицензия либо
      Apache-2.0-альтернатива (YOLOX, NanoDet) для закрытого продукта.
- [ ] Хранение моделей: `/userdata` всего 10 MB. Решить — microSD (`/mnt/sdcard`)
      или увеличение раздела; больше 2–3 моделей в `/userdata` не поместится.
- [ ] Транспорт событий: N последовательных PUT с `X-Event-Id`/`X-Frame-Index`
      или multipart/form-data одним запросом (зависит от приёмника).
- [ ] Подтвердить на стенде, что mini-runtime (`librknnmrt`) принимает только
      предкомпилированные модели и корректно сообщает о несовместимой версии.
- [ ] Решить, нужна ли поддержка `.aiisp`-моделей через WebUI (активация
      потребует перезапуска пайплайна, в отличие от `.rknn`).

### 11.4. План по фазам

#### Фаза 0. Стендовая проверка платформы

- [x] `dmesg | grep -i rknpu` — версия и режим (non-IOMMU);
- [x] `cat /proc/rknpu/load` — счётчик загрузки NPU (procfs включён);
- [x] `media-ctl -p` — наличие `rkisp_mainpath` и `rkisp_selfpath`;
- [x] `df -h /userdata`, `/proc/meminfo`, `/proc/media-mem` — свободные RAM/CMA;
- [ ] собрать `luckfox_pico_yolov5` под uclibc (`LUCKFOX_SDK_PATH` = корень OKO),
      прогнать после `RkLunch-stop.sh`, замерить FPS и точность на сцене стенда;
- [ ] проверить лимиты VI/VPSS: `VI_MAX_PHY_CHN_NUM`, `VPSS_MAX_CHN_NUM`.

#### Фаза 1. Модель

- [ ] экспорт YOLOv5n → ONNX (opset 12, фиксированный вход 640×384 под 960×540);
- [ ] конвертация `rknn-toolkit2` **1.5.x** (не новее runtime), `target_platform=rv1106`,
      INT8 с калибровочным набором со стенда (день/ночь);
- [ ] проверка точности в симуляторе toolkit;
- [ ] `rknn_query(RKNN_QUERY_SDK_VERSION)` на устройстве — сверить с runtime.

#### Фаза 2. VI chn1 и инференс в `divinus`

- [ ] `fnSetChannelConfig(pipe, 1, …)` + `fnEnableChannel`: `aEntityName="rkisp_selfpath"`,
      `depth=1`, `bufCount=2–3`, размер 640×384;
- [x] тред: `VI_GetChnFrame` → NV12→RGB888 (CPU, RGA непригодна) в буфер RKNN → `ReleaseChnFrame`
      (до инференса!) → `rknn_run` → декод якорей + NMS;
- [x] буферы — dma-heap/MMZ (`RK_MPI_SYS_MmzAlloc`), не malloc (NPU без IOMMU);
- [x] троттлинг `npu_fps`, пропуск кадров, таймауты, деградация при сбое init;
- [x] проверка, что основной поток и OSD не деградируют.

#### Фаза 3. События и кадры до/после

- [x] триггер по детекции с дебаунсом (кулдаун как в `ivs_events.c`);
- [x] индикатор в `/tmp/osd-ivs.txt` (виден только на RTSP main);
- [x] одиночный снимок `jpeg_get()` → `http_post_send()` — базовый сценарий (как сейчас);
- [x] кольцо пре-ролла в RAM (640×384 NV12 ≈ 0.35 MB/кадр; 3 с @10 fps ≈ 10.5 MB);
- [x] пост-ролл по таймеру, мерж повторных событий в окне;
- [x] кодирование буферизованных кадров: `RK_MPI_VENC_SendFrame`
      (`rk_mpi_venc.h:41`) в несвязанный JPEG-канал + один MMZ-блок;
- [x] расширить `http_post` на пакет кадров (N PUT или multipart), таймстемпы
      фиксировать при захвате;
- [x] ограничить объём (`max_frames`), отбрасывать старые, не блокировать `/api/jpeg`.

#### Фаза 4. WebUI: добавление/удаление/активация моделей

- [x] `GET /api/models` (список, размер, дата, активная), `POST /api/models`
      (multipart-загрузка `.rknn`), `POST /api/models/activate`,
      `POST /api/models/delete`;
- [x] загрузка потоковая в `*.part` + `fsync` + atomic `rename` (лимит буфера
      запроса 4 MB не должен становиться потолком размера модели);
- [x] хранение `/userdata/models/<name>.rknn` + `models.json` (имя, классы, input size);
- [x] активация: `rknn_init` → `rknn_query` (shapes, версия runtime) → пробный
      инференс → swap контекста → сохранение пути в `divinus.yaml`
      (`app_config_save()`); при ошибке — откат на предыдущую модель;
- [x] запрет удаления активной модели; защита от path traversal; проверка размера;
- [x] UI в секции Analytics (`res/index.html`): таблица, upload, activate, delete,
      вывод ошибки `rknn_init` (например, «модель собрана несовместимым toolkit»).

#### Фаза 5. Проверки и документация

- [x] RTSP `/live/0` — OSD на месте и обновляется (`bench/run.sh osd_check.py`);
- [x] `/api/jpeg` и событийный JPEG — без OSD (сравнение зон reg1…reg5);
- [x] событие — N кадров до/после, корректный порядок и таймстемпы;
- [ ] AIISP off/on + YOLO: fps, загрузка NPU, отсутствие пропусков в main;
- [x] перезагрузка: активная модель сохраняется, конфиг не теряется;
- [x] `FEATURES.md` §2/§3, `CHANGELOG.md`, `project/app/divinus/source/doc/endpoints.md`,
      `bench/README.md` (если добавляются скрипты).

### 11.5. Ограничения и риски

- **0.5 TOPS @420 МГц.** Реалистичный FPS YOLOv5n — единицы-десятки; цифры брать
  только из замера на стенде. Оверрайд 700 МГц не включён.
- **NPU без IOMMU.** Буферы должны быть физически непрерывными и жить всё время
  инференса; использовать dma-heap/MMZ.
- **AIISP конкурирует за NPU** и не применяется к кадрам selfpath (chn1):
  ночью детектор работает по шумному кадру.
- **VPSS-бюджет 4 канала.** YOLO на VI chn1 не тратит канал, но YOLO на VPSS
  возможен только вместо IVS.
- **RGN: только VENC/VPSS.** Overlay на VI невозможен; выравнивание 16 px
  ухудшает тонкие рамки, один канвас (double buffering даёт лаг).
- **Чистый JPEG.** Не открывать гейт G4 и не вешать регионы на JPEG-канал.
- **/userdata 10 MB, rootfs RO.** Модели — только в rw-раздел; для нескольких
  моделей нужен microSD или увеличение раздела.
- **Версия пары драйвер/runtime фиксирована (0.9.2 ↔ 1.5.2).** Модели,
  собранные новым toolkit, не загрузятся; обновление пары рискует сломать AIISP
  и RockIVA (предсобранные библиотеки).
- **Mini-runtime.** Только предкомпилированные `.rknn`; конвертации на устройстве нет.
- **Лицензия.** Ultralytics YOLOv5 — GPL-3.0; для закрытого продукта нужна
  коммерческая лицензия или Apache-2.0-модель.
- **События и память.** Шторм детекций должен ограничиваться `max_frames` и
  кулдауном, иначе кольцо и очередь POST съедят RAM.

### 11.6. Справочные пути

- Драйвер NPU: `sysdrv/source/kernel/drivers/rknpu/` (`include/rknpu_drv.h:33-36`),
  defconfig `arch/arm/configs/luckfox_rv1106_linux_defconfig:459-462`
- Runtime и модели: `media/iva/iva/librockiva/rockiva-rv1106-Linux/lib/`
  (`librknnmrt.so`), `media/rkpostisp/rv1106/` (`librkpostisp.so`,
  `rkmodel_1080_1920.aiisp`)
- MPI: `media/rockit/rockit/mpi/sdk/include/rk_mpi_venc.h:41` (`SendFrame`),
  `rk_mpi_sys.h:34` (`MmzAlloc`), `rk_comm_vi.h:228` (`aEntityName`),
  `rk_defines.h:77` (`VPSS_MAX_CHN_NUM`)
- Примеры: `media/rockit/rockit/mpi/example/mod/test_mpi_vi.cpp`,
  `test_mpi_venc.cpp`, `test_mpi_rgn.cpp`
- Приложение: `project/app/divinus/source/src/hal/plus/rk_hal.c`,
  `rk_vi.h`, `rk_venc.h`, `rk_rgn.h`, `rk_mb.h`; `src/jpeg.c`, `src/http_post.c`,
  `src/ivs_events.c`, `src/server.c`, `src/media.c`, `src/gating.c`,
  `files/divinus.yaml`, `source/doc/endpoints.md`
- Референс NPU-пути: `project/app/rkipc/rkipc/src/rv1106_ipc/video/video.c`
  (`:537-566`, `:1889-1998`, `:2057-2068`), `common/rockiva/rockiva.c`

### 11.7. Статус реализации (2026-11-05)

M1–M5 реализованы и проверены на стенде в конфигурации `yolo.source=vpss3`,
модель YOLOv5n 640×640 (runtime 1.6.0), 3 fps: детекция идёт, NPU 28–33%,
событие отдаёт 6 JPEG 640×640 с `X-Event-Id/Index/Total/Ts/Cause/Model`,
`api_check.py` — 80/80, RTSP/OSD — 20 кадров без регрессии, загрузка и
активация модели (с откатом при `rknn_init`) проверены. Сборка
`make -C project/app/divinus` и `./build.sh app` + `firmware` проходят.

После плана 2026-11-05 закрыты P1, P3 и P4: `vi1` отклоняется на всех слоях,
zero-copy учитывает `w_stride` и требует 4D-NHWC, разметка переведена на
`userdata` 40 МБ / `rootfs` 168 МБ и прошита полным `update.img`.

Ключевые уточнения к плану:

- Runtime в образе — **`librknnmrt.so` 1.6.0**, не 1.5.2; драйвер `rknpu`
  0.9.2 с ним работает, модель в примере собрана toolkit 1.6.0.
- **RGA не используется.** Легаси `c_RkRgaBlit` падает с SIGSEGV даже с
  корректным `rga_info_t`; im2d `importbuffer_fd` не импортирует ни MMZ-, ни
  dma-heap-дескрипторы; RGA-блит по буферу в куче (первая реализация)
  **вешал плату**. NV12→RGB888 считается на CPU, кольцо кадров — в MMZ с
  отображением через `mmap(fd)` (`MB_Handle2VirAddr` для MMZ валиден только
  на 4 КиБ). Это отклонение от §M1 плана; цена — ~28% одного ядра на 3 fps
  при 640×640.
- `media-ctl` показывает `rkisp_selfpath`, но включение VI chn1 через
  librockit **вешает плату**; дефолт `yolo.source` — `vpss3`, при
  IVS/MJPEG/четвёртом потоке санитайзер переводит на `vi0`. `vi1` теперь
  заблокирован в `gating.c`, `/api/yolo` (HTTP 400) и WebUI, сохранённый
  `vi1` при старте заменяется на `vpss3`.
- Zero-copy: вход пишется с учётом `w_stride`, для модели обязательны 4D-NHWC
  выходы и одинаковое число каналов во всех ветвях (3D/NC1HWC2 отклоняются
  при активации). На стенде лог подтвердил `input 640x640 stride 640 px, 3 4D
  outputs (80x80x255, 40x40x255, 20x20x255)`; раскладка NHWC без паддинга
  до 16 проверена хост-тестом на 80 классах (C = 255).
- `/userdata` после переразметки — UBIFS-том `ubi5_0` 29.1 МиБ (свободно
  ~28 МиБ); загрузка `.rknn` 3 МиБ через `/api/models` проверена, квота
  достижима. Полный `update.img` прошит 2026-11-05; `rootfs` 168 МБ при
  образе ~54 МиБ. Для поставки по-прежнему нужна модель 640×384 INT8
  (~2 МиБ, P2) — точность ещё не измерена.
- Событийный канал — отдельный VENC MJPG 4; `RK_MPI_VENC_SendFrame` работает
  только с MJPG (still-JPEG кодек 15 не выдаёт поток на этой сборке).
- Браузерный `ui_check.py` проходит: Chromium запущен с локально
  распакованным `libasound.so.2` (`apt-get download libasound2t64`), проверены
  8 разделов, отсутствие `vi1` в списке источников, загрузка, ошибка активации
  и удаление модели, i18n EN/RU и мобильная раскладка; скриншоты —
  `output/bench/ui/`.
