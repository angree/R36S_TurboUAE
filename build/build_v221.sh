#!/bin/sh
# Cross-builds Amiberry v2.21 as a 32-bit ARM (armhf) binary for the R36S, in WSL.
#   wsl sh /mnt/i/GITHUB/R36S_Amiberry/build/build_v221.sh [clean]
# v2.21 (May 2018) is the uae4arm-derived core labelled WinUAE 3.6 with the ARM32 JIT; it has
# no AArch64 JIT, so on the 64-bit console it runs through the 32-bit userland ArkOS ships.
# PLATFORM=rpi3-sdl2: plain SDL2 (no dispmanx), ARMv8 in AArch32 state, NEON.
# Sysroot: /opt/r36s-sysroot32 = Ubuntu 19.10 armhf -dev packages, same versions as the
# console's /usr/lib/arm-linux-gnueabihf.
set -e
SR=/opt/r36s-sysroot32; G=$SR/usr/lib/gcc/arm-linux-gnueabihf/9
B=/opt/r36s-build32; SRC=$B/amiberry-2.21
ls /mnt/i/GITHUB >/dev/null 2>&1 || sudo -n mount -t drvfs I: /mnt/i
sudo mkdir -p $B && sudo chown $(id -u) $B && mkdir -p $B/bin
[ -f $SR/usr/include/unicode/ucnv.h ] || true
INC="-isystem $SR/usr/include/arm-linux-gnueabihf -isystem $SR/usr/include -I$SR/usr/include/SDL2 -I$SR/usr/include/libxml2"
# -B$SR/usr/lib/arm-linux-gnueabihf: crt1.o/crti.o/crtn.o MUST come from the sysroot (glibc 2.30).
# The cross toolchain's own crt1.o is glibc 2.35's, which hands __libc_start_main no init
# function (2.34+ libc runs .init_array itself); on the console's 2.30 that means no C++
# static constructor ever runs - the GUI and every static std::list/deque crashed.
LNK="-B$SR/usr/lib/arm-linux-gnueabihf/ -L$G -L$SR/usr/lib/arm-linux-gnueabihf -L$SR/lib/arm-linux-gnueabihf -Wl,-rpath-link,$SR/usr/lib/arm-linux-gnueabihf:$SR/lib/arm-linux-gnueabihf -Wl,--allow-shlib-undefined"
printf '#!/bin/sh\nexec arm-linux-gnueabihf-gcc-9 --sysroot=%s -fno-pie -no-pie %s %s "$@"\n' "$SR" "$INC" "$LNK" > $B/bin/cc
printf '#!/bin/sh\nexec arm-linux-gnueabihf-g++-9 --sysroot=%s -fno-pie -no-pie -nostdinc++ -isystem %s/usr/include/c++/9 -isystem %s/usr/include/arm-linux-gnueabihf/c++/9 %s %s "$@"\n' "$SR" "$SR" "$SR" "$INC" "$LNK" > $B/bin/c++
printf '#!/bin/sh\ncase "$1" in --cflags) echo "-D_REENTRANT";; --libs) echo "-lSDL2";; --version) echo 2.0.10;; esac\n' > $B/bin/sdl2-config
printf '#!/bin/sh\ncase "$1" in --cflags) echo "-I%s/usr/include/libxml2";; --libs) echo "-lxml2";; esac\n' "$SR" > $B/bin/xml2-config
chmod +x $B/bin/*
export PATH=$B/bin:$PATH
if [ "$1" = clean ] || [ ! -d $SRC ]; then
  rm -rf $SRC && mkdir -p $SRC
  curl -sfL https://codeload.github.com/BlitterStudio/amiberry/tar.gz/refs/tags/v2.21 | tar -xz -C $SRC --strip-components=1
  python3 /mnt/i/GITHUB/R36S_Amiberry/build/patches/natmem.py $SRC/src/osdep/amiberry_mem.cpp
fi
cd $SRC
t0=$(date +%s)
make -j$(nproc) PLATFORM=rpi3-sdl2 CC=$B/bin/cc CXX=$B/bin/c++ AS=arm-linux-gnueabihf-as STRIP=arm-linux-gnueabihf-strip > $B/build.log 2>&1 || { grep -nE ' error |fatal error|undefined reference|cannot find' $B/build.log | head -30; tail -5 $B/build.log; echo "BUILD FAILED after $(( $(date +%s) - t0 )) s"; exit 1; }
echo "built in $(( $(date +%s) - t0 )) s"
P=$(ls -t amiberry* | grep -v '\.' | head -1); ls -la $P; file $P | cut -c1-150
echo "NEEDED: $(arm-linux-gnueabihf-readelf -d $P | grep NEEDED | sed 's/.*\[\(.*\)\]/\1/' | tr '\n' ' ')"
echo "max GLIBC: $(arm-linux-gnueabihf-readelf -V $P | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1)"
