/*
 * Jelly5 — Jellyfin for PS5
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The late-picture skip (JELLY5_AVLATE_V3). A picture that has settled behind
 * the sound is not shown and the next one is decoded at once, until the
 * picture is back with the sound. The decision is plain arithmetic on what the
 * gate in decode_next_video_frame() already has, kept here so that a host test
 * can run it without a console (app/tests/host/lateskip.sh).
 *
 * Why it is needed: the gate only waits for the sound, it never hurries after
 * it, and the presentation clock behind the gate (pp_clock) runs on wall time.
 * pp_clock's own late drops compare a picture with that wall-clock timeline,
 * not with the sound, so an offset against the sound is invisible to it. After
 * a stall, a reconnect or a rebuffer hold the picture could settle a few
 * tenths of a second behind the sound and stay there (measured on Live TV:
 * 0.43 s for 18 minutes after a reconnect, 0.4 to 0.9 s after rebuffers).
 *
 * A catch-up runs from its first skipped picture until the picture is back
 * with the sound. The rules:
 *  1. A picture more than EVO_LATE_SKIP_S behind the sound is skipped, at most
 *     EVO_LATE_SKIP_MAX in a row; then one is shown, and the next run begins.
 *     The picture on screen is the decoder's own memory, borrowed, not copied
 *     (pp_playback_push_frame), and the 4th picture received after it can
 *     reuse that memory, in both decoders (FFmpeg's ring of 4 frames; the
 *     native pool of 8 slots, 4 pictures deep). Two in a row keep one to spare.
 *  2. The sound clock moves when a block is handed to the port, in steps of up
 *     to ~0.2 s, and stands still between them. So a picture counts as late
 *     only once the lag has read late without a break for
 *     EVO_LATE_SKIP_HOLD_S, longer than one such step (60 fps with HE-AAC read
 *     0.10 s late every ~6 frames with the picture in step).
 *  3. Skipping only helps when pictures come out of the decoder faster than
 *     the sound plays: a skipped picture has already been decoded, what the
 *     skip saves is the wait for its turn on screen. With a decoder that
 *     cannot keep up (AV1 on the CPU, heavy 4K), or a stream that arrives no
 *     faster than it plays, skipping throws pictures away and gains nothing.
 *     So from the EVO_LATE_SKIP_PROBE-th picture of a catch-up, skipped and
 *     shown alike, the pictures' timestamps since its first are measured
 *     against the wall clock, and below EVO_LATE_SKIP_MIN_RATE times the
 *     sound's speed the catch-up ends and skipping stands down: 5 s the first
 *     time, twice as long each time after, 60 s at most. A catch-up that
 *     reaches the sound puts that back to 5 s. Measured over the whole
 *     catch-up, not one run, so that one slow picture (a keyframe) does not
 *     decide it; and the lag is not what is measured: over a few pictures the
 *     sound clock's steps are as large as the gain.
 *  4. What was read before a break says nothing about after it. A new stream
 *     or a seek starts from nothing (evo_late_skip_reset), and the first
 *     picture after EVO_LATE_SKIP_HOLD_S without one (a pause, a rebuffer
 *     hold, a stall) ends a catch-up: the lag has to read late for the hold
 *     again.
 *  5. The sound clock has to have settled. When the sound's timestamps jump,
 *     the clock takes the jump as soon as the packet is read
 *     (evo_audio_out.c), with up to ~2.7 s of decoded sound still to be heard
 *     before it. Until that sound has been heard the lag the gate reads is not
 *     there: the picture is in step with what is heard, and a catch-up would
 *     put it ahead, then freeze it when its own jump arrives.
 *     evo_late_skip_sound_settled() tells when the jump is heard; until then
 *     no picture counts as late, and a catch-up ends.
 *  6. (the caller's) When a run ends with a picture shown, pp_clock's timeline
 *     is put on that picture (note.reanchor). The run has moved the picture up
 *     to the sound and the timeline is where it was, so the picture is early
 *     on it by what was gained: pp_clock re-anchors by itself when that is
 *     more than its pacing window (100 ms), and makes the picture wait when it
 *     is less. A run of two gains less at any frame rate above 20 fps; that
 *     wait would give the gain back every time, and the catch-up would get
 *     nowhere (rule 3 would stand it down again and again, the picture still
 *     behind).
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EVO_LATE_SKIP_S           0.15  /* s behind the sound: late */
#define EVO_LATE_SKIP_HOLD_S      0.35  /* s of reading late without a break before the first skip */
#define EVO_LATE_SKIP_MAX         2     /* skips in a row, then one picture is shown */
#define EVO_LATE_SKIP_PROBE       12    /* pictures of a catch-up before its speed is judged */
#define EVO_LATE_SKIP_MIN_RATE    1.5   /* pictures must come at this many times the sound's speed */
#define EVO_LATE_SKIP_QUIET_S     5.0   /* the first stand-down */
#define EVO_LATE_SKIP_QUIET_MAX_S 60.0  /* the longest */
#define EVO_LATE_SKIP_MAX_STEP_S  0.25  /* a timestamp step per picture above this is a jump, not a rate */
#define EVO_LATE_SKIP_SHIFT_MAX_S 8.0   /* a jump not heard after this long: the count is off, not the sound */

typedef struct {
    double    late_since;   /* when the lag began to read late without a break; < 0: it does not */
    double    last_step;    /* when a picture last came through; < 0: none yet */
    int       streak;       /* pictures skipped in a row */
    int       on;           /* a catch-up is going on */
    double    t0;           /* when it began */
    int64_t   pts0;         /* its first picture's timestamp, us (INT64_MIN: none) */
    int       pics;         /* pictures since its first, skipped and shown */
    int       skipped;      /* pictures it skipped */
    double    worst;        /* the largest lag read in it */
    double    quiet_until;  /* standing down until then */
    double    quiet_s;      /* how long the next stand-down lasts */
    int       shift_known;  /* rule 5: shift_seen holds the sound clock's shift */
    long long shift_seen;
    long long shift_mark;   /* sound decoded when the shift moved, in samples; < 0: nothing waits to be heard */
    double    shift_at;     /* when it moved */
} evo_late_skip;

enum {
    EVO_LATE_NOTE_NONE = 0,
    EVO_LATE_NOTE_ENDED,      /* a catch-up ended; `why` says how */
    EVO_LATE_NOTE_STOOD_DOWN  /* the pictures did not come fast enough: no skipping for quiet_s */
};

enum {
    EVO_LATE_END_CAUGHT_UP = 0,  /* the picture is back with the sound */
    EVO_LATE_END_BREAK,          /* rule 4: a pause, a hold or a stall */
    EVO_LATE_END_SOUND_JUMP      /* rule 5: the sound clock jumped ahead of what is heard */
};

typedef struct {
    int    reanchor;  /* rule 6: the picture to be shown ends a run of skips */
    int    what;      /* for the log: EVO_LATE_NOTE_NONE when there is nothing to say */
    int    why;       /* EVO_LATE_NOTE_ENDED: EVO_LATE_END_* */
    int    skipped;   /* pictures the catch-up skipped */
    double worst_s;   /* the largest lag read in it */
    double rate;      /* STOOD_DOWN: how fast the pictures came, 1.0 = as fast as they play */
    double quiet_s;   /* STOOD_DOWN: for how long */
} evo_late_skip_note;

/* A new stream, or a seek. */
static inline void evo_late_skip_reset(evo_late_skip *s)
{
    s->late_since = -1.0;
    s->last_step = -1.0;
    s->streak = 0;
    s->on = 0;
    s->t0 = 0.0;
    s->pts0 = INT64_MIN;
    s->pics = 0;
    s->skipped = 0;
    s->worst = 0.0;
    s->quiet_until = 0.0;
    s->quiet_s = EVO_LATE_SKIP_QUIET_S;
    s->shift_known = 0;
    s->shift_seen = 0;
    s->shift_mark = -1;
    s->shift_at = 0.0;
}

/*
 * Rule 5. shift: the sound clock's shift for timestamp jumps, in samples
 * (audio_clock_shift); decoded, played: the sound's samples decoded and handed
 * to the port since the open or seek (audio_samples_decoded,
 * audio_samples_played: both media samples at 48 kHz). The shift moves when a
 * jumped packet is read, so the jump is heard once the port has played what
 * was decoded by then. Returns 1 when the sound clock tells what is heard.
 */
static inline int evo_late_skip_sound_settled(evo_late_skip *s, long long shift, long long decoded,
                                              long long played, double now_s)
{
    if (!s->shift_known) {
        s->shift_known = 1;
        s->shift_seen = shift;
        s->shift_mark = -1;
    } else if (shift != s->shift_seen) {
        s->shift_seen = shift;
        s->shift_mark = decoded;
        s->shift_at = now_s;
    }
    if (s->shift_mark >= 0 && (played >= s->shift_mark || now_s - s->shift_at > EVO_LATE_SKIP_SHIFT_MAX_S))
        s->shift_mark = -1;
    return s->shift_mark < 0;
}

/* The catch-up that is going on ends; the picture in hand is shown. */
static inline void evo_late_skip_end_(evo_late_skip *s, int why, evo_late_skip_note *note)
{
    if (s->on) {
        note->what = EVO_LATE_NOTE_ENDED;
        note->why = why;
        note->skipped = s->skipped;
        note->worst_s = s->worst;
        if (why == EVO_LATE_END_CAUGHT_UP)
            s->quiet_s = EVO_LATE_SKIP_QUIET_S;
    }
    if (s->streak)
        note->reanchor = 1;
    s->on = 0;
    s->streak = 0;
}

/*
 * One decoded picture at the gate, the sound running. behind_s: how far the
 * picture is behind the sound (> 0: late). pts_us: its timestamp, INT64_MIN
 * when it has none. frame_s: the nominal time between pictures, used when the
 * timestamps cannot be. speed: the playback speed (1.0 = normal).
 * sound_settled: evo_late_skip_sound_settled(). now_s: a monotonic clock.
 * Returns 1 when the picture is to be skipped, 0 when it is to be shown.
 */
static inline int evo_late_skip_step(evo_late_skip *s, double behind_s, int64_t pts_us, double frame_s,
                                     double speed, int sound_settled, double now_s,
                                     evo_late_skip_note *note)
{
    const int late = sound_settled && behind_s > EVO_LATE_SKIP_S;

    note->reanchor = 0;
    note->what = EVO_LATE_NOTE_NONE;
    note->why = 0;
    note->skipped = 0;
    note->worst_s = 0.0;
    note->rate = 0.0;
    note->quiet_s = 0.0;

    /* rule 4: after a break, start from nothing (a stand-down stays) */
    if (s->last_step >= 0.0 && now_s - s->last_step > EVO_LATE_SKIP_HOLD_S) {
        evo_late_skip_end_(s, EVO_LATE_END_BREAK, note);
        s->late_since = -1.0;
    }
    s->last_step = now_s;

    if (!late) {
        s->late_since = -1.0;
        if (s->on || s->streak)
            evo_late_skip_end_(s, sound_settled ? EVO_LATE_END_CAUGHT_UP : EVO_LATE_END_SOUND_JUMP, note);
        return 0;
    }
    if (s->late_since < 0.0)
        s->late_since = now_s;

    if (!s->on) {
        if (now_s < s->quiet_until)                           /* rule 3: standing down */
            return 0;
        if (now_s - s->late_since < EVO_LATE_SKIP_HOLD_S)     /* rule 2: not for long enough */
            return 0;
        s->on = 1;
        s->t0 = now_s;
        s->pts0 = pts_us;
        s->pics = 0;
        s->skipped = 0;
        s->worst = behind_s;
        s->streak = 0;
    } else {
        s->pics++;
        if (behind_s > s->worst)
            s->worst = behind_s;
        /* rule 3: do the pictures come faster than the sound plays? */
        if (s->pics >= EVO_LATE_SKIP_PROBE) {
            const double wall = now_s - s->t0;
            double media = (double)s->pics * frame_s;
            if (pts_us != INT64_MIN && s->pts0 != INT64_MIN && pts_us > s->pts0) {
                const double d = (double)(pts_us - s->pts0) / 1000000.0;
                if (d <= (double)s->pics * EVO_LATE_SKIP_MAX_STEP_S)
                    media = d;
            }
            if (speed < 0.25)
                speed = 0.25;
            if (media < EVO_LATE_SKIP_MIN_RATE * speed * wall) {
                note->what = EVO_LATE_NOTE_STOOD_DOWN;
                note->skipped = s->skipped;
                note->worst_s = s->worst;
                note->rate = media / wall;   /* wall > 0: media > 0 is below the product */
                note->quiet_s = s->quiet_s;
                note->reanchor = s->streak > 0;
                s->quiet_until = now_s + s->quiet_s;
                s->quiet_s *= 2.0;
                if (s->quiet_s > EVO_LATE_SKIP_QUIET_MAX_S)
                    s->quiet_s = EVO_LATE_SKIP_QUIET_MAX_S;
                s->on = 0;
                s->streak = 0;
                return 0;
            }
        }
    }

    if (s->streak >= EVO_LATE_SKIP_MAX) {   /* rule 1: this one is shown, then the next run */
        note->reanchor = 1;
        s->streak = 0;
        return 0;
    }
    s->streak++;
    s->skipped++;
    return 1;
}

#ifdef __cplusplus
}
#endif
