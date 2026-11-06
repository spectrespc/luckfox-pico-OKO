#!/bin/sh
# Pull the latest STM32 firmware changes from spectrespc/stm32_acpi-ec into
# the project/app/stm32_acpi-ec subtree.  Local OKO changes must be committed
# or stashed first; git subtree creates a merge commit and reports conflicts
# in the usual way.
set -eu

PREFIX="project/app/stm32_acpi-ec"
REMOTE="stm32"
BRANCH="main"

if ! git remote get-url "$REMOTE" >/dev/null 2>&1; then
    git remote add "$REMOTE" https://github.com/spectrespc/stm32_acpi-ec.git
    git remote set-url --push "$REMOTE" git@github.com:spectrespc/stm32_acpi-ec.git
fi

git subtree pull --prefix="$PREFIX" "$REMOTE" "$BRANCH"