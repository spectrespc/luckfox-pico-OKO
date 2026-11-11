# Пакет oko-lunch для OKO

Лаунчер приложения OKO: два POSIX-sh скрипта, которые ставятся в oem-раздел и
заменяют вендорские `RkLunch.sh`/`RkLunch-stop.sh` из пакета `rkipc`.
Пакет `rkipc` в образе больше не собирается (см. `project/app/Makefile`),
его исходники остаются в дереве как справочные.

| Файл в образе | Исходник | Назначение |
|---|---|---|
| `/oem/usr/bin/oko-lunch.sh` | `files/oko-lunch.sh` | запуск приложения |
| `/oem/usr/bin/oko-lunch-stop.sh` | `files/oko-lunch-stop.sh` | останов приложения |

Вызыватель — `/etc/init.d/S21appinit`, генерируемый heredoc-ом в
`project/build.sh` (`__PACKAGE_OEM`): `start` → `oko-lunch.sh`,
`stop` → `oko-lunch-stop.sh`.

## Состав пакета

- `Makefile` — копирует `files/*.sh` в `out/usr/bin/` с правами `a+x` и
  стейджит их через `MAROC_COPY_PKG_TO_APP_OUTPUT` в `RK_APP_OUTPUT`
  (`project/app/out` → `output/out/app_out/usr/bin` → `/oem/usr/bin`).
- `files/oko-lunch.sh` — запуск:
  1. ждёт монтирования `/userdata` (до ~3 с) и запускает
     `/oem/usr/ko/insmod_ko.sh` (fallback `/ko`);
  2. поднимает сеть в фоне: в netboot (`nfsroot=` в `/proc/cmdline`) только
     `ifconfig eth0 up` без DHCP/MAC, иначе обязательный
     `/usr/bin/oko-net apply`; без `oko-net` выводит явное сообщение, сеть не
     поднимается;
  3. создаёт симлинки для WebUI: `/userdata`, `/media/usb0`, `/mnt/sdcard` в
     `/oem/usr/www/*`;
  4. ставит `vm.overcommit_memory=1`;
  5. проверяет `/userdata/divinus.enable` и `/oem/usr/bin/divinus`, бутстрапит
     `/userdata/divinus.yaml` из `/oem/usr/share/divinus/divinus.yaml` и
     запускает `divinus` фоном из каталога `/userdata` (конфиг ищется в
     каталоге запуска, сохранение из WebUI пишет туда же).
- `files/oko-lunch-stop.sh` — останов: снимает зависший фоновый лаунчер,
  завершает `divinus`/`udhcpc`/`dhcpcd`, ждёт выхода `divinus` до 10 с и при
  необходимости делает `killall -9 divinus`.

## Что перенесено из RkLunch

Тело скриптов взято из `project/app/rkipc/rkipc/src/rv1106_ipc/` и уже
содержало OKO-правки (переход на `divinus`, `oko-net apply`, пропуск DHCP в
netboot). При переносе удалены мёртвые вендорские ветки:

- `rcS`/`rcK` по `/oem/usr/etc/init.d/S??*` — каталога в образе нет;
- fallback `/data/ethaddr.txt` + `udhcpc` — без `oko-net` сеть не поднимается;
- `ulimit -c unlimited` и запись `core_pattern` — владелец
  `/etc/profile.d/enable_coredump.sh` из `project/build.sh`
  (`| /usr/bin/coredump2sdcard.sh %p %e`, ядро падает на microSD
  `/mnt/sdcard/core-<pid>-<exe>`).

`vm.overcommit_memory=1` оставлен: других установщиков этого параметра в
проекте нет, медиастеку он нужен.

## Сборка и проверка

```sh
./build.sh app
ls output/out/app_out/usr/bin/oko-lunch*.sh
shellcheck -s sh project/app/oko-lunch/files/*.sh  # если shellcheck установлен
```

После полной сборки:

```sh
ls output/out/oem/usr/bin/oko-lunch*.sh
tools/native/check_image_contents.sh
```

На стенде: `S21appinit start` должен запустить `divinus` (`ps`), WebUI/RTSP
живы; `S21appinit stop` завершает `divinus` и сетевой клиент не дольше ~10 с,
повторный `start` поднимает всё обратно.