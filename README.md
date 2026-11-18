# Tracker – module player for NextUI

Plays MOD, S3M, XM, IT and about 90 other tracker formats on the TrimUI Brick (`tg5040`), from the SD card or
straight from [scene.org](https://www.scene.org)'s file mirror. Playback is [libxmp](https://github.com/libxmp/libxmp).

- **Now playing:** title, time, and a VU line per channel.
- **Pattern:** the song's pattern as it plays, four channels at a time, named by their instruments.
- **Scopes:** real per-channel scopes, drawn from each channel's sample data at its play position.
- **Browse:** the SD card's `Music` folder, scene.org's `/pub/music/` and `/pub/parties/`. Zipped modules play
  directly, and downloads are cached.

## Buttons

| | Player | Browse |
|---|---|---|
| A | play / pause | open folder, play song |
| B | to browse | up a folder |
| L1 / R1 | previous / next song | page up / down |
| ◄ ► | channel page (pattern), channel (scopes) | switch source |
| X / Y | mute / solo the selected channel | |
| SELECT | next screen | |
| START | to browse | back to the player |
| MENU | quit | quit |

## Install

Unzip `Tracker.pak.zip` into a new folder on the SD card, `Tools/tg5040/Tracker.pak/`. Put your own modules in
`Music/` on the card.

## Build

`./package.sh` builds `dist/Tracker.pak.zip` in an ARM64 Debian bullseye container (Docker). `build/build.sh --shots
<module>` also draws every screen to `build/out/shots/`. `./deploy.sh [ip]` copies a build to a Brick with SSH on.

Font: Geist Mono (SIL OFL). libxmp: MIT.
