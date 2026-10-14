#!/bin/bash
# Installs the TurboUAE libretro core on ArkOS (R36S and other RK3326 consoles). Run it once
# from the Ports menu. It
#   - copies turbouae_libretro.so/.info into RetroArch's cores folder
#   - copies the core's data (settings screen, keyboard font, WHDLoad booter) to bios/turbouae
#   - adds an emulator "turbouae" to the Amiga and CD32 systems of EmulationStation, next to
#     the ones that are there (nothing else changes; the default stays as it was), plus
#     /usr/local/bin/turbouae, which ArkOS's amiga.sh runs for it: the 64 bit RetroArch with
#     this core. Those systems also show .uae files. A backup of es_systems.cfg is kept as
#     es_systems.cfg.before-turbouae.
#   - if the Ports version of TurboUAE is installed, puts its Workbench machine into
#     roms/amiga as "Workbench (TurboUAE).uae"
# then restarts EmulationStation. Running it again updates the core; nothing of yours is
# overwritten.
here="$(cd "$(dirname "$0")" && pwd)"
SRC="$here/turbouae-core"
TTY=/dev/tty1
sudo chmod 666 $TTY 2>/dev/null
printf '\033c' > $TTY 2>/dev/null
say() { echo " $*" > $TTY 2>/dev/null; echo "$*"; }
ROMS=/roms; [ -d /roms2/bios ] && [ ! -d /roms/bios ] && ROMS=/roms2
RA=/home/ark/.config/retroarch
BASE=$ROMS/bios/turbouae
ES=/etc/emulationstation/es_systems.cfg

say "TurboUAE core - installing"
[ -f "$SRC/turbouae_libretro.so" ] || { say "turbouae-core folder not found next to this script"; sleep 6; exit 1; }
[ -d $RA/cores ] || { say "RetroArch cores folder not found ($RA/cores)"; sleep 6; exit 1; }
cp -f "$SRC/turbouae_libretro.so" "$SRC/turbouae_libretro.info" $RA/cores/ || { say "could not copy the core"; sleep 6; exit 1; }
chmod 755 $RA/cores/turbouae_libretro.so
rm -f $RA/cores/core_info.cache
say "core copied"

mkdir -p $BASE/conf $BASE/kickstarts
cp -rf "$SRC/data" $BASE/ && say "data copied to $BASE"
# the WHDLoad booter: the core's own copy, else the Ports version's next to it (release archive)
WB="$SRC/whdboot"; [ -d "$WB" ] || WB="$here/turbouae/whdboot"
cp -rn "$WB" $BASE/ 2>/dev/null
mkdir -p $BASE/whdboot/save-data/Kickstarts
cp -f "$WB"/save-data/Kickstarts/*.RTB $BASE/whdboot/save-data/Kickstarts/ 2>/dev/null
mkdir -p $BASE/whdboot/save-data/Kickstarts $BASE/whdboot/save-data/Savegames $BASE/whdboot/save-data/Autoboots $BASE/whdboot/save-data/Debugs

if [ -f $ES ]; then
  [ -f $ES.before-turbouae ] || sudo cp $ES $ES.before-turbouae
  sudo python3 - "$ES" <<'PY'
import re, sys
p = sys.argv[1]
s = open(p, encoding='utf-8', errors='surrogateescape').read()
out = []
changed = 0
for block in re.split(r'(<system>.*?</system>)', s, flags=re.S):
    m = re.search(r'<name>\s*(amiga|amigacd32)\s*</name>', block)
    if block.startswith('<system>') and m:
        b = block
        # an emulator of its own, "turbouae", next to the others (amiga.sh runs
        # /usr/local/bin/turbouae for it); an older install put the core into RetroArch's list
        b = re.sub(r'<emulator name="retroarch">.*?</emulator>',
                   lambda r: re.sub(r'[ \t]*<core>turbouae</core>[ \t]*\r?\n', '', r.group(0)), b, flags=re.S)
        if '<emulator name="turbouae">' not in b:
            em = re.search(r'([ \t]*)<emulators>[ \t]*(\r?\n)', b)
            if em:
                ind, nl = em.group(1), em.group(2)
                entry = (ind + '   <emulator name="turbouae">' + nl + ind + '      <cores>' + nl +
                         ind + '         <core>turbouae</core>' + nl + ind + '      </cores>' + nl +
                         ind + '   </emulator>' + nl)
                # right after "standalone" where there is one, else first
                st = re.search(r'<emulator name="standalone">.*?</emulator>[ \t]*\r?\n', b, flags=re.S)
                at = st.end() if st else em.end()
                b = b[:at] + entry + b[at:]
        e = re.search(r'<extension>(.*?)</extension>', b, flags=re.S)
        if e:
            exts = e.group(1).split()
            want = ['.uae', '.UAE', '.adz', '.ADZ', '.dms', '.DMS', '.hdz', '.HDZ'] if m.group(1) == 'amiga' else ['.uae', '.UAE']
            add = [x for x in want if x not in exts]
            if add:
                b = b[:e.start(1)] + ' '.join(exts + add) + b[e.end(1):]
        if b != block:
            changed += 1
        block = b
    out.append(block)
if changed:
    open(p, 'w', encoding='utf-8', errors='surrogateescape').write(''.join(out))
print('es_systems.cfg: %d system(s) updated' % changed)
PY
  say "EmulationStation: emulator turbouae added to Amiga / CD32"
else
  say "es_systems.cfg not found - choose the core in RetroArch by hand"
fi

# amiga.sh starts "/usr/local/bin/<emulator> -L <cores>/<core>_libretro.so <game>" for every
# emulator except "standalone"
sudo tee /usr/local/bin/turbouae > /dev/null <<'WRAP'
#!/bin/bash
# TurboUAE: EmulationStation's emulator "turbouae" (via amiga.sh) - the 64 bit RetroArch
# (through ArkOS's own retroarch script) with the TurboUAE core. The last argument is the game.
for game; do :; done
CORE=/home/ark/.config/retroarch/cores/turbouae_libretro.so
# EmulationStation leaves CPU, GPU and memory clock on "ondemand" unless a governor is chosen
# for the game, and the memory clock then stays low: 3D games crawl. Full speed for the Amiga;
# perfnorm (run by EmulationStation after the game) puts the governors back.
for g in /sys/devices/system/cpu/cpufreq/policy0/scaling_governor /sys/devices/platform/dmc/devfreq/dmc/governor \
         /sys/devices/platform/*.gpu/devfreq/*.gpu/governor; do
  [ -e "$g" ] && echo performance | sudo tee "$g" > /dev/null 2>&1
done
if [ -n "$game" ] && [ "$game" != "$CORE" ]; then
  exec /usr/local/bin/retroarch -L "$CORE" "$game"
fi
exec /usr/local/bin/retroarch -L "$CORE"
WRAP
sudo chmod 755 /usr/local/bin/turbouae && say "emulator start script: /usr/local/bin/turbouae"

W=/roms/ports/turbouae/conf/Workbench.uae
[ -f "$ROMS/ports/turbouae/conf/Workbench.uae" ] && W="$ROMS/ports/turbouae/conf/Workbench.uae"
if [ -f "$W" ] && [ -d $ROMS/amiga ] && [ ! -f "$ROMS/amiga/Workbench (TurboUAE).uae" ]; then
  cp "$W" "$ROMS/amiga/Workbench (TurboUAE).uae" && say "Workbench machine put into roms/amiga"
fi
sync
say ""
say "Done. Amiga and Amiga CD32 have one more emulator: 'turbouae'."
say "Choose it per game (Select on the game) or for the whole system;"
say "'Workbench (TurboUAE)' in the Amiga system already uses it."
say "L2 = TurboUAE settings, R2 = on-screen keyboard."
say "Restarting EmulationStation..."
sleep 6
printf '\033c' > $TTY 2>/dev/null

# The game list entry that makes the Workbench machine start with this core has to be written
# while EmulationStation is not running (it rewrites its game lists when it exits), and this
# script is a child of EmulationStation - so a helper outside of it does stop / write / start.
cat > /tmp/turbouae-es.sh <<HELPER
#!/bin/bash
[ -n "$TURBOUAE_NO_RESTART" ] || systemctl stop emulationstation
sleep 1
python3 - "$ROMS/amiga/gamelist.xml" <<'PY'
import os, sys
p = sys.argv[1]
path = './Workbench (TurboUAE).uae'
entry = ('\t<game>\n\t\t<path>%s</path>\n\t\t<name>Workbench (TurboUAE)</name>\n'
         '\t\t<emulator>turbouae</emulator>\n\t\t<core>turbouae</core>\n\t</game>\n' % path)
if not os.path.exists(os.path.join(os.path.dirname(p), path[2:])):
    sys.exit(0)
s = open(p, encoding='utf-8', errors='surrogateescape').read() if os.path.exists(p) else ''
if path in s:
    # an older install pointed the entry at RetroArch's emulator
    i = s.index(path)
    j = s.find('</game>', i)
    if j > 0:
        s = s[:i] + s[i:j].replace('<emulator>retroarch</emulator>', '<emulator>turbouae</emulator>') + s[j:]
        open(p, 'w', encoding='utf-8', errors='surrogateescape').write(s)
    sys.exit(0)
if '</gameList>' in s:
    s = s.replace('</gameList>', entry + '</gameList>', 1)
else:
    s = '<?xml version="1.0"?>\n<gameList>\n' + entry + '</gameList>\n'
open(p, 'w', encoding='utf-8', errors='surrogateescape').write(s)
PY
chown ark:ark "$ROMS/amiga/gamelist.xml" 2>/dev/null
[ -n "$TURBOUAE_NO_RESTART" ] || systemctl start emulationstation
HELPER
chmod +x /tmp/turbouae-es.sh
if [ -n "$TURBOUAE_NO_RESTART" ]; then sudo bash /tmp/turbouae-es.sh
else sudo systemd-run --no-block --unit=turbouae-es-$$ /tmp/turbouae-es.sh || sudo systemctl restart emulationstation
fi
exit 0
