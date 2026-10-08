# Lists the full path of every file/dir header in a plain (non-RDB) Amiga FFS/OFS hardfile
# whose name contains the given text. Usage: python hdf_find.py boot.hdf sysinfo
import struct, sys
data = open(sys.argv[1], 'rb').read()
want = sys.argv[2].lower()
BS = 512
def hdr(n):
    b = data[n*BS:(n+1)*BS]
    if len(b) < BS: return None
    typ, = struct.unpack('>l', b[0:4]); sec, = struct.unpack('>l', b[508:512])
    if typ != 2 or sec not in (1, 2, -3): return None
    ln = b[432]
    if ln > 30: return None
    return b[433:433+ln].decode('latin-1'), struct.unpack('>L', b[500:504])[0], sec
for n in range(len(data)//BS):
    h = hdr(n)
    if not h or want not in h[0].lower() or h[2] == 1: continue
    path, p, guard = [h[0]], h[1], 0
    while p and guard < 32:
        ph = hdr(p); guard += 1
        if not ph: break
        if ph[2] == 1: path.append(ph[0] + ':'); break
        path.append(ph[0]); p = ph[1]
    print('block %d: %s (%s)' % (n, '/'.join(reversed(path)).replace(':/', ':'), 'dir' if h[2] == 2 else 'file'))
