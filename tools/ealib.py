"""List / extract EA 'EALIB' archives (USNF_*.LIB).
Header: b'EALIB' + u16 count, then count x {char name[13]; u8 flags; u32 offset}.
Each entry: u32 unpacked size + PKWARE DCL 'implode' stream.
usage: ealib.py LIB -l | ealib.py LIB OUTDIR [NAME ...]"""
import os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from esa_extract import blast

def entries(d):
    assert d[:5] == b'EALIB', 'not an EALIB file'
    n = struct.unpack_from('<H', d, 5)[0]
    e = [(d[7+i*18:7+i*18+13].split(b'\0')[0].decode('latin1'), d[7+i*18+13], struct.unpack_from('<I', d, 7+i*18+14)[0]) for i in range(n)]
    for i, (name, flags, off) in enumerate(e):
        end = e[i+1][2] if i + 1 < n else len(d)
        yield name, flags, off, end

def read(d, off, end):
    size = struct.unpack_from('<I', d, off)[0]
    data = blast(d[off+4:end])
    assert len(data) == size, 'size mismatch'
    return data

if __name__ == '__main__':
    d = open(sys.argv[1], 'rb').read()
    if len(sys.argv) > 2 and sys.argv[2] == '-l':
        for name, flags, off, end in entries(d): print(f'{name:13} {end-off-4:>9}')
        sys.exit()
    os.makedirs(sys.argv[2], exist_ok=True)
    want = {x.upper() for x in sys.argv[3:]}
    for name, flags, off, end in entries(d):
        if want and name.upper() not in want: continue
        open(os.path.join(sys.argv[2], name), 'wb').write(read(d, off, end))
        print(name)
