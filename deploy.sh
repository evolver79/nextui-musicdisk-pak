#!/bin/sh
# Builds Tracker and copies it to the Brick over SSH (turn SSH on in the Brick's settings first).
# Usage: ./deploy.sh [ip] [password]     defaults: 192.168.68.167, tina
set -e
cd "$(dirname "$0")"
IP="${1:-192.168.68.167}"; PASS="${2:-tina}"
DIR="/mnt/SDCARD/Tools/tg5040/Tracker.pak"
OPTS="-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null"
run() { expect -c "set timeout 120; spawn $*; expect { \"password:\" { send \"$PASS\r\"; exp_continue } eof }"; }
build/build.sh
cp build/out/tracker Tracker.pak/tracker
run ssh $OPTS root@$IP "mkdir -p $DIR /mnt/SDCARD/Music"
run scp $OPTS Tracker.pak/tracker Tracker.pak/launch.sh Tracker.pak/DMMono-Light.ttf Tracker.pak/DMMono-Regular.ttf Tracker.pak/DMMono-Medium.ttf root@$IP:$DIR/
echo "Done. Open Tools > Tracker on the Brick."
