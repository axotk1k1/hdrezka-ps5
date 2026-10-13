/*
 * HdRezka - step trace + crash handler.
 *
 * Every milestone is appended to /download0/hdrezka.log (fsync'd), so the
 * last line survives the CE-108255-1 kill. A fatal signal writes RIP/RSP,
 * the fault address and the last step, then shows a notification.
 */
#ifndef HDREZKA_CRASH_LOG_H
#define HDREZKA_CRASH_LOG_H

#ifdef __cplusplus
extern "C" {
#endif

void crash_log_install(void);
void crash_log_step(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void crash_log_notify(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#ifdef __cplusplus
}
#endif

#endif
