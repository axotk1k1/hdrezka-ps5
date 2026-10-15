#pragma once

#include <atomic>
#include <cstdint>
#include <ctime>
#include <functional>
#include <string>
#include <vector>
#include <memory>
#include <map>
#include <mutex>

namespace rezka
{

struct Account {
    std::string name;
    std::string avatar;
    std::string status;
    std::string email;
    bool logged_in = false;
    bool premium = false;
    std::string premium_text;
    std::string id;
    int premium_days = 0;  // left of the subscription
    std::string details;   // what else the page says: e-mail, the date of joining
};

// A title of the account's "Досмотреть" list.
struct WatchLaterItem
{
    std::string id;
    std::string date;
    std::string title;
    std::string project_info; // the small print by the title: year, country, genre
    std::string url;
    std::string cover_url;
    std::string info;         // what was watched: the season, the episode, the voice
    std::string next_info;    // what is out since
};

struct BookmarkFolder
{
    std::string id;
    std::string name;
    std::string count;
    std::string url;
    bool checked = false; // on a title's page: the title is in this folder
    bool pending = false; // an add_post for it is on its way (the app's spinner)
};

// "Расширенный поиск" (HDrezkaTV's ExtendedSearch, m8.e0.c): what the site's
// top menu offers. Each category of li.b-topnav__item (not "single") with the
// genres of its select.select-category (value: the genre's list path, "Любой"
// for "любого жанра"), and the years of select.select-year ("Все" for "за все
// время").
struct SearchGenre
{
    std::string name;
    std::string value; // the list's path, e.g. /films/best/fiction/
};
struct SearchCategory
{
    std::string name; // Фильмы, Сериалы, ...
    std::string url;
    std::vector<SearchGenre> genres;
};
struct SearchFilters
{
    std::vector<SearchCategory> categories;
    std::vector<std::string> years; // "Все" first, then 2026, 2025, ...
};

struct CatalogItem
{
    std::string id;
    std::string title;
    std::string url;
    std::string cover_url;
    std::string description;
    std::string info;      // Year, Country, Genre from .b-content__inline_item-link div
    std::string cover_info; // episode / premiere line from span.info (HDrezkaTV)
    std::string entity;     // Фильм / Сериал / … from span.cat i.entity (HDrezkaTV)
    std::string quality;    // badge from poster cover, e.g. "4K", "HD"
    std::string category;
    float rating = 0.0f;
    int year = 0;
};

struct Translator
{
    std::string id;
    std::string name;
    bool is_premium = false;
    std::string camrip = "0";
    std::string ads = "0";
    std::string director = "0";
};

struct Episode
{
    std::string id;
    std::string title;
};

struct Season
{
    std::string id;
    std::string title;
    std::vector<Episode> episodes;
};

// A subtitle track of a stream ("subtitle" in get_cdn_series' answer).
struct SubtitleTrack
{
    std::string label; // "Русский", "English", ...
    std::string url;   // a WebVTT file
};

struct StreamQuality
{
    std::string quality; // e.g. "1080p Ultra", "1080p", "720p", "480p", "360p"
    std::string url;     // Playable MP4 or HLS manifest
    bool is_premium = false;
};

struct RatingInfo
{
    float value = 0.0f;
    std::string votes;
    bool valid = false;
};

struct Person
{
    std::string id;
    std::string name;
    std::string job;
    std::string photo;
    std::string url;
};

// One career block on an actor's page (Актёр / Режиссёр / …), as in HDrezkaTV.
struct PersonCareer
{
    std::string title;
    std::vector<CatalogItem> movies;
    // "В базе N фильмов …" when the page states a total. 0 when it doesn't.
    int expected = 0;
};

struct PersonDetails
{
    std::string id;
    std::string url;
    std::string name;
    std::string name_orig;
    std::string photo;
    std::string photo_hq;
    std::string date_of_birth;
    std::string place_of_birth;
    std::string date_of_death;
    std::string place_of_death;
    std::string height;
    std::vector<PersonCareer> careers;
    std::vector<CatalogItem> movies; // unique across careers
};

// A title in "Входит в списки" or "Из серии", with the place it holds in a list.
struct ListRef
{
    std::string title;
    std::string place;
    std::string url;
};

// A part of the franchise a title belongs to ("Все части франшизы").
struct FranchisePart
{
    int number = 1;
    std::string title;
    std::string year;
    std::string url;
    float rating = 0.0f; // 0 when the site gives none
    bool current = false; // the title whose page this is
};

// One episode of a season's release schedule ("Расписание выхода серий").
struct ScheduleRow
{
    std::string episode;  // "1 сезон 8 серия"
    std::string title;
    std::string subtitle; // the original title
    std::string date;
    bool exists = false;  // already on the site
};

struct ScheduleSeason
{
    std::string title;
    std::vector<ScheduleRow> rows;
};

// How often a voice-over is chosen for the title ("Статистика выбора озвучек").
struct VoiceStat
{
    std::string title;
    float percent = 0.0f;
};

struct Comment
{
    std::string id;
    int indent = 0; // the depth of a reply
    std::string author;
    std::string avatar;
    std::string date;
    std::string text; // line breaks kept as '\n'
    int likes = 0;
    bool spoiler = false;
    bool admin = false;
};

struct VideoDetails
{
    std::string id;
    std::string name;
    std::string original_name;
    std::string description;
    std::string cover_url;
    std::string backdrop_url;
    std::string year;
    std::string duration;
    std::string age;
    std::string release_date;
    std::string slogan;
    std::vector<std::string> genres;
    std::vector<std::string> countries;
    float rating = 0.0f;
    RatingInfo rating_hdrezka;
    RatingInfo rating_imdb;
    RatingInfo rating_kinopoisk;
    std::vector<Person> people;
    std::vector<ListRef> lists;
    std::vector<ListRef> collections;
    std::string year_url;                  // the site's list of the year's titles
    std::vector<std::string> country_urls; // beside countries, when they are links
    std::vector<std::string> genre_urls;   // beside genres
    std::string full_cover_url; // the large poster the page's cover links to
    int comments_count = 0;
    std::string franchise_title;
    std::vector<FranchisePart> franchises;
    std::vector<CatalogItem> related; // "Смотрите также"
    std::vector<ScheduleSeason> schedule;
    std::vector<VoiceStat> voice_stats;
    bool blocked = false; // "Видео недоступно в вашем регионе"
    bool has_trailer = false;
    std::string trailer_id; // YouTube id, filled by Client::get_trailer
    bool is_series = false;
    std::vector<Translator> translators;
    std::vector<Season> seasons;
    std::string favs;
    std::string embedded_streams;
    // The account's bookmark folders as the title's page lists them, each
    // checked when the title is in it (HDrezkaTV's "bookmarks" of the page).
    std::vector<BookmarkFolder> bookmarks;
};

class Client
{
public:
    Client();
    ~Client();

    void set_mirror(const std::string &mirror);
    const std::string &get_mirror() const { return mirror_; }
    void set_cookie_file(const std::string &path) { cookie_file_ = path; }
    const std::string &get_cookie_file() const { return cookie_file_; }
    // The site's own sign-in, as the app does it: POST /ajax/login/ with
    // login_name, login_password and login_not_save=0. The session is the
    // dle_user_id and dle_password cookies the answer sets, kept in the cookie
    // file; error is the site's message when it refuses.
    bool login(const std::string &email, const std::string &password, std::string &error);
    // POST /logout, then the session's cookies are dropped from the file.
    void logout();
    // The id of the signed-in user, from the cookie file; empty when there is none.
    std::string session_user_id() const;
    // The signed-in user's page (/user/<id>/): name, avatar, premium.
    bool get_account(Account &acc);
    // "Досмотреть": the titles the account has started (/continue/).
    std::vector<WatchLaterItem> get_watch_later();
    bool remove_watch_later(const std::string &id);
    // The account's bookmark folders (/favorites/), and a folder's titles.
    std::vector<BookmarkFolder> get_bookmark_folders();
    // A title in a folder, or out of it: the site toggles it. POST
    // /ajax/favorites/ with post_id, cat_id and action=add_post, as the app
    // does; error is the site's message when it refuses.
    bool add_to_bookmarks(const std::string &post_id, const std::string &cat_id, std::string &error);
    // "Досмотреть": the site is told what is being watched when it starts,
    // POST /ajax/send_save/ with post_id, translator_id, season, episode and
    // current_time=1 (0 for a film's season and episode), as the app does.
    bool send_save(const std::string &post_id, const std::string &translator_id, const std::string &season,
                   const std::string &episode);

    // Fetch slider premiere content (id: 1 films, 2 series, 3 cartoons, 82 anime)
    // Parses div.b-newest_slider__list like HDrezkaTV.
    std::vector<CatalogItem> get_slider_newest(int genre_id = 1);

    // Fetch catalog by section ("films", "series", "cartoons", "animation", "new")
    // filter: last | popular | watching | soon | year | added (HDrezkaTV tabs)
    std::vector<CatalogItem> get_catalog(const std::string &category,
                                         const std::string &filter = "popular",
                                         int page = 1);

    // The home list of HDrezkaTV (its "MovieList" screen): one page of
    // <mirror>[/page/N]/?filter=<filter>[&genre=G]. filter: watching | last |
    // popular; genre: 0 all, 1 films, 2 series, 3 cartoons, 82 anime.
    // has_next tells whether the site offers a page after this one.
    // prefix is the section before it: "" for the home list, "/new" for Новинки.
    std::vector<CatalogItem> get_movie_list(const std::string &filter, int genre, int page,
                                            bool *has_next = nullptr,
                                            const std::string &prefix = "");

    // The titles of any list page of the site (a genre, a country, a year,
    // a collection), by its address.
    std::vector<CatalogItem> get_list_at(const std::string &url);

    // Home discover rows: hero from newest slider + films/series/cartoons for filter.
    struct HomePage {
        std::vector<CatalogItem> hero;
        std::vector<CatalogItem> films;
        std::vector<CatalogItem> series;
        std::vector<CatalogItem> cartoons;
    };
    HomePage get_home_page(const std::string &filter = "last");

    // The full search, a page at a time, as the app asks for it:
    // /search/?do=search&subaction=search&q=<query>[&page=N].
    std::vector<CatalogItem> search(const std::string &query, int page = 1, bool *has_next = nullptr);
    // Any list of the site by its path or address, with whether a page follows
    // (the app's rule: div.b-navigation without a disabled "next" arrow).
    std::vector<CatalogItem> get_list_page(const std::string &path, bool *has_next = nullptr);
    // The page paths of the app's lists: the full search's, Расширенный
    // поиск's (a genre's path and a year, /page/N after them) and a country's
    // (/country/<name, spaces as +>/page/N).
    std::string search_path(const std::string &query, int page) const;
    static std::string extended_search_path(const std::string &link_part, int page);
    std::string country_path(const std::string &country, int page) const;
    // Расширенный поиск's choices, from the site's front page (m8.e0.c).
    bool get_search_filters(SearchFilters &out);
    // The live suggestions of the search box (POST /engine/ajax/search.php):
    // a few titles with their kind, rating and year, as HDrezkaTV shows them
    // under the field. Each carries url, title, entity, rating and info.
    std::vector<CatalogItem> search_suggest(const std::string &query);
    // Speech to text: the WAV (16-bit mono PCM) is POSTed to `endpoint`; the
    // answer is plain text or {"text": "..."}. Empty when it fails.
    std::string stt_recognize(const std::string &endpoint, const std::vector<unsigned char> &wav);

    // Actor page: careers + filmography (HDrezkaTV / browser .b-person__career).
    // Page 1 is the person URL. Later pages are /person/<id>-<slug>/page/N/
    // when the site paginates; a page that is not a person document is refused.
    // has_next is set when the stats ("В базе N") still exceed the cards, or
    // the page's own next link points at another person page.
    bool get_person(const std::string &person_url, PersonDetails &out, bool *has_next = nullptr);
    bool get_person_page(const std::string &person_url, int page, PersonDetails &out,
                         bool *has_next = nullptr);
    // Every title on an actor's page, each once, following /page/N/ while the
    // site still has a person page to give.
    std::vector<CatalogItem> get_person_films(const std::string &person_url);

    // Get item details
    bool get_details(const std::string &item_url, VideoDetails &out_details);

    // One page of a title's comments (pages count from 1), as the app reads
    // them: /ajax/get_comments/?news_id=<post id>&cstart=<page>.
    bool get_comments(const std::string &post_id, int page, std::vector<Comment> &out,
                      bool *has_next = nullptr);

    // The YouTube id of the trailer of a title (its post id), empty when none.
    bool get_trailer(const std::string &post_id, std::string &youtube_id);

    // Get episodes for a series with specific translator
    bool get_episodes(const VideoDetails &video,
                      const std::string &translator_id,
                      std::vector<Season> &out_seasons);

    // Get stream links
    // subtitles, when given, gets the stream's subtitle tracks (none: empty).
    std::vector<StreamQuality> get_streams(const VideoDetails &video,
                                           const std::string &translator_id,
                                           const std::string &season_id = "",
                                           const std::string &episode_id = "",
                                           std::vector<SubtitleTrack> *subtitles = nullptr);

    bool http_get_binary(const std::string &url, std::vector<uint8_t> &out_bytes);

    // An address outside the site (GitHub's API and release files): no
    // mirror headers or cookies, redirects followed. progress(now, total) is
    // called as it arrives (total 0 while unknown); returning false stops it.
    // http_code gets the answer's status. True for a 2xx answer.
    static bool fetch_url(const std::string &url, std::vector<uint8_t> &out,
                          const std::function<bool(size_t, size_t)> &progress = {}, long *http_code = nullptr,
                          const char *accept = "*/*");
    // The SHA-256 of bytes, as 64 lower-case hex digits.
    static std::string sha256_hex(const uint8_t *data, size_t size);

    // Probe known mirrors and keep the first one that returns a catalog page.
    // With a preferred mirror (the settings' "Источник") only that one is tried.
    // The last mirror that answered (data/cache/last_mirror.txt) is tried
    // first; one that answered in the last minutes is not asked again unless
    // `force`.
    bool pick_mirror(bool force = false);
    void set_preferred_mirror(const std::string &mirror);
    std::string preferred_mirror() const;
    // Whether `mirror` answers with a catalog page; it becomes the mirror when it does.
    bool check_mirror(const std::string &mirror);
    // "Включить обход SSL": no certificate check when on (every Client).
    static void set_ssl_bypass(bool on);

    // Utility: stream deobfuscator
    static std::string clear_trash(const std::string &data);
    static std::vector<StreamQuality> parse_stream_string(const std::string &raw);

    // The disk cache (disk_cache.hpp): pictures, list pages, the titles' and
    // actors' pages are kept as they arrive. A cache-only client never goes
    // to the network: get_movie_list, get_details, get_person_page,
    // get_episodes and http_get_binary answer from the disk at any age, or
    // fail. The streams and the comments are never kept.
    void set_cache_only(bool on) { cache_only_ = on; }
    // A title's kept page goes (its bookmarks changed).
    static void forget_page(const std::string &url);

private:
    bool cache_only_ = false;
    std::atomic<time_t> mirror_verified_at_{0}; // when pick_mirror last saw mirror_ answer
    std::string mirror_{"https://hdrezka.ag"};
    mutable std::mutex preferred_mu_;
    std::string preferred_mirror_;
    std::string cookie_file_;
    std::recursive_mutex net_mu_;
    std::string http_get(const std::string &url);
    bool fetch_binary(const std::string &url, std::vector<uint8_t> &out_bytes);
    std::string http_post(const std::string &url, const std::map<std::string, std::string> &fields);
    bool solve_anubis_challenge(const std::string &html, const std::string &page_url);
};

} // namespace rezka
