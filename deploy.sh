#!/bin/sh
# Builds Musicdisk and copies it to the Brick with the brick tool (~/Code/brick-tool; IP and password in ~/.config/brick/config).
set -e
cd "$(dirname "$0")"
brick ping >/dev/null
build/build.sh
cp build/out/musicdisk Musicdisk.pak/musicdisk
rm -rf Musicdisk.pak/fonts && cp -r ui/dot/fonts Musicdisk.pak/fonts
brick ssh 'mkdir -p /mnt/SDCARD/Music'
brick deploy Musicdisk.pak
echo "Done. Open Tools > Musicdisk on the Brick."
