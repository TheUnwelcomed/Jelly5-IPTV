# What this fork carries

Jelly5's `main` (https://github.com/02dnot/jelly5, 3d9e9db, 2026-11-26) plus two changes made for live TV
over a Jellyfin server on a slow path (130 ms, a few Mbit/s of headroom), each with its measurement in the
commit message:

| commit tag | what |
|---|---|
| JELLY5_AVLATE_V1/V2 | a picture late against the sound is skipped so the picture catches up, with a guard against the sound clock's steps |
| JELLY5_AVLATE_V3 | two in a row, then one shown; the skipping stands down when the pictures cannot come faster than the sound plays, waits while a jump in the sound's timestamps is unheard, starts over after a seek or a pause, and moves the presentation clock with the picture when a run ends; the decision is in `app/engine/media/include/evo_late_skip.h`, tested by `app/tests/host/lateskip.sh` |
| JELLY5_AVDIAG_V1 | `JELLY5_AVDIAG=1 app/scripts/build.sh`: the engine's log on the console with both clocks, the offset between them and the late skips, for reading a freeze or a desync after the fact |

Everything else is upstream Jelly5, unchanged. Build it the usual way (`app/scripts/build.sh`); the
diagnostic log is opt-in at build time and not meant for a release build.

## In Jelly5 now

Five of the seven changes this fork carried on Jelly5 0.7.0 went into Jelly5 on 2026-11-26, with
TheUnwelcomed as their author and plain comments in place of the tags. The code is the same; Jelly5 added
one limit to the watchdog.

| commit tag | in Jelly5 as |
|---|---|
| JELLY5_RCVBUF_V1, V2 | a2b2ad3 Streams: a 1 MB TCP receive buffer on every connection the player opens |
| JELLY5_AVCLOCK_V1 | d2e07af Playback: the sound's clock follows the sound's timestamps across a gap |
| JELLY5_PPCLOCK_V1 | fb8341f pp_clock: re-anchor the timeline when the gated frame is far off it |
| JELLY5_HWWATCH_V1 | 3d9e9db Playback: the decoder watchdog waits for a keyframe before it gives up (added there: 20 s with no picture reopens regardless, for a stream that never marks its keyframes) |

The first versions, all seven on Jelly5 0.7.0, are on the `iptv-live-tv` branch.

## Console test for V3

V3 has run only in the host test's model, not on a console. Use the diagnostic build, so the log says
what happened:

```
JELLY5_AVDIAG=1 app/scripts/build.sh
```

On a shared host with no root, `bash scripts/build-on-shared-host.sh` makes this build, with everything it needs
put under your home folder.

Close Jelly5 with the PS button before deploying. The log is `/data/evoplayer/jelly5-diag.log` (over
FTP). What to read in it:

- `picture was 0.43 s behind the sound - 6 frame(s) skipped to catch up (now -0.12 s)`: a catch-up,
  one line each, however many runs of two it took. With ` - cut short by a jump in the sound's
  timestamps` on the end, the new guard held it back; that is it working.
- `picture 0.40 s behind the sound, but pictures come at 0.8x - 8 frame(s) skipped, no skipping for 5 s`:
  a stand-down.
- In the line every 5 s: `av=`, the picture minus the sound as the player reads them (negative: the
  picture is behind), and `lateskip=`, the pictures skipped so far.

1. **A reconnect on Live TV**, what the skip is for. Play a channel, cut the console's network for about
   10 s, let it come back. Pass: a catch-up line within about a second of the picture returning,
   then `lateskip=` stays flat, `av=` stays near 0 and the lips match. Fail: `av=` stuck
   around −0.3 to −0.5 (the skip did nothing), or `lateskip=` still climbing a minute later with no new
   drop (the loop V3 ends).
2. **A 60 fps channel**, 10 minutes with no drops. Pass: `lateskip=` stays at 0 (the sound clock's steps
   alone skip nothing).
3. **Films**: a 1080p file and a 4K HDR remux, direct play. Seek back and forward several times, pause
   and resume, play at 2×. Pass: no stutter, lip sync holds, `av=` near 0 after each seek, at most one
   short catch-up line after one.
4. **A decoder that cannot keep up**, why Jelly5 left the skip out: a 4K AV1 file (decoded on the CPU).
   Pass: it plays as on Jelly5's own build; if the picture falls behind, stand-down lines 5, 10, 20,
   40 and 60 s apart, never a string of catch-up lines.
5. **A rebuffer**: throttle the network during a film until it buffers. Pass: after the buffering the
   picture goes on smoothly, with at most one catch-up line.
6. **HDMI bitstream** to a receiver, if you use it: plays and seeks as before.

The log with the time of each step is the most useful thing to keep; a failed step is worth an issue.
