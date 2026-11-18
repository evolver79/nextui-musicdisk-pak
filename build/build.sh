#!/bin/sh
# Builds build/out/tracker for the TrimUI Brick (tg5040) in an ARM64 Debian bullseye container, against a static libxmp.
# --shots <module>: also renders each screen to build/out/shots/*.bmp inside the container (no device needed).
set -e
cd "$(dirname "$0")/.."
XMP=libxmp-4.7.4
docker build -q -t nextui-tracker-build build >/dev/null
docker run --rm -v "$PWD:/w" -w /w nextui-tracker-build sh -c "
set -e
mkdir -p build/out
if [ ! -f build/out/xmp/lib/libxmp.a ]; then
  rm -rf build/out/$XMP && tar xzf deps/$XMP.tar.gz -C build/out
  cd build/out/$XMP && ./configure -q --prefix=/w/build/out/xmp --disable-shared --enable-static CFLAGS='-O2' >/dev/null && make -s -j4 >/dev/null && make -s install >/dev/null
  cd /w
fi
gcc -O2 -Wall -Wno-format-truncation -Wno-format-zero-length -Wno-restrict -o build/out/tracker src/main.c src/gfx.c src/player.c src/http.c src/browse.c \
  -Ibuild/out/xmp/include \$(pkg-config --cflags sdl2) \
  build/out/xmp/lib/libxmp.a \$(pkg-config --libs sdl2) -lSDL2_ttf -lssl -lcrypto -lpthread -lm
strip build/out/tracker
echo needs: \$(objdump -p build/out/tracker | grep NEEDED | tr -s ' ' | cut -d' ' -f3 | tr '\n' ' ')
if [ \"\$1\" = --shots ]; then
  mkdir -p build/out/shots /tmp/u
  SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy build/out/tracker /tmp/u Tracker.pak --shots build/out/shots \"\$2\"
fi
" sh "$@"
