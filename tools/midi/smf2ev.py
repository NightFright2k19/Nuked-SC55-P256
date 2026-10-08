import struct,sys
d=open(sys.argv[1],'rb').read()
def vlq(p):
    v=0
    while True:
        b=d[p];p+=1;v=(v<<7)|(b&0x7f)
        if not b&0x80: return v,p
fmt,ntrk,div=struct.unpack('>HHH',d[8:14]);p=14;ev=[];tempo=[]
for t in range(ntrk):
    assert d[p:p+4]==b'MTrk';ln=struct.unpack('>I',d[p+4:p+8])[0];p+=8;end=p+ln;tick=0;rs=0
    while p<end:
        dt,p=vlq(p);tick+=dt;b=d[p]
        if b==0xFF:
            ty=d[p+1];l,q=vlq(p+2)
            if ty==0x51: tempo.append((tick,int.from_bytes(d[q:q+3],'big')))
            p=q+l
        elif b in (0xF0,0xF7):
            l,q=vlq(p+1);ev.append((tick,[0xF0]+list(d[q:q+l]) if b==0xF0 else list(d[q:q+l])));p=q+l
        else:
            if b&0x80: rs=b;p+=1
            n=1 if (rs&0xF0) in (0xC0,0xD0) else 2
            ev.append((tick,[rs]+list(d[p:p+n])));p+=n
    p=end
tempo.sort();ev.sort(key=lambda e:e[0])
def sec(tk):
    s=0;lt=0;us=500000
    for t,u in tempo:
        if t>=tk:break
        s+=(t-lt)*us/div/1e6;lt=t;us=u
    return s+(tk-lt)*us/div/1e6
with open(sys.argv[2],'w') as f:
    for tk,b in ev: f.write(f"{sec(tk):.6f} {len(b)} "+" ".join(map(str,b))+"\n")
print("Events",len(ev),"Dauer %.1f s"%sec(ev[-1][0]))
