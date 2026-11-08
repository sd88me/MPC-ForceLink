#!/bin/sh
# Link-mode build (dev box): sign the controller script for MPC 3.10.0.49 and gather the device files here.
# The checksum is derived with the salt read from your own MPC binary (MPC_BIN, default ../../local/MPC); the salt itself is never
# written anywhere. Keep ForceLink.ini (and this folder) out of any public repo.
set -eu
cd "$(dirname "$0")"
P=../..
cp ForceLink.ini.in ForceLink.ini
python3 $P/tools/sign_script.py "${MPC_BIN:-$P/local/MPC}" ForceLink.lua ForceLink.ini >/dev/null
cp $P/force-link/flsurf $P/force-link/flsurf.a64 $P/force-link/handshake.syx $P/spoof/forcespoof.so .
echo "link build: $(grep -c '^checksum=' ForceLink.ini) checksum line, files: $(ls | tr '\n' ' ')"
