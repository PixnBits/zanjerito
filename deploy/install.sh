#!/bin/sh
# Lean install to /opt/zanjerito. Example config is copy-once.
# Optional Chromium kiosk unit: INSTALL_KIOSK=1 (never enabled automatically).
set -eu
PREFIX="${PREFIX:-/opt/zanjerito}"
UNIT_DST="${UNIT_DST:-/etc/systemd/system/zanjerito.service}"
KIOSK_UNIT_DST="${KIOSK_UNIT_DST:-/etc/systemd/system/zanjerito-kiosk.service}"
ROOT="$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)"
BIN="${BIN:-$ROOT/zanjerito}"

if [ ! -f "$BIN" ]; then
  echo "missing binary $BIN — run: make build" >&2
  exit 1
fi

install -d "$PREFIX"
install -m 755 "$BIN" "$PREFIX/zanjerito"
install -m 644 "$ROOT/deploy/zanjerito.service" "$UNIT_DST"

if [ ! -f "$PREFIX/config.json" ]; then
  install -m 644 "$ROOT/config/pinmap.example.json" "$PREFIX/config.json"
  echo "wrote $PREFIX/config.json (example; edit pin map)"
else
  echo "kept existing $PREFIX/config.json"
fi
if [ ! -f "$PREFIX/front-schedule.example.json" ]; then
  install -m 644 "$ROOT/config/front-schedule.example.json" "$PREFIX/front-schedule.example.json"
fi
if [ ! -f "$PREFIX/zanjerito.env" ]; then
  install -m 644 "$ROOT/deploy/zanjerito.env.example" "$PREFIX/zanjerito.env"
  echo "wrote $PREFIX/zanjerito.env (set LISTEN to this Pi LAN address)"
else
  echo "kept existing $PREFIX/zanjerito.env"
fi

# Opt-in: copy Chromium kiosk unit (does NOT enable it — headless-safe).
if [ "${INSTALL_KIOSK:-0}" = "1" ]; then
  install -m 644 "$ROOT/deploy/zanjerito-kiosk.service" "$KIOSK_UNIT_DST"
  echo "wrote $KIOSK_UNIT_DST (optional; enable only on a graphical Pi)"
  echo "  edit KIOSK_URL to match LISTEN, then:"
  echo "  sudo systemctl daemon-reload && sudo systemctl enable --now zanjerito-kiosk"
fi

echo "enable: sudo systemctl daemon-reload && sudo systemctl enable --now zanjerito"
