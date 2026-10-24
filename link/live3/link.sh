#!/bin/sh
# Live III side of the Force link: link.sh stock | a | b | capture | status
#   stock    stock MPC 3.9.1 app and identity (the addin drop-in 90-mpc-addins.conf is left alone).
#   a        version A: MPC 3.10 app (kit in /data/mpc310) as a Force (Gen 2), Force controller over the network,
#            Live III controls through the bridge (map.conf refines the mapping without a rebuild).
#   b        version B: MPC 3.10 app as the Live III itself; a "Force Link" MIDI port (shim, b.conf) connects to flsurf on
#            the Force, and the signed ForceLink controller script (b/) drives it as an extra surface.
#   capture  MPC 3.10 app with its own Live III controller, every controller byte logged to /tmp/native.log.
# Everything lives on /run, so a reboot is also "stock". Needs root (the autorun stick gives SSH until the next reboot).
# Files: this folder (/data/mpc-addins/forcespoof): forcespoof.so, live3_init.syx, map.conf, net.conf (the Force's IP).
set -eu
K=$(cd "$(dirname "$0")" && pwd)
D=/run/systemd/system/acvs.service.d
KIT=/data/mpc310
S=/data/Settings/MPC/MPC.settings
ver() { p=$(pidof MPC || true); [ -n "$p" ] && strings /proc/$p/exe | grep -m1 -E '^3\.[0-9]+\.[0-9.]+ \(' || echo "not running"; }

# Installed as an addin (in the shared, persistent addin drop-in), the shim is always loaded, also in stock, and
# carries the SHIFT + MENU setup switch; then only the 3.10 drop-in changes here.
addin_installed() { grep -qs "$K/forcespoof.so" /etc/systemd/system/acvs.service.d/90-mpc-addins.conf; }

dropins_310() {   # shim + MPC 3.10 drop-ins
    [ -x "$KIT/MPC" ] && [ -d "$KIT/Akai/SME0" ] || { echo "MPC 3.10 kit missing in $KIT"; exit 1; }
    [ -f "$S.bak-before-310" ] || cp -p "$S" "$S.bak-before-310"
    mkdir -p "$D"
    if addin_installed; then rm -f "$D/91-forcespoof.conf"; else
    base=$(sed -n 's/^Environment=LD_PRELOAD=//p' "$D/90-mpc-addins.conf" 2>/dev/null || true)
    printf '[Service]\nEnvironment=LD_PRELOAD=%s/forcespoof.so%s\n' "$K" "${base:+:$base}" > "$D/91-forcespoof.conf.new"
    mv "$D/91-forcespoof.conf.new" "$D/91-forcespoof.conf"; fi
    printf '[Service]\nBindReadOnlyPaths=%s/MPC:/usr/bin/MPC %s/Akai:/usr/share/Akai\n' "$KIT" "$KIT" > "$D/95-mpc310.conf.new"
    mv "$D/95-mpc310.conf.new" "$D/95-mpc310.conf"
}

restart() {
    : > /tmp/forcespoof.log
    systemctl daemon-reload; systemctl restart acvs; sleep 8
    echo "$1: acvs $(systemctl is-active acvs), MPC $(ver)"
}

DEV="/nvme/MPC Documents/Devices/ForceLink"   # where MPC 3.10 looks for user controller scripts

case "${1:-status}" in
switch)   # from the SHIFT + MENU setup switch (addin, flsurf): only acts if the setup differs
    case "${2:-}" in stock|b) ;; *) echo "usage: $0 switch stock|b"; exit 2 ;; esac
    now=$([ -f "$K/b.conf" ] && [ -f "$D/95-mpc310.conf" ] && echo b || echo stock)
    [ "$now" = "$2" ] && { echo "already $2"; exit 0; }
    exec sh "$0" "$2" ;;
a|on)
    [ -f "$K/net.conf.off" ] && [ ! -f "$K/net.conf" ] && mv "$K/net.conf.off" "$K/net.conf"
    [ -s "$K/net.conf" ] || { echo "put the Force's IP address in $K/net.conf"; exit 1; }
    echo s > "$K/dt.conf"; touch "$K/live3.conf"; rm -f "$K/tap.conf" "$K/b.conf"; rm -rf "$DEV"
    dropins_310
    restart "version A" ;;
b)
    if [ -f "$K/net.conf" ]; then mv "$K/net.conf" "$K/net.conf.off"; fi
    force=$(cat "$K/net.conf.off" 2>/dev/null || true)
    [ -n "$force" ] || { echo "no Force address (net.conf.off)"; exit 1; }
    [ -f "$K/b/ForceLink.ini" ] && [ -f "$K/b/ForceLink.lua" ] || { echo "ForceLink script missing in $K/b"; exit 1; }
    echo n > "$K/dt.conf"; rm -f "$K/live3.conf" "$K/tap.conf"; echo "$force" > "$K/b.conf"
    mkdir -p "$DEV"; cp "$K/b/ForceLink.ini" "$K/b/ForceLink.lua" "$DEV/"
    dropins_310
    restart "version B" ;;
capture)
    if [ -f "$K/net.conf" ]; then mv "$K/net.conf" "$K/net.conf.off"; fi
    echo n > "$K/dt.conf"; rm -f "$K/live3.conf" "$K/b.conf"; rm -rf "$DEV"; touch "$K/tap.conf"; : > /tmp/native.log
    dropins_310
    restart "capture (log: /tmp/native.log)" ;;
stock|off)
    rm -f "$D/91-forcespoof.conf" "$D/95-mpc310.conf" "$K/live3.conf" "$K/tap.conf" "$K/b.conf"; rm -rf "$DEV"
    echo n > "$K/dt.conf"; if [ -f "$K/net.conf" ]; then mv "$K/net.conf" "$K/net.conf.off"; fi
    restart "stock"
    echo "(MPC.settings from before 3.10: $S.bak-before-310)" ;;
status)
    echo "MPC: $(ver), acvs $(systemctl is-active acvs)"
    for f in 91-forcespoof.conf 95-mpc310.conf; do [ -f "$D/$f" ] && echo "drop-in $f: on" || echo "drop-in $f: off"; done
    m=stock
    if [ -f "$D/91-forcespoof.conf" ] && [ -f "$K/live3.conf" ]; then m=A; fi
    if [ -f "$D/91-forcespoof.conf" ] && [ "$(cat "$K/dt.conf" 2>/dev/null)" = n ]; then m=capture; fi
    if [ "$m" = A ] && [ -f "$K/tap.conf" ]; then m="A (+ traffic log)"; fi
    if { [ -f "$D/91-forcespoof.conf" ] || addin_installed; } && [ -f "$D/95-mpc310.conf" ] && [ -f "$K/b.conf" ]; then m=B; fi
    addin_installed && echo "addin: installed (SHIFT + MENU switch $( [ -f "$K/switch.conf" ] && echo on || echo off))"
    echo "mode: $m (dt.conf $(cat "$K/dt.conf" 2>/dev/null || echo none), force $(cat "$K/net.conf" 2>/dev/null || echo -))"
    grep -E "link|bridge" /tmp/forcespoof.log 2>/dev/null | tail -n 4 || true ;;
*) echo "usage: $0 stock|a|b|capture|status|switch stock|b"; exit 2 ;;
esac
