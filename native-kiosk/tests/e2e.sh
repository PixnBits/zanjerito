#!/usr/bin/env bash
# Headless taps against tools/stub_server.py. Needs build/zan-kiosk-host and python3.
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

cleanup() {
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
    env -u ZAN_API -u ZK_POLL_MS_STATUS "$BIN" >"$TMP/kiosk.out" 2>"$TMP/kiosk.err"
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

if [[ "$FAILS" -ne 0 ]]; then
    exit 1
fi
exit 0
