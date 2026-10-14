#!/bin/sh
# Cross-builds the TurboUAE libretro core (turbouae_libretro.so) for the R36S / ArkOS
# (aarch64, glibc 2.30) in WSL. Same compiler and sysroot as build_ports.sh, same patched
# Amiberry v3.3 source, plus build/patches/lr_patch.py and native/libretro/*.
#   wsl sh <repo>/build/build_lr.sh [clean]
# The source tree is separate from the Ports build (/opt/r36s-build/turbouae-lr): this one is
# compiled -fPIC with the SDL shim force-included.
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
SR=/opt/r36s-sysroot; G=$SR/usr/lib/gcc/aarch64-linux-gnu/9
B=/opt/r36s-build; SRC=$B/turbouae-lr; BIN=$B/binlr
UP=${UPSTREAM_TGZ:-$HERE/../amiberry-v3.3-upstream.tar.gz}
OUT=${OUT:-$HERE/../out}
mkdir -p $BIN "$OUT"

INC="-isystem $SR/usr/include/aarch64-linux-gnu -isystem $SR/usr/include -I$SR/usr/include/SDL2 -I$SR/usr/include/libxml2"
# see build_ports.sh for why each of these is here; the only difference is no -no-pie
LNK="-B$SR/usr/lib/aarch64-linux-gnu/ -L$G -L$SR/usr/lib/aarch64-linux-gnu -Wl,-rpath-link,$SR/usr/lib/aarch64-linux-gnu:$SR/lib/aarch64-linux-gnu -Wl,--allow-shlib-undefined"
printf '#!/bin/sh\nexec aarch64-linux-gnu-gcc-9 --sysroot=%s %s %s "$@"\n' "$SR" "$INC" "$LNK" > $BIN/cc
printf '#!/bin/sh\nexec aarch64-linux-gnu-g++-9 --sysroot=%s -nostdinc++ -isystem %s/usr/include/c++/9 -isystem %s/usr/include/aarch64-linux-gnu/c++/9 %s %s "$@"\n' "$SR" "$SR" "$SR" "$INC" "$LNK" > $BIN/c++
printf '#!/bin/sh\ncase "$1" in --cflags) echo "-D_REENTRANT";; --libs) echo "-lSDL2";; --version) echo 2.0.10;; esac\n' > $BIN/sdl2-config
printf '#!/bin/sh\ncase "$1" in --cflags) echo "-I%s/usr/include/libxml2";; --libs) echo "-lxml2";; esac\n' "$SR" > $BIN/xml2-config
chmod +x $BIN/*
export PATH=$BIN:$PATH

if [ "$1" = clean ] || [ ! -d $SRC ]; then
  rm -rf $SRC && mkdir -p $SRC
  tar -xzf "$UP" -C $SRC --strip-components=1
  python3 "$HERE/patches/natmem.py" $SRC/src/osdep/amiberry_mem.cpp
fi
python3 "$HERE/patches/r36s_hook.py" $SRC
python3 "$HERE/patches/exception_log.py" $SRC
python3 "$HERE/patches/sigsegv_quiet.py" $SRC
python3 "$HERE/patches/cpu_idle_cfg.py" $SRC
python3 "$HERE/patches/lr_patch.py" $SRC

cd $SRC
t0=$(date +%s)
make -j$(nproc) PLATFORM=go-advance PROG=turbouae_libretro.so CC=$BIN/cc CXX=$BIN/c++ AS=aarch64-linux-gnu-as \
  STRIP=aarch64-linux-gnu-strip SDL_CONFIG=$BIN/sdl2-config \
  EXTRA_CFLAGS="-fPIC -DTURBO_LIBRETRO" LR_CXXFLAGS="-include $SRC/src/osdep/lr_sdlshim.h" \
  EXTRA_LDFLAGS="-shared -Wl,-Bsymbolic -Wl,-z,defs" > $B/build-lr.log 2>&1 || {
    grep -nE ' error |fatal error|undefined reference|cannot find|can not be used' $B/build-lr.log | head -40
    tail -5 $B/build-lr.log; echo "BUILD FAILED after $(( $(date +%s) - t0 )) s"; exit 1; }
echo "built in $(( $(date +%s) - t0 )) s"
cp turbouae_libretro.so "$OUT/"
ls -la "$OUT/turbouae_libretro.so"; file turbouae_libretro.so | cut -c1-120
R=aarch64-linux-gnu-readelf
echo "NEEDED: $($R -d turbouae_libretro.so | grep NEEDED | sed 's/.*\[\(.*\)\]/\1/' | tr '\n' ' ')"
echo "TEXTREL: $($R -d turbouae_libretro.so | grep -c TEXTREL)   retro_ symbols: $($R --dyn-syms -W turbouae_libretro.so | grep -c ' retro_')"
echo "max GLIBC: $($R -V turbouae_libretro.so | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1)  max GLIBCXX: $($R -V turbouae_libretro.so | grep -o 'GLIBCXX_[0-9.]*' | sort -uV | tail -1)"
