#!/bin/sh
# Install the native kiosk files. Does not enable the unit or change the
# boot target unless --switch is passed. Never stops or restarts the
# irrigation daemon.
#
#   --dry-run   print actions, write nothing, do not run systemctl
#   --switch    also set multi-user, disable lightdm, enable zan-kiosk
#   --now       with --switch: stop lightdm and start zan-kiosk now
#   --bin PATH  kiosk binary (default: ../build/zan-kiosk-arm)
#
# DESTDIR prefixes file destinations (empty = the real root).
# SYSTEMCTL is the systemctl command (default: systemctl).
# Root is required only when DESTDIR is empty and this is not a dry run.
# PREFIX defaults to /opt/zanjerito. The unit file hardcodes that prefix.
set -eu

DRY=0
SWITCH=0
NOW=0
BIN=

usage() {
    printf 'usage: install-kiosk.sh [--dry-run] [--switch] [--now] [--bin PATH]\n' >&2
    exit 2
}

die() {
    printf 'install-kiosk: %s\n' "$1" >&2
    exit 1
}

while [ $# -gt 0 ]; do
    case $1 in
        --dry-run) DRY=1 ;;
        --switch) SWITCH=1 ;;
        --now) NOW=1 ;;
        --bin)
            shift
            if [ $# -eq 0 ] || [ -z "$1" ]; then
                usage
            fi
            BIN=$1
            ;;
        -h|--help) usage ;;
        *)
            printf 'install-kiosk: unknown argument: %s\n' "$1" >&2
            usage
            ;;
    esac
    shift
done

if [ "$NOW" -eq 1 ] && [ "$SWITCH" -eq 0 ]; then
    die "--now requires --switch"
fi

DESTDIR=${DESTDIR:-}
DESTDIR=${DESTDIR%/}
SYSTEMCTL=${SYSTEMCTL:-systemctl}
PREFIX=${PREFIX:-/opt/zanjerito}

SCRIPT_DIR=$(CDPATH='' cd -- "$(dirname "$0")" && pwd)

if [ -z "$BIN" ]; then
    BIN=$SCRIPT_DIR/../build/zan-kiosk-arm
fi
if [ ! -f "$BIN" ]; then
    die "missing binary $BIN (pass --bin PATH, or make -C native-kiosk arm)"
fi

if [ -z "$DESTDIR" ] && [ "$DRY" -eq 0 ]; then
    if [ "$(id -u)" -ne 0 ]; then
        die "need root, or set DESTDIR, or pass --dry-run"
    fi
fi

if [ "$PREFIX" != /opt/zanjerito ]; then
    printf 'install-kiosk: warning: unit still runs /opt/zanjerito/zan-kiosk-run.sh (PREFIX=%s)\n' "$PREFIX" >&2
fi

say_run() {
    if [ "$DRY" -eq 1 ]; then
        printf 'DRY-RUN:'
        for arg in "$@"; do
            printf ' %s' "$arg"
        done
        printf '\n'
        return 0
    fi
    "$@"
}

zan_api_from() {
    file=$1
    api=
    nl=$(printf '\r')
    while IFS= read -r line || [ -n "$line" ]; do
        line=${line%"$nl"}
        case $line in
            ''|'#'*) continue ;;
        esac
        case $line in
            'export ZAN_API='*) line=${line#export } ;;
        esac
        case $line in
            ZAN_API=*) api=${line#ZAN_API=} ;;
        esac
    done <"$file"
    case $api in
        \"*\") api=${api#\"}; api=${api%\"} ;;
        \'*\') api=${api#\'}; api=${api%\'} ;;
    esac
    printf '%s' "$api"
}

api_is_placeholder() {
    case ${1-} in
        ''|*CONTROLLER_HOST*) return 0 ;;
        *) return 1 ;;
    esac
}

opt=$DESTDIR$PREFIX
bin_dst=$opt/zan-kiosk
unit_dir=$DESTDIR/etc/systemd/system
unit_dst=$unit_dir/zan-kiosk.service
env_dir=$DESTDIR/etc/default
env_dst=$env_dir/zan-kiosk

if [ -f "$bin_dst" ]; then
    ts=$(date +%Y%m%d%H%M%S)
    bak=$bin_dst.bak.$ts
    n=0
    while [ -e "$bak" ]; do
        n=$((n + 1))
        bak=$bin_dst.bak.$ts.$n
    done
    say_run cp -a "$bin_dst" "$bak"
    if [ "$DRY" -eq 0 ]; then
        printf 'backed up %s -> %s\n' "$bin_dst" "$bak"
    fi
fi

say_run install -d "$opt"
say_run install -m 755 "$BIN" "$bin_dst"
say_run install -m 755 "$SCRIPT_DIR/zan-kiosk-run.sh" "$opt/zan-kiosk-run.sh"
say_run install -m 755 "$SCRIPT_DIR/rollback-desktop.sh" "$opt/rollback-desktop.sh"
say_run install -d "$unit_dir"
say_run install -m 644 "$SCRIPT_DIR/zan-kiosk.service" "$unit_dst"

if [ -f "$env_dst" ]; then
    if [ "$DRY" -eq 1 ]; then
        printf 'DRY-RUN: keep existing %s\n' "$env_dst"
    else
        printf 'kept existing %s\n' "$env_dst"
    fi
else
    say_run install -d "$env_dir"
    say_run install -m 644 "$SCRIPT_DIR/zan-kiosk.default.example" "$env_dst"
    if [ "$DRY" -eq 0 ]; then
        printf 'wrote %s (edit ZAN_API)\n' "$env_dst"
    fi
fi

say_run "$SYSTEMCTL" daemon-reload

if [ "$SWITCH" -eq 1 ]; then
    if [ ! -f "$env_dst" ]; then
        die "refusing --switch: $env_dst does not exist"
    fi
    api=$(zan_api_from "$env_dst")
    if api_is_placeholder "$api"; then
        die "refusing --switch: edit $env_dst and set ZAN_API (http://CONTROLLER_HOST:8080 is not a controller)"
    fi
    say_run "$SYSTEMCTL" set-default multi-user.target
    say_run "$SYSTEMCTL" disable lightdm
    say_run "$SYSTEMCTL" enable zan-kiosk
    if [ "$NOW" -eq 1 ]; then
        say_run "$SYSTEMCTL" stop lightdm
        say_run "$SYSTEMCTL" start zan-kiosk
    fi
fi

if [ "$DRY" -eq 1 ]; then
    exit 0
fi

if [ "$SWITCH" -eq 0 ]; then
    printf 'Installed under %s. The unit is not enabled.\n' "$PREFIX"
    printf 'Next:\n'
    printf '  1. Edit /etc/default/zan-kiosk and replace ZAN_API.\n'
    printf '  2. Writes stay off until ZAN_ALLOW_WRITES=1.\n'
    printf '  3. %s --switch    (next boot)\n' "$0"
    printf '     %s --switch --now\n' "$0"
else
    printf 'Installed under %s.\n' "$PREFIX"
    if [ "$NOW" -eq 1 ]; then
        printf 'Stopped lightdm and started zan-kiosk.\n'
    else
        printf 'Default target is multi-user. lightdm is disabled. zan-kiosk is enabled.\n'
        printf 'Reboot to leave the desktop.\n'
    fi
    printf 'The irrigation daemon was not restarted.\n'
fi
printf 'Rollback: %s/rollback-desktop.sh\n' "$PREFIX"
