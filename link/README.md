# Force link: an Akai Force as a full Force surface for an MPC Live III

Status 2026-10-24 (device, user-verified): working. The Live III runs the MPC 3.10 app on its stock 3.9.1 system under a
Force (Gen 2) identity. The Force's controller (pads, buttons, Q-links, OLEDs) is carried over the network and behaves as a
real Force surface: clip grid with colours and launch, NOTE, STEP SEQ, MATRIX/MIXER/KNOBS, audio on the Live III.
The Live III's own controls keep working through a bridge (partly mapped, see "To refine").

## Use: one switch, three setups

    ./forcelink stock     # both stock: Force runs its own MPC, Live III runs MPC 3.9.1
    ./forcelink a         # version A (below): Live III = MPC 3.10 as a Force (Gen 2), the Force is its surface
    ./forcelink b         # version B: Live III stays itself, the Force is an extra surface (built, not yet run)
    ./forcelink capture   # Force stock; Live III on MPC 3.10 with its own controller, traffic logged (sessions below)
    ./forcelink status
    ./forcelink install   # copy/refresh the device files (after a rebuild or a map.conf edit)
    ./forcelink probe <hex bytes>   # version A: send raw bytes to the Live III controller

| Setup | Live III | Force | Shared or parallel |
|---|---|---|---|
| stock | MPC 3.9.1, own controls | own MPC | separate instruments |
| A | MPC 3.10 as Force (Gen 2); own controls via the bridge (partly mapped) | surface for the Live III, full Force behaviour | shared: one mode for both |
| B (planned) | MPC 3.10 as itself; all its controls and its 8x8 mode native | extra surface via a controller script | parallel: the Force's grid is independent |

Version A is bundled in `a/` (shim, Live III start-up, `map.conf`). `forcelink on`/`off` still work as aliases of `a`/`stock`.

`hosts.conf` holds the two IP addresses (`FORCE=`, `LIVE3=`); keep it out of any repo.

Each `on`/`off` restarts MPC on the devices involved, which closes the open project: save first.

After a Live III reboot: run the autorun stick once (SSH until the next reboot), then `./forcelink on`. A reboot of either
device is a full "off" for that device (all changes live on /run or in RAM).

## What runs where

| Device | Files | Role |
|---|---|---|
| Force | `/data/forcelink/{flnet,link.sh}` | `flnet` owns the controller (Force MPC stopped) and serves its Public/Private ports on TCP 6760/6761 (side port 6762). Drops any `72` (DFU) command so the controller can never be put into firmware-update mode. |
| Live III | `/data/mpc-addins/forcespoof/{forcespoof.so,live3_init.syx,link.sh,net.conf}` | LD_PRELOAD shim inside MPC: product code `ADA2S`, Public/Private redirected to the Force over TCP, controller id 0x04<->0x40, Live III bridge. |
| Live III | `/data/mpc310/` (MPC 3.10.0.49 + `/usr/share/Akai`, built by `../mpc310/make_kit.sh` from the official update image) | Bind-mounted over the 3.9.1 app by a /run drop-in. |

Sources: `../spoof/forcespoof.c` (+ `bridge_test.c`, `idmap_test.c`), `../force-link/flnet.c` (+ `dfu_guard_test.c`),
`../mpc310/`. Build commands: see `../HANDOFF.md`.

## Switches (Live III, `/data/mpc-addins/forcespoof/`)

| File | Effect |
|---|---|
| `dt.conf` | `s` Force Gen 2 (used), `0`/absent Force Gen 1, `b` Gen 1 board too (no audio), `n` no spoofing |
| `net.conf` | the Force's IP; absent = MPC keeps its own controller |
| `live3.conf` | present = Live III bridge on (needs mode `s` and net.conf) |
| `tap.conf` | present = log controller traffic to `/tmp/native.log` (with net.conf absent: stock capture) |

## Version B (built offline 2026-10-24, not yet run)

| Piece | Where | What |
|---|---|---|
| `flsurf` | Force `/data/forcelink` | owns the controller (handshake, heartbeat), serves it as plain MIDI on TCP 6770: Q-link counts become relative steps, replies dropped, DFU commands dropped; `F0 7D 46 <text> F7` draws OLED text (flcmd) |
| shim B mode | Live III, same `forcespoof.so` | `b.conf` (the Force's IP) opens an ALSA sequencer port "Force Link" inside MPC and relays it to flsurf; no identity spoofing |
| `ForceLink.ini/.lua` | Live III `/nvme/MPC Documents/Devices/ForceLink/` (copied by `link.sh b`) | the controller script, signed for 3.10.0.49 by `b/build.sh` (checksum only; the salt is read from your own MPC binary and never stored) |

Script v1: MATRIX / NOTE / STEP SEQ buttons pick the Force pad mode (NOTE = the MPC 64-pad quadrant mode, 2x2 Force pads per MPC pad; STEP SEQ = the MPC's 16 step buttons on the top two rows). In MATRIX the Force shows its own 8x8 clip window (clip colours, dim when stopped, blinking when queued), pads launch
clips, row launch 1-8 launches scenes, track select 1-8 selects, track-arm row 1-8 stops a track, arrows move the window
(SHIFT: by 8), Q-links 1-8 with names/values on the OLEDs, PLAY/REC, PLAY LED. The Live III keeps all its own controls.

Offline tests: `b/test_forcelink.lua` (mock API, 32/32), `force-link/flsurf_test.c` (4/4). First run on the devices to
check: the script loads (MPC lists "ForceLink" under controllers; a "ForceLink: aperture ok, clips ok" message on screen
says where the clip window object lives), the "Force Link" port shows up, pads/colours/OLEDs.

## Sessions that need someone at the devices (version A)

**1. Live III pad LED quadrants** (with `forcelink a` running). Each pad has four LED quadrants; the 80 LED indexes are
16 pads x 4 + 16 step buttons, order unknown. Light one index at a time and note where it shows:

    for i in 00 01 02 03; do ./forcelink probe f0 47 7f 58 65 00 04 $i 7f 00 00 f7; sleep 2; done   # red, one by one
    ./forcelink probe f0 47 7f 58 65 00 04 00 00 00 00 f7                                            # off again

Write the result into `a/map.conf` as `quad <Force pad idx> <Live III LED>` lines (64 lines for the full 8x8: Force idx
= row*8 + col, row 0 = top), then `./forcelink install` and `./forcelink a`.

**2. The Live III's own 8x8 mode** (MPC 3.10 has "8x8: 64 Pads Mode": each pad quadrant is one Force pad). To copy it in
the bridge we need to see what the pads send in that mode: `./forcelink capture`, then on the Live III switch the pads to
8x8 mode, press each corner of pad 1 (bottom-left) then of pad 16, slowly, and save `/tmp/native.log` from the Live III
(`scp root@<live3>:/tmp/native.log .`). Expected: position or quadrant data per press that today's capture (plain notes)
does not have, plus the start-up table that turns it on.

## To refine

- Pad LEDs: the Live III pads have four LED quadrants each (MPC 3D-style pads). The 80 LED indexes = 16 pads x 4 quadrants
  + 16 step buttons, order unknown; the bridge currently writes one index per pad, so colours land on quadrants. Map with
  a probe (light index n, note where).
- Button LEDs: values pass through 1:1 (Force 0/1/3); Live III uses 0..7. Step buttons are RGB (via `65`), not mirrored yet.
- Unmapped Live III buttons: SET, PREV/NEXT, FULL LEVEL, 16 LEVEL, C, D, TC, SAMPLE EDIT, LOOP ON, LOCATE, MIC ON, the
  button under LOOP ON, OVERDUB, PLAY START, TOUCH STRIP (`04`), touch strip (`41`, CC `21`/`22`).
- Mapping tables are in `forcespoof.c` (`l3_pad`, `l3_btn`, `pad_force_note`, `bridge_led`); `bridge_test.c` must be
  updated with them.
