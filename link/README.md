# Force link: an Akai Force as an extra control surface for an MPC Live III

**link** is the setup: the Live III runs the MPC 3.10 app as itself (all its own controls and modes native); the
Force's own MPC is stopped and its pads, buttons, Q-links and OLEDs drive the Live III through the ForceLink
controller script, over the network. The Force's MATRIX / NOTE / STEP SEQ modes run in parallel with whatever the
Live III shows. **stock** is both devices running their own MPC.

The Live III's normal setup is link: its boot stick (`tools/mpc310_on_391/` in mpc-vst-plugins, with forcelink's boot
hook) brings it up in MPC 3.10 + link at every boot. The Force is switched on the device: hold MAIN + MENU until MENU
blinks, then press MENU (either device; `docs/DEVICE_SWITCH.md`). The Live III never restarts for that.

## Dev box

    ./forcelink link | stock       # switch both devices (stock = the Live III on plain 3.9.1 until its next boot)
    ./forcelink status
    ./forcelink install            # copy/refresh the device files after a build (../build.sh)
    ./forcelink addins             # install the MAIN + MENU switch addins on both devices, fresh token
    ./forcelink cap start | cap stop <name>   # record the Force controller's USB traffic (Force in stock)
    ./forcelink capture            # Force stock; Live III on 3.10 with its own controller, traffic logged

`hosts.conf` holds the two IP addresses (`FORCE=`, `LIVE3=`); it lives in `../local/`, out of git. Each switch restarts
MPC on the device that changes, which closes the open project: save first.

## What runs where

| Piece | Where | What |
|---|---|---|
| `flsurf` | Force `/data/forcelink` | owns the controller in link (handshake, heartbeat), serves it as plain MIDI on TCP 6770; drops DFU commands; `F0 7D 46 <text> F7` runs an OLED/LED command (flcmd); with `-s` watches MAIN + MENU |
| `forcelink-switch` addin | Force `/data/mpc-addins/forcelink-switch` | in stock: watches MAIN + MENU inside the Force's own MPC |
| shim `forcespoof.so` (addin `forcespoof`) | Live III `/data/mpc-addins/forcespoof` | opens the "Force Link" ALSA sequencer port inside MPC and relays it to flsurf (`b.conf` = the Force's IP); watches MAIN + MENU; no identity spoofing in link |
| `ForceLink.ini/.lua` | Live III `/nvme/MPC Documents/Devices/ForceLink/` (copied by `link.sh link`) | the controller script, signed for 3.10.0.49 by `script/build.sh` (checksum only; the salt is read from your own MPC binary and never stored) |
| MPC 3.10 kit | Live III `/data/mpc310` | bind-mounted over the 3.9.1 app by a /run drop-in |
| `boot.sh`, `home.conf` | Live III `/data/mpc-addins/forcespoof` | run by the boot stick once per boot: brings the Live III up in link |

Script features: MATRIX (8x8 clip window, colours, launch, row launch, arrows), NOTE (drum tracks in the real Force's
bank layout; other tracks plain notes), STEP SEQ (2 bars of MPC's step sequencer), Q-links on the OLEDs drawn like the
real Force, KNOBS bank 1-8 / 9-16, PLAY / REC. Status and next steps: `HANDOFF_LINK.md`; device tests:
`../docs/DEVICE_TESTS.md`.

Offline tests: `script/test_forcelink.lua` (mock API), `../force-link/flsurf_test.c`, `combo_test.c`,
`flswitch_test.c`, `oled_test.c`, `cmd_test.c`; `../build.sh` runs them.

The earlier Force-identity setup (the Live III's MPC posing as a Force, formerly "version A") is archived in
`../archive/force-identity/` and git history.
