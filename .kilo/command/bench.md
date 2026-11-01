---
description: Передача на тестовый стенд — подключения, сеть, прошивка, диагностика
agent: code
---

Актуальные параметры стенда (инжект `TESTBENCH.md` на момент вызова):

@TESTBENCH.md

## Порядок работы

1. **Подключения** — сверь §4: UART `/dev/ttyACM0`, `115200 8N1`, Ethernet
   `192.168.52.0/24`; проверь наличие устройства (`ls /dev/ttyACM*`) и утилит
   (`ls bench`, `ls output/bench/venv` при первом запуске).
2. **Сеть** — сверь §5: хост `192.168.52.200` (TFTP + NFS), DUT `192.168.52.150`,
   шлюз `192.168.52.254`; сравни с `NETBOOT_CMDS` в `bench/netboot.py` и `start.sh`.
3. **Учётки** — §6: `root` / вендорский дефолт; секреты в репозиторий не писать.
4. **Прошивка/загрузка** — выбери режим по §7:
   - A. SPI NAND: `./build.sh firmware` → `./rkflash.sh update` (полный образ);
   - B. Netboot: `./start.sh`, NFS-экспорт, `bench/run.sh netboot.py catch`
     (или `reboot`), лог `output/bench/netboot-<ts>.log`;
   - C. Покомпонентно: `upgrade_tool db output/image/download.bin`, затем
     `wl` по LBA с полным размером раздела (`oem` 0x2800/0xF000,
     `rootfs` 0x16800/0x69000) и `rd`; см. §7C.
5. **Диагностика** — пройди чек-лист §8 (модули, сенсор, `divinus`/`rkipc`, OSD
   в RTSP, сеть, монтирование, логи); команды на DUT — `bench/run.sh
   serial_exec.py …` / `bench/run.sh dut.py …`, OSD — `bench/run.sh osd_check.py`.
   При сетевом или файловом сбое документации источник истины — `bench/*.py`.
6. **По завершении** — если фактические значения разошлись с документом, обнови
   `TESTBENCH.md` и добавь запись в `CHANGELOG.md` → `[Не выпущено]` (см. §10 и
   `AGENTS.md` §4). Значения из `output/` не подменяют проверку скриптов.

При сомнении источник истины — `bench/*.py`, `netboot-catch.py`, `start.sh`,
`docker-compose.yml` и активный `.BoardConfig.mk`, а не текст документа.