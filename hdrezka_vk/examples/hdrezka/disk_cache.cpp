#include "disk_cache.hpp"

#include "logger.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <ctime>
#include <dirent.h>
#include <mutex>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

namespace rezka::disk_cache
{

namespace
{

std::mutex g_root_mu;
std::string g_root; // ends with '/'; empty while the cache is off
std::atomic<bool> g_ready{false};
std::atomic<int64_t> g_size{0};
std::atomic<uint32_t> g_tmp_serial{0};
std::mutex g_trim_mu; // one trim or clear at a time

const char *bucket_dir(Bucket bucket)
{
    switch (bucket) {
    case Bucket::images: return "images/";
    case Bucket::catalog: return "catalog/";
    default: return "meta/";
    }
}

std::string root_copy()
{
    std::lock_guard<std::mutex> lock(g_root_mu);
    return g_root;
}

std::string file_path(Bucket bucket, const std::string &key)
{
    const std::string base = root_copy();
    if (base.empty() || key.empty())
        return {};
    return base + bucket_dir(bucket) + key;
}

bool make_dir(const std::string &path)
{
    if (mkdir(path.c_str(), 0777) == 0 || errno == EEXIST)
        return true;
    struct stat st{};
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

int64_t file_size(const std::string &path)
{
    struct stat st{};
    return stat(path.c_str(), &st) == 0 ? static_cast<int64_t>(st.st_size) : -1;
}

void sub_size(int64_t n)
{
    int64_t now = g_size.load();
    while (!g_size.compare_exchange_weak(now, std::max<int64_t>(0, now - n))) {
    }
}

// The file at `path` if it is not older than max_age; a picture read is
// marked used (its date moved on, at most once a day) for the trim's LRU.
bool open_fresh(const std::string &path, long max_age, bool touch, FILE *&f, size_t &size)
{
    f = nullptr;
    struct stat st{};
    if (path.empty() || stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0)
        return false;
    const time_t now = time(nullptr);
    if (max_age > 0 && now - st.st_mtime > max_age)
        return false;
    f = fopen(path.c_str(), "rb");
    if (!f)
        return false;
    size = static_cast<size_t>(st.st_size);
    if (touch && now - st.st_mtime > 24 * 60 * 60)
        utimes(path.c_str(), nullptr);
    return true;
}

template <class Buffer> bool read_into(Bucket bucket, const std::string &key, Buffer &out, long max_age)
{
    out.clear();
    if (!g_ready)
        return false;
    FILE *f = nullptr;
    size_t size = 0;
    if (!open_fresh(file_path(bucket, key), max_age, bucket == Bucket::images, f, size))
        return false;
    out.resize(size);
    const size_t got = fread(&out[0], 1, size, f);
    fclose(f);
    if (got != size) {
        out.clear();
        return false;
    }
    return true;
}

struct Entry
{
    std::string path;
    int64_t size = 0;
    time_t mtime = 0;
};

void list_files(const std::string &dir, std::vector<Entry> &out)
{
    DIR *d = opendir(dir.c_str());
    if (!d)
        return;
    while (struct dirent *e = readdir(d)) {
        const std::string name = e->d_name;
        if (name == "." || name == "..")
            continue;
        Entry entry;
        entry.path = dir + name;
        struct stat st{};
        if (stat(entry.path.c_str(), &st) != 0 || !S_ISREG(st.st_mode))
            continue;
        entry.size = static_cast<int64_t>(st.st_size);
        entry.mtime = st.st_mtime;
        out.push_back(std::move(entry));
    }
    closedir(d);
}

std::vector<Entry> all_files(const std::string &base)
{
    std::vector<Entry> files;
    for (Bucket b : {Bucket::images, Bucket::catalog, Bucket::meta})
        list_files(base + bucket_dir(b), files);
    return files;
}

} // namespace

bool init(const std::vector<std::string> &roots)
{
    for (std::string base : roots) {
        if (base.empty())
            continue;
        if (base.back() != '/')
            base += '/';
        // The data folder itself, then the cache's
        const std::string parent = base.substr(0, base.find_last_of('/', base.size() - 2) + 1);
        if (!parent.empty())
            make_dir(parent);
        if (!make_dir(base))
            continue;
        bool ok = true;
        for (Bucket b : {Bucket::images, Bucket::catalog, Bucket::meta})
            ok = ok && make_dir(base + bucket_dir(b));
        if (!ok)
            continue;
        const std::string probe = base + ".probe";
        FILE *f = fopen(probe.c_str(), "wb");
        if (!f)
            continue;
        fclose(f);
        ::remove(probe.c_str());
        {
            std::lock_guard<std::mutex> lock(g_root_mu);
            g_root = base;
        }
        g_ready = true;
        write_log("[DEBUG] disk cache at %s\n", base.c_str());
        return true;
    }
    write_log("[DEBUG] disk cache: no writable folder, cache off\n");
    return false;
}

bool ready()
{
    return g_ready;
}

std::string url_key(const std::string &url)
{
    std::string path = url;
    // "https://host/path", "//host/path": the path only
    size_t start = std::string::npos;
    if (path.rfind("//", 0) == 0)
        start = 2;
    else if (const size_t scheme = path.find("://"); scheme != std::string::npos && scheme < 10)
        start = scheme + 3;
    if (start != std::string::npos) {
        const size_t slash = path.find('/', start);
        path = slash == std::string::npos ? "/" : path.substr(slash);
    }
    if (path.empty() || path.front() != '/')
        path.insert(path.begin(), '/');
    // FNV-1a, 64 bits
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : path) {
        h ^= c;
        h *= 1099511628211ull;
    }
    char hex[17];
    snprintf(hex, sizeof(hex), "%016llx", static_cast<unsigned long long>(h));
    return hex;
}

bool read(Bucket bucket, const std::string &key, std::string &out, long max_age)
{
    return read_into(bucket, key, out, max_age);
}

bool read(Bucket bucket, const std::string &key, std::vector<uint8_t> &out, long max_age)
{
    return read_into(bucket, key, out, max_age);
}

bool write(Bucket bucket, const std::string &key, const void *data, size_t size)
{
    if (!g_ready || !data || size == 0)
        return false;
    const std::string path = file_path(bucket, key);
    if (path.empty())
        return false;
    const std::string tmp = path + ".tmp" + std::to_string(g_tmp_serial.fetch_add(1));
    FILE *f = fopen(tmp.c_str(), "wb");
    if (!f)
        return false;
    const bool written = fwrite(data, 1, size, f) == size;
    const bool closed = fclose(f) == 0;
    if (!written || !closed) {
        ::remove(tmp.c_str());
        return false;
    }
    const int64_t before = file_size(path);
    if (rename(tmp.c_str(), path.c_str()) != 0) {
        ::remove(tmp.c_str());
        return false;
    }
    g_size += static_cast<int64_t>(size);
    if (before > 0)
        sub_size(before);
    return true;
}

void remove(Bucket bucket, const std::string &key)
{
    const std::string path = file_path(bucket, key);
    if (path.empty())
        return;
    const int64_t n = file_size(path);
    if (n >= 0 && ::remove(path.c_str()) == 0)
        sub_size(n);
}

void clear_bucket(Bucket bucket)
{
    const std::string base = root_copy();
    if (base.empty())
        return;
    std::lock_guard<std::mutex> lock(g_trim_mu);
    std::vector<Entry> files;
    list_files(base + bucket_dir(bucket), files);
    for (const Entry &e : files)
        if (::remove(e.path.c_str()) == 0)
            sub_size(e.size);
}

std::string read_note(const char *name)
{
    const std::string base = root_copy();
    if (base.empty())
        return {};
    FILE *f = fopen((base + name).c_str(), "rb");
    if (!f)
        return {};
    char buf[512];
    const size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    std::string text(buf, n);
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' '))
        text.pop_back();
    return text;
}

void write_note(const char *name, const std::string &text)
{
    const std::string base = root_copy();
    if (base.empty())
        return;
    const std::string path = base + name;
    const std::string tmp = path + ".tmp" + std::to_string(g_tmp_serial.fetch_add(1));
    FILE *f = fopen(tmp.c_str(), "wb");
    if (!f)
        return;
    const bool ok = fwrite(text.data(), 1, text.size(), f) == text.size();
    if (fclose(f) != 0 || !ok || rename(tmp.c_str(), path.c_str()) != 0)
        ::remove(tmp.c_str());
}

uint64_t size_bytes()
{
    return static_cast<uint64_t>(std::max<int64_t>(0, g_size.load()));
}

void trim(uint64_t limit)
{
    const std::string base = root_copy();
    if (base.empty())
        return;
    std::lock_guard<std::mutex> lock(g_trim_mu);
    std::vector<Entry> files = all_files(base);
    int64_t total = 0;
    for (Entry &e : files) {
        // A .tmp the title was closed on: never renamed, never to be read
        if (e.path.find(".tmp") != std::string::npos && time(nullptr) - e.mtime > 60) {
            ::remove(e.path.c_str());
            e.size = 0;
            continue;
        }
        total += e.size;
    }
    if (static_cast<uint64_t>(total) > limit) {
        std::sort(files.begin(), files.end(), [](const Entry &a, const Entry &b) { return a.mtime < b.mtime; });
        const int64_t target = static_cast<int64_t>(limit / 10 * 9);
        size_t removed = 0;
        for (const Entry &e : files) {
            if (total <= target)
                break;
            if (e.size > 0 && ::remove(e.path.c_str()) == 0) {
                total -= e.size;
                ++removed;
            }
        }
        write_log("[DEBUG] disk cache trimmed: %zu files removed\n", removed);
    }
    g_size = total;
    write_log("[DEBUG] disk cache: %lld bytes\n", static_cast<long long>(total));
}

void clear()
{
    const std::string base = root_copy();
    if (base.empty())
        return;
    {
        std::lock_guard<std::mutex> lock(g_trim_mu);
        for (const Entry &e : all_files(base))
            ::remove(e.path.c_str());
        ::remove((base + "last_mirror.txt").c_str());
    }
    g_size = 0;
}

} // namespace rezka::disk_cache
