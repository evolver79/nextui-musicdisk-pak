#!/bin/sh
# Musicdisk: MOD / S3M / XM / IT player for NextUI. Songs come from the SD card's Music folder.
cd "$(dirname "$0")"
./musicdisk "$USERDATA_PATH/Musicdisk" "$PWD" > "$LOGS_PATH/Musicdisk.txt" 2>&1
