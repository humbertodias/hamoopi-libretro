#ifndef HAMOOPI_CORE_H
#define HAMOOPI_CORE_H

#include <allegro.h>
#include <stdint.h>

// Core initialization and cleanup
void hamoopi_init(void);
void hamoopi_deinit(void);
void hamoopi_reset(void);

// Frame execution
void hamoopi_run_frame(void);

// Video
BITMAP* hamoopi_get_screen_buffer(void);

// Audio
void hamoopi_get_audio_samples(int16_t* buffer, size_t frames);

// Input
void hamoopi_set_input_state(unsigned port, unsigned device, unsigned index, unsigned id, int16_t state);

// Input state structure for two players
typedef struct {
   int16_t up;
   int16_t down;
   int16_t left;
   int16_t right;
   int16_t a;
   int16_t b;
   int16_t x;
   int16_t y;
   int16_t l;
   int16_t r;
   int16_t select;
   int16_t start;
} hamoopi_input_t;

extern hamoopi_input_t hamoopi_input[2];

#endif /* HAMOOPI_CORE_H */
