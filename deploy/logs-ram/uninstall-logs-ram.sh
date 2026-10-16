#!/usr/bin/env bash
# Remove RAM /var/log and restore the SD /var/log mount.
# Syncs first if the tmpfs is active. Does not touch the irrigation daemon
# or /opt/zanjerito data.
#
#   --dry-run   print actions, write nothing, do not run systemctl
#
# DESTDIR prefixes file destinations (empty = the real root).
# SYSTEMCTL is the systemctl command (default: systemctl).
# Root is required only when DESTDIR is empty and this is not a dry run.
set -euo pipefail

PATH=/usr/sbin:/usr/bin:/sbin:/bin
DRY=0

usage() {
    printf 'usage: uninstall-logs-ram.sh [--dry-run]\n' >&2
    exit 2
}

die() {
    printf 'uninstall-logs-ram: %s\n' "$1" >&2
    exit 1
}

while [[ $# -gt 0 ]]; do
    case $1 in
        --dry-run) DRY=1 ;;
        -h | --help) usage ;;
        *)
            printf 'uninstall-logs-ram: unknown argument: %s\n' "$1" >&2
            usage
            ;;
    esac
    shift
done

DESTDIR=${DESTDIR:-}
DESTDIR=${DESTDIR%/}
SYSTEMCTL=${SYSTEMCTL:-systemctl}

if [[ -z $DESTDIR && $DRY -eq 0 ]]; then
    if [[ $(id -u) -ne 0 ]]; then
        die "need root, or set DESTDIR, or pass --dry-run"
    fi
fi

dest() {
    printf '%s%s\n' "$DESTDIR" "$1"
}

say_run() {
    local arg
    if [[ $DRY -eq 1 ]]; then
        printf 'DRY-RUN:'
        for arg in "$@"; do
            printf ' %s' "$arg"
        done
        printf '\n'
        return 0
    fi
    "$@"
}

MARKER=$(dest /var/lib/zanjerito-logs-ram/installed)
MARKER_DIR=$(dest /var/lib/zanjerito-logs-ram)
BACKUP_ROOT=$(dest /var/backups/zanjerito-logs-ram)
SBIN=$(dest /usr/local/sbin)
UNIT_DIR=$(dest /etc/systemd/system)
DROPIN_DST=$(dest /etc/systemd/journald.conf.d/zanjerito.conf)
DROPIN_DIR=$(dest /etc/systemd/journald.conf.d)
HDD_LOG=$(dest /var/hdd.log)

restore_or_rm() {
    local path=$1
    local rel=${path#"$DESTDIR"}
    local bak=$BACKUP_ROOT$rel
    if [[ -e $bak ]]; then
        say_run install -d "$(dirname "$path")"
        say_run cp -a "$bak" "$path"
        if [[ $DRY -eq 0 ]]; then
            printf 'restored %s from backup\n' "$path"
        fi
    else
        if [[ -e $path || -L $path ]]; then
            say_run rm -f "$path"
        fi
    fi
}

if [[ -z $DESTDIR ]]; then
    say_run "$SYSTEMCTL" stop zanjerito-log-sync.timer || true
    say_run "$SYSTEMCTL" stop zanjerito-log-sync.service || true
    say_run "$SYSTEMCTL" stop zanjerito-logs-ram.service || true
    say_run "$SYSTEMCTL" disable zanjerito-log-sync.timer || true
    say_run "$SYSTEMCTL" disable zanjerito-logs-ram.service || true
    if [[ $DRY -eq 0 ]]; then
        fstype=$(findmnt -n -o FSTYPE /var/log 2>/dev/null || true)
        src=$(findmnt -n -o SOURCE /var/log 2>/dev/null || true)
        if [[ $fstype == tmpfs && $src == zanjerito-logs-ram ]]; then
            die "/var/log is still the zanjerito tmpfs; stop zanjerito-logs-ram.service failed"
        fi
        if findmnt -n /var/hdd.log >/dev/null 2>&1; then
            die "/var/hdd.log is still mounted; stop zanjerito-logs-ram.service failed"
        fi
    fi
fi

restore_or_rm "$SBIN/zanjerito-logs-ram.sh"
restore_or_rm "$SBIN/zanjerito-log-sync.sh"
restore_or_rm "$UNIT_DIR/zanjerito-logs-ram.service"
restore_or_rm "$UNIT_DIR/zanjerito-log-sync.service"
restore_or_rm "$UNIT_DIR/zanjerito-log-sync.timer"
restore_or_rm "$DROPIN_DST"
if [[ -d $DROPIN_DIR ]]; then
    say_run rmdir "$DROPIN_DIR" 2>/dev/null || true
fi

if [[ -z $DESTDIR ]]; then
    say_run "$SYSTEMCTL" daemon-reload
    say_run "$SYSTEMCTL" restart systemd-journald || true
fi

if [[ -d $HDD_LOG ]] && ! findmnt -n "$HDD_LOG" >/dev/null 2>&1; then
    say_run rmdir "$HDD_LOG" 2>/dev/null || true
fi

if [[ -e $MARKER ]]; then
    say_run rm -f "$MARKER"
fi
if [[ -d $MARKER_DIR ]]; then
    say_run rmdir "$MARKER_DIR" 2>/dev/null || true
fi
if [[ -d $BACKUP_ROOT ]]; then
    say_run rm -rf "$BACKUP_ROOT"
fi

if [[ $DRY -eq 1 ]]; then
    exit 0
fi

printf 'Uninstalled RAM /var/log. Disk /var/log is in use again.\n'
printf 'The irrigation daemon, config, and /opt/zanjerito data were not changed.\n'
if [[ -z $DESTDIR ]]; then
    printf 'journald was restarted to drop the 10M cap. A reboot is optional.\n'
fi
