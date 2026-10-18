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
if [[ "$(stat -c '%a' "$CONTROL")" != "640" ]]; then
  echo "FAIL: helper did not create a group-readable 0640 control file" >&2
  exit 1
fi
"$TMP/sonora-ctl" apply 1 right 0 "0,0,0,0,0,0,0,0,0,0"
if "$TMP/sonora-ctl" apply 1 center 7 "0,0,0,0,0,0,0,0,0,0"; then
  echo "FAIL: helper accepted a preamp above +6 dB" >&2
  exit 1
fi

# Exercise the Android-only ownership/path branch at compile time without an NDK or device.
"$CXX" -std=c++17 -O2 -Wall -Wextra -Werror -D__ANDROID__ \
  -DSONORA_CONTROL_FILE=\"/data/vendor/audio/sonora_state.bin\" \
  -I"$ROOT/native/android/include" -I"$ROOT/native/dsp/include" \
  -c "$ROOT/native/android/src/sonora_ctl.cpp" -o "$TMP/sonora-ctl-android-branch.o"
"$CXX" -std=c++17 -O2 -Wall -Wextra -Werror -D__ANDROID__ \
  -DSONORA_CONTROL_FILE=\"/data/vendor/audio/sonora_state.bin\" \
  -I"$ROOT/native/android/include" -I"$ROOT/native/dsp/include" \
  -c "$ROOT/native/android/src/sonora_effect.cpp" -o "$TMP/sonora-effect-android-branch.o"

# O_NOFOLLOW must reject a symbolic link rather than touching the target file.
SYMLINK="$TMP/sonora-link.bin"
ln -s "$CONTROL" "$SYMLINK"
"$CXX" -std=c++17 -O2 -Wall -Wextra -Werror -DSONORA_CONTROL_FILE=\"$SYMLINK\" \
  -I"$ROOT/native/android/include" -I"$ROOT/native/dsp/include" \
  "$ROOT/native/android/src/sonora_ctl.cpp" -o "$TMP/sonora-ctl-symlink"
if "$TMP/sonora-ctl-symlink" status >/dev/null 2>&1; then
  echo "FAIL: helper accepted a symlink as the shared-state file" >&2
  exit 1
fi

"$CXX" "${COMMON[@]}" "$ROOT/native/dsp/src/sonora_dsp.cpp" \
  "$ROOT/native/android/src/sonora_effect.cpp" "$ROOT/tests/native-effect-plugin.cpp" \
  -pthread -o "$TMP/test-effect"
"$TMP/test-effect"
