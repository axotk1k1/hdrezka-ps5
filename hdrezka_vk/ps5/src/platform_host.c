/*
 * PS5 Vulkan Template - platform.h on a Linux PC, for the host reference build.
 *
 * The host reference build (ps5/tools/host-reference.sh) runs the title's code
 * on the PC's own Vulkan driver with a headless surface: no pad, no sound,
 * messages on standard error. Its pictures are the reference the console's are compared to.
 *
 * Copyright (C) 2026 Mihawk
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 */
#include "platform.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static const char *say_prefix = "";

void
platform_init(const char *title_name)
{
   static char prefix[64];
   snprintf(prefix, sizeof(prefix), "[%s] ", title_name);
   say_prefix = prefix;
   say("starts (host reference build)");
}

void
say(const char *format, ...)
{
   va_list args;
   va_start(args, format);
   fputs(say_prefix, stderr);
   vfprintf(stderr, format, args);
   va_end(args);
   fputc('\n', stderr);
   fflush(stderr);
}

double
now_seconds(void)
{
   struct timespec t;
   clock_gettime(CLOCK_MONOTONIC, &t);
   return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

bool
pad_open(void)
{
   return false;
}

void
pad_poll(struct pad *pad)
{
   memset(pad, 0, sizeof(*pad));
}

int
pad_readings(const struct pad_reading **readings)
{
   *readings = NULL;
   return 0;
}

void
pad_vibrate(float large, float small)
{
   (void)large;
   (void)small;
}

void
pad_light_bar(uint8_t r, uint8_t g, uint8_t b)
{
   (void)r;
   (void)g;
   (void)b;
}

uint32_t
pad_players(void)
{
   return 0;
}

bool
pad_player(int player, struct pad *pad)
{
   (void)player;
   memset(pad, 0, sizeof(*pad));
   return false;
}

int
pad_player_readings(int player, const struct pad_reading **readings)
{
   (void)player;
   *readings = NULL;
   return 0;
}

void
pad_player_vibrate(int player, float large, float small)
{
   (void)player;
   (void)large;
   (void)small;
}

void
pad_player_light_bar(int player, uint8_t r, uint8_t g, uint8_t b)
{
   (void)player;
   (void)r;
   (void)g;
   (void)b;
}

bool
audio_start(audio_fill_fn fill, void *user)
{
   (void)fill;
   (void)user;
   return false;
}

void
audio_stop(void)
{
}

bool
keyboard_open(const char *title, const char *text, bool password, int max_chars)
{
   (void)title;
   (void)text;
   (void)password;
   (void)max_chars;
   return false;
}

int
keyboard_poll(char *out, size_t size)
{
   (void)out;
   (void)size;
   return -1;
}

/* The reference build has no microphone. */
bool
mic_start_recording(void)
{
   return false;
}

void
mic_stop_recording(void)
{
}

bool
mic_is_recording(void)
{
   return false;
}

size_t
mic_get_samples(int16_t *out, size_t max_samples)
{
   (void)out;
   (void)max_samples;
   return 0;
}

int
mic_sample_rate(void)
{
   return 16000;
}
