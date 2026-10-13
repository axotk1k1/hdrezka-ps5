#ifndef HDREZKA_APP_H
#define HDREZKA_APP_H

#include <stddef.h>

#define APP_PORT 34178
#define APP_NAME "HdRezka PS5"
#define DEFAULT_ORIGIN "https://hdrezka.ag"
/* Site returns 403 for browser UAs; phone app uses OkHttp. */
#define DEFAULT_UA "okhttp/4.12.0"
#define APP_UA DEFAULT_UA

typedef struct {
    char origin[256];
    char user_agent[256];
    char extra_cookies[4096];
    char www_dir[512];
    char data_dir[512];
    char cookie_file[512];
    char config_file[512];
} app_config_t;

extern app_config_t g_cfg;

void log_msg(const char *fmt, ...);
int  config_init(const char *root);
int  config_load(void);
int  config_save(void);
int  http_server_run(int port);

typedef struct {
    unsigned char *data;
    size_t len;
    int status;
    char content_type[128];
    char final_url[1024];
    char content_range[128];
    char accept_ranges[64];
    long content_length;
} proxy_result_t;

int proxy_fetch(const char *url, const char *method, const char *req_headers,
                const void *body, size_t body_len, const char *range,
                proxy_result_t *out);
void proxy_result_free(proxy_result_t *out);

/* Receives a streamed response: begin() gets the upstream status and headers
 * (content_length -1 if unknown), write() gets body chunks. Both return 0 on
 * success and -1 when the client is gone. */
typedef struct {
    int (*begin)(void *ctx, const proxy_result_t *head);
    int (*write)(void *ctx, const void *data, size_t len);
    void *ctx;
} proxy_sink_t;

/* 0 on success; -1 when the upstream failed before anything was sent to the
 * sink; -2 when the transfer broke or the client left mid-stream. */
int proxy_stream(const char *url, const char *range, proxy_sink_t *sink);

#endif
