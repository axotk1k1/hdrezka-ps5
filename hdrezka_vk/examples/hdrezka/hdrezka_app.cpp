#include "hdrezka_app.hpp"
#include <iostream>
#include <algorithm>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <curl/curl.h>

#include "logger.hpp"

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

void App::person_select_career(int career_idx)
{
    uint32_t ver = 0;
    std::vector<std::pair<int, std::string>> jobs;
    {
        std::lock_guard<std::mutex> lock(catalog_mutex);
        if (person.careers.empty()) {
            person_career_idx = 0;
            return;
        }
        person_career_idx = std::clamp(career_idx, 0, static_cast<int>(person.careers.size()) - 1);
        person_movies.clear();
        const auto &career = person.careers[static_cast<std::size_t>(person_career_idx)];
        person_movies.reserve(career.movies.size());
        for (const CatalogItem &it : career.movies)
            person_movies.push_back(movie_from_catalog(it, career.title));
        for (int i = 0; i < static_cast<int>(person_movies.size()); ++i)
            if (!person_movies[static_cast<std::size_t>(i)].cover_url.empty())
                jobs.emplace_back(i, person_movies[static_cast<std::size_t>(i)].cover_url);
        person_version++;
        ver = person_version.load();
    }
    if (jobs.empty())
        return;
    BigThread([this, jobs, ver]() {
        for (const auto &job : jobs) {
            if (stop_worker_ || person_version.load() != ver)
                return;
            std::vector<uint8_t> img_bytes;
            if (!client.http_get_binary(job.second, img_bytes))
                continue;
            int w = 0, h = 0;
            uint8_t *rgba =
                TextureLoader::decode_image_rgba(img_bytes.data(), img_bytes.size(), &w, &h);
            if (!rgba || w <= 0 || h <= 0)
                continue;
            PendingTexture pt;
            pt.person_movie_index = job.first;
            pt.width = w;
            pt.height = h;
            pt.rgba.assign(rgba, rgba + w * h * 4);
            TextureLoader::free_decoded_image(rgba);
            std::lock_guard<std::mutex> p_lock(pending_mutex);
            pending_textures.push_back(std::move(pt));
        }
    }).detach();
}

void App::person_async(const std::string &url, const std::string &name)
{
    const std::string full = photo_address(url, client.get_mirror());
    {
        std::lock_guard<std::mutex> lock(catalog_mutex);
        person = {};
        person.name = name;
        person.url = full;
        person_movies.clear();
        person_career_titles.clear();
        person_career_idx = 0;
        person_status = "Загрузка фильмографии...";
        person_loading = true;
        person_version++;
    }

    if (worker_thread_.joinable()) {
        stop_worker_ = true;
        worker_thread_.join();
        stop_worker_ = false;
    }

    worker_thread_ = BigThread([this, full, name]() {
        if (!client.pick_mirror()) {
            std::lock_guard<std::mutex> lock(catalog_mutex);
            person_status = "HDRezka не отвечает";
            person_loading = false;
            person_version++;
            return;
        }
        PersonDetails details;
        if (!client.get_person(full, details)) {
            std::lock_guard<std::mutex> lock(catalog_mutex);
            person_status = "Страница актёра недоступна";
            person_loading = false;
            person_version++;
            return;
        }
        if (details.name.empty())
            details.name = name;

        {
            std::lock_guard<std::mutex> lock(catalog_mutex);
            person = details;
            person_career_titles.clear();
            for (const PersonCareer &c : person.careers)
                person_career_titles.push_back(c.title + " (" + std::to_string(c.movies.size()) + ")");
            person_career_idx = 0;
            person_movies.clear();
            const std::vector<CatalogItem> *src = &person.movies;
            std::string genre = "Фильмография";
            if (!person.careers.empty()) {
                src = &person.careers[0].movies;
                genre = person.careers[0].title;
            }
            person_movies.reserve(src->size());
            for (const CatalogItem &it : *src)
                person_movies.push_back(movie_from_catalog(it, genre));
            person_status = person_movies.empty() ? "Фильмы не найдены" : "";
            person_loading = false;
            person_version++;
        }

        if (!person.photo.empty())
            request_photo(person.photo_hq.empty() ? person.photo : person.photo_hq);

        // Posters for the active career (first chip).
        for (size_t i = 0; i < person_movies.size(); ++i) {
            if (stop_worker_)
                break;
            std::string cover_url;
            {
                std::lock_guard<std::mutex> lock(catalog_mutex);
                if (i < person_movies.size())
                    cover_url = person_movies[i].cover_url;
            }
            if (cover_url.empty())
                continue;
            std::vector<uint8_t> img_bytes;
            if (!client.http_get_binary(cover_url, img_bytes))
                continue;
            int w = 0, h = 0;
            uint8_t *rgba = TextureLoader::decode_image_rgba(img_bytes.data(), img_bytes.size(), &w, &h);
            if (!rgba || w <= 0 || h <= 0)
                continue;
            PendingTexture pt;
            pt.person_movie_index = static_cast<int>(i);
            pt.width = w;
            pt.height = h;
            pt.rgba.assign(rgba, rgba + w * h * 4);
            TextureLoader::free_decoded_image(rgba);
            std::lock_guard<std::mutex> p_lock(pending_mutex);
            pending_textures.push_back(std::move(pt));
        }
    });
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

namespace {
// A request the player is waiting on: the poster worker stands aside for it.
struct UrgentRequest {
    std::atomic<int> &n;
    explicit UrgentRequest(std::atomic<int> &count) : n(count) { ++n; }
    ~UrgentRequest() { --n; }
};
} // namespace

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

void App::play_title_async(int movie_idx, int translator_idx, int quality_idx, int season_idx,
                           int episode_idx, double start_time)
{
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

    BigThread([this, movie_idx, translator_idx, quality_idx, season_idx, episode_idx, start_time]() {
        write_log("[DEBUG] play_title_async thread started movie_idx=%d tr=%d q=%d s=%d e=%d seek=%.1f\n",
                  movie_idx, translator_idx, quality_idx, season_idx, episode_idx, start_time);
        std::string url, title, voice = "HDRezka", item_url;
        VideoDetails det;
        bool have = false;
        std::string tr_id = "238";
        std::string s_id = "1";
        std::string e_id = "1";
        int q_idx = quality_idx;
        auto fail_buffering = [this](const char *status) {
            std::lock_guard<std::mutex> lock(catalog_mutex);
            catalog_status = status;
            catalog_version++;
            is_buffering = false;
        };
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
            if (m.details_loaded && !m.details.id.empty()) {
                det = m.details;
                have = true;
            }
            catalog_status = "Открытие потока...";
            catalog_version++;
        }
        if (!have) {
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
        write_log("[DEBUG] play_title_async before get_streams id=%s tr=%s s=%s e=%s is_series=%d\n",
                  det.id.c_str(), tr_id.c_str(), s_id.c_str(), e_id.c_str(), (int)det.is_series);
        auto streams = client.get_streams(det, tr_id, s_id, e_id);
        write_log("[DEBUG] play_title_async after get_streams count=%zu\n", streams.size());
        {
            std::lock_guard<std::mutex> lock(catalog_mutex);
            if (movie_idx < static_cast<int>(movies.size()))
                movies[movie_idx].streams = streams;
        }
        if (streams.empty()) {
            fail_buffering("Поток недоступен");
            return;
        }
        int best = 0;
        if (!streams.empty()) {
            if (quality_idx >= 0 && quality_idx < static_cast<int>(streams.size())) {
                best = quality_idx;
            } else {
                static const char* kTargets[] = {"1080p Ultra", "1080p", "720p", "480p", "360p"};
                const char* target = (quality_idx >= 0 && quality_idx < 5) ? kTargets[quality_idx] : "1080";
                bool found = false;
                for (size_t i = 0; i < streams.size(); ++i) {
                    if (streams[i].quality.find(target) != std::string::npos) {
                        best = static_cast<int>(i);
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    int best_rank = -1;
                    for (int i = 0; i < static_cast<int>(streams.size()); ++i) {
                        const auto &q = streams[i].quality;
                        int rank = 0;
                        if (q.find("2160") != std::string::npos || q.find("4K") != std::string::npos) rank = 4;
                        else if (q.find("1080") != std::string::npos) rank = 3;
                        else if (q.find("720") != std::string::npos) rank = 2;
                        else if (q.find("480") != std::string::npos) rank = 1;
                        if (rank >= best_rank) {
                            best_rank = rank;
                            best = i;
                        }
                    }
                }
            }
        }
        std::lock_guard<std::mutex> pl(play_mu_);
        pending_play_url_ = streams[best].url;
        pending_play_title_ = title;
        pending_play_voice_ = voice;
        pending_play_quality_ = streams[best].quality;
        pending_play_item_url_ = item_url;
        pending_seek_ = start_time;
        playing_movie_idx = movie_idx;
        playing_translator_idx = translator_idx;
        playing_quality_idx = best;
        playing_season_idx = season_idx;
        playing_episode_idx = episode_idx;
        write_log("[DEBUG] play_title_async chosen best=%d quality=%s url=%s\n",
                  best, pending_play_quality_.c_str(), pending_play_url_.c_str());
        std::lock_guard<std::mutex> lock(catalog_mutex);
        catalog_status.clear();
        catalog_version++;
    }).detach();
}

void App::play_stream(const std::string &title, const std::string &voice, const std::string &url)
{
    stop_playback();

    playing_title = title;
    playing_voice = voice;
    playing_quality = pending_play_quality_.empty() ? "HD" : pending_play_quality_;
    playing_url = pending_play_item_url_;
    const double seek_to = pending_seek_;
    pending_seek_ = -1.0;

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
    if (is_playing || is_buffering) {
        write_log("[DEBUG] stop_playback\n");
        if (is_playing)
            save_continue_progress();
        player.close();
        if (mixer) {
            mixer->swap_stream(1, nullptr);
            mixer->set_stream_gain(0, 0.0f, 0.1f);
        }
        if (video_texture.hui_handle != 0 && renderer) {
            renderer->destroy_texture(video_texture.hui_handle);
            video_texture.hui_handle = 0;
        }
        video_texture.release();
        is_playing = false;
        is_buffering = false;
    }
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
                        uint32_t handle = renderer->create_texture(pt.width, pt.height, pt.rgba.data());
                        if (handle != 0) {
                            person_movies[pt.person_movie_index].texture_id = handle;
                            person_movies[pt.person_movie_index].texture_ready = true;
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
        if (player.get_frame(frame_rgba)) {
            uint32_t vw = player.get_width();
            uint32_t vh = player.get_height();
            if (vw > 0 && vh > 0) {
                if (video_texture.image == VK_NULL_HANDLE || video_texture.width != vw || video_texture.height != vh) {
                    if (video_texture.hui_handle != 0 && renderer) {
                        renderer->destroy_texture(video_texture.hui_handle);
                        video_texture.hui_handle = 0;
                    }
                    video_texture.init(device, queue, vw, vh);
                    if (renderer) {
                        video_texture.hui_handle = renderer->import_texture(video_texture.view);
                    }
                }
                video_texture.update_frame(frame_rgba.data());
                is_buffering = false;
            }
        }
    }
}
} // namespace rezka
