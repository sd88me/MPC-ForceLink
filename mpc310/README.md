# MPC 3.10 application on a Live III running MPC OS 3.9.1 (option 3)

Offline-prepared 2026-10-24, NOT yet run on a device.

Why it should work (offline checks against the official `MPC-3.10.0-Gen2-update.img`, MPC 3.10.0.49):
- Same 23 NEEDED libraries as 3.9.1, glibc ceiling 2.39; all 1376 undefined symbols of the 3.10 binary resolve, with
  matching versions, against the 3.9.1 rootfs libraries. So the 3.10 binary runs on the 3.9.1 system as is (no chroot).
- `acvs.service` and `/usr/bin/az01-launch-MPC` are identical in both releases.
- `/usr/share/Akai`: 5199 files in 3.10 vs 5088; ~9.7 MB new/changed (CV pages, ACVS layout, new `SME0/Devices/Akai`
  Lua scripts, AIR components) plus firmware folders for ACVA3/ACVR2/ADA2S.
- The Live III's own controller firmware (`SME0/Firmware/ACVG/UpdateImage.rbin`) is byte-identical in both releases, so
  3.10 has nothing new to flash into it.
- systemd 255 on the device: `BindReadOnlyPaths=` is supported.

How: `make_kit.sh <update.img> <out>` builds `mpc310/` (MPC + Akai/ + scripts, ~415 MB). Copy it to the Live III
(`/data/mpc310` if there is room, else `/nvme`, but check that the mount is not `noexec` for the binary). `on.sh` writes
`/run/systemd/system/acvs.service.d/95-mpc310.conf` with `BindReadOnlyPaths=<kit>/MPC:/usr/bin/MPC <kit>/Akai:/usr/share/Akai`
and restarts acvs: the stock launcher then starts 3.10 in MPC's private mount namespace. `off.sh [--restore-settings]`
removes it. A reboot also reverts (/run is tmpfs), and the autorun stick keeps working because 3.9.1 boots again.

Risks still open: 3.10 may rewrite `MPC.settings` (on.sh backs it up), projects saved by 3.10 may not open in 3.9.1 (save
copies), and the 3.10 binary may expect newer system services or kernel behaviour (unknown until run). With ADA2S
spoofing, 3.10 has an ADA2S controller firmware updater (`SME0/Firmware/ADA2S`, DFU-enter sysex `F0 47 00 04 72 00 00 F7`):
flnet's DFU guard drops every `72` command before it reaches the Force controller.
