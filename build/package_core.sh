#!/bin/sh
# Packs the libretro core as it goes onto the SD card (EASYROMS partition):
#   ports/Install TurboUAE core.sh
#   ports/turbouae-core/{turbouae_libretro.so, turbouae_libretro.info, data/, whdboot/}
# into <out>/core-pkg and <out>/turbouae-core.tgz.   wsl sh build/package_core.sh
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=/opt/r36s-build/turbouae-lr
OUT=${OUT:-$HERE/../out}
P=$OUT/core-pkg
rm -rf "$P" && mkdir -p "$P/ports/turbouae-core"
cp "$HERE/../port/core/Install TurboUAE core.sh" "$P/ports/"
cp "$OUT/turbouae_libretro.so" "$HERE/../port/core/turbouae_libretro.info" "$P/ports/turbouae-core/"
cp -r $SRC/data $SRC/whdboot "$P/ports/turbouae-core/"
# save-data/Kickstarts keeps Amiberry's WHDLoad relocation tables (*.RTB, WHDLoad needs them);
# Kickstart images copied there by an earlier run of the build never go into the package
find "$P/ports/turbouae-core/whdboot/save-data/Kickstarts" -type f ! -iname '*.rtb' -delete 2>/dev/null || true
tar -czf "$OUT/turbouae-core.tgz" -C "$P" ports
ls -la "$OUT/turbouae-core.tgz"; du -sh "$P/ports/turbouae-core"/*
