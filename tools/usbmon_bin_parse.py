import struct,sys
d=open(sys.argv[1],'rb').read(); i=0; ev=[]
while i+48<=len(d):
    id_,typ,xf,ep,dev,bus,fs,fd,ts,tu,st,ln,lc=struct.unpack_from('<QBBBBHbbqiiII',d,i)
    if typ not in (0x53,0x43,0x45) or lc>65536: print('desync at',i); break
    data=d[i+48:i+48+lc]; i+=48+lc
    ev.append((ts+tu/1e6,chr(typ),xf,ep,dev,data))
print(len(ev),'events')
import pickle; pickle.dump(ev,open('ev.pkl','wb'))
sel=[e for e in ev if e[4]==5 and e[2]==3 and e[3]==1 and e[1]=='S' and e[5]]
print(len(sel),'bulk OUT submits to dev5 ep1; sizes',sorted(set(len(e[5]) for e in sel))[:20])
