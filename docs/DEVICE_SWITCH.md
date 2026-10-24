# Switching setups on the devices (design, 2026-10-24, not built yet)

Goal: change between stock, A and B with a button combo on either device, with no computer and no USB stick after
the first install. Same model as MPC Skipback and the button remap: an **addin**, a library MPC preloads, watching the
controller's button stream (`snd_rawmidi_read` on the "Private" port) and driving its LEDs. Installed once with the
shared addin installer (`tools/release/addin/` in mpc-vst-plugins), which keeps the other addins' LD_PRELOAD entries and
survives reboots (`mpc-addins-reload.service` handles the cold-boot `/etc` overlay).

## Pieces
| Device / setup | What sees the combo | How it switches |
|---|---|---|
| Force, stock (its MPC runs) | `forcelink` addin (armv7) in the Force's MPC | runs `link.sh a|b` detached (`systemd-run`), which stops MPC and starts flnet/flsurf |
| Force, A or B (its MPC stopped) | `flnet` / `flsurf` (they own the controller) | run `link.sh stock|a|b` detached |
| Live III, any setup | `forcelink` addin (aarch64) = today's `forcespoof.so`, made an addin | runs its `link.sh` detached (drop-ins in /run, restart acvs) |

The two sides tell each other over the network (a small TCP control port; the peer's IP is in the addin's settings
file, written at install), so one combo on either device switches both. Each side checks the other answered before
restarting anything; if the peer is off, only the local side switches and the chooser says so.

## The combo (proposed)
Hold **SHIFT + MENU** about 1 s: a chooser opens on the pads. Top row, three pads: stock (white), A (blue), B (green);
the current setup lit bright, the others dim. Tap one to switch, MENU or SHIFT+MENU again to cancel. On the Force the
Q-link OLEDs label the three choices; on the Live III its screen shows a short popup. Chosen so it does not clash with
the button remap's default rules (Menu x2, Mixer x2, Edit x2, Clip+Left, Rec Arm x2) or Skipback (Rec Arm x2).
The chooser takes the pads only while open; MPC never sees those presses.

## Safety
- Switching restarts MPC (closes the open project): the chooser asks for a second tap ("tap again to restart MPC").
- The Live III after a reboot always starts stock (the 3.10 kit drop-in stays in /run), so a bad state is one reboot
  away from normal. The addin itself is a no-op until the combo.
- Same rule as now: a build whose QEMU preload test fails is never installed.

## Order of work
1. Live III addin: forcespoof.so + combo + control port, packaged with the shared installer (aarch64). Removes the
   stick-after-every-reboot step.
2. Combo in flsurf / flnet (Force in A/B).
3. Force addin (armv7) for the stock side.
4. Release zips per device, catalog entries.
