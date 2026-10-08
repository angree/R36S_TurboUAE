#!/bin/sh
# Builds the Amiga-side benchmark with bebbo's m68k-amigaos-gcc (run inside WSL).
# Plain 68020, no FPU, libnix. dhry_1/dhry_2 stay separate files, as Dhrystone requires.
set -e
ls /mnt/i/GITHUB >/dev/null 2>&1 || sudo -n mount -t drvfs I: /mnt/i
cd /mnt/i/GITHUB/R36S_Amiberry/baseline/bench
python3 make_dhry.py
CC=/opt/amiga/bin/m68k-amigaos-gcc
FL="-O2 -m68020 -msoft-float -noixemul -fno-builtin -DTIME -Idhry21 -w"
$CC $FL -c dhry_1_amiga.c -o dhry_1.o
$CC $FL -c dhry21/dhry_2.c -o dhry_2.o
$CC -O2 -m68020 -msoft-float -noixemul -Wall -c amiga_bench.c -o amiga_bench.o
$CC -m68020 -msoft-float -noixemul -o dhrybench amiga_bench.o dhry_1.o dhry_2.o
rm -f *.o
ls -la dhrybench
$CC -O2 -m68020 -msoft-float -noixemul -Wall -o sysclick sysclick.c
ls -la sysclick
$CC -O2 -m68020 -msoft-float -noixemul -Wall -o rtgmode rtgmode.c
ls -la rtgmode
