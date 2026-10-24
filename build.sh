#!/bin/sh
# Build everything from source (Docker + QEMU). Needs your own MPC 3.10.0.49 binary at local/MPC (or MPC_BIN=...),
# used for the OLED font and the script checksum; nothing from it is written into the repo.
set -eu
cd "$(dirname "$0")"
[ -f "${MPC_BIN:-local/MPC}" ] || { echo "put your MPC binary at local/MPC (or set MPC_BIN)"; exit 1; }
export MPC_BIN="$(cd "$(dirname "${MPC_BIN:-local/MPC}")" && pwd)/$(basename "${MPC_BIN:-local/MPC}")"
# Live III shim (aarch64)
docker run --rm --platform linux/arm64 -v "$PWD":/w -w /w/spoof arm64v8/gcc:12-bookworm sh -c '
    gcc -std=gnu11 -O2 -Wall -Wextra -fPIC -shared -o forcespoof.so forcespoof.c ../force-link/combo.c \
        ../force-link/flswitch.c -ldl -lpthread &&
    LD_PRELOAD=$PWD/forcespoof.so /bin/ls / >/dev/null && echo "preload test ok (aarch64)"'
# never install a shim whose preload test failed (one crash-looped MPC): the line above must print "ok"
# Force daemons (armhf) + their offline tests
force-link/build.sh
# device bundles
cp spoof/forcespoof.so link/a/
cp force-link/flnet link/force/
link/b/build.sh
echo "build ok: ./link/forcelink install, then ./link/forcelink a|b|stock"
