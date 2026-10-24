import pickle,sys
exec(open('hs.py').read().split("t0=rows[0][0]")[0].replace("ev.pkl","ev.pkl"))
t0=rows[0][0]
bursts=[];cur=None;last=-9
for t,d,c,p in rows:
    if d=='OUT' and p[:5]==bytes([0xf0,0x47,0x7f,0x40,0x65]):
        if t-last>0.25: cur=[]; bursts.append((t-t0,cur))
        cur.append(p[7:11]); last=t
state={}
for bt,msgs in bursts:
    for m in msgs: state[m[0]]=tuple(m[1:4])
    if len(msgs)<8: continue
    print(f'--- burst t={bt:.1f}s  {len(msgs)} msgs; grid (top row first) rows=idx//8 from bottom')
    for row in range(7,-1,-1):
        print(' '.join('%02x%02x%02x'%state.get(row*8+c,(0,0,0)) for c in range(8)))
