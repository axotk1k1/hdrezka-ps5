#pragma once

#include "rezka_api.hpp"
#include "app_settings.hpp"
#include "video_player.hpp"
#include "texture_loader.hpp"
#include "gfx/vk/vk_renderer.hpp"
#include "audio/mixer.hpp"
#include "audio/stream_ring.hpp"
#include "VulkanDevice.h"

#include <string>
#include <vector>
#include <map>
#include <functional>
#include <set>
#include <memory>
#include <mutex>
#include <thread>
#include "big_thread.hpp"
#include <atomic>

namespace rezka
{

// The title's version: what the updater compares GitHub's releases with.
inline constexpr const char *kAppVersion = "1.0.1";
// The releases of the title (Настройки → О приложении, the updater).
inline constexpr const char *kUpdateRepo = "axotk1k1/hdrezka-ps5";

// A newer title found on GitHub: its version, notes and eboot.bin.
struct UpdateInfo
{
    std::string version;
    std::string notes;
    std::string url;    // eboot.bin's address
    std::string sha256; // when the release states it
    size_t size = 0;    // when the release states it
};

// Where an update stands (App::update_state).
enum class UpdateState : int
{
    idle,
    checking,
    latest,      // nothing newer
    available,   // update_info holds it
    downloading, // update_progress counts it
    installed,   // the new eboot.bin is in place: restart
    check_failed,
    install_failed,
};

struct MovieData
{
    std::string id;
    std::string title;
    std::string original_title;
    std::string url;
    std::string cover_url;
    std::string local_poster;
    std::string info;
    std::string cover_info; // episode / premiere from span.info
    std::string entity;     // Фильм / Сериал from i.entity
    std::string genre;
    int year = 2024;
    float rating = 0.0f;
    int rating_votes = 0;
    std::string blurb;
    std::string quality; // poster badge: "4K", "HD", "1080p", ...
    uint32_t texture_id = 0;
    bool texture_ready = false;
    VideoDetails details;
    bool details_loaded = false;
    std::vector<StreamQuality> streams;
};

// One labeled row on the HDrezkaTV-style home page.
struct HomeSection
{
    std::string title;
    int begin = 0; // index into movies[]
    int count = 0;
};

// One line of a WebVTT file: shown from start to end (seconds of the film).
struct SubtitleCue
{
    double start = 0.0;
    double end = 0.0;
    std::string text; // lines joined by '\n', tags removed
};

struct ContinueEntry
{
    std::string url;
    std::string title;
    double t = 0.0;
    std::string translator_id;
    std::string quality;
    int translator_idx = 0;
    int quality_idx = 0;
    int season_idx = 0;
    int episode_idx = 0;
};

class App
{
public:
    static App &instance()
    {
        static App inst;
        return inst;
    }

    Client client;
    // Answers from the disk cache only (data/cache/): what the last run saw,
    // shown at start before the network is asked.
    Client cache_client;
    Account account;
    VideoPlayer player;
    VideoTexture video_texture;
    hui::audio::StreamRing audio_ring{1u << 18}; // ~5.5s @ 48kHz stereo -- survive seeks

    vks::VulkanDevice *device = nullptr;
    VkQueue queue = VK_NULL_HANDLE;
    hui::gfx::VkRenderer *renderer = nullptr;
    hui::audio::Mixer *mixer = nullptr;

    // Catalog state
    std::mutex catalog_mutex;
    std::vector<MovieData> movies;
    std::vector<MovieData> featured_movies; // hero slides (also mirrored in movies)
    std::vector<int> featured_indices;      // indices into movies[] for the hero
    std::vector<HomeSection> home_sections; // Фильмы / Сериалы / Мультфильмы
    bool home_layout = true;                // discover rows vs flat catalog grid
    std::vector<uint32_t> fallback_posters;
    int current_category = 0; // home filter or catalog mapping (see load_*)
    int home_filter = 1;      // 0 watching, 1 last, 2 popular, 3 soon (HDrezkaTV tabs)
    std::atomic<bool> catalog_loading{false};
    std::atomic<uint32_t> catalog_version{1};
    std::string catalog_status;

    // Pending poster uploads to GPU on main thread
    struct PendingTexture {
        int movie_index = 0;
        int person_movie_index = -1; // poster for person_movies[i], when >= 0
        std::string photo_url; // set for an actor's photo, else movie_index is the poster's
        std::string bind_url;  // person poster applies only while this cover is still there
        int width = 0;
        int height = 0;
        std::vector<uint8_t> rgba;
    };
    std::mutex pending_mutex;
    std::vector<PendingTexture> pending_textures;
    // Actors' photos, by address. Requested by the product page; the texture
    // is made on the main thread like a poster. Both are guarded by catalog_mutex.
    std::map<std::string, uint32_t> photo_textures;
    std::set<std::string> photo_requests;
    void request_photo(const std::string &url);
    uint32_t photo_texture(const std::string &url);

    // Player state
    std::atomic<bool> is_playing{false};
    std::atomic<bool> is_buffering{false};
    std::string playing_title;
    std::string playing_voice;
    std::string playing_quality;
    std::string playing_url;
    int playing_movie_idx = -1;
    int playing_translator_idx = 0;
    int playing_quality_idx = 0;
    int playing_season_idx = 0;
    int playing_episode_idx = 0;
    std::mutex play_mu_;
    std::atomic<uint32_t> play_serial_{0};
    std::string pending_play_url_;
    std::string pending_play_title_;
    std::string pending_play_voice_;
    std::string pending_play_quality_;
    std::string pending_play_item_url_;
    double pending_seek_ = -1.0;
    float osd_timer = 4.0f;
    bool osd_visible = true;

    // Resume positions (url → entry), persisted under data/continue.txt
    std::mutex continue_mutex_;
    std::map<std::string, ContinueEntry> continue_map_;

    void init(vks::VulkanDevice *dev, VkQueue q, hui::gfx::VkRenderer *rend, hui::audio::Mixer *mix);
    void shutdown();

    void load_category_async(int category_idx);
    // HDrezkaTV home: newest slider hero + films/series/cartoons rows.
    // filter_idx: 0=watching, 1=last, 2=popular, 3=soon
    void load_home_async(int filter_idx = 1);
    // HDrezkaTV's home list: tab 0 watching, 1 last, 2 popular; type 0 all,
    // 1 films, 2 series, 3 cartoons, 4 anime. kind 0 is the home list, 1 the
    // drawer's Новинки (/new, latest), 2 its Анонсы (filter=soon); the tab
    // counts for the home list only. Replaces the catalogue with the first
    // page; load_movie_list_more_async appends the next one.
    void load_movie_list_async(int tab, int type, int kind = 0);
    void load_movie_list_more_async();
    int list_tab = 0;
    int list_type = 0;
    int list_kind = 0;                     // 3: a paged list of paged_list_async
    std::function<std::string(Client &, int)> list_path; // its pages; guarded by catalog_mutex
    int list_page = 0;                     // pages in movies[]; guarded by catalog_mutex
    bool list_has_next = false;            // guarded by catalog_mutex
    std::atomic<bool> list_more_loading{false};
    // Requests the player is waiting on (a title's page, its streams) while
    // this is above zero: the poster worker lets them have the connection.
    std::atomic<int> urgent_requests{0};
    std::atomic<int> poster_hint{0};       // the list's posters are fetched from this index on
    // The full search's results, a page at a time (the app's SearchResult screen).
    void search_async(const std::string &query);
    // Any of the app's paged lists that are not the home tabs (list_kind 3):
    // the catalogue is replaced with page 1 of path_of(page), and
    // load_movie_list_more_async appends the next pages while the site has them.
    void paged_list_async(const std::string &title, std::function<std::string(Client &, int)> path_of);
    // "Расширенный поиск": the site's categories, genres and years (search_filters,
    // guarded by catalog_mutex; filters_state 0 none, 1 loading, 2 loaded, 3 failed),
    // and the list a choice makes: link_part is the genre's path and the year.
    SearchFilters search_filters;
    std::atomic<int> filters_state{0};
    void load_search_filters_async();
    void extended_search_async(const std::string &title, const std::string &link_part);
    // "Поиск фильмов и сериалов по стране": /country/<name>.
    void country_search_async(const std::string &country);
    void remove_search_history(const std::string &query);
    // The search box: live suggestions for what is typed (suggest_version
    // counts their changes), and the history of the queries sent.
    std::vector<CatalogItem> search_suggestions; // guarded by catalog_mutex
    std::atomic<uint32_t> suggest_version{0};
    std::atomic<bool> suggest_loading{false};
    void suggest_async(const std::string &query);
    std::vector<std::string> get_search_history();
    void add_search_history(const std::string &query);
    void clear_search_history();
    // Voice search: the microphone records, the clip goes to the speech-to-text
    // endpoint (HDREZKA_STT_URL, or the first line of data/stt.txt). Without an
    // endpoint voice_available is false and the caller falls back on the
    // console's dictation. voice_state: 0 idle, 1 recording, 2 recognising,
    // 3 a text is ready in take_voice_text, 4 it failed.
    bool voice_available();
    bool voice_start();
    void voice_finish_async();
    void voice_cancel();
    std::string take_voice_text();
    std::atomic<int> voice_state{0};
    // Fills the catalogue with what the fetch returns; genre_label names it.
    void list_async(std::function<std::vector<CatalogItem>(Client &)> fetch, const std::string &genre_label);
    // Actor page (separate from the home catalogue).
    PersonDetails person;
    std::vector<MovieData> person_movies; // films of the active career chip
    std::vector<std::string> person_career_titles;
    int person_career_idx = 0;
    int person_page = 1;
    std::atomic<bool> person_loading{false};
    std::atomic<bool> person_has_next{false};
    std::atomic<bool> person_more_loading{false};
    std::atomic<uint32_t> person_version{0};
    std::atomic<uint32_t> person_epoch{0}; // bumped when the actor or the career changes
    std::string person_status;
    void person_async(const std::string &url, const std::string &name);
    void person_select_career(int career_idx);
    // The next /person/.../page/N/ when the site still has films to give.
    void person_load_more_async();
    // The home banner: the first ten of "Фильмы — Смотрят сейчас", each filled
    // in from its page (description, genre, country, the large poster). Its
    // own thread and texture queue, so the catalogue's list changes do not
    // drop its pictures. Guarded by catalog_mutex; hero_version counts changes.
    struct HeroSlide {
        CatalogItem item; // opens the title's page
        std::string title;
        std::string year;
        std::string genre;
        std::string country;
        std::string quality;
        std::string description;
        uint32_t texture = 0; // the full cover, once uploaded
        int tex_w = 0;
        int tex_h = 0;
    };
    std::vector<HeroSlide> hero_slides;
    std::atomic<uint32_t> hero_version{0};
    std::atomic<int> hero_state{0}; // 0 not asked yet, 1 loading, 2 loaded, 3 failed
    // Inserts (or finds) a title met on another title's page (its franchise,
    // "Смотрите также") in movies[] so its own page can open; -1 without a url.
    int adopt_catalog_item(const CatalogItem &item);
    // A title's comments, a page at a time (the app's Comments screen). The
    // first page replaces the list, later ones are appended.
    std::vector<Comment> comments;          // guarded by catalog_mutex
    std::string comments_post;              // the post id they belong to
    int comments_page = 0;
    bool comments_has_next = false;
    std::atomic<bool> comments_loading{false};
    std::atomic<uint32_t> comments_version{0};
    void load_comments_async(const std::string &post_id, bool more);
    // Inserts (or finds) a person film in movies[] so the product page can open it.
    int adopt_person_film(int person_movie_idx);
    // Legacy name: loads the actor page (does not replace the home catalogue).
    void person_films_async(const std::string &url, const std::string &name);
    void load_details_async(int movie_idx);

    void play_stream(const std::string &title, const std::string &voice, const std::string &url);
    void play_title_async(int movie_idx, int translator_idx, int quality_idx, int season_idx,
                          int episode_idx, double start_time = -1.0, std::string quality_label = {});
    // Applied on the next App::update, on the main thread. Does not stop the
    // decoder or touch the Vulkan texture.
    void queue_playback(int movie_idx, int translator_idx, int quality_idx, int season_idx,
                        int episode_idx, double start_time, std::string quality_label);
    void login_async(const std::string& email, const std::string& password);
    void refresh_account_async();
    // Signs out on the site and forgets the session here.
    void logout_async();
    std::atomic<bool> login_busy{false};      // "Входим в аккаунт"
    // The account's lists, loaded for the profile's screens. Guarded by
    // catalog_mutex; account_version counts their changes.
    std::vector<WatchLaterItem> watch_later;
    std::vector<BookmarkFolder> bookmark_folders;
    std::atomic<bool> account_lists_loading{false};
    std::atomic<bool> account_lists_again{false};
    std::atomic<uint32_t> account_version{0};
    void load_account_lists_async();
    void remove_watch_later_async(const std::string &id);
    // A title's bookmark folders, as the app's title screen has them: the
    // page's own list (movies[i].details.bookmarks, checked where the title
    // is), or the account's folders when the page gave none. Signed in only.
    void load_bookmark_choices_async(int movie_idx);
    // Puts the title in the folder or takes it out (the site toggles it); the
    // folder shows pending until the site answers, then flips, and the
    // profile's lists are asked for again. bookmark_error is the site's
    // refusal, for the screen to show once (bookmark_error_version counts them).
    void toggle_bookmark_folder_async(int movie_idx, const std::string &cat_id);
    std::atomic<bool> bookmark_choices_loading_{false};
    std::string bookmark_error; // guarded by catalog_mutex
    std::atomic<uint32_t> bookmark_error_version{0};
    // Whether the site's account is signed in (the app's own bookmarks and
    // "Досмотреть" are the site's then).
    bool signed_in();
    void stop_playback();
    void save_continue_progress();
    bool get_continue(const std::string &url, ContinueEntry &out);
    void load_continue_map();
    void persist_continue_map();

    // The settings screen's preferences (data/settings.cfg). Read and changed
    // on the main thread; settings_snapshot() is for the workers.
    const AppSettings &settings() const { return settings_; }
    AppSettings settings_snapshot();
    // Keeps the new preferences, saves them and applies what they change at once.
    void update_settings(const AppSettings &next);
    // A title the country filter hides ("2024, США, Боевики").
    bool hidden_by_filter(const std::string &info);
    // "Источник": whether a mirror answers. mirror_check: 0 idle, 1 checking,
    // 2 it answers (and is now the mirror), 3 it does not.
    void check_mirror_async(const std::string &mirror);
    std::atomic<int> mirror_check{0};
    // "Очистить кэш": data/cache/ emptied on a worker; cache_clearing while it runs.
    void clear_cache_async();
    std::atomic<bool> cache_clearing{false};
    uint64_t cache_size_bytes() const;
    // Counts the titles opened (load_details_async): a list shown from the
    // cache is not swapped under a title opened from it.
    std::atomic<uint32_t> details_requests{0};

    // The playing stream's subtitles: its tracks (from get_streams), the one
    // chosen (-1: off) and its cues once loaded. Guarded by catalog_mutex.
    std::vector<SubtitleTrack> playing_subtitles;
    int subtitle_idx = -1;
    std::vector<SubtitleCue> subtitle_cues;
    std::atomic<uint32_t> subtitle_version{0};
    void select_subtitle_async(int idx);
    // The cue on screen at `t`, empty when none.
    std::string subtitle_at(double t);

    // The updater: GitHub's latest release of kUpdateRepo (the API, then
    // version.json on raw.githubusercontent.com when the API refuses), its
    // eboot.bin downloaded beside the running one, checked (size, SHA-256)
    // and put in its place. At start when "Автоматическая проверка
    // обновлений" is on, or by hand. update_version counts the changes;
    // update_info, update_message are guarded by catalog_mutex.
    std::atomic<int> update_state{static_cast<int>(UpdateState::idle)};
    std::atomic<uint32_t> update_version{0};
    std::atomic<int> update_progress{0}; // per mille of the download
    std::atomic<bool> update_manual{false}; // the last check was asked for by hand
    std::atomic<bool> update_skipped{false}; // "Пропустить": not again this session
    UpdateInfo update_info;
    std::string update_message;
    void check_updates_async(bool manual);
    void download_update_async();
    UpdateState update_status() const { return static_cast<UpdateState>(update_state.load()); }

    // Bookmarks, by page url. toggle_favorite returns the new state.
    bool is_favorite(const std::string &url);
    bool toggle_favorite(const std::string &url, const std::string &title);
    void update(float dt);

private:
    App() = default;
    ~App() { shutdown(); }
    App(const App &) = delete;
    App &operator=(const App &) = delete;

    struct QueuedPlay {
        bool pending = false;
        int movie_idx = -1;
        int translator_idx = 0;
        int quality_idx = 0;
        int season_idx = 0;
        int episode_idx = 0;
        double start_time = -1.0;
        std::string quality_label;
    };

    void stop_decoder();
    void release_video_texture();
    QueuedPlay queued_play_;
    bool release_video_texture_ = false;
    bool video_sync_texture_ = false;
    // The film's audio ring is in the mixer only while the film plays: a
    // paused film keeps what it decoded instead of the mixer draining it.
    bool video_audio_live_ = false;
    // Swapped with the decoder's buffer every frame, so a 1080p RGBA frame
    // (8 MB) is not allocated and zeroed again for each picture.
    std::vector<uint8_t> video_frame_rgba_;
    void queue_person_posters(uint32_t epoch, int from);
    void load_favorites();
    void persist_favorites();
    std::mutex favorites_mutex_;
    std::mutex history_mutex_;
    std::vector<std::string> search_history_; // newest first
    std::string voice_text_;
    std::atomic<uint32_t> suggest_serial_{0};
    void load_search_history();
    void persist_search_history();
    std::map<std::string, std::string> favorites_; // url -> title

    AppSettings settings_;
    std::mutex settings_mutex_;
    void apply_settings();

    BigThread worker_thread_;
    std::atomic<bool> stop_worker_{false};
    // The banner's loader (see hero_slides) and its decoded pictures.
    struct HeroPending {
        int index = 0;
        std::string url; // the slide's page, still the same one when it lands
        int width = 0;
        int height = 0;
        std::vector<uint8_t> rgba;
    };
    std::vector<HeroPending> hero_pending_; // guarded by pending_mutex
    BigThread hero_thread_;
    std::atomic<bool> stop_hero_{false};
    float hero_retry_ = 0.0f; // seconds until a failed banner is asked for again
    bool update_auto_done_ = false; // the check at start has been made
    void set_update_state(UpdateState state, const std::string &message = {});
    void load_hero_async();
    void fill_hero_slides(Client &from, std::vector<HeroSlide> slides, const std::function<bool()> &wait_turn);
    bool network_started_ = false;
    void init_defaults();
};

} // namespace rezka
