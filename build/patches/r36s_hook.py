# Wires native/r36s_hook.cpp and native/r36s_vkbd.cpp into Amiberry v3.3: copies them into
# src/osdep, adds them to the Makefile and calls
#   r36s_frame()               once per frame - in emulation right before show_screen
#                              presents, in the GUI once per pass of its main loop
#   r36s_memory_map_changed()  at the end of map_banks() (JIT memory-hole protection)
# Usage: r36s_hook.py <amiberry source dir>
import os, shutil, sys
d = sys.argv[1]
for f in ('r36s_hook.cpp', 'r36s_vkbd.cpp', 'r36s_font35.h'):
    shutil.copy(os.path.join(os.path.dirname(__file__), '..', '..', 'native', f), d + '/src/osdep/' + f)

p = d + '/Makefile'; s = open(p).read()
if 'r36s_vkbd.o' not in s:
    a = '\tsrc/osdep/amiberry_gfx.o \\\n'
    assert a in s, 'Makefile anchor'
    s = s.replace(a, a + '\tsrc/osdep/r36s_hook.o \\\n\tsrc/osdep/r36s_vkbd.o \\\n', 1)
    open(p, 'w').write(s)

p = d + '/src/osdep/amiberry_gfx.cpp'; s = open(p).read()
if 'r36s_frame' not in s:
    a = s.index('void show_screen(int mode)')
    body = s[a:]
    n = body.count('\t\tSDL_RenderPresent(renderer);')
    assert n == 2, n
    body = body.replace('\t\tSDL_RenderPresent(renderer);', '\t\tr36s_frame(0);\n\t\tSDL_RenderPresent(renderer);')
    s = s[:a] + 'void r36s_frame(int in_gui);\n' + body
    open(p, 'w').write(s)

p = d + '/src/osdep/gui/main_window.cpp'; s = open(p).read()
if 'r36s_frame' not in s:
    a = s.index('\twhile (gui_running)')
    b = s.index('check_input();', a)
    s = s[:b] + 'r36s_frame(1);\n\t\t' + s[b:]
    s = s.replace('void update_gui_screen()', 'void r36s_frame(int in_gui);\nvoid update_gui_screen()', 1)
    open(p, 'w').write(s)

p = d + '/src/memory.cpp'; s = open(p).read()
if 'r36s_memory_map_changed' not in s:
    old = '\tmap_banks2(bank, start, size, realsize, 0);\n#ifdef WITH_PPC\n\tppc_generate_map_banks(bank, start, size);\n#endif\n}'
    assert old in s, 'map_banks'
    s = s.replace(old, old[:-1] + '\tr36s_memory_map_changed();\n}', 1)
    s = s.replace('void map_banks(addrbank* bank, int start, int size, int realsize)\n{',
                  'void r36s_memory_map_changed();\nvoid map_banks(addrbank* bank, int start, int size, int realsize)\n{', 1)
    open(p, 'w').write(s)
p = d + '/src/memory.cpp'; s = open(p).read()
if 'r36s_memory_unprotect_all' not in s:
    old = 'void memory_clear(void)\n{\n'
    assert old in s, 'memory_clear'
    s = s.replace(old, 'void r36s_memory_unprotect_all();\n' + old + '\tr36s_memory_unprotect_all();\n', 1)
    open(p, 'w').write(s)
print('r36s hook wired into', d)

p = d + '/src/osdep/amiberry_mem.cpp'; s = open(p).read()
if 'r36s_memory_unprotect_all' not in s:
    # mapped_malloc prepares a bank's memory (e.g. the filesys ROM at $EA0000) before
    # map_banks hooks it up, i.e. while that page is still a protected hole
    old = 'bool mapped_malloc(addrbank* ab)\n{\n'
    assert old in s, 'mapped_malloc'
    s = s.replace(old, 'void r36s_memory_unprotect_all();\n' + old + '\tr36s_memory_unprotect_all();\n', 1)
    open(p, 'w').write(s)
print('mapped_malloc hooked')

# GUI title overlay before every GUI present
p = d + '/src/osdep/gui/main_window.cpp'; s = open(p).read()
if 'r36s_gui_overlay' not in s:
    a = s.index('void update_gui_screen()')
    b = s.index('\tSDL_RenderPresent(renderer);', a)
    s = s[:b] + '\tr36s_gui_overlay();\n' + s[b:]
    s = s.replace('void r36s_frame(int in_gui);\n', 'void r36s_frame(int in_gui);\nvoid r36s_gui_overlay();\n', 1)
    open(p, 'w').write(s)

# TurboUAE: our name in the window title and the About panel, with credit to Amiberry (GPL v3)
p = d + '/src/osdep/target.h'; s = open(p).read()
old = '#define AMIBERRYVERSION _T("Amiberry v3.3 (2020-09-17)")'
if old in s:
    s = s.replace(old, '#define AMIBERRYVERSION _T("TurboUAE 0.1.0 - based on Amiberry v3.3 (2020-09-17)")', 1)
    open(p, 'w').write(s)
p = d + '/src/osdep/amiberry_gfx.cpp'; s = open(p).read()
s = s.replace('SDL_CreateWindow("Amiberry"', 'SDL_CreateWindow("TurboUAE"')
open(p, 'w').write(s)
print('TurboUAE naming applied')
