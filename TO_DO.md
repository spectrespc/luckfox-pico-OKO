# TO_DO: фокус/автофокус и видеоаналитика на NPU (RV1106 + SC230AI)

Статус: **реализовано и проверено на стенде.** Код, прошивка STM8
и STM32 (на 2026-11-07 CBT6: CLI 1.0.1 → WebUI 1.0.0; с 2026-11-13 прошивка
EC перенесена на STM32C011F6P6, proto v3, `GET_POWER 0x20`/`SET_POWER 0x21`),
WebUI и документация обновлены. Сон привода STM8 с 2026-11-21 работает и
проверен на стенде: тестовая плата, прошивка `2.3.2`, Wait mode (`WFI`) вместо
Active-halt (AWU стендового STM8S003F3P6 не считает — KI-077); T8 —
`tests/hardware_test.sh` 41/0 с 30-минутным soak, T9 проверен на уровне API;
остаётся обновление по I2C (KI-075, обход ST-Link). Heartbeat-Snapshot сна
камеры (§13) реализован 2026-11-18/19. Тихая кадентность `[sleep]` исправлена
2026-11-24 (единицы PMU-таймаута, KI-072), AOV-MCU отработал на NAND на
ядрах #37/#38 (2026-11-24/25), KI-079 и KI-080 закрыты.
Открытые пункты: сходимость AF на реальном объективе, mAP и ночная точность
модели, замена дефектного STM8 (KI-075/KI-077, §3.2), P3/P5 переноса из
вендорского `fastboot_client.c` (§14; P1/P2/P4/P6/P7 закрыты 2026-11-25),
остатки AOV — gate вендорского ISP-handover и overshoot 68–79 с окна
`wake_check` (§8.1), интеграция rkipc-backlog (§15.3).
Дата: 2026-11-25.

Смежные подсистемы вне состава файла: MQTT-канал событий и телеметрии
(2026-11-19) — статусы в `FEATURES_VERIFIED.md`/`FEATURES_UNVERIFIED.md`
и `CHANGELOG.md`.

Состав файла: §1–§10 — фокус и автофокус; §11 — видеоаналитика на NPU
(RKNN + YOLOv5n), события с кадрами и управление моделями через WebUI;
§12 — сборка и окружение; §13 — сон камеры (proof-of-life кадром);
§14 — разбор вендорского `fastboot_client.c` и перенос полезного;
§15 — каталог остатков вендорского кода и аудит ядра.

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

## 3.2. Экземпляр STM8S003F3P6 на стенде: замена чипа (KI-075, KI-077)

Стендовый экземпляр непригоден для I2C-обновления: IAP-стирание блока не
завершается, ядро стоит в записи-триггере и выходит только по IWDG. Гипотезы
о регистровой последовательности закрыты (2026-11-22): ожидание `PUL` (E0),
`FLASH_CR1.HALT`/`AHALT` (E1) и диагностическая сборка без ожидания `EOP` —
эффекта нет; последовательность совпадает с PM0051 §4.2 и SPL
(`research/stm8-iap-an2659/`, `KNOWN_ISSUES.md` KI-075). Тот же чип имеет
мёртвый AWU (KI-077).

- [ ] закупить заведомо оригинальные STM8S003F3P6 (TSSOP20), заменить чип на
      тестовой плате, сохранив текущий как «дефектный образец»;
- [ ] прошить `factory-2.3.2.ihx` по SWIM + `stm8flash -R`
      (`TESTBENCH.md` §7C), проверить `oko-motor version` → `2.3.2`;
- [ ] IAP-стирание: `enter-boot`, `i2ctransfer -y 3 w9@0x60 0x11 0x00 0x00
      0x9f 0x00 0x00 0x00 0x00 0x40` (write+STOP), затем отдельное
      `i2ctransfer -y 3 r2@0x60` → ожидается `0x00 0x11`, блок `0xFF` через
      SWIM, без `SCL hold` в `dmesg`;
- [ ] полный цикл `oko-motor flash app-2.3.3.ihx` → `verify` → `version`
      меняется, `RC=0`;
- [ ] AWU: повторить тест KI-077 (`AWUF` выставляется, Active-halt будится);
- [ ] при успехе — закрыть KI-075/KI-077, включить чанкование стирания на
      хосте (`util/motorctl.c:519`, по 512 Б на кадр) и обновить
      `TESTBENCH.md` §7C.

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
| `ipcweb` `src/image_api.cpp:107-113,335` (каталог удалён 2026-11-25; [upstream](https://github.com/LuckfoxTECH/luckfox-pico/tree/main/project/app/ipcweb)) | REST-поля `sAFMode`, `iZoomLevel`, `iFocusLevel` |
| `divinus` `src/hal/plus/rk_af_stats.c` | обёртки AF-статистики и `dlsym` уже добавлены |
| `oko-motor` (пакет `stm8-motor`) | I2C-доступ к новому STM8 `0x60` (focus = motor 0, zoom = motor 1), `SET_POS 0x09`, `set-pos`/`home`, прошивка через I2C-загрузчик; старый `oko-ec motor`/`0x39` не используется |

### 4.5. Прошивка STM8 (внешний репозиторий)

- Репозиторий: <https://github.com/spectrespc/stm8s103_i2c_motor> (MIT).
- README: <https://github.com/spectrespc/stm8s103_i2c_motor/blob/main/README.md>,
  русская версия — `README.ru.md`.
- Протокол: <https://github.com/spectrespc/stm8s103_i2c_motor/blob/main/docs/PROTOCOL.md>.
- Железо/распиновка: <https://github.com/spectrespc/stm8s103_i2c_motor/blob/main/docs/HARDWARE.md>.
- Тестовая плата (STM8S003F3P6, `BOARD=test`, версия 2.3.2 с Wait-mode сном,
  проверена на стенде 2026-11-21):
  `project/app/stm8-motor/docs/HARDWARE_TESTBOARD.md`.
- Сборка: `make clean && make` → `build/firmware.ihx`; прошивка `make flash`
  (STM8-SPL-SDCC, SDCC ≥ 4.0, ST-LINK/v2).
- Хостовые тесты протокола: `make -C tests test`.
- В OKO вендорено в `project/app/stm8-motor/`: upstream-база
  `3f36dcee4f05c02a48bcc48b8eea7e606fe7634e` (2026-11-05), добавлены
  I2C-загрузчик, раскладка `0x8000–0x9FFF`, `oko-motor`, `Makefile.stm8`
  и SDK-обёртка `Makefile`. Первичная прошивка — ST-Link (`factory.ihx`),
  обновление — `oko-motor flash` / `/api/flash` (KI-075: на стендовом
  экземпляре IAP-стирание не завершается, обход — ST-Link/SWIM).

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

- [ ] пересчёт ROI из координат стрима в сенсорный базис: функция
      `focus_geom_preview_to_sensor` (mirror/flip) и минимальный ROI 10 %
      (`FOCUS_BP_MIN_ROI`) уже есть в `focus_geom.c` (см. §6); открытым
      остаётся пересчёт окна при смене разрешения/сценария;
- [ ] ROI у края кадра: проверить, что окно не выходит за границы сенсора;
- [ ] для «всего кадра» проверить, что края не портят FV (виньетка, IR-засветка,
      OSD/маска) — при необходимости отбрасывать краевые окна;
- [ ] сравнить оба режима на одном сюжете: точность, число шагов, время
      поиска, устойчивость к шуму и подсветке.

**Критерии приёмки эксперимента**

- [ ] полный кадр: AF сходится на типовых сценах (лицо/текст/пейзаж);
- [ ] ROI: AF сходится на объекте в ROI, даже если фон резче объекта;
- [x] смена ROI не требует перезапуска приложения: `focus.c` применяет новое
      окно на ходу (флаг `pending` в рабочем цикле, без рестарта); прогон
      2026-11-07 `OKO_FOCUS_AF_TESTS=1` прошёл. Сходимость на объекте остаётся
      открытой (первые два критерия и §6);
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

Карта реализации (закрыта):

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

- [x] **Реализованы все строки таблицы** (2026-11-07…22): ручное управление и
      автофокус в карточке Focus assist, `home`/`zero` с асинхронным job'ом и
      гейтом версии STM8. Доказательство — `FEATURES_VERIFIED.md` («Помощь
      фокусировке»: `cells_used=225`; «Мотор STM8 `0x60`»): `home` — полный ход
      2960 (focus) / 2300 (zoom) шагов от любого счётчика (2026-11-22),
      `api_check.py` с `OKO_FOCUS_MOTOR_TESTS=1`. Детали алгоритма и
      ограничения — §5 (вариант B), §6 и `KNOWN_ISSUES.md`.

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
i2ctransfer -y 3 w1@0x60 0x0A r8              # сон: режим/состояние/счётчики

# T8 (сон привода): на DUT, divinus остановлен (killall divinus)
#   cd /userdata/stm8 && BUS=3 ADDR=0x60 FW_VERSION=2.3.2 \
#       SLEEP_SOAK_S=1800 ./tests/hardware_test.sh
# 2026-11-22: повтор с SLEEP_SOAK_S=300 — 41 PASS / 0 FAIL при одновременном
#   замере ESP32 INA3221 (CH3, вход 5 В DUT); сверка −6.7 %:
#   bench/run.sh sleep_power.py --scenario stm8 --stm8-soak 300
```

### 8.1. AOV: статус и остатки (gate KI-072)

- [x] **Порт MCU battery-ipc (2026-11-24).** Борд
      `battery_ipc_demo_v10-SC230AI-ADC` портирован под Luckfox
      (`i2c_addr = 0x32`, шина i2c4/m2) и воспроизводимо собирается
      `./build.sh mcu battery_ipc_demo_v10-SC230AI-ADC` (`CHANGELOG.md`,
      «Экспериментальная wakeup-IPC линия»). AOV-MCU на NAND, ядро `#37`:
      7 полных циклов с MCU-прокси (`RT_USING_RK_AOV=y`), fast-AE
      (SC230AI 640×480@120), детекция движения (`md_flag=1`), `-h-mbox-`,
      resume без зависаний (KI-072, запись 2026-11-24).
- [x] **Кадентность и свип (2026-11-24).** Причина плотного цикла — единицы
      `PMU_WAKEUP_TIMEOUT_CNT` (`0xff300048`): счётчик тактируется 32 кГц
      (32768 тиков/с), а divinus писал `×32`. `pmu_timeout_hz` по умолчанию
      исправлен на 32768, старые конфиги (< 1000) мигрируют в
      `app_config_parse`; после правки тихий `cycle` спит 10.08 с при
      интервале 10 с и 60.08 с при 60 с — ровно один цикл на
      `wake_interval_s`. V1 (`aov_mcu=1`, `keep_isp=1`, 60 с): интервалы
      68–79 с, `last_wake_src=2`, I2C3 и STM8 2.3.2 живы на каждом
      пробуждении (KI-080 закрыт). Артефакты
      `output/bench/power/20261124-220827-cycle`,
      `output/bench/power/20261125-073154-aov`.
- [x] **Окно mailbox 500 мс (ядро `#38`, 2026-11-25).**
      `RV1106_HPMCU_FAST_WKUP_TIMEOUT` уменьшено с 1000 до 500 мс, ядро
      прошито в NAND: 4 `-h-wait-` / 4 `-h-mbox-` / 0 `-h-timeout-`,
      решение MCU за ~0.2 с, фактический сон 60.28 с; фолбэк на 1000 мс не
      потребовался.
- [x] **Вендорский ISP-handover — gate AOV: закрыт как нереализуемый
      (2026-11-25).** Схема «AP отпускает media, MCU владеет ISP во время
      deep suspend» проверена на стенде и по документации. При
      `keep_isp=0` MCU не может даже писать регистры SC230AI
      (`[SC230AI] ERR write multi-regs addr=0x0103`, 10 ретраев), окно
      500 мс истекает (`-h-timeout-`), AP просыпается по таймауту ядра
      (`output/bench/aov-test/keepisp0-20261125b`,
      `.../quiet_keepisp0-20261125b.20261125-141406.log`). Руководство
      Rockchip AOV V1.0.4 определяет AOV как цикл «сон → кадр → сон» с
      питанием только PMUIO во сне (`aov-guide-extract.txt:197,609,646`),
      а прошивка MCU в обеих ветках `stream.c` уходит в
      `sysTick_stop(); wfi` (`:556-559`, `:640-643`). В PMU RV1106 один
      общий `RV1106_PMU_POWER_OFF_ENA`, per-domain keep нет. Доказательства
      и артефакты — KI-072, запись 2026-11-25; итог в
      `research/rv1106-aov/README.md`. Дальнейшая работа — только
      MCU-JTAG/`mcutool`-исследование, если владелец захочет отдельный
      проект (§15.6).
- [x] **Двухуровневый сон AOV (2026-11-25).** Tier 1: PMU-таймер будит
      SoC каждые `wake_interval_s`, MCU fast-AE решает движение/тишину;
      тихий цикл возвращает AP в сон без резюма Linux. Tier 2: каждый N-й
      тихий цикл (`N = heartbeat_interval_s / wake_interval_s`) ядро
      возвращает Linux с `aov_state=heartbeat` для телеметрии. Ядро:
      `aov_heartbeat_wakes`, `aov_quiet_wakes`, `aov_timeout_streak`,
      `OKO_AOV_HEARTBEAT`; три таймаута mailbox подряд — терминальный
      резюм с `aov_state=timeout`. `divinus`: программирует N, шлёт
      heartbeat по tier-2 без гейта `CLOCK_MONOTONIC`, самоизлечение
      `mcu_unresponsive` (снимает `aov_mcu`, интервал 60 с). Дефолты
      AOV-профиля при отсутствии ключей: `wake_interval_s=2`,
      `heartbeat=true`, `heartbeat_interval_s=300`; `sleep.enable` остаётся
      `false`. Netboot 2026-11-25 проверены таймаут-ветка (N=0 → 10.69 с,
      N=2 → 7.13 с, `PM: suspend entry`=1) — `two-tier-20261125/`. Живой
      MCU и приёмка 8.2–8.6 — за NAND-образом.
- [x] **USB-хост вне resume-пути (2026-11-25).** `[sleep] usb_on_motion`
      (по умолчанию `false`) отвязывает `xhci-hcd.0.auto` перед `mem`:
      стендовый resume-путь 0.195 с против 2.28 с, тихий цикл не
      пере-нумерует хаб/WiFi; на движение хост привязывается и
      выполняется `oko-wifi apply`. Ограничение: пока ключ включён,
      WiFi/хаб недоступны до движения. Открыто: нужен ли режим по
      умолчанию для Ethernet-only развёртываний и как учитывать его в
      окне движения.
- [x] **Политика интервала — решена tier 1 (2026-11-25).** Тихий цикл
      больше не платит resume-путь (~2.3 с) и рестарт `divinus`
      (10–25 с), поэтому короткий интервал не даёт прежнего overshoot.
      Остаточная латентность относится только к событийным резюмам
      (motion / tier-2): resume-путь 2.3 с на стенде — это dpm-резюм
      USB-хаба и WiFi-донгла, код `rv1106_pm.c` ≤10 мс; без USB 0.12–0.17 с.
      Профиль — KI-072 (2026-11-25), артефакты
      `output/bench/aov-test/resume-profile-20261125/`.
- [ ] **Латентность события без `execvp`-рестарта.** Проверено 2026-11-25:
      вариант «оставить процесс и подсистемы живыми через mem» отклонён —
      живой CSI/ISP даёт шторм `mipi-csi2-hw ERR1:0x10 (fs/fe mis)` и
      убивает HTTP/поток (KI-072). Остаётся только in-process
      реинициализация медиа (stop media, процесс/сервер живы), но она
      конфликтует с причиной `exec` — чистым освобождением dmabuf/RGA
      (KI-069); нужен отдельный проект и решение владельца. Опора — вендорский
      AOV-гайд V1.0.4 §3.1.2–3.1.3: перед `mem` вычитать остатки кадров
      (`RK_MPI_VI_GetChnFrame`→`ReleaseChnFrame`), `ISP_SingleFrame`,
      `RK_MPI_VI_PauseChn`, `UnbindSdcard/Ethernet/Soundcard`, затем
      `EnterSleep`; после пробуждения — `ISP_MultiFrame`, `VI_ResumeChn`,
      `Bind*` и convergence-проверки. Процесс при этом остаётся жив. Методика —
      `last_suspend_time`, `output/bench/power/*`, `bench/boot_probe.py`.
- [ ] **Frame-sync master/slave и dynamic FPS SC230AI — отложено.** Нет
      потребительской задачи; DTS sync-режим не задан. Регистры-кандидаты и
      порядок стендовой проверки — §15.6.
- [ ] **Прошивка и матрица тестов Э5/Э7** экспериментальной wakeup-IPC
      линии: статус bench-only — `update.img` собирается, прошивка через
      полный `update.img` не выполнялась (`FEATURES_UNVERIFIED.md`,
      «Wakeup-IPC»); консоль стенда доступна с 2026-11-23 (KI-078, RX
      восстановлен), полный цикл netboot/flash не подтверждён. Расширение
      TB-линии — только по решению владельца: вендорские файлы заморожены
      (AGENTS.md §6).
- [x] **Решение владельца (2026-11-25): вердикт принят.** AOV остаётся на
      совместимой схеме `aov_mcu=1` + `keep_isp=1` + PMU-таймер; ISP-handover
      не делается, MCU-JTAG-исследование не открывается, отдельный проект
      «перенос `divinus` через suspend» не запускается. Интервал по умолчанию
      остаётся 60 с; латентность движения равна интервалу.
- [ ] **Выпуск AOV из экспериментального профиля** — решение владельца
      2026-11-25: дефолты AOV-профиля `wake_interval_s=2`,
      `heartbeat=true`, `heartbeat_interval_s=300`, `sleep.enable=false`;
      осталось подтвердить живого MCU и приёмку на NAND-образе (8.2–8.6)
      и выбрать, включать ли сон по умолчанию.

Ограничение: AOV-ветка (`aov_mcu=1`) требует `keep_isp=1`; при `keep_isp=0`
handshake детерминированно уходит в `-h-timeout-` (сенсор недоступен MCU),
поэтому quiet-тесты `[sleep]` без MCU — с `aov_mcu=0`.

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
Осталось измерить качество на валидационном наборе; в `VERIFIED_FUNCTIONS.md`
чисел mAP пока нет (2026-11-25).

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

- [ ] Ночные сцены (ИК): точность на кадрах после AIISP и до него. На текущем
      стенде нет ИК-подсветки и управляемого IR-cut (`/api/night` отдаёт
      `ir_cut_pin1=999`, `irled_pin=999`, узла ircut в DTS Pro Max нет),
      поэтому «ночь» с ИК физически недоступна; ambient-матрица C1–C4 снята
      (`bench/yolo_night.py --phase ambient`), но числа не опубликованы —
      сцена нестабильна (KI-005). Замер ждёт неподвижную сцену и ИК-железо.
- [ ] Замер CMA под кольцом (KI-006): `max_frames=20` при 640×640 (~12 MB
      MMZ); `cat /proc/media-mem` и `/proc/meminfo` до/после, плюс 20 событий
      подряд без роста RSS/CMA.
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
- [x] Сервисы TFTP/NFS переведены в контейнер `services/`, NFS-хранилище
      перенесено в `output/srv/nfs`; матрица `nfs_env.py on/off` пройдена до
      и после purge, хостовые `tftpd-hpa`/`nfs-kernel-server`/`nfs-common`/
      `rpcbind` удалены, `/srv/nfs` и ACL `u:tftp` сняты.
- [ ] `luckfox_pico_w_defconfig` (bluez5_utils/mpv): ребейс
      `hcitool_patch`/`mpv_patch` под новые версии или отключение пакетов
      (KI-046).
- [ ] Сторонние `/oem`-blob'ы со старым ABI (`libssl.so.1.1`, Python 3.11):
      пересобрать из SDK или добавить compat-копии (KI-045; статус KI —
      «принятое ограничение миграции», 2026-11-09; задача — по решению
      владельца).

## 13. Сон камеры: proof-of-life кадром

- [x] **Snapshot (v1).** При тихом wake-цикле divinus перед повторным
      suspend отправляет JPEG через `[http_post]` на эндпоинт, производный
      от `http_post.url` (`frame` → `heartbeat`), с заголовками
      `X-Heartbeat`, `X-Heartbeat-Ts`, `X-OKO-Idle-S`,
      `X-OKO-Motion-Area`, `X-OKO-Sleep`, `X-OKO-Wake-Cycle`. Ключи
      `[sleep] heartbeat/heartbeat_interval_s/heartbeat_retries`, API
      `/api/sleep`, приёмник `PUT /ingest/heartbeat` +
      `GET /api/heartbeats`, `bench/heartbeat_check.py`. Сбой отправки не
      мешает сну (`fail_streak`). Проверка и soak — на стенде.
- [x] **Энергопрофиль `[sleep]` (2026-11-22).** Внешний INA3221 ESP32-моста
      (CH3 = вход 5 В DUT) + `bench/ina_log.py`/`bench/sleep_power.py`:
      `off` 0.8 мА, `idle` 364 мА / 1.72 Вт, полка deep suspend 86 мА
      (min 47, p05 124), окно 2.4 с за цикл ≈13 с, 4.9 мВт·ч/цикл (медиана);
      60-мин soak — 279 циклов, ≥200 heartbeat, `fail_streak=0`, `api_check` и
      `ui_check` 0; во время restore `/userdata` ушёл в `ro` (KI-071,
      power cycle вернул `rw`). Сверка с бортовым INA3221: −6.7 % тока.
      Артефакты — `output/bench/power/20261122-*`; методика — `TESTBENCH.md` §8.
- [x] **AOV MCU + интервал пробуждения 1 мин (закрыто 2026-11-25).**
      Причина прежнего поведения (deep suspend 2.4 с, цикл 12–18 с,
      `aov_state=off`, `mcu_run_count=0`) — единицы `PMU_WAKEUP_TIMEOUT_CNT`
      (KI-072, 2026-11-24). После правки: тихий сон 60.08 с при
      `wake_interval_s=60`; AOV-ветка (`aov_mcu=1` + `keep_isp=1`)
      просыпается по PMU-таймеру с MCU-детекцией (`md_flag=1`, `-h-mbox-`),
      подтверждено на NAND, ядра `#37`/`#38` (2026-11-24/25); интервал
      пробуждения 68–79 с из-за окна `wake_check` (§8.1). Артефакты
      `output/bench/power/20261125-073154-aov`,
      `output/bench/aov-test/summary_mbox500-20261125.20261125-073152.json`.
- [x] **Сон с детекцией движения (IVS), 2026-11-22.** `motion`-сценарий:
      3 цикла, IVS `area=1000‰` на каждом wake → `awake`, бодрствование
      24–26 с, 15.25–17.94 мВт·ч/цикл (среднее 16.85) против 4.9 мВт·ч при
      `wake_check=5`; парный цикл без IVS при `wake_check=30` — ~17–20 мВт·ч,
      т.е. основной вклад даёт длинное окно проверки (KI-079). Артефакты —
      `output/bench/power/20261122-motion14/`, `...-cycle-wc30/`.
      Ложное полнокадровое событие после wake закрыто 2026-11-24: окно
      устоятия `IVS_SETTLE_S=3` в `ivs_events.c` отбрасывает ≥ 900‰ после
      старта аналитики; `--scenario motion` × 3 — ни одного ложного события,
      реальное движение детектируется (`area=515‰`) (KI-079, артефакты
      `output/bench/power/20261124-222248-motion`, `.../20261125-073741-motion`).
- [ ] **MCU-frame heartbeat (v2, следующий шаг).** MCU пишет low-res
      JPEG/raw + статистику (площадь, счётчик кадров) в shared DDR (адрес
      meta/ISP-окна, протокол shared-буфера), AP на timer-wake читает
      готовый кадр и отправляет без подъёма media/ISP — дешевле Snapshot по
      энергии и времени цикла. Зависимости: handshake MCU и fast-AE уже
      работают (ядро `#38`, §8.1), блокирует только вендорский ISP-handover
      до suspend (KI-072, архитектура AOV); `RT_USING_JPEG_ENC` в
      MCU-сборке и протокол shared-буфера с валидацией кадра на AP не
      проверялись; wake по GPIO (`wakeup_mode=1` в `wakeup_param`). Статус:
      **запланировано, заблокировано ISP-handover (KI-072)**.

## 14. Разбор `fastboot_client.c` и перенос полезного в OKO

Статус: **анализ выполнен 2026-11-20, перенос запланирован; код не писался
и не тестировался.** Файл — вендорский (`project/app/fastboot_client/fastboot_client.c`,
один коммит вендора «Added fastboot_client and rk_smart_door for quick boot
applications»), поэтому по `AGENTS.md` §6 он остаётся справочником: править
его и строить на нём продуктовые фичи нельзя, весь полезный код переносится
в OKO-приложения.

### 14.1. Что это за файл

Не Android-fastboot, а демо вендорского thunder-boot: одиночный бинарник,
поднимающий ISP/VI/VENC/RTSP без `rkipc`. Фактическое состояние в репозитории:

- `Makefile` собирает его только при `RK_APP_TYPE`, содержащем
  `RK_FASTBOOT_CLIENT` (`project/app/fastboot_client/Makefile:43-47`); ни один
  BoardConfig в репо такого типа не задаёт.
- TB-линия использует `RKIPC_RV1106_WAKEUP_IPC`, а post-скрипт TB-образа
  удаляет бинарник (`oko-rv1106-tb-sc230ai-wakeup-post.sh:61`).
- В `output/out/oem/usr/bin/` бинарника нет; в `IMAGE/*/build_info.txt` только
  «Entering directory» без сборки.
- Единственный вызов в проде отсутствует: `rcS` TB-образа стартует `rkipc`.

### 14.2. Что найдено (результаты анализа)

| Механизм | Строки | Ценность для OKO |
|---|---|---|
| Порядок старта VENC → VI → bind → AIQ с комментарием «if is fast boot, must first init venc» | 775–819 | единственное в дереве подтверждение требования TB-линии; эталон для быстрого старта `divinus` |
| IQ из резервированной памяти: `get_cmd_val()` по `/proc/cmdline` (`rk_iqbin_addr/size`, `rk_color_mode`, `rk_night_mode`, `rk_cam_hdr`, `meta_load_addr/meta_part_size`), `mmap(/dev/mem)` + `preInit_iq_addr` / `preInit_scene` / `preInit_tb_info` | 653–673, 822–902 | запуск камеры, когда `/oem/usr/share/iqfiles` ещё не смонтирован; прямо связано с §13 v2 и AOV-треком |
| Smart IR целиком: GPIO ircut A/B и PWM IR-LED из `sensor_init_cfg` в meta, ожидание `/dev/block/by-name/meta`, day/night FSM, `make_meta --update --rk_color_mode`, автояркость под `RK_SMART_IR_AUTO_IRLED` | 140–264 | референс IR-логики при возврате к ИК-подсветке |
| `klog()` в `/dev/kmsg` + замеры стадий (`vi en`, `Aiq` в µs) | 676–685, 626, 927 | готовая методика измерения boot-тайминга через `dmesg`/UART |
| Ленивый RTSP: первые 30 кадров не отдаются, `create_rtsp_demo(554)` на 30-м | 385–408 | идея «прогрева» перед публикацией потока |
| Лог джиттера PTS в `/tmp/pts.txt` (`delay = now - pts`) | 291–313 | простая метрика end-to-end задержки |
| Смена разрешения на ходу (unbind → `SetChnAttr` → rebind) | 329–383 | **не переносить**: рискованно, у `divinus` есть свой ePTZ/кроп |

Дефекты, из-за которых код нельзя брать как есть:

- `vi_dev_init(0, 0)` закомментирован (781) — полагается на уже настроенный VI dev;
- `VENC_CHN_ATTR_S stJpegChnAttr` не обнуляется (memset закомментирован, 494);
- `int ret` в `switch_ir_thread` используется через `ret |=` без инициализации (173, 179) — UB;
- `get_cmd_val()` ищет `strstr(cmdline, string)` вместо `"key="`, `key_equal` формируется и не используется (666–669) — ложные совпадения по подстроке;
- `aiq_ctx` может остаться `NULL` после неудачного `sysctl_init`, но код продолжает работу (898–910);
- `system("make_meta ...")` вызывается из медиа-потока (234, 247) — блокирующая внешняя зависимость в реальном времени;
- фиксированный RTSP-порт 554 и запись в `/tmp/venc-test.bin`, `/tmp/pts.txt` конфликтуют с продуктовым медиасервером;
- функционально ~80 % файла дублирует `project/app/component/fastboot_server/fastboot_server.c`.

### 14.3. План переноса

- [x] **P1. Bench-probe таймингов.** Собран без правок под текущим SDK
      (`make -C project/app/fastboot_client fastboot_client-build`), запущен с
      UART-консоли вместо `divinus`. На продуктовом образе демо доходит до
      `vi chn2` за 13.6 мс и падает на `RK_MPI_SYS_Bind(VI1→VENC1)`:
      `vi_dev_init()` закомментирован (`:781`), `meta`/IQ-резерв и
      preconfigured VI dev есть только в TB-образе. Числовой бюджет «до
      первого кадра» для вендорской ветки недостижим; по плану P1 ограничен
      бюджетом `divinus` из P4. Обвязка — `bench/boot_probe.py`, артефакты —
      `output/bench/boot/` (подробности — §14.4).
- [x] **P2. Порядок инициализации.** Проверен по коду и меткам: `divinus`
      поднимает AIQ (`fnInit` → `fnPrepare` → `fnStart`) до VI и VENC;
      вендорский VENC→VI→bind→AIQ — требование thunder-boot, а не обычной
      загрузки. Перенос отклонён с доказательством (§14.4): выигрыш 15 мс из
      811 мс, риск для проверенной цепочки `rk_pipeline_create()`.
- [ ] **P3. IQ из резервированного окна.** Не начато: зависит от §13 v2 и
      вендорского ISP-handover (KI-072). Адреса согласуются с `RK_META_PARAM`;
      тот же механизм найден в `component/fastboot_server` (§15.2).
- [x] **P4. Наблюдаемость старта.** Добавлен `hal/boot_mark.c`/`.h`: метки
      `main`, `sys_init`, `aiq_init`, `aiq_prepare`, `aiq_start`, `vi`,
      `vi_bind`, `venc`, `bind`, `first_frame` в `/dev/kmsg` с мкс-дельтами;
      видны в `dmesg` и на UART-консоли стенда. Бюджет
      `main → first_frame` — **810 836 мкс (0.811 с)** (§14.4).
- [ ] **P5. Smart IR.** Не начато: ИК-железа на стенде нет (нет узла ircut и
      управляемой ИК-подсветки, `/api/night` отдаёт `999`). FSM day/night,
      GPIO ircut и PWM IR-LED сверять при возврате подсветки; см. §15.3.
- [x] **P6. Явно не переносить.** Решение зафиксировано: динамическая смена
      разрешения, `system()` из медиа-потока, фиксированный порт 554,
      глобальное состояние `g_*` и `get_cmd_val()` в текущем виде в OKO не
      переносятся.
- [x] **P7. Документация.** `STRUCTURE.md` — пометка, что `fastboot_client` —
      демо thunder-boot медиастека, а не Android fastboot, в образах не
      используется; `INIT_ORDER.md` §1.1 — ссылка на файл как на референс
      порядка старта TB-линии.

Зависимости: P3 связан с §13 v2 (MCU-frame heartbeat) и KI-072; P1–P2, P4
сделаны независимо. Критерий завершения — решение по каждому пункту
(перенесено/отклонено) зафиксировано в `CHANGELOG.md`, а измерения P1/P4
внесены в `VERIFIED_FUNCTIONS.md`.

### 14.4. Результаты P1/P2/P4 (стенд, 2026-11-25)

**P4 — метки стадий.** `hal/boot_mark.c` пишет в `/dev/kmsg` строку
`divinus-boot: <stage> dt=<us>`; ядро добавляет к ней секунды с загрузки,
поэтому метка видна и в `dmesg`, и в UART-логе. Дельта считается
`CLOCK_MONOTONIC` между соседними вызовами. Метки стоят в `main.c:56`,
`media.c` (`sys_init`), `hal/plus/rk_hal.c` (`aiq_init`, `aiq_prepare`,
`aiq_start`, `vi`, `vi_bind`, `venc`, `bind`) и в видео-цикле
(`first_frame`).

Измерение после `SIGHUP`-перезапуска (DUT `192.168.52.33`, ядро продуктового
образа; артефакт `output/bench/boot/20261125-110326-divinus.txt`):

| Стадия | Δ, мкс | Доля |
|---|---:|---:|
| `main` | 0 | — |
| `sys_init` | 194 780 | 24 % |
| `aiq_init` | 518 248 | 64 % |
| `aiq_prepare` | 6 338 | <1 % |
| `aiq_start` | 1 874 | <1 % |
| `vi` | 36 603 | 5 % |
| `vi_bind` | 5 210 | <1 % |
| `venc` | 2 192 | <1 % |
| `bind` | 12 638 | 2 % |
| `first_frame` | 32 953 | 4 % |
| **Итого `main → first_frame`** | **810 836** | **0.811 с** |

Повторный прогон 2026-11-25 (`output/bench/boot/20261125-110843-divinus.txt`)
дал 764 400 мкс (0.764 с); разброс 0.76–0.81 с определяется `aiq_init`
(450–518 мс).

**P2 — порядок.** Фактическая последовательность меток:
`sys_init → aiq_init → aiq_prepare → aiq_start → vi → vi_bind → venc → bind`.
Она противоположна вендорской в части AIQ/VENC: `fastboot_client` сначала
создаёт VENC и VI, затем bind, затем AIQ. Причина расхождения — назначение
кода: TB-образ приходит с уже сконфигурированным VI dev (MCU/загрузчик), и
первый кадр может появиться до AIQ, поэтому VENC обязателен раньше.
Продуктовый образ грузится обычной загрузкой (`rkisp_hw: is_thunderboot: 0`),
AIQ конфигурирует ISP до старта VI-потока, а `rk_video_create()` и
`rk_channel_bind()` в `divinus` требуют уже созданную VPSS-группу
(`hal/plus/rk_hal.c:359-477`). VENC-first означал бы разрыв этой цепочки;
измеренный выигрыш (VENC+bind) — 15 мс из 811 мс. **Перенос не выполняется.**

**P1 — вендорская ветка.** Файл собирается без правок (1 814 648 Б). Прогон
с UART-консоли при остановленном `divinus`: метки `main` → `SYS_Init` →
`venc chn1` → `venc chn2` → `vi ch1` → `vi chn2` за 13.6 мс, далее выход на
`RK_MPI_SYS_Bind`. Причины: `vi_dev_init(0, 0)` закомментирован (`:781`);
`/proc/cmdline` продукта не содержит `meta_load_addr`/`rk_iqbin_addr`, а
`/dev/mem`-окно meta/IQ существует только в TB-разметке. Дополнительный
дефект: путь `__FAILED` использует неинициализированные `fd`, `iq_mem`,
`aiq_ctx`, `rk_night_mode` (`:822`, `:956-970`) — UB, поэтому процесс
завершается без `main service exit`. Итог: числовой бюджет вендорской ветки
на продукте не снимается, P1 закрыт в объёме фолбэка (бюджет `divinus`).
Артефакт — `output/bench/boot/20261125-110452-vendor.txt`.

## 15. Каталог остатков вендорского кода и аудит ядра

Статус: аудит выполнен 2026-11-25 в режиме read-only; интеграция этого цикла —
только §14 (P1/P2/P4). Вендорские файлы не редактировались (`AGENTS.md` §6).
Сборка `project/app` собирает все каталоги с `Makefile` плюс явно
`component/fastboot_server` (`project/app/Makefile:17-25`); `rkipc` исключён
фильтром и возвращается только для `RK_APP_TYPE=RKIPC_RV1106_WAKEUP_IPC`.

### 15.1. Каталог остатков (A1)

Активная конфигурация: `.BoardConfig.mk` → `BoardConfig-SPI_NAND-Buildroot-RV1106_Luckfox_Pico_Pro_Max-SC230AI.mk`
(`RK_APP_TYPE=RKIPC_RV1106`, `RK_ENABLE_WIFI=y`, `RK_APP_IPCWEB_BACKEND`
закомментирован, `RK_ENABLE_FASTBOOT` не задан).

| Каталог | Назначение | Релевантность RV1106/SC230AI | Статус в сборке продукта | Лицензия | Вердикт |
|---|---|---|---|---|---|
| `component/fastboot_server` | TB-сервер: одиночный/dual/multi-cam быстрый старт медиастека, v4l2 luma link | TB-линия, другой сенсор/несколько камер | нет: `PKG_TARGET` пуст без `RK_FASTBOOT_SERVER`/`SMART_DOOR_RV1106_RMSL`/`RK_FASTBOOT_CLIENT_DUAL_CAMERA` (`Makefile:74-75`) | нет файла/заголовка лицензии — вендорский дроп SDK | справочник (§15.2) |
| `component/rkadk` | RKADK: media/record/RTSP/AOV examples + PDF-гайды | RV1106 поддержан, но API не используется OKO | нет: каталог не входит в `app_src`, ссылок из других Makefile нет | Rockchip BSD-3 (`rkadk/rkadk/LICENSE`) | **удалён 2026-11-25** (§15.7) |
| `component/lvgl` | LVGL GUI-библиотека | только CVR/face-gate сценарии | нет: `CONFIG_LVGL`/`RK_ENABLE_LVGL` не заданы (`Makefile:36-40`) | MIT (`lvgl/LICENCE.txt`) | **удалён 2026-11-25** (§15.7) |
| `component/rkfsmk` | prebuilt `librkfsmk.so` + заголовок: FAT check/format, `repair_mp4`, выравнивание файлов | собирается в TB-ветке `rkipc` | нет: не в `app_src`; `out/` — устаревший артефакт | исходников нет; заголовок Apache-2.0 | **удалён 2026-11-25** (§15.7) |
| `component/third_libs` | prebuilt freetype2/iconv/libjpeg9/libpng16 и libs по тулчейнам | зависимости `rkipc`/`rk_smart_door` | нет на продукте: вызывается только из `rkipc`/`rk_smart_door` | смешанные лицензии библиотек; своего LICENSE нет | справочник |
| `uvc_app_tiny` | UVC-gadget демо (ISP/VI/VENC/UAC) | RV1106 UVC не используется | нет: `PKG_TARGET` пуст без `UVC_TINY` (`Makefile:27-31`) | Rockchip BSD-3 (`uvc_app/LICENSE`) | игнор |
| `wifi_app` | wpa_supplicant 2.6 / hostapd / libnl / Realtek-сборки | **да**: `RK_ENABLE_WIFI=y` включает `build-wifi` | **да**, активна (`Makefile:45-46`) | wpa_supplicant/hostapd BSD, libnl LGPL-2.1 | справочник (активная сборка, не трогать) |
| `STM32CubeC0` | STM32C0 HAL/CMSIS (C011/C031) | нужен для `stm32_acpi-ec` | не входит в сборку SDK; gitignored локальная зависимость (`.gitignore:13-14`) | Apache-2.0 + BSD-3 (`LICENSE.md`) | игнор (SDK-зависимость) |
| `STM32CubeF1` | STM32F1 HAL/CMSIS | F1-порт EC (историческая линия) | то же | Apache-2.0 + BSD-3 | игнор |
| `stm32_acpi-ec` | прошивка EC STM32 (CBT6, proto v3) | **да**, стенд и `GET_POWER`/`SET_POWER` | собирается отдельно: CMake + `arm-none-eabi`, не через `project/app/Makefile` | MIT (`LICENSE`) | справочник (git subtree, `STM32_FIRMWARE.md`) |
| `rkipc` | вендорский IPC-демон | только TB-линия | продукт: нет (фильтр `project/app/Makefile:23`); TB: да | Rockchip BSD-3 (`rkipc/LICENSE`) | справочник; backlog — §15.3 |
| `ipcweb` | REST-бэкенд для `rkipc` (Cgicc/JWT) | нет: зависит от rkipc socket-server | нет: `RK_APP_IPCWEB_BACKEND` не задан (`Makefile:32-33`) | Rockchip BSD-3 | **удалён 2026-11-25** (§15.7) |
| `rk_smart_door` | демо умной двери: RMSL/IR-face, UVC/AI/AO | RV1103/1106, другой сенсор (sc3338) | нет: `PKG_TARGET` пуст без `SMART_DOOR_*` (`Makefile:16-26`) | Rockchip BSD-3 (`smart_door/LICENSE`) | только выводы (§15.4) |

OKO-код (`divinus`, `oko-*`, `stm8-motor`, `vpn-tools`) и MCU-SDK вне
каталога остатков; `fastboot_client` — в §14.

### 15.2. `component/fastboot_server` (B1)

Сверка с уже разобранным `fastboot_client.c` подтверждает оценку «~80 % дубль»:
те же `klog()`, `get_cmd_val()`, `vi_dev_init()`/`vi_chn_init()`,
`test_venc_init()` и `RK_MPI_SYS_Init`-последовательность.

| Механизм | Файл:строки | Ценность | Дефекты | Вердикт |
|---|---|---|---|---|
| Одиночный сервер (meta из `/dev/mem`, VENC→VI→bind→AIQ) | `fastboot_server.c:430-729` | подтверждает порядок TB и даёт второй экземпляр меток | `get_cmd_val()` по подстроке; `aiq_ctx` без проверки после `sysctl_init`; `fork()` без `wait` | не переносить (дубль) |
| meta/app-param из `meta_load_addr` | `fastboot_server.c:458-506` | тот же приём, что P3, но от `/dev/mem` | `metaSize` и смещения не валидируются (`MAP_FAILED` проверяется, размер — нет) | отложено вместе с P3 |
| `fork()` после bind + `/tmp/aov_lunch` | `fastboot_server.c:556-572` | способ оставить сервис после выхода родителя | глобальное состояние, скрытая вторая копия медиастека | не переносить |
| Dual-camera: AVS-склейка, ePTZ, cover/OSD, RTSP, два AIQ-контекста | `fastboot_server_dual_camera.c:206-280, 327-339, 576-808, 911-1017, 1377-1490` | ePTZ и OSD-идеи (в `divinus` уже есть), AVS — только для двух сенсоров | жёсткие `CAM_NUM=2`, `MMAP_START_FROM_ZERO`, OSD `/oem/usr/share/osd_image.data` | не переносить (нет второго сенсора) |
| Multi-cam smart door: luma-link ISP flood/projector (`/dev/media3`, `/dev/media4`) | `fastboot_server_multi_cam.c:437-444, 446-682`; `v4l2_link.c` | работа с media-graph через `mediactl` | привязано к ИК-подсветке двери и двум ISP-сущностям | не переносить |
| VENC `GetStream`-цикл и JPEG combo | `fastboot_server_dual_camera.c:281-390` | prime-кадры до RTSP (в `divinus` есть свой) | блокирующие вызовы в цикле | справочник |

Вывод: уникальные части — multi-sensor и smart-door (AVS, luma-link, IR);
для OKO переносимых механизмов нет, кроме уже учтённых в §14.

### 15.3. `rkipc` вне AF: backlog (B2)

Сверено с `divinus`: `night.c` (160), `region.c` (676), `ivs_events.c` (565),
`server.c` (`/api/isp` :2128, `/api/night` :2439, `/api/osd` :2517,
`/api/ivs` :5095), собственный RTSP-сервер и `rk_af_stats.c`. Интеграция —
следующим циклом после согласования приоритетов; сейчас только backlog.

| Механизм | `rkipc/rkipc/...` | Состояние в `divinus` | Приоритет | Вердикт |
|---|---|---|---|---|
| PWM IR-LED: `light_strength`, `light_brightness`, `fill_light_mode` | `common/isp/rv1106/isp.c:247-268, 622-677` | только GPIO on/off (`night.c:36-40`), PWM нет | высокий (при возврате ИК) | кандидат переноса |
| Гистерезис day/night: `filter_level`, `filter_time` | `common/isp/rv1106/isp.c:678-722` | нет; переключение по ADC/GPIO без гистерезиса | средний | кандидат переноса |
| IQ из резервной памяти (`rk_isp_fastboot_init`) | `common/isp/rv1106/isp.c:1690-1774` | нет (P3) | высокий, но заблокирован KI-072 | отложено |
| Smart IR FSM (rk_smart_ir) | `project/app/fastboot_client/fastboot_client.c:140-264` (референс, не rkipc) | нет | низкий до ИК-железа | отложено (P5) |
| OSD: bitmap-шрифты, `region_clip` | `common/osd/osd.c` (948), `common/region_clip/region_clip.c` (143) | 2BPP RGN + свой `region.c` | низкий | справочник |
| ROI-кроп для энкодера | `common/roi/roi.c` | ePTZ-кроп (`/api/eptz`) | низкий | не переносить |
| IVS rockiva (person/vehicle) | `common/rockiva/rockiva.c`, `common/ivm/event.c` (415) | IVS MD/OD + YOLO | низкий (лицензия библиотеки) | отложено |
| socket-server IPC (5 860 строк) | `common/socket_server/server.c` | HTTP REST в `divinus` | — | не нужен |
| RTSP-обвязка easymedia | `common/rtsp/rtsp.c` (139) | собственный RTSP | — | не нужен |
| Групповой ISP (multi-cam) | `common/isp/rv1106/isp.c:1946-1975` | один сенсор | — | не нужен |

Приоритеты: PWM IR и IQ-резерв — после появления ИК-железа и снятия KI-072;
гистерезис — при следующей правке `night.c`; OSD/ROI/rockiva — по запросу.

### 15.4. `rk_smart_door` (B3)

- Лицензия: `smart_door/LICENSE` — текст Rockchip BSD-3-стиля (redistribution
  разрешено), а не «без лицензии». Копирование кода всё равно запрещено:
  `AGENTS.md` §6 (вендорский код — только справочник).
- `rv1106_normal/main.c:88-193`: `RK_MPI_SYS_Init` → `rk_param_init` →
  `rk_isp_configure_media_pipeline` → `rkaiq_3a` → цикл команд из
  `/tmp/smart_door.ini` (rtsp/ai/ao/uvc). Полезного для `divinus` нет:
  `rkaiq_3a` и `rk_param` — вендорские обёртки, а порядок AIQ после VI
  противоположен измеренному в §14.4.
- `rv1106_ir_face` (`comm_isp.c`, `vi_face.cpp`) и `rv1106_rmsl`
  (`configs_skv.json`, `wifi/wifi_cmd.c`) завязаны на RMSL/face-библиотеки,
  сенсор sc3338 и `librknnmrt`; на OKO-железе не воспроизводятся.
- Вердикт: справочник; выводы — только «порядок TB-демо иной, чем у
  продуктового `divinus`». Интеграции нет.

### 15.5. `ipcweb` (B4)

`ipcweb-backend` — CGI/REST-фронт над `rkipc` через `socket_client`
(`src/rest_api.cpp:165-218`): network/NTP/DDNS/PPPoE/port, storage, video,
audio, stream URL, image, system, OSD, ROI, event, peripherals; JWT/Cgicc.
Собственного медиастека нет. Так как OKO не собирает `rkipc`
(`project/app/Makefile:23`), весь слой неприменим; `divinus` уже отдаёт
`/api/*` с той же функциональностью (проверено `bench/run.sh api_check.py`,
138 проверок). Вердикт: **не нужен**; возможные заимствования — только схема
ROI/DDNS/PPPoE, если появятся такие задачи.

### 15.6. `research/rv1106-aov` → интеграционные вердикты (B5)

- **Overshoot 68–79 с (`wake_check`).** Методика сокращения: снимать окно по
  меткам `[sleep]` и `power`-артефактам (`output/bench/power/*`), сравнивать
  фактический цикл с `wake_interval_s`; кандидаты — уменьшить окно проверки и
  `IVS_SETTLE_S`, не трогая PMU-таймер. Работа — следующая integration-итерация,
  в этом цикле не выполняется (§8.1).
- **ISP-handover.** Блокировка подтверждена: в `research/rv1106-aov/README.md`
  handover описан как обязанность MCU-стороны; AP-кодом не снимается (KI-072).
- **JTAG / `mcutool`.** Только шаг 4–5 порядка проверок README темы и только на
  debug-образе: `mcutool` (`0xff6c0000`) конфликтует с AOV-раскладкой OKO
  (`HPMCU` из idblock, `rtos@40000`). Вне цикла.
- **Frame-sync master/slave и dynamic FPS SC230AI.** Отложено: потребительской
  задачи нет; DTS sync-режим не задаёт. Регистры-кандидаты — `0x3225`,
  `0x3216/17`, `0x322e/2f` (master) и `0x320f/0x3230/0x3231` (slave),
  `vts=33750/fps`.
- **«Медиа-грабли» dehai — регресс-чеклист.** Ниже сжатая сверка с OKO.

| Пункт dehai | Состояние в OKO | Что проверить |
|---|---|---|
| `VI GetChnFrame` для NV12 ненадёжен | YOLO берёт VI0/VI1/VPSS3, проверено на стенде | при смене источника кадра |
| RGA-цель только MMZ, `MmzFlushCache(RK_FALSE)` | `rk_rga.c` импортирует fd, конвертер опционален | при включении RGA-конвертера |
| stride/UV по `vir_height` | `yolo.c` учитывает `virWidth`/`size_with_stride` | граничные разрешения |
| не блокировать в `GetStream` | `rk_video_thread` копирует пакет до `free` | при правках видео-цикла |
| stage MP4 на TF + `rename` | `record.c` пишет файл напрямую | риск обрезанного файла при сбое |
| OSD `pos` кратен 16 | `rk_rgn_pad16` | — |
| fMP4 `trun=0x701`, `tfdt` v1 | свой writer `fmt/moof.c` | сверка с плеерами |
| RKNN грузить лениво | `models_start`/`yolo_start` по конфигу | — |
| IVS `MDInterval`/пороги | `ivs_events.c` + defaults | — |
| watchdog/flock, остановка чужого rkipc | `oko-lunch` — единственный стример | — |

Вердикты §15.4–§15.6 перенесены в §8.1 (AOV) и `CHANGELOG.md`; новых
документов не создавалось.

### 15.7. Удалённый вендорский код и upstream-ссылки

2026-11-25 удалены каталоги, которые не включает ни один BoardConfig, не
собирает продуктовый OKO и чей код/API никто не вызывает. Исходники остаются
в вендорском репозитории
[`LuckfoxTECH/luckfox-pico`](https://github.com/LuckfoxTECH/luckfox-pico/tree/main);
ниже — прямые ссылки и отметки полезного на случай возврата.

| Удалённый каталог | Upstream | Что там полезного | Почему удалён |
|---|---|---|---|
| `project/app/component/rkadk` | [rkadk](https://github.com/LuckfoxTECH/luckfox-pico/tree/main/project/app/component/rkadk) | RKADK API: record/RTSP/AOV/photo/stream, examples (`rkadk_aov_record_test.c`, `rkadk_record_test.c`), PDF-гайды; разбор — [`research/rkadk-guide/`](research/rkadk-guide/README.md) | 111 файлов, 3.4 МБ; ни одна цель `project/app` его не вызывает |
| `project/app/component/lvgl` | [lvgl](https://github.com/LuckfoxTECH/luckfox-pico/tree/main/project/app/component/lvgl) | LVGL 9 и демо для CVR/face-gate GUI | 1088 файлов, 95 МБ; `RK_ENABLE_LVGL`/`CONFIG_LVGL` не заданы нигде |
| `project/app/ipcweb` | [ipcweb](https://github.com/LuckfoxTECH/luckfox-pico/tree/main/project/app/ipcweb) | REST-бэкенд над rkipc socket-server: схемы OSD/ROI/DDNS/PPPoE, JWT, `www-rkipc` | 282 файла, 5.7 МБ; `RK_APP_IPCWEB_BACKEND` закомментирован во всех BoardConfig, зависит от снятого rkipc |
| `project/app/component/rkfsmk` | [rkfsmk](https://github.com/LuckfoxTECH/luckfox-pico/tree/main/project/app/component/rkfsmk) | prebuilt `librkfsmk.so` без исходников: FAT check/format (`FsCheck`, `GetFat/SetFat`), `repair_mp4`, `kernel_*` предвыделение с выравниванием; заголовок Apache-2.0 | 8 файлов, 1.7 МБ; API (`rkfsmk_*`, `repair_mp4`, `kernel_*`) нигде не вызывается, `CMakeLists` не линкует `-lrkfsmk`; в `rkipc/Makefile` остаётся только guard `test ! -d`, поэтому правка вендорского Makefile не потребовалась |

Проверка после удаления: `make -C project/app info` проходит, удалённых
каталогов в `app_src` нет; OKO-код на них не ссылается. Для TB-ветки
`make -C project/app/rkipc rkfsmk-build` завершается успешно: цель
`rkfsmk-build` проверяет `test ! -d $(RK_APP_COMPONENT)/rkfsmk` и при
отсутствии каталога ничего не делает.

Кандидаты, оставленные осознанно (удалять только при отказе от
соответствующих вендорских профилей):

| Каталог | Upstream | Что полезного | Ограничение |
|---|---|---|---|
| `project/app/rk_smart_door` | [rk_smart_door](https://github.com/LuckfoxTECH/luckfox-pico/tree/main/project/app/rk_smart_door) | `rv1106_normal/main.c` (порядок SYS_Init → VI → rkaiq_3a), `common/uart/uart.c`, `wifi/wifi_cmd.c` | 126 МБ; нужен двум вендорским FASTBOOT-профилям (`RK_APP_TYPE="RK_FASTBOOT_SERVER SMART_DOOR_RV1106_NORMAL"`) и `luckfox-rv1106-tb-emmc-post.sh`; RMSL/face и sc3338 для OKO не переносимы |
| `project/app/component/fastboot_server` | [fastboot_server](https://github.com/LuckfoxTECH/luckfox-pico/tree/main/project/app/component/fastboot_server) | `fastboot_server_dual_camera.c` (AVS, ePTZ, OSD/cover), `v4l2_link.c` (media-graph через mediactl) | 189 КБ; те же два вендорских профиля; разбор — §15.2 |
| `project/app/uvc_app_tiny` | [uvc_app_tiny](https://github.com/LuckfoxTECH/luckfox-pico/tree/main/project/app/uvc_app_tiny) | UVC/UAC gadget (`uvc/`, `uac/`), ISP-обвязка | 37 МБ; `UVC_TINY` не задан ни в одном BoardConfig; ранее было решение «не трогать» (cleanup-план `1794156817413`, слой 3) |
| `project/app/fastboot_client` | [fastboot_client](https://github.com/LuckfoxTECH/luckfox-pico/tree/main/project/app/fastboot_client) | P1-референс: VENC→VI→bind→AIQ, `klog()`, IQ из meta | 39 КБ; включается только вручную, оставлен как компактный справочник §14 |
| `project/app/rkipc` | [rkipc](https://github.com/LuckfoxTECH/luckfox-pico/tree/main/project/app/rkipc) | IPC-демон, socket-server, OSD/IVS/ISP | 59 МБ; единственный потребитель — TB-линия `RKIPC_RV1106_WAKEUP_IPC`; backlog — §15.3 |
| `component/third_libs` | [third_libs](https://github.com/LuckfoxTECH/luckfox-pico/tree/main/project/app/component/third_libs) | freetype2/iconv/libjpeg9/libpng16 и libs по тулчейнам | 7.5 МБ; зависимость `rkipc`/`rk_smart_door` |

Вне git и удаляются/восстанавливаются отдельно: `project/app/STM32CubeC0`
(105 МБ) и `project/app/STM32CubeF1` (150 МБ) — локальные SDK для
`stm32_acpi-ec` в `.gitignore`; команды клонирования — `STM32_FIRMWARE.md`.
Их удаление не меняет репозиторий.
