#!/bin/sh
# Builds the release archive TurboUAE_<version>.lha: the Ports version, the RetroArch core with
# its installer, README, LICENSE and the complete source (GPL v3 section 6).
#   wsl sh build/package_release.sh 0.2.0
# Needs: the Ports build (/opt/r36s-build/amiberry-3.3), the core build (out/turbouae_libretro.so,
# /opt/r36s-build/turbouae-lr), and rel020/old = the 0.1.0 archive unpacked (upstream source
# tarball, Amiga tools, placeholders, LICENSE). Kickstarts, Workbench and games never go in.
set -e
V=${1:-0.2.0}
H=$(cd "$(dirname "$0")/.." && pwd)                 # C:\temp\turbouae_lr
PS=/opt/r36s-build/amiberry-3.3; LS=/opt/r36s-build/turbouae-lr
OLD=$H/rel020/old/TurboUAE; PORT=$OLD/ports
R=/tmp/rel/TurboUAE
rm -rf /tmp/rel && mkdir -p $R/ports/turbouae $R/ports/turbouae-core $R/source
cp $OLD/LICENSE $H/release/README.txt $R/

# Ports version
cp $H/port/ports/TurboUAE.sh $R/ports/
T=$R/ports/turbouae
cp $PS/amiberry $T/turbouae && chmod 755 $T/turbouae
cp -r $PS/data $T/
mkdir -p $T/conf && cp $PORT/turbouae/conf/amiberry.conf $PORT/turbouae/conf/retroarch.cfg $PORT/turbouae/conf/Workbench.uae.template $T/conf/
cp -r $OLD/ports/turbouae/work $OLD/ports/turbouae/hdf $OLD/ports/turbouae/kickstarts $T/
cp -r $PS/whdboot $T/
find $T/whdboot/save-data -type f ! -iname '*.rtb' ! -name foo.txt -delete

# RetroArch core (its WHDLoad booter is the Ports version's, see the installer)
cp "$H/port/core/Install TurboUAE core.sh" $R/ports/
cp $H/out/turbouae_libretro.so $H/port/core/turbouae_libretro.info $R/ports/turbouae-core/
cp -r $LS/data $R/ports/turbouae-core/

# source
S=$R/source
cp $OLD/source/amiberry-v3.3-upstream.tar.gz $S/
cp -r $OLD/source/amiga-tools $S/
mkdir -p $S/build/patches $S/native $S/port/core
cp $H/build/build_ports.sh $H/build/build_lr.sh $H/build/build_v221.sh $H/build/package_core.sh $H/build/package_release.sh $S/build/
cp $H/build/patches/*.py $S/build/patches/
cp -r $H/native/font $H/native/r36s_font35.h $H/native/r36s_hook.cpp $H/native/r36s_vkbd.cpp $S/native/
mkdir -p $S/native/libretro
cp $H/native/libretro/*.h $H/native/libretro/*.cpp $S/native/libretro/
cp "$H/port/core/Install TurboUAE core.sh" $H/port/core/turbouae_libretro.info $S/port/core/

# nothing licensed may slip in
if find $R -iname '*.rom' -o -iname '*.hdf' -o -iname 'kick*.a[0-9]*' ! -iname '*.rtb' | grep -v -i '\.rtb$' | grep .; then
  echo "LICENSED FILE IN THE RELEASE - stopping"; exit 1
fi
cd /tmp/rel && rm -f $H/release/TurboUAE_$V.lha && lha -aq $H/release/TurboUAE_$V.lha TurboUAE
ls -la $H/release/TurboUAE_$V.lha
du -sh $R/ports/* $R/source
