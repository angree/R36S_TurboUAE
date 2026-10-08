# Amiberry 2.x/3.x src/osdep/amiberry_mem.cpp: map the Amiga address space WITHOUT MAP_FIXED.
#
# Upstream maps 16 MB at 0x20000000 and the Z3/RTG area above it with MAP_FIXED, which
# silently replaces whatever is already there. On the R36S (ASLR on, non-PIE binary) the
# process heap starts anywhere from about 0x00c00000 to 0x40000000; when it sat under the
# natmem window the mapping wiped live heap and glibc aborted ("sysmalloc: Assertion")
# within a second of starting with JIT. Here every mapping is a hint whose result is checked,
# and the first area moves to the next free base if 0x20000000 is taken. MAP_NORESERVE: the
# Z3/RTG area is address space, not memory, and must not depend on how much is free.
# Works on v2.21 and v3.3 (different cast styles); usage: natmem.py <amiberry_mem.cpp>
import re, sys
p = sys.argv[1]
s = open(p).read()
if 'map_exact' in s:
    print('already patched'); sys.exit(0)
helper = '''
/* R36S: map exactly at want or fail - never over an existing mapping (see build/patches) */
static uae_u8* map_exact(void* want, size_t size)
{
	void* p = mmap(want, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
	if (p == MAP_FAILED)
		return (uae_u8*)MAP_FAILED;
	if (p != want) {
		munmap(p, size);
		return (uae_u8*)MAP_FAILED;
	}
	return (uae_u8*)p;
}
'''
anchor = '#define A3000MEM_START 0x08000000\n'
assert anchor in s
s = s.replace(anchor, anchor + helper, 1)

def stmt(start_pat):
    m = re.search(start_pat, s)
    assert m, start_pat
    return m.start(), s.index(';', m.start()) + 1

a, b = stmt(r'\tregs\.natmem_offset = [^;]*mmap\(reinterpret_cast<void \*>\(0x20000000\)')
s = s[:a] + '''\tregs.natmem_offset = nullptr;
	for (uintptr_t base = 0x20000000; base <= 0x30000000 && !regs.natmem_offset; base += 0x02000000) {
		uae_u8* m = map_exact(reinterpret_cast<void*>(base), natmem_size + BARRIER);
		if (m != MAP_FAILED)
			regs.natmem_offset = m;
		else
			write_log("natmem: 0x%08lx is taken, trying the next base\\n", (unsigned long)base);
	}''' + s[b:]
for off in ('Z3BASE_REAL', 'Z3BASE_UAE'):
    a, b = stmt(r'\tadditional_mem = [^;]*mmap\(regs\.natmem_offset \+ ' + off)
    s = s[:a] + '\tadditional_mem = map_exact(regs.natmem_offset + %s, ADDITIONAL_MEMSIZE + BARRIER);' % off + s[b:]
open(p, 'w').write(s)
print('patched', p)
