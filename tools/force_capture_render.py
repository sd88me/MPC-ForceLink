# Render a Force usbmon binary capture: final state of the 8 Q-link OLEDs (PNG) plus a text summary of
# OLED frame writes, pad RGB and button LEDs. usage: force_capture_render.py capture.bin out_prefix
import struct, sys, zlib
from collections import Counter

CIN = {4: 3, 5: 1, 6: 2, 7: 3}


def events(path):
    d = open(path, 'rb').read(); i = 0
    while i + 48 <= len(d):
        _, typ, xf, ep, dev, _, _, _, ts, tu, _, _, lc = struct.unpack_from('<QBBBBHbbqiiII', d, i)
        if typ not in (0x53, 0x43, 0x45) or lc > 65536: break
        yield ts + tu / 1e6, chr(typ), xf, ep, dev, d[i + 48:i + 48 + lc]
        i += 48 + lc


def midi_out(path):
    """(time, cable, message bytes) for every MPC -> controller USB-MIDI message, sysex reassembled per cable."""
    evs = [e for e in events(path) if e[1] == 'S' and e[2] == 3 and not (e[3] & 0x80) and e[5]]
    devs = Counter(e[4] for e in evs if b'\xf0\x47\x7f\x40' in e[5] or any(e[5][k + 1:k + 4] == b'\xf0\x47\x7f' for k in range(0, len(e[5]) - 3, 4)))
    dev = devs.most_common(1)[0][0] if devs else None
    bufs = {}
    for t, _, _, _, dv, data in evs:
        if dv != dev: continue
        for k in range(0, len(data) - 3, 4):
            h = data[k]; cable, cin = h >> 4, h & 15
            if cin == 0xB or cin == 0x9 or cin == 0x8:       # CC / note on / note off
                yield t, cable, bytes(data[k + 1:k + 4]); continue
            if cin not in CIN: continue
            p = data[k + 1:k + 1 + CIN[cin]]
            if p[:1] == b'\xf0': bufs[cable] = bytearray()
            bufs.setdefault(cable, bytearray()).extend(p)
            if cin in (5, 6, 7): yield t, cable, bytes(bufs.pop(cable))


def oled_blit(img, p):
    dsp, y0, y1, x0, x1 = p[7:12]; data = p[12:-1]
    pages, w = y1 - y0 + 1, x1 - x0 + 1
    bits = ''.join(f'{b:07b}' for b in data)
    if dsp not in img: img[dsp] = [[0] * 128 for _ in range(32)]
    for n in range(min(pages * w, len(bits) // 8)):
        v = int(bits[n * 8:n * 8 + 8], 2); pi, xi = divmod(n, w)
        for bit in range(8):
            y, x = (y0 + pi) * 8 + bit, x0 + xi
            if y < 32 and x < 128: img[dsp][y][x] = (v >> bit) & 1
    return dsp


def png(path, rows):
    h, w = len(rows), len(rows[0])
    raw = b''.join(b'\0' + bytes(rows[y]) for y in range(h))
    chunk = lambda t, b: struct.pack('>I', len(b)) + t + b + struct.pack('>I', zlib.crc32(t + b) & 0xffffffff)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 0, 0, 0, 0))
                           + chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b''))


def main(path, out):
    img, pads, leds, kinds, writes = {}, {}, {}, Counter(), Counter()
    for t, cable, m in midi_out(path):
        if m[:4] == b'\xf0\x47\x7f\x40':
            kinds[m[4]] += 1
            if m[4] == 0x0B: writes[oled_blit(img, m)] += 1
            elif m[4] == 0x65 and len(m) >= 12: pads[m[7]] = tuple(m[8:11])
        elif m[0] & 0xF0 == 0xB0: leds[m[1]] = m[2]
    print('sysex types:', ' '.join(f'{k:02X}x{v}' for k, v in sorted(kinds.items())))
    print('OLED writes per display:', dict(sorted(writes.items())))
    shown = [k for k in sorted(img) if k < 8]
    if shown:
        S = 3; rows = []
        for k in shown:
            for y in range(32):
                r = [255 if v else 0 for v in img[k][y] for _ in range(S)]
                rows += [r] * S
            rows += [[60] * (128 * S)] * 4
        png(out + '_oleds.png', rows)
        print('wrote', out + '_oleds.png', 'displays', shown)
    print('pad RGB (last):', ' '.join(f'{i}:{c[0]:02x}{c[1]:02x}{c[2]:02x}' for i, c in sorted(pads.items())))
    print('button LEDs (last):', ' '.join(f'{b:02x}={v}' for b, v in sorted(leds.items())))


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
