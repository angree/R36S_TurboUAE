#!/bin/sh
# Packs the Ports folder: port/Amiberry.sh + port/amiberry/{conf,work} + the v3.3 binary and
# its data/ from the last build_v33.sh run. Output: C:\temp\r36s_amiberry\port-amiberry.tgz
set -e
ls /mnt/i/GITHUB >/dev/null 2>&1 || sudo -n mount -t drvfs I: /mnt/i
P=/mnt/i/GITHUB/R36S_Amiberry/port; S=/opt/r36s-build/amiberry-3.3
rm -rf /tmp/pk && mkdir -p /tmp/pk/turbouae
cp $S/amiberry /tmp/pk/turbouae/turbouae && cp -r $S/data /tmp/pk/turbouae/
cp -r $P/turbouae/conf $P/turbouae/work /tmp/pk/turbouae/
cp $P/TurboUAE.sh /tmp/pk/
cd /tmp/pk && tar -czf /mnt/c/temp/r36s_amiberry/port-amiberry.tgz .
ls -la /mnt/c/temp/r36s_amiberry/port-amiberry.tgz
