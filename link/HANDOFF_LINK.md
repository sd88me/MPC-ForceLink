# Handoff: Force link (link setup; formerly "version B", renamed 2026-10-25)

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
