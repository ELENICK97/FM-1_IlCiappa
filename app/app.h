/* SPDX-License-Identifier: MPL-2.0
 * IL CIAPPA: a firmware for the M-VAVE FM-1 that says one word. The app's half of the contract with fm1-base
 * (device/unity.c, host/plat_host.c). */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "IL CIAPPA"
#define APP_SPLASH_RGB RGB(255, 196, 0)
#define APP_BLOCK 256u                   /* frames per audio block: 5.8 ms at 44.1 kHz */
#define APP_TOUCH                        /* velocity from the keys (lib/touch.h) */

/* the store: the settings and the panel calibration (the platform's) */
enum { OBJ_SETTINGS, OBJ_PANEL, OBJ_NOBJ };
#define APP_STORE_MAP {0xFC000u, 0xDC000u}

void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);

/* the host simulator's checks: mode, voice, voices, lit, extra_a, octave, stretch, arp, song, dirty */
#define APP_HOST_EXPECT
int app_expect(const char *what, int *got);
