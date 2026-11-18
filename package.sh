#!/bin/sh
# Builds dist/Tracker.pak.zip with the pak's files at the top of the zip (the layout Pak Store expects).
# For a manual install, unzip it into a new folder: SD card Tools/tg5040/Tracker.pak/.
set -e
cd "$(dirname "$0")"
build/build.sh
cp build/out/tracker Tracker.pak/tracker
mkdir -p dist
rm -f dist/Tracker.pak.zip
(cd Tracker.pak && zip -q -X ../dist/Tracker.pak.zip tracker launch.sh GeistMono-Regular.ttf GeistMono-Medium.ttf GeistMono-Bold.ttf LICENSE-GeistMono.txt LICENSE-libxmp.txt)
unzip -l dist/Tracker.pak.zip
