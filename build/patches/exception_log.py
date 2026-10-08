# Diagnostics: log the first 40 illegal-instruction / Line-A / Line-F exceptions (68k vectors
# 4, 10, 11) with the PC and the code words there, to find what the JIT gets wrong.
# Usage: exception_log.py <amiberry source dir>
import sys
p = sys.argv[1] + '/src/newcpu.cpp'
s = open(p).read()
if 'r36s exc' not in s:
    old = 'void REGPARAM2 Exception (int nr)\n{\n\tExceptionX (nr);\n}'
    assert old in s
    new = ('void REGPARAM2 Exception (int nr)\n{\n'
           '\tstatic int r36s_exc_logged;\n'
           '\tif ((nr == 4 || nr == 10 || nr == 11) && r36s_exc_logged < 40) {\n'
           '\t\tconst uaecptr pc = m68k_getpc();\n'
           '\t\tr36s_exc_logged++;\n'
           '\t\twrite_log(_T("r36s exc %d pc=%08x code=%04x %04x %04x %04x jit=%d\\n"), nr, pc,\n'
           '\t\t\tget_word(pc) & 0xffff, get_word(pc + 2) & 0xffff, get_word(pc + 4) & 0xffff, get_word(pc + 6) & 0xffff, currprefs.cachesize);\n'
           '\t}\n'
           '\tExceptionX (nr);\n}')
    s = s.replace(old, new, 1)
    open(p, 'w').write(s)
print('exception log patched')
