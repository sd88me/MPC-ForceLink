#!/bin/sh
# armhf build in Docker + QEMU (same toolchain image as force-shadow). Output: ./forcelink, ./flsurf, ./forcelink-switch.so
# The OLED font headers are generated locally and never committed: flfont_mpc.h from your MPC binary (MPC_BIN,
# default ../local/MPC), flfont_big.h from a capture of the real Force (QLINK_CAPTURE, default ../local/captures/qlinks.bin).
cd "$(dirname "$0")"
python3 gen_mpc_font.py "${MPC_BIN:-../local/MPC}" flfont_mpc.h || exit 1
python3 gen_big_font.py "${QLINK_CAPTURE:-../local/captures/qlinks.bin}" flfont_big.h || exit 1
docker run --rm --platform linux/arm/v7 -v "$PWD":/build -w /build arm32v7/debian:stretch bash -c '
  printf "deb http://archive.debian.org/debian stretch main\ndeb http://archive.debian.org/debian-security stretch/updates main\n" > /etc/apt/sources.list
  apt-get -o Acquire::Check-Valid-Until=false update -qq && apt-get install -y -qq gcc libc6-dev libasound2-dev >/dev/null &&
  gcc -O2 -Wall -Wextra -o forcelink forcelink.c flparse.c floled.c flcmd.c -lasound && strip forcelink &&
  gcc -O2 -Wall -Wextra -o flsurf flsurf.c floled.c flcmd.c dfu_guard_fn.c combo.c flswitch.c -lasound -lpthread && strip flsurf &&
  gcc -O2 -Wall -Wextra -o flsurf_test flsurf_test.c floled.c flcmd.c dfu_guard_fn.c combo.c flswitch.c -lasound -lpthread && ./flsurf_test &&
  gcc -O2 -Wall -Wextra -fPIC -shared -o forcelink-switch.so flswitch_addin.c combo.c flswitch.c -ldl -lpthread &&
  gcc -O2 -Wall -Wextra -o combo_test combo_test.c combo.c && ./combo_test | tail -1 &&
  gcc -O2 -Wall -Wextra -o flswitch_test flswitch_test.c flswitch.c -lpthread && ./flswitch_test | tail -1 &&
  LD_PRELOAD=$PWD/forcelink-switch.so /bin/ls / >/dev/null && echo "preload test ok (armhf addin)" &&
  ls -l forcelink flsurf forcelink-switch.so'
# never install an addin whose preload test failed: the line above must print "ok"
# flsurf for the Live III (aarch64): USB mode runs it there, against the Force's controller over the cable
docker run --rm --platform linux/arm64 -v "$PWD":/build -w /build arm64v8/debian:stretch bash -c '
  printf "deb http://archive.debian.org/debian stretch main\n" > /etc/apt/sources.list
  apt-get -o Acquire::Check-Valid-Until=false update -qq && apt-get install -y -qq gcc libc6-dev libasound2-dev >/dev/null &&
  gcc -O2 -Wall -Wextra -o flsurf.a64 flsurf.c floled.c flcmd.c dfu_guard_fn.c combo.c flswitch.c -lasound -lpthread && strip flsurf.a64 && ls -l flsurf.a64'
