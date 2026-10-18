#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${TMPDIR:-/tmp}/sonora-native-dsp-test"
CXX="${CXX:-g++}"
"$CXX" -std=c++17 -O2 -Wall -Wextra -Werror \
  -I"$ROOT/native/dsp/include" \
  "$ROOT/native/dsp/src/sonora_dsp.cpp" \
  "$ROOT/tests/native-dsp.cpp" \
  -o "$OUT"
"$OUT"
