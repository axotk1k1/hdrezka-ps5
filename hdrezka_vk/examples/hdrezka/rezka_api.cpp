#include <set>
#include "rezka_api.hpp"

#include <cctype>
#include <iostream>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <map>
#include <regex>

#ifdef __PS5__
#include <curl/curl.h>
#else
#include <curl/curl.h>
#endif

#include "console_curl.h"
#include <atomic>
#include "logger.hpp"
#include <openssl/sha.h>

namespace rezka
{

namespace
{

const std::vector<std::string> KNOWN_SALTS = {
    "IyMjI14hISMjIUBA",
    "QEBAQEAhIyMhXl5e",
    "JCQhIUAkJEBeIUAjJCRA",
    "JCQjISFAIyFAIyM=",
    "Xl5eIUAjIyEhIyM="
};

const char BASE64_CHARS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64_encode(const std::string &in)
{
    std::string out;
    int val = 0, valb = -6;
    for (unsigned char c : in) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            out.push_back(BASE64_CHARS[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) out.push_back(BASE64_CHARS[((val << 8) >> (valb + 8)) & 0x3F]);
    while (out.size() % 4) out.push_back('=');
    return out;
}

std::string base64_decode(const std::string &in)
{
    std::string clean = in;
    // Replace URL-safe characters
    for (char &c : clean) {
        if (c == '-') c = '+';
        else if (c == '_') c = '/';
    }
    while (clean.size() % 4) clean.push_back('=');

    std::vector<int> T(256, -1);
    for (int i = 0; i < 64; i++) T[(unsigned char)BASE64_CHARS[i]] = i;

    std::string out;
    int val = 0, valb = -8;
    for (unsigned char c : clean) {
        if (T[c] == -1) break;
        val = (val << 6) + T[c];
        valb += 6;
        if (valb >= 0) {
            out.push_back(char((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

// Named and numeric character references, as the site writes them.
std::string decode_entities(const std::string &in)
{
    if (in.find('&') == std::string::npos) return in;
    static const std::pair<const char *, const char *> kNamed[] = {
        {"amp", "&"}, {"lt", "<"}, {"gt", ">"}, {"quot", "\""}, {"apos", "'"}, {"nbsp", " "},
        {"laquo", "\xc2\xab"}, {"raquo", "\xc2\xbb"}, {"mdash", "\xe2\x80\x94"},
        {"ndash", "\xe2\x80\x93"}, {"hellip", "\xe2\x80\xa6"}, {"copy", "\xc2\xa9"},
        {"check", "\xe2\x9c\x93"}, {"bdquo", "\xe2\x80\x9e"}, {"ldquo", "\xe2\x80\x9c"},
        {"rdquo", "\xe2\x80\x9d"}, {"lsquo", "\xe2\x80\x98"}, {"rsquo", "\xe2\x80\x99"},
    };
    std::string out;
    out.reserve(in.size());
    for (size_t i = 0; i < in.size();) {
        const size_t semi = in[i] == '&' ? in.find(';', i) : std::string::npos;
        if (semi == std::string::npos || semi - i > 9) {
            out.push_back(in[i++]);
            continue;
        }
        const std::string name = in.substr(i + 1, semi - i - 1);
        std::string value;
        if (name.size() > 1 && name[0] == '#') {
            const bool hex = name[1] == 'x' || name[1] == 'X';
            const unsigned long cp = std::strtoul(name.c_str() + (hex ? 2 : 1), nullptr, hex ? 16 : 10);
            if (cp < 0x80) value.push_back(static_cast<char>(cp));
            else if (cp < 0x800) {
                value.push_back(static_cast<char>(0xc0 | (cp >> 6)));
                value.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
            } else if (cp < 0x10000) {
                value.push_back(static_cast<char>(0xe0 | (cp >> 12)));
                value.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
                value.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
            } else {
                value.push_back(static_cast<char>(0xf0 | (cp >> 18)));
                value.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3f)));
                value.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
                value.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
            }
        } else {
            for (const auto &entry : kNamed) {
                if (name == entry.first) {
                    value = entry.second;
                    break;
                }
            }
        }
        if (value.empty()) {
            out.push_back(in[i++]);
            continue;
        }
        out += value;
        i = semi + 1;
    }
    return out;
}

std::string strip_tags(const std::string &html)
{
    std::string out;
    bool in_tag = false;
    for (char c : html) {
        if (c == '<') in_tag = true;
        else if (c == '>') in_tag = false;
        else if (!in_tag) out.push_back(c);
    }
    // Clean multiple whitespaces
    std::string clean;
    bool prev_space = false;
    for (char c : out) {
        if (isspace((unsigned char)c)) {
            if (!prev_space) {
                clean.push_back(' ');
                prev_space = true;
            }
        } else {
            clean.push_back(c);
            prev_space = false;
        }
    }
    clean = decode_entities(clean);
    // Trim
    size_t first = clean.find_first_not_of(' ');
    if (first == std::string::npos) return "";
    size_t last = clean.find_last_not_of(' ');
    return clean.substr(first, (last - first + 1));
}

// The text between the '>' that closes the tag at `from` and `close`.
std::string inner_after(const std::string &html, size_t from, const char *close, size_t *end = nullptr)
{
    if (from == std::string::npos) return "";
    const size_t gt = html.find('>', from);
    if (gt == std::string::npos) return "";
    const size_t stop = html.find(close, gt);
    if (stop == std::string::npos) return "";
    if (end) *end = stop;
    return html.substr(gt + 1, stop - gt - 1);
}

// Where the class attribute that lists `word` among its classes starts
// (the position of the word inside it), from `from` on; npos when none does.
size_t find_class(const std::string &html, const char *word, size_t from = 0)
{
    const std::string key = "class=\"";
    const size_t len = strlen(word);
    for (size_t at = html.find(key, from); at != std::string::npos; at = html.find(key, at + 1)) {
        const size_t begin = at + key.size();
        const size_t end = html.find('"', begin);
        if (end == std::string::npos) break;
        for (size_t w = html.find(word, begin); w != std::string::npos && w + len <= end; w = html.find(word, w + 1)) {
            const bool left = w == begin || html[w - 1] == ' ';
            const bool right = w + len == end || html[w + len] == ' ';
            if (left && right) return w;
        }
    }
    return std::string::npos;
}

// The value of attribute `name` in the tag text `tag`.
std::string attr_of(const std::string &tag, const char *name)
{
    const std::string key = std::string(name) + "=\"";
    const size_t at = tag.find(key);
    if (at == std::string::npos) return "";
    const size_t stop = tag.find('"', at + key.size());
    if (stop == std::string::npos) return "";
    return tag.substr(at + key.size(), stop - at - key.size());
}

// Lower case for ASCII and the Cyrillic letters of UTF-8 text, so that the
// labels of the info table match whatever their case.
std::string lower_text(const std::string &s)
{
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = (unsigned char)s[i];
        if (c >= 'A' && c <= 'Z') {
            out.push_back((char)(c - 'A' + 'a'));
            continue;
        }
        // Cyrillic capitals: U+0410..U+042F are D0 90..D0 AF, U+0401 is D0 81.
        if (c == 0xD0 && i + 1 < s.size()) {
            const unsigned char d = (unsigned char)s[i + 1];
            if (d >= 0x90 && d <= 0x9F) {
                out.push_back((char)0xD0);
                out.push_back((char)(d + 0x20));
                ++i;
                continue;
            }
            if (d >= 0xA0 && d <= 0xAF) {
                out.push_back((char)0xD1);
                out.push_back((char)(d - 0x20));
                ++i;
                continue;
            }
            if (d == 0x81) {
                out.push_back((char)0xD1);
                out.push_back((char)0x91);
                ++i;
                continue;
            }
        }
        out.push_back((char)c);
    }
    return out;
}

// Drops the « » (or the plain quotes) that wrap a slogan.
std::string unquote(std::string s)
{
    const std::string open_guillemet = "\xC2\xAB";
    const std::string close_guillemet = "\xC2\xBB";
    if (s.rfind(open_guillemet, 0) == 0)
        s.erase(0, open_guillemet.size());
    else if (!s.empty() && s[0] == '"')
        s.erase(0, 1);
    if (s.size() >= close_guillemet.size() &&
        s.compare(s.size() - close_guillemet.size(), close_guillemet.size(), close_guillemet) == 0)
        s.erase(s.size() - close_guillemet.size());
    else if (!s.empty() && s.back() == '"')
        s.pop_back();
    return s;
}

// The cells of one table row; a label is the cell of class "l".
struct RowCell {
    bool label;
    std::string html; // the cell's inner HTML
};

std::vector<RowCell> row_cells(const std::string &row)
{
    std::vector<RowCell> cells;
    size_t pos = 0;
    while ((pos = row.find("<td", pos)) != std::string::npos) {
        const size_t gt = row.find('>', pos);
        if (gt == std::string::npos) break;
        const std::string open = row.substr(pos, gt - pos);
        size_t end = row.find("</td>", gt);
        if (end == std::string::npos) end = row.size();
        cells.push_back({open.find("class=\"l\"") != std::string::npos, row.substr(gt + 1, end - gt - 1)});
        pos = end;
    }
    return cells;
}

// A link inside an HTML fragment: its text and the text that follows it up to
// the next tag (where a list shows the place, "(2 место)").
struct LinkRef {
    std::string text;
    std::string tail;
    std::string href;
};

std::vector<LinkRef> links_of(const std::string &html)
{
    std::vector<LinkRef> out;
    size_t pos = 0;
    while ((pos = html.find("<a", pos)) != std::string::npos) {
        const char after = pos + 2 < html.size() ? html[pos + 2] : '>';
        if (after != ' ' && after != '>' && after != '\t' && after != '\n') {
            pos += 2;
            continue;
        }
        const size_t gt = html.find('>', pos);
        const size_t end = html.find("</a>", gt == std::string::npos ? pos : gt);
        if (gt == std::string::npos || end == std::string::npos) break;
        LinkRef ref;
        ref.text = strip_tags(html.substr(gt + 1, end - gt - 1));
        ref.href = attr_of(html.substr(pos, gt - pos), "href");
        size_t tail_end = html.find('<', end + 4);
        if (tail_end == std::string::npos) tail_end = html.size();
        ref.tail = html.substr(end + 4, tail_end - (end + 4));
        if (!ref.text.empty()) out.push_back(ref);
        pos = end + 4;
    }
    return out;
}

// The texts of a cell's links, or its comma-separated words when it has none.
std::vector<std::string> names_in(const std::string &html)
{
    std::vector<std::string> names;
    for (const LinkRef &ref : links_of(html))
        names.push_back(ref.text);
    if (names.empty()) {
        const std::string text = strip_tags(html);
        size_t start = 0;
        while (start <= text.size()) {
            size_t comma = text.find(',', start);
            if (comma == std::string::npos) comma = text.size();
            std::string word = strip_tags(text.substr(start, comma - start));
            if (!word.empty()) names.push_back(word);
            start = comma + 1;
        }
    }
    return names;
}

size_t curl_write_callback(void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t total = size * nmemb;
    std::string *mem = static_cast<std::string *>(userp);
    mem->append(static_cast<char *>(contents), total);
    return total;
}

size_t curl_write_binary_callback(void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t total = size * nmemb;
    auto *vec = static_cast<std::vector<uint8_t> *>(userp);
    const uint8_t *src = static_cast<const uint8_t *>(contents);
    vec->insert(vec->end(), src, src + total);
    return total;
}

} // namespace

Client::Client()
{
}

Client::~Client()
{
}

void Client::set_mirror(const std::string &mirror)
{
    mirror_ = mirror;
    while (!mirror_.empty() && mirror_.back() == '/') {
        mirror_.pop_back();
    }
}

// Some mirrors answer the app's own headers with a bare 403 on a title's page
// (to those not signed in, from some countries) while a browser gets the
// page behind the Anubis proof of work, which solve_anubis_challenge passes.
// The first such 403 switches every later request to a browser's identity.
static std::atomic<bool> g_browser_identity{false};
static const char *const kBrowserAgent =
    "Mozilla/5.0 (Linux; Android 12; BRAVIA 4K) AppleWebKit/537.36 (KHTML, like Gecko) "
    "Chrome/120.0.0.0 Safari/537.36";

static void setup_curl_headers(CURL *curl, struct curl_slist **headers, const std::string &mirror, const std::string &cookie_file, bool is_post = false)
{
    console_curl_setup(curl);

    const bool browser = g_browser_identity.load();
    if (browser) {
        *headers = curl_slist_append(*headers, (std::string("User-Agent: ") + kBrowserAgent).c_str());
    } else {
        *headers = curl_slist_append(*headers, "User-Agent: okhttp/4.12.0");
        *headers = curl_slist_append(*headers, "X-Hdrezka-Android-App-V2: 1");
        *headers = curl_slist_append(*headers, "X-Hdrezka-Android-App-Version-V2: 0.9.15 \xce\xb2");
    }
    *headers = curl_slist_append(*headers, "Accept-Language: ru-RU,ru;q=0.9,uk-UA,uk;q=0.8,en;q=0.7");
    *headers = curl_slist_append(*headers, "Accept: */*");

    std::string ref = "Referer: " + mirror + "/";
    *headers = curl_slist_append(*headers, ref.c_str());
    std::string orig = "Origin: " + mirror;
    *headers = curl_slist_append(*headers, orig.c_str());

    if (is_post) {
        *headers = curl_slist_append(*headers, "X-Requested-With: XMLHttpRequest");
        *headers = curl_slist_append(*headers, "Content-Type: application/x-www-form-urlencoded; charset=UTF-8");
    }

    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, *headers);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, browser ? kBrowserAgent : "okhttp/4.12.0");
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    if (!cookie_file.empty()) {
        curl_easy_setopt(curl, CURLOPT_COOKIEFILE, cookie_file.c_str());
        curl_easy_setopt(curl, CURLOPT_COOKIEJAR, cookie_file.c_str());
    } else {
        curl_easy_setopt(curl, CURLOPT_COOKIEFILE, "");
    }
}

bool Client::solve_anubis_challenge(const std::string &html, const std::string &page_url)
{
    if (html.find("anubis_challenge") == std::string::npos) return false;

    // Parse difficulty
    int difficulty = 0;
    size_t diff_pos = html.find("\"difficulty\":");
    if (diff_pos != std::string::npos) {
        difficulty = std::atoi(html.c_str() + diff_pos + 13);
    }
    if (difficulty < 1 || difficulty > 8) difficulty = 4;

    // Parse id
    std::string challenge_id;
    size_t id_pos = html.find("\"id\":\"");
    if (id_pos != std::string::npos) {
        size_t id_end = html.find("\"", id_pos + 6);
        if (id_end != std::string::npos) challenge_id = html.substr(id_pos + 6, id_end - (id_pos + 6));
    }

    // Parse randomData
    std::string random_data;
    size_t rd_pos = html.find("\"randomData\":\"");
    if (rd_pos != std::string::npos) {
        size_t rd_end = html.find("\"", rd_pos + 14);
        if (rd_end != std::string::npos) random_data = html.substr(rd_pos + 14, rd_end - (rd_pos + 14));
    }

    if (challenge_id.empty() || random_data.empty()) return false;

    // Solve PoW
    std::string target(static_cast<size_t>(difficulty), '0');
    uint32_t found_nonce = 0;
    char hex_digest[65] = {0};
    bool solved = false;

    for (uint32_t nonce = 0; nonce < (1 << 26); ++nonce) {
        std::string input = random_data + std::to_string(nonce);
        unsigned char md[SHA256_DIGEST_LENGTH];
        SHA256(reinterpret_cast<const unsigned char*>(input.data()), input.size(), md);
        for (int i = 0; i < 32; ++i) {
            snprintf(hex_digest + i * 2, 3, "%02x", md[i]);
        }
        hex_digest[64] = 0;

        bool match = true;
        for (int d = 0; d < difficulty; ++d) {
            if (hex_digest[d] != '0') {
                match = false;
                break;
            }
        }
        if (match) {
            found_nonce = nonce;
            solved = true;
            break;
        }
    }

    if (!solved) return false;

    // Submit pass challenge
    CURL *curl = curl_easy_init();
    if (!curl) return false;

    char *esc_redir = curl_easy_escape(curl, page_url.c_str(), static_cast<int>(page_url.length()));
    std::string pass_url = mirror_ + "/.within.website/x/cmd/anubis/api/pass-challenge?id=" + challenge_id +
                           "&response=" + std::string(hex_digest) +
                           "&nonce=" + std::to_string(found_nonce) +
                           "&redir=" + (esc_redir ? std::string(esc_redir) : page_url) +
                           "&elapsedTime=600";
    if (esc_redir) curl_free(esc_redir);

    struct curl_slist *headers = nullptr;
    setup_curl_headers(curl, &headers, mirror_, cookie_file_, false);
    std::string pass_resp;
    curl_easy_setopt(curl, CURLOPT_URL, pass_url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &pass_resp);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return (res == CURLE_OK);
}

bool Client::pick_mirror()
{
    static const char *candidates[] = {
        "https://hdrezka.ag",
        "https://rezka.ag",
        "https://hdrezka.me",
        "https://hdrezka-home.tv",
    };
    for (const char *candidate : candidates) {
        set_mirror(candidate);
        write_log("[DEBUG] pick_mirror trying %s\n", candidate);
        const std::string html = http_get("/films/?filter=popular");
        const bool ok = html.find("class=\"b-content__inline_item\"") != std::string::npos;
        write_log("[DEBUG] pick_mirror %s -> %zu bytes, catalog %s (mirror now %s)\n",
                  candidate, html.size(), ok ? "found" : "missing", mirror_.c_str());
        if (!ok && !html.empty()) {
            write_log("[DEBUG] pick_mirror head: %.300s\n", html.c_str());
        }
        if (ok)
            return true;
    }
    set_mirror("https://hdrezka.ag");
    return false;
}

bool Client::http_get_binary(const std::string &url, std::vector<uint8_t> &out_bytes)
{
    std::lock_guard<std::recursive_mutex> net_lock(net_mu_);
    out_bytes.clear();
    CURL *curl = curl_easy_init();
    if (!curl) return false;

    std::string full_url = url;
    if (full_url.rfind("http", 0) != 0) {
        full_url = mirror_ + (full_url.front() == '/' ? "" : "/") + full_url;
    }

    struct curl_slist *headers = nullptr;
    setup_curl_headers(curl, &headers, mirror_, cookie_file_, false);

    curl_easy_setopt(curl, CURLOPT_URL, full_url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_binary_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out_bytes);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    return (res == CURLE_OK && !out_bytes.empty());
}

std::string Client::http_get(const std::string &url)
{
    std::lock_guard<std::recursive_mutex> net_lock(net_mu_);
    write_log("[DEBUG] http_get curl_easy_init\n");
    CURL *curl = curl_easy_init();
    if (!curl) {
        write_log("[DEBUG] http_get curl_easy_init FAILED\n");
        return "";
    }

    std::string full_url = url;
    if (full_url.rfind("http", 0) != 0) {
        full_url = mirror_ + (full_url.front() == '/' ? "" : "/") + full_url;
    }

    struct curl_slist *headers = nullptr;
    write_log("[DEBUG] http_get setup headers (CA: %s)\n", console_curl_ca_file());
    setup_curl_headers(curl, &headers, mirror_, cookie_file_, false);

    std::string response;
    char errbuf[CURL_ERROR_SIZE] = {0};
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);
    curl_easy_setopt(curl, CURLOPT_URL, full_url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    write_log("[DEBUG] http_get perform %s\n", full_url.c_str());
    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    write_log("[DEBUG] http_get done rc=%d (%s) http=%ld bytes=%zu err='%s'\n",
              (int)res, curl_easy_strerror(res), http_code, response.size(), errbuf);
    char *effective = nullptr;
    curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &effective);
    if (effective && strstr(effective, "http")) {
        // Update mirror domain if redirected
        std::string eff_str(effective);
        size_t slash3 = eff_str.find('/', 8);
        if (slash3 != std::string::npos) {
            std::string new_origin = eff_str.substr(0, slash3);
            if (new_origin != mirror_ && new_origin.find("hdrezka") != std::string::npos) {
                mirror_ = new_origin;
            }
        }
    }
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) return "";

    if (http_code == 403 && !g_browser_identity.exchange(true)) {
        write_log("[DEBUG] http_get 403 for the app's headers: asking as a browser from now on\n");
        return http_get(url);
    }

    // Check for Anubis challenge
    if (response.find("anubis_challenge") != std::string::npos) {
        if (solve_anubis_challenge(response, full_url)) {
            // Re-fetch
            return http_get(url);
        }
    }

    return response;
}

// libcurl trace for POST diagnostics: only text and header lines, never bodies.
static int curl_trace_callback(CURL *, curl_infotype type, char *data, size_t size, void *)
{
    if (type != CURLINFO_TEXT && type != CURLINFO_HEADER_IN && type != CURLINFO_HEADER_OUT)
        return 0;
    std::string line(data, size > 200 ? 200 : size);
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r'))
        line.pop_back();
    write_log("[DEBUG] curl[%d] %s\n", (int)type, line.c_str());
    return 0;
}

std::string Client::http_post(const std::string &url, const std::map<std::string, std::string> &fields)
{
    std::lock_guard<std::recursive_mutex> net_lock(net_mu_);
    CURL *curl = curl_easy_init();
    if (!curl) return "";

    std::string full_url = url;
    if (full_url.rfind("http", 0) != 0) {
        full_url = mirror_ + (full_url.front() == '/' ? "" : "/") + full_url;
    }

    std::string post_data;
    for (auto it = fields.begin(); it != fields.end(); ++it) {
        if (it != fields.begin()) post_data += "&";
        char *k = curl_easy_escape(curl, it->first.c_str(), (int)it->first.length());
        char *v = curl_easy_escape(curl, it->second.c_str(), (int)it->second.length());
        if (k && v) {
            post_data += k;
            post_data += "=";
            post_data += v;
        }
        if (k) curl_free(k);
        if (v) curl_free(v);
    }

    struct curl_slist *headers = nullptr;
    setup_curl_headers(curl, &headers, mirror_, cookie_file_, true);

    std::string response;
    char errbuf[CURL_ERROR_SIZE] = {0};
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errbuf);
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
    curl_easy_setopt(curl, CURLOPT_DEBUGFUNCTION, curl_trace_callback);
    curl_easy_setopt(curl, CURLOPT_URL, full_url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    write_log("[DEBUG] http_post perform %s fields: %s\n", full_url.c_str(), post_data.c_str());
    write_log("[DEBUG] http_post before curl_easy_perform (post_len=%zu)\n", post_data.size());
    CURLcode res = curl_easy_perform(curl);
    long post_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &post_code);
    write_log("[DEBUG] http_post after curl_easy_perform rc=%d (%s) http=%ld err='%s'\n",
              (int)res, curl_easy_strerror(res), post_code, errbuf);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res == CURLE_OK && post_code == 403 && !g_browser_identity.exchange(true)) {
        write_log("[DEBUG] http_post 403 for the app's headers: asking as a browser from now on\n");
        return http_post(url, fields);
    }

    write_log("[DEBUG] http_post done rc=%d bytes=%zu resp: %.200s\n", (int)res, response.size(), response.c_str());

    if (res != CURLE_OK) return "";

    if (response.find("anubis_challenge") != std::string::npos) {
        if (solve_anubis_challenge(response, full_url)) {
            return http_post(url, fields);
        }
    }

    return response;
}

std::string Client::clear_trash(const std::string &data)
{
    if (data.empty()) return "";
    if (data.front() == '[' || data.rfind("http", 0) == 0) return data;

    std::string s = data;
    if (s.rfind("#h", 0) == 0) s = s.substr(2);

    int guard = 0;
    while (s.find("//_//") != std::string::npos && guard < 80) {
        guard++;
        size_t idx = s.find("//_//");
        std::string after = s.substr(idx + 5);
        bool matched = false;
        for (const auto &salt : KNOWN_SALTS) {
            if (after.rfind(salt, 0) == 0) {
                s = s.substr(0, idx) + after.substr(salt.length());
                matched = true;
                break;
            }
        }
        if (!matched && after.length() >= 16) {
            s = s.substr(0, idx) + after.substr(16);
        }
    }

    std::string decoded = base64_decode(s);
    if (!decoded.empty() && (decoded.front() == '[' || decoded.rfind("http", 0) == 0)) {
        return decoded;
    }

    // SuperZombi fallback
    std::vector<std::string> trash_codes;
    const std::vector<char> trash_chars = {'@', '#', '!', '^', '$'};
    // length 2
    for (char a : trash_chars) {
        for (char b : trash_chars) {
            std::string comb = {a, b};
            trash_codes.push_back(base64_encode(comb));
        }
    }
    // length 3
    for (char a : trash_chars) {
        for (char b : trash_chars) {
            for (char c : trash_chars) {
                std::string comb = {a, b, c};
                trash_codes.push_back(base64_encode(comb));
            }
        }
    }

    std::string raw = data;
    if (raw.rfind("#h", 0) == 0) raw = raw.substr(2);
    // split "//_//" and join
    std::string joined;
    size_t pos = 0;
    while (pos < raw.length()) {
        size_t next = raw.find("//_//", pos);
        if (next == std::string::npos) {
            joined += raw.substr(pos);
            break;
        }
        joined += raw.substr(pos, next - pos);
        pos = next + 5;
    }

    for (const auto &code : trash_codes) {
        size_t cpos = 0;
        while ((cpos = joined.find(code, cpos)) != std::string::npos) {
            joined.erase(cpos, code.length());
        }
    }

    std::string sz_decoded = base64_decode(joined);
    if (!sz_decoded.empty() && (sz_decoded.front() == '[' || sz_decoded.rfind("http", 0) == 0)) {
        return sz_decoded;
    }

    return !decoded.empty() ? decoded : data;
}

std::vector<StreamQuality> Client::parse_stream_string(const std::string &raw)
{
    std::vector<StreamQuality> qualities;
    if (raw.empty()) return qualities;

    std::regex re("\\[([^\\]]+)\\]([^,\\[]+)");
    std::sregex_iterator next(raw.begin(), raw.end(), re);
    std::sregex_iterator end;

    while (next != end) {
        std::smatch match = *next;
        std::string quality_label = match[1].str();
        std::string urls_str = match[2].str();

        // clean label tags
        quality_label = strip_tags(quality_label);

        // URLs may have " or "
        std::string best_url;
        auto is_teaser = [](const std::string &u) {
            return u.find("/zrkms.mp4") != std::string::npos;
        };

        size_t or_idx = urls_str.find(" or ");
        if (or_idx != std::string::npos) {
            std::string p1 = urls_str.substr(0, or_idx);
            std::string p2 = urls_str.substr(or_idx + 4);
            size_t h1 = p1.find(":hls:manifest.m3u8");
            if (h1 != std::string::npos) p1.erase(h1, 18);
            size_t h2 = p2.find(":hls:manifest.m3u8");
            if (h2 != std::string::npos) p2.erase(h2, 18);
            while (!p1.empty() && (isspace((unsigned char)p1.front()) || p1.front() == ',')) p1.erase(0, 1);
            while (!p1.empty() && (isspace((unsigned char)p1.back()) || p1.back() == ',')) p1.pop_back();
            while (!p2.empty() && (isspace((unsigned char)p2.front()) || p2.front() == ',')) p2.erase(0, 1);
            while (!p2.empty() && (isspace((unsigned char)p2.back()) || p2.back() == ',')) p2.pop_back();

            if (!is_teaser(p2) && p2.rfind("http", 0) == 0) best_url = p2;
            else if (!is_teaser(p1) && p1.rfind("http", 0) == 0) best_url = p1;
            else best_url = p2.empty() ? p1 : p2;
        } else {
            best_url = urls_str;
            size_t hls_pos = best_url.find(":hls:manifest.m3u8");
            if (hls_pos != std::string::npos) {
                best_url.erase(hls_pos, 18);
            }
        }

        // trim whitespace and commas
        while (!best_url.empty() && (isspace((unsigned char)best_url.front()) || best_url.front() == ',')) best_url.erase(0, 1);
        while (!best_url.empty() && (isspace((unsigned char)best_url.back()) || best_url.back() == ',')) best_url.pop_back();

        if (best_url.rfind("http", 0) == 0) {
            StreamQuality sq;
            sq.quality = quality_label;
            sq.url = best_url;
            sq.is_premium = (quality_label.find("Ultra") != std::string::npos || quality_label.find("2160p") != std::string::npos);
            qualities.push_back(sq);
        }

        ++next;
    }

    return qualities;
}

static std::vector<CatalogItem> parse_catalog_html(const std::string &html, const std::string &category, const std::string &mirror)
{
    std::vector<CatalogItem> items;
    if (html.empty()) return items;

    // Match the item card itself. A search for the bare class name also hits
    // b-content__inline_item-cover and -link, which splits the poster away
    // from the title so every card is dropped or left without a cover.
    const std::string mark = "class=\"b-content__inline_item\"";
    size_t pos = 0;
    while ((pos = html.find(mark, pos)) != std::string::npos) {
        size_t end_item = html.find(mark, pos + mark.size());
        if (end_item == std::string::npos) end_item = html.length();

        std::string chunk = html.substr(pos, end_item - pos);
        pos = end_item;

        CatalogItem item;
        // id: data-id="12345"
        size_t id_pos = chunk.find("data-id=\"");
        if (id_pos != std::string::npos) {
            size_t id_end = chunk.find("\"", id_pos + 9);
            if (id_end != std::string::npos) item.id = chunk.substr(id_pos + 9, id_end - (id_pos + 9));
        }

        // url: data-url="https://..." or <a href="...">
        size_t url_pos = chunk.find("data-url=\"");
        if (url_pos != std::string::npos) {
            size_t url_end = chunk.find("\"", url_pos + 10);
            if (url_end != std::string::npos) item.url = chunk.substr(url_pos + 10, url_end - (url_pos + 10));
        }
        if (item.url.empty()) {
            size_t href_pos = chunk.find("<a href=\"");
            if (href_pos != std::string::npos) {
                size_t href_end = chunk.find("\"", href_pos + 9);
                if (href_end != std::string::npos) item.url = chunk.substr(href_pos + 9, href_end - (href_pos + 9));
            }
        }
        if (!item.url.empty() && item.url.rfind("http", 0) != 0) {
            item.url = mirror + (item.url.front() == '/' ? "" : "/") + item.url;
        }

        // cover: check data-src="..." first (for lazy-loaded covers), then src="..."
        size_t img_pos = chunk.find("<img");
        if (img_pos != std::string::npos) {
            size_t dsrc_pos = chunk.find("data-src=\"", img_pos);
            size_t src_pos = chunk.find("src=\"", img_pos);
            if (dsrc_pos != std::string::npos && dsrc_pos < chunk.find(">", img_pos)) {
                size_t dsrc_end = chunk.find("\"", dsrc_pos + 10);
                if (dsrc_end != std::string::npos) item.cover_url = chunk.substr(dsrc_pos + 10, dsrc_end - (dsrc_pos + 10));
            } else if (src_pos != std::string::npos && src_pos < chunk.find(">", img_pos)) {
                size_t src_end = chunk.find("\"", src_pos + 5);
                if (src_end != std::string::npos) item.cover_url = chunk.substr(src_pos + 5, src_end - (src_pos + 5));
            }
        }
        if (!item.cover_url.empty() && item.cover_url.rfind("http", 0) != 0) {
            item.cover_url = mirror + (item.cover_url.front() == '/' ? "" : "/") + item.cover_url;
        }

        // title: inside .b-content__inline_item-link a
        size_t title_div = chunk.find("b-content__inline_item-link");
        if (title_div != std::string::npos) {
            size_t a_start = chunk.find("<a", title_div);
            if (a_start != std::string::npos) {
                size_t a_tag_close = chunk.find(">", a_start);
                size_t a_close = chunk.find("</a>", a_tag_close);
                if (a_tag_close != std::string::npos && a_close != std::string::npos) {
                    item.title = strip_tags(chunk.substr(a_tag_close + 1, a_close - (a_tag_close + 1)));
                }
            }
            // description / info: <div ...>Year, Country, Genre...</div>
            size_t div_start = chunk.find("<div", title_div);
            if (div_start != std::string::npos) {
                size_t div_tag_close = chunk.find(">", div_start);
                size_t div_close = chunk.find("</div>", div_tag_close);
                if (div_tag_close != std::string::npos && div_close != std::string::npos) {
                    item.info = strip_tags(chunk.substr(div_tag_close + 1, div_close - (div_tag_close + 1)));
                }
            }
        }

        // HDrezkaTV: div.b-content__inline_item-cover a span.info
        {
            size_t span = chunk.find("span.info");
            if (span != std::string::npos) {
                size_t gt = chunk.find('>', span);
                size_t end = chunk.find("</span>", gt);
                if (gt != std::string::npos && end != std::string::npos)
                    item.cover_info = strip_tags(chunk.substr(gt + 1, end - (gt + 1)));
            }
        }
        // HDrezkaTV: div.b-content__inline_item-cover a span.cat i.entity
        {
            size_t ent = chunk.find("i.entity");
            if (ent == std::string::npos)
                ent = chunk.find("class=\"entity\"");
            if (ent == std::string::npos)
                ent = chunk.find("class='entity'");
            if (ent != std::string::npos) {
                size_t gt = chunk.find('>', ent);
                size_t end = chunk.find("</i>", gt);
                if (end == std::string::npos)
                    end = chunk.find('<', gt + 1);
                if (gt != std::string::npos && end != std::string::npos)
                    item.entity = strip_tags(chunk.substr(gt + 1, end - (gt + 1)));
            }
        }
        if (!item.entity.empty() && (category.empty() || category == "slider" ||
                                     category == "home" || category == "main"))
            item.category = item.entity;
        else
            item.category = category;

        // Prefer link-div description; fall back to cover span.info
        if (item.description.empty() && !item.info.empty())
            item.description = item.info;
        if (item.info.empty() && !item.cover_info.empty())
            item.info = item.cover_info;

        // parse year from info
        std::regex year_re("(19|20)\\d{2}");
        std::smatch ym;
        if (std::regex_search(item.info, ym, year_re)) {
            item.year = std::stoi(ym.str());
        } else if (std::regex_search(item.cover_info, ym, year_re)) {
            item.year = std::stoi(ym.str());
        }

        // Quality badge on the poster cover (4K / HD / 1080p / CAM …).
        {
            std::smatch qm;
            std::regex q_re(
                "(?:b-content__inline_item-quality[^>]*>|"
                "info_flags[^>]*>|"
                "class=\"[^\"]*quality[^\"]*\"[^>]*>)\\s*([^<]{1,16})",
                std::regex::icase);
            if (std::regex_search(chunk, qm, q_re)) {
                item.quality = strip_tags(qm[1].str());
                while (!item.quality.empty() &&
                       (item.quality.back() == ' ' || item.quality.back() == '\n' ||
                        item.quality.back() == '\r'))
                    item.quality.pop_back();
            }
            if (item.quality.empty()) {
                static const char *kHints[] = {"2160p", "4K", "1080p", "720p", "HDRip", "HD",
                                              "BDRip", "WEBRip", "CAMRip", "TS"};
                for (const char *h : kHints) {
                    if (chunk.find(h) != std::string::npos) {
                        item.quality = h;
                        break;
                    }
                }
            }
        }

        if (!item.title.empty() && !item.url.empty()) {
            items.push_back(item);
        }
    }
    return items;
}

std::vector<CatalogItem> Client::get_slider_newest(int genre_id)
{
    std::map<std::string, std::string> fields;
    fields["id"] = std::to_string(genre_id);
    std::string html = http_post("/engine/ajax/get_newest_slider_content.php", fields);
    // HDrezkaTV scopes cards to div.b-newest_slider__list
    size_t list = html.find("b-newest_slider__list");
    std::string scoped = (list != std::string::npos) ? html.substr(list) : html;
    auto items = parse_catalog_html(scoped, "slider", mirror_);
    if (items.empty() && list != std::string::npos)
        items = parse_catalog_html(html, "slider", mirror_);
    if (items.empty())
        items = get_catalog("films", "last", 1);
    return items;
}

std::vector<CatalogItem> Client::get_catalog(const std::string &category, const std::string &filter, int page)
{
    std::string path;
    if (category.empty() || category == "home" || category == "main") path = "/";
    else if (category == "films") path = "/films/";
    else if (category == "series") path = "/series/";
    else if (category == "cartoons") path = "/cartoons/";
    else if (category == "animation") path = "/animation/";
    else if (category == "new") path = "/new/";
    else path = "/" + category + "/";

    std::string flt = filter.empty() ? "popular" : filter;
    path += "?filter=" + flt;
    if (page > 1) {
        path += "&page=" + std::to_string(page);
    }

    std::string html = http_get(path);
    return parse_catalog_html(html, category, mirror_);
}

std::vector<CatalogItem> Client::get_movie_list(const std::string &filter, int genre, int page,
                                                bool *has_next, const std::string &prefix)
{
    std::string path = prefix;
    if (page > 1)
        path += "/page/" + std::to_string(page);
    path += "/?filter=" + (filter.empty() ? std::string("watching") : filter);
    if (genre > 0)
        path += "&genre=" + std::to_string(genre);

    const std::string html = http_get(path);
    if (has_next) {
        // The app's rule: a next page exists when the navigation block is
        // there and its "next" arrow is not the disabled (no-page) one.
        const bool nav = html.find("b-navigation") != std::string::npos;
        const size_t next = html.find("b-navigation__next");
        bool disabled = false;
        if (next != std::string::npos) {
            const size_t from = next > 160 ? next - 160 : 0;
            const size_t no_page = html.rfind("no-page", next);
            disabled = no_page != std::string::npos && no_page >= from;
        }
        *has_next = nav && next != std::string::npos && !disabled;
    }
    return parse_catalog_html(html, "home", mirror_);
}

// The value of a JSON string member, unescaped (defined with the comments' reader).
static std::string json_string(const std::string &json, const char *key);

bool Client::login(const std::string &email, const std::string &password, std::string &error)
{
    error.clear();
    std::map<std::string, std::string> fields;
    fields["login_name"] = email;
    fields["login_password"] = password;
    fields["login_not_save"] = "0";
    const std::string response = http_post("/ajax/login/", fields);
    if (response.empty()) {
        error = "Ошибка сети";
        return false;
    }
    if (response.find("\"success\":true") == std::string::npos) {
        error = json_string(response, "message");
        return false;
    }
    write_log("[DEBUG] login ok, user id '%s'\n", session_user_id().c_str());
    return true;
}

void Client::logout()
{
    http_post("/logout", {});
    // Whatever the site answered, this device forgets the session: the cookie
    // file loses its dle_* and PHPSESSID lines (the mirror's own stay).
    std::lock_guard<std::recursive_mutex> net_lock(net_mu_);
    if (cookie_file_.empty())
        return;
    std::string kept;
    if (FILE *f = fopen(cookie_file_.c_str(), "r")) {
        char line[4096];
        while (fgets(line, sizeof(line), f)) {
            if (strstr(line, "\tdle_") || strstr(line, "\tPHPSESSID\t"))
                continue;
            kept += line;
        }
        fclose(f);
    }
    if (FILE *f = fopen(cookie_file_.c_str(), "w")) {
        fputs(kept.c_str(), f);
        fclose(f);
    }
}

std::string Client::session_user_id() const
{
    if (cookie_file_.empty())
        return "";
    std::string id;
    if (FILE *f = fopen(cookie_file_.c_str(), "r")) {
        char line[4096];
        while (fgets(line, sizeof(line), f)) {
            // Netscape format: domain, flag, path, secure, expiry, name, value
            const char *name = strstr(line, "\tdle_user_id\t");
            if (!name)
                continue;
            std::string value = name + strlen("\tdle_user_id\t");
            while (!value.empty() && (value.back() == '\n' || value.back() == '\r'))
                value.pop_back();
            if (!value.empty() && value != "deleted" && value != "0")
                id = value;
        }
        fclose(f);
    }
    return id;
}

bool Client::get_account(Account &acc)
{
    acc = {};
    acc.id = session_user_id();
    if (acc.id.empty())
        return false;
    const std::string html = http_get("/user/" + acc.id + "/");
    if (html.empty())
        return false;
    // Signed in is told by the page not offering to sign in.
    acc.logged_in = html.find("b-tophead__login") == std::string::npos;
    const size_t title = html.find("<title>");
    if (title != std::string::npos)
        acc.name = strip_tags(inner_after(html, title, "</title>"));
    // The e-mail: input.b-field with id="email" (the settings form)
    for (size_t at = find_class(html, "b-field"); at != std::string::npos; at = find_class(html, "b-field", at + 1)) {
        const size_t open = html.rfind('<', at);
        const size_t close = html.find('>', at);
        if (open == std::string::npos || close == std::string::npos)
            break;
        const std::string tag = html.substr(open, close - open);
        if (attr_of(tag, "id") == "email") {
            acc.email = decode_entities(attr_of(tag, "value"));
            break;
        }
    }
    // The avatar: div.b-userprofile__avatar_holder img
    const size_t holder = find_class(html, "b-userprofile__avatar_holder");
    if (holder != std::string::npos) {
        const size_t img = html.find("<img", holder);
        const size_t img_end = img == std::string::npos ? img : html.find('>', img);
        if (img_end != std::string::npos) {
            std::string src = attr_of(html.substr(img, img_end - img), "src");
            if (!src.empty() && src.rfind("http", 0) != 0)
                src = mirror_ + (src[0] == '/' ? "" : "/") + src;
            acc.avatar = src;
        }
    }
    // The subscription: li.b-tophead-premuser, the days left in its <b>
    const size_t prem = find_class(html, "b-tophead-premuser");
    if (prem != std::string::npos) {
        acc.premium = true;
        const size_t li_end = html.find("</li>", prem);
        const size_t b_at = html.find("<b>", prem);
        if (b_at != std::string::npos && (li_end == std::string::npos || b_at < li_end)) {
            const std::string digits = strip_tags(inner_after(html, b_at, "</b>"));
            int days = 0;
            for (char c : digits)
                if (c >= '0' && c <= '9')
                    days = days * 10 + (c - '0');
            acc.premium_days = days;
        }
    }
    return acc.logged_in;
}

std::vector<WatchLaterItem> Client::get_watch_later()
{
    std::vector<WatchLaterItem> out;
    const std::string html = http_get("/continue/");
    const std::string mark = "b-videosaves__list_item";
    bool first = true;
    for (size_t at = html.find(mark); at != std::string::npos;) {
        const size_t next = html.find(mark, at + mark.size());
        // The list opens with its heading row, which the app drops too
        if (first) {
            first = false;
            at = next;
            continue;
        }
        const size_t tag_open = html.rfind('<', at);
        const size_t tag_close = html.find('>', at);
        const std::string chunk = html.substr(at, (next == std::string::npos ? std::min(html.size(), at + 4000) : next) - at);
        WatchLaterItem item;
        if (tag_open != std::string::npos && tag_close != std::string::npos) {
            item.id = attr_of(html.substr(tag_open, tag_close - tag_open), "id");
            const std::string prefix = "videosave-";
            if (item.id.rfind(prefix, 0) == 0)
                item.id.erase(0, prefix.size());
        }
        const size_t date = find_class(chunk, "date");
        if (date != std::string::npos) item.date = strip_tags(inner_after(chunk, date, "</div>"));
        const size_t title = find_class(chunk, "title");
        if (title != std::string::npos) {
            const size_t a = chunk.find("<a", title);
            if (a != std::string::npos) {
                const std::string tag = chunk.substr(a, chunk.find('>', a) - a);
                item.url = attr_of(tag, "href");
                item.cover_url = attr_of(tag, "data-cover_url");
                item.title = strip_tags(inner_after(chunk, a, "</a>"));
            }
            const size_t small = chunk.find("<small", title);
            if (small != std::string::npos) item.project_info = strip_tags(inner_after(chunk, small, "</small>"));
        }
        const size_t info = find_class(chunk, "info");
        if (info != std::string::npos) {
            std::string cell = inner_after(chunk, info, "</div>");
            const size_t holder = cell.find("<span");
            if (holder != std::string::npos) {
                const size_t a = cell.find("<a", holder);
                if (a != std::string::npos) item.next_info = strip_tags(inner_after(cell, a, "</a>"));
                cell.erase(holder);
            }
            item.info = strip_tags(cell);
        }
        if (!item.url.empty() && item.url.rfind("http", 0) != 0)
            item.url = mirror_ + (item.url.front() == '/' ? "" : "/") + item.url;
        if (!item.title.empty())
            out.push_back(item);
        at = next;
    }
    write_log("[DEBUG] get_watch_later -> %zu\n", out.size());
    return out;
}

bool Client::remove_watch_later(const std::string &id)
{
    std::map<std::string, std::string> fields;
    fields["id"] = id;
    return http_post("/engine/ajax/cdn_saves_remove.php", fields).find("\"success\":true") != std::string::npos;
}

std::vector<BookmarkFolder> Client::get_bookmark_folders()
{
    std::vector<BookmarkFolder> out;
    const std::string html = http_get("/favorites/");
    const std::string mark = "class=\"b-favorites_content__cats_list_item";
    for (size_t at = html.find(mark); at != std::string::npos;) {
        const size_t next = html.find(mark, at + mark.size());
        const size_t tag_open = html.rfind('<', at);
        const size_t tag_close = html.find('>', at);
        const std::string chunk = html.substr(at, (next == std::string::npos ? std::min(html.size(), at + 2000) : next) - at);
        BookmarkFolder folder;
        if (tag_open != std::string::npos && tag_close != std::string::npos)
            folder.id = attr_of(html.substr(tag_open, tag_close - tag_open), "data-cat_id");
        const size_t name = chunk.find("class=\"name\"");
        if (name != std::string::npos) folder.name = strip_tags(inner_after(chunk, name, "</span>"));
        const size_t b = chunk.find("<b>");
        if (b != std::string::npos) folder.count = strip_tags(inner_after(chunk, b, "</b>"));
        const size_t link = chunk.find("b-favorites_content__cats_list_link");
        if (link != std::string::npos) {
            const size_t a = chunk.rfind("<a", link);
            if (a != std::string::npos) folder.url = attr_of(chunk.substr(a, chunk.find('>', a) - a), "href");
        }
        if (!folder.url.empty() && folder.url.rfind("http", 0) != 0)
            folder.url = mirror_ + (folder.url.front() == '/' ? "" : "/") + folder.url;
        if (!folder.name.empty())
            out.push_back(folder);
        at = next;
    }
    write_log("[DEBUG] get_bookmark_folders -> %zu\n", out.size());
    return out;
}

std::vector<CatalogItem> Client::get_list_at(const std::string &url)
{
    return parse_catalog_html(http_get(url), "list", mirror_);
}

Client::HomePage Client::get_home_page(const std::string &filter)
{
    HomePage page;
    std::string flt = filter.empty() ? "last" : filter;
    // Анонсы in the APK drawer use ?filter=soon; keep the rest as-is.
    if (flt == "announce")
        flt = "soon";

    auto films_slider = get_slider_newest(1);
    auto series_slider = get_slider_newest(2);
    page.hero.reserve(5);
    for (size_t i = 0; i < films_slider.size() && page.hero.size() < 3; ++i)
        page.hero.push_back(films_slider[i]);
    for (size_t i = 0; i < series_slider.size() && page.hero.size() < 5; ++i)
        page.hero.push_back(series_slider[i]);

    page.films = get_catalog("films", flt, 1);
    page.series = get_catalog("series", flt, 1);
    page.cartoons = get_catalog("cartoons", flt, 1);

    if (page.hero.empty()) {
        for (size_t i = 0; i < page.films.size() && page.hero.size() < 5; ++i)
            page.hero.push_back(page.films[i]);
    }
    return page;
}

bool Client::get_person(const std::string &person_url, PersonDetails &out)
{
    out = {};
    out.url = person_url;
    if (out.url.rfind("http", 0) != 0 && !mirror_.empty())
        out.url = mirror_ + (out.url.empty() || out.url.front() == '/' ? "" : "/") + out.url;

    std::string html = http_get(out.url);
    if (html.empty())
        return false;

    auto take_between = [&](const std::string &open_mark, const std::string &close_tag) -> std::string {
        size_t p = html.find(open_mark);
        if (p == std::string::npos)
            return {};
        size_t gt = html.find('>', p);
        if (gt == std::string::npos)
            return {};
        size_t end = html.find(close_tag, gt);
        if (end == std::string::npos)
            return {};
        return strip_tags(html.substr(gt + 1, end - (gt + 1)));
    };

    out.name = take_between("b-post__title", "</h1>");
    if (out.name.empty())
        out.name = take_between("b-person__name", "</");
    if (out.name.empty()) {
        size_t h1 = html.find("<h1");
        if (h1 != std::string::npos) {
            size_t gt = html.find('>', h1);
            size_t end = html.find("</h1>", gt);
            if (gt != std::string::npos && end != std::string::npos)
                out.name = strip_tags(html.substr(gt + 1, end - (gt + 1)));
        }
    }
    out.name_orig = take_between("b-post__origtitle", "</div>");
    if (out.name_orig.empty())
        out.name_orig = take_between("b-person__orig_name", "</");

    {
        size_t img = html.find("b-sidecover");
        if (img == std::string::npos)
            img = html.find("b-person__photo");
        if (img == std::string::npos)
            img = html.find("b-person__poster");
        if (img != std::string::npos) {
            size_t href = html.find("href=\"", img);
            size_t src = html.find("src=\"", img);
            size_t dsrc = html.find("data-src=\"", img);
            auto grab = [&](size_t at, size_t skip) -> std::string {
                if (at == std::string::npos || at > img + 800)
                    return {};
                size_t e = html.find('"', at + skip);
                if (e == std::string::npos)
                    return {};
                return html.substr(at + skip, e - (at + skip));
            };
            out.photo_hq = grab(href, 6);
            out.photo = grab(dsrc != std::string::npos ? dsrc : src, dsrc != std::string::npos ? 10 : 5);
            if (out.photo.empty())
                out.photo = out.photo_hq;
        }
        if (out.photo.empty()) {
            size_t og = html.find("property=\"og:image\"");
            if (og != std::string::npos) {
                size_t c = html.rfind("content=\"", og);
                if (c != std::string::npos && og - c < 80) {
                    size_t e = html.find('"', c + 9);
                    if (e != std::string::npos)
                        out.photo = html.substr(c + 9, e - (c + 9));
                }
            }
        }
        auto abs = [&](std::string &u) {
            if (u.empty())
                return;
            if (u.rfind("//", 0) == 0)
                u = "https:" + u;
            else if (u.rfind("http", 0) != 0)
                u = mirror_ + (u.front() == '/' ? "" : "/") + u;
        };
        abs(out.photo);
        abs(out.photo_hq);
    }

    {
        std::smatch m;
        std::regex id_re("/person/(\\d+)");
        if (std::regex_search(out.url, m, id_re))
            out.id = m[1].str();
    }

    // Careers -- each .b-person__career block (HDrezkaTV + browser client).
    std::set<std::string> seen_urls;
    size_t pos = 0;
    while ((pos = html.find("b-person__career", pos)) != std::string::npos) {
        // Prefer career content blocks, skip header-only class hits when possible.
        size_t block_start = pos;
        size_t next = html.find("b-person__career", pos + 16);
        size_t block_end = next == std::string::npos ? html.size() : next;
        std::string block = html.substr(block_start, block_end - block_start);

        std::string career_title = "Фильмография";
        for (const char *mark : {"b-person__career_header", "b-person__career_title", "<h2",
                                 "class=\"title\""}) {
            size_t t = block.find(mark);
            if (t == std::string::npos)
                continue;
            size_t gt = block.find('>', t);
            size_t end = block.find('<', gt == std::string::npos ? t : gt + 1);
            if (gt != std::string::npos && end != std::string::npos && end > gt) {
                std::string title = strip_tags(block.substr(gt + 1, end - (gt + 1)));
                if (!title.empty() && title != out.name) {
                    career_title = title;
                    break;
                }
            }
        }

        auto films = parse_catalog_html(block, "person", mirror_);
        PersonCareer career;
        career.title = career_title;
        for (const CatalogItem &item : films) {
            career.movies.push_back(item);
            if (seen_urls.insert(item.url).second)
                out.movies.push_back(item);
        }
        if (!career.movies.empty())
            out.careers.push_back(std::move(career));
        pos = block_end;
    }

    if (out.movies.empty()) {
        auto films = parse_catalog_html(html, "person", mirror_);
        for (const CatalogItem &item : films)
            if (seen_urls.insert(item.url).second)
                out.movies.push_back(item);
        if (!out.movies.empty()) {
            PersonCareer career;
            career.title = "Фильмография";
            career.movies = out.movies;
            out.careers.push_back(std::move(career));
        }
    }

    // Info table: date/place of birth, height, …
    auto fill_info = [&](const char *table_cls) {
        size_t table = html.find(table_cls);
        if (table == std::string::npos)
            return;
        size_t table_end = html.find("</table>", table);
        if (table_end == std::string::npos)
            table_end = std::min(html.size(), table + 8000);
        size_t tr = table;
        while ((tr = html.find("<tr", tr)) != std::string::npos && tr < table_end) {
            size_t tr_end = html.find("</tr>", tr);
            if (tr_end == std::string::npos || tr_end > table_end)
                break;
            std::string row = html.substr(tr, tr_end - tr);
            tr = tr_end + 5;
            size_t l = row.find("class=\"l\"");
            if (l == std::string::npos)
                l = row.find("<td");
            if (l == std::string::npos)
                continue;
            size_t lgt = row.find('>', l);
            size_t ltd = row.find("</td>", lgt);
            if (lgt == std::string::npos || ltd == std::string::npos)
                continue;
            std::string label = strip_tags(row.substr(lgt + 1, ltd - (lgt + 1)));
            for (char &c : label)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            while (!label.empty() && (label.back() == ':' || label.back() == ' '))
                label.pop_back();
            size_t vtd = row.find("<td", ltd + 5);
            if (vtd == std::string::npos)
                continue;
            size_t vgt = row.find('>', vtd);
            size_t vend = row.find("</td>", vgt);
            if (vgt == std::string::npos || vend == std::string::npos)
                continue;
            std::string value = strip_tags(row.substr(vgt + 1, vend - (vgt + 1)));
            if (label.find("дата рождения") != std::string::npos)
                out.date_of_birth = value;
            else if (label.find("место рождения") != std::string::npos)
                out.place_of_birth = value;
            else if (label.find("дата смерти") != std::string::npos)
                out.date_of_death = value;
            else if (label.find("место смерти") != std::string::npos)
                out.place_of_death = value;
            else if (label.find("рост") != std::string::npos)
                out.height = value;
        }
    };
    fill_info("b-post__info");
    fill_info("b-person__info");
    fill_info("b-person__infotable");

    return !out.name.empty() || !out.movies.empty();
}

std::vector<CatalogItem> Client::get_person_films(const std::string &person_url)
{
    PersonDetails details;
    if (!get_person(person_url, details))
        return {};
    return details.movies;
}

std::vector<CatalogItem> Client::search(const std::string &query)
{
    std::string path = "/search/?do=search&subaction=search&q=";
    CURL *curl = curl_easy_init();
    if (curl) {
        char *esc = curl_easy_escape(curl, query.c_str(), (int)query.length());
        if (esc) {
            path += esc;
            curl_free(esc);
        }
        curl_easy_cleanup(curl);
    }

    std::string html = http_get(path);
    return parse_catalog_html(html, "search", mirror_);
}

std::vector<CatalogItem> Client::search_suggest(const std::string &query)
{
    std::vector<CatalogItem> out;
    if (query.empty())
        return out;
    const std::string html = http_post("/engine/ajax/search.php", {{"q", query}});
    // One <li> a title: <a href><span class="enty">Фильм</span> Name <span class="rating">7.1</span></a>
    for (size_t at = html.find("<li"); at != std::string::npos;) {
        const size_t next = html.find("<li", at + 3);
        const std::string chunk = html.substr(at, (next == std::string::npos ? html.size() : next) - at);
        at = next;
        const size_t a = chunk.find("<a");
        const size_t a_end = a == std::string::npos ? a : chunk.find('>', a);
        if (a_end == std::string::npos)
            continue;
        CatalogItem item;
        item.url = attr_of(chunk.substr(a, a_end - a), "href");
        // The last row is the site's "show all results" link, not a title
        if (item.url.find(".html") == std::string::npos)
            continue;
        std::string inner = inner_after(chunk, a, "</a>");
        const size_t enty = find_class(inner, "enty");
        if (enty != std::string::npos) {
            item.entity = strip_tags(inner_after(inner, enty, "</span>"));
            const size_t open = inner.rfind("<span", enty);
            const size_t close = inner.find("</span>", enty);
            if (open != std::string::npos && close != std::string::npos)
                inner.erase(open, close + 7 - open);
        }
        const size_t rating = find_class(inner, "rating");
        if (rating != std::string::npos) {
            const std::string text = strip_tags(inner_after(inner, rating, "</span>"));
            item.rating = static_cast<float>(atof(text.c_str()));
            const size_t open = inner.rfind("<span", rating);
            const size_t close = inner.find("</span>", rating);
            if (open != std::string::npos && close != std::string::npos)
                inner.erase(open, close + 7 - open);
        }
        item.title = strip_tags(inner);
        if (item.title.empty())
            continue;
        if (item.url.rfind("http", 0) != 0)
            item.url = mirror_ + (item.url[0] == '/' ? "" : "/") + item.url;
        // "Name (2021)" or "Name (Original, 2021)": the year is the last number in brackets
        const size_t open = item.title.rfind('(');
        if (open != std::string::npos && item.title.back() == ')') {
            const std::string tail = item.title.substr(open + 1, item.title.size() - open - 2);
            const size_t comma = tail.rfind(',');
            const std::string year = comma == std::string::npos ? tail : tail.substr(comma + 1);
            item.year = atoi(year.c_str());
            item.info = strip_tags(year);
        }
        out.push_back(std::move(item));
    }
    return out;
}

std::string Client::stt_recognize(const std::string &endpoint, const std::vector<unsigned char> &wav)
{
    if (endpoint.empty() || wav.empty())
        return "";
    CURL *curl = curl_easy_init();
    if (!curl)
        return "";
    struct curl_slist *headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: audio/wav");
    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, endpoint.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, reinterpret_cast<const char *>(wav.data()));
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(wav.size()));
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    const CURLcode res = curl_easy_perform(curl);
    long code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK || code < 200 || code >= 300)
        return "";
    // {"text": "..."}: the value of the first "text" key; otherwise the body is the text
    const size_t key = response.find("\"text\"");
    if (key != std::string::npos) {
        const size_t colon = response.find(':', key);
        const size_t open = colon == std::string::npos ? colon : response.find('"', colon);
        if (open == std::string::npos)
            return "";
        std::string text;
        for (size_t i = open + 1; i < response.size() && response[i] != '"'; ++i) {
            if (response[i] == '\\' && i + 1 < response.size()) {
                const char e = response[++i];
                if (e == 'n' || e == 't') text.push_back(' ');
                else if (e == 'u' && i + 4 < response.size()) {
                    const unsigned cp = static_cast<unsigned>(strtoul(response.substr(i + 1, 4).c_str(), nullptr, 16));
                    i += 4;
                    if (cp < 0x80) text.push_back(static_cast<char>(cp));
                    else if (cp < 0x800) {
                        text.push_back(static_cast<char>(0xc0 | (cp >> 6)));
                        text.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
                    } else {
                        text.push_back(static_cast<char>(0xe0 | (cp >> 12)));
                        text.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
                        text.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
                    }
                } else text.push_back(e);
            } else
                text.push_back(response[i]);
        }
        return strip_tags(text);
    }
    return strip_tags(response);
}

bool Client::get_details(const std::string &item_url, VideoDetails &out_details)
{
    std::string html = http_get(item_url);
    if (html.empty()) return false;

    // Title: <div class="b-post__title"><h1>...</h1></div>
    size_t title_pos = html.find("b-post__title");
    if (title_pos != std::string::npos) {
        size_t h1_open = html.find("<h1>", title_pos);
        size_t h1_close = html.find("</h1>", h1_open);
        if (h1_open != std::string::npos && h1_close != std::string::npos) {
            out_details.name = strip_tags(html.substr(h1_open + 4, h1_close - (h1_open + 4)));
        }
    }

    // Original title: <div class="b-post__origtitle">...</div>
    size_t orig_pos = html.find("b-post__origtitle");
    if (orig_pos != std::string::npos) {
        size_t tag_close = html.find(">", orig_pos);
        size_t div_close = html.find("</div>", tag_close);
        if (tag_close != std::string::npos && div_close != std::string::npos) {
            out_details.original_name = strip_tags(html.substr(tag_close + 1, div_close - (tag_close + 1)));
        }
    }

    // Cover: <div class="b-sidecover"><a href="..."><img src="..."
    size_t cover_pos = html.find("b-sidecover");
    if (cover_pos != std::string::npos) {
        size_t src_pos = html.find("src=\"", cover_pos);
        if (src_pos != std::string::npos) {
            size_t src_end = html.find("\"", src_pos + 5);
            if (src_end != std::string::npos) out_details.cover_url = html.substr(src_pos + 5, src_end - (src_pos + 5));
        }
    }
    if (!out_details.cover_url.empty() && out_details.cover_url.rfind("http", 0) != 0) {
        out_details.cover_url = mirror_ + (out_details.cover_url.front() == '/' ? "" : "/") + out_details.cover_url;
    }

    // Description: <div class="b-post__description_text">...</div>
    size_t desc_pos = html.find("b-post__description_text");
    if (desc_pos != std::string::npos) {
        size_t tag_close = html.find(">", desc_pos);
        size_t div_close = html.find("</div>", tag_close);
        if (tag_close != std::string::npos && div_close != std::string::npos) {
            out_details.description = strip_tags(html.substr(tag_close + 1, div_close - (tag_close + 1)));
        }
    }

    // 1. Video ID: check #post_id, data-post_id, initCDN, or URL
    if (out_details.id.empty()) {
        size_t pid = html.find("id=\"post_id\"");
        if (pid == std::string::npos) pid = html.find("name=\"post_id\"");
        if (pid != std::string::npos) {
            size_t vp = html.find("value=\"", pid);
            if (vp != std::string::npos && vp < pid + 60) {
                size_t ve = html.find("\"", vp + 7);
                if (ve != std::string::npos) out_details.id = html.substr(vp + 7, ve - (vp + 7));
            }
        }
    }
    if (out_details.id.empty()) {
        size_t dpid = html.find("data-post_id=\"");
        if (dpid != std::string::npos) {
            size_t ve = html.find("\"", dpid + 14);
            if (ve != std::string::npos) out_details.id = html.substr(dpid + 14, ve - (dpid + 14));
        }
    }
    if (out_details.id.empty()) {
        std::regex init_re("initCDN(?:Series|Movies)Events\\s*\\(\\s*(\\d+)");
        std::smatch im;
        if (std::regex_search(html, im, init_re)) {
            out_details.id = im[1].str();
        }
    }
    if (out_details.id.empty()) {
        std::regex url_id_re("/(\\d+)-[^/]*\\.html");
        std::smatch um;
        if (std::regex_search(item_url, um, url_id_re)) {
            out_details.id = um[1].str();
        }
    }

    // 2. is_series: check URL, og:type, or initCDNSeriesEvents
    if (item_url.find("/series/") != std::string::npos ||
        html.find("video.tv_series") != std::string::npos ||
        html.find("initCDNSeriesEvents") != std::string::npos) {
        out_details.is_series = true;
    }

    // 3. Parse favs token: id="ctrl_favs" value="..."
    size_t favs_pos = html.find("ctrl_favs");
    if (favs_pos != std::string::npos) {
        size_t val_pos = html.find("value=\"", favs_pos);
        if (val_pos != std::string::npos) {
            size_t val_end = html.find("\"", val_pos + 7);
            if (val_end != std::string::npos) {
                out_details.favs = html.substr(val_pos + 7, val_end - (val_pos + 7));
            }
        }
    }

    // Check embedded streams in HTML e.g. "streams":"..."
    size_t st_pos = html.find("\"streams\":");
    if (st_pos != std::string::npos) {
        size_t val_start = html.find("\"", st_pos + 10);
        if (val_start != std::string::npos) {
            size_t val_end = html.find("\"", val_start + 1);
            if (val_end != std::string::npos) {
                std::string st_raw = html.substr(val_start + 1, val_end - (val_start + 1));
                std::string clean_st;
                for (size_t i = 0; i < st_raw.length(); ++i) {
                    if (st_raw[i] == '\\' && i + 1 < st_raw.length() && st_raw[i + 1] == '/') {
                        clean_st.push_back('/');
                        i++;
                    } else {
                        clean_st.push_back(st_raw[i]);
                    }
                }
                out_details.embedded_streams = clean_st;
            }
        }
    }

    // Translators list: <li class="b-translator__item" data-translator_id="56" ...>
    size_t tr_pos = 0;
    while ((tr_pos = html.find("data-translator_id=\"", tr_pos)) != std::string::npos) {
        size_t tr_end = html.find("\"", tr_pos + 20);
        if (tr_end == std::string::npos) break;

        Translator t;
        t.id = html.substr(tr_pos + 20, tr_end - (tr_pos + 20));

        size_t tag_end = html.find(">", tr_end);
        size_t li_end = html.find("</li>", tag_end);
        if (tag_end != std::string::npos && li_end != std::string::npos) {
            t.name = strip_tags(html.substr(tag_end + 1, li_end - (tag_end + 1)));
        }
        if (t.name.empty()) t.name = "Озвучка #" + t.id;

        // Attributes: camrip, ads, director
        if (tag_end != std::string::npos) {
            size_t cam_p = html.find("data-camrip=\"", tr_pos);
            if (cam_p != std::string::npos && cam_p < tag_end) {
                size_t e = html.find("\"", cam_p + 13);
                if (e != std::string::npos) t.camrip = html.substr(cam_p + 13, e - (cam_p + 13));
            }
            size_t ads_p = html.find("data-ads=\"", tr_pos);
            if (ads_p != std::string::npos && ads_p < tag_end) {
                size_t e = html.find("\"", ads_p + 10);
                if (e != std::string::npos) t.ads = html.substr(ads_p + 10, e - (ads_p + 10));
            }
            size_t dir_p = html.find("data-director=\"", tr_pos);
            if (dir_p != std::string::npos && dir_p < tag_end) {
                size_t e = html.find("\"", dir_p + 15);
                if (e != std::string::npos) t.director = html.substr(dir_p + 15, e - (dir_p + 15));
            }
        }

        out_details.translators.push_back(t);
        tr_pos = tr_end;
    }

    // If initCDN contains default translator ID not in list, add it
    std::regex init_args_re("initCDN(?:Series|Movies)Events\\s*\\(\\s*(\\d+)\\s*,\\s*(\\d+)");
    std::smatch arg_m;
    if (std::regex_search(html, arg_m, init_args_re)) {
        std::string def_tr_id = arg_m[2].str();
        bool exists = false;
        for (const auto &t : out_details.translators) {
            if (t.id == def_tr_id) { exists = true; break; }
        }
        if (!exists) {
            Translator def_t;
            def_t.id = def_tr_id;
            def_t.name = "Основний переклад";
            out_details.translators.insert(out_details.translators.begin(), def_t);
        }
    }

    // If no translators found, default to Rezka
    if (out_details.translators.empty()) {
        Translator def;
        def.id = "238"; // Rezka Studio default
        def.name = "HDRezka Studio";
        out_details.translators.push_back(def);
    }

    // Ratings -- same selectors as the browser client (hdrezka.js parseInfoTable).
    auto parse_rating_block = [&](const char *mark, RatingInfo &out) {
        size_t p = html.find(mark);
        if (p == std::string::npos)
            return;
        size_t bold = html.find("class=\"bold\"", p);
        if (bold == std::string::npos || bold > p + 400)
            bold = html.find("class='bold'", p);
        if (bold == std::string::npos || bold > p + 400)
            return;
        size_t gt = html.find('>', bold);
        size_t lt = html.find('<', gt);
        if (gt == std::string::npos || lt == std::string::npos)
            return;
        try {
            out.value = std::stof(strip_tags(html.substr(gt + 1, lt - gt - 1)));
            out.valid = out.value > 0.0f;
        } catch (...) {
            return;
        }
        size_t votes_i = html.find("<i>", p);
        if (votes_i != std::string::npos && votes_i < p + 500) {
            size_t ve = html.find("</i>", votes_i);
            if (ve != std::string::npos) {
                std::string v = strip_tags(html.substr(votes_i + 3, ve - (votes_i + 3)));
                while (!v.empty() && (v.front() == '(' || v.front() == ' '))
                    v.erase(v.begin());
                while (!v.empty() && (v.back() == ')' || v.back() == ' '))
                    v.pop_back();
                out.votes = v;
            }
        }
    };
    {
        size_t rp = html.find("b-post__rating");
        while (rp != std::string::npos && !out_details.rating_hdrezka.valid) {
            size_t num = html.find("class=\"num\"", rp);
            if (num == std::string::npos)
                num = html.find("class='num'", rp);
            if (num != std::string::npos && num < rp + 1500) {
                size_t gt = html.find('>', num);
                size_t lt = html.find('<', gt);
                if (gt != std::string::npos && lt != std::string::npos) {
                    try {
                        out_details.rating_hdrezka.value =
                            std::stof(strip_tags(html.substr(gt + 1, lt - gt - 1)));
                        out_details.rating_hdrezka.valid = out_details.rating_hdrezka.value > 0.0f;
                        out_details.rating = out_details.rating_hdrezka.value;
                    } catch (...) {
                    }
                }
            }
            size_t votes = html.find("class=\"votes\"", rp);
            if (votes == std::string::npos)
                votes = html.find("class='votes'", rp);
            if (votes != std::string::npos && votes < rp + 1500) {
                size_t gt = html.find('>', votes);
                size_t lt = html.find('<', gt);
                if (gt != std::string::npos && lt != std::string::npos) {
                    std::string v = strip_tags(html.substr(gt + 1, lt - gt - 1));
                    while (!v.empty() && (v.front() == '(' || v.front() == ' '))
                        v.erase(v.begin());
                    while (!v.empty() && (v.back() == ')' || v.back() == ' '))
                        v.pop_back();
                    out_details.rating_hdrezka.votes = v;
                }
            }
            rp = html.find("b-post__rating", rp + 1);
        }
        parse_rating_block("b-post__info_rates imdb", out_details.rating_imdb);
        parse_rating_block("b-post__info_rates.imdb", out_details.rating_imdb);
        parse_rating_block("b-post__info_rates kp", out_details.rating_kinopoisk);
        parse_rating_block("b-post__info_rates.kp", out_details.rating_kinopoisk);
        if (!out_details.rating_hdrezka.valid) {
            if (out_details.rating_kinopoisk.valid)
                out_details.rating = out_details.rating_kinopoisk.value;
            else if (out_details.rating_imdb.valid)
                out_details.rating = out_details.rating_imdb.value;
        }
    }

    // Cast / crew -- .person-name-item like the browser client.
    {
        size_t pos = 0;
        std::map<std::string, bool> seen;
        while ((pos = html.find("person-name-item", pos)) != std::string::npos) {
            size_t tag_start = html.rfind('<', pos);
            size_t tag_end = html.find('>', pos);
            if (tag_start == std::string::npos || tag_end == std::string::npos) {
                pos += 16;
                continue;
            }
            const std::string open_tag = html.substr(tag_start, tag_end - tag_start);
            Person p;
            auto attr = [&](const char *key) -> std::string {
                std::string k = std::string(key) + "=\"";
                size_t a = open_tag.find(k);
                if (a == std::string::npos)
                    return {};
                a += k.size();
                size_t b = open_tag.find('"', a);
                if (b == std::string::npos)
                    return {};
                return open_tag.substr(a, b - a);
            };
            p.id = attr("data-id");
            p.job = attr("data-job");
            p.photo = attr("data-photo");
            size_t name_prop = html.find("itemprop=\"name\"", tag_end);
            size_t block_end = html.find("</span>", tag_end);
            if (block_end == std::string::npos)
                block_end = html.find("</a>", tag_end);
            if (name_prop != std::string::npos && name_prop < tag_end + 400) {
                size_t gt = html.find('>', name_prop);
                size_t lt = html.find('<', gt);
                if (gt != std::string::npos && lt != std::string::npos)
                    p.name = strip_tags(html.substr(gt + 1, lt - gt - 1));
            }
            if (p.name.empty() && block_end != std::string::npos)
                p.name = strip_tags(html.substr(tag_end + 1, block_end - (tag_end + 1)));
            size_t href = open_tag.find("href=\"");
            if (href == std::string::npos) {
                size_t inner_a = html.find("href=\"", tag_end);
                if (inner_a != std::string::npos && inner_a < tag_end + 200) {
                    size_t he = html.find('"', inner_a + 6);
                    if (he != std::string::npos)
                        p.url = html.substr(inner_a + 6, he - (inner_a + 6));
                }
            }
            if (!p.url.empty() && p.url.rfind("http", 0) != 0)
                p.url = mirror_ + (p.url.front() == '/' ? "" : "/") + p.url;
            const std::string key = !p.id.empty() ? p.id : p.name;
            if (!p.name.empty() && !seen[key]) {
                seen[key] = true;
                out_details.people.push_back(p);
            }
            pos = tag_end + 1;
            if (out_details.people.size() >= 40)
                break;
        }
    }

    // Info table (`.b-post__info tr`), read as the browser client reads it:
    // a label cell of class "l" and the value beside it.
    {
        size_t table = std::string::npos;
        for (size_t p = html.find("b-post__info"); p != std::string::npos; p = html.find("b-post__info", p + 1)) {
            const char next = p + 12 < html.size() ? html[p + 12] : 0;
            if (next == '"' || next == ' ') {
                table = p;
                break;
            }
        }
        if (table != std::string::npos) {
            size_t table_end = html.find("</table>", table);
            if (table_end == std::string::npos) table_end = html.size();
            for (size_t tr = html.find("<tr", table); tr != std::string::npos && tr < table_end;
                 tr = html.find("<tr", tr + 3)) {
                size_t tr_end = html.find("</tr>", tr);
                if (tr_end == std::string::npos || tr_end > table_end) tr_end = table_end;
                const std::vector<RowCell> cells = row_cells(html.substr(tr, tr_end - tr));
                std::string label_html;
                std::string value_html;
                bool has_label = false;
                bool has_value = false;
                for (const RowCell &cell : cells) {
                    if (cell.label && !has_label) {
                        label_html = cell.html;
                        has_label = true;
                    } else if (!cell.label && !has_value) {
                        value_html = cell.html;
                        has_value = true;
                    }
                }
                if (!has_label) continue;

                std::string label = lower_text(strip_tags(label_html));
                while (!label.empty() && (label.back() == ':' || label.back() == ' ')) label.pop_back();
                const std::string value = strip_tags(value_html);

                if (label.find("слоган") != std::string::npos) {
                    out_details.slogan = value;
                } else if (label.find("дата выхода") != std::string::npos) {
                    out_details.release_date = value;
                    std::smatch ym;
                    std::regex date_year_re("(19|20)\\d{2}");
                    if (std::regex_search(value, ym, date_year_re)) out_details.year = ym[0].str();
                    for (const LinkRef &ref : links_of(value_html))
                        out_details.year_url = ref.href;
                } else if (label.find("страна") != std::string::npos) {
                    out_details.countries = names_in(value_html);
                    for (const LinkRef &ref : links_of(value_html))
                        out_details.country_urls.push_back(ref.href);
                } else if (label.find("жанр") != std::string::npos) {
                    out_details.genres = names_in(value_html);
                    for (const LinkRef &ref : links_of(value_html))
                        out_details.genre_urls.push_back(ref.href);
                } else if (label.find("возраст") != std::string::npos) {
                    std::smatch am;
                    std::regex age_re("\\d+\\+");
                    if (std::regex_search(value, am, age_re))
                        out_details.age = am[0].str();
                    else
                        out_details.age = value.substr(0, value.find(' '));
                } else if (label.find("время") != std::string::npos || label.find("длительн") != std::string::npos) {
                    out_details.duration = value;
                } else if (label.find("списки") != std::string::npos) {
                    std::regex place_re("\\(([^)]+)\\)");
                    for (const LinkRef &ref : links_of(value_html)) {
                        ListRef item;
                        item.title = ref.text;
                        item.url = ref.href;
                        std::smatch pm;
                        if (std::regex_search(ref.tail, pm, place_re)) item.place = pm[1].str();
                        out_details.lists.push_back(item);
                    }
                } else if (label.find("из серии") != std::string::npos) {
                    for (const LinkRef &ref : links_of(value_html)) {
                        ListRef item;
                        item.title = ref.text;
                        item.url = ref.href;
                        out_details.collections.push_back(item);
                    }
                }
            }
        }
    }
    out_details.has_trailer = html.find("show-trailer") != std::string::npos;

    // What the app's page shows besides: the large poster, the comments'
    // count, the franchise, the schedule, the voices' statistics and the
    // titles to watch next.
    {
        const size_t cover = html.find("b-sidecover");
        if (cover != std::string::npos) {
            const size_t a = html.find("<a ", cover);
            const size_t img = html.find("<img", cover);
            if (a != std::string::npos && img != std::string::npos && a < img)
                out_details.full_cover_url = attr_of(html.substr(a, html.find('>', a) - a), "href");
        }
        const size_t reviews = html.find("b-sideactions__reviews");
        if (reviews != std::string::npos) {
            const size_t em = html.find("<em", reviews);
            if (em != std::string::npos && em < reviews + 400)
                out_details.comments_count = std::atoi(strip_tags(inner_after(html, em, "</em>")).c_str());
        }
        out_details.blocked = html.find("class=\"b-player__restricted") != std::string::npos;

        const size_t ftitle = html.find("b-post__franchise_link_title");
        if (ftitle != std::string::npos)
            out_details.franchise_title = strip_tags(inner_after(html, ftitle, "</a>"));
        const std::string part_mark = "b-post__partcontent_item";
        for (size_t at = html.find(part_mark); at != std::string::npos;) {
            const size_t next = html.find(part_mark, at + part_mark.size());
            const size_t tag_open = html.rfind('<', at);
            const size_t tag_close = html.find('>', at);
            const std::string chunk = html.substr(at, (next == std::string::npos ? std::min(html.size(), at + 3000) : next) - at);
            FranchisePart part;
            if (tag_open != std::string::npos && tag_close != std::string::npos) {
                const std::string tag = html.substr(tag_open, tag_close - tag_open);
                part.url = attr_of(tag, "data-url");
                part.current = tag.find("current") != std::string::npos;
            }
            const auto cell = [&chunk](const char *cls) {
                const size_t c = chunk.find(cls);
                return c == std::string::npos ? std::string() : strip_tags(inner_after(chunk, c, "</div>"));
            };
            part.number = std::max(1, std::atoi(cell("td num").c_str()));
            part.title = cell("td title");
            part.year = cell("td year");
            part.rating = static_cast<float>(std::atof(cell("td rating").c_str()));
            if (!part.title.empty())
                out_details.franchises.push_back(part);
            at = next;
        }

        const std::string block_mark = "class=\"b-post__schedule_block\"";
        for (size_t at = html.find(block_mark); at != std::string::npos;) {
            const size_t next = html.find(block_mark, at + block_mark.size());
            const std::string block = html.substr(at, (next == std::string::npos ? html.find("b-post__rating_table", at) : next) - at);
            ScheduleSeason season;
            const size_t title = block.find("class=\"title\"");
            if (title != std::string::npos)
                season.title = strip_tags(inner_after(block, title, "</div>"));
            for (size_t row = block.find("<tr"); row != std::string::npos; row = block.find("<tr", row + 3)) {
                const size_t row_end = block.find("</tr>", row);
                if (row_end == std::string::npos) break;
                const std::string tr = block.substr(row, row_end - row);
                if (tr.find("load-more") != std::string::npos) continue;
                ScheduleRow r;
                const size_t c1 = tr.find("td-1"), c2 = tr.find("td-2"), c4 = tr.find("td-4");
                if (c1 == std::string::npos || c2 == std::string::npos) continue;
                r.episode = strip_tags(inner_after(tr, c1, "</td>"));
                const std::string names = inner_after(tr, c2, "</td>");
                const size_t b = names.find("<b");
                if (b != std::string::npos) r.title = strip_tags(inner_after(names, b, "</b>"));
                const size_t sp = names.find("<span");
                if (sp != std::string::npos) r.subtitle = strip_tags(inner_after(names, sp, "</span>"));
                if (c4 != std::string::npos) r.date = strip_tags(inner_after(tr, c4, "</td>"));
                r.exists = tr.find("exists-episode") != std::string::npos;
                season.rows.push_back(r);
            }
            if (!season.rows.empty())
                out_details.schedule.push_back(season);
            at = next;
        }

        const size_t stats = html.find("b-rgstats__help");
        if (stats != std::string::npos) {
            // The tip is markup inside the attribute, with tags of its own: it
            // runs from title=" to the quote that closes it.
            std::string tip;
            const size_t t0 = html.find("title=\"", stats);
            const size_t t1 = t0 == std::string::npos ? t0 : html.find('"', t0 + 7);
            if (t1 != std::string::npos && t0 < stats + 400)
                tip = decode_entities(html.substr(t0 + 7, t1 - t0 - 7));
            const std::string item_mark = "b-rgstats__list_item";
            for (size_t at = tip.find(item_mark); at != std::string::npos; at = tip.find(item_mark, at + 1)) {
                VoiceStat stat;
                const size_t t = tip.find("class=\"title\"", at);
                const size_t c = tip.find("class=\"count\"", at);
                if (t == std::string::npos || c == std::string::npos) break;
                stat.title = strip_tags(inner_after(tip, t, "</div>"));
                std::string count = strip_tags(inner_after(tip, c, "</div>"));
                for (char &ch : count)
                    if (ch == ',') ch = '.';
                stat.percent = static_cast<float>(std::atof(count.c_str()));
                if (!stat.title.empty())
                    out_details.voice_stats.push_back(stat);
            }
        }

        const size_t side = html.find("class=\"b-sidelist\"");
        if (side != std::string::npos)
            out_details.related = parse_catalog_html(html.substr(side), "related", mirror_);
    }

    // Year / genres from info table (best-effort).
    {
        std::regex year_re(">(19|20)\\d{2}<");
        std::smatch ym;
        if (out_details.year.empty() && std::regex_search(html, ym, year_re))
            out_details.year = ym[0].str().substr(1, 4);
    }

    write_log("[DEBUG] get_details extras comments=%d franchise=%zu schedule=%zu stats=%zu related=%zu full_cover=%s\n",
              out_details.comments_count, out_details.franchises.size(), out_details.schedule.size(),
              out_details.voice_stats.size(), out_details.related.size(), out_details.full_cover_url.c_str());
    write_log("[DEBUG] get_details done id=%s is_series=%d favs=%s translators=%zu "
              "rating=%.1f people=%zu kp=%d imdb=%d\n",
              out_details.id.c_str(), (int)out_details.is_series, out_details.favs.c_str(),
              out_details.translators.size(), out_details.rating, out_details.people.size(),
              (int)out_details.rating_kinopoisk.valid, (int)out_details.rating_imdb.valid);
    return true;
}

// The value of a JSON string member, unescaped (\" \/ \n \uXXXX).
static std::string json_string(const std::string &json, const char *key)
{
    const std::string mark = std::string("\"") + key + "\":\"";
    const size_t at = json.find(mark);
    if (at == std::string::npos) return "";
    std::string out;
    for (size_t i = at + mark.size(); i < json.size(); ++i) {
        const char c = json[i];
        if (c == '"') break;
        if (c != '\\' || i + 1 >= json.size()) {
            out.push_back(c);
            continue;
        }
        const char e = json[++i];
        if (e == 'n') out.push_back('\n');
        else if (e == 't') out.push_back(' ');
        else if (e == 'r') continue;
        else if (e == 'u' && i + 4 < json.size()) {
            unsigned long cp = std::strtoul(json.substr(i + 1, 4).c_str(), nullptr, 16);
            i += 4;
            if (cp >= 0xd800 && cp < 0xdc00 && i + 6 < json.size() && json[i + 1] == '\\' && json[i + 2] == 'u') {
                const unsigned long low = std::strtoul(json.substr(i + 3, 4).c_str(), nullptr, 16);
                cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
                i += 6;
            }
            out += decode_entities("&#" + std::to_string(cp) + ";");
        } else
            out.push_back(e);
    }
    return out;
}

bool Client::get_comments(const std::string &post_id, int page, std::vector<Comment> &out, bool *has_next)
{
    out.clear();
    if (has_next) *has_next = false;
    if (post_id.empty()) return false;
    const std::string json = http_get("/ajax/get_comments/?t=" + std::to_string(static_cast<long long>(time(nullptr)) * 1000) +
                                      "&news_id=" + post_id + "&cstart=" + std::to_string(std::max(1, page)) +
                                      "&type=0&comment_id=0&skin=hdrezka");
    const std::string html = json_string(json, "comments");
    const std::string nav = json_string(json, "navigation");
    const std::string mark = "comments-tree-item";
    for (size_t at = html.find(mark); at != std::string::npos;) {
        const size_t next = html.find(mark, at + mark.size());
        const size_t tag_open = html.rfind('<', at);
        const size_t tag_close = html.find('>', at);
        if (tag_open == std::string::npos || tag_close == std::string::npos) break;
        const std::string tag = html.substr(tag_open, tag_close - tag_open);
        // A reply's item is nested in its parent's: the chunk ends where the next item starts
        const std::string chunk = html.substr(at, (next == std::string::npos ? html.size() : next) - at);
        Comment c;
        c.id = attr_of(tag, "data-id");
        c.indent = std::atoi(attr_of(tag, "data-indent").c_str());
        const size_t ava = chunk.find("class=\"ava\"");
        if (ava != std::string::npos) {
            const size_t img = chunk.find("<img", ava);
            if (img != std::string::npos)
                c.avatar = attr_of(chunk.substr(img, chunk.find('>', img) - img), "src");
        }
        const size_t name = chunk.find("class=\"name\"");
        if (name != std::string::npos) c.author = strip_tags(inner_after(chunk, name, "</span>"));
        const size_t date = chunk.find("class=\"date\"");
        if (date != std::string::npos) {
            c.date = strip_tags(inner_after(chunk, date, "</span>"));
            const std::string left = "\xd0\xbe\xd1\x81\xd1\x82\xd0\xb0\xd0\xb2\xd0\xbb\xd0\xb5\xd0\xbd"; // "оставлен"
            const size_t l = c.date.find(left);
            if (l != std::string::npos) c.date.erase(l, left.size());
            while (!c.date.empty() && c.date.front() == ' ') c.date.erase(0, 1);
        }
        const size_t likes = chunk.find("b-comment__likes_count");
        if (likes != std::string::npos) {
            const size_t i = chunk.find("<i", likes);
            if (i != std::string::npos) c.likes = std::atoi(strip_tags(inner_after(chunk, i, "</i>")).c_str());
        }
        c.admin = chunk.find("b-comment__admin") != std::string::npos;
        c.spoiler = chunk.find("text_spoiler") != std::string::npos;
        const size_t text = chunk.find("class=\"text\"");
        if (text != std::string::npos) {
            const size_t inner = chunk.find("<div", text);
            size_t stop = chunk.find("class=\"actions\"", text);
            if (stop == std::string::npos) stop = chunk.find("b-comment__likes", text);
            if (inner != std::string::npos) {
                std::string body = chunk.substr(inner, (stop == std::string::npos ? chunk.size() : stop) - inner);
                // Line breaks survive the tag stripping as a marker
                for (size_t br = body.find("<br"); br != std::string::npos; br = body.find("<br", br + 1)) {
                    const size_t gt = body.find('>', br);
                    if (gt == std::string::npos) break;
                    body.replace(br, gt - br + 1, " \x01 ");
                }
                body = strip_tags(body);
                std::string lines;
                for (size_t i = 0; i < body.size(); ++i) {
                    if (body[i] != '\x01') {
                        lines.push_back(body[i]);
                        continue;
                    }
                    while (!lines.empty() && lines.back() == ' ') lines.pop_back();
                    lines.push_back('\n');
                    while (i + 1 < body.size() && body[i + 1] == ' ') ++i;
                }
                c.text = lines;
            }
        }
        if (!c.author.empty() || !c.text.empty())
            out.push_back(c);
        at = next;
    }
    if (has_next && nav.find("b-navigation") != std::string::npos) {
        const size_t next = nav.find("b-navigation__next");
        bool disabled = false;
        if (next != std::string::npos) {
            const size_t no_page = nav.rfind("no-page", next);
            disabled = no_page != std::string::npos && no_page + 160 >= next;
        }
        *has_next = next != std::string::npos && !disabled;
    }
    write_log("[DEBUG] get_comments post=%s page=%d -> %zu\n", post_id.c_str(), page, out.size());
    return !out.empty();
}

bool Client::get_trailer(const std::string &post_id, std::string &youtube_id)
{
    youtube_id.clear();
    if (post_id.empty()) return false;
    std::map<std::string, std::string> fields;
    fields["id"] = post_id;
    const std::string response = http_post("/engine/ajax/gettrailervideo.php", fields);
    // The answer is JSON whose "code" holds an embed frame; its slashes may be escaped.
    static const std::regex yt_re("(?:youtube\\.com|youtu\\.be)\\\\?/(?:embed\\\\?/)?([A-Za-z0-9_-]{6,})");
    std::smatch m;
    if (std::regex_search(response, m, yt_re)) youtube_id = m[1].str();
    write_log("[DEBUG] get_trailer post=%s youtube=%s resp_len=%zu\n", post_id.c_str(),
              youtube_id.c_str(), response.size());
    return !youtube_id.empty();
}

bool Client::get_episodes(const VideoDetails &video,
                          const std::string &translator_id,
                          std::vector<Season> &out_seasons)
{
    out_seasons.clear();
    std::map<std::string, std::string> fields;
    fields["id"] = video.id;
    fields["translator_id"] = translator_id.empty() ? "238" : translator_id;
    fields["action"] = "get_episodes";
    if (!video.favs.empty()) {
        fields["favs"] = video.favs;
    }

    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::system_clock::now().time_since_epoch()).count();
    std::string path = "/ajax/get_cdn_series/?t=" + std::to_string(now);
    std::string response = http_post(path, fields);
    write_log("[DEBUG] get_episodes id=%s tr=%s resp_len=%zu\n",
              video.id.c_str(), translator_id.c_str(), response.size());
    if (response.empty()) {
        Season s;
        s.id = "1";
        s.title = "Сезон 1";
        for (int e = 1; e <= 12; ++e) {
            Episode ep;
            ep.id = std::to_string(e);
            ep.title = "Серия " + std::to_string(e);
            s.episodes.push_back(ep);
        }
        out_seasons.push_back(s);
        return true;
    }

    size_t s_pos = 0;
    while ((s_pos = response.find("data-tab_id=\"", s_pos)) != std::string::npos) {
        size_t s_end = response.find("\"", s_pos + 13);
        if (s_end == std::string::npos) break;
        std::string sid = response.substr(s_pos + 13, s_end - (s_pos + 13));

        size_t tag_end = response.find(">", s_end);
        size_t li_end = response.find("</li>", tag_end);
        std::string s_title = "Сезон " + sid;
        if (tag_end != std::string::npos && li_end != std::string::npos) {
            s_title = strip_tags(response.substr(tag_end + 1, li_end - (tag_end + 1)));
        }

        Season s;
        s.id = sid;
        s.title = s_title;
        for (int e = 1; e <= 12; ++e) {
            Episode ep;
            ep.id = std::to_string(e);
            ep.title = "Серия " + std::to_string(e);
            s.episodes.push_back(ep);
        }
        out_seasons.push_back(s);
        s_pos = s_end;
    }

    if (out_seasons.empty()) {
        Season s;
        s.id = "1";
        s.title = "Сезон 1";
        for (int e = 1; e <= 12; ++e) {
            Episode ep;
            ep.id = std::to_string(e);
            ep.title = "Серия " + std::to_string(e);
            s.episodes.push_back(ep);
        }
        out_seasons.push_back(s);
    }
    return true;
}

std::vector<StreamQuality> Client::get_streams(const VideoDetails &video,
                                               const std::string &translator_id,
                                               const std::string &season_id,
                                               const std::string &episode_id)
{
    std::map<std::string, std::string> fields;
    fields["id"] = video.id;
    fields["translator_id"] = translator_id.empty() ? "238" : translator_id;
    if (!video.favs.empty()) {
        fields["favs"] = video.favs;
    }

    if (video.is_series) {
        fields["action"] = "get_stream";
        fields["season"] = season_id.empty() ? "1" : season_id;
        fields["episode"] = episode_id.empty() ? "1" : episode_id;
    } else {
        std::string camrip = "0", ads = "0", director = "0";
        for (const auto &t : video.translators) {
            if (t.id == translator_id) {
                camrip = t.camrip;
                ads = t.ads;
                director = t.director;
                break;
            }
        }
        fields["action"] = "get_movie";
        fields["is_camrip"] = camrip;
        fields["is_ads"] = ads;
        fields["is_director"] = director;
    }

    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::system_clock::now().time_since_epoch()).count();
    std::string path = "/ajax/get_cdn_series/?t=" + std::to_string(now);
    std::string response = http_post(path, fields);
    write_log("[DEBUG] get_streams req id=%s tr=%s s=%s e=%s favs=%s resp_len=%zu\n",
              video.id.c_str(), translator_id.c_str(), season_id.c_str(), episode_id.c_str(),
              video.favs.c_str(), response.size());

    if (response.empty()) {
        if (!video.is_series && !video.embedded_streams.empty()) {
            std::string decoded = clear_trash(video.embedded_streams);
            return parse_stream_string(decoded);
        }
        return {};
    }

    // Response is JSON: { "success": true, "url": "...", "subtitle": "..." }
    size_t url_key = response.find("\"url\":\"");
    if (url_key == std::string::npos) {
        url_key = response.find("\"url\": \"");
    }
    if (url_key == std::string::npos) {
        if (!video.is_series && !video.embedded_streams.empty()) {
            std::string decoded = clear_trash(video.embedded_streams);
            return parse_stream_string(decoded);
        }
        return {};
    }

    size_t val_start = response.find("\"", url_key + 6) + 1;
    size_t val_end = response.find("\"", val_start);
    if (val_end == std::string::npos) return {};

    std::string obfuscated = response.substr(val_start, val_end - val_start);
    // Unescape forward slashes if \/
    std::string clean_obf;
    for (size_t i = 0; i < obfuscated.length(); ++i) {
        if (obfuscated[i] == '\\' && i + 1 < obfuscated.length() && obfuscated[i + 1] == '/') {
            clean_obf.push_back('/');
            i++;
        } else {
            clean_obf.push_back(obfuscated[i]);
        }
    }

    std::string decoded = clear_trash(clean_obf);
    auto streams = parse_stream_string(decoded);
    write_log("[DEBUG] get_streams decoded %zu qualities\n", streams.size());
    return streams;
}

} // namespace rezka
