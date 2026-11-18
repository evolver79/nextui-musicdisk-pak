#!/bin/sh
# Builds Musicdisk and copies it to the Brick over SSH (turn SSH on in the Brick first: Tools > SSH Server).
# Usage: ./deploy.sh [ip] [password]     defaults: 192.168.68.196, tina
set -e
cd "$(dirname "$0")"
IP="${1:-192.168.68.196}"; PASS="${2:-tina}"
DIR="/mnt/SDCARD/Tools/tg5040/Musicdisk.pak"
# ssh reads the password from an askpass helper, so no prompt or expect is needed.
ASK="$(mktemp)"; printf '#!/bin/sh\necho "%s"\n' "$PASS" > "$ASK"; chmod +x "$ASK"
trap 'rm -f "$ASK"' EXIT
export SSH_ASKPASS="$ASK" SSH_ASKPASS_REQUIRE=force DISPLAY="${DISPLAY:-x}"
OPTS="-o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR -o ConnectTimeout=10"
build/build.sh
cp build/out/musicdisk Musicdisk.pak/musicdisk
ssh $OPTS root@$IP "mkdir -p '$DIR' /mnt/SDCARD/Music; rm -f '$DIR/musicdisk'"
# the Brick has no scp: the files go over as a tar stream
COPYFILE_DISABLE=1 tar -C Musicdisk.pak -cf - musicdisk launch.sh GeistMono-Regular.ttf GeistMono-Medium.ttf GeistMono-Bold.ttf | ssh $OPTS root@$IP "tar -xf - -C '$DIR'"
ssh $OPTS root@$IP "chmod +x '$DIR/musicdisk' '$DIR/launch.sh'; ls -la '$DIR'"
echo "Done. Open Tools > Musicdisk on the Brick."
