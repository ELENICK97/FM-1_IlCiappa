/* SPDX-License-Identifier: MPL-2.0
 * IL CIAPPA: the engine. One word, eight ways of saying it.
 *
 * The voices (voice_data.h) are espeak-ng saying "ciàppa", with a pitch mark on each glottal period. A voice here
 * is a TD-PSOLA reader: a source position t walks through the word at the speed STRETCH asks, and every target
 * period a grain two source periods long, Hann-windowed, is taken at the mark nearest t and laid down. The pitch
 * comes from how often grains are laid, the length from how fast t moves, the formants from how fast a grain is
 * played (FORMANT): three things a tape ties together, set apart. The unvoiced parts (the "CI", the burst of the
 * "PP") keep their own rate, so they stay noise and not a buzz, except in ROBOT, where everything buzzes.
 *
 * Its own unit at -O2 (fm1.toml o2). Runs in the audio interrupt but dsp_key / dsp_held (the main loop's). */
#include <stdint.h>
#include "plat.h"
#include "fastmath.h"
#include "ciappa.h"
#include "voice_data.h"

#define FS 44100.0f
#define SRC_STEP ((float)VOICE_SR / FS)      /* source samples per output sample, as spoken */
#define NGRAIN 4

volatile dsp_params_t dsp_par = {M_CANTA, 0, 0, 0, 1.0f, 1.0f, 0.0f, 0.0f, 0, 132};
volatile dsp_status_t dsp_stat = {0, 0, 0, 0.0f, -1, -1};

/* ------------------------------------------------------------------ bits --- */
static uint32_t rng = 0x1234567u;
static uint32_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static float hann[257];                      /* sin^2, 0 .. 1 .. 0 over 256 steps */
static inline float window(float x)          /* x in [0, 1) */
{
    float f = x * 256.0f;
    int32_t i = (int32_t)f;
    if (i < 0 || i > 255)
        return 0.0f;
    f -= (float)i;
    return hann[i] + f * (hann[i + 1] - hann[i]);
}

static inline float pcm_at(const voice_t *v, float pos)
{
    int32_t i = (int32_t)pos;
    float f;
    if (pos < 0.0f || i >= (int32_t)v->len - 1)
        return 0.0f;
    f = pos - (float)i;
    return ((float)v->pcm[i] + f * (float)(v->pcm[i + 1] - v->pcm[i])) * (1.0f / 32768.0f);
}

static inline float note_hz(float note) { return 440.0f * fm_exp2f((note - 69.0f) * (1.0f / 12.0f)); }

/* ---------------------------------------------------------------- voices --- */
typedef struct {
    float start, end, pos, inv, step;
    uint8_t on;
} grain_t;

typedef struct {
    uint8_t on, held, releasing, mode;
    int16_t key;                             /* the key (before transposing) that releases it; -1 none */
    const voice_t *v;
    float f0, f0base;                        /* the note, Hz; MESCOLA jumps around f0base */
    float t;                                 /* where in the word, source samples */
    float next;                              /* output samples to the next grain */
    int32_t mi;                              /* the mark nearest t, last time */
    grain_t g[NGRAIN];
    float amp, gain, vib;
    int32_t delay;                           /* CORO: samples before it starts */
    uint16_t loops;                          /* TIENI's A's, BALBUZIE's CI's */
    int8_t dir;                              /* TIENI: +1 forward, -1 back through the A */
    float sa, sb;                            /* MESCOLA: the syllable */
    uint32_t age;
} pv_t;
static pv_t pv[DSP_NVOICE];
static uint32_t age_now;

static void syllable(pv_t *p)                /* MESCOLA: the next bit of the word, at a jumpy pitch */
{
    static const int8_t JUMP[8] = {0, 0, 0, 12, -12, 7, 5, -5};
    const voice_t *v = p->v;
    switch (rnd() % 5u) {
    case 0:                                  /* CIA */
    case 1:
        p->sa = (float)v->seg[0];
        p->sb = (float)(v->seg[1] + (v->seg[2] - v->seg[1]) * 3u / 5u);
        break;
    case 2:                                  /* PPA */
        p->sa = (float)(v->seg[3] > 900u ? v->seg[3] - 900u : 0u);
        p->sb = (float)v->seg[4];
        break;
    case 3:                                  /* CI */
        p->sa = (float)v->seg[0];
        p->sb = (float)(v->seg[1] + 600u);
        break;
    default:                                 /* A */
        p->sa = (float)v->loop_a;
        p->sb = (float)v->loop_b;
        break;
    }
    p->t = p->sa;
    p->f0 = p->f0base * fm_exp2f((float)JUMP[rnd() & 7u] * (1.0f / 12.0f));
}

static pv_t *voice_alloc(void)
{
    uint32_t i, best = 0, oldest = 0xFFFFFFFFu;
    for (i = 0; i < DSP_NVOICE; i++)
        if (!pv[i].on)
            return &pv[i];
    for (i = 0; i < DSP_NVOICE; i++) {       /* the oldest, those on their way out first */
        uint32_t a = pv[i].age - (pv[i].releasing ? 0x40000000u : 0u);
        if (a < oldest) {
            oldest = a;
            best = i;
        }
    }
    return &pv[best];
}

static void voice_start(int32_t note, uint32_t vel, int32_t key, uint32_t vi, uint32_t mode, float detune,
                        int32_t delay)
{
    pv_t *p = voice_alloc();
    uint32_t i;
    const voice_t *v = &VOICES[vi % NVOICES];
    p->v = v;
    p->mode = (uint8_t)mode;
    p->key = (int16_t)key;
    p->f0base = p->f0 = note_hz((float)note + detune);
    p->gain = 0.3f + 0.7f * (float)vel / 127.0f;
    p->amp = 0.0f;
    p->vib = 0.0f;
    p->next = 0.0f;
    p->mi = 0;
    p->loops = 0;
    p->dir = 1;
    p->delay = delay;
    p->held = key >= 0;
    p->releasing = 0;
    for (i = 0; i < NGRAIN; i++)
        p->g[i].on = 0;
    if (mode == M_ROVESCIO) {
        p->t = (float)v->seg[4] - 1.0f;
        p->mi = (int32_t)v->nmark - 1;
    } else {
        p->t = (float)v->seg[0];
    }
    if (mode == M_MESCOLA)
        syllable(p);
    p->age = age_now++;
    p->on = 1;
}

static uint32_t voice_pick(void)
{
    uint32_t v = dsp_par.voice;
    return v == DSP_VOICE_RANDOM ? rnd() % NVOICES : v % NVOICES;
}

static void trigger(int32_t note, uint32_t vel, int32_t key)   /* a key: one voice, or three for CORO */
{
    uint32_t mode = dsp_par.mode, vi = voice_pick();
    int32_t n = note + dsp_par.transpose;
    if (mode == M_CORO) {                    /* root, third, fifth: three singers, a little loose */
        voice_start(n, vel, key, vi, mode, 0.0f, 0);
        voice_start(n + 4, vel * 3u / 4u, key, vi + 1u, mode, 0.07f, 300 + (int32_t)(rnd() % 900u));
        voice_start(n + 7, vel * 3u / 4u, key, vi + 2u, mode, -0.06f, 600 + (int32_t)(rnd() % 1200u));
    } else {
        voice_start(n, vel, key, vi, mode, 0.0f, 0);
    }
}

static void release(int32_t key)
{
    uint32_t i;
    for (i = 0; i < DSP_NVOICE; i++)
        if (pv[i].on && pv[i].held && pv[i].key == key)
            pv[i].held = 0;                  /* TIENI goes on to PPA, MESCOLA ends its syllable, the rest go on */
}

/* the mark nearest source position t, from the last one (t moves a little each time) */
static const vmark_t *mark_near(pv_t *p)
{
    const voice_t *v = p->v;
    int32_t i = p->mi, n = (int32_t)v->nmark;
    float t = p->t;
    while (i + 1 < n && (float)v->mark[i + 1].pos <= t)
        i++;
    while (i > 0 && (float)v->mark[i].pos > t)
        i--;
    if (i + 1 < n && (float)v->mark[i + 1].pos - t < t - (float)v->mark[i].pos)
        i++;
    p->mi = i;
    return &v->mark[i];
}

static void grain_emit(pv_t *p, float formant, float vibrato)
{
    const vmark_t *m = mark_near(p);
    float per = (float)(m->per & 0x7FFFu), half, step = SRC_STEP * formant, f0;
    int voiced = (m->per & 0x8000u) || p->mode == M_ROBOT;
    grain_t *g = &p->g[0];
    uint32_t i;
    for (i = 0; i < NGRAIN; i++) {           /* a free grain, or the one nearest its end */
        if (!p->g[i].on) {
            g = &p->g[i];
            break;
        }
        if (p->g[i].end - p->g[i].pos < g->end - g->pos)
            g = &p->g[i];
    }
    if (voiced) {
        float tout;
        f0 = p->f0;
        if (vibrato > 0.0f)
            f0 *= fm_exp2f(vibrato * fm_sinf(p->vib) * (1.0f / 12.0f));
        tout = FS / f0;                      /* the target period, output samples */
        half = fm_minf(per, tout * step);    /* at most two grains overlap, however high */
        if (p->mode == M_ROBOT)
            half = tout * step;              /* the robot's: exactly one period, square-ish and hollow */
        if (half < 8.0f)
            half = 8.0f;
        p->next += tout;
    } else {
        half = per;
        p->next += per / SRC_STEP;           /* noise at its own rate */
    }
    g->start = (float)m->pos - half;
    g->end = (float)m->pos + half;
    g->pos = g->start;
    g->inv = 1.0f / (2.0f * half);
    g->step = step;
    g->on = 1;
}

/* the letter of CIAPPA source position t falls in (0..5) */
static int letter(const voice_t *v, float t)
{
    uint32_t u = t < 0.0f ? 0u : (uint32_t)t;
    if (u < v->seg[1])
        return u < (v->seg[0] + v->seg[1]) / 2u ? 0 : 1;
    if (u < v->seg[2])
        return 2;
    if (u < v->seg[3])
        return u < v->seg[3] - (v->seg[3] - v->seg[2]) / 3u ? 3 : 4;
    return 5;
}

/* one voice, n samples, added to out */
static void voice_render(pv_t *p, float *out, uint32_t n, float speed, float formant, float vibrato)
{
    const voice_t *v = p->v;
    float closure_a = (float)(v->seg[2] + 300u), closure_b = (float)(v->seg[3] > 900u ? v->seg[3] - 900u : 0u);
    float end = (float)v->seg[4], vinc = FM_TWO_PI * 5.5f / FS;
    uint32_t i, k;
    for (i = 0; i < n; i++) {
        float s = 0.0f, sp = speed;
        if (p->delay > 0) {
            p->delay--;
            continue;
        }
        if (p->t > closure_a && p->t < closure_b)
            sp *= 4.0f;                      /* the PP's silence, shortened: the word stays snappy */
        p->vib += vinc;
        if (p->vib > FM_TWO_PI)
            p->vib -= FM_TWO_PI;
        if (p->mode == M_NASTRO) {           /* tape: one head, no grains */
            s = pcm_at(v, p->t);
            p->t += SRC_STEP * p->f0 / v->f0 * (p->t > closure_a && p->t < closure_b ? 3.0f : 1.0f);
            if (p->t >= end)
                p->releasing = 1;
        } else {
            switch (p->mode) {
            case M_TIENI:
                if (p->dir > 0) {
                    p->t += sp;
                    if (p->held && p->t >= (float)v->loop_b)
                        p->dir = -1;
                } else {
                    p->t -= sp;
                    if (p->t <= (float)v->loop_a) {
                        p->dir = 1;
                        p->loops++;
                    }
                    if (!p->held)
                        p->dir = 1;
                }
                break;
            case M_ROVESCIO:
                p->t -= sp;
                if (p->t <= (float)v->seg[0])
                    p->releasing = 1;
                break;
            case M_BALBUZIE:
                p->t += sp;
                if (p->loops < 3u && p->t >= (float)(v->seg[1] + (v->seg[2] - v->seg[1]) / 5u)) {
                    p->t = (float)v->seg[0];
                    p->loops++;
                }
                break;
            case M_MESCOLA:
                p->t += sp;
                if (p->t >= p->sb) {
                    if (p->held)
                        syllable(p);
                    else
                        p->releasing = 1;
                }
                break;
            default:
                p->t += sp;
                break;
            }
            if (p->mode != M_ROVESCIO && p->mode != M_MESCOLA && p->t >= end)
                p->releasing = 1;
            p->next -= 1.0f;
            if (p->next <= 0.0f && !p->releasing)
                grain_emit(p, p->mode == M_ROBOT ? 1.0f : formant, p->mode == M_ROBOT ? 0.0f : vibrato);
            for (k = 0; k < NGRAIN; k++) {
                grain_t *g = &p->g[k];
                if (!g->on)
                    continue;
                s += window((g->pos - g->start) * g->inv) * pcm_at(v, g->pos);
                g->pos += g->step;
                if (g->pos >= g->end)
                    g->on = 0;
            }
        }
        if (p->releasing)
            p->amp -= p->amp * 0.004f;       /* ~6 ms */
        else
            p->amp += (1.0f - p->amp) * 0.02f;   /* ~1 ms */
        out[i] += s * p->amp * p->gain;
        if (p->releasing && p->amp < 0.0005f) {
            p->on = 0;
            return;
        }
    }
}

/* ------------------------------------------------------ keys from the UI --- */
#define QN 32u
static volatile uint32_t q[QN];
static volatile uint32_t q_head, q_tail;     /* head: the main loop's; tail: the render's */

void dsp_key(int32_t note, uint32_t vel)
{
    uint32_t h = q_head;
    if (h - q_tail >= QN)
        return;
    q[h % QN] = (uint32_t)(note & 0x7F) | (vel & 0x7Fu) << 8;
    q_head = h + 1u;
}

static volatile uint8_t held_notes[32];
static volatile uint32_t held_n;
void dsp_held(const uint8_t *notes, uint32_t n)
{
    uint32_t i;
    if (n > 32u)
        n = 32u;
    held_n = 0;
    for (i = 0; i < n; i++)
        held_notes[i] = notes[i];
    held_n = n;
}

void dsp_note(int32_t note, uint32_t vel)
{
    if (vel)
        trigger(note, vel, note);
    else
        release(note);
}

/* --------------------------------------------------------- ARP and song --- */
/* the tune PLAY sings: "La Ciappa", an original, in eighth notes (note from C4, length) */
static const int8_t SONG[][2] = {
    {4, 1}, {7, 1}, {9, 1}, {7, 1}, {4, 1}, {0, 1}, {2, 2},
    {4, 1}, {7, 1}, {9, 1}, {12, 1}, {11, 1}, {7, 1}, {9, 2},
    {9, 1}, {12, 1}, {11, 1}, {7, 1}, {9, 1}, {4, 1}, {7, 2},
    {5, 1}, {4, 1}, {2, 1}, {7, 1}, {0, 4},
};
#define SONG_N (sizeof SONG / sizeof SONG[0])
static int32_t seq_left;                     /* samples to the next step */
static uint32_t arp_i, song_i;
static uint8_t song_was, arp_was;

static void sequencers(uint32_t n)
{
    float eighth = FS * 30.0f / (float)(dsp_par.bpm < 40u ? 40u : dsp_par.bpm);
    if (dsp_par.song != song_was || dsp_par.arp != arp_was) {
        song_was = dsp_par.song;
        arp_was = dsp_par.arp;
        seq_left = 0;
        song_i = 0;
        dsp_stat.song_step = -1;
    }
    if (!dsp_par.song && !(dsp_par.arp && held_n))
        return;
    seq_left -= (int32_t)n;
    if (seq_left > 0)
        return;
    if (dsp_par.song) {
        const int8_t *s = SONG[song_i % SONG_N];
        trigger(60 + s[0], 100u, -1);
        dsp_stat.song_step = (int8_t)(song_i % SONG_N);
        seq_left += (int32_t)(eighth * (float)s[1]);
        song_i = (song_i + 1u) % SONG_N;
    } else {
        uint32_t nh = held_n;            /* (the main loop may have just let them all go) */
        int32_t note;
        if (!nh)
            return;
        arp_i = (arp_i + 1u) % nh;
        note = held_notes[arp_i];
        trigger(note, 100u, -1);
        dsp_stat.arp_note = (int8_t)note;
        seq_left += (int32_t)eighth;
    }
    if (seq_left < 0)
        seq_left = 0;
}

/* ----------------------------------------------------------------- echo --- */
#define ECHO_N 16384u
static int16_t echo_buf[ECHO_N] PLAT_POOL;
static uint32_t echo_w;
static float echo_lp;

/* --------------------------------------------------------------- render --- */
static float level;

void dsp_render(float *out, uint32_t n)
{
    uint32_t i, k, lit = 0, voices = 0, extra = 0;
    float speed = SRC_STEP / fm_clampf(dsp_par.stretch, 0.25f, 4.0f);
    float formant = fm_clampf(dsp_par.formant, 0.5f, 2.0f), vibrato = dsp_par.vibrato;
    float echo = dsp_par.echo;
    uint32_t delay = 14553u;                 /* 0.33 s */
    while (q_tail != q_head) {               /* the keys the main loop saw */
        uint32_t e = q[q_tail % QN];
        dsp_note((int32_t)(e & 0x7Fu), e >> 8 & 0x7Fu);
        q_tail = q_tail + 1u;
    }
    sequencers(n);
    for (i = 0; i < n; i++)
        out[i] = 0.0f;
    for (k = 0; k < DSP_NVOICE; k++) {
        pv_t *p = &pv[k];
        if (!p->on)
            continue;
        voice_render(p, out, n, speed, formant, vibrato);
        if (p->on && p->delay <= 0) {
            voices++;
            lit |= 1u << letter(p->v, p->t);
            if (p->mode == M_TIENI && p->loops > extra)
                extra = p->loops;
        }
    }
    for (i = 0; i < n; i++) {
        float x = out[i] * 0.85f, d;
        if (dsp_par.mode == M_ROBOT)         /* 4-bit robot */
            x = (float)(int32_t)(x * 8.0f) * 0.125f;
        d = (float)echo_buf[(echo_w + ECHO_N - delay) % ECHO_N] * (1.0f / 32768.0f);
        echo_lp += (d - echo_lp) * 0.35f;    /* each repeat a little darker */
        {
            float w = fm_clampf(x + echo_lp * 0.45f, -1.0f, 1.0f);
            echo_buf[echo_w] = (int16_t)(w * (echo > 0.0f ? 32000.0f : 0.0f));
        }
        echo_w = (echo_w + 1u) % ECHO_N;
        x += echo_lp * echo;
        x = fm_clampf(x, -1.0f, 1.0f);
        x = x * (1.5f - 0.5f * x * x);       /* soft clip */
        level += (fm_fabsf(x) - level) * 0.0015f;
        out[i] = x;
    }
    dsp_stat.lit = (uint8_t)lit;
    dsp_stat.voices = (uint8_t)voices;
    dsp_stat.extra_a = (uint16_t)extra;
    dsp_stat.level = level;
}

void dsp_panic(void)
{
    uint32_t i;
    for (i = 0; i < DSP_NVOICE; i++)
        pv[i].on = 0;
    for (i = 0; i < ECHO_N; i++)
        echo_buf[i] = 0;
    echo_lp = 0.0f;
}

void dsp_init(void)
{
    uint32_t i;
    for (i = 0; i <= 256u; i++) {
        float s = fm_sinf(FM_PI * (float)i / 256.0f);
        hann[i] = s * s;
    }
    dsp_panic();
}

const char *dsp_voice_name(uint32_t v) { return v == DSP_VOICE_RANDOM ? "A CASO" : VOICES[v % NVOICES].name; }
uint32_t dsp_nvoices(void) { return NVOICES; }
