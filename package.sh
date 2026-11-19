#!/bin/sh
# Builds dist/Musicdisk.pak.zip with the pak's files at the top of the zip (the layout Pak Store expects).
# For a manual install, unzip it into a new folder: SD card Tools/tg5040/Musicdisk.pak/.
set -e
cd "$(dirname "$0")"
build/build.sh
cp build/out/musicdisk Musicdisk.pak/musicdisk
rm -rf Musicdisk.pak/fonts && cp -r ui/dot/fonts Musicdisk.pak/fonts
mkdir -p dist
rm -f dist/Musicdisk.pak.zip
(cd Musicdisk.pak && zip -q -X -r ../dist/Musicdisk.pak.zip musicdisk launch.sh fonts LICENSE-libxmp.txt)
unzip -l dist/Musicdisk.pak.zip
