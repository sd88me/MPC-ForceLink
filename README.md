# forcelink

Use an Akai Force as a control surface for an MPC Live III (Gen 2), over the network.

- **Version A:** the Live III runs MPC 3.10 as a Force (Gen 2); the Force is its full Force surface (shared modes).
- **Version B:** the Live III stays itself; the Force is an extra surface driven by an MPC 3.10 controller script
  (MATRIX clip grid, NOTE, STEP SEQ, Q-links with OLEDs drawn like the real Force; parallel to the Live III's own modes).
- **stock:** both back to normal. One switch: `link/forcelink stock|a|b`.

Status: personal research project, device-tested on one Force (MockbaMod, root) and one Live III (MPC 3.9.1 + the
3.10 app). Not a release yet. Use at your own risk: it restarts MPC (closes the open project) and needs root on both.

## Layout
| Folder | What |
|---|---|
| `link/` | the `forcelink` switch, device scripts, version A/B bundles; how to use: `link/README.md` |
| `force-link/` | Force-side daemons (`flsurf` for B, `flnet` for A), OLED renderer, tests |
| `spoof/` | Live III `LD_PRELOAD` shim (identity spoof, network relay, B's "Force Link" MIDI port) |
| `mpc310/` | run the MPC 3.10 app on a 3.9.1 Live III (kit built from the official image) |
| `tools/` | capture decoders, aarch64 disassembly helpers, script signer |
| `FINDINGS.md`, `HANDOFF.md`, `link/HANDOFF_B.md` | everything verified so far, and where to resume |

## Build
Put your own MPC 3.10.0.49 binary at `local/MPC` (git-ignored) and the two device IPs in `local/hosts.conf`
(`FORCE=...`, `LIVE3=...`), then `./build.sh`. The binary is only read: the OLED font and the script checksum are
derived from it at build time and are never committed. Akai's software is not included and must not be added.
