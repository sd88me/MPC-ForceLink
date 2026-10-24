import re,sys,collections
CIN={0x4:3,0x5:1,0x6:2,0x7:3,0x8:3,0x9:3,0xa:3,0xb:3,0xc:2,0xd:2,0xe:3,0xf:1}
msgs=[]  # (t,dir,cable,bytes)
for l in open(sys.argv[1]):
    m=re.match(r'\S+ (\d+) ([SC]) (B[io]):2:005:1 \S+ (\d+) = (.*)',l)
    if not m: continue
    t,sc,d,n,hexs=m.groups()
    if (d=='Bo' and sc!='S') or (d=='Bi' and sc!='C'): continue
    b=bytes.fromhex(hexs.replace(' ',''))
    for i in range(0,len(b)-3,4):
        h=b[i]; cin=h&15
        if cin==0: continue
        msgs.append((int(t),d,h>>4,cin,b[i+1:i+1+CIN.get(cin,3)]))
# reassemble sysex per (dir,cable)
out=[];buf={}
for t,d,c,cin,p in msgs:
    k=(d,c)
    if cin in (4,5,6,7):
        if p and p[0]==0xf0: buf[k]=bytearray()
        buf.setdefault(k,bytearray()).extend(p)
        if cin in (5,6,7):
            out.append((t,d,c,'SYX',bytes(buf.pop(k))))
    else:
        out.append((t,d,c,'MSG',p))
t0=out[0][0]
cnt=collections.Counter()
for t,d,c,ty,p in out:
    key=(d,c,ty,p[:5].hex() if ty=='SYX' else p[:2].hex())
    cnt[key]+=1
for k,v in sorted(cnt.items(), key=lambda x:-x[1])[:40]: print(v,k)
print('----- non-repeating timeline')
for t,d,c,ty,p in out:
    if ty=='MSG' and p[:2]==bytes([0xb0,0x35]): continue
    print(f'{(t-t0)/1e6:7.3f} {d} cab{c} {ty} {p.hex(" ")[:150]}')
