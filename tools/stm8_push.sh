#!/bin/sh
# Commit changes under project/app/stm8-motor and push them to the STM8
# firmware repository.  The directory is a git subtree: `git subtree push`
# reconstructs the upstream history, so the pushed branch stays a fast-forward
# of spectrespc/stm8s103_i2c_motor.
#
# Usage:
#   tools/stm8_push.sh "boot: fix erase timeout"
#
# Only the subtree path is committed; other OKO changes are left alone.
set -eu

PREFIX="project/app/stm8-motor"
REMOTE="stm8"
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
    git remote add "$REMOTE" https://github.com/spectrespc/stm8s103_i2c_motor.git
    git remote set-url --push "$REMOTE" git@github.com:spectrespc/stm8s103_i2c_motor.git
fi

git add -- "$PREFIX"
if git diff --cached --quiet -- "$PREFIX"; then
    echo "no changes under $PREFIX"
    exit 0
fi

git commit -m "$MSG" -- "$PREFIX"
git subtree push --prefix="$PREFIX" "$REMOTE" "$BRANCH"