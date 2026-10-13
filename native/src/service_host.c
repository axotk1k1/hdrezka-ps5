/*
 * HdRezka - embed payload HTTP server inside the native title process.
 */
#include "service_host.h"

#include "app.h"

#include <curl/curl.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

void log_msg(const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    (void)buf;
}

static int g_port = APP_PORT;
static char g_www_path[512] = "WWW NOT SET";

const char *hdrezka_www_path(void)
{
    return g_www_path;
}

static void *server_thread(void *arg)
{
    (void)arg;
    (void)http_server_run(g_port);
    return NULL;
}

static int www_ready(const char *dir)
{
    char path[600];
    snprintf(path, sizeof(path), "%s/index.html", dir);
    /* Prefer open() over access(): some sandboxes lie about R_OK. */
    const int fd = open(path, O_RDONLY);
    if (fd < 0)
        return 0;
    (void)close(fd);
    return 1;
}

static int pick_www_dir(char *out, size_t out_sz)
{
    static const char *const candidates[] = {
        "/app0/assets/www", /* always packaged by boilerplate assets/ */
        "/app0/www",
        "/data/homebrew/PPSA99380/www",
        "/data/homebrew/PPSA99380/assets/www",
        "/mnt/usb0/homebrew/PPSA99380/www",
        "/mnt/usb0/homebrew/PPSA99380/assets/www",
        "/mnt/usb1/homebrew/PPSA99380/www",
        "/mnt/usb1/homebrew/PPSA99380/assets/www",
        "/mnt/ext0/homebrew/PPSA99380/www",
        "/mnt/ext0/homebrew/PPSA99380/assets/www",
        "/mnt/ext1/homebrew/PPSA99380/www",
        "/mnt/ext1/homebrew/PPSA99380/assets/www",
        "/system_ex/app/PPSA99380/assets/www",
        "/system_ex/app/PPSA99380/www",
    };

    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i)
    {
        if (www_ready(candidates[i]))
        {
            snprintf(out, out_sz, "%s", candidates[i]);
            return 1;
        }
    }
    snprintf(out, out_sz, "%s", "NO WWW/INDEX.HTML");
    return 0;
}

#include "crash_log.h"

int hdrezka_start_embedded_server(int port)
{
    g_port = port > 0 ? port : APP_PORT;
    crash_log_step("embedded_server: starting on port %d...", g_port);

    /* A peer that closes early (WebKit aborts fetches, <video> seeks) must not
     * kill the title via SIGPIPE; covers curl/OpenSSL sockets as well. */
    {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = SIG_IGN;
        (void)sigemptyset(&sa.sa_mask);
        (void)sigaction(SIGPIPE, &sa, NULL);
    }

    if (curl_global_init(CURL_GLOBAL_DEFAULT) != 0)
    {
        snprintf(g_www_path, sizeof(g_www_path), "%s", "CURL INIT FAIL");
        crash_log_step("embedded_server: curl_global_init FAILED");
        return -1;
    }
    crash_log_step("embedded_server: curl_global_init OK");

    /* Writable title storage for cookies/config. */
    (void)config_init("/download0");
    crash_log_step("embedded_server: config_init(/download0) OK");

    if (!pick_www_dir(g_cfg.www_dir, sizeof(g_cfg.www_dir)))
    {
        snprintf(g_www_path, sizeof(g_www_path), "%s", "NO WWW/INDEX.HTML");
        crash_log_step("embedded_server: pick_www_dir FAILED");
        return -2;
    }
    snprintf(g_www_path, sizeof(g_www_path), "%s", g_cfg.www_dir);
    crash_log_step("embedded_server: pick_www_dir -> %s", g_cfg.www_dir);

    pthread_t thread;
    pthread_attr_t attr;
    if (pthread_attr_init(&attr) != 0)
    {
        crash_log_step("embedded_server: pthread_attr_init FAILED");
        return -3;
    }
    (void)pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    (void)pthread_attr_setstacksize(&attr, 512 * 1024);
    const int rc = pthread_create(&thread, &attr, server_thread, NULL);
    (void)pthread_attr_destroy(&attr);
    if (rc != 0)
    {
        crash_log_step("embedded_server: pthread_create FAILED rc=%d", rc);
        return -3;
    }
    crash_log_step("embedded_server: server thread launched");

    usleep(500000);
    crash_log_step("embedded_server: 500ms delay passed");
    return 0;
}
