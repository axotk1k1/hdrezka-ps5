#include "hdrezka_app.hpp"
#include <iostream>
#include <algorithm>
#include <cctype>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <curl/curl.h>

#include "logger.hpp"
#include "platform.h"

namespace
{
// A file kept beside the others in the data folder (continue.txt, favorites.txt).
std::string data_file_path(const std::string &cookie_file, const std::string &name)
{
    if (!cookie_file.empty()) {
        const auto slash = cookie_file.find_last_of("/\\");
        if (slash != std::string::npos)
            return cookie_file.substr(0, slash + 1) + name;
    }
    const std::string folders[] = {
        "/app0/data/",
        "/data/homebrew/PPSA99380/data/",
        "/mnt/usb0/homebrew/PPSA99380/data/",
        "data/",
    };
    for (const std::string &folder : folders) {
        FILE *f = fopen((folder + name).c_str(), "ab");
        if (f) {
            fclose(f);
            return folder + name;
        }
    }
    return name;
}

// A request the player is waiting on: the poster worker stands aside for it.
struct UrgentRequest {
    std::atomic<int> &n;
    explicit UrgentRequest(std::atomic<int> &count) : n(count) { ++n; }
    ~UrgentRequest() { --n; }
};
} // namespace

extern "C" void console_write_log(const char* fmt, ...) {
    std::lock_guard<std::mutex> lock(write_log_mutex());
    FILE* f = fopen("/data/homebrew/PPSA99380/data/debug.log", "a");
    if (!f) f = fopen("/mnt/usb0/debug.log", "a");
    if (!f) return;
    va_list args;
    va_start(args, fmt);
    vfprintf(f, fmt, args);
    va_end(args);
    fclose(f);
}

namespace rezka
{

void App::init_defaults()
{
    // Live catalog is loaded directly from HDRezka API in load_category_async().
    // No hardcoded demo seeds needed.
    std::lock_guard<std::mutex> lock(catalog_mutex);
    movies.clear();
}

void App::init(vks::VulkanDevice *dev, VkQueue q, hui::gfx::VkRenderer *rend, hui::audio::Mixer *mix)
{
    write_log("[DEBUG] App::init started\n");
    device = dev;
    queue = q;
    renderer = rend;
    mixer = mix;

    // Detect cookies file
    const char *cookie_paths[] = {
        "/app0/data/cookies.txt",
        "/data/homebrew/PPSA99380/data/cookies.txt",
        "/mnt/usb0/homebrew/PPSA99380/data/cookies.txt",
        "/mnt/usb1/homebrew/PPSA99380/data/cookies.txt",
        "data/cookies.txt",
        "./data/cookies.txt",
        "../data/cookies.txt",
        "/home/axotn1k1/vscode/hdrezka-ps5/data/cookies.txt"
    };
    for (const char *cp : cookie_paths) {
        if (access(cp, R_OK) == 0) {
            client.set_cookie_file(cp);
            break;
        }
    }

    player.set_audio_ring(&audio_ring);
    load_continue_map();
    load_favorites();
    load_search_history();

    {
        CURLcode gi = curl_global_init(CURL_GLOBAL_DEFAULT);
        curl_version_info_data *vi = curl_version_info(CURLVERSION_NOW);
        write_log("[DEBUG] App::init curl_global_init rc=%d (%s), curl %s, ssl %s\n",
                  (int)gi, curl_easy_strerror(gi),
                  vi && vi->version ? vi->version : "?",
                  vi && vi->ssl_version ? vi->ssl_version : "none");
    }
    write_log("[DEBUG] App::init cookie file: '%s'\n", client.get_cookie_file().c_str());

    init_defaults();
    write_log("[DEBUG] App::init init_defaults done\n");
    {
        std::lock_guard<std::mutex> lock(catalog_mutex);
        catalog_status = "Завантаження каталогу HDRezka...";
    }

    if (client.get_cookie_file().empty()) {
        const char *writable[] = {
            "/download0/cookies.txt",
            "/data/homebrew/PPSA99380/data/cookies.txt",
            "/mnt/usb0/homebrew/PPSA99380/data/cookies.txt",
#if defined(PS5_HOST_REFERENCE)
            "cookies.txt", // the reference build: in its title folder
#endif
        };
        for (const char *cp : writable) {
            FILE *f = fopen(cp, "ab");
            if (f) {
                fclose(f);
                client.set_cookie_file(cp);
                break;
            }
        }
    }

    // Preload local poster textures
    if (renderer) {
        std::lock_guard<std::mutex> lock(catalog_mutex);
        for (size_t i = 0; i < movies.size(); ++i) {
            if (movies[i].local_poster.empty()) continue;
            int w = 0, h = 0;
            const char *prefixes[] = {
                "/app0/assets/posters/",
                "/data/homebrew/PPSA99380/assets/posters/",
                "/mnt/usb0/homebrew/PPSA99380/assets/posters/",
                "/mnt/usb1/homebrew/PPSA99380/assets/posters/",
                "assets/posters/",
                "./assets/posters/",
                "../assets/posters/",
                "dist/PPSA99380/assets/posters/",
                "/home/axotn1k1/vscode/hdrezka-ps5/dist/usb/homebrew/PPSA99380/assets/posters/",
                "/home/axotn1k1/vscode/hdrezka-ps5/hdrezka_vk/ps5/assets/posters/",
                "/home/axotn1k1/vscode/hdrezka-ps5/www/img/",
                "www/img/",
                "../www/img/"
            };
            uint8_t *rgba = nullptr;
            for (const char *p : prefixes) {
                std::string full_path = std::string(p) + movies[i].local_poster;
                rgba = TextureLoader::load_file_rgba(full_path.c_str(), &w, &h);
                if (rgba && w > 0 && h > 0) break;
            }
            if (rgba && w > 0 && h > 0) {
                uint32_t handle = renderer->create_texture(w, h, rgba);
                if (handle != 0) {
                    movies[i].texture_id = handle;
                    movies[i].texture_ready = true;
                    fallback_posters.push_back(handle);
                }
                TextureLoader::free_decoded_image(rgba);
            }
        }
    }

    write_log("[DEBUG] App::init preloading done\n");
    network_started_ = false;
    write_log("[DEBUG] App::init finished\n");
}

void App::shutdown()
{
    stop_worker_ = true;
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
    stop_playback();
}

static MovieData movie_from_catalog(const CatalogItem &it, const std::string &genre)
{
    MovieData m;
    m.id = it.id;
    m.title = it.title;
    m.url = it.url;
    m.cover_url = it.cover_url;
    m.info = it.info;
    m.cover_info = it.cover_info;
    m.entity = it.entity;
    m.year = it.year;
    m.rating = it.rating;
    m.quality = it.quality;
    m.genre = !it.entity.empty() ? it.entity : genre;
    m.blurb = it.description.empty()
                  ? (it.info.empty() ? it.cover_info : it.info)
                  : it.description;
    Translator def_t;
    def_t.id = "238";
    def_t.name = "HDRezka Studio";
    m.details.translators.push_back(def_t);
    m.details.id = m.id;
    m.details.name = m.title;
    return m;
}

static void queue_posters(App *self, std::atomic<bool> &stop, size_t from, size_t to)
{
    for (size_t i = from; i < to; ++i) {
        if (stop)
            break;
        std::string cover_url;
        {
            std::lock_guard<std::mutex> lock(self->catalog_mutex);
            if (i < self->movies.size())
                cover_url = self->movies[i].cover_url;
        }
        if (cover_url.empty())
            continue;
        std::vector<uint8_t> img_bytes;
        if (!self->client.http_get_binary(cover_url, img_bytes))
            continue;
        int w = 0, h = 0;
        uint8_t *rgba = TextureLoader::decode_image_rgba(img_bytes.data(), img_bytes.size(), &w, &h);
        if (!rgba || w <= 0 || h <= 0)
            continue;
        App::PendingTexture pt;
        pt.movie_index = static_cast<int>(i);
        pt.width = w;
        pt.height = h;
        pt.rgba.assign(rgba, rgba + w * h * 4);
        TextureLoader::free_decoded_image(rgba);
        std::lock_guard<std::mutex> p_lock(self->pending_mutex);
        self->pending_textures.push_back(std::move(pt));
    }
}

void App::load_category_async(int category_idx)
{
    write_log("[DEBUG] App::load_category_async start %d\n", category_idx);
    current_category = category_idx;
    catalog_loading = true;

    if (worker_thread_.joinable()) {
        write_log("[DEBUG] App::load_category_async joining old thread\n");
        stop_worker_ = true;
        worker_thread_.join();
        stop_worker_ = false;
    }

    write_log("[DEBUG] App::load_category_async spawning thread\n");
    worker_thread_ = BigThread([this, category_idx]() {
        write_log("[DEBUG] worker_thread started\n");
        {
            std::lock_guard<std::mutex> lock(catalog_mutex);
            home_layout = false;
            home_sections.clear();
            featured_indices.clear();
            featured_movies.clear();
            catalog_status = "Подключение к HDRezka...";
            catalog_version++;
        }
        write_log("[DEBUG] worker_thread pick_mirror\n");
        if (!client.pick_mirror()) {
            write_log("[DEBUG] worker_thread pick_mirror failed\n");
            std::lock_guard<std::mutex> lock(catalog_mutex);
            catalog_status = "HDRezka не отвечает. Проверьте интернет.";
            catalog_loading = false;
            catalog_version++;
            return;
        }

        // Catalog tabs: films / series / cartoons / animation / new + filter
        struct CatMapping { const char *cat; const char *flt; };
        static const CatMapping kMappings[] = {
            { "films", "popular" },
            { "films", "last" },
            { "films", "watching" },
            { "films", "year" },
            { "films", "added" },
            { "series", "popular" },
            { "cartoons", "popular" },
            { "animation", "popular" },
            { "new", "last" },
        };
        // Screen passes: 10+category*10+filter for catalog, or legacy 4..7 for top tabs.
        int idx = category_idx;
        std::string cat = "films";
        std::string flt = "popular";
        if (category_idx >= 100) {
            const int kind = (category_idx / 100) - 1; // 0 films .. 3 anime
            const int filter = category_idx % 100;
            static const char *kCats[] = {"films", "series", "cartoons", "animation"};
            static const char *kFilters[] = {"popular", "last", "watching", "year", "added"};
            cat = kCats[std::clamp(kind, 0, 3)];
            flt = kFilters[std::clamp(filter, 0, 4)];
        } else if (category_idx >= 4 && category_idx <= 7) {
            static const char *kCats[] = {"films", "series", "cartoons", "animation"};
            cat = kCats[category_idx - 4];
            flt = "popular";
        } else {
            idx = std::clamp(category_idx, 0, (int)(sizeof(kMappings) / sizeof(kMappings[0])) - 1);
            cat = kMappings[idx].cat;
            flt = kMappings[idx].flt;
        }

        {
            std::lock_guard<std::mutex> lock(catalog_mutex);
            catalog_status = "Загрузка каталога...";
            catalog_version++;
        }

        auto items = client.get_catalog(cat, flt, 1);
        write_log("[DEBUG] worker_thread get_catalog returned %d items\n", (int)items.size());
        if (items.empty()) {
            std::lock_guard<std::mutex> lock(catalog_mutex);
            catalog_status = "Каталог HDRezka недоступен";
            catalog_loading = false;
            catalog_version++;
            return;
        }

        std::vector<MovieData> new_movies;
        new_movies.reserve(items.size());
        const std::string genre_fallback = cat.empty() ? "HDRezka" : cat;
        for (size_t i = 0; i < items.size(); ++i) {
            if (stop_worker_)
                break;
            new_movies.push_back(movie_from_catalog(items[i], genre_fallback));
        }

        if (!new_movies.empty() && !stop_worker_) {
            {
                std::lock_guard<std::mutex> lock(catalog_mutex);
                movies = std::move(new_movies);
                // Flat catalog: first few titles feed the banner.
                featured_indices.clear();
                featured_movies.clear();
                const int n = std::min(5, static_cast<int>(movies.size()));
                for (int i = 0; i < n; ++i) {
                    featured_indices.push_back(i);
                    featured_movies.push_back(movies[static_cast<size_t>(i)]);
                }
                catalog_status.clear();
                catalog_version++;
            }
            queue_posters(this, stop_worker_, 0, movies.size());
        }

        catalog_loading = false;
    });
}

void App::load_home_async(int filter_idx)
{
    write_log("[DEBUG] App::load_home_async start %d\n", filter_idx);
    home_filter = std::clamp(filter_idx, 0, 3);
    current_category = -1;
    catalog_loading = true;

    if (worker_thread_.joinable()) {
        stop_worker_ = true;
        worker_thread_.join();
        stop_worker_ = false;
    }

    const int flt_idx = home_filter;
    worker_thread_ = BigThread([this, flt_idx]() {
        static const char *kHomeFilters[] = {"watching", "last", "popular", "soon"};
        const std::string flt = kHomeFilters[std::clamp(flt_idx, 0, 3)];
        {
            std::lock_guard<std::mutex> lock(catalog_mutex);
            home_layout = true;
            catalog_status = "Подключение к HDRezka...";
            catalog_version++;
        }
        if (!client.pick_mirror()) {
            std::lock_guard<std::mutex> lock(catalog_mutex);
            catalog_status = "HDRezka не отвечает. Проверьте интернет.";
            catalog_loading = false;
            catalog_version++;
            return;
        }
        {
            std::lock_guard<std::mutex> lock(catalog_mutex);
            catalog_status = "Загрузка главной...";
            catalog_version++;
        }

        auto page = client.get_home_page(flt);
        if (stop_worker_) {
            catalog_loading = false;
            return;
        }

        constexpr int kRowCap = 8;
        std::vector<MovieData> new_movies;
        std::vector<HomeSection> sections;
        std::vector<int> hero_idx;
        std::vector<MovieData> hero_movies;

        auto append_pack = [&](const char *title, const std::vector<CatalogItem> &pack,
                               const std::string &genre) {
            HomeSection sec;
            sec.title = title;
            sec.begin = static_cast<int>(new_movies.size());
            const size_t n = std::min(pack.size(), static_cast<size_t>(kRowCap));
            for (size_t i = 0; i < n; ++i)
                new_movies.push_back(movie_from_catalog(pack[i], genre));
            sec.count = static_cast<int>(n);
            if (sec.count > 0)
                sections.push_back(sec);
        };

        // Hero slides first so posters load ASAP; rows follow.
        for (size_t i = 0; i < page.hero.size() && i < 5; ++i) {
            hero_idx.push_back(static_cast<int>(new_movies.size()));
            auto m = movie_from_catalog(page.hero[i], "Новинка");
            hero_movies.push_back(m);
            new_movies.push_back(std::move(m));
        }
        const int hero_count = static_cast<int>(hero_idx.size());

        append_pack("Фильмы", page.films, "films");
        append_pack("Сериалы", page.series, "series");
        append_pack("Мультфильмы", page.cartoons, "cartoons");
        (void)hero_count;

        if (new_movies.empty()) {
            std::lock_guard<std::mutex> lock(catalog_mutex);
            movies.clear();
            home_sections.clear();
            featured_indices.clear();
            featured_movies.clear();
            catalog_status = "Главная HDRezka недоступна";
            catalog_loading = false;
            catalog_version++;
            return;
        }

        {
            std::lock_guard<std::mutex> lock(catalog_mutex);
            movies = std::move(new_movies);
            home_sections = std::move(sections);
            featured_indices = std::move(hero_idx);
            featured_movies = std::move(hero_movies);
            home_layout = true;
            catalog_status.clear();
            catalog_version++;
        }
        queue_posters(this, stop_worker_, 0, movies.size());
        catalog_loading = false;
    });
}

// Posters of the movies that have none yet, those from poster_hint on first:
// the rows the focus is on are not kept waiting behind the whole list. A page
// appended to the list restarts the worker, and what the last run did not
// reach is picked up again here.
static void queue_missing_posters(App *self, std::atomic<bool> &stop)
{
    std::set<std::string> tried;
    while (!stop) {
        std::string cover_url;
        int index = -1;
        {
            std::lock_guard<std::mutex> lock(self->catalog_mutex);
            const int count = static_cast<int>(self->movies.size());
            const int hint = std::clamp(self->poster_hint.load(), 0, std::max(0, count - 1));
            for (int k = 0; k < count; ++k) {
                const int i = (hint + k) % count;
                const MovieData &m = self->movies[static_cast<size_t>(i)];
                if (m.texture_ready || m.cover_url.empty() || tried.count(m.cover_url))
                    continue;
                index = i;
                cover_url = m.cover_url;
                break;
            }
        }
        if (index < 0)
            break;
        // One connection, one lock, and no queue at it: without this pause a
        // title's page would wait for the whole list's posters.
        while (self->urgent_requests > 0 && !stop)
            usleep(20000);
        tried.insert(cover_url);
        std::vector<uint8_t> img_bytes;
        if (!self->client.http_get_binary(cover_url, img_bytes))
            continue;
        int w = 0, h = 0;
        uint8_t *rgba = TextureLoader::decode_image_rgba(img_bytes.data(), img_bytes.size(), &w, &h);
        if (!rgba || w <= 0 || h <= 0)
            continue;
        App::PendingTexture pt;
        pt.movie_index = index;
        pt.width = w;
        pt.height = h;
        pt.rgba.assign(rgba, rgba + w * h * 4);
        TextureLoader::free_decoded_image(rgba);
        std::lock_guard<std::mutex> p_lock(self->pending_mutex);
        self->pending_textures.push_back(std::move(pt));
    }
}

static const char *const kListFilters[] = {"watching", "last", "popular"};
static const int kListGenres[] = {0, 1, 2, 3, 82};

static std::vector<CatalogItem> fetch_list_page(Client &client, int tab, int type, int kind, int page,
                                                bool *has_next)
{
    if (kind == 1)
        return client.get_movie_list("last", kListGenres[type], page, has_next, "/new");
    if (kind == 2)
        return client.get_movie_list("soon", kListGenres[type], page, has_next);
    return client.get_movie_list(kListFilters[tab], kListGenres[type], page, has_next);
}

void App::load_movie_list_async(int tab, int type, int kind)
{
    write_log("[DEBUG] App::load_movie_list_async tab %d type %d kind %d\n", tab, type, kind);
    tab = std::clamp(tab, 0, 2);
    type = std::clamp(type, 0, 4);
    kind = std::clamp(kind, 0, 2);
    current_category = -1;
    catalog_loading = true;

    if (worker_thread_.joinable()) {
        stop_worker_ = true;
        worker_thread_.join();
        stop_worker_ = false;
    }
    {
        // Posters decoded for the list that is going away
        std::lock_guard<std::mutex> p_lock(pending_mutex);
        pending_textures.clear();
    }
    {
        std::lock_guard<std::mutex> lock(catalog_mutex);
        list_tab = tab;
        list_type = type;
        list_kind = kind;
        home_filter = tab;
    }

    worker_thread_ = BigThread([this, tab, type, kind]() {
        {
            std::lock_guard<std::mutex> lock(catalog_mutex);
            home_layout = true;
            home_sections.clear();
            featured_indices.clear();
            featured_movies.clear();
            movies.clear();
            list_page = 0;
            list_has_next = false;
            catalog_status = "Загрузка...";
            catalog_version++;
        }
        if (!client.pick_mirror()) {
            std::lock_guard<std::mutex> lock(catalog_mutex);
            catalog_status = "Ошибка сети";
            catalog_loading = false;
            catalog_version++;
            return;
        }
        bool has_next = false;
        auto items = fetch_list_page(client, tab, type, kind, 1, &has_next);
        if (stop_worker_) {
            catalog_loading = false;
            return;
        }
        std::vector<MovieData> new_movies;
        new_movies.reserve(items.size());
        for (const auto &it : items)
            new_movies.push_back(movie_from_catalog(it, ""));
        {
            std::lock_guard<std::mutex> lock(catalog_mutex);
            movies = std::move(new_movies);
            list_page = 1;
            list_has_next = has_next && !movies.empty();
            catalog_status = movies.empty() ? "Ничего не найдено" : "";
            catalog_version++;
        }
        catalog_loading = false;
        queue_missing_posters(this, stop_worker_);
    });
}

void App::load_movie_list_more_async()
{
    int tab = 0, type = 0, kind = 0, page = 0;
    {
        std::lock_guard<std::mutex> lock(catalog_mutex);
        if (!home_layout || !list_has_next || list_page < 1)
            return;
        tab = list_tab;
        type = list_type;
        kind = list_kind;
        page = list_page + 1;
    }
    if (catalog_loading || list_more_loading.exchange(true))
        return;

    if (worker_thread_.joinable()) {
        stop_worker_ = true;
        worker_thread_.join();
        stop_worker_ = false;
    }

    worker_thread_ = BigThread([this, tab, type, kind, page]() {
        bool has_next = false;
        auto items = fetch_list_page(client, tab, type, kind, page, &has_next);
        if (!stop_worker_) {
            std::lock_guard<std::mutex> lock(catalog_mutex);
            // The site's lists shift between requests: skip what is already here
            std::set<std::string> known;
            for (const auto &m : movies)
                known.insert(m.url);
            for (const auto &it : items) {
                if (known.insert(it.url).second)
                    movies.push_back(movie_from_catalog(it, ""));
            }
            list_page = page;
            list_has_next = has_next && !items.empty();
            catalog_version++;
        }
        list_more_loading = false;
        queue_missing_posters(this, stop_worker_);
    });
}

static std::string photo_address(const std::string &raw, const std::string &mirror);

void App::search_async(const std::string &query)
{
    list_async([query](Client &c) { return c.search(query); }, "Поиск");
}

void App::suggest_async(const std::string &query)
{
    const uint32_t serial = ++suggest_serial_;
    if (query.empty()) {
        std::lock_guard<std::mutex> lock(catalog_mutex);
        search_suggestions.clear();
        suggest_version++;
        suggest_loading = false;
        return;
    }
    suggest_loading = true;
    BigThread([this, query, serial]() {
        UrgentRequest urgent(urgent_requests);
        std::vector<CatalogItem> found = client.search_suggest(query);
        if (serial != suggest_serial_.load())
            return; // a newer query is on its way
        {
            std::lock_guard<std::mutex> lock(catalog_mutex);
            search_suggestions = std::move(found);
            suggest_version++;
        }
        suggest_loading = false;
    }).detach();
}

std::vector<std::string> App::get_search_history()
{
    std::lock_guard<std::mutex> lock(history_mutex_);
    return search_history_;
}

void App::load_search_history()
{
    const std::string path = data_file_path(client.get_cookie_file(), "search_history.txt");
    FILE *f = fopen(path.c_str(), "r");
    if (!f)
        return;
    char line[512];
    std::lock_guard<std::mutex> lock(history_mutex_);
    search_history_.clear();
    while (fgets(line, sizeof(line), f)) {
        std::string text(line);
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
            text.pop_back();
        if (!text.empty())
            search_history_.push_back(text);
    }
    fclose(f);
}

void App::persist_search_history()
{
    const std::string path = data_file_path(client.get_cookie_file(), "search_history.txt");
    FILE *f = fopen(path.c_str(), "w");
    if (!f)
        return;
    std::lock_guard<std::mutex> lock(history_mutex_);
    for (const std::string &q : search_history_)
        std::fprintf(f, "%s\n", q.c_str());
    fclose(f);
}

void App::add_search_history(const std::string &query)
{
    if (query.empty())
        return;
    {
        std::lock_guard<std::mutex> lock(history_mutex_);
        search_history_.erase(std::remove(search_history_.begin(), search_history_.end(), query),
                              search_history_.end());
        search_history_.insert(search_history_.begin(), query);
        if (search_history_.size() > 30)
            search_history_.resize(30);
    }
    persist_search_history();
}

void App::clear_search_history()
{
    {
        std::lock_guard<std::mutex> lock(history_mutex_);
        search_history_.clear();
    }
    persist_search_history();
}

namespace
{
// The speech-to-text address: HDREZKA_STT_URL, or the first line of data/stt.txt.
std::string stt_address(const std::string &cookie_file)
{
    if (const char *env = std::getenv("HDREZKA_STT_URL"))
        if (*env)
            return env;
    const std::string path = data_file_path(cookie_file, "stt.txt");
    FILE *f = fopen(path.c_str(), "r");
    if (!f)
        return "";
    char line[512] = "";
    if (!fgets(line, sizeof(line), f))
        line[0] = 0;
    fclose(f);
    std::string text(line);
    while (!text.empty() && isspace(static_cast<unsigned char>(text.back())))
        text.pop_back();
    return text;
}

// A 16-bit mono WAV of the samples.
std::vector<unsigned char> wav_of(const std::vector<int16_t> &samples, int rate)
{
    std::vector<unsigned char> out;
    auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) out.push_back(static_cast<unsigned char>(v >> (8 * i))); };
    auto u16 = [&](uint16_t v) { out.push_back(static_cast<unsigned char>(v)); out.push_back(static_cast<unsigned char>(v >> 8)); };
    const uint32_t bytes = static_cast<uint32_t>(samples.size() * 2);
    out.insert(out.end(), {'R', 'I', 'F', 'F'});
    u32(36 + bytes);
    out.insert(out.end(), {'W', 'A', 'V', 'E', 'f', 'm', 't', ' '});
    u32(16); u16(1); u16(1); u32(static_cast<uint32_t>(rate)); u32(static_cast<uint32_t>(rate) * 2); u16(2); u16(16);
    out.insert(out.end(), {'d', 'a', 't', 'a'});
    u32(bytes);
    for (int16_t v : samples)
        u16(static_cast<uint16_t>(v));
    return out;
}
} // namespace

bool App::voice_available()
{
    return !stt_address(client.get_cookie_file()).empty();
}

bool App::voice_start()
{
    if (voice_state.load() == 1 || voice_state.load() == 2)
        return false;
    if (!voice_available() || !mic_start_recording())
        return false;
    voice_state = 1;
    return true;
}

void App::voice_cancel()
{
    if (voice_state.load() == 1)
        mic_stop_recording();
    voice_state = 0;
}

void App::voice_finish_async()
{
    if (voice_state.exchange(2) != 1)
        return;
    BigThread([this]() {
        UrgentRequest urgent(urgent_requests);
        mic_stop_recording();
        const size_t total = mic_get_samples(nullptr, 0);
        std::vector<int16_t> samples(total);
        mic_get_samples(samples.data(), samples.size());
        std::string text;
        // Under a third of a second is no speech
        if (total > static_cast<size_t>(mic_sample_rate() / 3))
            text = client.stt_recognize(stt_address(client.get_cookie_file()), wav_of(samples, mic_sample_rate()));
        {
            std::lock_guard<std::mutex> lock(history_mutex_);
            voice_text_ = text;
        }
        voice_state = text.empty() ? 4 : 3;
    }).detach();
}

std::string App::take_voice_text()
{
    std::string text;
    {
        std::lock_guard<std::mutex> lock(history_mutex_);
        text.swap(voice_text_);
    }
    voice_state = 0;
    return text;
}

static int merge_person_page(PersonDetails &dst, const PersonDetails &src)
{
    int added = 0;
    auto take = [&](PersonCareer &into, const std::vector<CatalogItem> &movies, int expected) {
        std::set<std::string> seen;
        for (const CatalogItem &m : into.movies)
            if (!m.url.empty())
                seen.insert(m.url);
        for (const CatalogItem &m : movies) {
            if (m.url.empty() || !seen.insert(m.url).second)
                continue;
            into.movies.push_back(m);
            ++added;
        }
        if (expected > into.expected)
            into.expected = expected;
    };
    if (src.careers.empty()) {
        if (!src.movies.empty()) {
            if (dst.careers.empty()) {
                PersonCareer career;
                career.title = "Фильмография";
                dst.careers.push_back(std::move(career));
            }
            take(dst.careers.front(), src.movies, 0);
        }
    } else {
        for (const PersonCareer &career : src.careers) {
            PersonCareer *into = nullptr;
            for (PersonCareer &have : dst.careers) {
                if (have.title == career.title) {
                    into = &have;
                    break;
                }
            }
            if (!into) {
                dst.careers.push_back(career);
                added += static_cast<int>(career.movies.size());
            } else {
                take(*into, career.movies, career.expected);
            }
        }
    }
    dst.movies.clear();
    std::set<std::string> seen;
    for (const PersonCareer &career : dst.careers) {
        for (const CatalogItem &movie : career.movies) {
            if (!movie.url.empty() && seen.insert(movie.url).second)
                dst.movies.push_back(movie);
        }
    }
    return added;
}

static void sync_person_movies_locked(App *self)
{
    self->person_career_titles.clear();
    for (const PersonCareer &career : self->person.careers) {
        std::string label = career.title + " (" + std::to_string(career.movies.size()) + ")";
        self->person_career_titles.push_back(std::move(label));
    }
    if (self->person.careers.empty()) {
        self->person_career_idx = 0;
        self->person_movies.clear();
        return;
    }
    self->person_career_idx =
        std::clamp(self->person_career_idx, 0, static_cast<int>(self->person.careers.size()) - 1);
    const PersonCareer &career =
        self->person.careers[static_cast<std::size_t>(self->person_career_idx)];
    std::map<std::string, MovieData> keep;
    for (MovieData &movie : self->person_movies) {
        if (!movie.url.empty())
            keep.emplace(movie.url, std::move(movie));
    }
    self->person_movies.clear();
    self->person_movies.reserve(career.movies.size());
    for (const CatalogItem &it : career.movies) {
        auto found = keep.find(it.url);
        if (found != keep.end())
            self->person_movies.push_back(std::move(found->second));
        else
            self->person_movies.push_back(movie_from_catalog(it, career.title));
    }
}

void App::queue_person_posters(uint32_t epoch, int from)
{
    for (int i = from;; ++i) {
        if (stop_worker_ || person_epoch.load() != epoch)
            return;
        std::string cover_url;
        {
            std::lock_guard<std::mutex> lock(catalog_mutex);
            if (person_epoch.load() != epoch || i >= static_cast<int>(person_movies.size()))
                return;
            const MovieData &movie = person_movies[static_cast<std::size_t>(i)];
            if (movie.texture_id != 0 || movie.cover_url.empty())
                continue;
            cover_url = movie.cover_url;
        }
        std::vector<uint8_t> img_bytes;
        if (!client.http_get_binary(cover_url, img_bytes))
            continue;
        int w = 0, h = 0;
        uint8_t *rgba = TextureLoader::decode_image_rgba(img_bytes.data(), img_bytes.size(), &w, &h);
        if (!rgba || w <= 0 || h <= 0)
            continue;
        if (person_epoch.load() != epoch) {
            TextureLoader::free_decoded_image(rgba);
            return;
        }
        PendingTexture pt;
        pt.person_movie_index = i;
        pt.bind_url = cover_url;
        pt.width = w;
        pt.height = h;
        pt.rgba.assign(rgba, rgba + static_cast<size_t>(w) * static_cast<size_t>(h) * 4);
        TextureLoader::free_decoded_image(rgba);
        std::lock_guard<std::mutex> p_lock(pending_mutex);
        pending_textures.push_back(std::move(pt));
    }
}

void App::person_select_career(int career_idx)
{
    uint32_t epoch = 0;
    {
        std::lock_guard<std::mutex> lock(catalog_mutex);
        if (person.careers.empty()) {
            person_career_idx = 0;
            return;
        }
        person_career_idx = std::clamp(career_idx, 0, static_cast<int>(person.careers.size()) - 1);
        person_more_loading = false;
        person_epoch++;
        epoch = person_epoch.load();
        sync_person_movies_locked(this);
        person_version++;
    }
    BigThread([this, epoch]() { queue_person_posters(epoch, 0); }).detach();
}

void App::person_async(const std::string &url, const std::string &name)
{
    const std::string full = photo_address(url, client.get_mirror());
    uint32_t epoch = 0;
    {
        std::lock_guard<std::mutex> lock(catalog_mutex);
        person = {};
        person.name = name;
        person.url = full;
        person_movies.clear();
        person_career_titles.clear();
        person_career_idx = 0;
        person_page = 1;
        person_has_next = false;
        person_more_loading = false;
        person_status = "Загрузка фильмографии...";
        person_loading = true;
        person_epoch++;
        epoch = person_epoch.load();
        person_version++;
    }

    if (worker_thread_.joinable()) {
        stop_worker_ = true;
        worker_thread_.join();
        stop_worker_ = false;
    }

    worker_thread_ = BigThread([this, full, name, epoch]() {
        if (!client.pick_mirror()) {
            std::lock_guard<std::mutex> lock(catalog_mutex);
            if (person_epoch.load() == epoch) {
                person_status = "HDRezka не отвечает";
                person_loading = false;
                person_version++;
            }
            return;
        }
        bool has_next = false;
        PersonDetails details;
        if (!client.get_person(full, details, &has_next) || person_epoch.load() != epoch) {
            std::lock_guard<std::mutex> lock(catalog_mutex);
            if (person_epoch.load() == epoch) {
                person_status = "Страница актёра недоступна";
                person_loading = false;
                person_has_next = false;
                person_version++;
            }
            return;
        }
        if (details.name.empty())
            details.name = name;

        {
            std::lock_guard<std::mutex> lock(catalog_mutex);
            if (person_epoch.load() != epoch)
                return;
            person = details;
            person_career_idx = 0;
            person_page = 1;
            person_has_next = has_next;
            person_movies.clear();
            sync_person_movies_locked(this);
            person_status = person_movies.empty() ? "Фильмы не найдены" : "";
            person_loading = false;
            person_version++;
        }

        if (!person.photo.empty() && person_epoch.load() == epoch)
            request_photo(person.photo_hq.empty() ? person.photo : person.photo_hq);
        queue_person_posters(epoch, 0);
    });
}

void App::person_load_more_async()
{
    if (person_loading.load() || person_more_loading.exchange(true))
        return;
    int page = 0;
    int career = 0;
    std::string url;
    uint32_t epoch = 0;
    {
        std::lock_guard<std::mutex> lock(catalog_mutex);
        if (!person_has_next.load() || person.url.empty()) {
            person_more_loading = false;
            return;
        }
        page = person_page + 1;
        career = person_career_idx;
        url = person.url;
        epoch = person_epoch.load();
    }

    BigThread([this, page, career, url, epoch]() {
        bool has_next = false;
        PersonDetails more;
        const bool ok = client.get_person_page(url, page, more, &has_next);
        int poster_from = -1;
        bool stale = false;
        {
            std::lock_guard<std::mutex> lock(catalog_mutex);
            if (person_epoch.load() != epoch) {
                stale = true;
            } else {
                int added = 0;
                if (ok)
                    added = merge_person_page(person, more);
                if (!ok || added == 0) {
                    person_has_next = false;
                } else {
                    person_page = page;
                    person_has_next = has_next;
                    person_career_idx = career;
                    poster_from = static_cast<int>(person_movies.size());
                    sync_person_movies_locked(this);
                    if (poster_from > static_cast<int>(person_movies.size()))
                        poster_from = static_cast<int>(person_movies.size());
                }
                person_version++;
            }
        }
        if (!stale)
            person_more_loading = false;
        if (poster_from >= 0)
            queue_person_posters(epoch, poster_from);
    }).detach();
}

void App::person_films_async(const std::string &url, const std::string &name)
{
    person_async(url, name);
}

int App::adopt_person_film(int person_movie_idx)
{
    std::lock_guard<std::mutex> lock(catalog_mutex);
    if (person_movie_idx < 0 || person_movie_idx >= static_cast<int>(person_movies.size()))
        return -1;
    const MovieData &src = person_movies[static_cast<std::size_t>(person_movie_idx)];
    for (int i = 0; i < static_cast<int>(movies.size()); ++i) {
        if (movies[static_cast<std::size_t>(i)].url == src.url)
            return i;
    }
    movies.push_back(src);
    catalog_version++;
    return static_cast<int>(movies.size()) - 1;
}

// The address a photo is fetched from: relative paths go to the mirror.
int App::adopt_catalog_item(const CatalogItem &item)
{
    if (item.url.empty())
        return -1;
    std::lock_guard<std::mutex> lock(catalog_mutex);
    for (int i = 0; i < static_cast<int>(movies.size()); ++i) {
        if (movies[static_cast<std::size_t>(i)].url == item.url)
            return i;
    }
    movies.push_back(movie_from_catalog(item, ""));
    catalog_version++;
    return static_cast<int>(movies.size()) - 1;
}

void App::load_comments_async(const std::string &post_id, bool more)
{
    int page = 1;
    {
        std::lock_guard<std::mutex> lock(catalog_mutex);
        if (more) {
            if (comments_post != post_id || !comments_has_next)
                return;
            page = comments_page + 1;
        } else {
            comments.clear();
            comments_post = post_id;
            comments_page = 0;
            comments_has_next = false;
            comments_version++;
        }
    }
    if (comments_loading.exchange(true))
        return;
    BigThread([this, post_id, page]() {
        struct Urgent {
            std::atomic<int> &n;
            explicit Urgent(std::atomic<int> &count) : n(count) { ++n; }
            ~Urgent() { --n; }
        } urgent(urgent_requests);
        std::vector<Comment> got;
        bool has_next = false;
        client.get_comments(post_id, page, got, &has_next);
        {
            std::lock_guard<std::mutex> lock(catalog_mutex);
            if (comments_post == post_id) {
                comments.insert(comments.end(), got.begin(), got.end());
                comments_page = page;
                comments_has_next = has_next && !got.empty();
            }
        }
        comments_loading = false;
        comments_version++;
    }).detach();
}

static std::string photo_address(const std::string &raw, const std::string &mirror)
{
    if (raw.empty())
        return raw;
    if (raw.rfind("//", 0) == 0)
        return "https:" + raw;
    if (raw.rfind("http", 0) == 0)
        return raw;
    return mirror + (raw.front() == '/' ? "" : "/") + raw;
}

void App::request_photo(const std::string &raw)
{
    const std::string url = photo_address(raw, client.get_mirror());
    if (url.empty())
        return;
    {
        std::lock_guard<std::mutex> lock(catalog_mutex);
        if (!photo_requests.insert(url).second)
            return;
    }
    BigThread([this, url]() {
        std::vector<uint8_t> img_bytes;
        if (!client.http_get_binary(url, img_bytes))
            return;
        int w = 0, h = 0;
        uint8_t *rgba = TextureLoader::decode_image_rgba(img_bytes.data(), img_bytes.size(), &w, &h);
        if (!rgba || w <= 0 || h <= 0)
            return;
        PendingTexture pt;
        pt.movie_index = -1;
        pt.photo_url = url;
        pt.width = w;
        pt.height = h;
        pt.rgba.assign(rgba, rgba + w * h * 4);
        TextureLoader::free_decoded_image(rgba);
        std::lock_guard<std::mutex> p_lock(pending_mutex);
        pending_textures.push_back(std::move(pt));
    });
}

uint32_t App::photo_texture(const std::string &raw)
{
    const std::string url = photo_address(raw, client.get_mirror());
    std::lock_guard<std::mutex> lock(catalog_mutex);
    auto it = photo_textures.find(url);
    return it == photo_textures.end() ? 0 : it->second;
}

void App::list_async(std::function<std::vector<CatalogItem>(Client &)> fetch, const std::string &genre_label)
{
    catalog_loading = true;

    if (worker_thread_.joinable()) {
        stop_worker_ = true;
        worker_thread_.join();
        stop_worker_ = false;
    }

    worker_thread_ = BigThread([this, fetch, genre_label]() {
        {
            std::lock_guard<std::mutex> lock(catalog_mutex);
            home_layout = false;
            home_sections.clear();
        }
        if (!client.pick_mirror()) {
            std::lock_guard<std::mutex> lock(catalog_mutex);
            catalog_status = "HDRezka не отвечает. Проверьте интернет.";
            catalog_loading = false;
            catalog_version++;
            return;
        }
        auto items = fetch(client);
        if (items.empty()) {
            std::lock_guard<std::mutex> lock(catalog_mutex);
            catalog_status = "Ничего не найдено";
            catalog_loading = false;
            catalog_version++;
            return;
        }

        std::vector<MovieData> new_movies;
        new_movies.reserve(items.size());

        for (size_t i = 0; i < items.size(); ++i) {
            if (stop_worker_) break;
            new_movies.push_back(movie_from_catalog(items[i], genre_label));
        }

        if (!new_movies.empty() && !stop_worker_) {
            {
                std::lock_guard<std::mutex> lock(catalog_mutex);
                movies = std::move(new_movies);
                featured_indices.clear();
                featured_movies.clear();
                const int n = std::min(5, static_cast<int>(movies.size()));
                for (int i = 0; i < n; ++i) {
                    featured_indices.push_back(i);
                    featured_movies.push_back(movies[static_cast<size_t>(i)]);
                }
                catalog_status.clear();
                catalog_version++;
            }

            for (size_t i = 0; i < movies.size(); ++i) {
                if (stop_worker_) break;
                std::string cover_url;
                {
                    std::lock_guard<std::mutex> lock(catalog_mutex);
                    if (i < movies.size()) cover_url = movies[i].cover_url;
                }
                if (cover_url.empty()) continue;

                std::vector<uint8_t> img_bytes;
                if (client.http_get_binary(cover_url, img_bytes)) {
                    int w = 0, h = 0;
                    uint8_t *rgba = TextureLoader::decode_image_rgba(img_bytes.data(), img_bytes.size(), &w, &h);
                    if (rgba && w > 0 && h > 0) {
                        PendingTexture pt;
                        pt.movie_index = static_cast<int>(i);
                        pt.width = w;
                        pt.height = h;
                        pt.rgba.assign(rgba, rgba + w * h * 4);
                        TextureLoader::free_decoded_image(rgba);

                        std::lock_guard<std::mutex> p_lock(pending_mutex);
                        pending_textures.push_back(std::move(pt));
                    }
                }
            }
        }

        catalog_loading = false;
    });
}

void App::load_details_async(int movie_idx)
{
    BigThread([this, movie_idx]() {
        std::string url;
        {
            std::lock_guard<std::mutex> lock(catalog_mutex);
            if (movie_idx < 0 || movie_idx >= static_cast<int>(movies.size())) return;
            if (movies[movie_idx].details_loaded) return;
            url = movies[movie_idx].url;
        }

        write_log("[DEBUG] load_details_async start for movie_idx=%d url=%s\n", movie_idx, url.c_str());
        struct Urgent {
            std::atomic<int> &n;
            explicit Urgent(std::atomic<int> &count) : n(count) { ++n; }
            ~Urgent() { --n; }
        } urgent(urgent_requests);
        VideoDetails det;
        if (client.get_details(url, det)) {
            if (det.is_series) {
                std::string tr_id = det.translators.empty() ? "238" : det.translators[0].id;
                client.get_episodes(det, tr_id, det.seasons);
            }
            if (det.has_trailer)
                client.get_trailer(det.id, det.trailer_id);
            // Streams are requested only when playback starts (play_title_async),
            // not while the card opens.
            std::lock_guard<std::mutex> lock(catalog_mutex);
            if (movie_idx < static_cast<int>(movies.size())) {
                movies[movie_idx].details = det;
                movies[movie_idx].details_loaded = true;
                if (!det.description.empty())
                    movies[movie_idx].blurb = det.description;
                if (!det.name.empty())
                    movies[movie_idx].title = det.name;
                if (det.rating > 0.0f)
                    movies[movie_idx].rating = det.rating;
                if (det.rating_hdrezka.valid && !det.rating_hdrezka.votes.empty()) {
                    try {
                        std::string digits;
                        for (char c : det.rating_hdrezka.votes) {
                            if (c >= '0' && c <= '9')
                                digits.push_back(c);
                        }
                        if (!digits.empty())
                            movies[movie_idx].rating_votes = std::stoi(digits);
                    } catch (...) {
                    }
                }
                if (!det.year.empty()) {
                    try {
                        movies[movie_idx].year = std::stoi(det.year);
                    } catch (...) {
                    }
                }
                if (!det.genres.empty()) {
                    movies[movie_idx].genre = det.genres.front();
                    movies[movie_idx].info.clear();
                    for (size_t gi = 0; gi < det.genres.size() && gi < 3; ++gi) {
                        if (gi)
                            movies[movie_idx].info += ", ";
                        movies[movie_idx].info += det.genres[gi];
                    }
                    if (!det.countries.empty())
                        movies[movie_idx].info =
                            (det.year.empty() ? "" : det.year + ", ") + det.countries.front() +
                            ", " + movies[movie_idx].info;
                    else if (!det.year.empty())
                        movies[movie_idx].info = det.year + ", " + movies[movie_idx].info;
                }
                catalog_version++;
            }
            write_log("[DEBUG] load_details_async done movie_idx=%d rating=%.1f people=%zu\n",
                      movie_idx, det.rating, det.people.size());
        } else {
            write_log("[DEBUG] load_details_async client.get_details failed url=%s\n", url.c_str());
        }
    }).detach();
}

void App::login_async(const std::string &email, const std::string &password)
{
    if (login_busy.exchange(true))
        return;
    {
        std::lock_guard<std::mutex> lock(catalog_mutex);
        account.status.clear();
    }
    BigThread([this, email, password]() {
        UrgentRequest urgent(urgent_requests);
        std::string error;
        bool ok = client.pick_mirror();
        if (!ok)
            error = "Ошибка сети";
        else
            ok = client.login(email, password, error);
        Account acc;
        if (ok) {
            client.get_account(acc);
            acc.logged_in = true;
            acc.email = email;
            if (acc.name.empty())
                acc.name = email;
        } else
            // The app's own words for a refusal, when the site gives none
            acc.status = error.empty() ? "Неверный логин или пароль" : error;
        {
            std::lock_guard<std::mutex> lock(catalog_mutex);
            account = std::move(acc);
            catalog_version++;
        }
        account_version++;
        login_busy = false;
        if (ok)
            load_account_lists_async();
    }).detach();
}

void App::refresh_account_async()
{
    BigThread([this]() {
        if (client.session_user_id().empty() || !client.pick_mirror())
            return;
        Account acc;
        if (!client.get_account(acc))
            return;
        {
            std::lock_guard<std::mutex> lock(catalog_mutex);
            account = std::move(acc);
            catalog_version++;
        }
        account_version++;
        load_account_lists_async();
    }).detach();
}

void App::logout_async()
{
    {
        std::lock_guard<std::mutex> lock(catalog_mutex);
        account = {};
        watch_later.clear();
        bookmark_folders.clear();
        catalog_version++;
    }
    account_version++;
    BigThread([this]() {
        UrgentRequest urgent(urgent_requests);
        client.logout();
    }).detach();
}

void App::load_account_lists_async()
{
    if (account_lists_loading.exchange(true))
        return;
    BigThread([this]() {
        UrgentRequest urgent(urgent_requests);
        std::vector<WatchLaterItem> later = client.get_watch_later();
        std::vector<BookmarkFolder> folders = client.get_bookmark_folders();
        {
            std::lock_guard<std::mutex> lock(catalog_mutex);
            watch_later = std::move(later);
            bookmark_folders = std::move(folders);
        }
        account_lists_loading = false;
        account_version++;
    }).detach();
}

void App::remove_watch_later_async(const std::string &id)
{
    {
        std::lock_guard<std::mutex> lock(catalog_mutex);
        for (size_t i = 0; i < watch_later.size(); ++i)
            if (watch_later[i].id == id) {
                watch_later.erase(watch_later.begin() + static_cast<std::ptrdiff_t>(i));
                break;
            }
    }
    account_version++;
    BigThread([this, id]() {
        UrgentRequest urgent(urgent_requests);
        client.remove_watch_later(id);
    }).detach();
}

void App::load_continue_map()
{
    const std::string path = data_file_path(client.get_cookie_file(), "continue.txt");
    FILE *f = fopen(path.c_str(), "r");
    if (!f)
        return;
    char line[1024];
    std::lock_guard<std::mutex> lock(continue_mutex_);
    continue_map_.clear();
    while (fgets(line, sizeof(line), f)) {
        // url|t|tr_id|quality|tr_idx|q_idx|s_idx|e_idx|title
        ContinueEntry e;
        char url[512] = {}, tr[64] = {}, quality[64] = {}, title[256] = {};
        if (std::sscanf(line, "%511[^|]|%lf|%63[^|]|%63[^|]|%d|%d|%d|%d|%255[^\n]", url, &e.t, tr,
                        quality, &e.translator_idx, &e.quality_idx, &e.season_idx, &e.episode_idx,
                        title) >= 4) {
            e.url = url;
            e.translator_id = tr;
            e.quality = quality;
            e.title = title;
            if (!e.url.empty() && e.t > 5.0)
                continue_map_[e.url] = e;
        }
    }
    fclose(f);
    write_log("[DEBUG] continue_map loaded %zu entries from %s\n", continue_map_.size(),
              path.c_str());
}

void App::persist_continue_map()
{
    const std::string path = data_file_path(client.get_cookie_file(), "continue.txt");
    FILE *f = fopen(path.c_str(), "w");
    if (!f)
        return;
    std::lock_guard<std::mutex> lock(continue_mutex_);
    for (const auto &kv : continue_map_) {
        const auto &e = kv.second;
        std::fprintf(f, "%s|%.1f|%s|%s|%d|%d|%d|%d|%s\n", e.url.c_str(), e.t,
                     e.translator_id.c_str(), e.quality.c_str(), e.translator_idx, e.quality_idx,
                     e.season_idx, e.episode_idx, e.title.c_str());
    }
    fclose(f);
}

// Bookmarks: one "url<TAB>title" line per title.
void App::load_favorites()
{
    const std::string path = data_file_path(client.get_cookie_file(), "favorites.txt");
    FILE *f = fopen(path.c_str(), "r");
    if (!f)
        return;
    char line[1024];
    std::lock_guard<std::mutex> lock(favorites_mutex_);
    favorites_.clear();
    while (fgets(line, sizeof(line), f)) {
        std::string text(line);
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
            text.pop_back();
        const size_t tab = text.find('\t');
        const std::string url = text.substr(0, tab);
        if (!url.empty())
            favorites_[url] = tab == std::string::npos ? std::string() : text.substr(tab + 1);
    }
    fclose(f);
}

void App::persist_favorites()
{
    const std::string path = data_file_path(client.get_cookie_file(), "favorites.txt");
    FILE *f = fopen(path.c_str(), "w");
    if (!f)
        return;
    std::lock_guard<std::mutex> lock(favorites_mutex_);
    for (const auto &kv : favorites_)
        std::fprintf(f, "%s\t%s\n", kv.first.c_str(), kv.second.c_str());
    fclose(f);
}

bool App::is_favorite(const std::string &url)
{
    std::lock_guard<std::mutex> lock(favorites_mutex_);
    return favorites_.count(url) != 0;
}

bool App::toggle_favorite(const std::string &url, const std::string &title)
{
    if (url.empty())
        return false;
    bool on = false;
    {
        std::lock_guard<std::mutex> lock(favorites_mutex_);
        if (favorites_.count(url)) {
            favorites_.erase(url);
        } else {
            std::string clean = title;
            for (char &c : clean)
                if (c == '\t' || c == '\n' || c == '\r')
                    c = ' ';
            favorites_[url] = clean;
            on = true;
        }
    }
    persist_favorites();
    return on;
}

bool App::get_continue(const std::string &url, ContinueEntry &out)
{
    std::lock_guard<std::mutex> lock(continue_mutex_);
    auto it = continue_map_.find(url);
    if (it == continue_map_.end())
        return false;
    out = it->second;
    return out.t > 5.0;
}

void App::save_continue_progress()
{
    if (playing_url.empty())
        return;
    const double t = player.get_position();
    if (t < 5.0)
        return;
    ContinueEntry e;
    e.url = playing_url;
    e.title = playing_title;
    e.t = t;
    e.quality = playing_quality;
    e.translator_idx = playing_translator_idx;
    e.quality_idx = playing_quality_idx;
    e.season_idx = playing_season_idx;
    e.episode_idx = playing_episode_idx;
    e.translator_id = playing_voice;
    {
        std::lock_guard<std::mutex> clock(catalog_mutex);
        if (playing_movie_idx >= 0 && playing_movie_idx < static_cast<int>(movies.size())) {
            const auto &m = movies[playing_movie_idx];
            if (m.details_loaded && !m.details.translators.empty()) {
                const int i = std::clamp(playing_translator_idx, 0,
                                         static_cast<int>(m.details.translators.size()) - 1);
                e.translator_id = m.details.translators[i].id;
            }
        }
    }
    {
        std::lock_guard<std::mutex> lock(continue_mutex_);
        continue_map_[e.url] = e;
    }
    persist_continue_map();
    write_log("[DEBUG] save_continue url=%s t=%.1f tr=%s q=%s\n", e.url.c_str(), e.t,
              e.translator_id.c_str(), e.quality.c_str());
}

static std::string quality_key(std::string q)
{
    std::string out;
    out.reserve(q.size());
    for (unsigned char c : q) {
        if (c == ' ' || c == '\t')
            continue;
        out.push_back(static_cast<char>(std::tolower(c)));
    }
    return out;
}

static int quality_rank_label(const std::string &q)
{
    const std::string k = quality_key(q);
    if (k.find("2160") != std::string::npos || k.find("4k") != std::string::npos)
        return 6000;
    if (k.find("ultra") != std::string::npos || k.find("1080p+") != std::string::npos)
        return 1180;
    if (k.find("1080") != std::string::npos)
        return 1080;
    if (k.find("720") != std::string::npos)
        return 720;
    if (k.find("480") != std::string::npos)
        return 480;
    if (k.find("360") != std::string::npos)
        return 360;
    if (k.find("240") != std::string::npos)
        return 240;
    return 0;
}

static int pick_stream_index(const std::vector<StreamQuality> &streams, const std::string &wanted)
{
    if (streams.empty())
        return 0;
    if (!wanted.empty()) {
        const std::string key = quality_key(wanted);
        for (int i = 0; i < static_cast<int>(streams.size()); ++i) {
            if (quality_key(streams[static_cast<std::size_t>(i)].quality) == key)
                return i;
        }
        for (int i = 0; i < static_cast<int>(streams.size()); ++i) {
            const std::string have = quality_key(streams[static_cast<std::size_t>(i)].quality);
            if (!key.empty() && (have.find(key) != std::string::npos || key.find(have) != std::string::npos))
                return i;
        }
        const int want_rank = quality_rank_label(wanted);
        if (want_rank > 0) {
            int best = 0;
            int best_delta = 100000;
            for (int i = 0; i < static_cast<int>(streams.size()); ++i) {
                const int delta =
                    std::abs(quality_rank_label(streams[static_cast<std::size_t>(i)].quality) - want_rank);
                if (delta < best_delta) {
                    best_delta = delta;
                    best = i;
                }
            }
            return best;
        }
    }
    int best = 0;
    int best_rank = -1;
    for (int i = 0; i < static_cast<int>(streams.size()); ++i) {
        const int rank = quality_rank_label(streams[static_cast<std::size_t>(i)].quality);
        if (rank > best_rank) {
            best_rank = rank;
            best = i;
        }
    }
    return best;
}

void App::play_title_async(int movie_idx, int translator_idx, int quality_idx, int season_idx,
                           int episode_idx, double start_time)
{
    const uint32_t serial = ++play_serial_;
    {
        std::lock_guard<std::mutex> lock(catalog_mutex);
        if (movie_idx >= 0 && movie_idx < static_cast<int>(movies.size())) {
            playing_title = movies[movie_idx].title;
            playing_url = movies[movie_idx].url;
        }
        catalog_status = "Буферизация...";
        catalog_version++;
    }
    is_buffering = true;

    BigThread([this, movie_idx, translator_idx, quality_idx, season_idx, episode_idx, start_time, serial]() {
        write_log("[DEBUG] play_title_async thread started movie_idx=%d tr=%d q=%d s=%d e=%d seek=%.1f serial=%u\n",
                  movie_idx, translator_idx, quality_idx, season_idx, episode_idx, start_time, serial);
        std::string url, title, voice = "HDRezka", item_url;
        VideoDetails det;
        bool have = false;
        std::string tr_id = "238";
        std::string s_id = "1";
        std::string e_id = "1";
        std::string wanted_quality;
        auto fail_buffering = [this, serial](const char *status) {
            if (play_serial_.load() != serial)
                return;
            std::lock_guard<std::mutex> lock(catalog_mutex);
            catalog_status = status;
            catalog_version++;
            is_buffering = false;
        };
        if (play_serial_.load() != serial)
            return;
        {
            std::lock_guard<std::mutex> lock(catalog_mutex);
            if (movie_idx < 0 || movie_idx >= static_cast<int>(movies.size())) {
                catalog_status = "Фильм не найден";
                catalog_version++;
                is_buffering = false;
                return;
            }
            const auto &m = movies[movie_idx];
            title = m.title;
            item_url = m.url;
            static const char *kDefaultQualities[] = {"1080p Ultra", "1080p", "720p", "480p", "360p"};
            if (quality_idx >= 0 && quality_idx < static_cast<int>(m.streams.size()))
                wanted_quality = m.streams[static_cast<std::size_t>(quality_idx)].quality;
            else if (quality_idx >= 0 && quality_idx < 5)
                wanted_quality = kDefaultQualities[quality_idx];
            if (m.details_loaded && !m.details.id.empty()) {
                det = m.details;
                have = true;
            }
            catalog_status = "Открытие потока...";
            catalog_version++;
            write_log("[DEBUG] play_title_async wanted_quality='%s' from q_idx=%d streams=%zu\n",
                      wanted_quality.c_str(), quality_idx, m.streams.size());
        }
        if (!have) {
            if (play_serial_.load() != serial)
                return;
            VideoDetails fetched;
            if (item_url.empty() || !client.get_details(item_url, fetched)) {
                fail_buffering("Не удалось открыть страницу фильма");
                return;
            }
            if (fetched.is_series) {
                const std::string tr = fetched.translators.empty() ? "238" : fetched.translators.front().id;
                client.get_episodes(fetched, tr, fetched.seasons);
            }
            {
                std::lock_guard<std::mutex> lock(catalog_mutex);
                if (movie_idx < static_cast<int>(movies.size())) {
                    movies[movie_idx].details = fetched;
                    movies[movie_idx].details_loaded = true;
                    if (!fetched.description.empty())
                        movies[movie_idx].blurb = fetched.description;
                    if (!fetched.name.empty())
                        title = fetched.name;
                }
            }
            det = std::move(fetched);
        }
        {
            std::lock_guard<std::mutex> lock(catalog_mutex);
            if (movie_idx < static_cast<int>(movies.size())) {
                const auto &m = movies[movie_idx];
                if (!m.details.translators.empty()) {
                    const int tr_i = std::clamp(translator_idx, 0, (int)m.details.translators.size() - 1);
                    tr_id = m.details.translators[tr_i].id;
                    voice = m.details.translators[tr_i].name;
                }
                if (m.details.is_series && !m.details.seasons.empty()) {
                    const int s_i = std::clamp(season_idx, 0, (int)m.details.seasons.size() - 1);
                    s_id = m.details.seasons[s_i].id;
                    if (!m.details.seasons[s_i].episodes.empty()) {
                        const int e_i = std::clamp(episode_idx, 0, (int)m.details.seasons[s_i].episodes.size() - 1);
                        e_id = m.details.seasons[s_i].episodes[e_i].id;
                    }
                    title += " (" + m.details.seasons[s_i].title + ", серия " + e_id + ")";
                }
                det = m.details;
            }
        }
        if (play_serial_.load() != serial)
            return;
        write_log("[DEBUG] play_title_async before get_streams id=%s tr=%s s=%s e=%s is_series=%d wanted=%s\n",
                  det.id.c_str(), tr_id.c_str(), s_id.c_str(), e_id.c_str(), (int)det.is_series,
                  wanted_quality.c_str());
        auto streams = client.get_streams(det, tr_id, s_id, e_id);
        write_log("[DEBUG] play_title_async after get_streams count=%zu\n", streams.size());
        if (play_serial_.load() != serial)
            return;
        {
            std::lock_guard<std::mutex> lock(catalog_mutex);
            if (movie_idx < static_cast<int>(movies.size()))
                movies[movie_idx].streams = streams;
        }
        if (streams.empty()) {
            fail_buffering("Поток недоступен");
            return;
        }
        const int best = pick_stream_index(streams, wanted_quality);
        if (play_serial_.load() != serial)
            return;
        {
            std::lock_guard<std::mutex> pl(play_mu_);
            if (play_serial_.load() != serial)
                return;
            pending_play_url_ = streams[static_cast<std::size_t>(best)].url;
            pending_play_title_ = title;
            pending_play_voice_ = voice;
            pending_play_quality_ = streams[static_cast<std::size_t>(best)].quality;
            pending_play_item_url_ = item_url;
            pending_seek_ = start_time;
            playing_movie_idx = movie_idx;
            playing_translator_idx = translator_idx;
            playing_quality_idx = best;
            playing_season_idx = season_idx;
            playing_episode_idx = episode_idx;
            write_log("[DEBUG] play_title_async chosen best=%d quality=%s wanted=%s url=%s\n",
                      best, pending_play_quality_.c_str(), wanted_quality.c_str(), pending_play_url_.c_str());
        }
        {
            std::lock_guard<std::mutex> lock(catalog_mutex);
            catalog_status.clear();
            catalog_version++;
        }
    }).detach();
}

void App::halt_decoder()
{
    player.close();
    if (mixer) {
        mixer->swap_stream(1, nullptr);
        mixer->set_stream_gain(0, 0.0f, 0.1f);
    }
    if (device && device->logicalDevice)
        vkDeviceWaitIdle(device->logicalDevice);
    if (video_texture.hui_handle != 0 && renderer) {
        renderer->destroy_texture(video_texture.hui_handle);
        video_texture.hui_handle = 0;
    }
    video_texture.release();
}

void App::play_stream(const std::string &title, const std::string &voice, const std::string &url)
{
    const std::string quality = pending_play_quality_.empty() ? std::string("HD") : pending_play_quality_;
    const std::string item_url = pending_play_item_url_;
    const double seek_to = pending_seek_;
    pending_seek_ = -1.0;

    if (is_playing)
        save_continue_progress();
    halt_decoder();
    is_playing = false;

    playing_title = title;
    playing_voice = voice;
    playing_quality = quality;
    playing_url = item_url;

    write_log("[DEBUG] play_stream calling player.open(%s) seek=%.1f\n", url.c_str(), seek_to);
    is_buffering = true;
    if (player.open(url)) {
        write_log("[DEBUG] player.open succeeded\n");
        if (mixer) {
            mixer->set_stream_gain(0, 0.0f, 0.1f);
            mixer->swap_stream(1, &audio_ring);
            mixer->set_stream_gain(1, 1.0f, 0.1f);
        }
        if (seek_to > 5.0)
            player.seek(seek_to);
        is_playing = true;
        osd_timer = 4.0f;
        osd_visible = true;
    } else {
        write_log("[DEBUG] player.open FAILED\n");
        is_buffering = false;
        std::lock_guard<std::mutex> lock(catalog_mutex);
        catalog_status = "Ошибка воспроизведения видео";
        catalog_version++;
    }
}

void App::stop_playback()
{
    const uint32_t serial = ++play_serial_;
    write_log("[DEBUG] stop_playback serial=%u playing=%d buffering=%d\n", serial,
              (int)is_playing.load(), (int)is_buffering.load());
    {
        std::lock_guard<std::mutex> pl(play_mu_);
        pending_play_url_.clear();
        pending_play_title_.clear();
        pending_play_voice_.clear();
        pending_play_quality_.clear();
        pending_play_item_url_.clear();
        pending_seek_ = -1.0;
    }
    if (is_playing)
        save_continue_progress();
    halt_decoder();
    is_playing = false;
    is_buffering = false;
}

void App::update(float dt)
{
    if (!network_started_) {
        write_log("[DEBUG] App::update network_started = false, starting\n");
        network_started_ = true;
        load_movie_list_async(list_tab, list_type);
        refresh_account_async();
    }

    std::string url, title, voice, quality, item_url;
    double seek_to = -1.0;
    {
        std::lock_guard<std::mutex> pl(play_mu_);
        if (!pending_play_url_.empty()) {
            url = std::move(pending_play_url_);
            title = std::move(pending_play_title_);
            voice = std::move(pending_play_voice_);
            quality = std::move(pending_play_quality_);
            item_url = std::move(pending_play_item_url_);
            seek_to = pending_seek_;
            pending_seek_ = -1.0;
            pending_play_url_.clear();
        }
    }
    if (!url.empty()) {
        pending_play_quality_ = quality;
        pending_play_item_url_ = item_url;
        pending_seek_ = seek_to;
        play_stream(title, voice, url);
        if (!quality.empty())
            playing_quality = quality;
    }

    // Upload pending textures to GPU (throttle at most 2 per frame to ensure smooth 60 FPS)
    {
        std::vector<PendingTexture> to_upload;
        {
            std::lock_guard<std::mutex> p_lock(pending_mutex);
            if (!pending_textures.empty()) {
                size_t count = std::min<size_t>(pending_textures.size(), 2);
                to_upload.assign(pending_textures.begin(), pending_textures.begin() + count);
                pending_textures.erase(pending_textures.begin(), pending_textures.begin() + count);
            }
        }

        if (!to_upload.empty() && renderer) {
            std::lock_guard<std::mutex> lock(catalog_mutex);
            for (const auto &pt : to_upload) {
                if (!pt.photo_url.empty()) {
                    uint32_t handle = renderer->create_texture(pt.width, pt.height, pt.rgba.data());
                    if (handle != 0)
                        photo_textures[pt.photo_url] = handle;
                    continue;
                }
                if (pt.person_movie_index >= 0) {
                    if (pt.person_movie_index < static_cast<int>(person_movies.size())) {
                        MovieData &movie = person_movies[static_cast<std::size_t>(pt.person_movie_index)];
                        if (pt.bind_url.empty() || movie.cover_url == pt.bind_url) {
                            uint32_t handle = renderer->create_texture(pt.width, pt.height, pt.rgba.data());
                            if (handle != 0) {
                                movie.texture_id = handle;
                                movie.texture_ready = true;
                            }
                        }
                    }
                    person_version++;
                    continue;
                }
                if (pt.movie_index < static_cast<int>(movies.size())) {
                    uint32_t handle = renderer->create_texture(pt.width, pt.height, pt.rgba.data());
                    if (handle != 0) {
                        movies[pt.movie_index].texture_id = handle;
                        movies[pt.movie_index].texture_ready = true;
                    }
                }
            }
            catalog_version++;
        }
    }

    // Update video playback frame
    if (is_playing && player.is_open()) {
        if (osd_timer > 0.0f) {
            osd_timer -= dt;
            if (osd_timer <= 0.0f) osd_visible = false;
        }

        std::vector<uint8_t> frame_rgba;
        uint32_t vw = 0, vh = 0;
        if (player.get_frame(frame_rgba, vw, vh) && vw > 0 && vh > 0 &&
            frame_rgba.size() == static_cast<size_t>(vw) * static_cast<size_t>(vh) * 4) {
            if (video_texture.image == VK_NULL_HANDLE || video_texture.width != vw || video_texture.height != vh) {
                if (device && device->logicalDevice)
                    vkDeviceWaitIdle(device->logicalDevice);
                if (video_texture.hui_handle != 0 && renderer) {
                    renderer->destroy_texture(video_texture.hui_handle);
                    video_texture.hui_handle = 0;
                }
                video_texture.init(device, queue, vw, vh);
                if (renderer && video_texture.view != VK_NULL_HANDLE) {
                    video_texture.hui_handle = renderer->import_texture(video_texture.view);
                }
            }
            if (video_texture.update_frame(frame_rgba.data(), frame_rgba.size()))
                is_buffering = false;
        }
    }
}
} // namespace rezka
