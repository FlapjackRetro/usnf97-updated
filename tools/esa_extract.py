import sys,struct
def mk(rep):
    lens=[]
    for b in rep: lens += [b&15]*((b>>4)+1)
    cnt=[0]*14
    for l in lens: cnt[l]+=1
    offs=[0]*14
    for i in range(1,13): offs[i+1]=offs[i]+cnt[i]
    sym=[0]*len(lens)
    for s,l in enumerate(lens):
        sym[offs[l]]=s; offs[l]+=1
    return cnt,sym
LIT=mk([11,124,8,7,28,7,188,13,76,4,10,8,12,10,12,10,8,23,8,9,7,6,7,8,7,6,55,8,23,24,12,11,7,9,11,12,6,7,22,5,7,24,6,11,9,6,7,22,7,11,38,7,9,8,25,11,8,11,9,12,8,12,5,38,5,38,5,11,7,5,6,21,6,10,53,8,7,24,10,27,44,253,253,253,252,252,252,13,12,45,12,45,12,61,12,45,44,173])
LEN=mk([2,35,36,53,38,23]); DIST=mk([2,20,53,230,247,151,248])
BASE=[3,2,4,5,6,7,8,9,10,12,16,24,40,72,136,264]; EXTRA=[0,0,0,0,0,0,0,0,1,2,3,4,5,6,7,8]
def blast(src):
    pos=0; bb=0; bc=0
    def bits(n):
        nonlocal pos,bb,bc
        while bc<n: bb|=src[pos]<<bc; pos+=1; bc+=8
        v=bb&((1<<n)-1); bb>>=n; bc-=n; return v
    def dec(h):
        cnt,sym=h; code=first=index=0
        for l in range(1,14):
            code|=bits(1)^1; c=cnt[l]
            if code<first+c: return sym[index+code-first]
            index+=c; first+=c; first<<=1; code<<=1
        raise ValueError
    lit=bits(8); dic=bits(8); out=bytearray()
    while True:
        if bits(1):
            s=dec(LEN); ln=BASE[s]+bits(EXTRA[s])
            if ln==519: break
            s=2 if ln==2 else dic
            dist=(dec(DIST)<<s)+bits(s)+1
            for _ in range(ln): out.append(out[-dist])
        else:
            out.append(dec(LIT) if lit else bits(8))
    return bytes(out)

# ---- EA "ELECTRONIC_ARTS_ARCHIVE_FILE" (SETUP.ESA) reader ----
# Entry: name\0 label\0 u32 flags, u32 size, u32 dostime, method\0 ("PKWA"=DCL implode | "NULL"=stored), u32 csize, u32 offset
def entries(d):
    import struct
    p = d.index(b'\0') + 1
    while True:
        e = d.index(b'\0', p); name = d[p:e].decode('latin1'); p = e + 1
        e = d.index(b'\0', p); label = d[p:e].decode('latin1'); p = e + 1
        flags, size, t = struct.unpack_from('<IIi', d, p); p += 12
        e = d.index(b'\0', p); meth = d[p:e].decode(); p = e + 1
        csz, off = struct.unpack_from('<II', d, p); p += 8
        yield name, label, size, meth, csz, off
        if name.upper().startswith('PKCOMP'): break

if __name__ == '__main__':
    import sys, os
    if len(sys.argv) < 3:
        sys.exit("usage: esa_extract.py SETUP.ESA OUTDIR [NAME ...]   (-l to list)")
    d = open(sys.argv[1], 'rb').read()
    if sys.argv[2] == '-l':
        for x in entries(d): print(*x)
        sys.exit()
    os.makedirs(sys.argv[2], exist_ok=True)
    want = {n.upper() for n in sys.argv[3:]}
    for name, label, size, meth, csz, off in entries(d):
        if want and name.upper() not in want: continue
        raw = d[off:off + csz]
        data = blast(raw) if meth == 'PKWA' else raw
        assert len(data) == size, (name, len(data), size)
        open(os.path.join(sys.argv[2], name), 'wb').write(data)
        print(f"{name:20} {size:>10}  {meth}")
