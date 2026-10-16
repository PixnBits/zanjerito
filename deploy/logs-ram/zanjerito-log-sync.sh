#!/usr/bin/env bash
# Copy RAM /var/log back onto the SD bind at /var/hdd.log.
# Prefers rsync (new file, then rename). Falls back to a staging cp -a.
set -euo pipefail

PATH=/usr/sbin:/usr/bin:/sbin:/bin
RAM_LOG=/var/log
HDD_LOG=/var/hdd.log
TMPFS_NAME=zanjerito-logs-ram

# Last line is the top mount when several filesystems share the mountpoint.
mount_line=$(findmnt --mountpoint "$RAM_LOG" -n -o FSTYPE,SOURCE 2>/dev/null | tail -n1 || true)
fstype=
src=
if [[ -n $mount_line ]]; then
    read -r fstype src <<<"$mount_line"
fi
if [[ $fstype != tmpfs || $src != "$TMPFS_NAME" ]]; then
    printf 'zanjerito-log-sync: /var/log is not the zanjerito tmpfs; skip\n'
    exit 0
fi

if ! findmnt --mountpoint "$HDD_LOG" -n >/dev/null 2>&1; then
    printf 'zanjerito-log-sync: %s is not mounted; cannot sync\n' "$HDD_LOG" >&2
    exit 1
fi

if [[ ! -d $HDD_LOG ]]; then
    printf 'zanjerito-log-sync: %s is not a directory\n' "$HDD_LOG" >&2
    exit 1
fi

if command -v journalctl >/dev/null 2>&1; then
    journalctl --rotate >/dev/null 2>&1 || true
    journalctl --sync >/dev/null 2>&1 || true
fi

if command -v rsync >/dev/null 2>&1; then
    rsync -aH --delete-after "$RAM_LOG"/ "$HDD_LOG"/
else
    staging=$(mktemp -d "${HDD_LOG}.staging.XXXXXX")
    cleanup() { rm -rf "$staging"; }
    trap cleanup EXIT
    cp -a "$RAM_LOG"/. "$staging"/
    cp -a "$staging"/. "$HDD_LOG"/
fi

sync
printf 'zanjerito-log-sync: synced /var/log -> %s\n' "$HDD_LOG"
