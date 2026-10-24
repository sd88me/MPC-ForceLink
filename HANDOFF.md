# Handoff: Force as a hardware extension of an MPC Live III (2026-10-15)

Details live in `FINDINGS.md` (§9-§12.6) and memory `force-controller-protocol.md`. Code is in `force-link/`.

## Where we are
- **Input is fully verified on hardware** (§12.5, 12.6): pads ch9 notes 54+row*8+col (row 0 top, note-on vel always 127, pressure via aftertouch 0xA9), buttons ch0 notes (full named set), Q-link touch 0x53+k, Q-link knobs CC 0x10+k (running count since touch, step = v-prev mod 128), main encoder CC 100 relative.
- **Output verified visually:** 64 pad RGB and 8 Q-link OLEDs (§12.4). Sysex MUST be paced (drain + >=3 ms between messages; `amidi -s` burst dropped a display) and the ~4 s heartbeat answered or the displays blank.
- There is NO ninth physical display; disp 16 is not an OLED (purpose unknown).
- Daemon `force-link/forcelink` (armhf): handshake replay, heartbeat, parsing, stdin LED/OLED commands (`flcmd`). Latest on device: `/tmp/fl/forcelink.v4` (tmp is volatile, rebuild with `force-link/build.sh`). Offline: `preview.c`, tests (parse 11, cmd 10, oled round-trip).
- Not done: the daemon does not yet translate inputs into anything, and nothing feeds it Live III state.

## Next tests (need the user at the Force; MPC stops during each, project closes, so save first)
Run pattern: stage under a temp name, `nohup sh runN.sh` over `ssh -n`, EXIT trap restarts `acvs` (check `systemctl is-active acvs` afterwards; start it by hand if inactive).
1. **Clip-state colours** (makes sound): in MPC launch / queue / stop a clip while sniffing `/dev/usbmon2` (binary, tools in `tools/`) to get playing and queued pad colours and any blink behaviour. Needed for the mirror.
2. **Unknown messages:** meaning of `0A 00 07 <disp> ...`, `6A 00 03 n a b`, per-control `42`, `5A 00 20`, and what disp 16 does. Try varying fields in the daemon and watch the controller.
3. **Button LED states** (CC on Private port: 0 off, 1 dim, 3 bright, others unknown): sweep values 0-127 per button, record the colours.
4. **Output index orientation:** confirm pad idx 0 is the top-left LED (input is top-down; output unchecked).
5. **USB gadget to a host:** `echo external > /sys/devices/platform/usb-mux/state`, bind the MIDI gadget (setup was done once with `libcomposite`, torn down), connect rear USB-B to a PC and check enumeration. `internal` is the default and enumerates on the Force's own hub.
6. **Live-feed loop on the Force alone:** a script emits `clip`/`track`/`qlink` commands into `forcelink` from a file to prove latency and refresh rate (OLED full frame vs partial region writes).

## Plan for later (needs the Live III)
1. **Live III 3.10 (Lua, checksum cracked):** device script that exposes clip matrix, track names/levels, Q-link names to a MIDI/USB path the Force daemon reads. This gives full state with no root tricks. (Salt never to be published.)
2. **Live III 3.9.1 path (mapping-only Lua):** Force works as input surface and screen mirror only; for full state use the ADA2 route below.
3. **ADA2/IamForce-style route (all UNVERIFIED):** the 3.9.1 binary contains the Force (ADA2) control-surface code. Plan: autorun root script on a USB stick (integrity file is plain sha256), systemd drop-in in `/run/systemd/system/acvs.service.d/`, `LD_PRELOAD` shim that fakes the `inmusic,product-code` and the identity sysex so the Live III talks Force protocol to our Force. The Force already speaks that protocol, so the bridge is pass-through. Smoke tests in order: autorun trigger, root shell, drop-in honoured, preload loads, boots under ADA2 identity. Test on a disposable setup (autorun is unsandboxed root).
4. **MITM tap on the Force's own MPC:** extend `~/mpc-addin-remote/src/surface.c` (already interposes `snd_rawmidi_open/read`) to copy port traffic to `/tmp/forcelink.sock`; use it to learn the full ADA2 vocabulary (clip states, Q-link pages) from a working Force instead of guessing. Designed, not built; only deploy with the user watching (a preload fault can crash-loop MPC).
5. **Screen mirror:** autorun daemon grabs the DRM framebuffer on the Live III, streams to the Force screen; touch returns via uinput. No Force-side work started.
6. **Final shape on the Force:** stop local MPC, run our daemon as a service (with delayed start and a restore path), network-MIDI or USB-gadget link to the Live III, 9-track header via the main screen, Q-link OLEDs for names/levels.

## Rules to keep
Ask before restarting MPC unless the user said to; stage files under a temp name then `mv`; stop voice engines before restarting `acvs`; Force IP is in local/hosts.conf (DHCP, verify); BusyBox userland (no `timeout`, `head -n`, no pkill); never publish the Lua checksum salt; no git repo here.

## Update 2026-10-24 (network link working; see FINDINGS §13-15)
Transport is TCP (USB gadget blocked by the Force's usb-mux). Order of tests for the clip grid, all reversible:
1. **Shim mode `s` (ADA2S = Force Gen 2, az04 board, controller id 0x04 translated)** with the new flnet (DFU guard). Check: no "error connecting", audio present?, grid colours, NOTE / STEP SEQ, clip launch.
2. Mode `b` (Force Gen 1 board; known to kill audio) only as a diagnostic.
3. Option 3: MPC 3.10 via `mpc310/on.sh` (optionally combined with mode `s` or plain ADA2).
Device commands: Force `systemctl stop acvs; nohup /tmp/fl/flnet -r >> /tmp/fl/net.log 2>&1 &` (re-copy flnet if /tmp was wiped; restore: `kill $(pidof flnet); systemctl start acvs`). Live III: `echo s > /data/mpc-addins/forcespoof/dt.conf; echo <force ip> > .../net.conf; sh .../on.sh` (restore: `rm .../dt.conf; sh .../off.sh`). Live III root needs the autorun stick after every reboot.

## Handoff 2026-10-24: the Force link works (start here)
Read `link/README.md` first. `link/forcelink on|off|status|install` drives both devices (hosts in link/hosts.conf).
State: link ON when this was written (Live III: MPC 3.10.0.49 + ADA2S + bridge; Force: flnet, its MPC stopped).
Build commands:
- Live III shim: `cd spoof && docker run --rm --platform linux/arm64 -u $(id -u):$(id -g) -v $PWD:/b -w /b arm64v8/gcc:12-bookworm sh -c 'gcc -std=gnu11 -O2 -Wall -Wextra -fPIC -shared -o forcespoof.so forcespoof.c -ldl -lpthread && strip --strip-unneeded forcespoof.so'`; offline tests `gcc -DBRIDGE_TEST -o bt bridge_test.c -ldl -lpthread && ./bt`, idmap_test.c.
- Force flnet: armhf stretch container as in force-link/build.sh, `gcc -O2 -Wall -Wextra -o flnet flnet.c -lasound`; test dfu_guard_test.c.
- Then copy into link/live3, link/force and `./forcelink install` (then off/on to load the new shim).
Next work (link/README.md "To refine"): Live III pad LED quadrants (probe the 80 LED indexes), button LED values and step-button RGB, map the remaining Live III buttons and the touch strip; later a one-step start after reboot (stick + forcelink on).
Rules: ask before restarting MPC on either device (closes projects); never publish the Lua salt or the stick folder; the autorun stick is the only root path on the Live III and must be rerun after each reboot.

## Update 2026-10-24 (later): version A bundled, B started
- `link/forcelink stock|a|b|capture|status|install|probe` is the single switch (README in link/). Version A files in `link/a/`
  (shim build with map.conf overrides + /tmp/l3probe LED probe; bridge_test.c 9/9). A is parked: its next steps need someone
  at the devices (link/README.md "Sessions": pad LED quadrants via probe, and a capture of the Live III's own 8x8 mode).
- Version B (Live III native on 3.10 + the Force as a Lua-scripted extra surface, parallel modes) is next; `forcelink b` is a stub.
- **B built offline (2026-10-24):** `link/b/` (ForceLink.lua v0 + signed .ini via b/build.sh, flsurf, handshake.syx; shim B mode = `b.conf`), `forcelink b` wired on both devices and installed. Tests: b/test_forcelink.lua 12/12 (mock API), force-link/flsurf_test.c 4/4, bridge_test.c 9/9. tools/sign_script.py now knows 3.10.0.49 (salt offset 0x5287C10, verified against the shipped MPK script). Not run on hardware yet: first run needs the user watching (both MPCs restart).
- Open questions for B's first run: does MPC list the shim's seq port "Force Link" and bind the script to it (port-name forms in ForceLink.ini.in); where the per-device clip aperture lives (script reports on screen); clip State values' type.

## Handoff 2026-10-24 (B): see link/HANDOFF_B.md
