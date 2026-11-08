# Чанжлог

Формат — [Keep a Changelog 1.1.0](https://keepachangelog.com/ru/1.1.0/),
версионирование — [SemVer 2.0.0](https://semver.org/lang/ru/).

Проект — форк SDK **Luckfox Pico V1.4**; базовая точка отсчёта — вендорский SDK
V1.4. История V1.0–V1.4 в этот файл не переносится и находится в `UPDATE_LOG.md`
(EN) и `UPDATE_LOG_CN.md` (CN).

## Правила ведения

1. Каждое изменение (функция, исправление, удаление) добавляется в `[Не выпущено]`
   в момент изменения. При выпуске раздел превращается в версию с датой
   в формате `ГГГГ-ММ-ДД`.
2. SemVer: **major** — ломающие изменения совместимости прошивки или поддержки
   оборудования; **minor** — новые функции; **patch** — исправления.
3. Категории: `Добавлено`, `Изменено`, `Исправлено`, `Удалено`, `Устарело`,
   `Безопасность`. Пустые категории опускаются.
4. История не переписывается: исправление ошибки в записи оформляется новой
   записью, а не правкой прошлой.
5. Файл обязательно обновляется при каждом изменении поведения, сборки,
   конфигурации или документации проекта.
6. `UPDATE_LOG.md` / `UPDATE_LOG_CN.md` — неприкосновенный вендорский архив.

## [Не выпущено]

### Добавлено

- **Возвращён netboot-контур TFTP/NFS (OKO).** `bench/netboot.py`
  (режимы `catch`/`reboot`/`cmd`, определение shell-приглашения
  `root@luckfox`), фрагмент ядра `rv1106-netboot.config` (IP_PNP,
  ROOT_NFS), конфиг
  `BoardConfig-SPI_NAND-Buildroot-RV1106_Luckfox_Pico_Pro_Max-SC230AI-NETBOOT.mk`
  (оба фрагмента: netboot и SC230AI), NFS-ветки `oem`/`userdata` в
  S20linkmount (`project/build.sh`) и пропуск DHCP в `RkLunch.sh` при
  `nfsroot=` в cmdline. Хостовые сервисы настраивает
  `tools/native/setup_netboot.sh`: `tftpd-hpa` с корнем
  `<repo>/output/image` (chroot, `ProtectHome=read-only`, ACL на
  обход домашнего каталога) и NFS-экспорт `/srv/nfs/rootfs` для
  `192.168.52.0/24`. Первый прогон `netboot.py reboot`: TFTP boot.img,
  NFS-root и статический `192.168.52.150` подтверждены на DUT.
- **The TFTP/NFS netboot loop is back (OKO).** `bench/netboot.py`
  (`catch`/`reboot`/`cmd` modes, detects the `root@luckfox` shell
  prompt), the `rv1106-netboot.config` kernel fragment (IP_PNP,
  ROOT_NFS), the
  `BoardConfig-SPI_NAND-Buildroot-RV1106_Luckfox_Pico_Pro_Max-SC230AI-NETBOOT.mk`
  board config (both the netboot and SC230AI fragments), the
  `oem`/`userdata` NFS branches in the S20linkmount generator
  (`project/build.sh`) and the DHCP skip in `RkLunch.sh` when
  `nfsroot=` is present in the cmdline. Host services are configured by
  `tools/native/setup_netboot.sh`: `tftpd-hpa` rooted at
  `<repo>/output/image` (chroot, `ProtectHome=read-only`, ACLs for the
  home directory traversal) and the `/srv/nfs/rootfs` export for
  `192.168.52.0/24`. The first `netboot.py reboot` run confirmed TFTP
  boot.img, the NFS root and the static `192.168.52.150` on the DUT.

- **Нативная сборка SDK на Debian 13 (OKO).** Добавлены
  `tools/native/setup_host.sh` (apt-чек-лист бывшего контейнера с именами
  пакетов trixie: `libncurses-dev`, `u-boot-tools`, `ninja-build`,
  `squashfs-tools`, `mtd-utils`; проверка `ldd`/запуска вендорского
  кросс-тулчейна), `tools/native/env.sh` (`PATH`, `CROSS_COMPILE`, `ARCH`,
  `LANG/LC_ALL=C.UTF-8`, `sbin` в `PATH`) и `tools/native/smoke.sh` (отчёт
  `output/bench/native/smoke.log`: host-утилиты, кросс-gcc 8.3.0,
  `upgrade_tool ld`). Хост стенда — Debian 13 trixie, gcc 14.2; тулчейн
  `arm-rockchip830` запускается без библиотек совместимости.
- **Native SDK build on Debian 13 (OKO).** Added
  `tools/native/setup_host.sh` (the former container apt checklist with
  trixie package names: `libncurses-dev`, `u-boot-tools`, `ninja-build`,
  `squashfs-tools`, `mtd-utils`; verifies the vendor cross toolchain with
  `ldd` and a test run), `tools/native/env.sh` (`PATH`, `CROSS_COMPILE`,
  `ARCH`, `LANG/LC_ALL=C.UTF-8`, `sbin` in `PATH`) and
  `tools/native/smoke.sh` (report in `output/bench/native/smoke.log`: host
  tools, cross gcc 8.3.0, `upgrade_tool ld`). The bench host is Debian 13
  trixie with gcc 14.2; the `arm-rockchip830` toolchain runs without
  compatibility libraries.

- **OpenVPN-клиент с управлением из WebUI (OKO).** Третий тип профиля
  `ovpn`: `oko-vpn` разбирает клиентский конфиг OpenVPN, отклоняет
  `dev tap`, `plugin`, `management`, скриптовые директивы, `askpass`,
  `config` и `auth-user-pass <файл>`, принудительно ставит
  `redirect-gateway def1`, запускает демон `/usr/sbin/openvpn` с
  pid/status-файлами, ждёт `Initialization Sequence Completed` и адрес
  `tun0`, а для full tunnel выполняет тот же 15-секундный guard с откатом.
  Профили и учётные данные — `/userdata/vpn/<name>.conf` и
  `<name>.pass` (0600); `S45oko-vpn` подхватывает `tun0` в autostart и
  watchdog по pid. Импорт через `/api/vpn/import` принимает поля
  `user`/`pass` (multipart и form), WebUI показывает их только для типа
  `ovpn`; `GET /api/vpn` отдаёт `ovpn_bin`, `tunip`, `rx/tx` и
  `handshake_age: null`. Сборка: `BR2_PACKAGE_OPENVPN=y` (openvpn 2.5.7,
  OpenSSL, LZO/LZ4) в `luckfox_pico_defconfig` и `CONFIG_TUN=y` в
  `luckfox_rv1106_linux_defconfig`. Обновлены `FEATURES*.md`,
  `INIT_ORDER.md` (§4: `S60openvpn` неактивен), `bench/README.md` и
  хостовый тест `vpn_conf_test.c` (детект, отказы, `def1`). Стенд
  2026-11-08: внешний сервер OpenVPN (`192.168.52.200:1194`), inline
  сертификаты + `tls-crypt` + `auth-user-pass`, `tun0` 10.8.0.3/24, ping
  через туннель, iperf3 21–23 Мбит/с, full tunnel `def1` с guard, `down`
  без маршрутов, autostart после reboot; `vpn_check.py` 23/23 и 21/21,
  регрессия WG 19/19, `api_check` 98/0, `ui_check` зелёный.
- **OpenVPN client managed from the WebUI (OKO).** A third profile type
  `ovpn`: `oko-vpn` parses an OpenVPN client configuration, rejects
  `dev tap`, `plugin`, `management`, script directives, `askpass`,
  `config` and `auth-user-pass <file>`, forces `redirect-gateway def1`,
  starts the `/usr/sbin/openvpn` daemon with pid/status files, waits for
  `Initialization Sequence Completed` and a `tun0` address, and runs the
  same 15-second full-tunnel guard with a rollback. Profiles and
  credentials live in `/userdata/vpn/<name>.conf` and `<name>.pass`
  (0600); `S45oko-vpn` covers `tun0` in autostart and the pid-based
  watchdog. `/api/vpn/import` accepts `user`/`pass` (multipart and form),
  the WebUI shows them only for `ovpn`, and `GET /api/vpn` exposes
  `ovpn_bin`, `tunip`, `rx/tx` and `handshake_age: null`. Build:
  `BR2_PACKAGE_OPENVPN=y` (openvpn 2.5.7, OpenSSL, LZO/LZ4) in
  `luckfox_pico_defconfig` and `CONFIG_TUN=y` in
  `luckfox_rv1106_linux_defconfig`. `FEATURES*.md`, `INIT_ORDER.md`
  (§4: inactive `S60openvpn`), `bench/README.md` and the host test
  `vpn_conf_test.c` (detection, rejects, `def1`) are updated. Bench
  2026-11-08: an external OpenVPN server (`192.168.52.200:1194`) with inline
  certificates, `tls-crypt` and `auth-user-pass`, `tun0` 10.8.0.3/24, a
  tunnel ping, iperf3 21–23 Mbit/s, full tunnel `def1` with the guard,
  route-free `down` and autostart after a reboot; `vpn_check.py` 23/23 and
  21/21, the WG regression 19/19, `api_check` 98/0 and `ui_check` passed.
- **Сводка microSD в OSD (OKO).** `oko-osd` дописывает
  ` SD FREE <free>/<cap>GiB` в строку `MEM FREE` региона `reg4` (свободно
  первым, ёмкость второй, один знак после точки — как в WebUI); без
  смонтированной карты выводится `SD FREE -`. Источник — `df -k /mnt/sdcard`
  с проверкой монтирования по `/proc/mounts`, тот же паттерн, что у NPU:
  строк остаётся три, регион не обрезается. Стенд: `MEM FREE 77% SD FREE
  14.8/14.8GiB` совпало с `/api/status` (`15171/15177 MiB`), `umount` дал
  `SD FREE -`, после `mount` числа вернулись; `ui_check` и `api_check`
  (97/0) зелёные.
- **microSD summary in OSD (OKO).** `oko-osd` appends
  ` SD FREE <free>/<cap>GiB` to the `MEM FREE` line of `reg4` (free first,
  capacity second, one decimal, as in the WebUI); without a mounted card it
  reports `SD FREE -`. The source is `df -k /mnt/sdcard` guarded by
  `/proc/mounts`, the same trick as the NPU suffix: the file keeps three lines
  and the region does not clip. Bench result: `MEM FREE 77% SD FREE
  14.8/14.8GiB` matched `/api/status` (`15171/15177 MiB`), `umount` produced
  `SD FREE -`, and the numbers returned after `mount`; `ui_check` and
  `api_check` (97/0) passed.
- **Наблюдаемость ePTZ и геометрия оверлеев (OKO).** `rk_vpss_set_chn_crop`
  возвращает числовой код MPP, печатает его один раз и сохраняется в
  `media.c` под мьютексом (`media_eptz_last`); `/api/eptz` отдаёт фактические
  `applied`, `rc`, `chn` и `crop` и `note` при отказе. Кроп и поворот
  применяются в `rk_channel_bind()` сразу после `EnableChn` — при каждом
  старте канала (включение канала сбрасывает их). Новый модуль
  `analytics/nn_geom.c` (хост-тест `tests/nn_geom_test.c`) пересчитывает
  координаты детекций с учётом кропа и поворота 0/90/180/270; рамки
  выравниваются по 4 px вместо сетки 16 px. Текстовый OSD переякоривается при
  90/270 (`nn_map_anchor` в `rk_region_create2`), сохраняя угол и отступ.
  Новый `bench/geometry_check.py` (`--what zoom|boxes|osd`) и opt-in
  `OKO_GEOMETRY_TESTS=1`; в предпросмотре WebUI — бейдж `#preview-mode`
  («MP4 · main», «MJPEG · отдельный канал», «стоп-кадр · JPEG»,
  «остановлено») и подсказка, что зум/поворот применяются к выбранному потоку.
- **ePTZ observability and overlay geometry (OKO).**
  `rk_vpss_set_chn_crop` returns the numeric MPP code, logs it once and stores
  it in `media.c` under a mutex (`media_eptz_last`); `/api/eptz` now reports
  the actual `applied`, `rc`, `chn` and `crop` plus a `note` on failure. The
  crop and the rotation are applied in `rk_channel_bind()` right after
  `EnableChn` — on every channel start (the enable resets them). The new
  `analytics/nn_geom.c` module (host test `tests/nn_geom_test.c`) maps
  detection coordinates through the crop and the 0/90/180/270 rotation; boxes
  are aligned to 4 px instead of the 16 px grid. OSD text is re-anchored at
  90/270 (`nn_map_anchor` in `rk_region_create2`) and keeps its corner and
  margin. New `bench/geometry_check.py` (`--what zoom|boxes|osd`) and the
  opt-in `OKO_GEOMETRY_TESTS=1`; the WebUI preview gained the `#preview-mode`
  badge ("MP4 · main", "MJPEG · separate channel", "still · JPEG", "stopped")
  and a note that zoom/rotation apply to the selected stream.
- **Автофокус по AF-метрике (OKO).** Новый модуль `focus_af.c` выполняет
  поиск фокуса по статистике `metric_avg` (вариант B из `TO_DO.md` §5):
  зонд направления половинным шагом, coarse-проход с остановкой после
  `af_patience` неулучшений или у границы, fine-проход между лучшими
  точками и переход к лучшей позиции. После каждого хода — пауза
  `af_settle_ms` и медиана трёх свежих кадров; сырое среднее используется
  потому, что `metric_smooth` — EMA и отстаёт на несколько кадров. Поиск
  ограничен `[max(0, min_pos)+home_margin, max_pos-home_margin]` (KI-042:
  отрицательные позиции не командуются), `af_timeout_s` и проверкой
  `status==error`; на плоской сцене возвращается ошибка «peak not found».
  `POST /api/focus?af=1` запускает job в том же запросе, что и
  `enable/mode/roi/fps/motor=1`, `af=cancel` и `stop=1` отменяют его,
  `GET /api/focus` отдаёт блок `af` (`state`, `position`, `best_position`,
  `metric`, `best_metric`, `progress`, `done`, `error`). Взаимные
  блокировки: AF не стартует при home/zero или прошивке контроллера,
  home/zero/ручные движения и `POST /api/flash` получают 409 при активном
  поиске. Параметры `[focus]`: `af_coarse=150`, `af_fine=25`,
  `af_patience=3`, `af_settle_ms=600`, `af_timeout_s=60`,
  `af_min_gain_pct=2`. Чистые решающие правила вынесены в
  `focus_af_algo.c` и покрыты `tests/focus_af_test.c`; обновлены
  `endpoints.md`, `README.md` divinus, `TO_DO.md`, `STRUCTURE.md` и
  `FEATURES_UNVERIFIED.md`. На стенде 2026-11-07 обе ветки (`full`/`roi`)
  завершились `done`, но метрика не зависит от позиции мотора (объектив
  стенда не связан с приводом), поэтому сходимость не подтверждена;
  кривые — в `output/bench/af_curve_*.csv`.
- **Auto-focus over the AF metric (OKO).** The new `focus_af.c` searches for
  focus using the `metric_avg` statistics (variant B in `TO_DO.md` §5): a
  half-step direction probe, a coarse pass that stops after `af_patience`
  non-improvements or at a travel limit, a fine pass between the best points
  and a move to the best position. Each move is followed by an
  `af_settle_ms` pause and the median of three fresh frames; the raw average
  is used because `metric_smooth` is an EMA that lags several frames. The
  search is bounded by `[max(0, min_pos)+home_margin, max_pos-home_margin]`
  (KI-042: negative positions are never commanded), `af_timeout_s` and a
  `status==error` check; a flat scene returns "peak not found".
  `POST /api/focus?af=1` starts the job in the same request as
  `enable/mode/roi/fps/motor=1`, `af=cancel` and `stop=1` cancel it, and
  `GET /api/focus` exposes an `af` block (`state`, `position`,
  `best_position`, `metric`, `best_metric`, `progress`, `done`, `error`).
  Mutual exclusions: AF does not start during home/zero or a controller
  flash, and home/zero/manual moves and `POST /api/flash` return 409 while a
  search runs. `[focus]` keys: `af_coarse=150`, `af_fine=25`,
  `af_patience=3`, `af_settle_ms=600`, `af_timeout_s=60`,
  `af_min_gain_pct=2`. The pure decision rules live in `focus_af_algo.c` and
  are covered by `tests/focus_af_test.c`; `endpoints.md`, divinus
  `README.md`, `TO_DO.md`, `STRUCTURE.md` and `FEATURES_UNVERIFIED.md` are
  updated. On the bench (2026-11-07) both `full` and `roi` finished `done`,
  but the metric does not depend on the motor position (the bench lens is not
  coupled to the drive), so convergence is not confirmed; the curves are in
  `output/bench/af_curve_*.csv`.
- **Micro-fine автофокус и мобильный режим WebUI (OKO).** `POST
  /api/focus?af=fine` запускает локальную донастройку: окно
  `[pos−af_coarse, pos+af_coarse]` с клампом по безопасным границам, шаг
  `af_fine`, безусловный `done` без проверки `af_min_gain_pct`; в
  `GET /api/focus` добавлено поле `af.scope` (`full`/`fine`). В карточке
  Live появилась кнопка «Автофокус» рядом с jog-кнопками; на время job'а
  она блокируется вместе с остальными ручными кнопками, STOP по-прежнему
  отменяет поиск. Чистая функция `af_window()` покрыта
  `tests/focus_af_test.c`. На ширине ≤ 40em WebUI автоматически включает
  мобильный режим: нижняя навигация 4×2 с короткими подписями
  (`nav_short_*`), кнопки не ниже 44 px, компактная статус-строка (только
  IP и SD), тосты над навбаром и `font-size: 16px` у полей (анти-zoom iOS).
  Обновлены `bench/api_check.py` (`af=fine`, кривая
  `output/bench/af_curve_fine_*.csv`), `bench/ui_check.py` (раскладка всех
  вкладок на 1440×900 и 390×844, две колонки Live, нижний навбар, кнопка
  «Автофокус»), `bench/README.md`, `endpoints.md`, divinus `README.md`,
  `WEBUI_REORG.md`, `TO_DO.md`, `FEATURES_UNVERIFIED.md` и `TESTBENCH.md`.
- **Micro-fine auto-focus and the mobile WebUI (OKO).** `POST
  /api/focus?af=fine` runs a local correction: the window
  `[pos−af_coarse, pos+af_coarse]` clamped to the safe travel bounds, the
  `af_fine` step and an unconditional `done` without the `af_min_gain_pct`
  check; `GET /api/focus` gained `af.scope` (`full`/`fine`). The Live card
  has an Autofocus button next to the jog buttons; it is locked together with
  the other manual controls while a job runs, and STOP still cancels the
  search. The pure `af_window()` helper is covered by
  `tests/focus_af_test.c`. At ≤ 40em the WebUI switches to the mobile mode
  automatically: a bottom 4×2 navbar with short labels (`nav_short_*`),
  targets of at least 44 px, a trimmed status line (IP and SD only), toasts
  above the navbar and 16 px form fields (anti-zoom on iOS). Updated
  `bench/api_check.py` (`af=fine`, `output/bench/af_curve_fine_*.csv`),
  `bench/ui_check.py` (all tabs fit at 1440×900 and 390×844, two-column
  Live, bottom navbar, Autofocus button), `bench/README.md`, `endpoints.md`,
  divinus `README.md`, `WEBUI_REORG.md`, `TO_DO.md`,
  `FEATURES_UNVERIFIED.md` and `TESTBENCH.md`.
- **WebUI: объём и свободное место SD-карты в «Система → Статус» (OKO).**
  Рядом со строкой «Память» добавлены две read-only строки,
  `#status_sd_total_mb`/`#status_sd_free_mb`, заполняемые из уже
  существующих полей `/api/status` (`sd_total_mb`/`sd_free_mb`, MiB от
  `statvfs`). Хук `fmtMiB` печатает `GiB`/`MiB` и прочерк, когда карта
  отсутствует или не смонтирована (сервер отдаёт `0`); строки не входят в
  dirty-state и не добавляют кнопок `Apply`. Под карточкой — пояснение, что
  объём относится к смонтированному тому FAT32, а не к паспортной ёмкости
  карты. Статус-строка `#sb_sd` не менялась. Обновлены `endpoints.md`,
  divinus `README.md`, `WEBUI_REORG.md` и `bench/README.md`;
  `bench/ui_check.py` сверяет тексты с `window.lastStatus` и проверяет
  формат, отсутствие карты не требуется.
- **WebUI: SD card capacity and free space in System → Status (OKO).** Two
  read-only rows, `#status_sd_total_mb`/`#status_sd_free_mb`, sit next to
  Memory and are filled from the existing `/api/status` fields
  (`sd_total_mb`/`sd_free_mb`, MiB from `statvfs`). The `fmtMiB` read hook
  prints `GiB`/`MiB` and a dash when the card is absent or unmounted (the
  server reports `0`); the rows take no part in the dirty state and add no
  `Apply` buttons. A note under the card states that the size is the mounted
  FAT32 volume, not the card's advertised capacity. The `#sb_sd` status line
  is unchanged. Updated `endpoints.md`, divinus `README.md`, `WEBUI_REORG.md`
  and `bench/README.md`; `bench/ui_check.py` compares the rendered texts with
  `window.lastStatus` and checks the format, so no card is required.

### Изменено

- **Rootfs 2026.08.1 проверена на DUT и прошита в SPI NAND (OKO).** По NFS:
  `api_check` 98/98, `osd_check` 5/5 кадров, RTSP 1920×1080, Python 3.14.8 +
  OpenSSL 3.6.5, `oem`/`userdata` из `<nfsroot>/.nfsroot/`. После возврата
  `.BoardConfig.mk` на SC230AI и полной пересборки `update.img`
  (boot 6 МБ/oem 40 МБ/userdata 40 МБ/rootfs 168 МБ) прошит через Maskrom
  (`download` → `upgrade_tool uf`); DUT грузится с `root=ubi0:rootfs`,
  `/userdata` ≈29.1 МиБ, `api_check` 98/98, RTSP/OSD без регрессии,
  `/dev/net/tun` и OpenVPN 2.7.8 на месте. ABI-риск для сторонних blob'ов
  зафиксирован в KI-045, w-вариант — в KI-046.
- **The 2026.08.1 rootfs is verified on the DUT and flashed to SPI NAND
  (OKO).** Over NFS: `api_check` 98/98, `osd_check` 5/5 frames, RTSP at
  1920×1080, Python 3.14.8 + OpenSSL 3.6.5, `oem`/`userdata` mounted from
  `<nfsroot>/.nfsroot/`. After switching `.BoardConfig.mk` back to SC230AI
  and rebuilding `update.img` (boot 6 MB/oem 40 MB/userdata 40 MB/rootfs
  168 MB) it was flashed through Maskrom (`download` → `upgrade_tool uf`);
  the DUT boots with `root=ubi0:rootfs`, `/userdata` ≈29.1 MiB,
  `api_check` 98/98, RTSP/OSD clean, `/dev/net/tun` and OpenVPN 2.7.8
  present. The third-party blob ABI risk is tracked in KI-045 and the w
  variant in KI-046.

- **Buildroot обновлён до upstream 2026.08.1 (OKO).** Вместо вендорского
  `buildroot-2023.02.6` используется официальный релиз 2026.08.1
  (tarball с закреплённым sha256, `make_tarball.sh`), вендорская дельта
  перенесена внешними файлами: `luckfox_pico_defconfig`
  (`BR2_GLOBAL_PATCH_DIR`), `busybox.config`, патчи
  `patches-2026.08/{gnutls,libv4l}`; копирование `hcitool_patch`/`mpv_patch`
  в `buildroot_create`/`buildroot` убрано (пакеты не входят в активный
  defconfig). Гейт внешнего тулчейна
  (`arm-rockchip830`, gcc 8.3.0, uClibc-ng, headers 5.10) пройден без
  ослабления `BR2_TOOLCHAIN_*`; состав `BR2_PACKAGE_*` defconfig
  сохранён. Пакетные правки: GnuTLS 3.8.13 не собирает gnulib
  self-тесты (`pthread_t` в uClibc `bits/sigthread.h`), v4l-utils 1.32.0
  (`edid-decode`) использует `i2c_msg{...}` вместо C++20 designated
  initializer, busybox 1.38.0 собирается с отключённым yescrypt (нет
  `explicit_bzero` в uClibc-ng). Вендорские busybox-патчи перенумерованы
  в `0100`/`0101`. Версии: OpenSSL 3.x (было 1.1.1), Python 3.14.8
  (было 3.11.6), busybox 1.38.0 (было 1.36.1). `BUILDROOT_VER` и
  `BUILDROOT_PATH` обновлены; `rootfs.tar` собран нативно
  (`./build.sh rootfs`). Вендорская дельта и версии пакетов —
  `research/buildroot-2026-08/`.
- **Buildroot updated to upstream 2026.08.1 (OKO).** The vendor
  `buildroot-2023.02.6` is replaced by the official 2026.08.1 release
  (sha256-pinned tarball, `make_tarball.sh`), with the vendor delta kept
  outside the tree: `luckfox_pico_defconfig` (`BR2_GLOBAL_PATCH_DIR`),
  `busybox.config`, `patches-2026.08/{gnutls,libv4l}`; the
  `hcitool_patch`/`mpv_patch` copies in `buildroot_create`/`buildroot`
  are removed (the packages are not part of the active defconfig). The external
  toolchain gate (`arm-rockchip830`, gcc 8.3.0, uClibc-ng, headers 5.10)
  passes without relaxing `BR2_TOOLCHAIN_*`; the defconfig
  `BR2_PACKAGE_*` set is unchanged. Package fixes: GnuTLS 3.8.13 skips
  the gnulib self-tests (`pthread_t` in the uClibc `bits/sigthread.h`),
  v4l-utils 1.32.0 (`edid-decode`) uses `i2c_msg{...}` instead of a
  C++20 designated initializer, and busybox 1.38.0 builds with yescrypt
  disabled (no `explicit_bzero` in uClibc-ng). The vendor busybox
  patches are renumbered to `0100`/`0101`. Versions: OpenSSL 3.x (was
  1.1.1), Python 3.14.8 (was 3.11.6), busybox 1.38.0 (was 1.36.1).
  `BUILDROOT_VER` and `BUILDROOT_PATH` are updated; `rootfs.tar` is built
  natively (`./build.sh rootfs`). The vendor delta and package versions
  are recorded in `research/buildroot-2026-08/`.

- **Сборка U-Boot, ядра, media и app подтверждена на хосте без контейнера
  (OKO).** `./build.sh uboot|kernel|media|app` на Debian 13 с gcc 14.2 и
  вендорским кросс-gcc 8.3.0 прошли без правок исходников: host-tools
  U-Boot 2017 (`scripts/dtc`, `tools/`) и ядра 5.10 собраны gcc-14 только
  с предупреждениями, целевой код по-прежнему собирает кросс-gcc 8.
  Изменения сборки не потребовались.
- **U-Boot, kernel, media and app build on the container-free host (OKO).**
  `./build.sh uboot|kernel|media|app` on Debian 13 with gcc 14.2 and the
  vendor cross gcc 8.3.0 passed without source changes: the U-Boot 2017
  host tools (`scripts/dtc`, `tools/`) and the 5.10 kernel host tools build
  with gcc-14 warnings only, while target code still uses cross gcc 8. No
  build changes were required.

- **Итоговая регрессия KI-043/KI-044 и устойчивость bench-скриптов (OKO).**
  2026-11-08 на свежей загрузке: `nn_geom` 52/52, `api_check.py` 97/0
  (`OKO_FOCUS_MOTOR_TESTS=1` — 107/0), `ui_check.py` в обычном режиме и с
  `OKO_FOCUS_AF_TESTS=1` — passed, `geometry_check.py --what zoom` — 6
  проверок (кроп 4.28 против 53.02 к полному кадру, корреляция 0.985/0.112;
  возврат к 1x — 4.24, корреляция 1.000). `geometry_check.py`:
  `stable_capture()` отбрасывает тёмные первые кадры после рестарта, критерий
  кропа — нормированная корреляция вместо `mean|diff|` (её сбивает общий сдвиг
  яркости/AWB), рестарт MP4 вынесен в opt-in `OKO_GEOMETRY_MP4_TESTS=1`
  (после смены кропа он клинит RTSP). `api_check.py` опрашивает
  `focus.status` до 20 с: после нескольких циклов `enable=1/0` AFM может
  отдавать `idle`/`no_stats` до перезагрузки — это записано в
  `bench/README.md` и `TESTBENCH.md` §9, как и уточнённые признаки CMA
  (`cma_alloc -12`/503/нет кадров; высокий `loadavg` и `DW`-потоки сами по
  себе не признак).
- **Final KI-043/KI-044 regression and bench-script robustness (OKO).**
  2026-11-08 on a fresh boot: `nn_geom` 52/52, `api_check.py` 97/0
  (`OKO_FOCUS_MOTOR_TESTS=1` — 107/0), `ui_check.py` both in the default mode
  and with `OKO_FOCUS_AF_TESTS=1` passed, `geometry_check.py --what zoom` — 6
  checks (crop 4.28 versus 53.02 to the full frame, correlation 0.985/0.112;
  back to 1x — 4.24, correlation 1.000). `geometry_check.py`:
  `stable_capture()` drops the dark first frames after a restart, the crop
  criterion is normalised correlation instead of `mean|diff|` (a global
  brightness/AWB shift skews it), and the MP4 restart is opt-in via
  `OKO_GEOMETRY_MP4_TESTS=1` (after a crop change it wedges RTSP).
  `api_check.py` polls `focus.status` for up to 20 s: after several
  `enable=1/0` cycles the AFM can report `idle`/`no_stats` until a reboot —
  recorded in `bench/README.md` and `TESTBENCH.md` §9 together with the
  refined CMA signs (`cma_alloc -12`/503/no frames; high `loadavg` and `DW`
  threads are not a sign by themselves).
- **Проверены сборки `sysdrv`/`app` и уточнена команда обновления `oem`
  (OKO).** 2026-11-08: `./build.sh sysdrv` и `./build.sh app` проходят в
  Docker-контейнере; `app` кладёт бинарник в `output/out/app_out/usr/bin`
  (sha256 `ddbe6aa2…`, совпадает с `project/app/divinus/source/divinus`), но
  `output/out/oem` не обновляет. Стейджинг `oem` пересобирает
  `./build.sh firmware` (через `__PACKAGE_OEM`): после него
  `output/out/oem/usr/bin/divinus` имеет тот же sha256, `output/image/update.img`
  пересобран. В `FEATURES_VERIFIED.md` команда `make oem` заменена на фактические
  `./build.sh app` / `./build.sh firmware` (цели `make oem` в репозитории нет).
  В `bench/README.md` добавлен раздел «Порядок работы и CMA», в `TESTBENCH.md`
  §9 — признаки исчерпания CMA и правило перезагрузки; дата §1 обновлена.
- **`sysdrv`/`app` builds verified and the `oem` refresh command corrected
  (OKO).** 2026-11-08: `./build.sh sysdrv` and `./build.sh app` pass in the
  Docker container; `app` installs the binary into `output/out/app_out/usr/bin`
  (sha256 `ddbe6aa2…`, identical to `project/app/divinus/source/divinus`) but
  does not update `output/out/oem`. The `oem` staging is rebuilt by
  `./build.sh firmware` (via `__PACKAGE_OEM`): afterwards
  `output/out/oem/usr/bin/divinus` has the same sha256 and
  `output/image/update.img` is regenerated. `FEATURES_VERIFIED.md` now names
  the real `./build.sh app` / `./build.sh firmware` commands instead of
  `make oem` (there is no `make oem` target). `bench/README.md` gained a
  "Workflow and CMA" section, `TESTBENCH.md` §9 the CMA exhaustion signs and the
  reboot rule, and the §1 date is updated.
- **Измерена геометрия NN-рамок и OSD под поворотом (OKO).** Прогон
  `OKO_GEOMETRY_TESTS=1 geometry_check.py --what boxes|osd` 2026-11-07:
  рамки совпали с повёрнутой маской 0° — IoU 0.624 (90°), 0.713 (180°),
  0.711 (270°); OSD-текст остаётся в правом нижнем и левом верхнем углах на
  всех четырёх углах (12 проверок). Канва: 1080x1920 на 90/270, 1920x1080
  на 180. `geometry_check.py` после рестарта ждёт 6 с и берёт 20-й кадр:
  первые кадры после подъёма сервера бывают тёмными и давали ложные провалы.
  Рамки и поворот переведены в `FEATURES_VERIFIED.md`, в `TO_DO.md` §11.5
  закрыты 180/270; эталонная разметка положения рамок остаётся открытой;
  числа — в `VERIFIED_FUNCTIONS.md`.
- **NN box and OSD geometry under rotation measured (OKO).** The 2026-11-07
  `OKO_GEOMETRY_TESTS=1 geometry_check.py --what boxes|osd` run: the boxes
  matched the rotated 0° mask — IoU 0.624 (90°), 0.713 (180°), 0.711 (270°);
  the OSD text keeps the right-bottom and left-top corners at all four angles
  (12 checks). Canvas: 1080x1920 at 90/270, 1920x1080 at 180.
  `geometry_check.py` now waits 6 s after a restart and takes the 20th frame:
  the first frames after the server comes back are sometimes dark and caused
  false failures. Boxes and rotation moved to `FEATURES_VERIFIED.md`, the
  180/270 item in `TO_DO.md` §11.5 is closed; the reference box markup stays
  open; the numbers are in `VERIFIED_FUNCTIONS.md`.
- **ePTZ: смена зума/поворота применяется graceful-рестартом (OKO).**
  `/api/eptz` и `/api/rotate` больше не пытаются менять работающий канал:
  новое значение сохраняется, ответ содержит `applied:false` и
  `note:"settings saved; restarting the application"`, после `send_and_close`
  вызываются `app_config_save()` и `kill(getpid(), SIGHUP)` — тот же путь,
  что у `/api/substream`. `applied:true` отдаётся только когда живой канал
  совпадает с запросом; `crop` — фактический кроп канала (полный кадр при
  выключенном ePTZ), `rc` — код MPP последнего старта, `note` объясняет
  «stream is off»; повтор одинакового запроса рестарта не вызывает. Пока идёт
  поиск автофокуса, `home`/`zero` или прошивка, запрос отклоняется с
  `409 busy`. Кроп и поворот теперь применяются в `rk_channel_bind()` сразу
  после `EnableChn` — включение канала сбрасывает их (замеры 2026-11-07:
  mean|diff| 4.26 к центру 1x против 53.85 к полному кадру). KI-044 закрыт;
  `bench/geometry_check.py` ждёт подъём `/api/status` после POST и проверяет
  кроп после рестарта. Обновлены `doc/endpoints.md`, `divinus/README.md`,
  `WEBUI_REORG.md`, `KNOWN_ISSUES.md`, `FEATURES_VERIFIED.md` и
  `VERIFIED_FUNCTIONS.md`.
- **ePTZ: a zoom/rotation change is applied by a graceful restart (OKO).**
  `/api/eptz` and `/api/rotate` no longer touch a running channel: the new
  value is saved, the response carries `applied:false` and
  `note:"settings saved; restarting the application"`, and after
  `send_and_close` the handler calls `app_config_save()` and
  `kill(getpid(), SIGHUP)` — the `/api/substream` path. `applied:true` is
  returned only when the live channel matches the request; `crop` is the real
  channel rectangle (full frame when ePTZ is off), `rc` is the MPP code of the
  last start, and `note` explains "stream is off"; repeating the same request
  does not restart again. While an autofocus search, a `home`/`zero` job or a
  firmware write runs, the request is refused with `409 busy`. The crop and the
  rotation are now applied in `rk_channel_bind()` right after `EnableChn` —
  the enable resets them (2026-11-07: mean|diff| 4.26 to the centre of the 1x
  frame versus 53.85 to the full frame). KI-044 is closed;
  `bench/geometry_check.py` waits for `/api/status` after a POST and verifies
  the crop after the restart. Updated `doc/endpoints.md`,
  `divinus/README.md`, `WEBUI_REORG.md`, `KNOWN_ISSUES.md`,
  `FEATURES_VERIFIED.md` and `VERIFIED_FUNCTIONS.md`.
- **Документация: TO_DO и FEATURES приведены к индексу (OKO).** `TO_DO.md`
  сокращён до открытых работ: убраны закрытые чек-листы фаз 0–4 и вариантов A/B,
  механика объектива зафиксирована текстом, обновлены §6 («Открытые работы по
  фокусу») и §11.5. `FEATURES_UNVERIFIED.md` вычищен от закрытых примечаний и
  дублей строк таблицы, версии STM8 актуализированы. `FEATURES.md` оставлен
  тонким индексом: паспорт платформы, 5 команд проверки и ссылки на
  `TESTBENCH.md` §8 / `VERIFIED_FUNCTIONS.md` §3; дублирующие примечания
  удалены. Обновлены `AGENTS.md`, `STRUCTURE.md`, `README_RU.md` и
  `.kilo/command/docs-check.md` / `.kilo/skills/oko-docs/SKILL.md`.
- **Docs: TO_DO and FEATURES reduced to an index (OKO).** `TO_DO.md` is cut
  down to open work: the closed checklists of phases 0–4 and variants A/B are
  gone, the lens mechanics are recorded as text, and §6 ("Open focus work") and
  §11.5 are updated. `FEATURES_UNVERIFIED.md` is cleaned of closed notes and
  table-row duplicates, and the STM8 versions are brought up to date.
  `FEATURES.md` is now a thin index: the platform passport, five verification
  commands and links to `TESTBENCH.md` §8 / `VERIFIED_FUNCTIONS.md` §3; the
  duplicated notes are removed. `AGENTS.md`, `STRUCTURE.md`, `README_RU.md` and
  `.kilo/command/docs-check.md` / `.kilo/skills/oko-docs/SKILL.md` are updated.
- **Подтверждение SD-инкремента прогонами стенда (OKO).** Прогоны 2026-11-07
  подтвердили заявленное: `bench/ui_check.py` печатает
  `SD summary: 14.8 GiB / 14.8 GiB (raw 15177/15171 MiB)` и завершается
  `UI check passed` с кодом 0; тот же прогон с `OKO_FOCUS_AF_TESTS=1`
  (кнопка «Автофокус», `af=fine`) тоже даёт `UI check passed`, код 0;
  `bench/api_check.py` — `96 checks, 0 failures`, код 0. Счётчики в
  `bench/README.md`, `FEATURES_VERIFIED.md` и `VERIFIED_FUNCTIONS.md`
  приведены к фактическим 96 проверкам и дате прогона. Staged-копия
  `output/out/oem/usr/bin/divinus` намеренно не пересобиралась: перепрошивка
  в этом цикле не планируется.
- **SD increment confirmed by bench runs (OKO).** The 2026-11-07 runs
  confirmed the claims: `bench/ui_check.py` prints
  `SD summary: 14.8 GiB / 14.8 GiB (raw 15177/15171 MiB)` and ends with
  `UI check passed`, exit code 0; the same run with
  `OKO_FOCUS_AF_TESTS=1` (the Autofocus button, `af=fine`) also returns
  `UI check passed`, exit code 0; `bench/api_check.py` reports
  `96 checks, 0 failures`, exit code 0. The counters in `bench/README.md`,
  `FEATURES_VERIFIED.md` and `VERIFIED_FUNCTIONS.md` are brought to the
  measured 96 checks and the run date. The staged copy
  `output/out/oem/usr/bin/divinus` was deliberately not rebuilt: no reflash
  is planned in this cycle.
- **WebUI: каркас под высоту окна и две колонки Live (OKO).** Страница
  больше не прокручивается целиком: `body` — flex-колонка высотой
  `100vh`/`100dvh` с `overflow: hidden`, `#layout` растягивается
  (`flex: 1 1 auto; min-height: 0`), а прокручиваются только `#sidebar` и
  `#content`; широкие таблицы получили `.card table { display: block;
  overflow-x: auto }`. На ≥ 60em «Видео» — две колонки: превью слева,
  `#focus-card` справа со своей прокруткой (`max-height:
  calc(100dvh - 130px)`). Высота превью изменена с `100vh - 520px` на
  `100dvh - 250px` в двух колонках и `100dvh - 430px` в одной (фолбэк
  `100vh`); рамка ROI по-прежнему позиционируется от медиаэлемента.
  Обновлены `bench/ui_check.py` (проверка отсутствия скролла страницы на
  всех вкладках, две колонки, нижний навбар) и `bench/README.md`.
- **WebUI: viewport shell and two-column Live (OKO).** The page no longer
  scrolls as a whole: `body` is a `100vh`/`100dvh` flex column with
  `overflow: hidden`, `#layout` stretches (`flex: 1 1 auto; min-height: 0`)
  and only `#sidebar` and `#content` scroll; wide tables use
  `.card table { display: block; overflow-x: auto }`. At ≥ 60em Live is two
  columns: the preview on the left and `#focus-card` on the right with its
  own scroll (`max-height: calc(100dvh - 130px)`). The preview height changed
  from `100vh - 520px` to `100dvh - 250px` in two columns and
  `100dvh - 430px` in one column (`100vh` fallback); the ROI box is still
  positioned from the media element. Updated `bench/ui_check.py` (no page
  scroll on any tab, two columns, bottom navbar) and `bench/README.md`.

- **WebUI «Видео»: размер предпросмотра, автовзведение привода и выбор
  зоны (OKO).** `#preview` и его `video`/`img` ограничены по высоте
  (`calc(100vh - 520px)`) с сохранением пропорций; рамка ROI
  позиционируется от прямоугольника медиаэлемента, поэтому letterbox
  больше не смещает её. Переключатель `focus_motor` удалён: каждая команда
  движения (`move`, `speed`, `home`, `zero`, `af`, «Применить») несёт
  `motor=1` и взводит драйвер атомарно, ручной STOP отправляет
  `motor=0&stop=1&af=cancel`, а `GET /api/focus` теперь опрашивает
  контроллер и без взведения, чтобы UI знал о его наличии. Для выбора
  шкалы добавлен параметр `scale=focus|zoom|both` (старый
  `motor=focus|zoom|both` оставлен для скриптов). ROI выбирается
  двухшагово: «Выделить зону» включает режим, drag только запоминает
  `pendingRoi` без POST, «Готово»/Esc завершают или отменяют выбор, а
  «Применить» отправляет зону с `af=1`; вне режима drag показывает
  подсказку. В карточке появилась строка `#af-state` (прогресс, позиция,
  лучший FV) и блокировка ручных/home/zero кнопок на время поиска;
  STOP остаётся доступен для отмены. Обновлены `bench/api_check.py`
  (`af=bogus`→400, busy-матрица, opt-in `OKO_FOCUS_AF_TESTS=1` с записью
  `output/bench/af_curve_*.csv`), `bench/ui_check.py` (двухшаговый выбор
  зоны, отсутствие `#focus_motor`, `#af-state`) и `bench/README.md`.
- **WebUI Live: preview sizing, automatic driver arming and zone selection
  (OKO).** `#preview` and its `video`/`img` are height-limited
  (`calc(100vh - 520px)`) while keeping the aspect ratio; the ROI box is
  positioned from the media element rectangle, so letterboxing no longer
  shifts it. The `focus_motor` selector is gone: every motion command
  (`move`, `speed`, `home`, `zero`, `af`, Apply) carries `motor=1` and arms
  the driver atomically, the manual STOP sends
  `motor=0&stop=1&af=cancel`, and `GET /api/focus` now probes the controller
  even while it is disarmed so the UI knows it is present. The new
  `scale=focus|zoom|both` parameter selects the scale (the legacy
  `motor=focus|zoom|both` stays for scripts). ROI selection is two-step:
  "Select zone" enters the mode, a drag only stores `pendingRoi` without a
  POST, "Done"/Esc finish or cancel, and Apply sends the zone with `af=1`;
  outside the mode a drag shows a hint. The card gained an `#af-state` line
  (progress, position, best FV) and the manual/home/zero buttons are locked
  during a search; STOP stays available to cancel it. Updated
  `bench/api_check.py` (`af=bogus`→400, busy matrix, opt-in
  `OKO_FOCUS_AF_TESTS=1` recording `output/bench/af_curve_*.csv`),
  `bench/ui_check.py` (two-step zone selection, no `#focus_motor`,
  `#af-state`) and `bench/README.md`.

### Исправлено

- **Сборка драйвера atbm на GNU make 4.4 (OKO).** В
  `sysdrv/drv_ko/wifi/atbm/Makefile.build.customer` переменные
  `SDIO_HOST`…`MODPMSTAYAWK` объявлены через `:=` вместо `?=` с
  `$(shell echo …)`, а `DRIVER_PATH` в `atbm/Makefile` — через `:=`.
  Причина: make 4.4 при голом `export` рекурсивно экспортирует такие
  переменные и зависает на предупреждениях «не раскрывается рекурсивно
  для экспорта в функцию оболочки»; на Ubuntu 22.04 (make 4.3) сборка
  проходила. После правки `make -C sysdrv drv` завершается.
- **The atbm driver builds with GNU make 4.4 (OKO).** In
  `sysdrv/drv_ko/wifi/atbm/Makefile.build.customer` the
  `SDIO_HOST`…`MODPMSTAYAWK` variables use `:=` instead of `?=` with
  `$(shell echo …)`, and `DRIVER_PATH` in `atbm/Makefile` is also
  simply expanded. With make 4.4 a bare `export` recursively exports
  such variables and hangs on “recursive variable … references itself
  for export to shell function” warnings; make 4.3 on Ubuntu 22.04
  completed the build. `make -C sysdrv drv` now finishes.

- **`parser-conf.sh` драйвера ssv6x5x сделан исполняемым (OKO).** Файл
  `sysdrv/drv_ko/wifi/ssv6x5x/parser-conf.sh` вызывается через `env` из
  Makefile, но лежал в git с режимом `100644`, из-за чего при каждой
  сборке печаталось `Permission denied` и заголовок парсера не
  обновлялся. Режим исправлен на `100755`.
- **The ssv6x5x `parser-conf.sh` is now executable (OKO).** The driver
  Makefile calls `sysdrv/drv_ko/wifi/ssv6x5x/parser-conf.sh` through
  `env`, but the file was tracked as `100644`, so every build printed
  `Permission denied` and the parser header was never regenerated. The
  mode is fixed to `100755`.

- **Фокус: AF-метрика включается независимо от STM8 (OKO).** `POST
  /api/focus` применяет `enable`/`mode`/`fps`/ROI до взведения привода:
  блок `motor` перенесён после `focus_start`/`focus_configure`, поэтому
  отсутствующий, занятый или находящийся в бутлоадере контроллер больше не
  блокирует измерение. Ошибки `blocked`/409/502 сохранены для чистых моторных
  запросов, а `af=1|fine` по-прежнему требуют взведённого драйвера.
  `api_check.py` получил две проверки `enable=1` без `motor` и `enable=0`;
  обновлены `doc/endpoints.md`, `divinus/README.md`, `WEBUI_REORG.md` §8
  (обход через curl снят) и `KNOWN_ISSUES.md` (KI-043 закрыт). Счётчики в
  `bench/README.md`, `FEATURES_VERIFIED.md` и `VERIFIED_FUNCTIONS.md`
  приведены к фактическому прогону.
- **Focus: the AF metric starts regardless of the STM8 (OKO).** `POST
  /api/focus` applies `enable`/`mode`/`fps`/ROI before arming the driver: the
  `motor` block moved after `focus_start`/`focus_configure`, so a missing, busy
  or bootloader-stuck controller no longer blocks the measurement. The
  `blocked`/409/502 responses remain for pure motor requests, and `af=1|fine`
  still require the armed driver. `api_check.py` gained two checks (`enable=1`
  without `motor`, then `enable=0`); `doc/endpoints.md`, `divinus/README.md`,
  `WEBUI_REORG.md` §8 (the curl workaround is gone) and `KNOWN_ISSUES.md`
  (KI-043 closed) are updated. The counters in `bench/README.md`,
  `FEATURES_VERIFIED.md` and `VERIFIED_FUNCTIONS.md` follow the actual run.
- **ePTZ: кроп/поворот при старте канала и геометрия оверлеев (OKO).**
  Кроп и поворот теперь применяются в `rk_channel_bind()` сразу после
  `EnableChn` (через `rk_apply_stream_settings`), поэтому каждый старт канала
  получает сохранённые настройки, а не теряет их молча: включение канала
  сбрасывает `SetChnCrop`/`SetChnRotation`, а вызов после него работает
  (замеры 2026-11-07). Смена на работающем канале только сохраняется, и
  `/api/eptz` отдаёт `applied:false` и `note` (KI-044).
  NN-рамки поворачиваются вместе с
  картинкой: координаты детекций проходят через кроп и поворот
  (`nn_geom.c`), а не только масштабируются по неповёрнутому кадру.
  Систематический сдвиг рамки до 15 px вверх/влево от квантования на сетку
  16 px убран (выравнивание 4 px). Текстовый OSD при 90/270 переезжает к тому
  же углу кадра, а не остаётся за пределами portrait-картинки.
- **ePTZ: crop/rotation at channel start and overlay geometry (OKO).** The
  crop and rotation are now applied in `rk_channel_bind()` right after
  `EnableChn` (via `rk_apply_stream_settings`), so every channel start receives
  the stored settings instead of silently dropping them: the enable resets
  `SetChnCrop`/`SetChnRotation`, while a call after it works (2026-11-07
  measurements). A change on a running channel is only stored and `/api/eptz`
  reports `applied:false` plus a `note` (KI-044). NN boxes rotate with the
  picture:
  detection coordinates go through the crop and the rotation (`nn_geom.c`)
  instead of being scaled on the unrotated frame only. The systematic
  up-to-15-px up/left shift from the 16-px grid quantization is gone (4-px
  alignment). At 90/270 the OSD text moves to the same picture corner instead
  of staying outside the portrait picture.
- **Документация: ePTZ понижен до «ограничено», версии STM8 — 2.1.1 (OKO).**
  Строка ePTZ убрана из `FEATURES_VERIFIED.md` и добавлена в
  `FEATURES_UNVERIFIED.md` как **ограничено**: API принимает кроп и отдаёт
  запрошенные `x/y/w/h`, но влияние на main в сборке 2026-11-07 не замерялось;
  заведена KI-044 (кроп не наблюдается, HAL теряет код возврата MPP, кроп
  сбрасывается при рестарте канала). `VERIFIED_FUNCTIONS.md` помечает ePTZ как
  «ограничено» с честным доказательством (echo-проверка 2026-11-05). Версии
  STM8 в `FEATURES_VERIFIED.md` / `VERIFIED_FUNCTIONS.md` обновлены с
  2.0.1/2.0.2 до 2.1.1 (релиз `7969e922f`), перекрёстная ссылка на
  «непроверенные» `home`/`zero` убрана — они покрыты `api_check.py`.
- **Docs: ePTZ lowered to "limited", STM8 versions to 2.1.1 (OKO).** The ePTZ
  row is removed from `FEATURES_VERIFIED.md` and added to
  `FEATURES_UNVERIFIED.md` as **limited**: the API accepts the crop and returns
  the requested `x/y/w/h`, but its effect on main was not measured on the
  2026-11-07 build; KI-044 records that the crop is not observable, the HAL
  drops the MPP return code and a channel restart resets the crop.
  `VERIFIED_FUNCTIONS.md` marks ePTZ as "limited" with the honest evidence (a
  2026-11-05 echo check). STM8 versions in `FEATURES_VERIFIED.md` /
  `VERIFIED_FUNCTIONS.md` are updated from 2.0.1/2.0.2 to 2.1.1 (release
  `7969e922f`), and the cross-reference to "unverified" `home`/`zero` is gone
  because `api_check.py` covers them.
- **WebUI: опрос больше не сбрасывает «Включить» в «Помощи фокусировке» (OKO).**
  Поля `#focus_enable`/`#focus_mode`/`#focus_fps` подключены к общей модели
  `groups`/`snapshots` (`data-group="focus"`), кнопка «Применить» помечена
  `data-apply="focus"`/`data-ep="focus"`, снапшот снимается в `init()`.
  `focusApplyValues(dec, force)` пишет поля только при отсутствии
  несохранённой правки или по `force`; `focusPoll` раз в 2 с больше не
  затирает выбор, жёлтая точка и доступность «Применить» считаются через
  `groupDirty`, причём кнопка активна и при ожидающей выделенной зоне
  (`pendingRoi`). Явные действия (`focusStop`, «Автофокус» `af=fine`,
  успешное «Применить») применяют состояние сервера принудительно; при
  отказе сервера правка сохраняется и точку можно применить повторно.
  `bench/ui_check.py` получил регресс-проверку «несохранённый выбор
  переживает цикл опроса» без POST и без движения объектива. Добавлена
  запись KI-043 (включение метрики из WebUI зависит от STM8: `Apply` шлёт
  `motor=1`; обход — `POST /api/focus?enable=1&mode=full` без `motor`).
  Обновлены `KNOWN_ISSUES.md`, `bench/README.md`, `project/app/divinus/README.md`
  и `WEBUI_REORG.md` (RU+EN).
- **WebUI: the poll no longer resets "Enable" in Focus assist (OKO).** The
  `#focus_enable`/`#focus_mode`/`#focus_fps` fields now belong to the shared
  `groups`/`snapshots` model (`data-group="focus"`), the Apply button carries
  `data-apply="focus"`/`data-ep="focus"`, and the baseline snapshot is taken in
  `init()`. `focusApplyValues(dec, force)` writes the fields only when there is
  no unsaved edit or when `force` is set; the 2 s `focusPoll` no longer
  overwrites the selection, the navigation dot and Apply state come from
  `groupDirty`, and Apply stays enabled while a zone selection is pending
  (`pendingRoi`). Explicit actions (`focusStop`, the `af=fine` Autofocus
  button, a successful Apply) accept the server state forcibly; on a server
  rejection the edit is kept and can be applied again. `bench/ui_check.py`
  gained a regression check "an unsaved value survives a poll cycle" that
  sends no POST and does not move the lens. New KI-043: enabling the metric
  from the WebUI depends on the STM8 because Apply sends `motor=1`
  (workaround: `POST /api/focus?enable=1&mode=full` without `motor`).
  `KNOWN_ISSUES.md`, `bench/README.md`, `project/app/divinus/README.md` and
  `WEBUI_REORG.md` (RU+EN) are updated.
- **I2C STM8: сериализация транзакций (OKO).** Каждая I2C-транзакция
  в `focus_motor.c` теперь берёт внутрипроцессный мьютекс, помимо
  advisory-flock. Раньше опрос версии (`firmware_status`, shared-блок) и
  чтение позиции из другого потока могли перемешать фазы записи/чтения:
  `GET_POS` получал ответ предыдущего `GET_STATE` (позиция
  `0x00020101` = «2.1.1»). Ожидание занятой шины ограничено ~100 мс;
  нулевой ответ версии сразу после `SET_POS` перепроверяется, а
  автофокус повторяет чтение позиции перед тем, как счесть шкалу
  испорченной.
- **STM8 I2C: transaction serialisation (OKO).** Every I2C transaction in
  `focus_motor.c` now takes an in-process mutex in addition to the advisory
  flock. Previously the version poll (`firmware_status`, shared lock) and a
  position read from another thread could interleave their write/read phases:
  `GET_POS` received the previous `GET_STATE` reply (position `0x00020101` =
  "2.1.1"). Bus contention now waits for at most ~100 ms; a zero version reply
  right after `SET_POS` is re-read, and the auto-focus retries the position
  read before treating the scale as corrupted.

### Удалено

- **Docker-окружение сборки прошивки удалено (OKO).** Удалены `Dockerfile`,
  `docker-compose.yml`, `docker-entrypoint.sh`, `start.sh` и контейнер
  `luckfox_oko_build_env`; сборка, прошивка и сетевые сервисы стенда
  выполняются на хосте Debian 13 (`tools/native/`, systemd
  `tftpd-hpa`/`nfs-kernel-server`). Из документации и правил агента убраны
  ссылки на контейнер. Отдельные образы `tools/rknn-toolkit2/` и
  `tools/stm8-sdcc/` не затронуты.
- **The firmware build Docker environment is removed (OKO).** `Dockerfile`,
  `docker-compose.yml`, `docker-entrypoint.sh`, `start.sh` and the
  `luckfox_oko_build_env` container are gone; builds, flashing and the bench
  network services now run on the Debian 13 host (`tools/native/`, systemd
  `tftpd-hpa`/`nfs-kernel-server`). Container references were removed from
  the docs and agent rules. The separate `tools/rknn-toolkit2/` and
  `tools/stm8-sdcc/` images are unaffected.

- **Драйвер `motor.ko` (28BYJ-48, pan/tilt) удалён из форка (OKO).** Удалён
  каталог `sysdrv/drv_ko/motor` (исходники, `sample`, `src`), `motor` убран из
  `M_DIRS` в `sysdrv/drv_ko/Makefile`, из `sysdrv/drv_ko/insmod_ko.sh` убрана
  строка `__insmod motor.ko`, из `sysdrv/cfg/package.mk` — блок
  `CONFIG_SYSDRV_ENABLE_MOTOR`/`RK_ENABLE_MOTOR`. В образ модуль никогда не
  попадал; привод фокуса/зума — STM8 по I2C. KI-021 закрыт (2026-11-07).
  Обновлены `STRUCTURE.md`, `INIT_ORDER.md` (§1, §6, §7, §8), `TESTBENCH.md`
  (§1, §8, §9), `README_RU.md`, `KNOWN_ISSUES.md`, `AGENTS.md` и файлы в
  `.kilo/`.
- **The `motor.ko` driver (28BYJ-48, pan/tilt) is removed from the fork
  (OKO).** The `sysdrv/drv_ko/motor` tree (sources, `sample`, `src`) is gone,
  `motor` is dropped from `M_DIRS` in `sysdrv/drv_ko/Makefile`, the
  `__insmod motor.ko` line is removed from `sysdrv/drv_ko/insmod_ko.sh`, and
  the `CONFIG_SYSDRV_ENABLE_MOTOR`/`RK_ENABLE_MOTOR` block is removed from
  `sysdrv/cfg/package.mk`. The module never reached the image; focus/zoom is
  driven by the STM8 over I2C. KI-021 is closed (2026-11-07). `STRUCTURE.md`,
  `INIT_ORDER.md` (§1, §6, §7, §8), `TESTBENCH.md` (§1, §8, §9),
  `README_RU.md`, `KNOWN_ISSUES.md`, `AGENTS.md` and the files under `.kilo/`
  are updated.

## [0.2.0] — 2026-11-07

- `KNOWN_ISSUES.md` — реестр открытых ошибок и ограничений: критичные
  (`vi1`, librga, отсутствие модели 640×384, лицензия YOLOv5), функциональные
  (ночная точность, CMA, OSD, IVS, HDR, VENC), прошивка/конфигурация
  (`update.img` и env, `reboot loader`, `ubinfo`, старый YAML), безопасность и
  инструменты. Для каждой проблемы указаны проявление, обход и статус;
  ссылки добавлены в `STRUCTURE.md`, `README_RU.md`, `FEATURES.md` и `AGENTS.md`.
- `KNOWN_ISSUES.md` — a register of open defects and limitations: critical
  (`vi1`, librga, missing 640×384 model, YOLOv5 licence), functional (night
  accuracy, CMA, OSD, IVS, HDR, VENC), flashing/configuration (`update.img`
  and env, `reboot loader`, `ubinfo`, old YAML), security and tooling. Each
  issue lists its symptom, workaround and status; links added to
  `STRUCTURE.md`, `README_RU.md`, `FEATURES.md` and `AGENTS.md`.

### Изменено

- **WebUI: фокус в «Видео», автозапуск MP4 и ROI поверх потока (OKO).**
  Карточка «Помощь фокусировке» перенесена из «Изображения» в «Видео»
  (`sec-live`). Предпросмотр запускается сам при входе в раздел с каскадом
  MP4 → MJPEG → снимок `/image.jpg` (браузер без HEVC не декодирует H.265 и
  через ~4 с переходит на резервную ветку), при уходе из раздела поток
  освобождается. Зона фокуса выбирается перетаскиванием прямо поверх живого
  видео; рамка `#focus-box` находится в `#preview`, ROI по-прежнему не
  учитывает поворот 90°/270°. Дефолт `app_config.mp4_enable` изменён на
  `true` (фабричный `divinus.yaml` уже включал MP4/H.265), сохранённые
  конфиги `/userdata` не мигрируются. Обновлены `bench/ui_check.py`,
  `project/app/divinus/README.md`, `source/doc/endpoints.md`,
  `source/doc/WEBUI_REORG.md` и `FEATURES_VERIFIED.md`.
- **WebUI: focus card in Live, MP4 auto-start and ROI over the stream (OKO).**
  The Focus assist card moved from Image to Live (`sec-live`). The preview now
  starts by itself when the section opens, with an MP4 → MJPEG → `/image.jpg`
  cascade (a browser without HEVC cannot decode H.265 and switches to the
  fallback after ~4 s); leaving the section releases the stream. The focus area
  is selected by dragging directly over the live video; `#focus-box` lives
  inside `#preview`, and ROI still ignores 90°/270° rotation.
  `app_config.mp4_enable` now defaults to `true` (the shipped `divinus.yaml`
  already enabled MP4/H.265); saved `/userdata` configs are not migrated.
  Updated `bench/ui_check.py`, `project/app/divinus/README.md`,
  `source/doc/endpoints.md`, `source/doc/WEBUI_REORG.md` and
  `FEATURES_VERIFIED.md`.
- **NPU: драйвер `rknpu` 0.9.2 → 0.9.8 (OKO).** Импортирован upstream
  `drivers/rknpu` из `rockchip-linux/kernel`, ветка `develop-5.10`, коммит
  `276af44f9a7be47a2804ecabf37f8898541d244e` (0.9.8, `20240828`). Сохранено
  локальное отличие — enum `MONITOR_TPYE_DEV` в `rknpu_devfreq.c`; DT и
  non-IOMMU режим не менялись. При `CONFIG_PM_DEVFREQ=y` модуль линкует
  `rknpu_devfreq.o` (в 0.9.2 этот код был внутри `rknpu_drv.c`); `FENCE`,
  `SRAM` и `DRM_GEM` остаются выключены. Рантайм `librknnmrt.so` 1.6.0 не
  менялся. Проверено на DUT hot-swap: `v0.9.8`, YOLO и AIISP работают, три
  перезагрузки и 2-часовой soak без деградации; обновлены `FEATURES.md`,
  `FEATURES_VERIFIED.md`, `VERIFIED_FUNCTIONS.md` и `TESTBENCH.md` (§8).
- **NPU: `rknpu` driver 0.9.2 → 0.9.8 (OKO).** Imported the upstream
  `drivers/rknpu` from `rockchip-linux/kernel`, branch `develop-5.10`, commit
  `276af44f9a7be47a2804ecabf37f8898541d244e` (0.9.8, `20240828`). The local
  `MONITOR_TPYE_DEV` enum change in `rknpu_devfreq.c` is preserved; DT and
  non-IOMMU mode are unchanged. With `CONFIG_PM_DEVFREQ=y` the module now links
  `rknpu_devfreq.o` (0.9.2 kept that code inside `rknpu_drv.c`); `FENCE`,
  `SRAM` and `DRM_GEM` stay disabled. The `librknnmrt.so` 1.6.0 runtime is
  untouched. Verified on the DUT via hot-swap: `v0.9.8`, YOLO and AIISP work,
  three reboots and a 2-hour soak show no degradation; updated `FEATURES.md`,
  `FEATURES_VERIFIED.md`, `VERIFIED_FUNCTIONS.md` and `TESTBENCH.md` (§8).
- **VPN: флаги сборки и статус (OKO).** `sysdrv/drv_ko/Makefile` собирает
  `amneziawg` (`M_DIRS`), `sysdrv/cfg/package.mk` получил
  `CONFIG_SYSDRV_ENABLE_AWG=n` + `RK_ENABLE_AWG`, активный board config —
  `RK_ENABLE_AWG=y`; встроенный `CONFIG_WIREGUARD=y` не менялся.
  `/api/status` отдаёт `features.vpn` (хелпер и хотя бы один бинарь `wg`/`awg`).
  Обновлены `STRUCTURE.md`, `INIT_ORDER.md` (S45oko-vpn, порядок модулей),
  `FEATURES.md`, `FEATURES_UNVERIFIED.md` (KI-037…041), `TESTBENCH.md`,
  `bench/README.md`.
- **VPN: build flags and status (OKO).** `sysdrv/drv_ko/Makefile` builds
  `amneziawg` (`M_DIRS`), `sysdrv/cfg/package.mk` gained
  `CONFIG_SYSDRV_ENABLE_AWG=n` + `RK_ENABLE_AWG`, and the active board config
  sets `RK_ENABLE_AWG=y`; the built-in `CONFIG_WIREGUARD=y` is untouched.
  `/api/status` exposes `features.vpn` (helper plus at least one `wg`/`awg`
  binary). Updated `STRUCTURE.md`, `INIT_ORDER.md` (S45oko-vpn, module order),
  `FEATURES.md`, `FEATURES_UNVERIFIED.md` (KI-037…041), `TESTBENCH.md` and
  `bench/README.md`.
- **Версия прошивки из имени файла (OKO).** `/api/flash` больше не использует
  поле `version`: версия берётся из имени файла (`app-1.0.1.bin`,
  `app-2.0.1.ihx`; ровно одна тройка `major.minor.patch`, каждый компонент
  ≤ 255, иначе 400). Для STM32 она упаковывается в метаданные загрузчика
  (`0x00010001`), для STM8 сервер сверяет её с заголовком OKO8 через
  `oko-motor check FILE X.Y.Z` и отклоняет несовпадение до создания job.
  `/api/firmware` показывает STM32 точечно (`1.0.0`) вместо `0x00010000`.
  `Makefile.stm8` и CMake STM32 кладут версию в имена артефактов
  (`app-2.0.1.ihx`, `factory-2.0.1.ihx`, `app-1.0.1.bin`), сохраняя
  `app.ihx`/`app.bin` копиями для ST-Link; в WebUI поле версии удалено.
- **Firmware version from the file name (OKO).** `/api/flash` no longer uses
  the `version` field: the version comes from the file name
  (`app-1.0.1.bin`, `app-2.0.1.ihx`; exactly one `major.minor.patch` triple,
  each component ≤ 255, otherwise 400). STM32 packs it into the bootloader
  metadata (`0x00010001`); STM8 compares it with the OKO8 header through
  `oko-motor check FILE X.Y.Z` and rejects a mismatch before creating a job.
  `/api/firmware` shows STM32 as `1.0.0` instead of `0x00010000`.
  `Makefile.stm8` and the STM32 CMake place the version in artifact names
  (`app-2.0.1.ihx`, `factory-2.0.1.ihx`, `app-1.0.1.bin`) while keeping
  `app.ihx`/`app.bin` copies for ST-Link; the WebUI version field is gone.
- **Итоговый `update.img`.** Пересобран после изменения артефактов: sha256
  `ad0b07ca19e32462540187e770af2a3f1f8bb3b099491a217ded29df8ce625e4`
  (`output/bench/update-img-sha256.txt`); хеш `a4d93f3f…` выше относится к
  сборке до этой правки. `build.sh app`/`firmware`, `api_check.py` (84/84) и
  `ui_check.py` проходят на стенде.
- **Final `update.img`.** Rebuilt after the artifact change: sha256
  `ad0b07ca19e32462540187e770af2a3f1f8bb3b099491a217ded29df8ce625e4`
  (`output/bench/update-img-sha256.txt`); the `a4d93f3f…` hash above belongs
  to the build before this change. `build.sh app`/`firmware`,
  `api_check.py` (84/84) and `ui_check.py` pass on the bench.

### Добавлено

- **STM8: `SET_POS 0x09` и принудительный сброс позиций (2.0.2/2.1.1).**
  Прошивка получила команду `SET_POS` (`[09][motor][int32 BE]`): позиция и
  цель задаются без движения, команда применяется только вне `RUNNING` и
  помечает позицию для сохранения в EEPROM. Версии подняты до **2.0.2**
  (продукт) и **2.1.1** (тестовая плата); `oko-motor` получил `set-pos
  MOTOR VALUE` с проверкой readback и `home MOTOR [SPEED]`. `/api/focus`
  принимает `home=focus|zoom|both` и `zero=focus|zoom|both&value=N`:
  асинхронный job прижимает шкалу к отрицательному упору чанками, ждёт
  `IDLE` и обнуляет позицию; повторный запуск и прошивка контроллера во
  время job’а возвращают HTTP 409, а прошивка ниже 2.0.2/2.1.1 отклоняется
  с явной ошибкой. `GET /api/focus` отдаёт блок `home` и позиции/статусы
  обоих моторов. Homing считает шаги в open-loop и обнуляет шкалу через
  `SET_POS`, не читая отрицательную позицию (KI-042).
- **Ручное управление фокусом и зумом в WebUI (OKO).** `/api/focus`
  понимает `motor=focus|zoom` для `speed`/`move`/`position` (clamp по
  границам выбранной шкалы) и `motor=both` для скорости; в карточке Focus
  assist появились шаг 1/10/50/200, скорость 50–400, кнопки FOCUS/ZOOM ◀▶,
  STOP и readout позиций, а также кнопки Home/Zero с индикатором задачи.
  Дефолты границ заменены на даташитные: focus 0…2935, zoom 0…2275; новые
  ключи `focus.home_margin`, `focus.zoom_speed`, `focus.zoom_min_pos`,
  `focus.zoom_max_pos`. Старые конфиги с неизменённой парой `-100000`/
  `100000` автоматически переводятся на 0/2935 при загрузке.
  `bench/api_check.py` и `bench/ui_check.py`
  дополнены проверками новых параметров и панели.
- **STM8: `SET_POS 0x09` and forced position reset (2.0.2/2.1.1).** The
  firmware gained `SET_POS` (`[09][motor][int32 BE]`): position and target
  are set without motion, the command is applied only outside `RUNNING`, and
  the position is marked for EEPROM persistence. Versions were bumped to
  **2.0.2** (product) and **2.1.1** (test board); `oko-motor` gained
  `set-pos MOTOR VALUE` with a readback check and `home MOTOR [SPEED]`.
  `/api/focus` accepts `home=focus|zoom|both` and
  `zero=focus|zoom|both&value=N`: a background job drives the scale to the
  negative stop in chunks, waits for `IDLE`, then zeroes the position; a
  second job or a controller flash returns HTTP 409, and firmware older than
  2.0.2/2.1.1 is refused with an explicit error. `GET /api/focus` exposes a
  `home` block and both motor positions/statuses. Homing counts steps
  open-loop and zeroes the scale through `SET_POS` without reading the
  negative position (KI-042).
- **Manual focus and zoom control in the WebUI (OKO).** `/api/focus`
  understands `motor=focus|zoom` for `speed`/`move`/`position` (clamped by
  that scale's bounds) and `motor=both` for speed; the Focus assist card now
  has step 1/10/50/200, speed 50–400, FOCUS/ZOOM ◀▶ buttons, STOP and a
  position readout, plus Home/Zero buttons with a job indicator. The default
  bounds now match the datasheet: focus 0…2935, zoom 0…2275; new keys are
  `focus.home_margin`, `focus.zoom_speed`, `focus.zoom_min_pos` and
  `focus.zoom_max_pos`. Old configs that still carry the untouched
  `-100000`/`100000` pair are migrated to 0/2935 on load.
  `bench/api_check.py` and `bench/ui_check.py` cover
  the new parameters and panel.
- **STM8: прошивка тестовой платы `BOARD=test` (2.1.1).** В
  `project/app/stm8-motor` добавлен вариант сборки под тестовую плату на
  STM8S003F3P6 с отдельной картой пинов (`PD5/PC7/PD3/PD2` + `PA1/PA2` для
  focus, `PC4/PC3/PC6/PC5` + `PA3/PD6` для zoom). Карта выбирается на этапе
  компиляции (`-DBOARD_TEST`) в `stepper.c`, `hw_config.c` и `boot_hw.c`;
  продуктовая сборка по умолчанию остаётся 2.0.2. Смена `BOARD`
  отслеживается маркером `build/.board` и автоматически перекомпилирует все
  исходники, поэтому смешанная сборка с чужой картой не возникает.
  `stm8flash` использует профиль `stm8s003f3` (`FLASH_DEV`);
  документация — `docs/HARDWARE_TESTBOARD.md`, модель MCU и размер EEPROM
  уточнены в `TO_DO.md`, `docs/HARDWARE.md/.ru` и `eeprom.h`. Исправлено
  ошибочное утверждение, что пропуск шагов ловится по `nFAULT`: при 5 В и
  обмотках 40 Ом ток заклинившего мотора равен рабочему и OCP не
  срабатывает.
- **STM8: test-board firmware `BOARD=test` (2.1.1).** `project/app/stm8-motor`
  gained a compile-time variant for the STM8S003F3P6 test board with its own
  pin map (`PD5/PC7/PD3/PD2` + `PA1/PA2` for focus, `PC4/PC3/PC6/PC5` +
  `PA3/PD6` for zoom). The map is selected with `-DBOARD_TEST` in
  `stepper.c`, `hw_config.c` and `boot_hw.c`; the default production build
  stays 2.0.2. A `build/.board` marker makes a `BOARD` switch recompile every
  source, so a mixed build with the wrong pin map cannot occur.
  `stm8flash` now uses the `stm8s003f3` profile (`FLASH_DEV`); documented in
  `docs/HARDWARE_TESTBOARD.md`, with the MCU model and EEPROM size corrected
  in `TO_DO.md`, `docs/HARDWARE.md/.ru` and `eeprom.h`. Also corrected the
  claim that missed steps are caught by `nFAULT`: at 5 V and 40 Ω coils a
  stalled motor draws the same current as a running one, so OCP never trips.
- **Архив внешних исследований `research/` (OKO).** Создана не-сборочная
  папка `research/` с правилами оформления тем: заметка о переносимости
  upstream-кода, read-only снимки с провенансом (путь, лицензия, sha256,
  размер, дата, пин) и лимит на тему. Первая тема —
  `research/rockchip-npu-mainline/`: разбор mainline-драйвера NPU
  `drivers/accel/rocket` (RK3588, четыре ioctl, job → tasks, отдельный
  IOMMU-домен на fd) и вывод, что прямой перенос на RV1106 невозможен;
  снимок шести upstream-файлов (включая `rocket_registers.h`) на пине
  `d48723711bae9b8cdc56719c448ba42fef28cbee`, с обязанностью обновлять
  `CHANGELOG.md` при каждой правке темы. Обновлены `STRUCTURE.md`,
  `AGENTS.md` (карта документации) и `README_RU.md`.
- **External research archive `research/` (OKO).** Added the non-build
  `research/` folder with topic rules: a portability note, read-only upstream
  snapshots with provenance (path, licence, sha256, size, date, pin) and a
  per-topic limit. The first topic is `research/rockchip-npu-mainline/`: an
  analysis of the mainline NPU driver `drivers/accel/rocket` (RK3588, four
  ioctls, job → tasks, a separate IOMMU domain per fd) concluding that a
  direct port to RV1106 is not possible; six upstream files (including
  `rocket_registers.h`) are pinned at
  `d48723711bae9b8cdc56719c448ba42fef28cbee`, and every topic change must
  update `CHANGELOG.md`. Updated `STRUCTURE.md`, `AGENTS.md` (documentation
  map) and `README_RU.md`.
- **Рамки детекции YOLO на видеопотоке (OKO).** Порт вендорского NN-оверлея
  rkipc: регион RGN `12` (`layer 6`) формата 2BPP с двумя цветами (класс 0 —
  красный, прочие — синий), канва главного потока `1920×1088` ≈ 518 КиБ
  (BGRA5551 заняла бы ~4.1 МиБ). Перерисовка в `yolo_capture_thread` после
  каждого инференса (5 Hz): канва полностью очищается, поэтому при нуле
  детекций рамки исчезают; координаты `0..10000` масштабируются в размер
  закодированного потока (90/270 — с обменом сторон) и выравниваются по сетке
  16 px. Добавлены `yolo.boxes` (по умолчанию `false`),
  `POST /api/yolo?boxes=0|1`, живое включение/выключение, поле WebUI EN/RU и
  проверка в `gating` при старте YOLO. Регион пересоздаётся при смене размера
  или поворота главного потока и при потере привязки к VENC; без
  `RK_MPI_RGN_GetCanvasInfo`/`UpdateCanvas` детектор продолжает работать, а
  рамки отключаются с одним предупреждением. DUT: CMA +512 КиБ
  (53 824→54 336), CPU `divinus` 29% при 5 fps без роста, `boxes=2|bogus` →
  HTTP 400, поворот 90° пересоздаёт канву `1088×1920`. Обновлены
  `endpoints.md`, `README` divinus, `FEATURES*.md`, `VERIFIED_FUNCTIONS.md`,
  `TESTBENCH.md` и `TO_DO.md`. Ограничения: только главный поток, два цвета,
  180/270 не проверялись.
- **YOLO detection boxes on the video stream (OKO).** Port of the vendor rkipc
  NN overlay: RGN region `12` (`layer 6`) in 2BPP with two colours (class 0 is
  red, everything else is blue) and a `1920×1088` main-stream canvas of about
  518 KiB (BGRA5551 would take ~4.1 MiB). `yolo_capture_thread` repaints it
  after every inference (5 Hz) and clears the whole canvas, so the boxes vanish
  on zero detections; `0..10000` coordinates are scaled to the encoded stream
  (swapped for 90/270) and snapped to the 16-pixel grid. Added `yolo.boxes`
  (default `false`), `POST /api/yolo?boxes=0|1`, live switching, an EN/RU WebUI
  field and a `gating` check when YOLO starts. The region is recreated when the
  main-stream size or rotation changes and when the overlay is no longer
  attached to the VENC channel; without
  `RK_MPI_RGN_GetCanvasInfo`/`UpdateCanvas` the detector keeps running and the
  boxes are disabled with one warning. DUT: CMA +512 KiB (53,824→54,336),
  `divinus` CPU 29% at 5 fps with no increase, `boxes=2|bogus` → HTTP 400, 90°
  rotation recreates the canvas as `1088×1920`. Updated `endpoints.md`, the
  divinus `README`, `FEATURES*.md`, `VERIFIED_FUNCTIONS.md`, `TESTBENCH.md` and
  `TO_DO.md`. Limitations: main stream only, two colours, 180/270 untested.
- **RGA-конверсия NV12→RGB888 для YOLO (OKO).** На `rknpu` 0.9.8 im2d
  `importbuffer_fd` импортирует MMZ- и dma-heap-дескрипторы; добавлен
  опциональный `yolo.convert: cpu | rga` (по умолчанию `cpu`) и HAL
  `hal/plus/rk_rga.c` (dlopen `librga.so`). RGA-путь отдаёт im2d fd кольца и
  входа `rknn_create_mem`, при ошибке один раз логирует и навсегда
  откатывается на CPU. Замер: CPU `divinus` ~35.6% → ~29.4% при 5 fps
  640×384 без потери fps, 2-часовой soak без зависаний. Легаси
  `c_RkRgaBlit` не используется. Обновлены `KNOWN_ISSUES.md` (KI-002),
  `endpoints.md`, `FEATURES*.md`, `VERIFIED_FUNCTIONS.md`, `TESTBENCH.md`.
- **RGA NV12→RGB888 conversion for YOLO (OKO).** On `rknpu` 0.9.8 im2d
  `importbuffer_fd` imports MMZ and dma-heap descriptors, so YOLO gained the
  optional `yolo.convert: cpu | rga` setting (default `cpu`) and an
  `hal/plus/rk_rga.c` HAL (dlopen of `librga.so`). The RGA path feeds im2d
  with the ring and `rknn_create_mem` fds; on an error it logs once and
  permanently falls back to the CPU converter. Measured `divinus` CPU
  ~35.6% → ~29.4% at 5 fps 640×384 with no fps loss and a 2-hour soak without
  hangs. Legacy `c_RkRgaBlit` is not used. Updated `KNOWN_ISSUES.md`
  (KI-002), `endpoints.md`, `FEATURES*.md`, `VERIFIED_FUNCTIONS.md` and
  `TESTBENCH.md`.
- **Второй рантайм RKNN 2.x для YOLO (OKO).** В образ добавлен
  `librknnmrt2.so` (2.3.2, SONAME изменён на `librknnmrt2.so`, см.
  `media/rknn2/README.md`); AIISP и RockIVA продолжают использовать
  `librknnmrt.so` 1.6.0 в том же процессе. `divinus` получил `yolo.runtime:
  1 | 2` и `yolo.model_dir`; HAL держит два handle (2.x — абсолютный путь,
  `RTLD_LOCAL`), после `rknn_init` проверяется major версии SDK.
  `models.json` хранит `runtime` модели, активация «чужого» рантайма
  отклоняется с причиной, переключение 1↔2 не требует рестарта. Сборка модели
  2.x — `tools/rknn-toolkit2/Dockerfile.toolkit2` (вендорское колесо только
  cp38). Обновлены `endpoints.md`, `FEATURES.md`, `FEATURES_VERIFIED.md`,
  `FEATURES_UNVERIFIED.md`, `VERIFIED_FUNCTIONS.md`. Точность (mAP)
  2.x-модели на размеченном наборе не измерялась.
- **Second RKNN 2.x runtime for YOLO (OKO).** The image now ships
  `librknnmrt2.so` (2.3.2, SONAME changed to `librknnmrt2.so`, see
  `media/rknn2/README.md`); AIISP and RockIVA keep using the 1.6.0
  `librknnmrt.so` in the same process. `divinus` gained `yolo.runtime: 1 | 2`
  and `yolo.model_dir`; the HAL keeps two handles (2.x by absolute path,
  `RTLD_LOCAL`) and checks the SDK major version after `rknn_init`.
  `models.json` records the model runtime, activation of a model from the
  other runtime is refused with a reason, and switching 1↔2 needs no restart.
  The 2.x model is built by `tools/rknn-toolkit2/Dockerfile.toolkit2` (the
  vendor wheel is cp38 only). Updated `endpoints.md`, `FEATURES.md`,
  `FEATURES_VERIFIED.md`, `FEATURES_UNVERIFIED.md`, `VERIFIED_FUNCTIONS.md`.
  The 2.x model accuracy (mAP) on a labelled set is not measured.
- **VPN: WireGuard и AmneziaWG (OKO).** Out-of-tree модуль `amneziawg.ko`
  (вендорский слепок `amneziawg-linux-kernel-module` `v3.1.20260906`, без
  правок) в `sysdrv/drv_ko/amneziawg`; вендорские `wireguard-tools`
  1.0.20210914 и `amneziawg-tools` 3.1.20260812 в `project/app/vpn-tools`
  собираются app-тулчейном в `/oem/usr/bin/{wg,awg}` (`wg-quick` отключён).
  POSIX-хелпер `/usr/bin/oko-vpn` (validate/up/down/status/autostart/watchdog/
  delete/set-auto) применяет `.conf` через `syncconf`, ведёт split-маршруты,
  делает guard с откатом default route для `0.0.0.0/0`, хранит профили в
  `/userdata/vpn`; `S45oko-vpn` запускает autostart и watchdog.
  `divinus` получил `vpn_conf.c` (парсер `.conf`, отклоняет
  `PreUp/PostUp/PreDown/PostDown`, знает AWG v3.1), `/api/vpn`,
  `/api/vpn/import`, `/api/vpn/export`, токен на delete, `features.vpn` и
  карточку Network → VPN (EN/RU). Добавлены `bench/vpn_check.py` и хостовый
  тест `tests/vpn_conf_test.c`.
- **VPN: проверено на стенде (OKO).** 2026-11-06 на DUT .97: модуль
  `amneziawg` загружается (`dmesg`: `AmneziaWG 3.1.20260906`), `wg`/`awg`
  версий 1.0.20210914/3.1.20260812; handshake WireGuard и AmneziaWG v3.1 с
  loopback-пиром, split-маршруты, API `import/up/down/delete/export`, отказ
  хукам и delete без токена, autostart+watchdog после reboot, guard полного
  туннеля (positive и auto-revert с восстановлением default). `api_check.py`
  — 88/88, `ui_check.py` (карточка Network → VPN, EN/RU) — без ошибок.
  `./build.sh sysdrv`, `app`, `firmware` проходят: `amneziawg.ko` в
  `/oem/usr/ko` (69 688 Б после strip, vermagic 5.10.160), `oem.img`
  19 005 440 Б, `rootfs.img` 57 016 320 Б, `update.img` 82 971 210 Б
  (sha256 `4e6dbc4d…`). Внешний VPN-пир и `ping` через туннель не
  прогонялись; статус в `FEATURES_UNVERIFIED.md`.
- **VPN: verified on the bench (OKO).** 2026-11-06 on DUT .97: the
  `amneziawg` module loads (`dmesg`: `AmneziaWG 3.1.20260906`), `wg`/`awg` are
  1.0.20210914/3.1.20260812; WireGuard and AmneziaWG v3.1 handshakes with a
  loopback peer, split routes, API `import/up/down/delete/export`, rejection of
  hooks and delete without a token, autostart+watchdog after reboot, and the
  full-tunnel guard (positive and auto-revert restoring the default route).
  `api_check.py` — 88/88, `ui_check.py` (Network → VPN card, EN/RU) — clean.
  `./build.sh sysdrv`, `app` and `firmware` pass: `amneziawg.ko` lands in
  `/oem/usr/ko` (69,688 B after stripping, vermagic 5.10.160), `oem.img` is
  19,005,440 B, `rootfs.img` 57,016,320 B and `update.img` 82,971,210 B
  (sha256 `4e6dbc4d…`). An external VPN peer and a tunnel `ping` were not run;
  status in `FEATURES_UNVERIFIED.md`.
- **VPN: WireGuard and AmneziaWG (OKO).** An out-of-tree `amneziawg.ko`
  (vendored `amneziawg-linux-kernel-module` `v3.1.20260906`, unmodified) in
  `sysdrv/drv_ko/amneziawg`; vendored `wireguard-tools` 1.0.20210914 and
  `amneziawg-tools` 3.1.20260812 in `project/app/vpn-tools` built by the app
  toolchain into `/oem/usr/bin/{wg,awg}` (`wg-quick` disabled). The POSIX
  helper `/usr/bin/oko-vpn` (validate/up/down/status/autostart/watchdog/
  delete/set-auto) applies a `.conf` through `syncconf`, manages split routes,
  guards and rolls back the default route for `0.0.0.0/0`, and keeps profiles
  in `/userdata/vpn`; `S45oko-vpn` starts autostart and the watchdog.
  `divinus` gained `vpn_conf.c` (`.conf` parser, rejects
  `PreUp/PostUp/PreDown/PostDown`, supports AWG v3.1), `/api/vpn`,
  `/api/vpn/import`, `/api/vpn/export`, a delete token, `features.vpn` and the
  Network → VPN card (EN/RU). Added `bench/vpn_check.py` and the host test
  `tests/vpn_conf_test.c`.
- **Помощь фокусировке и прошивка контроллеров (OKO).** `divinus` получил
  тред AF-статистики (`focus.c`, `hal/plus/rk_af_stats.c` — единственный TU с
  заголовками rkaiq), геометрию ROI в базисных пунктах (`focus_geom.c`),
  API `/api/focus` и карточку в Image; мотор STM8 `0x60` на `/dev/i2c-3`
  (`focus_motor.c`, `focus_proto.c`). Добавлены `/api/flash`, `/api/firmware`,
  фоновый воркер прошивки (`flash.c`, стоп `S21oko-ec`, приостановка мотора,
  `oko-flash`), кэш версий STM32/STM8 (`firmware_status.c`) и загрузка NPU в
  строку CPU OSD. Новые пакеты: `project/app/stm8-motor` (прошивка STM8,
  загрузчик I2C, `oko-motor`) и `project/app/oko-flash`; отдельный образ
  `tools/stm8-sdcc`. Разметка STM8: boot `0x8000–0x89FF`, заголовок `0x8A00`,
  приложение `0x8A10–0x9FFF` (2.5 КиБ / 5.5 КиБ; измерено 2412/4279 байт).
  Хостовые тесты: `focus_geom_test.c`, `focus_proto_test.c`,
  `stm8-motor/tests` (CRC, заголовок, кадры, ihx). Стенд:
  `bench/focus_check.py`, `bench/firmware_check.py`.
- **Focus assist and controller firmware (OKO).** `divinus` gained the AF
  statistics thread (`focus.c`, `hal/plus/rk_af_stats.c` is the only TU with
  rkaiq headers), ROI geometry in basis points (`focus_geom.c`), `/api/focus`
  and an Image card; the STM8 motor at `0x60` on `/dev/i2c-3`
  (`focus_motor.c`, `focus_proto.c`). Added `/api/flash`, `/api/firmware`, a
  background flash worker (`flash.c`, stops `S21oko-ec`, suspends the motor,
  runs `oko-flash`), a STM32/STM8 version cache (`firmware_status.c`) and NPU
  load on the OSD CPU line. New packages: `project/app/stm8-motor` (STM8
  firmware, I2C bootloader, `oko-motor`) and `project/app/oko-flash`; a
  separate `tools/stm8-sdcc` image. STM8 layout: boot `0x8000–0x89FF`, header
  `0x8A00`, app `0x8A10–0x9FFF` (2.5 KiB / 5.5 KiB; measured 2412/4279 bytes).
  Host tests: `focus_geom_test.c`, `focus_proto_test.c`, `stm8-motor/tests`
  (CRC, header, frames, ihx). Bench: `bench/focus_check.py`,
  `bench/firmware_check.py`.
- **Исходники прошивки STM32.** Полный репозиторий
  `spectrespc/stm32_acpi-ec` на пине
  `41728ddaf6936d49a5fb65676ca5203dd631b4f6` добавлен как git subtree в
  `project/app/stm32_acpi-ec/` (загрузчик, приложение, HAL, CMake, тесты,
  хостовый инструмент). `tools/stm32_push.sh` коммитит только этот каталог и
  отправляет изменения в `stm32/main`, `tools/stm32_pull.sh` подтягивает
  upstream; сборка STM32 — отдельная CMake-сборка и в `./build.sh` не входит.
  **STM32 firmware sources.** The complete `spectrespc/stm32_acpi-ec`
  repository at commit `41728ddaf6936d49a5fb65676ca5203dd631b4f6` is now a
  git subtree in `project/app/stm32_acpi-ec/` (bootloader, application, HAL,
  CMake, tests, host tool). `tools/stm32_push.sh` commits only that directory
  and pushes to `stm32/main`, `tools/stm32_pull.sh` pulls upstream; the STM32
  build is a separate CMake build and is not part of `./build.sh`.

- **Исходники прошивки STM8.** Локальные изменения (I2C-загрузчик, команда
  `0x08 GET_STATE`, `oko-motor check/boot-reset`, тесты и dual-mode
  `Makefile`) отправлены в `spectrespc/stm8s103_i2c_motor` коммитом
  `ef106e6513afaf15db71c2cbc57628a9dfb932c3`; репозиторий добавлен как git
  subtree в `project/app/stm8-motor/`. `tools/stm8_push.sh` коммитит только
  этот каталог и отправляет изменения в `stm8/main`, `tools/stm8_pull.sh`
  подтягивает upstream; сборка прошивки — SDCC, `oko-motor` собирается в SDK.
  **STM8 firmware sources.** The local changes (I2C bootloader, `0x08
  GET_STATE`, `oko-motor check/boot-reset`, tests and the dual-mode
  `Makefile`) were pushed to `spectrespc/stm8s103_i2c_motor` as commit
  `ef106e6513afaf15db71c2cbc57628a9dfb932c3`; the repository is now a git
  subtree at `project/app/stm8-motor/`. `tools/stm8_push.sh` commits only that
  directory and pushes to `stm8/main`, `tools/stm8_pull.sh` pulls upstream;
  firmware uses SDCC while `oko-motor` is built in the SDK.

- **Статусы задач.** В плане и `TO_DO.md` отмечены выполненные пункты;
  на стенде проверены A6, мотор STM8, загрузчик/обновление STM8, версии,
  NPU OSD и реальная прошивка STM32 (CLI 1.0.1 → WebUI 1.0.0). Открыты
  обрыв питания при стирании и оптическая связь объектива с мотором.
  **Task status.** Completed plan/TO_DO items are marked; the bench verified
  A6, the STM8 motor, the STM8 bootloader/update, versions, NPU OSD and a real
  STM32 update (CLI 1.0.1 → WebUI 1.0.0). Still open: power loss during erase
  and the lens-to-motor optical coupling.

### Исправлено

- **STM8: ложный `STATUS_ERROR` при пробуждении драйвера.** `stepper_tick()`
  опрашивал nFAULT сразу после подъёма nSLEEP, до выдержки `wake_ticks`, и
  DRV8833 в окне tWAKE иногда держал nFAULT низким: 15–25% первых шагов после
  простоя обрывались ошибкой (на стенде 3 из 20 реверсов). Проверка nFAULT
  перенесена за блок `wake_ticks`; на исправленной прошивке 40 движений и
  sweep 100–800 шаг/с — 0 ложных ERROR. **STM8: spurious `STATUS_ERROR` on
  driver wake.** `stepper_tick()` sampled nFAULT immediately after nSLEEP
  rose, before the `wake_ticks` delay; the DRV8833 can hold nFAULT low during
  its tWAKE window, so 15–25% of the first steps after idle aborted with an
  error (3 of 20 reverse cycles on the bench). The nFAULT check now runs
  after the `wake_ticks` block; with the fix, 40 moves and the 100–800 pps
  sweep produced no spurious errors.
- **Сборка STM8 и негативные тесты.** Цели `app`/`factory`/`bin` в
  `Makefile.stm8` сериализованы через `.NOTPARALLEL`, чтобы `make -j` не
  запускал несколько `mkapp.py` над одними файлами; проверено чистым
  `make -j8 all`. `firmware_check.py --negative` теперь завершается ошибкой,
  если `app-2.0.1.ihx` отсутствует, вместо тихого пропуска кейсов.
- **STM8 build and negative tests.** The `app`/`factory`/`bin` targets in
  `Makefile.stm8` are serialized with `.NOTPARALLEL` so `make -j` cannot run
  several `mkapp.py` instances over the same files (verified with a clean
  `make -j8 all`). `firmware_check.py --negative` now fails when
  `app-2.0.1.ihx` is missing instead of silently skipping cases.

- **OSD: три строки снова рендерятся.** `oko-osd` снова пишет literal
  `\n` (backslash + n) в `/tmp/osd-sys.txt`: `region_read_text_file()`
  читает только первую строку файла, а `text.c` распознаёт только эту
  последовательность. Вариант с реальными `0x0A` оставлял в кадре лишь
  `CPU … NPU …`. Проверено `od -c` и тремя текст-полосами в вырезке
  sys-региона (`output/bench/osd-fix2/`).
- **OSD: three lines render again.** `oko-osd` writes the literal `\n`
  (backslash + n) sequence to `/tmp/osd-sys.txt` again:
  `region_read_text_file()` reads only the first line of the file and
  `text.c` recognizes only that sequence. Real `0x0A` bytes left only
  `CPU … NPU …` in the frame. Verified with `od -c` and three text bands in
  the sys crop (`output/bench/osd-fix2/`).
- **STM32: слот CBT6 и защита от переразмера.** Установленный загрузчик —
  CBT6 (128 КиБ, слот 120 КиБ), а не C8T6: он принимает 60 КиБ, а
  C8T6-сборка приложения даёт `img_size=0` (метаданные читаются из страниц
  C8T6). Образы пересобраны с `-DMCU=CBT6`; `/api/flash` отклоняет
  STM32-файлы >120 КиБ до создания job; `oko-ec flash` при ошибке записи
  отправляет `RESET` и возвращает приложение (проверено: 130 КиБ →
  `ERR_PARAM` → `RESET` → app 1.0.0).
- **STM32: CBT6 slot and oversize guard.** The installed bootloader is CBT6
  (128 KiB, 120 KiB app slot), not C8T6: it accepts a 60 KiB image, while a
  C8T6 application build reports `img_size=0` (it reads metadata from the
  C8T6 pages). Images are rebuilt with `-DMCU=CBT6`; `/api/flash` rejects
  STM32 files >120 KiB before creating a job; `oko-ec flash` sends `RESET`
  on a write error and returns to the application (verified: 130 KiB →
  `ERR_PARAM` → `RESET` → app 1.0.0).
- **STM32: реальная прошивка проверена.** CLI `1.0.1` (`0x00010001`) →
  `Application started`, `img_size=2324`; затем WebUI `/api/flash` `1.0.0`
  (`0x00010000`) → job `done`, телеметрия и `flags=0`; после reboot —
  app 1.0.0. KI-036 закрыт. Финальный `update.img` — sha256 `a4d93f3f…`.
- **STM32: real flashing verified.** CLI `1.0.1` (`0x00010001`) →
  `Application started`, `img_size=2324`; then the WebUI `/api/flash`
  `1.0.0` (`0x00010000`) → job `done`, telemetry and `flags=0`; after reboot
  the app runs 1.0.0. KI-036 is closed. Final `update.img` sha256
  `a4d93f3f…`.
- **STM32: вход в загрузчик и запуск после прошивки.** `oko-ec flash` теперь
  проверяет роль через `GET_INFO`, при `device_type=2` отправляет
  `ENTER_UPDATE` (0x0E) и ждёт загрузчик до 5 с, а после `COMMIT` отправляет
  `RESET` (0x08) и ждёт приложение до 3 с; при неподтверждённом старте
  печатается предупреждение. Раньше `BEGIN_UPDATE` уходил в работающее
  приложение (`ERR_CMD`), а после `COMMIT` загрузчик оставался активным.
  Переход проверен на стенде без записи: `info` → `enter-update` →
  `info` (bootloader) → `reset` → `info` (app 1.0.0), телеметрия жива.
  Реальная запись и закрытие KI-036 — в записи выше; финальный `update.img`
  — sha256 `a4d93f3f…`.
- **STM32: версия через CMake cache.** `FW_VERSION_STRING` переведён в
  `CACHE STRING`, поэтому `-DFW_VERSION_STRING=1.0.1` даёт
  `FW_VERSION=0x00010001`, а не перекрывается значением по умолчанию.
  Собраны `output/bench/stm32/app-1.0.0.bin` (sha256 `4bb4f433…`) и
  `app-1.0.1.bin` (sha256 `cb1f8e6e…`) для CBT6.
- **STM32: bootloader entry and application start after a flash.**
  `oko-ec flash` now checks the device role via `GET_INFO`, sends
  `ENTER_UPDATE` (0x0E) when `device_type=2` and waits up to 5 s for the
  bootloader, then sends `RESET` (0x08) after `COMMIT` and waits up to 3 s for
  the application; an unconfirmed start produces a warning. Before this,
  `BEGIN_UPDATE` went to the running application (`ERR_CMD`) and the
  bootloader stayed active after `COMMIT`. The transition is verified on the
  bench without writing: `info` → `enter-update` → `info` (bootloader) →
  `reset` → `info` (app 1.0.0), telemetry still alive. The real write and
  KI-036 closure are in the entry above; the final `update.img` sha256 is
  `a4d93f3f…`.
- **STM32: firmware version through the CMake cache.**
  `FW_VERSION_STRING` is now a `CACHE STRING`, so
  `-DFW_VERSION_STRING=1.0.1` yields `FW_VERSION=0x00010001` instead of being
  overridden by the default. Built `output/bench/stm32/app-1.0.0.bin`
  (sha256 `4bb4f433…`) and `app-1.0.1.bin` (sha256 `cb1f8e6e…`) for CBT6.
- **AF-статистика SC230AI.** В IQ включён
  `af_v31.TuningPara.contrast_af.enable=1` (оба блока, день/ночь); без него
  AFM отдавал 225 нулевых ячеек. A6 пройден: `status=running`,
  `cells_used=225`, `metric_avg` 527k–530k за 20 с. ROI на этой сборке
  применяется выбором ячеек полнокадровой карты (аппаратное окно AFM карту не
  меняет), поэтому `mode=roi` теперь меняет метрику. Неудачное применение окна
  повторяется, а `SetAttrib` до 5 раз; нулевая карта один раз включает
  `contrast_af` как fallback.
- **SC230AI AF statistics.** The IQ now has
  `af_v31.TuningPara.contrast_af.enable=1` (both day/night blocks); without it
  the AFM returned 225 zero cells. A6 passes: `status=running`,
  `cells_used=225`, `metric_avg` 527k–530k over 20 s. On this build the ROI is
  applied by selecting cells of the full-frame map (the hardware AFM window
  does not move the map), so `mode=roi` now changes the metric. A failed window
  apply is retried, `SetAttrib` is retried up to 5 times, and a zero map enables
  `contrast_af` once as a fallback.
- **STM8: роль и версия без boot-команды.** Добавлена общая команда `0x08`
  (`[role][major][minor][patch]`); `oko-motor`, `focus_motor.c` и
  `firmware_status.c` сначала читают роль, поэтому `0x10` больше не уходит в
  работающее приложение, а `info` в загрузчике не принимает `06 00 00` за
  версию `6.0.0`. Прошивка STM8 пересобрана: boot 2412 Б, app 4279 Б.
- **STM8: role and version without a boot command.** Added the shared `0x08`
  command (`[role][major][minor][patch]`); `oko-motor`, `focus_motor.c` and
  `firmware_status.c` read the role first, so `0x10` never reaches a running
  application and `info` in the bootloader no longer mistakes `06 00 00` for
  version `6.0.0`. STM8 firmware rebuilt: boot 2412 B, app 4279 B.
- **STM8: загрузчик и прошивка по I2C.** Исправлен порядок инициализации I2C
  в загрузчике (`PE` до `ACK`), IWDG кормится в циклах стирания/записи, а
  `oko-motor` выдерживает паузу перед чтением статуса (`erase` 500 мс,
  `write` 40 мс, `finish` 150 мс). `enter-boot`/`boot-reset`/`info` проверены;
  `app.ihx` заливается через `/api/flash`; `oko-motor move 0 -10` больше не
  воспринимает `-10` как опцию (`+` в `getopt`). Первичная запись —
  `factory.ihx` через ST-Link.
- **STM8: bootloader and I2C flashing.** Fixed the bootloader I2C init order
  (`PE` before `ACK`), the IWDG is fed in erase/write loops, and `oko-motor`
  waits before reading a status (`erase` 500 ms, `write` 40 ms, `finish`
  150 ms). `enter-boot`/`boot-reset`/`info` are verified; `app.ihx` uploads via
  `/api/flash`; `oko-motor move 0 -10` no longer treats `-10` as an option
  (`+` in `getopt`). The first write is `factory.ihx` over ST-Link.
- **Фокус и прошивка: безопасность и устойчивость.** `GET /api/focus`
  теперь только читает (`map=1`), все изменения (`enable`, `mode`, ROI,
  `motor`, `move`, `stop`, `reset`) требуют `POST`; WebUI и `focus_check.py`
  отправляют их POST-запросом с query. Временный файл прошивки создаётся
  через `mkstemp` с расширением и открывается с `O_NOFOLLOW`; `sha256sum`,
  `oko-motor check`, `oko-flash` и init-скрипты запускаются через
  `fork`+`execv` с абсолютными путями (без `popen`/`system`/`execvp`), дочерние
  процессы не наследуют сокеты divinus. `S21oko-ec` перезапускается только
  если демон работал до задачи (по `/tmp/oko-ec.pid`). Ошибка
  `focus_motor_stop_all()` при `motor=0` возвращает 502 вместо ложного
  «disabled». Строка `/tmp/osd-sys.txt` снова содержит три строки
  (`\n` исправлен).
- **Focus and flashing safety.** `GET /api/focus` is read-only (`map=1`); all
  changes (`enable`, `mode`, ROI, `motor`, `move`, `stop`, `reset`) require
  `POST`; the WebUI and `focus_check.py` send them as POST queries. The
  firmware temp file is created with `mkstemp` plus the extension and opened
  with `O_NOFOLLOW`; `sha256sum`, `oko-motor check`, `oko-flash` and init
  scripts run via `fork`+`execv` with absolute paths (no
  `popen`/`system`/`execvp`), and children do not inherit divinus sockets.
  `S21oko-ec` is restarted only if the daemon was running before the job
  (checked through `/tmp/oko-ec.pid`). A failed `focus_motor_stop_all()` on
  `motor=0` returns 502 instead of reporting disabled. `/tmp/osd-sys.txt` is
  back to three lines (the `\n` escaping is fixed).

- **Прошивка через WebUI.** Присутствие контроллера берётся из кэша
  `firmware_status`, а не сырой записью в I2C; временный файл сохраняет
  расширение (`.ihx/.hex/.bin`), поэтому HEX не разбирается как BIN; перед
  заданием STM8 проверяется `oko-motor check` (магия/CRC/диапазон) и плохой
  образ отдаёт 400; SNMP-таймстамп STM32 (`ts`) старше 15 с считается
  отсутствием. Добавлены негативные проверки `firmware_check.py --negative`.
- **WebUI flashing.** Controller presence comes from the `firmware_status`
  cache instead of a raw I2C write; the temporary file keeps its extension
  (`.ihx/.hex/.bin`), so HEX is not parsed as BIN; `oko-motor check`
  (magic/CRC/range) runs before an STM8 job and a bad image returns 400; an
  STM32 snapshot (`ts`) older than 15 s counts as absent. Added
  `firmware_check.py --negative`.

- `tools/rknn-toolkit2/` — Docker-пайплайн YOLOv5n для `rv1106`: образ
  `rknn-toolkit2` 1.6.0 (колесо с `--no-deps`), отдельный образ для экспорта
  `airockchip/yolov5`, `export_yolov5.sh`, `convert_rknn.sh`, `convert.py`.
  Собрана модель 640×384 INT8 (2.6 МиБ, sha256 `8285e1bd…`), активирована на
  DUT: `input 640x384 NHWC stride 640, 3 4D outputs`; 5 fps — NPU 17%, CPU
  `divinus` ~28.5%, main 30.3 fps (640×640 для сравнения: NPU 28%, CPU ~66%).
  Симулятор toolkit на `bus.jpg` даёт 3 person + 1 truck; mAP не измерен.
  Обновлены `TO_DO.md`, `KNOWN_ISSUES.md`, `FEATURES.md`,
  `VERIFIED_FUNCTIONS.md` и `STRUCTURE.md`.
- `tools/rknn-toolkit2/` — Docker pipeline for YOLOv5n on `rv1106`: an
  `rknn-toolkit2` 1.6.0 image (wheel installed with `--no-deps`), a separate
  `airockchip/yolov5` export image, `export_yolov5.sh`, `convert_rknn.sh` and
  `convert.py`. A 640×384 INT8 model was built (2.6 MiB, sha256 `8285e1bd…`)
  and activated on the DUT: `input 640x384 NHWC stride 640, 3 4D outputs`;
  at 5 fps — NPU 17%, `divinus` CPU ~28.5%, main 30.3 fps (640×640 control:
  NPU 28%, CPU ~66%). The toolkit simulator detects 3 persons + 1 truck on
  `bus.jpg`; mAP is not measured. `TO_DO.md`, `KNOWN_ISSUES.md`,
  `FEATURES.md`, `VERIFIED_FUNCTIONS.md` and `STRUCTURE.md` were updated.
- `FEATURES.md` в корне: сводка поддерживаемого функционала — платформа и состав
  образа, видео/стриминг и статусы гейтов, WebUI/API, сеть, `oko-ec`, microSD,
  известные ограничения и команды проверки; продублировано краткой
  English-сводкой. Ссылки добавлены в `README_RU.md`, `STRUCTURE.md` и
  `AGENTS.md`.
- `FEATURES.md` in the repository root: a summary of supported functionality —
  platform and image contents, video/streaming and gate statuses, WebUI/API,
  networking, `oko-ec`, microSD, known limitations and verification commands,
  with a short English digest. Links added to `README_RU.md`, `STRUCTURE.md`
  and `AGENTS.md`.
- I2C3-M1 переведён на 100 кГц (`clock-frequency = <100000>`) в активном
  `rv1106g-luckfox-pico-pro-max-sc230ai.dts`; устройства SHT21/INA3221/
  STM32 ACPI-EC на `/dev/i2c-3` работают на этой скорости.
- I2C3-M1 runs at 100 kHz (`clock-frequency = <100000>`) in the active
  `rv1106g-luckfox-pico-pro-max-sc230ai.dts`; the SHT21/INA3221/STM32
  ACPI-EC devices on `/dev/i2c-3` use that bus speed.
- WebUI/API: `/api/network` и секция **Network → Ethernet** переключают
  DHCP/static IPv4. Настройки `oko_net_mode`, `oko_ipaddr`, `oko_netmask`,
  `oko_gateway`, `oko_dns` и постоянный `oko_mac` хранятся в env-разделе
  U-Boot (`fw_printenv`/`fw_setenv`, `/etc/fw_env.config`), а не в
  `/userdata`, и переживают reboot и покомпонентную прошивку. Полный
  `update.img` перезаписывает `env.img`, но MAC восстанавливается из serial;
  статические настройки после полной прошивки задаются заново.
- WebUI/API: `/api/network` and **Network → Ethernet** switch DHCP/static
  IPv4. `oko_net_mode`, `oko_ipaddr`, `oko_netmask`, `oko_gateway`,
  `oko_dns` and the persistent `oko_mac` live in the U-Boot env partition
  (`fw_printenv`/`fw_setenv`, `/etc/fw_env.config`), not in `/userdata`, so
  they survive a reboot and component flashing. A full `update.img` rewrites
  `env.img`, but the MAC is re-derived from the serial number; static
  settings must be re-applied after a full flash.
- Buildroot: включены `BR2_PACKAGE_UBOOT_TOOLS=y` и
  `BR2_PACKAGE_UBOOT_TOOLS_FWPRINTENV=y` (`/usr/sbin/fw_printenv`,
  `fw_setenv`); добавлен `/etc/fw_env.config` для `/dev/mtd0`.
- Buildroot: enabled `BR2_PACKAGE_UBOOT_TOOLS=y` and
  `BR2_PACKAGE_UBOOT_TOOLS_FWPRINTENV=y` (`/usr/sbin/fw_printenv`,
  `fw_setenv`); added `/etc/fw_env.config` for `/dev/mtd0`.
- `divinus`: медиа-фичи этапа divinus-media-features — ePTZ (кроп VPSS),
  поворот 90/180/270 (VENC), SmartP/SVC/motion deblur/QP, второй поток
  `/live/1` (RTSP на путь), AIISP (`/api/aiisp`, модель
  `rkmodel_1080_1920.aiisp`), IVS (`/api/ivs`: MD/OD, снимок, клип, OSD),
  runtime IQ и HDR API; непроверенные комбинации блокируются `gating.c`
  с причиной. WebUI получил секции Geometry, Encoding, Night image, Image,
  Analytics, Sub stream, Streams и Storage. Результаты гейтов —
  `project/app/divinus/source/doc/README.md`.
- `divinus`: media feature set from the divinus-media-features stage — ePTZ
  (VPSS crop), 90/180/270 rotation (VENC), SmartP/SVC/motion deblur/QP, a
  second `/live/1` stream (per-path RTSP), AIISP (`/api/aiisp`, model
  `rkmodel_1080_1920.aiisp`), IVS (`/api/ivs`: MD/OD, snapshot, clip, OSD),
  runtime IQ and HDR APIs; unverified combinations are refused by `gating.c`
  with a reason. The WebUI gained Geometry, Encoding, Night image, Image,
  Analytics, Sub stream, Streams and Storage sections. Gate results are in
  `project/app/divinus/source/doc/README.md`.
- microSD: udev-скрипт `oko-sdcard` монтирует карту в `/mnt/sdcard` и пишет
  `/run/oko-sd.state`; `record.source`, `record.max_bytes` и
  `record.min_free_mb` выбирают поток и чистят старые сегменты. Запись в `/oem`
  и `/userdata` запрещена, без реально смонтированной карты — отказ.
  DTS `&sdmmc` переведён из `non-removable`/SDIO в режим съёмной карты.
- microSD: the `oko-sdcard` udev helper mounts the card at `/mnt/sdcard` and
  writes `/run/oko-sd.state`; `record.source`, `record.max_bytes` and
  `record.min_free_mb` select the feed and prune the oldest segments. Writing
  to `/oem` or `/userdata` is refused, and recording without a real mount is
  blocked. The `&sdmmc` DTS node no longer uses `non-removable`/SDIO flags.
- Сборка: `RK_AIISP_MODEL=rkmodel_1080_1920.aiisp` в BoardConfig копирует
  выбранную модель в `/oem/usr/lib`; из oem удаляются стресс/AVS-демо
  (`sample_*_stresstest`, `sample_*avs*`, `rk_mpi_avs_test`, `rkisp_demo`,
  `j2s4b_dev`), чтобы уменьшить размер oem.
- Build: `RK_AIISP_MODEL=rkmodel_1080_1920.aiisp` in the board config installs
  the selected model in `/oem/usr/lib`; stress/AVS demos
  (`sample_*_stresstest`, `sample_*avs*`, `rk_mpi_avs_test`, `rkisp_demo`,
  `j2s4b_dev`) are pruned from oem to reduce its size.
- `oko-osd`: в эмуляцию телеметрии добавлены LTE (RSRP, dBm + 1–5 полос) и
  WiFi (RSSI, dBm + 1–5 полос); значения меняются раз в 15 с вместе с
  GPS/температурой/батареей.
- `oko-osd`: added simulated LTE (RSRP in dBm + 1–5 bars) and WiFi (RSSI in dBm
  + 1–5 bars) to the telemetry; values change every 15 s together with
  GPS/temperature/battery.
- `project/app/divinus/`: вендоренный OpenIPC/divinus (MIT, upstream-коммит
  `bca1932ed8b348af68fb33a82a67218e8f45db7a`), Makefile-обёртка для сборки в общем
  цикле SDK (кросс-компилятор `arm-rockchip830-linux-uclibcgnueabihf`, hard-float)
  со стейджингом `/oem/usr/bin/divinus` и `/oem/usr/share/divinus/divinus.yaml`,
  фабричный конфиг под SC230AI (H.265 1920x1080/25, JPEG, web 80 и RTSP 554
  с обязательной аутентификацией, watchdog 0) и `README.md` с пином коммита
  и правилами обновления.
- `RkLunch.sh`: переключатель `/userdata/divinus.enable` — opt-in запуск `divinus`
  вместо `rkipc`; без маркера порядок инициализации и запуск `rkipc` не меняются.
- `project/app/divinus/`: vendored OpenIPC/divinus (MIT, upstream commit
  `bca1932ed8b348af68fb33a82a67218e8f45db7a`), a Makefile wrapper for the SDK build
  loop (cross-compiler `arm-rockchip830-linux-uclibcgnueabihf`, hard-float) that
  stages `/oem/usr/bin/divinus` and `/oem/usr/share/divinus/divinus.yaml`, a
  SC230AI factory config (H.265 1920x1080/25, JPEG, web 80 and RTSP 554 with
  authentication enabled, watchdog 0), and a `README.md` with the pinned commit
  and update rules.
- `RkLunch.sh`: `/userdata/divinus.enable` toggle for opt-in `divinus` startup
  instead of `rkipc`; without the marker the init order and `rkipc` startup are
  unchanged.
- `project/app/divinus/source/src/compat.c`: локальный патч ABI `mmap` для
  uClibc hard-float — собственный 64-битный `mmap` (из-за
  `_FILE_OFFSET_BITS=64` и `-rdynamic`) перехватывал вызовы `librockit.so`
  с 32-битным `off_t`, offset читался из неверного stack-слота, и кольцо VENC
  не отображалось (`mmap part 1 failed!`). Для `__ARM_PCS_VFP && __UCLIBC__`
  используется libc-вариант `mmap`/`mmap64`; `LD_PRELOAD` не нужен.
- `project/app/divinus/source/src/compat.c`: local ABI `mmap` patch for uClibc
  hard-float — the package's own 64-bit `mmap` (caused by
  `_FILE_OFFSET_BITS=64` plus `-rdynamic`) interposed the 32-bit `off_t`
  `mmap` calls of `librockit.so`, the offset was read from the wrong stack slot
  and the VENC ring could not be mapped (`mmap part 1 failed!`). On
  `__ARM_PCS_VFP && __UCLIBC__` the libc `mmap`/`mmap64` is used; no
  `LD_PRELOAD` is required.
- Тестовый маркер `S99oko-test` в оверлее `overlay-luckfox-buildroot-init`:
  скрипт пишет `/tmp/oko-test-ok` (`marker=OKO-TEST-2026.10-01`, uptime, версия
  ядра) и строку `OKO-TEST: …` в `/dev/kmsg`; доказывает, что загружена сборка
  после этой правки.
- `project/cfg/BoardConfig_IPC/overlay/overlay-luckfox-buildroot-init/`:
  `etc/init.d/S20oko-osd` (активация `divinus` по умолчанию при отсутствии
  `/userdata/rkipc.enable`, бутстрап OSD-конфига, создание `/tmp/osd.fifo` и
  запуск демона) и `usr/bin/oko-osd` (эмуляция GPS/температуры/АКБ в
  `/tmp/osd-dyn.txt` каждые 15 с; сообщения из FIFO в `/tmp/osd-msg.txt`
  с TTL 60 с). UART/ADC-оборудование не используется.
- `project/app/divinus/`: фабричный `divinus.yaml` включает OSD (`reg0` —
  дата/время `$t`, `reg1` — телеметрия, `reg2` — внешние сообщения) и локальный
  патч `source/src/region.c`: текст региона `@<path>` рендерится из первой строки
  файла, перечитывается раз в секунду, обновляет кадр при изменении и гасит
  регион при пустом/нечитаемом файле.
- `project/app/divinus/`: локальный патч `source/src/hal/plus/rk_hal.c` и
  `rk_rgn.h` для OSD на RV1106: преобразование растра `ARGB1555` в `BGRA5551`,
  выравнивание позиции и размера региона на 16 пикселей, поле `BITMAP_S`
  перед указателем данных (`pData` на смещении 16 в используемом
  `librockit.so`), выделение canvas региона и отказ от повторного прикрепления
  уже прикреплённого региона (иначе `RK_MPI_RGN_SetBitMap` возвращает ошибку,
  а повторный attach приводит к oops).
- `project/app/divinus/`: local `source/src/hal/plus/rk_hal.c` and `rk_rgn.h`
  patch for RV1106 OSD: converts `ARGB1555` text bitmaps to `BGRA5551`, aligns
  the region position and size to 16 pixels, adds the `BITMAP_S` field before
  the data pointer (`pData` at offset 16 in the shipped `librockit.so`),
  allocates the region canvas and skips re-attaching an already attached region
  (otherwise `RK_MPI_RGN_SetBitMap` fails and a repeated attach leads to an
  oops).
- Overlay `overlay-luckfox-buildroot-init`: `etc/init.d/S20oko-osd` (enables
  `divinus` by default unless `/userdata/rkipc.enable` exists, bootstraps the OSD
  config, creates `/tmp/osd.fifo`, starts the daemon) and `usr/bin/oko-osd`
  (simulated GPS/temperature/battery written to `/tmp/osd-dyn.txt` every 15 s;
  FIFO messages shown in `/tmp/osd-msg.txt` for 60 s). No UART/ADC hardware is
  used.
- `project/app/divinus/`: the factory `divinus.yaml` enables OSD (`reg0` —
  date/time `$t`, `reg1` — telemetry, `reg2` — external messages) and a local
  `source/src/region.c` patch renders `@<path>` region text from the first line
  of a file, re-reads it once per second, refreshes the frame on change and
  hides the region while the file is empty or unreadable.
- Netboot-монтирование `oem` и `userdata` с хоста: `mount_part()` в генерируемом
  `project/build.sh` `S20linkmount` при `root=/dev/nfs` монтирует
  `<host>:<nfsroot>/.nfsroot/<раздел>` по NFS (сервер и путь парсятся из
  `nfsroot=` в `/proc/cmdline`, в том числе без завершающих опций; при неудачном
  разборе печатается причина); при ошибке монтирования — откат на ubifs из SPI NAND.
- `RkLunch.sh`: ветка выбора `/oem/usr/share/rkipc-300w.ini` для сенсора
  `sc230ai` (раньше `post_chk` завершался с `Error: not found rkipc.ini !!!`).
- `RkLunch.sh`: в netboot (`nfsroot=` в cmdline) `network_init` не меняет MAC и
  не запускает `udhcpc` — статический адрес из `ip=` сохраняется, NFS-соединения
  корня и разделов не разрываются.
- `bench/`: утилиты тестового стенда в репозитории вместо `/tmp` —
  `serial_exec.py` (UART), `dut.py` (SSH), `netboot.py` (`catch`/`reboot`/`cmd`
  для U-Boot), `osd_check.py` (попарный диф OSD-регионов по RTSP), а также
  `setup.sh`, `run.sh`, `requirements.txt` и `README.md`; venv, логи и кадры —
  в `output/bench/` (gitignore).
- `bench/`: bench utilities in the repository instead of `/tmp` —
  `serial_exec.py` (UART), `dut.py` (SSH), `netboot.py` (`catch`/`reboot`/`cmd`
  for U-Boot), `osd_check.py` (pairwise OSD region diff over RTSP), plus
  `setup.sh`, `run.sh`, `requirements.txt` and `README.md`; the venv, logs and
  frames live in `output/bench/` (gitignore).
- `project/app/divinus/tests/bmp_align_test.c`: хостовый тест выравнивания
  растра OSD (660x46 → 672x48, stride 672), не входит в Makefile пакета;
  правило ре-вендоринга требует прогонять его после HAL-патча.
- `project/app/divinus/tests/bmp_align_test.c`: host test for the OSD raster
  alignment (660x46 → 672x48, stride 672), not part of the package Makefile;
  the re-vendoring rules require running it after the HAL patch.
- I2C3-M1 включён по умолчанию на 100 кГц:
  `sysdrv/source/kernel/arch/arm/boot/dts/rv1106g-luckfox-pico-pro-max.dts`,
  блок `&i2c3` — `status = "okay"`, `clock-frequency = <100000>`,
  `pinctrl-0 = <&i2c3m1_xfer>` (выводы `GPIO1_D2`/`GPIO1_D3`). Устройство
  `/dev/i2c-3` появляется при загрузке без вызова `luckfox-config`; группы
  I2C3-M0 из общего dtsi больше не мультиплексируются.
- I2C3-M1 enabled by default at 100 kHz:
  `sysdrv/source/kernel/arch/arm/boot/dts/rv1106g-luckfox-pico-pro-max.dts`,
  the `&i2c3` node — `status = "okay"`, `clock-frequency = <100000>`,
  `pinctrl-0 = <&i2c3m1_xfer>` (`GPIO1_D2`/`GPIO1_D3` pins). `/dev/i2c-3` is
  available at boot without running `luckfox-config`; the I2C3-M0 groups from
  the shared dtsi are no longer muxed.
- `project/app/oko-ec/`: утилита `/oem/usr/bin/oko-ec` для устройств на
  `/dev/i2c-3` — SHT21 `0x40`, INA226 `0x41`, STM32 ACPI-EC `0x42`. Команды
  `scan`, `info`, `status`, `battery`, `cells`, `temp`, `ina226`, `sht21`,
  `sensors`, OTA STM32 (`flash`/`enter-update`/`reset`) и демон
  `oko-ec daemon` (интервал 2 с, `/tmp/oko-ec.json`, `/tmp/oko-ec.osd`,
  `/tmp/oko-ec.pid`). `oko-osd` берёт реальные `T`/`BAT` из свежего
  `/tmp/oko-ec.osd` (≤ 30 с) и иначе работает на симуляции. Датчики читаются
  через `i2c-dev` без драйверов ядра; INA226 — без калибровочного регистра
  (шунт 100 мОм по умолчанию), SHT21 — no-hold с CRC8. Опция `--json`/`--quiet`,
  адреса/шунт/тайминги переопределяются. Вендорен хостовый инструмент upstream
  `spectrespc/stm32_acpi-ec` на коммите
  `41728ddaf6936d49a5fb65676ca5203dd631b4f6` (файла `LICENSE` в upstream нет);
  `S21oko-ec` запускает демон при наличии `/dev/i2c-3` и `/oem/usr/bin/oko-ec`,
  opt-out — `/userdata/oko-ec.disable`.
- `project/app/oko-ec/`: the `/oem/usr/bin/oko-ec` utility for devices on
  `/dev/i2c-3` — SHT21 `0x40`, INA226 `0x41`, STM32 ACPI-EC `0x42`. Commands:
  `scan`, `info`, `status`, `battery`, `cells`, `temp`, `ina226`, `sht21`,
  `sensors`, STM32 OTA (`flash`/`enter-update`/`reset`) and the
  `oko-ec daemon` loop (2 s interval, `/tmp/oko-ec.json`, `/tmp/oko-ec.osd`,
  `/tmp/oko-ec.pid`). `oko-osd` uses real `T`/`BAT` from a fresh
  `/tmp/oko-ec.osd` (≤ 30 s) and otherwise keeps simulating them. Sensors are
  read through `i2c-dev` without kernel drivers; INA226 uses no calibration
  register (100 mOhm shunt by default), SHT21 is no-hold with CRC8.
  `--json`/`--quiet` are supported and addresses/shunt/timings are overridable.
  The upstream host tool `spectrespc/stm32_acpi-ec` is vendored at commit
  `41728ddaf6936d49a5fb65676ca5203dd631b4f6` (no `LICENSE` file in upstream);
  `S21oko-ec` starts the daemon when `/dev/i2c-3` and `/oem/usr/bin/oko-ec`
  exist, with `/userdata/oko-ec.disable` as opt-out.
- `oko-ec`: поддержка INA3221 (die ID `0x3220`, три канала, 13-битные значения
  `D15:D3`, LSB 40 мкВ/8 мВ) с автоопределением чипа по `0xFF` и выбором канала
  `--ina-channel 1..3` (`OKO_EC_INA_CHANNEL`); INA226 (`0x2260`) продолжает
  поддерживаться. OSD (reg1) выводит четыре строки: SHT21 (температура и
  влажность), STM32 (напряжение/ток/температура/SOC/ячейки), INA3221
  (напряжение/ток/мощность) и LTE/WiFi; при отсутствии источника — `--`, без
  свежих данных — прежняя симуляция `T`/`BAT`. На стенде на `0x41` определён
  INA3221 (силовые входы пока не подключены, показания 0).
- `oko-ec`: INA3221 support (die ID `0x3220`, three channels, 13-bit values in
  `D15:D3`, 40 uV/8 mV LSB) with chip auto-detection via register `0xFF` and
  channel selection `--ina-channel 1..3` (`OKO_EC_INA_CHANNEL`); INA226
  (`0x2260`) remains supported. The OSD region reg1 now shows four lines:
  SHT21 (temperature and humidity), STM32 (voltage/current/temperature/SOC/
  cells), INA3221 (voltage/current/power) and LTE/WiFi; a missing source is
  shown as `--`, and stale data falls back to the previous `T`/`BAT`
  simulation. The testbench device at `0x41` is an INA3221 (power inputs are
  not connected yet, so the readings are zero).

- Разметка SPI NAND: `boot` увеличен с 4 до 6 МБ, `oem` — с 30 до 40 МБ,
  `rootfs` уменьшен с 210 до 198 МБ
  (`256K(env),256K@256K(idblock),512K(uboot),6M(boot),40M(oem),10M(userdata),198M(rootfs)`,
  всего 255 МБ). После этой сборки нужна **полная прошивка** `update.img`:
  покомпонентная выкатка со старой таблицей несовместима, `/userdata` и данные
  на NAND теряются.
- SPI NAND layout: `boot` grows from 4 to 6 MiB, `oem` from 30 to 40 MiB and
  `rootfs` shrinks from 210 to 198 MiB
  (`256K(env),256K@256K(idblock),512K(uboot),6M(boot),40M(oem),10M(userdata),198M(rootfs)`,
  255 MiB total). This build requires a **full `update.img` flash**: component
  flashing with the old table is incompatible, and `/userdata` plus the data on
  NAND are lost.
- WebUI/API storage: `/api/storage` получил `mount`, `unmount` и `format`
  (FAT32/MBR, метка `OKO`, один раздел), поля `device`, `fs`,
  `format_supported`, `format_busy` и одноразовый `format_token`; формат
  требует `confirm=FORMAT`, блокируется при записи и сериализован с udev через
  `/run/oko-sd.lock`. Секция Storage получила `Mount`, `Unmount`, `Format card`
  и скрытый токен.
- WebUI/API storage: `/api/storage` gained `mount`, `unmount` and `format`
  (FAT32/MBR, label `OKO`, one partition), plus `device`, `fs`,
  `format_supported`, `format_busy` and a one-shot `format_token`; formatting
  requires `confirm=FORMAT`, is refused while recording and is serialized with
  udev through `/run/oko-sd.lock`. The Storage section gained `Mount`,
  `Unmount`, `Format card` and a hidden token field.
- `VERIFIED_FUNCTIONS.md`: реестр проверенных на стенде функций (дата, плата,
  стенд `192.168.52.97`, доказательства, ограничения) и правила его ведения;
  ссылки добавлены в `README_RU.md` и `STRUCTURE.md`. После прогона 2026-11-04
  обновлены статусы разметки, формата, hotplug, AIISP, RTSP и записи.
- `VERIFIED_FUNCTIONS.md`: a registry of functions verified on the bench (date,
  board, `192.168.52.97` test stand, evidence and limitations) with maintenance
  rules; linked from `README_RU.md` and `STRUCTURE.md`. The 2026-11-04 run
  updated the statuses for the NAND layout, format, hotplug, AIISP, RTSP and
  recording.
- Buildroot: `sysdrv/tools/board/buildroot/luckfox_pico_defconfig` включает
  `BR2_PACKAGE_UTIL_LINUX_BINARIES=y` и `BR2_PACKAGE_UTIL_LINUX_WIPEFS=y`,
  поэтому в rootfs появляются `sfdisk`, `wipefs`, `fdisk` и другие утилиты
  (нужны для форматирования microSD).
- Buildroot: `sysdrv/tools/board/buildroot/luckfox_pico_defconfig` enables
  `BR2_PACKAGE_UTIL_LINUX_BINARIES=y` and `BR2_PACKAGE_UTIL_LINUX_WIPEFS=y`, so
  the rootfs contains `sfdisk`, `wipefs`, `fdisk` and the rest of the basic
  util-linux set (needed to format the microSD card).
- WebUI: страница `/` переработана в восемь разделов — **Live, Image, Streams,
  Events, Storage, Network, OSD, System** — с боковым меню, липкой статус-строкой
  (IP, аптайм, load, температура, microSD), переключателем **EN/RU** (по
  умолчанию `en`, выбор в `localStorage`), тостами ошибок (`reason`/`hint`) и
  индикацией неприменённых изменений; описание — в
  `project/app/divinus/source/doc/WEBUI_REORG.md`.
- WebUI: the `/` page is reworked into eight sections — **Live, Image, Streams,
  Events, Storage, Network, OSD, System** — with a sidebar, a sticky status bar
  (IP, uptime, load, SoC temperature, microSD), an **EN/RU** switch (default
  `en`, choice kept in `localStorage`), error toasts built from `reason`/`hint`,
  and unsaved-change indicators; see
  `project/app/divinus/source/doc/WEBUI_REORG.md`.
- API: `/api/cmd` получил `reboot`, `export` (in-memory YAML как вложение) и
  `log` (хвост `/var/log/messages`, до 256 КиБ); добавлены `POST
  /api/cmd/import` (multipart, замена `divinus.yaml` с ротацией `.bak`),
  `POST /api/web-auth` (смена пароля WebUI с проверкой текущего),
  `/api/recordings` (+ `/file`, `/delete`), `/api/telemetry`; `/api/status`
  теперь отдаёт `ip`, `sd_free_mb`, `sd_total_mb`.
- API: `/api/cmd` gained `reboot`, `export` (in-memory YAML as an attachment) and
  `log` (tail of `/var/log/messages`, up to 256 KiB); added `POST
  /api/cmd/import` (multipart, replaces `divinus.yaml` and rotates `.bak`),
  `POST /api/web-auth` (WebUI password change with current-password check),
  `/api/recordings` (+ `/file`, `/delete`) and `/api/telemetry`; `/api/status`
  now returns `ip`, `sd_free_mb` and `sd_total_mb`.
- NTP: `/api/time` принимает `ntp=on|off` и `ntp_server`; ключи
  `ntp_enable`/`ntp_server` пишутся в секцию `system` YAML. `ntp.c` на старте
  учитывает их через 10 с (после `S49ntp`): при выключенном NTP останавливает
  демон, при заданном сервере поднимает `ntpd -c /tmp/divinus-ntp.conf`, при
  пустом сервере не трогает вендорский `/etc/init.d/S49ntp` (или поднимает его,
  если тот не стартовал). Проверено тремя перезагрузками стенда.
- NTP: `/api/time` accepts `ntp=on|off` and `ntp_server`; the `ntp_enable` /
  `ntp_server` keys are stored in the YAML `system` section. Ten seconds after
  startup (after `S49ntp`) `ntp.c` applies them: a disabled client stops the
  daemon, a configured server starts `ntpd -c /tmp/divinus-ntp.conf`, and an
  empty server leaves the vendor `/etc/init.d/S49ntp` daemon alone (or starts it
  if it did not come up). Verified with three bench reboots.
- Стенд: `bench/api_check.py` (матрица GET/POST и негативных проверок API),
  `bench/ui_check.py` (headless Chromium: разделы, EN/RU, dirty-state,
  отсутствие горизонтальной прокрутки на 1440 и 390 px, скриншоты в
  `output/bench/ui/`) и `bench/deploy_divinus.py` (выкатка собранного бинарника
  в `/oem` по SSH для быстрой итерации).
- Bench: `bench/api_check.py` (GET/POST and negative API matrix),
  `bench/ui_check.py` (headless Chromium: sections, EN/RU, dirty state, no
  horizontal scroll at 1440 and 390 px, screenshots in `output/bench/ui/`) and
  `bench/deploy_divinus.py` (installs the built binary into `/oem` over SSH for
  fast iteration).
- Документация OKO: `project/app/divinus/source/doc/WEBUI_REORG.md` (разбор
  «до/после», карта разделов, матрица функций ipcweb, backlog), обновлены
  `endpoints.md`, `INIT_ORDER.md` (порядок NTP и откат конфига),
  `TESTBENCH.md` (§1, §7B, §8), `STRUCTURE.md`, `bench/README.md`,
  `FEATURES.md`, `VERIFIED_FUNCTIONS.md`; `doc/webui.png` заменён, прежний
  экран сохранён как `doc/webui-before.png`.
- OKO documentation: `project/app/divinus/source/doc/WEBUI_REORG.md` (before and
  after, section map, ipcweb feature matrix, backlog), updated
  `endpoints.md`, `INIT_ORDER.md` (NTP ordering and config rollback),
  `TESTBENCH.md` (§1, §7B, §8), `STRUCTURE.md`, `bench/README.md`,
  `FEATURES.md`, `VERIFIED_FUNCTIONS.md`; `doc/webui.png` is replaced and the
  previous screen is kept as `doc/webui-before.png`.
- `divinus`: NPU-аналитика YOLOv5n. RKNN подключается через `dlopen`
  `librknnmrt.so` (`hal/plus/rk_rknn.h`, копия `rknn_api.h` 1.6.0), конверсия
  NV12→RGB888 — на CPU (`yolo_convert_nv12_rgb`): librga на этом образе
  непригодна, легаси `c_RkRgaBlit` падает с SIGSEGV, а im2d
  `importbuffer_fd` не импортирует ни MMZ-, ни dma-heap-дескрипторы; эта же
  RGA-операция на куче и вешала плату. Новые модули:
  `analytics/yolo.c` (тред захвата, кольцо пре-ролла, событие, отправитель),
  `analytics/yolo_postprocess.c` (декод трёх ветвей и NMS),
  `analytics/models.c` (каталог `/userdata/models`, атомарная загрузка,
  активация с откатом). Секция `[yolo]` (`enable`, `source`, `model`,
  `input_width/height`, `fps`, `conf_threshold`, `nms_threshold`, `classes`,
  `pre_seconds`, `post_seconds`, `max_frames`) добавлена в разбор и печать
  конфига; API — `GET/POST /api/yolo`, `GET /api/models`,
  `POST /api/models`, `/api/models/activate`, `/api/models/delete`; WebUI —
  карточки «Object detection (NPU)» и «Models» в разделе Events.
  Событие отправляется N последовательными `PUT` с `X-Event-Id`,
  `X-Frame-Index`, `X-Frame-Total`, `X-Frame-Ts`, `X-Event-Cause`, `X-Model`;
  `http_post.host` теперь принимает `host:port`. Гейт `yolo` в `gating.c`
  проверяет runtime, модель и занятость канала VPSS 3; RGA не используется
  (препроцессинг на CPU). `yolo.source` по умолчанию `vpss3`, при IVS/MJPEG
  санитайзер переводит на `vi0`.
  Замер M0 на стенде: `librknnmrt.so` — **1.6.0** (не 1.5.2), драйвер 0.9.2
  совместим, `/proc/rknpu/load` = 77% на YOLOv5n 640×640, ~9 кадр/с,
  `MemAvailable` −8.7 МБ; включение VI selfpath (chn1) в этой сборке
  привело к зависанию платы, поэтому `source: vi1` заблокирован.
  Проверено на стенде (`source: vpss3`, YOLOv5n 640×640, 3 fps): детекция
  идёт, NPU 28–33%, событие отдаёт 6 JPEG 640×640 с заголовками
  `X-Event-*`, модель из `/tmp`; `api_check.py` — 78/78, RTSP/OSD — 20
  кадров без регрессии; загрузка/активация/удаление модели и откат при
  `rknn_init` проверены. CPU-конверсия добавляет ~28% одного ядра на 3 fps
  при 640×640 (для целевых 640×384 дешевле). Загрузка модели 3 МиБ на стенде
  не проходит по свободному месту `/userdata` (~1.5 МиБ).
- `divinus`: YOLOv5n NPU analytics. The RKNN runtime is loaded with
  `dlopen("librknnmrt.so")` (`hal/plus/rk_rknn.h`, a copy of the 1.6.0
  `rknn_api.h`), and NV12→RGB888 conversion runs on the CPU
  (`yolo_convert_nv12_rgb`): librga is unusable on this image, the legacy
  `c_RkRgaBlit` segfaults and im2d `importbuffer_fd` imports neither MMZ nor
  dma-heap descriptors; the same RGA operation on heap memory also hung the
  board. New modules: `analytics/yolo.c` (capture thread,
  pre-roll ring, event assembly, sender), `analytics/yolo_postprocess.c`
  (three-branch decode and NMS), `analytics/models.c` (`/userdata/models`,
  atomic upload, activation with rollback). The `[yolo]` section (`enable`,
  `source`, `model`, `input_width/height`, `fps`, `conf_threshold`,
  `nms_threshold`, `classes`, `pre_seconds`, `post_seconds`, `max_frames`) is
  parsed and dumped; the API adds `GET/POST /api/yolo`, `GET /api/models`,
  `POST /api/models`, `/api/models/activate`, `/api/models/delete`; the WebUI
  adds the "Object detection (NPU)" and "Models" cards to Events. An event is
  sent as N `PUT` requests carrying `X-Event-Id`, `X-Frame-Index`,
  `X-Frame-Total`, `X-Frame-Ts`, `X-Event-Cause`, `X-Model`;
  `http_post.host` now accepts `host:port`. The `yolo` gate in `gating.c`
  checks the runtime, the model file and VPSS channel 3; RGA is not used
  (pre-processing runs on the CPU). `yolo.source` defaults to `vpss3`, and the
  sanitiser switches to `vi0` when IVS or MJPEG owns the channel. M0 bench measurements: `librknnmrt.so` is **1.6.0** (not
  1.5.2), driver 0.9.2 is compatible, `/proc/rknpu/load` is 77% on YOLOv5n
  640×640 at ~9 fps with `MemAvailable` down 8.7 MB; enabling VI selfpath
  (chn1) hung this board, so `source: vi1` is blocked.
  Verified on the bench (`source: vpss3`, YOLOv5n 640×640, 3 fps): detection
  runs, NPU 28–33%, an event delivers 6 JPEGs at 640×640 with the
  `X-Event-*` headers from a model in `/tmp`; `api_check.py` is 78/78 and
  RTSP/OSD captured 20 frames without regressions; model upload, activation,
  deletion and the `rknn_init` rollback were exercised. The CPU conversion
  costs about 28% of one core at 3 fps for 640×640 (cheaper at the target
  640×384). A 3 MiB model upload does not fit the ~1.5 MiB free in
  `/userdata` on this bench.


### Изменено

- `FEATURES.md` разделён на три файла: `FEATURES.md` — паспорт платформы,
  разделение по статусу проверки, общие примечания и команды; новый
  `FEATURES_VERIFIED.md` — только подтверждённые функции и возможности со
  статусом «работает»; новый `FEATURES_UNVERIFIED.md` — ограниченные и
  заблокированные возможности, непроверенные комбинации потоков, датчиков и
  microSD. Ссылки обновлены в `AGENTS.md`, `STRUCTURE.md`, `README_RU.md`,
  `KNOWN_ISSUES.md` и `TO_DO.md`.
- `FEATURES.md` is split into three files: `FEATURES.md` now holds the platform
  passport, the split by verification status, shared notes and commands; the new
  `FEATURES_VERIFIED.md` lists only confirmed features with the **works** status;
  the new `FEATURES_UNVERIFIED.md` lists limited and blocked capabilities and
  unverified stream, sensor and microSD combinations. Links were updated in
  `AGENTS.md`, `STRUCTURE.md`, `README_RU.md`, `KNOWN_ISSUES.md` and `TO_DO.md`.
- `FEATURES.md` переработан: вместо разнородных списков и прозы — короткий
  паспорт платформы и одна сводная таблица функционала с колонками «Группа»,
  «Функционал», «Описание», «Управление (API, секция YAML)», «Что добавлено
  (OKO vs вендорский V1.4)» и «Проверено и работает». English-раздел сжат до
  зеркальной таблицы `Feature | What was added | Verified & works`; ограничения
  привязаны к строкам таблицы или вынесены в «Примечания». Строка `FEATURES.md`
  в карте документации `AGENTS.md` приведена к новой структуре.
- `FEATURES.md` reworked: the mixed lists and prose are replaced by a short
  platform passport and one consolidated feature table with `Group`, `Feature`,
  `Description`, `Control (API, YAML section)`, `What was added (OKO vs vendor
  V1.4)` and `Verified & works` columns. The English section is reduced to a
  mirrored `Feature | What was added | Verified & works` table; limitations are
  attached to table rows or collected under "Notes". The `FEATURES.md` row in
  the `AGENTS.md` documentation map now matches the new structure.
- `project/app/divinus/source/src/record.c`: запись разрешена только ниже
  реально смонтированного `/mnt/sdcard`; `/oem`, `/userdata`, `/tmp` и `/`
  отвергаются. Добавлен `storageBusy`: во время формата/размонтирования новый
  сегмент не открывается. Раз в секунду и перед ротацией проверяется
  монтирование: при извлечении карты файл закрывается, запись останавливается,
  NAND не используется. `start=1` при отказе возвращает `blocked` с причиной.
- `project/app/divinus/source/src/record.c`: recording is allowed only below a
  really mounted `/mnt/sdcard`; `/oem`, `/userdata`, `/tmp` and `/` are refused.
  `storageBusy` prevents a new segment from opening during format/unmount. The
  mount is checked once per second and before rotation: if the card disappears,
  the file is closed, recording stops and SPI NAND is never used. A failed
  `start=1` now returns `blocked` with a reason.
- SC230AI DTS: новый `rv1106g-luckfox-pico-pro-max-sc230ai.dts` (копия Pro Max
  DTS) выключает неиспользуемые `&sc3336` и `&mis5001`; активный BoardConfig
  использует этот файл. Общий вендорский dtsi и DTS других плат не изменены.
- SC230AI DTS: the new `rv1106g-luckfox-pico-pro-max-sc230ai.dts` (a copy of the
  Pro Max DTS) disables the unused `&sc3336` and `&mis5001`; the active board
  config points at this file. The shared vendor dtsi and other board DTS files
  are unchanged.
- `rv1106-sc230ai.config`: явно выключены `CONFIG_VIDEO_SC3336`,
  `CONFIG_VIDEO_IMX415`, `CONFIG_VIDEO_MIS5001`, `CONFIG_VIDEO_MIA1321`;
  собирается только `CONFIG_VIDEO_SC230AI=m`.
- `rv1106-sc230ai.config`: `CONFIG_VIDEO_SC3336`, `CONFIG_VIDEO_IMX415`,
  `CONFIG_VIDEO_MIS5001` and `CONFIG_VIDEO_MIA1321` are explicitly disabled;
  only `CONFIG_VIDEO_SC230AI=m` is built.
- `project/app/divinus/source/src/region.c`: буфер локального `@file`-провайдера
  текста OSD увеличен с 80 до 256 байт (и `region_file_prev`), чтобы строка
  телеметрии могла содержать SHT21/STM32/INA3221/LTE/WiFi; поведение для
  коротких строк не меняется.
- `project/app/divinus/source/src/region.c`: the local OSD `@file` text provider
  buffer is increased from 80 to 256 bytes (including `region_file_prev`), so the
  telemetry line can carry SHT21/STM32/INA3221/LTE/WiFi; short strings behave as
  before.
- `sc230ai`: режим 1920×1080 переведён с 25 на **30 fps** (`VTS = 0x0465`,
  HTS остаётся 2200, MIPI link 371.25 Мбит/с не менялся). Проверено на DUT:
  `v4l2-ctl` даёт стабильные **30.01–30.02 fps** на 300 кадрах, ошибок
  CSI/MIPI нет. Модуль собран отдельно и загружен через
  `/oem/usr/ko/sc230ai.ko` без полной перепрошивки.
- `sc230ai`: в проект перенесена запись internal sync master/slave регистров
  при старте и остановке потока (из upstream 6.1 / X5): `0x300a`, `0x3032`,
  `0x3222`, `0x3224`, `0x3614`. При `NO_SYNC` поведение не меняется; проверены
  загрузка модуля, 30 fps и OSD.
- `sc230ai`: ported internal sync master/slave register writes at stream
  start/stop from upstream 6.1 / X5: `0x300a`, `0x3032`, `0x3222`, `0x3224`,
  `0x3614`. With `NO_SYNC` behavior is unchanged; module load, 30 fps and OSD
  were verified.
- `sc230ai`: добавлен экспериментальный **HDR_X2** (SHDR/DCG, 2 lane,
  742.5 Мбит/с, 1920×1080@30): таблица из SigmaStar, link frequency
  371.25 МГц, VTS 2250, запись long/short exposure и gain в `set_hdrae()`.
  По умолчанию не включается (`rockchip,camera-hdr-mode = <1>`); HDR IQ для
  SC230AI на RV1106 не проверялся. Linear 30 fps работает без регрессий.
- `sc230ai`: added experimental **HDR_X2** (SHDR/DCG, 2 lanes,
  742.5 Mbps, 1920×1080@30): SigmaStar register table, 371.25 MHz link
  frequency, VTS 2250, and long/short exposure + gain writes in
  `set_hdrae()`. Disabled by default (`rockchip,camera-hdr-mode = <1>`);
  SC230AI HDR IQ on RV1106 has not been validated. Linear 30 fps is unaffected.
- `divinus`: основной поток (`mp4`) и `mjpeg` переведены на **30 fps**
  (`fps: 30`), битрейт `mp4` остаётся 4096 кбит/с. На DUT измерено
  **30.08 fps** за 20 с и ≈4.73 Мбит/с; OSD работает.
- `sc230ai`: the 1920×1080 mode is switched from 25 to **30 fps**
  (`VTS = 0x0465`, HTS stays 2200, MIPI link 371.25 Mbps unchanged). Verified
  on the DUT: `v4l2-ctl` reports stable **30.01–30.02 fps** over 300 frames with
  no CSI/MIPI errors. The module was built separately and loaded through
  `/oem/usr/ko/sc230ai.ko` without a full reflash.
- `divinus`: the main stream (`mp4`) and `mjpeg` are switched to **30 fps**
  (`fps: 30`); the `mp4` bitrate remains 4096 kbps. Measured on the DUT:
  **30.08 fps** over 20 s and ≈4.73 Mbps; OSD works.
- OSD: добавлен чёрный контур текста — `regN_outl: 0x8000`,
  `regN_thick: 1.0` для всех регионов. Конфиг применён на DUT и
  сохранён в `/userdata/divinus.yaml`; контур проверен по кадрам RTSP.
- OSD: added a black text outline — `regN_outl: 0x8000`, `regN_thick: 1.0`
  for all regions. Applied on the DUT and persisted in
  `/userdata/divinus.yaml`; verified from RTSP frames.
- OSD: добавлен `reg4` в левом нижнем углу — реальные загрузка CPU,
  процент свободной памяти (`MemAvailable/MemTotal`) и температура
  `soc-thermal`; файл `/tmp/osd-sys.txt`, обновление раз в 5 с.
- OSD: added `reg4` in the bottom-left corner — real CPU load, free memory
  percentage (`MemAvailable/MemTotal`) and `soc-thermal` temperature; source
  `/tmp/osd-sys.txt`, updated every 5 s.
- `divinus`: битрейт основного RTSP-потока (`mp4.bitrate`, H.265 CBR)
  увеличен вдвое — `2048` → `4096` кбит/с; измеренный поток вырос
  с ≈2.28 до ≈4.5–4.9 Мбит/с. На DUT значение применено и сохранено в
  `/userdata/divinus.yaml`.
- `divinus`: main RTSP stream bitrate (`mp4.bitrate`, H.265 CBR) doubled —
  `2048` → `4096` kbps; the measured stream grew from ≈2.28 to ≈4.5–4.9 Mbps.
  Applied on the DUT and persisted in `/userdata/divinus.yaml`.
- OSD: размер шрифта всех регионов — `24.0` px; время перенесено в правый
  нижний угол, GPS — в правый верхний, телеметрия `T/BAT/LTE/WiFi` разбита на
  четыре строки в левом верхнем углу, сообщения — под ней в `reg3`. Цвет —
  ярко-зелёный `regN_color: 0x07e0` (RGB565). `S20oko-osd` мигрирует старый
  `/userdata/divinus.yaml` (без `reg0_size`/`reg0_color`/`reg3_text`) с
  сохранением первого бэкапа `/userdata/divinus.yaml.bak`.
- OSD: font size for all regions is `24.0` px; time moved to bottom-right, GPS
  to top-right, `T/BAT/LTE/WiFi` split into four lines in the top-left, and
  messages placed below them in `reg3`. Color is bright green
  `regN_color: 0x07e0` (RGB565). `S20oko-osd` migrates an old
  `/userdata/divinus.yaml` (without `reg0_size`/`reg0_color`/`reg3_text`) and
  keeps the first backup `/userdata/divinus.yaml.bak`.
- `divinus` RGN (`rk_hal.c`): в конверсии BGRA5551 выставляются оба бита
  альфы (бит 0 и бит 15); сборки librockit для RV1106 различаются, без этого
  не белый цвет OSD (зелёный/красный) рендерится полностью прозрачным.
- `divinus` RGN (`rk_hal.c`): the BGRA5551 conversion now sets both alpha bits
  (bit 0 and bit 15); RV1106 librockit builds differ, and otherwise a non-white
  OSD color (green/red) is rendered fully transparent.
- Активная конфигурация переключена на
  `project/cfg/BoardConfig_IPC/BoardConfig-SPI_NAND-Buildroot-RV1106_Luckfox_Pico_Pro_Max-SC230AI.mk`:
  SPI NAND, Buildroot, SC230AI, фрагмент ядра только `rv1106-sc230ai.config`.
  Сборка, прошивка и проверки выполняются в автономном режиме, без TFTP/NFS.
- `Dockerfile`, `docker-entrypoint.sh`, `docker-compose.yml`, `start.sh`: убраны
  TFTP-сервер, пакеты `tftpd-hpa`/`atftp` и соответствующие сообщения; контейнер
  больше не запускает сетевых сервисов.
- `AGENTS.md`, `README_RU.md`, `TESTBENCH.md`, `INIT_ORDER.md`, `STRUCTURE.md`,
  `bench/README.md`, `.kilo/command/bench.md`, `.kilo/command/docs-check.md`,
  `.kilo/skills/oko-docs/SKILL.md`: netboot удалён из описания стенда, прошивки,
  диагностики и утилит; основной режим — SPI NAND.
- После перехода на автономную загрузку DUT получает адрес по DHCP; фактический
  адрес на `2026-11-01` — `192.168.52.65`. `TESTBENCH.md` §5 и примеры в
  `README_RU.md` / `bench/README.md` / `/bench` обновлены; `192.168.52.150`
  остаётся резервным адресом для DHCP-резервирования.
- Active configuration switched to
  `project/cfg/BoardConfig_IPC/BoardConfig-SPI_NAND-Buildroot-RV1106_Luckfox_Pico_Pro_Max-SC230AI.mk`:
  SPI NAND, Buildroot, SC230AI, kernel fragment `rv1106-sc230ai.config` only.
  Build, flashing and checks run standalone without TFTP/NFS.
- After the switch to standalone boot the DUT obtains its address via DHCP; the
  actual address on `2026-11-01` is `192.168.52.65`. `TESTBENCH.md` §5 and the
  examples in `README_RU.md` / `bench/README.md` / `/bench` were updated;
  `192.168.52.150` remains a reserved address for a DHCP reservation.
- `Dockerfile`, `docker-entrypoint.sh`, `docker-compose.yml`, `start.sh`: removed
  the TFTP server, the `tftpd-hpa`/`atftp` packages and related messages; the
  container no longer starts network services.
- `AGENTS.md`, `README_RU.md`, `TESTBENCH.md`, `INIT_ORDER.md`, `STRUCTURE.md`,
  `bench/README.md`, `.kilo/command/bench.md`, `.kilo/command/docs-check.md`,
  `.kilo/skills/oko-docs/SKILL.md`: netboot removed from the bench, flashing,
  diagnostics and tooling docs; SPI NAND is the primary mode.
- Поведение запуска стримера: `divinus` стартует по умолчанию — маркер
  `/userdata/divinus.enable` создаёт `S20oko-osd`; штатный `rkipc` запускается
  только при opt-out `/userdata/rkipc.enable`. Логика `RkLunch.sh` не менялась.
- `S20oko-osd` перезаписывает `/userdata/divinus.yaml` фабричным, если в нём нет
  `reg0_text`/`reg0_size`/`reg0_color` (старый или ручной конфиг); первый прежний
  файл сохраняется как `divinus.yaml.bak`, ручные правки такого файла не
  переживают перезагрузку.
- `INIT_ORDER.md`, `TESTBENCH.md`, `STRUCTURE.md`, `README_RU.md`: divinus по
  умолчанию, строка `S20oko-osd` в порядке init, OSD-чек-лист и файлы оверлея.
- Streamer startup: `divinus` now starts by default — `S20oko-osd` creates
  `/userdata/divinus.enable`; the stock `rkipc` runs only when the
  `/userdata/rkipc.enable` opt-out marker exists. `RkLunch.sh` itself is
  unchanged.
- `S20oko-osd` replaces `/userdata/divinus.yaml` with the factory file when it
  lacks `reg0_text` (old or hand-edited config); such a file does not survive a
  reboot.
- `INIT_ORDER.md`, `TESTBENCH.md`, `STRUCTURE.md`, `README_RU.md`: default
  divinus startup, the `S20oko-osd` init step, the OSD checklist and the new
  overlay files.
- `STRUCTURE.md`: добавлен пакет `project/app/divinus/` (вендорские исходники,
  фабричный конфиг, Makefile) и уточнён состав `project/app/`.
- `INIT_ORDER.md`: ветка `divinus.enable` в `post_chk` и зависимости `divinus`
  (общий rockit/ISP-конвейер с `rkipc`, `dlopen` вендорных библиотек,
  патч ABI `mmap` в `compat.c`).
- `TESTBENCH.md`: чек-лист divinus (`/api/status`, `/image.jpg`, RTSP-корень),
  диагностика сбоев и особенности портов/конфига.
- `STRUCTURE.md`: added the `project/app/divinus/` package (vendored sources,
  factory config, Makefile) and clarified the `project/app/` contents.
- `INIT_ORDER.md`: documented the `divinus.enable` branch in `post_chk` and the
  `divinus` dependencies (shared rockit/ISP pipeline with `rkipc`, `dlopen` of
  vendor libraries, the `compat.c` `mmap` ABI patch).
- `TESTBENCH.md`: added the divinus checklist (`/api/status`, `/image.jpg`, RTSP
  root) plus failure diagnostics and port/config notes.
- `INIT_ORDER.md`: источник `oem`/`userdata` при netboot, ветка `S20linkmount`,
  строка `S99oko-test` в порядке init-скриптов, поведение сети при netboot.
- `TESTBENCH.md`: `.nfsroot` внутри экспорта `/srv/nfs/rootfs`, деплой rsync с
  бэкапом, проверки маркера и NFS-монтирований, статический IP в netboot.
- `INIT_ORDER.md`: зафиксировано условие, что сборка прошивки OKO выполняется в
  Docker-контейнере `luckfox_oko_build_env` (`docker compose build` / `up -d`,
  запуск через `start.sh`); в разделе источников сборки добавлена строка про
  Docker-окружение. Сборка без Docker остаётся вендорским fallback в `README_RU.md`.
- `INIT_ORDER.md`: recorded that the OKO firmware build runs in the
  `luckfox_oko_build_env` Docker container (`docker compose build` / `up -d`, started via
  `start.sh`), and added a Docker environment row to the build sources table. Building
  without Docker remains a vendor fallback in `README_RU.md`.
- Уточнены правила делегирования субагентам: промпт самодостаточный, с выжимкой фактов,
  без путей и номеров разделов документации и без имён skills; зафиксирована область
  применения `AGENTS.md` (исполняет только агент `code`, правила дополняют базовые
  инструкции) и привязка команд `/bench`, `/docs-check` к `code`.
- Refined sub-agent delegation rules: the prompt is self-contained with distilled facts,
  without documentation paths, section numbers, or skill names; recorded that `AGENTS.md`
  applies only to the `code` agent and supplements the base instructions, and that
  `/bench` and `/docs-check` are bound to `code`.
- `STRUCTURE.md`: описание `.kilo/` приведено к фактическому составу (команды, skill,
  планы), в список корневых файлов добавлены документы OKO и `AGENTS.md`;
  `.kilo/.gitignore` больше не игнорирует сам себя.
- `STRUCTURE.md`: the `.kilo/` description now matches the actual layout (commands, skill,
  plans), OKO documents and `AGENTS.md` were added to the root files list, and
  `.kilo/.gitignore` no longer ignores itself.
- `TESTBENCH.md`: новый режим «C. Покомпонентная прошивка изменённых разделов» —
  маппинг исходников на разделы SPI NAND (`oem`, `rootfs`, `userdata`, `boot`),
  команды `./rkflash.sh oem|rootfs|userdata|rd` из Maskrom, правило «полный
  `update.img` только при первичной прошивке или смене таблицы разделов, loader,
  U-Boot env», сверка хешей образов с `.flashed.sha256` и альтернатива сырыми
  `mtd erase/write` из `sd_update.txt`.
- `README_RU.md`: в быстром старте прошивка полным образом — `./rkflash.sh update`
  (без аргумента скрипт ничего не прошивает), добавлена ссылка на покомпонентный
  режим; команда `/bench` дополнена режимами прошивки и `divinus` в чек-листе.
- `TESTBENCH.md`: new "C. Per-partition flashing of changed sections" mode — a
  source-to-SPI-NAND-partition map (`oem`, `rootfs`, `userdata`, `boot`), the
  `./rkflash.sh oem|rootfs|userdata|rd` commands from Maskrom, the rule that a full
  `update.img` is only for the first flash or a partition table, loader, or U-Boot
  env change, image hash comparison against `.flashed.sha256`, and the raw
  `mtd erase/write` alternative from `sd_update.txt`.
- `README_RU.md`: the quick start now flashes the full image with
  `./rkflash.sh update` (the script flashes nothing without an argument) and links
  to the per-partition mode; the `/bench` command now lists the flashing modes and
  `divinus` in its checklist.
- `TESTBENCH.md`: зафиксировано покрытие шрифта `simsun_en` — ASCII `0x21–0x7e`
  и несколько CJK-иероглифов, кириллицы нет; текст OSD пишется латиницей.
- `TESTBENCH.md`: documented the `simsun_en` font coverage — ASCII `0x21–0x7e`
  plus a few CJK characters, no Cyrillic; OSD text must be Latin.
- Логи netboot и OSD-проверок перенесены из `/tmp/kilo` в `output/bench/`
  (`netboot-<ts>.log`, `osd-<ts>-*.png`); `netboot-catch.py` читает
  `OKO_NETBOOT_LOG` и создаёт каталог сам. Обновлены `TESTBENCH.md` (§4, §7B,
  §8, §10), `README_RU.md`, `STRUCTURE.md` (папка `bench/`, `output/bench/`),
  команда `/bench` и правило в `AGENTS.md` §6.
- Netboot and OSD-check artifacts moved from `/tmp/kilo` to `output/bench/`
  (`netboot-<ts>.log`, `osd-<ts>-*.png`); `netboot-catch.py` honours
  `OKO_NETBOOT_LOG` and creates the directory itself. Updated `TESTBENCH.md`
  (§4, §7B, §8, §10), `README_RU.md`, `STRUCTURE.md` (`bench/`, `output/bench/`),
  the `/bench` command and the rule in `AGENTS.md` §6.
- WebUI: запись настроек переведена на `POST application/x-www-form-urlencoded`
  (сервер подставляет тело в `req->query`, обработчики не менялись); `GET`
  оставлен для совместимости и помечен устаревшим в `endpoints.md`. Кнопка
  `Apply` активна только при отличиях от последнего прочитанного состояния,
  ответ сервера разбирается вместо безусловного «success».
- WebUI: writes now use `POST application/x-www-form-urlencoded` (the server
  maps the body to `req->query`, so the handlers are unchanged); `GET` is kept
  for compatibility and marked deprecated in `endpoints.md`. `Apply` is enabled
  only when a field differs from the last read state, and the server response is
  parsed instead of assuming success.
- `/api/osd/{id}`: `color` и `outl` отдаются как `#RRGGBB` с ведущими нулями,
  `text`/`img`/`font` экранируются в JSON; пустые `text`/`img` очищают значение;
  PNG-загрузка прописывает `img=/tmp/osd{id}.png`, поэтому картинка реально
  отображается.
- `/api/osd/{id}`: `color` and `outl` are returned as zero-padded `#RRGGBB`, and
  `text`/`img`/`font` are JSON-escaped; empty `text`/`img` clear the value; a PNG
  upload sets `img=/tmp/osd{id}.png`, so the image is actually displayed.
- `divinus.yaml` (фабричный и рабочий): в `system` добавлены `ntp_enable` и
  `ntp_server`; существующие файлы без этих ключей читаются со значениями по
  умолчанию.
- `divinus.yaml` (factory and working copies): `system` gains `ntp_enable` and
  `ntp_server`; existing files without the keys parse with defaults.
- `unescape_uri()` вызывается после разделения `key=value`, поэтому `%3D` в
  значении параметра больше не влияет на разбор, а `NULL` для параметра без
  значения безопасен.
- `unescape_uri()` now runs after the `key=value` split, so an encoded `%3D` in
  a value no longer affects parsing, and `NULL` for a parameter without a value
  is safe.
- `divinus`: OSD на RV1106 обновляется через canvas
  (`RK_MPI_RGN_GetCanvasInfo`/`RK_MPI_RGN_UpdateCanvas`): растр пишется прямо
  в память региона, без `calloc` и копии на каждый кадр. Если символы
  отсутствуют, `GetCanvasInfo` вернул ошибку или размер/stride canvas не
  совпал, остаётся прежний путь `RK_MPI_RGN_SetBitMap` с однократным
  предупреждением на handle. Позиция и `fgAlpha` меняются через
  `RK_MPI_RGN_SetDisplayAttr` вместо detach/attach, поэтому `opal` из
  `/api/osd/<id>` применяется и при неизменной позиции. Упаковка
  `ARGB1555 → BGRA5551` вынесена в `hal/plus/rk_rgn_pack.c`, добавлен хостовый
  тест `tests/rk_rgn_pack_test.c`, `tests/bmp_align_test.c` переведён на общий
  helper. `u32CanvasNum` остаётся равным 1.
- `divinus`: the RV1106 OSD is updated through the canvas interface
  (`RK_MPI_RGN_GetCanvasInfo`/`RK_MPI_RGN_UpdateCanvas`) — the raster is
  written directly into the region memory, without a `calloc` and a copy per
  frame. If the symbols are missing, `GetCanvasInfo` fails or the canvas
  size/stride does not match, the previous `RK_MPI_RGN_SetBitMap` path is used
  with a single warning per handle. Position and `fgAlpha` are changed with
  `RK_MPI_RGN_SetDisplayAttr` instead of detach/attach, so `opal` from
  `/api/osd/<id>` is applied even when the position is unchanged. The
  `ARGB1555 → BGRA5551` packing moved to `hal/plus/rk_rgn_pack.c`, a host test
  `tests/rk_rgn_pack_test.c` was added, and `tests/bmp_align_test.c` now uses
  the shared helper. `u32CanvasNum` stays at 1.

- Разметка SPI NAND: `userdata` расширен с 10 до 40 МБ за счёт `rootfs`
  (198→168 МБ), сумма `userdata`+`rootfs` сохранена (208 МБ). После прошивки
  полным `update.img` том `ubi5_0` — 29.1 МиБ (свободно ~28 МиБ), загрузка
  модели 3 МиБ через `/api/models` проверена; `rootfs` занимает ~54 МиБ из
  168 МБ. Обновлены `BoardConfig`, `TESTBENCH.md` (LBA/размеры),
  `INIT_ORDER.md` и `FEATURES.md`. Если `reboot loader` не переводит плату в
  loader-режим, U-Boot на UART останавливается `<Ctrl+C>` и команда
  `download` уводит её в `Maskrom`.
- SPI NAND layout: `userdata` grew from 10 to 40 MB by shrinking `rootfs`
  (198→168 MB); the combined size stays 208 MB. After flashing the full
  `update.img` the `ubi5_0` volume is 29.1 MiB (~28 MiB free), and a 3 MiB
  model upload through `/api/models` was verified; `rootfs` uses ~54 MiB of
  its 168 MB. `BoardConfig`, `TESTBENCH.md` (LBA/sizes), `INIT_ORDER.md` and
  `FEATURES.md` were updated. When `reboot loader` does not enter loader mode,
  stopping U-Boot on UART with `<Ctrl+C>` and issuing `download` switches the
  board to `Maskrom`.

- `bench/ui_check.py`: добавлена браузерная проверка раздела Events — список
  источников YOLO не содержит `vi1`, модель загружается через карточку
  Models, ошибка `rknn_init` показывается в тосте, удаление убирает строку.
  Прогон выполнен в headless Chromium (без root `libasound.so.2` распакован
  из `libasound2t64`), итог — `UI check passed`, скриншоты в
  `output/bench/ui/`; способ запуска описан в `bench/README.md`.
- `bench/ui_check.py`: added a browser check of the Events section — the YOLO
  source list has no `vi1`, a model is uploaded from the Models card, the
  `rknn_init` error appears in a toast, and deleting the row removes it. The
  run used headless Chromium (without root, `libasound.so.2` was unpacked from
  `libasound2t64`) and ended with `UI check passed`; screenshots are in
  `output/bench/ui/`, and the command is documented in `bench/README.md`.

- Зафиксирована проверка AIISP + YOLO на `vpss3` (демо-модель 640×640,
  3 fps): AIISP on/off не роняет детекцию, main 30.2–32.8 fps, NPU 28%,
  `librkpostisp.so` загружен, пропусков в `/live/0` не видно; результаты — в
  `VERIFIED_FUNCTIONS.md`. Ночная точность и CMA не измерялись (нет
  цели/разметки), целевая модель 640×384 ещё не собрана.
- Recorded the AIISP + YOLO check on `vpss3` (640×640 demo model, 3 fps):
  toggling AIISP does not break detection, main runs at 30.2–32.8 fps with 28%
  NPU, `librkpostisp.so` is loaded and `/live/0` shows no gaps; results are in
  `VERIFIED_FUNCTIONS.md`. Night accuracy and CMA were not measured (no
  target/labels), and the target 640×384 model is not built yet.

- `TO_DO.md` §11 сокращён до незакрытых задач: удалены реализованные фазы
  NPU-аналитики, исходное состояние, принятые решения и статус; остались модель
  640×384 с точностью, ночная точность/CMA, лицензия YOLOv5 и вопрос
  `.aiisp`-моделей через WebUI.
- `TO_DO.md` §11 is reduced to open work: the implemented NPU phases, initial
  state, decisions and status were removed; it now lists only the 640×384 model
  plus accuracy, night accuracy/CMA, the YOLOv5 licence and the `.aiisp` model
  question for the WebUI.

### Удалено

- Из oem активной платы удалены `sample_*` и `simple_*` (включая прежние
  стресс/AVS-демо); диагностические `rk_*_test`/`rk_mpi_*_test`, `rkipc`,
  `rkaiq_*`, `divinus` и `oko-*` сохранены.
- `sample_*` and `simple_*` (including the old stress/AVS demos) are removed
  from the active board oem; diagnostic `rk_*_test`/`rk_mpi_*_test`, `rkipc`,
  `rkaiq_*`, `divinus` and `oko-*` are kept.
- Из oem удалены сенсорные модули и IQ других плат (`sc3336`, `mis5001`,
  `imx415`, `mia1321` и другие сенсорные `.ko`); остаётся только `sc230ai.ko`
  и `sc230ai_CMK-OT2115-PC1_30IRC-F16.json`. Вендорские исходники и DTS других
  плат не тронуты.
- Sensor modules and IQ files of the other boards (`sc3336`, `mis5001`,
  `imx415`, `mia1321` and other sensor `.ko` files) are removed from oem; only
  `sc230ai.ko` and `sc230ai_CMK-OT2115-PC1_30IRC-F16.json` remain. Vendor
  sources and DTS files of the other boards are untouched.
- Netboot-режим: файлы `BoardConfig-SPI_NAND-Buildroot-RV1106_Luckfox_Pico_Pro_Max-NETBOOT.mk`,
  `BoardConfig-SPI_NAND-Buildroot-RV1106_Luckfox_Pico_Pro_Max-NETBOOT-SC230AI.mk`,
  фрагмент ядра `sysdrv/source/kernel/arch/arm/configs/rv1106-netboot.config`,
  утилиты `bench/netboot.py` и `netboot-catch.py`.
- NFS-root в `S20linkmount` (`project/build.sh`): ветка монтирования
  `oem`/`userdata` с хоста и сообщения `netboot: …`; разделы всегда берутся из
  SPI NAND (ubifs).
- Netboot-ветка в `RkLunch.sh`: статическая сеть из `ip=` больше не
  поддерживается, `network_init` всегда настраивает MAC и запускает DHCP.
- Netboot mode: the
  `BoardConfig-SPI_NAND-Buildroot-RV1106_Luckfox_Pico_Pro_Max-NETBOOT.mk` and
  `BoardConfig-SPI_NAND-Buildroot-RV1106_Luckfox_Pico_Pro_Max-NETBOOT-SC230AI.mk`
  files, the `sysdrv/source/kernel/arch/arm/configs/rv1106-netboot.config`
  kernel fragment, and the `bench/netboot.py` / `netboot-catch.py` tools.
- NFS root in `S20linkmount` (`project/build.sh`): the host-side
  `oem`/`userdata` mount branch and `netboot: …` messages; partitions always come
  from SPI NAND (ubifs).
- Netboot branch in `RkLunch.sh`: static networking from `ip=` is no longer
  supported; `network_init` always configures the MAC and starts DHCP.
- `divinus`: приведены к вендорскому ABI структуры HAL, которые передаются в
  librockit: `VIDEO_FRAME_S` (`pVirAddr` — 2 элемента, а не 3) и хвост
  `VI_CHN_ATTR_S` (`stShareBufChn` — три `int`, а не один). Прежние размеры
  приводили к чтению за границей структуры при `RK_MPI_VI_SetChnAttr` и
  сдвигу `u64PTS`. Проверено `_Static_assert` по заголовкам SDK для
  VI/VPSS/VENC/VIDEO_FRAME.
- `divinus`: HAL structures passed to librockit now match the vendor ABI:
  `VIDEO_FRAME_S` (`pVirAddr` holds two elements, not three) and the
  `VI_CHN_ATTR_S` tail (`stShareBufChn` is three `int`s, not one). The old
  sizes made `RK_MPI_VI_SetChnAttr` read past the object and shifted
  `u64PTS`. Verified with `_Static_assert` against the SDK headers for
  VI/VPSS/VENC/VIDEO_FRAME.


### Исправлено

- `divinus`: `yolo.source=vi1` закрыт на всех уровнях — `gating.c` блокирует
  его с причиной, `GET`/`POST /api/yolo?source=vi1` и неизвестные значения
  возвращают HTTP 400 `{"error":"invalid"}`, из WebUI опция удалена, а
  стартовый санитайзер переводит сохранённый `vi1` в `vpss3`. В zero-copy
  входе учитывается `w_stride` (строки пишутся с шагом тензора), при
  активации проверяются 4D-NHWC выходы и одинаковая размерность каналов всех
  ветвей, атрибуты входа/выходов пишутся в лог. Хостовый
  `yolo_postprocess_test` покрывает 80 классов (C = 255, `C % 16 != 0`) и
  проверяет, что каналы NHWC не выравниваются до 16; `bench/api_check.py`
  проверяет отказ для `vi1` и неизвестного значения.
- `divinus`: `yolo.source=vi1` is closed at every level — `gating.c` blocks it
  with a reason, `GET`/`POST /api/yolo?source=vi1` and unknown values return
  HTTP 400 `{"error":"invalid"}`, the WebUI option is removed, and the startup
  sanitiser turns a saved `vi1` into `vpss3`. The zero-copy input honours
  `w_stride` (rows are written at the tensor stride), activation validates
  4D-NHWC outputs and identical channel counts in all branches, and the input
  and output attributes are logged. The host `yolo_postprocess_test` covers 80
  classes (C = 255, `C % 16 != 0`) and verifies that NHWC channels are not
  padded to 16; `bench/api_check.py` checks the rejection of `vi1` and of an
  unknown value.
- DHCP: причина смены IP — MAC хранился в `/data/ethaddr.txt` (раздел
  `/userdata`), а `dhcpcd` использовал DUID из `/var/db/dhcpcd`; оба
  сбрасываются при полной прошивке. Дополнительно на `eth0` работали два
  клиента (`S41dhcpcd` и `udhcpc` из `RkLunch.sh`). Теперь MAC выводится из
  `/proc/device-tree/serial-number` и хранится в env (`oko_mac`), DHCP-клиент
  один — `dhcpcd` с client ID по MAC (`clientid`, без `duid`),
  `S41dhcpcd` заменён заглушкой, а `RkLunch.sh` вызывает `oko-net apply`.
  Проверено: после reboot MAC `02:47:8d:05:67:56` и IP `192.168.52.97`
  сохраняются; static `.220` также переживает reboot.
- DHCP: the changing IP was caused by the MAC living in `/data/ethaddr.txt`
  (`/userdata`) and `dhcpcd` using a DUID from `/var/db/dhcpcd`; both are
  recreated by a full firmware update. Two clients also ran on `eth0`
  (`S41dhcpcd` and `udhcpc` from `RkLunch.sh`). Now the MAC is derived from
  `/proc/device-tree/serial-number` and stored in env (`oko_mac`), there is
  one DHCP client (`dhcpcd` with `clientid` by MAC, no `duid`), `S41dhcpcd`
  is a stub, and `RkLunch.sh` calls `oko-net apply`. Verified: after reboot
  MAC `02:47:8d:05:67:56` and IP `192.168.52.97` are preserved; static
  `.220` also survives a reboot.
- WebUI/MP4: HTTP-поток `/video.mp4` больше не отдаёт пустой chunk и не рвёт
  соединение. `send_mp4_to_client()` теперь, как и recorder, режет Annex B
  через `nal_split()`: VPS/SPS/PPS HEVC приходят одним access unit, и старый
  обход `pack->nalu[]` не находил их, поэтому `moov` не строился. Проверено:
  PyAV декодирует 30 кадров hevc 1920x1080 с `http://<DUT>/video.mp4`.
- WebUI/MP4: the `/video.mp4` HTTP stream no longer sends an empty chunk and
  closes. `send_mp4_to_client()` now splits Annex B with `nal_split()` like the
  recorder: HEVC VPS/SPS/PPS arrive in one access unit, and the old
  `pack->nalu[]` walk missed them, so `moov` was never built. Verified: PyAV
  decodes 30 hevc 1920x1080 frames from `http://<DUT>/video.mp4`.
- WebUI: вкладки MP4/MJPEG показывают понятное сообщение, если поток выключен,
  а для MP4 добавлена подсказка про H.265 и MJPEG. MJPEG при
  `mjpeg.enable=true` отдаёт `multipart/x-mixed-replace` (проверено).
- WebUI: the MP4/MJPEG tabs show a clear message when the stream is disabled,
  and the MP4 tab hints at H.265 vs MJPEG. With `mjpeg.enable=true` the MJPEG
  endpoint returns a `multipart/x-mixed-replace` stream (verified).
- microSD/udev: eudev в образе читает только `/lib/udev/rules.d`, поэтому
  правило `60-oko-sdcard.rules` перенесено из `/etc/udev/rules.d` в
  `/lib/udev/rules.d`; теперь `oko-sdcard add/remove` действительно вызывается
  и пишет `/run/oko-sd.state`. Вендорское `61-sd-cards-auto-mount.rules`
  заменено заглушкой: его безусловный remove-`umount` снимал только что
  отформатированную карту на stale-событиях `sfdisk`/`partprobe`; stale-remove
  для ещё существующего `mmcblkX` теперь игнорируется. Проверено: format
  создаёт один FAT32-раздел `OKO`, а unbind/bind `mmc1:aaaa` даёт `present=0`
  → `present=1` и автомонтирование.
- microSD/udev: eudev in this image reads only `/lib/udev/rules.d`, so
  `60-oko-sdcard.rules` moved from `/etc/udev/rules.d` to `/lib/udev/rules.d`;
  `oko-sdcard add/remove` is now actually invoked and writes
  `/run/oko-sd.state`. The vendor `61-sd-cards-auto-mount.rules` is replaced by
  a stub: its unconditional remove-`umount` detached the freshly formatted card
  on stale `sfdisk`/`partprobe` events; stale remove events for an existing
  `mmcblkX` are now ignored. Verified: format creates one FAT32 `OKO` partition
  and unbinding/binding `mmc1:aaaa` changes `present=0` to `present=1` with
  automount.
- `oko-sdcard`: определение карты переведено с `/sys/block/*/removable`
  (в этом 5.10-ядре всегда `0`) на `device/type=SD`; `unmount` снимает все
  наложения `/mnt/sdcard`, включая старые p1+p2. VFAT монтируется с
  `uid=1000,gid=1000,fmask=0133,dmask=0022`.
- `oko-sdcard`: card detection now uses `device/type=SD` because
  `/sys/block/*/removable` is always `0` in this 5.10 kernel; `unmount` drops
  all stacked `/mnt/sdcard` mounts, including legacy p1+p2. VFAT mounts with
  `uid=1000,gid=1000,fmask=0133,dmask=0022`.
- Сборка: выбранная AIISP-модель копируется в oem **до** `build_mkimg oem`;
  раньше `cp` из post-clean выполнялся после создания `oem.img`, и
  `rkmodel_1080_1920.aiisp` отсутствовал в прошивке.
- Build: the selected AIISP model is copied into oem **before**
  `build_mkimg oem`; previously the post-clean `cp` ran after `oem.img` was
  created, so `rkmodel_1080_1920.aiisp` was missing from the firmware.
- `divinus/record`: H.265-запись больше не создаёт пустые `.mp4`. VENC-пак
  для HEVC приходит одним NAL-элементом на весь access unit; `record.c` теперь
  сам режет Annex B через `nal_split` и передаёт VPS/SPS/PPS/IDR/слайсы в
  muxer. Проверено: 8-секундный клип декодируется как 222 кадра 1920x1080.
  Заодно исправлено удаление старых сегментов: `recordPath` больше не
  исключается из квоты, когда файл уже закрыт.
- `divinus/record`: H.265 recording no longer produces empty `.mp4` files. The
  VENC pack for HEVC arrives as one NAL entry for the whole access unit, so
  `record.c` now splits Annex B with `nal_split` and passes
  VPS/SPS/PPS/IDR/slices to the muxer. Verified: an 8 s clip decodes as 222
  frames of 1920x1080. The quota prune was also fixed: a closed `recordPath`
  is no longer skipped, so old segments are actually removed.
- `divinus/rtsp`: `PLAY` больше не вызывает блокирующий геттер канала под уже
  взятым мьютексом RTSP. Дедлок останавливал видео-поток, из-за чего
  `RK_MPI_RGN_SetBitMap` начинал возвращать `0xfffffffe` и OSD пропадал.
  Неизвестный или выключенный `/live/<n>` теперь возвращает 404, а не main.
- `divinus/rtsp`: `PLAY` no longer calls the locking channel getter while the
  RTSP mutex is already held. The deadlock stalled the video thread, which made
  `RK_MPI_RGN_SetBitMap` return `0xfffffffe` and dropped the OSD. An unknown or
  disabled `/live/<n>` now returns 404 instead of falling back to main.
- `divinus`: второй поток при выключении делает soft-stop и оставляет канал
  выделенным (повторное включение мгновенное); создание канала и смена
  параметров энкодера применяются перезапуском в безопасном порядке
  `main → sub → JPEG`. Это устраняет разрушение OSD на RV1106.
- `divinus`: disabling the sub stream is now a soft stop that keeps the channel
  allocated (re-enabling is immediate); creating the channel or changing
  encoder parameters is applied by a restart in the safe
  `main -> sub -> JPEG` order. This removes the RV1106 OSD corruption.
- `divinus/hal/config.h`: `MAX_SECTIONS` увеличено с 16 до 40. Новые секции
  (eptz/rotate/venc/substream/aiisp/ivs/iq/hdr) выводили файл за лимит
  `find_sections()`, и после `app_config_save()` конфиг читался частично
  (`Section 'osd' not found`, `Configuration error code: 1`).
- `divinus/hal/config.h`: `MAX_SECTIONS` raised from 16 to 40. The new
  sections (eptz/rotate/venc/substream/aiisp/ivs/iq/hdr) exceeded
  `find_sections()`'s table, so after `app_config_save()` the configuration
  was read partially (`Section 'osd' not found`, `Configuration error code: 1`).
- `divinus/app_config.c`: сохранение OSD пишет `regX_*` с отступом в два
  пробела, как фабричный YAML; раньше отступ 4 пробела ломал разбор `osd`
  после рестарта и HDR-перезапуск терял регионы.
- `divinus/app_config.c`: OSD save emits `regX_*` with the factory two-space
  indentation; the previous four-space indent broke the `osd` section after a
  restart, so the HDR restart lost the overlays.
- `divinus/hal/plus/rk_hal.c`: `rk_video_destroy()` больше не прерывает
  teardown при ошибке `UnBind` — вызывающий код уже отвязал пару, и повторная
  отвязка оставляла VENC/VPSS канал неразрушенным.
- `divinus/hal/plus/rk_hal.c`: `rk_video_destroy()` no longer aborts teardown
  on an `UnBind` error; callers already unbound the pair, and the second
  unbind left the VENC/VPSS channel alive.
- OSD: RGN переведён на один canvas (`region.overlay.canvas = 1`). При двух
  canvas драйвер периодически отдавал кадр с OSD предыдущей секунды — время на
  видео шло не по порядку. Проверено 60 с: кадров со «старым» временем нет.
- OSD: the RGN now uses a single canvas (`region.overlay.canvas = 1`). With two
  canvases the driver periodically emitted a frame with the previous second's
  OSD, so the displayed time was not sequential. Verified for 60 s with no
  stale-time frames.
- `netboot-catch.py`: после появления login-приглашения (или по таймауту 180 с)
  скрипт закрывает `/dev/ttyACM*` и завершается, больше не держит serial и не
  спамит `^C` в консоль; окно ожидания U-Boot остаётся 900 с.
- `docker-entrypoint.sh`: TFTP-корень больше не world-writable (`0755`), сервер
  запускается без `--create` — загрузка файлов на хост отключена; скачивание
  `boot.img` для netboot работает как прежде.
- `netboot-catch.py`: after detecting the login prompt (or after a 180 s timeout)
  the script closes `/dev/ttyACM*` and exits, so it no longer holds the serial
  port or floods the console with `^C`; the U-Boot catch window stays at 900 s.
- `docker-entrypoint.sh`: the TFTP root is no longer world-writable (`0755`) and
  the server runs without `--create`, disabling uploads; downloading `boot.img`
  for netboot keeps working.
- `TESTBENCH.md` §7C, `README_RU.md`, команда `/bench`: исправлена процедура
  покомпонентной прошивки. `rkflash.sh oem/rootfs` (`upgrade_tool di`) в чистом
  Maskrom без загруженного loader отвечает `parameter is invalid`; нужен
  `db download.bin` + `wl <LBA> <размер всего раздела в секторах>` (`oem`
  0x2800/0xF000, `rootfs` 0x16800/0x69000). `wl` с длиной образа оставляет в
  хвосте старые UBI-PEB → `ubi_attach: bad image sequence number` и panic.
- `TESTBENCH.md` §7C, `README_RU.md`, the `/bench` command: fixed the
  per-partition flashing procedure. `rkflash.sh oem/rootfs` (`upgrade_tool di`)
  in bare Maskrom without a loaded loader reports `parameter is invalid`; use
  `db download.bin` plus `wl <LBA> <full partition size in sectors>` (`oem`
  0x2800/0xF000, `rootfs` 0x16800/0x69000). Passing the image length to `wl`
  leaves stale UBI PEBs → `ubi_attach: bad image sequence number` and panic.
- `netboot-catch.py`: `OKO_NETBOOT_LOG` без каталога (например, `netboot.log`)
  больше не падает на `os.makedirs('')` — родительский каталог нормализуется
  в `.`.
- `netboot-catch.py`: `OKO_NETBOOT_LOG` without a directory component (for
  example, `netboot.log`) no longer fails on `os.makedirs('')`; the parent
  directory is normalized to `.`.
- `INIT_ORDER.md`: убрано дублирование условия бутстрапа `divinus.yaml` —
  замена при отсутствии файла или `reg0_text` описана один раз, резервное
  копирование в `post_chk` отмечено отдельно.
- `INIT_ORDER.md`: removed the duplicated `divinus.yaml` bootstrap condition —
  replacement when the file or `reg0_text` is missing is described once, and
  the `post_chk` fallback is called out separately.
- `divinus.yaml`, который не разбирается (например, после ручной правки или
  импорта), больше не оставляет устройство без стримера: `main.c` при ошибке
  разбора один раз восстанавливает `divinus.yaml.bak` и повторяет разбор,
  и только затем завершается. Проверено на стенде: битый `web_port` →
  восстановление, `/api/status` отвечает.
- A `divinus.yaml` that no longer parses (after a manual edit or an import) no
  longer leaves the device without a streamer: on a parse error `main.c`
  restores `divinus.yaml.bak` once and retries, exiting only if that also
  fails. Verified on the bench: a broken `web_port` is rolled back and
  `/api/status` keeps answering.
- `/api/time?ts=`: отметка времени читалась в `short`, из-за чего эпоха
  обрезалась и на устройство выставлялось случайное время; теперь `long` с
  проверкой нижней границы.
- `/api/time?ts=`: the timestamp was read into a `short`, so the epoch was
  truncated and the device got a random time; it is a `long` with a lower-bound
  check now.
- Проверки символов: `compat.c` определяет функцию `__ctype_b`, а `-rdynamic`
  экспортирует её поверх таблицы ctype uClibc, поэтому любой макрос
  `isalnum()`/`isspace()` разыменовывал код как таблицу и ронял процесс
  (`SIGSEGV`). В `escape_url()`, разборе ONVIF и проверке имени NTP-сервера
  заменено на явные ASCII-сравнения.
- Character checks: `compat.c` defines a function named `__ctype_b` and
  `-rdynamic` exports it over the uClibc ctype table, so every
  `isalnum()`/`isspace()` macro dereferenced code as a table and crashed the
  process (`SIGSEGV`). `escape_url()`, the ONVIF parser and the NTP server-name
  check now use explicit ASCII comparisons.
- `send_file()` отдаёт только файлы под `/mnt/sdcard/` и резолвит симлинки
  (`realpath`). Раньше любой несовпавший URI трактовался как абсолютный путь от
  `/`, а MIME-тип был равен самому URI, поэтому `GET //etc/passwd` отдавал файл
  системы. **Ломающее изменение** для сторонних скриптов, которые скачивали
  файлы вне карты; для архива записей используйте `/api/recordings/file`.
- `send_file()` serves only files under `/mnt/sdcard/` and resolves symlinks
  (`realpath`). Previously any unmatched URI was treated as an absolute path
  from `/` and the MIME type was the URI itself, so `GET //etc/passwd` leaked the
  system file. **Breaking change** for third-party scripts that downloaded files
  outside the card; use `/api/recordings/file` for the recording archive.

### Безопасность

- `project/app/divinus/files/divinus.yaml`: фабричный конфиг включает
  аутентификацию web и RTSP (`admin` / `oko-change-me`), `web_auth_skiplocal`
  выключен; пароль — публичный фабричный дефолт, его нужно сменить до подключения
  камеры к недоверенной сети (см. `TESTBENCH.md` §6).
- `project/app/divinus/files/divinus.yaml`: the factory config now enables web and
  RTSP authentication (`admin` / `oko-change-me`) and disables
  `web_auth_skiplocal`; the password is a public factory default and must be
  changed before the camera is connected to an untrusted network (see
  `TESTBENCH.md` §6).
- `POST /api/cmd/import` принимает только `multipart/form-data`, ограничен
  128 КиБ, требует подстроки `system:` и `web_port:` и пишет файл только после
  этих проверок; предыдущий конфиг сохраняется как `.bak` до перезаписи.
- `POST /api/cmd/import` accepts `multipart/form-data` only, is limited to
  128 KiB, requires the `system:` and `web_port:` markers, and writes the file
  only after those checks; the previous config is kept as `.bak` before the
  overwrite.
- Смена пароля WebUI (`/api/web-auth`) требует текущий пароль и выполняется
  только методом `POST`; пароли по-прежнему не читаются ни одним GET-эндпоинтом.
- The WebUI password change (`/api/web-auth`) requires the current password and
  is `POST`-only; passwords are still never returned by any GET endpoint.

## [0.1.0] — 2026-10-29

Первая версия OKO-форка. База — Luckfox SDK V1.4.

### Добавлено

- Docker-окружение сборки: `Dockerfile` (Ubuntu 22.04, `luckfox-builder:22.04`,
  пользователь `builder` с UID/GID хоста, кросс-тулчейн Rockchip в `PATH`,
  `tftpd-hpa`), `docker-compose.yml` (контейнер `luckfox_oko_build_env`,
  `network_mode: host`, `TFTP_ROOT=/workspace/output/image`),
  `docker-entrypoint.sh` (запуск TFTP-сервера на `69/udp`) и `start.sh`
  (сборка образа, запуск контейнера, Git LFS, вход в shell).
- Конфигурация платы
  `project/cfg/BoardConfig_IPC/BoardConfig-SPI_NAND-Buildroot-RV1106_Luckfox_Pico_Pro_Max-NETBOOT-SC230AI.mk`
  (RV1106 Pro Max, SPI NAND, Buildroot, NETBOOT, SC230AI, `RKIPC_RV1106`,
  CMA 66M, `RK_ENABLE_WIFI=y`) и локальный симлинк `.BoardConfig.mk` на неё
  (симлинк не версионируется — `.gitignore` его исключает, создаётся
  `./build.sh lunch`). Рядом присутствует промежуточный вариант `…-NETBOOT.mk`
  без SC230AI.
- Фрагменты конфигурации ядра `rv1106-netboot.config` (`CONFIG_IP_PNP`,
  `CONFIG_ROOT_NFS`) и `rv1106-sc230ai.config`; подключены через
  `RK_KERNEL_DEFCONFIG_FRAGMENT`.
- Локальные симлинки быстрого доступа к конфигурации (не версионируются,
  каталог `config/` в `.gitignore`): `config/buildroot_defconfig`,
  `config/kernel_defconfig`, `config/dts_config`.
- Автоматизация netboot по последовательному порту: `netboot-catch.py`
  (последний `/dev/ttyACM*`, 115200, настройка `ipaddr`/`netmask`/`serverip`/
  `gatewayip`/`sys_bootargs`, `tftp` → `boot_fit`, лог `/tmp/kilo/netboot-run4.log`).
- Документация проекта: `README_RU.md`, `CHANGELOG.md`, `TESTBENCH.md`,
  `STRUCTURE.md`, `INIT_ORDER.md`.
- `.gitignore` для артефактов сборки (`output/`, `IMAGE/`, `config/`, `*.o`,
  `*.ko`, `build/`), core-дампов и Python-кэша.

### Изменено

- `sysdrv/drv_ko/insmod_ko.sh`: добавлена загрузка `sc230ai.ko` и `sc230ai`
  в список выгрузки неиспользуемых сенсоров (`__rmmod_camera_sensor`).
- Драйвер шагового двигателя 28BYJ-48 из вендорского SDK V1.4
  (`sysdrv/drv_ko/motor`, строка `__insmod motor.ko`) остаётся **выключенным**:
  `RK_ENABLE_MOTOR` не задан, модуль не собирается, а загрузка пропускается
  проверкой `-f`.
- Device tree `sysdrv/source/kernel/arch/arm/boot/dts/rv1106-luckfox-pico-pro-max-ipc.dtsi`
  адаптирован под текущую плату и сенсор: добавлены регуляторы и узел SC230AI,
  endpoint CSI D-PHY, mux линии сброса.
- Драйвер `sysdrv/source/kernel/drivers/media/i2c/sc230ai.c`: для не-thunderboot
  сборки линии `reset`/`pwdn` запрашиваются в состоянии `GPIOD_OUT_LOW`.