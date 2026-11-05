# forcelink

Use an Akai Force as a control surface for an MPC Live III (Gen 2), over the network.

- **link:** the Live III stays itself on MPC 3.10; the Force is an extra surface driven by an MPC 3.10 controller script
  (MATRIX clip grid, NOTE, STEP SEQ, Q-links with OLEDs drawn like the real Force; parallel to the Live III's own modes).
- **stock:** both back to normal. One switch: `link/forcelink link|stock`, or MAIN + MENU on either device.
- (An earlier Force-identity setup, "version A", is archived in `archive/force-identity/`.)

Status: personal research project, device-tested on one Force (MockbaMod, root) and one Live III (MPC 3.9.1 + the
3.10 app). Not a release yet. Use at your own risk: it restarts MPC (closes the open project) and needs root on both.

## Layout
| Folder | What |
|---|---|
| `link/` | the `forcelink` switch, device scripts, the controller script (`link/script/`); how to use: `link/README.md` |
| `force-link/` | Force side: `flsurf`, the switch addin, OLED renderer, tests |
| `spoof/` | Live III `LD_PRELOAD` shim: the "Force Link" MIDI port, the MAIN + MENU switch |
| `mpc310/` | run the MPC 3.10 app on a 3.9.1 Live III (kit built from the official image) |
| `tools/` | capture decoders, aarch64 disassembly helpers, script signer |
| `FINDINGS.md`, `HANDOFF.md`, `link/HANDOFF_LINK.md` | everything verified so far, and where to resume |

## Build
Put your own MPC 3.10.0.49 binary at `local/MPC` (git-ignored) and the two device IPs in `local/hosts.conf`
(`FORCE=...`, `LIVE3=...`), then `./build.sh`. The binary is only read: the OLED font and the script checksum are
derived from it at build time and are never committed. Akai's software is not included and must not be added.
