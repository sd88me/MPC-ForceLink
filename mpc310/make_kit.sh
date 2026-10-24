#!/bin/sh
# Build the MPC 3.10 kit from the official Gen2 update image (offline, on the dev box):
#   make_kit.sh <MPC-3.10.0-Gen2-update.img> <out dir>
# Out: <out>/mpc310/{MPC, Akai/ (= /usr/share/Akai of 3.10), on.sh, off.sh, VERSION}. Copy <out>/mpc310 to /data on the
# Live III (see README.md). The rootfs is the third xz stream of the image (2026-10-24: 0x258c850).
set -eu
img=$1; out=$2
here=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$out/mpc310"
python3 - "$img" "$out/rootfs310.img" <<'PY'
import lzma, mmap, sys
src, dst = sys.argv[1], sys.argv[2]
f = open(src, 'rb'); m = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
offs = []; i = 0
while (i := m.find(b'\xfd7zXZ\x00', i)) >= 0: offs.append(i); i += 1
if len(offs) < 3: sys.exit("expected three xz streams (recovery, boot, rootfs)")
f.seek(offs[2]); d = lzma.LZMADecompressor(format=lzma.FORMAT_XZ)
with open(dst, 'wb') as o:
    while not d.eof:
        c = f.read(1 << 20)
        if not c: break
        o.write(d.decompress(c))
PY
debugfs -R "dump /usr/bin/MPC $out/mpc310/MPC" "$out/rootfs310.img" 2>/dev/null
chmod 755 "$out/mpc310/MPC"
debugfs -R "rdump /usr/share/Akai $out/mpc310" "$out/rootfs310.img" 2>/dev/null
strings "$out/mpc310/MPC" | grep -m1 -E '^3\.10\.[0-9.]+ \(' > "$out/mpc310/VERSION"
cp "$here/on.sh" "$here/off.sh" "$out/mpc310/"
rm -f "$out/rootfs310.img"
echo "kit: $out/mpc310 ($(cat "$out/mpc310/VERSION"))"; du -sh "$out/mpc310"
