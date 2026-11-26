/*
 * Jelly5 — Jellyfin for PS5
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Host test of the late-picture skip (evo_late_skip.h).
 *
 * The first half drives evo_late_skip_step() by hand. The second half runs it
 * inside a small model of the player: a decoder with a speed (and slower
 * keyframes, if asked), the decode thread's sleep between pictures, a sound
 * clock (smooth, or moving in steps as the PS5's does) that holds while it is
 * more than 0.5 s ahead of the newest picture, a jump in the timestamps with
 * the sound clock taking it before it is heard, the gate that waits for the
 * sound, and a presentation clock on wall time behind the gate. With the skip
 * off, the model shows what the skip is for: a picture that starts behind the
 * sound stays there.
 */
#include "evo_late_skip.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int fails;

#define CHECK(cond, ...) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "FAIL %s:%d: %s\n  ", __func__, __LINE__, #cond); \
            fprintf(stderr, __VA_ARGS__); \
            fputc('\n', stderr); \
            fails++; \
        } \
    } while (0)

/* ---------------------------------------------------------------- by hand */

#define F25 0.04   /* a 25 fps picture, s */

static int64_t us(double s) { return (int64_t)llround(s * 1000000.0); }

typedef struct {
    evo_late_skip s;
    double now;     /* the wall clock, s */
    int    pic;     /* the next picture's number; its timestamp is pic * F25 */
    double speed;
    int    settled;
} hand;

static void hand_init(hand *h, double now)
{
    memset(h, 0, sizeof *h);
    evo_late_skip_reset(&h->s);
    h->now = now;
    h->speed = 1.0;
    h->settled = 1;
}

/* One picture, `lag` behind the sound; the next comes `wall` seconds later. */
static int hand_step(hand *h, double lag, double wall, evo_late_skip_note *note)
{
    const int skip = evo_late_skip_step(&h->s, lag, us(h->pic * F25), F25, h->speed, h->settled, h->now, note);
    h->pic++;
    h->now += wall;
    return skip;
}

/* Late pictures for `seconds`, one every 40 ms, all expected to be shown. */
static void shown_for(hand *h, double lag, double seconds, const char *why)
{
    const double end = h->now + seconds;
    while (h->now < end) {
        evo_late_skip_note note;
        CHECK(!hand_step(h, lag, F25, &note), "%s: a picture was skipped at %.3f s", why, h->now);
    }
}

/* Pictures `lag` behind, one every `wall` seconds, until a note other than a
 * re-anchor (or `max` pictures). The skips and the shown pictures go in
 * pattern[] as 's' and '.', a re-anchor as 'r'. */
static evo_late_skip_note until_note(hand *h, double lag, double wall, int max, char *pattern)
{
    evo_late_skip_note note;
    memset(&note, 0, sizeof note);
    for (int n = 0; n < max; n++) {
        const int skip = hand_step(h, lag, wall, &note);
        if (pattern) {
            pattern[n] = skip ? 's' : note.reanchor ? 'r' : '.';
            pattern[n + 1] = 0;
        }
        if (note.what != EVO_LATE_NOTE_NONE)
            return note;
    }
    note.what = -1;
    return note;
}

static void test_hold_then_runs_of_two(void)
{
    hand h;
    char pat[64];
    evo_late_skip_note note;

    hand_init(&h, 100.0);
    shown_for(&h, 0.43, EVO_LATE_SKIP_HOLD_S - 0.02, "inside the hold");
    /* from the hold on, a fast decoder (5 ms a picture): two skipped, one shown
     * with the timeline put on it, two skipped ... */
    note = until_note(&h, 0.43, 0.005, 30, pat);
    CHECK(note.what == -1 && !strcmp(pat, "ssrssrssrssrssrssrssrssrssrssr"), "pattern %s, note %d", pat, note.what);
    /* the picture back with the sound ends it */
    note = until_note(&h, 0.43, 0.005, 2, pat);
    CHECK(!strcmp(pat, "ss"), "pattern %s", pat);
    CHECK(!hand_step(&h, 0.10, 0.005, &note), "in step, but skipped");
    CHECK(note.what == EVO_LATE_NOTE_ENDED && note.why == EVO_LATE_END_CAUGHT_UP && note.reanchor,
          "note %d why %d reanchor %d", note.what, note.why, note.reanchor);
    CHECK(note.skipped == 22 && fabs(note.worst_s - 0.43) < 1e-9, "%d skipped, worst %.2f", note.skipped, note.worst_s);

    /* ended on a shown picture: nothing to re-anchor, the note still comes */
    hand_init(&h, 100.0);
    shown_for(&h, 0.43, EVO_LATE_SKIP_HOLD_S - 0.02, "inside the hold");
    note = until_note(&h, 0.43, 0.005, 3, pat);
    CHECK(!strcmp(pat, "ssr"), "pattern %s", pat);
    CHECK(!hand_step(&h, 0.10, 0.005, &note), "in step, but skipped");
    CHECK(note.what == EVO_LATE_NOTE_ENDED && !note.reanchor && note.skipped == 2, "note %d reanchor %d skipped %d",
          note.what, note.reanchor, note.skipped);
}

static void test_a_lag_that_comes_and_goes_never_skips(void)
{
    /* The sound clock's steps: late for 50 ms, in step for 150 ms, for a minute. */
    evo_late_skip s;
    double now = 5.0;
    evo_late_skip_reset(&s);
    for (int pic = 0; pic < 60 * 60; pic++) {
        evo_late_skip_note note;
        const double lag = (pic % 12) < 3 ? 0.19 : 0.05;
        CHECK(!evo_late_skip_step(&s, lag, us(pic / 60.0), 1.0 / 60.0, 1.0, 1, now, &note), "skipped picture %d", pic);
        now += 1.0 / 60.0;
    }
}

static void test_slow_pictures_stand_down_and_back_off(void)
{
    /* Pictures come one every 50 ms of wall time, 40 ms of film each: 0.8x. */
    static const double want_quiet[] = { 5.0, 10.0, 20.0, 40.0, 60.0, 60.0 };
    hand h;
    char pat[64];
    evo_late_skip_note note;

    hand_init(&h, 0.0);
    shown_for(&h, 0.5, EVO_LATE_SKIP_HOLD_S - 0.02, "inside the hold");
    for (unsigned i = 0; i < sizeof want_quiet / sizeof want_quiet[0]; i++) {
        note = until_note(&h, 0.5 + i, 0.05, 40, pat);
        /* the 13th picture of the catch-up is the first that is judged */
        CHECK(!strcmp(pat, "ssrssrssrssr."), "stand-down %u: pattern %s", i, pat);
        CHECK(note.what == EVO_LATE_NOTE_STOOD_DOWN && note.skipped == 8 && !note.reanchor,
              "stand-down %u: note %d, %d skipped, reanchor %d", i, note.what, note.skipped, note.reanchor);
        CHECK(fabs(note.rate - 0.8) < 0.01, "stand-down %u: rate %.3f", i, note.rate);
        CHECK(fabs(note.quiet_s - want_quiet[i]) < 1e-9, "stand-down %u: quiet for %.0f s", i, note.quiet_s);
        /* nothing is skipped while it stands down, late as the picture is */
        shown_for(&h, 0.5 + i, want_quiet[i] - 0.2, "standing down");
        h.now += 0.2;   /* (a short step, not a break) */
    }

    /* The pictures come fast again and the catch-up reaches the sound: the
     * next stand-down is 5 s again. */
    h.now += 60.0;
    shown_for(&h, 0.5, EVO_LATE_SKIP_HOLD_S - 0.02, "after a break, the hold again");
    note = until_note(&h, 0.5, 0.005, 40, pat);
    CHECK(note.what == -1, "fast: note %d (%s)", note.what, pat);
    CHECK(!hand_step(&h, 0.1, 0.005, &note) && note.what == EVO_LATE_NOTE_ENDED, "fast: note %d", note.what);
    shown_for(&h, 0.5, EVO_LATE_SKIP_HOLD_S - 0.02, "the hold again");
    note = until_note(&h, 0.5, 0.05, 40, pat);
    CHECK(note.what == EVO_LATE_NOTE_STOOD_DOWN && fabs(note.quiet_s - 5.0) < 1e-9,
          "slow again: note %d, quiet for %.0f s", note.what, note.quiet_s);

    /* One slow picture in a catch-up (a keyframe at 4 times the others) does
     * not end it: the speed is judged over the whole catch-up. 2x decoder. */
    hand_init(&h, 0.0);
    shown_for(&h, 0.5, EVO_LATE_SKIP_HOLD_S - 0.02, "inside the hold");
    for (int n = 0; n < 40; n++) {
        const double wall = (n == 10) ? 4.0 * 0.02 : 0.02 * 21.0 / 24.0;
        (void)hand_step(&h, 0.5, wall, &note);
        CHECK(note.what == EVO_LATE_NOTE_NONE, "picture %d of a 2x catch-up: note %d", n, note.what);
    }
}

static void test_reset_and_break(void)
{
    hand h;
    char pat[64];
    evo_late_skip_note note;

    hand_init(&h, 0.0);
    shown_for(&h, 0.4, EVO_LATE_SKIP_HOLD_S - 0.02, "inside the hold");
    CHECK(hand_step(&h, 0.4, 0.005, &note), "late for the hold: not skipped");

    /* A seek in the middle of a run: the first late picture after it is shown,
     * and so is every one until the lag has read late for the hold again. */
    evo_late_skip_reset(&h.s);
    shown_for(&h, 0.4, EVO_LATE_SKIP_HOLD_S - 0.02, "after a seek");
    CHECK(hand_step(&h, 0.4, 0.005, &note), "after a seek, late for the hold");

    /* A break in the middle of a run (a pause, a rebuffer hold): the run ends
     * on the first picture after it, with the timeline put on it; then the hold. */
    h.now += 3.0;
    CHECK(!hand_step(&h, 0.4, F25, &note), "the picture after a break was skipped");
    CHECK(note.what == EVO_LATE_NOTE_ENDED && note.why == EVO_LATE_END_BREAK && note.reanchor && note.skipped == 1,
          "after a break: note %d why %d reanchor %d skipped %d", note.what, note.why, note.reanchor, note.skipped);
    shown_for(&h, 0.4, EVO_LATE_SKIP_HOLD_S - 0.06, "after a break");
    CHECK(hand_step(&h, 0.4, 0.005, &note), "after a break, late for the hold");

    /* A stand-down is over after a reset: a new stream gets a new try. */
    hand_init(&h, 1000.0);
    shown_for(&h, 0.4, EVO_LATE_SKIP_HOLD_S - 0.02, "inside the hold");
    note = until_note(&h, 0.4, 0.05, 40, pat);
    CHECK(note.what == EVO_LATE_NOTE_STOOD_DOWN, "note %d", note.what);
    evo_late_skip_reset(&h.s);
    shown_for(&h, 0.4, EVO_LATE_SKIP_HOLD_S - 0.02, "new stream, inside the hold");
    note = until_note(&h, 0.4, 0.005, 40, pat);
    CHECK(note.what == -1 && pat[0] == 's', "new stream, fast: note %d (%s)", note.what, pat);
}

static void test_speed_and_timestamps(void)
{
    hand h;
    char pat[64];
    evo_late_skip_note note;

    /* At 2x the sound plays two seconds a second: pictures at 2.5x do not gain
     * enough on it (1.5 x 2 is asked), pictures at 8x do. */
    for (int fast = 0; fast < 2; fast++) {
        hand_init(&h, 0.0);
        h.speed = 2.0;
        shown_for(&h, 0.5, EVO_LATE_SKIP_HOLD_S - 0.02, "inside the hold");
        note = until_note(&h, 0.5, F25 / (fast ? 8.0 : 2.5), 40, pat);
        if (fast)
            CHECK(note.what == -1, "8x at 2x speed: note %d (%s)", note.what, pat);
        else
            CHECK(note.what == EVO_LATE_NOTE_STOOD_DOWN && note.skipped == 8, "2.5x at 2x speed: note %d, %d skipped",
                  note.what, note.skipped);
    }

    /* No timestamps, or timestamps that jump in the catch-up (a reconnect at
     * the live edge) or go back: the nominal picture time is used, and fast
     * pictures are still fast, slow ones still slow. */
    for (int kind = 0; kind < 3; kind++) {
        for (int fast = 0; fast < 2; fast++) {
            evo_late_skip s;
            double now = 0.0;
            int stood_down = 0, skipped = 0;
            evo_late_skip_reset(&s);
            for (int pic = 0; pic < 40 && !stood_down; pic++) {
                int64_t pts = us(pic * F25);
                if (kind == 0)
                    pts = INT64_MIN;
                else if (kind == 1 && pic >= 14)
                    pts += us(10.0);
                else if (kind == 2 && pic >= 14)
                    pts -= us(10.0);
                const int skip = evo_late_skip_step(&s, 0.5, pts, F25, 1.0, 1, now, &note);
                skipped += skip;
                stood_down = note.what == EVO_LATE_NOTE_STOOD_DOWN;
                now += pic < 9 ? F25 : fast ? 0.005 : 0.05;
            }
            if (fast)
                CHECK(!stood_down && skipped >= 20, "kind %d fast: stood down %d, %d skipped", kind, stood_down, skipped);
            else
                CHECK(stood_down && skipped == 8, "kind %d slow: stood down %d, %d skipped", kind, stood_down, skipped);
        }
    }
}

static void test_sound_clock_not_settled(void)
{
    hand h;
    char pat[64];
    evo_late_skip_note note;
    evo_late_skip s;

    /* While the sound clock is ahead of what is heard, a late picture is not late. */
    hand_init(&h, 0.0);
    h.settled = 0;
    shown_for(&h, 0.4, 2.0, "the sound clock not settled");
    /* ...and a catch-up that is going on ends, with the timeline put on the picture */
    h.settled = 1;
    shown_for(&h, 0.4, EVO_LATE_SKIP_HOLD_S - 0.02, "inside the hold");
    note = until_note(&h, 0.4, 0.005, 4, pat);
    CHECK(!strcmp(pat, "ssrs"), "pattern %s", pat);
    h.settled = 0;
    CHECK(!hand_step(&h, 0.4, 0.005, &note), "not settled, but skipped");
    CHECK(note.what == EVO_LATE_NOTE_ENDED && note.why == EVO_LATE_END_SOUND_JUMP && note.reanchor,
          "note %d why %d reanchor %d", note.what, note.why, note.reanchor);

    /* evo_late_skip_sound_settled: a shift is heard once what was decoded when
     * it moved has been played */
    evo_late_skip_reset(&s);
    CHECK(evo_late_skip_sound_settled(&s, 4800, 90000, 10000, 1.0), "the first call: a shift from before");
    CHECK(evo_late_skip_sound_settled(&s, 4800, 95000, 20000, 1.1), "no change");
    CHECK(!evo_late_skip_sound_settled(&s, 24000, 100000, 30000, 1.2), "a jump, not heard yet");
    CHECK(!evo_late_skip_sound_settled(&s, 24000, 110000, 99999, 2.6), "one sample short");
    CHECK(evo_late_skip_sound_settled(&s, 24000, 120000, 100000, 2.7), "heard");
    CHECK(evo_late_skip_sound_settled(&s, 24000, 130000, 110000, 2.8), "and it stays so");
    CHECK(!evo_late_skip_sound_settled(&s, -9600, 200000, 120000, 3.0), "a jump back");
    CHECK(!evo_late_skip_sound_settled(&s, -9600, 200000, 120000, 3.0 + EVO_LATE_SKIP_SHIFT_MAX_S - 0.1), "not yet");
    CHECK(evo_late_skip_sound_settled(&s, -9600, 200000, 120000, 3.0 + EVO_LATE_SKIP_SHIFT_MAX_S + 0.1),
          "never heard: given up on");
    CHECK(!evo_late_skip_sound_settled(&s, 0, 300000, 120000, 20.0), "a jump");
    evo_late_skip_reset(&s);   /* a seek: the counts start again from 0 */
    CHECK(evo_late_skip_sound_settled(&s, 0, 1000, 0, 20.1), "after a seek");
}

/* ------------------------------------------------------- inside the player */

typedef struct {
    double fps;           /* pictures a second of film */
    double decode_rate;   /* how fast the decoder goes: 1.0 = as fast as the film plays at 1x */
    int    kf_every;      /* every kf_every-th picture takes 4 times as long (0: none); the average is decode_rate */
    double lag0;          /* how far the picture starts behind the sound, s */
    double sound_step;    /* the sound clock moves in steps of this, s (0: smoothly) */
    double speed;         /* the playback speed */
    int    stamps;        /* the pictures carry timestamps */
    int    skip_on;       /* 0: the player without the late-picture skip */
    int    clock_stays;   /* 1: pp_clock's timeline is not put on the picture when a run ends (no rule 6) */
    int    unguarded;     /* 1: the skip is told the sound clock has always settled (no rule 5) */
    double jump_at;       /* the timestamps jump by jump_s at this point of the film, s (0: no jump) */
    double jump_s;
    double queue_s;       /* the sound clock takes the jump this long before it is heard */
    double seconds;       /* how long it runs */
} model_in;

typedef struct {
    long   decoded, shown, skipped, clock_drops, skipped_late;   /* skipped_late: after the first 3 s */
    int    longest_run, ended, stood_down, reanchors;
    double first_skip_at, last_skip_at;
    double lag_end;       /* sound minus picture, as seen and heard, at the last picture shown, s */
    double most_ahead;    /* the most the picture was ahead of the sound (seen and heard) after 1 s, s */
    double longest_gap;   /* the longest time between two pictures on screen after 1 s, s */
} model_out;

typedef struct {
    const model_in *in;
    double t;             /* the wall clock */
    double heard;         /* where the sound being heard is in the film, s */
    double newest_pts;    /* the timestamp of the newest picture through the gate (the sound's hold) */
    int    have_newest;
} model_state;

static int in_jump_window(const model_state *m)
{
    return m->in->jump_at > 0.0 && m->heard >= m->in->jump_at - m->in->queue_s && m->heard < m->in->jump_at;
}

/* What the sound clock reads: the film position heard, in timestamps, with a
 * jump taken queue_s early; in steps, if asked (never behind, up to one ahead). */
static double sound_read(const model_state *m)
{
    double a = m->heard;
    if (m->in->jump_at > 0.0 && m->heard >= m->in->jump_at - m->in->queue_s)
        a += m->in->jump_s;
    return m->in->sound_step > 0.0 ? ceil(a / m->in->sound_step) * m->in->sound_step : a;
}

static double pts_of(const model_in *in, double film)
{
    return film + (in->jump_at > 0.0 && film >= in->jump_at ? in->jump_s : 0.0);
}

/* The wall clock moves on by dt; the sound plays on, unless it reads more than
 * 0.5 s ahead of the newest picture (evo_audio_out.c holds it there). */
static void advance(model_state *m, double dt)
{
    const double step = 0.0005;
    m->t += dt;
    while (dt > 1e-12) {
        const double d = dt < step ? dt : step;
        if (!m->have_newest || sound_read(m) - m->newest_pts <= 0.5)
            m->heard += d * m->in->speed;
        dt -= d;
    }
}

static model_out model(const model_in *in)
{
    const double frame_s = 1.0 / in->fps;
    const double avg_decode_s = frame_s / in->decode_rate;
    const double base_decode_s = in->kf_every > 0 ? avg_decode_s * in->kf_every / (in->kf_every + 3.0) : avg_decode_s;
    model_state m;
    evo_late_skip st;
    model_out out;
    double anchor_t = 0.0, anchor_p = 0.0, last_show = -1.0;
    int anchored = 0, run = 0;

    memset(&m, 0, sizeof m);
    memset(&out, 0, sizeof out);
    m.in = in;
    m.heard = in->lag0;
    out.first_skip_at = out.last_skip_at = -1.0;
    evo_late_skip_reset(&st);

    for (long i = 0;; i++) {
        const double film = (double)i * frame_s;
        const double pts = pts_of(in, film);
        double due;

        advance(&m, in->kf_every > 0 && i % in->kf_every == 0 ? 4.0 * base_decode_s : base_decode_s);
        if (m.t > in->seconds)
            break;
        out.decoded++;
        m.newest_pts = pts;   /* the video clock moves before the gate */
        m.have_newest = 1;

        /* the gate: an early picture waits for the sound (1.5 s at most); a late one goes through */
        for (double waited = 0.0; sound_read(&m) - pts < -0.008 && waited < 1.5; waited += 0.001)
            advance(&m, 0.001);

        if (in->skip_on) {
            evo_late_skip_note note;
            const int settled = in->unguarded || !in_jump_window(&m);
            const int skip = evo_late_skip_step(&st, sound_read(&m) - pts, in->stamps ? us(pts) : INT64_MIN,
                                                frame_s, in->speed, settled, m.t, &note);
            if (note.reanchor) {
                out.reanchors++;
                if (!in->clock_stays)   /* as decode_next_video_frame() does: pp_clock_reanchor() */
                    anchored = 0;
            }
            out.ended += note.what == EVO_LATE_NOTE_ENDED;
            out.stood_down += note.what == EVO_LATE_NOTE_STOOD_DOWN;
            if (skip) {
                out.skipped++;
                if (out.first_skip_at < 0.0)
                    out.first_skip_at = m.t;
                out.last_skip_at = m.t;
                if (m.t > 3.0)
                    out.skipped_late++;
                if (++run > out.longest_run)
                    out.longest_run = run;
                advance(&m, 0.0002);   /* the decode thread's sleep between calls */
                continue;
            }
        }
        run = 0;

        /* the presentation clock (pp_clock): a timeline on wall time; far
         * early, or late after a break, re-anchors; a little early waits, late
         * by more than 0.8 s is dropped, anything else is shown at once */
        if (!anchored) {
            anchored = 1;
            anchor_t = m.t;
            anchor_p = pts;
        }
        due = anchor_t + (pts - anchor_p) / in->speed;
        if (due - m.t > 0.100 || (m.t - due > 0.800 && m.t - last_show > 0.800)) {
            anchor_t = m.t;
            anchor_p = pts;
        } else if (due > m.t) {
            advance(&m, due - m.t);
        } else if (m.t - due > 0.800) {
            out.clock_drops++;
            advance(&m, 0.0002);
            continue;
        }
        out.shown++;
        if (m.t > 1.0 && last_show > 0.0 && m.t - last_show > out.longest_gap)
            out.longest_gap = m.t - last_show;
        last_show = m.t;
        out.lag_end = m.heard - film;
        if (m.t > 1.0 && film - m.heard > out.most_ahead)
            out.most_ahead = film - m.heard;
        advance(&m, 0.0002);
    }
    return out;
}

static void test_model_live_tv(void)
{
    /* Live TV after a reconnect: the picture 0.43 s behind the sound, a
     * hardware decoder with speed to spare. */
    model_in in = { .fps = 25.0, .decode_rate = 8.0, .lag0 = 0.43, .speed = 1.0, .stamps = 1, .seconds = 120.0 };
    model_out off, on;

    off = model(&in);
    CHECK(fabs(off.lag_end - 0.43) < 0.05, "without the skip the lag ends at %.3f s", off.lag_end);

    in.skip_on = 1;
    on = model(&in);
    CHECK(on.lag_end <= EVO_LATE_SKIP_S + 0.04 && on.lag_end > -0.05, "the lag ends at %.3f s", on.lag_end);
    CHECK(on.first_skip_at >= EVO_LATE_SKIP_HOLD_S && on.first_skip_at < EVO_LATE_SKIP_HOLD_S + 0.1,
          "the first skip at %.3f s", on.first_skip_at);
    CHECK(on.last_skip_at < 1.0 && on.skipped >= 6 && on.skipped <= 12, "last skip at %.3f s, %ld skipped",
          on.last_skip_at, on.skipped);
    CHECK(on.longest_run == EVO_LATE_SKIP_MAX, "runs of up to %d", on.longest_run);
    CHECK(on.stood_down == 0 && on.ended == 1, "%d stand-downs, %d catch-ups", on.stood_down, on.ended);

    /* 60 fps, 0.9 s behind (after a rebuffer), no timestamps, slow keyframes:
     * the sound holds until the picture is 0.5 s behind, the skip does the rest */
    in = (model_in){ .fps = 60.0, .decode_rate = 4.0, .kf_every = 30, .lag0 = 0.9, .speed = 1.0, .skip_on = 1,
                     .seconds = 120.0 };
    on = model(&in);
    CHECK(on.lag_end <= EVO_LATE_SKIP_S + 0.02 && on.lag_end > -0.05, "60 fps: the lag ends at %.3f s", on.lag_end);
    CHECK(on.last_skip_at < 2.0 && on.stood_down == 0, "60 fps: last skip at %.3f s, %d stand-downs",
          on.last_skip_at, on.stood_down);

    /* the sound clock in steps of 0.2 s: a real lag of 0.4 s is still closed... */
    in = (model_in){ .fps = 60.0, .decode_rate = 4.0, .lag0 = 0.4, .sound_step = 0.2, .speed = 1.0,
                     .stamps = 1, .skip_on = 1, .seconds = 120.0 };
    on = model(&in);
    CHECK(on.skipped > 0 && on.lag_end <= EVO_LATE_SKIP_S + 0.02 && on.lag_end > -0.1,
          "stepped clock: %ld skipped, the lag ends at %.3f s", on.skipped, on.lag_end);
    CHECK(on.last_skip_at < 1.5 && on.stood_down == 0, "stepped clock: last skip at %.3f s, %d stand-downs",
          on.last_skip_at, on.stood_down);
    /* ...and with the picture in step, the steps alone skip nothing */
    in.lag0 = 0.0;
    on = model(&in);
    CHECK(on.skipped == 0, "stepped clock, in step: %ld skipped", on.skipped);
}

static void test_model_the_gain_is_kept(void)
{
    /* Lags from 0.16 to 1.2 s, the usual picture rates, decoders from 2x to
     * 16x with keyframes at 4 times the others or without, the sound clock
     * smooth and in steps: the catch-up is over within 3 s, never more than
     * two in a row, and the picture ends with the sound; later, at most one
     * run of two puts right what a slow keyframe against a stepping sound
     * clock pushed over the threshold (17 of 4,050 cases in 120 s, never more
     * than one run). (The lags are 3.1 ms
     * off the round values, so that none comes to rest on the threshold or on
     * a step of the sound clock to the last digit.) A decoder at 1.6x may
     * instead stand down, and its keyframes move the lag by more than a
     * picture; it never skips on and on (under 2 % of its pictures after the
     * first 3 s). */
    static const double fps[] = { 24.0, 25.0, 30.0, 50.0, 60.0 };
    static const double rate[] = { 1.6, 2.0, 3.0, 4.0, 8.0, 16.0 };
    static const double step[] = { 0.0, 0.1, 0.2 };
    int bad = 0, cases = 0;

    for (unsigned a = 0; a < 5; a++)
        for (unsigned b = 0; b < 6; b++)
            for (unsigned c = 0; c < 3; c++)
                for (int kf = 0; kf < 2; kf++)
                    for (int cs = 16; cs <= 120; cs += 4) {
                        const model_in in = { .fps = fps[a], .decode_rate = rate[b], .kf_every = kf ? 25 : 0,
                                              .lag0 = cs / 100.0 + 0.0031, .sound_step = step[c], .speed = 1.0,
                                              .stamps = 1, .skip_on = 1, .seconds = 40.0 };
                        const model_out o = model(&in);
                        const int slow = rate[b] < 2.0;
                        cases++;
                        /* at rest the gate reads a picture when it leaves the decoder, up to
                         * one picture time before it is shown: that much more is left */
                        if (o.longest_run > EVO_LATE_SKIP_MAX ||
                            (slow ? o.skipped_late > o.decoded / 50 : o.skipped_late > EVO_LATE_SKIP_MAX) ||
                            (!slow && o.stood_down) ||
                            (!(slow && o.stood_down) && o.lag_end > EVO_LATE_SKIP_S + (slow ? 2.0 : 1.0) / fps[a]) ||
                            o.lag_end < -step[c] - 0.05) {
                            if (bad++ < 5)
                                fprintf(stderr, "  %g fps, %gx%s, steps of %g s, %.2f s behind: runs of %d, "
                                        "%ld skipped after 3 s, %d stand-downs, ends %+.3f s\n", fps[a], rate[b],
                                        kf ? " with keyframes" : "", step[c], in.lag0, o.longest_run,
                                        o.skipped_late, o.stood_down, o.lag_end);
                        }
                    }
    CHECK(bad == 0, "%d of %d cases", bad, cases);

    /* The same player with pp_clock's timeline left where it was: a run of two
     * gains 33 ms at 60 fps, inside pp_clock's 100 ms window, so the next
     * picture waits it away and the lag is back. The catch-up gets nowhere:
     * rule 3 sees pictures that gain nothing and stands down, time after
     * time, with the picture still 0.4 s behind. */
    {
        model_in in = { .fps = 60.0, .decode_rate = 8.0, .lag0 = 0.40, .speed = 1.0, .stamps = 1,
                        .skip_on = 1, .clock_stays = 1, .seconds = 40.0 };
        model_out o = model(&in);
        CHECK(o.ended == 0 && o.stood_down >= 3 && o.lag_end > 0.35,
              "the timeline left alone: %d catch-ups, %d stand-downs, ends %+.3f s", o.ended, o.stood_down, o.lag_end);
        in.clock_stays = 0;
        o = model(&in);
        CHECK(o.ended == 1 && o.stood_down == 0 && o.last_skip_at < 1.0 && o.lag_end < EVO_LATE_SKIP_S + 0.02,
              "the timeline on the picture: %d catch-ups, %d stand-downs, last skip at %.2f s, ends %+.3f s",
              o.ended, o.stood_down, o.last_skip_at, o.lag_end);
    }
}

static void test_model_timestamp_jump(void)
{
    /* The timestamps jump 0.4 s forward at 10 s, the sound queue 2.6 s deep,
     * picture and sound in step. Jelly5 without the skip plays through it;
     * with the skip and rule 5 it plays the same. Without rule 5 the skip
     * runs the picture ahead of the sound, and the picture freezes when its
     * own jump arrives. */
    model_in in = { .fps = 25.0, .decode_rate = 8.0, .lag0 = 0.0, .speed = 1.0, .stamps = 1,
                    .jump_at = 10.0, .jump_s = 0.4, .queue_s = 2.6, .seconds = 30.0 };
    model_out off, on, raw;

    off = model(&in);
    in.skip_on = 1;
    on = model(&in);
    in.unguarded = 1;
    raw = model(&in);

    CHECK(off.longest_gap < 0.06 && off.most_ahead < 0.05, "without the skip: gap %.3f s, ahead %.3f s",
          off.longest_gap, off.most_ahead);
    CHECK(on.skipped == 0 && on.longest_gap < 0.06 && on.most_ahead < 0.05,
          "with rule 5: %ld skipped, gap %.3f s, ahead %.3f s", on.skipped, on.longest_gap, on.most_ahead);
    CHECK(raw.skipped > 0 && raw.longest_gap > 0.15 && raw.most_ahead > 0.15,
          "without rule 5: %ld skipped, gap %.3f s, ahead %.3f s", raw.skipped, raw.longest_gap, raw.most_ahead);

    /* A real lag across the same jump is still caught up, once the jump is heard. */
    in.unguarded = 0;
    in.lag0 = 0.43;
    on = model(&in);
    CHECK(on.lag_end <= EVO_LATE_SKIP_S + 0.04 && on.lag_end > -0.05 && on.most_ahead < 0.05,
          "a lag across the jump: ends %+.3f s, ahead %.3f s", on.lag_end, on.most_ahead);
}

static void test_model_decoder_that_cannot_keep_up(void)
{
    /* AV1 on the CPU, 4K: 30 fps film, 24 pictures a second out of the decoder.
     * Skipping cannot help; what matters is that it costs next to nothing. */
    model_in in = { .fps = 30.0, .decode_rate = 0.8, .lag0 = 0.0, .speed = 1.0, .stamps = 1, .seconds = 300.0 };
    model_out off, on;

    off = model(&in);
    in.skip_on = 1;
    on = model(&in);
    /* 5 + 10 + 20 + 40 + 60 s of standing down, then every 60 s: 8 or 9 tries in 5 minutes */
    CHECK(on.stood_down >= 7 && on.stood_down <= 10, "%d stand-downs", on.stood_down);
    CHECK(on.skipped == (long)on.stood_down * 8, "%ld skipped in %d stand-downs", on.skipped, on.stood_down);
    CHECK(on.ended == 0, "%d catch-ups ended as if they had worked", on.ended);
    CHECK(on.longest_run <= EVO_LATE_SKIP_MAX, "runs of %d", on.longest_run);
    CHECK(on.shown >= off.shown - 80, "%ld pictures shown with the skip, %ld without", on.shown, off.shown);

    /* A decoder a little faster than the film (1.2x), the picture 0.43 s
     * behind: catching up would take seconds of 1 picture in 3. It stands down. */
    in = (model_in){ .fps = 25.0, .decode_rate = 1.2, .lag0 = 0.43, .speed = 1.0, .stamps = 1,
                     .skip_on = 1, .seconds = 30.0 };
    on = model(&in);
    CHECK(on.stood_down >= 1 && on.skipped <= 8L * on.stood_down + 16,
          "1.2x: %d stand-downs, %d catch-ups, %ld skipped", on.stood_down, on.ended, on.skipped);
}

int main(void)
{
    test_hold_then_runs_of_two();
    test_a_lag_that_comes_and_goes_never_skips();
    test_slow_pictures_stand_down_and_back_off();
    test_reset_and_break();
    test_speed_and_timestamps();
    test_sound_clock_not_settled();
    test_model_live_tv();
    test_model_the_gain_is_kept();
    test_model_timestamp_jump();
    test_model_decoder_that_cannot_keep_up();
    if (fails) {
        fprintf(stderr, "%d check(s) failed\n", fails);
        return 1;
    }
    puts("lateskip: ok");
    return 0;
}
