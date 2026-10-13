#pragma once

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
    void logout() {}
    bool login(const std::string& email, const std::string& password, std::string& error) { error = "Not implemented"; return false; }
    bool get_account(Account& acc) { return false; }

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

    // Quick search or full search
    std::vector<CatalogItem> search(const std::string &query);

    // Actor page: careers + filmography (HDrezkaTV / browser .b-person__career).
    bool get_person(const std::string &person_url, PersonDetails &out);
    // The titles on an actor's page, each once.
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
    std::vector<StreamQuality> get_streams(const VideoDetails &video,
                                           const std::string &translator_id,
                                           const std::string &season_id = "",
                                           const std::string &episode_id = "");

    bool http_get_binary(const std::string &url, std::vector<uint8_t> &out_bytes);

    // Probe known mirrors and keep the first one that returns a catalog page.
    bool pick_mirror();

    // Utility: stream deobfuscator
    static std::string clear_trash(const std::string &data);
    static std::vector<StreamQuality> parse_stream_string(const std::string &raw);

private:
    std::string mirror_{"https://hdrezka.ag"};
    std::string cookie_file_;
    std::recursive_mutex net_mu_;
    std::string http_get(const std::string &url);
    std::string http_post(const std::string &url, const std::map<std::string, std::string> &fields);
    bool solve_anubis_challenge(const std::string &html, const std::string &page_url);
};

} // namespace rezka
