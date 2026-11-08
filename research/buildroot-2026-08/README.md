# Buildroot 2026.08.1 для OKO

## Вопрос

Можно ли собрать rootfs OKO на upstream Buildroot 2026.08.1 с вендорским
кросс-тулчейном `arm-rockchip830` (gcc 8.3.0, uClibc-ng) на хосте
Debian 13 с gcc 14.2, сохранив набор пакетов `luckfox_pico_defconfig`,
и какие правки для этого нужны?

## Что это

Сравнение вендорского Buildroot 2023.02.6 из SDK Luckfox V1.4.1 с upstream
2026.08.1 (релиз 2026-10-22) и перенос конфигурации OKO. Вендорский
tarball — это upstream 2023.02.6 плюс единственный файл
`configs/luckfox_pico_defconfig`; остальные отличия (defconfig'и,
`busybox.config`, патчи пакетов) лежат вне tarball и подставляются
`sysdrv/Makefile` (`buildroot_create`, `buildroot`). Поэтому миграция
свелась к upstream-дереву, переносу внешних файлов и двум пакетным
патчам под старый uClibc/кросс-gcc.

## Выводы для OKO

**Переносимо:**

- upstream 2026.08.1 собирается внешним тулчейном `arm-rockchip830`
  (uClibc-ng, headers 5.10, gcc 8.3.0): гейт `make toolchain` пройден,
  wrapper и sysroot собираются без ослабления `BR2_TOOLCHAIN_*`;
- состав пакетов `luckfox_pico_defconfig` сохранён 1:1, ни один
  `BR2_PACKAGE_*` из defconfig не потерян;
- `busybox.config` и оба вендорских busybox-патча применяются к
  busybox 1.38.0 (после перенумерации в `0100`/`0101`, чтобы не
  пересекаться с upstream-патчами `0009`/`0010`);
- вендорские патчи `hcitool_patch` (bluez5_utils) и `mpv_patch`
  активному `luckfox_pico_defconfig` не нужны — эти пакеты в него не
  входят; копирование оставлено ради `luckfox_pico_w_defconfig`.

**Не переносимо без правок:**

- `CONFIG_USE_BB_CRYPT_YES` (yescrypt) в busybox 1.38.0 требует
  `explicit_bzero`, которого нет в uClibc-ng вендорского тулчейна —
  отключено, пароли используют SHA-256 (`FEATURE_DEFAULT_PASSWD_ALGO`);
- GnuTLS 3.8.13 импортирует gnulib с self-тестами; тесты включают
  `<signal.h>` до `<pthread.h>` и падают на `bits/sigthread.h`
  uClibc-ng — патч `gnutls/0001` убирает `src/gl/tests` из `SUBDIRS`;
- v4l-utils 1.32.0 (`edid-decode`) использует C++20 designated
  initializers в присваивании — патч `libv4l/0001` оборачивает их в
  `i2c_msg{...}`, что принимает gcc 8.

**Ограничения:**

- `luckfox_pico_w_defconfig` не мигрирован и помечен untested: старые
  `hcitool_patch`/`mpv_patch` почти наверняка не применить к новым
  bluez5/mpv;
- ABI изменился: OpenSSL 1.1.1 → 3.x, Python 3.11 → 3.14; бинарники
  `/oem`, собранные в SDK-цикле, нужно пересобирать, сторонние blob'ы
  с `DT_NEEDED libssl.so.1.1` требуют compat-копии.

## Проверенные факты

- `git ls-files sysdrv/source/buildroot` пуст: дерево в `.gitignore`,
  в git лежит tarball и внешние конфиги.
- `sysdrv/Makefile`: `BUILDROOT_VER ?= buildroot-2026.08.1`, цели
  `buildroot_create`/`buildroot` распаковывают tarball и копируют
  `luckfox_pico_defconfig`, `luckfox_pico_w_defconfig`,
  `busybox.config`, патчи `busybox_patch/`.
- `project/build.sh:37`: `BUILDROOT_PATH=.../buildroot-2026.08.1`.
- Гейт: `make toolchain` в 2026.08.1 собрал `toolchain-wrapper` и
  скопировал sysroot вендорского тулчейна без ошибок.
- Полная сборка: `make buildroot_create -C sysdrv` + `./build.sh rootfs`
  → `output/images/rootfs.tar` (93 МБ), Python 3.14.8, OpenSSL 3.5.
- Версии пакетов «было → стало» — в `inventory.md`.

## Ссылки

- https://buildroot.org/downloads/buildroot-2026.08.1.tar.gz
- https://buildroot.org/downloads/buildroot-2023.02.6.tar.gz
- Buildroot manual, «External toolchain backend»:
  https://buildroot.org/downloads/manual/manual.html

## Провенанс снимков

Снимков upstream-файлов нет: вместо копий используется официальный
tarball с закреплённым sha256, он же лежит в
`sysdrv/tools/board/buildroot/buildroot-2026.08.1.tar.gz`.

| Upstream | Локальный путь | Лицензия | sha256 | Размер | Дата снятия | Pin |
|---|---|---|---|---|---|---|
| buildroot-2026.08.1.tar.gz | `sysdrv/tools/board/buildroot/buildroot-2026.08.1.tar.gz` | GPL-2.0-or-later (mixed) | `38a6c152490096018720a9ade379ee1e571c9bf5f93a3657a166566945a98b0d` | 8 177 436 Б | 2026-11-08 | релиз 2026.08.1 от 2026-10-22 |

## Как обновить снимок

```sh
sysdrv/tools/board/buildroot/make_tarball.sh 2026.08.1
make buildroot_create -C sysdrv
source tools/native/env.sh && ./build.sh rootfs
```

При смене версии обновить `SHA256` в `make_tarball.sh`, `BUILDROOT_VER`
в `sysdrv/Makefile`, `BUILDROOT_PATH` в `project/build.sh`, таблицу
версий в `inventory.md` и записи в `CHANGELOG.md`.