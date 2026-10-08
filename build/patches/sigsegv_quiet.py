# Amiberry v3.3 src/osdep/sigsegv_handler.cpp (AArch64 part): a JIT access fault that the
# handler resolves is normal operation once memory holes are protected (r36s_hook.cpp) - it is
# how compiled code learns that an address is not plain RAM. Upstream treats every one as an
# error: it dumps all registers and a backtrace to the log (a visible stutter each time) and
# after 200 of them, ever, stops the emulation with "Too many access violations. Please turn
# off JIT." - Xtreme Racing reached that after 32 laps.
# Here: details and the counter only for faults the handler could NOT resolve; resolved ones
# count towards a burst limit (2000) that r36s_frame() clears every 5 seconds, so only a
# runaway still stops the machine.
# Usage: sigsegv_quiet.py <amiberry source dir>
import sys
p = sys.argv[1] + '/src/osdep/sigsegv_handler.cpp'
s = open(p).read()
if 'r36s_fault_burst' in s:
    print('sigsegv already quiet'); sys.exit(0)
a = s.index('void signal_segv(int signum, siginfo_t* info, void* ptr)')
b = s.index('\tif (handled != HANDLE_EXCEPTION_NONE)\n\t\treturn;', a)
body = s[a:b]
assert body.count('if (handled != HANDLE_EXCEPTION_A4000RAM) {') == 3, body.count('if (handled != HANDLE_EXCEPTION_A4000RAM) {')
# the two detail blocks
body = body.replace('if (handled != HANDLE_EXCEPTION_A4000RAM) {', 'if (handled == HANDLE_EXCEPTION_NONE) {', 2)
old = '''	if (handled != HANDLE_EXCEPTION_A4000RAM) {
		--max_signals;
		if (max_signals <= 0) {'''
new = '''	if (handled == HANDLE_EXCEPTION_OK && ++r36s_fault_burst > 2000)
		max_signals = 0;
	if (handled == HANDLE_EXCEPTION_NONE || max_signals <= 0) {
		--max_signals;
		if (max_signals <= 0) {'''
assert old in body
body = body.replace(old, new, 1)
s = s[:a] + 'int r36s_fault_burst;\n' + body + s[b:]
open(p, 'w').write(s)
print('sigsegv quiet patched')
