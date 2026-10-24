#!/bin/sh
# Once per boot, run by the USB stick's run.sh (its boot hook for installed addins): bring the Live III up in its
# home setup (home.conf: "link" = MPC 3.10 + the Force link). Restarts MPC once; the marker on /run stops a second
# run in the same boot (the stick's script can run again when MPC starts). Without home.conf it does nothing.
K=$(cd "$(dirname "$0")" && pwd)
M=/run/forcelink-boot-done
[ -f "$M" ] && { echo "forcelink: already set up this boot"; exit 0; }
: > "$M"
home=$(cat "$K/home.conf" 2>/dev/null || true)
case "$home" in
link|b) echo "forcelink: home setup link (MPC 3.10 + Force link)"; exec sh "$K/link.sh" link ;;
*) echo "forcelink: no home setup"; exit 0 ;;
esac
