#!/system/bin/sh
MODDIR=${0%/*}
HELPER="$MODDIR/bin/sonora-ctl"

if [ -x "$HELPER" ]; then
  "$HELPER" apply 0 center 0 0,0,0,0,0,0,0,0,0,0 >/dev/null 2>&1 || true
fi

exit 0
