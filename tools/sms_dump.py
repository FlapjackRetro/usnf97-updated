# Dumps USNF.SMS (USNF '97 symbol map): u32 count, then count*(u32 stroff, u32 addr), then NUL-terminated names.
import sys, struct
d = open(sys.argv[1], 'rb').read()
n, = struct.unpack_from('<I', d, 0); sb = 4 + n * 8
rows = []
for i in range(n):
    o, a = struct.unpack_from('<II', d, 4 + i * 8)
    rows.append((a, d[sb + o:d.index(b'\0', sb + o)].decode('latin1')))
for a, s in sorted(rows): print(f"{a:08x} {s}")
