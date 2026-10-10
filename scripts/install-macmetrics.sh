#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
BIN="$HOME/.local/bin/macmetrics"
PLIST="$HOME/Library/LaunchAgents/com.kevintang.macmetrics.plist"
make -C "$ROOT/macmetrics"
mkdir -p "$HOME/.local/bin" "$HOME/Library/LaunchAgents"
cp "$ROOT/macmetrics/macmetrics" "$BIN"
chmod 755 "$BIN"
cat > "$PLIST" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key>
  <string>com.kevintang.macmetrics</string>
  <key>ProgramArguments</key>
  <array>
    <string>${BIN}</string>
  </array>
  <key>RunAtLoad</key>
  <true/>
  <key>KeepAlive</key>
  <true/>
  <key>ThrottleInterval</key>
  <integer>2</integer>
  <key>ProcessType</key>
  <string>Background</string>
  <key>StandardOutPath</key>
  <string>/tmp/macmetrics.log</string>
  <key>StandardErrorPath</key>
  <string>/tmp/macmetrics.log</string>
</dict>
</plist>
EOF
UIDN=$(id -u)
launchctl bootout "gui/${UIDN}" "$PLIST" 2>/dev/null || true
launchctl bootstrap "gui/${UIDN}" "$PLIST"
echo "installed $BIN"
echo "plist $PLIST"
