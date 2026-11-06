#!/bin/sh
# Commit changes under project/app/stm32_acpi-ec and push them to the
# STM32 firmware repository.  The directory is a git subtree, not a plain
# copy: `git subtree push` reconstructs the upstream history, so the pushed
# branch remains a fast-forward of spectrespc/stm32_acpi-ec.
#
# Usage:
#   tools/stm32_push.sh "app: fix battery current sign"
#
# The script commits only the subtree path; unrelated OKO changes stay
# unstaged.  Run tools/stm32_pull.sh first if the upstream branch moved.
set -eu

PREFIX="project/app/stm32_acpi-ec"
REMOTE="stm32"
BRANCH="main"
MSG="${1:-}"

if [ -z "$MSG" ]; then
    echo "usage: $0 \"commit message\"" >&2
    exit 2
fi
if [ ! -d "$PREFIX" ]; then
    echo "$PREFIX is missing; run git subtree add/pull first" >&2
    exit 2
fi

if ! git remote get-url "$REMOTE" >/dev/null 2>&1; then
    git remote add "$REMOTE" https://github.com/spectrespc/stm32_acpi-ec.git
    git remote set-url --push "$REMOTE" git@github.com:spectrespc/stm32_acpi-ec.git
fi

git add -- "$PREFIX"
if git diff --cached --quiet -- "$PREFIX"; then
    echo "no changes under $PREFIX"
    exit 0
fi

git commit -m "$MSG" -- "$PREFIX"
git subtree push --prefix="$PREFIX" "$REMOTE" "$BRANCH"