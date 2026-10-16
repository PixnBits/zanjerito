#!/usr/bin/env bash
# Install RAM /var/log (16M tmpfs), journald cap, and hourly/shutdown sync.
# Does not touch the irrigation daemon, config, env, or /opt/zanjerito data.
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
    printf 'usage: install-logs-ram.sh [--dry-run]\n' >&2
    exit 2
}

die() {
    printf 'install-logs-ram: %s\n' "$1" >&2
    exit 1
}

while [[ $# -gt 0 ]]; do
    case $1 in
        --dry-run) DRY=1 ;;
        -h | --help) usage ;;
        *)
            printf 'install-logs-ram: unknown argument: %s\n' "$1" >&2
            usage
            ;;
    esac
    shift
done

DESTDIR=${DESTDIR:-}
DESTDIR=${DESTDIR%/}
SYSTEMCTL=${SYSTEMCTL:-systemctl}

SCRIPT_DIR=$(CDPATH='' cd -- "$(dirname "$0")" && pwd)

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
BACKUP_ROOT=$(dest /var/backups/zanjerito-logs-ram)
SBIN=$(dest /usr/local/sbin)
UNIT_DIR=$(dest /etc/systemd/system)
DROPIN_DIR=$(dest /etc/systemd/journald.conf.d)
HDD_LOG=$(dest /var/hdd.log)
DROPIN_DST=$DROPIN_DIR/zanjerito.conf

backup_if_original() {
    local path=$1
    local rel=${path#"$DESTDIR"}
    local bak=$BACKUP_ROOT$rel
    if [[ $DRY -eq 1 ]]; then
        if [[ -e $path && ! -e $MARKER && ! -e $bak ]]; then
            printf 'DRY-RUN: backup %s -> %s\n' "$path" "$bak"
        fi
        return 0
    fi
    if [[ -e $path && ! -e $MARKER && ! -e $bak ]]; then
        say_run install -d "$(dirname "$bak")"
        say_run cp -a "$path" "$bak"
        printf 'backed up %s -> %s\n' "$path" "$bak"
    fi
}

if [[ -z $DESTDIR ]]; then
    if [[ -e /etc/log2ram.conf || -e /usr/local/bin/log2ram || -e /etc/systemd/system/log2ram.service ]]; then
        die "log2ram is installed; remove it before zanjerito-logs-ram (both use /var/hdd.log)"
    fi
    if [[ -e /usr/lib/systemd/system/log2ram.service ]]; then
        die "log2ram is installed; remove it before zanjerito-logs-ram (both use /var/hdd.log)"
    fi
fi

for f in \
    zanjerito-logs-ram.sh \
    zanjerito-log-sync.sh \
    zanjerito-logs-ram.service \
    zanjerito-log-sync.service \
    zanjerito-log-sync.timer \
    journald-zanjerito.conf; do
    if [[ ! -f $SCRIPT_DIR/$f ]]; then
        die "missing $SCRIPT_DIR/$f"
    fi
done

backup_if_original "$SBIN/zanjerito-logs-ram.sh"
backup_if_original "$SBIN/zanjerito-log-sync.sh"
backup_if_original "$UNIT_DIR/zanjerito-logs-ram.service"
backup_if_original "$UNIT_DIR/zanjerito-log-sync.service"
backup_if_original "$UNIT_DIR/zanjerito-log-sync.timer"
backup_if_original "$DROPIN_DST"

say_run install -d "$SBIN"
say_run install -m 755 "$SCRIPT_DIR/zanjerito-logs-ram.sh" "$SBIN/zanjerito-logs-ram.sh"
say_run install -m 755 "$SCRIPT_DIR/zanjerito-log-sync.sh" "$SBIN/zanjerito-log-sync.sh"
say_run install -d "$UNIT_DIR"
say_run install -m 644 "$SCRIPT_DIR/zanjerito-logs-ram.service" "$UNIT_DIR/zanjerito-logs-ram.service"
say_run install -m 644 "$SCRIPT_DIR/zanjerito-log-sync.service" "$UNIT_DIR/zanjerito-log-sync.service"
say_run install -m 644 "$SCRIPT_DIR/zanjerito-log-sync.timer" "$UNIT_DIR/zanjerito-log-sync.timer"
say_run install -d "$DROPIN_DIR"
say_run install -m 644 "$SCRIPT_DIR/journald-zanjerito.conf" "$DROPIN_DST"
say_run install -d "$HDD_LOG"
say_run install -d "$(dirname "$MARKER")"
if [[ $DRY -eq 0 ]]; then
    date -u +%Y-%m-%dT%H:%M:%SZ >"$MARKER"
fi

if [[ -z $DESTDIR ]]; then
    say_run "$SYSTEMCTL" daemon-reload
    say_run "$SYSTEMCTL" enable zanjerito-logs-ram.service
    say_run "$SYSTEMCTL" enable zanjerito-log-sync.timer
    if [[ $DRY -eq 0 && -d /var/log ]]; then
        used_kb=$(du -sk /var/log | awk '{print $1}')
        if [[ -n $used_kb ]] && ((used_kb >= 16384)); then
            printf 'install-logs-ram: warning: /var/log is %sK (need under 16384K)\n' "$used_kb" >&2
            printf 'install-logs-ram: vacuum before reboot, e.g. journalctl --vacuum-size=8M\n' >&2
        fi
    fi
fi

if [[ $DRY -eq 1 ]]; then
    exit 0
fi

printf 'Installed RAM /var/log files.\n'
printf 'The irrigation daemon, config, and /opt/zanjerito data were not changed.\n'
if [[ -z $DESTDIR ]]; then
    printf 'Enabled zanjerito-logs-ram.service and zanjerito-log-sync.timer.\n'
    printf 'Reboot to mount the 16M tmpfs (do not start the ram unit on a live journald).\n'
    printf 'Uninstall: %s/uninstall-logs-ram.sh\n' "$SCRIPT_DIR"
else
    printf 'DESTDIR=%s (units not enabled)\n' "$DESTDIR"
fi
