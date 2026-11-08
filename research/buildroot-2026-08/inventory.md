# Инвентаризация миграции Buildroot 2023.02.6 → 2026.08.1

Дата сверки: 2026-11-08. Хост: Debian 13 trixie, gcc 14.2.0.
Тулчейн: `arm-rockchip830-linux-uclibcgnueabihf` (crosstool-NG 1.24.0,
gcc 8.3.0, uClibc-ng, headers 5.10).

## 1. Вендорская дельта к upstream 2023.02.6

`buildroot-2023.02.6.tar.gz` из SDK сравнивался с официальным
`buildroot-2023.02.6.tar.gz`. Отличие в tarball ровно одно:

| Путь | Статус | Нужен OKO |
|---|---|---|
| `configs/luckfox_pico_defconfig` | добавлен вендором | да |

Всё остальное лежит вне tarball в `sysdrv/tools/board/buildroot/`:

| Файл/каталог | Назначение | Нужен активному defconfig |
|---|---|---|
| `luckfox_pico_defconfig` | состав пакетов OKO | да |
| `luckfox_pico_w_defconfig` | Wi-Fi/audio-вариант | нет (untested) |
| `busybox.config` | конфиг busybox | да |
| `busybox_patch/0100-…` | UTF-8 в терминале | да |
| `busybox_patch/0101-…` | `halt/reboot` с аргументом | да |
| `hcitool_patch/` | патчи bluez5_utils | нет (пакет не в defconfig) |
| `mpv_patch/` | патч mpv | нет (пакет не в defconfig) |
| `font/zkkhei.ttf` | шрифт OSD | используется приложением, не Buildroot |

`rockchip-mali` не является вендорским добавлением: пакет есть в
upstream 2023.02.6 и 2026.08.1, наш defconfig его не включает.

## 2. Дельта OKO к upstream 2026.08.1

| Путь | Изменение |
|---|---|
| `configs/luckfox_pico_defconfig` (копируется `buildroot_create`) | `BR2_GLOBAL_PATCH_DIR="../../../../sysdrv/tools/board/buildroot/patches-2026.08"` |
| `package/busybox/busybox.config` | `# CONFIG_USE_BB_CRYPT_YES is not set` (нет `explicit_bzero` в uClibc-ng) |
| `patches-2026.08/gnutls/0001-…` | не собирать `src/gl/tests` (pthread_t в `bits/sigthread.h`) |
| `patches-2026.08/libv4l/0001-…` | designated initializers → `i2c_msg{...}` для gcc 8 |
| `busybox_patch/0100`, `0101` | перенумерованы из `0009`, `0010` (конфликт с upstream-нумерацией) |

## 3. Версии пакетов rootfs (было → стало)

| Пакет | 2023.02.6 | 2026.08.1 |
|---|---|---|
| busybox | 1.36.1 | 1.38.0 |
| python3 | 3.11.6 | 3.14.8 |
| openssl | 1.1.1 | 3.x |
| gnutls | 3.7.8 | 3.8.13 |
| openssh | 9.3p2 | 10.6p1 |
| libcurl | 8.4.0 | 8.22.0 |
| wget | 1.21.3 | 1.25.0 |
| rsync | 3.2.7 | 3.5.0 |
| librsync | 2.3.2 | 2.3.4 |
| iptables | 1.8.9 | 1.8.11 |
| openvpn | 2.5.7 | 2.7.8 |
| ntp | 4.2.8p15 | 4.2.8p18 |
| dhcpcd | 9.4.1 | 10.2.4 |
| e2fsprogs | 1.46.5 | 1.47.4 |
| libv4l (v4l-utils) | 1.22.1 | 1.32.0 |
| util-linux | 2.38 | 2.41.5 |
| bash | 5.2.15 | 5.2.37 |
| nano | 7.2 | 9.0 |
| p7zip | 17.04 | 17.06 |
| xz | 5.2.10 | 5.8.4 |
| freetype | 2.12.1 | 2.14.3 |
| libdrm | 2.4.115 | 2.4.134 |
| dtc | 1.6.1 | 1.7.2 |
| iperf | 2.1.8 | 2.2.1 |
| iperf3 | 3.14 | 3.21 |
| lrzsz | 0.12.20 | 0.12.21rc |
| socat | 1.7.4.4 | 1.8.1.3 |
| dialog | 1.3-20220117 | 1.3-20251001 |
| htop | 3.2.1 | 3.5.1 |
| ca-certificates | 20230311 | 20260816+nmu1 |
| pcre2 | 10.42 | 10.49 |
| libmd | 1.0.4 | 1.1.0 |
| libevent | 2.1.12 | 2.1.13 |
| expat | 2.5.0 | 2.9.0 |
| ncurses | 6.4 | 6.6 |
| readline | 8.2 | 8.3 |

## 4. Состав defconfig

Все `BR2_PACKAGE_*`-строки исходного `luckfox_pico_defconfig` присутствуют
в `.config` 2026.08.1 (проверено скриптом сравнения). Автоматически
добавленные/исчезнувшие символы — только зависимости пакетов:

- исчезли: `BR2_PACKAGE_LIBOPENSSL_ENABLE_SSL2`,
  `BR2_PACKAGE_PYTHON_{ASYNC_TIMEOUT,CCHARDET,TYPING_EXTENSIONS}`
  (OpenSSL 3 убрал SSLv2, новые версии aiohttp не тянут эти модули);
- добавились зависимости новых версий: `BR2_PACKAGE_C_ARES`,
  `BR2_PACKAGE_LIBCAP_NG` и др. (не расширяют набор явно выбранных
  пакетов).

## 5. Результат сборки

| Проверка | Результат |
|---|---|
| `make toolchain` | OK, wrapper + sysroot вендорского тулчейна |
| `make buildroot_create -C sysdrv` | OK, дерево из нового tarball |
| `./build.sh rootfs` (стадия buildroot) | OK, `output/images/rootfs.tar` 93 450 240 Б |
| ABI | `libssl.so.3`, `libcrypto.so.3`, `libpython3.14.so` |
| busybox 1.38.0 | собран, yescrypt отключён |
| gnutls 3.8.13 | собран с патчем gnulib-тестов |
| v4l-utils 1.32.0 | собран с патчем edid-decode |

## 6. Открытые вопросы

1. `luckfox_pico_w_defconfig` — не мигрирован; старые патчи bluez5/mpv
   не проверялись на новых версиях.
2. ABI `/oem`: сторонние blob'ы с `DT_NEEDED libssl.so.1.1` требуют
   пересборки или compat-копии (см. KNOWN_ISSUES).
3. Проверка rootfs на DUT (NFS/netboot и SPI NAND) — этапы 4–6 плана.