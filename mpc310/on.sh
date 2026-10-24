#!/bin/sh
# Live III (MPC OS 3.9.1, root): run the MPC 3.10 application on the 3.9.1 system, until off.sh or the next reboot.
# Nothing on the read-only root changes: a /run drop-in bind-mounts the 3.10 binary and /usr/share/Akai over the 3.9.1
# ones inside MPC's own mount namespace (BindReadOnlyPaths), and the stock launcher starts it. Backs up MPC.settings
# first (off.sh --restore-settings puts it back). Closes the open project: save first.
set -eu
K=$(cd "$(dirname "$0")" && pwd)
D=/run/systemd/system/acvs.service.d
S=/data/Settings/MPC/MPC.settings
[ -x "$K/MPC" ] && [ -d "$K/Akai/SME0" ] || { echo "kit incomplete in $K"; exit 1; }
cp -p "$S" "$S.bak-before-310" && echo "MPC.settings backed up to $S.bak-before-310"
mkdir -p "$D"
printf '[Service]\n# MPC 3.10 on the 3.9.1 system (forcelink/mpc310). Remove with off.sh.\nBindReadOnlyPaths=%s/MPC:/usr/bin/MPC %s/Akai:/usr/share/Akai\n' "$K" "$K" > "$D/95-mpc310.conf.new"
mv "$D/95-mpc310.conf.new" "$D/95-mpc310.conf"
systemctl daemon-reload
systemctl restart acvs
sleep 5
p=$(pidof MPC || true)
echo "acvs: $(systemctl is-active acvs), MPC pid ${p:-none}"
[ -n "$p" ] && strings /proc/$p/exe | grep -m1 -E '^3\.[0-9]+\.[0-9.]+ \(' || true
