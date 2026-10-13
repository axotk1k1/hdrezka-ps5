#include "app.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

/* FreeBSD values; the PS5 kernel uses the same ABI. A send() to a socket the
 * browser already closed must not raise SIGPIPE: its default action kills the
 * whole title (CE-108255-1). */
#ifndef SO_NOSIGPIPE
#define SO_NOSIGPIPE 0x0800
#endif
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0x20000
#endif

/* Client threads run libcurl + OpenSSL handshakes plus ~16 KiB of local
 * buffers; the console's default pthread stack is far too small for that. */
#define HTTP_THREAD_STACK (1024 * 1024)

static void http_socket_nosigpipe(int fd) {
    int on = 1;
    (void)setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
}

static size_t url_encode(const char *in, char *out, size_t out_sz) {
    static const char *hex = "0123456789ABCDEF";
    size_t n = 0;
    while (*in && n + 4 < out_sz) {
        unsigned char c = (unsigned char)*in++;
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out[n++] = (char)c;
        } else {
            out[n++] = '%';
            out[n++] = hex[c >> 4];
            out[n++] = hex[c & 15];
        }
    }
    out[n] = 0;
    return n;
}

#define REQ_MAX (256 * 1024)

typedef struct {
    char method[16];
    char path[2048];
    char query[2048];
    char range[128];
    char content_type[128];
    char *body;
    size_t body_len;
} request_t;

static const char *mime_of(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot) return "application/octet-stream";
    if (!strcmp(dot, ".html")) return "text/html; charset=utf-8";
    if (!strcmp(dot, ".css")) return "text/css; charset=utf-8";
    if (!strcmp(dot, ".js")) return "application/javascript; charset=utf-8";
    if (!strcmp(dot, ".json")) return "application/json; charset=utf-8";
    if (!strcmp(dot, ".svg")) return "image/svg+xml";
    if (!strcmp(dot, ".png")) return "image/png";
    if (!strcmp(dot, ".jpg") || !strcmp(dot, ".jpeg")) return "image/jpeg";
    if (!strcmp(dot, ".webp")) return "image/webp";
    return "application/octet-stream";
}

static void send_all(int fd, const void *buf, size_t n) {
    const char *p = buf;
    while (n) {
        ssize_t w = send(fd, p, n, MSG_NOSIGNAL);
        if (w <= 0) return;
        p += w;
        n -= (size_t)w;
    }
}

static void send_response(int fd, int code, const char *status, const char *ctype,
                          const char *extra, const void *body, size_t len) {
    char hdr[1024];
    int n = snprintf(hdr, sizeof(hdr),
                     "HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
                     "Connection: close\r\nCache-Control: no-store\r\n%s\r\n",
                     code, status, ctype, len, extra ? extra : "");
    send_all(fd, hdr, (size_t)n);
    if (body && len) send_all(fd, body, len);
}

static void send_json(int fd, int code, const char *json) {
    send_response(fd, code, code == 200 ? "OK" : "Error",
                  "application/json; charset=utf-8", NULL, json, strlen(json));
}

static int url_decode(char *s) {
    char *w = s, *r = s;
    while (*r) {
        if (*r == '%' && isxdigit((unsigned char)r[1]) && isxdigit((unsigned char)r[2])) {
            char tmp[3] = {r[1], r[2], 0};
            *w++ = (char)strtol(tmp, NULL, 16);
            r += 3;
        } else if (*r == '+') {
            *w++ = ' ';
            r++;
        } else {
            *w++ = *r++;
        }
    }
    *w = 0;
    return 0;
}

static const char *query_get(const char *query, const char *key, char *out, size_t out_sz) {
    char needle[128];
    snprintf(needle, sizeof(needle), "%s=", key);
    const char *p = query;
    while (p && *p) {
        if (!strncmp(p, needle, strlen(needle))) {
            p += strlen(needle);
            size_t n = 0;
            while (*p && *p != '&' && n + 1 < out_sz) out[n++] = *p++;
            out[n] = 0;
            url_decode(out);
            return out;
        }
        p = strchr(p, '&');
        if (p) p++;
    }
    if (out_sz) out[0] = 0;
    return NULL;
}

static void json_get_str(const char *src, const char *key, char *dst, size_t dst_sz) {
    char needle[128];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char *p = strstr(src ? src : "", needle);
    dst[0] = 0;
    if (!p) return;
    p = strchr(p + strlen(needle), ':');
    if (!p) return;
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\n') p++;
    if (*p != '"') return;
    p++;
    size_t n = 0;
    while (*p && *p != '"' && n + 1 < dst_sz) {
        if (*p == '\\' && p[1]) p++;
        dst[n++] = *p++;
    }
    dst[n] = 0;
}

static int safe_www_path(const char *url_path, char *out, size_t out_sz) {
    char rel[1024];
    snprintf(rel, sizeof(rel), "%s", url_path[0] == '/' ? url_path + 1 : url_path);
    if (!rel[0]) snprintf(rel, sizeof(rel), "index.html");
    if (strstr(rel, "..")) return -1;
    snprintf(out, out_sz, "%s/%s", g_cfg.www_dir, rel);
    struct stat st;
    if (stat(out, &st) == 0 && S_ISDIR(st.st_mode)) {
        snprintf(out, out_sz, "%s/%s/index.html", g_cfg.www_dir, rel);
    }
    return 0;
}

static void handle_static(int fd, const char *url_path) {
    char path[1024];
    if (safe_www_path(url_path, path, sizeof(path)) < 0) {
        send_json(fd, 403, "{\"ok\":false,\"error\":\"forbidden\"}");
        return;
    }
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        send_response(fd, 404, "Not Found", "text/plain", NULL, "not found", 9);
        return;
    }
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz < 0 || sz > 8 * 1024 * 1024) {
        fclose(fp);
        send_json(fd, 500, "{\"ok\":false}");
        return;
    }
    char *buf = malloc((size_t)sz);
    if (!buf) {
        fclose(fp);
        send_json(fd, 500, "{\"ok\":false}");
        return;
    }
    fread(buf, 1, (size_t)sz, fp);
    fclose(fp);
    send_response(fd, 200, "OK", mime_of(path), NULL, buf, (size_t)sz);
    free(buf);
}

static void absolute_url(const char *in, char *out, size_t out_sz) {
    if (!strncmp(in, "http://", 7) || !strncmp(in, "https://", 8)) {
        snprintf(out, out_sz, "%s", in);
        return;
    }
    if (in[0] == '/') snprintf(out, out_sz, "%s%s", g_cfg.origin, in);
    else snprintf(out, out_sz, "%s/%s", g_cfg.origin, in);
}

static void handle_proxy(int fd, request_t *req) {
    char url[2048] = {0};
    char method[16] = "GET";
    char extra_hdr[512] = {0};
    const void *body = NULL;
    size_t body_len = 0;
    char range[128] = {0};

    query_get(req->query, "url", url, sizeof(url));
    if (!url[0] && req->body && req->body_len) {
        json_get_str(req->body, "url", url, sizeof(url));
        json_get_str(req->body, "method", method, sizeof(method));
        if (!method[0]) snprintf(method, sizeof(method), "GET");
        if (req->content_type[0]) snprintf(extra_hdr, sizeof(extra_hdr), "Content-Type: %s", req->content_type);
        /* Reconstruct form posts: the JS sends JSON; re-POST as form if "form" present. */
        if (strstr(req->body, "\"form\"")) {
            /* Pass original JSON body through as JSON — Python server is preferred
               for form encoding. Here we still forward as application/json if needed.
               The JS always uses POST /proxy with JSON; we convert form object to
               x-www-form-urlencoded in a small helper below. */
        }
        body = req->body;
        body_len = req->body_len;
        if (!extra_hdr[0]) snprintf(extra_hdr, sizeof(extra_hdr), "Content-Type: application/json");
    }
    if (req->range[0]) snprintf(range, sizeof(range), "%s", req->range);

    if (!url[0]) {
        send_json(fd, 400, "{\"ok\":false,\"error\":\"url required\"}");
        return;
    }
    char abs[2048];
    absolute_url(url, abs, sizeof(abs));

    /* If the JSON payload contains a form object, encode it. */
    char formbuf[4096];
    formbuf[0] = 0;
    if (req->body && strstr(req->body, "\"form\"")) {
        const char *p = strstr(req->body, "\"form\"");
        p = strchr(p, '{');
        if (p) {
            p++;
            size_t used = 0;
            while (*p && *p != '}' && used + 8 < sizeof(formbuf)) {
                while (*p && *p != '"') p++;
                if (*p != '"') break;
                p++;
                char key[128]; size_t k = 0;
                while (*p && *p != '"' && k + 1 < sizeof(key)) key[k++] = *p++;
                key[k] = 0;
                if (*p == '"') p++;
                while (*p && *p != '"') p++;
                if (*p != '"') break;
                p++;
                char val[1024]; size_t v = 0;
                while (*p && *p != '"' && v + 1 < sizeof(val)) {
                    if (*p == '\\' && p[1]) p++;
                    val[v++] = *p++;
                }
                val[v] = 0;
                if (*p == '"') p++;
                char ek[256], ev[1536];
                url_encode(key, ek, sizeof(ek));
                url_encode(val, ev, sizeof(ev));
                used += (size_t)snprintf(formbuf + used, sizeof(formbuf) - used,
                                         "%s%s=%s", used ? "&" : "", ek, ev);
            }
            if (formbuf[0]) {
                body = formbuf;
                body_len = strlen(formbuf);
                snprintf(method, sizeof(method), "POST");
                snprintf(extra_hdr, sizeof(extra_hdr),
                         "Content-Type: application/x-www-form-urlencoded");
            }
        }
    }

    proxy_result_t pr;
    const char *range_val = range[0] ? range : NULL;
    if (range_val && !strncmp(range_val, "bytes=", 6)) range_val += 6;
    if (proxy_fetch(abs, method, extra_hdr, body, body_len, range_val, &pr) < 0) {
        send_json(fd, 502, "{\"ok\":false,\"error\":\"proxy failed\"}");
        return;
    }
    char extra[1200];
    snprintf(extra, sizeof(extra), "X-Final-Url: %s\r\n%s%s%s",
             pr.final_url[0] ? pr.final_url : abs,
             pr.content_range[0] ? "Content-Range: " : "",
             pr.content_range[0] ? pr.content_range : "",
             pr.content_range[0] ? "\r\n" : "");
    send_response(fd, pr.status ? pr.status : 200, "OK",
                  pr.content_type, extra, pr.data, pr.len);
    proxy_result_free(&pr);
}

/* /stream: the video is forwarded chunk by chunk. Buffering the whole film in
 * memory (as /proxy does) exhausts the console's RAM and kills the title. */
static int stream_sink_begin(void *ctx, const proxy_result_t *head) {
    int fd = (int)(intptr_t)ctx;
    char hdr[2048];
    char len_hdr[64] = "";
    char range_hdr[256] = "";
    if (head->content_length >= 0)
        snprintf(len_hdr, sizeof(len_hdr), "Content-Length: %ld\r\n", head->content_length);
    if (head->content_range[0])
        snprintf(range_hdr, sizeof(range_hdr), "Content-Range: %s\r\n", head->content_range);
    const char *status = head->status == 206 ? "Partial Content"
                       : head->status == 416 ? "Range Not Satisfiable"
                       : head->status >= 400 ? "Error"
                       : "OK";
    int n = snprintf(hdr, sizeof(hdr),
                     "HTTP/1.1 %d %s\r\nContent-Type: %s\r\n%s%sAccept-Ranges: bytes\r\n"
                     "X-Final-Url: %s\r\nConnection: close\r\nCache-Control: no-store\r\n\r\n",
                     head->status ? head->status : 200, status, head->content_type,
                     len_hdr, range_hdr, head->final_url);
    if (n <= 0 || (size_t)n >= sizeof(hdr)) return -1;
    const char *p = hdr;
    size_t left = (size_t)n;
    while (left) {
        ssize_t w = send(fd, p, left, MSG_NOSIGNAL);
        if (w <= 0) return -1;
        p += w;
        left -= (size_t)w;
    }
    return 0;
}

static int stream_sink_write(void *ctx, const void *data, size_t len) {
    int fd = (int)(intptr_t)ctx;
    const char *p = data;
    while (len) {
        ssize_t w = send(fd, p, len, MSG_NOSIGNAL);
        if (w <= 0) return -1;
        p += w;
        len -= (size_t)w;
    }
    return 0;
}

static void handle_stream(int fd, request_t *req) {
    char url[2048] = {0};
    query_get(req->query, "url", url, sizeof(url));
    if (!url[0]) {
        send_json(fd, 400, "{\"ok\":false,\"error\":\"url required\"}");
        return;
    }
    char abs[2048];
    absolute_url(url, abs, sizeof(abs));
    const char *range = req->range[0] ? req->range : NULL;
    if (range && !strncmp(range, "bytes=", 6)) range += 6;
    proxy_sink_t sink = {stream_sink_begin, stream_sink_write, (void *)(intptr_t)fd};
    if (proxy_stream(abs, range, &sink) == -1)
        send_json(fd, 502, "{\"ok\":false,\"error\":\"proxy failed\"}");
}

static void handle_config_get(int fd) {
    char buf[2048];
    snprintf(buf, sizeof(buf),
             "{\"origin\":\"%s\",\"user_agent\":\"%s\",\"extra_cookies\":\"%s\","
             "\"mirrors\":[\"https://hdrezka.ag\",\"https://rezka.ag\",\"https://hdrezka.me\"]}",
             g_cfg.origin, g_cfg.user_agent, g_cfg.extra_cookies);
    send_json(fd, 200, buf);
}

static void handle_config_post(int fd, request_t *req) {
    if (req->body) {
        char tmp[4096];
        json_get_str(req->body, "origin", tmp, sizeof(tmp));
        if (tmp[0] && (!strncmp(tmp, "http://", 7) || !strncmp(tmp, "https://", 8)))
            snprintf(g_cfg.origin, sizeof(g_cfg.origin), "%s", tmp);
        json_get_str(req->body, "user_agent", tmp, sizeof(tmp));
        if (tmp[0]) snprintf(g_cfg.user_agent, sizeof(g_cfg.user_agent), "%s", tmp);
        json_get_str(req->body, "extra_cookies", g_cfg.extra_cookies, sizeof(g_cfg.extra_cookies));
        size_t n = strlen(g_cfg.origin);
        if (n && g_cfg.origin[n - 1] == '/') g_cfg.origin[n - 1] = 0;
        config_save();
    }
    handle_config_get(fd);
}

static int parse_request(int fd, request_t *req) {
    memset(req, 0, sizeof(*req));
    char *buf = malloc(REQ_MAX);
    if (!buf) return -1;
    size_t used = 0;
    while (used < REQ_MAX - 1) {
        ssize_t n = recv(fd, buf + used, REQ_MAX - 1 - used, 0);
        if (n <= 0) {
            free(buf);
            return -1;
        }
        used += (size_t)n;
        buf[used] = 0;
        if (strstr(buf, "\r\n\r\n")) break;
    }
    char *headers_end = strstr(buf, "\r\n\r\n");
    if (!headers_end) {
        free(buf);
        return -1;
    }
    size_t header_len = (size_t)(headers_end - buf) + 4;
    sscanf(buf, "%15s %2047s", req->method, req->path);
    char *q = strchr(req->path, '?');
    if (q) {
        *q = 0;
        snprintf(req->query, sizeof(req->query), "%s", q + 1);
        url_decode(req->path);
    }
    char *line = buf;
    size_t content_length = 0;
    while (line && line < headers_end) {
        char *nl = strstr(line, "\r\n");
        if (!nl) break;
        *nl = 0;
        if (!strncasecmp(line, "Content-Length:", 15)) content_length = (size_t)atol(line + 15);
        if (!strncasecmp(line, "Content-Type:", 13)) {
            const char *v = line + 13;
            while (*v == ' ') v++;
            snprintf(req->content_type, sizeof(req->content_type), "%s", v);
        }
        if (!strncasecmp(line, "Range:", 6)) {
            const char *v = line + 6;
            while (*v == ' ') v++;
            snprintf(req->range, sizeof(req->range), "%s", v);
        }
        line = nl + 2;
    }
    size_t have = used > header_len ? used - header_len : 0;
    if (content_length > REQ_MAX - 1) content_length = REQ_MAX - 1;
    req->body = malloc(content_length + 1);
    if (!req->body) {
        free(buf);
        return -1;
    }
    if (have > content_length) have = content_length;
    memcpy(req->body, buf + header_len, have);
    while (have < content_length) {
        ssize_t n = recv(fd, req->body + have, content_length - have, 0);
        if (n <= 0) break;
        have += (size_t)n;
    }
    req->body_len = have;
    req->body[have] = 0;
    free(buf);
    return 0;
}

#include "crash_log.h"

static void handle_client(int fd) {
    request_t req;
    if (parse_request(fd, &req) < 0) {
        close(fd);
        return;
    }
    log_msg("%s %s", req.method, req.path);
    crash_log_step("http_server: request %s %s (fd=%d)", req.method, req.path, fd);
    if (!strcmp(req.path, "/api/health")) {
        send_json(fd, 200, "{\"ok\":true}");
    } else if (!strcmp(req.path, "/api/config") && !strcmp(req.method, "GET")) {
        handle_config_get(fd);
    } else if (!strcmp(req.path, "/api/config") && !strcmp(req.method, "POST")) {
        handle_config_post(fd, &req);
    } else if (!strcmp(req.path, "/proxy") || !strcmp(req.path, "/img")) {
        handle_proxy(fd, &req);
    } else if (!strcmp(req.path, "/stream")) {
        handle_stream(fd, &req);
    } else {
        handle_static(fd, req.path);
    }
    crash_log_step("http_server: finished %s %s", req.method, req.path);
    free(req.body);
    close(fd);
}

static void *client_thread(void *arg) {
    int fd = (int)(intptr_t)arg;
    handle_client(fd);
    return NULL;
}

int http_server_run(int port) {
    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) return -1;
    int opt = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);
    if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(srv);
        return -1;
    }
    listen(srv, 32);
    log_msg("listening on %d", port);
    for (;;) {
        int fd = accept(srv, NULL, NULL);
        if (fd < 0) {
            if (errno == EINTR || errno == ECONNABORTED) continue;
            break;
        }
        http_socket_nosigpipe(fd);
        pthread_t th;
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        pthread_attr_setstacksize(&attr, HTTP_THREAD_STACK);
        if (pthread_create(&th, &attr, client_thread, (void *)(intptr_t)fd) != 0)
            close(fd);
        pthread_attr_destroy(&attr);
    }
    close(srv);
    return 0;
}
