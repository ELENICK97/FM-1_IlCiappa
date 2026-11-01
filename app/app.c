/* SPDX-License-Identifier: MPL-2.0
 * IL CIAPPA: the app. Every key says CIAPPA, at its pitch; SELECT picks how (the modes, ciappa.h), PRESET who says
 * it, the four knobs how long, how big, how wobbly and how far away. The screen shows the word as it's said.
 * Part of the unity build (device/unity.c, host/plat_host.c), after the platform: gfx.c and announce.c are in
 * scope. The engine is dsp.c, its own unit. */
#include "fastmath.h"
#include "ciappa.h"

#define SET_MAGIC 0x43494131u            /* "CIA1" */
typedef struct {
    uint32_t magic;
    uint8_t mode, voice;                 /* M_*; 0..NVOICES-1, NVOICES = A CASO */
    int8_t octave;                       /* -2 .. +2 */
    int8_t stretch;                      /* K1: -8 .. +8, 2^(i/4): x0.25 .. x4 */
    int8_t formant;                      /* K2: -8 .. +8, 2^(i/8) */
    uint8_t vibrato, echo;               /* K3, K4: 0 .. 10 */
    uint8_t bpm;                         /* ALGO: 60 .. 240, in steps of 4 (stored / 2) */
} settings_t;
static settings_t set;
static uint8_t set_dirty, arp_on, song_on;

static const char *const MODE_NAME[M_N] = {"CIAPPA", "CIAAAAPPA", "NASTRO", "APPAIC", "CI-CI-CIAPPA",
                                           "PAPPACIA", "CORO", "ROBOT"};
static const char *const MODE_HINT[M_N] = {"canta ogni nota", "tieni premuto", "veloce e lento",
                                           "al contrario", "balbuziente", "sillabe a caso", "tre voci",
                                           "bip bop"};

/* --------------------------------------------------------------- params --- */
static float stretch_of(int32_t i) { return fm_exp2f((float)i * 0.25f); }
static float formant_of(int32_t i) { return fm_exp2f((float)i * 0.125f); }

static void params_push(void)            /* the settings to the engine */
{
    dsp_par.mode = set.mode;
    dsp_par.voice = set.voice >= dsp_nvoices() ? DSP_VOICE_RANDOM : set.voice;
    dsp_par.stretch = stretch_of(set.stretch);
    dsp_par.formant = formant_of(set.formant);
    dsp_par.vibrato = (float)set.vibrato * 0.1f;
    dsp_par.echo = (float)set.echo * 0.07f;
    dsp_par.transpose = (int8_t)(12 * set.octave);
    dsp_par.bpm = (uint16_t)(set.bpm * 2u);
    dsp_par.arp = arp_on;
    dsp_par.song = song_on;
}

static void settings_defaults(void)
{
    set.magic = SET_MAGIC;
    set.mode = M_CANTA;
    set.voice = 0;
    set.octave = 0;
    set.stretch = 0;
    set.formant = 0;
    set.vibrato = 2;
    set.echo = 0;
    set.bpm = 66;                        /* 132 bpm */
}

void app_init(void)
{
    dsp_init();
    if (plat_store_load(OBJ_SETTINGS, &set, sizeof set) != (int)sizeof set || set.magic != SET_MAGIC
        || set.mode >= M_N || set.voice > dsp_nvoices() || set.octave < -2 || set.octave > 2
        || set.stretch < -8 || set.stretch > 8 || set.formant < -8 || set.formant > 8 || set.vibrato > 10u
        || set.echo > 10u || set.bpm < 30u || set.bpm > 120u)
        settings_defaults();
    params_push();
}

/* ---------------------------------------------------------------- audio --- */
static float mono[APP_BLOCK];

static void midi_in(void)                /* audio interrupt: MIDI in sings too */
{
    uint32_t pkt;
    int port;
    while (plat_midi_in(&pkt, &port)) {
        uint32_t st = (pkt >> 8) & 0xFFu, d1 = (pkt >> 16) & 0x7Fu, d2 = pkt >> 24;
        if ((st & 0xF0u) == 0x90u && d2)
            dsp_note((int32_t)d1, d2);
        else if ((st & 0xF0u) == 0x80u || (st & 0xF0u) == 0x90u)
            dsp_note((int32_t)d1, 0u);
    }
}

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    static float gain_now;
    float gain = (float)master_q12 / 4096.0f * 7800000.0f;
    uint32_t i, done = 0;
    midi_in();
    while (done < n) {
        uint32_t m = n - done > APP_BLOCK ? APP_BLOCK : n - done;
        dsp_render(mono, m);
        for (i = 0; i < m; i++) {
            int32_t s;
            gain_now += (gain - gain_now) * 0.004f;
            s = (int32_t)(mono[i] * gain_now);
            lr[2 * (done + i)] = s;
            lr[2 * (done + i) + 1] = s;
        }
        done += m;
    }
}

void app_panic(void) { dsp_panic(); }

/* ------------------------------------------------------------------- UI --- */
static struct {
    char msg[2][24];
    uint32_t msg_until;
    uint32_t keys, btn;
    uint8_t redraw;
    uint8_t last_lit, last_voices;
    uint16_t last_extra;
    uint32_t joke_key, joke_t;           /* a silly button's CIAPPA: its note, and when to let it go */
} ui;

static void text_cat(char *d, const char *s)
{
    while (*d)
        d++;
    while (*s)
        *d++ = *s++;
    *d = 0;
}

static void int_text(int32_t v, char *b)
{
    char t[12];
    int n = 0;
    uint32_t u = (uint32_t)(v < 0 ? -v : v);
    do
        t[n++] = (char)('0' + u % 10u);
    while ((u /= 10u) && n < 11);
    if (v < 0)
        *b++ = '-';
    while (n)
        *b++ = t[--n];
    *b = 0;
}

void app_say(const char *a, const char *b)
{
    uint32_t i;
    char t[48];
    for (i = 0; i < 23u && a && a[i]; i++)
        ui.msg[0][i] = a[i];
    ui.msg[0][i] = 0;
    for (i = 0; i < 23u && b && b[i]; i++)
        ui.msg[1][i] = b[i];
    ui.msg[1][i] = 0;
    ui.msg_until = plat_ms() + 1300u;
    ui.redraw = 1;
    if (ann_on()) {
        t[0] = 0;
        text_cat(t, ui.msg[0]);
        if (ui.msg[1][0]) {
            text_cat(t, " ");
            text_cat(t, ui.msg[1]);
        }
        ann_say(t, 0, ANN_NOW);
    }
}

static void stretch_text(char *t)        /* x0.25 .. x4.0 */
{
    float s = stretch_of(set.stretch);
    int32_t h = (int32_t)(s * 100.0f + 0.5f);
    char d[8];
    t[0] = 'x';
    t[1] = 0;
    int_text(h / 100, d);
    text_cat(t, d);
    text_cat(t, ".");
    d[0] = (char)('0' + (h / 10) % 10);  /* one decimal, two below 1 */
    d[1] = h < 100 ? (char)('0' + h % 10) : 0;
    d[2] = 0;
    text_cat(t, d);
}

static const char *formant_word(void)
{
    static const char *const W[17] = {"ORCO", "ORCO", "ORCO", "OMONE", "OMONE", "VOCIONE", "VOCIONE", "BASSO",
                                      "VERA", "ALTO", "VOCINA", "VOCINA", "PUFFO", "PUFFO", "GNOMO", "GNOMO",
                                      "GNOMO"};
    return W[set.formant + 8];
}

static void voice_text(char *t)
{
    t[0] = 0;
    text_cat(t, set.voice >= dsp_nvoices() ? "A CASO" : dsp_voice_name(set.voice));
}

/* a screen reader that starts listening hears the whole screen */
static void speak(void)
{
    char t[96], v[16];
    if (ann_on() && ann_where()) {
        voice_text(v);
        t[0] = 0;
        text_cat(t, "IL CIAPPA. ");
        text_cat(t, MODE_NAME[set.mode]);
        text_cat(t, ". VOCE ");
        text_cat(t, v);
        ann_say(t, 0, ANN_NOW);
    }
    ann_tick(plat_ms());
}

static void changed(void)
{
    set_dirty = 1;
    params_push();
    ui.redraw = 1;
}

static void mode_step(int32_t d)
{
    set.mode = (uint8_t)((set.mode + M_N + (uint32_t)(d % (int32_t)M_N + (int32_t)M_N)) % M_N);
    changed();
    app_say(MODE_NAME[set.mode], "");
}

static void voice_step(int32_t d)
{
    uint32_t n = dsp_nvoices() + 1u;     /* and A CASO */
    char t[16];
    set.voice = (uint8_t)((set.voice + n * 4u + (uint32_t)d) % n);
    changed();
    voice_text(t);
    app_say("VOCE", t);
}

/* the buttons with nothing to do do something anyway */
static const char *const JOKE[NB] = {"STRACIAPPA", "CIAPPA?", "CIAPPATI", "CIAPPISSIMO", "MEGACIAPPA",
                                     "CIAPPA MONDIALE", "", "", "", "CIAPPA DOP", "", "CIAPPA 2000", "", ""};

static uint32_t rnd_ui(void)
{
    static uint32_t r = 0xC1A99Au;
    r ^= r << 13;
    r ^= r >> 17;
    r ^= r << 5;
    return r + plat_ms();
}

static void held_list(uint32_t keys)     /* the held keys' notes, low to high, for the ARP */
{
    uint8_t notes[NKEYS];
    uint32_t k, n = 0;
    for (k = 0; k < NKEYS; k++)
        if (keys >> k & 1u)
            notes[n++] = (uint8_t)(53 + k);
    dsp_held(notes, n);
}

static void input(void)
{
    uint32_t keys, btn = plat_buttons(), k;
    int32_t e;
    touch_poll();
    keys = touch_keys();
    for (k = 0; k < NKEYS; k++) {        /* key 0 is F3 (MIDI 53) */
        uint32_t bit = 1u << k;
        int32_t note = 53 + (int32_t)k;
        int32_t out = note + 12 * set.octave;
        if ((keys & bit) && !(ui.keys & bit)) {
            uint32_t v = touch_vel(k);
            if (v < 1u)
                v = 100u;
            if (!arp_on)
                dsp_key(note, v);
            plat_midi_send(0x09u | 0x90u << 8 | (uint32_t)out << 16 | v << 24, TO_USB);
        } else if (!(keys & bit) && (ui.keys & bit)) {
            if (!arp_on)
                dsp_key(note, 0u);
            plat_midi_send(0x08u | 0x80u << 8 | (uint32_t)out << 16, TO_USB);
        }
    }
    if (keys != ui.keys)
        held_list(keys);
    for (k = 0; k < NB; k++) {
        uint32_t bit = 1u << k;
        if (!(btn & bit) || (ui.btn & bit))
            continue;
        switch (k) {
        case B_OCTDN:
        case B_OCTUP: {
            int32_t o = clamp(set.octave + (k == B_OCTUP ? 1 : -1), -2, 2);
            char t[4];
            set.octave = (int8_t)o;
            changed();
            t[0] = o < 0 ? '-' : '+';
            t[1] = (char)('0' + (o < 0 ? -o : o));
            t[2] = 0;
            app_say("OTTAVA", t);
            break;
        }
        case B_ARP:
            arp_on = !arp_on;
            song_on = 0;
            params_push();
            app_say("ARPEGGIO", arp_on ? "SI" : "NO");
            break;
        case B_PLAY:
            song_on = !song_on;
            arp_on = 0;
            params_push();
            app_say(song_on ? "LA CIAPPA" : "STOP", song_on ? "canzone" : "");
            break;
        case B_HOME:
            dsp_panic();
            song_on = arp_on = 0;
            params_push();
            app_say("ZITTO!", "");
            break;
        case B_SAVE:
            if (plat_store_save(OBJ_SETTINGS, &set, sizeof set) == 0) {
                set_dirty = 0;
                app_say("SALVATO", "");
            } else {
                app_say("SALVA", "FALLITO");
            }
            break;
        default:                         /* a joke: a CIAPPA at a random pitch */
            if (ui.joke_key)
                dsp_key((int32_t)ui.joke_key, 0u);
            ui.joke_key = 48u + rnd_ui() % 30u;
            ui.joke_t = plat_ms() + 400u;
            dsp_key((int32_t)ui.joke_key, 100u);
            app_say(JOKE[k], "");
            break;
        }
    }
    if (ui.joke_key && (int32_t)(plat_ms() - ui.joke_t) >= 0) {
        dsp_key((int32_t)ui.joke_key, 0u);
        ui.joke_key = 0;
    }
    for (k = 0; k < NE; k++) {
        char t[16];
        if ((e = plat_enc((int)k)) == 0)
            continue;
        switch (k) {
        case EN_SELECT:
            mode_step(e);
            break;
        case EN_PRESET:
            voice_step(e);
            break;
        case EN_ALGO:
            set.bpm = (uint8_t)clamp(set.bpm + e * 2, 30, 120);
            changed();
            int_text(set.bpm * 2, t);
            app_say("BPM", t);
            break;
        case EN_K1:
            set.stretch = (int8_t)clamp(set.stretch + e, -8, 8);
            changed();
            stretch_text(t);
            app_say("DURATA", t);
            break;
        case EN_K2:
            set.formant = (int8_t)clamp(set.formant + e, -8, 8);
            changed();
            app_say("TIMBRO", formant_word());
            break;
        case EN_K3:
            set.vibrato = (uint8_t)clamp(set.vibrato + e, 0, 10);
            changed();
            int_text(set.vibrato * 10, t);
            text_cat(t, "%");
            app_say("VIBRATO", t);
            break;
        default:
            set.echo = (uint8_t)clamp(set.echo + e, 0, 10);
            changed();
            int_text(set.echo * 10, t);
            text_cat(t, "%");
            app_say("ECO", t);
            break;
        }
    }
    if (keys != ui.keys || btn != ui.btn)
        ui.redraw = 1;
    ui.keys = keys;
    ui.btn = btn;
    plat_glow(1);
    plat_leds(btn | (set_dirty ? 1u << B_SAVE : 0u) | (arp_on ? 1u << B_ARP : 0u) | (song_on ? 1u << B_PLAY : 0u),
              keys);
}

void app_input_only(void) { input(); }

/* ------------------------------------------------------------ the word --- */
/* block letters, 7 rows, a bit per column (bit 4 the leftmost of 5) */
static const uint8_t GL_C[7] = {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E};
static const uint8_t GL_I[7] = {0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E};   /* 3 wide, in the middle */
static const uint8_t GL_A[7] = {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11};
static const uint8_t GL_P[7] = {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10};

typedef struct {
    const uint8_t *g;
    uint8_t cols, first;                 /* its columns (5, or 3 for the I: from column 1) */
    uint8_t letter;                      /* 0..5 in CIAPPA */
} glyph_t;

/* the word, with TIENI's extra A's: CIAAAAAPPA. Its letters fit 232 pixels, so the more A's the thinner. */
static void draw_word(int32_t y0, int32_t h)
{
    glyph_t w[20];
    uint32_t n = 0, i, cols = 0, extra = dsp_stat.extra_a, lit = dsp_stat.lit;
    int32_t px, x, rowh = 10, bounce;
    if (extra > 6u)                      /* past six the letters would be hairs */
        extra = 6u;
    w[n++] = (glyph_t){GL_C, 5, 0, 0};
    w[n++] = (glyph_t){GL_I, 3, 1, 1};
    for (i = 0; i <= extra; i++)
        w[n++] = (glyph_t){GL_A, 5, 0, 2};
    w[n++] = (glyph_t){GL_P, 5, 0, 3};
    w[n++] = (glyph_t){GL_P, 5, 0, 4};
    w[n++] = (glyph_t){GL_A, 5, 0, 5};
    for (i = 0; i < n; i++)
        cols += w[i].cols;
    cols += n - 1u;                      /* a column between letters */
    px = 232 / (int32_t)cols;
    if (px > 9)
        px = 9;
    if (px < 2)
        px = 2;
    x = (240 - px * (int32_t)cols) / 2;
    bounce = (int32_t)(dsp_stat.level * 30.0f);
    if (bounce > 8)
        bounce = 8;
    cv_begin(240, (uint32_t)h, C_BLACK);
    for (i = 0; i < n; i++) {
        int on = (lit >> w[i].letter) & 1u;
        uint16_t c = on ? C_HI : (dsp_stat.voices ? C_GRAY : C_WHITE);
        int32_t r, cc, y = (h - 7 * rowh) / 2 - (on ? bounce + 4 : 0);
        for (r = 0; r < 7; r++)
            for (cc = 0; cc < w[i].cols; cc++)
                if (w[i].g[r] >> (4 - w[i].first - cc) & 1u)
                    cv_rect(x + cc * px, y + r * rowh, px - (px > 3 ? 1 : 0), rowh - 1, c);
        x += (w[i].cols + 1) * px;
    }
    cv_blit(0, (uint32_t)y0);
}

static void draw(void)
{
    char t[24];
    uint32_t now = plat_ms(), i;
    int32_t x;
    /* the header: the name and the voice, or a message for a moment */
    cv_begin(240, 32, C_BLACK);
    if (now < ui.msg_until) {
        x = cv_text(8, 6, &FONT_B, ui.msg[0], C_WHITE);
        cv_text(x + 8, 6, &FONT_B, ui.msg[1], C_HI);
    } else {
        cv_text(8, 6, &FONT_B, APP_NAME, C_WHITE);
        voice_text(t);
        cv_text(232 - text_w(&FONT_B, t), 6, &FONT_B, t, C_HI);
    }
    cv_rect(0, 31, 240, 1, C_LINE);
    cv_blit(0, 0);
    draw_word(32, 96);
    /* the mode: its name big, a hint, where it is among them */
    cv_begin(240, 44, C_BLACK);
    {
        const felucca_font_t *f = text_w(&FONT_M, MODE_NAME[set.mode]) > 200 ? &FONT_B : &FONT_M;
        cv_text((240 - text_w(f, MODE_NAME[set.mode])) / 2, f == &FONT_M ? 0 : 4, f, MODE_NAME[set.mode], C_HI);
        cv_text((240 - text_w(&FONT_XS, MODE_HINT[set.mode])) / 2, 27, &FONT_XS, MODE_HINT[set.mode], C_GRAY);
        cv_text(6, 8, &FONT_XS, "<", C_DIM);
        cv_text(228, 8, &FONT_XS, ">", C_DIM);
    }
    cv_blit(0, 128);
    /* the knobs, and the bottom line */
    cv_begin(240, 68, C_BLACK);
    cv_rect(0, 0, 240, 1, C_LINE);
    {
        char v[4][12];
        static const char *const L[4] = {"DURATA", "TIMBRO", "VIBRATO", "ECO"};
        stretch_text(v[0]);
        v[1][0] = 0;
        text_cat(v[1], formant_word());
        int_text(set.vibrato * 10, v[2]);
        text_cat(v[2], "%");
        int_text(set.echo * 10, v[3]);
        text_cat(v[3], "%");
        for (i = 0; i < 4u; i++) {
            int32_t cx = 4 + (int32_t)i * 59;
            cv_text(cx, 5, &FONT_XS, L[i], C_GRAY);
            cv_text(cx, 18, &FONT_B, v[i], C_WHITE);
        }
    }
    {
        char o[8];
        o[0] = 'O';
        o[1] = 'T';
        o[2] = 'T';
        o[3] = ' ';
        o[4] = set.octave < 0 ? '-' : '+';
        o[5] = (char)('0' + (set.octave < 0 ? -set.octave : set.octave));
        o[6] = 0;
        cv_text(4, 46, &FONT_XS, o, C_GRAY);
        int_text(set.bpm * 2, t);
        text_cat(t, " BPM");
        cv_text(64, 46, &FONT_XS, t, C_GRAY);
        cv_text(132, 46, &FONT_XS, "ARP", arp_on ? C_HI : C_DIM);
        cv_text(164, 46, &FONT_XS, "CANZONE", song_on ? C_HI : C_DIM);
    }
    cv_blit(0, 172);
}

void app_ui_init(void)
{
    touch_on(1);
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui.redraw = 1;
}

void app_frame(void)
{
    static uint32_t last_draw;
    uint32_t now = plat_ms();
    input();
    speak();
    if (dsp_stat.lit != ui.last_lit || dsp_stat.voices != ui.last_voices || dsp_stat.extra_a != ui.last_extra) {
        ui.last_lit = dsp_stat.lit;
        ui.last_voices = dsp_stat.voices;
        ui.last_extra = dsp_stat.extra_a;
        ui.redraw = 1;
    }
    if (dsp_stat.voices && now - last_draw > 50u)   /* the word bounces while it's said */
        ui.redraw = 1;
    if (ui.msg_until && now >= ui.msg_until) {
        ui.msg_until = 0;
        ui.redraw = 1;
    }
    if (ui.redraw) {
        ui.redraw = 0;
        last_draw = now;
        draw();
    }
}

#ifdef PLAT_HOST
int app_expect(const char *what, int *got)
{
    if (!strcmp(what, "mode"))
        *got = set.mode;
    else if (!strcmp(what, "voice"))
        *got = set.voice;
    else if (!strcmp(what, "voices"))
        *got = dsp_stat.voices;
    else if (!strcmp(what, "lit"))
        *got = dsp_stat.lit;
    else if (!strcmp(what, "extra_a"))
        *got = dsp_stat.extra_a;
    else if (!strcmp(what, "octave"))
        *got = set.octave;
    else if (!strcmp(what, "stretch"))
        *got = set.stretch;
    else if (!strcmp(what, "arp"))
        *got = arp_on;
    else if (!strcmp(what, "song"))
        *got = song_on;
    else if (!strcmp(what, "dirty"))
        *got = set_dirty;
    else
        return 0;
    return 1;
}
#endif
