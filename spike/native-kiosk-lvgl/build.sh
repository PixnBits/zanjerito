#!/bin/bash
# Fully static LVGL home screen. -static so the binary does not need the Pi's glibc 2.28.
set -euo pipefail
cd "$(dirname "$0")/.."

build_one() {
    local cc="$1"
    local out="$2"
    shift 2
    local extra=("$@")
    local obj
    obj="$(mktemp -d)"
    # shellcheck disable=SC2064
    trap "rm -rf '$obj'" RETURN

    local cflags=(
        -Os
        -std=gnu11
        -ffunction-sections
        -fdata-sections
        -DLV_CONF_INCLUDE_SIMPLE
        -Ihome
        -Ilvgl
        "${extra[@]}"
    )

    echo "compile main.c with $cc"
    "$cc" -c "${cflags[@]}" -Wall -Wextra -Werror -o "$obj/main.o" home/main.c

    local srcs=()
    local f
    while IFS= read -r f; do
        srcs+=("$f")
    done < <(find lvgl/src -type f -name '*.c' | sort)

    local nproc
    nproc="$(nproc 2>/dev/null || echo 2)"
    local -a pids=()
    local fail=0
    local src base
    for src in "${srcs[@]}"; do
        base="${src//\//_}"
        "$cc" -c "${cflags[@]}" -o "$obj/$base.o" "$src" &
        pids+=("$!")
        if (( ${#pids[@]} >= nproc )); then
            if ! wait "${pids[0]}"; then
                fail=1
            fi
            pids=("${pids[@]:1}")
        fi
    done
    for pid in "${pids[@]+"${pids[@]}"}"; do
        if ! wait "$pid"; then
            fail=1
        fi
    done
    if (( fail )); then
        echo "compile failed" >&2
        exit 1
    fi

    local objs=()
    local o
    for o in "$obj"/*.o; do
        objs+=("$o")
    done
    echo "link $out"
    "$cc" -static -o "$out" "${objs[@]}" -Wl,--gc-sections -s -lm
    echo "built $out"
}

want="${1:-all}"
built=0

if [[ "$want" == "all" || "$want" == "arm" ]]; then
    if command -v arm-linux-gnueabihf-gcc >/dev/null 2>&1; then
        build_one arm-linux-gnueabihf-gcc home/zan-lvgl-arm -march=armv7-a -mfpu=vfpv3-d16 -mfloat-abi=hard
        built=1
    elif [[ "$want" == "arm" ]]; then
        echo "arm-linux-gnueabihf-gcc not found" >&2
        exit 1
    fi
fi

if [[ "$want" == "all" || "$want" == "host" ]]; then
    host_cc=""
    if command -v gcc >/dev/null 2>&1; then
        host_cc="gcc"
    elif command -v cc >/dev/null 2>&1; then
        host_cc="cc"
    fi
    if [[ -n "$host_cc" ]]; then
        build_one "$host_cc" home/zan-lvgl-host
        built=1
    elif [[ "$want" == "host" ]]; then
        echo "host gcc not found" >&2
        exit 1
    fi
fi

if [[ "$built" -ne 1 ]]; then
    echo "no compiler found" >&2
    exit 1
fi
