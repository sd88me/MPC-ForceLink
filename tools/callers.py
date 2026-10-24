# find function start (paciasp/bti c) before addr, then list BL callers of it. usage: callers.py ELF addr_hex...
import sys
sys.path.insert(0, __import__('os').path.join(__import__('os').path.dirname(__import__('os').path.abspath(__file__)), '..', 'local', 'pylib'))
import numpy as np
from elftools.elf.elffile import ELFFile
e = ELFFile(open(sys.argv[1], 'rb')); t = e.get_section_by_name('.text')
base = t['sh_addr']; data = t.data(); w = np.frombuffer(data[:len(data)//4*4], dtype='<u4')
bl = np.nonzero((w & 0xFC000000) == 0x94000000)[0]
off = (w[bl] & 0x3FFFFFF).astype(np.int64); off[off >= 1 << 25] -= 1 << 26
tgt = base + bl.astype(np.int64)*4 + off*4
def fstart(a):
    i = (a - base)//4
    while i > 0 and int(w[i]) not in (0xd503233f, 0xd503245f): i -= 1   # paciasp, bti c
    return base + i*4
for a in sys.argv[2:]:
    s = fstart(int(a, 16))
    cs = base + bl[tgt == s]*4
    print(a, 'func', hex(s), 'callers:', ' '.join(hex(int(c)) for c in cs[:30]))
