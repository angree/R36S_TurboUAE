#!/bin/bash
# TurboUAE for R36S / ArkOS (Ports) - port by Grzegorz Korycki. Based on Amiberry v3.3
# (BlitterStudio, GPL v3) with the AArch64 JIT, fixed and extended for this console
# (I:\GITHUB\R36S_Amiberry). Opens the settings GUI with the Workbench machine loaded;
# press Start in the GUI (or pick another config) to run it.
#
# Controls (read straight from the built-in pad):
#   left stick = Amiga mouse, A = left button, B = right button
#   Select+X   = settings GUI      Select+Y = on-screen keyboard      Select+Start = quit
#   X          = double click
#   GUI: d-pad moves between controls, A activates; the stick moves a pointer, B clicks.
# Logs: ports/turbouae/launcher.log and ports/turbouae/amiberry.log
progdir="$(cd "$(dirname "$0")" && pwd)"
GAMEDIR="$progdir/turbouae"
LOG="$GAMEDIR/launcher.log"
log() { echo "[$(date '+%H:%M:%S')] $*" >> "$LOG"; sync; }
: > "$LOG"
cd "$GAMEDIR" || exit 1
chmod +x ./turbouae
log "start $(uname -m) user=$(id -un) free=$(grep MemAvailable /proc/meminfo | tr -s ' ')"
log "missing libs: $(ldd ./turbouae 2>&1 | grep -c 'not found')"
# first start: the machine config gets this card's absolute paths (the GUI can then change
# and save it freely - it is never overwritten afterwards)
[ -f conf/Workbench.uae ] || sed "s|@DIR@|$GAMEDIR|g" conf/Workbench.uae.template > conf/Workbench.uae
GUI="yes"; [ -n "$1" ] && GUI="no"
stdbuf -oL -eL ./turbouae --config conf/Workbench.uae -s use_gui=$GUI >> "$LOG" 2>&1
rc=$?
log "turbouae ended rc=$rc"
sync
exit 0
