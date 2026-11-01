/* SPDX-License-Identifier: MPL-2.0
 * IL CIAPPA: the engine's interface. The engine (dsp.c, its own unit at -O2) says "CIAPPA" at any pitch and any
 * length: TD-PSOLA on pitch marks found offline (tools/make_voice.py), so the voice sings each note with its own
 * formants, and a few stranger ways of saying it (the modes). Everything here runs in the audio interrupt but
 * dsp_key, which the main loop calls. */
#pragma once
#include <stdint.h>

#define DSP_NVOICE 8                     /* polyphony: CORO takes three a key */

enum {                                   /* the modes, as SELECT turns them */
    M_CANTA,                             /* each key sings CIAPPA at its pitch */
    M_TIENI,                             /* CIAAAAAA... while held, PPA on release */
    M_NASTRO,                            /* tape: pitch and speed together, chipmunk and demon */
    M_ROVESCIO,                          /* APPAIC: the word backwards, still in tune */
    M_BALBUZIE,                          /* CI-CI-CI-CIAPPA */
    M_MESCOLA,                           /* PPA-CIA-A-CI...: random syllables while held */
    M_CORO,                              /* a major chord of Ciappas, three voices */
    M_ROBOT,                             /* every grain at the note, noise too, crushed */
    M_N
};
#define DSP_VOICE_RANDOM 255u            /* dsp_params.voice: a voice at random each note */

typedef struct {                         /* set by the UI, read once a block */
    uint8_t mode;                        /* M_* */
    uint8_t voice;                       /* 0 .. NVOICES-1, or DSP_VOICE_RANDOM */
    uint8_t arp;                         /* the held keys, in turn */
    uint8_t song;                        /* the built-in tune (PLAY) */
    float stretch;                       /* 0.25 .. 4: how long the word lasts (1 = as spoken) */
    float formant;                       /* 0.5 (ORCO) .. 2 (GNOMO) */
    float vibrato;                       /* 0 .. 1 semitone */
    float echo;                          /* 0 .. 0.7 */
    int8_t transpose;                    /* semitones, the octave buttons' */
    uint16_t bpm;                        /* ARP and the song */
} dsp_params_t;
extern volatile dsp_params_t dsp_par;

typedef struct {                         /* what the UI draws, written by the engine once a block */
    uint8_t lit;                         /* letters sounding: bit 0 C, 1 I, 2 A, 3 P, 4 P, 5 A */
    uint8_t voices;                      /* voices sounding */
    uint16_t extra_a;                    /* TIENI: how many A's the longest held one has said */
    float level;                         /* the output's envelope, 0 .. ~1: the mouth */
    int8_t song_step;                    /* the tune's step, -1 stopped */
    int8_t arp_note;                     /* the last ARP note, -1 none */
} dsp_status_t;
extern volatile dsp_status_t dsp_stat;

void dsp_init(void);
void dsp_key(int32_t note, uint32_t vel);          /* main loop: vel 0 = released; note before transposing */
void dsp_note(int32_t note, uint32_t vel);         /* render context (MIDI in): the same, at once */
void dsp_held(const uint8_t *notes, uint32_t n);   /* main loop: the keys held now (the ARP's), in order */
void dsp_panic(void);
void dsp_render(float *out, uint32_t n);           /* n mono samples at 44.1 kHz, about -1..1 */
const char *dsp_voice_name(uint32_t v);
uint32_t dsp_nvoices(void);
