#!/bin/sh
# Force side of the Force link: link.sh link | stock | status | switch <mode>
#   link   stop the Force's own MPC; flsurf owns the controller (handshake, heartbeat, OLED text) and serves it as a plain
#          MIDI surface to the Live III's ForceLink script (TCP 6770).
#   stock  stop flsurf and start the Force's MPC again. (The archived Force-identity mode, flnet, is in git history.)
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
switch)   # from the SHIFT + MENU setup switch (addin, flsurf): only acts if the setup differs
    want=$2; [ "$want" = b ] && want=link             # older name
    case "$want" in stock|link|toggle) ;; *) echo "usage: $0 switch stock|link|toggle"; exit 2 ;; esac
    now=$([ -n "$(pidof flsurf || true)" ] && echo link || echo stock)
    to=$want; [ "$to" = toggle ] && { [ "$now" = link ] && to=stock || to=link; }
    [ "$now" = "$to" ] && { echo "already $to"; exit 0; }
    exec sh "$0" "$to" ;;
link|b) if [ -f "$K/switch.conf" ]; then start flsurf -s "$K/switch.conf" "$K/handshake.syx"; else start flsurf "$K/handshake.syx"; fi ;;
stock|off)
    daemons_off
    systemctl start acvs
    echo "Force: stock, acvs $(systemctl is-active acvs)" ;;
status)
    echo "flnet: $(pidof flnet || echo off), flsurf: $(pidof flsurf || echo off), acvs: $(systemctl is-active acvs || true)"
    grep -E "client|blocked|flsurf" /tmp/forcelink.log 2>/dev/null | tail -n 4 || true ;;
*) echo "usage: $0 link|stock|status|switch stock|link|toggle"; exit 2 ;;
esac
