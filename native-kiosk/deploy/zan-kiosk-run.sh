#!/bin/sh
# prepare | run | restore-cursor for zan-kiosk.service.
# ZAN_ROOT prefixes paths this script opens (sysfs, tty, default binary).
# It does not prefix --fb, --touch, or --api. Those are passed to the client.
# Default ZAN_ROOT is empty, so the paths are the real ones.
set -u

ZAN_ROOT=${ZAN_ROOT-}

usage() {
    printf 'usage: zan-kiosk-run.sh prepare|run|restore-cursor\n' >&2
    exit 2
}

normalize_level() {
    b=$1
    if [ "${#b}" -gt 8 ]; then
        return 1
    fi
    case $b in
        ''|*[!0-9]*) return 1 ;;
    esac
    n=$b
    while [ "${n#0}" != "$n" ]; do
        n=${n#0}
    done
    if [ -z "$n" ]; then
        n=0
    fi
    if [ "$n" -gt 255 ]; then
        return 1
    fi
    printf '%s' "$n"
}

prepare_cursor_blink() {
    blink=$ZAN_ROOT/sys/class/graphics/fbcon/cursor_blink
    if [ ! -e "$blink" ]; then
        printf 'prepare: skipped cursor_blink (absent)\n' >&2
        return 0
    fi
    if printf '0\n' >"$blink"; then
        printf 'prepare: wrote 0 to %s\n' "$blink" >&2
    else
        printf 'prepare: could not write %s (ignored)\n' "$blink" >&2
    fi
}

prepare_tty() {
    tty=$ZAN_ROOT/dev/tty1
    if [ ! -e "$tty" ]; then
        printf 'prepare: skipped tty1 (absent)\n' >&2
        return 0
    fi
    if printf '\033[?25l' >"$tty"; then
        printf 'prepare: hid vt cursor on %s\n' "$tty" >&2
    else
        printf 'prepare: could not hide vt cursor on %s (ignored)\n' "$tty" >&2
    fi
    if printf '\033[9;0]' >>"$tty"; then
        printf 'prepare: disabled console blank on %s\n' "$tty" >&2
    else
        printf 'prepare: could not disable console blank on %s (ignored)\n' "$tty" >&2
    fi
    if command -v setterm >/dev/null 2>&1; then
        # The target is the console device. stdin and stdout are both tty1.
        # shellcheck disable=SC2094
        if setterm --blank 0 --powerdown 0 --cursor off <"$tty" >"$tty"; then
            printf 'prepare: setterm --blank 0 --powerdown 0 --cursor off\n' >&2
        else
            printf 'prepare: setterm failed (ignored)\n' >&2
        fi
    else
        printf 'prepare: setterm not found, skipped\n' >&2
    fi
}

prepare_backlight() {
    level=${BACKLIGHT-}
    if [ -z "$level" ]; then
        printf 'prepare: skipped backlight (BACKLIGHT unset)\n' >&2
        return 0
    fi
    norm=$(normalize_level "$level") || {
        printf 'prepare: skipped backlight (BACKLIGHT=%s is not 0-255)\n' "$level" >&2
        return 0
    }
    found=0
    for path in "$ZAN_ROOT"/sys/class/backlight/*/brightness; do
        if [ ! -e "$path" ]; then
            continue
        fi
        found=1
        if printf '%s\n' "$norm" >"$path"; then
            printf 'prepare: wrote backlight %s to %s\n' "$norm" "$path" >&2
        else
            printf 'prepare: could not write %s (ignored)\n' "$path" >&2
        fi
    done
    if [ "$found" -eq 0 ]; then
        printf 'prepare: skipped backlight (no device)\n' >&2
    fi
}

prepare() {
    prepare_cursor_blink
    prepare_tty
    prepare_backlight
    exit 0
}

run_kiosk() {
    api=${ZAN_API-}
    case $api in
        '')
            printf 'zan-kiosk-run: ZAN_API is not set. Edit /etc/default/zan-kiosk.\n' >&2
            exit 1
            ;;
        *CONTROLLER_HOST*)
            printf 'zan-kiosk-run: ZAN_API is still the placeholder (%s). Edit /etc/default/zan-kiosk.\n' "$api" >&2
            exit 1
            ;;
    esac

    fb=${ZAN_FB:-/dev/fb0}
    if [ -n "${ZAN_BIN-}" ]; then
        bin=$ZAN_BIN
    else
        bin=$ZAN_ROOT/opt/zanjerito/zan-kiosk
    fi

    set -- --fb "$fb" --api "$api"
    if [ "${ZAN_ALLOW_WRITES-}" = 1 ]; then
        set -- "$@" --allow-writes
    fi
    if [ -n "${ZAN_TOUCH-}" ]; then
        set -- "$@" --touch "$ZAN_TOUCH"
    fi
    if [ -n "${ZAN_EXTRA_ARGS-}" ]; then
        # Split on spaces. No glob and no second parse, so ';' stays an argument.
        case $- in
            *f*) extra_f=1 ;;
            *) extra_f=0 ;;
        esac
        set -f
        # shellcheck disable=SC2086
        set -- "$@" $ZAN_EXTRA_ARGS
        if [ "$extra_f" -eq 0 ]; then
            set +f
        fi
    fi

    if [ ! -x "$bin" ]; then
        printf 'zan-kiosk-run: not executable: %s\n' "$bin" >&2
        exit 127
    fi
    exec "$bin" "$@"
    printf 'zan-kiosk-run: exec %s failed\n' "$bin" >&2
    exit 127
}

restore_cursor() {
    tty=$ZAN_ROOT/dev/tty1
    if [ ! -e "$tty" ]; then
        printf 'restore-cursor: skipped (no %s)\n' "$tty" >&2
        exit 0
    fi
    if printf '\033[?25h' >"$tty"; then
        printf 'restore-cursor: showed vt cursor on %s\n' "$tty" >&2
    else
        printf 'restore-cursor: could not write %s (ignored)\n' "$tty" >&2
    fi
    exit 0
}

cmd=${1-}
case $cmd in
    prepare) prepare ;;
    run) run_kiosk ;;
    restore-cursor) restore_cursor ;;
    *) usage ;;
esac
