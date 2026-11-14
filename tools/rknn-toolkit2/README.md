# RKNN Toolkit 2 (Docker)

Сборка и квантование YOLO-моделей для `rv1106` без установки `rknn-toolkit2`
на хост: на хосте только Docker. Образы собираются из официальных колёс
`rknn-toolkit2` **1.6.0** и **2.5.0**, ONNX для YOLOv5 берётся из форка
`airockchip/yolov5`, для YOLO11 — из `airockchip/ultralytics_yolo11`;
артефакты кладутся в `output/rknn/` (gitignore).

## Образы

```sh
# 1.6.0 — рантайм librknnmrt.so (AIISP, RockIVA, старые YOLO-модели)
docker build -f tools/rknn-toolkit2/Dockerfile.toolkit \
    -t oko-rknn-toolkit2:1.6.0 tools/rknn-toolkit2
docker build -f tools/rknn-toolkit2/Dockerfile.export \
    -t oko-yolov5-export tools/rknn-toolkit2

# 2.5.0 — рантайм librknnmrt2.so и YOLO11 (RKOPT, 9 выходов)
docker build -f tools/rknn-toolkit2/Dockerfile.toolkit2 \
    -t oko-rknn-toolkit2:2.5.0 tools/rknn-toolkit2
docker build -f tools/rknn-toolkit2/Dockerfile.export11 \
    -t oko-yolo11-export tools/rknn-toolkit2
```

`Dockerfile.toolkit*` ставят колесо с `--no-deps` и только минимальный набор
пакетов для ONNX-конвертации: полный набор зависимостей колеса тянет
TensorFlow и Torch, которые конвертеру не нужны.

Версии 2.5.0 pinned по `sha256sum.txt` вендора:

| Компонент | Версия | Зачем |
|---|---|---|
| колесо toolkit2 | `2.5.0-cp38` | INT8-конвертация для rv1106 |
| `protobuf` | `4.25.4` | требование 2.5.0 (`>=4.21.6,<=4.25.4`) |
| `onnx` / `onnxruntime` | `1.17.0` / `1.17.3` | `>=1.16.1` / `>=1.17`, ещё есть cp38 |
| `numpy` | `1.24.4` | требование `<=1.26.4`, последняя ветка с cp38 |
| `torch` | `1.10.1+cpu` | toolkit импортирует torch, конвертации хватает CPU |

## Калибровочные кадры

Нужно 20–200 JPEG с рабочей сцены (день/ночь, то же освещение, что в проде).
Снимать через HTTP-снимок DUT:

```sh
mkdir -p output/bench/calib
for i in $(seq -w 1 100); do
    curl -fsS -u admin:oko-change-me \
        -o "output/bench/calib/frame-$i.jpg" http://<DUT_IP>/image.jpg
    sleep 0.15
done
```

## Экспорт ONNX

### YOLOv5n (3 выхода, 3*(5+classes))

```sh
tools/rknn-toolkit2/export_yolov5.sh 384 640
```

Аргументы — высота и ширина. Скрипт клонирует `airockchip/yolov5` на
закреплённый коммит, скачивает `yolov5n.pt`, экспортирует с `--rknpu` и
сохраняет `output/rknn/yolov5n-640x384.onnx`. Выход — три 4D-тензора
(`255` каналов), sigmoid уже в графе; `RK_anchors.txt` совпадает с якорями
`yolo_postprocess.c`.

### YOLO11n (RKOPT, 9 выходов)

```sh
tools/rknn-toolkit2/export_yolo11.sh 384 640
```

Скрипт клонирует `airockchip/ultralytics_yolo11` на коммит
`0692e9297670acf4cc6d0cec773d7a9493cb8a5f`, скачивает `yolo11n.pt`
(ultralytics assets v8.3.0) и запускает `ultralytics/engine/exporter.py` с
`format: rknn`. Важно: RKOPT-ветка экспорта включается именно
`format: rknn`; при `format: onnx` получается обычный граф
`84x5040` без DFL-разбиения.

Результат — `output/rknn/yolo11n-640x384.onnx`, вход `[1,3,384,640]`,
9 выходов на трёх масштабах: `[1,64,H,W]` (box, 16 бинов DFL × 4),
`[1,80,H,W]` (классы, sigmoid в графе) и `[1,1,H,W]` (сумма классов).

## Квантование

```sh
# YOLOv5n под рантайм 1.6.0
tools/rknn-toolkit2/convert_rknn.sh \
    output/rknn/yolov5n-640x384.onnx \
    output/bench/calib \
    output/rknn/yolov5n-640x384.rknn

# YOLO11n под рантайм 2.5.0
RKNN_TOOLKIT_IMAGE=oko-rknn-toolkit2:2.5.0 \
  tools/rknn-toolkit2/convert_rknn.sh \
    output/rknn/yolo11n-640x384.onnx \
    output/bench/calib \
    output/rknn/yolo11n-640x384-r2.rknn
```

Конвертер: `target_platform='rv1106'`, INT8 (`asymmetric_quantized-8`),
`mean=0`, `std=255`. Размеры на калибровке 2026-11-14 (60 кадров с DUT):

| Модель | Файл | Размер | Квота загрузки |
|---|---|---|---|
| YOLOv5n 640×384 | `yolov5n-640x384.rknn` | 2 656 727 Б | помещается |
| YOLOv5n 640×384 toolkit 2.5.0 | `yolov5n-640x384-r2.rknn` | 2 659 026 Б | помещается |
| YOLO11n 640×384 toolkit 2.5.0 | `yolo11n-640x384-r2.rknn` | 3 905 345 Б | 4,3 МиБ (было 3 МиБ) |

Квота `analytics/models.h` поднята до 4386 КиБ (измеренный размер +15 %),
буфер HTTP-запроса в `server.c` — до 10 МиБ. Модель больше квоты отклоняется
с читаемым сообщением, а не обрывом соединения.

## Загрузка и проверка

```sh
curl -fsS -u admin:oko-change-me \
    -F 'file=@output/rknn/yolo11n-640x384-r2.rknn;filename=yolo11n-640x384-r2.rknn' \
    http://<DUT_IP>/api/models
curl -fsS -u admin:oko-change-me -X POST -d 'name=yolo11n-640x384-r2.rknn' \
    http://<DUT_IP>/api/models/activate
```

Архитектура определяется по числу выходов (`3` → yolov5, `9` → yolo11) и
пишется в `models.json` (поле `arch`) и в `/api/yolo` (`"arch":"yolo11"`).
При активации `divinus` пишет в консоль:

```
[yolo] Model /userdata/models/yolo11n-640x384-r2.rknn: input 640x384 NHWC
       stride 640 px, arch=yolo11 9 outputs (48x80x64, 48x80x80, 48x80x1, ...)
```

YOLO11-модель требует `yolo.runtime=2`: при `runtime=1` активация
отклоняется с причиной «YOLO11 models require RKNN runtime 2».

Проверено на стенде 2026-11-14: YOLO11n 640×384 при 5 fps — NPU 28 %
(YOLOv5n — 17 %), CPU `divinus` ~12 %, детектор отдаёт историю через
`/api/yolo?detections=1`; сквозное событие с роликом проверяет
`bench/run.sh event_check.py`.

## Второй рантайм 2.x

Модель, собранная toolkit 2.x, не обязана грузиться рантаймом 1.6.0, поэтому
в образе лежит вторая копия мини-рантайма
(`/oem/usr/lib/librknnmrt2.so`, см. `media/rknn2/README.md`). Рантайм —
**2.5.0** (собран `Dockerfile.runtime`, SONAME перештампован в
`librknnmrt2.so`).

```sh
RKNN_TOOLKIT_IMAGE=oko-rknn-toolkit2:2.5.0 \
  tools/rknn-toolkit2/convert_rknn.sh \
    output/rknn/yolov5n-640x384.onnx \
    output/bench/calib \
    output/rknn/yolov5n-640x384-r2.rknn
```

Активация на устройстве:

```sh
curl -fsS -u admin:oko-change-me -X POST \
    'http://<DUT_IP>/api/yolo?runtime=2'
curl -fsS -u admin:oko-change-me -X POST \
    'http://<DUT_IP>/api/models/activate?name=yolov5n-640x384-r2.rknn'
```

`yolo.runtime` применяется к следующей активации без рестарта; AIISP при этом
продолжает работать на 1.6.0 в том же процессе. `models.json` помнит рантайм
и архитектуру каждой модели.

## Ограничения

- Модель `.rknn` в репозиторий не коммитится; её место — `output/rknn/` или
  `/userdata/models` на устройстве.
- Для другой модели/входа повторите экспорт и квантование; квоту и
  `yolo.input_width/height` менять синхронно.
- `Hanzo-Huang/rk3576-ai-demos/models/yolo11n.rknn` — модель под RK3576,
  без указания источника/лицензии. На rv1106 она не проверялась и через API
  не загружается: 7,26 МБ больше квоты 4,3 МиБ (ответ API — читаемая ошибка,
  а не `rknn_init` с `-13`).
- mAP YOLO11 на размеченном наборе не измерялся; сравнение с YOLOv5 по
  точности остаётся открытым пунктом.
- Образы большие (Torch/EXPORT ~2.5 ГБ); при повторной сборке слои кешируются.
