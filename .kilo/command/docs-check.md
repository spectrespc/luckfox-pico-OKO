---
description: Проверить актуальность STRUCTURE/INIT_ORDER/TESTBENCH против репозитория
agent: code
---

## Снимок репозитория

- Активный BoardConfig: !`readlink -f .BoardConfig.mk`
- Документы: !`ls -l STRUCTURE.md INIT_ORDER.md TESTBENCH.md 2>&1`
- Init-скрипты: !`ls output/out/rootfs_uclibc_rv1106/etc/init.d/S??* 2>&1`
- Модули в артефакте: !`ls output/out/oem/usr/ko/ 2>/dev/null | grep -E 'motor|hpmcu'`

## Чек-лист сверки

| Документ | Раздел | Сверить с |
|---|---|---|
| `INIT_ORDER.md` | §1 | симлинк `.BoardConfig.mk` и значения в целевом `.mk` (`RK_CHIP`, `RK_BOOT_MEDIUM`, `RK_KERNEL_DTS`, `RK_ENABLE_MOTOR`) |
| `INIT_ORDER.md` | §2 | фрагменты `rv1106-netboot.config`, DTS, bootargs |
| `INIT_ORDER.md` | §4 | `ls output/out/rootfs_uclibc_rv1106/etc/init.d/S??*` и оверлей `project/cfg/BoardConfig_IPC/overlay/overlay-luckfox-buildroot-init/etc/init.d/` |
| `INIT_ORDER.md` | §6 | `grep -n '__insmod\|insmod_wifi' sysdrv/drv_ko/insmod_ko.sh` |
| `INIT_ORDER.md` | §7 | статус motor: `grep -n RK_ENABLE_MOTOR "$(readlink -f .BoardConfig.mk)"` |
| `TESTBENCH.md` | §4–§5 | `CMDS` в `netboot-catch.py`, `start.sh`, `docker-compose.yml` |
| `TESTBENCH.md` | §1 | «Дата обновления» — должна соответствовать дате последней правки дока |
| `STRUCTURE.md` | Корневые папки/файлы | `ls` корня репозитория |

## Порядок выполнения

1. Пройди чек-лист, выполняя команды из таблицы (не полагайся только на снимок выше).
2. Для каждой строки зафиксируй статус: `совпадает` / `расхождение` / `не проверено`
   (если артефакта `output/` нет или он устарел).
3. Собери отчёт: `Документ | Раздел | Ожидалось | Факт | Статус`.
4. Расхождения исправляй в источнике: обнови соответствующий раздел дока, при правке
   `TESTBENCH.md` — «Дату обновления» в §1, затем добавь запись в `CHANGELOG.md` →
   `[Не выпущено]`.
5. Ничего не меняй в вендорских `README.md`, `README_CN.md`, `UPDATE_LOG.md`,
   `UPDATE_LOG_CN.md`.

Источник истины при противоречии — скрипты, `.mk` и исходники, а не документ.