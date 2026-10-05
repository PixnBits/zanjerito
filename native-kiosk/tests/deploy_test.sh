#!/usr/bin/env bash
# Installer, unit, and wrapper checks. No root, no real systemctl, no hardware.
set -euo pipefail

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
REPO=$(CDPATH= cd -- "$ROOT/.." && pwd)
cd "$ROOT"

INSTALL=$ROOT/deploy/install-kiosk.sh
ROLLBACK=$ROOT/deploy/rollback-desktop.sh
RUN=$ROOT/deploy/zan-kiosk-run.sh
UNIT=$ROOT/deploy/zan-kiosk.service
EXAMPLE=$ROOT/deploy/zan-kiosk.default.example
DOC=$REPO/docs/native-kiosk-boot.md

CHECKS=0
pass() { CHECKS=$((CHECKS + 1)); }

trap 'echo "deploy-check: FAILED at line $LINENO" >&2' ERR

TMP=$(mktemp -d)
cleanup() { rm -rf "$TMP"; }
trap 'echo "deploy-check: FAILED at line $LINENO" >&2' ERR
trap cleanup EXIT

STUB=$TMP/systemctl
LOG=$TMP/systemctl.log
cat >"$STUB" <<'EOF'
#!/bin/sh
if [ -z "${LOG:-}" ]; then
    echo "systemctl stub: LOG is unset" >&2
    exit 99
fi
printf '%s\n' "$*" >>"$LOG"
exit 0
EOF
chmod 755 "$STUB"

BADSTUB=$TMP/systemctl-fail
cat >"$BADSTUB" <<'EOF'
#!/bin/sh
if [ -z "${LOG:-}" ]; then
    echo "systemctl stub: LOG is unset" >&2
    exit 99
fi
printf '%s\n' "$*" >>"$LOG"
case ${1-} in
    enable) exit 1 ;;
esac
exit 0
EOF
chmod 755 "$BADSTUB"

FAKEBIN=$TMP/zan-kiosk-new
printf '#!/bin/sh\nexit 0\n' >"$FAKEBIN"
chmod 755 "$FAKEBIN"

reset_log() { : >"$LOG"; }

# --- shellcheck ---
if command -v shellcheck >/dev/null 2>&1; then
    shellcheck deploy/zan-kiosk-run.sh deploy/install-kiosk.sh deploy/rollback-desktop.sh
    echo "shellcheck: ok (local binary)"
    pass
elif command -v docker >/dev/null 2>&1 && docker info >/dev/null 2>&1; then
    docker run --rm -v "$PWD":/mnt -w /mnt koalaman/shellcheck:stable \
        deploy/zan-kiosk-run.sh deploy/install-kiosk.sh deploy/rollback-desktop.sh
    echo "shellcheck: ok (docker koalaman/shellcheck:stable)"
    pass
else
    if command -v docker >/dev/null 2>&1; then
        echo "SKIP: shellcheck not installed and docker is not usable"
    else
        echo "SKIP: shellcheck not installed and docker is not installed"
    fi
    pass
fi

# --- unit text ---
for needle in \
    'After=plymouth-quit.service network-online.target zanjerito.service' \
    'Wants=network-online.target' \
    'Conflicts=lightdm.service getty@tty1.service' \
    'StartLimitIntervalSec=0' \
    'User=pi' \
    'Group=pi' \
    'SupplementaryGroups=video input' \
    'EnvironmentFile=-/etc/default/zan-kiosk' \
    'ExecStartPre=+/opt/zanjerito/zan-kiosk-run.sh prepare' \
    'ExecStart=/opt/zanjerito/zan-kiosk-run.sh run' \
    'ExecStopPost=+/opt/zanjerito/zan-kiosk-run.sh reset-backlight' \
    'ExecStopPost=+/opt/zanjerito/zan-kiosk-run.sh restore-cursor' \
    'Restart=always' \
    'RestartSec=2' \
    'WantedBy=multi-user.target' \
    'all-off' \
    'max_on'
do
    if ! grep -F "$needle" "$UNIT" >/dev/null; then
        echo "unit missing: $needle" >&2
        exit 1
    fi
done
if grep -E '^ExecStop=' "$UNIT" >/dev/null; then
    echo "unit must not have ExecStop=" >&2
    exit 1
fi
reset_line=$(grep -n 'ExecStopPost=+/opt/zanjerito/zan-kiosk-run.sh reset-backlight' "$UNIT" | head -1 | cut -d: -f1)
restore_line=$(grep -n 'ExecStopPost=+/opt/zanjerito/zan-kiosk-run.sh restore-cursor' "$UNIT" | head -1 | cut -d: -f1)
if [ -z "$reset_line" ] || [ -z "$restore_line" ] || [ "$reset_line" -ge "$restore_line" ]; then
    echo "reset-backlight ExecStopPost must come before restore-cursor" >&2
    exit 1
fi
if grep -E '^Requires=' "$UNIT" >/dev/null; then
    echo "unit must not Requires= the daemon" >&2
    exit 1
fi
if grep -E '^(Wants|Requires)=.*plymouth' "$UNIT" >/dev/null; then
    echo "unit must not Wants= or Requires= plymouth" >&2
    exit 1
fi
after_line=$(grep -E '^After=' "$UNIT" || true)
if ! printf '%s\n' "$after_line" | grep -F 'plymouth-quit.service' >/dev/null; then
    echo "unit After= must include plymouth-quit.service" >&2
    exit 1
fi
if ! printf '%s\n' "$after_line" | grep -F 'network-online.target' >/dev/null; then
    echo "unit After= must keep network-online.target" >&2
    exit 1
fi
if ! printf '%s\n' "$after_line" | grep -F 'zanjerito.service' >/dev/null; then
    echo "unit After= must keep zanjerito.service" >&2
    exit 1
fi
if printf '%s\n' "$after_line" | grep -F 'plymouth-quit-wait' >/dev/null; then
    echo "unit must not order After=plymouth-quit-wait.service" >&2
    exit 1
fi
conflicts_n=$(grep -c -E '^Conflicts=' "$UNIT" || true)
if [ "$conflicts_n" -ne 1 ]; then
    echo "unit must keep a single Conflicts= line" >&2
    exit 1
fi
if ! grep -E '^Conflicts=lightdm.service getty@tty1.service$' "$UNIT" >/dev/null; then
    echo "unit Conflicts= must stay lightdm.service getty@tty1.service" >&2
    exit 1
fi
pass

if ! grep -F 'ZAN_API=http://CONTROLLER_HOST:8080' "$EXAMPLE" >/dev/null; then
    echo "example env missing placeholder ZAN_API" >&2
    exit 1
fi
if grep -E '^[[:space:]]*(export[[:space:]]+)?ZAN_ALLOW_WRITES=' "$EXAMPLE" >/dev/null; then
    echo "example env must not set ZAN_ALLOW_WRITES" >&2
    exit 1
fi
if ! grep -F "An existing install may already have writes on" "$EXAMPLE" >/dev/null; then
    echo "example env should say writes are on for existing installs and off here" >&2
    exit 1
fi
if ! grep -E '^ZAN_DIM_AFTER_SEC=120$' "$EXAMPLE" >/dev/null; then
    echo "example env missing active ZAN_DIM_AFTER_SEC=120" >&2
    exit 1
fi
if ! grep -E '^ZAN_OFF_AFTER_SEC=600$' "$EXAMPLE" >/dev/null; then
    echo "example env missing active ZAN_OFF_AFTER_SEC=600" >&2
    exit 1
fi
if ! grep -E '^# ZAN_DIM_LEVEL=51$' "$EXAMPLE" >/dev/null; then
    echo "example env should comment ZAN_DIM_LEVEL=51 (client default)" >&2
    exit 1
fi
if ! grep -E '^# ZAN_BACKLIGHT=/sys/class/backlight/rpi_backlight$' "$EXAMPLE" >/dev/null; then
    echo "example env should comment the default backlight path" >&2
    exit 1
fi
if ! grep -F '0 disables' "$EXAMPLE" >/dev/null; then
    echo "example env should say 0 disables dim and off" >&2
    exit 1
fi
if grep -F 'There is no idle dim' "$EXAMPLE" >/dev/null; then
    echo "example env still says there is no idle dim" >&2
    exit 1
fi
if grep -E '^[[:space:]]*(export[[:space:]]+)?ZAN_DIM_LEVEL=' "$EXAMPLE" >/dev/null; then
    echo "example env must not set ZAN_DIM_LEVEL (client default)" >&2
    exit 1
fi
if grep -E '^[[:space:]]*(export[[:space:]]+)?ZAN_BACKLIGHT=' "$EXAMPLE" >/dev/null; then
    echo "example env must not set ZAN_BACKLIGHT" >&2
    exit 1
fi
pass

# --- systemd-analyze ---
if ! command -v systemd-analyze >/dev/null 2>&1; then
    echo "SKIP: systemd-analyze not installed"
    pass
else
    sroot=$TMP/analyze-root
    mkdir -p "$sroot/opt/zanjerito" "$sroot/etc/systemd/system"
    printf '#!/bin/sh\nexit 0\n' >"$sroot/opt/zanjerito/zan-kiosk-run.sh"
    chmod 755 "$sroot/opt/zanjerito/zan-kiosk-run.sh"
    cp "$UNIT" "$sroot/etc/systemd/system/zan-kiosk.service"
    sa_out=$TMP/analyze.out
    if ! systemd-analyze verify --root="$sroot" --recursive-errors=no \
        "$sroot/etc/systemd/system/zan-kiosk.service" >"$sa_out" 2>&1; then
        echo "systemd-analyze verify failed:" >&2
        cat "$sa_out" >&2
        exit 1
    fi
    if [ -s "$sa_out" ]; then
        echo "systemd-analyze verify produced unexpected output:" >&2
        cat "$sa_out" >&2
        exit 1
    fi
    printf '\nBogusKey=1\n' >>"$sroot/etc/systemd/system/zan-kiosk.service"
    if systemd-analyze verify --root="$sroot" --recursive-errors=no \
        "$sroot/etc/systemd/system/zan-kiosk.service" >"$sa_out" 2>&1; then
        echo "systemd-analyze verify should fail on an unknown key" >&2
        exit 1
    fi
    echo "systemd-analyze verify: ok"
    pass
fi

# --- bad args (must not call systemctl) ---
reset_log
if SYSTEMCTL=$STUB LOG=$LOG "$INSTALL" --not-a-flag >/dev/null 2>&1; then
    echo "unknown arg should fail" >&2
    exit 1
fi
if [ -s "$LOG" ]; then
    echo "unknown arg called systemctl" >&2
    exit 1
fi
reset_log
if SYSTEMCTL=$STUB LOG=$LOG "$INSTALL" --now --bin "$FAKEBIN" >/dev/null 2>&1; then
    echo "--now without --switch should fail" >&2
    exit 1
fi
if [ -s "$LOG" ]; then
    echo "--now without --switch called systemctl" >&2
    exit 1
fi
reset_log
if SYSTEMCTL=$STUB LOG=$LOG "$INSTALL" --bin >/dev/null 2>&1; then
    echo "--bin without a path should fail" >&2
    exit 1
fi
if SYSTEMCTL=$STUB LOG=$LOG "$ROLLBACK" --nope >/dev/null 2>&1; then
    echo "rollback unknown arg should fail" >&2
    exit 1
fi
pass

# --- non-root, no DESTDIR: refuse before any systemctl or write ---
if [ "$(id -u)" -ne 0 ]; then
    reset_log
    stage=$TMP/root-refuse
    mkdir -p "$stage"
    if PREFIX=$stage DESTDIR= SYSTEMCTL=$STUB LOG=$LOG "$INSTALL" --bin "$FAKEBIN" >/dev/null 2>&1; then
        echo "install without DESTDIR should require root" >&2
        exit 1
    fi
    if [ -s "$LOG" ] || [ -n "$(find "$stage" -type f -print)" ]; then
        echo "root refusal still did work" >&2
        exit 1
    fi
    reset_log
    if DESTDIR= SYSTEMCTL=$STUB LOG=$LOG "$ROLLBACK" >/dev/null 2>&1; then
        echo "rollback without DESTDIR should require root" >&2
        exit 1
    fi
    if [ -s "$LOG" ]; then
        echo "rollback root refusal called systemctl" >&2
        exit 1
    fi
    pass
fi

# --- dry-run install-only: print, write nothing, no switch verbs ---
reset_log
D=$TMP/dry-install
mkdir -p "$D"
out=$(DESTDIR=$D SYSTEMCTL=$STUB LOG=$LOG "$INSTALL" --dry-run --bin "$FAKEBIN")
if [ -n "$(find "$D" -type f -print)" ]; then
    echo "dry-run install wrote files" >&2
    exit 1
fi
if [ -s "$LOG" ]; then
    echo "dry-run install called systemctl" >&2
    cat "$LOG" >&2
    exit 1
fi
printf '%s\n' "$out" | grep -F 'DRY-RUN:' >/dev/null
printf '%s\n' "$out" | grep -F 'daemon-reload' >/dev/null
printf '%s\n' "$out" | grep -F 'install -m 755' >/dev/null
printf '%s\n' "$out" | grep -F 'install -m 644' >/dev/null
if printf '%s\n' "$out" | grep -E '(^| )(set-default|disable|enable|stop|start)( |$)' >/dev/null; then
    echo "dry-run install-only printed a switch command:" >&2
    printf '%s\n' "$out" >&2
    exit 1
fi
pass

# --- dry-run --switch --now: needs a real ZAN_API already on disk ---
reset_log
D=$TMP/dry-switch
mkdir -p "$D/etc/default"
printf 'ZAN_API=http://127.0.0.1:8080\n' >"$D/etc/default/zan-kiosk"
cp "$D/etc/default/zan-kiosk" "$D/env.before"
out=$(DESTDIR=$D SYSTEMCTL=$STUB LOG=$LOG "$INSTALL" --dry-run --switch --now --bin "$FAKEBIN")
cmp "$D/etc/default/zan-kiosk" "$D/env.before"
if [ -e "$D/opt/zanjerito/zan-kiosk" ]; then
    echo "dry-run --switch wrote the binary" >&2
    exit 1
fi
if [ -s "$LOG" ]; then
    echo "dry-run --switch called systemctl" >&2
    exit 1
fi
for needle in \
    'daemon-reload' \
    'set-default multi-user.target' \
    'disable lightdm' \
    'enable zan-kiosk' \
    'stop lightdm' \
    'start zan-kiosk'
do
    if ! printf '%s\n' "$out" | grep -F "$needle" >/dev/null; then
        echo "dry-run --switch --now missing: $needle" >&2
        printf '%s\n' "$out" >&2
        exit 1
    fi
done
if printf '%s\n' "$out" | grep -F 'zanjerito.service' >/dev/null; then
    echo "dry-run --switch mentioned zanjerito.service" >&2
    exit 1
fi
pass

# --- dry-run rollback: four lines, stub not called ---
reset_log
out=$(DESTDIR=$TMP SYSTEMCTL=$STUB LOG=$LOG "$ROLLBACK" --dry-run)
if [ -s "$LOG" ]; then
    echo "dry-run rollback called systemctl" >&2
    exit 1
fi
expect=$TMP/rollback-dry.expect
{
    printf 'DRY-RUN: %s disable --now zan-kiosk\n' "$STUB"
    printf 'DRY-RUN: %s/opt/zanjerito/zan-kiosk-run.sh reset-backlight\n' "$TMP"
    printf 'DRY-RUN: %s enable lightdm\n' "$STUB"
    printf 'DRY-RUN: %s set-default graphical.target\n' "$STUB"
    printf 'DRY-RUN: %s start lightdm\n' "$STUB"
} >"$expect"
printf '%s\n' "$out" >"$TMP/rollback-dry.out"
cmp "$expect" "$TMP/rollback-dry.out"
pass

# --- real install-only into DESTDIR ---
reset_log
D=$TMP/inst
mkdir -p "$D/opt/zanjerito" "$D/etc/default"
printf 'OLD\n' >"$D/opt/zanjerito/zan-kiosk"
chmod 755 "$D/opt/zanjerito/zan-kiosk"
printf 'ZAN_API=http://127.0.0.1:9\n# marker-keep\n' >"$D/etc/default/zan-kiosk"
cp "$D/etc/default/zan-kiosk" "$D/env.before"
out=$(DESTDIR=$D SYSTEMCTL=$STUB LOG=$LOG "$INSTALL" --bin "$FAKEBIN")
printf '%s\n' "$out" | grep -F 'not enabled' >/dev/null
printf '%s\n' "$out" | grep -F 'kept existing' >/dev/null
cmp "$D/etc/default/zan-kiosk" "$D/env.before"
cmp "$FAKEBIN" "$D/opt/zanjerito/zan-kiosk"
mapfile -t baks < <(find "$D/opt/zanjerito" -maxdepth 1 -name 'zan-kiosk.bak.*' -print)
if [ "${#baks[@]}" -ne 1 ]; then
    echo "expected one binary backup, got ${#baks[@]}" >&2
    exit 1
fi
printf 'OLD\n' >"$TMP/old.expect"
cmp "$TMP/old.expect" "${baks[0]}"
[ "$(stat -c %a "$D/opt/zanjerito/zan-kiosk")" = 755 ]
[ "$(stat -c %a "$D/opt/zanjerito/zan-kiosk-run.sh")" = 755 ]
[ "$(stat -c %a "$D/opt/zanjerito/rollback-desktop.sh")" = 755 ]
[ "$(stat -c %a "$D/etc/systemd/system/zan-kiosk.service")" = 644 ]
cmp "$RUN" "$D/opt/zanjerito/zan-kiosk-run.sh"
cmp "$ROLLBACK" "$D/opt/zanjerito/rollback-desktop.sh"
cmp "$UNIT" "$D/etc/systemd/system/zan-kiosk.service"
printf 'daemon-reload\n' >"$TMP/reload.expect"
cmp "$TMP/reload.expect" "$LOG"
if grep -E 'enable|set-default|disable|stop|start|zanjerito\.service' "$LOG" >/dev/null; then
    echo "install-only systemctl log has a switch command" >&2
    cat "$LOG" >&2
    exit 1
fi
pass

# --- --switch with the placeholder refuses; log is daemon-reload only ---
reset_log
D=$TMP/refuse
if DESTDIR=$D SYSTEMCTL=$STUB LOG=$LOG "$INSTALL" --switch --bin "$FAKEBIN" >"$TMP/refuse.out" 2>"$TMP/refuse.err"; then
    echo "--switch with placeholder ZAN_API should fail" >&2
    exit 1
fi
grep -F 'refusing --switch' "$TMP/refuse.err" >/dev/null
cmp "$EXAMPLE" "$D/etc/default/zan-kiosk"
cmp "$TMP/reload.expect" "$LOG"
if grep -E 'enable|set-default|disable|stop|start|zanjerito\.service' "$LOG" >/dev/null; then
    echo "refused --switch still switched" >&2
    cat "$LOG" >&2
    exit 1
fi
pass

# --- --switch without --now, real API ---
reset_log
D=$TMP/switch
mkdir -p "$D/etc/default"
printf 'ZAN_API=http://127.0.0.1:8080\n' >"$D/etc/default/zan-kiosk"
DESTDIR=$D SYSTEMCTL=$STUB LOG=$LOG "$INSTALL" --switch --bin "$FAKEBIN" >/dev/null
{
    printf 'daemon-reload\n'
    printf 'set-default multi-user.target\n'
    printf 'disable lightdm\n'
    printf 'enable zan-kiosk\n'
} >"$TMP/switch.expect"
cmp "$TMP/switch.expect" "$LOG"
if grep -F 'zanjerito.service' "$LOG" >/dev/null; then
    echo "--switch mentioned zanjerito.service" >&2
    exit 1
fi
pass

# --- --switch --now ---
reset_log
D=$TMP/switch-now
mkdir -p "$D/etc/default"
printf 'ZAN_API=http://127.0.0.1:8080\n' >"$D/etc/default/zan-kiosk"
DESTDIR=$D SYSTEMCTL=$STUB LOG=$LOG "$INSTALL" --switch --now --bin "$FAKEBIN" >/dev/null
{
    printf 'daemon-reload\n'
    printf 'set-default multi-user.target\n'
    printf 'disable lightdm\n'
    printf 'enable zan-kiosk\n'
    printf 'stop lightdm\n'
    printf 'start zan-kiosk\n'
} >"$TMP/now.expect"
cmp "$TMP/now.expect" "$LOG"
if grep -F 'zanjerito.service' "$LOG" >/dev/null; then
    echo "--switch --now mentioned zanjerito.service" >&2
    exit 1
fi
pass

# --- rollback: exactly four calls, in order ---
reset_log
DESTDIR=$TMP SYSTEMCTL=$STUB LOG=$LOG "$ROLLBACK" >/dev/null 2>&1
{
    printf 'disable --now zan-kiosk\n'
    printf 'enable lightdm\n'
    printf 'set-default graphical.target\n'
    printf 'start lightdm\n'
} >"$TMP/rb.expect"
cmp "$TMP/rb.expect" "$LOG"
if grep -F 'zanjerito' "$LOG" >/dev/null; then
    echo "rollback mentioned zanjerito" >&2
    exit 1
fi
# idempotent: a second run appends the same four calls
DESTDIR=$TMP SYSTEMCTL=$STUB LOG=$LOG "$ROLLBACK" >/dev/null 2>&1
{
    cat "$TMP/rb.expect"
    cat "$TMP/rb.expect"
} >"$TMP/rb2.expect"
cmp "$TMP/rb2.expect" "$LOG"
pass

# --- rollback continues after a failed step and exits non-zero ---
reset_log
if DESTDIR=$TMP SYSTEMCTL=$BADSTUB LOG=$LOG "$ROLLBACK" >"$TMP/rb-fail.out" 2>"$TMP/rb-fail.err"; then
    echo "rollback should exit non-zero when a step fails" >&2
    exit 1
fi
cmp "$TMP/rb.expect" "$LOG"
grep -F 'rollback-desktop: failed:' "$TMP/rb-fail.err" >/dev/null
pass

# --- wrapper run ---
ARGV=$TMP/kiosk-argv
PPID_FILE=$TMP/ppid.txt
cat >"$ARGV" <<'EOF'
#!/bin/sh
if [ -n "${PPID_FILE:-}" ]; then
    tr '\0' ' ' <"/proc/$PPID/cmdline" >"$PPID_FILE" || true
fi
printf '%s\n' "$@"
EOF
chmod 755 "$ARGV"

run_wrap() {
    # -u first, then the caller's NAME=VALUE assignments (GNU env applies those after -u).
    env -u ZAN_API -u ZAN_ALLOW_WRITES -u ZAN_TOUCH -u ZAN_EXTRA_ARGS -u ZAN_FB -u ZAN_ROOT -u ZAN_BIN \
        -u ZAN_DIM_AFTER_SEC -u ZAN_OFF_AFTER_SEC -u ZAN_DIM_LEVEL -u ZAN_BACKLIGHT -u BACKLIGHT \
        "$@" "$RUN" run
}

out=$(run_wrap ZAN_BIN=$ARGV ZAN_API=http://127.0.0.1:8080 PPID_FILE=$PPID_FILE)
printf '%s\n' "$out" >"$TMP/argv.out"
{
    printf '%s\n' --fb /dev/fb0 --api http://127.0.0.1:8080
} >"$TMP/argv.expect"
cmp "$TMP/argv.expect" "$TMP/argv.out"
if grep -F 'zan-kiosk-run.sh' "$PPID_FILE" >/dev/null; then
    echo "run left a shell around the client" >&2
    cat "$PPID_FILE" >&2
    exit 1
fi
if printf '%s\n' "$out" | grep -F -- '--allow-writes' >/dev/null; then
    echo "allow-writes present by default" >&2
    exit 1
fi
pass

out=$(run_wrap ZAN_BIN=$ARGV ZAN_API=http://127.0.0.1:8080 ZAN_ALLOW_WRITES=1)
printf '%s\n' "$out" >"$TMP/argv.out"
{
    printf '%s\n' --fb /dev/fb0 --api http://127.0.0.1:8080 --allow-writes
} >"$TMP/argv.expect"
cmp "$TMP/argv.expect" "$TMP/argv.out"
pass

out=$(run_wrap ZAN_BIN=$ARGV ZAN_API=http://127.0.0.1:8080 ZAN_ALLOW_WRITES=0 \
    ZAN_FB=/dev/fb1 ZAN_TOUCH=/dev/input/event3 ZAN_EXTRA_ARGS='--duration 1')
printf '%s\n' "$out" >"$TMP/argv.out"
{
    printf '%s\n' --fb /dev/fb1 --api http://127.0.0.1:8080 --touch /dev/input/event3 --duration 1
} >"$TMP/argv.expect"
cmp "$TMP/argv.expect" "$TMP/argv.out"
pass

marker=$TMP/should-not-exist
rm -f "$marker"
out=$(run_wrap ZAN_BIN=$ARGV ZAN_API=http://127.0.0.1:8080 ZAN_EXTRA_ARGS="* ; touch $marker")
if [ -e "$marker" ]; then
    echo "ZAN_EXTRA_ARGS was evaluated by a shell" >&2
    exit 1
fi
printf '%s\n' "$out" >"$TMP/argv.out"
{
    printf '%s\n' --fb /dev/fb0 --api http://127.0.0.1:8080 '*' ';' touch "$marker"
} >"$TMP/argv.expect"
cmp "$TMP/argv.expect" "$TMP/argv.out"
pass

if err=$(run_wrap ZAN_BIN=$ARGV 2>&1); then
    echo "unset ZAN_API should fail" >&2
    exit 1
fi
printf '%s\n' "$err" | grep -F 'ZAN_API is not set' >/dev/null

if err=$(run_wrap ZAN_BIN=$ARGV ZAN_API=http://CONTROLLER_HOST:8080 2>&1); then
    echo "placeholder ZAN_API should fail" >&2
    exit 1
fi
printf '%s\n' "$err" | grep -F 'placeholder' >/dev/null
pass

rootbin=$TMP/rootbin
mkdir -p "$rootbin/opt/zanjerito"
cp "$ARGV" "$rootbin/opt/zanjerito/zan-kiosk"
chmod 755 "$rootbin/opt/zanjerito/zan-kiosk"
out=$(env -u ZAN_BIN -u ZAN_ALLOW_WRITES -u ZAN_DIM_AFTER_SEC -u ZAN_OFF_AFTER_SEC \
    -u ZAN_DIM_LEVEL -u ZAN_BACKLIGHT -u BACKLIGHT \
    ZAN_ROOT=$rootbin ZAN_API=http://127.0.0.1:8080 "$RUN" run)
printf '%s\n' "$out" >"$TMP/argv.out"
{
    printf '%s\n' --fb /dev/fb0 --api http://127.0.0.1:8080
} >"$TMP/argv.expect"
cmp "$TMP/argv.expect" "$TMP/argv.out"
pass

# --- prepare / restore-cursor ---
SETLOG=$TMP/setterm.log
mkdir -p "$TMP/bin" "$TMP/emptybin"
cat >"$TMP/bin/setterm" <<'EOF'
#!/bin/sh
printf '%s\n' "$*" >>"${SETLOG:?}"
printf 'SET'
exit 1
EOF
chmod 755 "$TMP/bin/setterm"

fake=$TMP/fake-root
mkdir -p "$fake/sys/class/graphics/fbcon" "$fake/sys/class/backlight/rpi_backlight" "$fake/dev"
printf '1\n' >"$fake/sys/class/graphics/fbcon/cursor_blink"
printf '9\n' >"$fake/sys/class/backlight/rpi_backlight/brightness"
: >"$fake/dev/tty1"
: >"$SETLOG"
ZAN_ROOT=$fake BACKLIGHT=128 PATH="$TMP/bin" SETLOG=$SETLOG "$RUN" prepare >/dev/null 2>&1
[ "$(tr -d '\n' <"$fake/sys/class/graphics/fbcon/cursor_blink")" = 0 ]
[ "$(tr -d '\n' <"$fake/sys/class/backlight/rpi_backlight/brightness")" = 128 ]
grep -F -- '--blank 0 --powerdown 0 --cursor off' "$SETLOG" >/dev/null
grep -F 'SET' "$fake/dev/tty1" >/dev/null
pass

# escapes survive when setterm is absent (a regular file would be truncated by setterm's redirect)
fake2=$TMP/fake-esc
mkdir -p "$fake2/dev"
: >"$fake2/dev/tty1"
PATH="$TMP/emptybin" ZAN_ROOT=$fake2 "$RUN" prepare >/dev/null 2>&1
hide=$(printf '\033[?25l')
blank=$(printf '\033[9;0]')
grep -a -F "$hide" "$fake2/dev/tty1" >/dev/null
grep -a -F "$blank" "$fake2/dev/tty1" >/dev/null
pass

# invalid backlight is ignored; missing sysfs still succeeds
printf '7\n' >"$fake/sys/class/backlight/rpi_backlight/brightness"
PATH="$TMP/emptybin" ZAN_ROOT=$fake BACKLIGHT=999 "$RUN" prepare >/dev/null 2>&1
[ "$(tr -d '\n' <"$fake/sys/class/backlight/rpi_backlight/brightness")" = 7 ]
PATH="$TMP/emptybin" ZAN_ROOT=$fake BACKLIGHT=abc "$RUN" prepare >/dev/null 2>&1
[ "$(tr -d '\n' <"$fake/sys/class/backlight/rpi_backlight/brightness")" = 7 ]
PATH="$TMP/emptybin" ZAN_ROOT=$fake BACKLIGHT=0 "$RUN" prepare >/dev/null 2>&1
[ "$(tr -d '\n' <"$fake/sys/class/backlight/rpi_backlight/brightness")" = 0 ]

empty=$TMP/fake-empty
mkdir -p "$empty"
PATH="$TMP/emptybin" ZAN_ROOT=$empty BACKLIGHT=10 "$RUN" prepare >/dev/null 2>&1
pass

printf 'x' >"$fake2/dev/tty1"
ZAN_ROOT=$fake2 "$RUN" restore-cursor >/dev/null 2>&1
show=$(printf '\033[?25h')
grep -a -F "$show" "$fake2/dev/tty1" >/dev/null
ZAN_ROOT=$empty "$RUN" restore-cursor >/dev/null 2>&1
if "$RUN" nosuch >/dev/null 2>&1; then
    echo "unknown subcommand should fail" >&2
    exit 1
fi
pass

# prepare: chgrp/chmod on the configured nodes, and max brightness when BACKLIGHT is unset.
# Stubs stand in for chgrp and chmod so this does not need root and does not touch real sysfs.
CHLOG=$TMP/chgrp-chmod.log
cat >"$TMP/bin/chgrp" <<'EOF'
#!/bin/sh
printf '%s\n' "chgrp $*" >>"${CHLOG:?}"
exit 0
EOF
cat >"$TMP/bin/chmod" <<'EOF'
#!/bin/sh
printf '%s\n' "chmod $*" >>"${CHLOG:?}"
exit 0
EOF
chmod 755 "$TMP/bin/chgrp" "$TMP/bin/chmod"

acc=$TMP/fake-access
mkdir -p "$acc/sys/class/backlight/rpi_backlight" "$acc/dev"
printf '11\n' >"$acc/sys/class/backlight/rpi_backlight/brightness"
printf '300\n' >"$acc/sys/class/backlight/rpi_backlight/max_brightness"
printf '1\n' >"$acc/sys/class/backlight/rpi_backlight/bl_power"
: >"$CHLOG"
env -u BACKLIGHT PATH="$TMP/bin:$PATH" CHLOG=$CHLOG ZAN_ROOT=$acc \
    ZAN_BACKLIGHT=/sys/class/backlight/rpi_backlight \
    "$RUN" prepare >/dev/null 2>&1
[ "$(tr -d '[:space:]' <"$acc/sys/class/backlight/rpi_backlight/brightness")" = 300 ]
grep -F "chgrp video $acc/sys/class/backlight/rpi_backlight/brightness" "$CHLOG" >/dev/null
grep -F "chgrp video $acc/sys/class/backlight/rpi_backlight/bl_power" "$CHLOG" >/dev/null
grep -F "chmod g+w $acc/sys/class/backlight/rpi_backlight/brightness" "$CHLOG" >/dev/null
grep -F "chmod g+w $acc/sys/class/backlight/rpi_backlight/bl_power" "$CHLOG" >/dev/null
pass

# BACKLIGHT set writes the fixed level on the configured directory only.
printf '11\n' >"$acc/sys/class/backlight/rpi_backlight/brightness"
: >"$CHLOG"
PATH="$TMP/bin:$PATH" CHLOG=$CHLOG ZAN_ROOT=$acc BACKLIGHT=128 \
    ZAN_BACKLIGHT=/sys/class/backlight/rpi_backlight \
    "$RUN" prepare >/dev/null 2>&1
[ "$(tr -d '[:space:]' <"$acc/sys/class/backlight/rpi_backlight/brightness")" = 128 ]
grep -F "chmod g+w $acc/sys/class/backlight/rpi_backlight/brightness" "$CHLOG" >/dev/null
pass

# Two backlight devices: BACKLIGHT touches only the configured directory.
two=$TMP/fake-two
mkdir -p "$two/sys/class/backlight/rpi_backlight" \
    "$two/sys/class/backlight/other_backlight" "$two/dev"
printf '11\n' >"$two/sys/class/backlight/rpi_backlight/brightness"
printf '22\n' >"$two/sys/class/backlight/other_backlight/brightness"
printf '1\n' >"$two/sys/class/backlight/rpi_backlight/bl_power"
printf '1\n' >"$two/sys/class/backlight/other_backlight/bl_power"
printf '300\n' >"$two/sys/class/backlight/rpi_backlight/max_brightness"
printf '111\n' >"$two/sys/class/backlight/other_backlight/max_brightness"
: >"$CHLOG"
env -u ZAN_BACKLIGHT PATH="$TMP/bin:$PATH" CHLOG=$CHLOG ZAN_ROOT=$two BACKLIGHT=128 \
    "$RUN" prepare >/dev/null 2>&1
[ "$(tr -d '[:space:]' <"$two/sys/class/backlight/rpi_backlight/brightness")" = 128 ]
[ "$(tr -d '[:space:]' <"$two/sys/class/backlight/rpi_backlight/bl_power")" = 0 ]
[ "$(tr -d '[:space:]' <"$two/sys/class/backlight/other_backlight/brightness")" = 22 ]
[ "$(tr -d '[:space:]' <"$two/sys/class/backlight/other_backlight/bl_power")" = 1 ]
grep -F "chgrp video $two/sys/class/backlight/rpi_backlight/brightness" "$CHLOG" >/dev/null
grep -F "chmod g+w $two/sys/class/backlight/rpi_backlight/brightness" "$CHLOG" >/dev/null
grep -F "chmod g+w $two/sys/class/backlight/rpi_backlight/bl_power" "$CHLOG" >/dev/null
if grep -F other_backlight "$CHLOG" >/dev/null; then
    echo "prepare touched other_backlight" >&2
    exit 1
fi
pass

# BACKLIGHT unset: still bl_power 0 on the default device only.
printf '11\n' >"$two/sys/class/backlight/rpi_backlight/brightness"
printf '22\n' >"$two/sys/class/backlight/other_backlight/brightness"
printf '1\n' >"$two/sys/class/backlight/rpi_backlight/bl_power"
printf '1\n' >"$two/sys/class/backlight/other_backlight/bl_power"
: >"$CHLOG"
env -u BACKLIGHT -u ZAN_BACKLIGHT PATH="$TMP/bin:$PATH" CHLOG=$CHLOG ZAN_ROOT=$two \
    "$RUN" prepare >/dev/null 2>&1
[ "$(tr -d '[:space:]' <"$two/sys/class/backlight/rpi_backlight/brightness")" = 300 ]
[ "$(tr -d '[:space:]' <"$two/sys/class/backlight/rpi_backlight/bl_power")" = 0 ]
[ "$(tr -d '[:space:]' <"$two/sys/class/backlight/other_backlight/brightness")" = 22 ]
[ "$(tr -d '[:space:]' <"$two/sys/class/backlight/other_backlight/bl_power")" = 1 ]
if grep -F other_backlight "$CHLOG" >/dev/null; then
    echo "prepare touched other_backlight with BACKLIGHT unset" >&2
    exit 1
fi
pass

printf '11\n' >"$two/sys/class/backlight/rpi_backlight/brightness"
printf '22\n' >"$two/sys/class/backlight/other_backlight/brightness"
printf '1\n' >"$two/sys/class/backlight/rpi_backlight/bl_power"
printf '1\n' >"$two/sys/class/backlight/other_backlight/bl_power"
: >"$CHLOG"
PATH="$TMP/bin:$PATH" CHLOG=$CHLOG ZAN_ROOT=$two BACKLIGHT=64 \
    ZAN_BACKLIGHT=/sys/class/backlight/other_backlight \
    "$RUN" prepare >/dev/null 2>&1
[ "$(tr -d '[:space:]' <"$two/sys/class/backlight/other_backlight/brightness")" = 64 ]
[ "$(tr -d '[:space:]' <"$two/sys/class/backlight/other_backlight/bl_power")" = 0 ]
[ "$(tr -d '[:space:]' <"$two/sys/class/backlight/rpi_backlight/brightness")" = 11 ]
[ "$(tr -d '[:space:]' <"$two/sys/class/backlight/rpi_backlight/bl_power")" = 1 ]
grep -F "chmod g+w $two/sys/class/backlight/other_backlight/brightness" "$CHLOG" >/dev/null
grep -F "chgrp video $two/sys/class/backlight/other_backlight/bl_power" "$CHLOG" >/dev/null
if grep -F rpi_backlight "$CHLOG" >/dev/null; then
    echo "prepare touched rpi_backlight while ZAN_BACKLIGHT is the other device" >&2
    exit 1
fi
pass

# Missing bl_power is a quiet skip; prepare still exits 0.
nopow=$TMP/fake-nopow
mkdir -p "$nopow/sys/class/backlight/rpi_backlight" "$nopow/dev"
printf '11\n' >"$nopow/sys/class/backlight/rpi_backlight/brightness"
printf '255\n' >"$nopow/sys/class/backlight/rpi_backlight/max_brightness"
env -u BACKLIGHT -u ZAN_BACKLIGHT PATH="$TMP/bin:$PATH" CHLOG=$CHLOG ZAN_ROOT=$nopow \
    "$RUN" prepare >/dev/null 2>&1
[ "$(tr -d '[:space:]' <"$nopow/sys/class/backlight/rpi_backlight/brightness")" = 255 ]
[ ! -e "$nopow/sys/class/backlight/rpi_backlight/bl_power" ]
pass

# bl_power as a directory, or an unwritable file, is non-fatal.
badbl=$TMP/fake-badbl
mkdir -p "$badbl/sys/class/backlight/rpi_backlight/bl_power" "$badbl/dev"
printf '11\n' >"$badbl/sys/class/backlight/rpi_backlight/brightness"
printf '255\n' >"$badbl/sys/class/backlight/rpi_backlight/max_brightness"
env -u BACKLIGHT -u ZAN_BACKLIGHT PATH="$TMP/bin:$PATH" CHLOG=$CHLOG ZAN_ROOT=$badbl \
    "$RUN" prepare >/dev/null 2>&1
[ "$(tr -d '[:space:]' <"$badbl/sys/class/backlight/rpi_backlight/brightness")" = 255 ]
[ -d "$badbl/sys/class/backlight/rpi_backlight/bl_power" ]
if [ "$(id -u)" != 0 ]; then
    rmdir "$badbl/sys/class/backlight/rpi_backlight/bl_power"
    printf '1\n' >"$badbl/sys/class/backlight/rpi_backlight/bl_power"
    chmod a-w "$badbl/sys/class/backlight/rpi_backlight/bl_power"
    printf '11\n' >"$badbl/sys/class/backlight/rpi_backlight/brightness"
    env -u BACKLIGHT -u ZAN_BACKLIGHT PATH="$TMP/bin:$PATH" CHLOG=$CHLOG ZAN_ROOT=$badbl \
        "$RUN" prepare >/dev/null 2>&1
    [ "$(tr -d '[:space:]' <"$badbl/sys/class/backlight/rpi_backlight/brightness")" = 255 ]
    [ "$(tr -d '[:space:]' <"$badbl/sys/class/backlight/rpi_backlight/bl_power")" = 1 ]
    chmod u+w "$badbl/sys/class/backlight/rpi_backlight/bl_power"
fi
pass

# reset-backlight uses that same directory, not every device.
printf '3\n' >"$two/sys/class/backlight/rpi_backlight/brightness"
printf '9\n' >"$two/sys/class/backlight/other_backlight/brightness"
printf '1\n' >"$two/sys/class/backlight/rpi_backlight/bl_power"
printf '1\n' >"$two/sys/class/backlight/other_backlight/bl_power"
env -u ZAN_BACKLIGHT -u BACKLIGHT ZAN_ROOT=$two "$RUN" reset-backlight >/dev/null 2>&1
[ "$(tr -d '[:space:]' <"$two/sys/class/backlight/rpi_backlight/brightness")" = 300 ]
[ "$(tr -d '[:space:]' <"$two/sys/class/backlight/rpi_backlight/bl_power")" = 0 ]
[ "$(tr -d '[:space:]' <"$two/sys/class/backlight/other_backlight/brightness")" = 9 ]
[ "$(tr -d '[:space:]' <"$two/sys/class/backlight/other_backlight/bl_power")" = 1 ]
printf '44\n' >"$two/sys/class/backlight/rpi_backlight/brightness"
printf '1\n' >"$two/sys/class/backlight/rpi_backlight/bl_power"
printf '9\n' >"$two/sys/class/backlight/other_backlight/brightness"
printf '1\n' >"$two/sys/class/backlight/other_backlight/bl_power"
env -u BACKLIGHT ZAN_ROOT=$two ZAN_BACKLIGHT=/sys/class/backlight/other_backlight \
    "$RUN" reset-backlight >/dev/null 2>&1
[ "$(tr -d '[:space:]' <"$two/sys/class/backlight/other_backlight/brightness")" = 111 ]
[ "$(tr -d '[:space:]' <"$two/sys/class/backlight/other_backlight/bl_power")" = 0 ]
[ "$(tr -d '[:space:]' <"$two/sys/class/backlight/rpi_backlight/brightness")" = 44 ]
[ "$(tr -d '[:space:]' <"$two/sys/class/backlight/rpi_backlight/bl_power")" = 1 ]
pass

# A missing backlight directory is a quiet skip.
miss=$TMP/fake-miss
mkdir -p "$miss"
env -u BACKLIGHT -u ZAN_BACKLIGHT PATH="$TMP/bin:$PATH" ZAN_ROOT=$miss "$RUN" prepare >/dev/null 2>&1
pass

# reset-backlight writes the raw max (may be above 255) and bl_power 0. Missing files still exit 0.
printf '3\n' >"$acc/sys/class/backlight/rpi_backlight/brightness"
printf '1\n' >"$acc/sys/class/backlight/rpi_backlight/bl_power"
env -u BACKLIGHT ZAN_ROOT=$acc ZAN_BACKLIGHT=/sys/class/backlight/rpi_backlight \
    "$RUN" reset-backlight >/dev/null 2>&1
[ "$(tr -d '[:space:]' <"$acc/sys/class/backlight/rpi_backlight/brightness")" = 300 ]
[ "$(tr -d '[:space:]' <"$acc/sys/class/backlight/rpi_backlight/bl_power")" = 0 ]
env -u BACKLIGHT -u ZAN_BACKLIGHT ZAN_ROOT=$miss "$RUN" reset-backlight >/dev/null 2>&1
pass

# Invalid dim/off/level values are skipped. Valid siblings are still passed, in order.
# 0 is a real value and must be passed through (it disables dim and off in the client).
out=$(run_wrap ZAN_BIN=$ARGV ZAN_API=http://127.0.0.1:8080 \
    ZAN_TOUCH=/dev/input/event3 \
    ZAN_DIM_AFTER_SEC=2 ZAN_OFF_AFTER_SEC=9 ZAN_DIM_LEVEL=17 \
    ZAN_BACKLIGHT=/sys/class/backlight/panel \
    ZAN_EXTRA_ARGS='--duration 1')
printf '%s\n' "$out" >"$TMP/argv.out"
{
    printf '%s\n' --fb /dev/fb0 --api http://127.0.0.1:8080 \
        --touch /dev/input/event3 \
        --dim-after 2 --off-after 9 --dim-level 17 \
        --backlight /sys/class/backlight/panel \
        --duration 1
} >"$TMP/argv.expect"
cmp "$TMP/argv.expect" "$TMP/argv.out"
pass

err=$(run_wrap ZAN_BIN=$ARGV ZAN_API=http://127.0.0.1:8080 \
    ZAN_DIM_AFTER_SEC=nope ZAN_OFF_AFTER_SEC=12 ZAN_DIM_LEVEL=999 \
    ZAN_BACKLIGHT=/sys/class/backlight/rpi_backlight 2>&1 >/dev/null || true)
printf '%s\n' "$err" | grep -F 'skipped --dim-after' >/dev/null
printf '%s\n' "$err" | grep -F 'skipped --dim-level' >/dev/null
out=$(run_wrap ZAN_BIN=$ARGV ZAN_API=http://127.0.0.1:8080 \
    ZAN_DIM_AFTER_SEC=nope ZAN_OFF_AFTER_SEC=12 ZAN_DIM_LEVEL=999 \
    ZAN_BACKLIGHT=/sys/class/backlight/rpi_backlight 2>/dev/null)
printf '%s\n' "$out" >"$TMP/argv.out"
{
    printf '%s\n' --fb /dev/fb0 --api http://127.0.0.1:8080 \
        --off-after 12 \
        --backlight /sys/class/backlight/rpi_backlight
} >"$TMP/argv.expect"
cmp "$TMP/argv.expect" "$TMP/argv.out"
pass

out=$(run_wrap ZAN_BIN=$ARGV ZAN_API=http://127.0.0.1:8080 ZAN_DIM_AFTER_SEC=0 ZAN_OFF_AFTER_SEC=0)
printf '%s\n' "$out" >"$TMP/argv.out"
{
    printf '%s\n' --fb /dev/fb0 --api http://127.0.0.1:8080 --dim-after 0 --off-after 0
} >"$TMP/argv.expect"
cmp "$TMP/argv.expect" "$TMP/argv.out"
pass

# rollback runs reset-backlight against DESTDIR and still only calls systemctl four times.
reset_log
rb=$TMP/rollback-root
mkdir -p "$rb/opt/zanjerito" "$rb/sys/class/backlight/rpi_backlight"
cp "$RUN" "$rb/opt/zanjerito/zan-kiosk-run.sh"
chmod 755 "$rb/opt/zanjerito/zan-kiosk-run.sh"
printf '4\n' >"$rb/sys/class/backlight/rpi_backlight/brightness"
printf '400\n' >"$rb/sys/class/backlight/rpi_backlight/max_brightness"
printf '1\n' >"$rb/sys/class/backlight/rpi_backlight/bl_power"
DESTDIR=$rb SYSTEMCTL=$STUB LOG=$LOG "$ROLLBACK" >/dev/null 2>&1
cmp "$TMP/rb.expect" "$LOG"
[ "$(tr -d '[:space:]' <"$rb/sys/class/backlight/rpi_backlight/brightness")" = 400 ]
[ "$(tr -d '[:space:]' <"$rb/sys/class/backlight/rpi_backlight/bl_power")" = 0 ]
pass

# --- no real addresses or this machine's names in the shipped text ---
scan=("$ROOT/deploy" "$DOC")
if grep -RInE '[0-9]{1,3}(\.[0-9]{1,3}){3}' "${scan[@]}" | grep -v '127\.0\.0\.1'; then
    echo "found an IPv4 literal other than 127.0.0.1" >&2
    exit 1
fi
host=$(hostname 2>/dev/null || true)
short=$(hostname -s 2>/dev/null || true)
home_user=$(basename "$HOME")
for name in "$host" "$short"; do
    if [ -n "$name" ] && grep -RInF "$name" "${scan[@]}" >/dev/null; then
        echo "found hostname $name in deploy or boot doc" >&2
        exit 1
    fi
done
case $home_user in
    ''|pi|root) ;;
    *)
        if grep -RInF "$home_user" "${scan[@]}" >/dev/null; then
            echo "found home directory name in deploy or boot doc" >&2
            exit 1
        fi
        ;;
esac
pass

# --- docs still say the things operators need ---
if [ ! -f "$DOC" ]; then
    echo "missing $DOC" >&2
    exit 1
fi
for needle in \
    'sudo /opt/zanjerito/rollback-desktop.sh' \
    'sudo systemctl disable --now zan-kiosk && sudo systemctl enable lightdm && sudo systemctl set-default graphical.target && sudo systemctl start lightdm' \
    'sudo systemctl stop zan-kiosk && sudo systemctl start lightdm' \
    'dmesg -n 1' \
    'KD_GRAPHICS' \
    'cursor_blink' \
    'config.txt' \
    'cmdline' \
    'max_on' \
    'CONTROLLER_HOST' \
    'ZAN_ALLOW_WRITES' \
    'make -C native-kiosk arm' \
    'plymouth-quit.service' \
    'plymouth-quit-wait.service'
do
    if ! grep -F "$needle" "$DOC" >/dev/null; then
        echo "boot doc missing: $needle" >&2
        exit 1
    fi
done
if ! grep -F '## Boot-persistent kiosk (full kiosk mode)' "$ROOT/README.md" >/dev/null; then
    echo "native-kiosk README missing the boot section" >&2
    exit 1
fi
if ! grep -F 'docs/native-kiosk-boot.md' "$REPO/README.md" >/dev/null; then
    echo "root README missing the boot doc link" >&2
    exit 1
fi
if ! grep -F 'deploy-check' "$ROOT/Makefile" >/dev/null; then
    echo "Makefile missing deploy-check" >&2
    exit 1
fi
pass

trap - ERR
echo "deploy-check: ok ($CHECKS checks)"
