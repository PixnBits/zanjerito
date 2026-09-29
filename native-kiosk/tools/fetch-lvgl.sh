#!/bin/sh
# Shallow-clone the pinned LVGL tag into .cache/lvgl and require that HEAD
# matches the pinned commit. A checkout that already matches is left alone.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"

PIN=tools/lvgl.pin
tag=
commit=
repo=

while IFS= read -r line || [ -n "$line" ]; do
    case "$line" in
        ''|'#'*) continue ;;
    esac
    key=${line%%=*}
    val=${line#*=}
    case "$key" in
        tag) tag=$val ;;
        commit) commit=$val ;;
        repo) repo=$val ;;
    esac
done < "$PIN"

if [ -z "$tag" ] || [ -z "$commit" ] || [ -z "$repo" ]; then
    echo "fetch-lvgl: $PIN must set tag, commit, and repo" >&2
    exit 1
fi

dest=.cache/lvgl
mkdir -p .cache

if [ -d "$dest/.git" ]; then
    got=$(git -C "$dest" rev-parse HEAD)
    if [ "$got" = "$commit" ]; then
        exit 0
    fi
    echo "fetch-lvgl: $dest HEAD $got != pinned $commit" >&2
    exit 1
fi

if [ -e "$dest" ]; then
    echo "fetch-lvgl: $dest exists but is not a git checkout" >&2
    exit 1
fi

GIT_TERMINAL_PROMPT=0 git clone --depth 1 --branch "$tag" "$repo" "$dest"
got=$(git -C "$dest" rev-parse HEAD)
if [ "$got" != "$commit" ]; then
    echo "fetch-lvgl: cloned HEAD $got != pinned $commit" >&2
    exit 1
fi
