# AmneziaWG — модуль ядра для OKO

Out-of-tree модуль `amneziawg.ko` для ядра Linux 5.10.160 (RV1106). Встроенный
`CONFIG_WIREGUARD=y` не трогается: семейства netlink `wireguard` и `amneziawg`
сосуществуют, интерфейсы создаются как `type wireguard` и `type amneziawg`.

## Источник

| Параметр | Значение |
|---|---|
| Репозиторий | https://github.com/amnezia-vpn/amneziawg-linux-kernel-module |
| Тег | `v3.1.20260906` |
| Коммит | `4569c4c67f3a57414969260cafbbd04694fbaae0` |
| Дата коммита | 2026-09-06 |
| Лицензия | GPL-2.0 (`COPYING`) |

В `src/` лежит слепок каталога `src/` апстрима без `.git`. Правок в исходники не
вносилось: обновление — повторное копирование каталога на нужном теге.

## Сборка

Модуль собирается штатным `sysdrv`-конвейером при `RK_ENABLE_AWG=y`:

```
./build.sh sysdrv        # на хосте
```

`sysdrv/drv_ko/Makefile` вызывает этот Makefile, тот — Kbuild ядра:

```
make ARCH=arm CROSS_COMPILE=... -C <kernel> M=src \
     WIREGUARD_VERSION="3.1.20260906" modules
```

Готовый `amneziawg.ko` копируется в `sysdrv/out/kernel_drv_ko/` и далее в
`/oem/usr/ko`. Модуль не грузится на старте: `oko-vpn` делает `insmod` при
первом `up` профиля типа `awg`.

## Проверка

```
modinfo -F vermagic output/out/sysdrv_out/kernel_drv_ko/amneziawg.ko
modinfo -F depends  output/out/sysdrv_out/kernel_drv_ko/amneziawg.ko
```

vermagic обязан совпадать с `uname -r` на DUT. Отладочный
`CONFIG_AMNEZIAWG_DEBUG` не включается.

## Совместимость

Слой `src/compat/` рассчитан на ядра 5.10+ и содержит заглушки для
`DEV_STATS_INC`, `timer_delete`/`timer_delete_sync`, `dst_cache_pcpu` и др.
Первая сборка под 5.10.160 подтверждает совместимость; при ошибке сборки
следует откатиться на тег `v1.0.20260725` (см. `KNOWN_ISSUES.md`).