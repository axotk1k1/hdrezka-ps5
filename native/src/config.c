#include "app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#define mkdir_p(path) _mkdir(path)
#else
#include <unistd.h>
#define mkdir_p(path) mkdir(path, 0755)
#endif

app_config_t g_cfg;

static void json_get(const char *src, const char *key, char *dst, size_t dst_sz) {
    char needle[128];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char *p = strstr(src, needle);
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

int config_init(const char *root) {
    memset(&g_cfg, 0, sizeof(g_cfg));
    snprintf(g_cfg.origin, sizeof(g_cfg.origin), "%s", DEFAULT_ORIGIN);
    snprintf(g_cfg.user_agent, sizeof(g_cfg.user_agent), "%s", DEFAULT_UA);
    snprintf(g_cfg.www_dir, sizeof(g_cfg.www_dir), "%s/www", root);
    snprintf(g_cfg.data_dir, sizeof(g_cfg.data_dir), "%s/data", root);
    snprintf(g_cfg.cookie_file, sizeof(g_cfg.cookie_file), "%s/data/cookies.txt", root);
    snprintf(g_cfg.config_file, sizeof(g_cfg.config_file), "%s/data/config.json", root);
    mkdir_p(g_cfg.data_dir);
    return config_load();
}

int config_load(void) {
    FILE *fp = fopen(g_cfg.config_file, "rb");
    if (!fp) return 0;
    char buf[8192];
    size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
    fclose(fp);
    buf[n] = 0;
    json_get(buf, "origin", g_cfg.origin, sizeof(g_cfg.origin));
    json_get(buf, "user_agent", g_cfg.user_agent, sizeof(g_cfg.user_agent));
    json_get(buf, "extra_cookies", g_cfg.extra_cookies, sizeof(g_cfg.extra_cookies));
    size_t len = strlen(g_cfg.origin);
    if (len && g_cfg.origin[len - 1] == '/') g_cfg.origin[len - 1] = 0;
    return 0;
}

static void json_escape(const char *src, char *dst, size_t dst_sz) {
    size_t n = 0;
    while (*src && n + 2 < dst_sz) {
        if (*src == '"' || *src == '\\') {
            dst[n++] = '\\';
        }
        dst[n++] = *src++;
    }
    dst[n] = 0;
}

int config_save(void) {
    char o[512], ua[512], ck[8192];
    json_escape(g_cfg.origin, o, sizeof(o));
    json_escape(g_cfg.user_agent, ua, sizeof(ua));
    json_escape(g_cfg.extra_cookies, ck, sizeof(ck));
    FILE *fp = fopen(g_cfg.config_file, "wb");
    if (!fp) return -1;
    fprintf(fp,
            "{\n  \"origin\": \"%s\",\n  \"user_agent\": \"%s\",\n  \"extra_cookies\": \"%s\"\n}\n",
            o, ua, ck);
    fclose(fp);
    return 0;
}
