# The `iptv-live-tv` branch

Jelly5 0.7.0 plus six patches made for live TV over a Jellyfin server on a slow path (130 ms, a few
Mbit/s of headroom), each with its measurement in the commit message:

| commit tag | what |
|---|---|
| JELLY5_RCVBUF_V1 | a 1 MB TCP receive buffer on the player's connection (the console's 128 KB window capped a connection at 8 Mbit/s) |
| JELLY5_RCVBUF_V2 | the same option on every connection the demuxer opens by itself (HLS segments kept the small window) |
| JELLY5_AVDIAG_V1 | `JELLY5_AVDIAG=1 app/scripts/build.sh`: the engine's log on the console with both clocks, for reading a freeze or a desync after the fact |
| JELLY5_AVCLOCK_V1 | the sound's clock follows a jump in the sound's timestamps (7–14 s of frozen picture after every dropped connection) |
| JELLY5_AVLATE_V1/V2 | a picture late against the sound is skipped so the picture catches up, with a guard against a bursty sound clock |
| JELLY5_PPCLOCK_V1 | the presentation timeline re-anchors on a frame that is far early or far late instead of throttling or dropping 20 frames |

Everything else is upstream Jelly5 (https://github.com/02dnot/jelly5), unchanged. Build it the usual way
(`app/scripts/build.sh`); the diagnostic log is opt-in at build time and not meant for a release build.
