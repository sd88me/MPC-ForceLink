#!/bin/sh
# armhf build in Docker + QEMU (same toolchain image as force-shadow). Output: ./forcelink, ./flsurf
# The OLED font header is generated from the local MPC binary (MPC_BIN, default ../local/MPC); it is never committed.
cd "$(dirname "$0")"
python3 gen_mpc_font.py "${MPC_BIN:-../local/MPC}" flfont_mpc.h || exit 1
docker run --rm --platform linux/arm/v7 -v "$PWD":/build -w /build arm32v7/debian:stretch bash -c '
  printf "deb http://archive.debian.org/debian stretch main\ndeb http://archive.debian.org/debian-security stretch/updates main\n" > /etc/apt/sources.list
  apt-get -o Acquire::Check-Valid-Until=false update -qq && apt-get install -y -qq gcc libc6-dev libasound2-dev >/dev/null &&
  gcc -O2 -Wall -Wextra -o forcelink forcelink.c flparse.c floled.c flcmd.c -lasound && strip forcelink &&
  gcc -O2 -Wall -Wextra -o flsurf flsurf.c floled.c flcmd.c dfu_guard_fn.c -lasound && strip flsurf &&
  gcc -O2 -Wall -Wextra -o flsurf_test flsurf_test.c floled.c flcmd.c dfu_guard_fn.c -lasound && ./flsurf_test &&
  ls -l forcelink flsurf'
