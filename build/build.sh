#!/bin/sh
# Builds build/out/musicdisk for the TrimUI Brick (tg5040) in an ARM64 Debian bullseye container, against a static libxmp.
# --shots <module>: also renders each screen to build/out/shots/*.bmp inside the container (no device needed).
set -e
cd "$(dirname "$0")/.."
XMP=libxmp-4.7.4
mkdir -p deps; [ -f deps/$XMP.tar.gz ] || curl -sfL -o deps/$XMP.tar.gz https://github.com/libxmp/libxmp/releases/download/$XMP/$XMP.tar.gz
docker build -q -t nextui-musicdisk-build build >/dev/null
docker run --rm -v "$PWD:/w" -w /w nextui-musicdisk-build sh -c "
set -e
mkdir -p build/out
if [ ! -f build/out/xmp/lib/libxmp.a ]; then
  rm -rf build/out/$XMP && tar xzf deps/$XMP.tar.gz -C build/out
  cd build/out/$XMP && ./configure -q --prefix=/w/build/out/xmp --disable-shared --enable-static CFLAGS='-O2' >/dev/null && make -s -j4 >/dev/null && make -s install >/dev/null
  cd /w
fi
gcc -O2 -Wall -Wno-format-truncation -Wno-format-zero-length -Wno-restrict -o build/out/musicdisk src/main.c src/gfx.c src/player.c src/http.c src/browse.c src/demo.c src/zipx.c src/text.c ui/dot/src/dot.c \
  -Iui/dot/src -Ibuild/out/xmp/include \$(pkg-config --cflags sdl2) \
  build/out/xmp/lib/libxmp.a \$(pkg-config --libs sdl2) -lSDL2_ttf -lssl -lcrypto -lz -lpthread -lm
strip build/out/musicdisk
echo needs: \$(objdump -p build/out/musicdisk | grep NEEDED | tr -s ' ' | cut -d' ' -f3 | tr '\n' ' ')
if [ \"\$1\" = --shots ]; then
  mkdir -p build/out/shots /tmp/u/cache/pub/music/groups/abyss_music
  cp \"\$2\" /tmp/u/cache/pub_music_artists_bigjim_bj-aday.mod
  cp \"\$2\" /tmp/u/cache/pub_music_artists_alien_aln_dawn.mod
  cp \"\$2\" /tmp/u/cache/pub/music/groups/abyss_music/abyss%5Bfinal%5D.mod
  rm -rf Musicdisk.pak/fonts && cp -r ui/dot/fonts Musicdisk.pak/fonts
  SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy build/out/musicdisk /tmp/u Musicdisk.pak --shots build/out/shots \"\$2\" \"\$3\"
fi
" sh "$@"
