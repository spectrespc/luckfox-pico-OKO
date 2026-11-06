# vpn-tools — `wg` и `awg` для OKO

Пакет собирает два userspace-бинарника app-тулчейном
(`arm-rockchip830-linux-uclibcgnueabihf`) и кладёт их в `/oem/usr/bin`:

| Бинарь | Источник | Версия / коммит |
|---|---|---|
| `wg` | https://github.com/WireGuard/wireguard-tools | `v1.0.20210914`, `3ba6527130c502144e7388b900138bca6260f4e8` |
| `awg` | https://github.com/amnezia-vpn/amneziawg-tools | `v3.1.20260812`, `ee0f0a9aa34ff0a0da4b3433b9512781cfe02843` |

`amneziawg-tools` не имеет тега `v3.1.20260906` (последний релиз на момент
сборки — `v3.1.20260812`, он же `master`). Инструменты этого тега уже знают
все параметры v3.1 (`HeaderProtectionKey`, `I1…I5`, `RandomTrailers`,
`AdvancedSecurity`, `ContentPaddingAddition`), поэтому совместимы с модулем
`amneziawg-linux-kernel-module` `v3.1.20260906`.

Исходники вендорены в `vendor/<name>/src` без `.git`; лицензия GPL-2.0
сохранена в `vendor/<name>/COPYING`. Правок в код не вносилось.

## Почему не Buildroot

Кэш `dl/` содержит 106 пакетов и **не содержит** `wireguard-tools`;
сборка не должна зависеть от сети. Оба пакета маленькие, поэтому они
собираются тем же app-тулчейном, что `divinus`/`oko-ec`.

## Сборка

```sh
./build.sh app        # внутри Docker; вызывает project/app/Makefile
# или точечно:
make -C project/app/vpn-tools
```

`Makefile` собирает `src/wg` в каждом каталоге с:

```
PLATFORM=linux CC=$(RK_APP_CROSS)-gcc AR=$(RK_APP_CROSS)-ar \
STRIP=$(RK_APP_CROSS)-strip \
WITH_SYSTEMDUNITS=no WITH_BASHCOMPLETION=no WITH_WGQUICK=no wg
```

`WITH_WGQUICK=no` принципиален: `wg-quick`/`awg-quick` выполняют `PreUp`/
`PostUp` из загруженного `.conf`, а OKO применяет конфигурацию собственным
хелпером `/usr/bin/oko-vpn` и отклоняет такие хуки при импорте.

## Проверка

```sh
file  project/app/vpn-tools/out/usr/bin/wg
file  project/app/vpn-tools/out/usr/bin/awg
readelf -h project/app/vpn-tools/out/usr/bin/awg | grep Machine
# ELF 32-bit LSB executable, ARM, EABI5 ... /lib/ld-uClibc.so.0
# Machine: ARM

# на DUT
/oem/usr/bin/wg --version
/oem/usr/bin/awg --version
```

Оба бинаря статически не линкуются: им нужен `uClibc` из образа, как и
остальным app-бинарникам OKO.

## Ограничения

- `wg` не знает AWG-параметров — он предназначен только для профилей
  `type=wg` (встроенное ядро WireGuard). Для `awg0` вызывается `awg`.
- Синтаксис `show`/`setconf` у `awg` совпадает с `wg`, поэтому `oko-vpn`
  использует один и тот же код с выбором бинаря по типу профиля.
- Инструменты и модуль должны быть одного поколения (v3.1); см.
  `KNOWN_ISSUES.md`, KI-039.