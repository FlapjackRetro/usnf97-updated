"""Convert a USNF '97 .PIC (from ealib.py) to PNG.
Header: u16 type, u32 width, u32 height, u32 dataOffset (0x40); type 1 adds at 0x1a
u32 spanTable, u32 spanTableBytes. Type 0 = raw 8-bit rows. Type 1 = span list of
{u16 y, u16 x0, u16 x1, u32 pixelOffset} records (inclusive x0..x1, 0xFFFF y ends),
transparent elsewhere. Cockpit/canopy frames are type 1.
usage: pic2png.py FILE.PIC out.png [PALETTE.PAL]   (no palette = grayscale by index range)"""
import struct, sys, zlib

def decode(b):
    typ, w, h, off = struct.unpack_from('<HIII', b, 0)
    img = [[None] * w for _ in range(h)]
    if typ == 0:
        for y in range(h):
            img[y] = list(b[off + y * w: off + (y + 1) * w])
    else:
        tab, tsize = struct.unpack_from('<II', b, 0x1a)
        for k in range(tsize // 10):
            y, x0, x1, po = struct.unpack_from('<HHHI', b, tab + k * 10)
            if y == 0xffff: break
            for x in range(x0, x1 + 1): img[y][x] = b[off + po + (x - x0)]
    return typ, w, h, img

def write_png(path, w, h, rows):
    raw = b''.join(b'\0' + r for r in rows)
    def ch(t, d): c = struct.pack('>I', len(d)) + t + d; return c + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n' + ch(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) +
                           ch(b'IDAT', zlib.compress(raw, 6)) + ch(b'IEND', b''))

if __name__ == '__main__':
    typ, w, h, img = decode(open(sys.argv[1], 'rb').read())
    pal = None
    if len(sys.argv) > 3:
        p = open(sys.argv[3], 'rb').read(); pal = bytes(min(255, v * 4 + v // 16) for v in p)
    used = sorted({v for r in img for v in r if v is not None}) or [0]
    lo, hi = used[0], used[-1]
    rows = []
    for y in range(h):
        r = bytearray()
        for x in range(w):
            v = img[y][x]
            if v is None: r += b'\0\0\0\0'
            elif pal: r += pal[v*3:v*3+3] + b'\xff'
            else: g = 40 + 215 * (v - lo) // max(1, hi - lo); r += bytes((g, g, g, 255))
        rows.append(bytes(r))
    write_png(sys.argv[2], w, h, rows)
    print(f'{sys.argv[1]}: type {typ}, {w}x{h}')
