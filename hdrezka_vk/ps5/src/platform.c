/*
 * PS5 Vulkan Template - the console: klog, the splash, the pad, sound, time, the exit.
 *
 * Copyright (C) 2026 Mihawk
 *
 * This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
 *
 * Every console call here is one PS5_vkQuake, PS5_RetroArch or PS5_Vulkan's
 * smoke test runs with; no SDK header declares them.
 */
#include "platform.h"

#include <ps5platform/klog.h>

#include <pthread.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

int sceSystemServiceHideSplashScreen(void);
int sceSystemServiceLoadExec(const char *path, const char *const *argv);
int sceUserServiceInitialize(const void *params);
int sceUserServiceGetInitialUser(int32_t *user_id);
/* The signed-in users, at most four; unused entries are -1. */
struct user_list {
   int32_t user_id[4];
};
int sceUserServiceGetLoginUserIdList(struct user_list *list);
int scePadInit(void);
int scePadOpen(int32_t user_id, int32_t port_type, int32_t index, const void *params);
int scePadGetHandle(int32_t user_id, int32_t port_type, int32_t index);
int scePadRead(int32_t handle, void *samples, int32_t capacity);
int scePadClose(int32_t handle);
int scePadSetVibrationMode(int32_t handle, int32_t mode);
int scePadSetVibration(int32_t handle, const void *vibration);
int scePadSetLightBar(int32_t handle, const void *color);
int sceAudioOutInit(void);
int sceAudioOutOpen(int32_t user, int32_t type, int32_t index, uint32_t grain, uint32_t rate,
                    uint32_t format);
int sceAudioOutOutput(int32_t port, const void *samples);
int sceAudioOutClose(int32_t port);
int sceKernelUsleep(uint32_t microseconds);

void
platform_init(const char *title_name)
{
   static char prefix[64];
   snprintf(prefix, sizeof(prefix), "[%s] ", title_name);
   const int captured = ps5_klog_capture_stderr(prefix);
   say("starts (standard error to klog: %s)", captured == 0 ? "yes" : "no");
   /* The shell's splash covers the title's frames until it is dismissed. */
   say("splash dismissed: %d", sceSystemServiceHideSplashScreen());
}

void
say(const char *format, ...)
{
   va_list args;
   va_start(args, format);
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

/* The title ends by asking the shell to close it. exit() and returning from
 * _start both kill it, and the console then reports a crash over a run that
 * worked. The CRT calls this after main returns; the shell closes the title
 * asynchronously, so it must wait and never return. */
void
catchReturnFromMain(int status)
{
   say("exit status %d: asking the shell to close the title", status);
   fflush(NULL);
   (void)sceSystemServiceLoadExec("exit", NULL);
   for (;;)
      sceKernelUsleep(100000);
}

/* ----------------------------------------------------------------- the pad */

/* A finger on the touch pad as ScePadData has it. */
struct pad_touch {
   uint16_t x, y; /* 0..1919, 0..1079 */
   uint8_t id;
   uint8_t reserved[3];
};

/* One sample as the console writes it: 120 bytes, the layout PS5_vkQuake and
 * ProsperoLight read (ScePadData: the motion sensors, then touchData). */
struct pad_sample {
   uint32_t buttons;
   uint8_t left_x, left_y, right_x, right_y, l2, r2;
   uint8_t analog_padding[2];
   float orientation[4];
   float acceleration[3];
   float angular_velocity[3];
   uint8_t touch_count; /* touchData.touchNum */
   uint8_t touch_reserved[7];
   struct pad_touch touch[2];
   int32_t connected;
   uint64_t timestamp_us;
   uint8_t extension[16];
   uint8_t connected_count;
   uint8_t remaining[15];
};
_Static_assert(sizeof(struct pad_sample) == 120, "the console's pad samples are 120 bytes");
_Static_assert(offsetof(struct pad_sample, connected) == 0x4c, "connection state sits at 0x4c");
_Static_assert(offsetof(struct pad_sample, timestamp_us) == 0x50, "the timestamp sits at 0x50");
_Static_assert(offsetof(struct pad_sample, touch_count) == 0x34, "touchData sits at 0x34");
_Static_assert(offsetof(struct pad_sample, touch) == 0x3c, "the fingers sit at 0x3c");

#define PAD_TOUCH_W 1920.0f
#define PAD_TOUCH_H 1080.0f

/* The two-motor rumble, as PS5_vkQuake and ProsperoLight drive it (the other
 * mode is the DualSense's haptics). */
#define PAD_VIBRATION_COMPATIBLE 2

/* A player: a signed-in user's controller, PS5_ProsperoEden's way. The console
 * pairs each controller with a user, and scePadOpen opens a user's. Player 0 is
 * the user who started the title and stays theirs; about once a second
 * pad_poll reads the signed-in users again, a new user's controller takes the
 * first free player, and a player is let go when their user signs out. */
struct pad_player_state {
   int32_t user;
   int32_t handle;
   bool connected;
   struct pad_sample last;
   struct pad_reading taken[64];
   int taken_count;
};

static struct pad_player_state pad_states[PAD_PLAYERS];
static bool pad_service;
static double pad_next_scan;
/* A user whose controller would not open, said once rather than every scan. */
static int32_t pad_unopened = -1;
#define PAD_SCAN_SECONDS 1.0

/* A player's state while they have a controller, else NULL (and always NULL
 * before pad_open has set the players up). */
static struct pad_player_state *
pad_state(int player)
{
   if (!pad_service || player < 0 || player >= PAD_PLAYERS || pad_states[player].handle < 0)
      return NULL;
   return &pad_states[player];
}

static void
pad_rest(struct pad_player_state *state)
{
   memset(state, 0, sizeof(*state));
   state->user = state->handle = -1;
   state->last.left_x = state->last.left_y = state->last.right_x = state->last.right_y = 128;
}

static bool
pad_player_open(int player, int32_t user, int attempts)
{
   struct pad_player_state *state = &pad_states[player];
   pad_rest(state);
   /* A title can start before the pad service has published the device. */
   int32_t handle = -1;
   for (int attempt = 0; attempt < attempts && handle < 0; attempt++) {
      handle = scePadOpen(user, 0, 0, NULL);
      if (handle < 0 && attempt + 1 < attempts)
         sceKernelUsleep(100000);
   }
   /* A handle this process already holds for the user is reused. */
   if (handle < 0)
      handle = scePadGetHandle(user, 0, 0);
   if (handle < 0) {
      if (user != pad_unopened)
         say("pad: scePadOpen failed for user %d (player %d)", (int)user, player);
      pad_unopened = user;
      return false;
   }
   state->user = user;
   state->handle = handle;
   say("pad: player %d opened for user %d, rumble mode %d", player, (int)user,
       scePadSetVibrationMode(handle, PAD_VIBRATION_COMPATIBLE));
   return true;
}

static void
pad_player_close(int player, const char *why)
{
   struct pad_player_state *state = &pad_states[player];
   if (state->handle < 0)
      return;
   say("pad: player %d let go (user %d %s), close %d", player, (int)state->user, why,
       scePadClose(state->handle));
   pad_rest(state);
}

/* The signed-in users again: player 0 stays; a player whose user signed out is
 * let go; a new user's controller takes the first free player. */
static void
pad_scan(void)
{
   struct user_list list = {{-1, -1, -1, -1}};
   if (sceUserServiceGetLoginUserIdList(&list) < 0)
      return;
   for (int player = 1; player < PAD_PLAYERS; player++) {
      if (pad_states[player].handle < 0)
         continue;
      bool signed_in = false;
      for (int i = 0; i < 4; i++)
         signed_in |= list.user_id[i] >= 0 && list.user_id[i] == pad_states[player].user;
      if (!signed_in)
         pad_player_close(player, "signed out");
   }
   for (int i = 0; i < 4; i++) {
      const int32_t user = list.user_id[i];
      bool known = user < 0;
      for (int player = 0; player < PAD_PLAYERS && !known; player++)
         known = pad_states[player].handle >= 0 && pad_states[player].user == user;
      for (int player = 0; player < PAD_PLAYERS && !known; player++) {
         if (pad_states[player].handle < 0) {
            (void)pad_player_open(player, user, 1);
            break;
         }
      }
   }
}

bool
pad_open(void)
{
   for (int player = 0; player < PAD_PLAYERS; player++)
      pad_rest(&pad_states[player]);
   (void)sceUserServiceInitialize(NULL);
   int32_t user = -1;
   if (sceUserServiceGetInitialUser(&user) < 0 || scePadInit() < 0) {
      say("pad: no user or no pad service");
      return false;
   }
   pad_service = true;
   const bool opened = pad_player_open(0, user, 10);
   pad_scan();
   pad_next_scan = now_seconds() + PAD_SCAN_SECONDS;
   return opened;
}

static float
stick(uint8_t value)
{
   const float v = ((float)value - 128.0f) / 127.0f;
   const float dead = 0.12f;
   if (v > -dead && v < dead)
      return 0.0f;
   return v > 0 ? (v - dead) / (1.0f - dead) : (v + dead) / (1.0f - dead);
}

static void
pad_fill(const struct pad_player_state *state, struct pad *pad)
{
   const uint32_t before = pad->held;
   pad->held = state->handle >= 0 ? state->last.buttons : 0;
   pad->pressed = pad->held & ~before;
   pad->left_x = stick(state->last.left_x);
   pad->left_y = stick(state->last.left_y);
   pad->right_x = stick(state->last.right_x);
   pad->right_y = stick(state->last.right_y);
   pad->l2 = state->last.l2 / 255.0f;
   pad->r2 = state->last.r2 / 255.0f;
   const int touches = state->handle >= 0 ? state->last.touch_count : 0;
   pad->touch_count = touches > 2 ? 2 : touches;
   if (pad->touch_count > 0) {
      const float x = state->last.touch[0].x / (PAD_TOUCH_W - 1.0f);
      const float y = state->last.touch[0].y / (PAD_TOUCH_H - 1.0f);
      pad->touch_x = x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x;
      pad->touch_y = y < 0.0f ? 0.0f : y > 1.0f ? 1.0f : y;
   }
}

/* L2 and R2 as buttons once the trigger is past PAD_TRIGGER_PRESS, as well as
 * by the bits the console sets: a light pull is a press too. */
static uint32_t
pad_buttons(const struct pad_sample *s)
{
   uint32_t buttons = s->buttons;
   if (buttons & PAD_INTERCEPTED)
      return buttons;
   if (s->l2 > PAD_TRIGGER_PRESS)
      buttons |= PAD_L2;
   if (s->r2 > PAD_TRIGGER_PRESS)
      buttons |= PAD_R2;
   return buttons;
}

void
pad_poll(struct pad *pad)
{
   if (pad_service && now_seconds() >= pad_next_scan) {
      pad_scan();
      pad_next_scan = now_seconds() + PAD_SCAN_SECONDS;
   }
   static struct pad_sample samples[64];
   for (int player = 0; player < PAD_PLAYERS; player++) {
      struct pad_player_state *state = pad_state(player);
      if (state == NULL)
         continue;
      state->taken_count = 0;
      const int count = scePadRead(state->handle, samples, 64);
      if (count < 0)
         state->connected = false;
      for (int i = 0; i < count && i < 64; i++) {
         const struct pad_sample *s = &samples[i];
         const uint32_t buttons = pad_buttons(s);
         state->taken[state->taken_count++] = (struct pad_reading){
            buttons, s->left_x, s->left_y, s->right_x, s->right_y, s->l2, s->r2,
            s->connected != 0, s->timestamp_us};
         state->connected = s->connected != 0;
         if (s->connected && !(s->buttons & PAD_INTERCEPTED)) {
            state->last = *s;
            state->last.buttons = buttons;
         }
      }
   }
   (void)pad_player(0, pad);
}

uint32_t
pad_players(void)
{
   uint32_t players = 0;
   for (int player = 0; player < PAD_PLAYERS; player++) {
      const struct pad_player_state *state = pad_state(player);
      if (state != NULL && state->connected)
         players |= 1u << player;
   }
   return players;
}

bool
pad_player(int player, struct pad *pad)
{
   const struct pad_player_state *state = pad_state(player);
   if (state == NULL) {
      struct pad_player_state rest;
      pad_rest(&rest);
      pad_fill(&rest, pad);
      return false;
   }
   pad_fill(state, pad);
   return true;
}

int
pad_player_readings(int player, const struct pad_reading **readings)
{
   const struct pad_player_state *state = pad_state(player);
   *readings = state != NULL ? state->taken : NULL;
   return state != NULL ? state->taken_count : 0;
}

int
pad_readings(const struct pad_reading **readings)
{
   return pad_player_readings(0, readings);
}

void
pad_player_vibrate(int player, float large, float small)
{
   const struct pad_player_state *state = pad_state(player);
   if (state == NULL)
      return;
   const float l = large < 0.0f ? 0.0f : large > 1.0f ? 1.0f : large;
   const float s = small < 0.0f ? 0.0f : small > 1.0f ? 1.0f : small;
   const uint8_t motors[2] = {(uint8_t)(l * 255.0f), (uint8_t)(s * 255.0f)};
   (void)scePadSetVibration(state->handle, motors);
}

void
pad_vibrate(float large, float small)
{
   pad_player_vibrate(0, large, small);
}

void
pad_player_light_bar(int player, uint8_t r, uint8_t g, uint8_t b)
{
   const struct pad_player_state *state = pad_state(player);
   if (state == NULL)
      return;
   const uint8_t color[4] = {r, g, b, 0};
   (void)scePadSetLightBar(state->handle, color);
}

void
pad_light_bar(uint8_t r, uint8_t g, uint8_t b)
{
   pad_player_light_bar(0, r, g, b);
}

/* --------------------------------------------------------------- the sound */

#define AUDIO_GRAIN 256
#define AUDIO_ALREADY_INITIALISED ((int)0x8026000e)

static int32_t audio_port = -1;
static pthread_t audio_thread;
static atomic_bool audio_running;
static audio_fill_fn audio_fill;
static void *audio_user;

static void *
audio_main(void *unused)
{
   (void)unused;
   static int16_t grain[AUDIO_GRAIN * 2] __attribute__((aligned(64)));
   unsigned errors = 0;
   while (atomic_load(&audio_running)) {
      audio_fill(grain, AUDIO_GRAIN, audio_user);
      /* Blocks for one grain: this is what paces the thread. An output that
       * stops taking grains is said once, not left to stall the program. */
      if (sceAudioOutOutput(audio_port, grain) < 0) {
         if (errors++ == 0)
            say("audio: sceAudioOutOutput failed");
         sceKernelUsleep(5000);
      }
   }
   (void)sceAudioOutOutput(audio_port, NULL); /* drain the grain still queued */
   return NULL;
}

bool
audio_start(audio_fill_fn fill, void *user)
{
   if (audio_port >= 0 || fill == NULL)
      return false;
   const int init = sceAudioOutInit();
   if (init < 0 && init != AUDIO_ALREADY_INITIALISED) {
      say("audio: sceAudioOutInit 0x%08x", (unsigned)init);
      return false;
   }
   /* The system user's main port: 256 frames, 48 kHz, 16-bit stereo. */
   audio_port = sceAudioOutOpen(0xff, 0, 0, AUDIO_GRAIN, 48000, 1);
   if (audio_port < 0) {
      say("audio: sceAudioOutOpen 0x%08x", (unsigned)audio_port);
      audio_port = -1;
      return false;
   }
   audio_fill = fill;
   audio_user = user;
   atomic_store(&audio_running, true);
   if (pthread_create(&audio_thread, NULL, audio_main, NULL) != 0) {
      say("audio: no thread");
      atomic_store(&audio_running, false);
      (void)sceAudioOutClose(audio_port);
      audio_port = -1;
      return false;
   }
   say("audio: 48 kHz stereo on port %d", (int)audio_port);
   return true;
}

void
audio_stop(void)
{
   if (audio_port < 0)
      return;
   atomic_store(&audio_running, false);
   pthread_join(audio_thread, NULL);
   (void)sceAudioOutClose(audio_port);
   audio_port = -1;
}

/* The console's keyboard. libSceImeDialog is not among the modules a title
 * starts with, so it is loaded when first asked for and its calls are looked
 * up by name: a console that has no such module costs a false, not a title
 * that will not start. The structures are the IME dialog's as PS4 and PS5
 * share them; its text is UTF-16. */
int sceKernelLoadStartModule(const char *path, size_t argc, const void *argv, uint32_t flags,
                             void *option, int *result);
int sceKernelDlsym(int handle, const char *symbol, void **address);

struct ime_dialog_param {
   int32_t user_id;
   int32_t type;                 /* 0: the default keyboard */
   uint64_t supported_languages; /* 0: all */
   int32_t enter_label;
   int32_t input_method;
   void *filter;
   uint32_t option;
   uint32_t max_text_length;
   uint16_t *input_text_buffer;
   float pos_x, pos_y;
   int32_t horizontal_alignment;
   int32_t vertical_alignment;
   const uint16_t *placeholder;
   const uint16_t *title;
   int8_t reserved[16];
};
struct ime_dialog_result {
   int32_t end_status; /* 0 entered, 1 cancelled, 2 aborted */
   int8_t reserved[12];
};
#define IME_OPTION_PASSWORD 0x00000004u
#define IME_OPTION_NO_AUTO_CAPITALIZATION 0x00000002u
#define IME_OPTION_NO_LEARNING 0x00000040u
#define IME_DIALOG_FINISHED 2

static struct {
   bool looked_up, available, open;
   int (*init)(const struct ime_dialog_param *param, void *extended);
   int (*get_status)(void);
   int (*get_result)(struct ime_dialog_result *result);
   int (*term)(void);
   uint16_t text[256];
   uint16_t title[64];
} ime;

static size_t
utf8_to_utf16(const char *in, uint16_t *out, size_t capacity)
{
   size_t n = 0;
   const unsigned char *s = (const unsigned char *)(in ? in : "");
   while (*s && n + 2 < capacity) {
      uint32_t cp = *s++;
      int more = cp >= 0xf0 ? 3 : cp >= 0xe0 ? 2 : cp >= 0xc0 ? 1 : 0;
      if (more)
         cp &= 0x3f >> more;
      while (more-- > 0 && (*s & 0xc0) == 0x80)
         cp = (cp << 6) | (*s++ & 0x3f);
      if (cp >= 0x10000) {
         cp -= 0x10000;
         out[n++] = (uint16_t)(0xd800 | (cp >> 10));
         out[n++] = (uint16_t)(0xdc00 | (cp & 0x3ff));
      } else
         out[n++] = (uint16_t)cp;
   }
   out[n] = 0;
   return n;
}

static void
utf16_to_utf8(const uint16_t *in, char *out, size_t size)
{
   size_t n = 0;
   while (*in && n + 5 < size) {
      uint32_t cp = *in++;
      if (cp >= 0xd800 && cp < 0xdc00 && *in >= 0xdc00 && *in < 0xe000)
         cp = 0x10000 + ((cp - 0xd800) << 10) + (*in++ - 0xdc00);
      if (cp < 0x80)
         out[n++] = (char)cp;
      else if (cp < 0x800) {
         out[n++] = (char)(0xc0 | (cp >> 6));
         out[n++] = (char)(0x80 | (cp & 0x3f));
      } else if (cp < 0x10000) {
         out[n++] = (char)(0xe0 | (cp >> 12));
         out[n++] = (char)(0x80 | ((cp >> 6) & 0x3f));
         out[n++] = (char)(0x80 | (cp & 0x3f));
      } else {
         out[n++] = (char)(0xf0 | (cp >> 18));
         out[n++] = (char)(0x80 | ((cp >> 12) & 0x3f));
         out[n++] = (char)(0x80 | ((cp >> 6) & 0x3f));
         out[n++] = (char)(0x80 | (cp & 0x3f));
      }
   }
   if (size)
      out[n] = 0;
}

static bool
ime_look_up(void)
{
   if (ime.looked_up)
      return ime.available;
   ime.looked_up = true;
   const int module = sceKernelLoadStartModule("/system/common/lib/libSceImeDialog.sprx", 0, NULL, 0, NULL, NULL);
   if (module < 0) {
      say("keyboard: no libSceImeDialog (0x%08x)", (unsigned)module);
      return false;
   }
   void *address[4] = { NULL, NULL, NULL, NULL };
   static const char *const names[4] = { "sceImeDialogInit", "sceImeDialogGetStatus",
                                         "sceImeDialogGetResult", "sceImeDialogTerm" };
   for (int i = 0; i < 4; i++) {
      if (sceKernelDlsym(module, names[i], &address[i]) < 0 || !address[i]) {
         say("keyboard: libSceImeDialog has no %s", names[i]);
         return false;
      }
   }
   ime.init = (int (*)(const struct ime_dialog_param *, void *))address[0];
   ime.get_status = (int (*)(void))address[1];
   ime.get_result = (int (*)(struct ime_dialog_result *))address[2];
   ime.term = (int (*)(void))address[3];
   ime.available = true;
   return true;
}

bool
keyboard_open(const char *title, const char *text, bool password, int max_chars)
{
   if (ime.open || !ime_look_up())
      return false;
   struct ime_dialog_param param;
   memset(&param, 0, sizeof(param));
   if (sceUserServiceGetInitialUser(&param.user_id) < 0)
      return false;
   utf8_to_utf16(text, ime.text, sizeof(ime.text) / sizeof(ime.text[0]));
   utf8_to_utf16(title, ime.title, sizeof(ime.title) / sizeof(ime.title[0]));
   param.option = IME_OPTION_NO_AUTO_CAPITALIZATION | IME_OPTION_NO_LEARNING | (password ? IME_OPTION_PASSWORD : 0);
   const int capacity = (int)(sizeof(ime.text) / sizeof(ime.text[0])) - 1;
   param.max_text_length = (uint32_t)(max_chars > 0 && max_chars < capacity ? max_chars : capacity);
   param.input_text_buffer = ime.text;
   param.title = ime.title;
   param.pos_x = 1920.0f / 2.0f;
   param.pos_y = 1080.0f / 2.0f;
   param.horizontal_alignment = 1; /* centred on the position */
   param.vertical_alignment = 1;
   const int result = ime.init(&param, NULL);
   if (result < 0) {
      say("keyboard: sceImeDialogInit 0x%08x", (unsigned)result);
      return false;
   }
   ime.open = true;
   return true;
}

int
keyboard_poll(char *out, size_t size)
{
   if (!ime.open)
      return -1;
   if (ime.get_status() != IME_DIALOG_FINISHED)
      return 0;
   struct ime_dialog_result result;
   memset(&result, 0, sizeof(result));
   const int got = ime.get_result(&result);
   ime.term();
   ime.open = false;
   if (got < 0 || result.end_status != 0)
      return -1;
   utf16_to_utf8(ime.text, out, size);
   return 1;
}

/* The microphone (the DualSense's, or a headset's). libSceAudioIn is, like the
 * IME dialog, not among the modules a title starts with: it is loaded when
 * first asked for and its calls are looked up by name, so a console without it
 * costs a false. A thread of its own reads grains of 16 kHz mono 16-bit
 * samples into a buffer of MIC_MAX_SECONDS; mic_get_samples copies it out.
 * The call set is the PS4/PS5 one: sceAudioInOpen(user, type, index, grain,
 * freq, param), sceAudioInInput(port, dest) (blocks for one grain) and
 * sceAudioInClose. "sceAudioInRead" is tried as a second name for the read. */
#define MIC_RATE 16000
#define MIC_GRAIN 256
#define MIC_MAX_SECONDS 20
#define MIC_MAX_SAMPLES (MIC_RATE * MIC_MAX_SECONDS)

static struct {
   bool looked_up, available;
   int (*open)(int32_t user, uint32_t type, uint32_t index, uint32_t grain, uint32_t freq, uint32_t param);
   int (*input)(int32_t port, void *dest);
   int (*close)(int32_t port);
   int32_t port;
   pthread_t thread;
   bool running_thread;
   atomic_bool recording;
   atomic_uint count;
} mic = { .port = -1 };
static int16_t mic_buffer[MIC_MAX_SAMPLES];

static bool
mic_look_up(void)
{
   if (mic.looked_up)
      return mic.available;
   mic.looked_up = true;
   const int module = sceKernelLoadStartModule("/system/common/lib/libSceAudioIn.sprx", 0, NULL, 0, NULL, NULL);
   if (module < 0) {
      say("mic: no libSceAudioIn (0x%08x)", (unsigned)module);
      return false;
   }
   void *open = NULL, *input = NULL, *close = NULL;
   if (sceKernelDlsym(module, "sceAudioInOpen", &open) < 0 || !open ||
       sceKernelDlsym(module, "sceAudioInClose", &close) < 0 || !close) {
      say("mic: libSceAudioIn has no Open/Close");
      return false;
   }
   if ((sceKernelDlsym(module, "sceAudioInInput", &input) < 0 || !input) &&
       (sceKernelDlsym(module, "sceAudioInRead", &input) < 0 || !input)) {
      say("mic: libSceAudioIn has no sceAudioInInput / sceAudioInRead");
      return false;
   }
   mic.open = (int (*)(int32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t))open;
   mic.input = (int (*)(int32_t, void *))input;
   mic.close = (int (*)(int32_t))close;
   mic.available = true;
   return true;
}

static void *
mic_main(void *unused)
{
   (void)unused;
   static int16_t grain[MIC_GRAIN] __attribute__((aligned(64)));
   while (atomic_load(&mic.recording)) {
      if (mic.input(mic.port, grain) < 0) {
         say("mic: read failed");
         break;
      }
      unsigned at = atomic_load(&mic.count);
      if (at + MIC_GRAIN > MIC_MAX_SAMPLES)
         break; /* the buffer is full: what there is, is kept */
      memcpy(&mic_buffer[at], grain, sizeof(grain));
      atomic_store(&mic.count, at + MIC_GRAIN);
   }
   atomic_store(&mic.recording, false);
   return NULL;
}

bool
mic_start_recording(void)
{
   if (mic.running_thread || !mic_look_up())
      return false;
   int32_t user = 0;
   if (sceUserServiceGetInitialUser(&user) < 0)
      return false;
   /* type 0: voice chat (the pad's microphone), index 0, S16 mono (param 0) */
   mic.port = mic.open(user, 0, 0, MIC_GRAIN, MIC_RATE, 0);
   if (mic.port < 0) {
      say("mic: sceAudioInOpen 0x%08x", (unsigned)mic.port);
      mic.port = -1;
      return false;
   }
   atomic_store(&mic.count, 0);
   atomic_store(&mic.recording, true);
   if (pthread_create(&mic.thread, NULL, mic_main, NULL) != 0) {
      atomic_store(&mic.recording, false);
      (void)mic.close(mic.port);
      mic.port = -1;
      return false;
   }
   mic.running_thread = true;
   return true;
}

void
mic_stop_recording(void)
{
   if (!mic.running_thread)
      return;
   atomic_store(&mic.recording, false);
   pthread_join(mic.thread, NULL);
   mic.running_thread = false;
   (void)mic.close(mic.port);
   mic.port = -1;
}

bool
mic_is_recording(void)
{
   return mic.running_thread && atomic_load(&mic.recording);
}

size_t
mic_get_samples(int16_t *out, size_t max_samples)
{
   const size_t have = atomic_load(&mic.count);
   if (out && max_samples) {
      const size_t n = have < max_samples ? have : max_samples;
      memcpy(out, mic_buffer, n * sizeof(int16_t));
   }
   return have;
}

int
mic_sample_rate(void)
{
   return MIC_RATE;
}
