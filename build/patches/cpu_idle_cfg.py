# Amiberry v3.3 src/cfgfile.cpp: the CPU Idle slider (prefs cpu_idle) is neither saved nor
# read back, so it is lost on every start. Saved and parsed as "cpu_idle" (as in WinUAE).
# Usage: cpu_idle_cfg.py <amiberry source dir>
import sys
p = sys.argv[1] + '/src/cfgfile.cpp'
s = open(p).read()
if '_T("cpu_idle")' in s:
    print('cpu_idle already in cfgfile'); sys.exit(0)
w = '\tcfgfile_write(f, _T("cachesize"), _T("%d"), p->cachesize);\n'
assert w in s
s = s.replace(w, w + '\tcfgfile_write(f, _T("cpu_idle"), _T("%d"), p->cpu_idle);\n', 1)
r = '\tif (cfgfile_intval(option, value, _T("cachesize"), &p->cachesize, 1)'
assert r in s
s = s.replace(r, '\tif (cfgfile_intval(option, value, _T("cpu_idle"), &p->cpu_idle, 1))\n\t\treturn 1;\n' + r, 1)
open(p, 'w').write(s)
print('cpu_idle saved/parsed')
