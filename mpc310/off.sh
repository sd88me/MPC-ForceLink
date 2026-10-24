#!/bin/sh
# Back to the stock MPC 3.9.1 application. --restore-settings also puts back the MPC.settings saved by on.sh.
set -eu
D=/run/systemd/system/acvs.service.d
S=/data/Settings/MPC/MPC.settings
rm -f "$D/95-mpc310.conf"
systemctl daemon-reload
if [ "${1:-}" = "--restore-settings" ] && [ -f "$S.bak-before-310" ]; then
    systemctl stop acvs
    cp -p "$S" "$S.after-310" && cp -p "$S.bak-before-310" "$S" && echo "MPC.settings restored (3.10's copy kept as $S.after-310)"
    systemctl start acvs
else
    systemctl restart acvs
fi
sleep 5
p=$(pidof MPC || true)
echo "acvs: $(systemctl is-active acvs), MPC pid ${p:-none}"
[ -n "$p" ] && strings /proc/$p/exe | grep -m1 -E '^3\.[0-9]+\.[0-9.]+ \(' || true
