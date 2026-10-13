/*
 * HdRezka - step trace + crash handler (see crash_log.h).
 */
#include "crash_log.h"

#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ucontext.h>
#include <unistd.h>

extern int sceKernelSendNotificationRequest(int device, void *request, size_t size, int blocking);
extern int sceKernelUsleep(unsigned int microseconds);

#define LOG_PATH "/download0/hdrezka.log"

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static char g_last_step[256] = "start";

static struct {
    unsigned char reserved[45];
    char message[3075];
} g_notify;

static void append_raw(const char *text, size_t len)
{
    const int fd = open(LOG_PATH, O_WRONLY | O_CREAT | O_APPEND, 0666);
    if (fd < 0)
        return;
    (void)write(fd, text, len);
    (void)fsync(fd);
    (void)close(fd);
}

void crash_log_notify(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    memset(&g_notify, 0, sizeof(g_notify));
    vsnprintf(g_notify.message, sizeof(g_notify.message), fmt, ap);
    va_end(ap);
    (void)sceKernelSendNotificationRequest(0, &g_notify, sizeof(g_notify), 0);
}

void crash_log_step(const char *fmt, ...)
{
    char line[320];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof(line) - 2, fmt, ap);
    va_end(ap);
    if (n < 0)
        return;
    if ((size_t)n > sizeof(line) - 2)
        n = (int)sizeof(line) - 2;

    pthread_mutex_lock(&g_lock);
    snprintf(g_last_step, sizeof(g_last_step), "%s", line);
    line[n++] = '\n';
    append_raw(line, (size_t)n);
    pthread_mutex_unlock(&g_lock);
}

static const char *signal_name(int sig)
{
    switch (sig) {
    case SIGSEGV: return "SIGSEGV";
    case SIGBUS: return "SIGBUS";
    case SIGILL: return "SIGILL";
    case SIGFPE: return "SIGFPE";
    case SIGABRT: return "SIGABRT";
    case SIGPIPE: return "SIGPIPE";
    case SIGTRAP: return "SIGTRAP";
    case SIGSYS: return "SIGSYS";
    default: return "SIG?";
    }
}

static void on_fatal(int sig, siginfo_t *info, void *context)
{
    const ucontext_t *uc = (const ucontext_t *)context;
    const uintptr_t rip = uc ? (uintptr_t)uc->uc_mcontext.mc_rip : 0;
    const uintptr_t rsp = uc ? (uintptr_t)uc->uc_mcontext.mc_rsp : 0;
    const uintptr_t addr = info ? (uintptr_t)info->si_addr : 0;
    /* Load bias: runtime address of a known function vs its link address
     * (resolved with nm on build/llvm-pie.elf). */
    const uintptr_t anchor = (uintptr_t)&crash_log_install;

    char line[512];
    const int n = snprintf(line, sizeof(line),
                           "CRASH %s(%d) code=%d rip=0x%lx rsp=0x%lx addr=0x%lx "
                           "anchor(crash_log_install)=0x%lx thread=%p last=[%s]\n",
                           signal_name(sig), sig, info ? info->si_code : 0,
                           (unsigned long)rip, (unsigned long)rsp, (unsigned long)addr,
                           (unsigned long)anchor, (void *)pthread_self(), g_last_step);
    if (n > 0)
        append_raw(line, (size_t)n < sizeof(line) ? (size_t)n : sizeof(line) - 1);

    crash_log_notify("HdRezka %s rip=0x%lx addr=0x%lx\nlast: %s", signal_name(sig),
                     (unsigned long)rip, (unsigned long)addr, g_last_step);
    /* Give the notification time to show, then let the system kill us. */
    (void)sceKernelUsleep(4000000);

    struct sigaction dfl;
    memset(&dfl, 0, sizeof(dfl));
    dfl.sa_handler = SIG_DFL;
    sigaction(sig, &dfl, NULL);
    raise(sig);
}

void crash_log_install(void)
{
    /* Fresh log per launch; keep the previous one for comparison. */
    (void)rename(LOG_PATH, "/download0/hdrezka.prev.log");

    /* Alternate stack so a stack overflow can still be reported. */
    static unsigned char alt_stack[64 * 1024];
    stack_t ss;
    memset(&ss, 0, sizeof(ss));
    ss.ss_sp = alt_stack;
    ss.ss_size = sizeof(alt_stack);
    (void)sigaltstack(&ss, NULL);

    static const int fatal[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGTRAP, SIGSYS};
    for (size_t i = 0; i < sizeof(fatal) / sizeof(fatal[0]); ++i) {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_sigaction = on_fatal;
        sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
        sigemptyset(&sa.sa_mask);
        (void)sigaction(fatal[i], &sa, NULL);
    }

    struct sigaction ign;
    memset(&ign, 0, sizeof(ign));
    ign.sa_handler = SIG_IGN;
    sigemptyset(&ign.sa_mask);
    (void)sigaction(SIGPIPE, &ign, NULL);

    crash_log_step("boot: crash handler installed, anchor=0x%lx",
                   (unsigned long)(uintptr_t)&crash_log_install);
}
