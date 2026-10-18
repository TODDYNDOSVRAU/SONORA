#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
CONTROL="$TMP/sonora-control.bin"
CXX="${CXX:-c++}"
COMMON=( -std=c++17 -O2 -Wall -Wextra -Werror -DSONORA_CONTROL_FILE=\"$CONTROL\" -I"$ROOT/native/android/include" -I"$ROOT/native/dsp/include" )
"$CXX" "${COMMON[@]}" "$ROOT/native/android/src/sonora_ctl.cpp" -o "$TMP/sonora-ctl"
"$TMP/sonora-ctl" init
"$TMP/sonora-ctl" status | grep -q '^CONTROL=1$'
"$TMP/sonora-ctl" apply 1 right 0 "0,0,0,0,0,0,0,0,0,0"
if "$TMP/sonora-ctl" apply 1 center 7 "0,0,0,0,0,0,0,0,0,0"; then
  echo "FAIL: helper accepted a preamp above +6 dB" >&2
  exit 1
fi
"$CXX" "${COMMON[@]}" "$ROOT/native/dsp/src/sonora_dsp.cpp" "$ROOT/native/android/src/sonora_effect.cpp" "$ROOT/tests/native-effect-plugin.cpp" -o "$TMP/test-effect"
"$TMP/test-effect"
