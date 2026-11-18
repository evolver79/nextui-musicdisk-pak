#!/bin/sh
# Tracker: MOD / S3M / XM / IT player for NextUI. Downloads are cached in NextUI's user data folder.
cd "$(dirname "$0")"
./tracker "$USERDATA_PATH/Tracker" "$PWD" > "$LOGS_PATH/Tracker.txt" 2>&1
