# Push 2 → Force-style controller for MPC Live III: offline findings (2026-10-10)

**Terminology (2026-10-25):** sections below say "version B" for what is now called **link** (the Force as an
extra surface for the Live III) and "version A" for the archived Force-identity setup.

Sources: `~/roms/L3_MMC` (MMC dump, rootfs = p10, MPC **3.10.0.23** aarch64, product code **ACVG**, RK3588),
Force 3.9.1 rootfs in `~/fwcmp/F`, KikGen libs and wiki and Ableton push-interface in `ref/`. No hardware has been tested.

## 1. MPC 3.10 has an official Lua controller-script layer (new, not in Force 3.9.1)
- Engine: Lua 5.4 + sol2, built into `/usr/bin/MPC`. Example script: `/usr/share/Akai/SME0/Devices/Akai/mpkMiniMkIV.{ini,lua}`.
- `.ini` holds the port-name matches (`input=` and `output=`, in order = port index), `script=` and `checksum=`.
- Search dirs: `/usr/share/Akai/SME0/Devices` **and** `<user root>/MPC Documents/Devices`. User root is chosen from
  devicetree flags: `/storage`, `/data/UserData`, `/nvme` (`inmusic,nvme-fitted`) or `/sdcard`. On the Live III it's
  probably `/nvme`, so the folder is the internal drive's `MPC Documents/Devices/`. **Confirm on hardware.**
- **Checksum = sha256(lua_bytes ‖ 36-char salt)**. The salt is XOR-obfuscated in the binary at file offset
  0x5295d30 (key 0x5f694973d3c78945, decoded by the initializer at 0xc9e148). Verified against the shipped MPK script.
  `tools/sign_script.py <MPC> <lua> [ini]` signs a script. The salt is only ever read from your own dump; don't publish it.
- Verification (fn 0x2069900) is skipped when `getHardwareProduct()` (0x270b830, reads `inmusic,product-code`) is 0,
  meaning desktop MPC software has no checksum gate. That's useful for iterating on a PC first if desktop 3.10 ships the
  same engine (unverified).

### API surface relevant to a Force-style surface (from registered names and docstrings in the binary)
| Need | API |
|---|---|
| 8×8 clip launch | `clipMatrixAperture.SetColumns/SetRows` (≤16×16), `TriggerClip(c,r)`, `TriggerRow(r)`, `StopClipsOnTrack(c)`, `SelectClip`, `SelectTrack`, `MoveToColumn`, `clipProperties[c][r].Colour/State/Selected` (listenable; states Stopped/WillPlay/Playing/WillRecord…), `sceneProperties`, `trackProperties`, `Show()`/`SetLabel()` draws the aperture on the MPC screen |
| Track select / mute / solo / arm | per-track `mute`, `solo`, `recArm` (writeable), track colour; `currentTrack.Next/Previous`; `mutes.AddNoteToTrackMute` |
| 8 encoders → Q-links | `qlinks.AddInfiniteCCToQLink` (1–63 cw / 64–127 ccw, the same scheme Push 2 uses), `SetInfiniteCCMagnitude`, `SetQLinkFineAdjust`, `Touch`, per-qlink `TargetName`, `ValueString`, `IsTouched`, `Rotate(delta)` |
| Pads | `pads.AddNoteToMpcPad` (16, with velocity), `AddNoteToForcePad`, `AddNoteToVirtualPad`, `pads[i].Colour` (listenable), `PressVirtual/TapVirtual` |
| Force buttons (virtual press) | `controlSurface.buttons.force.{launch,note,stepSeq,clip,mixer,matrix,mute,recArm,select,edit,copy,assignA,assignB,master,stopAll,trackSelect1-8,trackAssign1-8,rowLaunch1-8,…}` with `Press/ReleaseAll/DoublePress` (documented as "Force Control Surface.") |
| Step sequencer | `stepSequencer` interface, sequence event API (`CreateNoteEvent`, `DeleteEvent`, `SetNoteVelocity`, `Events`…), transport and position (`sequencer.*`, `BeatPosition`) |
| Modes / UI | `engineMode.RequestModeChange/GetAvailableModes/CurrentMode`, `ui.ShowTUIFeedback`, `popups.*` |
| LEDs / Push config | `midiOut.SendMIDINoteOn/CC/Sysex(outputIndex)`, `script.AddNoteToScript/AddCCToScript` → `HandleMIDINoteOn/CC/Sysex`, `timer` |

## 2. What cannot be done with a Lua script alone
- **Push 2 display**: it's a USB bulk endpoint driven via libusb (VID 0x2982, PID 0x1967, 960×160), not MIDI. Lua has no USB access.
- **Root on Gen2**: SSH needs an Akai-signed cert (`az01-ssh-login`), and the rootfs is signed. No LD_PRELOAD or native
  helper is possible, so the KikGen IamForce route (spoof ADA2 + rawmidi hooks) is **blocked on the Live III**.
  It would otherwise be viable: Force control-surface code (`ADA2PadStepSeqResponder`, `ADA2QLinks`, `ADA2TrackAssign`,
  `ForceLaunchButtons`…) and the ADA2 product entry are all compiled into the 3.10 aarch64 binary.
- **Scale/note layouts on 64 pads**: no obvious "play note into the track" call. Options: 16-pad `AddNoteToMpcPad` with
  MPC pad-perform scales, `AddNoteToVirtualPad` (count unknown), or remap outside the MPC (bridge).

## 3. Architectures
- **A. Pure Lua script (no hacks)**: Push 2 plugged straight into the Live III. Covers clip matrix, scenes, track
  select/mute/solo/arm, Q-links on encoders, velocity pads, LEDs and the touch strip. Parameter names appear on the MPC screen, and the Push display stays dark.
- **B. A + small Linux bridge** (Pi 4/CM4/Zero 2 W with a USB-gadget port): the Push connects to the bridge, and the bridge presents USB-MIDI to the MPC.
  The Lua script streams Q-link names, values, track names and colours as sysex to the bridge, which renders the 8-column display
  (Force-style per-knob readouts) and can do the 64-pad scale remapping. This is the only route that gets you everything.
- **C. IamForce-on-Gen2**: blocked until a Gen2 root exists.

## 4. Hardware test checklist (in order)
1. Signed hello-world `.ini/.lua` in `MPC Documents/Devices/Push2/` → does it load? Where's the log?
2. Push 2 ALSA port names on the Live III (`Ableton Push 2 MIDI 1/2`?), and whether bus power is enough (LEDs dim without the PSU).
3. Infinite-CC sign convention vs Push 2's two's complement.
4. Whether `buttons.force.*` and `AddNoteToForcePad` do anything on non-Force hardware.
5. Virtual pad count and whether `AddNoteToVirtualPad` can carry a 64-pad note layout.

## 5. Live III on 3.9.1 (from `MPC-3.9.1-Gen2-update.img`, rootfs xz at 0x21a7870, MPC 3.9.1.2 f3af35cdc95)
- The Lua device-script loader already exists and uses the same search dirs (system `Devices` + `<root>/MPC Documents/Devices`),
  but the stock firmware ships **no** scripts.
- **No checksum**: the `.ini` parser (0x1a8bb00) reads only `device`, `manufacturer`, `input`, `output` and `script`. There's no
  `scriptVersion` or `checksum` string, and no SHA call reachable from the loader. Unsigned user scripts should load (untested).
- It's the **mapping-only** subset: `AddNoteToMpcPad/ForcePad`, `AddNoteToControl/AddCCToControl` (incl. Force button keys),
  `AddInfiniteCCToQLink`, `AddNoteToScript` + handlers, `SendMIDINoteOn/CC/Sysex`, `timer`, `TapVirtual`.
- **Missing vs 3.10**: `clipMatrixAperture` (clip grid + clip colour/state), per-track mute/solo/recArm properties,
  qlink `TargetName`, `HandleMIDISysex`, `RequestModeChange`, `ShowTUIFeedback`, `AddNoteToVirtualPad`.
  So there's no clip-state or Q-link-name feedback, and no way to know what a knob controls.

## 6. Push 1: the display is plain MIDI SysEx, so a Lua script can drive it
Source: Ableton's Push 1 remote script (`ref/push1/Sysex.py`, `consts.py`, `pushbase_sysex.py`). Push 1 was built by Akai (manufacturer ID 0x47).
- Character LCD, 4 lines × 68 chars, in 4 blocks of 17 (2 encoders per block, so 8.5 chars per knob).
- Write line n (0–3): `F0 47 7F 15 <18+n> 00 45 00 <68 ASCII bytes> F7`. Clear line: `F0 47 7F 15 <1C+n> 00 00 F7`.
- Custom glyphs: 0x00/0x01 up/down arrows, 0x03/0x04 bar left/right, 0x05 split block, 0x06 split dash, 0x1D full block,
  0x1C ellipsis, 0x1E/0x1F right/left arrows, 0x7F select. Enough for Live-style value bars.
- Mode: `F0 47 7F 15 62 00 01 <0=Live,1=User> F7`. Welcome/goodbye: `F0 47 7F 15 01 00 01 <1|0> F7`.
- Aftertouch mode `…5C 00 01 <0 poly|1 mono> F7`, touch-strip mode `…63 00 01 <mode> F7`, pad sensitivity `…5D 00 20 …`.
- Pads: notes 36–99 with velocity, aftertouch. LEDs use a fixed velocity palette (no RGB SysEx). Encoders are relative two's
  complement plus touch notes, the same scheme as Push 2.
- With 3.10's `midiOut.SendMIDISysex` (pass the bytes without F0/F7) plus `qlinks[i].TargetName/ValueString` listeners, Push 1
  can show **per-knob name and value readouts with no bridge**. On 3.9.1 there's no `TargetName`, so only script-chosen labels and bars.
- The MPC already ships `Midi Learn/Ableton Push 1.xmm` (basic note/CC map).

## 7. Autorun = a MockbaMod-style side-service route on Gen2 (Live III), no root/SSH needed
Confirmed in BOTH the 3.10 (ACVG) and 3.9.1 Gen2 MPC binaries, offline. This is a bigger lever than the Lua layer.

- **The MPC app itself runs shell scripts.** When a mounted drive (scanned under `/media/...`, i.e. a USB stick/SD)
  contains an `autorun` set, MPC parses `autorun.json` (rapidjson: `scripts[]` with `path`, `message`, `askQuestion`,
  `yes/noButtonText`), shows "About to run script: ", and on confirm launches the script with a shell.
  Loader at `0x2746cc0`; path built at `0x2747958` ("autorun"); JSON schema parser `0x2744640`.
- **The integrity check is plain, unsalted SHA-256.** `autorun.check` is compared against the hex SHA-256 of the script
  file (hash wrapper `0x2743d00`→`0x2743f2c`→SHA core `0x10d7600`). It does **not** touch the Lua salt global
  (`0x7dc7928`), the salt blob or the XOR key — unlike the device-script checksum. So for any script you write,
  `sha256sum script.sh > autorun.check` satisfies it. (Matches KikGen's "looks like a SHA256 key" note; they didn't
  realise it's trivially regenerable.) **No Akai secret, no signing, no SSH, no defeating the signed rootfs.**
- **Privilege + persistence.** The MPC service runs as root, and the KikGen example script freely does
  `systemctl stop/start inmusic-mpc`, so the autorun script runs with root. A script can therefore launch a **detached
  background daemon** (`nohup mydaemon & disown`) that keeps running after the script returns and MPC restarts — exactly
  the MockbaMod "side service alongside MPC OS" model, but reached through an official, unsigned path.
- **So the Push 2 display IS reachable this way:** a native aarch64 libusb daemon, dropped on a USB stick and started by
  autorun, can claim the Push 2's bulk *display* interface (VID 0x2982 PID 0x1967, 960×160) while MPC keeps the Push's
  *MIDI* interface over ALSA. The Lua device-script then streams qlink names/values/track data to that daemon over a
  virtual MIDI port (or a local socket), and the daemon renders the 8-knob Force-style screen. **No Raspberry Pi needed.**

### Still UNVERIFIED (hardware needed) — do not treat as working yet
1. That autorun actually triggers on a Live III and from which drive/path (USB-A stick root? a specific folder?). KikGen's
   example was an MPC One on older firmware; confirm the trigger on this device/firmware.
2. That a backgrounded daemon survives the `trap ... EXIT` / MPC restart and isn't killed with the script's session.
3. USB arbitration: that MPC grabs only the Push 2 MIDI interface and leaves interface 0 (display) claimable by libusb.
4. Build: the daemon must be aarch64, glibc ≤ 2.39 (matches the rootfs), statically link or bundle libusb.
5. Autorun runs as root with no sandbox — a bad script can brick the device. Test on a disposable setup.

## 8. Firmware comparison: 3.9.1 vs 3.10 (pickup notes, 2026-10-11)

| Capability | 3.9.1 | 3.10 |
|---|---|---|
| Lua device-script loader | present, NO checksum (unsigned loads) | present, checksum = sha256(lua ‖ salt); tools/sign_script.py regenerates it |
| Lua API breadth | mapping subset only: pads, note layouts, transport, encoders -> Q-links | adds clip-matrix aperture, track mute/solo/recArm, qlink names |
| Force-style launch grid / mute-solo-arm row | NO | YES |
| autorun.json launcher (root shell scripts from /media) | present (seen in binary) | present, verified |
| autorun.check | ASSUMED plain unsalted sha256 (same codebase) — NOT yet traced in 3.9.1 | VERIFIED plain unsalted sha256 (wrapper 0x2743d00) |
| Push 2 display via autorun-launched libusb daemon | version-independent (USB, not Lua) | same |
| Push 1 LCD (SysEx from Lua) | works, but Lua has no names/state to show -> values only | works with names + values |

### Data feed for the display daemon on 3.9.1
Lua can't see track names/clip state, so the daemon would need to: (a) listen on ALSA for MPC's own pad/CC feedback, (b) draw only knob values exposed by the mapping layer, and/or (c) use Private-port button/LED traffic (see memory: force-remote-buttons).

### Recommendation
- 3.10 for the full Force-style layout. 3.9.1 for no-checksum simplicity but a reduced feature set.

### Open items to pick up later
1. Trace the 3.9.1 autorun.check validator (confirm unsalted; find its hash wrapper address in g2_391/MPC) — use tools/xref.py / callers.py with PYTHONPATH=pylib.
2. Hardware unknowns (both versions): autorun trigger path on Live III, daemon survival across MPC restart/trap EXIT, USB interface arbitration (MPC vs libusb on Push 2 display iface), aarch64 build vs glibc <= 2.39.
3. Drafts still to write: autorun.json + self-signing autorun.check helper, libusb Push 2 display daemon skeleton, Lua feeder script.
4. Autorun runs as root unsandboxed — test with a disposable setup first.

## 9. Force as an extension of the Live III (2026-10-15)

Idea: use the Force as a hardware extension of the Live III (controller + display mirror). Unlike the Push 2 / Live III work, the Force is owned, rooted (MockbaMod) and testable now.

### Decision (user)
**Stop the local MPC on the Force and run our own daemon that mirrors the Live III.** Chosen over layering on top of a running local MPC, because the local MPC owns the buttons, LEDs and displays and would keep reacting locally. Correction to an earlier note: the Force DOES have separate per-knob displays (the Q-link displays). They are not just a region of the main screen.

### What the daemon must own once local MPC is stopped
| Item | Needed | Status |
|---|---|---|
| Inputs: buttons, 8x8 pads, Q-link knobs + touch | read raw from the Private port (ch1 notes, see memory force-remote-buttons) | readable; pad/encoder details to confirm |
| Pad / button LEDs (RGB) | ADA2 protocol output (KikGen docs; ADA2 code is in the 3.10 binary) | UNKNOWN, needs hardware capture |
| Per-knob Q-link displays | protocol to drive them directly | UNKNOWN: find how MPC drives them (ADA2 `ADA2QLinks`?) |
| Main touchscreen | our own renderer (force-shadow / DrmVnc style), touch events via evdev | feasible, existing work |
| Audio / MIDI | local MPC gone, so the Force makes no sound itself; it is a pure surface | by design |

### Live III side (feed)
3.10 Lua: `clipMatrixAperture` + `clipProperties[c][r].Colour/State`, `trackProperties`, `qlinks[i].TargetName/ValueString`, `buttons.force.*`, `stepSequencer`. The script streams state to the Force over MIDI sysex or a network daemon. The Force daemon renders grid LEDs, per-knob names/values and a mirrored screen, and sends presses/encoders back. On 3.9.1 there is no clip/Q-link name feedback, so the mirror would be very limited. Prefer 3.10.

### Transport (item 1, tested 2026-10-15 on the Force, kernel 6.18.26-az01)
- UDC `ff580000.usb` is a dwc2 OTG controller (`dr_mode=otg`, no extcon) in peripheral mode (GOTGCTL CONIDSTS=1). `libcomposite`, `usb_f_midi`, `usb_f_ecm`, `configfs` are built in.
- Created a `midi.usb0` gadget (1 in / 1 out, "Force Link", VID 1d6b PID 0104) and bound it. State went to `configured` with no cable from us, and the gadget enumerated on the Force's OWN hub (`usb 2-1.7`, "Force Hub"; ALSA cards `ForceLink` + `Link`). MPC grabbed both ports (`amidi` busy). Torn down afterwards; state `not attached`.
- Conclusion: the gadget stack works, but this controller appears wired, or cabled, into the Force's internal hub, so it may not be a free link to the Live III. **To test at the device:** re-create the gadget (the script was in the session scratchpad) with a USB-B cable to the PC / Live III host port. If `lsusb` / `amidi -l` on the far end shows it, the transport works; otherwise use Ableton Link (present in the Force binary, tempo/phase only) plus MIDI over the LAN.
- Note: with local MPC stopped, nothing else will have grabbed the gadget ports.

### Open items
1. Capture the pad/button LED and Q-link display protocol (run MPC, sniff the Private/ADA2 traffic, or read ADA2 code in the 3.9.1 binary).
2. USB-B cable test (above); decide USB-MIDI vs network transport.
3. Confirm the Force can run with local MPC stopped without a crash loop (cf. memory force-drmvnc-crashloop) and keep its screen/touch under our control.
4. Hardware item 4 from section 4 (`buttons.force.*` on non-Force hardware) applies to this mirror too.
5. Draft: Force daemon (input reader + LED/display writer), Live III Lua feeder.

## 10. Force controller protocol, sniffed live (2026-10-15)

Method: usbmon on the Force (MPC running, `/dev/usbmon2` binary; the text interface truncates each packet to 32 bytes, so use binary).
The controller is USB device `09e8:0040` (STM32 MCU) with MIDI cables: cable 0 = pad/OLED/system sysex, cable 1 = button LEDs (CC) and replies.
Tools: `tools/usbmon_bin_parse.py` (binary -> events), `tools/force_oled_decode.py` (OLED reassemble+render), `tools/force_usbmon_dec.py` (text capture decoder). Sample capture: `ref/force_usbmon_oled_capture.bin`.
Note: the first captures were empty because MPC was not in normal mode; MPC must be in the normal Force UI.
Matches KikGen's `MPC-internal-controller---Midi-aspects.md` (sysexId 0x40 = ADA2, 8 Q-links).

### Messages MPC -> controller (all verified on the wire unless marked)
| Message | Meaning |
|---|---|
| `F0 47 7F 40 65 00 04 <pad> <R> <G> <B> F7` | pad/button RGB LED, 7-bit R,G,B (0-127). Sent in bursts (the whole grid on a view change). pad indexes seen 0x00-0x3F+ (grid), 0x48/0x49 more. Also used for pulsing (idx 0/1 swept over time, ~10 Hz) |
| `B0 <button> <state>` on cable 1 | button LED (CC number = button, value 0 off, 1 dim, 3 bright, sometimes other); tap blinks `B0 35 01/03` at ~2 Hz |
| `F0 47 7F 40 0B <lenH><lenL> <disp> <y0> <y1> <x0> <x1> <packed> F7` | **Q-link OLED pixel write**. disp 0-7 = the 8 per-knob 128x32 OLEDs (disp 16 = a further display, region pages 1-3 x 24-100, purpose unknown). len = 5 + packed bytes (14-bit 7+7). y0,y1 are 8-px PAGES (0-3), x0,x1 columns 0-127. Pixel data = 7-bit packed, MSB-first bit stream, re-cut into bytes; each byte = one column of 8 vertical pixels (LSB = top), laid out page by page, then column by column. packed len = ceil(pages*cols*8/7). Verified: decoded text renders correctly ("AUDIO 001 ..." per-display) |
| `F0 47 7F 40 0A 00 07 <disp> 00 03 00 7F 00 00 F7` x8 | per-OLED setup/mode, sent once per display on a KNOBS-view switch (meaning not decoded: mode, contrast?) |
| `F0 47 00 40 52 00 04 13 75 21 69 F7` | heartbeat every ~4 s; controller answers `F0 47 7F 40 53 00 05 13 75 21 69 01 F7` on cable 1 |

### Messages controller -> MPC (known from KikGen's headers, not re-sniffed here)
Buttons = note on/off; Q-link knobs = relative CC `0x10..0x17` (FORCE_KN_QLINK_1..8) plus touch notes `0x53..0x5A`; pads = notes with velocity/aftertouch.

### Consequences
- The **per-knob displays are plain sysex pixel writes** on the same USB-MIDI device as everything else. Nothing proprietary beyond the packing above, so our own daemon (MPC stopped) can draw any bitmap on all 8 OLEDs.
- The pad grid and button LEDs are equally simple (RGB sysex + CC). So the daemon can take over **all** Force output hardware once the local MPC is stopped. Remaining risk: the handshake the MPC does on start (identity request, `50/51`, `62`, `5A` configuration messages from the KikGen wiki) must be replayed. To capture: sniff during an MPC start (`systemctl restart` while usbmon runs).
- The daemon would open the controller's ALSA ports (`hw:1,0,0/1/2`, "Akai Pro Force Public/Private/MIDI Port") via rawmidi after MPC releases them.

### Correction to section 9 (USB transport)
`az01-usbmux-switch` and `/sys/devices/platform/usb-mux/state` (states `internal`/`external`) exist on the Force. The state is `internal`, so the dwc2 gadget port was routed to the Force's own hub, which explains why my test gadget enumerated on the Force itself. Setting `external` should route it to the rear USB-B. Test at the device: `echo external > /sys/devices/platform/usb-mux/state`, then bind the gadget. Also the stock `az01-setup-otg-console.sh` uses the same UDC (`ff580000.usb`) for a serial console, so the gadget path is a supported use.
The earlier "wired into the hub" conclusion should be read with that in mind.

### Not done
Offline tracing of ADA2 code in the 3.9.1 / 3.10 binaries was NOT done beyond KikGen's sources (their wiki and `tkgl_*` headers). The remaining unknowns are the start-up handshake (above), the meaning of `0A` and `disp 16`, and the CC button map.

### 10.1 MPC start-up handshake (sniffed 2026-10-15, `systemctl restart acvs`, usbmon binary)
Capture: `ref/force_usbmon_mpc_startup.bin`, decoder `tools/force_handshake_dec.py`. About 12.9 s after the restart, MPC opens the ports and sends (OUT = MPC to controller, all `F0 47 7F 40 ...` unless noted):

1. `F0 7E 00 06 01 F7` (Identity Request). The controller replies on cable 0 AND cable 1: `F0 7E 00 06 02 47 40 00 19 00 00 04 03 7F 00 00 00 00 41 <serial ASCII> 00 F7`, i.e. mfr 0x47, ADA2 (0x40), fw 0.4.3, serial string.
2. `F0 47 00 40 52 00 04 <4 bytes> F7` challenge and `F0 47 7F 40 53 00 05 <same 4> 02 F7` reply (earlier heartbeat used `13 75 21 69`; here the 4 bytes were `31 39 16 40`, so they vary per request). Repeated every ~4 s afterwards.
3. `62 00 01 65`: select the port events are sent to (KikGen: "set midi port"). `64 00 03 <n> <a> <b>` for n=0..3: config, echoed back by the controller. `63 00 03 <n> 00 00` for n=0,1. `41 00 00` query, which returns 19 zero-ish bytes.
4. `42 00 0c <3 x (00 <btn> <val> ...)>` x88: per-pad/button configuration sent in the first moments, looks like one record per control with three entries `(btn,0,val) (btn,1,val) (btn,2,0x7F)`: probably LED colour-state palette per control. Same message type with `00 18` length for the pads (6 entries each).
5. `5A 00 20 <32 bytes>` x2: pad configuration (KikGen: "pad configuration of some sort"), identical both times.
6. Then LED state: `0A 00 07 <disp> ...` x8 (OLED setup, see above), `0B` frame writes, `65` RGB pad bursts, CC button LEDs `B0 <btn> <state>`.
A DFU/escape (`72`) was never used. Before ~12.9 s the device was un-claimed (no MIDI traffic on the controller); the first 12 s are MPC booting.

### 10.2 Implication
The whole bring-up is ~10 small sysex messages plus the per-control `42`/`5A` configuration tables. Since the controller reacts only to MPC's messages, a replacement daemon that replays this sequence (capturing the `42`/`5A` payloads verbatim from the capture file) should bring the surface up without MPC. Next: write the daemon skeleton and test on the Force with MPC stopped (`systemctl stop acvs`, and `systemctl start acvs` to return).
Caution: restarting `acvs` closed the open project, so MPC came back at the Select Project screen. Save the work before any restart.

### 10.3 Standalone replay test (2026-10-15, user not at the device, so visuals UNVERIFIED)
`force-link/`: `gen.py` builds `handshake.syx` (the 100 start-up sysex captured in 10.1, incl. the identity request and `42`/`5A` tables) and `pattern.syx` (8x OLED setup `0A`, 8 OLED frames = border + bar of growing width via `0B`, 64 pad RGB via `65`); `run.sh` is the device-side runner.
It does `systemctl stop acvs`, then `amidi -s` over `hw:1,0,0` (Public), holds 40 s with a `52` challenge every 4 s, and always runs `systemctl start acvs` from an EXIT trap.
- Verified (from the rawmidi receive logs): with MPC stopped the controller answered exactly like for MPC: identity reply, echoed `64` config, `41` reply, `53` heartbeat replies (first one with `02`, later `01`). So the handshake replays cleanly and the controller accepts commands from us.
- NOT verified: that the pads lit in the pattern colours and the 8 OLEDs showed the border and bar frames. Ask the user to look at the device, or check by eye next time.
- `amidi` has no inter-message delay option, so the burst went out back to back. If anything is missing visually, add pacing.
- MPC came back normally (`acvs` active, new pid).
Next: pad/knob/button input handling in the daemon, a text renderer for the OLEDs, then the Live III feed.

### 10.4 forcelink daemon: input handling (2026-10-15)
`force-link/flparse.{c,h}` (parser + button-name table, tested natively by `parse_test.c`, 10/10), `forcelink.c` (ALSA rawmidi daemon), `build.sh` (armhf Docker, stretch archive sources as in force-shadow's README; add `libasound2-dev`). Run: `forcelink [-r] [-p pattern.syx] handshake.syx` with `acvs` stopped.
- Replays the handshake, answers the 4 s challenge itself, reads BOTH ports (`hw:1,0,0` = port 0 "Public", `hw:1,0,1` = port 1 "Private") and prints parsed events, one per line: `PAD n= row= col= down|up vel=`, `BTN <NAME> down|up`, `TOUCH knob=k`, `KNOB k= delta=`, `SYSEX/CC/NOTE` fallbacks. `-r` also logs raw bytes to stderr. stdin commands: `pad n r g b`, `btn cc val`, `syx <hex...>`, `quit`.
- Smoke test on the device (MPC stopped, then restored): the daemon starts, handshake accepted, challenge replies parsed (`SYSEX port=1 len=13` x4). Nobody pressed anything, so the input event decoding is **unverified on hardware**.
- UNVERIFIED assumptions in the parser, to confirm by pressing things with `-r`: pads arrive as notes 0-63 on port 0, with note = row_from_bottom*8 + col; buttons are notes on port 1 (Private) using the KikGen numbers; Q-link knobs are relative CC 0x10-0x17 (1-63 = +, 65-127 = -(128-v)); touch = notes 0x53-0x5A. Any message not matching the tables is printed raw, so mismatches show up.
- The daemon cannot yet render text on the OLEDs (frames are only the test pattern).

## 11. How much works with Live III on 3.9.1 (2026-10-15)
Everything on the **Force** side is independent of the Live III firmware: handshake, pads, buttons, knobs, OLEDs.
It depends on the Live III's capabilities (section 5 / 8):

| Feature | 3.9.1 | 3.10 |
|---|---|---|
| Force pads/buttons/knobs controlling the Live III (Force as a USB-MIDI controller) | YES via Lua mapping: `AddNoteToMpcPad/ForcePad`, `AddNoteToControl`, `AddInfiniteCCToQLink` (no checksum, unsigned scripts should load, untested) | YES |
| Force 8x8 clip launch | NO (no `clipMatrixAperture`); only pad note mapping | YES |
| Track mute/solo/arm buttons | NO (button presses can be mapped, but no state feedback or properties) | YES |
| Pad/button LED feedback of clip/track state | NO from Lua (no clip state); only what the script itself decides | YES |
| Per-knob OLED text (Q-link names, values) | NO names (no `TargetName`); script-chosen labels/values only; no `HandleMIDISysex` | YES |
| Mirror of the Live III screen on the Force (+ touch back) | YES if the autorun daemon works (framebuffer grab and uinput are independent of Lua) | YES |
| Transport between the two (Force as USB gadget on a Live III host port, or LAN) | same | same |

Summary: on 3.9.1 the Force is a good **input surface plus a screen mirror**. It is not a state-aware surface (clip colours, names) unless that state is obtained outside Lua, e.g. a Live III autorun daemon that sniffs the Live III's own controller USB traffic (same Akai sysex family for pad/button LEDs) or scrapes the screen. 3.10 is required for the full Force-style grid, mute/solo/arm and OLED names.

### 10.5 OLED renderer (2026-10-15)
`force-link/floled.{c,h}` (128x32 1-bpp framebuffer, text with two baked Terminus console fonts 8x16 bold / 6x12 from `gen_font.py` -> `flfont.h`, rect/bar, `fl_fb_sysex()` = the `0B` full-frame message), `oled_test.c` (offline: renders 8 sample Q-link frames, checks the sysex round-trips by decoding it independently: passes; the montage looked right: centred 16-char name, value text, bar).
Daemon command: `qlink <1-8> <name>|<value>|<bar 0-127|-1>`, e.g. `qlink 1 Cutoff|1.2kHz|90`. Built armhf and staged at `/tmp/fl/forcelink` on the Force. Not run on the device (user away); the packing matches the captured frames, so the risk is low but the panel appearance is unverified.

## 12. Getting clip/track/Q-link STATE out of a 3.9.1 Live III without Lua (2026-10-15)
Sources: `g2_391/rootfs.img` (Live III 3.9.1, read with debugfs), `g2_391/MPC.strings`, the Force 3.9.1 rootfs, our sniffed Force traffic, KikGen's Iamforce page.

### Findings
1. **The Live III 3.9.1 binary already contains the whole Force control surface**: `ADA2PadStepSeqResponder`, `ADA2QLinks`, `ADA2TrackAssignResponder/Plumbing`, `ADA2PadsResponder`, `ADA2PadMap`, `ADA2EditButtonsPlumbing`, `ADA2PadSixteenLevelResponder` ... (321 `ADA2` strings). Lua lacks clip state, but the C++ surface code has all of it. So the state is available inside MPC, just not to Lua.
2. **The Force is the reference implementation.** Its MPC 3.9.1 runs the same ADA2 code and talks to its controller with exactly the protocol I sniffed (pad RGB `65`, button CC LEDs, Q-link OLED `0B`). So whatever a Live III running ADA2 code would emit for clip matrix / track / Q-links is already observable on the Force. The Force gives the full mapping (clip colours -> pad index, etc.) for free: capture a loaded project's views with usbmon.
3. **Preferred route = IamForce on Gen2.** KikGen's mechanism (Iamforce wiki): MPC checks the hardware type two ways: `/sys/firmware/devicetree/base/inmusic,product-code` and a universal identity request to the controller. IamForce hooks `open64`/`read` (return ADA2's code) and `snd_rawmidi_open/read/write` (replace the Public/Private ports with virtual ones and rewrite the sysex id between Live and Force) via LD_PRELOAD. MPC then runs the **Force control surface** on a Live and the Live III's MPC becomes "Force-like". With our Force controller attached to the Live III, the bridge is trivial because the controller already speaks Force: the daemon forwards bytes (MPC <-> Force controller), no remapping of pads/LEDs/OLEDs.
   - Earlier I marked this "blocked on Gen2" (section 2) because it assumed no root. Section 7 changes that: the autorun mechanism executes scripts as root. The MPC service on the Live III is `acvs.service` -> `/usr/bin/az01-launch-MPC` (a plain shell script on the read-only signed rootfs, `exec .../MPC "$@"`). A root autorun script could add a systemd drop-in under `/run/systemd/system/acvs.service.d/` (tmpfs, not covered by the signed rootfs) with `Environment=LD_PRELOAD=...`, then `daemon-reload` + restart. **UNVERIFIED**: (a) autorun triggers on a Live III; (b) a /run drop-in is honoured and not blocked by the signed-fs/hardening; (c) whether the MPC binary (aarch64, ASLR off via `setarch -R`) loads a preload; (d) whether the Live III's hardware boots under an ADA2 product identity (hardware audio paths differ from a Force), which IamForce solved for older MPC Live firmware.
4. **Without preload, other sources of state are weak.** The Live III 3.9.1 kernel has no `usbmon` (not in `modules.dep`/builtin), so you cannot sniff the Live III's own controller traffic passively. It has `snd-seq`, `snd-seq-midi`, `snd-usb-audio`, `libcomposite`/configfs, dwc2/dwc3. `az0x-usb-gadget` exists (the Live III's own gadget). `az01-network-midi.service` (network MIDI, same `DeviceControlServer` as the Force) only exposes MIDI, not state. Screen scraping (framebuffer grab + OCR/pixel colour sampling of the clip grid) is possible from a root daemon but fragile.
5. **Other leads seen**: `az01-script-runner` (aarch64 ELF: `socket`/`bind`/`recvfrom`/`system`) is a local command runner, presumably MPC's privileged helper (not explored: not needed). `acvs.service` is identical to the Force's, so the MockbaMod-style `LD_PRELOAD` pattern from the Force skill is the model to copy.

### Plan, in order of value
1. (Force, can do now) Capture the Force's MPC <-> controller protocol with a project loaded: clip matrix pad colours/states, mute/solo/arm LEDs, the Q-link OLEDs in each mode; build a reference table.
2. (Force, can do now) Prove the "man in the middle": run MPC on the Force with an LD_PRELOAD shim that forwards the Public/Private rawmidi traffic through our daemon (like IamForce's virtual ports). This is exactly the Gen2 bridge, minus the identity spoof; the Force already identifies as ADA2.
3. (Live III, needs hardware) autorun smoke test, then the /run drop-in + preload test, then try ADA2 identity.
4. Fallback if 3 fails on 3.9.1: Force = input surface + screen mirror only (section 11).

### 12.1 Force reference capture with a project loaded (2026-10-15)
Setup: loaded the BassHouse template project on the Force (7 tracks, populated clip matrix), pressed MATRIX, MIXER, KNOBS, NOTE, MUTE, ARM and MATRIX again, with usbmon running. Capture `ref/force_usbmon_matrix_project.bin`, grid dumper `tools/force_pad_grid_dump.py`, last OLED frames `ref/force_oled_mixer_view.png`.

**Pad grid = clip matrix (verified against the screen).** Pad sysex `65 00 04 <idx> R G B`, 7-bit colours. Per track colour (full on = clip present, `~06 01 05` dim = empty slot of that track): Drum Loop `7f1964`, Top Loop `197c7f`, BassHouse `7f3419`, Pads `60197f`, Vocal FX `19277f`, Bass Line `207f19`, SFX `7f1949`, matching the on-screen track colours exactly. idx = row*8 + col, col = track (left to right), **row 0 = the track "launch/stop" header row (grey `191919`)**, rows 1..7 = scenes 1..7 in screen order (same row order as the screen). The bottom-to-top physical orientation of idx rows on the hardware is NOT verified (the header row at idx 0-7 suggests the physical bottom row, but the screen shows it at the top). The empty column 8 stays `000000`.
So a state mirror needs only: track colours + per-slot (empty / has clip / playing / queued) and the header row.

**Q-link OLEDs.** After the matrix is up, `0A` setup x8 then `0B` frames. In the track-level view each of the 8 displays shows the **track name** (upper-case, centred, small font) with a **level bar** (outlined box, solid fill) drawn on pages 2-3. Bar updates are PARTIAL writes (pages 2-3, x 71-88 / 42-88), not full frames. **`disp=16` is a 9th display** (full 128x32) showing a header, white bar "TRACK" + the large selected track name ("Drum Loop"). This is likely the main/selected-track display between the encoders. Our renderer (`floled.c`) can already produce all of these; the partial updates can be a later optimisation.

**Buttons.** Button LEDs are CC on cable 1: values 0 off, 1 dim/on, 2, 3 bright, 4 seen on `0x71`/`0x38`. Mode buttons (MIXER `0x0B`, ARM `0x5D`? `0x5B`, `0x04`...) flip between `01` and `03`. The pad-row buttons `0x38-0x3F` (LAUNCH_1..8) get `02`.
New message: `F0 47 7F 40 6A 00 03 <n> <a> <b> F7` (n=0,1,2; once at project load, `02 01 00` for n=2). Meaning not decoded.

### 12.2 Step 2 (MITM shim on the Force): status
Not built. The Force's MPC already runs with `LD_PRELOAD=/data/hwremap/hwremap.so:/data/mpc-addins/remote/mpc_remote_addin.so:...skipback...commander`, and `mpc-addin-remote/src/surface.c` already interposes `snd_rawmidi_open/read` (chained via libc `dlsym`/`dlvsym` to coexist with other shims). That proves rawmidi interposition on the MPC 3.9.1 process works here, which is the mechanism IamForce uses. The remaining piece is a tap on `snd_rawmidi_write`/`read` for the Public/Private handles that forwards to our daemon (and can rewrite). I held off building and restarting MPC with it while the user is away, since a preload fault can crash-loop MPC. Proposed design: extend/replace `surface.c`'s hook with a small socket (`/tmp/forcelink.sock`) carrying (port, bytes) both ways.

### 12.3 Offline daemon update (2026-10-15, no device needed)
New in `force-link/` (all offline-tested, native tests pass):
- `flcmd.{c,h}`: one command interpreter for the daemon and the preview: `pad`, `clip <track> <row> <empty|clip> r g b`, `hdrrow`, `btn`, `qlink`, `track <n> <name>|<bar>`, `bar <n> <name> <bar>` (partial update, pages 2-3 only), `hdr <label>|<big>` (display 16), `syx`, `quit`. `forcelink.c` now uses it for stdin.
- **Empty-slot colour rule derived and verified:** MPC draws an empty slot of a track as `floor(c*5/100)` per channel of the track colour; reproduces all 7 captured pairs exactly (`fl_dim`, tested).
- `floled`: `fl_fb_track()` (small centred upper-case name + outlined level bar, as MPC's mixer view), `fl_fb_header()` (display 16: white label bar + large name), `fl_fb_sysex_region()` (partial writes; length matches the captured 47-byte form for a 2x18 region).
- `preview.c`: reads commands and decodes the exact bytes the controller would receive (pad `65`, OLED `0B` incl. partial regions), writes a PPM of the surface: pad grid (idx row 0 at the bottom) + 8 Q-link displays + display 16. `examples/matrix_demo.txt` + `matrix_demo_preview.png` reproduce the captured BassHouse view.
- Tests: `cmd_test.c` (10 checks), `oled_test.c` (frame round-trip), `parse_test.c` (input parser).
Still needs the device: pad/OLED appearance, input decoding, `6A`/`0A` meaning, clip play/queued colours (not captured yet: playing a clip makes sound, so capture when the user is at the device).

### 12.4 First on-device visual test (2026-10-15, user observed)
- Rainbow pad pattern: all 64 pads lit correctly. Q-link OLED test frames drawn on all 8 displays.
- **Burst-send drops frames:** `amidi -s pattern.syx` (5.7 KB in one burst) left display 4 blank; re-sending that frame alone worked. `forcelink` (drain + 3 ms between sysex) drew all 8 first time, so always pace sysex writes (>=3 ms, drain per message).
- **No ninth physical display.** The Force has 8 Q-link OLEDs plus the main screen. Disp 16 ("TRACK" header) is NOT a physical OLED; its purpose is unknown (earlier "9th display" wording is wrong).
- Controller blanks displays without the ~4 s heartbeat (seen when the heartbeat was omitted).
- Still unverified: input decoding, usb-mux external, playing/queued clip colours.

### 12.5 Input protocol VERIFIED on hardware (2026-10-15, raw `-r` capture)
- **Pads:** channel 9 (0x99/0x89), note = 54 + row*8 + col, row 0 = TOP, so top-left = 54 and bottom-right = 117; polyphonic aftertouch 0xA9. (Earlier "note 0..63" assumption was wrong.)
- **Buttons:** plain notes on channel 0 (seen on port hw:1,0,0), names via flparse.c table (NOTE=4, MIXER=11, LAUNCH=116 confirmed by press order; others from KikGen, not individually confirmed). Note 117 on ch9 is a pad, not STEP_SEQ.
- **Q-link touch:** ch0 note 0x53+k. **Q-link knobs:** CC 0x10+k carry a running count since touch-down (1,2,3..., wraps mod 128); step = (v - prev) mod 128 as signed 7-bit. Reset on touch-down. One direction observed fully; reverse direction seen as falling count.
- flparse.c updated (10/10 pass). Not yet verified: individual button names, main encoder, pad orientation of OUTPUT idx rows (input now top-down; check output idx 0 = top), second knob direction in the new decoder.

### 12.6 Buttons / encoder verified (2026-10-15)
User pressed the full button set in order; every table name matched the physical button (log order == press order): ENCODER_PUSH, MENU, PLAY, STOP, REC, UNDO, LOAD, SAVE, MATRIX, CLIP, MIXER, NAVIGATE, SHIFT, LEFT, UP, RIGHT, DOWN, KNOBS, LAUNCH, NOTE, STEP_SEQ, SELECT, EDIT, COPY, DELETE, ARP, TAP_TEMPO, MUTE, SOLO, REC_ARM, CLIP_STOP, track-select 1-8 (notes 96-103, formerly COLUMN_n), track-arm 1-8 (notes 41-48, formerly MUTE_PAD_n), LAUNCH_1-8 (56-63), MASTER, STOP_ALL. All send note on/off, ch0, vel 127/0.
- PLAY/STOP/REC additionally send CC 118/117/119 (127 down, 0 up).
- Main encoder = CC 100 ch0 relative (1 = cw, 127 = ccw); push = ENCODER_PUSH note.
- Q-link knobs: steps +-1 per click, bigger when fast (+2,+3,+12 seen); touch-down resets the counter.
- Pad 2nd-row/3rd-col (expect note 64) not yet exercised.
- Pads verified (v4 run): note 64 = row1/col2, 61 = row0/col7, 110 = row7/col0, 54 = row0/col0. Note-on velocity was always 127 (even light presses); pressure is only in aftertouch (A9), e.g. 127 -> 40 -> 0. Input side fully verified.

## 13. Live III 3.9.1 on hardware: preload route, steps 1-2 (2026-10-23, read-only over SSH)
- On-demand root SSH works (autorun stick). MPC 3.9.1 aarch64, kernel 6.18.26-imb PREEMPT_RT, BusyBox.
- **Drop-in + LD_PRELOAD PROVEN:** `/run/systemd/system/acvs.service.d/90-mpc-addins.conf` is merged by systemd (`systemctl cat acvs`) and the running MPC has commander/remote/skipback preloaded (`/proc/<pid>/environ` and `maps`). Launcher is `az01-launch-MPC` -> `setarch -R MPC` (no systemd-inhibit). /run is tmpfs, so it must be redone each boot.
- `inmusic,product-code` = `ACVG\0`; also `inmusic,az04-pcb-rev` = `G`, `-suffix` = `A`.
- The Live III's own controller is ALSA card 0 "MPC LIVE III" with rawmidi `hw:0,0,0..3` = **Public, Private, DIN-A, DIN-B**, the same port names as the Force controller. MPC holds midiC0D0 (in 1, out 2 fds) plus `/dev/snd/seq`.
- Plan (IamForce model): shim hooks `open64`/`read` on the product-code (-> `ADA2`), `snd_rawmidi_open` for Public/Private, and the identity sysex both ways. Unknown: how the Live III's internal MCU reacts to Force-family sysex, and hardware-specific audio/screen config keyed on the product code. UNTESTED; the first test restarts MPC (closes project).
- **Step 3 result (2026-10-23, Live III):** shim `spoof/forcespoof.so` (open/open64/openat/openat64 on `inmusic,product-code` -> memfd "ADA2") loaded via drop-in `91-forcespoof.conf`; MPC read the file twice and booted without a crash (`acvs` active), but the screen showed "error connecting to MPC Live 3, restart unit" (user-observed). Reading: product code says ADA2 while the internal controller still identifies as a Live III, so MPC rejects the pairing. Reverted with `off.sh`; stock addin list restored, `acvs` active. Next: also rewrite the identity reply (and any hardware-specific traffic) on `snd_rawmidi_read/write` for Public/Private, and bridge to the Force.
- **USB link built, not yet run (2026-10-23):** Force side `force-link/gadget.sh up|down` (usb-mux external, 2-port MIDI gadget "Force Link" on `ff580000.usb`) + `force-link/flbridge` (relays controller Public/Private `hw:1,0,0/1` <-> gadget `hw:G,0,0/1`, sysex paced); staged at `/tmp/fl` on the Force. Live III side `spoof/forcespoof.so` v2 (staged, inactive) also redirects MPC's Public/Private `snd_rawmidi_open` to the card whose name has "ForceLink" when present. Needs: Force rear USB-B cable to a Live III USB-A host port, Force MPC stopped (closes its project).
- **USB gadget link on the Force: BLOCKED by the usb-mux (2026-10-23, device).** `echo external > /sys/devices/platform/usb-mux/state` (with the 2-port MIDI gadget bound to `ff580000.usb`) made the Force's whole internal hub disappear (`usb 2-1: USB disconnect`, `2-1.3`, `2-1.5`; the controller `09e8:0040` and its "Force" ALSA card vanished; `usb5537 ... probed in hub mode`). The gadget card (`fmidi`) existed but the UDC stayed `not attached`, so the Live III saw nothing new (its card list unchanged). So the mux swaps the one dwc2 port between the internal hub and the rear USB-B: the Force cannot reach its own controller and a host over the gadget at the same time. Restored: `internal`, `acvs` active, controller card back. Also: with the controller card present the ALSA order was `0 Force` (controller), `1 ADA2` (codec), so the controller ports are `hw:0,0,x` then, not `hw:1`; look the card up by name. Transport alternatives: network (Wi-Fi/Ethernet) MIDI/TCP between the two daemons, or the Force's rear host port acting as a USB host to a Live III gadget (Live III already has an `f_midi` gadget card, "MPCLiveIII").

## 14. Force driven by a Live III (3.9.1) over the network: WORKS, grid does not (2026-10-23, device, user-observed)
Setup: Force MPC stopped, `force-link/flnet` (Force, armhf) serves the controller's Public/Private rawmidi ports on TCP 6760/6761 (raw bytes, sysex paced towards the controller, controller card found by name "Force"); on the Live III `spoof/forcespoof.so` v3 (drop-in `91-forcespoof.conf` first in LD_PRELOAD, `net.conf` holds the Force's IP) spoofs `inmusic,product-code` to ADA2 (memfd) and wraps MPC's Public/Private rawmidi handles (info/params forwarded to the real port, read/write/poll/status via socketpair + relay thread to the Force). `on.sh`/`off.sh` apply/undo. RTT Live III -> Force 29 ms (Wi-Fi), heartbeat (52/53) stable.
- **Works:** the Live III MPC accepts the Force controller as ADA2 (no "error connecting" screen once the identity reply comes from the real Force), runs the full start-up (identity, 62/64/63/41, 42 tables, 5A, 0A, 0B OLED frames), Force OLEDs show content, buttons/views work (MATRIX, CLIP, MIXER, KNOBS change the Live III screen), row-launch buttons work, pad presses reach MPC (99 note/A9 aftertouch) and MPC lights the pressed pad (65 red/black on that idx).
- **Does not:** NOT STEP_SEQ/NOTE changing view; no clip colours on the 8x8 grid and grid pads don't launch clips. Log analysis: over the whole session the Live III sent `65` only for pressed pads (idx 0..0x3a, red 7f0000 then black) plus idx 0x40-0x4c in a few colours; never a 64-pad clip-colour burst like the real Force (§12.1). Consistent with §5/§8: 3.9.1 has no clip-matrix state to drive an 8x8 grid (that arrived in 3.10 via clipMatrixAperture). The Live III's own pads/buttons stay dark (MPC no longer owns its controller).
- Undo: Live III `sh /data/mpc-addins/forcespoof/off.sh`; Force `touch /tmp/fl/stop` (script `/tmp/fl/run_net.sh` restarts acvs; also stops after 30 min).
- **Devicetree reads by MPC 3.9.1 on the Live III at start-up (2026-10-23, shim log):** `inmusic,product-code` x2, `inmusic,az04-pcb-rev` x2, `inmusic,az04-pcb-suffix` x2, `serial-number` x5, `chosen/u-boot,version` x6, `dsi@fde20000/panel@0/compatible` x1. It never opens `/compatible`, so spoofing it (dt.conf `a`) changes nothing. No az01/az05/az07 board file is opened: the board family is presumably chosen by an existence check (stat/access, not hooked) before the open. Spoofing the board (mode `b`) would need stat/access/faccessat hooks as well, and risks MPC picking Force (RK3288) audio/display settings on RK3588 hardware.
- Force side now runs `flnet` by hand (the 30-min `run_net.sh` timer was killed so it would not start acvs under flnet). Restore by hand: `kill $(pidof flnet); systemctl start acvs`.
- **1b (board spoof, dt.conf `b`, 2026-10-24 device):** with stat/access/faccessat/statx hooks MPC reads `az01-pcb-rev`/`-suffix` (spoofed D) instead of az04 and also polls `chosen/*` thousands of times. Side effects as predicted: "Failed to find Linux audio device", "UI has started, but no audio device was found", no `/sys/class/backlight/mipi-backlight`. UI up, Force link up. Grid / NOTE / STEP SEQ NOT yet checked (user had to stop). Restored: dt.conf removed, off.sh (stock LD_PRELOAD, acvs active, no audio errors), Force flnet stopped + acvs started, mux internal.

## 15. Offline, 2026-10-24: ADA2S = Force (Gen 2), and option 3 kit
- **Product table in MPC 3.9.1** (`0x75cf3b0`, 0x70-byte entries: code string, uint16 controller id with bit 8 = valid; looked up by `0x23e2380`; the identity reply's family byte (offset 6) is matched against it in `0x23c06d0` via `0x23e2180`): ACV5/ACV5S 0x3a, ACV8 (Live) 0x3b, ACVA (One) 0x46, ACVB (Live II) 0x47, ACVC (XL) 0x5a, ACVG (Live III) 0x58, ACVM (Key 61) 0x4b, ACVR (Key 37) 0x57, **ADA2 (Force) 0x40, ADA2S 0x04**, ACVK 0x41, ACVA3 0x00, ACVR2 0x01, ACVM2 0x02.
- **ADA2S = "Akai Professional Force (Gen 2)"** (3.10 Gen2 image devicetree: `compatible = inmusic,ada2s | inmusic,az04b | inmusic,az04 | rockchip,rk3588s`, same DSI panel as the Live III). It is on the Live III's az04 platform, but its audio is an on-board I2S codec (`/codec: inmusic,az04-codec`, 2 in / 2 out, 44.1k, `/sound: simple-audio-card`) instead of the Live III's USB audio, so MPC may again fail to find audio in ADA2S mode (to test).
- **Shim mode `s`** (dt.conf `s`): product code `ADA2S`, board files untouched, and the relay rewrites the controller id 0x04 <-> 0x40 in Akai sysex headers (`F0 47 7F|00 <id>`) and in the identity reply family byte. `spoof/idmap_test.c` passes (header split anywhere, challenge, identity, payload untouched). Not yet run.
- **DFU guard in flnet:** 3.10 ships an ADA2S controller updater whose DFU-enter is `F0 47 00 04 72 00 00 F7`, which the id rewrite would turn into the Gen 1 Force's own DFU command. flnet now drops every Akai sysex with command `72` before it reaches the controller (`force-link/dfu_guard_test.c` passes). Deploy this flnet before any ADA2S or 3.10 test.
- **Option 3 kit:** `mpc310/` (README, make_kit.sh, on.sh, off.sh); offline findings in mpc310/README.md. 3.10 binary resolves fully against 3.9.1 libraries; Live III controller firmware identical in both.

## 16. WORKING: Force as a full Force surface for a Live III (2026-10-24, device, user-verified)
- **Mode `s` on MPC 3.9.1** (product ADA2S, id 0x04<->0x40): audio OK (the Live III's own 50-in/54-out USB audio), OLEDs OK, but like ADA2 no clip grid, NOTE/STEP SEQ do nothing, grid pads do not launch. So 3.9.1 itself lacks the grid for any Force identity.
- **MPC 3.10.0.49 application on the 3.9.1 system** (`mpc310/on.sh`: `BindReadOnlyPaths` of the 3.10 binary + /usr/share/Akai, kit on /data/mpc310) **plus mode `s`**: stable (NRestarts 0), audio OK, Force link up, no DFU attempts (flnet guard counter 0). User verified: normal Live III UI; **Force pads show the clips and launch them; NOTE and STEP SEQ work.**
- Still not working: the Live III's own pads/buttons (MPC drives the Force controller instead of its own).
- Everything is RAM/run-only: a reboot returns the stock 3.9.1 app and identity. To re-enable after a reboot: stick (SSH), Force `systemctl stop acvs; nohup /tmp/fl/flnet -r ...` (re-copy flnet), Live III `echo s > dt.conf; forcespoof/on.sh; mpc310/on.sh`.

## 17. Live III native controller + bridge (2026-10-24, device)
- **Native protocol (MPC 3.10 stock capture, `ref/live3_native_stock_capture.log`):** same Akai family, controller id **0x58**. Identity reply `F0 7E 00 06 02 47 58 00 19 01 05 00 07 7F ...`. MPC start-up: `52` challenge, `6C` and `6B` config tables, `62 00 01 65`, `41`, `5B`, then `65` RGB writes (LED idx 0x00-0x4F = 80: 16 pads x 4 quadrants + 16 step buttons, order not mapped) and button LED CCs on Private (values 0..7).
- **Inputs (all on Private):** pads ch10 notes, pads 1..16 (bottom-left first) = `25 24 2A 52 28 26 2E 2C 30 2F 2D 2B 31 37 33 35`, velocity + poly aftertouch. Buttons ch1 notes: SET 0A, steps 1-16 5C-6B, PREV 6D, NEXT 6C, NOTE REPEAT 0B, FULL LEVEL 27, 16 LEVEL 28, ERASE 09, A-D 23-26, COPY 7A, MUTES 72, TC 2D, UNDO 43, LOAD 29, SAVE 32, SAMPLE EDIT 2B, TRACK MUTE 2C, Q-LINK 00, MENU 7B, MAIN 34, SHIFT 31, SOUNDS 79, EDIT 0C, ARRANGE 70, MIXER 21, TAP TEMPO 35, LOOP ON 4A, < 44, > 45, LOCATE 46, << 47, >> 48, + 36, MIC ON 22, (under LOOP ON) 20, REC 49, OVERDUB 50, STOP 51, PLAY 52, PLAY START 53, - 37, TOUCH STRIP 04. Q-links 1-4: touch notes 54-57, turns CC 10-13 plain relative (01 / 7F). Main encoder CC 64 (01/7F, same as the Force), push 6F. Touch strip: note 41 touch, CC 21/22 position.
- **Bridge (shim, mode s + live3.conf):** replays MPC's start-up (`spoof/live3_init.syx`, 725 bytes) + heartbeat to the Live III controller, translates its input into Force events injected into MPC's Private stream, mirrors Force pad LEDs of the bottom-left 4x4 and mapped button LEDs. `spoof/bridge_test.c` 6/6. **User-verified:** Q-links and encoder work, the 16 pads act as the Force's bottom-left 4x4 and light up (quadrant mapping wrong), step buttons work.
- Packaged as `link/` (`forcelink on|off|status|install`, device scripts in /data on both). See link/README.md.

## 18. Version B first run (2026-10-24, device, user-verified)
- The shim's ALSA sequencer port "Force Link" appears in MPC 3.10 and MPC connects to it both ways (`/proc/asound/seq/clients`).
- User controller scripts: MPC scans `/nvme/MPC Documents/Devices/<folder>/*.ini` (and `/usr/share/Akai/SME0/Devices`). **The .ini must start with an `[info]` line**, otherwise it is read but ignored (the .lua is never opened). The checksum covers the .lua only.
- `print()` from a device script goes to MPC's stdout (journalctl -u acvs). `ShowTUIFeedback` shows a message on screen.
- **3.10.0.49 API layout:** `<device>.clipMatrixAperture` holds the per-device window, and `clipProperties[col][row]`, `sceneProperties`, `trackProperties` live on the aperture (not on the device). Aperture members include GetOriginX/Y, ToGlobalColumn/Row, ToLocalColumn/Row, BankUp/Down/Left/Right, Move*, CanMove* (properties), SetFollowsTUI, Show/Hide (draws a white box with the label on the MPC matrix screen). Clip `State.value` is a number (0 = Stopped). `mpc.clipMatrix` (global) also has HasClip/GetClipColour/ClipIsPlaying/ClipIsRecording/TriggerClip/StopTrackClips/... .
- User-verified with ForceLink.lua v0: Force pads show the clips, tapping a pad plays the clip, OLEDs show Q-link names; the Live III's own controls unaffected.
- flsurf bug found and fixed: setting the output handles blocking also made the inputs blocking (shared per open); inputs are now set non-blocking explicitly.
- My mistake on the way: a shim build with a re-entrant access() hook (logging inside is_code) crash-looped MPC (5 restarts, then failed) for a minute; fixed with a re-entry guard. Never deploy a build whose QEMU preload check failed.
- **ForceLink v1 (2026-10-24, offline 20/20, loaded on device):** MATRIX / NOTE / STEP SEQ modes on the Force. 3.10.0.49 API confirmed on device: `mpc.controlSurface.stepSequencer` {Tap, Press, Release, IsPressed, LightRGBColour, LightBrightness, GetCurrentMode, Help}; `<dev>.mappings.pads` {AddNoteToMpcPad, AddNoteToMpcPadQuadrant, AddNoteToForcePad, AddNoteToVirtualPad, Remove*, Is*Mapped, Help}; `<dev>.mappings.script` {Add/RemoveNoteToScript, Add/RemoveCCToScript, IsNote/CCMappedToScript, SetHandleSysex, Help}. NOTE mode maps each Force pad to an MPC pad quadrant (quadrant order QUAD = {0,1,2,3} = TL,TR,BL,BR: a guess, to verify).
- **STEP SEQ / sequence reading (2026-10-24, device + offline):** 3.10.0.49 scripts cannot read sequence events: `events` (currentSequence or clipMatrix.selectedClip) has GetSize and create/move/delete/set functions only; no indexing, iteration or metatable. `sequencer.GetTCPeriod()` = 240 (1/16 at 960 PPQ, bar = 3840 ticks); `GetEndOrLoop()` in ticks; `PlayheadPosition()`, `JumpToPulse`, `IsPlaying`. Internal route found but parked: events.GetSize binding at 0x2f939a0 (3.10.0.49) reads wrapper+0x98 -> virtual +0x10 -> event list, count at list+0x88; a shim hook there could capture the list for decoding (multi-session RE, build-specific).
- ForceLink v2 STEP page: 32 steps = two bars of MPC's 16-step sequencer (live bar + last-seen bar, pending taps while playing, JumpToPulse when stopped), MPC pads bottom-left, 16 velocity levels bottom-right, LEFT/RIGHT page bar pairs. NOTE mode = plain notes 36-99 via flsurf notemap. KNOBS = Force knobs on Q-links 1-8/9-16 (the API's qlink button Press/Hold does nothing).

## 19. Lua can probably read sequence events through #GetState (2026-10-24, offline, MPC 3.10.0.49)
The Lua registrar (0x2efdb04) adds `#GetState` to every scriptable object, next to `Help`; `mpc.GetState()` returns
the whole tree and `DiffStates` diffs two. The event serialiser (0x2f972c0) writes NumEvents and per event Time, type
flags and NoteEvent {Note, MidiNote, Velocity, MidiVelocity, Length, NoteEnd, Probability, Ratchet, ...}. It is
reached from the mpc.clipMatrix state (ScriptableClipLanesWrapper) and the project state (ScriptableProject). In
memory the event list is an rb-tree at list+0x60 with the event at node+0x20; count at list+0x88. Not yet run on a
device: probe = SHIFT + STEP SEQ in link/script/ForceLink.lua (see link/HANDOFF_LINK.md).

## 20. Force NOTE layout, Q-link OLED rendering, and script pad APIs (2026-10-24, offline, MPC 3.10.0.49)
**Q-link OLEDs, pixel-exact.** MPC's OLED font is an 8-byte/glyph row-major table in the MPC binary (3.10 at
0x65ce660, identical in 3.9.1 at 0x509a660): row 0 blank, 5x7 glyphs, slashed zero. Text is upper-cased,
proportional (advance = ink width + 1, space = 4 + 1), centred at floor((128 - advances) / 2), ink on rows 7-13.
Value bar: outline rows 18-29 full width, fill rows 21-26 from x 3, 122 px at full scale. No value text on the OLED.
Value changes are partial writes (pages 2-3). `force-link/gen_mpc_font.py` bakes the font from the local binary at
build time (never committed); `floled.c` renders it; all 11 captured real-Force frames reproduce with 0 pixel
differences. The script sends `q.GetPosition()` (0-127) as the bar and bar-only updates on value changes.
**Drum NOTE layout (ADA2PadMap2x2 at 0x1522980).** 64 pads = banks A-D as 4x4 quadrants: A bottom-left,
B bottom-right, C top-left, D top-right, each numbered from its bottom-left (y counts up, as MPC pad 1 =
bottom-left). PadMap1x1 = one 4x4 bank. UI strings "Drum Banks %c-%c" / "Drum Bank %c" (paging A-D / E-H).
**Script pad APIs.** `AddNoteToMpcPad` pad <= 15 (current bank only); `AddNoteToForcePad` pad <= 63;
`AddNoteToVirtualPad` pad <= 127 (absolute program pad, ignores bank and note map). `pads.TapVirtual/PressVirtual/
ReleaseVirtual/IsPressedVirtual/VelocityVirtual` (1-64), `pads.LightRGBColor/LightBrightness` (1-64),
`pads.CurrentBank` (writeable), `EnsurePadVisible`, `qlinks[i].GetPosition/SetPosition`. A `force` control-surface
namespace (all Force buttons + Force pads: Tap/Press/Release/IsPressed/LastHitPad, no light getter) is registered
unconditionally. Only script callbacks: HandleMIDINoteOn/Off, HandleMIDICC, HandleTimer; only `out:` mapping is
`out:cc:qlink`, so a script cannot read what the Force's own pad responders would light.
**Not resolved offline:** the melodic NOTE layout (ADA2PadNoteResponder: scale type, base note, octave shown as
internal - 2; row interval and colours run through generic property/queue code). Needs a capture of the real Force.

## 21. Real Force NOTE mode, melodic track (2026-10-25, device capture `local/captures/note-keys.bin`, Force MPC 3.9.1)
Pad index = row * 8 + col, row 0 = top (56 = bottom-left). User actions: NOTE on a keygroup/plugin track, pads,
octave up/down, then chromatic -> scale mode, change scale, change key.
- **Chromatic layout:** each row 5 semitones (a 4th) above the row below, +1 semitone per pad to the right
  (verified from which pads light together). All pads dim white `19 19 19`; root-note pads (pitch class of the key)
  in a colour (`66 05 66` here).
- **Scale (in-key) layout:** every pad is a scale note; each row starts 3 scale steps above the row below; the
  bottom-left pad is the root, so a key change leaves the root positions unchanged. Roots coloured (`66 05 35` /
  `66 35 05` seen), other pads dim white.
- **Scale mode on, before the scale change** (about 1:54): the layout stayed chromatic and a set of pads took the root
  colour (pitch classes 0, 2, 4, 6, 8 in that capture). Meaning not certain yet.
- **Pressing a pad:** the pad, and every pad with the exact same note, `00 7f 00` (bright green); pads with the same
  pitch class in other octaves `00 2a 11` (dim green); restored on release.
- **Octave up/down:** shown on the main display only ("Octave n"); pad colours do not change.
- SHIFT (LED `31`) relights the 16 RGB buttons at indexes 64-79 with their SHIFT functions.

## 22. Real Force NOTE mode, drum track (2026-10-25, device capture `local/captures/note-drum.bin`, Force MPC 3.9.1)
- **One bank of 16 pads, in the bottom-left 4x4** (indexes 32-35, 40-43, 48-51, 56-59; pad 1 = bottom-left, rows
  upward = MPC order). The other 48 pads are off (`00 00 00`) and hitting them does nothing visible. So the default is
  ADA2PadMap1x1, not the 2x2 four-bank layout (FINDINGS 20 found both in the binary; 2x2 must be an option).
- Colours: each pad's program colour at reduced level (components up to `3f`, e.g. `1f 00 00`, `28 00 3f`); a hit
  shows the pad brighter (`3f 00 00`), then back.
- SHIFT held cleared the 16 pads for a moment (`00 00 00`) and restored them after; NOTE LED value 4 once (meaning tbd).
