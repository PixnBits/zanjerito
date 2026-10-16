#!/usr/bin/env bash
# Copy RAM /var/log back onto the SD bind at /var/hdd.log.
# Prefers rsync; falls back to a staging cp -a so a failed copy leaves dest intact.
set -euo pipefail

PATH=/usr/sbin:/usr/bin:/sbin:/bin
RAM_LOG=/var/log
HDD_LOG=/var/hdd.log
TMPFS_NAME=zanjerito-logs-ram

fstype=$(findmnt -n -o FSTYPE /var/log 2>/dev/null || true)
src=$(findmnt -n -o SOURCE /var/log 2>/dev/null || true)
if [[ $fstype != tmpfs || $src != "$TMPFS_NAME" ]]; then
    printf 'zanjerito-log-sync: /var/log is not the zanjerito tmpfs; skip\n'
    exit 0
fi

if ! findmnt -n "$HDD_LOG" >/dev/null 2>&1; then
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
    rsync -aH --delete-after --inplace --no-whole-file "$RAM_LOG"/ "$HDD_LOG"/
else
    staging=$(mktemp -d "${HDD_LOG}.staging.XXXXXX")
    cleanup() { rm -rf "$staging"; }
    trap cleanup EXIT
    cp -a "$RAM_LOG"/. "$staging"/
    cp -a "$staging"/. "$HDD_LOG"/
fi

sync
printf 'zanjerito-log-sync: synced /var/log -> %s\n' "$HDD_LOG"
