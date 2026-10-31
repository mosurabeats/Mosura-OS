/* Glue for the browser simulator: a scratch buffer the page writes into. */
#include "mosura.h"

static uint8_t scratch[4096];
uint8_t *sim_buf(void) { return scratch; }
int sim_fb_bytes(void) { return MOS_FB_BYTES; }
int sim_leds_size(void) { return (int)sizeof(mos_leds_t); }
