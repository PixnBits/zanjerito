#!/usr/bin/env bash
# Mount /var/log as a 16M tmpfs (log2ram-style).
# start: bind SD /var/log to /var/hdd.log, tmpfs on /var/log, copy HDD -> RAM.
# stop:  sync RAM -> HDD, then unmount tmpfs and bind.
set -euo pipefail

PATH=/usr/sbin:/usr/bin:/sbin:/bin
RAM_LOG=/var/log
HDD_LOG=/var/hdd.log
SIZE=16M
SIZE_KB=16384
TMPFS_NAME=zanjerito-logs-ram
SYNC=/usr/local/sbin/zanjerito-log-sync.sh
# The EXIT trap runs after start returns. A local is unset there, and set -u
# would fail a start that succeeded.
finished=0

usage() {
    printf 'usage: zanjerito-logs-ram.sh start|stop\n' >&2
    exit 2
}

# Top mount at this path. findmnt without --mountpoint also lists the parent
# when the path is its own partition; the last line is the mount on top.
mountpoint_line() {
    findmnt --mountpoint "$1" -n -o FSTYPE,SOURCE 2>/dev/null | tail -n1 || true
}

ram_is_ours() {
    local line fstype src
    line=$(mountpoint_line "$RAM_LOG")
    [[ -n $line ]] || return 1
    read -r fstype src <<<"$line"
    [[ $fstype == tmpfs && $src == "$TMPFS_NAME" ]]
}

hdd_is_mounted() {
    findmnt --mountpoint "$HDD_LOG" -n >/dev/null 2>&1
}

copy_tree() {
    local src=$1 dst=$2
    if command -v rsync >/dev/null 2>&1; then
        rsync -aH --delete-after "$src"/ "$dst"/
    else
        cp -a "$src"/. "$dst"/
    fi
}

start() {
    if ram_is_ours && hdd_is_mounted; then
        printf 'zanjerito-logs-ram: already active\n'
        return 0
    fi
    if ram_is_ours && ! hdd_is_mounted; then
        printf 'zanjerito-logs-ram: /var/log is our tmpfs but %s is not mounted\n' "$HDD_LOG" >&2
        exit 1
    fi
    local line fstype
    line=$(mountpoint_line "$RAM_LOG")
    fstype=
    if [[ -n $line ]]; then
        read -r fstype _ <<<"$line"
    fi
    if [[ $fstype == tmpfs ]]; then
        printf 'zanjerito-logs-ram: /var/log is a tmpfs from another source; refusing\n' >&2
        exit 1
    fi

    finished=0
    cleanup() {
        if [[ $finished -eq 0 ]]; then
            # A real /var/log partition stays mounted. Only our tmpfs comes off.
            if ram_is_ours; then
                umount -l "$RAM_LOG" 2>/dev/null || true
            fi
            umount -l "$HDD_LOG" 2>/dev/null || true
        fi
    }
    trap cleanup EXIT

    mkdir -p "$HDD_LOG"
    mount --bind "$RAM_LOG" "$HDD_LOG"
    mount --make-private "$HDD_LOG"

    local used_kb
    used_kb=$(du -sk "$HDD_LOG" | awk '{print $1}')
    if [[ -z $used_kb ]] || ((used_kb >= SIZE_KB)); then
        printf 'zanjerito-logs-ram: %s is %sK, needs to be under %sK (%s tmpfs)\n' \
            "$HDD_LOG" "${used_kb:-unknown}" "$SIZE_KB" "$SIZE" >&2
        printf 'zanjerito-logs-ram: vacuum first, e.g. journalctl --vacuum-size=8M\n' >&2
        exit 1
    fi

    mount -t tmpfs -o "nosuid,noexec,noatime,nodev,mode=0755,size=${SIZE}" "$TMPFS_NAME" "$RAM_LOG"
    copy_tree "$HDD_LOG" "$RAM_LOG"
    mkdir -p "$RAM_LOG/journal"

    finished=1
    printf 'zanjerito-logs-ram: /var/log is tmpfs %s\n' "$SIZE"
}

stop() {
    local rc=0
    if ram_is_ours; then
        if [[ -x $SYNC ]]; then
            "$SYNC" || rc=$?
        else
            printf 'zanjerito-logs-ram: missing %s\n' "$SYNC" >&2
            rc=1
        fi
        umount -l "$RAM_LOG" || rc=$?
    fi
    if hdd_is_mounted; then
        umount -l "$HDD_LOG" || rc=$?
    fi
    exit "$rc"
}

case ${1-} in
    start) start ;;
    stop) stop ;;
    *) usage ;;
esac
