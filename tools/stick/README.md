# The Live III boot stick (forcelink's "normal setup")

**Obsolete (2026-11-05: the Live III runs stock MPC 3.10.1.8, no overlay stick needed). Superseded 2026-10-24:** the boot stick now comes from the separate repo `~/mpc310-on-391` (`build_stick.py`), which
includes this boot hook. Notes below are kept for history.

The Live III's `/etc` is not persistent and its root is read-only, so the only boot-time hook is MPC's stock USB
autorun. The SSH stick (made by mpc-vst-plugins' `tools/usb_stick/make_stick.py`, branch `usb-bootstrap`) is changed in
two places (verified on the device 2026-10-24):
- `run.sh-boot-hook.patch`: after re-adding the installed addins, run each addin's own `boot.sh` from
  `/data/mpc-addins/<id>/`. forcelink's (`link/live3/boot.sh`) switches to `home.conf` (b = MPC 3.10 + Force link)
  once per boot (marker `/run/forcelink-boot-done`).
- `autorun.json-hands-free.patch`: `askQuestion: false`. MPC then runs the stick at start without a prompt; it ran
  once per boot (the restart did not trigger it again).

After editing the stick's files, re-sign: `python3 -I recheck.py <your Live III 3.9.1 MPC binary> <stick folder> write`
(run it first without `write`: on an unchanged stick it must say `matches existing: True`). It needs
`make_stick.py` and `saltscan.py` from that branch next to it. The salt is read from your binary and never stored or
printed. Never publish a signed stick or the salt.
