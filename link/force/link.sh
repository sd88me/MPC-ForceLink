#!/bin/sh
# Force side of the Force link: link.sh a | b | stock | status
#   a      stop the Force's own MPC; flnet serves its controller ports to the Live III's MPC (TCP 6760/6761, side 6762).
#   b      stop the Force's own MPC; flsurf owns the controller (handshake, heartbeat, OLED text) and serves it as a plain
#          MIDI surface to the Live III's ForceLink script (TCP 6770).
#   stock  stop either daemon and start the Force's MPC again.
# Each switch that stops or starts MPC closes the open project. Files: this folder (/data/forcelink). Log: /tmp/forcelink.log.
set -eu
K=$(cd "$(dirname "$0")" && pwd)
daemons_off() { for d in flnet flsurf; do p=$(pidof $d || true); [ -n "$p" ] && kill $p; done; sleep 1; }
start() {   # start <daemon> [args]
    daemons_off
    systemctl stop acvs; sleep 3
    nohup "$K/$@" > /tmp/forcelink.log 2>&1 &
    sleep 2
    echo "Force: $1 $(pidof "$1" || echo FAILED), acvs $(systemctl is-active acvs || true)"; cat /tmp/forcelink.log
}
case "${1:-status}" in
a|on)  start flnet ;;
b)     start flsurf "$K/handshake.syx" ;;
stock|off)
    daemons_off
    systemctl start acvs
    echo "Force: stock, acvs $(systemctl is-active acvs)" ;;
status)
    echo "flnet: $(pidof flnet || echo off), flsurf: $(pidof flsurf || echo off), acvs: $(systemctl is-active acvs || true)"
    grep -E "client|blocked|flsurf" /tmp/forcelink.log 2>/dev/null | tail -n 4 || true ;;
*) echo "usage: $0 a|b|stock|status"; exit 2 ;;
esac
