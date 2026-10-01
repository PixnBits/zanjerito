#!/bin/sh
# prepare | run | restore-cursor | reset-backlight for zan-kiosk.service.
# ZAN_ROOT prefixes paths this script opens (sysfs, tty, default binary).
# It does not prefix --fb, --touch, or --api. Those are passed to the client.
# ZAN_BACKLIGHT is prefixed. Default ZAN_ROOT is empty, so the paths are the real ones.
set -u

ZAN_ROOT=${ZAN_ROOT-}

usage() {
    printf 'usage: zan-kiosk-run.sh prepare|run|restore-cursor|reset-backlight\n' >&2
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

# Seconds, 0 through 1000000000. Not the 0-255 backlight cap.
normalize_seconds() {
    b=$1
    if [ "${#b}" -gt 10 ]; then
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
    if [ "$n" -gt 1000000000 ]; then
        return 1
    fi
    printf '%s' "$n"
}

backlight_sysdir() {
    printf '%s%s' "$ZAN_ROOT" "${ZAN_BACKLIGHT:-/sys/class/backlight/rpi_backlight}"
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

# chgrp video and chmod g+w so the pi user (group video) can dim later.
# Best effort. A missing file or a missing chgrp does not fail prepare.
prepare_backlight_access() {
    dir=$(backlight_sysdir)
    for name in brightness bl_power; do
        path=$dir/$name
        if [ ! -e "$path" ]; then
            printf 'prepare: skipped %s (absent)\n' "$path" >&2
            continue
        fi
        if ! command -v chgrp >/dev/null 2>&1; then
            printf 'prepare: chgrp not found, skipped %s\n' "$path" >&2
        elif chgrp video "$path"; then
            printf 'prepare: chgrp video %s\n' "$path" >&2
        else
            printf 'prepare: could not chgrp %s (ignored)\n' "$path" >&2
        fi
        if ! command -v chmod >/dev/null 2>&1; then
            printf 'prepare: chmod not found, skipped %s\n' "$path" >&2
        elif chmod g+w "$path"; then
            printf 'prepare: chmod g+w %s\n' "$path" >&2
        else
            printf 'prepare: could not chmod %s (ignored)\n' "$path" >&2
        fi
    done
}

# BACKLIGHT unset: start from the panel max. The client saves that and restores it.
prepare_backlight_max() {
    dir=$(backlight_sysdir)
    mx=$dir/max_brightness
    br=$dir/brightness
    if [ ! -f "$mx" ] || [ ! -e "$br" ]; then
        printf 'prepare: skipped max brightness (absent)\n' >&2
        return 0
    fi
    level=$(tr -d '[:space:]' <"$mx" 2>/dev/null || true)
    case $level in
        ''|*[!0-9]*)
            printf 'prepare: skipped max brightness (unreadable)\n' >&2
            return 0
            ;;
    esac
    if printf '%s\n' "$level" >"$br"; then
        printf 'prepare: wrote max brightness %s to %s\n' "$level" "$br" >&2
    else
        printf 'prepare: could not write %s (ignored)\n' "$br" >&2
    fi
}

# BACKLIGHT set: write that level to the configured directory only.
prepare_backlight() {
    level=${BACKLIGHT-}
    dir=$(backlight_sysdir)
    prepare_backlight_access
    if [ -z "$level" ]; then
        prepare_backlight_max
        return 0
    fi
    norm=$(normalize_level "$level") || {
        printf 'prepare: skipped backlight (BACKLIGHT=%s is not 0-255)\n' "$level" >&2
        return 0
    }
    br=$dir/brightness
    if [ ! -e "$br" ]; then
        printf 'prepare: skipped backlight (no device)\n' >&2
        return 0
    fi
    if printf '%s\n' "$norm" >"$br"; then
        printf 'prepare: wrote backlight %s to %s\n' "$norm" "$br" >&2
    else
        printf 'prepare: could not write %s (ignored)\n' "$br" >&2
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
    if [ -n "${ZAN_DIM_AFTER_SEC-}" ]; then
        norm=$(normalize_seconds "$ZAN_DIM_AFTER_SEC") || {
            printf 'zan-kiosk-run: skipped --dim-after (ZAN_DIM_AFTER_SEC=%s is not 0-1000000000)\n' "$ZAN_DIM_AFTER_SEC" >&2
            norm=
        }
        if [ -n "$norm" ]; then
            set -- "$@" --dim-after "$norm"
        fi
    fi
    if [ -n "${ZAN_OFF_AFTER_SEC-}" ]; then
        norm=$(normalize_seconds "$ZAN_OFF_AFTER_SEC") || {
            printf 'zan-kiosk-run: skipped --off-after (ZAN_OFF_AFTER_SEC=%s is not 0-1000000000)\n' "$ZAN_OFF_AFTER_SEC" >&2
            norm=
        }
        if [ -n "$norm" ]; then
            set -- "$@" --off-after "$norm"
        fi
    fi
    if [ -n "${ZAN_DIM_LEVEL-}" ]; then
        norm=$(normalize_level "$ZAN_DIM_LEVEL") || {
            printf 'zan-kiosk-run: skipped --dim-level (ZAN_DIM_LEVEL=%s is not 0-255)\n' "$ZAN_DIM_LEVEL" >&2
            norm=
        }
        if [ -n "$norm" ]; then
            set -- "$@" --dim-level "$norm"
        fi
    fi
    if [ -n "${ZAN_BACKLIGHT-}" ]; then
        set -- "$@" --backlight "$ZAN_BACKLIGHT"
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

# Root, best effort, always exits 0. Configured directory only.
# Writes max_brightness into brightness and 0 into bl_power.
reset_backlight() {
    dir=$(backlight_sysdir)
    br=$dir/brightness
    mx=$dir/max_brightness
    bl=$dir/bl_power
    if [ -f "$mx" ] && [ -e "$br" ]; then
        level=$(tr -d '[:space:]' <"$mx" 2>/dev/null || true)
        case $level in
            ''|*[!0-9]*)
                printf 'reset-backlight: skipped brightness (bad max)\n' >&2
                ;;
            *)
                if printf '%s\n' "$level" >"$br"; then
                    printf 'reset-backlight: wrote %s to %s\n' "$level" "$br" >&2
                else
                    printf 'reset-backlight: could not write %s (ignored)\n' "$br" >&2
                fi
                ;;
        esac
    else
        printf 'reset-backlight: skipped brightness (absent)\n' >&2
    fi
    if [ -e "$bl" ]; then
        if printf '0\n' >"$bl"; then
            printf 'reset-backlight: wrote 0 to %s\n' "$bl" >&2
        else
            printf 'reset-backlight: could not write %s (ignored)\n' "$bl" >&2
        fi
    else
        printf 'reset-backlight: skipped bl_power (absent)\n' >&2
    fi
    exit 0
}

cmd=${1-}
case $cmd in
    prepare) prepare ;;
    run) run_kiosk ;;
    restore-cursor) restore_cursor ;;
    reset-backlight) reset_backlight ;;
    *) usage ;;
esac
