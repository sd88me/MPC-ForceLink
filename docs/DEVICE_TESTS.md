# Device test plan (staged 2026-10-25)

State when staged: both devices run exactly the repo's builds (checked by checksum). Live III: MPC 3.10.0.49 + B from
the boot stick, ForceLink script loaded, waiting for the Force. Force: stock MPC with the `forcelink-switch` addin
ready. Save projects first: every switch restarts MPC on the device that switches.

Report per step: OK, or what you saw. Log reading is done from the dev box (commands in brackets).

## 1. Setup switch (SHIFT + MENU)
| # | Do | Expect |
|---|---|---|
| 1.1 | Force: hold SHIFT + MENU ~1.5 s, let go | Force MENU LED blinks |
| 1.2 | Force: press MENU within 4 s | Force MPC stops, pads/OLEDs come back as the B surface (~10 s). Live III does not restart, project stays open |
| 1.3 | Force: SHIFT + MENU hold, release, MENU | Force back to its own MPC (stock) |
| 1.4 | Live III: same gesture | Live III MENU blinks, then the Force switches to B; Live III does not restart |
| 1.5 | Live III: same gesture again | Force back to stock |
| 1.6 | Arm, then press a pad, or wait 5 s | cancelled, MENU LED back to normal, nothing restarts |
| 1.7 | Note anything SHIFT + MENU does in MPC itself on either device | (MPC sees the presses too) |
[logs: Live III `/tmp/forcespoof.log`, Force `/tmp/forcelink-switch.log` (stock) or `/tmp/forcelink.log` (B)]

## 2. B basics (regression; Force in B)
MATRIX: clip colours, launch a clip and a row, arrows move the 8x8 window, PLAY / REC. Expect as before.

## 3. Q-link OLEDs (new: drawn like the real Force)
| # | Do | Expect |
|---|---|---|
| 3.1 | Look at the 8 OLEDs | Q-link name centred in small capitals on the top half, full-width outlined bar below (no value text) |
| 3.2 | Turn a knob | only its bar moves, smoothly, matching the MPC's Q-link |
| 3.3 | KNOBS | OLEDs switch to Q-links 9-16 |
Compare with the Force's own MPC (stock) if anything looks off: name size, bar position.

## 4. NOTE on a drum track (new: real Force layout)
| # | Do | Expect |
|---|---|---|
| 4.1 | Live III: select a drum track. Force: NOTE | 4 banks of 4x4: A bottom-left, B bottom-right, C top-left, D top-right, each lit in its pads' colours |
| 4.2 | Hit the bottom-left pad of each quadrant | plays A01, B01, C01, D01 |
| 4.3 | Change the Live III's pad bank, hit the same Force pads | same sounds as 4.2 (independent of the Live III's bank) |
| 4.4 | Select a keygroup/plugin track | pads play plain chromatic notes as before (melodic layout not done yet: see 7) |

## 5. STEP SEQ (fixes from before, never tested)
Live III: 16 LEVELS off. Force: STEP SEQ on a drum track with a 2-bar sequence.
| # | Do | Expect |
|---|---|---|
| 5.1 | Look | top 4 rows = 32 steps (2 bars); bar B not blank when the playhead is in bar A |
| 5.2 | Tap steps in both bars, stopped and playing | the step you tapped toggles, in the right bar |
| 5.3 | Bottom-left 4x4 | selects the pad you edit; it is lit |
| 5.4 | Bottom-right 4x4 | plays the selected pad at 16 velocities |
| 5.5 | LEFT / RIGHT | page through bar pairs |

## 6. Reading sequence data (`#GetState` probe)
Put 2-3 notes in the current sequence, then on the Force: SHIFT + STEP SEQ. Nothing visible happens except a short
message on the Live III screen; I read the result. Risk: if MPC freezes, say so (restart fixes it).
[`journalctl -u acvs | grep 'probe state'` on the Live III]

## 7. Capture the real Force (for melodic NOTE, and to check 3-5 against the original)
Force in stock (step 1.3). For each, I start a recording, you do the actions, then I stop it:
| Capture | Do on the Force (its own MPC) |
|---|---|
| `note-keys` | keygroup/plugin track, NOTE: press a few pads (bottom row, a middle row), octave up/down, change scale and root in NOTE settings |
| `note-drum` | drum track, NOTE: hit pads in each quadrant, page banks if there is a way (arrows) |
| `stepseq` | drum track, STEP SEQ: a 2-bar pattern, tap steps, select a pad, use the velocity levels |
| `qlinks` | KNOBS on a few pages incl. pan and a plugin's params; turn knobs |
[`forcelink cap start`, then `forcelink cap stop <name>`: decodes OLEDs, pad colours and LEDs from `local/captures/`]

## 8. Other addins still fine
Force: Skipback (Rec Arm twice saves), button remap combos (e.g. MENU twice), Remote. Live III: Skipback, Remote.

## 9. Reboots
- Live III: power-cycle with the stick in: back in 3.10 + B by itself (~30 s); then step 1.4 works.
- Force: power-cycle: stock MPC with the switch ready (its addin is persistent); step 1.1 works.

## After
Results go into FINDINGS.md (dated, device) and HANDOFF_B.md; fixes follow from what fails. Melodic NOTE gets built
from the `note-keys` capture.
