# Musicdisk – module player for NextUI

Plays MOD, S3M, XM, IT and about 90 other musicdisk formats on the TrimUI Brick (`tg5040`), from the SD card or
straight from [scene.org](https://www.scene.org)'s file mirror. Playback is [libxmp](https://github.com/libxmp/libxmp).

- **Now playing:** title, time, and a VU line per channel.
- **Demo:** nine line-art demo effects that move with the song (starfield, donut, tunnel, twister, copper bars, dot
  ball, plasma contours, bobs, sine scroller), over a ticker with the
  title and the sample names. A new effect every four song positions; ◄ ► to switch.
- **Pattern:** the song's pattern as it plays, four channels at a time, named by their instruments.
- **Scopes:** real per-channel scopes, drawn from each channel's sample data at its play position.
- **Browse:** the SD card's `Music` folder, scene.org's `/pub/music/` and `/pub/parties/`, and **Downloaded**: every
  song fetched from scene.org, newest first, with the folder it came from. Zipped modules play directly. Downloads
  stay in the cache (`.userdata/tg5040/Musicdisk/cache`, by their path on scene.org) and play offline; X twice
  deletes one. Without internet, Musicdisk opens on Downloaded.

## Buttons

| | Player | Browse |
|---|---|---|
| A | play / pause | open folder, play song |
| B | to browse | up a folder |
| L1 / R1 | previous / next song | page up / down |
| ◄ ► | effect (demo), channel page (pattern), channel (scopes) | switch source |
| X / Y | mute / solo the selected channel | X twice: delete a download (Downloaded) |
| SELECT | next screen: now playing, demo, pattern, scopes | |
| START | to browse | back to the player |
| MENU | quit | quit |

## Install

Unzip `Musicdisk.pak.zip` into a new folder on the SD card, `Tools/tg5040/Musicdisk.pak/`. Put your own modules in
`Music/` on the card.

## Build

`./package.sh` builds `dist/Musicdisk.pak.zip` in an ARM64 Debian bullseye container (Docker). `build/build.sh --shots
<module>` also draws every screen to `build/out/shots/`. `./deploy.sh [ip]` copies a build to a Brick with SSH on.

Font: Geist Mono (SIL OFL). libxmp: MIT.
