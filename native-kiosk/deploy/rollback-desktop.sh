#!/bin/sh
# Put the desktop back. Does not stop, restart, or disable the irrigation daemon.
#
#   systemctl disable --now zan-kiosk
#   zan-kiosk-run.sh reset-backlight   (best effort; does not fail rollback)
#   systemctl enable lightdm
#   systemctl set-default graphical.target
#   systemctl start lightdm
#
# Each systemctl step runs even if an earlier one failed. The script exits
# non-zero if any systemctl step failed. --dry-run prints the four systemctl
# commands and reset-backlight, and runs none.
#
# DESTDIR empty and not --dry-run: root required. DESTDIR is not a file
# prefix here; it only marks a staged test so root is not required.
# SYSTEMCTL overrides the systemctl command (default: systemctl).
set -u

DRY=0

usage() {
    printf 'usage: rollback-desktop.sh [--dry-run]\n' >&2
    exit 2
}

while [ $# -gt 0 ]; do
    case $1 in
        --dry-run) DRY=1 ;;
        -h|--help) usage ;;
        *)
            printf 'rollback-desktop: unknown argument: %s\n' "$1" >&2
            usage
            ;;
    esac
    shift
done

DESTDIR=${DESTDIR:-}
SYSTEMCTL=${SYSTEMCTL:-systemctl}

if [ -z "$DESTDIR" ] && [ "$DRY" -eq 0 ]; then
    if [ "$(id -u)" -ne 0 ]; then
        printf 'rollback-desktop: need root, or set DESTDIR, or pass --dry-run\n' >&2
        exit 1
    fi
fi

fail=0

step() {
    if [ "$DRY" -eq 1 ]; then
        printf 'DRY-RUN:'
        for arg in "$@"; do
            printf ' %s' "$arg"
        done
        printf '\n'
        return 0
    fi
    if "$@"; then
        return 0
    fi
    printf 'rollback-desktop: failed:' >&2
    for arg in "$@"; do
        printf ' %s' "$arg" >&2
    done
    printf '\n' >&2
    fail=1
    return 0
}

# Best effort. A missing script or a failed write does not fail rollback.
reset_backlight_step() {
    if [ -n "$DESTDIR" ]; then
        script=$DESTDIR/opt/zanjerito/zan-kiosk-run.sh
        root=$DESTDIR
    else
        script=/opt/zanjerito/zan-kiosk-run.sh
        root=
    fi
    if [ "$DRY" -eq 1 ]; then
        printf 'DRY-RUN: %s reset-backlight\n' "$script"
        return 0
    fi
    if [ ! -x "$script" ]; then
        printf 'rollback-desktop: reset-backlight skipped (no %s)\n' "$script" >&2
        return 0
    fi
    if ZAN_ROOT=$root "$script" reset-backlight >/dev/null; then
        return 0
    fi
    printf 'rollback-desktop: reset-backlight failed (ignored)\n' >&2
    return 0
}

step "$SYSTEMCTL" disable --now zan-kiosk
reset_backlight_step
step "$SYSTEMCTL" enable lightdm
step "$SYSTEMCTL" set-default graphical.target
step "$SYSTEMCTL" start lightdm

exit "$fail"
