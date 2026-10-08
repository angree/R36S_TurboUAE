#!/bin/sh
# Cross-builds Amiberry v3.3 for the R36S (ArkOS, Ubuntu 19.10 userland, glibc 2.30) in WSL.
#   wsl sh /mnt/i/GITHUB/R36S_Amiberry/build/build_v33.sh [clean]
# Compiler: Ubuntu's gcc-9 AArch64 cross (g++-9-aarch64-linux-gnu). Headers and libraries:
# /opt/r36s-sysroot, unpacked from the same Ubuntu 19.10 arm64 -dev packages the console's
# libraries come from, so nothing newer than the console has can be linked in. The C++
# headers and libstdc++ are taken from the sysroot too (9.2.1), not from the cross package.
# (The console's own gcc can build this as well, but two cpuemu files at once run it out of
# its 1 GB of memory, and one at a time takes the better part of an hour.)
set -e
SR=/opt/r36s-sysroot; G=$SR/usr/lib/gcc/aarch64-linux-gnu/9
B=/opt/r36s-build; SRC=$B/amiberry-3.3
TGZ=/mnt/i/GITHUB/R36S_Amiberry/baseline/agent/jobs/done
ls /mnt/i/GITHUB >/dev/null 2>&1 || sudo -n mount -t drvfs I: /mnt/i
sudo mkdir -p $B && sudo chown $(id -u) $B && mkdir -p $B/bin
[ -f $SR/usr/include/unicode/ucnv.h ] || tar -xzf /mnt/c/temp/r36s_amiberry/sysroot/icu-headers.tgz -C $SR

# The -dev packages link libm.so and friends to ABSOLUTE paths (/lib/aarch64-linux-gnu/...).
# Inside a sysroot those dangle, the linker skips them and quietly takes the cross
# toolchain's glibc 2.35 instead - which no longer has the __*_finite functions that
# -Ofast calls. Point every absolute link back into the sysroot.
find $SR -type l | while read l; do
  t=$(readlink "$l")
  case "$t" in /*) [ -e "$SR$t" ] && ln -sfn "$SR$t" "$l";; esac
done

INC="-isystem $SR/usr/include/aarch64-linux-gnu -isystem $SR/usr/include -I$SR/usr/include/SDL2 -I$SR/usr/include/libxml2"
# -L only, no -B for the sysroot's gcc directory: -B would also make the 9.5 cross compiler
# read the 9.2.1 compiler-internal headers (arm_neon.h), whose builtins it does not have.
# --allow-shlib-undefined: the sysroot's libSDL2 wants X11 and Wayland, which are not in it
# (and the console's own SDL2 is a KMSDRM-only build anyway).
# -B$SR/usr/lib/aarch64-linux-gnu: crt1.o/crti.o/crtn.o MUST come from the sysroot (glibc 2.30).
# The cross toolchain's own crt1.o is glibc 2.35's, which hands __libc_start_main no init
# function (2.34+ libc runs .init_array itself); on the console's 2.30 that means no C++
# static constructor ever runs - the GUI and every static std::list/deque crashed.
LNK="-B$SR/usr/lib/aarch64-linux-gnu/ -L$G -L$SR/usr/lib/aarch64-linux-gnu -Wl,-rpath-link,$SR/usr/lib/aarch64-linux-gnu:$SR/lib/aarch64-linux-gnu -Wl,--allow-shlib-undefined"
# non-PIE: the JIT wants code and data below 4 GB, as in the stock ArkOS binary
printf '#!/bin/sh\nexec aarch64-linux-gnu-gcc-9 --sysroot=%s -fno-pie -no-pie %s %s "$@"\n' "$SR" "$INC" "$LNK" > $B/bin/cc
printf '#!/bin/sh\nexec aarch64-linux-gnu-g++-9 --sysroot=%s -fno-pie -no-pie -nostdinc++ -isystem %s/usr/include/c++/9 -isystem %s/usr/include/aarch64-linux-gnu/c++/9 %s %s "$@"\n' "$SR" "$SR" "$SR" "$INC" "$LNK" > $B/bin/c++
printf '#!/bin/sh\ncase "$1" in --cflags) echo "-D_REENTRANT";; --libs) echo "-lSDL2";; --version) echo 2.0.10;; esac\n' > $B/bin/sdl2-config
printf '#!/bin/sh
case "$1" in --cflags) echo "-I%s/usr/include/libxml2";; --libs) echo "-lxml2";; esac
' "$SR" > $B/bin/xml2-config
chmod +x $B/bin/*
export PATH=$B/bin:$PATH

if [ "$1" = clean ] || [ ! -d $SRC ]; then
  rm -rf $SRC && mkdir -p $SRC
  tar -xzf "$(ls $TGZ/04-build-v33_*/amiberry-v3.3.tar.gz | head -1)" -C $SRC --strip-components=1
  python3 /mnt/i/GITHUB/R36S_Amiberry/build/patches/natmem.py $SRC/src/osdep/amiberry_mem.cpp
fi
# always refresh the hook (it is our own code and changes often)
if true; then
  python3 /mnt/i/GITHUB/R36S_Amiberry/build/patches/r36s_hook.py $SRC
  python3 /mnt/i/GITHUB/R36S_Amiberry/build/patches/exception_log.py $SRC
  python3 /mnt/i/GITHUB/R36S_Amiberry/build/patches/sigsegv_quiet.py $SRC
  python3 /mnt/i/GITHUB/R36S_Amiberry/build/patches/cpu_idle_cfg.py $SRC
fi
cd $SRC
t0=$(date +%s)
make -j$(nproc) PLATFORM=go-advance CC=$B/bin/cc CXX=$B/bin/c++ AS=aarch64-linux-gnu-as STRIP=aarch64-linux-gnu-strip SDL_CONFIG=$B/bin/sdl2-config > $B/build.log 2>&1 || { grep -nE ' error |fatal error|undefined reference|cannot find' $B/build.log | head -30; tail -5 $B/build.log; echo "BUILD FAILED after $(( $(date +%s) - t0 )) s"; exit 1; }
echo "built in $(( $(date +%s) - t0 )) s"
ls -la amiberry; file amiberry | cut -c1-150
echo "NEEDED: $(aarch64-linux-gnu-readelf -d amiberry | grep NEEDED | sed 's/.*\[\(.*\)\]/\1/' | tr '\n' ' ')"
echo "max GLIBC: $(aarch64-linux-gnu-readelf -V amiberry | grep -o 'GLIBC_[0-9.]*' | sort -uV | tail -1)  max GLIBCXX: $(aarch64-linux-gnu-readelf -V amiberry | grep -o 'GLIBCXX_[0-9.]*' | sort -uV | tail -1)"
