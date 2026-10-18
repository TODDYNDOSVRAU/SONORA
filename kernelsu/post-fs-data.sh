#!/system/bin/sh
MODDIR=${0%/*}
HELPER="$MODDIR/bin/sonora-ctl"

if [ ! -x "$HELPER" ]; then
  log -t SonoraSpatial "sonora-ctl missing; audio effect remains unavailable"
  exit 0
fi

if "$HELPER" init >/dev/null 2>&1; then
  log -t SonoraSpatial "shared control state initialized"
else
  log -t SonoraSpatial "shared control state unavailable; effect should remain in bypass"
fi

exit 0
