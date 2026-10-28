# SilentCartographer

**Halo: Combat Evolved, ported to Windows, Linux and Android.**

[![Join the SilentCartographer Discord](https://invidget.switchblade.xyz/jAaYaXtvyK)](https://discord.gg/jAaYaXtvyK)

SilentCartographer runs the original Xbox Halo: Combat Evolved on a PC,
a Steam Deck or an Android phone, from your own copy of the game. It is
built on a decompilation of the Xbox game (build 01.01.14.2342,
`cachebeta.exe`, SHA-256
`4cc87b45f721270392a96f1674ed2b5cd4a7bb4355faeab4531d1cf1884d9520`): the
game's own code, compiled for each platform, with a new layer that stands in
for the Xbox.

<img width="1289" height="995" alt="The game on Linux" src="https://github.com/user-attachments/assets/0d3ad50f-f8b8-46cf-aef8-e3661da2a7d7" />

SilentCartographer is a fork of [OpenCE](https://github.com/OpenCommunityEdition/OpenCE),
released under the GNU General Public License, version 3 or later. It is
built on OpenCE's code from before OpenCE moved to the AGPL on 19 October
2026 (refer to "Licence", below). It has its own network versions, so it
does not play network games with OpenCE.

## Downloads

Each build of the `main` branch that passes on every platform becomes a
release. These links always give the latest one:

| Platform | Release (to play) | Debug (to report problems) |
| --- | --- | --- |
| Windows | [halo-windows-release.zip](https://github.com/GuvenHuso/SilentCartographer/releases/latest/download/halo-windows-release.zip) | [halo-windows-debug.zip](https://github.com/GuvenHuso/SilentCartographer/releases/latest/download/halo-windows-debug.zip) |
| Linux | [halo-linux-release.zip](https://github.com/GuvenHuso/SilentCartographer/releases/latest/download/halo-linux-release.zip) | [halo-linux-debug.zip](https://github.com/GuvenHuso/SilentCartographer/releases/latest/download/halo-linux-debug.zip) |
| Android | [halo-android-release.zip](https://github.com/GuvenHuso/SilentCartographer/releases/latest/download/halo-android-release.zip) | [halo-android-debug.zip](https://github.com/GuvenHuso/SilentCartographer/releases/latest/download/halo-android-debug.zip) |

- **Release** builds are for playing.
- **Debug** builds stop at the first problem they find and write it to the
  log. Use one to report a problem.
- **Questions, problems, or people to play with:** join the
  [SilentCartographer Discord](https://discord.gg/jAaYaXtvyK).
- **The game updates itself.** At start-up it looks for a newer release and
  asks whether to install it. Refer to "Updates" in
  [port/linux/README.md](port/linux/README.md#updates).
- **Older builds:** the [Releases](https://github.com/GuvenHuso/SilentCartographer/releases)
  page keeps the last five. If the latest build has a problem, get an
  earlier one there.

## A legal copy of the game is required

**SilentCartographer does not include Halo, and never will.** The downloads
hold only SilentCartographer's own program. Halo's maps, sounds, textures
and everything else of the game come from **your own copy** of Halo: Combat
Evolved for the original Xbox.

- **Use a disc you own.** Make an image (`.iso` or `.xiso`) of your own
  Halo: Combat Evolved disc. Every version of the game works, from any
  region.
- **Do not download the game, and do not share it.** This project does not
  help anyone get Halo without owning it. Please do not ask for, link to or
  post game files anywhere in this project.
- **Custom Edition maps** also need `bitmaps.map`, `sounds.map` and
  `loc.map` from your own installation of Halo Custom Edition (refer to
  [docs/custom_edition_caches.md](docs/custom_edition_caches.md)).

### Setting up the game

1. Start SilentCartographer.
2. At the first start it asks for your disc image. Select it.
3. It copies the `maps/` folder out of the image, then the game starts.
   Nothing is uploaded anywhere.

On Windows and Linux, `maps/` goes next to the program. On Android, copy
the disc image to the phone first; the app keeps `maps/` in its own data
folder (refer to [port/android/README.md](port/android/README.md)). The
maps of the European (PAL) version were made for a slower console:
SilentCartographer plays them as the North American (NTSC) maps play, so
players of both versions can play together.

## Features

**The game, on your devices**

- Windows, Linux (the Steam Deck too) and Android, from one code base.
- One frame for each refresh of your display, not the Xbox's 30, with the
  game's own timing kept for everything else.
- Any resolution and aspect ratio, and a field of view of your choice.
- A high-resolution HUD redrawn by hand, sharp text at any resolution, and
  the menus of Halo Custom Edition for PC.
- Keyboard and mouse, gamepads, and on Android touch controls with
  gyroscope aiming.
- Split screen: the Xbox's two-player co-op campaign on one computer, and
  up to 4 players on each computer in network games.

**Multiplayer**

- System link on your local network, with up to 128 players on up to 128
  machines, and Windows, Linux and Android playing together.
- Internet play with invite links and a server browser. No game server is
  needed: one player hosts.
- A new netcode: each machine moves its own player at once, and the host
  decides for the game (refer to [port/linux/NETCODE.md](port/linux/NETCODE.md)).
- Voice chat, text chat, and votes to kick a player.
- Map sharing: players who join without a Custom Edition map download it
  from the others.
- Streamer mode, which hides slurs, swearing and insults in what other
  players write.

**Online co-op campaign**

- The campaign with friends over the internet.
- Server Setup's Co-op Options: friendly fire between players, whether the
  players can hurt their AI allies, extra enemies that grow with the
  players, and player collisions.

**Custom content**

- Halo Custom Edition maps, played as Custom Edition plays them.
- Mods: folders of Halo PC tag files (texture, sound, text and model
  packs) that play over the maps on your own machine. The main menu's Mods
  turns them on and off and sets their order (refer to
  [docs/mods.md](docs/mods.md)).

Every setting, and the controls, are in
[port/linux/README.md](port/linux/README.md). They are almost the same on
every platform.

## Platform guides and building

| Platform | Guide |
| --- | --- |
| Windows (32-bit x86, OpenGL 4.5, SDL3) | [port/windows/README.md](port/windows/README.md) |
| Linux and the Steam Deck (32-bit x86, OpenGL 4.5, SDL3) | [port/linux/README.md](port/linux/README.md) |
| Android (arm64 app, OpenGL ES 3, SDL3) | [port/android/README.md](port/android/README.md) |

To build SilentCartographer from its source, refer to
[docs/building.md](docs/building.md). The Xbox SDK is not needed.

## Credits

SilentCartographer stands on the work of many people.

- **Halo: Combat Evolved** is by Bungie, published by Microsoft.
- **The decompilation** is the work of
  [punpckhdq/halo](https://github.com/punpckhdq/halo) and of
  [bnunu/halo-1](https://github.com/bnunu/halo-1), which this port started
  from.
- **The port** is OpenCE's, by everyone who contributed to it up to 18
  October 2026: MrBruh, Jonas Volman, xshxdex98, justinfilip, eltanschauung,
  Milenko, Paul Hontz, chestahh, Addison Bair, hkmodd, Jeff Clark, RockBooch,
  bnunu, jcompto95-tech, thelinkin3000, Fadi Derouiche, WhiteBulL, saulo
  benigno, Brendan Turner, Constantine Luke Matthew Bubar, meowsandstuff,
  Daniel, Fernando, Jonathon Machinski, Luke, Mootjuh, TwistedFlog, bfenty
  and chester.
- **SilentCartographer** is maintained by Guven
  ([GuvenHuso](https://github.com/GuvenHuso)).
- **Art:** the controller buttons by saulob; the
  high-resolution HUD by MrBruh, from reference photos by Aerocatia; icons
  from [Lucide](https://lucide.dev). Refer to [AUTHORS.md](AUTHORS.md).
- **Fonts:** [Overpass](https://overpassfont.org/) (Red Hat, Delve
  Withrington), and OpenCE, made from Newtown by Roger White (refer to
  [port/assets/fonts/README.md](port/assets/fonts/README.md)).
- **Libraries:** SDL 3, Expat, KCP, Mbed TLS, MiniUPnPc, Monocypher, musl
  (its maths functions), Opus, SMAA, stb, stb_vorbis, tomlc17 and zlib; and
  for streamer mode, Surge AI's list of profanity and dwyl's English words.
  Each is in `port/third_party`, with its licence.

## Licence

SilentCartographer is free software: you can give it to others and change
it under the terms of the GNU General Public License as published by the
Free Software Foundation, version 3 of the License or (at your option) any
later version (`GPL-3.0-or-later`). It comes with no warranty. The full text
is in [LICENSE](LICENSE).

- **What it is built on.** SilentCartographer starts from OpenCE's commit
  `81a768bc` (18 October 2026), the last before OpenCE moved to the AGPL
  3.0. OpenCE's code to that commit, and the decompilation it carries (of
  [bnunu/halo-1](https://github.com/bnunu/halo-1), and of
  [punpckhdq/halo](https://github.com/punpckhdq/halo) from before 17
  October 2026), was released under [CC0 1.0](LICENSES/CC0-1.0.txt). Nothing
  OpenCE or punpckhdq/halo released under the AGPL is in it.
- **The disc image readers** (`port/linux/src/xiso.c`, the Android app's
  `XisoExtractor.java`, the web page's `xiso-worker.js`) were written again
  for SilentCartographer from a description of the Xbox's disc file system,
  in place of code that followed extract-xiso, whose licence's advertising
  clause the GPL does not allow.
- **Contributions** are under `GPL-3.0-or-later`.
- **Third-party code, fonts and art** keep their own licences, which are
  next to them (`port/third_party`, `port/assets/fonts`,
  `port/assets/icons`, `port/assets/text_filter`). The release downloads
  carry their notices.
- **Halo is not covered.** The licence covers the work of SilentCartographer's
  and OpenCE's authors. It gives no rights in Halo, its maps or anything else
  that Microsoft owns.

## Disclaimer

SilentCartographer is an unofficial fan project. It is not made, endorsed
or supported by Microsoft, Xbox Game Studios, Halo Studios or Bungie, and it
is not affiliated with them. Halo and Halo: Combat Evolved are trademarks of
Microsoft Corporation; the game and its content are © Microsoft
Corporation.

SilentCartographer includes no game data. You need your own legal copy of
Halo: Combat Evolved for the original Xbox to play. The software is
provided as it is, without warranty of any kind (refer to [LICENSE](LICENSE)).
