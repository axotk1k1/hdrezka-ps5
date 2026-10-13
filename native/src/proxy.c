#include "app.h"
#include "console_curl.h"

#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

struct mem {
    unsigned char *data;
    size_t len;
};

static size_t write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
    struct mem *m = userdata;
    size_t n = size * nmemb;
    unsigned char *p = realloc(m->data, m->len + n);
    if (!p) return 0;
    memcpy(p + m->len, ptr, n);
    m->data = p;
    m->len += n;
    return n;
}

static size_t header_cb(char *buffer, size_t size, size_t nitems, void *userdata) {
    proxy_result_t *out = userdata;
    size_t n = size * nitems;
    char line[1024];
    if (n >= sizeof(line)) n = sizeof(line) - 1;
    memcpy(line, buffer, n);
    line[n] = 0;
    char *colon = strchr(line, ':');
    if (!colon) return size * nitems;
    *colon = 0;
    char *val = colon + 1;
    while (*val == ' ') val++;
    char *end = val + strlen(val);
    while (end > val && (end[-1] == '\r' || end[-1] == '\n' || end[-1] == ' ')) {
        *--end = 0;
    }
    if (!strcasecmp(line, "Content-Type")) {
        snprintf(out->content_type, sizeof(out->content_type), "%s", val);
    } else if (!strcasecmp(line, "Content-Range")) {
        snprintf(out->content_range, sizeof(out->content_range), "%s", val);
    } else if (!strcasecmp(line, "Accept-Ranges")) {
        snprintf(out->accept_ranges, sizeof(out->accept_ranges), "%s", val);
    }
    return size * nitems;
}

/* Common handle setup for proxy_fetch and proxy_stream. The caller adds the
 * body/header callbacks, the timeouts and frees *hdrs_out after the transfer. */
static CURL *proxy_setup(const char *url, const char *method, const char *req_headers,
                         const void *body, size_t body_len, const char *range,
                         struct curl_slist **hdrs_out) {
    CURL *curl = curl_easy_init();
    if (!curl) return NULL;
    console_curl_setup(curl);
    struct curl_slist *hdrs = NULL;
    char referer[300];
    snprintf(referer, sizeof(referer), "Referer: %s/", g_cfg.origin);
    hdrs = curl_slist_append(hdrs, referer);
    char origin[300];
    snprintf(origin, sizeof(origin), "Origin: %s", g_cfg.origin);
    hdrs = curl_slist_append(hdrs, origin);
    hdrs = curl_slist_append(hdrs, "Accept: */*");
    hdrs = curl_slist_append(hdrs, "Accept-Language: ru-RU,ru;q=0.9,en;q=0.8");
    hdrs = curl_slist_append(hdrs, "X-Hdrezka-Android-App-V2: 1");
    hdrs = curl_slist_append(hdrs, "X-Hdrezka-Android-App-Version-V2: 0.9.15 \xce\xb2");
    if (req_headers && req_headers[0]) {
        hdrs = curl_slist_append(hdrs, req_headers);
    }
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
    /* Always OkHttp — saved config may still hold a browser UA (site 403). */
    curl_easy_setopt(curl, CURLOPT_USERAGENT, APP_UA);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_COOKIEFILE, g_cfg.cookie_file);
    curl_easy_setopt(curl, CURLOPT_COOKIEJAR, g_cfg.cookie_file);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    if (g_cfg.extra_cookies[0]) {
        curl_easy_setopt(curl, CURLOPT_COOKIE, g_cfg.extra_cookies);
    }
    if (range && range[0]) curl_easy_setopt(curl, CURLOPT_RANGE, range);
    if (method && strcasecmp(method, "GET")) {
        curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method);
    }
    if (body && body_len) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)body_len);
    }
    *hdrs_out = hdrs;
    return curl;
}

int proxy_fetch(const char *url, const char *method, const char *req_headers,
                const void *body, size_t body_len, const char *range,
                proxy_result_t *out) {
    memset(out, 0, sizeof(*out));
    snprintf(out->content_type, sizeof(out->content_type), "application/octet-stream");
    struct curl_slist *hdrs = NULL;
    CURL *curl = proxy_setup(url, method, req_headers, body, body_len, range, &hdrs);
    if (!curl) return -1;
    struct mem mem = {0};
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &mem);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_cb);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, out);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
    CURLcode rc = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    char *eff = NULL;
    curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &eff);
    if (eff) snprintf(out->final_url, sizeof(out->final_url), "%s", eff);
    curl_slist_free_all(hdrs);
    curl_easy_cleanup(curl);
    if (rc != CURLE_OK) {
        free(mem.data);
        return -1;
    }
    out->data = mem.data;
    out->len = mem.len;
    out->status = (int)status;
    out->content_length = (long)mem.len;
    return 0;
}

struct stream_state {
    CURL *curl;
    proxy_sink_t *sink;
    proxy_result_t head;
    int began;
};

/* Hands the response head to the sink once the first body bytes arrive, so the
 * status and headers from the upstream are known. */
static int stream_begin(struct stream_state *s) {
    long status = 0;
    curl_easy_getinfo(s->curl, CURLINFO_RESPONSE_CODE, &status);
    curl_off_t len = -1;
    curl_easy_getinfo(s->curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &len);
    char *eff = NULL;
    curl_easy_getinfo(s->curl, CURLINFO_EFFECTIVE_URL, &eff);
    if (eff) snprintf(s->head.final_url, sizeof(s->head.final_url), "%s", eff);
    s->head.status = (int)status;
    s->head.content_length = (long)len;
    s->began = 1;
    return s->sink->begin(s->sink->ctx, &s->head);
}

static size_t stream_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
    struct stream_state *s = userdata;
    size_t n = size * nmemb;
    if (!s->began && stream_begin(s) < 0) return 0;
    if (s->sink->write(s->sink->ctx, ptr, n) < 0) return 0;
    return n;
}

int proxy_stream(const char *url, const char *range, proxy_sink_t *sink) {
    struct curl_slist *hdrs = NULL;
    CURL *curl = proxy_setup(url, "GET", NULL, NULL, 0, range, &hdrs);
    if (!curl) return -1;
    struct stream_state s;
    memset(&s, 0, sizeof(s));
    s.curl = curl;
    s.sink = sink;
    snprintf(s.head.content_type, sizeof(s.head.content_type), "application/octet-stream");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, stream_write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_cb);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &s.head);
    /* No total timeout: a film runs for hours. Abort only on connect failure
     * or when the transfer stalls. */
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 30L);
    CURLcode rc = curl_easy_perform(curl);
    int ret = 0;
    if (rc != CURLE_OK && !s.began) {
        ret = -1;
    } else if (!s.began) {
        /* Empty body: the head still has to go out. */
        if (stream_begin(&s) < 0) ret = -2;
    } else if (rc != CURLE_OK) {
        ret = -2;
    }
    curl_slist_free_all(hdrs);
    curl_easy_cleanup(curl);
    return ret;
}

void proxy_result_free(proxy_result_t *out) {
    if (!out) return;
    free(out->data);
    out->data = NULL;
    out->len = 0;
}
