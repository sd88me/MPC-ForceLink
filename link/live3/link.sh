#!/bin/sh
# Live III side of the Force link: link.sh link | stock | capture | status | switch <mode>
#   link     the Live III's own MPC (3.10.1.x, no overlay); a "Force Link" MIDI port (shim, b.conf) connects
#            to flsurf on the Force, and the signed ForceLink controller script (script/) drives it as an extra surface.
#   stock    no Force Link port or controller script; the addin drop-in 90-mpc-addins.conf is left alone.
#   capture  the MPC with its own Live III controller, every controller byte logged to /tmp/native.log.
# Everything lives on /run, so a reboot is also "stock". Needs root (the autorun stick gives SSH until the next reboot).
# Files: this folder (/data/mpc-addins/forcespoof): forcespoof.so, net.conf.off (the Force's IP), home.conf, script/.
# (The archived Force-identity mode, formerly "a", is in git history.)
set -eu
K=$(cd "$(dirname "$0")" && pwd)
D=/run/systemd/system/acvs.service.d
S=/data/Settings/MPC/MPC.settings
ver() { p=$(pidof MPC || true); [ -n "$p" ] && strings /proc/$p/exe | grep -m1 -E '^3\.[0-9]+\.[0-9.]+ \(' || echo "not running"; }

# Installed as an addin (in the shared, persistent addin drop-in), the shim is always loaded, also in stock, and
# carries the MAIN + MENU setup switch; then only b.conf / the script change here.
addin_installed() { grep -qs "$K/forcespoof.so" /etc/systemd/system/acvs.service.d/90-mpc-addins.conf "$D/90-mpc-addins.conf"; }

dropins_310() {   # shim drop-in (only when the shim is not installed as an addin)
    mkdir -p "$D"
    if addin_installed; then rm -f "$D/91-forcespoof.conf"; else
    base=$(sed -n 's/^Environment=LD_PRELOAD=//p' "$D/90-mpc-addins.conf" 2>/dev/null || true)
    printf '[Service]\nEnvironment=LD_PRELOAD=%s/forcespoof.so%s\n' "$K" "${base:+:$base}" > "$D/91-forcespoof.conf.new"
    mv "$D/91-forcespoof.conf.new" "$D/91-forcespoof.conf"; fi
}

restart() {
    : > /tmp/forcespoof.log
    systemctl daemon-reload; systemctl restart acvs; sleep 8
    echo "$1: acvs $(systemctl is-active acvs), MPC $(ver)"
}

DEV="/nvme/MPC Documents/Devices/ForceLink"   # where MPC 3.10 looks for user controller scripts

case "${1:-status}" in
switch)   # from the MAIN + MENU setup switch (addin, flsurf): only acts if the setup differs
    want=$2; [ "$want" = b ] && want=link             # older name
    case "$want" in stock|link|toggle) ;; *) echo "usage: $0 switch stock|link|toggle"; exit 2 ;; esac
    now=$([ -f "$K/b.conf" ] && echo link || echo stock)
    home=$(cat "$K/home.conf" 2>/dev/null || true)   # Live III: a home setup it always stays in
    [ "$home" = b ] && home=link
    if [ -n "$home" ]; then [ "$now" = "$home" ] && { echo "stays in its home setup ($home)"; exit 0; }; exec sh "$0" "$home"; fi
    to=$want; [ "$to" = toggle ] && { [ "$now" = link ] && to=stock || to=link; }
    [ "$now" = "$to" ] && { echo "already $to"; exit 0; }
    exec sh "$0" "$to" ;;
link|b)
    if [ -f "$K/net.conf" ]; then mv "$K/net.conf" "$K/net.conf.off"; fi
    force=$(cat "$K/net.conf.off" 2>/dev/null || true)
    [ -n "$force" ] || { echo "no Force address (net.conf.off)"; exit 1; }
    [ -f "$K/script/ForceLink.ini" ] && [ -f "$K/script/ForceLink.lua" ] || { echo "ForceLink script missing in $K/script"; exit 1; }
    # b.peer (optional): the address of another surface (e.g. an Ableton Move running movelink) used instead of the Force's;
    # delete the file to go back to the Force. The Live III dials one peer at a time.
    peer=$(cat "$K/b.peer" 2>/dev/null || true)
    echo n > "$K/dt.conf"; rm -f "$K/live3.conf" "$K/tap.conf"; echo "${peer:-$force}" > "$K/b.conf"
    mkdir -p "$DEV"; cp "$K/script/ForceLink.ini" "$K/script/ForceLink.lua" "$DEV/"
    dropins_310
    restart "link" ;;
capture)
    if [ -f "$K/net.conf" ]; then mv "$K/net.conf" "$K/net.conf.off"; fi
    echo n > "$K/dt.conf"; rm -f "$K/live3.conf" "$K/b.conf"; rm -rf "$DEV"; touch "$K/tap.conf"; : > /tmp/native.log
    dropins_310
    restart "capture (log: /tmp/native.log)" ;;
stock|off)
    rm -f "$D/91-forcespoof.conf" "$D/95-mpc310.conf" "$K/live3.conf" "$K/tap.conf" "$K/b.conf"; rm -rf "$DEV"
    echo n > "$K/dt.conf"; if [ -f "$K/net.conf" ]; then mv "$K/net.conf" "$K/net.conf.off"; fi
    restart "stock"
    ;;
status)
    echo "MPC: $(ver), acvs $(systemctl is-active acvs)"
    for f in 91-forcespoof.conf; do [ -f "$D/$f" ] && echo "drop-in $f: on" || echo "drop-in $f: off"; done
    m=stock
    if [ -f "$K/tap.conf" ]; then m=capture; fi
    if [ -f "$K/b.conf" ]; then m=link; fi
    addin_installed && echo "addin: installed (MAIN + MENU switch $( [ -f "$K/switch.conf" ] && echo on || echo off))"
    echo "mode: $m (home $(cat "$K/home.conf" 2>/dev/null || echo none), Force $(cat "$K/b.conf" 2>/dev/null || echo -))"
    grep -E "link|bridge" /tmp/forcespoof.log 2>/dev/null | tail -n 4 || true ;;
*) echo "usage: $0 link|stock|capture|status|switch stock|link|toggle"; exit 2 ;;
esac
