# RKNN Toolkit 2 (Docker)

Сборка и квантование YOLOv5n для `rv1106` без установки `rknn-toolkit2` на хост:
на хосте только Docker. Образы собираются из официального колеса
`rknn-toolkit2` **1.6.0** и форка `airockchip/yolov5`; артефакты кладутся в
`output/rknn/` (gitignore).

## Образы

```sh
docker build -f tools/rknn-toolkit2/Dockerfile.toolkit \
    -t oko-rknn-toolkit2:1.6.0 tools/rknn-toolkit2
docker build -f tools/rknn-toolkit2/Dockerfile.export \
    -t oko-yolov5-export tools/rknn-toolkit2
```

`Dockerfile.toolkit` ставит колесо с `--no-deps` и только минимальный набор
пакетов для ONNX-конвертации: полный набор зависимостей колеса тянет
TensorFlow и Torch, которые конвертеру не нужны.

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

```sh
tools/rknn-toolkit2/export_yolov5.sh 384 640
```

Аргументы — высота и ширина. Скрипт клонирует `airockchip/yolov5` на
закреплённый коммит, скачивает `yolov5n.pt`, экспортирует с `--rknpu` и
сохраняет `output/rknn/yolov5n-640x384.onnx`. Выход — три 4D-тензора
(`255` каналов), sigmoid уже в графе; `RK_anchors.txt` совпадает с якорями
`yolo_postprocess.c`.

## Квантование

```sh
tools/rknn-toolkit2/convert_rknn.sh \
    output/rknn/yolov5n-640x384.onnx \
    output/bench/calib \
    output/rknn/yolov5n-640x384.rknn
```

Конвертер: `target_platform='rv1106'`, INT8 (`asymmetric_quantized-8`),
`mean=0`, `std=255`. Готовый файл — около 2.6 МиБ, укладывается в квоту
загрузки 3 МиБ.

## Второй рантайм 2.x

Модель, собранная toolkit 2.x, не обязана грузиться рантаймом 1.6.0, поэтому
в образе может лежать вторая копия мини-рантайма
(`/oem/usr/lib/librknnmrt2.so`, см. `media/rknn2/README.md`). Образ
конвертера для 2.x собирается отдельно (вендор поставляет только cp38-колесо,
поэтому база — Python 3.8):

```sh
docker build -f tools/rknn-toolkit2/Dockerfile.toolkit2 \
    -t oko-rknn-toolkit2:2.3.2 tools/rknn-toolkit2
RKNN_TOOLKIT_IMAGE=oko-rknn-toolkit2:2.3.2 \
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
    'http://<DUT_IP>/api/models/activate?name=yolov5n-r2.rknn'
```

`yolo.runtime` применяется к следующей активации без рестарта; AIISP при этом
продолжает работать на 1.6.0 в том же процессе. `models.json` помнит рантайм
каждой модели, и активация «чужой» модели отклоняется с причиной. mAP
2.x-модели на размеченном наборе пока не измерялся — сравнение с 1.6.0 по
точности остаётся открытым пунктом.

## Загрузка и проверка

```sh
curl -fsS -u admin:oko-change-me \
    -F 'file=@output/rknn/yolov5n-640x384.rknn;filename=yolov5n.rknn' \
    http://<DUT_IP>/api/models
curl -fsS -u admin:oko-change-me -X POST -d 'name=yolov5n.rknn' \
    http://<DUT_IP>/api/models/activate
```

Размер входа должен совпадать с `yolo.input_width/height` (по умолчанию
640×384). При активации `divinus` пишет в консоль:

```
[yolo] Model /userdata/models/yolov5n.rknn: input 640x384 NHWC stride 640 px,
       3 4D outputs (48x80x255, 24x40x255, 12x20x255)
```

Проверка на стенде (2026-11-05): 640×384 при 5 fps — NPU 17%, CPU `divinus`
~28.5% (без YOLO ~7%), main 30.3 fps; 640×640 при 5 fps — NPU 28%, CPU ~66%.
Симулятор toolkit на `bus.jpg` даёт 3 person + 1 truck; mAP на валидационном
наборе пока не измерялся.

## Ограничения

- Модель `.rknn` в репозиторий не коммитится; её место — `output/rknn/` или
  `/userdata/models` на устройстве.
- Для другой модели/входа повторите экспорт и квантование; квоту 3 МиБ и
  `yolo.input_width/height` менять синхронно.
- Образы большие (Torch/EXPORT ~2.5 ГБ); при повторной сборке слои кешируются.
