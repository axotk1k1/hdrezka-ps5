#pragma once
// The persistent cache under the data folder (data/cache/):
//   images/   posters, full covers, actors' photos: kept until the size limit
//   catalog/  the list pages (home tabs, Новинки, Анонсы): the last answer of
//             each, shown at once while the network is asked again
//   meta/     the titles' pages (24 h) and the actors' pages (7 days)
//   last_mirror.txt  the mirror that answered last
// A file is keyed by its address without the mirror's domain, so another
// mirror finds the same files. Every write goes to a .tmp file renamed into
// place: a title closed half-way leaves no cut picture behind. Any thread may
// call any of these; none of them is for the render thread's frame loop.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace rezka::disk_cache
{

enum class Bucket : int
{
    images,
    catalog,
    meta,
};

inline constexpr long kTitleTtl = 24L * 60 * 60;      // a title's page
inline constexpr long kPersonTtl = 7L * 24 * 60 * 60; // an actor's filmography
inline constexpr uint64_t kSizeLimit = 500ull * 1024 * 1024;

// Takes the first of the folders where the cache's folders can be made and
// written. False when none can (the cache then stays off).
bool init(const std::vector<std::string> &roots);
bool ready();

// The address without its scheme and domain ("/i/2026/8/26/x.jpg"), hashed.
std::string url_key(const std::string &url);

// max_age: seconds since the file was written; 0 takes it at any age.
bool read(Bucket bucket, const std::string &key, std::string &out, long max_age = 0);
bool read(Bucket bucket, const std::string &key, std::vector<uint8_t> &out, long max_age = 0);
bool write(Bucket bucket, const std::string &key, const void *data, size_t size);
void remove(Bucket bucket, const std::string &key);
// Every file of a bucket.
void clear_bucket(Bucket bucket);

// A small text kept at the cache's root (last_mirror.txt).
std::string read_note(const char *name);
void write_note(const char *name, const std::string &text);

// Bytes on disk: counted by trim() at start, then kept up to date.
uint64_t size_bytes();
// Counts the files and, above `limit`, removes the oldest until it is 90% of it.
void trim(uint64_t limit = kSizeLimit);
// Everything goes.
void clear();

} // namespace rezka::disk_cache
