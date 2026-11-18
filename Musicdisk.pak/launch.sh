#!/bin/sh
# Musicdisk: MOD / S3M / XM / IT player for NextUI. Downloads are cached in NextUI's user data folder.
cd "$(dirname "$0")"
./musicdisk "$USERDATA_PATH/Musicdisk" "$PWD" > "$LOGS_PATH/Musicdisk.txt" 2>&1
