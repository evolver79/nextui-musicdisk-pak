#!/bin/sh
# Builds dist/Musicdisk.pak.zip with the pak's files at the top of the zip (the layout Pak Store expects).
# For a manual install, unzip it into a new folder: SD card Tools/tg5040/Musicdisk.pak/.
set -e
cd "$(dirname "$0")"
build/build.sh
cp build/out/musicdisk Musicdisk.pak/musicdisk
mkdir -p dist
rm -f dist/Musicdisk.pak.zip
(cd Musicdisk.pak && zip -q -X ../dist/Musicdisk.pak.zip musicdisk launch.sh GeistMono-Regular.ttf GeistMono-Medium.ttf GeistMono-Bold.ttf LICENSE-GeistMono.txt LICENSE-libxmp.txt)
unzip -l dist/Musicdisk.pak.zip
