# TurboUAE (Ports version): WHDLoad games get the JIT and a machine it can boot.
# Calls r36s_whdload_machine() (native/r36s_hook.cpp) at the end of whdload_auto_prefs().
# Idempotent.   python3 whd_jit.py <amiberry source dir>
import sys
d = sys.argv[1]
p = d + '/src/osdep/amiberry_whdbooter.cpp'
s = open(p).read()
if 'r36s_whdload_machine' not in s:
    old = '''		_stprintf(txt2, "collision_level=%s", game_detail.sprites);
		cfgfile_parse_line(prefs, txt2, 0);
	}
}'''
    assert s.count(old) == 1, 'end of whdload_auto_prefs not found'
    s = s.replace(old, '''		_stprintf(txt2, "collision_level=%s", game_detail.sprites);
		cfgfile_parse_line(prefs, txt2, 0);
	}
	r36s_whdload_machine(prefs);
}''', 1)
    s = s.replace('void whdload_auto_prefs(struct uae_prefs* prefs, char* filepath)',
                  'void r36s_whdload_machine(struct uae_prefs* p);\n\nvoid whdload_auto_prefs(struct uae_prefs* prefs, char* filepath)', 1)
    open(p, 'w').write(s)
print('whd_jit: ok')
