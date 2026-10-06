#!/usr/bin/env bash
# Headless taps against tools/stub_server.py. Needs build/zan-kiosk-host and python3.
# Cases:
#   a  read-only STOP+OK sends nothing
#   b  STOP+OK posts cancel; a later Cancel does not
#   c  pause for days, and pause until tomorrow morning
#   d  resume posts only with --allow-writes
#   e  confirm-STOP ignores a tap on the rail
#   f  unreachable API shows the stale pill and exits 0
#   g  no API exits 2
#   h  one pause POST; stray taps after OK do not add another
#   i  a second STOP while the first POST is held does not double-post
#   j  cancel POST is not stuck behind a slow GET
#   k  tile tap opens the station sheet and does not POST
#   l  Schedules button, then Close
#   m  STOP from the station sheet posts one cancel
#   n  needs-update card; STOP still posts
#   o  idle dim, then off; a tile tap while off wakes and does not open the sheet
#   p  a dimmed tile tap is swallowed; dimmed STOP opens confirm on that first tap
#   q  a running fixture never dims or blanks
#   r  a fault fixture never dims; a paused fixture dims and never blanks
#   s  SIGTERM restores the saved brightness
#   t  SIGINT restores the saved brightness
#   u  16 bpp RGB565 fake fb matches the memory display
#   v  32 bpp XRGB8888 fake fb matches the memory display
#   w  24 bpp is rejected (exit 78) and the fake fb stays zeros
#   x  all-black --fbshot exits 3
#   y  --fbshot without --fb exits 2
#   z  padded stride keeps the sentinel and matches an unpadded fbshot
#   aa bottom rows 474..479 are background, not the old red step strip
set -euo pipefail

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"

BIN=$ROOT/build/zan-kiosk-host
STUB=$ROOT/tools/stub_server.py
FIX=$ROOT/tests/fixtures

# Centers from zk_layout.c (integer division). test_layout checks STOP height
# 264 (304 while running), chip size 255x188, and each target's center.
# HOME STOP (550,12 236x264) -> (668,144)
# HOME slot y=292 h=168 -> (668,376)  Pause, and Resume on the paused screen
# Confirm OK (120,240 270x130) -> (255,305); Cancel (410,240) -> (545,305)
# Picker chips at (14,72), 255x188, gap 12:
#   chip 0 tomorrow morning (141,166); chip 1 "2 days" (408,166)
STOP_X=668
STOP_Y=144
SLOT_X=668
SLOT_Y=376
# Home tile 0 with rain: (14,198 256x126) -> (142,261)
TILE0_X=142
TILE0_Y=261
# Schedules button (416,12 120x108) -> (476,66)
SCHED_X=476
SCHED_Y=66
OK_X=255
OK_Y=305
CANCEL_X=545
CANCEL_Y=305
CHIP_TOMORROW_X=141
CHIP_TOMORROW_Y=166
CHIP_DAYS2_X=408
CHIP_DAYS2_Y=166
# Chip 2 "1 week": (14, 272) 255x188 -> center (141, 366).
# (255, 305) is inside that chip while the picker is still up.
CHIP_WEEK_X=141
CHIP_WEEK_Y=366

FAILS=0
STUB_PID=
KIOSK_PID=
SAMPLER_PID=
PORT=
LOG=
TMP=

stop_kiosk() {
    if [[ -n "${KIOSK_PID}" ]]; then
        kill "$KIOSK_PID" 2>/dev/null || true
        wait "$KIOSK_PID" 2>/dev/null || true
        KIOSK_PID=
    fi
}

stop_sampler() {
    if [[ -n "${SAMPLER_PID}" ]]; then
        kill "$SAMPLER_PID" 2>/dev/null || true
        wait "$SAMPLER_PID" 2>/dev/null || true
        SAMPLER_PID=
    fi
}

cleanup() {
    stop_sampler
    stop_kiosk
    if [[ -n "${STUB_PID}" ]]; then
        kill "$STUB_PID" 2>/dev/null || true
        wait "$STUB_PID" 2>/dev/null || true
        STUB_PID=
    fi
    if [[ -n "${TMP}" && -d "${TMP}" ]]; then
        rm -rf "$TMP"
    fi
}
trap cleanup EXIT

if [[ ! -x "$BIN" ]]; then
    echo "e2e: missing $BIN (build the host first)" >&2
    exit 1
fi
if ! command -v python3 >/dev/null 2>&1; then
    echo "e2e: python3 is required" >&2
    exit 1
fi
if ! command -v timeout >/dev/null 2>&1; then
    echo "e2e: timeout is required" >&2
    exit 1
fi

TMP=$(mktemp -d)
mkdir -p "$TMP/empty-sysfs-backlight"
: >"$TMP/zk-tty"
# TEST-ONLY: never scan the machine's real /sys/class/backlight.
export ZAN_SYSFS_BACKLIGHT_ROOT="$TMP/empty-sysfs-backlight"

cat >"$TMP/png.py" <<'PY'
import struct, sys, zlib

def load(path):
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise SystemExit("not a png: %s" % path)
    pos = 8
    w = h = None
    color = None
    raw = b""
    while pos + 8 <= len(data):
        ln = struct.unpack(">I", data[pos:pos + 4])[0]
        typ = data[pos + 4:pos + 8]
        chunk = data[pos + 8:pos + 8 + ln]
        pos += 12 + ln
        if typ == b"IHDR":
            w, h, bit, color, comp, filt, inter = struct.unpack(">IIBBBBB", chunk)
            if bit != 8 or color != 2 or inter != 0:
                raise SystemExit("unexpected png format %s" % path)
        elif typ == b"IDAT":
            raw += chunk
        elif typ == b"IEND":
            break
    if w is None:
        raise SystemExit("no IHDR")
    decomp = zlib.decompress(raw)
    stride = w * 3
    rows = []
    i = 0
    prev = bytearray(stride)
    for y in range(h):
        filt = decomp[i]
        i += 1
        row = bytearray(decomp[i:i + stride])
        i += stride
        if filt == 1:
            for x in range(stride):
                a = row[x - 3] if x >= 3 else 0
                row[x] = (row[x] + a) & 255
        elif filt == 2:
            for x in range(stride):
                row[x] = (row[x] + prev[x]) & 255
        elif filt == 3:
            for x in range(stride):
                a = row[x - 3] if x >= 3 else 0
                row[x] = (row[x] + ((a + prev[x]) // 2)) & 255
        elif filt == 4:
            for x in range(stride):
                a = row[x - 3] if x >= 3 else 0
                b = prev[x]
                c = prev[x - 3] if x >= 3 else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                row[x] = (row[x] + pr) & 255
        elif filt != 0:
            raise SystemExit("bad filter %d" % filt)
        rows.append(row)
        prev = row
    return w, h, rows

def pix(rows, x, y):
    i = x * 3
    row = rows[y]
    return row[i], row[i + 1], row[i + 2]

def near(rgb, target, tol):
    return all(abs(a - b) <= tol for a, b in zip(rgb, target))

def main():
    cmd, path = sys.argv[1], sys.argv[2]
    w, h, rows = load(path)
    if cmd == "size":
        print("%dx%d" % (w, h))
        return
    if cmd == "sha":
        import hashlib
        print(hashlib.sha256(open(path, "rb").read()).hexdigest())
        return
    # Stale pill fill is #F6D5CC. Count matches in the lower band.
    if cmd == "stale":
        n = 0
        y1 = min(h, 470)
        x1 = min(w, 520)
        for y in range(400, y1):
            for x in range(20, x1):
                if near(pix(rows, x, y), (0xF6, 0xD5, 0xCC), 8):
                    n += 1
        print(n)
        return
    # Confirm-STOP OK button is #B5361A, above the label.
    if cmd == "stopok":
        rgb = pix(rows, 200, 260)
        print("%d %d %d" % rgb)
        return
    # Needs-update card is cream #FCF5E6 in the main column.
    if cmd == "needsupdate":
        n = 0
        for y in range(40, 200):
            for x in range(30, 500):
                if near(pix(rows, x, y), (0xFC, 0xF5, 0xE6), 8):
                    n += 1
        print(n)
        return
    # Hold-to-edit on Schedules is dusk plum #4E3F63.
    if cmd == "schedules":
        n = 0
        for y in range(340, 450):
            for x in range(40, 520):
                if near(pix(rows, x, y), (0x4E, 0x3F, 0x63), 12):
                    n += 1
        print(n)
        return
    # Count pixels near R G B inside [x0,x1) x [y0,y1).
    # nearcount PATH X0 X1 Y0 Y1 R G B TOL
    if cmd == "nearcount":
        x0, x1, y0, y1 = (int(sys.argv[i]) for i in range(3, 7))
        r, g, b, tol = (int(sys.argv[i]) for i in range(7, 11))
        n = 0
        y0 = max(0, y0)
        x0 = max(0, x0)
        y1 = min(h, y1)
        x1 = min(w, x1)
        for y in range(y0, y1):
            for x in range(x0, x1):
                if near(pix(rows, x, y), (r, g, b), tol):
                    n += 1
        print(n)
        return
    # bgmatch PATH YREF Y0 Y1 TOL
    # Every pixel on rows [Y0, Y1) matches the same x on row YREF within TOL.
    if cmd == "bgmatch":
        yref, y0, y1, tol = (int(sys.argv[i]) for i in range(3, 7))
        if yref < 0 or yref >= h or y0 < 0 or y1 > h or y0 >= y1:
            raise SystemExit("bad rows yref=%d y0=%d y1=%d h=%d" % (yref, y0, y1, h))
        bad = 0
        shown = None
        for y in range(y0, y1):
            for x in range(w):
                a = pix(rows, x, y)
                b = pix(rows, x, yref)
                if any(abs(i - j) > tol for i, j in zip(a, b)):
                    bad += 1
                    if shown is None:
                        shown = (x, y, a, b)
        if bad:
            x, y, a, b = shown
            raise SystemExit(
                "bgmatch %d pixels differ; first (%d,%d) %s vs row %d %s tol %d"
                % (bad, x, y, a, yref, b, tol))
        return
    if cmd == "pix":
        x, y = int(sys.argv[3]), int(sys.argv[4])
        if x < 0 or y < 0 or x >= w or y >= h:
            raise SystemExit("pixel out of range")
        print("%d %d %d" % pix(rows, x, y))
        return
    # close A B x y tol — exit 0 when that pixel matches within tol per channel.
    if cmd == "close":
        bpath = sys.argv[3]
        x, y, tol = int(sys.argv[4]), int(sys.argv[5]), int(sys.argv[6])
        wb, hb, rowsb = load(bpath)
        if (w, h) != (wb, hb):
            raise SystemExit("size %dx%d vs %dx%d" % (w, h, wb, hb))
        a = pix(rows, x, y)
        b = pix(rowsb, x, y)
        if any(abs(i - j) > tol for i, j in zip(a, b)):
            raise SystemExit("pixel %d,%d %s vs %s tol %d" % (x, y, a, b, tol))
        return
    if cmd == "identical":
        bpath = sys.argv[3]
        wb, hb, rowsb = load(bpath)
        if (w, h) != (wb, hb) or rows != rowsb:
            raise SystemExit("png differs")
        return
    raise SystemExit("unknown cmd")

if __name__ == "__main__":
    main()
PY

stop_stub() {
    if [[ -n "${STUB_PID}" ]]; then
        kill "$STUB_PID" 2>/dev/null || true
        wait "$STUB_PID" 2>/dev/null || true
        STUB_PID=
    fi
}

start_stub() {
    local dir=$1
    shift
    stop_stub
    LOG=$TMP/requests.log
    : >"$LOG"
    : >"${LOG}.ts"
    : >"$TMP/stub.out"
    python3 "$STUB" --dir "$dir" --log "$LOG" --port 0 "$@" >"$TMP/stub.out" &
    STUB_PID=$!
    local i port
    port=
    for i in $(seq 1 100); do
        if ! kill -0 "$STUB_PID" 2>/dev/null; then
            echo "stub exited" >&2
            cat "$TMP/stub.out" >&2 || true
            return 1
        fi
        port=$(awk '/^PORT /{print $2; exit}' "$TMP/stub.out" || true)
        if [[ -n "$port" ]]; then
            PORT=$port
            return 0
        fi
        sleep 0.05
    done
    echo "stub did not print PORT" >&2
    return 1
}

# Foreground. Caller checks $?. Extra args go before --script (for example --allow-writes).
kiosk() {
    local dur=$1
    local script=$2
    local limit=$3
    shift 3
    timeout "$limit" env -u ZAN_API -u ZK_POLL_MS_STATUS \
        -u ZK_POWER_FORCE -u ZAN_DIM_AFTER_SEC -u ZAN_OFF_AFTER_SEC \
        -u ZAN_DIM_LEVEL -u ZAN_BACKLIGHT \
        "$BIN" \
        --api "http://127.0.0.1:${PORT}" \
        "$@" \
        --script "$script" \
        --duration "$dur" \
        >"$TMP/kiosk.out" 2>"$TMP/kiosk.err"
}

show_kiosk_err() {
    echo "--- kiosk stderr ---" >&2
    cat "$TMP/kiosk.err" >&2 || true
}

post_lines() {
    if [[ ! -s "$LOG" ]]; then
        return 0
    fi
    sed -e 's/\r$//' -e 's/[[:space:]]*$//' "$LOG" | sed '/^$/d' | grep '^POST ' || true
}

assert_posts() {
    local want=$1
    local got
    got=$(post_lines)
    if [[ "$got" != "$want" ]]; then
        echo "POST log mismatch" >&2
        printf ' got: [%s]\n' "$got" >&2
        printf 'want: [%s]\n' "$want" >&2
        return 1
    fi
}

png_size() {
    python3 "$TMP/png.py" size "$1"
}

run_case() {
    local name=$1
    shift
    if "$@"; then
        printf 'PASS %s\n' "$name"
    else
        printf 'FAIL %s\n' "$name"
        FAILS=$((FAILS + 1))
    fi
}

case_a() {
    local png=$TMP/a.png
    start_stub "$FIX/home-rain" || return 1
    kiosk 4 "wait:900;tap:${STOP_X},${STOP_Y};wait:400;tap:${OK_X},${OK_Y};wait:600;shot:${png}" 12 || {
        show_kiosk_err
        return 1
    }
    assert_posts "" || return 1
    [[ -f "$png" ]] || { echo "missing png" >&2; return 1; }
    [[ "$(png_size "$png")" == "800x480" ]] || { echo "size $(png_size "$png")" >&2; return 1; }
}

case_b() {
    local png=$TMP/b.png
    start_stub "$FIX/home-rain" || return 1
    # STOP+OK posts cancel. A later STOP+Cancel must not add a POST.
    kiosk 5 \
        "wait:700;tap:${STOP_X},${STOP_Y};wait:400;tap:${OK_X},${OK_Y};wait:700;tap:${STOP_X},${STOP_Y};wait:400;tap:${CANCEL_X},${CANCEL_Y};wait:500;shot:${png}" \
        12 --allow-writes || { show_kiosk_err; return 1; }
    assert_posts "POST /api/run/cancel" || return 1
    [[ "$(png_size "$png")" == "800x480" ]] || return 1
}

case_c() {
    start_stub "$FIX/home-rain" || return 1
    kiosk 4 \
        "wait:700;tap:${SLOT_X},${SLOT_Y};wait:400;tap:${CHIP_DAYS2_X},${CHIP_DAYS2_Y};wait:400;tap:${OK_X},${OK_Y};wait:700" \
        12 --allow-writes || { show_kiosk_err; return 1; }
    assert_posts 'POST /api/pause {"days":2,"reason":"kiosk"}' || return 1

    start_stub "$FIX/home-rain" || return 1
    kiosk 4 \
        "wait:700;tap:${SLOT_X},${SLOT_Y};wait:400;tap:${CHIP_TOMORROW_X},${CHIP_TOMORROW_Y};wait:400;tap:${OK_X},${OK_Y};wait:700" \
        12 --allow-writes || { show_kiosk_err; return 1; }
    assert_posts 'POST /api/pause {"until":"tomorrow_morning","reason":"kiosk"}' || return 1
}

case_d() {
    start_stub "$FIX/paused-rain" || return 1
    kiosk 3 "wait:900;tap:${SLOT_X},${SLOT_Y};wait:700" 12 --allow-writes || { show_kiosk_err; return 1; }
    assert_posts "POST /api/pause/resume" || return 1

    start_stub "$FIX/paused-rain" || return 1
    kiosk 3 "wait:900;tap:${SLOT_X},${SLOT_Y};wait:700" 12 || { show_kiosk_err; return 1; }
    assert_posts "" || return 1
}

case_e() {
    local a=$TMP/e1.png b=$TMP/e2.png
    local c1 c2
    start_stub "$FIX/home-rain" || return 1
    # Second shot is after a tap on the rail Pause point while confirm-STOP is up.
    kiosk 5 \
        "wait:900;tap:${STOP_X},${STOP_Y};wait:500;shot:${a};tap:${SLOT_X},${SLOT_Y};wait:500;shot:${b}" \
        12 --allow-writes || { show_kiosk_err; return 1; }
    assert_posts "" || return 1
    [[ -f "$a" && -f "$b" ]] || { echo "missing modal shots" >&2; return 1; }
    c1=$(python3 "$TMP/png.py" stopok "$a")
    c2=$(python3 "$TMP/png.py" stopok "$b")
    # OK button fill, above the STOP label. Home at this point is not this color.
    if [[ "$c1" != "181 54 26" || "$c2" != "181 54 26" ]]; then
        echo "confirm STOP not held open (pixels $c1 / $c2)" >&2
        return 1
    fi
    if ! cmp -s "$a" "$b"; then
        echo "modal shots differ" >&2
        return 1
    fi
}

case_f() {
    local healthy=$TMP/healthy.png offline=$TMP/offline.png
    local closed rc stale_h stale_o
    start_stub "$FIX/home-rain" || return 1
    kiosk 3 "wait:1200;shot:${healthy}" 12 || { show_kiosk_err; return 1; }
    [[ "$(png_size "$healthy")" == "800x480" ]] || return 1
    stop_stub
    closed=$(python3 -c 'import socket; s=socket.socket(); s.bind(("127.0.0.1", 0)); print(s.getsockname()[1]); s.close()')
    python3 -c 'import socket,sys
s=socket.socket()
try:
    s.connect(("127.0.0.1", int(sys.argv[1])))
except OSError:
    sys.exit(0)
sys.exit(1)' "$closed" || { echo "port $closed is not closed" >&2; return 1; }
    PORT=$closed
    set +e
    kiosk 8 "wait:7000;shot:${offline}" 15
    rc=$?
    set -e
    if [[ "$rc" -ne 0 ]]; then
        echo "unreachable exit $rc" >&2
        show_kiosk_err
        return 1
    fi
    [[ -f "$offline" ]] || { echo "missing offline png" >&2; return 1; }
    [[ "$(png_size "$offline")" == "800x480" ]] || return 1
    if cmp -s "$healthy" "$offline"; then
        echo "offline png matches healthy" >&2
        return 1
    fi
    stale_h=$(python3 "$TMP/png.py" stale "$healthy")
    stale_o=$(python3 "$TMP/png.py" stale "$offline")
    if [[ "$stale_h" -gt 40 || "$stale_o" -lt 200 ]]; then
        echo "stale pill pixels healthy=$stale_h offline=$stale_o" >&2
        return 1
    fi
}

case_g() {
    local rc
    set +e
    env -u ZAN_API -u ZK_POLL_MS_STATUS -u ZK_POWER_FORCE \
        -u ZAN_DIM_AFTER_SEC -u ZAN_OFF_AFTER_SEC -u ZAN_DIM_LEVEL -u ZAN_BACKLIGHT \
        "$BIN" >"$TMP/kiosk.out" 2>"$TMP/kiosk.err"
    rc=$?
    set -e
    if [[ "$rc" -ne 2 ]]; then
        echo "expected exit 2, got $rc" >&2
        show_kiosk_err
        return 1
    fi
}

# Pause OK, then stray taps at OK and on the 1-week chip: still one pause POST.
case_h() {
    start_stub "$FIX/home-rain" || return 1
    kiosk 6 \
        "wait:700;tap:${SLOT_X},${SLOT_Y};wait:400;tap:${CHIP_DAYS2_X},${CHIP_DAYS2_Y};wait:400;tap:${OK_X},${OK_Y};wait:100;tap:${OK_X},${OK_Y};wait:150;tap:${CHIP_WEEK_X},${CHIP_WEEK_Y};wait:50;tap:${OK_X},${OK_Y};wait:900" \
        12 --allow-writes || { show_kiosk_err; return 1; }
    assert_posts 'POST /api/pause {"days":2,"reason":"kiosk"}' || return 1
}

# Two STOP+OK sequences while the first POST is still held by the stub.
case_i() {
    start_stub "$FIX/home-rain" --post-delay 3 || return 1
    kiosk 8 \
        "wait:700;tap:${STOP_X},${STOP_Y};wait:300;tap:${OK_X},${OK_Y};wait:800;tap:${STOP_X},${STOP_Y};wait:300;tap:${OK_X},${OK_Y};wait:800" \
        20 --allow-writes || { show_kiosk_err; return 1; }
    assert_posts "POST /api/run/cancel" || return 1
}

# GETs held 8s. The cancel POST must show up soon after OK, not after the GET.
case_j() {
    local shot=$TMP/j-start.png
    local seen delta
    start_stub "$FIX/home-rain" --get-delay 8 || return 1
    timeout 22 env -u ZAN_API -u ZK_POLL_MS_STATUS \
        -u ZK_POWER_FORCE -u ZAN_DIM_AFTER_SEC -u ZAN_OFF_AFTER_SEC \
        -u ZAN_DIM_LEVEL -u ZAN_BACKLIGHT \
        "$BIN" \
        --api "http://127.0.0.1:${PORT}" \
        --allow-writes \
        --script "shot:${shot};wait:300;tap:${STOP_X},${STOP_Y};wait:300;tap:${OK_X},${OK_Y};wait:2000" \
        --duration 6 \
        >"$TMP/kiosk.out" 2>"$TMP/kiosk.err" &
    KIOSK_PID=$!
    seen=$(python3 - "$shot" <<'PY'
import os, sys, time
path = sys.argv[1]
deadline = time.monotonic() + 12
while time.monotonic() < deadline:
    if os.path.isfile(path) and os.path.getsize(path) > 32:
        print("%.6f" % time.monotonic())
        raise SystemExit(0)
    time.sleep(0.02)
raise SystemExit(1)
PY
) || { echo "case j: start shot missing" >&2; show_kiosk_err; stop_kiosk; return 1; }
    delta=$(python3 - "${LOG}.ts" "$seen" <<'PY'
import sys, time
path, seen_s = sys.argv[1], sys.argv[2]
seen = float(seen_s)
deadline = time.monotonic() + 8
while time.monotonic() < deadline:
    try:
        lines = open(path, encoding="utf-8").read().splitlines()
    except OSError:
        lines = []
    for line in lines:
        parts = line.split()
        if len(parts) >= 3 and parts[1] == "POST" and parts[2] == "/api/run/cancel":
            delta = float(parts[0]) - seen
            print("%.3f" % delta)
            raise SystemExit(0 if 0.0 <= delta <= 2.5 else 2)
    time.sleep(0.05)
sys.stderr.write("no POST ts\n")
raise SystemExit(1)
PY
) || { echo "case j: POST not within 2.5s of start shot (delta=${delta:-missing})" >&2; stop_kiosk; return 1; }
    echo "case_j post_delta_s ${delta}"
    if ! wait "$KIOSK_PID"; then
        echo "case j: kiosk failed" >&2
        show_kiosk_err
        KIOSK_PID=
        return 1
    fi
    KIOSK_PID=
    assert_posts "POST /api/run/cancel" || return 1
}

# Tile tap opens the station sheet. Writes stay off.
case_k() {
    local png=$TMP/k-sheet.png
    start_stub "$FIX/home-rain" || return 1
    kiosk 4 "wait:800;tap:${TILE0_X},${TILE0_Y};wait:400;shot:${png}" 12 --allow-writes || {
        show_kiosk_err
        return 1
    }
    assert_posts "" || return 1
    [[ "$(png_size "$png")" == "800x480" ]] || return 1
}

# Schedules button opens Schedules; Close (slot) returns.
case_l() {
    local a=$TMP/l-sched.png b=$TMP/l-home.png
    local n
    start_stub "$FIX/home-rain" || return 1
    kiosk 5 "wait:800;tap:${SCHED_X},${SCHED_Y};wait:400;shot:${a};tap:${SLOT_X},${SLOT_Y};wait:400;shot:${b}" 12 || {
        show_kiosk_err
        return 1
    }
    n=$(python3 "$TMP/png.py" schedules "$a")
    if [[ "$n" -lt 80 ]]; then
        echo "schedules screen plum pixels $n" >&2
        return 1
    fi
    if cmp -s "$a" "$b"; then
        echo "Close did not leave Schedules" >&2
        return 1
    fi
}

# STOP from the station sheet still confirms and sends one cancel.
case_m() {
    start_stub "$FIX/home-rain" || return 1
    kiosk 5 "wait:800;tap:${TILE0_X},${TILE0_Y};wait:400;tap:${STOP_X},${STOP_Y};wait:400;tap:${OK_X},${OK_Y};wait:600" 12 --allow-writes || {
        show_kiosk_err
        return 1
    }
    assert_posts "POST /api/run/cancel" || return 1
}

# 404 /api/kiosk shows the needs-update card; STOP still works.
case_n() {
    local png=$TMP/n-need.png
    local n
    start_stub "$FIX/home-rain" --kiosk-404 || return 1
    kiosk 5 "wait:900;shot:${png};tap:${STOP_X},${STOP_Y};wait:400;tap:${OK_X},${OK_Y};wait:600" 12 --allow-writes || {
        show_kiosk_err
        return 1
    }
    n=$(python3 "$TMP/png.py" needsupdate "$png")
    if [[ "$n" -lt 200 ]]; then
        echo "needs-update cream pixels $n" >&2
        return 1
    fi
    assert_posts "POST /api/run/cancel" || return 1
}

trim_file() {
    tr -d '[:space:]' <"$1"
}

make_backlight() {
    local dir=$1
    mkdir -p "$dir"
    printf '200\n' >"$dir/brightness"
    printf '255\n' >"$dir/max_brightness"
    printf '0\n' >"$dir/bl_power"
    printf '200\n' >"$dir/actual_brightness"
}

sample_backlight() {
    local dir=$1 out=$2 pid=$3
    : >"$out"
    while kill -0 "$pid" 2>/dev/null; do
        printf '%s %s\n' "$(trim_file "$dir/brightness")" "$(trim_file "$dir/bl_power")" >>"$out"
        sleep 0.05
    done
}

# Caller sets BLDIR. Extra args are client flags (api or fixture, timers, backlight).
start_power_kiosk() {
    local dur=$1
    local script=$2
    shift 2
    stop_sampler
    stop_kiosk
    env -u ZAN_API -u ZK_POLL_MS_STATUS \
        -u ZAN_DIM_AFTER_SEC -u ZAN_OFF_AFTER_SEC -u ZAN_DIM_LEVEL -u ZAN_BACKLIGHT \
        ZK_POWER_FORCE=1 \
        "$BIN" \
        "$@" \
        --script "$script" \
        --duration "$dur" \
        >"$TMP/kiosk.out" 2>"$TMP/kiosk.err" &
    KIOSK_PID=$!
    sample_backlight "$BLDIR" "$TMP/bl.trace" "$KIOSK_PID" &
    SAMPLER_PID=$!
}

wait_kiosk_done() {
    local tenths=$1
    local i
    for ((i = 0; i < tenths; i++)); do
        if ! kill -0 "$KIOSK_PID" 2>/dev/null; then
            set +e
            wait "$KIOSK_PID"
            KIOSK_RC=$?
            set -e
            KIOSK_PID=
            stop_sampler
            return 0
        fi
        sleep 0.1
    done
    echo "kiosk still running" >&2
    show_kiosk_err
    stop_kiosk
    stop_sampler
    return 1
}

nearcount() {
    python3 "$TMP/png.py" nearcount "$1" "$2" "$3" "$4" "$5" "$6" "$7" "$8" "$9"
}

# Home with the rain strip is #CFE6E1. The gap between tile rows is #EFDDBE.
# The station sheet paints that gap cream, so a swallowed tile tap stays beige.
home_rain_ok() {
    local png=$1 rain gap
    rain=$(nearcount "$png" 300 520 140 180 207 230 225 12)
    gap=$(nearcount "$png" 30 200 325 334 239 221 190 12)
    if [[ "$rain" -lt 400 || "$gap" -lt 400 ]]; then
        echo "home layout not held (rain=$rain gap=$gap) $png" >&2
        return 1
    fi
}

# The stub records POST/PUT/DELETE only. An empty log is zero writes.
no_writes() {
    if [[ -s "$LOG" ]] && grep -q '[^[:space:]]' "$LOG"; then
        echo "request log is not empty" >&2
        cat "$LOG" >&2
        return 1
    fi
}

trace_fail() {
    echo "brightness trace:" >&2
    cat "$TMP/bl.trace" >&2 || true
    show_kiosk_err
    return 1
}

# Dim, then off. A tile tap while off restores brightness and does not open the sheet.
case_o() {
    local home=$TMP/o-home.png after=$TMP/o-after.png
    BLDIR=$TMP/bl-o
    make_backlight "$BLDIR"
    start_stub "$FIX/home-rain" || return 1
    start_power_kiosk 12 \
        "wait:900;shot:${home};wait:7000;tap:${TILE0_X},${TILE0_Y};wait:500;shot:${after}" \
        --api "http://127.0.0.1:${PORT}" \
        --dim-after 1 --off-after 3 --dim-level 40 \
        --backlight "$BLDIR"
    wait_kiosk_done 160 || return 1
    if [[ "$KIOSK_RC" -ne 0 ]]; then
        echo "power o exit $KIOSK_RC" >&2
        show_kiosk_err
        return 1
    fi
    if ! python3 - "$TMP/bl.trace" <<'PY'
import sys
phase = 0
wake = 0
for line in open(sys.argv[1]):
    parts = line.split()
    if len(parts) != 2:
        continue
    br, bl = parts
    if phase == 0 and br == "40" and bl == "0":
        phase = 1
    elif phase == 1 and br == "0" and bl == "1":
        phase = 2
    elif phase == 2 and br == "200" and bl == "0":
        wake += 1
if phase != 2 or wake < 10:
    sys.exit(1)
PY
    then
        trace_fail
        return 1
    fi
    home_rain_ok "$home" || return 1
    home_rain_ok "$after" || return 1
    no_writes || return 1
    assert_posts "" || return 1
    [[ "$(trim_file "$BLDIR/brightness")" == 200 ]]
    [[ "$(trim_file "$BLDIR/bl_power")" == 0 ]]
    [[ "$(trim_file "$BLDIR/actual_brightness")" == 200 ]]
}

# Dimmed tile tap is swallowed. After it dims again, STOP passes on the first tap.
case_p() {
    local home=$TMP/p-home.png conf=$TMP/p-conf.png
    local ok
    BLDIR=$TMP/bl-p
    make_backlight "$BLDIR"
    start_stub "$FIX/home-rain" || return 1
    start_power_kiosk 10 \
        "wait:3000;tap:${TILE0_X},${TILE0_Y};wait:450;shot:${home};wait:2200;tap:${STOP_X},${STOP_Y};wait:500;shot:${conf}" \
        --api "http://127.0.0.1:${PORT}" \
        --dim-after 1 --off-after 15 --dim-level 40 \
        --backlight "$BLDIR"
    wait_kiosk_done 140 || return 1
    if [[ "$KIOSK_RC" -ne 0 ]]; then
        echo "power p exit $KIOSK_RC" >&2
        show_kiosk_err
        return 1
    fi
    if ! python3 - "$TMP/bl.trace" <<'PY'
import sys
phase = 0
for line in open(sys.argv[1]):
    parts = line.split()
    if len(parts) != 2:
        continue
    br, bl = parts
    if bl != "0":
        sys.exit(1)
    if phase == 0 and br == "40":
        phase = 1
    elif phase == 1 and br == "200":
        phase = 2
    elif phase == 2 and br == "40":
        phase = 3
    elif phase == 3 and br == "200":
        phase = 4
if phase != 4:
    sys.exit(1)
PY
    then
        trace_fail
        return 1
    fi
    home_rain_ok "$home" || return 1
    ok=$(python3 "$TMP/png.py" stopok "$conf")
    if [[ "$ok" != "181 54 26" ]]; then
        echo "dimmed STOP did not open confirm ($ok)" >&2
        return 1
    fi
    no_writes || return 1
    assert_posts "" || return 1
}

# Running (watering) stays at the original brightness past the off timer.
case_q() {
    local png=$TMP/q-run.png
    local n
    BLDIR=$TMP/bl-q
    make_backlight "$BLDIR"
    start_power_kiosk 5 \
        "wait:4000;shot:${png}" \
        --fixture "$FIX/running" \
        --dim-after 1 --off-after 2 --dim-level 40 \
        --backlight "$BLDIR"
    wait_kiosk_done 80 || return 1
    if [[ "$KIOSK_RC" -ne 0 ]]; then
        echo "power q exit $KIOSK_RC" >&2
        show_kiosk_err
        return 1
    fi
    if ! python3 - "$TMP/bl.trace" <<'PY'
import sys
n = 0
for line in open(sys.argv[1]):
    parts = line.split()
    if len(parts) != 2:
        continue
    n += 1
    if parts != ["200", "0"]:
        sys.exit(1)
if n < 20:
    sys.exit(1)
PY
    then
        trace_fail
        return 1
    fi
    n=$(nearcount "$png" 300 520 200 320 15 94 92 16)
    if [[ "$n" -lt 200 ]]; then
        echo "running screen teal pixels $n" >&2
        return 1
    fi
    [[ "$(trim_file "$BLDIR/brightness")" == 200 ]]
    [[ "$(trim_file "$BLDIR/bl_power")" == 0 ]]
}

# Fault stays on. Pause dims and never blanks; exit restores the original level.
case_r() {
    local fault=$TMP/r-fault.png paused=$TMP/r-paused.png
    local n
    BLDIR=$TMP/bl-r
    make_backlight "$BLDIR"
    start_power_kiosk 5 \
        "wait:4000;shot:${fault}" \
        --fixture "$FIX/home-fault" \
        --dim-after 1 --off-after 2 --dim-level 40 \
        --backlight "$BLDIR"
    wait_kiosk_done 80 || return 1
    if [[ "$KIOSK_RC" -ne 0 ]]; then
        echo "power r fault exit $KIOSK_RC" >&2
        show_kiosk_err
        return 1
    fi
    if ! python3 - "$TMP/bl.trace" <<'PY'
import sys
n = 0
for line in open(sys.argv[1]):
    parts = line.split()
    if len(parts) != 2:
        continue
    n += 1
    if parts != ["200", "0"]:
        sys.exit(1)
if n < 20:
    sys.exit(1)
PY
    then
        trace_fail
        return 1
    fi
    n=$(nearcount "$fault" 20 500 135 180 181 54 26 16)
    if [[ "$n" -lt 400 ]]; then
        echo "fault banner red pixels $n" >&2
        return 1
    fi

    make_backlight "$BLDIR"
    start_power_kiosk 6 \
        "wait:5000;shot:${paused}" \
        --fixture "$FIX/paused-rain" \
        --dim-after 1 --off-after 2 --dim-level 40 \
        --backlight "$BLDIR"
    wait_kiosk_done 90 || return 1
    if [[ "$KIOSK_RC" -ne 0 ]]; then
        echo "power r pause exit $KIOSK_RC" >&2
        show_kiosk_err
        return 1
    fi
    if ! python3 - "$TMP/bl.trace" <<'PY'
import sys
saw = False
for line in open(sys.argv[1]):
    parts = line.split()
    if len(parts) != 2:
        continue
    br, bl = parts
    if bl != "0" or br == "0":
        sys.exit(1)
    if br == "40":
        saw = True
    elif br != "200":
        sys.exit(1)
if not saw:
    sys.exit(1)
PY
    then
        trace_fail
        return 1
    fi
    n=$(nearcount "$paused" 40 360 20 110 78 63 99 16)
    if [[ "$n" -lt 400 ]]; then
        echo "paused banner plum pixels $n" >&2
        return 1
    fi
    [[ "$(trim_file "$BLDIR/brightness")" == 200 ]]
    [[ "$(trim_file "$BLDIR/bl_power")" == 0 ]]
}

# SIGTERM or SIGINT while off puts the saved brightness back and clears bl_power.
signal_restore() {
    local sig=$1
    local label=$2
    local i saw=0
    BLDIR=$TMP/bl-$label
    make_backlight "$BLDIR"
    start_power_kiosk 20 "wait:18000" \
        --fixture "$FIX/home-rain" \
        --dim-after 1 --off-after 2 --dim-level 40 \
        --backlight "$BLDIR"
    for ((i = 0; i < 80; i++)); do
        if grep -q '^0 1$' "$TMP/bl.trace"; then
            saw=1
            break
        fi
        if ! kill -0 "$KIOSK_PID" 2>/dev/null; then
            echo "kiosk exited before off" >&2
            trace_fail
            return 1
        fi
        sleep 0.1
    done
    if [[ "$saw" -ne 1 ]]; then
        echo "never reached off" >&2
        trace_fail
        return 1
    fi
    kill -s "$sig" "$KIOSK_PID"
    for ((i = 0; i < 30; i++)); do
        if ! kill -0 "$KIOSK_PID" 2>/dev/null; then
            break
        fi
        sleep 0.1
    done
    if kill -0 "$KIOSK_PID" 2>/dev/null; then
        echo "SIG$sig did not stop the kiosk" >&2
        kill -KILL "$KIOSK_PID" 2>/dev/null || true
        stop_sampler
        return 1
    fi
    set +e
    wait "$KIOSK_PID"
    set -e
    KIOSK_PID=
    stop_sampler
    [[ "$(trim_file "$BLDIR/brightness")" == 200 ]]
    [[ "$(trim_file "$BLDIR/bl_power")" == 0 ]]
}

case_s() {
    signal_restore TERM s
}

case_t() {
    signal_restore INT t
}

# Framebuffer cases use a regular file plus ZK_FB_FAKE. They must not open a
# real framebuffer, backlight, or tty. ZK_TTY is a temp file. Touch autodetect
# is pointed at a path that does not exist. ZAN_API is unset.
run_isolated() {
    local limit=$1
    shift
    timeout "$limit" env -u ZAN_API -u ZK_POLL_MS_STATUS -u ZK_POWER_FORCE \
        -u ZAN_DIM_AFTER_SEC -u ZAN_OFF_AFTER_SEC -u ZAN_DIM_LEVEL -u ZAN_BACKLIGHT \
        ZAN_SYSFS_BACKLIGHT_ROOT="$TMP/empty-sysfs-backlight" \
        ZK_TTY="$TMP/zk-tty" \
        "$@"
}

# home-norain: background (2,2), STOP face (580,40), soil fill on tile 0 (34,248).
panel_case() {
    local bpp=$1
    local fmt=$2
    local tol=$3
    local pp=$4
    local fb=$TMP/fb$bpp
    local shot=$TMP/panel$bpp.png
    local mem=$TMP/mem$bpp.png
    local bytes=$((800 * 480 * pp))
    local stride=$((800 * pp))
    local rc t0 t1
    local p1 p2 p3
    : >"$fb"
    truncate -s "$bytes" "$fb"
    set +e
    run_isolated 20 ZK_FB_FAKE="800x480x${bpp}" \
        "$BIN" \
        --fb "$fb" \
        --fixture "$FIX/home-norain" \
        --duration 3 \
        --dim-after 0 --off-after 0 \
        --touch "$TMP/no-touch-device" \
        --backlight "$TMP/empty-sysfs-backlight" \
        >"$TMP/kiosk.out" 2>"$TMP/kiosk.err"
    rc=$?
    set -e
    if [[ "$rc" -ne 0 ]]; then
        echo "fb ${bpp} render exit $rc" >&2
        show_kiosk_err
        return 1
    fi
    if ! grep -E "fb: .* 800x480 ${bpp}bpp ${fmt} stride ${stride}" "$TMP/kiosk.err" >/dev/null; then
        echo "missing ${bpp}bpp ${fmt} line" >&2
        show_kiosk_err
        return 1
    fi
    set +e
    run_isolated 10 ZK_FB_FAKE="800x480x${bpp}" \
        "$BIN" --fb "$fb" --fbshot "$shot" \
        >"$TMP/kiosk.out" 2>"$TMP/kiosk.err"
    rc=$?
    set -e
    if [[ "$rc" -ne 0 ]]; then
        echo "fbshot ${bpp} exit $rc" >&2
        show_kiosk_err
        return 1
    fi
    python3 - "$TMP/kiosk.out" "$bpp" "$fmt" <<'PY' || return 1
import re, sys
text = open(sys.argv[1]).read().splitlines()
if len(text) != 1:
    raise SystemExit("expected one stdout line, got %d: %s" % (len(text), text))
m = re.fullmatch(
    r"fbshot: \S+ (\d+)x(\d+) (\d+)bpp (RGB565|XRGB8888) nonblack=(\d+)/(\d+)",
    text[0])
if not m:
    raise SystemExit("bad fbshot line: " + text[0])
w, h, bpp, fmt, nb, total = m.groups()
nb, total = int(nb), int(total)
if (w, h, bpp, fmt) != ("800", "480", sys.argv[2], sys.argv[3]):
    raise SystemExit("geometry %s" % (m.groups(),))
if total != 800 * 480 or nb * 2 <= total:
    raise SystemExit("nonblack %d/%d" % (nb, total))
PY
    set +e
    run_isolated 15 \
        "$BIN" \
        --fixture "$FIX/home-norain" \
        --script "wait:500;shot:${mem}" \
        --duration 2 \
        --dim-after 0 --off-after 0 \
        --backlight "$TMP/empty-sysfs-backlight" \
        >"$TMP/kiosk.out" 2>"$TMP/kiosk.err"
    rc=$?
    set -e
    if [[ "$rc" -ne 0 ]]; then
        echo "memory shot ${bpp} exit $rc" >&2
        show_kiosk_err
        return 1
    fi
    [[ -f "$mem" && -f "$shot" ]] || { echo "missing png" >&2; return 1; }
    python3 "$TMP/png.py" close "$mem" "$shot" 2 2 "$tol" || return 1
    python3 "$TMP/png.py" close "$mem" "$shot" 580 40 "$tol" || return 1
    python3 "$TMP/png.py" close "$mem" "$shot" 34 248 "$tol" || return 1
    p1=$(python3 "$TMP/png.py" pix "$mem" 2 2)
    p2=$(python3 "$TMP/png.py" pix "$mem" 580 40)
    p3=$(python3 "$TMP/png.py" pix "$mem" 34 248)
    python3 -c 'import sys
ps = [tuple(int(x) for x in s.split()) for s in sys.argv[1:]]
if len(set(ps)) != 3:
    raise SystemExit("pixels not distinct: %s" % (ps,))
r, g, b = ps[1]
if not (r > 160 and r > g + 40 and r > b + 40):
    raise SystemExit("stop pixel not red: %s" % (ps[1],))
if ps[0] == (0, 0, 0):
    raise SystemExit("background is black")
' "$p1" "$p2" "$p3" || return 1
}

case_u() {
    panel_case 16 RGB565 8 2
}

case_v() {
    panel_case 32 XRGB8888 1 4
}

case_w() {
    local fb=$TMP/fb24
    local rc t0 t1
    : >"$fb"
    truncate -s $((800 * 480 * 3)) "$fb"
    t0=$(date +%s)
    set +e
    run_isolated 5 ZK_FB_FAKE=800x480x24 \
        "$BIN" \
        --fb "$fb" \
        --fixture "$FIX/home-norain" \
        --duration 3 \
        --dim-after 0 --off-after 0 \
        --touch "$TMP/no-touch-device" \
        --backlight "$TMP/empty-sysfs-backlight" \
        >"$TMP/kiosk.out" 2>"$TMP/kiosk.err"
    rc=$?
    set -e
    t1=$(date +%s)
    if [[ "$rc" -ne 78 ]]; then
        echo "24 bpp exit $rc" >&2
        show_kiosk_err
        return 1
    fi
    if [[ $((t1 - t0)) -ge 3 ]]; then
        echo "24 bpp ran for the duration" >&2
        return 1
    fi
    if ! grep -F 'unsupported framebuffer format' "$TMP/kiosk.err" >/dev/null; then
        echo "24 bpp missing format error" >&2
        show_kiosk_err
        return 1
    fi
    python3 -c 'import sys
b = open(sys.argv[1], "rb").read()
if not b or any(x != 0 for x in b):
    raise SystemExit("fb was written")
' "$fb" || return 1
    set +e
    run_isolated 10 ZK_FB_FAKE=800x480x24 \
        "$BIN" --fb "$fb" --fbshot "$TMP/fb24.png" \
        >"$TMP/kiosk.out" 2>"$TMP/kiosk.err"
    rc=$?
    set -e
    if [[ "$rc" -ne 1 ]]; then
        echo "24 bpp fbshot exit $rc" >&2
        show_kiosk_err
        return 1
    fi
    if ! grep -F 'unsupported framebuffer format' "$TMP/kiosk.err" >/dev/null; then
        echo "24 bpp fbshot missing format error" >&2
        show_kiosk_err
        return 1
    fi
}

case_x() {
    local fb=$TMP/fbblack
    local png=$TMP/fbblack.png
    local rc
    : >"$fb"
    truncate -s $((800 * 480 * 2)) "$fb"
    set +e
    run_isolated 10 ZK_FB_FAKE=800x480x16 \
        "$BIN" --fb "$fb" --fbshot "$png" \
        >"$TMP/kiosk.out" 2>"$TMP/kiosk.err"
    rc=$?
    set -e
    if [[ "$rc" -ne 3 ]]; then
        echo "black fbshot exit $rc" >&2
        show_kiosk_err
        return 1
    fi
    if ! grep -E '^fbshot: .+ 800x480 16bpp RGB565 nonblack=0/384000$' "$TMP/kiosk.out" >/dev/null; then
        echo "black fbshot line:" >&2
        cat "$TMP/kiosk.out" >&2 || true
        return 1
    fi
    [[ "$(wc -l <"$TMP/kiosk.out")" -eq 1 ]] || { echo "extra stdout" >&2; return 1; }
    [[ "$(png_size "$png")" == "800x480" ]] || return 1
}

case_y() {
    local rc
    set +e
    run_isolated 5 \
        "$BIN" --fbshot "$TMP/nofb.png" \
        --fixture "$FIX/home-norain" --duration 1 \
        --dim-after 0 --off-after 0 \
        >"$TMP/kiosk.out" 2>"$TMP/kiosk.err"
    rc=$?
    set -e
    if [[ "$rc" -ne 2 ]]; then
        echo "fbshot without --fb exit $rc" >&2
        show_kiosk_err
        return 1
    fi
}

# Padded line_length. Padding bytes stay 0xA5. The visible frame matches
# the same fixture drawn at the tight stride.
stride_case() {
    local bpp=$1
    local fmt=$2
    local pp=$3
    local stride=$4
    local fb=$TMP/fbpad$bpp
    local plain=$TMP/fbplain$bpp
    local shot_pad=$TMP/shotpad$bpp.png
    local shot_plain=$TMP/shotplain$bpp.png
    local bytes=$((stride * 480))
    local plain_bytes=$((800 * 480 * pp))
    local rc

    python3 -c 'import sys; open(sys.argv[1], "wb").write(b"\xa5" * int(sys.argv[2]))' \
        "$fb" "$bytes" || return 1
    set +e
    run_isolated 20 ZK_FB_FAKE="800x480x${bpp}x${stride}" \
        "$BIN" \
        --fb "$fb" \
        --fixture "$FIX/home-norain" \
        --duration 3 \
        --dim-after 0 --off-after 0 \
        --touch "$TMP/no-touch-device" \
        --backlight "$TMP/empty-sysfs-backlight" \
        >"$TMP/kiosk.out" 2>"$TMP/kiosk.err"
    rc=$?
    set -e
    if [[ "$rc" -ne 0 ]]; then
        echo "padded ${bpp} render exit $rc" >&2
        show_kiosk_err
        return 1
    fi
    if ! grep -E "fb: .* 800x480 ${bpp}bpp ${fmt} stride ${stride}$" "$TMP/kiosk.err" >/dev/null; then
        echo "missing padded ${bpp}bpp line" >&2
        show_kiosk_err
        return 1
    fi
    python3 - "$fb" "$bpp" "$stride" <<'PY' || return 1
import sys
path, bpp, stride = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
w, h = 800, 480
pix = w * (bpp // 8)
data = open(path, "rb").read()
need = stride * h
if len(data) != need:
    raise SystemExit("len %d != %d" % (len(data), need))
if pix >= stride:
    raise SystemExit("no padding")
sent = bytes([0xA5]) * (stride - pix)
for y in range(h):
    row = data[y * stride:(y + 1) * stride]
    if row[pix:] != sent:
        raise SystemExit("padding row %d" % y)
PY
    : >"$plain"
    truncate -s "$plain_bytes" "$plain" || return 1
    set +e
    run_isolated 20 ZK_FB_FAKE="800x480x${bpp}" \
        "$BIN" \
        --fb "$plain" \
        --fixture "$FIX/home-norain" \
        --duration 3 \
        --dim-after 0 --off-after 0 \
        --touch "$TMP/no-touch-device" \
        --backlight "$TMP/empty-sysfs-backlight" \
        >"$TMP/kiosk.out" 2>"$TMP/kiosk.err"
    rc=$?
    set -e
    if [[ "$rc" -ne 0 ]]; then
        echo "plain ${bpp} render exit $rc" >&2
        show_kiosk_err
        return 1
    fi
    set +e
    run_isolated 10 ZK_FB_FAKE="800x480x${bpp}x${stride}" \
        "$BIN" --fb "$fb" --fbshot "$shot_pad" \
        >"$TMP/kiosk.out" 2>"$TMP/kiosk.err"
    rc=$?
    set -e
    if [[ "$rc" -ne 0 ]]; then
        echo "padded fbshot ${bpp} exit $rc" >&2
        show_kiosk_err
        return 1
    fi
    set +e
    run_isolated 10 ZK_FB_FAKE="800x480x${bpp}" \
        "$BIN" --fb "$plain" --fbshot "$shot_plain" \
        >"$TMP/kiosk.out" 2>"$TMP/kiosk.err"
    rc=$?
    set -e
    if [[ "$rc" -ne 0 ]]; then
        echo "plain fbshot ${bpp} exit $rc" >&2
        show_kiosk_err
        return 1
    fi
    python3 "$TMP/png.py" identical "$shot_pad" "$shot_plain" || {
        echo "padded ${bpp} png differs from unpadded" >&2
        return 1
    }
}

case_z() {
    stride_case 32 XRGB8888 4 3328 || return 1
    stride_case 16 RGB565 2 1664 || return 1
}

# Rows 474..479 used to be a COL_STOP (#B5361A) bar and dashes. They are
# screen background now (COL_BG #EFDDBE). Row 470 is the same band.
case_aa() {
    local fix png fb rc red bg
    for fix in home-norain running; do
        png=$TMP/aa-$fix.png
        fb=$TMP/aa-$fix.fb
        : >"$fb"
        truncate -s $((800 * 480 * 4)) "$fb"
        set +e
        run_isolated 20 ZK_FB_FAKE=800x480x32 \
            "$BIN" \
            --fb "$fb" \
            --fixture "$FIX/$fix" \
            --duration 3 \
            --dim-after 0 --off-after 0 \
            --touch "$TMP/no-touch-device" \
            --backlight "$TMP/empty-sysfs-backlight" \
            >"$TMP/kiosk.out" 2>"$TMP/kiosk.err"
        rc=$?
        set -e
        if [[ "$rc" -ne 0 ]]; then
            echo "aa $fix render exit $rc" >&2
            show_kiosk_err
            return 1
        fi
        set +e
        run_isolated 10 ZK_FB_FAKE=800x480x32 \
            "$BIN" --fb "$fb" --fbshot "$png" \
            >"$TMP/kiosk.out" 2>"$TMP/kiosk.err"
        rc=$?
        set -e
        if [[ "$rc" -ne 0 ]]; then
            echo "aa $fix fbshot exit $rc" >&2
            show_kiosk_err
            return 1
        fi
        [[ "$(png_size "$png")" == "800x480" ]] || {
            echo "aa $fix size $(png_size "$png")" >&2
            return 1
        }
        # Same channel tolerance as the fault-banner COL_STOP nearcount.
        red=$(nearcount "$png" 0 800 474 480 181 54 26 16)
        if [[ "$red" -ne 0 ]]; then
            echo "aa $fix COL_STOP pixels in rows 474..479: $red" >&2
            return 1
        fi
        bg=$(nearcount "$png" 0 800 470 471 239 221 190 8)
        if [[ "$bg" -ne 800 ]]; then
            echo "aa $fix row 470 is not COL_BG: $bg/800" >&2
            return 1
        fi
        python3 "$TMP/png.py" bgmatch "$png" 470 474 480 1 || {
            echo "aa $fix rows 474..479 differ from row 470" >&2
            return 1
        }
    done
}

run_case a case_a
run_case b case_b
run_case c case_c
run_case d case_d
run_case e case_e
run_case f case_f
run_case g case_g
run_case h case_h
run_case i case_i
run_case j case_j
run_case k case_k
run_case l case_l
run_case m case_m
run_case n case_n
run_case o case_o
run_case p case_p
run_case q case_q
run_case r case_r
run_case s case_s
run_case t case_t
run_case u case_u
run_case v case_v
run_case w case_w
run_case x case_x
run_case y case_y
run_case z case_z
run_case aa case_aa

if [[ "$FAILS" -ne 0 ]]; then
    exit 1
fi
exit 0
