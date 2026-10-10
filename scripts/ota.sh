#!/bin/sh
# Upload a firmware image to a SmallTV web updater.
# Usage: scripts/ota.sh <ip-or-host> [firmware.bin]
set -eu
HOST=${1:?ip or host}
BIN=${2:-firmware/.pio/build/smalltv/firmware.bin}
ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
case $BIN in
  /*) ;;
  *) BIN="$ROOT/$BIN" ;;
esac
test -f "$BIN"
echo "uploading $(wc -c < "$BIN" | tr -d ' ') bytes to http://$HOST/update"
curl -f --max-time 180 -F "firmware=@${BIN}" "http://${HOST}/update"
echo
echo "uploaded"
