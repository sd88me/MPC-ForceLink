# Print vtable slots for classes by mangled typeinfo name (e.g. 20ADA2PadNoteResponder). usage: vtables.py ELF name...
import sys
sys.path.insert(0, __import__('os').path.join(__import__('os').path.dirname(__import__('os').path.abspath(__file__)), '..', 'local', 'pylib'))
from elftools.elf.elffile import ELFFile
p = sys.argv[1]; e = ELFFile(open(p, 'rb')); d = open(p, 'rb').read()
rel = {}
for s in e.iter_sections():
    if s.header.sh_type == 'SHT_RELA':
        for r in s.iter_relocations(): rel[r['r_offset']] = r['r_addend']
byadd = {}
for off, a in rel.items(): byadd.setdefault(a, []).append(off)
for name in sys.argv[2:]:
    na = d.find(name.encode() + b'\0')
    while na > 0 and d[na-1:na].isalnum(): na = d.find(name.encode() + b'\0', na + 1)
    for tn in byadd.get(na, []):
        ti = tn - 8
        for v in byadd.get(ti, []):
            if v == ti: continue
            slots = []
            k = v + 8
            while rel.get(k) and 0xc00000 < rel[k] < 0x5000000 and len(slots) < 40: slots.append(hex(rel[k])); k += 8
            if slots: print(name, 'vtable', hex(v + 8), ' '.join(slots))
