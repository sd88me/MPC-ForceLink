# Switching setups on the devices

Status 2026-10-24: installed on both devices; both addins load and listen (device). The button gesture itself is not yet
tried by hand.
Version A is no longer a target here: the switch toggles **stock <-> B**.

## Use
On either device: **hold SHIFT + MENU** about 1.5 s until MENU blinks, let go, **press MENU** within 4 s. Both
devices switch to the other setup (MPC restarts on both: the open project closes). Any other press, or waiting,
cancels. If the other device does not answer, only this one switches (logged).

## Pieces
Same model as MPC Skipback and the button remap: libraries MPC preloads, watching the controller's button stream
(`snd_rawmidi_read` on the "Private" port) and blinking its MENU LED, installed once with the shared addin installer
(`link/addin/installer`, copied from mpc-vst-plugins) so they persist across reboots and keep other addins.

| Where | What sees the combo | Switches to |
|---|---|---|
| Live III, stock and B | the shim `forcespoof.so`, installed as addin `forcespoof` (aarch64) | the other one |
| Force, stock (its MPC runs) | addin `forcelink-switch` (armv7, `force-link/flswitch_addin.c`) | B |
| Force, B (its MPC stopped) | `flsurf -s switch.conf` (it owns the controller) | stock |

Shared code: `force-link/combo.c` (the gesture, `combo_test.c`) and `force-link/flswitch.c` (config, TCP 6771
listener, `FLSW <token> <mode>`, `flswitch_test.c`). A switch tells the peer first, then runs
`systemd-run --no-block -p KillMode=process sh link.sh switch <mode>` so it outlives the MPC restart it causes;
`link.sh switch` does nothing if the device is already in that setup. Logs: Live III `/tmp/forcespoof.log`, Force
`/tmp/forcelink-switch.log` (stock) or `/tmp/forcelink.log` (B).

`switch.conf` (`token=`, `peer=`, `script=`) is written by `link/forcelink addins` with a fresh random token; the
listeners refuse requests without it. Keep it private.

## Install
`./build.sh`, `link/forcelink install`, then `link/forcelink addins` (installs both addins, MPC not restarted; they
load at the next switch or reboot). Remove: `sh /data/mpc-addins/<id>/uninstall.sh` on each device.

## Risks
- On this Live III the shared addin drop-in lives in /run (no persistent /etc), as for every other addin there: after a
  reboot the addins (this one too) load only once the autorun stick has run. The Force's is persistent.
- The Live III shim then loads at every MPC start, also after a reboot. A shim that broke MPC would do it at every
  boot; recovery is the autorun stick (SSH) and `uninstall.sh`. Rule kept: never install a build whose preload test
  failed. A reboot still always comes up in stock (the 3.10 drop-in lives in /run).
- On the Force the addin chains with Skipback and the button remap (same `dlsym` approach); not yet tried together.
- MENU presses during the gesture also reach MPC (not swallowed); check on the device whether SHIFT + MENU does
  anything there that gets in the way.
