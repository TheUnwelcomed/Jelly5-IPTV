<p align="center">
  <img src="docs/media/icon.png" alt="Jelly5" width="180"><br>
  <b>Jelly5-IPTV — Jelly5 for jailbroken PS5 consoles, tuned for Live TV over long IPTV paths.</b><br>
  The same native Jellyfin and Emby client, with player fixes measured on a real IPTV stack.
</p>

<p align="center">
  <img alt="Platform: PS5" src="https://img.shields.io/badge/platform-PS5%20(jailbroken)-4b5bdc">
  <img alt="Jellyfin" src="https://img.shields.io/badge/Jellyfin-client-7b5cd6">
  <img alt="Emby" src="https://img.shields.io/badge/Emby-client-52b54b">
  <img alt="License: GPL-3.0-or-later" src="https://img.shields.io/badge/license-GPL--3.0--or--later-555">
  <img alt="Status: early" src="https://img.shields.io/badge/status-early%20release-c06">
  <img alt="Fork of Jelly5 0.7.0" src="https://img.shields.io/badge/fork%20of-Jelly5%200.7.0-7b5cd6">
</p>

<p align="center">
  <img src="docs/media/screenshot-home.webp" alt="Jelly5's home screen: a featured series' backdrop, logo and overview, with Play and More Info" width="100%">
</p>

<table>
  <tr>
    <td width="33%"><img src="docs/media/screenshot-movies.webp" alt="The Movies library"></td>
    <td width="33%"><img src="docs/media/screenshot-shows.webp" alt="The TV Shows library"></td>
    <td width="33%"><img src="docs/media/screenshot-search.webp" alt="Search with the on-screen keyboard and suggestions"></td>
  </tr>
  <tr>
    <td align="center"><sub>Movies</sub></td>
    <td align="center"><sub>TV Shows</sub></td>
    <td align="center"><sub>Search</sub></td>
  </tr>
</table>

**Jelly5-IPTV is a fork of [Jelly5](https://github.com/02dnot/Jelly5) 0.7.0 by 02dnot**, made by TheUnwelcomed. It is the same app — same interface, same features — with seven changes to its player for Live TV that reaches the PS5 over a long path: a Jellyfin server with an IPTV (Xtream/M3U) feed on a remote host, about 130 ms and a few Mbit/s of headroom away. On that path stock Jelly5 0.7.0 capped every connection at about 8 Mbit/s, froze the picture for 7–14 s after a dropped connection, left the picture 0.4 s behind the sound after a reconnect, and could restart a channel from the beginning during a provider's pause. Each change below was found by measuring, and checked the same way afterwards. Everything else is upstream Jelly5, unchanged.

Jelly5 brings your Jellyfin or Emby library to the PS5 as a real app: a fast, sharp interface made for the TV, and 4K HDR video played by the console's own hardware. With [Seerr](https://github.com/seerr-team/seerr), you can also request new films and series without leaving the couch.

**Contents:** [What's different](#whats-different-from-jelly5-070) · [How it was found](#how-it-was-found) · [Features](#features) · [Installation](#installation) · [Controls](#controls) · [Emby](#emby) · [Seerr](#seerr-optional) · [Formats and limits](#formats-and-limits) · [Troubleshooting](#troubleshooting) · [Privacy](#privacy) · [Building](#building) · [Credits](#credits)

## What's different from Jelly5 0.7.0

All in the player and its media engine; each change is marked in the code with its tag, and each commit message carries the measurement behind it. Times are from the tests on the author's setup, 2026-10-17 → 10-19.

| Tag | What it changes | What it fixed, as measured |
| --- | --- | --- |
| `JELLY5_RCVBUF_V1` | The player's stream connection asks for a 1 MB TCP receive buffer (set before connecting, so the window scale is negotiated for it). | The console's default ~128 KB window capped a connection at window ÷ round trip: 128 KB / 0.128 s ≈ 8 Mbit/s, under a 6.5–8.7 Mbit/s channel's peaks, so the player fell behind, froze and reconnected. The server was stalled by the PS5's window 8.6–10.3 s of every 30. After: the console grants 512 KB — window 511 KB, nothing queued, no time stalled, a ceiling of about 31 Mbit/s at that round trip. |
| `JELLY5_RCVBUF_V2` | The same option on every connection FFmpeg's demuxer opens by itself: the HLS playlist, every segment, every refresh. | FFmpeg copies only a few options to those connections, so a server transcode's segments kept the 128 KB window (126–128 KB, ≤ 7.8 Mbit/s) and a 20 Mbit/s transcode failed like direct play. After: 511–513 KB on every segment connection, nothing queued. |
| `JELLY5_AVCLOCK_V1` | The sound's clock follows a jump in the sound's timestamps (between 0.3 s and 120 s against the usual packet step). | After a dropped connection the stream picks up at the live edge; the sound clock only counted samples played, so the picture sat "early" and showed one frame every 1.5 s for 7–14 s with the sound running. |
| `JELLY5_AVLATE_V1/V2` | A picture that has been more than 0.15 s behind the sound for 0.35 s is skipped and the next decoded at once (at most 12 in a row). | After a reconnect the picture stayed 0.4 s behind the sound for the next 18 minutes (−0.4 to −0.9 s after rebuffers). V2's 0.35-s hold stops false skips on 60 fps channels, whose sound clock moves in bursts. |
| `JELLY5_PPCLOCK_V1` | The presentation timeline re-anchors on a frame more than 100 ms early, or more than 800 ms late after nothing has been shown for that long. | After a sound jump the picture ran at about 10 frames a second with every second frame missing (4,009 frames skipped in 4.5 minutes); after a rebuffer the old recovery first dropped 20 frames. |
| `JELLY5_HWWATCH_V1` | The hardware-decoder watchdog reopens on the software decoder only if a keyframe went in during its 6 silent seconds. | A reconnect lands mid-GOP; during a provider's pause the next keyframe was more than 6 s away, the watchdog declared the decoder dead, and the channel restarted from the beginning ("started and instantly restarted"). |
| `JELLY5_AVDIAG_V1` | Opt-in diagnostic build (`JELLY5_AVDIAG=1 scripts/build.sh`): the engine's log on the console at `/data/evoplayer/jelly5-diag.log`, every 5 s with the console's clock, both media clocks and their difference, and every starvation. | Not for a release build: the file grows while the app runs (~100 KB an hour). It is how the clock problems above were read from the player's side. |

[`IPTV-PATCHES.md`](IPTV-PATCHES.md) lists the same changes in short.

## How it was found

Jelly5-IPTV was built and tested by TheUnwelcomed against TheUnwelcomed's own live-TV stack: a Jellyfin 10.11.4 server with an Xtream IPTV feed, a stream proxy, ffmpeg wrappers and monitoring tools, all on a shared, rootless host (the private Unwelcomed-IPTV project), with the PS5 at home about 130 ms away, on Wi-Fi and on a cable. That stack's tools did the measuring:

- **From the server side**, its live-TV monitor read every connection to the PS5 from the kernel — receive window, round trip, retransmits, time the server was stalled by the receiver, what was queued — and the stream's own timestamps. That is how the 128 KB window was found, and how each fix was checked afterwards.
- **From the player's side**, the diagnostic build's log (`JELLY5_AVDIAG_V1`) gave the console's clocks, lined up second by second with the server's logs.

The same work fixed what the server sends, in that stack and not in this app: timestamps kept continuous when the provider's connection is replaced, a PS5 that joins a running channel getting its sound with its picture, and a provider's replayed buffer no longer sent twice. Jelly5-IPTV is the client half: it is useful with any Jellyfin or Emby server, and most where the path is long.

## Features

**Browsing**
- Home with a hero, *Continue watching*, *Next up*, *Recently added*, recommendations and genres
- Title pages with logo art, cast, seasons and episodes, trailers, extras and theme music
- Movie, TV and music libraries with sorting, filters and A–Z jumping, and search across everything
- Several users and servers, a screensaver from your own backdrops, and a liquid-glass look throughout
- In all 27 of the PS5's system languages, following the PS5's language (or a choice in Settings): English, Norwegian, German, French, Spanish, Italian, Portuguese, Dutch, Swedish, Danish, Finnish, Polish, Czech, Hungarian, Romanian, Greek, Russian, Ukrainian, Turkish, Arabic, Japanese, Korean, Chinese (Traditional and Simplified), Thai, Vietnamese and Indonesian

**Playback**
- Hardware-decoded H.264 and HEVC up to 4K with HDR10 and HLG; direct play where the PS5 can, server transcoding where it can't
- Audio and subtitle tracks, subtitle styling and online subtitle search, and subtitles synced automatically to the film's dialogue
- Trickplay previews, chapters, *Skip intro*, versions, and auto-play of the next episode
- Playback speed (0.75–2×), audio delay, and a night mode for late evenings
- Upscaling (optional): lower-resolution video sharpened to your TV's resolution, with FSR 1 or an AI network for animation
- HDMI bitstream (optional): Dolby Digital, Dolby Digital Plus (with Atmos) and DTS go to your TV or receiver untouched
- Resume and watched state synced with the server, and a maximum quality per server
- A dropped connection picks up where it broke, without stopping the film
- A read-ahead buffer of 30 seconds to 5 minutes, for networks that come and go

**Live TV**
- Your server's channels (IPTV/M3U or a tuner) in a programme guide: what's on now and next, filters for favourites, films, sport, news, kids and series, and a line where it is now
- *Live now* on the home screen, and the guide opens on the channel you watched last
- Change channel with L1/R1 while watching, a channel list with what's on each (△), and back to the previous channel
- Record a programme or every episode of a series, and watch your recordings (where the server records and your account may)

**Music**
- Albums, artists, playlists and Instant Mix, playing on while you browse
- *Now playing* with a queue, shuffle, repeat and time-synced lyrics

**Made for the PS5**
- DualSense adaptive triggers for scrubbing, and a light bar that takes the colour of what's playing
- A 120 Hz interface where the TV supports it, and TV-remote control over HDMI-CEC
- *Watch together* (Jellyfin SyncPlay) and remote control from other Jellyfin or Emby apps

**Requests with Seerr** (optional)
- Search, request and track films and series, a *Discover* tab, and trailers as a QR code for your phone

## Installation

### What you need

- A **jailbroken PS5** that can load payloads, with **ShadowMount+**:

  | Firmware | Jailbreak | Status |
  | --- | --- | --- |
  | 11.60 | Poops | Tested |
  | 13.60 | Relapse | Works (reported by a user) |

  Other firmware with the same tools should work too. If yours does (or doesn't), open an issue.
- A way to copy files to the console: **[ps5upload](https://github.com/phantomptr/ps5upload)** (recommended), or FTP (**ftpsrv** on port 2121 or **etaHEN** on 1337) with a client such as FileZilla.
- A **Jellyfin** server (tested with 12.1) or an **Emby** server (tested with 4.10) that the PS5 can reach.

### Install

1. Get the app's zip and unzip it. Inside is a folder called `PPSA99505`. **This fork has no release download yet:** build it (see [Building](#building)) to get these Live TV changes. Upstream's [releases](https://github.com/02dnot/Jelly5/releases) are the unpatched app.
2. Start the jailbreak with ShadowMount+ (and your FTP server, if you use FTP).
3. Upload the `PPSA99505` folder to `/data/homebrew/` on the console, so that `/data/homebrew/PPSA99505/eboot.bin` exists. Create `/data/homebrew` first if it isn't there.
4. After a moment, ShadowMount+ adds a **Jelly5** tile under *Media* on the home screen. If it doesn't, run your payloads again, or reboot and jailbreak again.
5. Open Jelly5, pick your server from the list or type its address (for example `192.168.1.20:8096`), and sign in. On Jellyfin, **Quick Connect** is quickest: scan the QR code with your phone and tap *Authorize*.

Jelly5 remembers your accounts. Add more users and servers from the profile picker.

The zip also holds `PPSA99505.ffpfsc`, the same app as a PFS image for loaders that mount images; the folder is the tested route.

### Update

1. **Close Jelly5 completely first** (PS button, then close it). Replacing files under a running app can crash the console.
2. Upload the new folder's files **over** the old ones in `/data/homebrew/PPSA99505/`.

Don't delete the old folder and copy in a new one, and never keep a second folder with the same ID under `/data/homebrew`: ShadowMount+ mounts the folder, and replacing it breaks the mount. Your accounts and settings are kept.

### Uninstall

Close Jelly5 and delete `/data/homebrew/PPSA99505`. Its accounts, settings and image cache live in the app's own data (`/download0/jelly5`); nothing else on the console is touched.

## Controls

| Button | In the menus | In the player |
| --- | --- | --- |
| ✕ | Select | Play / pause, select |
| ○ | Back | Hide the controls, then leave the player |
| D-pad | Move | Show the controls; left/right seek 10 s |
| L1 / R1 | Previous / next tab | Previous / next chapter (Live TV: channel) |
| L2 / R2 | Previous / next letter (A–Z libraries); a day back / on (Live TV guide) | Rewind / fast forward, faster the harder you press |
| △ | Search | Episodes (on a film: chapters; Live TV: channels) |
| □ | Sort and filter (libraries); favourite channel (guide) | Audio and subtitles |
| Options | Options for the selected title | The controls |
| Touchpad | *Now playing*, while music plays | The controls |
| L3 | | Playback info |

With HDMI Device Link on the PS5 and CEC on the TV, the TV remote works too: OK is ✕ and Back is ○.

In the Live TV guide, ✕ on what's on now watches the channel, ✕ on a later programme offers to record it, and Options opens the same choices on anything (also on a channel). ○ goes back to now, then to the top To jump days, press L2/R2, or go up from the channel column to the day (‹ I dag ›) and press ←/→.

## Emby

Jelly5 detects whether a server is Jellyfin or Emby, and the same screens work on both. It needs no Emby Premiere. On Emby:

- You sign in with user name and password (Emby has no Quick Connect).
- *Watch together*, lyrics and Jellyfin's home-screen order aren't available.
- *Skip intro* uses Emby's intro markers, which Emby only creates with Premiere.
- Scrubbing previews need *Thumbnail image extraction* turned on in the library's settings.
- Without Premiere, Emby transcodes in software and without HDR tone mapping.
- With Premiere, each PS5 user counts as one of Emby's devices.
- Behind a reverse proxy that serves Emby under `/emby`, the plain address is enough: Jelly5 tries `/emby` itself.
- Live TV needs Emby Premiere: without it, Emby lists no channels and the *Live TV* tab stays away.

Versions before 0.4.0 don't know Emby; update if an Emby account doesn't connect.

## Seerr (optional)

With [Seerr](https://github.com/seerr-team/seerr) (the successor of Overseerr and Jellyseerr) 3.4 or newer, Jelly5 shows what your library is missing and lets you request it. Seerr needs Jellyfin or Emby as its media server, your user imported, and permission to request.

Turn it on under *Settings* (your picture, top right) → *Seerr*:

| Setting | |
| --- | --- |
| **Address** | Seerr's address on your network, e.g. `http://192.168.1.20:5055`. A public domain that only works from outside your home won't work from the PS5. |
| **Sign-in** | *Automatic*: approve once, and Seerr signs in through your Jellyfin account from then on (Quick Connect must be on in Jellyfin). Or your *Jellyfin* / *Emby password*, or a *Seerr account*, typed once. Only Seerr's session is stored, never a password. On Emby the session lasts 30 days, then the PS5 asks you to sign in again. |
| **Test connection** | Checks the address, the session and the pictures. |

Once signed in, Seerr's results appear in Search, a *Discover* tab shows what's trending and upcoming, and title pages offer *Request*. A series you have only part of offers *Request more seasons* under Options on a season or an episode. Seerr's own "hide available / requested" settings apply. Separate 4K Radarr/Sonarr servers aren't offered.

## Formats and limits

| | Plays | Notes |
| --- | --- | --- |
| Video | H.264, HEVC (Main, Main 10), VP9 and older formats | H.264 4:2:2 and 4:4:4, and AV1, are transcoded by the server |
| HDR | HDR10, HLG, HDR10+ (as HDR10), Dolby Vision (its HDR10 base layer; profile 5 rebuilt as HDR10) | With the PS5's HDR on, the TV stays in HDR while Jelly5 is open, and HDR video goes to it as HDR10 (never as Dolby Vision). With it off, HDR video is tone mapped to SDR |
| Audio | AAC, AC3, E-AC3, TrueHD, DTS (incl. DTS-HD MA), FLAC, Opus, MP3 and more | Played as multichannel PCM; with *HDMI bitstream* on, Dolby Digital, Dolby Digital Plus and DTS go to the TV/receiver as they are (TrueHD and DTS-HD MA stay lossless PCM) |
| Subtitles | SRT, ASS/SSA, PGS, DVD and DVB, WebVTT | Embedded or external |
| Containers | MKV, MP4, TS/M2TS, AVI and more | No Blu-ray folders or ISO files |
| Live TV | Channels from the server's tuners (IPTV/M3U, HDHomeRun) as MPEG-TS or HLS | Played directly where the PS5 can; interlaced (1080i, 576i) and MPEG-2 channels are deinterlaced and converted by the server. No pausing back in time (timeshift). This fork: the receive-buffer and clock changes above; tested with Jellyfin 10.11.4 |

Limits of the platform, not of Jelly5:

- HDMI bitstream covers Dolby Digital, Dolby Digital Plus (Atmos in DD+) and DTS core, when the TV or receiver supports them. TrueHD (incl. its Atmos) and DTS-HD MA/DTS:X play as lossless 7.1 PCM.
- No true 24p output: the display runs at 60 or 120 Hz.
- No 3D: side-by-side and top-and-bottom files are refused, and 3D Blu-ray (MVC) plays in 2D.
- The app can't quit itself: close it with the PS button.

## Troubleshooting

| Problem | What to do |
| --- | --- |
| No Jelly5 tile | Check the path is exactly `/data/homebrew/PPSA99505/eboot.bin`, then rerun ShadowMount+ or reboot and jailbreak again. |
| The upload fails ("Text file busy") | Jelly5 is still running: close it first. |
| Your server isn't listed | Type its address. Discovery needs UDP port 7359; in Docker, publish `7359/udp`, and in Jellyfin turn on *Enable auto discovery*. |
| No previews when scrubbing | The server makes them. Jellyfin: turn on *Enable trickplay image extraction* in the library and run *Generate Trickplay Images*. Emby: turn on *Thumbnail image extraction*. |
| A title won't play or stutters | Press **L3** while it plays and include that info in an issue. Over Wi-Fi, lower *Maximum quality* in the settings. |
| Live TV freezes, restarts or drifts out of sync on a long path | The changes in this fork address the cases measured above. If it still happens, build with `JELLY5_AVDIAG=1` and include the console's log and the time of the problem. |
| The receiver shows PCM, not Dolby or Atmos | Turn on *Settings → HDMI bitstream* (and night mode off). TrueHD and DTS-HD MA always play as PCM: see *Formats and limits*. |
| Seerr isn't answering | Use Seerr's local address (port 5055 by default) and check it with *Test connection*. |
| Seerr's automatic sign-in fails | Seerr is older than 3.4, Quick Connect is off in Jellyfin, or your user isn't in Seerr. Use your password instead, or import the user. |
| Seerr shows no pictures | Seerr fetches them from TMDB, so the Seerr server needs internet access. |

**Reporting a problem:** open an issue with your firmware, your server and its version, what you did and what happened. For playback problems, the L3 playback info helps a lot. For Live TV on a long path, add the approximate round trip to the server and whether the problem is buffering, a freeze after a drop, or sound and picture apart.

## Privacy

Jelly5 talks to your Jellyfin or Emby server and, if you turn it on, your Seerr server, and nothing else. It never contacts TMDB, YouTube or other services: posters come through Seerr's image cache, and trailers are a QR code your phone opens. The only exception is opt-in: with *Check for updates* on, it asks GitHub once per launch whether there's a newer release. It keeps its accounts, settings, Seerr session (never a password) and a 96 MB image cache in its own folder, and release builds send no logs anywhere.

## Building

Jelly5 builds on macOS (Apple silicon or Intel) and Linux with the [PS5 payload SDK](https://github.com/ps5-payload-dev/sdk) and pacbrew, which `scripts/setup-toolchain.sh` downloads (pinned by checksum) into the git-ignored `toolchain/` folder. First install:

- **macOS**: `brew install llvm lld coreutils bash nasm meson ninja pkgconf` (the system's bash 3.2 is too old for the build script).
- **Linux** (Debian/Ubuntu): `sudo apt install build-essential clang lld llvm curl unzip zip python3-venv nasm meson ninja-build pkg-config`. clang, lld and llvm must be the same version (tested with LLVM 21); set `LLVM_CONFIG` (e.g. `llvm-config-21`) if several are installed.

```sh
scripts/setup-toolchain.sh                  # once: SDK, pacbrew sysroot, host zlib, AV1 (dav1d + FFmpeg 7.1)
cd app
eval "$(../scripts/setup-toolchain.sh --env)"
scripts/build.sh --release                  # → build/app/Jelly5-<version>.zip
```

On macOS, run the build with Homebrew's bash: `"$(brew --prefix)/bin/bash" scripts/build.sh`.

This fork's changes are on `main` (and on the `iptv-live-tv` branch they were made on). `JELLY5_AVDIAG=1 scripts/build.sh` makes the diagnostic build described above (not for everyday use).

A plain `scripts/build.sh` is a development build: it reads the git-ignored `.env.local` (`JF_URL`, `PS5_HOST`, …), uses `JF_URL` as the default server, and sends a debug log over UDP to the build machine (`scripts/log.sh`). `scripts/deploy.py` uploads it to the console over FTP.

Tests run on the build machine (they need libcurl's headers: `libcurl4-openssl-dev` on Debian/Ubuntu, included with macOS):

- `app/tests/host/run.sh`: the client against your Jellyfin server (`JF_URL`, `JF_USER`, `JF_PASS` in `.env.local`); `run.sh emby` against Emby (`EMBY_URL`, `EMBY_USER`, `EMBY_PASS`)
- `app/tests/host/seerr.sh`: the Seerr client (`SEERR_URL`); it sends no request unless given `--for-real`
- `app/tests/host/routes.sh`: no server needed; checks that every Jellyfin request is the same as on `main`

## Credits

**Jelly5-IPTV — TheUnwelcomed.** The Live TV changes above, found, measured and tested on TheUnwelcomed's own Jellyfin IPTV stack, shared host and PS5, built with the tools of that stack. The patches were written with Claude (Anthropic); their commits say so.

**[Jelly5](https://github.com/02dnot/Jelly5) — 02dnot**, the app this fork is built on. Jelly5 stands on the work of others in the PS5 scene and beyond:

- **[Nuvio PS5](https://github.com/theghostonline/Nuvio-PS5)** by Husam Osman: the native player, its session and subtitle handling, and the app packaging toolkit
- **[EVO Player](https://github.com/sainsaji/EVO-PLAYER-PS5)**: the media engine (demuxing, `sceVideodec2`, the AGC renderer, audio, HDR), shader tooling and much of what is known about the PS5's GPU from user space
- **[ps5-payload-dev SDK](https://github.com/ps5-payload-dev/sdk)** by John Törnblom and contributors, plus the pacbrew sysroot
- **[Jelly5-Seerr](https://github.com/viviandsx/Jelly5-Seer)** by [@viviandsx](https://github.com/viviandsx): the Seerr integration (client, search, requests, Discover) and the Linux build. Thank you!
- **[Switchfin](https://github.com/dragonflylee/switchfin)**, a reference for the Jellyfin API
- **[OverShifted/LiquidGlass](https://github.com/OverShifted/LiquidGlass)** (MIT), whose refraction profile the glass shader follows
- [FFmpeg](https://ffmpeg.org), [libass](https://github.com/libass/libass), FreeType, HarfBuzz, [cJSON](https://github.com/DaveGamble/cJSON), [NanoSVG](https://github.com/memononen/nanosvg), [QR Code generator](https://www.nayuki.io/page/qr-code-generator-library) by Project Nayuki, the [Unicode CLDR](https://cldr.unicode.org) (dates and language names), and the Inter, Roboto and Noto typefaces
- The [Jellyfin](https://jellyfin.org) project, [Seerr](https://github.com/seerr-team/seerr), and [Emby](https://emby.media)'s API documentation ([Emby.SDK](https://github.com/MediaBrowser/Emby.SDK))

Every component, its licence and where it is used are listed in [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).

## License

Jelly5 is free software under the **GNU General Public License v3.0 or later** (see [`LICENSE`](LICENSE)). Code taken from Nuvio PS5, EVO Player and other projects keeps its original notices. Jelly5-IPTV is a modified version under the same licence: its changes (2026-10-17 → 10-20) are listed in [What's different](#whats-different-from-jelly5-070) and marked in the code with their `JELLY5_…` tags.

## Disclaimer

Jelly5 is an unofficial homebrew project, provided **as is, without any warranty** of any kind, express or implied, as set out in sections 15 and 16 of the GPL. **You use it entirely at your own risk.** The authors and contributors are not responsible or liable for any damage, data loss, malfunction, account or online-service consequences, or any other harm, to your console, your devices, your data or anything else, arising from installing, using or being unable to use it.

Running homebrew requires a jailbroken console, which may break Sony's terms of service and void your warranty. Whether you do so is your choice and your responsibility.

Jelly5 contains no media and gives access to no content of its own: it plays what is on your own Jellyfin or Emby server. Use it only with media you have the right to watch.

Jelly5 is not affiliated with or endorsed by Sony Interactive Entertainment, the Jellyfin project or Emby LLC. *PlayStation*, *PS5* and *DualSense* are trademarks of Sony Interactive Entertainment Inc. *Emby* is a trademark of Emby LLC.
