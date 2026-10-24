# recompute autorun.check for a stick folder; prints only the digest and whether it matches. usage: recheck.py MPC stickdir [write]
import json, sys, pathlib
sys.path.insert(0, str(pathlib.Path(__file__).parent))
import saltscan, make_stick
salt, hits = saltscan.find_salt(sys.argv[1])
a = pathlib.Path(sys.argv[2]) / 'autorun'
files = json.loads((a / 'autorun.json').read_text())['value0']['files']
d = make_stick.autorun_check(a, files, salt)
cur = (a / 'autorun.check').read_bytes().strip()
print('salt hits', len(hits), 'matches existing:', d == cur)
if len(sys.argv) > 3: (a / 'autorun.check').write_bytes(d); print('written')
