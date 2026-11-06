#!/bin/sh
# Pull the latest STM8 firmware changes from spectrespc/stm8s103_i2c_motor into
# the project/app/stm8-motor subtree.  Commit or stash local changes first;
# git subtree creates a merge commit and reports conflicts normally.
set -eu

PREFIX="project/app/stm8-motor"
REMOTE="stm8"
BRANCH="main"

if ! git remote get-url "$REMOTE" >/dev/null 2>&1; then
    git remote add "$REMOTE" https://github.com/spectrespc/stm8s103_i2c_motor.git
    git remote set-url --push "$REMOTE" git@github.com:spectrespc/stm8s103_i2c_motor.git
fi

git subtree pull --prefix="$PREFIX" "$REMOTE" "$BRANCH"