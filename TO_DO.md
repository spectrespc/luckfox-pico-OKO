# TO_DO: фокус/автофокус и видеоаналитика на NPU (RV1106 + SC230AI)

Статус: **реализовано и проверено на стенде.** Код, прошивка STM8
и STM32 (CBT6: CLI 1.0.1 → WebUI 1.0.0), WebUI и документация обновлены.
Открытые пункты: сходимость AF на реальном объективе, геометрия NN-рамок и
OSD под поворотом, наблюдаемость кропа ePTZ, mAP и ночная точность модели.
Дата: 2026-11-07.

Состав файла: §1–§10 — фокус и автофокус; §11 — видеоаналитика на NPU
(RKNN + YOLOv5n), события с кадрами и управление моделями через WebUI.

## 1. Цель

Добавить в OKO управление фокусировкой и автофокусом для установленного
моторизованного объектива. Объектив будет подключён к внешнему контроллеру
**STM8S003F3P6** (I2C-слейв; сборка идёт под регистрово совместимый
STM8S103F3P6), который управляет шаговыми приводами фокуса и
зума. Наработки по прошивке STM8 находятся в репозитории:
<https://github.com/spectrespc/stm8s103_i2c_motor>.

Объектив установлен, STM8S003F3P6 подключён к I2C3 (`0x60`) и ST-Link/V2;
драйвера и DTS-описания линзы в Linux-части нет — управление идёт из
`divinus` по I2C.

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
| В IQ `af_v31` был `contrast_af.enable = 0`; OKO ставит `1` в обоих режимах, иначе AFM отдаёт нули | `media/isp/release_camera_engine_rkaiq_rv1106_*/isp_iqfiles/sc230ai_CMK-OT2115-PC1_30IRC-F16.json:7905-7906, 15741-15742` |
| AF включён в сборку rkaiq | `media/isp/Makefile:107` (`-DRKAIQ_ENABLE_AF=ON`) |
| `rkipc` уже вызывает AF-функции AIQ (значит, символы в `librkaiq.so` есть) | `project/app/rkipc/rkipc/common/isp/rv1106/isp.c:1568-1688` |
| `divinus` (приложение по умолчанию) AF не умеет; его AIQ-обёртка — таблица `dlsym` | `project/app/divinus/source/src/hal/plus/rk_aiq.h:50-89`; `doc/endpoints.md:39-56` |
| Есть внешний контроллер STM8S003F3P6 на `0x60`; старый `oko-ec motor` (`0x39`, протокол v2) — другое устройство и для нового STM8 не используется; новый путь — `oko-motor`/`oko-flash` | `project/app/stm8-motor/`, `project/app/oko-flash/`, `project/app/oko-ec/source/main.c` |
| Объектив подключён к STM8 (два шаговых мотора: focus + zoom, драйверы DRV8833) | этот файл, §3, §4.5 |
| Прошивка STM8: `https://github.com/spectrespc/stm8s103_i2c_motor`, I2C-адрес `0x60` (см. §4.5) | внешний репозиторий |

## 3. Привод: STM8S003F3P6 + шаговые моторы

Архитектура уже выбрана: объектив подключается к STM8S003F3P6 (сборка — под
регистры STM8S103F3P6, раскладка TSSOP20 идентична), а RV1106
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
- прошивка: SDCC ≥ 4.0 + STM8-SPL-SDCC, сборка `make -f Makefile.stm8 all` →
  `build/app.ihx` (обновление по I2C) и `build/factory.ihx` (первичная запись
  ST-LINK/v2); в OKO всё вендорено в `project/app/stm8-motor/`, SDCC — в
  образе `tools/stm8-sdcc`.

Зафиксировано (2026-11-06/07):

- motor 0 = focus, motor 1 = zoom; ход focus 2935, zoom 2275; направление —
  отрицательное к упору, home = прижатие и `SET_POS(0)`;
- адрес STM8 `0x60` на `/dev/i2c-3`, 100 кГц (`i2cdetect` видит устройство);
  подтяжки PB4/PB5 и питание DRV8833 проверены косвенно — обмен и движение
  работают, `nFAULT` не срабатывает; старый `oko-ec motor` (`0x39`) — другое
  устройство и к новому STM8 отношения не имеет;
- `home`/`zero` доступны из WebUI и через `oko-motor home`;
- первичная прошивка выполнена по ST-Link/SWIM (`factory.ihx`), обновление —
  `oko-motor flash` / `/api/flash` (см. `TESTBENCH.md` §7C).

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

Номер шины `7` — из примера репозитория; на OKO подтверждён
`/dev/i2c-3` (`i2cdetect -y 3` видит `0x60`). Байтовый порядок и точные размеры ответов —
только по `docs/PROTOCOL.md`.

Ограничения прошивки (важно для алгоритма AF):

- **open-loop**: позиция только программная, энкодера нет; пропуск шагов
  **не** определяется по `nFAULT`: при питании 5 В и обмотках 40 Ом ток
  заклинившего мотора (≈125 мА) равен рабочему, до порога OCP далеко;
  `nFAULT` ловит только КЗ, UVLO и тепловую защиту. Реального положения нет;
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
| `divinus` `src/hal/plus/rk_af_stats.c` | обёртки AF-статистики и `dlsym` уже добавлены |
| `oko-motor` (пакет `stm8-motor`) | I2C-доступ к новому STM8 `0x60` (focus = motor 0, zoom = motor 1), `SET_POS 0x09`, `set-pos`/`home`, прошивка через I2C-загрузчик; старый `oko-ec motor`/`0x39` не используется |

### 4.5. Прошивка STM8 (внешний репозиторий)

- Репозиторий: <https://github.com/spectrespc/stm8s103_i2c_motor> (MIT).
- README: <https://github.com/spectrespc/stm8s103_i2c_motor/blob/main/README.md>,
  русская версия — `README.ru.md`.
- Протокол: <https://github.com/spectrespc/stm8s103_i2c_motor/blob/main/docs/PROTOCOL.md>.
- Железо/распиновка: <https://github.com/spectrespc/stm8s103_i2c_motor/blob/main/docs/HARDWARE.md>.
- Тестовая плата (STM8S003F3P6, `BOARD=test`, версия 2.1.1):
  `project/app/stm8-motor/docs/HARDWARE_TESTBOARD.md`.
- Сборка: `make clean && make` → `build/firmware.ihx`; прошивка `make flash`
  (STM8-SPL-SDCC, SDCC ≥ 4.0, ST-LINK/v2).
- Хостовые тесты протокола: `make -C tests test`.
- В OKO вендорено в `project/app/stm8-motor/`: upstream-база
  `3f36dcee4f05c02a48bcc48b8eea7e606fe7634e` (2026-11-05), добавлены
  I2C-загрузчик, раскладка `0x8000–0x9FFF`, `oko-motor`, `Makefile.stm8`
  и SDK-обёртка `Makefile`. Первичная прошивка — ST-Link (`factory.ihx`),
  обновление — `oko-motor flash` / `/api/flash` (KI-035).

## 5. Варианты реализации

### Вариант A. VCM на I2C → штатный автофокус rkaiq

На текущем железе **не применяется**: привод шаговый, через STM8.
Оставлен как справка на случай замены объектива на VCM-модуль.

Когда подходит: объектив с VCM-приводом (dw9714/ak7375/vm149c/dw9768/…).

Не применяется: привод шаговый через STM8. Справка по API и драйверам —
§4.1 и §10.

Плюсы: минимум своего кода, штатные режимы auto/semi-auto/manual, окно, lock,
oneshot, tracking. Минусы: требуется именно VCM и правка IQ.

### Вариант B. STM8 + шаговые моторы → свой AF (выбранный)

Объектив подключён к STM8S003F3P6 (`0x60`); Linux-сторона общается с ним по
I2C и не управляет обмотками напрямую. Реализовано в `focus_af.c` (зонд
направления, coarse+fine, медиана трёх свежих кадров, `af_patience`,
`af_timeout_s`, micro-fine `af=fine` с `af_window()` и хост-тестом), кривые
пишет `bench/api_check.py` в `output/bench/af_curve_*.csv`. Открыто только
подтверждение сходимости на настоящем объективе: на стенде метрика не зависит
от позиции мотора. Плюсы: готовый привод фокуса и зума, гальванически
развязанная механика, не требует VCM и правок IQ. Минусы: open-loop без
обратной связи и энкодера, нет разгона, EEPROM-запись ≤ 1 Гц; безопасность
упоров и калибровка — на приложении.

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
- [ ] ROI у края кадра: проверить, что окно не выходит за границы сенсора;
- [ ] при смене сценария/разрешения окно нужно пересчитывать;
- [ ] для «всего кадра» проверить, что края не портят FV (виньетка, IR-засветка,
      OSD/маска) — при необходимости отбрасывать краевые окна;
- [ ] сравнить оба режима на одном сюжете: точность, число шагов, время
      поиска, устойчивость к шуму и подсветке.

**Критерии приёмки эксперимента**

- [ ] полный кадр: AF сходится на типовых сценах (лицо/текст/пейзаж);
- [ ] ROI: AF сходится на объекте в ROI, даже если фон резче объекта;
- [ ] смена ROI не требует перезапуска приложения (проверено: ROI
      применяется live, но сходимость на объекте ещё не доказана);
- [ ] оба режима укладываются в заданный таймаут и не гоняют мотор в упор.

Прогон 2026-11-07 (`OKO_FOCUS_AF_TESTS=1`): обе ветки (`full`, `roi
2500,2500,5000,5000`) завершились `done` за ~11 с; метрика от позиции мотора
не зависит (объектив стенда оптически не связан с мотором), поэтому критерии
сходимости остаются открытыми до прогона на реальном объективе.

## 6. Открытые работы по фокусу

- сходимость AF на настоящем объективе: на стенде метрика не зависит от
  позиции мотора, ветки `full` и `roi` завершаются `done` только механически;
- поведение при разной освещённости и с IR-подсветкой;
- continuous-режим (ручной и oneshot `af=1` уже есть);
- ROI у края кадра и пересчёт окна при смене разрешения/сценария
  (минимальный размер 10 % и границы уже заданы в `focus_geom.c`).

## 7. Интеграция в приложение (`divinus`)

Карта реализации:

| Файл | Что менять |
|---|---|
| `project/app/divinus/source/src/hal/plus/rk_aiq.h` | добавить указатели `fnSetFocusMode`, `fnGetFocusMode`, `fnSetFocusPosition`, `fnGetFocusPos`, `fnOneshotFocus`, `fnGetAfStats` и `dlsym`-загрузку |
| `project/app/divinus/source/src/hal/plus/rk_hal.c` | реализовать обёртки над `_rk_aiq_ctx`, проверять `_rk_aiq_ctx` перед вызовом |
| `project/app/divinus/source/src/...` (I2C) | клиент STM8 (`focus_motor.c`, `focus_proto.c`): `/dev/i2c-3`, `set speed` / `move` / `get position` / `get status` / `version` / `SET_POS 0x09` для обоих моторов |
| `project/app/divinus/source/src/focus_home.c` | асинхронный job прижатия к упору и обнуления позиции (`home`/`zero`), состояние в `GET /api/focus` |
| `project/app/divinus/source/src/focus_af.c` | асинхронный поиск автофокуса: зонд направления, coarse+fine по метрике AF, взаимные блокировки с home/zero/flash, `af` в `GET /api/focus` |
| `project/app/divinus/source/src/focus_af_algo.c` | чистые решающие правила (`af_tracker_*`, `af_pick_direction`, `af_window`), хостовый тест `tests/focus_af_test.c` |
| `project/app/divinus/source/src/...` (HTTP) | `/api/focus`: `enable`, `mode`, `roi`, `fps`, `motor` (`true/false` — драйвер, `focus/zoom/both` — устаревшая шкала), `scale`, `speed`, `move`, `position`, `home`, `zero`, `value`, `stop`, `reset`, `af` (`1`/`fine`/`cancel`) |
| `project/app/divinus/source/doc/endpoints.md` | описание endpoint, полей JSON и кодов ошибок |

**Реализовано:** ручное управление и автофокус в карточке Focus assist, а
также `home`/`zero` с асинхронным job'ом и гейтом версии STM8. Детали
алгоритма и ограничения — §5B, §6 и `KNOWN_ISSUES.md`.

Замечания:

- `divinus` использует `dlsym`, поэтому отсутствие AF-символов в конкретной
  сборке `librkaiq.so` не должно ронять приложение — функция опциональна.
- ONVIF `videosources.xml` по-прежнему лишь статически объявляет
  `AutoFocusMode=AUTO`; ONVIF-команд автофокуса нет.
- AF гоняет только мотор focus; зум остаётся ручным.

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

# STM8 (0x60 на /dev/i2c-3; старый oko-ec motor 0x39 не используется)
i2cdetect -y 3
i2ctransfer -y 3 w1@0x60 0xFF r3              # версия прошивки
i2ctransfer -y 3 w2@0x60 0x05 0x00 r1         # статус focus
```

## 9. Риски и ограничения

- **IQ привязан к имени модуля/объектива.** Смена `camera-module-lens-name`
  без переименования IQ или без нового IQ ломает загрузку тюнинга
  (`BoardConfig...SC230AI.mk:93`, `/proc/rkisp-vir0`).
- **Contrast AF в IQ.** В вендорском IQ SC230AI `af_v31.TuningPara.contrast_af.enable = 0`;
  OKO включает его и в IQ-файле, и в рантайме (`contrast_af` как fallback гейта
  G13), иначе AFM отдаёт нулевые ячейки.
- **PDAF нет**, HDR2/HDR3 на SC230AI заблокированы
  (`FEATURES_UNVERIFIED.md`) —
  AF-статистика в HDR не проверена.
- **ROI и координаты.** Окно AFM задаётся в координатах сенсорного кадра, а
  ROI из WebUI/ONVIF — в координатах стрима; без пересчёта (crop/ePTZ,
  масштаб, mirror/flip) фокус будет искать не в той области.
- **Малый ROI.** При размере меньше окна 15×15 FV становится шумным; нужен
  минимальный размер ROI и/или расширение окна с весами.
- **STM8: open-loop без энкодера.** Позиция программная; пропуск шагов по
  `nFAULT` **не** ловится (при 5 В и 40 Ом ток заклинившего мотора равен
  рабочему ≈125 мА, OCP не срабатывает; `nFAULT` — только КЗ/UVLO/TSD).
  Реального положения нет. AF должен проверять FV после каждого перемещения,
  а не доверять счётчику шагов.
- **STM8: нет разгона.** Длинные перемещения на большой скорости могут
  срываться; для AF использовать малые шаги и умеренную скорость.
- **STM8: порядок команд.** `move` игнорируется, пока не задана скорость
  (сначала `set speed`), см. README репозитория.
- **STM8: EEPROM.** Запись ≤ 1 Гц и только в покое; не сохранять позицию на
  каждый шаг.
- **Адрес и протокол.** Новый STM8 использует `0x60`; старый `oko-ec motor`
  (`0x39`) — другое устройство и оставлен без изменений. Интеграция — через
  `oko-motor`/`oko-flash`.
- **Питание и механика.** Обмотки идут через DRV8833 (`VM` 2.7–10.8 В);
  нужны общий GND с RV1106, подтяжки I2C 4.7 кОм на PB4/PB5 и защита
  механических упоров (программные границы + `nFAULT`).
- **Ресурсы RV1106.** Моторы висят на STM8, PWM/GPIO для них не нужны;
  прямой путь не выбирается — PWM10/PWM11 заняты подсветкой
  (`sc230ai_init.c:41-64`).
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
- Шаговик/STM8: `project/app/stm8-motor/`, `project/app/oko-ec/`
- Прошивка STM8 (внешняя): <https://github.com/spectrespc/stm8s103_i2c_motor>
  - README: `README.md` / `README.ru.md`
  - протокол: `docs/PROTOCOL.md`
  - железо/распиновка: `docs/HARDWARE.md`
  - тестовая плата: `docs/HARDWARE_TESTBOARD.md` (`make ... BOARD=test`)
  - сборка/прошивка: `make`, `make flash` (SDCC ≥ 4.0, ST-LINK/v2)
- Документация проекта: `STRUCTURE.md`, `INIT_ORDER.md`, `TESTBENCH.md`,
  `FEATURES_VERIFIED.md`, `FEATURES_UNVERIFIED.md`

## 11. Видеоаналитика на NPU (RKNN + YOLOv5n)

Функциональность реализована и проверена на стенде: YOLOv5n на `vpss3`,
runtime 1.6.0, `/userdata` 40 МБ. В разделе — только незакрытые задачи;
результаты проверок — в `VERIFIED_FUNCTIONS.md`.

### 11.1. Точность модели 640×384

Модель собрана Docker-пайплайном `tools/rknn-toolkit2/`: YOLOv5n → ONNX
640×384 → INT8 `.rknn` 2.6 МиБ, активирована на DUT (4D-NHWC, вход 640×384).
Осталось измерить качество на валидационном наборе.

- [ ] Проверить mAP на валидационном наборе (COCO val или свой размеченный);
      сравнить с float ONNX/`yolov5n.pt`, зафиксировать числа в `output/bench/`
      и `VERIFIED_FUNCTIONS.md`.
- [ ] Если качество ниже приемлемого — дообучение/подбор калибровки (больше
      кадров SC230AI, день/ночь) и повторное квантование.
- [ ] Приёмка: точность задокументирована числами; при смене модели обновить
      `yolo.input_width/height`, `conf_threshold` и `KNOWN_ISSUES.md`.

### 11.2. AIISP, ночная точность и память

Рабочая комбинация AIISP + YOLO на `vpss3` измерена: main 30.2–32.8 fps,
NPU 28%, `librkpostisp.so` загружен, пропусков в `/live/0` не видно.
Остаются ночная точность и замер памяти под кольцом.

- [ ] Ночные сцены (ИК): точность на кадрах после AIISP и до него.
- [ ] Замер CMA под кольцом: `max_frames=20` при 640×640 (~12 MB MMZ);
      `cat /proc/media-mem` и `/proc/meminfo` до/после, плюс 20 событий подряд
      без роста RSS/CMA.
- [ ] При ограничении — гейт или предупреждение в `gating.c`; результат — в
      `VERIFIED_FUNCTIONS.md`.
- [ ] Приёмка: задокументированная рабочая комбинация и явный отказ для
      нерабочей.

### 11.3. Открытые вопросы

- [ ] Лицензия YOLOv5 (GPL-3.0): нужна коммерческая лицензия либо
      Apache-2.0-альтернатива (YOLOX, NanoDet) для закрытого продукта.
- [ ] Нужна ли поддержка `.aiisp`-моделей через WebUI: активация потребует
      перезапуска пайплайна, в отличие от `.rknn`.

### 11.4. Опорные факты

- Пара драйвер/runtime — `rknpu` 0.9.8 + `librknnmrt.so` 1.6.0; модели
  toolkit 1.6.0 идут на runtime 1, модели toolkit 2.x — на
  `librknnmrt2.so` 2.3.2 (`yolo.runtime: 2`).
- NPU без IOMMU: буферы должны быть физически непрерывными (dma-heap/MMZ),
  не `malloc`.
- `vi1` (selfpath) вешает плату и заблокирован; рабочая ветка — `vpss3`,
  аварийная — `vi0`. При IVS/MJPEG/четвёртом потоке санитайзер переводит
  `vpss3` на `vi0`.
- `/userdata` — том ~29 МиБ, квота модели 3 МиБ; собранная модель 640×384 —
  2.6 МиБ. В образ она не входит: после прошивки загружается через
  `/api/models`; сборка — `tools/rknn-toolkit2/` (Docker).
- AIISP обрабатывает кадр `vpss3`, поэтому ночная точность зависит от него;
  при `vi0` кадр идёт без AIISP.
- Модели — только предкомпилированные `.rknn`; конвертации на устройстве нет.
- Событийный канал — отдельный VENC MJPG 4; лишние регионы на него не вешать,
  событийный JPEG должен оставаться без OSD.

### 11.5. Рамки детекции на видеопотоке

Реализовано 2026-11-07: порт вендорского NN-оверлея rkipc — регион RGN `12`
(2BPP, layer 6), `yolo.boxes`, два цвета, перерисовка после каждого инференса
(5 Hz), очистка канвы при нуле детекций, пересоздание при смене размера или
поворота главного потока и при потере привязки к VENC. Проверено на DUT
(см. `VERIFIED_FUNCTIONS.md`, `FEATURES_VERIFIED.md`).

- [x] 0/90/180/270: канва, маппинг координат и переякорение OSD проверены
      2026-11-07 `OKO_GEOMETRY_TESTS=1` — IoU рамок 0.624/0.713/0.711 против
      повёрнутой маски 0°, OSD-текст в своих углах (12 проверок); артефакты в
      `output/bench/geometry/{boxes,osd}-*`.
- [ ] Эталонная разметка положения рамок на повёрнутой сцене: без неё
      проверяется только воспроизводимость маппинга (`geometry_check.py`).
- [ ] Цвета по классам: доступны два (класс 0 и все прочие); нужен ли третий —
      решение владельца.
- [ ] Рамки на sub/JPEG при `osd.scope=all`: сейчас зависят от
      multi-attach RGN, который заблокирован (см. `FEATURES_UNVERIFIED.md`).
- [x] Кроп ePTZ учитывается в маппинге (`nn_geom.c`); смена кропа/угла
      применяется graceful-рестартом приложения (KI-044 закрыт).

## 12. Сборка и окружение (после миграции 2026-11-08)

- [x] Нативная сборка на Debian 13: `tools/native/setup_host.sh`, `env.sh`,
      `smoke.sh`; Docker-контейнер сборки удалён.
- [x] Buildroot 2026.08.1: rootfs собран, проверен по NFS и прошит в SPI NAND
      (`api_check` 98/98, RTSP/OSD).
- [x] Netboot TFTP/NFS: `bench/netboot.py`, переключение NAND/NFS через env
      `bench/nfs_env.py`, `tools/native/setup_netboot.sh`; netboot-фрагмент
      входит в штатный `boot.img`.
- [ ] Сервисы TFTP/NFS переведены в контейнер `services/`, NFS-хранилище
      перенесено в `output/srv/nfs`; остаётся гейт на DUT
      (`bench/run.sh nfs_env.py on` / `off`) и удаление хостовых
      `tftpd-hpa`/`nfs-kernel-server`/`rpcbind` (нужен `sudo`, порядок —
      в `services/README.md`).
- [ ] `luckfox_pico_w_defconfig` (bluez5_utils/mpv): ребейс
      `hcitool_patch`/`mpv_patch` под новые версии или отключение пакетов
      (KI-046).
- [ ] Сторонние `/oem`-blob'ы со старым ABI (`libssl.so.1.1`, Python 3.11):
      пересобрать из SDK или добавить compat-копии (KI-045).
