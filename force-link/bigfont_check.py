#!/usr/bin/env python3
"""Offline check: re-render every large-font value frame in a capture and count pixel differences in the text rows.
usage: bigfont_check.py <capture.bin> <renderer binary: prints 32 rows of 0/1 for `value bar bipolar`>"""
import hashlib, os, subprocess, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'tools'))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import force_capture_render as r, gen_big_font as g  # noqa: E402
img, done = {}, {}
for _, _, m in r.midi_out(sys.argv[1]):
    if m[:5] != b'\xf0\x47\x7f\x40\x0b':
        continue
    d = r.oled_blit(img, m)
    if d >= 8:
        continue
    f = img[d]
    tall = [y for y in range(3, 18) if any(f[y])]
    if not tall or tall[-1] - tall[0] < 9:
        continue
    cols = [x for x in range(128) if any(f[y][x] for y in range(5, 17))]
    segs, cur = [], []
    for x in range(129):
        if x < 128 and x in cols:
            cur.append(x)
        elif cur:
            segs.append(cur); cur = []
    text, prev, ok = '', None, True
    for sg in segs:
        rows = [''.join('#' if f[y][c] else '.' for c in sg) for y in range(5, 17)]
        ch = g.FINGERPRINT.get(hashlib.sha1('/'.join(rows).encode()).hexdigest()[:10])
        if not ch:
            ok = False; break
        if prev is not None and sg[0] - prev > 5:
            text += ' '
        text += ch; prev = sg[-1]
    if not ok or text in done:
        continue
    out = subprocess.run([sys.argv[2], text, '0', '0'], capture_output=True, text=True).stdout.split()
    done[text] = sum(int(out[y][x]) != f[y][x] for y in range(18) for x in range(128))
bad = {t: v for t, v in done.items() if v}
print(len(done), 'strings,', len(bad), 'differ', dict(list(bad.items())[:10]))
sys.exit(1 if bad else 0)
