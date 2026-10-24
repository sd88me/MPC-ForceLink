import pickle,collections
ev=pickle.load(open('ev.pkl','rb'))
CIN={4:3,5:1,6:2,7:3,8:3,9:3,0xa:3,0xb:3,0xc:2,0xd:2,0xe:3}
t0=None;buf={}
rows=[]
for t,ty,xf,ep,dev,data in ev:
    if xf!=3 or not data or dev!=5: continue
    if (ty=='S' and ep==1) or (ty=='C' and ep==0x81):
        d='OUT' if ty=='S' else 'IN '
        for i in range(0,len(data)-3,4):
            h=data[i];cin=h&15;c=h>>4
            if cin not in CIN: continue
            p=bytes(data[i+1:i+1+CIN[cin]])
            k=(d,c)
            if cin in (4,5,6,7):
                if p[0]==0xf0: buf[k]=bytearray()
                buf.setdefault(k,bytearray()).extend(p)
                if cin>4: rows.append((t,d,c,bytes(buf.pop(k))))
            else: rows.append((t,d,c,p))
t0=rows[0][0]
last=None
for t,d,c,p in rows:
    if p[:5]==bytes([0xf0,0x47,0x7f,0x40,0x65]) or p[:2] in (b'\xb0\x35',): continue
    if p[:5]==bytes([0xf0,0x47,0x7f,0x40,0x0b]): s=p[:12].hex(' ')+f' ...({len(p)}B)'
    else: s=p.hex(' ')
    print(f'{t-t0:8.3f} {d} cab{c} {s[:150]}')
