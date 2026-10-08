# Extracts files from a plain (non-RDB) Amiga OFS/FFS hardfile.
#   hdf_get.py <hdf> list [text]          every file whose path contains text, with icon type
#   hdf_get.py <hdf> get <Vol:path> <out> copy one file out
import struct, sys
data = open(sys.argv[1], 'rb').read()
BS = 512
ffs = data[3] & 1
def blk(n): return data[n * BS:(n + 1) * BS]
def L(b, o): return struct.unpack('>l', b[o:o + 4])[0]
def hdr(n):
    b = blk(n)
    if len(b) < BS or L(b, 0) != 2 or L(b, 508) not in (1, 2, -3): return None
    ln = b[432]
    if ln > 30: return None
    return b[433:433 + ln].decode('latin-1'), struct.unpack('>L', b[500:504])[0], L(b, 508)
def path(n):
    h = hdr(n); parts = [h[0]]; p = h[1]; g = 0
    while p and g < 40:
        ph = hdr(p); g += 1
        if not ph: break
        if ph[2] == 1: parts.append(ph[0] + ':'); break
        parts.append(ph[0]); p = ph[1]
    return '/'.join(reversed(parts)).replace(':/', ':')
def read_file(n):
    b = blk(n); size = struct.unpack('>L', b[324:328])[0]; out = bytearray(); cur = n
    while cur:
        c = blk(cur)
        cnt = L(c, 8)
        for i in range(cnt):
            d = L(c, 308 - 4 * i)
            db = blk(d)
            out += db if ffs else db[24:24 + L(db, 12)]
        cur = L(c, 504)
    return bytes(out[:size])
files = {}
for n in range(len(data) // BS):
    h = hdr(n)
    if h and h[2] == -3: files[path(n)] = n
if sys.argv[2] == 'list':
    t = sys.argv[3] if len(sys.argv) > 3 else ''
    for p in sorted(files):
        if t.lower() in p.lower():
            extra = ''
            if p.endswith('.info'):
                d = read_file(files[p]); extra = ' type=%d size=%d' % (d[0x30] if len(d) > 0x30 else -1, len(d))
            print(p + extra)
else:
    open(sys.argv[4], 'wb').write(read_file(files[sys.argv[3]]))
    print('written', sys.argv[4])
