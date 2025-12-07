#include "libretro.h"
#include <allegro.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

// Forward declarations from HAMOOPI
extern void hamoopi_init();
extern void hamoopi_run_frame();
extern void hamoopi_deinit();
extern void hamoopi_reset();
extern BITMAP* hamoopi_get_screen_buffer();
extern void hamoopi_set_input_state(unsigned port, unsigned device, unsigned index, unsigned id, int16_t state);
extern void hamoopi_get_audio_samples(int16_t* buffer, size_t frames);

static retro_log_printf_t log_cb;
static retro_video_refresh_t video_cb;
static retro_audio_sample_t audio_cb;
static retro_audio_sample_batch_t audio_batch_cb;
static retro_environment_t environ_cb;
static retro_input_poll_t input_poll_cb;
static retro_input_state_t input_state_cb;

// Video settings
#define HAMOOPI_WIDTH  640
#define HAMOOPI_HEIGHT 480
#define HAMOOPI_FPS    60

static uint32_t* frame_buf = NULL;

void retro_init(void)
{
   // Initialize frame buffer
   frame_buf = (uint32_t*)malloc(HAMOOPI_WIDTH * HAMOOPI_HEIGHT * sizeof(uint32_t));
   
   // Initialize Allegro (minimal setup for libretro)
   allegro_init();
   install_timer();
   set_color_depth(32);
   
   // Initialize game
   hamoopi_init();
}

void retro_deinit(void)
{
   hamoopi_deinit();
   
   if (frame_buf)
   {
      free(frame_buf);
      frame_buf = NULL;
   }
   
   allegro_exit();
}

unsigned retro_api_version(void)
{
   return RETRO_API_VERSION;
}

void retro_set_controller_port_device(unsigned port, unsigned device)
{
   (void)port;
   (void)device;
}

void retro_get_system_info(struct retro_system_info *info)
{
   memset(info, 0, sizeof(*info));
   info->library_name     = "HAMOOPI";
   info->library_version  = "v0.01a";
   info->need_fullpath    = false;
   info->valid_extensions = NULL; // Anything is fine, we don't use content.
}

void retro_get_system_av_info(struct retro_system_av_info *info)
{
   memset(info, 0, sizeof(*info));
   info->timing.fps            = HAMOOPI_FPS;
   info->timing.sample_rate    = 44100.0;
   info->geometry.base_width   = HAMOOPI_WIDTH;
   info->geometry.base_height  = HAMOOPI_HEIGHT;
   info->geometry.max_width    = HAMOOPI_WIDTH;
   info->geometry.max_height   = HAMOOPI_HEIGHT;
   info->geometry.aspect_ratio = (float)HAMOOPI_WIDTH / (float)HAMOOPI_HEIGHT;
}

void retro_set_environment(retro_environment_t cb)
{
   environ_cb = cb;
   
   bool no_content = true;
   cb(RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME, &no_content);
   
   // Set up logging
   struct retro_log_callback log;
   if (cb(RETRO_ENVIRONMENT_GET_LOG_INTERFACE, &log))
      log_cb = log.log;
}

void retro_set_audio_sample(retro_audio_sample_t cb)
{
   audio_cb = cb;
}

void retro_set_audio_sample_batch(retro_audio_sample_batch_t cb)
{
   audio_batch_cb = cb;
}

void retro_set_input_poll(retro_input_poll_t cb)
{
   input_poll_cb = cb;
}

void retro_set_input_state(retro_input_state_t cb)
{
   input_state_cb = cb;
}

void retro_set_video_refresh(retro_video_refresh_t cb)
{
   video_cb = cb;
}

void retro_reset(void)
{
   hamoopi_reset();
}

static void update_input(void)
{
   if (!input_poll_cb || !input_state_cb)
      return;

   input_poll_cb();

   // Map libretro buttons to HAMOOPI controls
   // Player 1
   int16_t p1_up    = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP);
   int16_t p1_down  = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN);
   int16_t p1_left  = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT);
   int16_t p1_right = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT);
   int16_t p1_b     = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B);
   int16_t p1_a     = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A);
   int16_t p1_y     = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y);
   int16_t p1_x     = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X);
   int16_t p1_l     = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L);
   int16_t p1_r     = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R);
   int16_t p1_sel   = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT);
   int16_t p1_start = input_state_cb(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START);

   // Player 2
   int16_t p2_up    = input_state_cb(1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP);
   int16_t p2_down  = input_state_cb(1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN);
   int16_t p2_left  = input_state_cb(1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT);
   int16_t p2_right = input_state_cb(1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT);
   int16_t p2_b     = input_state_cb(1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B);
   int16_t p2_a     = input_state_cb(1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A);
   int16_t p2_y     = input_state_cb(1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y);
   int16_t p2_x     = input_state_cb(1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X);
   int16_t p2_l     = input_state_cb(1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L);
   int16_t p2_r     = input_state_cb(1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R);
   int16_t p2_sel   = input_state_cb(1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT);
   int16_t p2_start = input_state_cb(1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START);

   // Pass input to HAMOOPI
   hamoopi_set_input_state(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP, p1_up);
   hamoopi_set_input_state(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN, p1_down);
   hamoopi_set_input_state(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT, p1_left);
   hamoopi_set_input_state(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, p1_right);
   hamoopi_set_input_state(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B, p1_b);
   hamoopi_set_input_state(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A, p1_a);
   hamoopi_set_input_state(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y, p1_y);
   hamoopi_set_input_state(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X, p1_x);
   hamoopi_set_input_state(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L, p1_l);
   hamoopi_set_input_state(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R, p1_r);
   hamoopi_set_input_state(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, p1_sel);
   hamoopi_set_input_state(0, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, p1_start);

   hamoopi_set_input_state(1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_UP, p2_up);
   hamoopi_set_input_state(1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_DOWN, p2_down);
   hamoopi_set_input_state(1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_LEFT, p2_left);
   hamoopi_set_input_state(1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_RIGHT, p2_right);
   hamoopi_set_input_state(1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_B, p2_b);
   hamoopi_set_input_state(1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_A, p2_a);
   hamoopi_set_input_state(1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_Y, p2_y);
   hamoopi_set_input_state(1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_X, p2_x);
   hamoopi_set_input_state(1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_L, p2_l);
   hamoopi_set_input_state(1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_R, p2_r);
   hamoopi_set_input_state(1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_SELECT, p2_sel);
   hamoopi_set_input_state(1, RETRO_DEVICE_JOYPAD, 0, RETRO_DEVICE_ID_JOYPAD_START, p2_start);
}

static void convert_allegro_bitmap_to_rgb(void)
{
   BITMAP* screen_buf = hamoopi_get_screen_buffer();
   if (!screen_buf || !frame_buf)
      return;

   // Convert Allegro bitmap to RGB format for libretro
   for (int y = 0; y < HAMOOPI_HEIGHT && y < screen_buf->h; y++)
   {
      for (int x = 0; x < HAMOOPI_WIDTH && x < screen_buf->w; x++)
      {
         int pixel = getpixel(screen_buf, x, y);
         int r = getr(pixel);
         int g = getg(pixel);
         int b = getb(pixel);
         
         // Convert to XRGB8888 format
         frame_buf[y * HAMOOPI_WIDTH + x] = (0xFF << 24) | (r << 16) | (g << 8) | b;
      }
   }
}

void retro_run(void)
{
    // Update input state
    update_input();
    
    // Run one frame of the game
    hamoopi_run_frame();
    
    // Convert Allegro bitmap to RGB buffer
    convert_allegro_bitmap_to_rgb();
    
    // Send video frame to frontend
    video_cb(frame_buf, HAMOOPI_WIDTH, HAMOOPI_HEIGHT, HAMOOPI_WIDTH * sizeof(uint32_t));
    
    // Generate and send audio samples
    // 44100 Hz / 60 FPS = 735 samples per frame
    if (audio_batch_cb)
    {
        static int16_t audio_samples[735 * 2]; // Stereo
        hamoopi_get_audio_samples(audio_samples, 735);
        audio_batch_cb(audio_samples, 735);
    }
}

bool retro_load_game(const struct retro_game_info *info)
{
   enum retro_pixel_format fmt = RETRO_PIXEL_FORMAT_XRGB8888;
   if (!environ_cb(RETRO_ENVIRONMENT_SET_PIXEL_FORMAT, &fmt))
   {
      if (log_cb)
         log_cb(RETRO_LOG_ERROR, "XRGB8888 is not supported.\n");
      else
         fprintf(stderr, "HAMOOPI: XRGB8888 is not supported.\n");
      return false;
   }

   (void)info;
   return true;
}

void retro_unload_game(void)
{
}

unsigned retro_get_region(void)
{
   return RETRO_REGION_NTSC;
}

bool retro_load_game_special(unsigned type, const struct retro_game_info *info, size_t num)
{
   (void)type;
   (void)info;
   (void)num;
   return false;
}

size_t retro_serialize_size(void)
{
   return 0;
}

bool retro_serialize(void *data, size_t size)
{
   (void)data;
   (void)size;
   return false;
}

bool retro_unserialize(const void *data, size_t size)
{
   (void)data;
   (void)size;
   return false;
}

void *retro_get_memory_data(unsigned id)
{
   (void)id;
   return NULL;
}

size_t retro_get_memory_size(unsigned id)
{
   (void)id;
   return 0;
}

void retro_cheat_reset(void)
{
}

void retro_cheat_set(unsigned index, bool enabled, const char *code)
{
   (void)index;
   (void)enabled;
   (void)code;
}
