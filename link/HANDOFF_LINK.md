# Handoff: Force link (link setup; formerly "version B", renamed 2026-10-25)

# START HERE: state at the end of the 2026-10-25 session (read this block first; everything below is history)

## What this is
An Akai Force used as an extra control surface for an MPC Live III. Live III: MPC OS 3.9.1 (kept for its USB-script SSH
route) running the MPC 3.10.0.49 app on top of it, started at every boot by a USB stick; the Force's own MPC is stopped
and `flsurf` serves its controller (pads, buttons, knobs, OLEDs) over TCP to a signed MPC controller script
(`ForceLink.lua`) on the Live III. Two setups: **link** (this) and **stock** (both devices on their own MPC; "version A/B"
are retired names, A is archived in `archive/force-identity/`).

## Repos and private files (nothing private is in git)
- `~/forcelink` (git, local only, not pushed): this project. Private, git-ignored: `local/MPC` (your MPC 3.10.0.49 binary,
  needed to build: OLED font + script checksum), `local/hosts.conf` (device IPs, symlinked at `link/hosts.conf`),
  `local/captures/` (usbmon captures of the real Force: `note-keys`, `note-drum`, `stepseq`, `qlinks`, ...; needed for the
  large Q-link font: `force-link/gen_big_font.py`), `local/pylib`. Generated headers `flfont_mpc.h`, `flfont_big.h`
  and the signed `link/script/ForceLink.ini` are never committed. **Never publish the checksum salt or a built stick.**
- `~/push2-force`: private workbench (firmware images, MPC binaries, early notes). `~/mpc310-on-391` + mpc-vst-plugins
  `tools/mpc310_on_391/` (PR #545, merged 2026-10-25): the public "MPC 3.10 on a 3.9.1 Live III" USB stick builder.
- `~/mpc-vst-plugins` branch `usb-bootstrap`: local only; its commit f2fbe7f and docs/NOTES.md still hold the autorun
  SALT VALUE: strip it before any push (user said keep that branch local).

## Build, deploy, switch (all from `~/forcelink`, dev box = WSL)
- `./build.sh`: everything, with tests (Lua mock 52+ checks, flsurf/flcmd/combo/flswitch/oled tests, the QEMU aarch64
  `spoof/stepsnap_test.cpp`, preload tests of both libraries). **Never deploy a build whose preload test failed.**
- `link/forcelink install` (files to both devices, nothing restarts), then `link/forcelink link` (restarts `flsurf` on the
  Force and MPC on the Live III: closes the open project) or just on the Live III: `sh /data/mpc-addins/forcespoof/link.sh link`
  over SSH. `link/forcelink stock` / `status` / `addins` (SHIFT + MENU switch addins) / `cap start|stop <name>` (usbmon
  capture of the real Force, Force in stock). SSH is root key login; the Live III's comes from the boot stick (below).
- Script change = signed `.ini` + `.lua` in `/nvme/MPC Documents/Devices/ForceLink/` on the Live III (done by `link.sh link`).
- Logs: Live III `journalctl -u acvs | grep ForceLink` (script `print`s), `/tmp/forcespoof.log` (shim + `stepsnap:` lines);
  Force `/tmp/forcelink.log` (flsurf; `-r` adds raw bytes), `/tmp/forcelink-switch.log` (stock addin).

## Device state right now (checked 2026-10-25, matches the builds)
Live III: MPC 3.10.0.49 in link, home setup `link`, shim with the stepsnap patch active (guard counter 0, no
`stepsnap.off`). Force: `flsurf` running (its MPC stopped). Boot stick (LEXAR, in the Live III) brings the Live III up in
3.10 + link by itself at every boot (hands-free, ~30 s; `disable-310` file on the stick = plain 3.9.1; old sticks in
`autorun.prev*`). SHIFT + MENU (hold, release, MENU) toggles the Force stock <-> link from either device.

## What works (device-verified unless noted)
SHIFT + MENU switch; MATRIX (8x8 clip window, colours, launch, arrows); drum NOTE (4 banks of 4x4 as the real Force:
unconfirmed that the real one shows 4 banks, it showed one with a 16-pad kit); Q-links: 2 Force pages via KNOBS (Live III
banks 1+2 / 3+4), knob j of bank b = MPC Q-link `4*(3-j)+b`, MPC's own relative mapping with flsurf capping steps at +-1,
name view + large value view while touched (pixel-identical to the real Force, built, not hand-tested), pan bars;
melodic NOTE (chromatic rows a 4th apart, roots C, green press feedback, UP/DOWN octave: built, not hand-tested);
STEP SEQ on real note data (see below): clip creation, lane following the Live III's last hit pad, adding/removing steps
all worked in the user's tests; pad quadrant select works.

## STEP SEQ (the active work)
Design and assumptions: the "STEP SEQ rebuilt" + "first hand test" + "second hand test" sections below, FINDINGS 27-28.
In short: `spoof/stepsnap.c` patches MPC's `events.GetSize()` (3.10.0.49 only, word-for-word check, crash guard, fault-safe
reads) so the script's 250 ms `GetSize()` polls make the library read the clip's note list; it answers the script's
subscription (note, window, 32 steps) with per-step velocities by sysex on the Force Link port; the script draws the real
Force's page (FINDINGS 23) and edits with `CreateNoteEvent` / `DeleteEvent` (own string, or MPC's own encoder's string
fetched by the library for notes MPC already held). The page is sticky (chosen on entry: library + drum track + pad bank
A -> real-data page, else the old MPC-step-button page).
**Status (hand tests 2026-10-25, last build): works.** Drum STEP SEQ: pad select, 16 levels, adding steps, overdubbed
hits showing. Melodic STEP SEQ: 16 lanes, steps, audition, in key. Q-link value view, pan bars and melodic NOTE hand-tested OK.
Fixes this round:
- MPC hands our own `pads.Press` back to the script as a ch9 pad note; those echoes are ignored for 2 ticks after our press,
  note-offs ignored (they toggled steps).
- This kit's drum pads A01-A16 are notes 36-51 in order (`DEFAULT_NOTES`); pads 1-3 confirmed from the log, rest hand-tested OK.
- Melodic tracks use the same real-data page (`padNote`): lane p = scale step p from the root (chromatic: keyBase + p).
  Picking a lane plays it: script sends `audition <note> <vel>` to flsurf, which injects a ch9 note-on/off (250 ms) for
  the track; lane notes 54-63 are taken off the script's pad mapping for 3 ticks (`readd`).
- **Scale and root follow the Live III (FINDINGS 29).** `spoof/stepsnap.c` hooks MPC's pad-perform layout builder
  (0x16c1224, first two words checked) to capture its `this`; `scale_check` reads settings = `*(this+0x118)`: scale type
  +0x260, root +0x2b8, third field +0x330 (always 2; unknown) and sends sysex `07 <type> <root> <third>` when they
  change (re-sent on a new subscribe). Needs the Live III to have built a pad layout once since MPC started (open NOTE on a
  melodic track or change the scale). Root 0 = A; scale type 0 Chromatic, 1 Major, 2 Natural Minor, 3 Harmonic Minor,
  4 Pent Major, 5 Pent Minor, 6 Dorian, 7 Phrygian, 8 Lydian, 9 Mixolydian, 10 Aeolian, 11 Locrian, 12 Blues, 13 Flamenco,
  14 Gypsy, 15 Hungarian Gypsy, 16 Persian, 17 Major Bebop, 18 Whole Tone (confirmed 0-2, 6, 15 on the device; interval
  sets of the exotic ones are guesses). Chromatic = 4ths layout, else in-key layout via flsurf `notemap scale <root> <mask>`.
- SHIFT+NOTE runs `probeScale` (dumps what scripts can reach; found nothing useful, can go).
**Still open:**
1. Drum pad->note map is assumed (36 + pad), pad bank A only; other banks / non-sequential kits show wrong lanes or a dim
   page. The memory-reading route (as for the scale) could read the program's real map: find the pad-note table.
2. No playhead step. Skipped on purpose: a clip's own length/position is unreadable by scripts and a global-playhead
   modulo would drift. Worth building only if the clip length can be read from memory (same technique).
3. Unverified: length of an auto-created clip; `DeleteEvent` with MPC's own string removing exactly the right note
   (log: `step N - note X at T (MPC's string): true/false`).
4. The "third field" (+0x330) and the chord/bank-behaviour fields of the pad-perform object (see serializer 0x17a7468:
   Type +0x208, ScaleBasedChord +0x388, ChromaticChord +0x3e0, BankBehaviour +0x4e8, StartOctave, Velocity) are unmapped.
5. Exotic scale interval sets unverified (Flamenco, Gypsy, Persian, Major Bebop).
6. Diagnostic logging is on (`events:`, `notes in the list:`, `ch9 note`, `pad ... pressed`): noisy, thin out once stable.

## Not tested by hand yet (built and deployed 2026-10-25)
Nothing outstanding from this list (all hand-tested 2026-10-25).
docs/DEVICE_TESTS.md has the plan (sections 3, 4, 5).

## Safety switches and rules
- Patch off: `touch /data/mpc-addins/forcespoof/stepsnap.off` on the Live III, restart MPC. It also disables itself after
  two starts that did not survive 30 s (`stepsnap.guard`; delete it to re-arm).
- Back to plain: `link/forcelink stock`, or reboot the Live III with the stick out. A reboot always comes up in stock/3.9.1
  unless the stick is in.
- Ask before restarting MPC when the user may have a project open (each Live III deploy restarts it); never run a build
  whose QEMU preload test failed; stage files as `.new` then `mv`; the safety classifier stops long low-level C blocks
  (write them in small edits); user approval does not override it.

## Ideas queue (after STEP SEQ is solid)
Read the program's pad note map; clip-relative playhead; the real Force's in-key NOTE layout (needs scale/root: same
memory-reading technique); capture the real Force with a 17+ pad kit (does NOTE show 4 banks?); a public release of
forcelink (README, signed-script build instructions, no Akai files); tell Eurikon / poloq about the stick tool.

---
(older sections, newest last)

Read first: `README.md` (this folder: switching, files, sessions), `../FINDINGS.md` sections 13-18 (all device facts).

## Switching
`./forcelink stock | a | b | capture | status | install` from this folder. Each switch restarts MPC on the devices it
changes. After a Live III reboot, run the USB stick once for SSH, then switch again.

## State when written
Both devices in link. Live III: MPC 3.10.0.49, ForceLink script loaded, Force Link port connected. Force: flsurf running.

## Feature status (device-tested unless marked)
- MATRIX: clip colours (dim stopped, bright playing, blink queued), clip and row launch, arrows move the window, row
  launch / track select / stop-row / arrow LEDs: working.
- NOTE: 64 plain notes (36-99, bottom-left first) into the selected track, MPC pad colours: working.
- Q-links: Force knobs drive MPC Q-links, KNOBS switches 1-8 / 9-16, names on OLEDs: working. Opening the MPC Q-link
  mode menu from the Force does not work (API button presses have no effect); use the Live III's own Q-LINK hold.
- PLAY / REC: working.
- STEP SEQ (latest build, NOT yet tested): 32 steps as two bars of MPC's own 16-step sequencer (bar on the playhead is
  live, other bar last-seen), pad select bottom-left (pads.Tap, 1-based), velocity levels bottom-right (play only),
  LEFT/RIGHT page bar pairs. Known problems before the last fixes: bar B going blank, steps landing in the wrong bar,
  16 LEVELS being toggled (removed). The log line "ForceLink: bar N ... steps" shows step colour/brightness per bar.

## Limits found
- The events list API itself only creates/moves/deletes, but `#GetState` probably reads events (section below).
- Script button presses (controlSurface.<button>.Press/Hold) did not act for Q-LINK or 16 LEVELS.

## Made to match the real Force (2026-10-24, offline; staged, not deployed)
- Q-link OLEDs: MPC's own font and geometry, pixel-exact against 11 captured real-Force frames (FINDINGS 20).
  Bar = Q-link knob position; value changes redraw only the bar. Build: `force-link/build.sh` (generates
  `flfont_mpc.h` from the local MPC binary; keep it out of any repo), then `link/script/build.sh`.
- NOTE on drum tracks: the Force's bank layout (A bottom-left, B bottom-right, C top-left, D top-right, 4x4 each),
  every pad mapped to its absolute program pad (AddNoteToVirtualPad), colours from that pad. Melodic tracks unchanged.
- Still to capture for melodic NOTE (about 5 min at the Force): `./forcelink stock`, then on the Force
  `cat /dev/usbmon2 > /tmp/note.bin` (as in FINDINGS 10) while: NOTE on a keygroup track, octave up/down, change
  scale/root in its settings, press a few pads; also a pan Q-link page. Decode with
  `tools/force_capture_render.py /tmp/note.bin out` (pad colours, LEDs, OLEDs).
- Tests: `script/test_forcelink.lua`, `flsurf_test` 4/4, `oled_test`, `cmd_test`.

## Found offline 2026-10-24: events may be readable from Lua -- NO (device, 2026-10-25, FINDINGS 25)
`#GetState` is nil from scripts. Use the shim route below ("Fallback"), with event offsets from the serialiser.

### (original notes)
- Every scriptable object gets `#GetState` ("Provides the state of this object") registered next to `Help` in the
  Lua registrar (0x2efdb04, unconditional). `mpc.GetState()` gives the whole tree; `DiffStates(a, b)` diffs two.
  An internal JS test harness uses `JSON.stringify(mpc.GetState())`. Returns a `Scripting::State` (via a future).
- The event serialiser (0x2f972c0) writes `NumEvents` and `Events` / `Event N` with: TypeString, IsSelected, IsMuted,
  Time, Is*Event flags, and for notes `NoteEvent`: Note, MidiNote, Velocity, MidiVelocity, Length, NoteEnd,
  ModifierType, ModifierValue, Probability, Ratchet. It is called from the state of `ScriptableClipLanesWrapper`
  (= mpc.clipMatrix: Tracks / Clips / events) and `ScriptableProject` (Sequences / Sequence N / events).
- In memory: event list = rb-tree at list+0x60 (nodes from list+0x78, event at node+0x20), count at list+0x88.
- Encoded events (CreateNoteEvent's string, DeleteEvent's arg) are base64.
- Probe is in the staged build: SHIFT + STEP SEQ logs `ForceLink: probe state.<obj>...` for seqEvents, clipEvents,
  currentSequence, clipMatrix, project (400 lines each max). Next device step: place 2-3 notes, press SHIFT + STEP
  SEQ, `journalctl -u acvs | grep 'probe state'`. Risk: if GetState waits on the thread the script runs on it
  could hang MPC; `forcelink stock` / restart acvs recovers.
- If it works, the STEP page reads real 32 steps from GetState (Time / Note / Velocity) and edits with
  CreateNoteEvent / DeleteEvent; the shim route below is only needed if it doesn't.

## Fallback: reading sequence data inside MPC via the shim (the "reverse engineering version")
Offline start point (MPC 3.10.0.49 only): the script's `events.GetSize()` is implemented at 0x2f939a0. It loads
wrapper+0x98, calls its virtual +0x10 to get the event list, and reads the count at list+0x88. Plan:
1. Shim hook at that function to capture the event-list pointer when the script calls GetSize.
2. Dump the list and its entries to the log on the device while known notes are placed; work out the entry layout
   (time in ticks, note, velocity, length).
3. Shim sends decoded notes for the current track to the script over the Force Link port; the STEP page draws real
   32 steps from them and edits with CreateNoteEvent / DeleteEvent.
Rules for this: build and preload-test under QEMU first; never deploy a build whose preload test failed (one did and
crash-looped MPC); `forcelink stock` or a reboot recovers.

## Learning from the real Force instead of guessing
The Force's own MPC already implements NOTE and STEP SEQ on this controller, so its traffic is a reference:
- Capture it: `forcelink stock` (Force runs its own MPC), then usbmon on the Force while using NOTE / STEP SEQ
  (tools in `../tools/`: usbmon_bin_parse.py, force_pad_grid_dump.py, as in FINDINGS 10-12). That gives the exact pad
  layout, colours, LED values, the 32-step page, pad select and 16 levels as the real Force draws them.
- The MPC binary contains the Force surface code (ADA2 step-sequencer and pad responders); reading those routines
  would show the logic, but the captures are the cheaper first step.
Use the captures to copy layout and colours into ForceLink.lua; sequence data still needs the internal route.

## Files
- `script/ForceLink.lua` (script), `script/test_forcelink.lua` (mock API test), `script/build.sh` (signs + gathers), `script/ForceLink.ini.in`.
- `../force-link/flsurf.c` (+ `flsurf_test.c`): the Force side of link.
- `../spoof/forcespoof.c`: the Live III shim.
Deploy a script change: `cd b && ./build.sh`, then copy ForceLink.ini/.lua to the Live III
`/data/mpc-addins/forcespoof/script/` and `/nvme/MPC Documents/Devices/ForceLink/` and restart acvs (or `forcelink install`
then `forcelink link`).

## Device test results 2026-10-25 (docs/DEVICE_TESTS.md)
- 7 (captures of the real Force): done, FINDINGS 21-24. 1 (switch): passes after the unique-unit fix. 6 (#GetState):
  no, FINDINGS 25. 2 (MATRIX basics): OK. 4 (drum NOTE): OK; the Live III follows to the bank of the pad hit.
- 3 (Q-link OLEDs): KNOBS only toggles 1-8 / 9-16 (expected); bars jump to max and back instead of following the
  value; turning a Q-link on the Live III does not update the Force. Diagnostic log line added
  (`ForceLink: qlink <i> bank <b> pos=... value=...`); read it after the next Live III restart.
- 5 (STEP SEQ, current version): unreliable (other pads' steps appear/disappear, pad select off, bottom-right lacks
  pad colours, bar B lit white only). Rebuild on the shim's event reading + the real Force layout (FINDINGS 23).

## State 2026-10-25 (end of the device test session)
Working on the device: SHIFT + MENU switch (both devices, after the unique-unit fix), MATRIX, drum NOTE (4 banks),
Q-links: two Force pages via KNOBS (page 1 = Live III banks 1 + 2, page 2 = 3 + 4), knob j of bank b = MPC Q-link
4 * (3 - j) + b, empty ones named by the Live's number, MPC's own relative mapping with flsurf capping steps at 1
(user: "seems better"). Boot stick: Live III comes up in 3.10 + link by itself; Force reboot fine.
Diagnostics still in the script: one `ForceLink: qlink ...` log line per Q-link change and timer tick (remove when done).

Next, in order (from the real-Force captures, FINDINGS 21-24):
1. Q-link OLEDs: the large value view while a knob is touched or turned (glyphs to harvest from
   `local/captures/qlinks.bin`), bipolar bar for pan-like targets.
2. Melodic NOTE: rows a 4th apart (chromatic), in-key layout (rows 3 scale steps), coloured roots, green press
   feedback (needs the track's scale/root: not in the script API; chromatic + root C first, or read from MPC).
3. Drum NOTE: check with a kit using more than 16 pads whether the real Force shows 4 banks (user's photo test).
4. STEP SEQ rebuild: the real Force page (FINDINGS 23) on real event data from the shim (FINDINGS 25 route).
Known: the Live III follows to the bank of a pad hit from the Force (MPC behaviour); the Force's MPC once stopped
answering its controller after SHIFT + NOTE (restart fixed it).

## Built offline 2026-10-25 (not yet on the devices)
1. Q-link OLEDs: value view in the large font while a Force knob is touched (and 0.5 s after), centre bars for pan
   values; large glyphs harvested at build time from `local/captures/qlinks.bin` (gen_big_font.py, fingerprints only),
   all 200 captured value strings pixel-identical (bigfont_check.py); letters not in the capture use the scaled small
   font. flcmd `qname` / `qval`.
2. Melodic NOTE: the real Force's chromatic layout (rows a 4th apart, bottom-left C 48), roots (C) in the track colour,
   other pads dim white, pressed note bright green and same pitch class dim green (flsurf `notemap keys <base>` copies
   each press on channel 16 for the script), UP / DOWN = octave. In-key layout and the track's own root/scale not done
   (not readable from scripts).
Q-link diagnostics removed from the script. Deploy: `forcelink install`, then `forcelink link` (restarts MPC on the
Live III and flsurf on the Force).

## STEP SEQ rebuilt on real note data (built 2026-10-25, DEPLOYED to the Live III the same day; page not yet tried by hand)

**What it is.** The real Force's STEP SEQ page (FINDINGS 23) driven by the notes MPC actually holds, not by MPC's 16
step lights. Layout (Force rows from the top): rows 0-3 = 32 steps (bars A + B), an empty step is the selected pad's colour
at 5 %, a set step is coloured by its velocity (yellow-orange soft to red full, the real Force's gradient), the
playhead step is green; rows 4-7 cols 0-3 = the 16 pads of bank A (selected = white, others 25 % of their colour);
cols 4-7 = the 16 velocity levels (same gradient). LEFT / RIGHT page bar pairs; while playing the page follows the playhead.

**How it works (three parts).**
1. `spoof/stepsnap.c` (in the Live III shim, link mode only): patches the tail of `events.GetSize()` (FINDINGS 27) so that
   every time the script calls it, the list MPC just looked up is walked and answered on the Force Link port. Replies are
   sysex `F0 7D 53 ...` (protocol in `spoof/stepsnap.h`): `03` hello, `02` the 32 velocities of the subscribed note,
   `04` the notes in the list. Safety: patch only if the 13 words at 0x2f939a0 are exactly 3.10.0.49's
   (`spoof/check_orig.py`); all reads fault-safe; a crash guard (`/data/mpc-addins/forcespoof/stepsnap.guard`, counts starts
   that did not survive 30 s, disables the patch at 2); `touch /data/mpc-addins/forcespoof/stepsnap.off` switches it off.
2. `link/script/ForceLink.lua`: in STEP mode it subscribes (`F0 7D 53 01 note start step n`) and every 250 ms calls
   `GetSize()` on the selected clip's events and the sequence's (that is what makes the library read). The page is drawn from
   the answers; if no answer for 5 s, off a drum track, or not bank A, the OLD page (MPC's step lights) is used.
3. Editing: an empty step -> `events.CreateNoteEvent(note, tick, velocity/127, 120, 100)` (inserts the note; the tapped
   velocity, or the fixed level); a set step that this script created -> `events.DeleteEvent(<its encoded string>)`; a set
   step that MPC already had -> MPC's own step button (`stepSequencer.Tap`, needs that bar to be MPC's step view; the old
   JumpToPulse mechanism). Tapping a level pad fixes the entry velocity to that level (tap it again to release); tapping a
   pad selects it (and plays it). Every edit logs one `ForceLink: step ...` line.

**Deployed state (checked on the device 2026-10-25).** `forcespoof.so` and `ForceLink.lua` match the build; the shim log
(`/tmp/forcespoof.log` on the Live III) shows `stepsnap: patched events.GetSize() at 0xaaaaada339bc` and a first read
(`list ...: 0 events`); MPC ran 30+ s with no restart, the guard cleared itself. The Force side is unchanged.

**Things that are assumptions until tried (check in this order).**
1. Time base: event times are assumed to start at 0 at the start of the clip (`STEP_ORIGIN = 0` in the script). The shim
   logs the first 8 notes of every list it reads (`stepsnap:   time T note N vel V len L`): if the steps do not line up with
   what MPC shows, compare those times with the steps and set `STEP_ORIGIN`.
2. `CreateNoteEvent`'s velocity is passed as 0-1 (like `SetNoteVelocity`'s "normalised float"): if added steps come out
   very quiet or loud, it takes 0-127 instead (the script log line shows the value).
3. `DeleteEvent` with the string `CreateNoteEvent` returned: expected to remove exactly that note. Notes MPC already
   held are removed with MPC's step button; if that misbehaves the fix is the encoded format (FINDINGS 27) or deleting
   via the library.
4. Only bank A of the drum program, with MPC's default note numbers (A01 = 37, A02 = 36, A03 = 42 ...): a program with
   other note assignments shows other pads' steps. `pads.CurrentBank` other than 0 uses the old page.
5. The replies come back as sysex on the Force Link seq port and need `SetHandleSysex(true)`: if the log never shows
   `ForceLink: stepsnap active`, the sysex path is the problem, not the patch.

**Device test (docs/DEVICE_TESTS.md section 5, rewritten).** Force in link, a drum track with a clip that has a few
notes, Live III on the clip's track:
1. Press STEP SEQ. Within ~1 s the page should show the real steps (set ones red-orange, others very dim). The Live III
   log (`journalctl -u acvs | grep ForceLink`) says `stepsnap active`; the shim log lists the clip's first notes.
2. Tap an empty step: a note appears in MPC at that step (and the Force lights it with the velocity you tapped). Tap it
   again: it goes. Try both bars and LEFT / RIGHT.
3. Tap another pad (bottom-left 4x4): the steps shown change to that pad's; tap a level pad: new steps get that velocity.
4. Play: the green step follows the playhead; the page follows into bar 2.
Report what differs; the logs (`ForceLink: step ...`, `stepsnap: ...`) say where.

**If MPC misbehaves after this deploy:** `touch /data/mpc-addins/forcespoof/stepsnap.off` and restart MPC, or
`./forcelink stock`; two crashes disable the patch by themselves.

**Next after this:** read the track's real note numbers for pads beyond A01-A16 / other programs; melodic STEP SEQ lanes;
the in-key NOTE layout (needs the track's scale/root from memory too: same technique, another function).

## STEP SEQ, first hand test and the fixes (2026-10-25, later; DEPLOYED, Live III MPC restarted)
**What the first test showed (user): pad quadrant added steps to "pad 16" and lit like a step sequencer, the Live III's pad
did not change, steps went "somewhere", lights flickered.** Causes, from the logs:
1. Entering STEP SEQ asked MPC for its own step-sequencer mode. There MPC's 16 pads ARE its step buttons and its pad lights
   ARE its step lights, so `pads.Tap` toggled steps and `pads.LightRGBColor` returned step lights. **Fix:** with the library
   active (`snap.on`) the script never asks for that mode; MPC stays in its pad mode (as in NOTE), so a tap selects and
   plays the pad and its colour is the program's. The old page (which needs MPC's step mode) is only the fallback.
2. The page followed MPC's GLOBAL playhead (bar ~32 = tick 123,840) but a clip's events are clip-relative from tick 0 (the
   shim log lists real clips starting at time 0), so steps were created at ticks far outside the clip. **Fix:** no
   playhead following; the window is paged by LEFT / RIGHT from bar pair 0, up to one pair past the last note (the library
   reports the last note's end in the `04` message). The clip's own length and position are not readable by scripts, so the
   green playhead step is not shown (the real Force shows one: open).
3. Flicker: the view changed each bar while following the playhead (rows were dropped on every view change). Gone with 2.
4. Removing a note MPC already held used MPC's step button, which needs MPC's step mode (not active now). **New:** the library
   asks MPC's OWN event encoder (0x2f9dda0, words checked first; called on MPC's script thread from the GetSize hook) for
   the string `DeleteEvent` takes, and sends it (about 2.4 KB of sysex, shim buffers now 8 KB) to the script, which then
   calls `DeleteEvent`. Notes the script created itself are deleted with the string `CreateNoteEvent` returned. Both log
   (`ForceLink: step ... (MPC's string)`, shim log `stepsnap: encoded note ...`). Disable by creating
   `/data/mpc-addins/forcespoof/stepsnap.off` (everything) - there is no separate switch for the encoder call.
Confirmed on the device: the patch re-installs after the restart, the library's hello reaches the script
(`ForceLink: stepsnap active (library version 1)`), so the sysex path library -> Force Link port -> script works.

**Try again (same steps as docs/DEVICE_TESTS.md section 5), noting:**
- bottom-left 4x4 should now select and play the pad (Live III shows it selected) and keep the program colours;
- steps you add appear in MPC at the right place (check in MPC's own editor), within the first bar pairs;
- tapping a set step should remove it (watch for `asked MPC's string` then `(MPC's string): true` in the log);
- if a step lands one step off, or all steps are shifted: look at the shim log's first notes of the list and set
  `STEP_ORIGIN` in the script.

## STEP SEQ, second hand test and fixes (2026-10-25, later; DEPLOYED)
Findings from the log of that test: `no clip at 0,6: CreateClip -> nil` (the call returns nothing but the clip is made); from
10:31:41 the OLD page's `bar N (pair 0) steps ...` lines appear: the page had flipped back to the legacy MPC-step-button
page (my 5 s "no answers" / bank / track test kept re-deciding), which is what added "random notes", broke pad selection
and flickered. Changes:
- **Sticky page**: chosen when STEP SEQ is entered (library there + drum track + pad bank A -> real-data page, else the old
  page with MPC's step mode) and kept until left. In the real-data page an unsupported track/bank shows a dim page, the TUI
  says "drum track, pad bank A only", and taps are ignored (they never go to MPC's step buttons).
- **16 levels**: velocity is passed to `Press` as a float 0-1 (1/16 ... 1): the API returns velocities as floats, so
  ints 8-127 all clamped to full. If the levels still sound alike, the pad API ignores velocity (then FullLevel, MPC's
  "full level" pad property, may be on: `pads.FullLevel`).
- **Following the Live III's pad**: works out from our own presses whether `LastHitPad` counts from 0 or 1 (logged:
  `LastHitPad counts from N`), and ignores changes within ~1 s of our own press.
- **Log**: every pad press in the real-data page logs row/col/velocity and `recording= overdub= playing=`: pad presses
  from the Force are recorded by MPC like any hit when it is recording / overdubbing (that, not the page, may be the
  "random notes" if they appear while pressing the pad quadrant).
- A fixed level (a level pad tapped) stays until tapped again: all new steps then get that velocity (the earlier steps
  at vel 64 in the log were level 8 left on). The selected level is white.
