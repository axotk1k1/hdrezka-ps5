// ps5-homebrew-ui - Design "Storefront": browse, inspect and buy titles.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Copied into HdRezka by PS5_VulkanTemplate's new-title.py from PS5_VKHomebrewUI
// 2188642 (src/concepts/store.cpp), in a namespace of its own: change it at
// will. Its program (../hdrezka.cpp) draws it through ps5ui::make_screen, below.
//
// A digital shop window on deep charcoal: a featured banner, filter chips and
// a grid of product cards, a full-screen product page, a cart drawer and a
// checkout. Nothing is sold: the prices are invented "credits" derived from
// each title's name. What makes it feel finished:
//
//   - commerce clarity: one function draws every price, so a discount (badge,
//     old price struck through, new price in the accent), "Free" and "Owned"
//     look the same on a card, in the banner, on the page and in the cart;
//   - the banner cross-fades every six seconds over a wide crop of the cover
//     that drifts slowly (its uv rectangle is animated, not its position), and
//     waits while it is focused or while anything is open over it;
//   - Cross opens the product page with a shared element: the card's cover
//     grows into the page's preview, its crop opening up to the whole cover,
//     while the rest of the page fades in around it (and back on Circle);
//   - "Add to cart" throws a small copy of the cover along an arc into the
//     cart counter, which bumps and rolls to its new number when it lands;
//   - the cart is a frosted drawer: rows collapse with a spring when removed,
//     the subtotal counts to its new value in the mono face, and the checkout
//     asks first, with the focus on Cancel;
//   - one focus highlight glides between banner, chips and cards, the grid
//     scrolls as a page with the chips sticking under the top bar, and every
//     edge answers with a soft refusal.

#include "concepts/concepts.hpp"

#include "core/tween.hpp"
#include "ui/glyphs.hpp"
#include "ui/motion.hpp"
#include "../hdrezka_app.hpp"
#include "../logger.hpp"
#include "../qr_code.hpp"
#include "platform.h"
struct pad &ps5_pad();

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace hui::screen
{

namespace
{

using gfx::Color;
using gfx::Rect;

constexpr int kKeyCols = 12;
constexpr int kKeyRows = 5;
constexpr int kKeyLayouts = 3; // 0 ENG, 1 РУС, 2 #!
const char *kKeysEN[48] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "-", "=",
    "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "@", ".",
    "a", "s", "d", "f", "g", "h", "j", "k", "l", ";", "'", "_",
    "z", "x", "c", "v", "b", "n", "m", ",", "/", "?", "!", "+"
};
const char *kKeysRU[48] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "-", "=",
    "й", "ц", "у", "к", "е", "н", "г", "ш", "щ", "з", "х", "ъ",
    "ф", "ы", "в", "а", "п", "р", "о", "л", "д", "ж", "э", "ё",
    "я", "ч", "с", "м", "и", "т", "ь", "б", "ю", ".", ",", "/"
};
const char *kKeysSYM[48] = {
    "!", "@", "#", "$", "%", "^", "&", "*", "(", ")", "_", "+",
    "`", "~", "{", "}", "[", "]", "\\", "|", ";", ":", "\"", "'",
    "<", ">", ",", ".", "?", "/", "=", "+", "@", "#", "$", "%",
    "*", "&", "(", ")", "-", "_", ".", ",", "/", "\\", "|", "~"
};

// ---- the design language ---------------------------------------------------

const Color kWhite = Color::rgb(0xffffff);
const Color kBlack = Color::rgb(0x000000);
const Color kClear = Color::rgb(0x000000, 0.0f);
const Color kInk = Color::rgb(0xf5f5f7);      // clean crisp white
const Color kAccent = Color::rgb(0xe50914);   // HDRezka vivid red (#e50914)
const Color kOnAccent = Color::rgb(0xffffff); // text on the accent
const Color kCoal = Color::rgb(0x08090c);     // deep cinematic black
const Color kCoalLow = Color::rgb(0x101116);
const Color kPanel = Color::rgb(0x16171e);    // plates and panels
const Color kOwned = Color::rgb(0x8fdab2);
const Color kStar = Color::rgb(0xf5c518);     // gold star rating
const Color kGood = Color::rgb(0x7cff8e);      // the site's green ratings
const Color kLink = Color::rgb(0x7ec8ff);      // "Нажмите, чтобы развернуть"
const Color kPlayTop = Color::rgb(0x4a98e6);   // the blue Смотреть button
const Color kPlayBottom = Color::rgb(0x2d6cc0);
const Color kClockBlue = Color::rgb(0x2c5f96);
const Color kFocusText = Color::rgb(0x151922); // text on a white focus plate
const Color kFocusHint = Color::rgb(0x1f5ca6);

constexpr float kWidth = gfx::kVirtualWidth;
constexpr float kHeight = gfx::kVirtualHeight;
constexpr float kMargin = 80.0f;
constexpr float kRight = kWidth - kMargin;

// The home page scrolls as one sheet under the top bar.
// HDrezkaTV / www discover: full-bleed hero, chips on the hero, then rows.
constexpr float kPageTop = 86.0f;
constexpr float kBannerY = 72.0f;
constexpr float kBannerH = 560.0f;
constexpr float kBannerArtW = 1280.0f; // wide cinematic backdrop
constexpr float kBannerRadius = 0.0f;  // full-bleed like www .home-hero
constexpr float kBannerSeconds = 7.0f;
constexpr int kFeatured = 5;
constexpr float kChipH = 40.0f;
constexpr float kStickY = 96.0f;
constexpr float kChipsY = kBannerY + kBannerH - 58.0f; // overlaid on hero bottom
constexpr float kGridY = kBannerY + kBannerH + 12.0f;
constexpr int kTopTabs = 5;
constexpr int kColumns = 6;     // flat catalog grid
constexpr int kHomeColumns = 4; // HDrezkaTV / www home rows
constexpr float kCardGap = 20.0f;
constexpr float kHomeCardGap = 16.0f;
constexpr float kCardW = (kWidth - 2.0f * kMargin - (kColumns - 1) * kCardGap) / kColumns;
constexpr float kHomeCardW =
    (kWidth - 2.0f * kMargin - (kHomeColumns - 1) * kHomeCardGap) / kHomeColumns;
constexpr float kCoverH = 414.0f;
constexpr float kHomeCoverH = kHomeCardW * 1.42f;
constexpr float kCardH = 414.0f;
constexpr float kHomeCardH = kHomeCoverH;
constexpr float kCardRadius = 14.0f;
constexpr float kRowPitch = 480.0f;
constexpr float kHomeRowTitleH = 34.0f;
constexpr float kHomeRowPitch = kHomeRowTitleH + kHomeCoverH + 28.0f;
constexpr float kLift = 0.04f;
constexpr float kViewBottom = 998.0f;
constexpr float kFadeFoot = 60.0f;
constexpr float kFadeHead = 20.0f;
constexpr float kStuckTop = kStickY + kChipH + 20.0f;

// The product page: the title's cover on the right, behind a column of its
// facts, buttons and description on the left, with the trailer beside the
// description. The column scrolls when a row opens or the focus goes down.
constexpr float kPageX = 96.0f;
constexpr float kPageRowW = 1100.0f;       // a fact row and its focus plate
constexpr float kPageRowPitch = 46.0f;     // from one fact row to the next
constexpr float kPageLinePitch = 32.0f;    // a line of an opened list
constexpr float kPageButtonH = 64.0f;
constexpr float kPageTextW = 1100.0f;      // the description's column
constexpr float kPageTopKeep = 200.0f;     // the focus stays below this line ...
constexpr float kPageViewBottom = 1000.0f; // ... and above this one
constexpr float kTrailerX = 1296.0f;
constexpr float kTrailerW = 528.0f;
constexpr float kTrailerH = 297.0f;
constexpr Rect kClockBox{1566.0f, -24.0f, 258.0f, 156.0f};
constexpr float kInfoW = 680.0f; // the wrap width of a title's copy
// Where a title's cover leaves from on its way into the cart (the cart flight).
constexpr Rect kFlightFrom{96.0f, 130.0f, 460.0f, 690.0f};
constexpr float kButtonRadius = 32.0f;

// The cart: its counter in the top bar and the drawer.
constexpr float kCartX = 1776.0f;
constexpr float kCartY = 78.0f;
constexpr float kDrawerW = 640.0f;
constexpr float kLinesTop = 200.0f;
constexpr float kLineH = 100.0f;
constexpr float kLinesView = 600.0f;
constexpr float kFlySeconds = 0.62f;

constexpr Rect kDialog{600.0f, 340.0f, 720.0f, 400.0f};
constexpr float kToastSeconds = 3.6f;

constexpr float kCardCropY = 0.0f;

constexpr const char *kTechniques[] = {
    "Shared-element transition: the card's cover and its uv crop grow into the product page",
    "Featured banner: cross-fade plus a slow uv-rectangle drift, paused while focused",
    "Add to cart: an arc flight into the counter, which bumps and rolls its number",
    "Frosted cart drawer: spring-collapsed rows, a counting subtotal, cancel-first checkout",
    "One price routine everywhere: badge, struck-through old price, Free and Owned",
    "A page that scrolls as one sheet with sticky filter chips and per-pixel edge fades",
    "Cards drawn layer by layer (covers, shapes, each font) to keep the draw calls low",
};

constexpr app::TourStep kTour[] = {
    {0.6f, 0, Direction::right},
    {0.5f, action_bit(Action::confirm)},
    {1.3f, action_bit(Action::confirm), Direction::none, "product"},
    {0.28f, 0, Direction::none, "flight"},
    {1.1f, action_bit(Action::back)},
    {0.7f, 0, Direction::right},
    {0.4f, action_bit(Action::confirm)},
    {1.0f, action_bit(Action::confirm)},
    {1.0f, action_bit(Action::west)},
    {1.1f, action_bit(Action::confirm), Direction::none, "play"},
    {0.9f, 0, Direction::right, "confirm"},
    {0.4f, action_bit(Action::confirm)},
    {0.7f, action_bit(Action::back)},
    {0.4f, action_bit(Action::back)},
    {0.5f, 0, Direction::down},
    {0.9f, 0, Direction::none, "purchased"},
    {1.2f, 0, Direction::up},
};

// HDrezkaTV home tabs (EnumC2014t + drawer Анонсы): watching / last / popular / soon
constexpr int kHomeChips = 4;
constexpr const char *kHomeChipNames[kHomeChips] = {
    "Смотрят сейчас", "Последние", "Популярные", "Анонсы"};

// Catalog filter chips (when top tab is Фильмы / Сериалы / …)
constexpr int kCatalogChips = 5;
constexpr const char *kCatalogChipNames[kCatalogChips] = {
    "Популярные", "Последние", "Смотрят", "За год", "По дате"};

constexpr int kFilters = kCatalogChips; // max chip slots for layout arrays

// What one pass over a group of cards records. A card alternates between its
// cover texture, plain shapes and several font atlases; drawn card by card,
// every switch would start a new draw call. The grid and the cart therefore
// draw all covers, then all shapes, then each face: the same pictures in a
// third of the draw calls.
enum Layer : unsigned
{
    kImages = 1,
    kShapes = 2,
    kSemibold = 4,
    kRegular = 8,
    kMono = 16,
    kAllLayers = 31,
};

enum class Zone : std::uint8_t
{
    banner,
    chips,
    grid,
};

enum class PageFocus : std::uint8_t
{
    watch,       // Смотреть: the voice and quality dialog
    resume,      // Продолжить: plays from the saved place
    bookmark,    // adds the title to the bookmarks, or takes it out
    qr,          // the QR code of the title's page, for a phone
    genres,      // Жанр: the full list opens under the row
    lists,       // Входит в списки
    collections, // Из серии
    cast,        // an actor: searches the titles with them
    trailer,     // the trailer's QR code: YouTube plays in a browser
};

// One line per catalogue entry, in its order: what the banner says under the
// title. A banner has room for a sentence fragment, not for the whole blurb.
constexpr const char *kPitches[] = {
    "The city rebuilds itself every lap.",
    "Chart a drowned coast, one tide at a time.",
    "Bend light. Grow impossible trees.",
    "One stolen ferry. One very long night.",
    "Fold, glide and unfold across unsent letters.",
    "The radio was switched off years ago.",
    "Small parcels, large distances.",
    "Two smiths, one forge, one prophecy.",
    "A valley of patient robots awaits orders.",
    "No map, no engine. Read the water.",
    "Keep one flower alive all winter.",
    "Four runners. Never drop the beat.",
    "A mountain, a volcano and a clock.",
    "Roads that hum when you find the line.",
    "You are the moth. The lantern has opinions.",
    "Spin a tiny moon just fast enough for rain.",
    "Late-night calls from a town under water.",
    "Every door is a little bit alive.",
    "Carry the last candle across a dark continent.",
    "Pottery for very specific dreams.",
    "One staircase, rebuilt every climb.",
    "Twenty minutes of perfect light.",
    "Light the right beacon at the right moment.",
    "Learn the forest floor by name.",
};
constexpr int kPitchCount = static_cast<int>(sizeof(kPitches) / sizeof(kPitches[0]));

// What the shop asks for a title. Everything is derived from the title's
// name, so the same title costs the same on every run and on every screen.
struct Offer
{
    int base = 0;    // list price in credits; 0 is a free title
    int percent = 0; // discount, 0 when there is none
    bool starts_owned = false;
    int ratings = 0; // how many players rated it
    int size_gb = 0;

    bool free() const
    {
        return base == 0;
    }
    // What is charged now: the discount applied, rounded down to ten credits.
    int price() const
    {
        return base * (100 - percent) / 100 / 10 * 10;
    }
};

Offer make_offer(const char *title)
{
    // FNV-1a over the title; the salt picks a pleasant spread for the sample
    // catalogue (a few free and owned titles, about a quarter on sale).
    std::uint32_t hash = 2166136261u ^ 2906u;
    for (const char *c = title; *c != '\0'; ++c)
    {
        hash ^= static_cast<unsigned char>(*c);
        hash *= 16777619u;
    }
    constexpr int kBase[] = {1490, 1990, 2490, 2990, 3490, 3990, 4990, 5990};
    constexpr int kPercent[] = {20, 30, 40, 50};
    Offer offer;
    const bool free = hash % 8 == 0;
    offer.base = free ? 0 : kBase[(hash >> 13) % 8];
    offer.starts_owned = !free && (hash >> 3) % 6 == 0;
    offer.percent = !free && (hash >> 7) % 3 == 0 ? kPercent[(hash >> 11) % 4] : 0;
    offer.ratings = 180 + static_cast<int>((hash >> 5) % 4200);
    offer.size_gb = 4 + static_cast<int>((hash >> 17) % 44);
    return offer;
}

// "2,490": credits with a thousands separator.
void format_credits(char *out, std::size_t size, int amount)
{
    if (amount >= 1000)
        std::snprintf(out, size, "%d,%03d", amount / 1000, amount % 1000);
    else
        std::snprintf(out, size, "%d", amount);
}

// A cover's uv rectangle from a top-left based crop in 0..1.
constexpr Rect crop(float x, float y, float w, float h)
{
    return {x, y, w, h};
}

constexpr Rect kCoverUv = crop(0.0f, 0.0f, 1.0f, 1.0f);
constexpr Rect kCardCrop = crop(0.0f, 0.0f, 1.0f, 1.0f);

Rect scaled(const Rect &r, float scale)
{
    return {r.cx() - r.w * scale * 0.5f, r.cy() - r.h * scale * 0.5f, r.w * scale, r.h * scale};
}

// Baseline that centres a line of the given size on cy.
float centred(float cy, float size)
{
    return cy + size * 0.35f;
}

// The mark of the invented currency: a ring with a small diamond in it.
void draw_coin(gfx::DrawList &list, float cx, float cy, float radius, Color colour)
{
    list.ring(cx, cy, radius, std::max(1.5f, radius * 0.24f), colour);
    const float d = radius * 0.62f;
    list.rotated_rect({cx - d * 0.5f, cy - d * 0.5f, d, d}, 1.0f, 0.7854f, colour);
}

// A play icon (right-pointing triangle)
void draw_cart(gfx::DrawList &list, float cx, float cy, float size, Color colour)
{
    const float r = size * 0.45f;
    const float stroke = std::max(2.0f, size * 0.12f);
    list.line(cx - r * 0.5f, cy - r * 0.8f, cx + r * 0.8f, cy, stroke, colour);
    list.line(cx + r * 0.8f, cy, cx - r * 0.5f, cy + r * 0.8f, stroke, colour);
    list.line(cx - r * 0.5f, cy + r * 0.8f, cx - r * 0.5f, cy - r * 0.8f, stroke, colour);
}

// What the product page shows about its title. It is copied out of the
// catalogue every frame, so the page follows the details as they arrive and
// the resume point as playback saves it.
constexpr float kCastPitch = 240.0f; // a cast card and the gap after it
constexpr float kCastW = 220.0f;
constexpr float kCastH = 330.0f; // a portrait, 2:3, like the posters; the name sits under it

struct PageModel
{
    std::string url;
    std::string title;
    std::string original;
    std::string description; // "Загрузка…" until the details arrive
    int description_lines = 0;
    std::string slogan;
    std::string release_date;
    std::string year;
    std::string duration;
    std::string age;
    std::vector<std::string> countries;
    std::vector<std::string> genres;
    std::vector<rezka::ListRef> lists;
    std::vector<rezka::ListRef> collections;
    std::vector<rezka::Person> cast;
    rezka::RatingInfo kinopoisk;
    rezka::RatingInfo imdb;
    rezka::RatingInfo hdrezka;
    bool has_trailer = false;
    std::string trailer_id;
    bool has_resume = false;
    rezka::ContinueEntry resume;
    std::string resume_voice; // the voice the resume point was saved with
    // What HDrezkaTV's page shows besides
    bool loaded = false;
    bool is_series = false;
    bool blocked = false;
    bool pending = false; // "Ожидается": no video yet
    std::string post_id;
    std::string cover;      // the poster's address
    std::string full_cover; // ... and the large one's, the backdrop
    std::string year_url;
    std::vector<std::string> country_urls;
    std::vector<std::string> genre_urls;
    int comments_count = 0;
    std::string franchise_title;
    std::vector<rezka::FranchisePart> franchises;
    std::vector<rezka::CatalogItem> related;
    std::vector<rezka::ScheduleSeason> schedule;
    std::vector<rezka::VoiceStat> voice_stats;
    // The account's bookmark folders, checked where the title is (signed in)
    std::vector<rezka::BookmarkFolder> bookmarks;
    bool signed_in = false;
};

// One fact row of the page: its kind (0 slogan, 1 release date, 2 country,
// 3 genres, 4 lists, 5 collections), the baseline of its line, and how many
// lines of a list it takes when opened.
struct PageRow
{
    int kind = 0;
    float base = 0.0f;
    int lines = 0;
    bool open = false;
};

// Where everything on the product page sits, in page coordinates: before the
// scroll, which moves the content but not the clock.
struct PageLayout
{
    Rect watch{}, resume{}, bookmark{}, qr{};
    bool has_resume = false;
    float hint_y = 0.0f;
    float title_y = 0.0f;
    float original_y = 0.0f;
    float stats_y = 0.0f;
    std::vector<PageRow> rows;
    float desc_y = 0.0f; // baseline of the "Описание" heading
    Rect trailer{};
    bool has_trailer = false;
    Rect poster{};
    bool has_cast = false;
    float cast_y = 0.0f;
    std::vector<Rect> cast_cards;
    int cast_focus = 0;
    float bottom = 0.0f; // where the content ends
};

class Store final : public app::Concept
{
  public:
    explicit Store(app::Context &context) : context_(context)
    {
        // The shelf stays empty until HDRezka answers. The compiled demo
        // catalog (Lumen Drift and the rest) is not a fallback.
        rebuild_visible();
        refresh_catalog_from_app();
        apply_palette(true);
        ring_.snap(focus_target());
        ring_radius_.snap(focus_radius());
        plate_.snap(0.0f);
        glow_.snap(kAccent);
        Rect chips[kFilters];
        chip_layout(chips);
        filter_ = std::clamp(filter_, 0, active_chip_count() - 1);
        chip_pill_.snap(chips[filter_]);
        page_ring_.snap(Rect{kPageX, 130.0f, 270.0f, kPageButtonH}.inset(-6.0f));
        page_ring_radius_.snap(kButtonRadius + 6.0f);
        drawer_ring_.snap(checkout_rect());
    }

    void refresh_catalog_from_app()
    {
        auto &app = rezka::App::instance();
        std::vector<rezka::MovieData> current_movies;
        std::vector<int> feat_order;
        std::vector<rezka::HomeSection> sections;
        bool home = true;
        {
            std::lock_guard<std::mutex> lock(app.catalog_mutex);
            current_movies = app.movies;
            feat_order = app.featured_indices;
            sections = app.home_sections;
            home = app.home_layout;
            if (home)
                filter_ = std::clamp(app.home_filter, 0, kHomeChips - 1);
        }
        home_layout_ = home;
        home_sections_ = std::move(sections);
        {
            std::lock_guard<std::mutex> lock(app.catalog_mutex);
            tv_status_ = app.catalog_status;
        }
        tv_rebuild_cards(current_movies);

        const int count = static_cast<int>(current_movies.size());
        if (count == 0)
            return;

        offers_.clear();
        owned_.clear();
        card_titles_.clear();
        line_titles_.clear();
        copy_.clear();
        wished_.resize(static_cast<std::size_t>(count), false);
        lift_.resize(static_cast<std::size_t>(count));

        const float title_w = home_layout_ ? (kHomeCardW - 24.0f) : (kCardW - 24.0f);
        for (int i = 0; i < count; ++i)
        {
            const auto &m = current_movies[i];
            const Offer offer = make_offer(m.title.c_str());
            offers_.push_back(offer);
            owned_.push_back(offer.starts_owned);
            newest_year_ = std::max(newest_year_, m.year);

            card_titles_.push_back(context_.fonts.semibold.font->fit(m.title.c_str(), 24, title_w));
            line_titles_.push_back(
                context_.fonts.semibold.font->fit(m.title.c_str(), 24, kDrawerW - 334.0f));

            Copy copy;
            const gfx::Font &lead = *context_.fonts.semibold.font;
            const char *p = m.blurb.empty() ? m.info.c_str() : m.blurb.c_str();
            copy.pitch_lines = std::min(2, static_cast<int>(lead.wrap(p, 30, kInfoW).size()));
            while (copy.pitch_lines == 2 && copy.pitch_width > 260.0f &&
                   lead.wrap(p, 30, copy.pitch_width - 20.0f).size() == 2)
                copy.pitch_width -= 20.0f;
            copy.blurb_lines = std::min(
                4, static_cast<int>(context_.fonts.regular.font->wrap(p, 26, kInfoW).size()));
            copy_.push_back(copy);
        }

        if (feat_order.empty())
        {
            for (int i = 0; i < std::min(count, kFeatured); ++i)
                feat_order.push_back(i);
        }
        featured_ = std::move(feat_order);
        if (banner_ >= static_cast<int>(featured_.size()))
            banner_ = 0;
        if (page_item_ >= count)
            page_item_ = 0;

        rebuild_visible();
    }

    const app::ConceptInfo &info() const override
    {
        static const app::ConceptInfo kInfo{
            "hdrezka",
            "HDRezka",
            "Клиент HDRezka для PlayStation 5",
            "examples/hdrezka/kit/screen.cpp",
            audio::SoundSet::glass,
            kAccent,
            kTechniques,
        };
        return kInfo;
    }

    void enter() override
    {
        age_ = 0.0f;
        dialog_open_ = false;
        drawer_open_ = false;
        page_open_ = false;
    }

    void seek_player_to(rezka::App &app, double target)
    {
        const double dur = app.player.get_duration();
        if (dur > 0.0)
            target = std::clamp(target, 0.0, dur);
        else
            target = std::max(0.0, target);
        seek_cursor_ = target;
        app.player.seek(target);
    }

    // ---- the player: HDrezkaTV's VideoPlayer screen ---------------------------
    //
    // The picture, and over it the app's controls: a scrim, "Сезон X Серия Y"
    // and the title, the time on both sides of the progress line, and a row
    // of round buttons (previous episode, play or pause, next episode; the
    // quality on the right). They leave four seconds after the last press.
    // Hidden, the pad is the app's remote: left and right jump ten seconds,
    // up shows the controls, down toggles them, Cross pauses.

    enum PlButton
    {
        kPlPrevious,
        kPlPlay,
        kPlNext,
        kPlSubtitles,
        kPlQuality,
    };
    static std::string pl_time(double seconds)
    {
        const int t = std::max(0, static_cast<int>(seconds));
        char text[24];
        if (t >= 3600)
            std::snprintf(text, sizeof(text), "%d:%02d:%02d", t / 3600, (t / 60) % 60, t % 60);
        else
            std::snprintf(text, sizeof(text), "%02d:%02d", t / 60, t % 60);
        return text;
    }
    // The episode before or after the one playing; false when there is none.
    bool pl_episode(int delta, int &season, int &episode) const
    {
        auto &app = rezka::App::instance();
        std::lock_guard<std::mutex> lock(app.catalog_mutex);
        const int index = app.playing_movie_idx;
        if (index < 0 || index >= static_cast<int>(app.movies.size()))
            return false;
        const rezka::VideoDetails &d = app.movies[static_cast<std::size_t>(index)].details;
        if (!d.is_series || d.seasons.empty())
            return false;
        season = std::clamp(app.playing_season_idx, 0, static_cast<int>(d.seasons.size()) - 1);
        episode = app.playing_episode_idx + delta;
        if (episode < 0)
        {
            if (--season < 0)
                return false;
            episode = static_cast<int>(d.seasons[static_cast<std::size_t>(season)].episodes.size()) - 1;
        }
        else if (episode >= static_cast<int>(d.seasons[static_cast<std::size_t>(season)].episodes.size()))
        {
            if (++season >= static_cast<int>(d.seasons.size()))
                return false;
            episode = 0;
        }
        return episode >= 0;
    }
    // "Сезон 1 Серия 2" for a series, nothing for a film.
    std::string pl_episode_line() const
    {
        auto &app = rezka::App::instance();
        std::lock_guard<std::mutex> lock(app.catalog_mutex);
        const int index = app.playing_movie_idx;
        if (index < 0 || index >= static_cast<int>(app.movies.size()))
            return {};
        const rezka::VideoDetails &d = app.movies[static_cast<std::size_t>(index)].details;
        if (!d.is_series || d.seasons.empty())
            return {};
        const int s = std::clamp(app.playing_season_idx, 0, static_cast<int>(d.seasons.size()) - 1);
        const rezka::Season &season = d.seasons[static_cast<std::size_t>(s)];
        const int e = std::clamp(app.playing_episode_idx, 0, std::max(0, static_cast<int>(season.episodes.size()) - 1));
        return "Сезон " + season.id + " Серия " + (season.episodes.empty() ? std::to_string(e + 1) : season.episodes[static_cast<std::size_t>(e)].id);
    }
    std::vector<std::string> pl_qualities() const
    {
        std::vector<std::string> names;
        auto &app = rezka::App::instance();
        std::lock_guard<std::mutex> lock(app.catalog_mutex);
        const int index = app.playing_movie_idx;
        if (index >= 0 && index < static_cast<int>(app.movies.size()))
            for (const rezka::StreamQuality &q : app.movies[static_cast<std::size_t>(index)].streams)
                names.push_back(q.quality);
        return names;
    }
    // The stream's subtitle tracks: "Выкл." then each language.
    std::vector<std::string> pl_subtitles() const
    {
        std::vector<std::string> names;
        auto &app = rezka::App::instance();
        std::lock_guard<std::mutex> lock(app.catalog_mutex);
        if (app.playing_subtitles.empty())
            return names;
        names.push_back("Выкл.");
        for (const rezka::SubtitleTrack &t : app.playing_subtitles)
            names.push_back(t.label);
        return names;
    }
    // The buttons the row has now, left to right.
    std::vector<int> pl_buttons() const
    {
        std::vector<int> row;
        int s = 0, e = 0;
        if (pl_episode(-1, s, e))
            row.push_back(kPlPrevious);
        row.push_back(kPlPlay);
        if (pl_episode(1, s, e))
            row.push_back(kPlNext);
        if (!pl_subtitles().empty())
            row.push_back(kPlSubtitles);
        if (pl_qualities().size() > 1)
            row.push_back(kPlQuality);
        return row;
    }
    void pl_show(bool on)
    {
        pl_controls_ = on;
        pl_timer_ = 4.0f;
        if (on)
        {
            pl_focus_ = kPlPlay;
            pl_scrub_ = false;
        }
    }
    void pl_flash(int icon, const std::string &text, int side)
    {
        pl_flash_icon_ = icon;
        pl_flash_text_ = text;
        pl_flash_side_ = side;
        pl_flash_left_ = 2.0f;
    }
    void pl_step_episode(int delta, app::Feedback &feedback)
    {
        auto &app = rezka::App::instance();
        int season = 0, episode = 0;
        if (!pl_episode(delta, season, episode))
        {
            tv_refuse(feedback, false);
            return;
        }
        feedback.play(audio::Cue::select);
        const int index = app.playing_movie_idx;
        const int voice = app.playing_translator_idx;
        const int quality = app.playing_quality_idx;
        app.stop_playback();
        app.play_title_async(index, voice, quality, season, episode);
    }

    void update_player(const InputFrame &input, float dt, app::Feedback &feedback)
    {
        auto &app = rezka::App::instance();
        buffer_clock_ += dt;
        pl_flash_left_ = std::max(0.0f, pl_flash_left_ - dt);
        pl_bar_.target = pl_controls_ && pl_scrub_ ? 1.0f : 0.0f;
        pl_bar_.update(dt, 18.0f);
        pl_in_.target = pl_controls_ ? 1.0f : 0.0f;
        pl_in_.update(dt, omega(14.0f));
        app.osd_visible = false; // the controls keep their own time

        if (app.is_buffering && !app.is_playing)
        {
            if (input.is_pressed(Action::back))
            {
                app.stop_playback();
                feedback.play(audio::Cue::back);
            }
            pl_controls_ = true;
            pl_timer_ = 4.0f;
            pl_quality_open_ = false;
            pl_sub_open_ = false;
            return;
        }
        const double position = app.player.get_position();
        const double duration = app.player.get_duration();

        // The end of the file: "Автоматически включать следующую серию", else back to the page
        if (app.player.is_finished())
        {
            int season = 0, episode = 0;
            if (app.settings().auto_next_episode && pl_episode(1, season, episode))
                pl_step_episode(1, feedback);
            else
            {
                app.stop_playback();
                feedback.play(audio::Cue::back);
            }
            return;
        }

        if (pl_sub_open_)
        {
            const std::vector<std::string> names = pl_subtitles();
            const int count = static_cast<int>(names.size());
            if (input.is_pressed(Action::back) || input.nav == Direction::left)
            {
                pl_sub_open_ = false;
                feedback.play(audio::Cue::modal_close);
            }
            else if (input.nav == Direction::up || input.nav == Direction::down)
            {
                const int next = pl_sub_focus_ + (input.nav == Direction::down ? 1 : -1);
                if (next < 0 || next >= count)
                    tv_refuse(feedback, input.nav_repeat);
                else
                {
                    pl_sub_focus_ = next;
                    tv_tick(feedback, kWidth * 0.8f);
                }
            }
            else if (input.is_pressed(Action::confirm) && pl_sub_focus_ < count)
            {
                pl_sub_open_ = false;
                feedback.play(audio::Cue::select);
                app.select_subtitle_async(pl_sub_focus_ - 1);
            }
            return;
        }

        if (pl_quality_open_)
        {
            const std::vector<std::string> names = pl_qualities();
            const int count = static_cast<int>(names.size());
            if (input.is_pressed(Action::back) || input.nav == Direction::left)
            {
                pl_quality_open_ = false;
                feedback.play(audio::Cue::modal_close);
            }
            else if (input.nav == Direction::up || input.nav == Direction::down)
            {
                const int next = pl_quality_focus_ + (input.nav == Direction::down ? 1 : -1);
                if (next < 0 || next >= count)
                    tv_refuse(feedback, input.nav_repeat);
                else
                {
                    pl_quality_focus_ = next;
                    tv_tick(feedback, kWidth * 0.8f);
                }
            }
            else if (input.is_pressed(Action::confirm) && pl_quality_focus_ < count)
            {
                pl_quality_open_ = false;
                feedback.play(audio::Cue::select);
                if (pl_quality_focus_ != app.playing_quality_idx)
                {
                    // Applied next App::update. stop_playback here would free the
                    // frame the current DrawList still samples.
                    const int index = app.playing_movie_idx;
                    const int voice = app.playing_translator_idx;
                    const int season = app.playing_season_idx;
                    const int episode = app.playing_episode_idx;
                    std::string label;
                    {
                        std::lock_guard<std::mutex> lock(app.catalog_mutex);
                        if (index >= 0 && index < static_cast<int>(app.movies.size())) {
                            const auto &streams = app.movies[static_cast<std::size_t>(index)].streams;
                            if (pl_quality_focus_ >= 0 &&
                                pl_quality_focus_ < static_cast<int>(streams.size()))
                                label = streams[static_cast<std::size_t>(pl_quality_focus_)].quality;
                        }
                    }
                    write_log("[DEBUG] player quality change focus=%d playing=%d label=%s pos=%.1f\n",
                              pl_quality_focus_, app.playing_quality_idx, label.c_str(), position);
                    app.queue_playback(index, voice, pl_quality_focus_, season, episode, position,
                                       std::move(label));
                }
            }
            return;
        }

        if (!pl_controls_)
        {
            if (input.is_pressed(Action::back))
            {
                app.stop_playback();
                feedback.play(audio::Cue::back);
            }
            else if (input.is_pressed(Action::confirm))
            {
                app.player.toggle_pause();
                pl_flash(app.player.is_playing() ? kTvIconPlayArrow : kTvIconPause, "", 0);
            }
            else if (input.nav == Direction::left || input.nav == Direction::right)
            {
                // "Шаг быстрой перемотки" вперед и назад, ten seconds by default
                const int dir = input.nav == Direction::left ? -1 : 1;
                const int step = dir < 0 ? app.settings().step_replay : app.settings().step_forward;
                seek_player_to(app, position + static_cast<double>(step * dir));
                pl_flash(dir < 0 ? kTvIconFastRewind : kTvIconFastForward,
                         std::string(dir < 0 ? "-" : "+") + std::to_string(step) + " сек. (" + pl_time(seek_cursor_) + ")",
                         dir);
            }
            else if (input.nav == Direction::up || input.nav == Direction::down)
                pl_show(true);
            return;
        }

        // The controls are up
        if (input.pressed != 0 || input.nav != Direction::none)
            pl_timer_ = 4.0f;
        if (!pl_scrub_)
        {
            pl_timer_ -= dt;
            if (pl_timer_ <= 0.0f)
            {
                pl_controls_ = false;
                return;
            }
        }
        const std::vector<int> row = pl_buttons();
        if (pl_scrub_)
        {
            // On the line: left and right move a preview 5% a press, Cross goes there
            if (input.nav == Direction::left || input.nav == Direction::right)
                pl_fraction_ = std::clamp(pl_fraction_ + (input.nav == Direction::left ? -0.05f : 0.05f), 0.0f, 1.0f);
            else if (input.is_pressed(Action::confirm) && duration > 0.0)
            {
                seek_player_to(app, duration * static_cast<double>(pl_fraction_));
                pl_scrub_ = false;
                feedback.play(audio::Cue::select);
            }
            else if (input.is_pressed(Action::back) || input.nav == Direction::down)
            {
                pl_scrub_ = false;
                pl_focus_ = kPlPlay;
            }
            return;
        }
        if (input.is_pressed(Action::back))
        {
            app.stop_playback();
            feedback.play(audio::Cue::back);
            return;
        }
        int at = 0;
        for (int i = 0; i < static_cast<int>(row.size()); ++i)
            if (row[static_cast<std::size_t>(i)] == pl_focus_)
                at = i;
        if (input.nav == Direction::left || input.nav == Direction::right)
        {
            const int next = at + (input.nav == Direction::right ? 1 : -1);
            if (next < 0 || next >= static_cast<int>(row.size()))
                tv_refuse(feedback, input.nav_repeat);
            else
            {
                pl_focus_ = row[static_cast<std::size_t>(next)];
                tv_tick(feedback, kWidth * 0.5f);
            }
        }
        else if (input.nav == Direction::up)
        {
            pl_scrub_ = true;
            pl_fraction_ = duration > 0.0 ? static_cast<float>(std::clamp(position / duration, 0.0, 1.0)) : 0.0f;
        }
        else if (input.nav == Direction::down)
            pl_controls_ = false;
        else if (input.is_pressed(Action::confirm))
        {
            switch (pl_focus_)
            {
            case kPlPrevious:
                pl_step_episode(-1, feedback);
                break;
            case kPlNext:
                pl_step_episode(1, feedback);
                break;
            case kPlSubtitles:
            {
                pl_controls_ = false;
                pl_sub_open_ = true;
                std::lock_guard<std::mutex> lock(app.catalog_mutex);
                pl_sub_focus_ = app.subtitle_idx + 1;
                feedback.play(audio::Cue::modal_open);
                break;
            }
            case kPlQuality:
                pl_controls_ = false;
                pl_quality_open_ = true;
                pl_quality_focus_ = std::clamp(app.playing_quality_idx, 0, std::max(0, static_cast<int>(pl_qualities().size()) - 1));
                feedback.play(audio::Cue::modal_open);
                break;
            default:
                app.player.toggle_pause();
                feedback.play(audio::Cue::toggle);
                break;
            }
        }
    }

    // The cue playing, as the settings' "Субтитры" place and size it: white
    // lines on a half black plate, inside the picture, 24 dp from its edges.
    void pl_draw_subtitle(gfx::DrawList &list, const Rect &picture) const
    {
        auto &app = rezka::App::instance();
        const std::string text = app.subtitle_at(app.player.get_position());
        if (text.empty())
            return;
        const float dp = kTvDp;
        const rezka::AppSettings &s = app.settings();
        const float size = static_cast<float>(rezka::kSubtitleFontSp[std::clamp(s.subtitle_font_size, 0, 3)]) * dp;
        const float line_h = size * 1.3f;
        const int arrangement = std::clamp(s.subtitle_arrangement, 0, 5);
        const bool top = arrangement < 3;
        const int side = arrangement % 3; // 0 centre, 1 start, 2 end
        const float margin = 24.0f * dp;
        const float max_w = picture.w * 0.8f;
        std::vector<std::string> lines;
        std::size_t at = 0;
        while (at <= text.size())
        {
            std::size_t nl = text.find('\n', at);
            if (nl == std::string::npos)
                nl = text.size();
            for (std::string &part : tv_medium().font->wrap(text.substr(at, nl - at), size, max_w))
                lines.push_back(std::move(part));
            at = nl + 1;
        }
        const float block_h = static_cast<float>(lines.size()) * line_h;
        float y = top ? picture.y + margin : picture.y + picture.h - margin - block_h;
        // Clear of the controls while they are up
        if (!top && pl_in_.value > 0.01f)
            y -= pl_in_.value * 150.0f * dp;
        for (const std::string &line : lines)
        {
            const float w = tv_medium().measure(line, size, 0.0f);
            const float x = side == 0 ? picture.cx() - w * 0.5f : side == 1 ? picture.x + margin : picture.x + picture.w - margin - w;
            list.rounded_rect({x - 8.0f * dp, y, w + 16.0f * dp, line_h}, 4.0f * dp, kBlack.with_alpha(0.55f));
            tv_text(list, tv_medium(), line, x, y, size, line_h, kWhite);
            y += line_h;
        }
    }

    // The app's waiting ring: a black disc and three quarters of a turning circle.
    void pl_spinner(gfx::DrawList &list) const
    {
        const float dp = kTvDp;
        list.circle(kWidth * 0.5f, kHeight * 0.5f, 44.0f * dp, kBlack.with_alpha(0.6f));
        list.arc(kWidth * 0.5f, kHeight * 0.5f, 20.0f * dp, 8.0f, buffer_clock_ * 5.0f, 4.4f, tv_primary());
    }

    void draw_player(app::Frame &frame) const
    {
        auto &app = rezka::App::instance();
        frame.scene.clear();
        frame.overlay.clear();
        frame.glass = false;
        frame.backdrop.mode = gfx::BackdropMode::none;
        const float dp = kTvDp;
        gfx::DrawList &list = frame.overlay;

        frame.scene.rounded_rect({0.0f, 0.0f, kWidth, kHeight}, 0.0f, kBlack);
        const bool has_frame = app.video_texture.hui_handle != 0 && !app.is_buffering;
        if (!has_frame)
        {
            pl_spinner(frame.scene);
            return;
        }
        float vw = static_cast<float>(app.player.get_width());
        float vh = static_cast<float>(app.player.get_height());
        if (vw < 1.0f || vh < 1.0f) {
            vw = static_cast<float>(app.video_texture.width);
            vh = static_cast<float>(app.video_texture.height);
        }
        Rect picture{0.0f, 0.0f, kWidth, kHeight};
        if (vw >= 1.0f && vh >= 1.0f) {
            const float scale = std::min(kWidth / vw, kHeight / vh);
            const float dw = vw * scale;
            const float dh = vh * scale;
            picture = {(kWidth - dw) * 0.5f, (kHeight - dh) * 0.5f, dw, dh};
        }
        frame.scene.image(app.video_texture.hui_handle, picture, gfx::kFullUv, kWhite, 0.0f);
        pl_draw_subtitle(frame.scene, picture);

        const float shown = pl_in_.value;
        if (shown > 0.01f)
        {
            list.push_opacity(tween::clamp01(shown));
            list.gradient_rect({0.0f, 0.0f, kWidth, kHeight}, 0.0f, kBlack.with_alpha(0.1f), kBlack.with_alpha(0.8f));
            // The column slides up from the bottom edge: 56 dp from the sides, 32 from the bottom
            list.push_transform(1.0f, 0.0f, 0.0f, 0.0f, (1.0f - shown) * 80.0f * dp);
            const float left = 56.0f * dp, right = kWidth - 56.0f * dp;
            const float buttons_y = kHeight - (32.0f + 40.0f) * dp;
            const float seek_y = buttons_y - (16.0f + 24.0f) * dp; // a 24 dp row
            const float title_y = seek_y - (16.0f + 36.0f) * dp;
            const std::string episode = pl_episode_line();
            tv_text(list, tv_regular(), episode, left, title_y - 28.0f * dp, 16.0f * dp, 24.0f * dp, tv_on_surface(), gfx::Align::left, 0.5f * dp);
            // The title alone: the episode has its own line above
            std::string title = item_title(app.playing_movie_idx);
            if (title.empty())
                title = app.playing_title;
            tv_text(list, tv_regular(), tv_regular().font->fit(title, 28.0f * dp, right - left), left, title_y, 28.0f * dp, 36.0f * dp,
                    tv_on_surface());

            const double duration = app.player.get_duration();
            const double position = app.player.get_position();
            const float played = duration > 1.0 ? static_cast<float>(std::clamp(position / duration, 0.0, 1.0)) : 0.0f;
            const float fraction = pl_scrub_ ? pl_fraction_ : played;
            const std::string now = pl_time(pl_scrub_ ? duration * static_cast<double>(pl_fraction_) : position);
            const std::string total = pl_time(duration);
            const float now_w = tv_medium().measure(now, 16.0f * dp, 0.5f * dp) + 24.0f * dp;
            const float total_w = tv_medium().measure(total, 16.0f * dp, 0.5f * dp) + 24.0f * dp;
            tv_text(list, tv_medium(), now, left + 12.0f * dp, seek_y, 16.0f * dp, 24.0f * dp, tv_on_surface(), gfx::Align::left, 0.5f * dp);
            tv_text(list, tv_medium(), total, right - 12.0f * dp, seek_y, 16.0f * dp, 24.0f * dp, tv_on_surface(), gfx::Align::right, 0.5f * dp);
            // The line: 4 dp, 10 while the focus is on it, with a thumb then
            const float h = (4.0f + 6.0f * pl_bar_.value) * dp;
            const Rect bar{left + now_w + 4.0f * dp, seek_y + 12.0f * dp - h * 0.5f, right - total_w - left - now_w - 8.0f * dp, h};
            list.rounded_rect(bar, h * 0.5f, tv_on_surface().with_alpha(0.2f));
            if (fraction > 0.001f && app.settings().rainbow_progress)
            {
                // "Радужная полоса прогресса": the spectrum along the whole line, shown as far as played
                static const Color kRainbow[] = {Color::rgb(0xff5252), Color::rgb(0xffab40), Color::rgb(0xffff00),
                                                 Color::rgb(0x69f0ae), Color::rgb(0x40c4ff), Color::rgb(0x7c4dff),
                                                 Color::rgb(0xe040fb)};
                const float played_w = bar.w * fraction;
                list.push_clip({bar.x, bar.y - 1.0f, played_w, bar.h + 2.0f});
                const float seg = bar.w / 6.0f;
                for (int i = 0; i < 6; ++i)
                    list.gradient_rect_h({bar.x + seg * static_cast<float>(i) - (i ? 1.0f : 0.0f), bar.y, seg + (i ? 1.0f : 0.0f), bar.h},
                                         (i == 0 || i == 5) ? h * 0.5f : 0.0f, kRainbow[i], kRainbow[i + 1]);
                list.pop_clip();
            }
            else if (fraction > 0.001f)
                list.gradient_rect_h({bar.x, bar.y, bar.w * fraction, bar.h}, h * 0.5f, tv_primary(), tv_inverse_primary());
            if (pl_bar_.value > 0.05f)
                list.circle(bar.x + bar.w * fraction, bar.cy(), 1.05f * h * pl_bar_.value, tv_primary());

            const std::vector<int> row = pl_buttons();
            float x = left;
            for (const int button : row)
            {
                // The quality keeps the right edge, the subtitles stand left of it
                const bool trailing = button == kPlQuality || button == kPlSubtitles;
                const bool has_quality = std::find(row.begin(), row.end(), kPlQuality) != row.end();
                const float bx = button == kPlQuality ? right - 40.0f * dp
                                 : button == kPlSubtitles ? right - (has_quality ? 92.0f : 40.0f) * dp
                                                          : x;
                if (!trailing)
                    x += 52.0f * dp;
                const bool focused = !pl_scrub_ && pl_controls_ && button == pl_focus_;
                const Rect r = scaled({bx, buttons_y, 40.0f * dp, 40.0f * dp}, focused ? 1.2f : 1.0f);
                list.circle(r.cx(), r.cy(), r.w * 0.5f, focused ? tv_on_surface() : tv_on_surface().with_alpha(0.2f));
                const int icon = button == kPlPrevious ? kTvIconSkipPrevious : button == kPlNext ? kTvIconSkipNext
                                 : button == kPlSubtitles ? kTvIconSubtitles
                                 : button == kPlQuality ? kTvIconHighQuality : app.player.is_playing() ? kTvIconPause : kTvIconPlayArrow;
                const float size = 24.0f * dp * (focused ? 1.2f : 1.0f);
                tv_icon(list, icon, r.cx() - size * 0.5f, r.cy() - size * 0.5f, size, focused ? tv_inverse_on_surface() : tv_on_surface());
            }
            list.pop_transform();
            list.pop_opacity();
        }

        // What a press did, for two seconds: a disc for play and pause, a pill for a jump
        if (pl_flash_left_ > 0.0f)
        {
            list.push_opacity(tween::clamp01(pl_flash_left_ * 4.0f));
            if (pl_flash_text_.empty())
            {
                list.circle(kWidth * 0.5f, kHeight * 0.5f, 44.0f * dp, kBlack.with_alpha(0.6f));
                tv_icon(list, pl_flash_icon_, kWidth * 0.5f - 24.0f * dp, kHeight * 0.5f - 24.0f * dp, 48.0f * dp, kWhite);
            }
            else
            {
                const float text_w = tv_medium().measure(pl_flash_text_, 16.0f * dp, 0.5f * dp);
                const float w = (20.0f + 36.0f + 16.0f + 20.0f) * dp + text_w;
                const float cx = kWidth * (pl_flash_side_ < 0 ? 0.25f : 0.75f);
                const Rect pill{cx - w * 0.5f, kHeight * 0.5f - 38.0f * dp, w, 76.0f * dp};
                list.rounded_rect(pill, pill.h * 0.5f, kBlack.with_alpha(0.6f));
                tv_icon(list, pl_flash_icon_, pill.x + 20.0f * dp, pill.cy() - 18.0f * dp, 36.0f * dp, kWhite);
                tv_text(list, tv_medium(), pl_flash_text_, pill.x + 72.0f * dp, pill.cy() - 12.0f * dp, 16.0f * dp, 24.0f * dp, kWhite,
                        gfx::Align::left, 0.5f * dp);
            }
            list.pop_opacity();
        }

        if (pl_sub_open_)
        {
            const std::vector<std::string> names = pl_subtitles();
            int chosen = -1;
            {
                std::lock_guard<std::mutex> lock(app.catalog_mutex);
                chosen = app.subtitle_idx + 1;
            }
            const float w = 320.0f * dp;
            const float h = std::min(kHeight - 48.0f * dp, (20.0f + 28.0f + 16.0f + 20.0f) * dp + static_cast<float>(names.size()) * 52.0f * dp);
            const Rect card{kWidth - 24.0f * dp - w, (kHeight - h) * 0.5f, w, h};
            list.bordered_rect(card, 28.0f * dp, tv_surface().with_alpha(0.5f), 1.0f * dp, tv_border_variant().with_alpha(0.5f));
            tv_text(list, tv_regular(), "Субтитры", card.cx(), card.y + 20.0f * dp, 22.0f * dp, 28.0f * dp, tv_on_surface(), gfx::Align::center);
            for (int i = 0; i < static_cast<int>(names.size()); ++i)
            {
                const Rect r{card.x + 20.0f * dp, card.y + 64.0f * dp + static_cast<float>(i) * 52.0f * dp, w - 40.0f * dp, 48.0f * dp};
                const bool focused = i == pl_sub_focus_;
                if (focused)
                    list.rounded_rect(r, 8.0f * dp, tv_on_surface());
                const Color content = focused ? tv_inverse_on_surface() : tv_on_surface();
                tv_text(list, tv_medium(), names[static_cast<std::size_t>(i)], r.x + 16.0f * dp, r.cy() - 10.0f * dp, 14.0f * dp, 20.0f * dp, content,
                        gfx::Align::left, 0.1f * dp);
                if (i == chosen)
                    tv_icon(list, kTvIconCheck, r.x + r.w - 32.0f * dp, r.cy() - 8.0f * dp, 16.0f * dp, content);
            }
        }

        if (pl_quality_open_)
        {
            // The app's side sheet: on the right, half see-through
            const std::vector<std::string> names = pl_qualities();
            const float w = 320.0f * dp;
            const float h = std::min(kHeight - 48.0f * dp, (20.0f + 28.0f + 16.0f + 20.0f) * dp + static_cast<float>(names.size()) * 52.0f * dp);
            const Rect card{kWidth - 24.0f * dp - w, (kHeight - h) * 0.5f, w, h};
            list.bordered_rect(card, 28.0f * dp, tv_surface().with_alpha(0.5f), 1.0f * dp, tv_border_variant().with_alpha(0.5f));
            tv_text(list, tv_regular(), "Качество", card.cx(), card.y + 20.0f * dp, 22.0f * dp, 28.0f * dp, tv_on_surface(), gfx::Align::center);
            for (int i = 0; i < static_cast<int>(names.size()); ++i)
            {
                const Rect r{card.x + 20.0f * dp, card.y + 64.0f * dp + static_cast<float>(i) * 52.0f * dp, w - 40.0f * dp, 48.0f * dp};
                const bool focused = i == pl_quality_focus_;
                if (focused)
                    list.rounded_rect(r, 8.0f * dp, tv_on_surface());
                const Color content = focused ? tv_inverse_on_surface() : tv_on_surface();
                tv_text(list, tv_medium(), names[static_cast<std::size_t>(i)], r.x + 16.0f * dp, r.cy() - 10.0f * dp, 14.0f * dp, 20.0f * dp, content,
                        gfx::Align::left, 0.1f * dp);
                if (i == app.playing_quality_idx)
                    tv_icon(list, kTvIconCheck, r.x + r.w - 32.0f * dp, r.cy() - 8.0f * dp, 16.0f * dp, content);
            }
        }
    }

#if defined(PS5_HOST_REFERENCE)
    // The reference build has no pad. HDREZKA_HOST_KEYS scripts one for its
    // pictures: "2.0:left,2.5:down,3:confirm" presses each at its second;
    // "4:type=name@mail.ru" types into the open text field.
    void host_script(InputFrame &input, float dt)
    {
        static const char *script = std::getenv("HDREZKA_HOST_KEYS");
        static float seconds = 0.0f;
        const float before = seconds;
        seconds += dt;
        for (const char *p = script; p && *p;)
        {
            char *colon = nullptr;
            const float at = std::strtof(p, &colon);
            if (!colon || *colon != ':')
                break;
            const char *name = colon + 1;
            const char *end = std::strchr(name, ',');
            const std::string key(name, end ? static_cast<std::size_t>(end - name) : std::strlen(name));
            if (at > before && at <= seconds)
            {
                if (key == "left") input.nav = Direction::left;
                else if (key == "right") input.nav = Direction::right;
                else if (key == "up") input.nav = Direction::up;
                else if (key == "down") input.nav = Direction::down;
                else if (key == "confirm") input.pressed |= action_bit(Action::confirm);
                else if (key == "back") input.pressed |= action_bit(Action::back);
                else if (key == "menu") input.pressed |= action_bit(Action::menu);
                else if (key == "west") input.pressed |= action_bit(Action::west);
                else if (key == "l1") input.pressed |= action_bit(Action::page_prev);
                else if (key == "r1") input.pressed |= action_bit(Action::page_next);
                // "type=text": into the field the keyboard sheet is open for
                else if (key.rfind("type=", 0) == 0 && kb_open_ && kb_target_) *kb_target_ += key.substr(5);
            }
            p = end ? end + 1 : nullptr;
        }
    }
#endif

    void update(const InputFrame &pad_input, float dt, app::Feedback &feedback) override
    {
        InputFrame input = pad_input;
#if defined(PS5_HOST_REFERENCE)
        host_script(input, dt);
#endif
        if (rezka::App::instance().is_playing || rezka::App::instance().is_buffering)
        {
            update_player(input, dt, feedback);
            return;
        }

        const bool triangle_pressed = input.is_pressed(Action::north) || ((ps5_pad().pressed & PAD_TRIANGLE) != 0);
        if (!search_open_ && sx_screen_ == SxScreen::none && !upd_open_ && triangle_pressed)
        {
            search_open_ = true;
            search_query_.clear();
            key_row_ = 0;
            key_col_ = 0;
            feedback.play(audio::Cue::open);
            return;
        }

        auto &app = rezka::App::instance();
        uint32_t cur_ver = app.catalog_version.load();
        if (cur_ver != last_catalog_version_)
        {
            last_catalog_version_ = cur_ver;
            refresh_catalog_from_app();
        }
        if (app.hero_version.load() != tv_hero_version_ || app.hero_state.load() != tv_hero_state_)
            tv_refresh_hero();
        // The site refused a bookmark: its word, once
        if (app.bookmark_error_version.load() != bookmark_error_version_)
        {
            bookmark_error_version_ = app.bookmark_error_version.load();
            std::string error;
            {
                std::lock_guard<std::mutex> lock(app.catalog_mutex);
                error = app.bookmark_error;
            }
            if (!error.empty())
                show_message(error);
        }
        upd_poll();

        age_ += dt;
        clock_ += dt;
        if (page_open_)
            rebuild_page_model();
        search_tick(dt, feedback);
        // The update's dialog, then the console's keyboard (or the sheet of
        // keys), take the pad while they are up.
        if (upd_open_)
            upd_update(input, feedback);
        else if (kb_active())
            kb_update(input, feedback);
        // The topmost layer takes the input; the layers under it only animate.
        else if (sx_screen_ != SxScreen::none)
            sx_update(input, dt, feedback);
        else if (search_open_)
            update_search(input, dt, feedback);
        else if (settings_open_)
            update_settings(input, dt, feedback);
        else if (profile_open_)
            update_profile(input, feedback);
        else if (person_open_)
            update_person(input, feedback);
        else if (qr_open_)
            update_qr(input, feedback);
        else if (dialog_open_)
            wd_update(input, feedback);
        else if (drawer_open_)
            update_drawer(input, feedback);
        else if (page_open_)
            dp_update(input, feedback);
        else
            update_home(input, feedback);
        animate(dt, feedback);
    }

    void draw(app::Frame &frame) const override
    {
        if (rezka::App::instance().is_playing || rezka::App::instance().is_buffering)
        {
            draw_player(frame);
            return;
        }

        // The app's surface colour, plain, behind everything.
        frame.backdrop.mode = gfx::BackdropMode::gradient;
        frame.backdrop.colors[0] = tv_surface();
        frame.backdrop.colors[1] = tv_surface();
        frame.backdrop.colors[2] = tv_surface();
        frame.backdrop.params[0] = 0.0f;
        frame.backdrop.params[1] = 0.0f;
        frame.backdrop.params[2] = 0.0f;
        frame.backdrop.time = 0.0f;

        const float page = page_.value;
        const float drawer = drawer_.value;
        const float dialog = dialog_.value;

        // Anything opened over the home page pushes it back a little.
        gfx::DrawList &scene = frame.scene;
        scene.push_transform(1.0f - 0.03f * std::max(page, 0.6f * drawer) * travel(), 960, 540, 0,
                             0);
        tv_draw_home(scene);
        scene.pop_transform();
        // The home page has no hint row, as the app has none; the layers over
        // it keep theirs, each arriving once its layer has.
        const auto arriving = [](float value) { return tween::clamp01((value - 0.4f) / 0.6f); };

        gfx::DrawList &overlay = frame.overlay;
        frame.glass = false;
        (void)arriving;
        (void)dialog;
        dp_draw(overlay);
        cm_draw(overlay);
        dp_draw_dialog(overlay);
        draw_person(overlay);
        draw_qr(overlay);
        draw_search(overlay);
        sx_draw(overlay);
        draw_profile(overlay);
        draw_settings(overlay);
        wd_draw(overlay);
        upd_draw(overlay);
        draw_toast(overlay, frame.glass_texture);
    }

    std::span<const app::TourStep> tour() const override
    {
        return kTour;
    }

  private:
    // A cart row. It stays in the list while it leaves, so the rows below it
    // can close the gap smoothly.
    struct Line
    {
        int item = 0;
        tween::Spring open; // 1 at rest; the row's height and opacity
        bool leaving = false;
        bool sold = false;  // leaving because it was bought: slides out too
        float delay = 0.0f; // seconds before it starts to leave
    };

    // Line counts of a title's page copy, measured once.
    struct Copy
    {
        int pitch_lines = 1;
        int blurb_lines = 1;
        float pitch_width = kInfoW;
    };

    // ---- data --------------------------------------------------------------

    const demo::Item &item(int index) const
    {
        // Colours only. Titles, blurbs and covers come from the live catalog.
        // Indexing the demo shelf here crashed once a real page grew past it.
        (void)index;
        static demo::Item plate{
            "",
            "HDRezka",
            "HDRezka",
            "",
            0,
            0.0f,
            0.0f,
            0,
            1,
            Color::rgb(0x140818),
            Color::rgb(0x5a2048),
            Color::rgb(0xff3355),
            0,
        };
        return plate;
    }

    std::string item_title(int index) const
    {
        auto &app = rezka::App::instance();
        std::lock_guard<std::mutex> lock(app.catalog_mutex);
        if (index >= 0 && index < static_cast<int>(app.movies.size()))
            return app.movies[index].title;
        return {};
    }

    std::string item_blurb(int index) const
    {
        auto &app = rezka::App::instance();
        std::lock_guard<std::mutex> lock(app.catalog_mutex);
        if (index >= 0 && index < static_cast<int>(app.movies.size()))
        {
            if (!app.movies[index].blurb.empty())
                return app.movies[index].blurb;
            return app.movies[index].info;
        }
        return {};
    }

    std::string item_meta(int index) const
    {
        auto &app = rezka::App::instance();
        std::lock_guard<std::mutex> lock(app.catalog_mutex);
        if (index >= 0 && index < static_cast<int>(app.movies.size()))
        {
            const auto &m = app.movies[index];
            if (!m.info.empty())
                return m.info;
            if (!m.genre.empty())
                return m.genre;
        }
        return "HDRezka";
    }

    std::uint32_t item_cover(int index) const
    {
        auto &app = rezka::App::instance();
        std::lock_guard<std::mutex> lock(app.catalog_mutex);
        if (index >= 0 && index < static_cast<int>(app.movies.size()))
        {
            if (app.movies[index].texture_ready && app.movies[index].texture_id != 0)
                return app.movies[index].texture_id;
        }
        return 0;
    }

    float item_rating(int index) const
    {
        auto &app = rezka::App::instance();
        std::lock_guard<std::mutex> lock(app.catalog_mutex);
        if (index >= 0 && index < static_cast<int>(app.movies.size()))
            return app.movies[index].rating;
        return 0.0f;
    }

    // The short line that sells a title; the blurb stands in for titles the
    // table above does not know.
    const char *pitch(int index) const
    {
        (void)index;
        return "";
    }
    const Offer &offer(int index) const
    {
        // The shelf is empty until HDRezka answers: never index past it.
        static const Offer kNone{};
        if (index < 0 || index >= static_cast<int>(offers_.size()))
            return kNone;
        return offers_[static_cast<std::size_t>(index)];
    }
    bool owned(int index) const
    {
        if (index < 0 || index >= static_cast<int>(owned_.size()))
            return false;
        return owned_[static_cast<std::size_t>(index)];
    }
    bool calm() const
    {
        return context_.settings.reduced_motion;
    }
    // Spring speed: plain, near-instant fades under "Reduce motion".
    float omega(float normal) const
    {
        return calm() ? 40.0f : normal;
    }
    // Scales every decorative slide: nothing travels under "Reduce motion".
    float travel() const
    {
        return calm() ? 0.0f : 1.0f;
    }
    // The idle pulse of whatever has the focus; a steady light when calm.
    float breathe() const
    {
        return calm() ? 0.5f : ui::breathe(clock_);
    }

    bool matches(int filter, int index) const
    {
        (void)filter;
        (void)index;
        return true;
    }

    int active_chip_count() const
    {
        return home_layout_ ? kHomeChips : kCatalogChips;
    }

    const char *chip_name(int i) const
    {
        if (home_layout_)
            return kHomeChipNames[std::clamp(i, 0, kHomeChips - 1)];
        return kCatalogChipNames[std::clamp(i, 0, kCatalogChips - 1)];
    }

    int grid_columns() const
    {
        return home_layout_ ? kHomeColumns : kColumns;
    }

    void rebuild_visible()
    {
        visible_.clear();
        const int total = static_cast<int>(offers_.size());
        for (int f = 0; f < kFilters; ++f)
            counts_[f] = total;

        if (home_layout_ && !home_sections_.empty())
        {
            for (const auto &sec : home_sections_)
            {
                for (int i = 0; i < sec.count; ++i)
                {
                    const int idx = sec.begin + i;
                    if (idx >= 0 && idx < total)
                        visible_.push_back(idx);
                }
            }
        }
        else
        {
            for (int i = 0; i < total; ++i)
                visible_.push_back(i);
        }
        const int count = static_cast<int>(visible_.size());
        focus_ = std::clamp(focus_, 0, std::max(0, count - 1));
        if (count == 0 && zone_ == Zone::grid)
            zone_ = Zone::chips;
    }

    int live_lines() const
    {
        int count = 0;
        for (const Line &line : lines_)
            count += line.leaving ? 0 : 1;
        return count;
    }
    // Index into lines_ of the nth row that is not leaving, or -1.
    int live_line(int nth) const
    {
        for (int i = 0; i < static_cast<int>(lines_.size()); ++i)
        {
            if (!lines_[static_cast<std::size_t>(i)].leaving && nth-- == 0)
                return i;
        }
        return -1;
    }
    bool in_cart(int index) const
    {
        for (const Line &line : lines_)
        {
            if (!line.leaving && line.item == index)
                return true;
        }
        return false;
    }
    int subtotal() const
    {
        int sum = 0;
        for (const Line &line : lines_)
            sum += line.leaving ? 0 : offer(line.item).price();
        return sum;
    }
    // The subtotal as the drawer shows it: on its way to the real one.
    int subtotal_shown() const
    {
        const float t = tween::cubic_out(subtotal_count_.progress());
        return static_cast<int>(std::lround(
            tween::lerp(static_cast<float>(subtotal_from_), static_cast<float>(subtotal_to_), t)));
    }

    // ---- geometry ----------------------------------------------------------

    static Rect banner_rect()
    {
        // Full-bleed hero like www .home-hero / HDrezkaTV showcase.
        return {0.0f, kBannerY, kWidth, kBannerH};
    }
    static Rect banner_art()
    {
        const Rect b = banner_rect();
        return {b.x + b.w - kBannerArtW, b.y, kBannerArtW, b.h};
    }
    // A card's place on the unscrolled page (index into visible_).
    Rect card_rect(int visible_index) const
    {
        if (home_layout_ && !home_sections_.empty())
        {
            int remaining = visible_index;
            for (int s = 0; s < static_cast<int>(home_sections_.size()); ++s)
            {
                const int n = home_sections_[static_cast<std::size_t>(s)].count;
                if (remaining < n)
                {
                    const int col = remaining % kHomeColumns;
                    const int sub = remaining / kHomeColumns;
                    const float y = kGridY + static_cast<float>(s) * kHomeRowPitch +
                                    kHomeRowTitleH + static_cast<float>(sub) * (kHomeCoverH + 20.0f);
                    return {kMargin + static_cast<float>(col) * (kHomeCardW + kHomeCardGap), y,
                            kHomeCardW, kHomeCardH};
                }
                remaining -= n;
            }
        }
        const int cols = grid_columns();
        const float cw = home_layout_ ? kHomeCardW : kCardW;
        const float gap = home_layout_ ? kHomeCardGap : kCardGap;
        const float ch = home_layout_ ? kHomeCardH : kCardH;
        const float pitch = home_layout_ ? kHomeRowPitch : kRowPitch;
        const int col = visible_index % cols;
        const int row = visible_index / cols;
        return {kMargin + static_cast<float>(col) * (cw + gap),
                kGridY + 36.0f + static_cast<float>(row) * pitch, cw, ch};
    }
    // The chips scroll with the page until they reach the top bar, then stay.
    float chips_y() const
    {
        return std::max(kChipsY - scroll_.value, kStickY);
    }
    // Chip rectangles on screen, left to right; widths follow their labels.
    void chip_layout(Rect out[kFilters]) const
    {
        const ui::Fonts &fonts = context_.fonts;
        const int n = active_chip_count();
        float x = kMargin + ui::button_width(ui::Button::l2, 30) + 16.0f;
        for (int i = 0; i < kFilters; ++i)
        {
            if (i < n)
            {
                const float w = 48.0f + fonts.semibold.measure(chip_name(i), 22);
                out[i] = {x, chips_y(), w, kChipH};
                x += w + 12.0f;
            }
            else
            {
                out[i] = {x, chips_y(), 0.0f, kChipH};
            }
        }
    }
    Rect checkout_rect() const
    {
        return {kWidth - kDrawerW + 44.0f, 904.0f, kDrawerW - 88.0f, 68.0f};
    }
    // Top of a cart row inside the list (before scrolling): the rows above
    // it, each as tall as it is open.
    float line_offset(int index) const
    {
        float y = 0.0f;
        for (int i = 0; i < index && i < static_cast<int>(lines_.size()); ++i)
            y += lines_[static_cast<std::size_t>(i)].open.value * kLineH;
        return y;
    }

    // What the home page's focus highlight surrounds, in page coordinates
    // (the scroll offset is taken off when it is drawn, so the highlight
    // rides with the page instead of chasing it).
    Rect focus_target() const
    {
        if (zone_ == Zone::banner)
            return banner_rect().inset(-7.0f);
        if (zone_ == Zone::chips || visible_.empty())
        {
            Rect chips[kFilters];
            chip_layout(chips);
            Rect chip = chips[filter_].inset(-6.0f);
            chip.y += scroll_.value;
            return chip;
        }
        return card_rect(focus_).inset(-5.0f);
    }
    float focus_radius() const
    {
        if (zone_ == Zone::banner)
            return kBannerRadius + 7.0f;
        if (zone_ == Zone::chips || visible_.empty())
            return kChipH * 0.5f + 6.0f;
        return kCardRadius + 5.0f;
    }

    // Where the page scroll should rest for the current focus.
    float scroll_target() const
    {
        const int cols = grid_columns();
        if (zone_ != Zone::grid || visible_.empty() || focus_ < cols)
            return 0.0f;
        // Any row but the first sends the banner away and parks the chips
        // under the top bar; from there the page moves only as far as needed
        // to keep the focused row whole and clear of the fade at the bottom.
        const float top = card_rect(focus_).y;
        const float card_h = home_layout_ ? kHomeCardH : kCardH;
        const float lowest = std::max(kChipsY - kStickY, top + card_h + kFadeFoot - kViewBottom);
        const float highest = top - kStuckTop;
        return std::clamp(scroll_.target, lowest, std::max(lowest, highest));
    }

    // Wide cinematic crop from the center of the poster (not a stretched full cover).
    Rect banner_uv(int slot, float seconds) const
    {
        (void)slot;
        const float drift = 0.02f * std::sin(seconds * 0.35f);
        const float w = 0.72f;
        const float h = 0.48f;
        const float x = (1.0f - w) * 0.5f + drift;
        const float y = (1.0f - h) * 0.42f;
        return crop(x, y, w, h);
    }

    // Poster nearest the horizontal center of the first grid row.
    int center_poster_index() const
    {
        if (visible_.empty())
        {
            if (featured_.empty())
                return -1;
            return featured_[static_cast<std::size_t>(banner_)];
        }
        const int count = static_cast<int>(visible_.size());
        const int cols = grid_columns();
        const int row0 = std::min(count, cols);
        const int center_col = row0 / 2;
        return visible_[static_cast<std::size_t>(center_col)];
    }

    void apply_top_tab_load()
    {
        auto &app = rezka::App::instance();
        if (top_tab_ <= 0)
        {
            home_layout_ = true;
            filter_ = std::clamp(filter_, 0, kHomeChips - 1);
            app.load_home_async(filter_);
        }
        else
        {
            home_layout_ = false;
            filter_ = std::clamp(filter_, 0, kCatalogChips - 1);
            // encode: (kind+1)*100 + filter  → films=100.., series=200..
            const int kind = std::clamp(top_tab_ - 1, 0, 3);
            app.load_category_async((kind + 1) * 100 + filter_);
        }
    }

    // The three "screenshots" of a title: the cover and two crops of it.
    // Where the product page's cover starts its journey: the card it was
    // opened on, or the banner artwork. False when neither is on screen.
    // ---- input: the home page ----------------------------------------------

    // The edge of something: a quiet "no" (and nothing at all for a held
    // direction, which only means the player has not let go yet).
    void refuse(app::Feedback &feedback, bool repeat, float dx, float dy)
    {
        if (repeat)
            return;
        feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
        feedback.rumble(0.25f, 0.05f);
        nudge_.trigger();
        nudge_x_ = dx;
        nudge_y_ = dy;
    }

    void tick_card(app::Feedback &feedback) const
    {
        const Rect r = card_rect(focus_);
        feedback.play(audio::Cue::focus, 1.0f, ui::pan_for_x(r.cx()));
    }

    void erase_from(std::string &target)
    {
        while (!target.empty() && (static_cast<unsigned char>(target.back()) & 0xc0) == 0x80)
            target.pop_back();
        if (!target.empty())
            target.pop_back();
    }

    static const char **keys_for_lang(int lang)
    {
        if (lang == 1)
            return kKeysRU;
        if (lang == 2)
            return kKeysSYM;
        return kKeysEN;
    }

    static const char *lang_button_label(int lang)
    {
        if (lang == 1)
            return "РУС";
        if (lang == 2)
            return "#!";
        return "ENG";
    }

    void cycle_key_lang()
    {
        key_lang_ = (key_lang_ + 1) % kKeyLayouts;
    }

    void navigate_key_grid(const InputFrame &input, app::Feedback &feedback)
    {
        if (input.nav == Direction::left)
        {
            if (key_row_ == 4)
            {
                if (key_col_ >= 9)
                    key_col_ = 7;
                else if (key_col_ >= 7)
                    key_col_ = 3;
                else if (key_col_ >= 3)
                    key_col_ = 0;
                else
                    key_col_ = 9;
            }
            else
            {
                key_col_ = (key_col_ + kKeyCols - 1) % kKeyCols;
            }
            feedback.play(audio::Cue::focus);
        }
        else if (input.nav == Direction::right)
        {
            if (key_row_ == 4)
            {
                if (key_col_ <= 2)
                    key_col_ = 3;
                else if (key_col_ <= 6)
                    key_col_ = 7;
                else if (key_col_ <= 8)
                    key_col_ = 9;
                else
                    key_col_ = 0;
            }
            else
            {
                key_col_ = (key_col_ + 1) % kKeyCols;
            }
            feedback.play(audio::Cue::focus);
        }
        else if (input.nav == Direction::up)
        {
            key_row_ = (key_row_ + kKeyRows - 1) % kKeyRows;
            feedback.play(audio::Cue::focus);
        }
        else if (input.nav == Direction::down)
        {
            key_row_ = (key_row_ + 1) % kKeyRows;
            feedback.play(audio::Cue::focus);
        }
    }

    void draw_key_action_row(gfx::DrawList &list, float ox, float ay, float cell, float gap, float h,
                             const char *action_label) const
    {
        const ui::Fonts &fonts = context_.fonts;
        const float w0 = 3.0f * cell + 2.0f * gap;
        const Rect btn0{ox, ay, w0, h};
        const bool on0 = (key_row_ == 4 && key_col_ <= 2);
        list.rounded_rect(btn0, 12.0f, on0 ? kAccent : kPanel);
        if (on0)
            list.bordered_rect(btn0, 12.0f, kClear, 2.0f, kWhite);
        ui::text(list, fonts.semibold, lang_button_label(key_lang_), btn0.cx(), centred(btn0.cy(), 20), 20,
                 on0 ? kOnAccent : kInk, gfx::Align::center);

        const float x1 = ox + 3.0f * (cell + gap);
        const float w1 = 4.0f * cell + 3.0f * gap;
        const Rect btn1{x1, ay, w1, h};
        const bool on1 = (key_row_ == 4 && key_col_ >= 3 && key_col_ <= 6);
        list.rounded_rect(btn1, 12.0f, on1 ? kAccent : kPanel);
        if (on1)
            list.bordered_rect(btn1, 12.0f, kClear, 2.0f, kWhite);
        ui::text(list, fonts.semibold, "ПРОБЕЛ", btn1.cx(), centred(btn1.cy(), 20), 20,
                 on1 ? kOnAccent : kInk, gfx::Align::center);

        const float x2 = ox + 7.0f * (cell + gap);
        const float w2 = 2.0f * cell + 1.0f * gap;
        const Rect btn2{x2, ay, w2, h};
        const bool on2 = (key_row_ == 4 && (key_col_ == 7 || key_col_ == 8));
        list.rounded_rect(btn2, 12.0f, on2 ? kAccent : kPanel);
        if (on2)
            list.bordered_rect(btn2, 12.0f, kClear, 2.0f, kWhite);
        ui::text(list, fonts.semibold, "СТЕРЕТЬ", btn2.cx(), centred(btn2.cy(), 20), 20,
                 on2 ? kOnAccent : kInk, gfx::Align::center);

        const float x3 = ox + 9.0f * (cell + gap);
        const float w3 = 3.0f * cell + 2.0f * gap;
        const Rect btn3{x3, ay, w3, h};
        const bool on3 = (key_row_ == 4 && key_col_ >= 9);
        list.rounded_rect(btn3, 12.0f, on3 ? kAccent : kAccent.with_alpha(0.85f));
        if (on3)
        {
            list.glow(btn3, 12.0f, 16.0f, kAccent.with_alpha(0.55f));
            list.bordered_rect(btn3, 12.0f, kClear, 2.0f, kWhite);
        }
        ui::text(list, fonts.semibold, action_label, btn3.cx(), centred(btn3.cy(), 20), 20, kWhite,
                 gfx::Align::center);
    }

    // Actor page, measured like HDrezkaTV's PersonDetails / PersonMovies:
    // a full-screen Material sheet, the portrait beside the name, then the
    // same 2:3 poster grid as the home list. Circle (or Закрыть) returns to
    // the title. Does not replace the home catalogue.
    struct PersonMetrics
    {
        float pad = 0.0f;
        float gap = 0.0f;
        float photo_x = 0.0f;
        float photo_y = 0.0f;
        float photo_w = 0.0f;
        float photo_h = 0.0f;
        float text_x = 0.0f;
        float grid_top = 0.0f;
        float card_w = 0.0f;
        float cover_h = 0.0f;
        float row_pitch = 0.0f;
        float view_bottom = 0.0f;
        int cols = 7;
        Rect close{};
    };

    static const char *person_films_word(int count)
    {
        const int n10 = count % 10;
        const int n100 = count % 100;
        if (n10 == 1 && n100 != 11)
            return "фильм";
        if (n10 >= 2 && n10 <= 4 && (n100 < 12 || n100 > 14))
            return "фильма";
        return "фильмов";
    }

    PersonMetrics person_metrics() const
    {
        PersonMetrics m;
        m.pad = 48.0f * kTvDp;
        m.gap = 12.0f * kTvDp;
        m.cols = 7;
        m.photo_y = 24.0f * kTvDp;
        m.photo_x = m.pad;
        m.photo_w = 112.0f * kTvDp;
        m.photo_h = m.photo_w * 1.5f;
        m.text_x = m.photo_x + m.photo_w + 24.0f * kTvDp;
        m.grid_top = m.photo_y + m.photo_h + 16.0f * kTvDp;
        m.card_w = (kWidth - 2.0f * m.pad - static_cast<float>(m.cols - 1) * m.gap) /
                   static_cast<float>(m.cols);
        m.cover_h = m.card_w * 1.5f;
        const float text_h = 12.0f * kTvDp + 2.0f * (14.0f * kTvDp) + 4.0f * kTvDp + 2.0f * (12.0f * kTvDp);
        m.row_pitch = m.cover_h + text_h + m.gap;
        m.view_bottom = kHeight - 28.0f * kTvDp;
        const float close_w =
            tv_medium().measure("Закрыть", 14.0f * kTvDp, 0.1f * kTvDp) + 32.0f * kTvDp;
        m.close = {kWidth - m.pad - close_w, m.photo_y, close_w, 36.0f * kTvDp};
        return m;
    }

    void open_filmography(const std::string &url, const std::string &name, app::Feedback &feedback)
    {
        if (url.empty())
        {
            show_message("Страница актёра недоступна");
            feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
            return;
        }
        person_open_ = true;
        person_focus_ = 0;
        person_zone_ = 0;
        person_scroll_ = 0.0f;
        person_nav_key_.clear();
        person_return_page_item_ = page_item_;
        rezka::App::instance().person_async(url, name);
        feedback.play(audio::Cue::open);
    }

    void close_person(app::Feedback &feedback)
    {
        person_open_ = false;
        if (person_return_page_item_ >= 0)
        {
            page_open_ = true;
            page_item_ = person_return_page_item_;
            rebuild_page_model();
        }
        feedback.play(audio::Cue::back);
    }

    void update_person(const InputFrame &input, app::Feedback &feedback)
    {
        auto &app = rezka::App::instance();
        std::string nav_key;
        int career_count = 0;
        int film_count = 0;
        const bool loading = app.person_loading.load();
        {
            std::lock_guard<std::mutex> lock(app.catalog_mutex);
            nav_key = app.person.url;
            career_count = static_cast<int>(app.person_career_titles.size());
            film_count = static_cast<int>(app.person_movies.size());
        }
        if (nav_key != person_nav_key_)
        {
            person_nav_key_ = nav_key;
            person_focus_ = 0;
            person_scroll_ = 0.0f;
            person_zone_ = 0;
        }

        if (input.is_pressed(Action::back) ||
            (person_zone_ == 2 && input.is_pressed(Action::confirm)))
        {
            close_person(feedback);
            return;
        }

        auto step_career = [&](int delta) {
            if (career_count <= 1)
                return;
            int cur = 0;
            {
                std::lock_guard<std::mutex> lock(app.catalog_mutex);
                cur = app.person_career_idx;
            }
            app.person_select_career((cur + delta + career_count) % career_count);
            person_focus_ = 0;
            person_scroll_ = 0.0f;
            person_zone_ = 1;
            feedback.play(audio::Cue::tab);
        };
        if (input.is_pressed(Action::page_prev) || input.is_pressed(Action::jump_prev))
        {
            step_career(-1);
            return;
        }
        if (input.is_pressed(Action::page_next) || input.is_pressed(Action::jump_next))
        {
            step_career(1);
            return;
        }

        const PersonMetrics m = person_metrics();
        const bool chips = career_count > 1;
        const int cols = m.cols;

        if (person_zone_ == 2)
        {
            if (input.nav == Direction::down)
            {
                person_zone_ = chips ? 1 : (film_count > 0 ? 0 : 2);
                if (person_zone_ != 2)
                    feedback.play(audio::Cue::focus);
            }
            return;
        }

        if (person_zone_ == 1)
        {
            if (input.nav == Direction::left || input.nav == Direction::right)
                step_career(input.nav == Direction::right ? 1 : -1);
            else if (input.nav == Direction::up)
            {
                person_zone_ = 2;
                feedback.play(audio::Cue::focus);
            }
            else if (input.nav == Direction::down && film_count > 0)
            {
                person_zone_ = 0;
                person_focus_ = std::clamp(person_focus_, 0, film_count - 1);
                feedback.play(audio::Cue::focus);
            }
            return;
        }

        if (film_count <= 0)
        {
            if (input.nav == Direction::up)
            {
                person_zone_ = chips ? 1 : 2;
                feedback.play(audio::Cue::focus);
            }
            return;
        }

        if (person_focus_ >= film_count)
            person_focus_ = film_count - 1;
        const int rows = (film_count + cols - 1) / cols;
        int row = person_focus_ / cols;
        const int col = person_focus_ % cols;
        int next = person_focus_;
        if (input.nav == Direction::left)
            next = col == 0 ? person_focus_ : person_focus_ - 1;
        else if (input.nav == Direction::right)
            next = (col == cols - 1 || person_focus_ + 1 >= film_count) ? person_focus_ : person_focus_ + 1;
        else if (input.nav == Direction::up)
        {
            if (row == 0)
            {
                person_zone_ = chips ? 1 : 2;
                feedback.play(audio::Cue::focus);
                return;
            }
            next = person_focus_ - cols;
        }
        else if (input.nav == Direction::down)
        {
            next = person_focus_ + cols;
            if (next >= film_count && next / cols == (film_count - 1) / cols)
                next = film_count - 1;
            else if (next >= film_count)
                next = person_focus_;
        }
        if (input.nav != Direction::none && next != person_focus_)
        {
            person_focus_ = next;
            row = person_focus_ / cols;
            feedback.play(audio::Cue::focus);
        }

        const float view_h = m.view_bottom - m.grid_top;
        const float y0 = static_cast<float>(row) * m.row_pitch;
        const float card_bottom = y0 + m.row_pitch - m.gap;
        if (y0 < person_scroll_)
            person_scroll_ = y0;
        if (card_bottom - person_scroll_ > view_h)
            person_scroll_ = card_bottom - view_h;
        const float content = static_cast<float>(rows) * m.row_pitch - m.gap;
        const float max_scroll = std::max(0.0f, content - view_h);
        person_scroll_ = std::clamp(person_scroll_, 0.0f, max_scroll);

        if (input.is_pressed(Action::confirm))
        {
            const int idx = app.adopt_person_film(person_focus_);
            if (idx < 0)
            {
                feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
                return;
            }
            person_open_ = false;
            open_page(idx, false, feedback);
            rezka::App::instance().load_details_async(idx);
            return;
        }

        const bool short_page = content <= view_h + 1.0f;
        const bool near_end = short_page || row >= rows - 2;
        if (near_end && !loading && app.person_has_next.load() && !app.person_more_loading.load())
            app.person_load_more_async();
    }

    void draw_person(gfx::DrawList &list) const
    {
        if (!person_open_)
            return;
        auto &app = rezka::App::instance();
        rezka::PersonDetails person;
        std::vector<rezka::MovieData> films;
        std::vector<std::string> careers;
        int career_idx = 0;
        std::string status;
        const bool loading = app.person_loading.load();
        const bool more = app.person_more_loading.load();
        {
            std::lock_guard<std::mutex> lock(app.catalog_mutex);
            person = app.person;
            films = app.person_movies;
            careers = app.person_career_titles;
            career_idx = app.person_career_idx;
            status = app.person_status;
        }

        const PersonMetrics m = person_metrics();
        const ui::Fonts &fonts = context_.fonts;
        list.rounded_rect({0, 0, kWidth, kHeight}, 0.0f, tv_surface());

        const bool close_on = person_zone_ == 2;
        list.rounded_rect(m.close, m.close.h * 0.5f,
                          close_on ? tv_on_surface() : tv_secondary_container());
        tv_text(list, tv_medium(), "Закрыть", m.close.cx(), m.close.y + 8.0f * kTvDp, 14.0f * kTvDp,
                20.0f * kTvDp, close_on ? tv_inverse_on_surface() : tv_on_secondary_container(),
                gfx::Align::center, 0.1f * kTvDp);

        const Rect photo{m.photo_x, m.photo_y, m.photo_w, m.photo_h};
        const std::uint32_t photo_tex =
            app.photo_texture(person.photo_hq.empty() ? person.photo : person.photo_hq);
        const float photo_r = 8.0f * kTvDp;
        if (photo_tex != 0)
            list.image(photo_tex, photo, cover_uv(photo), kWhite, photo_r);
        else
            list.rounded_rect(photo, photo_r, tv_surface_variant());
        list.bordered_rect(photo, photo_r, kClear, 2.0f * kTvDp, tv_border().with_alpha(0.65f));

        const float text_w = m.close.x - m.text_x - 16.0f * kTvDp;
        float ty = m.photo_y + 4.0f * kTvDp;
        const std::string name = person.name.empty() ? "Фильмография" : person.name;
        tv_text(list, tv_medium(), fonts.hand.font->fit(name, 28.0f * kTvDp, text_w), m.text_x, ty,
                28.0f * kTvDp, 36.0f * kTvDp, tv_on_surface());
        ty += 40.0f * kTvDp;
        if (!person.name_orig.empty())
        {
            tv_text(list, tv_regular(), fonts.pixel.font->fit(person.name_orig, 16.0f * kTvDp, text_w),
                    m.text_x, ty, 16.0f * kTvDp, 24.0f * kTvDp, tv_on_surface().with_alpha(0.7f));
            ty += 28.0f * kTvDp;
        }
        int expected = 0;
        std::string role;
        if (career_idx >= 0 && career_idx < static_cast<int>(person.careers.size()))
        {
            expected = person.careers[static_cast<std::size_t>(career_idx)].expected;
            role = person.careers[static_cast<std::size_t>(career_idx)].title;
        }
        const int shown = static_cast<int>(films.size());
        std::string count = role.empty() ? "" : role + "  ·  ";
        if (expected > shown)
            count += std::to_string(shown) + " из " + std::to_string(expected) + " " +
                     person_films_word(expected);
        else
            count += std::to_string(shown) + " " + person_films_word(shown);
        if (more)
            count += "  ·  загрузка";
        tv_text(list, tv_regular(), count, m.text_x, ty, 14.0f * kTvDp, 20.0f * kTvDp,
                tv_on_surface_variant());
        ty += 28.0f * kTvDp;
        if (careers.size() <= 1 && !person.date_of_birth.empty())
            tv_text(list, tv_regular(),
                    fonts.pixel.font->fit("Дата рождения: " + person.date_of_birth, 14.0f * kTvDp, text_w),
                    m.text_x, ty, 14.0f * kTvDp, 20.0f * kTvDp, tv_on_surface().with_alpha(0.8f));

        if (careers.size() > 1)
        {
            float chip_x = m.text_x;
            const float chip_y = m.photo_y + m.photo_h - 36.0f * kTvDp;
            for (int i = 0; i < static_cast<int>(careers.size()); ++i)
            {
                const float w =
                    tv_medium().measure(careers[static_cast<std::size_t>(i)], 14.0f * kTvDp, 0.1f * kTvDp) +
                    24.0f * kTvDp;
                if (chip_x + w > kWidth - m.pad)
                    break;
                const Rect chip{chip_x, chip_y, w, 36.0f * kTvDp};
                const bool on = i == career_idx;
                const bool focus = person_zone_ == 1 && on;
                list.rounded_rect(chip, chip.h * 0.5f,
                                  focus ? tv_on_surface()
                                        : (on ? tv_secondary_container()
                                              : tv_surface_variant().with_alpha(0.45f)));
                const Color ink = focus ? tv_inverse_on_surface()
                                        : (on ? tv_on_secondary_container()
                                              : tv_on_surface().with_alpha(0.8f));
                tv_text(list, tv_medium(), careers[static_cast<std::size_t>(i)], chip.cx(),
                        chip.y + 8.0f * kTvDp, 14.0f * kTvDp, 20.0f * kTvDp, ink, gfx::Align::center,
                        0.1f * kTvDp);
                chip_x += w + 8.0f * kTvDp;
            }
        }

        if (loading && films.empty())
        {
            tv_text(list, tv_medium(), "Загрузка фильмографии...", kWidth * 0.5f, m.grid_top + 48.0f * kTvDp,
                    18.0f * kTvDp, 28.0f * kTvDp, tv_on_surface(), gfx::Align::center);
        }
        else if (films.empty())
        {
            tv_text(list, tv_medium(), status.empty() ? "Фильмы не найдены" : status, kWidth * 0.5f,
                    m.grid_top + 48.0f * kTvDp, 18.0f * kTvDp, 28.0f * kTvDp, tv_on_surface(),
                    gfx::Align::center);
        }
        else
        {
            const float radius = 8.0f * kTvDp;
            const float view_h = m.view_bottom - m.grid_top;
            const int count = static_cast<int>(films.size());
            const int first_row = std::max(0, static_cast<int>((person_scroll_ - m.gap) / m.row_pitch) - 1);
            const int last_row = static_cast<int>((person_scroll_ + view_h) / m.row_pitch) + 1;
            const int first = first_row * m.cols;
            const int last = std::min(count, (last_row + 1) * m.cols);
            const int focused = person_zone_ == 0 ? person_focus_ : -1;
            list.push_clip({0.0f, m.grid_top, kWidth, view_h});
            for (int pass = 0; pass < 2; ++pass)
            {
                for (int i = first; i < last; ++i)
                {
                    if ((i == focused) != (pass == 1))
                        continue;
                    const int col = i % m.cols;
                    const int row = i / m.cols;
                    const float x = m.pad + static_cast<float>(col) * (m.card_w + m.gap);
                    const float y = m.grid_top + static_cast<float>(row) * m.row_pitch - person_scroll_;
                    const auto &movie = films[static_cast<std::size_t>(i)];
                    const Rect cover{x, y, m.card_w, m.cover_h};
                    if (movie.texture_id != 0)
                        list.image(movie.texture_id, cover, cover_uv(cover), kWhite, radius);
                    else
                        list.rounded_rect(cover, radius, tv_surface_variant());
                    if (i == focused)
                        list.bordered_rect(cover, radius, kClear, 3.0f * kTvDp, tv_on_surface());

                    std::string title_lines[2];
                    tv_two_lines(*tv_medium().font, movie.title, 14.0f * kTvDp, m.card_w, title_lines);
                    float ty_card = cover.y + m.cover_h + 8.0f * kTvDp;
                    for (const std::string &line : title_lines)
                    {
                        if (!line.empty())
                            tv_text(list, tv_medium(), line, x, ty_card, 14.0f * kTvDp, 14.0f * kTvDp,
                                    tv_on_surface());
                        ty_card += 14.0f * kTvDp;
                    }
                    ty_card += 4.0f * kTvDp;
                    std::string sub = movie.info;
                    if (sub.empty() && movie.year > 0)
                        sub = std::to_string(movie.year);
                    std::string part = movie.cover_info;
                    if (part == sub)
                        part.clear();
                    if (!sub.empty())
                        tv_text(list, tv_regular(), tv_regular().font->fit(sub, 12.0f * kTvDp, m.card_w), x,
                                ty_card, 12.0f * kTvDp, 12.0f * kTvDp, tv_on_surface().with_alpha(0.6f));
                    ty_card += 12.0f * kTvDp;
                    if (!part.empty())
                        tv_text(list, tv_regular(), tv_regular().font->fit(part, 12.0f * kTvDp, m.card_w), x,
                                ty_card, 12.0f * kTvDp, 12.0f * kTvDp, tv_on_surface().with_alpha(0.6f));
                }
            }
            if (more)
                tv_text(list, tv_regular(), "Загрузка…", kWidth * 0.5f, m.grid_top + view_h - 22.0f * kTvDp,
                        12.0f * kTvDp, 18.0f * kTvDp, tv_on_surface_variant(), gfx::Align::center);
            list.pop_clip();
        }

        tv_text(list, tv_regular(), "○ Закрыть     ✕ Открыть     L1/R1 амплуа", kWidth * 0.5f,
                kHeight - 22.0f * kTvDp, 12.0f * kTvDp, 18.0f * kTvDp, tv_on_surface().with_alpha(0.55f),
                gfx::Align::center);
    }

    // The screens over the search: Расширенный поиск (O8) and the countries (N8).
    enum class SxScreen : std::uint8_t
    {
        none,
        extended,
        country,
    };
    // Where Circle takes a list of results.
    enum class SearchBack : std::uint8_t
    {
        home,
        search,
        extended,
        country,
    };

    // The grid takes a list of results, headed by its title; Circle goes back
    // to the screen it came from (the app's back stack: search, Расширенный
    // поиск, the countries).
    void tv_show_results(const std::string &title, SearchBack back)
    {
        search_open_ = false;
        sx_screen_ = SxScreen::none;
        page_open_ = false;
        zone_ = Zone::grid;
        focus_ = 0;
        scroll_.snap(0.0f);
        tv_zone_ = TvZone::grid;
        tv_focus_ = 0;
        tv_tab_delay_ = -1.0f;
        tv_scroll_.snap(0.0f);
        tv_cards_.clear();
        tv_list_title_ = title;
        tv_results_back_ = back;
        home_layout_ = false;
    }

    void submit_search(app::Feedback &feedback)
    {
        if (search_query_.empty())
        {
            feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
            return;
        }
        tv_show_results(search_query_, SearchBack::search);
        {
            auto &app = rezka::App::instance();
            std::lock_guard<std::mutex> lock(app.catalog_mutex);
            app.catalog_status = "Поиск: " + search_query_;
        }
        rezka::App::instance().add_search_history(search_query_);
        rezka::App::instance().search_async(search_query_);
        feedback.play(audio::Cue::select);
    }

    // ---- the search: HDrezkaTV's search screen -------------------------------
    //
    // The field on top with the keyboard, microphone and clear buttons; under
    // it the history of queries ("История поиска", "Очистить все") while the
    // field is empty, and the live suggestions of the site with "Показать все
    // результаты" once something is typed. A field press opens the console's
    // keyboard (kb_begin; a PC gets the sheet of keys). The microphone records
    // for the speech-to-text endpoint when one is set (App::voice_start) and
    // otherwise opens the keyboard titled "Голосовой поиск", whose dictation
    // takes the pad's microphone.

    // The field row, left to right as the app has it (L8.a.b): the microphone,
    // the field, Расширенный поиск and the countries; the list under it.
    enum SearchZone : int
    {
        kSearchMic,
        kSearchField,
        kSearchExtended,
        kSearchCountry,
        kSearchList,
    };
    enum SearchRowKind : int
    {
        kRowHistory,
        kRowClearAll,
        kRowSuggest,
        kRowShowAll,
    };
    struct SearchRow
    {
        int kind = kRowHistory;
        std::string text;
        std::string sub;
        float rating = 0.0f;
        int index = 0; // into the history or the suggestions
    };

    static constexpr float kVoiceSeconds = 8.0f;

    std::vector<SearchRow> search_rows(const std::vector<std::string> &history,
                                       const std::vector<rezka::CatalogItem> &suggestions) const
    {
        std::vector<SearchRow> rows;
        if (search_query_.empty())
        {
            for (std::size_t i = 0; i < history.size(); ++i)
                rows.push_back({kRowHistory, history[i], "", 0.0f, static_cast<int>(i)});
            if (!history.empty())
                rows.push_back({kRowClearAll, "Очистить все", "", 0.0f, 0});
            return rows;
        }
        for (std::size_t i = 0; i < suggestions.size(); ++i)
        {
            const auto &s = suggestions[i];
            std::string sub = s.entity;
            if (!s.info.empty())
                sub += sub.empty() ? s.info : " · " + s.info;
            rows.push_back({kRowSuggest, s.title, sub, s.rating, static_cast<int>(i)});
        }
        rows.push_back({kRowShowAll, "Показать все результаты", "", 0.0f, 0});
        return rows;
    }

    void search_text_entry(const char *title, app::Feedback &feedback)
    {
        search_kb_pending_ = true;
        search_kb_before_ = search_query_;
        kb_begin(&search_query_, title, false, feedback);
    }

    void search_voice(app::Feedback &feedback)
    {
        auto &app = rezka::App::instance();
        if (app.voice_start())
        {
            search_voice_timer_ = 0.0f;
            feedback.play(audio::Cue::modal_open);
            return;
        }
        show_message(app.voice_available() ? "Микрофон недоступен — открываю диктовку клавиатуры"
                                           : "Распознавание речи не настроено — открываю диктовку клавиатуры");
        search_text_entry("Голосовой поиск", feedback);
    }

    // Edges and timers that run whatever layer has the pad.
    void search_tick(float dt, app::Feedback &feedback)
    {
        auto &app = rezka::App::instance();
        if (search_kb_pending_ && !kb_active())
        {
            search_kb_pending_ = false;
            // What was typed or dictated is searched, as the app does on the IME's "done"
            if (search_open_ && !search_query_.empty() && search_query_ != search_kb_before_)
                submit_search(feedback);
        }
        if (search_open_ != search_was_open_)
        {
            search_was_open_ = search_open_;
            if (search_open_)
            {
                search_zone_ = kSearchField;
                search_row_ = 0;
                search_suggested_ = search_query_;
                search_debounce_ = 0.0f;
                app.suggest_async(search_query_);
            }
            else
                app.voice_cancel();
        }
        if (search_open_ && search_query_ != search_suggested_)
        {
            search_debounce_ += dt;
            if (search_debounce_ > 0.3f)
            {
                search_suggested_ = search_query_;
                search_debounce_ = 0.0f;
                search_row_ = 0;
                app.suggest_async(search_query_);
            }
        }
    }

    // The microphone's turns: recording, recognising, and what comes of it.
    // true while the voice layer has the pad.
    bool update_voice(const InputFrame &input, float dt, app::Feedback &feedback)
    {
        auto &app = rezka::App::instance();
        switch (app.voice_state.load())
        {
        case 1:
            search_voice_timer_ += dt;
            if (input.is_pressed(Action::back))
            {
                app.voice_cancel();
                feedback.play(audio::Cue::modal_close);
            }
            else if (input.is_pressed(Action::confirm) || search_voice_timer_ > kVoiceSeconds)
            {
                app.voice_finish_async();
                feedback.play(audio::Cue::select);
            }
            return true;
        case 2:
            return true;
        case 3:
            search_query_ = app.take_voice_text();
            submit_search(feedback);
            return true;
        case 4:
            app.take_voice_text();
            show_message("Не удалось распознать речь — открываю диктовку клавиатуры");
            search_text_entry("Голосовой поиск", feedback);
            return true;
        default:
            return false;
        }
    }

    void update_search(const InputFrame &input, float dt, app::Feedback &feedback)
    {
        auto &app = rezka::App::instance();
        if (update_voice(input, dt, feedback))
            return;
        std::vector<rezka::CatalogItem> suggestions;
        {
            std::lock_guard<std::mutex> lock(app.catalog_mutex);
            suggestions = app.search_suggestions;
        }
        const std::vector<SearchRow> rows = search_rows(app.get_search_history(), suggestions);
        const int count = static_cast<int>(rows.size());
        search_row_ = std::clamp(search_row_, 0, std::max(0, count - 1));

        if (input.is_pressed(Action::back))
        {
            search_open_ = false;
            feedback.play(audio::Cue::back);
            return;
        }
        if (input.is_pressed(Action::west) && !search_query_.empty())
        {
            erase_from(search_query_);
            feedback.play(audio::Cue::toggle);
            return;
        }
        if (input.is_pressed(Action::north) || input.is_pressed(Action::menu) ||
            ((ps5_pad().pressed & PAD_TRIANGLE) != 0))
        {
            submit_search(feedback);
            return;
        }

        if (search_zone_ != kSearchList)
        {
            if (input.nav == Direction::left || input.nav == Direction::right)
            {
                const int next = search_zone_ + (input.nav == Direction::right ? 1 : -1);
                if (next < kSearchMic || next > kSearchCountry)
                    tv_refuse(feedback, input.nav_repeat);
                else
                {
                    search_zone_ = next;
                    feedback.play(audio::Cue::focus);
                }
                return;
            }
            if (input.nav == Direction::down)
            {
                if (count == 0)
                    tv_refuse(feedback, input.nav_repeat);
                else
                {
                    search_zone_ = kSearchList;
                    search_row_ = 0;
                    search_row_del_ = false;
                    feedback.play(audio::Cue::focus);
                }
                return;
            }
            if (input.nav == Direction::up)
            {
                tv_refuse(feedback, input.nav_repeat);
                return;
            }
            if (!input.is_pressed(Action::confirm))
                return;
            switch (search_zone_)
            {
            case kSearchMic:
                search_voice(feedback);
                break;
            case kSearchExtended:
                sx_open(SxScreen::extended, feedback);
                break;
            case kSearchCountry:
                sx_open(SxScreen::country, feedback);
                break;
            default:
                search_text_entry("Поиск фильмов и сериалов", feedback);
                break;
            }
            return;
        }

        const SearchRow &here = rows[static_cast<std::size_t>(search_row_)];
        if (input.nav == Direction::left || input.nav == Direction::right)
        {
            // A history row has its delete button at its right end (M8.b.f)
            const bool to_delete = input.nav == Direction::right;
            if (here.kind != kRowHistory || to_delete == search_row_del_)
                tv_refuse(feedback, input.nav_repeat);
            else
            {
                search_row_del_ = to_delete;
                feedback.play(audio::Cue::focus);
            }
            return;
        }
        if (input.nav == Direction::up || input.nav == Direction::down)
        {
            if (input.nav == Direction::up && search_row_ == 0)
            {
                search_zone_ = kSearchField;
                search_row_del_ = false;
                feedback.play(audio::Cue::focus);
                return;
            }
            const int next = std::clamp(search_row_ + (input.nav == Direction::down ? 1 : -1), 0, count - 1);
            if (next == search_row_)
                tv_refuse(feedback, input.nav_repeat);
            else
            {
                search_row_ = next;
                if (rows[static_cast<std::size_t>(next)].kind != kRowHistory)
                    search_row_del_ = false;
                feedback.play(audio::Cue::focus);
            }
            return;
        }
        if (count == 0 || !input.is_pressed(Action::confirm))
            return;
        const SearchRow &row = here;
        switch (row.kind)
        {
        case kRowHistory:
            if (search_row_del_)
            {
                // The last one gone, the focus goes back to the field, as in the app (C8.C0140q)
                const bool last = app.get_search_history().size() <= 1;
                app.remove_search_history(row.text);
                if (last)
                {
                    search_zone_ = kSearchField;
                    search_row_del_ = false;
                }
                feedback.play(audio::Cue::toggle);
                break;
            }
            search_query_ = row.text;
            submit_search(feedback);
            break;
        case kRowClearAll:
            app.clear_search_history();
            search_zone_ = kSearchField;
            feedback.play(audio::Cue::toggle);
            break;
        case kRowShowAll:
            submit_search(feedback);
            break;
        default:
        {
            const rezka::CatalogItem &item = suggestions[static_cast<std::size_t>(row.index)];
            const int index = app.adopt_catalog_item(item);
            if (index < 0)
            {
                tv_refuse(feedback, false);
                break;
            }
            // The app keeps the suggestion's title in the history (L8.h)
            app.add_search_history(item.title);
            search_open_ = false;
            dp_stack_.clear();
            open_page(index, false, feedback);
            break;
        }
        }
    }

    // A button of the field row: the app's OutlinedIconButton, 48 dp, its icon
    // 24 dp; filled under the focus. The microphone's rings pulse while it
    // listens (M8.b.a: two circles of the primary colour at a quarter, scaled
    // 1.5..1.7 and 1.5..1.3).
    void draw_search_button(gfx::DrawList &list, float cx, float cy, bool focused, int what) const
    {
        const float dp = kTvDp;
        const float r = 24.0f * dp;
        if (what == kSearchMic)
        {
            const int voice = rezka::App::instance().voice_state.load();
            if (voice == 1 || voice == 2)
            {
                const float s1 = 1.6f + 0.1f * std::sin(clock_ * 6.28f);
                const float s2 = 1.4f + 0.1f * std::sin(clock_ * 3.14f);
                list.circle(cx, cy, r * s1, tv_primary().with_alpha(0.25f));
                list.circle(cx, cy, r * s2, tv_primary().with_alpha(0.25f));
            }
        }
        if (focused)
            list.circle(cx, cy, r, tv_on_surface());
        else
            list.ring(cx, cy, r, 1.0f * dp, tv_border());
        const Color ink = focused ? tv_inverse_on_surface() : tv_on_surface();
        if (what == kSearchMic)
            draw_mic_icon(list, cx, cy, 0.85f, ink);
        else if (what == kSearchCountry)
            tv_icon(list, kTvIconLanguage, cx - 12.0f * dp, cy - 12.0f * dp, 24.0f * dp, ink);
        else
            draw_manage_search_icon(list, cx, cy, ink);
    }

    // Material's ManageSearch: three lines on the left, a lens on the right.
    void draw_manage_search_icon(gfx::DrawList &list, float cx, float cy, Color ink) const
    {
        const float u = kTvDp;
        const float x0 = cx - 10.0f * u;
        list.rounded_rect({x0, cy - 6.0f * u, 7.0f * u, 2.0f * u}, 1.0f * u, ink);
        list.rounded_rect({x0, cy - 1.0f * u, 7.0f * u, 2.0f * u}, 1.0f * u, ink);
        list.rounded_rect({x0, cy + 4.0f * u, 9.0f * u, 2.0f * u}, 1.0f * u, ink);
        list.ring(cx + 2.5f * u, cy - 1.5f * u, 5.0f * u, 2.0f * u, ink);
        list.line(cx + 6.0f * u, cy + 2.0f * u, cx + 10.0f * u, cy + 6.0f * u, 2.0f * u, ink);
    }

    // The microphone, drawn: a capsule, the cup round it, a stem and a foot.
    void draw_mic_icon(gfx::DrawList &list, float cx, float cy, float scale, Color ink) const
    {
        const float u = kTvDp * scale;
        list.rounded_rect({cx - 4.5f * u, cy - 11.0f * u, 9.0f * u, 14.0f * u}, 4.5f * u, ink);
        list.bordered_rect({cx - 8.0f * u, cy - 5.0f * u, 16.0f * u, 14.0f * u}, 8.0f * u, kClear, 1.6f * u, ink);
        list.rounded_rect({cx - 0.8f * u, cy + 9.0f * u, 1.6f * u, 3.0f * u}, 0.0f, ink);
        list.rounded_rect({cx - 4.0f * u, cy + 11.5f * u, 8.0f * u, 1.6f * u}, 0.8f * u, ink);
    }

    void draw_search(gfx::DrawList &list) const
    {
        if (!search_open_)
            return;
        const float dp = kTvDp;
        auto &app = rezka::App::instance();
        std::vector<rezka::CatalogItem> suggestions;
        bool loading = false;
        {
            std::lock_guard<std::mutex> lock(app.catalog_mutex);
            suggestions = app.search_suggestions;
        }
        loading = app.suggest_loading.load();
        const std::vector<std::string> history = app.get_search_history();
        const std::vector<SearchRow> rows = search_rows(history, suggestions);
        list.rounded_rect({0, 0, kWidth, kHeight}, 0.0f, tv_surface());

        // The field row (L8.a.b): 24 dp from the top, 48 dp in from the sides,
        // 16 dp apart: the microphone, the field, Расширенный поиск, the countries.
        const float left = 48.0f * dp;
        const float gap16 = 16.0f * dp;
        const float button = 48.0f * dp;
        const bool has_query = !search_query_.empty();
        const float field_x = left + button + gap16;
        const float field_w = kWidth - 2.0f * left - 3.0f * (button + gap16);
        const Rect field{field_x, 24.0f * dp, field_w, 56.0f * dp};
        draw_search_button(list, left + button * 0.5f, field.cy(), search_zone_ == kSearchMic, kSearchMic);
        draw_search_button(list, field.x + field.w + gap16 + button * 0.5f, field.cy(),
                           search_zone_ == kSearchExtended, kSearchExtended);
        draw_search_button(list, field.x + field.w + 2.0f * gap16 + button * 1.5f, field.cy(),
                           search_zone_ == kSearchCountry, kSearchCountry);
        const bool field_on = search_zone_ == kSearchField;
        list.rounded_rect(field, 28.0f * dp, tv_surface_variant().with_alpha(0.3f));
        list.bordered_rect(field, 28.0f * dp, kClear, (field_on ? 2.0f : 1.0f) * dp,
                           field_on ? tv_primary() : tv_border_variant());
        tv_icon(list, kTvIconSearch, field.x + 20.0f * dp, field.cy() - 12.0f * dp, 24.0f * dp,
                tv_on_surface_variant());
        const float text_x = field.x + 60.0f * dp;
        if (has_query)
        {
            const std::string shown = tv_regular().font->fit(search_query_, 18.0f * dp, field.w - 100.0f * dp);
            const float w = tv_text(list, tv_regular(), shown, text_x, field.cy() - 14.0f * dp, 18.0f * dp,
                                    28.0f * dp, tv_on_surface());
            if (field_on && std::fmod(clock_, 1.0f) < 0.5f)
                list.rounded_rect({text_x + w + 2.0f, field.cy() - 12.0f * dp, 2.0f, 24.0f * dp}, 0.0f, tv_primary());
        }
        else
            tv_text(list, tv_regular(), "Поиск фильмов и сериалов", text_x, field.cy() - 14.0f * dp, 18.0f * dp,
                    28.0f * dp, tv_on_surface_variant());
        // The rows
        const float list_w = 560.0f * dp;
        float top = 104.0f * dp;
        if (!has_query)
        {
            if (history.empty())
            {
                tv_text(list, tv_regular(), "Введите название фильма или сериала в строку поиска", left, top,
                        16.0f * dp, 24.0f * dp, tv_on_surface_variant());
                draw_profile_ill(list, kIllVoid, {kWidth * 0.5f - 160.0f * dp, 200.0f * dp, 320.0f * dp, 220.0f * dp});
            }
            else
            {
                tv_text(list, tv_medium(), "История поиска", left, top, 14.0f * dp, 20.0f * dp,
                        tv_on_surface_variant(), gfx::Align::left, 0.1f * dp);
                top += 28.0f * dp;
            }
        }
        else if (suggestions.empty())
        {
            tv_text(list, tv_regular(), loading ? "Поиск…" : "Ничего не найдено", left, top, 16.0f * dp, 24.0f * dp,
                    tv_on_surface_variant());
            top += 32.0f * dp;
            if (!loading)
            {
                tv_text(list, tv_regular(), "Попробуйте изменить поисковой запрос", left, top - 4.0f * dp,
                        14.0f * dp, 20.0f * dp, tv_on_surface_variant().with_alpha(0.7f));
                top += 28.0f * dp;
            }
        }
        const float row_h = 56.0f * dp;
        const float gap = 8.0f * dp;
        const int visible = std::max(1, static_cast<int>((kHeight - top - 24.0f * dp) / (row_h + gap)));
        const int count = static_cast<int>(rows.size());
        const int first = std::clamp(search_row_ - visible / 2, 0, std::max(0, count - visible));
        for (int i = first; i < count && i < first + visible; ++i)
        {
            const SearchRow &row = rows[static_cast<std::size_t>(i)];
            Rect r{left, top + static_cast<float>(i - first) * (row_h + gap), list_w, row_h};
            const bool row_focus = search_zone_ == kSearchList && i == search_row_;
            if (row.kind == kRowHistory)
            {
                // The row, and its delete button 12 dp after it (M8.b.f)
                r.w -= row_h + 12.0f * dp;
                const bool del_on = row_focus && search_row_del_;
                const float cx = r.x + r.w + 12.0f * dp + row_h * 0.5f;
                if (del_on)
                    list.circle(cx, r.cy(), 24.0f * dp, tv_on_surface());
                tv_text(list, tv_medium(), "×", cx, r.cy() - 16.0f * dp, 28.0f * dp, 32.0f * dp,
                        del_on ? tv_inverse_on_surface() : tv_on_surface_variant(), gfx::Align::center);
            }
            const bool on = row_focus && !(row.kind == kRowHistory && search_row_del_);
            list.rounded_rect(r, 16.0f * dp, on ? tv_secondary_container() : tv_surface_variant().with_alpha(0.3f));
            if (on)
                list.bordered_rect(r, 16.0f * dp, kClear, 2.0f * dp, tv_primary());
            const Color ink = on ? tv_on_secondary_container() : tv_on_surface();
            switch (row.kind)
            {
            case kRowHistory:
                tv_icon(list, kTvIconAccessTime, r.x + 20.0f * dp, r.cy() - 12.0f * dp, 24.0f * dp,
                        tv_on_surface_variant());
                tv_text(list, tv_regular(), tv_regular().font->fit(row.text, 16.0f * dp, r.w - 90.0f * dp),
                        r.x + 60.0f * dp, r.cy() - 12.0f * dp, 16.0f * dp, 24.0f * dp, ink);
                break;
            case kRowClearAll:
                tv_text(list, tv_medium(), row.text, r.x + 60.0f * dp, r.cy() - 12.0f * dp, 16.0f * dp, 24.0f * dp,
                        tv_error(), gfx::Align::left, 0.1f * dp);
                break;
            case kRowShowAll:
                tv_icon(list, kTvIconSearch, r.x + 20.0f * dp, r.cy() - 12.0f * dp, 24.0f * dp, tv_primary());
                tv_text(list, tv_medium(), row.text, r.x + 60.0f * dp, r.cy() - 12.0f * dp, 16.0f * dp, 24.0f * dp,
                        tv_primary(), gfx::Align::left, 0.1f * dp);
                break;
            default:
                tv_text(list, tv_medium(), tv_medium().font->fit(row.text, 16.0f * dp, r.w - 150.0f * dp),
                        r.x + 20.0f * dp, r.y + 6.0f * dp, 16.0f * dp, 24.0f * dp, ink);
                tv_text(list, tv_regular(), row.sub, r.x + 20.0f * dp, r.y + 32.0f * dp, 12.0f * dp, 18.0f * dp,
                        tv_on_surface_variant());
                if (row.rating > 0.0f)
                {
                    char rating[16];
                    std::snprintf(rating, sizeof(rating), "%.1f", row.rating);
                    tv_text(list, tv_medium(), rating, r.x + r.w - 20.0f * dp, r.cy() - 12.0f * dp, 16.0f * dp,
                            24.0f * dp, tv_rating_color(row.rating), gfx::Align::right);
                }
                break;
            }
        }
        draw_voice(list);
        kb_draw(list);
    }

    // "Говорите…": the microphone is open (or its clip is out for recognition).
    void draw_voice(gfx::DrawList &list) const
    {
        const int state = rezka::App::instance().voice_state.load();
        if (state != 1 && state != 2)
            return;
        const float dp = kTvDp;
        // The app shows it in the screen's body, under the field row (M8.b.d):
        // a wave 300 x 64 dp over "Говорите…", 16 dp apart, centred.
        const float body_top = 104.0f * dp;
        list.rounded_rect({0, body_top, kWidth, kHeight - body_top}, 0.0f, tv_surface());
        const float cx = kWidth * 0.5f;
        const float cy = body_top + (kHeight - body_top) * 0.5f;
        const Rect wave{cx - 150.0f * dp, cy - 64.0f * dp, 300.0f * dp, 64.0f * dp};
        const int bars = 30;
        const float bar_w = wave.w / static_cast<float>(bars);
        for (int i = 0; i < bars; ++i)
        {
            // No level from the pad's microphone here: the wave breathes while it listens
            const float phase = clock_ * 7.0f + static_cast<float>(i) * 0.55f;
            const float level = state == 1 ? 0.25f + 0.75f * std::fabs(std::sin(phase) * std::cos(phase * 0.37f)) : 0.15f;
            const float h = std::max(4.0f * dp, wave.h * level);
            list.rounded_rect({wave.x + static_cast<float>(i) * bar_w + bar_w * 0.2f, wave.cy() - h * 0.5f, bar_w * 0.6f, h},
                              bar_w * 0.3f, tv_primary());
        }
        tv_text(list, tv_regular(), state == 1 ? "Говорите…" : "Распознаём…", cx, wave.y + wave.h + 16.0f * dp, 24.0f * dp,
                32.0f * dp, tv_on_surface(), gfx::Align::center);
        if (state == 1)
            tv_text(list, tv_regular(), "Крестик — готово     Кружок — отмена", cx, wave.y + wave.h + 60.0f * dp, 14.0f * dp,
                    20.0f * dp, tv_on_surface_variant(), gfx::Align::center);
    }

    // ---- Расширенный поиск (O8) and the countries (N8) ------------------------
    //
    // Расширенный поиск: its title centred on top (24 dp down, 48 dp in), then a
    // column 24 dp padded, its items 12 dp apart: "Выберите категорию:" over the
    // site's categories, "Выберите жанр:" over the chosen category's genres
    // (both FlowRows of FilterChips, 8 dp apart), "Выберите год:" over a row of
    // the years that scrolls sideways, and "Поехали!". The headers are the app's
    // sticky headers, 16 dp by 8 dp of padding. The list it makes is
    // "<category> <genre> <year>", at the genre's path and the year (B8.C0071j).
    //
    // The countries: the title, a field that narrows the list as it is typed,
    // and the countries of N8.a as chips; one chosen lists /country/<name>.

    enum SxKind : int
    {
        kSxCategory,
        kSxGenre,
        kSxYear,
        kSxGo,
        kSxField,
        kSxCountry,
    };
    struct SxItem
    {
        int kind = kSxCategory;
        int index = 0;
        Rect r; // on the screen, the column's scroll applied
    };
    struct SxHeader
    {
        const char *text = "";
        float y = 0.0f;
    };
    struct SxLayout
    {
        std::vector<SxItem> items;
        std::vector<SxHeader> headers;
        float content_h = 0.0f; // the column's height, unscrolled
        float view_top = 0.0f;
        float years_y = 0.0f;   // the year row's top, -1 when none
        float years_w = 0.0f;   // the year row's whole width
    };
    static float sx_chip_h() { return 36.0f * kTvDp; }

    void sx_open(SxScreen screen, app::Feedback &feedback)
    {
        sx_screen_ = screen;
        sx_focus_ = 0;
        sx_scroll_.snap(0.0f);
        if (screen == SxScreen::extended)
            rezka::App::instance().load_search_filters_async();
        feedback.play(audio::Cue::open);
    }

    rezka::SearchFilters sx_filters() const
    {
        auto &app = rezka::App::instance();
        std::lock_guard<std::mutex> lock(app.catalog_mutex);
        return app.search_filters;
    }

    // The countries the field lets through, by their index in kCountryFilterChoices.
    std::vector<int> sx_countries() const
    {
        std::vector<int> out;
        const std::string query = sx_lower(sx_country_query_);
        for (int i = 0; i < static_cast<int>(std::size(rezka::kCountryFilterChoices)); ++i)
            if (query.empty() || sx_lower(rezka::kCountryFilterChoices[i]).find(query) != std::string::npos)
                out.push_back(i);
        return out;
    }
    // Lower case for Russian and English, to match what is typed.
    static std::string sx_lower(const std::string &text)
    {
        std::string out;
        out.reserve(text.size());
        for (std::size_t i = 0; i < text.size(); ++i)
        {
            const unsigned char c = static_cast<unsigned char>(text[i]);
            const unsigned char n = i + 1 < text.size() ? static_cast<unsigned char>(text[i + 1]) : 0;
            if (c < 0x80)
                out.push_back(static_cast<char>(std::tolower(c)));
            else if (c == 0xd0 && n >= 0x90 && n <= 0x9f) // А..П
            {
                out.push_back(static_cast<char>(0xd0));
                out.push_back(static_cast<char>(n + 0x20));
                ++i;
            }
            else if (c == 0xd0 && n >= 0xa0 && n <= 0xaf) // Р..Я
            {
                out.push_back(static_cast<char>(0xd1));
                out.push_back(static_cast<char>(n - 0x20));
                ++i;
            }
            else if (c == 0xd0 && n == 0x81) // Ё
            {
                out.push_back(static_cast<char>(0xd1));
                out.push_back(static_cast<char>(0x91));
                ++i;
            }
            else
                out.push_back(static_cast<char>(c));
        }
        return out;
    }

    float sx_chip_w(const std::string &text) const
    {
        return tv_medium().measure(text, 14.0f * kTvDp, 0.1f * kTvDp) + 32.0f * kTvDp;
    }
    // Chips from x0 to x1, wrapping, from y; returns the y under the last line.
    float sx_flow(SxLayout &L, int kind, const std::vector<std::string> &names, float x0, float x1, float y,
                  float scroll) const
    {
        const float gap = 8.0f * kTvDp;
        float x = x0;
        for (int i = 0; i < static_cast<int>(names.size()); ++i)
        {
            const float w = sx_chip_w(names[static_cast<std::size_t>(i)]);
            if (x > x0 && x + w > x1)
            {
                x = x0;
                y += sx_chip_h() + gap;
            }
            L.items.push_back({kind, i, {x, y - scroll, w, sx_chip_h()}});
            x += w + gap;
        }
        return names.empty() ? y : y + sx_chip_h();
    }

    SxLayout sx_layout(const rezka::SearchFilters &f) const
    {
        const float dp = kTvDp;
        SxLayout L;
        const float left = 48.0f * dp + 2.0f * dp;
        const float right = kWidth - 48.0f * dp - 2.0f * dp;
        L.view_top = 72.0f * dp;
        const float scroll = sx_scroll_.value;
        float y = L.view_top + 24.0f * dp;
        const float header_h = 36.0f * dp;
        const float spacing = 12.0f * dp;
        L.years_y = -1.0f;
        if (sx_screen_ == SxScreen::extended)
        {
            if (f.categories.empty())
                return L;
            const int cat = std::clamp(sx_cat_, 0, static_cast<int>(f.categories.size()) - 1);
            std::vector<std::string> names;
            for (const auto &c : f.categories)
                names.push_back(c.name);
            L.headers.push_back({"Выберите категорию:", y});
            y += header_h + spacing;
            y = sx_flow(L, kSxCategory, names, left, right, y, scroll) + spacing;
            names.clear();
            for (const auto &g : f.categories[static_cast<std::size_t>(cat)].genres)
                names.push_back(g.name);
            L.headers.push_back({"Выберите жанр:", y});
            y += header_h + spacing;
            y = sx_flow(L, kSxGenre, names, left, right, y, scroll) + spacing;
            if (!f.years.empty())
            {
                L.headers.push_back({"Выберите год:", y});
                y += header_h + spacing;
                // One row, sideways: the years scroll under the column's edges
                L.years_y = y;
                float x = left - sx_year_scroll_.value;
                for (int i = 0; i < static_cast<int>(f.years.size()); ++i)
                {
                    const float w = sx_chip_w(f.years[static_cast<std::size_t>(i)]);
                    L.items.push_back({kSxYear, i, {x, y - scroll, w, sx_chip_h()}});
                    x += w + 8.0f * dp;
                }
                L.years_w = x + sx_year_scroll_.value - left;
                y += sx_chip_h() + spacing;
            }
            const float go_w = tv_medium().measure("Поехали!", 14.0f * dp, 0.1f * dp) + 48.0f * dp;
            L.items.push_back({kSxGo, 0, {left, y - scroll, go_w, 40.0f * dp}});
            y += 40.0f * dp + 24.0f * dp;
        }
        else
        {
            L.items.push_back({kSxField, 0, {left, y - scroll, right - left, 56.0f * dp}});
            y += 56.0f * dp + 24.0f * dp;
            std::vector<std::string> names;
            for (int i : sx_countries())
                names.push_back(rezka::kCountryFilterChoices[i]);
            y = sx_flow(L, kSxCountry, names, left, right, y, scroll) + 24.0f * dp;
        }
        L.content_h = y - L.view_top;
        return L;
    }

    // The item nearest in a direction, as the TV's focus search finds it: ahead
    // of the edge, the distance along the way plus twice the offset across.
    static int sx_neighbour(const std::vector<SxItem> &items, int from, Direction nav)
    {
        if (from < 0 || from >= static_cast<int>(items.size()))
            return items.empty() ? -1 : 0;
        const Rect a = items[static_cast<std::size_t>(from)].r;
        int best = -1;
        float best_score = 1e30f;
        for (int i = 0; i < static_cast<int>(items.size()); ++i)
        {
            if (i == from)
                continue;
            const Rect b = items[static_cast<std::size_t>(i)].r;
            float along = 0.0f, across = 0.0f;
            if (nav == Direction::down)
            {
                if (b.y < a.y + a.h * 0.5f)
                    continue;
                along = b.y - (a.y + a.h);
                across = std::fabs(b.cx() - a.cx());
            }
            else if (nav == Direction::up)
            {
                if (b.y + b.h > a.y + a.h * 0.5f)
                    continue;
                along = a.y - (b.y + b.h);
                across = std::fabs(b.cx() - a.cx());
            }
            else if (nav == Direction::right)
            {
                if (b.x < a.x + a.w * 0.5f || std::fabs(b.cy() - a.cy()) > a.h * 0.5f)
                    continue;
                along = b.x - (a.x + a.w);
            }
            else
            {
                if (b.x + b.w > a.x + a.w * 0.5f || std::fabs(b.cy() - a.cy()) > a.h * 0.5f)
                    continue;
                along = a.x - (b.x + b.w);
            }
            const float score = std::max(0.0f, along) + 2.0f * across;
            if (score < best_score)
            {
                best_score = score;
                best = i;
            }
        }
        return best;
    }

    void sx_update(const InputFrame &input, float dt, app::Feedback &feedback)
    {
        auto &app = rezka::App::instance();
        const rezka::SearchFilters f = sx_filters();
        SxLayout L = sx_layout(f);
        const int count = static_cast<int>(L.items.size());
        sx_focus_ = std::clamp(sx_focus_, 0, std::max(0, count - 1));

        if (input.is_pressed(Action::back))
        {
            sx_screen_ = SxScreen::none;
            feedback.play(audio::Cue::back);
            return;
        }
        if (sx_screen_ == SxScreen::extended && f.categories.empty())
        {
            // Loading, or failed: ✕ asks again
            if (input.is_pressed(Action::confirm) && app.filters_state.load() == 3)
            {
                app.load_search_filters_async();
                feedback.play(audio::Cue::select);
            }
            sx_animate(L, dt);
            return;
        }
        if (input.nav != Direction::none && count > 0)
        {
            const int next = sx_neighbour(L.items, sx_focus_, input.nav);
            if (next < 0)
                tv_refuse(feedback, input.nav_repeat);
            else
            {
                sx_focus_ = next;
                tv_tick(feedback, L.items[static_cast<std::size_t>(next)].r.cx());
            }
        }
        if (input.is_pressed(Action::confirm) && sx_focus_ < count)
        {
            const SxItem &it = L.items[static_cast<std::size_t>(sx_focus_)];
            switch (it.kind)
            {
            case kSxCategory:
            {
                // The genre chosen stays when the new category has it (O8.l)
                const auto &cats = f.categories;
                const int old_cat = std::clamp(sx_cat_, 0, static_cast<int>(cats.size()) - 1);
                const std::string genre = sx_genre_ < static_cast<int>(cats[static_cast<std::size_t>(old_cat)].genres.size())
                                              ? cats[static_cast<std::size_t>(old_cat)].genres[static_cast<std::size_t>(sx_genre_)].name
                                              : std::string();
                sx_cat_ = it.index;
                sx_genre_ = 0;
                const auto &genres = cats[static_cast<std::size_t>(it.index)].genres;
                for (int g = 0; g < static_cast<int>(genres.size()); ++g)
                    if (genres[static_cast<std::size_t>(g)].name == genre)
                        sx_genre_ = g;
                feedback.play(audio::Cue::select);
                break;
            }
            case kSxGenre:
                sx_genre_ = it.index;
                feedback.play(audio::Cue::select);
                break;
            case kSxYear:
                sx_year_ = it.index;
                feedback.play(audio::Cue::select);
                break;
            case kSxGo:
                sx_run_extended(f, feedback);
                return;
            case kSxField:
                kb_begin(&sx_country_query_, "Поиск фильмов и сериалов по стране", false, feedback);
                break;
            default:
            {
                const std::string country = rezka::kCountryFilterChoices[it.index];
                tv_show_results(country, SearchBack::country);
                app.country_search_async(country);
                feedback.play(audio::Cue::select);
                return;
            }
            }
        }
        sx_animate(L, dt);
    }

    // The column follows the focus; the year row slides to keep its chip in view.
    void sx_animate(const SxLayout &L, float dt)
    {
        const float dp = kTvDp;
        if (sx_focus_ < static_cast<int>(L.items.size()))
        {
            const SxItem &it = L.items[static_cast<std::size_t>(sx_focus_)];
            const float view_h = kHeight - L.view_top;
            const float top = it.r.y + sx_scroll_.value - L.view_top; // in the column
            float target = sx_scroll_.target;
            if (top - target < 48.0f * dp)
                target = top - 48.0f * dp;
            if (top + it.r.h - target > view_h - 48.0f * dp)
                target = top + it.r.h - view_h + 48.0f * dp;
            sx_scroll_.target = std::clamp(target, 0.0f, std::max(0.0f, L.content_h - view_h));
            if (it.kind == kSxYear)
            {
                const float left = 50.0f * dp, right = kWidth - 50.0f * dp;
                const float x = it.r.x + sx_year_scroll_.value - left; // in the row
                float ys = sx_year_scroll_.target;
                if (x - ys < 0.0f)
                    ys = x;
                if (x + it.r.w - ys > right - left)
                    ys = x + it.r.w - (right - left);
                sx_year_scroll_.target = std::clamp(ys, 0.0f, std::max(0.0f, L.years_w - (right - left)));
            }
        }
        sx_scroll_.update(dt, omega(12.0f));
        sx_year_scroll_.update(dt, omega(12.0f));
    }

    void sx_run_extended(const rezka::SearchFilters &f, app::Feedback &feedback)
    {
        const int cat = std::clamp(sx_cat_, 0, static_cast<int>(f.categories.size()) - 1);
        const rezka::SearchCategory &c = f.categories[static_cast<std::size_t>(cat)];
        if (c.genres.empty())
        {
            tv_refuse(feedback, false);
            return;
        }
        const rezka::SearchGenre &g = c.genres[static_cast<std::size_t>(std::clamp(sx_genre_, 0, static_cast<int>(c.genres.size()) - 1))];
        std::string year = f.years.empty() ? std::string()
                                           : f.years[static_cast<std::size_t>(std::clamp(sx_year_, 0, static_cast<int>(f.years.size()) - 1))];
        if (year == "Все")
            year.clear();
        // "<category> <genre> <year>", the genre left out when "Любой" (B8.C0071j)
        std::string title = c.name + " " + (g.name == "Любой" ? std::string() : g.name) + " " + year;
        for (std::size_t at = title.find("  "); at != std::string::npos; at = title.find("  "))
            title.erase(at, 1);
        while (!title.empty() && title.back() == ' ')
            title.pop_back();
        // The year as the site's path takes it: its digits
        std::string year_part;
        for (char ch : year)
            if (ch >= '0' && ch <= '9')
                year_part.push_back(ch);
        tv_show_results(title, SearchBack::extended);
        rezka::App::instance().extended_search_async(title, g.value + year_part);
        feedback.play(audio::Cue::select);
    }

    // A FilterChip of M8.b.e: outlined, tinted when chosen, light under the focus.
    void sx_draw_chip(gfx::DrawList &list, const Rect &r, const std::string &text, bool selected, bool focused) const
    {
        const float dp = kTvDp;
        const float radius = 8.0f * dp;
        Color fill = kClear, content = tv_on_surface_variant(), border = tv_border();
        float border_w = 1.0f * dp;
        if (focused && selected)
        {
            fill = tv_on_primary_container();
            content = tv_on_primary();
            border = tv_on_primary_container();
        }
        else if (focused)
        {
            fill = tv_on_surface();
            content = tv_inverse_on_surface();
            border_w = 0.0f;
        }
        else if (selected)
        {
            fill = tv_secondary_container().with_alpha(0.4f);
            content = tv_on_secondary_container();
            border = tv_secondary();
        }
        const Rect g = focused ? scaled(r, 1.05f) : r;
        if (border_w > 0.0f)
            list.bordered_rect(g, radius, fill, border_w, border);
        else
            list.rounded_rect(g, radius, fill);
        tv_text(list, tv_medium(), text, g.cx(), g.y + 8.0f * dp, 14.0f * dp, 20.0f * dp, content, gfx::Align::center,
                0.1f * dp);
    }

    void sx_draw(gfx::DrawList &list) const
    {
        if (sx_screen_ == SxScreen::none)
            return;
        const float dp = kTvDp;
        auto &app = rezka::App::instance();
        list.rounded_rect({0, 0, kWidth, kHeight}, 0.0f, tv_surface());
        const bool extended = sx_screen_ == SxScreen::extended;
        tv_text(list, tv_regular(), extended ? "Расширенный поиск" : "Поиск фильмов и сериалов по стране", kWidth * 0.5f,
                24.0f * dp, 24.0f * dp, 32.0f * dp, tv_on_surface(), gfx::Align::center);
        const rezka::SearchFilters f = sx_filters();
        const SxLayout L = sx_layout(f);
        if (extended && f.categories.empty())
        {
            const int state = app.filters_state.load();
            if (state == 3)
            {
                tv_text(list, tv_regular(), "Ошибка", kWidth * 0.5f, 220.0f * dp, 22.0f * dp, 28.0f * dp, tv_on_surface(),
                        gfx::Align::center);
                tv_text(list, tv_regular(), "Нажмите ✕, чтобы повторить", kWidth * 0.5f, 256.0f * dp, 14.0f * dp, 20.0f * dp,
                        tv_on_surface_variant(), gfx::Align::center);
            }
            else
                list.arc(kWidth * 0.5f, 240.0f * dp, 20.0f * dp, 4.0f * dp, clock_ * 5.0f, 4.4f, tv_primary());
            kb_draw(list);
            return;
        }
        const float view_top = L.view_top;
        list.push_clip({0.0f, view_top, kWidth, kHeight - view_top});
        const int cat = f.categories.empty() ? 0 : std::clamp(sx_cat_, 0, static_cast<int>(f.categories.size()) - 1);
        const std::vector<int> countries = extended ? std::vector<int>() : sx_countries();
        for (int i = 0; i < static_cast<int>(L.items.size()); ++i)
        {
            const SxItem &it = L.items[static_cast<std::size_t>(i)];
            if (it.r.y + it.r.h < view_top - 8.0f * dp || it.r.y > kHeight)
                continue;
            const bool focused = i == sx_focus_;
            switch (it.kind)
            {
            case kSxCategory:
                sx_draw_chip(list, it.r, f.categories[static_cast<std::size_t>(it.index)].name, it.index == cat, focused);
                break;
            case kSxGenre:
                sx_draw_chip(list, it.r, f.categories[static_cast<std::size_t>(cat)].genres[static_cast<std::size_t>(it.index)].name,
                             it.index == sx_genre_, focused);
                break;
            case kSxYear:
                if (it.r.x + it.r.w < 0.0f || it.r.x > kWidth)
                    break;
                sx_draw_chip(list, it.r, f.years[static_cast<std::size_t>(it.index)], it.index == sx_year_, focused);
                break;
            case kSxGo:
            {
                const Rect g = focused ? scaled(it.r, 1.05f) : it.r;
                list.rounded_rect(g, g.h * 0.5f, focused ? tv_on_surface() : tv_secondary_container().with_alpha(0.6f));
                tv_text(list, tv_medium(), "Поехали!", g.cx(), g.cy() - 10.0f * dp, 14.0f * dp, 20.0f * dp,
                        focused ? tv_inverse_on_surface() : tv_on_secondary_container(), gfx::Align::center, 0.1f * dp);
                break;
            }
            case kSxField:
            {
                list.rounded_rect(it.r, 28.0f * dp, tv_surface_variant().with_alpha(0.3f));
                list.bordered_rect(it.r, 28.0f * dp, kClear, (focused ? 2.0f : 1.0f) * dp,
                                   focused ? tv_primary() : tv_border_variant());
                tv_icon(list, kTvIconSearch, it.r.x + 20.0f * dp, it.r.cy() - 12.0f * dp, 24.0f * dp, tv_on_surface_variant());
                const bool empty = sx_country_query_.empty();
                tv_text(list, tv_regular(), empty ? std::string("Поиск") : sx_country_query_, it.r.x + 60.0f * dp,
                        it.r.cy() - 14.0f * dp, 18.0f * dp, 28.0f * dp, empty ? tv_on_surface_variant() : tv_on_surface());
                break;
            }
            default:
                sx_draw_chip(list, it.r, rezka::kCountryFilterChoices[it.index], false, focused);
                break;
            }
        }
        // The headers over the items, each held at the top while its own items scroll
        for (std::size_t h = 0; h < L.headers.size(); ++h)
        {
            float y = L.headers[h].y - sx_scroll_.value;
            const float next = h + 1 < L.headers.size() ? L.headers[h + 1].y - sx_scroll_.value : 1e9f;
            if (y < view_top)
                y = std::min(view_top, next - 36.0f * dp);
            list.rounded_rect({0.0f, y, kWidth, 36.0f * dp}, 0.0f, tv_surface());
            tv_text(list, tv_medium(), L.headers[h].text, 48.0f * dp + 16.0f * dp, y + 8.0f * dp, 16.0f * dp, 20.0f * dp,
                    tv_on_surface(), gfx::Align::left, 0.1f * dp);
        }
        if (!extended && countries.empty())
            tv_text(list, tv_regular(), "Ничего не найдено", kWidth * 0.5f, view_top + 140.0f * dp, 16.0f * dp, 24.0f * dp,
                    tv_on_surface_variant(), gfx::Align::center);
        list.pop_clip();
        kb_draw(list);
    }

    void type_into(std::string &target)
    {
        if (key_row_ < 4) {
            const char **cur_keys = keys_for_lang(key_lang_);
            const int idx = key_row_ * kKeyCols + key_col_;
            if (idx >= 0 && idx < 48) {
                target += cur_keys[idx];
                if (target.size() > 64) target.resize(64);
            }
        } else {
            if (key_col_ <= 2) {
                cycle_key_lang();
            } else if (key_col_ >= 3 && key_col_ <= 6) {
                target.push_back(' ');
            } else if (key_col_ >= 7 && key_col_ <= 8) {
                erase_from(target);
            }
        }
    }

    // ---- the profile: HDrezkaTV's account screen -----------------------------
    //
    // Signed out: the login form on the left (a field opens the console's own
    // keyboard through kb_begin; on a PC the sheet of keys comes up instead),
    // a QR code of the site on the right. Signed in: the greeting with the
    // avatar, the subscription with its days, "Досмотреть", "Закладки" and
    // "Выйти", and the lists those two open.

    enum ProfileView : int
    {
        kProfileMain,
        kProfileWatchLater,
        kProfileBookmarks,
    };
    enum ProfileIll : int
    {
        kIllEnter,
        kIllWelcome,
        kIllMovieNight,
        kIllHomeCinema,
        kIllVoid,
        kIllCount,
    };
    enum ProfileItem : int
    {
        kProfileLogin = 0,    // signed out: the login field
        kProfilePassword = 1, // ... the password field
        kProfileSubmit = 2,   // ... the sign-in button
        kProfileContinue = 0, // signed in: Досмотреть
        kProfileFolders = 1,  // ... Закладки
        kProfileLogout = 2,   // ... Выйти
    };

    static std::string tv_days_text(int days)
    {
        const int tens = days % 100;
        const int ones = days % 10;
        const char *word = (tens >= 11 && tens <= 14) ? "дней" : ones == 1 ? "день" : (ones >= 2 && ones <= 4) ? "дня" : "дней";
        return std::to_string(days) + " " + word;
    }

    void update_profile(const InputFrame &input, app::Feedback &feedback)
    {
        auto &app = rezka::App::instance();
        rezka::Account acc;
        std::size_t later_count = 0, folder_count = 0;
        {
            std::lock_guard<std::mutex> lock(app.catalog_mutex);
            acc = app.account;
            later_count = app.watch_later.size();
            folder_count = app.bookmark_folders.size();
        }
        // Signing in or out resets the screen
        if (acc.logged_in != profile_was_logged_)
        {
            profile_was_logged_ = acc.logged_in;
            profile_view_ = kProfileMain;
            profile_focus_ = 0;
            if (acc.logged_in)
                login_password_.clear();
        }
        if (acc.logged_in && !acc.avatar.empty())
            app.request_photo(acc.avatar);

        if (profile_view_ != kProfileMain)
        {
            const int count = static_cast<int>(profile_view_ == kProfileWatchLater ? later_count : folder_count);
            update_profile_list(input, feedback, acc, count);
            return;
        }
        if (input.is_pressed(Action::back))
        {
            profile_open_ = false;
            feedback.play(audio::Cue::back);
            return;
        }
        const int last = 2;
        if (input.nav == Direction::up || input.nav == Direction::down)
        {
            const int step = input.nav == Direction::down ? 1 : -1;
            const int next = std::clamp(profile_focus_ + step, 0, last);
            if (next == profile_focus_)
                tv_refuse(feedback, input.nav_repeat);
            else
            {
                profile_focus_ = next;
                feedback.play(audio::Cue::focus);
            }
            return;
        }
        if (!input.is_pressed(Action::confirm))
            return;

        if (acc.logged_in)
        {
            if (profile_focus_ == kProfileLogout)
            {
                app.logout_async();
                feedback.play(audio::Cue::select);
                return;
            }
            // The list opened is the site's as it is now
            app.load_account_lists_async();
            profile_view_ = profile_focus_ == kProfileContinue ? kProfileWatchLater : kProfileBookmarks;
            profile_list_focus_ = 0;
            feedback.play(audio::Cue::open);
            return;
        }
        if (profile_focus_ == kProfileLogin)
            kb_begin(&login_email_, "Логин или e-mail", false, feedback);
        else if (profile_focus_ == kProfilePassword)
            kb_begin(&login_password_, "Пароль", true, feedback);
        else if (login_email_.empty() || login_password_.empty())
            tv_refuse(feedback, false);
        else if (!app.login_busy.load())
        {
            app.login_async(login_email_, login_password_);
            feedback.play(audio::Cue::select);
        }
    }

    void update_profile_list(const InputFrame &input, app::Feedback &feedback, const rezka::Account &,
                             int count)
    {
        auto &app = rezka::App::instance();
        if (input.is_pressed(Action::back))
        {
            profile_view_ = kProfileMain;
            feedback.play(audio::Cue::back);
            return;
        }
        if (input.nav == Direction::up || input.nav == Direction::down)
        {
            const int next = std::clamp(profile_list_focus_ + (input.nav == Direction::down ? 1 : -1), 0,
                                        std::max(0, count - 1));
            if (next == profile_list_focus_)
                tv_refuse(feedback, input.nav_repeat);
            else
            {
                profile_list_focus_ = next;
                feedback.play(audio::Cue::focus);
            }
            return;
        }
        if (count == 0 || !input.is_pressed(Action::confirm))
            return;
        if (profile_view_ == kProfileWatchLater)
        {
            rezka::WatchLaterItem item;
            {
                std::lock_guard<std::mutex> lock(app.catalog_mutex);
                if (profile_list_focus_ >= static_cast<int>(app.watch_later.size()))
                    return;
                item = app.watch_later[static_cast<std::size_t>(profile_list_focus_)];
            }
            rezka::CatalogItem entry;
            entry.id = item.id;
            entry.title = item.title;
            entry.url = item.url;
            entry.cover_url = item.cover_url;
            entry.info = item.project_info;
            const int index = app.adopt_catalog_item(entry);
            if (index < 0)
            {
                tv_refuse(feedback, false);
                return;
            }
            profile_open_ = false;
            dp_stack_.clear();
            open_page(index, false, feedback);
            return;
        }
        rezka::BookmarkFolder folder;
        {
            std::lock_guard<std::mutex> lock(app.catalog_mutex);
            if (profile_list_focus_ >= static_cast<int>(app.bookmark_folders.size()))
                return;
            folder = app.bookmark_folders[static_cast<std::size_t>(profile_list_focus_)];
        }
        profile_open_ = false;
        dp_open_list(folder.name, folder.url, feedback);
    }

    // One of the profile's buttons: a rounded plate, its icon and label.
    void draw_profile_row(gfx::DrawList &list, const Rect &r, bool focused, int icon, const std::string &label,
                          const std::string &trailing = "") const
    {
        const float dp = kTvDp;
        list.rounded_rect(r, 28.0f * dp, focused ? tv_secondary_container() : tv_surface_variant().with_alpha(0.3f));
        if (focused)
            list.bordered_rect(r, 28.0f * dp, kClear, 2.0f * dp, tv_primary());
        const Color ink = focused ? tv_on_secondary_container() : tv_on_surface();
        if (icon >= 0)
            tv_icon(list, icon, r.x + 24.0f * dp, r.cy() - 12.0f * dp, 24.0f * dp, ink);
        tv_text(list, tv_medium(), label, r.x + (icon >= 0 ? 64.0f : 24.0f) * dp, r.cy() - 12.0f * dp, 18.0f * dp,
                24.0f * dp, ink, gfx::Align::left, 0.1f * dp);
        if (!trailing.empty())
            tv_text(list, tv_regular(), trailing, r.x + r.w - 24.0f * dp, r.cy() - 10.0f * dp, 14.0f * dp,
                    20.0f * dp, tv_on_surface_variant(), gfx::Align::right);
    }

    // A field of the login form.
    void draw_profile_field(gfx::DrawList &list, const Rect &r, bool focused, const char *label,
                            const std::string &value, bool secret) const
    {
        const float dp = kTvDp;
        list.rounded_rect(r, 8.0f * dp, tv_surface_variant().with_alpha(0.25f));
        list.bordered_rect(r, 8.0f * dp, kClear, (focused ? 2.0f : 1.0f) * dp,
                           focused ? tv_primary() : tv_border());
        const bool empty = value.empty();
        // The label floats above the text, as in the app's outlined fields
        tv_text(list, tv_regular(), label, r.x + 16.0f * dp, r.y + 6.0f * dp, 12.0f * dp, 16.0f * dp,
                focused ? tv_primary() : tv_on_surface_variant());
        const std::string shown =
            secret ? kb_masked(value) : value;
        const std::string fit = tv_regular().font->fit(shown, 16.0f * dp, r.w - 32.0f * dp);
        tv_text(list, tv_regular(), empty ? std::string("Нажмите, чтобы ввести") : fit, r.x + 16.0f * dp,
                r.y + 26.0f * dp, 16.0f * dp, 24.0f * dp,
                empty ? tv_on_surface_variant().with_alpha(0.6f) : tv_on_surface());
    }

    // An illustration of the strip tv/ill-*.png, fitted into `box` and centred.
    void draw_profile_ill(gfx::DrawList &list, int which, const Rect &box) const
    {
        const std::uint32_t texture = tv_ill_[which];
        if (texture == 0 || tv_ill_aspect_[which] <= 0.0f)
            return;
        float w = box.w, h = box.w / tv_ill_aspect_[which];
        if (h > box.h)
        {
            h = box.h;
            w = h * tv_ill_aspect_[which];
        }
        list.image(texture, {box.cx() - w * 0.5f, box.cy() - h * 0.5f, w, h}, gfx::kFullUv, kWhite);
    }

    void draw_profile(gfx::DrawList &list) const
    {
        if (!profile_open_)
            return;
        const float dp = kTvDp;
        auto &app = rezka::App::instance();
        rezka::Account acc;
        std::vector<rezka::WatchLaterItem> later;
        std::vector<rezka::BookmarkFolder> folders;
        {
            std::lock_guard<std::mutex> lock(app.catalog_mutex);
            acc = app.account;
            later = app.watch_later;
            folders = app.bookmark_folders;
        }
        list.rounded_rect({0, 0, kWidth, kHeight}, 0.0f, tv_surface());
        const float left = 48.0f * dp;
        const float col_w = 400.0f * dp;

        if (profile_view_ != kProfileMain)
        {
            draw_profile_list(list, acc, later, folders);
            kb_draw(list);
            return;
        }

        if (!acc.logged_in)
        {
            tv_text(list, tv_regular(), "Вход в аккаунт", left, 40.0f * dp, 28.0f * dp, 36.0f * dp, tv_on_surface());
            tv_text(list, tv_regular(), "Войдите, чтобы смотреть закладки и продолжать с того же места", left,
                    80.0f * dp, 14.0f * dp, 20.0f * dp, tv_on_surface_variant());
            const Rect email{left, 120.0f * dp, col_w, 56.0f * dp};
            const Rect pass{left, 192.0f * dp, col_w, 56.0f * dp};
            draw_profile_field(list, email, profile_focus_ == kProfileLogin, "Логин или e-mail", login_email_, false);
            draw_profile_field(list, pass, profile_focus_ == kProfilePassword, "Пароль", login_password_, true);

            const bool busy = app.login_busy.load();
            const std::string status = busy ? std::string("Входим в аккаунт…") : acc.status;
            if (!status.empty())
                tv_text(list, tv_regular(), status, left, 260.0f * dp, 14.0f * dp, 20.0f * dp,
                        busy ? tv_on_surface_variant() : tv_error());
            const Rect button{left, 292.0f * dp, 200.0f * dp, 48.0f * dp};
            const bool on = profile_focus_ == kProfileSubmit;
            list.rounded_rect(button, 24.0f * dp, on ? tv_primary() : tv_primary_container());
            if (on)
                list.bordered_rect(button.inset(-3.0f * dp), 27.0f * dp, kClear, 2.0f * dp, tv_on_surface());
            tv_text(list, tv_medium(), "Войти", button.cx(), button.cy() - 12.0f * dp, 18.0f * dp, 24.0f * dp,
                    on ? tv_on_primary() : tv_on_primary_container(), gfx::Align::center, 0.1f * dp);
            draw_profile_ill(list, kIllEnter, {left, 360.0f * dp, 200.0f * dp, 150.0f * dp});

            // The QR code: the site, to sign in on a phone and come back
            const Rect card{kWidth - 48.0f * dp - 360.0f * dp, 48.0f * dp, 360.0f * dp, 380.0f * dp};
            list.rounded_rect(card, 28.0f * dp, tv_surface_variant().with_alpha(0.3f));
            const Rect plate{card.x + 40.0f * dp, card.y + 24.0f * dp, 280.0f * dp, 280.0f * dp};
            list.rounded_rect(plate, 16.0f * dp, kWhite);
            if (profile_qr_.size > 0)
            {
                const int n = profile_qr_.size;
                const float module = (plate.w - 32.0f * dp) / static_cast<float>(n + 2);
                const float ax = plate.x + 16.0f * dp + module;
                const float ay = plate.y + 16.0f * dp + module;
                for (int y = 0; y < n; ++y)
                    for (int x = 0; x < n; ++x)
                        if (profile_qr_.at(x, y))
                            list.rounded_rect({ax + x * module, ay + y * module, module + 0.6f, module + 0.6f}, 0.0f,
                                              Color::rgb(0x101214));
            }
            std::vector<std::string> lines = tv_regular().font->wrap(
                "Быстрый вход по QR-коду: откройте сайт на смартфоне и войдите в аккаунт", 14.0f * dp,
                card.w - 48.0f * dp);
            float y = plate.y + plate.h + 16.0f * dp;
            for (std::size_t i = 0; i < lines.size() && i < 3; ++i, y += 20.0f * dp)
                tv_text(list, tv_regular(), lines[i], card.cx(), y, 14.0f * dp, 20.0f * dp, tv_on_surface_variant(),
                        gfx::Align::center);
            kb_draw(list);
            return;
        }

        // The greeting
        const float avatar = 96.0f * dp;
        const Rect face{left, 40.0f * dp, avatar, avatar};
        list.circle(face.cx(), face.cy(), avatar * 0.5f, tv_primary_container());
        if (!dp_photo(list, acc.avatar, face, avatar * 0.5f, kWhite, 1.0f))
            tv_icon(list, kTvIconAccount, face.x + avatar * 0.2f, face.y + avatar * 0.2f, avatar * 0.6f,
                    tv_on_primary_container());
        const float tx = face.x + avatar + 24.0f * dp;
        const std::string name = acc.name.empty() ? std::string("Пользователь") : acc.name;
        tv_text(list, tv_regular(), tv_regular().font->fit("Привет, " + name + "!", 28.0f * dp, 520.0f * dp), tx,
                face.y + 4.0f * dp, 28.0f * dp, 36.0f * dp, tv_on_surface());
        if (!acc.email.empty())
            tv_text(list, tv_regular(), tv_regular().font->fit(acc.email, 14.0f * dp, 520.0f * dp), tx,
                    face.y + 44.0f * dp, 14.0f * dp, 20.0f * dp, tv_on_surface_variant());
        // The subscription
        const std::string sub = acc.premium
            ? (acc.premium_days > 0 ? "Premium · осталось " + tv_days_text(acc.premium_days) : std::string("Premium"))
            : std::string("Подписка не оформлена");
        const float sub_w = tv_medium().measure(sub, 14.0f * dp, 0.1f * dp) + 32.0f * dp;
        const Rect pill{tx, face.y + 68.0f * dp, sub_w, 28.0f * dp};
        list.rounded_rect(pill, 14.0f * dp, acc.premium ? tv_primary_container() : tv_surface_variant().with_alpha(0.5f));
        tv_text(list, tv_medium(), sub, pill.cx(), pill.cy() - 10.0f * dp, 14.0f * dp, 20.0f * dp,
                acc.premium ? tv_on_primary_container() : tv_on_surface_variant(), gfx::Align::center, 0.1f * dp);

        const float row_w = 400.0f * dp;
        const float row_h = 56.0f * dp;
        float y = 176.0f * dp;
        draw_profile_row(list, {left, y, row_w, row_h}, profile_focus_ == kProfileContinue, kTvIconPlayArrow,
                         "Досмотреть", std::to_string(later.size()));
        y += row_h + 12.0f * dp;
        draw_profile_row(list, {left, y, row_w, row_h}, profile_focus_ == kProfileFolders, kTvIconBookmark,
                         "Закладки", std::to_string(folders.size()));
        y += row_h + 12.0f * dp;
        draw_profile_row(list, {left, y, row_w, row_h}, profile_focus_ == kProfileLogout, kTvIconBlock, "Выйти");
        draw_profile_ill(list, kIllWelcome, {kWidth - 48.0f * dp - 420.0f * dp, 120.0f * dp, 420.0f * dp, 220.0f * dp * 2.0f});
        kb_draw(list);
    }

    // "Досмотреть" and "Закладки": a column of rows, the focused one kept in view.
    void draw_profile_list(gfx::DrawList &list, const rezka::Account &, const std::vector<rezka::WatchLaterItem> &later,
                           const std::vector<rezka::BookmarkFolder> &folders) const
    {
        const float dp = kTvDp;
        const bool watch = profile_view_ == kProfileWatchLater;
        const float left = 48.0f * dp;
        const float width = kWidth - 2.0f * left;
        tv_text(list, tv_regular(), watch ? "Досмотреть" : "Закладки", left, 40.0f * dp, 28.0f * dp, 36.0f * dp,
                tv_on_surface());
        const int count = static_cast<int>(watch ? later.size() : folders.size());
        if (count == 0)
        {
            const bool loading = rezka::App::instance().account_lists_loading.load();
            tv_text(list, tv_regular(), loading ? "Загрузка…" : watch ? "Здесь пока пусто" : "Закладок пока нет",
                    kWidth * 0.5f, 360.0f * dp + 200.0f * dp, 18.0f * dp, 24.0f * dp, tv_on_surface_variant(),
                    gfx::Align::center);
            draw_profile_ill(list, watch ? kIllMovieNight : kIllHomeCinema,
                             {kWidth * 0.5f - 300.0f * dp, 120.0f * dp, 600.0f * dp, 220.0f * dp});
            return;
        }
        const float row_h = 64.0f * dp;
        const float gap = 8.0f * dp;
        const float top = 100.0f * dp;
        const int visible = std::max(1, static_cast<int>((kHeight - top - 24.0f * dp) / (row_h + gap)));
        const int first = std::clamp(profile_list_focus_ - visible / 2, 0, std::max(0, count - visible));
        for (int i = first; i < count && i < first + visible; ++i)
        {
            const Rect r{left, top + static_cast<float>(i - first) * (row_h + gap), width, row_h};
            const bool on = i == profile_list_focus_;
            list.rounded_rect(r, 16.0f * dp, on ? tv_secondary_container() : tv_surface_variant().with_alpha(0.3f));
            if (on)
                list.bordered_rect(r, 16.0f * dp, kClear, 2.0f * dp, tv_primary());
            const Color ink = on ? tv_on_secondary_container() : tv_on_surface();
            if (watch)
            {
                const auto &item = later[static_cast<std::size_t>(i)];
                tv_text(list, tv_medium(), tv_medium().font->fit(item.title, 16.0f * dp, width * 0.62f),
                        r.x + 24.0f * dp, r.y + 8.0f * dp, 16.0f * dp, 24.0f * dp, ink);
                const std::string sub = item.info.empty() ? item.project_info : item.info;
                tv_text(list, tv_regular(), tv_regular().font->fit(sub, 12.0f * dp, width * 0.62f), r.x + 24.0f * dp,
                        r.y + 34.0f * dp, 12.0f * dp, 18.0f * dp, tv_on_surface_variant());
                if (!item.next_info.empty())
                    tv_text(list, tv_regular(), tv_regular().font->fit(item.next_info, 12.0f * dp, width * 0.3f),
                            r.x + r.w - 24.0f * dp, r.cy() - 9.0f * dp, 12.0f * dp, 18.0f * dp, tv_primary(),
                            gfx::Align::right);
            }
            else
            {
                const auto &folder = folders[static_cast<std::size_t>(i)];
                tv_icon(list, kTvIconBookmark, r.x + 24.0f * dp, r.cy() - 12.0f * dp, 24.0f * dp, ink);
                tv_text(list, tv_medium(), tv_medium().font->fit(folder.name, 16.0f * dp, width * 0.7f),
                        r.x + 64.0f * dp, r.cy() - 12.0f * dp, 16.0f * dp, 24.0f * dp, ink);
                tv_text(list, tv_regular(), folder.count, r.x + r.w - 24.0f * dp, r.cy() - 10.0f * dp, 14.0f * dp,
                        20.0f * dp, tv_on_surface_variant(), gfx::Align::right);
            }
        }
    }

    // ---- the home page: HDrezkaTV's "MovieList" screen ----------------------
    //
    // The Android TV app's home, measure for measure: its layout is in dp on a
    // 960x540 screen, which is two virtual pixels a dp here. A navigation
    // drawer of round icons on the left (it opens over a dimmed page that
    // slides aside), a pill tab row (Смотрят сейчас / Последние поступления /
    // Популярное) that switches as the focus moves, a row of type chips, a
    // seven-column grid of 2:3 posters with two lines of title and two of
    // year, country and genre, and a clock hanging from the top edge. Colours
    // are the app's dark scheme, type is Roboto at the app's sizes.

    static constexpr float kTvDp = 2.0f;
    static constexpr float kTvRail = 80.0f * kTvDp;        // the closed drawer's width
    static constexpr float kTvSlide = 220.0f * kTvDp;      // the page moves this far when it opens
    static constexpr float kTvItem = 56.0f * kTvDp;        // a drawer item, closed: a circle
    static constexpr float kTvItemOpen = 256.0f * kTvDp;   // ... and open
    static constexpr float kTvPad = 48.0f * kTvDp;         // the page's side padding
    static constexpr float kTvTabsY = 24.0f * kTvDp;       // 16 above the row, 8 inside it
    static constexpr float kTvTabH = 36.0f * kTvDp;
    static constexpr float kTvChipsY = 84.0f * kTvDp;
    static constexpr float kTvChipH = 36.0f * kTvDp;
    static constexpr float kTvGridTop = 136.0f * kTvDp;    // the grid's viewport starts here
    static constexpr float kTvGridPad = 24.0f * kTvDp;     // its content padding, top and bottom
    static constexpr float kTvGap = 12.0f * kTvDp;
    static constexpr float kTvTitleSize = 14.0f * kTvDp;
    static constexpr float kTvSubSize = 12.0f * kTvDp;
    static constexpr float kTvHeroH = 280.0f * kTvDp;     // the banner on Главная
    static constexpr float kTvHeroGap = 24.0f * kTvDp;    // ... and the space under it
    static constexpr float kTvHeroSeconds = 7.0f;         // a slide's time when left alone
    // "Столбцы в списках" (5 to 9, the app's default 7) sets the grid.
    static int tv_columns() { return std::clamp(rezka::App::instance().settings().columns, 5, 9); }
    static float tv_card_w()
    {
        const int columns = tv_columns();
        return (kWidth - kTvRail - 2.0f * kTvPad - static_cast<float>(columns - 1) * kTvGap) /
               static_cast<float>(columns);
    }
    static float tv_cover_h() { return tv_card_w() * 1.5f; }
    static float tv_card_h()
    {
        return tv_cover_h() + 12.0f * kTvDp + 2.0f * kTvTitleSize + 4.0f * kTvDp + 2.0f * kTvSubSize;
    }
    static float tv_row_pitch() { return tv_card_h() + kTvGap; }
    static constexpr int kTvDrawerItems = 7;
    static constexpr int kTvHomeItem = 2;
    static constexpr int kTvTabs = 3;
    static constexpr int kTvTypes = 5;

    static constexpr const char *kTvDrawerNames[kTvDrawerItems] = {
        "Профиль", "Поиск", "Главная", "Новинки", "Подборки", "Анонсы", "Настройки"};
    static constexpr const char *kTvTabNames[kTvTabs] = {"Смотрят сейчас", "Последние поступления",
                                                          "Популярное"};
    static constexpr const char *kTvTypeNames[kTvTypes] = {"Все", "Фильмы", "Сериалы",
                                                            "Мультфильмы", "Аниме"};

    // The app's dark colour scheme (q9.a in the APK), by Material role.
    // "Черная тема" turns the surface black, as the app does for OLED screens.
    static Color tv_surface()
    {
        return rezka::App::instance().settings().black_mode ? Color::rgb(0x000000) : Color::rgb(0x191c1e);
    }
    static Color tv_on_surface() { return Color::rgb(0xe1e2e5); }
    static Color tv_surface_variant() { return Color::rgb(0x40484c); }
    static Color tv_on_surface_variant() { return Color::rgb(0xc0c7cd); }
    static Color tv_primary_container() { return Color::rgb(0x004d66); }
    static Color tv_on_primary_container() { return Color::rgb(0xc0e8ff); }
    static Color tv_on_primary() { return Color::rgb(0x003547); }
    static Color tv_secondary() { return Color::rgb(0xb4cad6); }
    static Color tv_secondary_container() { return Color::rgb(0x364954); }
    static Color tv_on_secondary_container() { return Color::rgb(0xd0e6f3); }
    static Color tv_border() { return Color::rgb(0x8a9297); }
    static Color tv_inverse_on_surface() { return Color::rgb(0x191c1e); }

    enum class TvZone : std::uint8_t
    {
        drawer,
        tabs,
        chips,
        hero, // the banner's buttons, between the chips and the grid
        grid,
    };

    // A banner slide, measured once per change: the title in capitals on at
    // most two lines, the facts line, three lines of the description.
    struct TvHero
    {
        rezka::CatalogItem item;
        std::string title[2];
        std::vector<std::string> facts; // year, genre, country
        std::string quality;
        std::vector<std::string> blurb;
        std::uint32_t texture = 0;
        float aspect = 0.0f; // the picture's width over its height
    };

    // What a card shows, measured once per title.
    struct TvCard
    {
        std::string url;
        std::string title[2];
        std::string sub[2];
        std::uint32_t texture = 0;
        float lift = 0.0f; // 1 while the focus is on it
    };

    // Roboto, in the two kit font slots this title does not otherwise use.
    const ui::FontRef &tv_regular() const
    {
        return context_.fonts.pixel;
    }
    const ui::FontRef &tv_medium() const
    {
        return context_.fonts.hand;
    }

    // One line of text in a line box of the given height, as Android centres
    // a font's ascent and descent in it. Returns the width.
    float tv_text(gfx::DrawList &list, const ui::FontRef &font, std::string_view value, float x,
                  float top, float size, float line_height, Color color,
                  gfx::Align align = gfx::Align::left, float tracking = 0.0f) const
    {
        const float ascent = font.font->ascent(size);
        const float descent = font.font->descent(size);
        const float baseline = top + (line_height - (ascent + descent)) * 0.5f + ascent;
        return list.text(*font.font, font.texture, value, x, baseline, size, color, align, tracking);
    }

    // At most two lines, the second ending in an ellipsis when text remains.
    static void tv_two_lines(const gfx::Font &font, const std::string &text, float size, float width,
                             std::string out[2])
    {
        out[0].clear();
        out[1].clear();
        std::vector<std::string> lines = font.wrap(text, size, width);
        // A word wider than the column breaks where it must, as on Android.
        for (std::size_t i = 0; i < lines.size() && i < 3; ++i)
        {
            if (font.measure(lines[i], size) <= width)
                continue;
            std::size_t cut = 0;
            for (std::size_t at = 0; at < lines[i].size();)
            {
                std::size_t next = at + 1;
                while (next < lines[i].size() && (static_cast<unsigned char>(lines[i][next]) & 0xc0) == 0x80)
                    ++next;
                if (font.measure(std::string_view(lines[i]).substr(0, next), size) > width)
                    break;
                cut = at = next;
            }
            if (cut == 0)
                continue;
            lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(i) + 1, lines[i].substr(cut));
            lines[i].resize(cut);
        }
        if (lines.empty())
            return;
        out[0] = lines[0];
        if (lines.size() == 2)
            out[1] = lines[1];
        else if (lines.size() > 2)
        {
            std::string rest = lines[1];
            for (std::size_t i = 2; i < lines.size(); ++i)
                rest += " " + lines[i];
            out[1] = font.fit(rest, size, width);
        }
    }

    // The cards' texts and covers, from the catalogue copy made by
    // refresh_catalog_from_app. Titles already measured keep their lines.
    void tv_rebuild_cards(const std::vector<rezka::MovieData> &movies)
    {
        std::vector<TvCard> cards(movies.size());
        for (std::size_t i = 0; i < movies.size(); ++i)
        {
            const rezka::MovieData &m = movies[i];
            TvCard &card = cards[i];
            if (i < tv_cards_.size() && tv_cards_[i].url == m.url)
                card = std::move(tv_cards_[i]);
            else
            {
                card.url = m.url;
                tv_two_lines(*tv_medium().font, m.title, kTvTitleSize, tv_card_w(), card.title);
                tv_two_lines(*tv_regular().font, m.info, kTvSubSize, tv_card_w(), card.sub);
            }
            card.texture = m.texture_ready ? m.texture_id : 0;
        }
        tv_cards_ = std::move(cards);
        const int count = static_cast<int>(tv_cards_.size());
        tv_focus_ = std::clamp(tv_focus_, 0, std::max(0, count - 1));
        // A list of results still on its way keeps the grid's focus; an empty
        // one gives it to the chips (home) or the drawer.
        const bool waiting = !home_layout_ && rezka::App::instance().catalog_loading.load();
        if (count == 0 && tv_zone_ == TvZone::grid && !waiting)
            tv_zone_ = home_layout_ ? TvZone::chips : TvZone::drawer;
    }

    // Russian and English capitals, in UTF-8.
    static std::string tv_upper(const std::string &text)
    {
        std::string out;
        out.reserve(text.size());
        for (std::size_t i = 0; i < text.size(); ++i)
        {
            const unsigned char c = static_cast<unsigned char>(text[i]);
            const unsigned char n = i + 1 < text.size() ? static_cast<unsigned char>(text[i + 1]) : 0;
            if (c < 0x80)
                out.push_back(static_cast<char>(std::toupper(c)));
            else if (c == 0xd0 && n >= 0xb0 && n <= 0xbf) // а..п
            {
                out.push_back(static_cast<char>(0xd0));
                out.push_back(static_cast<char>(n - 0x20));
                ++i;
            }
            else if (c == 0xd1 && n >= 0x80 && n <= 0x8f) // р..я
            {
                out.push_back(static_cast<char>(0xd0));
                out.push_back(static_cast<char>(n + 0x20));
                ++i;
            }
            else if (c == 0xd1 && n == 0x91) // ё
            {
                out.push_back(static_cast<char>(0xd0));
                out.push_back(static_cast<char>(0x81));
                ++i;
            }
            else
                out.push_back(static_cast<char>(c));
        }
        return out;
    }
    // The banner's text column: the left part, over the dark side of the picture.
    static float tv_hero_text_w() { return tv_hero_rect().w * 0.46f; }
    static constexpr float kTvHeroTitle = 32.0f * kTvDp;      // one line ...
    static constexpr float kTvHeroTitleSmall = 24.0f * kTvDp; // ... or two, smaller
    static constexpr float kTvHeroBlurb = 14.0f * kTvDp;

    void tv_refresh_hero()
    {
        auto &app = rezka::App::instance();
        std::vector<rezka::App::HeroSlide> slides;
        {
            std::lock_guard<std::mutex> lock(app.catalog_mutex);
            slides = app.hero_slides;
        }
        tv_hero_version_ = app.hero_version.load();
        tv_hero_state_ = app.hero_state.load();
        const std::string shown = tv_hero_index_ < static_cast<int>(tv_hero_.size())
                                      ? tv_hero_[static_cast<std::size_t>(tv_hero_index_)].item.url
                                      : std::string();
        std::vector<TvHero> hero(slides.size());
        for (std::size_t i = 0; i < slides.size(); ++i)
        {
            const rezka::App::HeroSlide &s = slides[i];
            TvHero &h = hero[i];
            h.item = s.item;
            const std::string upper = tv_upper(s.title);
            if (tv_medium().measure(upper, kTvHeroTitle, 0.5f * kTvDp) <= tv_hero_text_w())
                h.title[0] = upper;
            else
                tv_two_lines(*tv_medium().font, upper, kTvHeroTitleSmall, tv_hero_text_w(), h.title);
            // A title on two lines leaves room for two lines of description.
            const std::size_t most = h.title[1].empty() ? 3 : 2;
            for (const std::string *part : {&s.year, &s.genre, &s.country})
                if (!part->empty())
                    h.facts.push_back(*part);
            h.quality = s.quality.empty() ? "HD" : s.quality;
            // Three lines at most, the last one ending in an ellipsis when text remains.
            std::vector<std::string> lines = tv_regular().font->wrap(s.description, kTvHeroBlurb, tv_hero_text_w());
            if (lines.size() > most)
            {
                std::string rest = lines[most - 1];
                for (std::size_t k = most; k < lines.size(); ++k)
                    rest += " " + lines[k];
                lines.resize(most);
                lines[most - 1] = tv_regular().font->fit(rest, kTvHeroBlurb, tv_hero_text_w());
            }
            h.blurb = std::move(lines);
            h.texture = s.texture;
            h.aspect = (s.tex_w > 0 && s.tex_h > 0) ? static_cast<float>(s.tex_w) / static_cast<float>(s.tex_h) : 0.0f;
        }
        tv_hero_ = std::move(hero);
        // The slide on screen stays on screen when the list is filled in.
        tv_hero_index_ = 0;
        for (int i = 0; i < static_cast<int>(tv_hero_.size()); ++i)
            if (tv_hero_[static_cast<std::size_t>(i)].item.url == shown)
                tv_hero_index_ = i;
        if (tv_hero_prev_ >= static_cast<int>(tv_hero_.size()))
            tv_hero_prev_ = -1;
        if (tv_zone_ == TvZone::hero && !tv_hero_shown())
            tv_zone_ = TvZone::chips;
    }
    // Shows another slide: the new one fades in over the old one.
    void tv_hero_show(int index)
    {
        const int count = static_cast<int>(tv_hero_.size());
        if (count == 0)
            return;
        index = ((index % count) + count) % count;
        if (index == tv_hero_index_)
            return;
        tv_hero_prev_ = tv_hero_index_;
        tv_hero_index_ = index;
        tv_hero_fade_ = 0.0f;
        tv_hero_timer_ = 0.0f;
    }

    void tv_ensure_icons()
    {
        auto &app = rezka::App::instance();
        if (tv_icons_ != 0 || tv_icons_tried_ || !app.renderer)
            return;
        tv_icons_tried_ = true;
        int w = 0, h = 0;
        std::uint8_t *rgba =
            rezka::TextureLoader::load_file_rgba(VK_EXAMPLE_ASSETS_DIR "tv/icons.png", &w, &h);
        if (rgba && w > 0 && h > 0)
            tv_icons_ = app.renderer->create_texture(w, h, rgba);
        if (rgba)
            rezka::TextureLoader::free_decoded_image(rgba);
        // The trailer tile's badge keeps its own colours
        rgba = rezka::TextureLoader::load_file_rgba(VK_EXAMPLE_ASSETS_DIR "tv/youtube.png", &w, &h);
        if (rgba && w > 0 && h > 0)
            tv_youtube_ = app.renderer->create_texture(w, h, rgba);
        if (rgba)
            rezka::TextureLoader::free_decoded_image(rgba);
        // The profile's illustrations
        static const char *const ill_files[5] = {
            VK_EXAMPLE_ASSETS_DIR "tv/ill-enter.png", VK_EXAMPLE_ASSETS_DIR "tv/ill-welcome_cats.png",
            VK_EXAMPLE_ASSETS_DIR "tv/ill-movie_night.png", VK_EXAMPLE_ASSETS_DIR "tv/ill-home_cinema.png",
            VK_EXAMPLE_ASSETS_DIR "tv/ill-void.png"};
        for (int i = 0; i < 5; ++i)
        {
            rgba = rezka::TextureLoader::load_file_rgba(ill_files[i], &w, &h);
            if (rgba && w > 0 && h > 0)
            {
                tv_ill_[i] = app.renderer->create_texture(w, h, rgba);
                tv_ill_aspect_[i] = static_cast<float>(w) / static_cast<float>(h);
            }
            if (rgba)
                rezka::TextureLoader::free_decoded_image(rgba);
        }
    }

    // The Material icons of the strip, in its order.
    enum TvIcon
    {
        kTvIconAccount,
        kTvIconSearch,
        kTvIconHome,
        kTvIconNew,
        kTvIconLibrary,
        kTvIconCampaign,
        kTvIconSettings,
        kTvIconCheck,
        kTvIconPlayArrow,
        kTvIconAccessTime,
        kTvIconBlock,
        kTvIconBookmark,
        kTvIconBookmarkBorder,
        kTvIconStackedBarChart,
        kTvIconQrCode2,
        kTvIconSchedule,
        kTvIconCalendarMonth,
        kTvIconStarRate,
        kTvIconInfoOutlined,
        kTvIconThumbUpAltOutlined,
        kTvIconThumbUpAlt,
        kTvIconOpenInNew,
        kTvIconLockOutlined,
        kTvIconLinkOutlined,
        kTvIconKeyboardArrowRight,
        kTvIconPause,
        kTvIconSkipPrevious,
        kTvIconSkipNext,
        kTvIconSubtitles,
        kTvIconSpeed,
        kTvIconHighQuality,
        kTvIconFullscreen,
        kTvIconFastRewind,
        kTvIconFastForward,
        // The settings screen's (Outlined, as the app's preferences)
        kTvIconPalette,
        kTvIconNightlight,
        kTvIconViewColumn,
        kTvIconThumbUp,
        kTvIconColorLens,
        kTvIconAnimation,
        kTvIconLanguage,
        kTvIconLockOpen,
        kTvIconDoNotDisturbOn,
        kTvIconPlayCircle,
        kTvIconForward10,
        kTvIconReplay10,
        kTvIconCached,
        kTvIconDownloading,
        kTvIconReplay,
        kTvIconTextFields,
        kTvIconTranslate,
        kTvIconBrowserUpdated,
        kTvIconCoffee,
        kTvIconRadioChecked,
        kTvIconRadioUnchecked,
        kTvIconCheckBox,
        kTvIconCheckBoxBlank,
        kTvIconRemove,
        kTvIconAdd,
        kTvIconSend,
        kTvIconCount,
    };
    void tv_icon(gfx::DrawList &list, int icon, float x, float y, float size, Color color) const
    {
        if (tv_icons_ == 0)
            return;
        const float u = 1.0f / static_cast<float>(kTvIconCount);
        list.image(tv_icons_, {x, y, size, size}, {u * static_cast<float>(icon), 0.0f, u, 1.0f},
                   color);
    }

    // ---- the settings: HDrezkaTV's Settings screen ---------------------------
    //
    // "Настройки" over two equal columns (P8 and R8 in the APK): on the left
    // the groups, Внешний вид, Подключение и предпочтения, Плеер, Субтитры and
    // О приложении; the one with the focus shows its preferences on the right.
    // A switch flips with Cross, a list opens its side sheet of choices on the
    // right edge, a number (columns, the seek steps) is changed with left and
    // right once Cross has taken it. Everything is kept at once
    // (App::update_settings, data/settings.cfg).

    enum StKind : int
    {
        kStSwitch,
        kStList,
        kStRange,
        kStMulti,
        kStAction, // Cross does something: О приложении's rows
    };
    // О приложении's QR cards.
    enum class StModal : std::uint8_t
    {
        none,
        telegram,
        donate,
    };
    enum StPref : int
    {
        kStBlack,
        kStColumns,
        kStVoices,
        kStRecommendations,
        kStClock,
        kStRainbow,
        kStAnimDrawer,
        kStSource,
        kStSsl,
        kStCountries,
        kStClearCache,
        kStQuality,
        kStAutoNext,
        kStStepForward,
        kStStepReplay,
        kStSpeed,
        kStBufferSize,
        kStBufferDuration,
        kStBackBuffer,
        kStSubtitlePlace,
        kStSubtitleSize,
        kStAppInfo,
        kStAutoUpdate,
        kStTelegram,
        kStDonate,
    };
    struct StDef
    {
        StPref key;
        StKind kind;
        const char *title;
        const char *summary; // a switch's line, or the line under a list's sheet title
        int icon;
    };
    struct StGroup
    {
        const char *title;
        int icon;
        const StDef *items;
        int count;
    };
    static constexpr StDef kStStyle[] = {
        {kStBlack, kStSwitch, "Черная тема", "Лучше подходит для OLED-дисплеев", kTvIconNightlight},
        {kStColumns, kStRange, "Столбцы в списках", nullptr, kTvIconViewColumn},
        {kStVoices, kStSwitch, "Показывать количество голосов", "Рядом с рейтингом будет показано количество голосов",
         kTvIconStarRate},
        {kStRecommendations, kStSwitch, "Рекомендации", "Показывать похожие фильмы внизу страницы", kTvIconThumbUp},
        {kStClock, kStSwitch, "Часы", "Показывать часы и дату на некоторых экранах", kTvIconAccessTime},
        {kStRainbow, kStSwitch, "Радужная полоса прогресса", "Радужная полоса прогресса в плеере", kTvIconColorLens},
        {kStAnimDrawer, kStList, "Анимация при открытии панели навигации", nullptr, kTvIconAnimation},
    };
    static constexpr StDef kStConnection[] = {
        {kStSource, kStList, "Источник", nullptr, kTvIconLanguage},
        {kStSsl, kStSwitch, "Включить обход SSL", "Внимание! Проверка сертификата безопасности будет отключена",
         kTvIconLockOpen},
        {kStCountries, kStMulti, "Фильтры контента", "Выберите страны, контент из которых Вы бы не хотели видеть",
         kTvIconDoNotDisturbOn},
        {kStClearCache, kStAction, "Очистить кэш", nullptr, kTvIconCached},
    };
    static constexpr StDef kStPlayer[] = {
        {kStQuality, kStList, "Качество по умолчанию", nullptr, kTvIconHighQuality},
        {kStAutoNext, kStSwitch, "Автоматически включать следующую серию", "Плеер начнет воспроизведение следующей серии",
         kTvIconSkipNext},
        {kStStepForward, kStRange, "Шаг быстрой перемотки вперед", nullptr, kTvIconForward10},
        {kStStepReplay, kStRange, "Шаг быстрой перемотки назад", nullptr, kTvIconReplay10},
        {kStSpeed, kStList, "Скорость воспроизведения по умолчанию", nullptr, kTvIconSpeed},
        {kStBufferSize, kStList, "Размер буфера потока",
         "Выбирайте не больше, чем размер свободной оперативной памяти вашего устройства. Если не знаете, оставьте автовыбор",
         kTvIconCached},
        {kStBufferDuration, kStList, "Длительность буфера",
         "Столько времени воспроизведения будет предзагружаться в буфер. Если выбран небольшой размер буфера потока, то "
         "возможно длительность будет меньше.",
         kTvIconDownloading},
        {kStBackBuffer, kStList, "Длительность буфера перемотки",
         "Столько времени будет оставаться в буфере для перемотки назад", kTvIconReplay},
    };
    static constexpr StDef kStSubtitle[] = {
        {kStSubtitlePlace, kStList, "Расположение субтитров", nullptr, kTvIconSubtitles},
        {kStSubtitleSize, kStList, "Размер шрифта субтитров", nullptr, kTvIconTextFields},
    };
    // О приложении, as the app's section is laid out (the version first, the
    // update check under it), with this title's own links: the version row
    // checks for an update, the channel and the donation open their QR codes.
    static constexpr StDef kStAboutItems[] = {
        {kStAppInfo, kStAction, "HDrezka homebrew for PS5", "Версия: 1.0.1", kTvIconInfoOutlined},
        {kStAutoUpdate, kStSwitch, "Автоматическая проверка обновлений", "Проверять наличие обновлений при запуске приложения",
         kTvIconBrowserUpdated},
        {kStTelegram, kStAction, "Telegram канал", "t.me/hdrezkaps5", kTvIconSend},
        {kStDonate, kStAction, "Поддержать автора", "Донат от 5$ в USDT (TRC20)", kTvIconCoffee},
    };
    static constexpr const char *kTelegramUrl = "https://t.me/hdrezkaps5";
    static constexpr const char *kDonateAddress = "TANuxMByEoucUwMfgHpjjqfCvWtuyyPCop";
    static constexpr int kStGroups = 5;
    static constexpr int kStAbout = 4;
    static StGroup st_group(int g)
    {
        switch (g)
        {
        case 0:
            return {"Внешний вид", kTvIconPalette, kStStyle, static_cast<int>(std::size(kStStyle))};
        case 1:
            return {"Подключение и предпочтения", kTvIconLanguage, kStConnection, static_cast<int>(std::size(kStConnection))};
        case 2:
            return {"Плеер", kTvIconPlayCircle, kStPlayer, static_cast<int>(std::size(kStPlayer))};
        case 3:
            return {"Субтитры", kTvIconSubtitles, kStSubtitle, static_cast<int>(std::size(kStSubtitle))};
        default:
            return {"О приложении", kTvIconInfoOutlined, kStAboutItems, static_cast<int>(std::size(kStAboutItems))};
        }
    }

    // ---- what each preference holds --------------------------------------------

    static bool *st_flag(rezka::AppSettings &s, StPref key)
    {
        switch (key)
        {
        case kStBlack: return &s.black_mode;
        case kStVoices: return &s.show_rating_voices;
        case kStRecommendations: return &s.show_recommendations;
        case kStClock: return &s.show_clock;
        case kStRainbow: return &s.rainbow_progress;
        case kStSsl: return &s.bypass_ssl;
        case kStAutoNext: return &s.auto_next_episode;
        case kStAutoUpdate: return &s.check_update;
        default: return nullptr;
        }
    }
    // A list preference's choices and the index it holds.
    static int *st_choice(rezka::AppSettings &s, StPref key, const rezka::SettingChoice *&choices, int &count)
    {
        using namespace rezka;
        switch (key)
        {
        case kStAnimDrawer: choices = kAnimNavDrawerChoices; count = setting_count(kAnimNavDrawerChoices); return &s.anim_nav_drawer;
        case kStSource: choices = kSourceChoices; count = setting_count(kSourceChoices); return &s.source;
        case kStQuality: choices = kVideoQualityChoices; count = setting_count(kVideoQualityChoices); return &s.video_quality;
        case kStSpeed: choices = kPlaySpeedChoices; count = setting_count(kPlaySpeedChoices); return &s.play_speed;
        case kStBufferSize: choices = kStreamBufferSizeChoices; count = setting_count(kStreamBufferSizeChoices); return &s.stream_buffer_size;
        case kStBufferDuration: choices = kBufferDurationChoices; count = setting_count(kBufferDurationChoices); return &s.buffer_duration;
        case kStBackBuffer: choices = kBackBufferDurationChoices; count = setting_count(kBackBufferDurationChoices); return &s.back_buffer_duration;
        case kStSubtitlePlace: choices = kSubtitleArrangementChoices; count = setting_count(kSubtitleArrangementChoices); return &s.subtitle_arrangement;
        case kStSubtitleSize: choices = kSubtitleFontSizeChoices; count = setting_count(kSubtitleFontSizeChoices); return &s.subtitle_font_size;
        default: choices = nullptr; count = 0; return nullptr;
        }
    }
    // A number preference, its range and step (IntRangePreference).
    static int *st_number(rezka::AppSettings &s, StPref key, int &lo, int &hi, int &step)
    {
        if (key == kStColumns)
        {
            lo = 5, hi = 9, step = 1;
            return &s.columns;
        }
        lo = 5, hi = 60, step = 5;
        if (key == kStStepForward)
            return &s.step_forward;
        if (key == kStStepReplay)
            return &s.step_replay;
        return nullptr;
    }
    // The line under a preference's title.
    std::string st_supporting(const StDef &def) const
    {
        rezka::AppSettings s = rezka::App::instance().settings();
        if (def.key == kStAppInfo)
            return std::string("Версия: ") + rezka::kAppVersion;
        if (def.key == kStClearCache)
        {
            auto &app = rezka::App::instance();
            if (app.cache_clearing.load())
                return "Очистка…";
            const double mb = static_cast<double>(app.cache_size_bytes()) / (1024.0 * 1024.0);
            char line[48];
            std::snprintf(line, sizeof(line), mb < 0.05 ? "Занято: 0 МБ" : "Занято: %.1f МБ", mb);
            return line;
        }
        if (def.kind == kStSwitch || def.kind == kStMulti || def.kind == kStAction)
        {
            return def.summary ? def.summary : "";
        }
        if (def.kind != kStList)
            return {};
        const rezka::SettingChoice *choices = nullptr;
        int count = 0;
        const int *at = st_choice(s, def.key, choices, count);
        std::string text = choices[std::clamp(*at, 0, count - 1)].label;
        if (def.key == kStSource)
        {
            if (s.source == 2)
                text = s.custom_mirror.empty() ? std::string("Свой домен не задан") : s.custom_mirror;
            const int check = rezka::App::instance().mirror_check.load();
            if (check == 1)
                text += " • проверка…";
            else if (check == 2)
                text += " • доступно";
            else if (check == 3)
                text += " • недоступно";
        }
        return text;
    }
    static float st_item_h(const StDef &def)
    {
        return (def.kind == kStRange ? 48.0f : 64.0f) * kTvDp;
    }

    // ---- input --------------------------------------------------------------

    void open_settings(app::Feedback &feedback)
    {
        settings_open_ = true;
        st_zone_ = 0;
        st_group_ = 0;
        st_item_ = 0;
        st_editing_ = false;
        st_sheet_ = false;
        st_scroll_.snap(0.0f);
        st_filter_dirty_ = false;
        st_modal_ = StModal::none;
        rezka::App::instance().mirror_check = 0;
        feedback.play(audio::Cue::open);
    }

    void st_close(app::Feedback &feedback)
    {
        settings_open_ = false;
        feedback.play(audio::Cue::back);
        // A new country filter shows on the list at once
        if (st_filter_dirty_)
        {
            st_filter_dirty_ = false;
            tv_load(tv_kind_);
        }
    }

    // Keeps a changed copy and redoes what depends on it on this screen.
    void st_commit(const rezka::AppSettings &next)
    {
        auto &app = rezka::App::instance();
        const rezka::AppSettings before = app.settings();
        app.update_settings(next);
        if (before.columns != next.columns)
        {
            // The cards are measured for their width: measure them again
            tv_cards_.clear();
            last_catalog_version_ = 0;
        }
        if (before.country_filter != next.country_filter)
            st_filter_dirty_ = true;
        if (before.source != next.source || before.custom_mirror != next.custom_mirror)
        {
            // "Переподключиться": the new mirror is checked, the list reloads once it answers
            app.check_mirror_async(next.mirror());
            st_reconnect_ = true;
        }
    }

    void update_settings(const InputFrame &input, float dt, app::Feedback &feedback)
    {
        auto &app = rezka::App::instance();
        // The keyboard just closed over "Свой домен" or the countries' search
        if (st_kb_ != 0)
        {
            const int which = st_kb_;
            st_kb_ = 0;
            if (which == 1)
            {
                std::string domain = st_text_;
                while (!domain.empty() && (domain.back() == '/' || domain.back() == ' '))
                    domain.pop_back();
                if (!domain.empty())
                {
                    if (domain.find("://") == std::string::npos)
                        domain = "https://" + domain;
                    rezka::AppSettings next = app.settings();
                    next.source = 2;
                    next.custom_mirror = domain;
                    st_commit(next);
                }
            }
            else
            {
                st_sheet_focus_ = 0;
                st_sheet_scroll_.snap(0.0f);
            }
        }
        if (st_reconnect_ && app.mirror_check.load() >= 2)
        {
            st_reconnect_ = false;
            if (app.mirror_check.load() == 2)
            {
                show_message("Зеркало доступно");
                tv_load(tv_kind_);
            }
            else
                show_message("Зеркало недоступно");
        }
        st_scroll_.update(dt, omega(16.0f));
        st_sheet_scroll_.update(dt, omega(16.0f));
        if (st_modal_ != StModal::none)
        {
            // The QR card: Circle (or Cross) closes it
            if (input.is_pressed(Action::back) || input.is_pressed(Action::confirm))
            {
                st_modal_ = StModal::none;
                feedback.play(audio::Cue::modal_close);
            }
            return;
        }
        if (st_sheet_)
        {
            update_settings_sheet(input, feedback);
            return;
        }

        const StGroup group = st_group(st_group_);
        if (st_zone_ == 0)
        {
            if (input.is_pressed(Action::back))
            {
                st_close(feedback);
                return;
            }
            if (input.nav == Direction::up || input.nav == Direction::down)
            {
                const int next = st_group_ + (input.nav == Direction::down ? 1 : -1);
                if (next < 0 || next >= kStGroups)
                    tv_refuse(feedback, input.nav_repeat);
                else
                {
                    // The focus on a group shows it, as the app's onFocusChanged does
                    st_group_ = next;
                    st_item_ = 0;
                    st_scroll_.snap(0.0f);
                    tv_tick(feedback, kWidth * 0.25f);
                }
            }
            else if (input.nav == Direction::right || input.is_pressed(Action::confirm))
            {
                if (group.count == 0)
                    tv_refuse(feedback, false);
                else
                {
                    st_zone_ = 1;
                    tv_tick(feedback, kWidth * 0.75f);
                }
            }
            else if (input.nav == Direction::left)
                tv_refuse(feedback, input.nav_repeat);
            return;
        }

        const StDef &def = group.items[std::clamp(st_item_, 0, group.count - 1)];
        rezka::AppSettings s = app.settings();
        if (st_editing_)
        {
            int lo = 0, hi = 0, step = 1;
            int *value = st_number(s, def.key, lo, hi, step);
            if (input.is_pressed(Action::back) || input.is_pressed(Action::confirm))
            {
                st_editing_ = false;
                feedback.play(audio::Cue::select);
            }
            else if (value && (input.nav == Direction::left || input.nav == Direction::right))
            {
                const int next = *value + (input.nav == Direction::right ? step : -step);
                if (next < lo || next > hi)
                    tv_refuse(feedback, input.nav_repeat);
                else
                {
                    *value = next;
                    st_commit(s);
                    tv_tick(feedback, kWidth * 0.75f);
                }
            }
            else if (input.nav == Direction::up || input.nav == Direction::down)
                tv_refuse(feedback, input.nav_repeat);
            return;
        }
        if (input.is_pressed(Action::back) || input.nav == Direction::left)
        {
            st_zone_ = 0;
            feedback.play(input.nav == Direction::left ? audio::Cue::focus : audio::Cue::back);
            return;
        }
        if (input.nav == Direction::up || input.nav == Direction::down)
        {
            const int next = st_item_ + (input.nav == Direction::down ? 1 : -1);
            if (next < 0 || next >= group.count)
                tv_refuse(feedback, input.nav_repeat);
            else
            {
                st_item_ = next;
                tv_tick(feedback, kWidth * 0.75f);
            }
            return;
        }
        if (input.nav == Direction::right)
        {
            tv_refuse(feedback, input.nav_repeat);
            return;
        }
        if (!input.is_pressed(Action::confirm))
            return;
        switch (def.kind)
        {
        case kStSwitch:
            if (bool *flag = st_flag(s, def.key))
            {
                *flag = !*flag;
                st_commit(s);
                feedback.play(audio::Cue::toggle);
            }
            break;
        case kStRange:
            st_editing_ = true;
            feedback.play(audio::Cue::select);
            break;
        case kStAction:
            if (def.key == kStAppInfo)
            {
                // The version: a check for an update, by hand
                app.check_updates_async(true);
                show_message("Проверка обновлений...");
                feedback.play(audio::Cue::select);
            }
            else if (def.key == kStClearCache)
            {
                // data/cache/ goes on a worker; the line under the title counts down to 0
                app.clear_cache_async();
                show_message("Кэш очищен");
                feedback.play(audio::Cue::select);
            }
            else
            {
                const bool telegram = def.key == kStTelegram;
                if (!qr::encode(telegram ? kTelegramUrl : kDonateAddress, st_qr_))
                {
                    tv_refuse(feedback, false);
                    break;
                }
                st_modal_ = telegram ? StModal::telegram : StModal::donate;
                feedback.play(audio::Cue::modal_open);
            }
            break;
        case kStList:
        case kStMulti:
        {
            st_sheet_ = true;
            st_sheet_key_ = def.key;
            st_text_.clear();
            st_sheet_scroll_.snap(0.0f);
            st_sheet_focus_ = 0;
            if (def.kind == kStList)
            {
                const rezka::SettingChoice *choices = nullptr;
                int count = 0;
                st_sheet_focus_ = std::clamp(*st_choice(s, def.key, choices, count), 0, count - 1);
            }
            feedback.play(audio::Cue::modal_open);
            break;
        }
        }
    }

    // The countries the search leaves, by their index in the app's list.
    std::vector<int> st_countries() const
    {
        std::vector<int> out;
        for (int i = 0; i < static_cast<int>(std::size(rezka::kCountryFilterChoices)); ++i)
        {
            const std::string name = rezka::kCountryFilterChoices[i];
            if (st_text_.empty() || utf8_lower(name).find(utf8_lower(st_text_)) != std::string::npos)
                out.push_back(i);
        }
        return out;
    }
    // Lower case for Latin and Cyrillic, enough to search a country's name.
    static std::string utf8_lower(const std::string &in)
    {
        std::string out;
        for (std::size_t i = 0; i < in.size(); ++i)
        {
            const unsigned char c = static_cast<unsigned char>(in[i]);
            if (c < 0x80)
                out += static_cast<char>(std::tolower(c));
            else if (c == 0xd0 && i + 1 < in.size())
            {
                unsigned char d = static_cast<unsigned char>(in[++i]);
                if (d >= 0x90 && d <= 0x9f) // А..П
                    out += static_cast<char>(0xd0), out += static_cast<char>(d + 0x20);
                else if (d >= 0xa0 && d <= 0xaf) // Р..Я
                    out += static_cast<char>(0xd1), out += static_cast<char>(d - 0x20);
                else if (d == 0x81) // Ё
                    out += static_cast<char>(0xd1), out += static_cast<char>(0x91);
                else
                    out += static_cast<char>(0xd0), out += static_cast<char>(d);
            }
            else
                out += static_cast<char>(c);
        }
        return out;
    }

    // The side sheet of a list (radio buttons) or of the countries (check boxes
    // under a search row: row 0 is the search, the countries follow).
    void update_settings_sheet(const InputFrame &input, app::Feedback &feedback)
    {
        auto &app = rezka::App::instance();
        rezka::AppSettings s = app.settings();
        const bool multi = st_sheet_key_ == kStCountries;
        const std::vector<int> countries = multi ? st_countries() : std::vector<int>{};
        const rezka::SettingChoice *choices = nullptr;
        int count = 0;
        int *chosen = multi ? nullptr : st_choice(s, static_cast<StPref>(st_sheet_key_), choices, count);
        if (multi)
            count = 1 + static_cast<int>(countries.size());
        if (input.is_pressed(Action::back) || input.nav == Direction::left)
        {
            st_sheet_ = false;
            feedback.play(audio::Cue::modal_close);
            return;
        }
        if (input.nav == Direction::up || input.nav == Direction::down)
        {
            const int next = st_sheet_focus_ + (input.nav == Direction::down ? 1 : -1);
            if (next < 0 || next >= count)
                tv_refuse(feedback, input.nav_repeat);
            else
            {
                st_sheet_focus_ = next;
                tv_tick(feedback, kWidth * 0.8f);
            }
            return;
        }
        if (!input.is_pressed(Action::confirm))
            return;
        if (multi)
        {
            if (st_sheet_focus_ == 0)
            {
                st_kb_ = 2;
                kb_begin(&st_text_, "Поиск", false, feedback);
                return;
            }
            const std::string name = rezka::kCountryFilterChoices[countries[static_cast<std::size_t>(st_sheet_focus_ - 1)]];
            if (!s.country_filter.erase(name))
                s.country_filter.insert(name);
            st_commit(s);
            feedback.play(audio::Cue::toggle);
            return;
        }
        if (st_sheet_key_ == kStSource && st_sheet_focus_ == 2)
        {
            // "Свой домен…": typed with the console's keyboard (the sheet of keys on a PC)
            st_sheet_ = false;
            st_kb_ = 1;
            st_text_ = s.custom_mirror;
            kb_begin(&st_text_, "Адрес зеркала HDRezka", false, feedback);
            return;
        }
        *chosen = st_sheet_focus_;
        st_commit(s);
        st_sheet_ = false;
        feedback.play(audio::Cue::select);
    }

    // ---- drawing ------------------------------------------------------------

    // A tv-material3 ListItem: 8 dp corners, the inverse surface under the
    // focus, the secondary container (40%) when selected.
    void st_list_item(gfx::DrawList &list, const Rect &r0, bool focused, bool selected, int icon, const std::string &title,
                      const std::string &supporting, float trailing_w = 0.0f) const
    {
        const float dp = kTvDp;
        const Rect r = scaled(r0, focused ? 1.03f : 1.0f);
        Color ink = tv_on_surface(), sub = tv_on_surface_variant();
        if (focused)
        {
            list.rounded_rect(r, 8.0f * dp, tv_on_surface());
            ink = tv_inverse_on_surface();
            sub = tv_inverse_on_surface().with_alpha(0.8f);
        }
        else if (selected)
        {
            list.rounded_rect(r, 8.0f * dp, tv_secondary_container().with_alpha(0.4f));
            ink = tv_on_secondary_container();
            sub = tv_on_secondary_container().with_alpha(0.8f);
        }
        float x = r.x + 16.0f * dp;
        if (icon >= 0)
        {
            tv_icon(list, icon, x, r.cy() - 12.0f * dp, 24.0f * dp, ink);
            x += 24.0f * dp + 16.0f * dp;
        }
        const float text_w = r.x + r.w - 16.0f * dp - trailing_w - x;
        if (supporting.empty())
            tv_text(list, tv_medium(), tv_medium().font->fit(title, 16.0f * dp, text_w), x, r.cy() - 12.0f * dp, 16.0f * dp,
                    24.0f * dp, ink, gfx::Align::left, 0.15f * dp);
        else
        {
            tv_text(list, tv_medium(), tv_medium().font->fit(title, 16.0f * dp, text_w), x, r.cy() - 21.0f * dp, 16.0f * dp,
                    24.0f * dp, ink, gfx::Align::left, 0.15f * dp);
            tv_text(list, tv_regular(), tv_regular().font->fit(supporting, 12.0f * dp, text_w), x, r.cy() + 3.0f * dp,
                    12.0f * dp, 16.0f * dp, sub, gfx::Align::left, 0.4f * dp);
        }
    }

    // A Material switch: a 52x32 dp track, the thumb 24 dp on and 16 off.
    void st_switch(gfx::DrawList &list, float right, float cy, bool on, bool focused) const
    {
        const float dp = kTvDp;
        const Rect track{right - 52.0f * dp, cy - 16.0f * dp, 52.0f * dp, 32.0f * dp};
        if (on)
        {
            list.rounded_rect(track, 16.0f * dp, tv_primary());
            list.circle(track.x + track.w - 16.0f * dp, cy, 12.0f * dp, tv_on_primary());
            tv_icon(list, kTvIconCheck, track.x + track.w - 24.0f * dp, cy - 8.0f * dp, 16.0f * dp, tv_primary());
        }
        else
        {
            const Color outline = focused ? tv_inverse_on_surface().with_alpha(0.7f) : tv_border();
            list.bordered_rect(track, 16.0f * dp, tv_surface_variant().with_alpha(focused ? 0.15f : 1.0f), 2.0f * dp, outline);
            list.circle(track.x + 16.0f * dp, cy, 8.0f * dp, outline);
        }
    }

    void draw_settings(gfx::DrawList &list) const
    {
        if (!settings_open_)
            return;
        const float dp = kTvDp;
        auto &app = rezka::App::instance();
        rezka::AppSettings s = app.settings();
        list.rounded_rect({0, 0, kWidth, kHeight}, 0.0f, tv_surface());
        // Text("Настройки"): headlineSmall, 24 dp from the top, 48 from the side
        tv_text(list, tv_regular(), "Настройки", 48.0f * dp, 24.0f * dp, 24.0f * dp, 32.0f * dp, tv_on_surface());

        // Row: 8 dp below, 24 from the sides, two columns 8 dp apart, each a list padded 16
        const float top = 64.0f * dp;
        const float col_w = (kWidth - 48.0f * dp - 8.0f * dp) * 0.5f;
        const float left_x = 24.0f * dp + 16.0f * dp;
        const float right_x = 24.0f * dp + col_w + 8.0f * dp + 16.0f * dp;
        const float item_w = col_w - 32.0f * dp;

        for (int g = 0; g < kStGroups; ++g)
        {
            const StGroup group = st_group(g);
            const Rect r{left_x, top + 16.0f * dp + static_cast<float>(g) * 52.0f * dp, item_w, 48.0f * dp};
            st_list_item(list, r, st_zone_ == 0 && g == st_group_, g == st_group_, group.icon, group.title, "");
        }

        const StGroup group = st_group(st_group_);
        {
            // The focused row kept in view: the list scrolls by whole rows
            float y = 16.0f * dp, focus_top = 0.0f, focus_bottom = 0.0f;
            for (int i = 0; i < group.count; ++i)
            {
                if (i == st_item_)
                    focus_top = y, focus_bottom = y + st_item_h(group.items[i]);
                y += st_item_h(group.items[i]) + 4.0f * dp;
            }
            const float view = kHeight - top;
            const float content = y + 12.0f * dp;
            float want = st_scroll_.target;
            if (focus_bottom + 16.0f * dp - want > view)
                want = focus_bottom + 16.0f * dp - view;
            if (focus_top - 16.0f * dp < want)
                want = focus_top - 16.0f * dp;
            want = std::clamp(want, 0.0f, std::max(0.0f, content - view));
            st_scroll_.target = want;

            list.push_clip({right_x - 16.0f * dp, top, col_w, view});
            y = top + 16.0f * dp - st_scroll_.value;
            for (int i = 0; i < group.count; ++i)
            {
                const StDef &def = group.items[i];
                const float h = st_item_h(def);
                const Rect r{right_x, y, item_w, h};
                const bool focused = st_zone_ == 1 && i == st_item_;
                float trailing = 0.0f;
                if (def.kind == kStSwitch)
                    trailing = 60.0f * dp;
                else if (def.kind == kStRange)
                    trailing = 96.0f * dp;
                st_list_item(list, r, focused, false, def.icon, def.title, st_supporting(def), trailing);
                const Rect rs = scaled(r, focused ? 1.03f : 1.0f);
                const Color ink = focused ? tv_inverse_on_surface() : tv_on_surface();
                if (def.kind == kStSwitch)
                {
                    bool *flag = st_flag(s, def.key);
                    st_switch(list, rs.x + rs.w - 16.0f * dp, rs.cy(), flag && *flag, focused);
                }
                else if (def.kind == kStRange)
                {
                    int lo = 0, hi = 0, step = 1;
                    const int *value = st_number(s, def.key, lo, hi, step);
                    const float cx = rs.x + rs.w - 16.0f * dp - 40.0f * dp;
                    const bool editing = focused && st_editing_;
                    if (editing)
                        list.rounded_rect({cx - 44.0f * dp, rs.cy() - 16.0f * dp, 88.0f * dp, 32.0f * dp}, 16.0f * dp,
                                          tv_primary());
                    const Color num = editing ? tv_on_primary() : ink;
                    tv_icon(list, kTvIconRemove, cx - 40.0f * dp, rs.cy() - 10.0f * dp, 20.0f * dp,
                            num.with_alpha(value && *value > lo ? 1.0f : 0.35f));
                    tv_icon(list, kTvIconAdd, cx + 20.0f * dp, rs.cy() - 10.0f * dp, 20.0f * dp,
                            num.with_alpha(value && *value < hi ? 1.0f : 0.35f));
                    tv_text(list, tv_medium(), value ? std::to_string(*value) : "", cx, rs.cy() - 12.0f * dp, 16.0f * dp,
                            24.0f * dp, num, gfx::Align::center, 0.1f * dp);
                }
                y += h + 4.0f * dp;
            }
            list.pop_clip();
        }

        if (st_sheet_)
            draw_settings_sheet(list, s);
        st_draw_modal(list);
        kb_draw(list);
    }

    // The QR code on a white plate (a phone's camera reads dark on light): the
    // quiet zone of four modules round it.
    void draw_qr_plate(gfx::DrawList &list, const qr::Code &code, const Rect &plate) const
    {
        const float dp = kTvDp;
        list.rounded_rect(plate, 12.0f * dp, kWhite);
        const int n = code.size;
        if (n <= 0)
            return;
        const float module = plate.w / static_cast<float>(n + 8);
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x)
                if (code.at(x, y))
                    list.rounded_rect({plate.x + static_cast<float>(x + 4) * module, plate.y + static_cast<float>(y + 4) * module,
                                       module + 0.6f, module + 0.6f},
                                      0.0f, Color::rgb(0x191c1e));
    }

    // ---- the updater's dialog ------------------------------------------------
    //
    // "Доступно обновление!": the new version and its notes, Обновить and
    // Пропустить; then the download's progress, then what came of it. A check
    // by hand that finds nothing says so in a toast.

    void upd_poll()
    {
        auto &app = rezka::App::instance();
        const std::uint32_t version = app.update_version.load();
        if (version == upd_version_)
            return;
        upd_version_ = version;
        std::string message;
        {
            std::lock_guard<std::mutex> lock(app.catalog_mutex);
            message = app.update_message;
        }
        const bool manual = app.update_manual.load();
        switch (app.update_status())
        {
        case rezka::UpdateState::available:
            if (manual || !app.update_skipped.load())
            {
                upd_open_ = true;
                upd_focus_ = 0;
            }
            break;
        case rezka::UpdateState::latest:
        case rezka::UpdateState::check_failed:
            if (manual)
                show_message(message);
            break;
        case rezka::UpdateState::downloading:
        case rezka::UpdateState::installed:
        case rezka::UpdateState::install_failed:
            upd_open_ = true;
            upd_focus_ = 0;
            break;
        default:
            break;
        }
    }

    void upd_update(const InputFrame &input, app::Feedback &feedback)
    {
        auto &app = rezka::App::instance();
        const rezka::UpdateState state = app.update_status();
        if (state == rezka::UpdateState::downloading)
            return; // the download runs to its end
        if (state != rezka::UpdateState::available)
        {
            // Installed or failed: one button, Ок
            if (input.is_pressed(Action::confirm) || input.is_pressed(Action::back))
            {
                upd_open_ = false;
                feedback.play(audio::Cue::modal_close);
            }
            return;
        }
        if (input.nav == Direction::left || input.nav == Direction::right)
        {
            const int next = input.nav == Direction::right ? 1 : 0;
            if (next == upd_focus_)
                tv_refuse(feedback, input.nav_repeat);
            else
            {
                upd_focus_ = next;
                tv_tick(feedback, kWidth * (next ? 0.55f : 0.45f));
            }
            return;
        }
        const bool skip = input.is_pressed(Action::back) || (input.is_pressed(Action::confirm) && upd_focus_ == 1);
        if (skip)
        {
            // Пропустить: not asked again this session
            app.update_skipped = true;
            app.update_state = static_cast<int>(rezka::UpdateState::idle);
            upd_open_ = false;
            feedback.play(audio::Cue::modal_close);
            return;
        }
        if (input.is_pressed(Action::confirm))
        {
            app.download_update_async();
            feedback.play(audio::Cue::select);
        }
    }

    // A dialog button: a pill, light under the focus.
    void upd_button(gfx::DrawList &list, const Rect &r, const char *label, bool focused, bool primary) const
    {
        const float dp = kTvDp;
        const Rect g = focused ? scaled(r, 1.05f) : r;
        list.rounded_rect(g, g.h * 0.5f,
                          focused ? tv_on_surface() : primary ? tv_primary_container() : tv_surface_variant().with_alpha(0.5f));
        tv_text(list, tv_medium(), label, g.cx(), g.cy() - 10.0f * dp, 14.0f * dp, 20.0f * dp,
                focused ? tv_inverse_on_surface() : primary ? tv_on_primary_container() : tv_on_surface(), gfx::Align::center,
                0.1f * dp);
    }

    void upd_draw(gfx::DrawList &list) const
    {
        if (!upd_open_)
            return;
        const float dp = kTvDp;
        auto &app = rezka::App::instance();
        const rezka::UpdateState state = app.update_status();
        rezka::UpdateInfo info;
        std::string message;
        {
            std::lock_guard<std::mutex> lock(app.catalog_mutex);
            info = app.update_info;
            message = app.update_message;
        }
        list.rounded_rect({0.0f, 0.0f, kWidth, kHeight}, 0.0f, kBlack.with_alpha(0.6f));
        const float w = 560.0f * dp;
        const float text_w = w - 48.0f * dp;
        // The notes, at most ten lines
        std::vector<std::string> notes;
        if (state == rezka::UpdateState::available)
            for (const std::string &part : tv_regular().font->wrap(info.notes, 14.0f * dp, text_w))
                if (notes.size() < 10)
                    notes.push_back(part);
        float h = 24.0f * dp + 36.0f * dp + 28.0f * dp + 24.0f * dp + 40.0f * dp + 24.0f * dp;
        if (!notes.empty())
            h += 12.0f * dp + static_cast<float>(notes.size()) * 20.0f * dp;
        if (state == rezka::UpdateState::downloading)
            h += 8.0f * dp;
        const Rect card{(kWidth - w) * 0.5f, (kHeight - h) * 0.5f, w, h};
        list.bordered_rect(card, 28.0f * dp, tv_surface(), 1.0f * dp, tv_border_variant());
        const float x = card.x + 24.0f * dp;
        float y = card.y + 24.0f * dp;
        tv_icon(list, kTvIconBrowserUpdated, x, y + 4.0f * dp, 28.0f * dp, tv_primary());
        const char *title = state == rezka::UpdateState::installed        ? "Обновление установлено"
                            : state == rezka::UpdateState::install_failed ? "Обновление не установлено"
                                                                          : "Доступно обновление!";
        tv_text(list, tv_regular(), title, x + 40.0f * dp, y, 24.0f * dp, 36.0f * dp, tv_on_surface());
        y += 36.0f * dp;
        const std::string version = "Версия " + info.version + " (у вас " + rezka::kAppVersion + ")";
        tv_text(list, tv_medium(), version, x, y, 14.0f * dp, 28.0f * dp, tv_primary(), gfx::Align::left, 0.1f * dp);
        y += 28.0f * dp;
        if (!notes.empty())
        {
            y += 12.0f * dp;
            for (const std::string &line : notes)
            {
                tv_text(list, tv_regular(), line, x, y, 14.0f * dp, 20.0f * dp, tv_on_surface_variant());
                y += 20.0f * dp;
            }
        }
        y += 24.0f * dp;
        const float buttons_y = card.y + card.h - 24.0f * dp - 40.0f * dp;
        switch (state)
        {
        case rezka::UpdateState::available:
            upd_button(list, {card.x + card.w - 24.0f * dp - 2.0f * 148.0f * dp - 12.0f * dp, buttons_y, 148.0f * dp, 40.0f * dp},
                       "Обновить", upd_focus_ == 0, true);
            upd_button(list, {card.x + card.w - 24.0f * dp - 148.0f * dp, buttons_y, 148.0f * dp, 40.0f * dp}, "Пропустить",
                       upd_focus_ == 1, false);
            break;
        case rezka::UpdateState::downloading:
        {
            const float p = static_cast<float>(app.update_progress.load()) / 1000.0f;
            char text[64];
            std::snprintf(text, sizeof(text), "Скачивание обновления... %d%%", static_cast<int>(p * 100.0f + 0.5f));
            tv_text(list, tv_regular(), text, x, buttons_y - 4.0f * dp, 14.0f * dp, 20.0f * dp, tv_on_surface());
            const Rect bar{x, buttons_y + 28.0f * dp, text_w, 8.0f * dp};
            list.rounded_rect(bar, 4.0f * dp, tv_surface_variant());
            list.rounded_rect({bar.x, bar.y, std::max(8.0f * dp, bar.w * p), bar.h}, 4.0f * dp, tv_primary());
            break;
        }
        default:
            tv_text(list, tv_regular(), tv_regular().font->fit(message, 14.0f * dp, text_w - 140.0f * dp), x,
                    buttons_y + 10.0f * dp, 14.0f * dp, 20.0f * dp,
                    state == rezka::UpdateState::installed ? tv_on_surface() : tv_error());
            upd_button(list, {card.x + card.w - 24.0f * dp - 120.0f * dp, buttons_y, 120.0f * dp, 40.0f * dp}, "Ок", true,
                       true);
            break;
        }
    }

    // О приложении's cards: the project's channel and the donation, each a
    // dialog of the app's (corners 28 dp, the surface, a hairline border) over
    // a dimmed screen: the title, the QR code, what it holds and how to close.
    void st_draw_modal(gfx::DrawList &list) const
    {
        if (st_modal_ == StModal::none)
            return;
        const float dp = kTvDp;
        const bool donate = st_modal_ == StModal::donate;
        list.rounded_rect({0.0f, 0.0f, kWidth, kHeight}, 0.0f, kBlack.with_alpha(0.6f));
        const float w = 480.0f * dp;
        const float qr_side = 240.0f * dp;
        const float h = (donate ? 452.0f : 404.0f) * dp;
        const Rect card{(kWidth - w) * 0.5f, (kHeight - h) * 0.5f, w, h};
        list.bordered_rect(card, 28.0f * dp, tv_surface(), 1.0f * dp, tv_border_variant());
        float y = card.y + 24.0f * dp;
        tv_text(list, tv_regular(), donate ? "Поддержать автора" : "Telegram проекта", card.cx(), y, 24.0f * dp, 32.0f * dp,
                tv_on_surface(), gfx::Align::center);
        y += 36.0f * dp;
        if (donate)
        {
            tv_text(list, tv_regular(), "USDT (сеть TRC20) — на любую сумму от 5$", card.cx(), y, 14.0f * dp, 20.0f * dp,
                    tv_on_surface_variant(), gfx::Align::center);
            y += 28.0f * dp;
        }
        else
            y += 4.0f * dp;
        draw_qr_plate(list, st_qr_, {card.cx() - qr_side * 0.5f, y, qr_side, qr_side});
        y += qr_side + 20.0f * dp;
        if (donate)
        {
            // The address whole, large enough to read and to type
            tv_text(list, tv_medium(), tv_medium().font->fit(kDonateAddress, 18.0f * dp, w - 32.0f * dp), card.cx(), y,
                    18.0f * dp, 28.0f * dp, tv_on_surface(), gfx::Align::center, 0.3f * dp);
            y += 32.0f * dp;
            tv_text(list, tv_regular(), "Адрес кошелька USDT TRC20", card.cx(), y, 12.0f * dp, 16.0f * dp,
                    tv_on_surface_variant(), gfx::Align::center);
        }
        else
            tv_text(list, tv_medium(), kTelegramUrl, card.cx(), y, 18.0f * dp, 28.0f * dp, tv_primary(), gfx::Align::center,
                    0.2f * dp);
        tv_text(list, tv_regular(), "Нажмите Кружок для закрытия", card.cx(), card.y + card.h - 40.0f * dp, 14.0f * dp,
                20.0f * dp, tv_on_surface_variant().with_alpha(0.8f), gfx::Align::center);
    }

    void draw_settings_sheet(gfx::DrawList &list, rezka::AppSettings &s) const
    {
        const float dp = kTvDp;
        list.rounded_rect({0.0f, 0.0f, kWidth, kHeight}, 0.0f, kBlack.with_alpha(0.6f));
        // The dialog's window at the end: 24 dp from the top, bottom and right, corners 28
        const float w = 400.0f * dp;
        const Rect card{kWidth - 24.0f * dp - w, 24.0f * dp, w, kHeight - 48.0f * dp};
        list.bordered_rect(card, 28.0f * dp, tv_surface(), 1.0f * dp, tv_border_variant());
        const StPref key = static_cast<StPref>(st_sheet_key_);
        const StDef *def = nullptr;
        for (int g = 0; g < kStAbout && !def; ++g)
        {
            const StGroup group = st_group(g);
            for (int i = 0; i < group.count; ++i)
                if (group.items[i].key == key)
                    def = &group.items[i];
        }
        if (!def)
            return;
        // Title: titleLarge, 20 dp in, 20 from the top, 8 below
        float y = card.y + 20.0f * dp;
        tv_text(list, tv_regular(), tv_regular().font->fit(def->title, 22.0f * dp, w - 40.0f * dp), card.x + 20.0f * dp, y,
                22.0f * dp, 28.0f * dp, tv_on_surface());
        y += 28.0f * dp + 8.0f * dp;
        if (def->summary && def->kind == kStList)
        {
            for (const std::string &line : tv_regular().font->wrap(def->summary, 14.0f * dp, w - 40.0f * dp))
            {
                tv_text(list, tv_regular(), line, card.x + 20.0f * dp, y, 14.0f * dp, 20.0f * dp, tv_on_surface_variant());
                y += 20.0f * dp;
            }
            y += 8.0f * dp;
        }

        const bool multi = key == kStCountries;
        std::vector<std::string> labels;
        std::vector<bool> checked;
        std::vector<std::string> extra;
        if (multi)
        {
            labels.push_back(st_text_.empty() ? std::string("Поиск") : st_text_);
            checked.push_back(false);
            for (const int i : st_countries())
            {
                labels.push_back(rezka::kCountryFilterChoices[i]);
                checked.push_back(s.country_filter.count(rezka::kCountryFilterChoices[i]) != 0);
            }
        }
        else
        {
            const rezka::SettingChoice *choices = nullptr;
            int count = 0;
            const int *at = st_choice(s, key, choices, count);
            for (int i = 0; i < count; ++i)
            {
                labels.push_back(choices[i].label);
                checked.push_back(i == *at);
            }
            if (key == kStSource && !s.custom_mirror.empty())
                extra = {"", "", s.custom_mirror};
        }

        // The list: 20 dp in, 8 from the top, 4 between rows, a 48 dp row
        const float list_top = y;
        const float view = card.y + card.h - 20.0f * dp - list_top;
        const float pitch = 52.0f * dp;
        const float focus_y = static_cast<float>(st_sheet_focus_) * pitch;
        float want = st_sheet_scroll_.target;
        if (focus_y + pitch - want > view)
            want = focus_y + pitch - view;
        if (focus_y < want)
            want = focus_y;
        st_sheet_scroll_.target = std::max(0.0f, want);
        list.push_clip({card.x, list_top - 4.0f * dp, card.w, view + 8.0f * dp});
        for (int i = 0; i < static_cast<int>(labels.size()); ++i)
        {
            const float ry = list_top + static_cast<float>(i) * pitch - st_sheet_scroll_.value;
            if (ry + pitch < list_top - 8.0f * dp || ry > list_top + view + 8.0f * dp)
                continue;
            const Rect r{card.x + 20.0f * dp, ry, w - 40.0f * dp, 48.0f * dp};
            const bool focused = i == st_sheet_focus_;
            const bool search = multi && i == 0;
            if (search)
            {
                // OutlinedTextField: the search through the countries
                list.bordered_rect(r, 8.0f * dp, kClear, (focused ? 2.0f : 1.0f) * dp, focused ? tv_primary() : tv_border());
                tv_icon(list, kTvIconSearch, r.x + 12.0f * dp, r.cy() - 12.0f * dp, 24.0f * dp, tv_on_surface_variant());
                tv_text(list, tv_regular(), labels[0], r.x + 48.0f * dp, r.cy() - 12.0f * dp, 16.0f * dp, 24.0f * dp,
                        st_text_.empty() ? tv_on_surface_variant() : tv_on_surface());
                continue;
            }
            const Color ink = focused ? tv_inverse_on_surface() : tv_on_surface();
            if (focused)
                list.rounded_rect(scaled(r, 1.03f), 8.0f * dp, tv_on_surface());
            const int icon = multi ? (checked[i] ? kTvIconCheckBox : kTvIconCheckBoxBlank)
                                   : (checked[i] ? kTvIconRadioChecked : kTvIconRadioUnchecked);
            const Color mark = checked[i] ? (focused ? tv_inverse_primary() : tv_primary()) : ink;
            tv_icon(list, icon, r.x + 12.0f * dp, r.cy() - 12.0f * dp, 24.0f * dp, mark);
            const std::string sub = i < static_cast<int>(extra.size()) ? extra[static_cast<std::size_t>(i)] : std::string();
            if (sub.empty())
                tv_text(list, tv_medium(), tv_medium().font->fit(labels[i], 16.0f * dp, r.w - 64.0f * dp), r.x + 48.0f * dp,
                        r.cy() - 12.0f * dp, 16.0f * dp, 24.0f * dp, ink, gfx::Align::left, 0.15f * dp);
            else
            {
                tv_text(list, tv_medium(), labels[i], r.x + 48.0f * dp, r.cy() - 20.0f * dp, 16.0f * dp, 22.0f * dp, ink,
                        gfx::Align::left, 0.15f * dp);
                tv_text(list, tv_regular(), tv_regular().font->fit(sub, 12.0f * dp, r.w - 64.0f * dp), r.x + 48.0f * dp,
                        r.cy() + 2.0f * dp, 12.0f * dp, 16.0f * dp, focused ? ink.with_alpha(0.8f) : tv_on_surface_variant());
            }
        }
        list.pop_clip();
    }

    // ---- layout ------------------------------------------------------------

    // The page's left edge: it slides aside as the drawer opens.
    float tv_page_x() const
    {
        // "Анимация при открытии панели навигации": the page slides aside, or
        // (Сжатие экрана) stays and shrinks towards its right edge.
        if (rezka::App::instance().settings().anim_nav_drawer == 1)
            return kTvRail;
        return kTvRail + kTvSlide * tv_open_.value;
    }
    static float tv_drawer_item_y(int item)
    {
        // Seven items centred in the height, the profile set apart above and
        // the settings below by 24 dp.
        const float gap = 24.0f * kTvDp;
        const float total = kTvDrawerItems * kTvItem + 2.0f * gap;
        float y = (kHeight - total) * 0.5f + static_cast<float>(item) * kTvItem;
        if (item >= 1)
            y += gap;
        if (item == kTvDrawerItems - 1)
            y += gap;
        return y;
    }
    // Tab rectangles on the page (x from the page's left edge).
    void tv_tab_layout(Rect out[kTvTabs]) const
    {
        float x = kTvPad;
        for (int i = 0; i < kTvTabs; ++i)
        {
            const float w = tv_medium().measure(kTvTabNames[i], 16.0f * kTvDp, 0.2f * kTvDp) +
                            32.0f * kTvDp;
            out[i] = {x, kTvTabsY, w, kTvTabH};
            x += w + 12.0f * kTvDp;
        }
    }
    void tv_chip_layout(Rect out[kTvTypes]) const
    {
        float x = kTvPad;
        for (int i = 0; i < kTvTypes; ++i)
        {
            const bool selected = i == tv_type_;
            // A selected chip leads with a check mark: 12 before it, 8 after.
            float w = tv_medium().measure(kTvTypeNames[i], 14.0f * kTvDp, 0.1f * kTvDp) +
                      (selected ? 12.0f + 18.0f + 8.0f + 16.0f : 32.0f) * kTvDp;
            out[i] = {x, kTvChipsY, w, kTvChipH};
            x += w + 12.0f * kTvDp;
        }
    }
    // A card's place in the grid's content (before scrolling), from the page's left edge.
    // On the home list the banner comes first in that content.
    Rect tv_card_rect(int index) const
    {
        const int col = index % tv_columns();
        const int row = index / tv_columns();
        return {kTvPad + static_cast<float>(col) * (tv_card_w() + kTvGap),
                kTvGridPad + tv_grid_offset() + static_cast<float>(row) * tv_row_pitch(), tv_card_w(),
                tv_card_h()};
    }
    // The banner: Главная alone (the home tabs, not Новинки, Анонсы or a
    // search), while its slides are loading or there.
    bool tv_hero_shown() const
    {
        return home_layout_ && tv_kind_ == 0 && tv_hero_state_ != 3;
    }
    float tv_grid_offset() const
    {
        return tv_hero_shown() ? kTvHeroH + kTvHeroGap : 0.0f;
    }
    // The banner in the grid's content, before scrolling.
    static Rect tv_hero_rect()
    {
        return {kTvPad, kTvGridPad, kWidth - kTvRail - 2.0f * kTvPad, kTvHeroH};
    }
    // Compose's bring-into-view on a TV: the focused row's top rests 30% down
    // the viewport, as far as the content allows.
    float tv_scroll_target() const
    {
        const int count = static_cast<int>(tv_cards_.size());
        if (tv_zone_ == TvZone::hero)
            return 0.0f;
        if (tv_zone_ != TvZone::grid || count == 0)
            return tv_scroll_.target;
        const float view = kHeight - kTvGridTop;
        const int rows = (count + tv_columns() - 1) / tv_columns();
        const float content =
            2.0f * kTvGridPad + tv_grid_offset() + static_cast<float>(rows) * tv_row_pitch() - kTvGap;
        const float top = tv_card_rect(tv_focus_).y;
        return std::clamp(top - 0.3f * view, 0.0f, std::max(0.0f, content - view));
    }

    // ---- input -------------------------------------------------------------

    void tv_tick(app::Feedback &feedback, float x) const
    {
        feedback.play(audio::Cue::focus, 1.0f, ui::pan_for_x(x));
    }
    void tv_refuse(app::Feedback &feedback, bool repeat) const
    {
        if (repeat)
            return;
        feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
        feedback.rumble(0.25f, 0.05f);
    }
    void tv_open_drawer(app::Feedback &feedback)
    {
        tv_return_ = tv_zone_;
        tv_zone_ = TvZone::drawer;
        tv_drawer_focus_ = tv_kind_ == 1 ? 3 : tv_kind_ == 2 ? 5 : kTvHomeItem;
        feedback.play(audio::Cue::open);
    }
    void tv_close_drawer()
    {
        const bool has_cards = !tv_cards_.empty();
        TvZone to = tv_return_;
        if (!home_layout_)
        {
            // Search results have the grid alone to go back to
            if (!has_cards)
                return;
            to = TvZone::grid;
        }
        else
        {
            if (to == TvZone::drawer)
                to = TvZone::tabs;
            if (to == TvZone::grid && !has_cards)
                to = TvZone::chips;
            if (to == TvZone::tabs && tv_kind_ != 0)
                to = TvZone::chips;
            if (to == TvZone::hero && !tv_hero_focusable())
                to = TvZone::chips;
        }
        tv_zone_ = to;
    }
    // Starts over with a list: the home tabs, Новинки or Анонсы.
    void tv_load(int kind)
    {
        tv_kind_ = kind;
        tv_tab_delay_ = -1.0f;
        tv_focus_ = 0;
        tv_scroll_.snap(0.0f);
        tv_cards_.clear();
        home_layout_ = true;
        rezka::App::instance().load_movie_list_async(tv_tab_, tv_type_, tv_kind_);
    }
    void tv_open_profile(app::Feedback &feedback)
    {
        profile_open_ = true;
        search_open_ = false;
        page_open_ = false;
        key_lang_ = 0; // ENG for email/password
        key_row_ = 0;
        key_col_ = 0;
        login_field_ = 0;
        login_fields_focus_ = true;
        profile_view_ = 0;
        profile_focus_ = 0;
        profile_list_focus_ = 0;
        {
            auto &app = rezka::App::instance();
            bool logged_in = false;
            {
                std::lock_guard<std::mutex> lock(app.catalog_mutex);
                logged_in = app.account.logged_in;
            }
            profile_was_logged_ = logged_in;
            // As the app's screens do, the lists are asked for each time: what
            // was watched or bookmarked since is the site's to tell. What was
            // loaded before shows meanwhile.
            if (logged_in)
                app.load_account_lists_async();
            // The code of the site, for signing in on a phone
            if (!logged_in)
                qr::encode(app.client.get_mirror() + "/", profile_qr_);
        }
        feedback.play(audio::Cue::open);
    }
    void tv_open_search(app::Feedback &feedback)
    {
        search_open_ = true;
        profile_open_ = false;
        page_open_ = false;
        feedback.play(audio::Cue::open);
    }

    void tv_update_drawer(const InputFrame &input, app::Feedback &feedback)
    {
        if (input.nav == Direction::up || input.nav == Direction::down)
        {
            const int next = tv_drawer_focus_ + (input.nav == Direction::down ? 1 : -1);
            if (next < 0 || next >= kTvDrawerItems)
                tv_refuse(feedback, input.nav_repeat);
            else
            {
                tv_drawer_focus_ = next;
                tv_tick(feedback, kTvRail);
            }
        }
        else if (input.nav == Direction::right || input.is_pressed(Action::back))
        {
            tv_close_drawer();
            feedback.play(audio::Cue::back);
        }
        else if (input.nav == Direction::left)
            tv_refuse(feedback, input.nav_repeat);

        if (!input.is_pressed(Action::confirm) || tv_zone_ != TvZone::drawer)
            return;
        switch (tv_drawer_focus_)
        {
        case 0:
            tv_close_drawer();
            tv_open_profile(feedback);
            break;
        case 1:
            tv_close_drawer();
            tv_open_search(feedback);
            break;
        case 2:
        case 3:
        case 5:
        {
            const int kind = tv_drawer_focus_ == 3 ? 1 : tv_drawer_focus_ == 5 ? 2 : 0;
            if (kind != tv_kind_ || !home_layout_)
                tv_load(kind);
            tv_return_ = kind == 0 ? TvZone::tabs : TvZone::chips;
            tv_close_drawer();
            feedback.play(audio::Cue::select);
            break;
        }
        case 6:
            tv_close_drawer();
            open_settings(feedback);
            break;
        default:
            // Подборки has no screen here yet; the app's own word for that.
            show_message("В разработке");
            feedback.play(audio::Cue::select);
            break;
        }
    }

    bool tv_hero_focusable() const
    {
        return tv_hero_shown() && !tv_hero_.empty();
    }
    // Смотреть opens the title's page, Мой список puts it in the bookmarks or takes it out.
    void tv_hero_press(app::Feedback &feedback)
    {
        tv_hero_timer_ = 0.0f;
        if (tv_hero_index_ >= static_cast<int>(tv_hero_.size()))
        {
            tv_refuse(feedback, false);
            return;
        }
        const TvHero &h = tv_hero_[static_cast<std::size_t>(tv_hero_index_)];
        auto &app = rezka::App::instance();
        // Without an account, Мой список is this console's own bookmarks
        if (tv_hero_focus_ == 1 && !app.signed_in())
        {
            const bool on = app.toggle_favorite(h.item.url, h.item.title);
            show_message(on ? "Добавлено в закладки" : "Убрано из закладок");
            feedback.play(audio::Cue::select);
            return;
        }
        const int index = app.adopt_catalog_item(h.item);
        if (index < 0)
        {
            tv_refuse(feedback, false);
            return;
        }
        open_page(index, false, feedback);
        // Signed in, the title's page opens on its folders, as its bookmark button does
        if (tv_hero_focus_ == 1)
        {
            app.load_bookmark_choices_async(index);
            dp_open_dialog(DpDialog::bookmarks, feedback);
        }
    }

    void tv_update_home(const InputFrame &input, app::Feedback &feedback)
    {
        tv_ensure_icons();
        if (input.is_pressed(Action::menu) || ((ps5_pad().pressed & PAD_OPTIONS) != 0))
        {
            tv_open_profile(feedback);
            return;
        }
        if (tv_zone_ == TvZone::drawer)
        {
            tv_update_drawer(input, feedback);
            return;
        }
        const int count = static_cast<int>(tv_cards_.size());
        // Search results take the page: no tabs, no chips, and Circle leaves them.
        const bool listing = home_layout_;
        if (!listing && tv_zone_ != TvZone::grid)
            tv_zone_ = count > 0 ? TvZone::grid : TvZone::drawer;
        if (listing && tv_kind_ != 0 && tv_zone_ == TvZone::tabs)
            tv_zone_ = TvZone::chips;
        if (tv_zone_ == TvZone::hero && !tv_hero_focusable())
            tv_zone_ = TvZone::chips;

        if (input.is_pressed(Action::back))
        {
            if (!listing)
            {
                // Back where the results came from (the search, Расширенный
                // поиск, the countries), the home list under it again
                const SearchBack back = tv_results_back_;
                tv_results_back_ = SearchBack::home;
                tv_zone_ = TvZone::tabs;
                tv_list_title_.clear();
                tv_load(0);
                if (back != SearchBack::home)
                {
                    search_open_ = true;
                    sx_screen_ = back == SearchBack::extended ? SxScreen::extended
                                 : back == SearchBack::country ? SxScreen::country
                                                               : SxScreen::none;
                }
                feedback.play(audio::Cue::back);
            }
            else if (tv_zone_ == TvZone::grid && tv_focus_ >= tv_columns())
            {
                tv_focus_ %= tv_columns();
                feedback.play(audio::Cue::back);
            }
            else if (tv_zone_ == TvZone::grid || tv_zone_ == TvZone::hero)
            {
                tv_zone_ = TvZone::chips;
                tv_chip_focus_ = tv_type_;
                feedback.play(audio::Cue::back);
            }
            else
                tv_open_drawer(feedback);
            return;
        }
        // L1 / R1 turn the banner's slides while it has the focus.
        if (tv_zone_ == TvZone::hero &&
            (input.is_pressed(Action::page_prev) || input.is_pressed(Action::page_next)))
        {
            tv_hero_show(tv_hero_index_ + (input.is_pressed(Action::page_next) ? 1 : -1));
            tv_tick(feedback, tv_page_x() + tv_hero_rect().cx());
            return;
        }

        const Direction nav = input.nav;
        if (tv_zone_ == TvZone::tabs && nav != Direction::none)
        {
            if (nav == Direction::left || nav == Direction::right)
            {
                const int next = tv_tab_ + (nav == Direction::right ? 1 : -1);
                if (next < 0)
                    tv_open_drawer(feedback);
                else if (next >= kTvTabs)
                    tv_refuse(feedback, input.nav_repeat);
                else
                {
                    // The tab under the focus is the selected one, as in the
                    // app; the list follows once the focus rests.
                    tv_tab_ = next;
                    tv_tab_delay_ = 0.35f;
                    tv_focus_ = 0;
                    tv_scroll_.snap(0.0f);
                    Rect tabs[kTvTabs];
                    tv_tab_layout(tabs);
                    tv_tick(feedback, tv_page_x() + tabs[next].cx());
                }
            }
            else if (nav == Direction::down)
            {
                tv_zone_ = TvZone::chips;
                tv_chip_focus_ = tv_type_;
                tv_tick(feedback, tv_page_x() + kTvPad);
            }
            else
                tv_refuse(feedback, input.nav_repeat);
        }
        else if (tv_zone_ == TvZone::chips && nav != Direction::none)
        {
            if (nav == Direction::left || nav == Direction::right)
            {
                const int next = tv_chip_focus_ + (nav == Direction::right ? 1 : -1);
                if (next < 0)
                    tv_open_drawer(feedback);
                else if (next >= kTvTypes)
                    tv_refuse(feedback, input.nav_repeat);
                else
                {
                    tv_chip_focus_ = next;
                    Rect chips[kTvTypes];
                    tv_chip_layout(chips);
                    tv_tick(feedback, tv_page_x() + chips[next].cx());
                }
            }
            else if (nav == Direction::up)
            {
                if (tv_kind_ == 0)
                {
                    tv_zone_ = TvZone::tabs;
                    tv_tick(feedback, tv_page_x() + kTvPad);
                }
                else
                    tv_refuse(feedback, input.nav_repeat);
            }
            else if (tv_hero_focusable())
            {
                tv_zone_ = TvZone::hero;
                tv_hero_focus_ = 0;
                tv_hero_timer_ = 0.0f;
                tv_tick(feedback, tv_page_x() + kTvPad);
            }
            else if (count > 0)
            {
                // The card last left, or the one under the chip.
                tv_zone_ = TvZone::grid;
                tv_focus_ = std::clamp(tv_focus_, 0, count - 1);
                tv_tick(feedback, tv_page_x() + tv_card_rect(tv_focus_).cx());
            }
            else
                tv_refuse(feedback, input.nav_repeat);
        }
        else if (tv_zone_ == TvZone::hero && nav != Direction::none)
        {
            // Left and right go between the two buttons, and past them to
            // the slide before or after; up is the chips, down the first row.
            tv_hero_timer_ = 0.0f;
            if (nav == Direction::left || nav == Direction::right)
            {
                const int next = tv_hero_focus_ + (nav == Direction::right ? 1 : -1);
                if (next < 0 || next > 1)
                    tv_hero_show(tv_hero_index_ + (next > 1 ? 1 : -1));
                else
                    tv_hero_focus_ = next;
                tv_tick(feedback, tv_page_x() + kTvPad + 120.0f * kTvDp * static_cast<float>(tv_hero_focus_));
            }
            else if (nav == Direction::up)
            {
                tv_zone_ = TvZone::chips;
                tv_chip_focus_ = tv_type_;
                tv_tick(feedback, tv_page_x() + kTvPad);
            }
            else if (count > 0)
            {
                tv_zone_ = TvZone::grid;
                tv_focus_ = std::min(tv_focus_ % tv_columns(), count - 1);
                tv_tick(feedback, tv_page_x() + tv_card_rect(tv_focus_).cx());
            }
            else
                tv_refuse(feedback, input.nav_repeat);
        }
        else if (tv_zone_ == TvZone::grid && nav != Direction::none && count > 0)
        {
            const int col = tv_focus_ % tv_columns();
            int next = tv_focus_;
            if (nav == Direction::left)
                next = col == 0 ? -1 : tv_focus_ - 1;
            else if (nav == Direction::right)
                next = (col == tv_columns() - 1 || tv_focus_ + 1 >= count) ? tv_focus_ : tv_focus_ + 1;
            else if (nav == Direction::up)
                next = tv_focus_ - tv_columns();
            else
            {
                // Down into a short last row lands on its last card.
                next = tv_focus_ + tv_columns();
                if (next >= count && next / tv_columns() == (count - 1) / tv_columns())
                    next = count - 1;
                else if (next >= count)
                    next = tv_focus_;
            }
            if (nav == Direction::left && next < 0)
                tv_open_drawer(feedback);
            else if (nav == Direction::up && next < 0)
            {
                if (tv_hero_focusable())
                {
                    tv_zone_ = TvZone::hero;
                    tv_hero_focus_ = 0;
                    tv_hero_timer_ = 0.0f;
                    tv_tick(feedback, tv_page_x() + kTvPad);
                }
                else if (listing)
                {
                    tv_zone_ = TvZone::chips;
                    tv_chip_focus_ = tv_type_;
                    tv_tick(feedback, tv_page_x() + kTvPad);
                }
                else
                    tv_refuse(feedback, input.nav_repeat);
            }
            else if (next == tv_focus_)
                tv_refuse(feedback, input.nav_repeat);
            else
            {
                tv_focus_ = next;
                tv_tick(feedback, tv_page_x() + tv_card_rect(next).cx());
            }
            // The app asks for the next page a page ahead of the focus (the
            // home tabs, and the paged lists: search, filter, countries).
            if (tv_focus_ + 2 * tv_columns() >= count)
                rezka::App::instance().load_movie_list_more_async();
        }

        if (!input.is_pressed(Action::confirm))
            return;
        if (tv_zone_ == TvZone::grid && count > 0)
            open_page(tv_focus_, false, feedback);
        else if (tv_zone_ == TvZone::hero)
            tv_hero_press(feedback);
        else if (tv_zone_ == TvZone::chips)
        {
            if (tv_chip_focus_ != tv_type_)
            {
                tv_type_ = tv_chip_focus_;
                tv_load(tv_kind_);
            }
            feedback.play(audio::Cue::select);
        }
        else if (tv_zone_ == TvZone::tabs && count > 0)
        {
            tv_zone_ = TvZone::grid;
            feedback.play(audio::Cue::select);
        }
    }

    void tv_animate(float dt)
    {
        // A tab the focus has settled on loads its list.
        if (tv_tab_delay_ >= 0.0f)
        {
            tv_tab_delay_ -= dt;
            if (tv_tab_delay_ < 0.0f)
                tv_load(0);
        }
        // Posters come for the rows in view first
        rezka::App::instance().poster_hint =
            std::max(0, (tv_focus_ / tv_columns() - 1) * tv_columns());
        tv_open_.target = tv_zone_ == TvZone::drawer ? 1.0f : 0.0f;
        tv_open_.update(dt, omega(14.0f));
        tv_scroll_.target = tv_scroll_target();
        tv_scroll_.update(dt, omega(12.0f));
        const bool covered = page_open_ || search_open_ || profile_open_ || person_open_;
        tv_clock_.target = (rezka::App::instance().settings().show_clock && tv_zone_ != TvZone::drawer &&
                            home_layout_ && !covered)
                               ? 1.0f
                               : 0.0f;
        tv_clock_.update(dt, omega(12.0f));
        const float rate = std::min(1.0f, dt * 14.0f);
        for (int i = 0; i < static_cast<int>(tv_cards_.size()); ++i)
        {
            const float want = (tv_zone_ == TvZone::grid && i == tv_focus_) ? 1.0f : 0.0f;
            float &lift = tv_cards_[static_cast<std::size_t>(i)].lift;
            lift += (want - lift) * rate;
        }
        Rect tabs[kTvTabs];
        tv_tab_layout(tabs);
        tv_pill_.target(tabs[tv_tab_]);
        if (!tv_pill_set_)
        {
            tv_pill_.snap(tabs[tv_tab_]);
            tv_pill_set_ = true;
        }
        tv_pill_.update(dt, 22.0f);
        tv_tabs_lit_.target = tv_zone_ == TvZone::tabs ? 1.0f : 0.0f;
        tv_tabs_lit_.update(dt, 22.0f);
        // The banner turns by itself while nothing is opened over the page.
        tv_hero_fade_ = std::min(1.0f, tv_hero_fade_ + dt / 0.6f);
        if (tv_hero_fade_ >= 1.0f)
            tv_hero_prev_ = -1;
        if (tv_hero_shown() && tv_hero_.size() > 1 && !covered && tv_zone_ != TvZone::drawer)
        {
            tv_hero_timer_ += dt;
            if (tv_hero_timer_ >= kTvHeroSeconds)
                tv_hero_show(tv_hero_index_ + 1);
        }
        for (int i = 0; i < 2; ++i)
        {
            const float want = (tv_zone_ == TvZone::hero && i == tv_hero_focus_) ? 1.0f : 0.0f;
            tv_hero_lift_[i] += (want - tv_hero_lift_[i]) * rate;
        }
    }

    // ---- drawing -----------------------------------------------------------

    void tv_draw_tabs(gfx::DrawList &list) const
    {
        const float size = 16.0f * kTvDp;
        if (!home_layout_ || tv_kind_ != 0)
        {
            // Other lists are headed by their name where the tabs would be.
            const char *title = !home_layout_ ? (tv_list_title_.empty() ? "Поиск" : tv_list_title_.c_str())
                                : tv_kind_ == 1 ? "Новинки" : "Анонсы";
            tv_text(list, tv_regular(), title, kTvPad, kTvTabsY, 24.0f * kTvDp, kTvTabH,
                    tv_on_surface());
            return;
        }
        Rect tabs[kTvTabs];
        tv_tab_layout(tabs);
        // The pill: light under the focus, a quiet tint under the selected tab otherwise.
        const float lit = tv_tabs_lit_.value;
        const Color pill = mix(tv_secondary_container().with_alpha(0.4f), tv_on_surface(), lit);
        const Rect p = tv_pill_.value();
        list.rounded_rect(p, p.h * 0.5f, pill);
        for (int i = 0; i < kTvTabs; ++i)
        {
            const bool selected = i == tv_tab_;
            const Color resting =
                selected ? tv_on_primary_container() : tv_on_surface().with_alpha(0.4f);
            const Color focused = selected ? tv_surface_variant() : tv_on_surface();
            tv_text(list, tv_medium(), kTvTabNames[i], tabs[i].x + 16.0f * kTvDp, tabs[i].y + 8.0f * kTvDp,
                    size, 20.0f * kTvDp, mix(resting, focused, lit), gfx::Align::left, 0.2f * kTvDp);
        }
    }

    void tv_draw_chips(gfx::DrawList &list) const
    {
        if (!home_layout_)
            return;
        Rect chips[kTvTypes];
        tv_chip_layout(chips);
        const float radius = 8.0f * kTvDp;
        for (int i = 0; i < kTvTypes; ++i)
        {
            const bool selected = i == tv_type_;
            const bool focused = tv_zone_ == TvZone::chips && i == tv_chip_focus_;
            Color fill = kClear, content = tv_on_surface_variant(), border = tv_border();
            float border_w = 1.0f * kTvDp;
            if (focused && selected)
            {
                fill = tv_on_primary_container();
                content = tv_on_primary();
                border = tv_on_primary_container();
                border_w = 1.1f * kTvDp;
            }
            else if (focused)
            {
                fill = tv_on_surface();
                content = tv_inverse_on_surface();
                border_w = 0.0f;
            }
            else if (selected)
            {
                fill = tv_secondary_container().with_alpha(0.4f);
                content = tv_on_secondary_container();
                border = tv_secondary();
            }
            if (border_w > 0.0f)
                list.bordered_rect(chips[i], radius, fill, border_w, border);
            else
                list.rounded_rect(chips[i], radius, fill);
            float x = chips[i].x + (selected ? 12.0f : 16.0f) * kTvDp;
            if (selected)
            {
                tv_icon(list, kTvIconCheck, x, chips[i].cy() - 9.0f * kTvDp, 18.0f * kTvDp, content);
                x += (18.0f + 8.0f) * kTvDp;
            }
            tv_text(list, tv_medium(), kTvTypeNames[i], x, chips[i].y + 8.0f * kTvDp, 14.0f * kTvDp,
                    20.0f * kTvDp, content, gfx::Align::left, 0.1f * kTvDp);
        }
    }

    void tv_draw_grid(gfx::DrawList &list) const
    {
        const float scroll = tv_scroll_.value;
        const float view = kHeight - kTvGridTop;
        const int count = static_cast<int>(tv_cards_.size());
        const float radius = 8.0f * kTvDp;
        const auto &app = rezka::App::instance();
        if (count == 0)
        {
            if (app.catalog_loading)
            {
                // The app's placeholder grid while the first page loads
                const float pulse = 0.35f + 0.15f * std::sin(clock_ * 3.0f);
                for (int i = 0; i < 2 * tv_columns(); ++i)
                {
                    const Rect r = tv_card_rect(i);
                    list.rounded_rect({r.x, kTvGridTop + r.y, r.w, tv_cover_h()}, radius,
                                      tv_surface_variant().with_alpha(pulse));
                }
            }
            else if (!tv_status_.empty())
                tv_text(list, tv_regular(), tv_status_, (kWidth - kTvRail) * 0.5f,
                        kTvGridTop + tv_grid_offset() + (view - tv_grid_offset()) * 0.4f - scroll,
                        22.0f * kTvDp, 28.0f * kTvDp, tv_on_surface(), gfx::Align::center);
            return;
        }

        // Only the rows in the viewport; the focused card goes last, over its neighbours.
        const int first_row = std::max(0, static_cast<int>((scroll - kTvGridPad) / tv_row_pitch()) - 1);
        const int last_row = static_cast<int>((scroll + view) / tv_row_pitch()) + 1;
        const int first = first_row * tv_columns();
        const int last = std::min(count, (last_row + 1) * tv_columns());
        const int focused = tv_zone_ == TvZone::grid ? tv_focus_ : -1;
        list.push_clip({-kTvSlide, kTvGridTop, kWidth + kTvSlide, view});
        for (int pass = 0; pass < 2; ++pass)
        {
            for (int i = first; i < last; ++i)
            {
                if ((i == focused) != (pass == 1))
                    continue;
                const TvCard &card = tv_cards_[static_cast<std::size_t>(i)];
                Rect r = tv_card_rect(i);
                r.y += kTvGridTop - scroll;
                // The cover alone grows under the focus, by a tenth, with the
                // border colour's 3 dp frame; the texts under it stay put.
                const float scale = 1.0f + 0.1f * card.lift;
                const Rect cover{r.x, r.y, r.w, tv_cover_h()};
                const Rect grown = scaled(cover, scale);
                const float grown_radius = radius * scale;
                if (card.texture != 0)
                    list.image(card.texture, grown, cover_uv(grown), kWhite, grown_radius);
                else
                    list.rounded_rect(grown, grown_radius, tv_surface_variant());
                if (card.lift > 0.01f)
                    list.bordered_rect(grown, grown_radius, kClear, 3.0f * kTvDp * scale,
                                       tv_border().with_alpha(card.lift));
                float y = cover.y + tv_cover_h() + 12.0f * kTvDp;
                for (const std::string &line : card.title)
                {
                    if (!line.empty())
                        tv_text(list, tv_medium(), line, r.x, y, kTvTitleSize, kTvTitleSize,
                                tv_on_surface());
                    y += kTvTitleSize;
                }
                y += 4.0f * kTvDp;
                for (const std::string &line : card.sub)
                {
                    if (!line.empty())
                        tv_text(list, tv_regular(), line, r.x, y, kTvSubSize, kTvSubSize,
                                tv_on_surface().with_alpha(0.6f));
                    y += kTvSubSize;
                }
            }
        }
        list.pop_clip();
    }

    // The clock: time over date on a tab hanging from the top edge, 64 dp in from the right.
    void tv_draw_clock(gfx::DrawList &list) const
    {
        const float shown = tv_clock_.value;
        if (shown < 0.01f)
            return;
        char time[16] = "";
        char date[48] = "";
        page_clock(time, sizeof(time), date, sizeof(date));
        const float pad = 16.0f * kTvDp;
        const float time_size = 22.0f * kTvDp;
        const float date_size = 16.0f * kTvDp;
        const float text_w = std::max(tv_medium().measure(time, time_size),
                                      tv_regular().measure(date, date_size, 0.5f * kTvDp));
        const float w = text_w + 2.0f * pad;
        const float h = 28.0f * kTvDp + 24.0f * kTvDp + 2.0f * pad;
        const float x = kWidth - 64.0f * kTvDp - w;
        const float y = -(1.0f - shown) * h;
        list.push_opacity(shown);
        // Square at the top (it runs off the screen), 16 dp round at the bottom
        list.rounded_rect({x, y - pad, w, h + pad}, pad, tv_primary_container());
        tv_text(list, tv_medium(), time, x + w * 0.5f, y + pad, time_size, 28.0f * kTvDp,
                tv_on_surface(), gfx::Align::center);
        tv_text(list, tv_regular(), date, x + w * 0.5f, y + pad + 28.0f * kTvDp, date_size,
                24.0f * kTvDp, tv_on_surface().with_alpha(0.7f), gfx::Align::center, 0.5f * kTvDp);
        list.pop_opacity();
    }

    void tv_draw_drawer(gfx::DrawList &list) const
    {
        const float open = tv_open_.value;
        // The modal drawer's scrim over the page
        if (open > 0.004f)
            list.rounded_rect({0.0f, 0.0f, kWidth, kHeight}, 0.0f, kBlack.with_alpha(0.5f * open));
        const float x = 12.0f * kTvDp;
        const float width = kTvItem + (kTvItemOpen - kTvItem) * open;
        const int selected = !home_layout_ ? 1 : tv_kind_ == 1 ? 3 : tv_kind_ == 2 ? 5 : kTvHomeItem;
        for (int i = 0; i < kTvDrawerItems; ++i)
        {
            const Rect r{x, tv_drawer_item_y(i), width, kTvItem};
            const bool focused = tv_zone_ == TvZone::drawer && i == tv_drawer_focus_;
            // Closed, the items that are not selected step back to 40%.
            Color content = tv_on_surface().with_alpha(0.4f + 0.6f * open);
            if (focused)
            {
                list.rounded_rect(r, r.h * 0.5f, tv_on_surface());
                content = tv_inverse_on_surface();
            }
            else if (i == selected)
            {
                list.rounded_rect(r, r.h * 0.5f, tv_secondary_container().with_alpha(0.4f));
                content = tv_on_secondary_container();
            }
            tv_icon(list, i, r.x + 16.0f * kTvDp, r.y + 16.0f * kTvDp, 24.0f * kTvDp, content);
            if (open > 0.02f)
            {
                list.push_clip({r.x, r.y, r.w - 16.0f * kTvDp, r.h});
                tv_text(list, tv_medium(), kTvDrawerNames[i], r.x + (16.0f + 24.0f + 8.0f) * kTvDp,
                        r.y + 18.0f * kTvDp, 14.0f * kTvDp, 20.0f * kTvDp,
                        content.with_alpha(tween::clamp01(open * 1.5f - 0.5f)), gfx::Align::left,
                        0.1f * kTvDp);
                list.pop_clip();
            }
        }
    }

    // ---- the banner ------------------------------------------------------
    //
    // A slide of "Фильмы — Смотрят сейчас": the large poster on the right,
    // never stretched (its upper part is cut to the frame, where the faces
    // are), dissolving into the page on the left and below; over the dark
    // side the label, the title in capitals, the facts, the description and
    // the two buttons; the slide's dots in the corner.

    static Color tv_hero_red() { return Color::rgb(0xf2453d); }
    static float tv_smooth(float t) { return t * t * (3.0f - 2.0f * t); }

    // The picture's frame on the right, its width from its own proportions,
    // and the part of the texture that fills it.
    static void tv_hero_frame(const Rect &b, float aspect, Rect &frame, Rect &uv)
    {
        const float w = std::min(b.w * 0.62f, b.h * aspect * 1.75f);
        frame = {b.x + b.w - w, b.y, w, b.h};
        const float box = frame.w / frame.h;
        if (box > aspect)
        {
            const float h = aspect / box;
            uv = {0.0f, (1.0f - h) * 0.2f, 1.0f, h};
        }
        else
        {
            const float u = box / aspect;
            uv = {(1.0f - u) * 0.5f, 0.0f, u, 1.0f};
        }
    }

    void tv_draw_hero_picture(gfx::DrawList &list, const Rect &b, const TvHero &h, float alpha) const
    {
        if (h.texture == 0 || h.aspect <= 0.0f || alpha <= 0.0f)
            return;
        Rect frame, uv;
        tv_hero_frame(b, h.aspect, frame, uv);
        list.image(h.texture, frame, uv, kWhite.with_alpha(alpha), 16.0f * kTvDp);
    }

    // The label, the title, the facts and the description, from the top of the text block.
    void tv_draw_hero_text(gfx::DrawList &list, float x, float y, const TvHero &h, float alpha) const
    {
        if (alpha <= 0.0f)
            return;
        const float dp = kTvDp;
        list.push_opacity(alpha);
        tv_text(list, tv_medium(), "Сейчас смотрят", x, y, 14.0f * dp, 20.0f * dp, tv_hero_red(), gfx::Align::left,
                0.8f * dp);
        y += 28.0f * dp;
        const bool two = !h.title[1].empty();
        const float size = two ? kTvHeroTitleSmall : kTvHeroTitle;
        const float line = two ? 30.0f * dp : 40.0f * dp;
        for (const std::string &part : h.title)
        {
            if (part.empty())
                continue;
            tv_text(list, tv_medium(), part, x, y, size, line, kWhite, gfx::Align::left, 0.5f * dp);
            y += line;
        }
        y += 10.0f * dp;
        // Год  •  Жанр  •  Страна  •  [HD]
        float fx = x;
        const float fact = 15.0f * dp;
        const Color soft = tv_on_surface().with_alpha(0.85f);
        for (const std::string &part : h.facts)
        {
            fx += tv_text(list, tv_regular(), part, fx, y, fact, 22.0f * dp, soft);
            fx += tv_text(list, tv_regular(), "  •  ", fx, y, fact, 22.0f * dp, soft.with_alpha(0.6f));
        }
        const float badge_w = tv_medium().measure(h.quality, 11.0f * dp, 0.5f * dp) + 12.0f * dp;
        const Rect badge{fx, y + 3.0f * dp, badge_w, 16.0f * dp};
        list.bordered_rect(badge, 3.0f * dp, kClear, 1.0f * dp, tv_on_surface().with_alpha(0.75f));
        tv_text(list, tv_medium(), h.quality, badge.cx(), badge.y, 11.0f * dp, 16.0f * dp, tv_on_surface(),
                gfx::Align::center, 0.5f * dp);
        y += 22.0f * dp + 12.0f * dp;
        for (const std::string &part : h.blurb)
        {
            tv_text(list, tv_regular(), part, x, y, kTvHeroBlurb, 21.0f * dp, tv_on_surface().with_alpha(0.75f));
            y += 21.0f * dp;
        }
        list.pop_opacity();
    }
    // The text block's height, for centring it with the buttons in the banner.
    static float tv_hero_block_h(const TvHero &h)
    {
        const float dp = kTvDp;
        const float title = h.title[1].empty() ? 40.0f * dp : 60.0f * dp;
        return 28.0f * dp + title + 10.0f * dp + 34.0f * dp + static_cast<float>(h.blurb.size()) * 21.0f * dp;
    }

    void tv_draw_hero_button(gfx::DrawList &list, const Rect &r, int which, bool light, int icon,
                             const char *label) const
    {
        const float dp = kTvDp;
        const float lift = tv_hero_lift_[which];
        const Rect grown = scaled(r, 1.0f + 0.06f * lift);
        const float radius = grown.h * 0.5f;
        Color ink = tv_on_surface();
        if (light)
        {
            list.rounded_rect(grown, radius, mix(tv_on_surface(), kWhite, lift));
            ink = tv_inverse_on_surface();
        }
        else
        {
            list.bordered_rect(grown, radius, mix(kBlack.with_alpha(0.35f), kWhite, lift), 1.5f * dp,
                               tv_on_surface().with_alpha(0.7f));
            ink = mix(tv_on_surface(), tv_inverse_on_surface(), lift);
        }
        if (lift > 0.01f)
            list.bordered_rect(grown.inset(-4.0f * dp), radius + 4.0f * dp, kClear, 2.0f * dp,
                               tv_on_surface().with_alpha(lift));
        tv_icon(list, icon, grown.x + 20.0f * dp, grown.cy() - 12.0f * dp, 24.0f * dp, ink);
        tv_text(list, tv_medium(), label, grown.x + 52.0f * dp, grown.cy() - 12.0f * dp, 16.0f * dp, 24.0f * dp, ink,
                gfx::Align::left, 0.2f * dp);
    }

    void tv_draw_hero(gfx::DrawList &list) const
    {
        if (!tv_hero_shown())
            return;
        const float dp = kTvDp;
        const float view = kHeight - kTvGridTop;
        Rect b = tv_hero_rect();
        b.y += kTvGridTop - tv_scroll_.value;
        if (b.y + b.h < kTvGridTop || b.y > kHeight)
            return;
        list.push_clip({-kTvSlide, kTvGridTop, kWidth + kTvSlide, view});
        if (tv_hero_.empty())
        {
            // Loading: the frame of the picture, pulsing as the grid's placeholders do
            const float pulse = 0.25f + 0.1f * std::sin(clock_ * 3.0f);
            const Rect frame{b.x + b.w * 0.38f, b.y, b.w * 0.62f, b.h};
            list.rounded_rect(frame, 16.0f * dp, tv_surface_variant().with_alpha(pulse));
            list.gradient_rect_h({frame.x - 1.0f, frame.y, frame.w * 0.6f, frame.h}, 0.0f, tv_surface(),
                                 tv_surface().with_alpha(0.0f));
            list.pop_clip();
            return;
        }
        const TvHero &now = tv_hero_[static_cast<std::size_t>(tv_hero_index_)];
        const bool fading = tv_hero_prev_ >= 0 && tv_hero_prev_ < static_cast<int>(tv_hero_.size());
        const float t = fading ? tv_smooth(tv_hero_fade_) : 1.0f;

        // The pictures: the old one whole, the new one coming in over it.
        if (fading)
            tv_draw_hero_picture(list, b, tv_hero_[static_cast<std::size_t>(tv_hero_prev_)], 1.0f);
        tv_draw_hero_picture(list, b, now, t);

        // The page's colour over the picture's edges: wide on the left and
        // below, where the text is, a little at the top and on the right.
        // They follow the picture's own frame (the wider of the two while fading).
        const Color page = tv_surface();
        const Color none = page.with_alpha(0.0f);
        float left = b.x + b.w * 0.38f;
        for (int i : {tv_hero_index_, fading ? tv_hero_prev_ : -1})
        {
            if (i < 0 || tv_hero_[static_cast<std::size_t>(i)].aspect <= 0.0f)
                continue;
            Rect frame, uv;
            tv_hero_frame(b, tv_hero_[static_cast<std::size_t>(i)].aspect, frame, uv);
            left = i == tv_hero_index_ ? frame.x : std::min(left, frame.x);
        }
        const Rect area{left, b.y, b.x + b.w - left + 1.0f, b.h};
        list.gradient_rect_h({area.x - 2.0f, area.y, area.w * 0.55f, area.h}, 0.0f, page, none);
        list.gradient_rect_h({area.x, area.y, area.w * 0.8f, area.h}, 0.0f, page.with_alpha(0.55f), none);
        list.gradient_rect({area.x, area.y + area.h * 0.5f, area.w, area.h * 0.5f + 2.0f}, 0.0f, none, page);
        list.gradient_rect({area.x, area.y + area.h * 0.78f, area.w, area.h * 0.22f + 2.0f}, 0.0f, none, page);
        list.gradient_rect({area.x, area.y - 1.0f, area.w, area.h * 0.14f}, 0.0f, page, none);
        list.gradient_rect_h({area.x + area.w * 0.9f, area.y, area.w * 0.1f + 1.0f, area.h}, 0.0f, none, page);

        // The text, the old slide's leaving in the first half of the fade, the new one's coming in the second.
        const float x = b.x + 32.0f * dp;
        const float buttons_h = 44.0f * dp;
        const auto top_of = [&](const TvHero &h) {
            return b.y + (b.h - (tv_hero_block_h(h) + 20.0f * dp + buttons_h)) * 0.5f;
        };
        if (fading)
        {
            const TvHero &old = tv_hero_[static_cast<std::size_t>(tv_hero_prev_)];
            tv_draw_hero_text(list, x, top_of(old), old, 1.0f - std::min(1.0f, t * 2.0f));
        }
        tv_draw_hero_text(list, x, top_of(now), now, fading ? std::max(0.0f, t * 2.0f - 1.0f) : 1.0f);

        // The buttons stay put under the text block's bottom, as the slide changes.
        const float by = b.y + b.h - (b.h - (tv_hero_block_h(now) + 20.0f * dp + buttons_h)) * 0.5f - buttons_h;
        const float play_w = tv_medium().measure("Смотреть", 16.0f * dp, 0.2f * dp) + 52.0f * dp + 24.0f * dp;
        // Signed in, the site's folders are chosen on the title's page
        auto &app = rezka::App::instance();
        const bool saved = !app.signed_in() && app.is_favorite(now.item.url);
        const char *list_label = saved ? "В моём списке" : "Мой список";
        const float list_w = tv_medium().measure(list_label, 16.0f * dp, 0.2f * dp) + 52.0f * dp + 24.0f * dp;
        tv_draw_hero_button(list, {x, by, play_w, buttons_h}, 0, true, kTvIconPlayArrow, "Смотреть");
        tv_draw_hero_button(list, {x + play_w + 16.0f * dp, by, list_w, buttons_h}, 1, false,
                            saved ? kTvIconCheck : kTvIconAdd, list_label);

        // The dots, the slide shown drawn long.
        const int count = static_cast<int>(tv_hero_.size());
        float dx = b.x + b.w - 28.0f * dp;
        const float dy = b.y + b.h - 24.0f * dp;
        for (int i = count - 1; i >= 0; --i)
        {
            const bool on = i == tv_hero_index_;
            const float w = on ? 22.0f * dp : 8.0f * dp;
            dx -= w;
            list.rounded_rect({dx, dy - 4.0f * dp, w, 8.0f * dp}, 4.0f * dp,
                              on ? tv_on_surface() : tv_on_surface().with_alpha(0.35f));
            dx -= 8.0f * dp;
        }
        list.pop_clip();
    }

    void tv_draw_home(gfx::DrawList &list) const
    {
        const bool shrink = rezka::App::instance().settings().anim_nav_drawer == 1;
        if (shrink)
            list.push_transform(1.0f - 0.12f * tv_open_.value, kWidth, kHeight * 0.5f, tv_page_x(), 0.0f);
        else
            list.push_transform(1.0f, 0.0f, 0.0f, tv_page_x(), 0.0f);
        tv_draw_tabs(list);
        tv_draw_chips(list);
        tv_draw_hero(list);
        tv_draw_grid(list);
        list.pop_transform();
        tv_draw_clock(list);
        tv_draw_drawer(list);
    }

    // ---- text entry ----------------------------------------------------------
    //
    // A field is typed into with the console's own keyboard (the platform
    // layer's keyboard_open: the PS5 IME dialog). Where there is none — the
    // reference build on a PC, or a console that refuses — this sheet comes up
    // instead: the kit's key table in the app's colours, with the field over it.

    void kb_begin(std::string *target, const char *title, bool password, app::Feedback &feedback)
    {
        kb_target_ = target;
        kb_title_ = title;
        kb_password_ = password;
        if (keyboard_open(title, target->c_str(), password, 64))
        {
            kb_system_ = true;
            return;
        }
        kb_open_ = true;
        key_row_ = 1;
        key_col_ = 0;
        key_lang_ = 0;
        feedback.play(audio::Cue::modal_open);
    }
    bool kb_active() const
    {
        return kb_open_ || kb_system_;
    }
    void kb_update(const InputFrame &input, app::Feedback &feedback)
    {
        if (kb_system_)
        {
            char text[512] = "";
            const int state = keyboard_poll(text, sizeof(text));
            if (state == 0)
                return;
            if (state > 0 && kb_target_)
                *kb_target_ = text;
            kb_system_ = false;
            return;
        }
        if (!kb_target_)
        {
            kb_open_ = false;
            return;
        }
        std::string &target = *kb_target_;
        if (input.is_pressed(Action::back) || input.is_pressed(Action::menu))
        {
            kb_open_ = false;
            feedback.play(audio::Cue::modal_close);
            return;
        }
        if (input.is_pressed(Action::west))
        {
            erase_from(target);
            feedback.play(audio::Cue::toggle);
            return;
        }
        if (input.is_pressed(Action::north))
        {
            cycle_key_lang();
            feedback.play(audio::Cue::toggle);
            return;
        }
        navigate_key_grid(input, feedback);
        if (!input.is_pressed(Action::confirm))
            return;
        if (key_row_ < 4)
        {
            const int index = key_row_ * kKeyCols + key_col_;
            if (target.size() < 96)
                target += keys_for_lang(key_lang_)[index];
            feedback.play(audio::Cue::select);
        }
        else if (key_col_ <= 2)
        {
            cycle_key_lang();
            feedback.play(audio::Cue::toggle);
        }
        else if (key_col_ <= 6)
        {
            target.push_back(' ');
            feedback.play(audio::Cue::select);
        }
        else if (key_col_ <= 8)
        {
            erase_from(target);
            feedback.play(audio::Cue::toggle);
        }
        else
        {
            kb_open_ = false;
            feedback.play(audio::Cue::modal_close);
        }
    }
    // What a password field shows: a dot a character.
    static std::string kb_masked(const std::string &text)
    {
        std::string dots;
        for (const char c : text)
            if ((static_cast<unsigned char>(c) & 0xc0) != 0x80)
                dots += "\xe2\x80\xa2";
        return dots;
    }
    void kb_draw(gfx::DrawList &list) const
    {
        if (!kb_open_ || !kb_target_)
            return;
        const float dp = kTvDp;
        const float cell = 36.0f * dp, gap = 4.0f * dp;
        const float grid_w = kKeyCols * cell + (kKeyCols - 1) * gap;
        const float w = grid_w + 40.0f * dp;
        const float h = (20.0f + 28.0f + 16.0f + 48.0f + 16.0f) * dp + 5.0f * (cell + gap) + 16.0f * dp;
        const Rect card{(kWidth - w) * 0.5f, kHeight - h - 24.0f * dp, w, h};
        list.rounded_rect({0.0f, 0.0f, kWidth, kHeight}, 0.0f, kBlack.with_alpha(0.6f));
        list.bordered_rect(card, 28.0f * dp, tv_surface(), 1.0f * dp, tv_border_variant());
        tv_text(list, tv_regular(), kb_title_, card.x + 20.0f * dp, card.y + 20.0f * dp, 22.0f * dp, 28.0f * dp, tv_on_surface());
        // The field being typed into, with a caret
        const Rect field{card.x + 20.0f * dp, card.y + 64.0f * dp, grid_w, 48.0f * dp};
        list.bordered_rect(field, 8.0f * dp, kClear, 2.0f * dp, tv_primary());
        const std::string shown = tv_regular().font->fit(kb_password_ ? kb_masked(*kb_target_) : *kb_target_, 16.0f * dp, grid_w - 40.0f * dp);
        const float text_w = tv_text(list, tv_regular(), shown, field.x + 16.0f * dp, field.cy() - 12.0f * dp, 16.0f * dp, 24.0f * dp,
                                     tv_on_surface(), gfx::Align::left, 0.5f * dp);
        if (std::fmod(clock_, 1.0f) < 0.5f)
            list.rounded_rect({field.x + 16.0f * dp + text_w + 2.0f, field.cy() - 11.0f * dp, 2.0f, 22.0f * dp}, 0.0f, tv_primary());
        const float gx = card.x + 20.0f * dp, gy = field.y + field.h + 16.0f * dp;
        const char **keys = keys_for_lang(key_lang_);
        for (int row = 0; row < 4; ++row)
            for (int col = 0; col < kKeyCols; ++col)
            {
                const Rect r{gx + static_cast<float>(col) * (cell + gap), gy + static_cast<float>(row) * (cell + gap), cell, cell};
                const bool focused = row == key_row_ && col == key_col_;
                list.rounded_rect(r, 8.0f * dp, focused ? tv_on_surface() : tv_surface_variant().with_alpha(0.4f));
                tv_text(list, tv_medium(), keys[row * kKeyCols + col], r.cx(), r.cy() - 12.0f * dp, 16.0f * dp, 24.0f * dp,
                        focused ? tv_inverse_on_surface() : tv_on_surface(), gfx::Align::center);
            }
        // The last row: the layout, the space, the erase and the done keys
        const struct
        {
            int from, to;
            const char *label;
        } actions[4] = {{0, 2, lang_button_label(key_lang_)}, {3, 6, "Пробел"}, {7, 8, "Стереть"}, {9, 11, "Готово"}};
        for (const auto &action : actions)
        {
            const Rect r{gx + static_cast<float>(action.from) * (cell + gap), gy + 4.0f * (cell + gap),
                         static_cast<float>(action.to - action.from + 1) * (cell + gap) - gap, cell};
            const bool focused = key_row_ == 4 && key_col_ >= action.from && key_col_ <= action.to;
            const bool done = action.from == 9;
            list.rounded_rect(r, 8.0f * dp, focused ? tv_on_surface() : done ? tv_primary() : tv_surface_variant().with_alpha(0.4f));
            tv_text(list, tv_medium(), action.label, r.cx(), r.cy() - 10.0f * dp, 14.0f * dp, 20.0f * dp,
                    focused ? tv_inverse_on_surface() : done ? tv_on_primary() : tv_on_surface(), gfx::Align::center, 0.1f * dp);
        }
    }

    // ---- a title's page: HDrezkaTV's "MovieDetails" screen -------------------
    //
    // One scrolling column, as in the app (its LazyColumn, 24 dp around and
    // 12 dp between items): the hero (the large poster as a backdrop fading
    // into the surface, the poster card, the buttons, the title), the row of
    // rating and fact tiles, the tagline and release date, the country, genre,
    // list and collection links, the description beside the trailer, the cast,
    // the release schedule, the franchise and "Смотрите также". Sizes are the
    // app's dp, two pixels each.

    enum class DpWhat : std::uint8_t
    {
        poster,
        watch,
        resume,
        bookmark,
        stats,
        qr,
        chip,      // a: 0 КиноПоиск 1 IMDb 2 HDrezka 3 runtime 4 age 5 year 6 comments
        link,      // a: 0 countries 1 genres 2 lists 3 collections; b: which
        link_all,  // a collapsed row of links: opens the list of them
        desc,
        trailer,
        cast,
        schedule,
        franchise,
        related,
    };
    struct DpItem
    {
        DpWhat what = DpWhat::watch;
        int a = 0;
        int b = 0;
        Rect r{}; // on the unscrolled page
    };
    // A band of the page the focus moves along sideways. A wide one scrolls.
    struct DpRow
    {
        std::vector<DpItem> items;
        bool hero = false;
        bool scrolls = false;
        float content_w = 0.0f; // from kDpLeft to the last item's right edge
    };
    struct DpLayout
    {
        std::vector<DpRow> rows;
        Rect poster{};
        float buttons_y = 0.0f, resume_y = 0.0f, title_y = 0.0f, original_y = 0.0f;
        float chips_y = 0.0f, tagline_y = 0.0f, date_y = 0.0f;
        float link_y[4] = {0, 0, 0, 0}; // each list's band, 0 when it has none
        float link_h[4] = {0, 0, 0, 0};
        float desc_y = 0.0f, cast_y = 0.0f, schedule_y = 0.0f, franchise_y = 0.0f, related_y = 0.0f;
        float bottom = 0.0f;
    };
    enum class DpDialog : std::uint8_t
    {
        none,
        text,     // the whole description, or a comment
        stats,    // Статистика выбора озвучек
        schedule, // Расписание выхода серий
        links,    // all of a row's links
        image,    // the poster, full screen
        bookmarks, // the account's folders, the title's checked (signed in)
    };

    static constexpr float kDpEdge = 24.0f * kTvDp;  // the column's own padding
    static constexpr float kDpLeft = 72.0f * kTvDp;  // ... plus an item's 48 dp
    static constexpr float kDpWide = kWidth - 2.0f * kDpLeft;
    static constexpr float kDpGap = 12.0f * kTvDp;
    static constexpr float kDpHeroH = 0.85f * (kHeight - 2.0f * kDpEdge);
    static constexpr float kDpCardW = 120.0f * kTvDp;
    static constexpr float kDpCardH = 180.0f * kTvDp;
    static constexpr float kDpTileW = (kDpWide - 20.0f * kTvDp) * 0.5f; // description and trailer
    static constexpr float kDpTileH = kDpTileW * 9.0f / 16.0f;

    static Color tv_primary() { return Color::rgb(0x70d2ff); }
    static Color tv_inverse_primary() { return Color::rgb(0x00668a); }
    static Color tv_border_variant() { return Color::rgb(0x40484c); }
    static Color tv_error() { return Color::rgb(0xffb4ab); }
    // The app's colour of a score: ten steps from red (below 1) to green (9 and up).
    static Color tv_rating_color(float value)
    {
        static const std::uint32_t kScale[10] = {0xe01123, 0xec3c18, 0xf45f04, 0xfe8d04, 0xfeb101,
                                                 0xf3cf00, 0xc3cb02, 0x8ec720, 0x4fc140, 0x01bb5e};
        return Color::rgb(kScale[std::clamp(static_cast<int>(value), 0, 9)]);
    }
    // 1234567 -> "1 234 567", as the app groups a count.
    static std::string dp_grouped(int value)
    {
        std::string digits = std::to_string(std::max(0, value));
        for (int at = static_cast<int>(digits.size()) - 3; at > 0; at -= 3)
            digits.insert(static_cast<std::size_t>(at), " ");
        return digits;
    }

    const std::vector<std::string> &dp_link_names(int list) const
    {
        return list == 0 ? page_model_.countries : list == 1 ? page_model_.genres
               : list == 2 ? dp_list_names_ : dp_collection_names_;
    }
    std::string dp_link_url(int list, int index) const
    {
        const PageModel &m = page_model_;
        const std::size_t i = static_cast<std::size_t>(index);
        if (list == 0)
            return i < m.country_urls.size() ? m.country_urls[i] : std::string();
        if (list == 1)
            return i < m.genre_urls.size() ? m.genre_urls[i] : std::string();
        if (list == 2)
            return i < m.lists.size() ? m.lists[i].url : std::string();
        return i < m.collections.size() ? m.collections[i].url : std::string();
    }
    static const char *dp_link_label(int list)
    {
        static const char *kLabels[4] = {"Страна:", "Жанр:", "Входит в списки:", "Из серии:"};
        return kLabels[list];
    }
    // What a chip of the fact row says: its value and the label under it.
    bool dp_chip_text(int chip, std::string &value, std::string &votes, const char *&label,
                      Color &color) const
    {
        const PageModel &m = page_model_;
        const rezka::RatingInfo *rating = chip == 0 ? &m.kinopoisk : chip == 1 ? &m.imdb : chip == 2 ? &m.hdrezka : nullptr;
        votes.clear();
        color = tv_on_surface();
        if (rating)
        {
            if (!rating->valid)
                return false;
            char text[16];
            // The app prints the double as it is: 7.9, 7.94, 8.0
            std::snprintf(text, sizeof(text), "%.2f", rating->value);
            value = text;
            if (value.size() > 3 && value.back() == '0')
                value.pop_back();
            if (!rating->votes.empty() && rezka::App::instance().settings().show_rating_voices)
                votes = "(" + rating->votes + ")";
            label = chip == 0 ? "КиноПоиск" : chip == 1 ? "IMDb" : "HDrezka";
            color = tv_rating_color(rating->value);
            return true;
        }
        if (chip == 3)
            value = m.duration, label = "Длительность";
        else if (chip == 4)
            value = m.age, label = "Возраст";
        else if (chip == 5)
            value = m.year, label = "Год выхода";
        else
        {
            value = m.comments_count > 0 ? dp_grouped(m.comments_count) : std::string();
            label = "Комментарии";
        }
        return !value.empty();
    }

    // What does not change from frame to frame: the wrapped texts.
    void dp_prepare()
    {
        const PageModel &m = page_model_;
        const std::string key = m.url + "|" + m.title + "|" + std::to_string(m.description.size()) + "|" +
                                std::to_string(m.lists.size() + m.collections.size() + m.cast.size());
        if (key == dp_key_)
            return;
        dp_key_ = key;
        const gfx::Font &regular = *tv_regular().font;
        dp_title_lines_ = regular.wrap(m.title, 36.0f * kTvDp, kDpWide);
        if (dp_title_lines_.size() > 2)
        {
            dp_title_lines_[1] = regular.fit(dp_title_lines_[1] + " " + dp_title_lines_[2], 36.0f * kTvDp, kDpWide);
            dp_title_lines_.resize(2);
        }
        if (dp_title_lines_.empty())
            dp_title_lines_.push_back("");
        // The description tile holds eight lines; more ends in the app's hint.
        dp_desc_lines_ = regular.wrap(m.description, 16.0f * kTvDp, kDpTileW - 32.0f * kTvDp);
        dp_desc_more_ = dp_desc_lines_.size() > 8;
        if (dp_desc_more_)
        {
            dp_desc_lines_.resize(8);
            const float hint_w = tv_medium().measure("… Нажмите, чтобы развернуть", 16.0f * kTvDp);
            std::string &last = dp_desc_lines_.back();
            while (!last.empty() && regular.measure(last, 16.0f * kTvDp) + hint_w > kDpTileW - 32.0f * kTvDp)
            {
                last.pop_back();
                while (!last.empty() && (static_cast<unsigned char>(last.back()) & 0xc0) == 0x80)
                    last.pop_back();
            }
            while (!last.empty() && (last.back() == ' ' || last.back() == '.'))
                last.pop_back();
        }
        dp_list_names_.clear();
        for (const rezka::ListRef &ref : m.lists)
            dp_list_names_.push_back(ref.place.empty() ? ref.title : ref.title + " " + ref.place);
        dp_collection_names_.clear();
        for (const rezka::ListRef &ref : m.collections)
            dp_collection_names_.push_back(ref.title);
        dp_cast_names_.clear();
        for (const rezka::Person &p : m.cast)
        {
            std::string lines[2];
            tv_two_lines(*tv_medium().font, p.name, 14.0f * kTvDp, kDpCardW, lines);
            dp_cast_names_.push_back(lines[0]);
            dp_cast_names_.push_back(lines[1]);
        }
        dp_related_lines_.clear();
        for (const rezka::CatalogItem &item : m.related)
        {
            std::string lines[2];
            tv_two_lines(*tv_medium().font, item.title, 14.0f * kTvDp, kDpCardW, lines);
            dp_related_lines_.push_back(lines[0]);
            dp_related_lines_.push_back(lines[1]);
            tv_two_lines(regular, item.info, 12.0f * kTvDp, kDpCardW, lines);
            dp_related_lines_.push_back(lines[0]);
            dp_related_lines_.push_back(lines[1]);
        }
        auto &app = rezka::App::instance();
        if (!m.full_cover.empty())
            app.request_photo(m.full_cover);
        if (!m.cover.empty())
            app.request_photo(m.cover);
        if (!m.trailer_id.empty())
            app.request_photo("https://img.youtube.com/vi/" + m.trailer_id + "/hqdefault.jpg");
        for (const rezka::CatalogItem &item : m.related)
            app.request_photo(item.cover_url);
    }

    // The collapsed text of a row with more links than it shows: "A, B, C."
    std::string dp_joined(int list) const
    {
        std::string all;
        const std::vector<std::string> &names = dp_link_names(list);
        for (std::size_t i = 0; i < names.size(); ++i)
            all += names[i] + (i + 1 < names.size() ? ", " : ". ");
        return all;
    }

    DpLayout dp_layout() const
    {
        const PageModel &m = page_model_;
        DpLayout L;
        const float dp = kTvDp;
        // The hero's column is packed against its bottom edge, 8 dp apart.
        float bottom = kDpEdge + kDpHeroH;
        if (!m.original.empty())
        {
            L.original_y = bottom - 28.0f * dp;
            bottom = L.original_y - 8.0f * dp;
        }
        L.title_y = bottom - 44.0f * dp * static_cast<float>(dp_title_lines_.size());
        bottom = L.title_y - 8.0f * dp - 8.0f * dp - 8.0f * dp; // a spacer of 8 between two gaps
        if (m.has_resume && !m.blocked)
        {
            L.resume_y = bottom - 16.0f * dp;
            bottom = L.resume_y - 4.0f * dp - 8.0f * dp;
        }
        L.buttons_y = bottom - 40.0f * dp;
        bottom = L.buttons_y - 8.0f * dp;
        const float poster_h = std::max(60.0f, bottom - kDpEdge - 48.0f * dp);
        L.poster = {kDpLeft, kDpEdge + 24.0f * dp, poster_h * 2.0f / 3.0f, poster_h};

        DpRow poster;
        poster.hero = true;
        poster.items.push_back({DpWhat::poster, 0, 0, L.poster});
        L.rows.push_back(poster);

        if (!m.blocked)
        {
            DpRow row;
            row.hero = true;
            float x = kDpLeft;
            const auto add = [&](DpWhat what, float w) {
                row.items.push_back({what, 0, 0, {x, L.buttons_y, w, 40.0f * dp}});
                x += w + 12.0f * dp;
            };
            const float label = 14.0f * dp;
            add(DpWhat::watch, (16.0f + 20.0f + 8.0f + 16.0f) * dp +
                                   tv_medium().measure(m.pending ? "Ожидается" : "Смотреть", label, 0.1f * dp));
            if (m.has_resume)
                add(DpWhat::resume, 32.0f * dp + tv_medium().measure("Продолжить *", label, 0.1f * dp));
            add(DpWhat::bookmark, 40.0f * dp);
            if (!m.voice_stats.empty())
                add(DpWhat::stats, 40.0f * dp);
            add(DpWhat::qr, 40.0f * dp);
            L.rows.push_back(row);
        }

        float y = kDpEdge + kDpHeroH + kDpGap;
        // The fact tiles
        {
            DpRow row;
            row.scrolls = true;
            float x = kDpLeft;
            for (int chip = 0; chip < 7; ++chip)
            {
                std::string value, votes;
                const char *label = "";
                Color color;
                if (!dp_chip_text(chip, value, votes, label, color))
                    continue;
                float top = tv_medium().measure(value, 24.0f * dp);
                if (!votes.empty())
                    top += 4.0f * dp + tv_regular().measure(votes, 12.0f * dp);
                const float w = std::max(80.0f * dp, std::max(top, tv_regular().measure(label, 14.0f * dp)) + 32.0f * dp);
                row.items.push_back({DpWhat::chip, chip, 0, {x, y, w, 62.0f * dp}});
                x += w + 8.0f * dp;
            }
            if (!row.items.empty())
            {
                row.content_w = x - 8.0f * dp - kDpLeft;
                L.chips_y = y;
                L.rows.push_back(row);
                y += 62.0f * dp + kDpGap;
            }
        }
        const float line18 = 22.0f * dp; // an 18 sp line
        if (!m.slogan.empty())
        {
            L.tagline_y = y;
            y += 16.0f * dp + line18 + kDpGap;
        }
        if (!m.release_date.empty())
        {
            L.date_y = y;
            y += 16.0f * dp + line18 + kDpGap;
        }
        static const int kShown[4] = {5, 5, 1, 2};
        for (int list = 0; list < 4; ++list)
        {
            const std::vector<std::string> &names = dp_link_names(list);
            if (names.empty())
                continue;
            DpRow row;
            L.link_y[list] = y;
            const float size = 18.0f * dp;
            if (static_cast<int>(names.size()) > kShown[list])
            {
                // One plate with the names in a sentence and the hint to open it
                const float text_w = kWidth - 2.0f * (kDpEdge + 32.0f * dp) - 32.0f * dp;
                const std::string all = std::string(dp_link_label(list)) + " " + dp_joined(list) + "Нажмите, чтобы развернуть";
                const float lines = static_cast<float>(tv_regular().font->wrap(all, size, text_w).size());
                L.link_h[list] = 32.0f * dp + std::max(1.0f, lines) * line18;
                row.items.push_back({DpWhat::link_all, list, 0,
                                     {kDpEdge + 32.0f * dp, y, kWidth - 2.0f * (kDpEdge + 32.0f * dp), L.link_h[list]}});
            }
            else
            {
                // The label, then a bordered chip a name, wrapping as a flow row does
                float x = kDpLeft + tv_medium().measure(dp_link_label(list), size) + 8.0f * dp;
                float row_y = y + 4.0f * dp;
                const float chip_h = 16.0f * dp + line18;
                for (int i = 0; i < static_cast<int>(names.size()); ++i)
                {
                    const float w = tv_regular().measure(names[static_cast<std::size_t>(i)], size) + 32.0f * dp;
                    if (x + w > kDpLeft + kDpWide && x > kDpLeft)
                    {
                        x = kDpLeft;
                        row_y += chip_h + 8.0f * dp;
                    }
                    row.items.push_back({DpWhat::link, list, i, {x, row_y, w, chip_h}});
                    x += w + 8.0f * dp;
                }
                L.link_h[list] = row_y + chip_h + 4.0f * dp - y;
            }
            L.rows.push_back(row);
            y += L.link_h[list] + kDpGap;
        }
        // The description, and the trailer beside it
        {
            L.desc_y = y;
            DpRow row;
            const float tile_y = y + (16.0f + 28.0f + 16.0f) * dp;
            row.items.push_back({DpWhat::desc, 0, 0, {kDpLeft, tile_y, kDpTileW, kDpTileH}});
            if (!m.trailer_id.empty())
                row.items.push_back({DpWhat::trailer, 0, 0, {kDpLeft + kDpTileW + 20.0f * dp, tile_y, kDpTileW, kDpTileH}});
            L.rows.push_back(row);
            y = tile_y + kDpTileH + kDpGap;
        }
        const float heading = (16.0f + 28.0f + 16.0f) * dp; // 16 above a titleLarge line, 16 under
        if (!m.cast.empty())
        {
            L.cast_y = y;
            DpRow row;
            row.scrolls = true;
            for (int i = 0; i < static_cast<int>(m.cast.size()); ++i)
                row.items.push_back({DpWhat::cast, i, 0,
                                     {kDpLeft + static_cast<float>(i) * (kDpCardW + 12.0f * dp), y + heading, kDpCardW, kDpCardH}});
            row.content_w = static_cast<float>(m.cast.size()) * (kDpCardW + 12.0f * dp) - 12.0f * dp;
            L.rows.push_back(row);
            y += heading + kDpCardH + (12.0f + 28.0f + 4.0f + 24.0f) * dp + kDpGap;
        }
        if (!m.schedule.empty())
        {
            L.schedule_y = y;
            const int shown = std::min<int>(5, static_cast<int>(m.schedule.front().rows.size()));
            const float h = (16.0f + static_cast<float>(shown) * 44.0f + static_cast<float>(shown) * 8.0f + 8.0f + 20.0f + 16.0f) * dp;
            DpRow row;
            row.items.push_back({DpWhat::schedule, 0, 0, {kDpLeft, y + (16.0f + 28.0f) * dp + kDpGap, kDpWide, h}});
            L.rows.push_back(row);
            y = row.items[0].r.y + h + kDpGap;
        }
        if (!m.franchises.empty())
        {
            L.franchise_y = y;
            y += (16.0f + 28.0f) * dp + kDpGap;
            for (int i = 0; i < static_cast<int>(m.franchises.size()); ++i)
            {
                DpRow row;
                row.items.push_back({DpWhat::franchise, i, 0, {kDpLeft, y, kDpWide, 64.0f * dp}});
                L.rows.push_back(row);
                y += 64.0f * dp + kDpGap;
            }
        }
        if (!m.related.empty() && rezka::App::instance().settings().show_recommendations)
        {
            L.related_y = y;
            DpRow row;
            row.scrolls = true;
            for (int i = 0; i < static_cast<int>(m.related.size()); ++i)
                row.items.push_back({DpWhat::related, i, 0,
                                     {kDpLeft + static_cast<float>(i) * (kDpCardW + 12.0f * dp), y + heading, kDpCardW, kDpCardH}});
            row.content_w = static_cast<float>(m.related.size()) * (kDpCardW + 12.0f * dp) - 12.0f * dp;
            L.rows.push_back(row);
            y += heading + kDpCardH + (12.0f + 28.0f + 4.0f + 24.0f) * dp + kDpGap;
        }
        L.bottom = y - kDpGap + kDpEdge;
        return L;
    }

    // Where the focus is, kept inside the layout as it changes under it.
    const DpItem *dp_focused(const DpLayout &L) const
    {
        if (L.rows.empty())
            return nullptr;
        const int row = std::clamp(dp_row_, 0, static_cast<int>(L.rows.size()) - 1);
        const DpRow &r = L.rows[static_cast<std::size_t>(row)];
        if (r.items.empty())
            return nullptr;
        return &r.items[static_cast<std::size_t>(std::clamp(dp_col_, 0, static_cast<int>(r.items.size()) - 1))];
    }
    // A wide row's offset: Compose rests the focused item 30% in from the left.
    static float dp_row_offset(const DpRow &row, int col)
    {
        if (!row.scrolls || row.items.empty())
            return 0.0f;
        const DpItem &item = row.items[static_cast<std::size_t>(std::clamp(col, 0, static_cast<int>(row.items.size()) - 1))];
        const float view = kWidth - 2.0f * kDpEdge;
        const float content = row.content_w + 2.0f * (kDpLeft - kDpEdge);
        return std::clamp(item.r.x - kDpEdge - 0.3f * view, 0.0f, std::max(0.0f, content - view));
    }
    int dp_row_of(const DpLayout &L, DpWhat what) const
    {
        for (int i = 0; i < static_cast<int>(L.rows.size()); ++i)
            if (!L.rows[static_cast<std::size_t>(i)].items.empty() && L.rows[static_cast<std::size_t>(i)].items[0].what == what)
                return i;
        return -1;
    }

    void dp_open_dialog(DpDialog dialog, app::Feedback &feedback)
    {
        dp_bookmarks_asked_ = false;
        dp_dialog_ = dialog;
        dp_dialog_focus_ = 0;
        dp_dialog_scroll_.snap(0.0f);
        feedback.play(audio::Cue::modal_open);
    }
    // A list of the site by its address, in the grid the search results use.
    void dp_open_list(const std::string &title, const std::string &url, app::Feedback &feedback)
    {
        if (url.empty())
        {
            tv_refuse(feedback, false);
            return;
        }
        tv_list_title_ = title;
        tv_results_back_ = SearchBack::home;
        page_open_ = false;
        dp_stack_.clear();
        dp_dialog_ = DpDialog::none;
        tv_zone_ = TvZone::grid;
        tv_focus_ = 0;
        tv_tab_delay_ = -1.0f;
        tv_scroll_.snap(0.0f);
        tv_cards_.clear();
        home_layout_ = false;
        // A list of the site, a page at a time (m8.V.e: the address, /page/N)
        const bool pages = url.find('?') == std::string::npos;
        rezka::App::instance().paged_list_async(title, [url, pages](rezka::Client &, int page) {
            return pages ? rezka::Client::extended_search_path(url, page) : url;
        });
        feedback.play(audio::Cue::select);
    }
    // Another title's page over this one; Circle comes back.
    void dp_open_title(const rezka::CatalogItem &item, app::Feedback &feedback)
    {
        const int index = rezka::App::instance().adopt_catalog_item(item);
        if (index < 0 || index == page_item_)
        {
            tv_refuse(feedback, false);
            return;
        }
        std::vector<int> stack = dp_stack_;
        stack.push_back(page_item_);
        open_page(index, false, feedback);
        dp_stack_ = std::move(stack);
    }

    void dp_update_dialog(const InputFrame &input, app::Feedback &feedback)
    {
        const PageModel &m = page_model_;
        const bool listing = dp_dialog_ == DpDialog::links || dp_dialog_ == DpDialog::bookmarks;
        if (input.is_pressed(Action::back) || (!listing && (input.is_pressed(Action::confirm) || input.nav == Direction::left)))
        {
            dp_dialog_ = DpDialog::none;
            feedback.play(audio::Cue::modal_close);
            return;
        }
        if (dp_dialog_ == DpDialog::links)
        {
            const int count = static_cast<int>(dp_link_names(dp_dialog_list_).size());
            if (input.nav == Direction::up || input.nav == Direction::down)
            {
                const int next = dp_dialog_focus_ + (input.nav == Direction::down ? 1 : -1);
                if (next < 0 || next >= count)
                    tv_refuse(feedback, input.nav_repeat);
                else
                {
                    dp_dialog_focus_ = next;
                    tv_tick(feedback, kWidth * 0.5f);
                }
            }
            if (input.is_pressed(Action::confirm) && dp_dialog_focus_ < count)
                dp_open_list(dp_link_names(dp_dialog_list_)[static_cast<std::size_t>(dp_dialog_focus_)],
                             dp_link_url(dp_dialog_list_, dp_dialog_focus_), feedback);
            return;
        }
        if (dp_dialog_ == DpDialog::bookmarks)
        {
            // A folder pressed is toggled on the site and stays in the list,
            // its check following the answer, as the app's dialog does.
            const int count = static_cast<int>(m.bookmarks.size());
            // A page that gave no folders (still loading, or none on it): the account's own
            if (count == 0 && m.loaded && !dp_bookmarks_asked_)
            {
                dp_bookmarks_asked_ = true;
                rezka::App::instance().load_bookmark_choices_async(page_item_);
            }
            if (input.nav == Direction::up || input.nav == Direction::down)
            {
                const int next = dp_dialog_focus_ + (input.nav == Direction::down ? 1 : -1);
                if (next < 0 || next >= count)
                    tv_refuse(feedback, input.nav_repeat);
                else
                {
                    dp_dialog_focus_ = next;
                    tv_tick(feedback, kWidth * 0.5f);
                }
            }
            if (input.is_pressed(Action::confirm))
            {
                if (dp_dialog_focus_ < count && !m.bookmarks[static_cast<std::size_t>(dp_dialog_focus_)].pending)
                {
                    rezka::App::instance().toggle_bookmark_folder_async(
                        page_item_, m.bookmarks[static_cast<std::size_t>(dp_dialog_focus_)].id);
                    feedback.play(audio::Cue::toggle);
                }
                else
                    tv_refuse(feedback, false);
            }
            return;
        }
        // The other dialogs scroll, 150 px a press as the app's description does
        (void)m;
        if (input.nav == Direction::down)
            dp_dialog_scroll_.target = std::min(dp_dialog_scroll_.target + 150.0f, std::max(0.0f, dp_dialog_content_ - dp_dialog_view_));
        else if (input.nav == Direction::up)
            dp_dialog_scroll_.target = std::max(0.0f, dp_dialog_scroll_.target - 150.0f);
    }

    void dp_update(const InputFrame &input, app::Feedback &feedback)
    {
        if (dp_dialog_ != DpDialog::none)
        {
            dp_update_dialog(input, feedback);
            return;
        }
        if (comments_open_)
        {
            cm_update(input, feedback);
            return;
        }
        if (input.is_pressed(Action::back))
        {
            if (!dp_stack_.empty())
            {
                const int index = dp_stack_.back();
                std::vector<int> stack(dp_stack_.begin(), dp_stack_.end() - 1);
                open_page(index, false, feedback);
                dp_stack_ = std::move(stack);
            }
            else
                page_open_ = false;
            feedback.play(audio::Cue::back);
            return;
        }
        const DpLayout L = dp_layout();
        const int rows = static_cast<int>(L.rows.size());
        if (rows == 0)
            return;
        dp_row_ = std::clamp(dp_row_, 0, rows - 1);
        dp_cols_.resize(static_cast<std::size_t>(rows), -1);
        const DpRow &row = L.rows[static_cast<std::size_t>(dp_row_)];
        dp_col_ = std::clamp(dp_col_, 0, std::max(0, static_cast<int>(row.items.size()) - 1));
        const DpItem here = row.items[static_cast<std::size_t>(dp_col_)];

        if (input.nav == Direction::left || input.nav == Direction::right)
        {
            // Along the row; a link row that wrapped is still one run of chips
            const int next = dp_col_ + (input.nav == Direction::right ? 1 : -1);
            if (next < 0 || next >= static_cast<int>(row.items.size()))
                tv_refuse(feedback, input.nav_repeat);
            else
            {
                dp_col_ = next;
                dp_lift_ = 0.0f;
                tv_tick(feedback, row.items[static_cast<std::size_t>(next)].r.cx());
            }
        }
        else if (input.nav == Direction::up || input.nav == Direction::down)
        {
            const int next = dp_row_ + (input.nav == Direction::down ? 1 : -1);
            if (next < 0 || next >= rows)
                tv_refuse(feedback, input.nav_repeat);
            else
            {
                dp_cols_[static_cast<std::size_t>(dp_row_)] = dp_col_;
                const DpRow &to = L.rows[static_cast<std::size_t>(next)];
                int col = dp_cols_[static_cast<std::size_t>(next)];
                if (col < 0 || col >= static_cast<int>(to.items.size()))
                {
                    // A row not visited yet: the item nearest on screen
                    const float from_x = here.r.cx() - dp_row_offset(row, dp_col_);
                    float best = 1e9f;
                    col = 0;
                    for (int i = 0; i < static_cast<int>(to.items.size()); ++i)
                    {
                        const float d = std::fabs(to.items[static_cast<std::size_t>(i)].r.cx() - from_x);
                        if (d < best)
                            best = d, col = i;
                    }
                    if (to.scrolls)
                        col = 0;
                }
                dp_row_ = next;
                dp_col_ = col;
                dp_lift_ = 0.0f;
                tv_tick(feedback, to.items[static_cast<std::size_t>(col)].r.cx());
            }
        }
        if (!input.is_pressed(Action::confirm))
            return;
        const PageModel &m = page_model_;
        switch (here.what)
        {
        case DpWhat::poster:
            dp_open_dialog(DpDialog::image, feedback);
            break;
        case DpWhat::watch:
            if (m.pending || !m.loaded)
                tv_refuse(feedback, false);
            else
                wd_open(false, feedback);
            break;
        case DpWhat::resume:
            if (m.has_resume)
            {
                const rezka::ContinueEntry &e = m.resume;
                translator_idx_ = e.translator_idx;
                quality_idx_ = e.quality_idx;
                season_idx_ = e.season_idx;
                episode_idx_ = e.episode_idx;
                feedback.play(audio::Cue::select);
                rezka::App::instance().play_title_async(page_item_, translator_idx_, quality_idx_, season_idx_,
                                                        episode_idx_, e.t);
            }
            break;
        case DpWhat::bookmark:
            toggle_bookmark(feedback);
            break;
        case DpWhat::stats:
            dp_open_dialog(DpDialog::stats, feedback);
            break;
        case DpWhat::qr:
            open_qr(m.url, m.title, feedback);
            break;
        case DpWhat::chip:
            if (here.a == 5)
                dp_open_list(m.year, m.year_url, feedback);
            else if (here.a == 6)
                cm_open(feedback);
            else
                feedback.play(audio::Cue::select);
            break;
        case DpWhat::link:
            dp_open_list(dp_link_names(here.a)[static_cast<std::size_t>(here.b)], dp_link_url(here.a, here.b), feedback);
            break;
        case DpWhat::link_all:
            dp_dialog_list_ = here.a;
            dp_open_dialog(DpDialog::links, feedback);
            break;
        case DpWhat::desc:
            if (dp_desc_more_)
            {
                dp_dialog_title_ = "Описание";
                dp_dialog_text_ = m.description;
                dp_open_dialog(DpDialog::text, feedback);
            }
            break;
        case DpWhat::trailer:
            // The app hands the address to YouTube; a console hands it to a phone.
            open_qr("https://www.youtube.com/watch?v=" + m.trailer_id, "Трейлер на YouTube", feedback);
            break;
        case DpWhat::cast:
            if (here.a < static_cast<int>(m.cast.size()))
                open_filmography(m.cast[static_cast<std::size_t>(here.a)].url, m.cast[static_cast<std::size_t>(here.a)].name, feedback);
            break;
        case DpWhat::schedule:
            dp_open_dialog(DpDialog::schedule, feedback);
            break;
        case DpWhat::franchise:
            if (here.a < static_cast<int>(m.franchises.size()) && !m.franchises[static_cast<std::size_t>(here.a)].current)
            {
                rezka::CatalogItem item;
                item.url = m.franchises[static_cast<std::size_t>(here.a)].url;
                item.title = m.franchises[static_cast<std::size_t>(here.a)].title;
                dp_open_title(item, feedback);
            }
            break;
        case DpWhat::related:
            if (here.a < static_cast<int>(m.related.size()))
                dp_open_title(m.related[static_cast<std::size_t>(here.a)], feedback);
            break;
        }
    }

    void dp_animate(float dt)
    {
        dp_lift_ += (1.0f - dp_lift_) * std::min(1.0f, dt * 14.0f);
        dp_dialog_in_.target = dp_dialog_ != DpDialog::none ? 1.0f : 0.0f;
        dp_dialog_in_.update(dt, omega(16.0f));
        dp_dialog_scroll_.update(dt, omega(14.0f));
        if (!page_open_)
            return;
        const DpLayout L = dp_layout();
        if (L.rows.empty())
            return;
        dp_row_ = std::clamp(dp_row_, 0, static_cast<int>(L.rows.size()) - 1);
        const DpRow &row = L.rows[static_cast<std::size_t>(dp_row_)];
        const DpItem *item = dp_focused(L);
        // The hero keeps itself whole on screen; below it the focus rests 30% down.
        float target = 0.0f;
        if (item && !row.hero)
            target = std::clamp(item->r.y - 0.3f * kHeight, 0.0f, std::max(0.0f, L.bottom - kHeight));
        dp_scroll_.target = target;
        dp_scroll_.update(dt, omega(12.0f));
        dp_row_x_.resize(L.rows.size());
        for (std::size_t i = 0; i < L.rows.size(); ++i)
        {
            const int col = static_cast<int>(i) == dp_row_ ? dp_col_ : (i < dp_cols_.size() ? std::max(0, dp_cols_[i]) : 0);
            dp_row_x_[i].target = dp_row_offset(L.rows[i], col);
            dp_row_x_[i].update(dt, omega(12.0f));
        }
    }

    // ---- drawing -----------------------------------------------------------

    // A tv-material Surface: the plate, and the colour its content takes.
    // `lift` is how far the focus has arrived on it (0 without it).
    Color dp_surface(gfx::DrawList &list, const Rect &r, float radius, float lift, float scale, Color rest_fill,
                     Color rest_content, float rest_border = 0.0f, Color border = {}, float focus_border = 0.0f,
                     Color focus_fill = tv_on_surface(), Color focus_content = tv_inverse_on_surface()) const
    {
        const Rect grown = scaled(r, 1.0f + (scale - 1.0f) * lift);
        const Color fill = mix(rest_fill, focus_fill, lift);
        const float width = rest_border + (focus_border - rest_border) * lift;
        if (width > 0.05f)
            list.bordered_rect(grown, radius, fill, width, border);
        else if (fill.a > 0.004f)
            list.rounded_rect(grown, radius, fill);
        return mix(rest_content, focus_content, lift);
    }
    // An image cropped to fill r, as ContentScale.Crop does, from the photo cache.
    bool dp_photo(gfx::DrawList &list, const std::string &url, const Rect &r, float radius, Color tint = kWhite,
                  float aspect = 2.0f / 3.0f) const
    {
        const std::uint32_t texture = url.empty() ? 0 : rezka::App::instance().photo_texture(url);
        if (texture == 0)
            return false;
        const float want = r.w / r.h;
        Rect uv = gfx::kFullUv;
        if (want > aspect)
            uv = {0.0f, (1.0f - aspect / want) * 0.5f, 1.0f, aspect / want};
        else
            uv = {(1.0f - want / aspect) * 0.5f, 0.0f, want / aspect, 1.0f};
        list.image(texture, r, uv, tint, radius);
        return true;
    }
    void dp_heading(gfx::DrawList &list, const char *text, float y) const
    {
        tv_text(list, tv_regular(), text, kDpLeft, y + 16.0f * kTvDp, 22.0f * kTvDp, 28.0f * kTvDp, tv_on_surface());
    }
    // A bold label and its value on one 18 sp line (Слоган, Дата выхода).
    void dp_fact(gfx::DrawList &list, const char *label, const std::string &value, float y) const
    {
        const float size = 18.0f * kTvDp;
        const float x = kDpLeft + tv_text(list, tv_medium(), label, kDpLeft, y + 8.0f * kTvDp, size, 22.0f * kTvDp,
                                          tv_on_surface());
        tv_text(list, tv_regular(), tv_regular().font->fit(value, size, kDpWide - (x - kDpLeft) - 8.0f * kTvDp),
                x + 8.0f, y + 8.0f * kTvDp, size, 22.0f * kTvDp, tv_on_surface().with_alpha(0.8f));
    }

    void dp_draw_item(gfx::DrawList &list, const DpItem &item, float lift) const
    {
        const PageModel &m = page_model_;
        const float dp = kTvDp;
        const Rect &r = item.r;
        switch (item.what)
        {
        case DpWhat::poster:
        {
            const Rect grown = scaled(r, 1.0f + 0.1f * lift);
            if (!dp_photo(list, m.cover, grown, 12.0f * dp))
            {
                const std::uint32_t cover = item_cover(page_item_);
                if (cover != 0)
                    list.image(cover, grown, cover_uv(grown), kWhite, 12.0f * dp);
                else
                    list.rounded_rect(grown, 12.0f * dp, tv_surface_variant());
            }
            if (lift > 0.01f)
                list.bordered_rect(grown, 12.0f * dp, kClear, 2.0f * dp, tv_border().with_alpha(lift));
            break;
        }
        case DpWhat::watch:
        {
            // The one filled button: primary at rest, the focus turns it light
            const Color content = dp_surface(list, r, r.h * 0.5f, lift, 1.1f,
                                             m.pending ? tv_surface_variant().with_alpha(0.4f) : tv_primary(),
                                             m.pending ? tv_on_surface().with_alpha(0.4f) : tv_on_primary());
            const Rect g = scaled(r, 1.0f + 0.1f * lift);
            tv_icon(list, m.pending ? kTvIconAccessTime : kTvIconPlayArrow, g.x + 16.0f * dp, g.cy() - 10.0f * dp, 20.0f * dp, content);
            tv_text(list, tv_medium(), m.pending ? "Ожидается" : "Смотреть", g.x + 44.0f * dp, g.cy() - 10.0f * dp,
                    14.0f * dp, 20.0f * dp, content, gfx::Align::left, 0.1f * dp);
            break;
        }
        case DpWhat::resume:
        {
            const Color content = dp_surface(list, r, r.h * 0.5f, lift, 1.1f, kClear, tv_on_surface().with_alpha(0.8f),
                                             1.5f * dp, mix(tv_on_surface_variant().with_alpha(0.4f), tv_on_surface_variant(), lift),
                                             1.65f * dp);
            tv_text(list, tv_medium(), "Продолжить *", r.cx(), r.cy() - 10.0f * dp, 14.0f * dp, 20.0f * dp, content,
                    gfx::Align::center, 0.1f * dp);
            break;
        }
        case DpWhat::bookmark:
        case DpWhat::stats:
        case DpWhat::qr:
        {
            const Color content = dp_surface(list, r, r.h * 0.5f, lift, 1.1f, kClear, tv_on_surface(), 2.0f * dp,
                                             mix(tv_on_surface_variant().with_alpha(0.4f), tv_on_surface(), lift), 2.0f * dp);
            bool saved = false;
            if (m.signed_in)
            {
                for (const rezka::BookmarkFolder &f : m.bookmarks)
                    saved = saved || f.checked;
            }
            else
                saved = rezka::App::instance().is_favorite(m.url);
            const int icon = item.what == DpWhat::qr ? kTvIconQrCode2 : item.what == DpWhat::stats ? kTvIconStackedBarChart
                             : saved ? kTvIconBookmark : kTvIconBookmarkBorder;
            tv_icon(list, icon, r.cx() - 10.0f * dp, r.cy() - 10.0f * dp, 20.0f * dp, content);
            break;
        }
        case DpWhat::chip:
        {
            std::string value, votes;
            const char *label = "";
            Color color;
            dp_chip_text(item.a, value, votes, label, color);
            const Color content = dp_surface(list, r, 12.0f * dp, lift, 1.1f, tv_surface(), tv_on_surface());
            const Rect g = scaled(r, 1.0f + 0.1f * lift);
            float top = tv_medium().measure(value, 24.0f * dp);
            if (!votes.empty())
                top += 4.0f * dp + tv_regular().measure(votes, 12.0f * dp);
            float x = g.cx() - top * 0.5f;
            x += tv_text(list, tv_medium(), value, x, g.cy() - 23.0f * dp, 24.0f * dp, 24.0f * dp, mix(color, content, lift));
            if (!votes.empty())
                tv_text(list, tv_regular(), votes, x + 4.0f * dp, g.cy() - 13.0f * dp, 12.0f * dp, 14.0f * dp,
                        content.with_alpha(0.6f));
            tv_text(list, tv_regular(), label, g.cx(), g.cy() + 9.0f * dp, 14.0f * dp, 14.0f * dp, content.with_alpha(0.6f),
                    gfx::Align::center);
            break;
        }
        case DpWhat::link:
        {
            const Color content = dp_surface(list, r, 8.0f * dp, lift, 1.0f, kClear, tv_on_surface_variant(), 1.0f * dp,
                                             tv_border().with_alpha(1.0f - lift), 0.0f);
            tv_text(list, tv_regular(), dp_link_names(item.a)[static_cast<std::size_t>(item.b)], r.x + 16.0f * dp,
                    r.y + 8.0f * dp, 18.0f * dp, 22.0f * dp, content);
            break;
        }
        case DpWhat::link_all:
        {
            const Color content = dp_surface(list, r, 12.0f * dp, lift, 1.0f, tv_surface(), tv_on_surface());
            const float size = 18.0f * dp;
            const std::string label = dp_link_label(item.a);
            const std::string hint = "Нажмите, чтобы развернуть";
            const std::vector<std::string> lines =
                tv_regular().font->wrap(label + " " + dp_joined(item.a) + hint, size, r.w - 32.0f * dp);
            float y = r.y + 16.0f * dp;
            for (std::size_t i = 0; i < lines.size(); ++i, y += 22.0f * dp)
            {
                std::string line = lines[i];
                float x = r.x + 16.0f * dp;
                if (i == 0 && line.rfind(label, 0) == 0)
                {
                    x += tv_text(list, tv_medium(), label, x, y, size, 22.0f * dp, content);
                    line.erase(0, label.size());
                }
                // The hint closes the sentence in the accent colour
                const std::size_t at = i + 1 == lines.size() ? line.rfind(hint) : std::string::npos;
                if (at != std::string::npos)
                {
                    x += tv_text(list, tv_regular(), line.substr(0, at), x, y, size, 22.0f * dp, content.with_alpha(0.8f));
                    tv_text(list, tv_medium(), hint, x, y, size, 22.0f * dp, mix(tv_primary(), tv_inverse_primary(), lift));
                }
                else
                    tv_text(list, tv_regular(), line, x, y, size, 22.0f * dp, content.with_alpha(0.8f));
            }
            break;
        }
        case DpWhat::desc:
        {
            const Color content = dp_surface(list, r, 16.0f * dp, lift, 1.05f, tv_surface(), tv_on_surface(), 1.0f * dp,
                                             tv_border().with_alpha(1.0f - lift), 0.0f);
            const Rect g = scaled(r, 1.0f + 0.05f * lift);
            float y = g.y + 12.0f * dp;
            for (std::size_t i = 0; i < dp_desc_lines_.size(); ++i, y += 24.0f * dp)
            {
                const float w = tv_text(list, tv_regular(), dp_desc_lines_[i], g.x + 16.0f * dp, y, 16.0f * dp, 24.0f * dp,
                                        content, gfx::Align::left, 0.5f * dp * 0.0f);
                if (dp_desc_more_ && i + 1 == dp_desc_lines_.size())
                {
                    const float x = g.x + 16.0f * dp + w;
                    const float dots = tv_text(list, tv_regular(), "… ", x, y, 16.0f * dp, 24.0f * dp, content);
                    tv_text(list, tv_medium(), "Нажмите, чтобы развернуть", x + dots, y, 16.0f * dp, 24.0f * dp,
                            mix(tv_primary(), tv_inverse_primary(), lift));
                }
            }
            break;
        }
        case DpWhat::trailer:
        {
            const Rect g = scaled(r, 1.0f + 0.05f * lift);
            if (!dp_photo(list, "https://img.youtube.com/vi/" + m.trailer_id + "/hqdefault.jpg", g, 16.0f * dp, kWhite, 4.0f / 3.0f))
                list.rounded_rect(g, 16.0f * dp, tv_surface_variant());
            list.gradient_rect(g, 16.0f * dp, tv_surface().with_alpha(0.5f), tv_surface().with_alpha(0.0f));
            if (tv_youtube_ != 0)
                list.image(tv_youtube_, {g.cx() - 32.0f * dp, g.cy() - 32.0f * dp, 64.0f * dp, 64.0f * dp}, gfx::kFullUv, kWhite);
            list.bordered_rect(g, 16.0f * dp, kClear, (1.0f + lift) * dp, tv_border());
            break;
        }
        case DpWhat::cast:
        case DpWhat::related:
        {
            const Rect grown = scaled(r, 1.0f + 0.1f * lift);
            const bool cast = item.what == DpWhat::cast;
            const std::size_t i = static_cast<std::size_t>(item.a);
            const std::string &url = cast ? m.cast[i].photo : m.related[i].cover_url;
            if (!dp_photo(list, url, grown, 8.0f * dp * (1.0f + 0.1f * lift)))
                list.rounded_rect(grown, 8.0f * dp, tv_surface_variant());
            if (lift > 0.01f)
                list.bordered_rect(grown, 8.0f * dp * 1.1f, kClear, 3.0f * dp, tv_border().with_alpha(lift));
            float y = r.y + r.h + 12.0f * dp;
            if (cast)
            {
                // The name and the job are centred under the photo
                for (int k = 0; k < 2; ++k, y += 14.0f * dp)
                    if (!dp_cast_names_[i * 2 + static_cast<std::size_t>(k)].empty())
                        tv_text(list, tv_medium(), dp_cast_names_[i * 2 + static_cast<std::size_t>(k)], r.cx(), y, 14.0f * dp,
                                14.0f * dp, tv_on_surface(), gfx::Align::center);
                if (!m.cast[i].job.empty())
                    tv_text(list, tv_regular(), tv_regular().font->fit(m.cast[i].job, 12.0f * dp, r.w), r.cx(), y + 4.0f * dp,
                            12.0f * dp, 12.0f * dp, tv_on_surface().with_alpha(0.6f), gfx::Align::center);
            }
            else
            {
                for (int k = 0; k < 2; ++k, y += 14.0f * dp)
                    if (!dp_related_lines_[i * 4 + static_cast<std::size_t>(k)].empty())
                        tv_text(list, tv_medium(), dp_related_lines_[i * 4 + static_cast<std::size_t>(k)], r.x, y, 14.0f * dp,
                                14.0f * dp, tv_on_surface());
                y += 4.0f * dp;
                for (int k = 2; k < 4; ++k, y += 12.0f * dp)
                    if (!dp_related_lines_[i * 4 + static_cast<std::size_t>(k)].empty())
                        tv_text(list, tv_regular(), dp_related_lines_[i * 4 + static_cast<std::size_t>(k)], r.x, y, 12.0f * dp,
                                12.0f * dp, tv_on_surface().with_alpha(0.6f));
            }
            break;
        }
        case DpWhat::schedule:
        {
            const Color content = dp_surface(list, r, 12.0f * dp, lift, 1.0f, tv_surface(), tv_on_surface());
            float y = r.y + 16.0f * dp;
            const std::vector<rezka::ScheduleRow> &rows = m.schedule.front().rows;
            for (std::size_t i = 0; i < rows.size() && i < 5; ++i, y += 52.0f * dp)
                dp_schedule_row(list, rows[i], r.x + 16.0f * dp, y, r.w - 32.0f * dp, content);
            y += 8.0f * dp;
            const Color accent = mix(tv_primary(), tv_inverse_primary(), lift);
            tv_icon(list, kTvIconCalendarMonth, r.x + 16.0f * dp, y, 20.0f * dp, accent);
            tv_text(list, tv_medium(), "Нажмите \"ОК\", чтобы посмотреть полное расписание", r.x + 52.0f * dp, y, 14.0f * dp,
                    20.0f * dp, accent, gfx::Align::left, 0.1f * dp);
            break;
        }
        case DpWhat::franchise:
        {
            const rezka::FranchisePart &part = m.franchises[static_cast<std::size_t>(item.a)];
            const Color content = dp_surface(list, r, 8.0f * dp, lift, 1.0f,
                                             part.current ? tv_secondary_container().with_alpha(0.4f) : kClear,
                                             part.current ? tv_on_secondary_container() : tv_on_surface());
            const float number_w = tv_text(list, tv_medium(), std::to_string(part.number), r.x + 16.0f * dp, r.cy() - 14.0f * dp,
                                           22.0f * dp, 28.0f * dp, content);
            const float x = r.x + 16.0f * dp + std::max(number_w, 24.0f * dp) + 16.0f * dp;
            tv_text(list, tv_medium(), part.year, x, r.y + 12.0f * dp, 12.0f * dp, 16.0f * dp, content.with_alpha(0.8f), gfx::Align::left,
                    0.5f * dp);
            tv_text(list, tv_medium(), tv_medium().font->fit(part.title, 16.0f * dp, r.w - (x - r.x) - 96.0f * dp), x,
                    r.y + 30.0f * dp, 16.0f * dp, 20.0f * dp, content, gfx::Align::left, 0.2f * dp);
            if (part.rating > 0.0f)
            {
                char text[16];
                std::snprintf(text, sizeof(text), "%.2f", part.rating);
                std::string value = text;
                if (value.size() > 3 && value.back() == '0')
                    value.pop_back();
                const Color color = mix(tv_rating_color(part.rating), content, lift);
                tv_icon(list, kTvIconStarRate, r.x + r.w - 16.0f * dp - 18.0f * dp, r.cy() - 9.0f * dp, 18.0f * dp, color);
                tv_text(list, tv_medium(), value, r.x + r.w - 16.0f * dp - 20.0f * dp, r.cy() - 10.0f * dp, 16.0f * dp, 20.0f * dp,
                        color, gfx::Align::right);
            }
            break;
        }
        }
    }

    // An episode of the schedule: whether it is out, its titles, its number and date.
    void dp_schedule_row(gfx::DrawList &list, const rezka::ScheduleRow &row, float x, float y, float w, Color content) const
    {
        const float dp = kTvDp;
        tv_icon(list, row.exists ? kTvIconCheck : kTvIconSchedule, x, y + 12.0f * dp, 20.0f * dp, content);
        const float text_w = w - 36.0f * dp - 200.0f * dp;
        tv_text(list, tv_medium(), tv_medium().font->fit(row.title, 16.0f * dp, text_w), x + 36.0f * dp, y, 16.0f * dp, 20.0f * dp,
                content, gfx::Align::left, 0.2f * dp);
        tv_text(list, tv_regular(), tv_regular().font->fit(row.subtitle, 14.0f * dp, text_w), x + 36.0f * dp, y + 24.0f * dp,
                14.0f * dp, 20.0f * dp, content.with_alpha(0.6f));
        tv_text(list, tv_medium(), row.episode, x + w, y + 6.0f * dp, 12.0f * dp, 16.0f * dp, content, gfx::Align::right, 0.5f * dp);
        tv_text(list, tv_medium(), row.date, x + w, y + 22.0f * dp, 12.0f * dp, 16.0f * dp, content, gfx::Align::right, 0.5f * dp);
    }

    void dp_draw(gfx::DrawList &list) const
    {
        const float shown = page_.value;
        if (shown < 0.004f)
            return;
        const PageModel &m = page_model_;
        const float dp = kTvDp;
        const DpLayout L = dp_layout();
        list.push_opacity(tween::clamp01(shown * 1.4f));
        list.rounded_rect({0.0f, 0.0f, kWidth, kHeight}, 0.0f, tv_surface());
        list.push_clip({0.0f, 0.0f, kWidth, kHeight});
        list.push_transform(1.0f, 0.0f, 0.0f, 0.0f, -dp_scroll_.value);

        // The backdrop: the large poster from 200 dp in, under the app's two
        // scrims (the surface from the left up to 450 px, gone by 1000; and
        // rising from 600 px down to the hero's bottom edge).
        const Rect hero{kDpEdge, kDpEdge, kWidth - 2.0f * kDpEdge, kDpHeroH};
        const Rect art{hero.x + 200.0f * dp, hero.y, hero.w - 200.0f * dp, hero.h};
        if (!dp_photo(list, m.full_cover, art, 0.0f) && !dp_photo(list, m.cover, art, 0.0f))
        {
            const std::uint32_t cover = item_cover(page_item_);
            if (cover != 0)
            {
                const float h = (2.0f / 3.0f) / (art.w / art.h);
                list.image(cover, art, {0.0f, (1.0f - h) * 0.5f, 1.0f, h}, kWhite);
            }
        }
        list.rounded_rect({hero.x, hero.y, 450.0f, hero.h}, 0.0f, tv_surface());
        list.gradient_rect_h({hero.x + 450.0f, hero.y, 550.0f, hero.h}, 0.0f, tv_surface(), tv_surface().with_alpha(0.0f));
        list.gradient_rect({hero.x, hero.y + 600.0f, hero.w, hero.h - 600.0f + 1.0f}, 0.0f, tv_surface().with_alpha(0.0f), tv_surface());

        // What is not focusable
        if (m.blocked)
        {
            tv_icon(list, kTvIconBlock, kDpLeft, L.buttons_y + 8.0f * dp, 24.0f * dp, tv_on_surface());
            tv_text(list, tv_regular(), "Видео недоступно в вашем регионе", kDpLeft + 32.0f * dp, L.buttons_y + 8.0f * dp, 16.0f * dp,
                    24.0f * dp, tv_on_surface(), gfx::Align::left, 0.5f * dp);
        }
        if (L.resume_y > 0.0f)
        {
            // "* Продолжить смотреть сезон 1 серия 2 (voice) с 12:34", as much of it as is known
            const rezka::ContinueEntry &e = m.resume;
            char when[24];
            const int t = static_cast<int>(e.t);
            if (t >= 3600)
                std::snprintf(when, sizeof(when), "%d:%02d:%02d", t / 3600, (t / 60) % 60, t % 60);
            else
                std::snprintf(when, sizeof(when), "%02d:%02d", t / 60, t % 60);
            std::string line = "* Продолжить смотреть ";
            if (m.is_series)
                line += "сезон " + std::to_string(e.season_idx + 1) + " серия " + std::to_string(e.episode_idx + 1) + " ";
            if (!m.resume_voice.empty())
                line += m.is_series ? "(" + m.resume_voice + ") " : m.resume_voice + " ";
            line += std::string("с ") + when;
            tv_text(list, tv_regular(), line, kDpLeft, L.resume_y, 12.0f * dp, 16.0f * dp, tv_on_surface(), gfx::Align::left, 0.2f * dp);
        }
        float y = L.title_y;
        for (const std::string &line : dp_title_lines_)
        {
            tv_text(list, tv_regular(), line, kDpLeft, y, 36.0f * dp, 44.0f * dp, tv_on_surface());
            y += 44.0f * dp;
        }
        if (L.original_y > 0.0f)
            tv_text(list, tv_regular(), tv_regular().font->fit(m.original, 22.0f * dp, kDpWide), kDpLeft, L.original_y, 22.0f * dp,
                    28.0f * dp, tv_on_surface().with_alpha(0.6f));
        if (L.tagline_y > 0.0f)
            dp_fact(list, "Слоган:", m.slogan, L.tagline_y);
        if (L.date_y > 0.0f)
            dp_fact(list, "Дата выхода:", m.release_date, L.date_y);
        for (int i = 0; i < 4; ++i)
            if (L.link_y[i] > 0.0f && static_cast<int>(dp_link_names(i).size()) <= (i < 2 ? 5 : i == 2 ? 1 : 2))
                tv_text(list, tv_medium(), dp_link_label(i), kDpLeft, L.link_y[i] + 12.0f * dp, 18.0f * dp, 22.0f * dp, tv_on_surface());
        dp_heading(list, "Описание", L.desc_y);
        if (!m.trailer_id.empty())
            tv_text(list, tv_regular(), "Трейлер", kDpLeft + kDpTileW + 20.0f * dp, L.desc_y + 16.0f * dp, 22.0f * dp, 28.0f * dp,
                    tv_on_surface());
        if (L.cast_y > 0.0f)
            dp_heading(list, "Актерский состав и съемочная группа", L.cast_y);
        if (L.schedule_y > 0.0f)
            dp_heading(list, "Расписание выхода серий", L.schedule_y);
        if (L.franchise_y > 0.0f)
            dp_heading(list, m.franchise_title.empty() ? "Все части франшизы" : m.franchise_title.c_str(), L.franchise_y);
        if (L.related_y > 0.0f)
            dp_heading(list, "Смотрите также", L.related_y);

        // The rows, the focused item last in its row so that it lies over its neighbours
        const int focus_row = dp_dialog_ == DpDialog::none && !comments_open_ ? std::clamp(dp_row_, 0, static_cast<int>(L.rows.size()) - 1) : -1;
        for (int i = 0; i < static_cast<int>(L.rows.size()); ++i)
        {
            const DpRow &row = L.rows[static_cast<std::size_t>(i)];
            const float offset = static_cast<std::size_t>(i) < dp_row_x_.size() ? dp_row_x_[static_cast<std::size_t>(i)].value : 0.0f;
            const int focus = i == focus_row ? std::clamp(dp_col_, 0, static_cast<int>(row.items.size()) - 1) : -1;
            list.push_transform(1.0f, 0.0f, 0.0f, -offset, 0.0f);
            for (int k = 0; k < static_cast<int>(row.items.size()); ++k)
                if (k != focus)
                    dp_draw_item(list, row.items[static_cast<std::size_t>(k)], 0.0f);
            if (focus >= 0)
                dp_draw_item(list, row.items[static_cast<std::size_t>(focus)], dp_lift_);
            list.pop_transform();
        }
        list.pop_transform();
        list.pop_clip();
        list.pop_opacity();
    }

    // The dialogs' common shell: the app's Card (28 dp corners, the surface,
    // a 1 dp outline) over the dimmed page, with a title.
    Rect dp_card(gfx::DrawList &list, float w, float h, const std::string &title, bool centred_title = false) const
    {
        const float dp = kTvDp;
        const Rect card{(kWidth - w) * 0.5f, (kHeight - h) * 0.5f, w, h};
        list.rounded_rect({0.0f, 0.0f, kWidth, kHeight}, 0.0f, kBlack.with_alpha(0.6f));
        list.bordered_rect(card, 28.0f * dp, tv_surface(), 1.0f * dp, tv_border_variant());
        if (!title.empty())
            tv_text(list, tv_regular(), tv_regular().font->fit(title, 22.0f * dp, w - 40.0f * dp),
                    centred_title ? card.cx() : card.x + 20.0f * dp, card.y + 20.0f * dp, 22.0f * dp, 28.0f * dp, tv_on_surface(),
                    centred_title ? gfx::Align::center : gfx::Align::left);
        return card;
    }

    void dp_draw_dialog(gfx::DrawList &list) const
    {
        const float shown = dp_dialog_in_.value;
        if (shown < 0.01f || dp_dialog_ == DpDialog::none)
            return;
        const PageModel &m = page_model_;
        const float dp = kTvDp;
        list.push_opacity(tween::clamp01(shown));
        if (dp_dialog_ == DpDialog::image)
        {
            // The poster whole, over a dimmed copy of itself
            list.rounded_rect({0.0f, 0.0f, kWidth, kHeight}, 0.0f, tv_surface());
            const std::string &url = m.full_cover.empty() ? m.cover : m.full_cover;
            dp_photo(list, url, {0.0f, 0.0f, kWidth, kHeight}, 0.0f, kWhite.with_alpha(0.25f));
            const float h = kHeight - 48.0f * dp;
            dp_photo(list, url, {(kWidth - h * 2.0f / 3.0f) * 0.5f, 24.0f * dp, h * 2.0f / 3.0f, h}, 12.0f * dp);
        }
        else if (dp_dialog_ == DpDialog::text)
        {
            const float w = 640.0f * dp, h = kHeight - 48.0f * dp;
            const Rect card = dp_card(list, w, h, dp_dialog_title_);
            const std::vector<std::string> lines = tv_regular().font->wrap(dp_dialog_text_, 16.0f * dp, w - 80.0f * dp);
            const Rect view{card.x, card.y + 64.0f * dp, w, h - 84.0f * dp};
            dp_dialog_view_ = view.h;
            dp_dialog_content_ = static_cast<float>(lines.size()) * 24.0f * dp + 40.0f * dp;
            list.push_clip(view);
            float y = view.y + 20.0f * dp - dp_dialog_scroll_.value;
            for (const std::string &line : lines)
            {
                if (y > view.y - 48.0f && y < view.y + view.h)
                    tv_text(list, tv_regular(), line, card.x + 40.0f * dp, y, 16.0f * dp, 24.0f * dp, tv_on_surface());
                y += 24.0f * dp;
            }
            list.pop_clip();
        }
        else if (dp_dialog_ == DpDialog::stats)
        {
            const float w = 480.0f * dp;
            const float h = std::min(kHeight - 48.0f * dp, (84.0f + static_cast<float>(m.voice_stats.size()) * 44.0f) * dp);
            const Rect card = dp_card(list, w, h, "Статистика выбора озвучек");
            const Rect view{card.x, card.y + 64.0f * dp, w, h - 80.0f * dp};
            dp_dialog_view_ = view.h;
            dp_dialog_content_ = static_cast<float>(m.voice_stats.size()) * 44.0f * dp;
            list.push_clip(view);
            float y = view.y - dp_dialog_scroll_.value;
            for (const rezka::VoiceStat &stat : m.voice_stats)
            {
                char percent[16];
                std::snprintf(percent, sizeof(percent), "%.2f%%", stat.percent);
                tv_text(list, tv_regular(), tv_regular().font->fit(stat.title, 14.0f * dp, w - 130.0f * dp), card.x + 20.0f * dp, y,
                        14.0f * dp, 20.0f * dp, tv_on_surface());
                tv_text(list, tv_medium(), percent, card.x + w - 20.0f * dp, y, 14.0f * dp, 20.0f * dp, tv_on_surface(), gfx::Align::right);
                const Rect track{card.x + 20.0f * dp, y + 26.0f * dp, w - 40.0f * dp, 4.0f * dp};
                list.rounded_rect(track, 2.0f * dp, tv_surface_variant());
                list.rounded_rect({track.x, track.y, std::max(4.0f * dp, track.w * stat.percent / 100.0f), track.h}, 2.0f * dp, tv_primary());
                y += 44.0f * dp;
            }
            list.pop_clip();
        }
        else if (dp_dialog_ == DpDialog::schedule)
        {
            const float w = 640.0f * dp, h = kHeight - 48.0f * dp;
            const Rect card = dp_card(list, w, h, "Расписание выхода серий");
            const Rect view{card.x, card.y + 64.0f * dp, w, h - 84.0f * dp};
            dp_dialog_view_ = view.h;
            list.push_clip(view);
            float y = view.y - dp_dialog_scroll_.value;
            for (const rezka::ScheduleSeason &season : m.schedule)
            {
                tv_text(list, tv_medium(), tv_medium().font->fit(season.title, 16.0f * dp, w - 40.0f * dp), card.x + 20.0f * dp, y + 12.0f * dp,
                        16.0f * dp, 20.0f * dp, tv_primary(), gfx::Align::left, 0.2f * dp);
                y += 44.0f * dp;
                for (const rezka::ScheduleRow &row : season.rows)
                {
                    if (y > view.y - 60.0f * dp && y < view.y + view.h)
                        dp_schedule_row(list, row, card.x + 36.0f * dp, y, w - 72.0f * dp, tv_on_surface());
                    y += 52.0f * dp;
                }
            }
            dp_dialog_content_ = y + dp_dialog_scroll_.value - view.y;
            list.pop_clip();
        }
        else if (dp_dialog_ == DpDialog::links)
        {
            const std::vector<std::string> &names = dp_link_names(dp_dialog_list_);
            std::string title = dp_link_label(dp_dialog_list_);
            if (!title.empty() && title.back() == ':')
                title.pop_back();
            const float w = 480.0f * dp;
            const float item_h = 48.0f * dp;
            const float h = std::min(kHeight - 48.0f * dp, (84.0f * dp) + static_cast<float>(names.size()) * (item_h + 4.0f * dp));
            const Rect card = dp_card(list, w, h, title);
            const Rect view{card.x, card.y + 64.0f * dp, w, h - 80.0f * dp};
            const float focus_y = static_cast<float>(dp_dialog_focus_) * (item_h + 4.0f * dp);
            const float scroll = std::clamp(focus_y - view.h * 0.4f, 0.0f,
                                            std::max(0.0f, static_cast<float>(names.size()) * (item_h + 4.0f * dp) - view.h));
            list.push_clip(view);
            for (int i = 0; i < static_cast<int>(names.size()); ++i)
            {
                const Rect r{card.x + 20.0f * dp, view.y + static_cast<float>(i) * (item_h + 4.0f * dp) - scroll, w - 40.0f * dp, item_h};
                const bool focused = i == dp_dialog_focus_;
                if (focused)
                    list.rounded_rect(r, 8.0f * dp, tv_on_surface());
                tv_text(list, tv_medium(), tv_medium().font->fit(names[static_cast<std::size_t>(i)], 14.0f * dp, r.w - 32.0f * dp),
                        r.x + 16.0f * dp, r.cy() - 10.0f * dp, 14.0f * dp, 20.0f * dp, focused ? tv_inverse_on_surface() : tv_on_surface(),
                        gfx::Align::left, 0.1f * dp);
            }
            list.pop_clip();
        }
        else if (dp_dialog_ == DpDialog::bookmarks)
        {
            const std::vector<rezka::BookmarkFolder> &folders = m.bookmarks;
            const float w = 480.0f * dp;
            const float item_h = 48.0f * dp;
            const float rows = std::max(1.0f, static_cast<float>(folders.size()));
            const float h = std::min(kHeight - 48.0f * dp, (84.0f * dp) + rows * (item_h + 4.0f * dp));
            const Rect card = dp_card(list, w, h, "Закладки");
            const Rect view{card.x, card.y + 64.0f * dp, w, h - 80.0f * dp};
            if (folders.empty())
                tv_text(list, tv_regular(), "Загрузка…", card.cx(), view.y + 14.0f * dp, 14.0f * dp, 20.0f * dp,
                        tv_on_surface_variant(), gfx::Align::center);
            const float focus_y = static_cast<float>(dp_dialog_focus_) * (item_h + 4.0f * dp);
            const float scroll = std::clamp(focus_y - view.h * 0.4f, 0.0f,
                                            std::max(0.0f, static_cast<float>(folders.size()) * (item_h + 4.0f * dp) - view.h));
            list.push_clip(view);
            for (int i = 0; i < static_cast<int>(folders.size()); ++i)
            {
                const rezka::BookmarkFolder &f = folders[static_cast<std::size_t>(i)];
                const Rect r{card.x + 20.0f * dp, view.y + static_cast<float>(i) * (item_h + 4.0f * dp) - scroll, w - 40.0f * dp, item_h};
                const bool focused = i == dp_dialog_focus_;
                if (focused)
                    list.rounded_rect(r, 8.0f * dp, tv_on_surface());
                const Color ink = focused ? tv_inverse_on_surface() : tv_on_surface();
                if (f.pending)
                    list.arc(r.x + 28.0f * dp, r.cy(), 10.0f * dp, 2.5f * dp, clock_ * 5.0f, 4.4f, ink);
                else
                    tv_icon(list, f.checked ? kTvIconCheckBox : kTvIconCheckBoxBlank, r.x + 16.0f * dp, r.cy() - 12.0f * dp,
                            24.0f * dp, f.checked && !focused ? tv_primary() : ink);
                tv_text(list, tv_medium(), tv_medium().font->fit(f.name, 14.0f * dp, r.w - 72.0f * dp), r.x + 56.0f * dp,
                        r.cy() - 10.0f * dp, 14.0f * dp, 20.0f * dp, ink, gfx::Align::left, 0.1f * dp);
            }
            list.pop_clip();
        }
        list.pop_opacity();
    }

    // ---- the watch dialogs ---------------------------------------------------
    //
    // The app's SelectVoice(AndEpisode)Dialog: one card, 48 dp in from the
    // sides, with a flow of chips a section (Выберите озвучку / сезон /
    // серию), a hint and the Смотреть button; OK on a chip selects it, OK on
    // the selected chip moves on to the next choice. Then SelectVideoQuality:
    // a list of the qualities.

    struct WdItem
    {
        int section = 0; // 0 voice, 1 season, 2 episode, 3 the Смотреть button
        int index = 0;
        Rect r{}; // in the card's scrolling content; the button's is on the card
    };
    struct WdLayout
    {
        std::vector<WdItem> items;
        float header_y[3] = {-1.0f, -1.0f, -1.0f};
        float content_h = 0.0f;
        float view_h = 0.0f;
        Rect card{};
        bool hint = false;
    };

    void wd_open(bool, app::Feedback &feedback)
    {
        dialog_open_ = true;
        wd_quality_ = false;
        wd_focus_section_ = -1; // settled by the first layout
        wd_scroll_.snap(0.0f);
        feedback.play(audio::Cue::modal_open);
    }
    int wd_selected(int section) const
    {
        return section == 0 ? translator_idx_ : section == 1 ? season_idx_ : episode_idx_;
    }
    std::vector<std::string> wd_qualities() const
    {
        std::vector<std::string> names;
        auto &app = rezka::App::instance();
        std::lock_guard<std::mutex> lock(app.catalog_mutex);
        if (page_item_ >= 0 && page_item_ < static_cast<int>(app.movies.size()))
            for (const rezka::StreamQuality &q : app.movies[static_cast<std::size_t>(page_item_)].streams)
                names.push_back(q.quality);
        if (names.empty())
            names = {"1080p Ultra", "1080p", "720p", "480p", "360p"};
        return names;
    }
    // The chips' labels by section, from the title's details.
    void wd_labels(std::vector<std::string> out[3], std::vector<int> &premium) const
    {
        auto &app = rezka::App::instance();
        std::lock_guard<std::mutex> lock(app.catalog_mutex);
        if (page_item_ < 0 || page_item_ >= static_cast<int>(app.movies.size()))
            return;
        const rezka::VideoDetails &d = app.movies[static_cast<std::size_t>(page_item_)].details;
        if (d.translators.size() > 1 || (d.translators.size() == 1 && !d.translators[0].name.empty()))
            for (const rezka::Translator &t : d.translators)
            {
                out[0].push_back(t.name);
                premium.push_back(t.is_premium ? 1 : 0);
            }
        if (!d.is_series)
            return;
        for (const rezka::Season &s : d.seasons)
            out[1].push_back("Сезон " + s.id);
        if (!d.seasons.empty())
        {
            const rezka::Season &season = d.seasons[static_cast<std::size_t>(std::clamp(season_idx_, 0, static_cast<int>(d.seasons.size()) - 1))];
            for (const rezka::Episode &e : season.episodes)
                out[2].push_back("Серия " + e.id);
        }
    }
    WdLayout wd_layout(const std::vector<std::string> labels[3]) const
    {
        const float dp = kTvDp;
        WdLayout L;
        const float card_w = kWidth - 96.0f * dp;
        const float inner_w = card_w - 32.0f * dp - 4.0f * dp;
        float y = 0.0f;
        int choices = 0;
        for (int s = 0; s < 3; ++s)
        {
            if (labels[s].empty())
                continue;
            choices = std::max(choices, static_cast<int>(labels[s].size()));
            if (y > 0.0f)
                y += 12.0f * dp;
            L.header_y[s] = y;
            y += (24.0f + 20.0f + 8.0f + 12.0f + 2.0f) * dp;
            float x = 0.0f;
            for (int i = 0; i < static_cast<int>(labels[s].size()); ++i)
            {
                const float w = tv_medium().measure(labels[s][static_cast<std::size_t>(i)], 16.0f * dp, 0.5f * dp) + 32.0f * dp;
                if (x + w > inner_w && x > 0.0f)
                {
                    x = 0.0f;
                    y += 48.0f * dp;
                }
                L.items.push_back({s, i, {x, y, w, 40.0f * dp}});
                x += w + 8.0f * dp;
            }
            y += 42.0f * dp;
        }
        L.content_h = y;
        L.hint = choices > 1;
        const float foot = (L.hint ? 36.0f : 0.0f) * dp + 72.0f * dp;
        const float max_view = kHeight - 48.0f * dp - foot;
        L.view_h = std::min(L.content_h, max_view);
        const float card_h = L.view_h + foot;
        L.card = {48.0f * dp, (kHeight - card_h) * 0.5f, card_w, card_h};
        L.items.push_back({3, 0, {L.card.x + 16.0f * dp, L.card.y + card_h - 56.0f * dp, card_w - 32.0f * dp, 40.0f * dp}});
        return L;
    }
    int wd_find(const WdLayout &L, int section, int index) const
    {
        for (int i = 0; i < static_cast<int>(L.items.size()); ++i)
            if (L.items[static_cast<std::size_t>(i)].section == section && (section == 3 || L.items[static_cast<std::size_t>(i)].index == index))
                return i;
        return static_cast<int>(L.items.size()) - 1;
    }

    void wd_update(const InputFrame &input, app::Feedback &feedback)
    {
        if (wd_quality_)
        {
            const std::vector<std::string> names = wd_qualities();
            const int count = static_cast<int>(names.size());
            if (input.is_pressed(Action::back) || input.nav == Direction::left)
            {
                wd_quality_ = false;
                feedback.play(audio::Cue::back);
            }
            else if (input.nav == Direction::up || input.nav == Direction::down)
            {
                const int next = wd_quality_focus_ + (input.nav == Direction::down ? 1 : -1);
                if (next < 0 || next >= count)
                    tv_refuse(feedback, input.nav_repeat);
                else
                {
                    wd_quality_focus_ = next;
                    tv_tick(feedback, kWidth * 0.5f);
                }
            }
            else if (input.is_pressed(Action::confirm))
            {
                quality_idx_ = std::clamp(wd_quality_focus_, 0, count - 1);
                dialog_open_ = false;
                wd_quality_ = false;
                start_watch(feedback);
            }
            return;
        }
        std::vector<std::string> labels[3];
        std::vector<int> premium;
        wd_labels(labels, premium);
        const WdLayout L = wd_layout(labels);
        if (wd_focus_section_ < 0)
        {
            // The app starts on the selected voice, else season, else episode, else the button
            wd_focus_section_ = 3;
            for (int s = 0; s < 3; ++s)
                if (labels[s].size() > 1)
                {
                    wd_focus_section_ = s;
                    break;
                }
            wd_focus_index_ = wd_focus_section_ < 3 ? std::clamp(wd_selected(wd_focus_section_), 0, static_cast<int>(labels[wd_focus_section_].size()) - 1) : 0;
        }
        int at = wd_find(L, wd_focus_section_, wd_focus_index_);
        const WdItem here = L.items[static_cast<std::size_t>(at)];
        if (input.is_pressed(Action::back))
        {
            dialog_open_ = false;
            feedback.play(audio::Cue::modal_close);
            return;
        }
        // The button's place among the chips, for moving up and down
        const auto place = [&L](const WdItem &item) {
            Rect r = item.r;
            if (item.section == 3)
                r.y = L.content_h + 100.0f, r.x = 0.0f;
            return r;
        };
        int next = at;
        if (input.nav == Direction::left && at > 0 && L.items[static_cast<std::size_t>(at - 1)].r.y == here.r.y && here.section != 3)
            next = at - 1;
        else if (input.nav == Direction::right && at + 1 < static_cast<int>(L.items.size()) &&
                 L.items[static_cast<std::size_t>(at + 1)].r.y == here.r.y && L.items[static_cast<std::size_t>(at + 1)].section != 3)
            next = at + 1;
        else if (input.nav == Direction::up || input.nav == Direction::down)
        {
            const Rect from = place(here);
            const float sign = input.nav == Direction::down ? 1.0f : -1.0f;
            float best_dy = 1e9f, best_dx = 1e9f;
            for (int i = 0; i < static_cast<int>(L.items.size()); ++i)
            {
                const Rect r = place(L.items[static_cast<std::size_t>(i)]);
                const float dy = (r.y - from.y) * sign;
                if (dy < 1.0f)
                    continue;
                const float dx = std::fabs(r.x - from.x);
                if (dy < best_dy - 1.0f || (std::fabs(dy - best_dy) <= 1.0f && dx < best_dx))
                    best_dy = dy, best_dx = dx, next = i;
            }
        }
        if (input.nav != Direction::none)
        {
            if (next == at)
                tv_refuse(feedback, input.nav_repeat);
            else
            {
                wd_focus_section_ = L.items[static_cast<std::size_t>(next)].section;
                wd_focus_index_ = L.items[static_cast<std::size_t>(next)].index;
                tv_tick(feedback, kWidth * 0.5f);
            }
            return;
        }
        if (!input.is_pressed(Action::confirm))
            return;
        if (here.section == 3)
        {
            // "Качество по умолчанию": anything but "Всегда спрашивать" plays at once
            if (const char *label = rezka::App::instance().settings().quality_label())
            {
                dialog_open_ = false;
                press_.trigger();
                feedback.play(audio::Cue::select);
                write_log("[DEBUG] start_watch movie=%d tr=%d default quality='%s' s=%d e=%d\n", page_item_,
                          translator_idx_, label, season_idx_, episode_idx_);
                rezka::App::instance().play_title_async(page_item_, translator_idx_, -1, season_idx_, episode_idx_,
                                                        -1.0, label);
                return;
            }
            // The qualities next, as the app asks by default
            wd_quality_ = true;
            {
                const int count = std::max(1, static_cast<int>(wd_qualities().size()));
                wd_quality_focus_ = std::clamp(quality_idx_, 0, count - 1);
            }
            feedback.play(audio::Cue::select);
            return;
        }
        if (wd_selected(here.section) != here.index)
        {
            if (here.section == 0)
                translator_idx_ = here.index;
            else if (here.section == 1)
            {
                season_idx_ = here.index;
                episode_idx_ = 0;
            }
            else
                episode_idx_ = here.index;
            feedback.play(audio::Cue::select);
            return;
        }
        // The selected chip again: on to the next choice, or to the button
        int to = 3;
        for (int s = here.section + 1; s < 3; ++s)
            if (!labels[s].empty())
            {
                to = s;
                break;
            }
        wd_focus_section_ = to;
        wd_focus_index_ = to < 3 ? std::clamp(wd_selected(to), 0, static_cast<int>(labels[to].size()) - 1) : 0;
        tv_tick(feedback, kWidth * 0.5f);
    }

    void wd_animate(float dt)
    {
        if (!dialog_open_ || wd_quality_ || wd_focus_section_ < 0 || wd_focus_section_ == 3)
        {
            wd_scroll_.update(dt, omega(14.0f));
            return;
        }
        std::vector<std::string> labels[3];
        std::vector<int> premium;
        wd_labels(labels, premium);
        const WdLayout L = wd_layout(labels);
        const WdItem &item = L.items[static_cast<std::size_t>(wd_find(L, wd_focus_section_, wd_focus_index_))];
        float target = wd_scroll_.target;
        if (item.r.y - 60.0f * kTvDp < target)
            target = item.r.y - 60.0f * kTvDp;
        if (item.r.y + item.r.h + 8.0f * kTvDp > target + L.view_h)
            target = item.r.y + item.r.h + 8.0f * kTvDp - L.view_h;
        wd_scroll_.target = std::clamp(target, 0.0f, std::max(0.0f, L.content_h - L.view_h));
        wd_scroll_.update(dt, omega(14.0f));
    }

    void wd_draw(gfx::DrawList &list) const
    {
        const float shown = dialog_.value;
        if (shown < 0.01f)
            return;
        const float dp = kTvDp;
        list.push_opacity(tween::clamp01(shown));
        list.rounded_rect({0.0f, 0.0f, kWidth, kHeight}, 0.0f, kBlack.with_alpha(0.6f));
        if (wd_quality_)
        {
            const std::vector<std::string> names = wd_qualities();
            const float w = 420.0f * dp;
            const float h = (20.0f + 28.0f + 16.0f + 20.0f) * dp + static_cast<float>(names.size()) * 52.0f * dp;
            const Rect card{(kWidth - w) * 0.5f, (kHeight - h) * 0.5f, w, h};
            list.bordered_rect(card, 28.0f * dp, tv_surface(), 1.0f * dp, tv_border_variant());
            tv_text(list, tv_regular(), "Выберите качество", card.x + 20.0f * dp, card.y + 20.0f * dp, 22.0f * dp, 28.0f * dp, tv_on_surface());
            for (int i = 0; i < static_cast<int>(names.size()); ++i)
            {
                const Rect r{card.x + 20.0f * dp, card.y + 64.0f * dp + static_cast<float>(i) * 52.0f * dp, w - 40.0f * dp, 48.0f * dp};
                const bool focused = i == wd_quality_focus_;
                if (focused)
                    list.rounded_rect(r, 8.0f * dp, tv_on_surface());
                tv_text(list, tv_medium(), names[static_cast<std::size_t>(i)], r.x + 16.0f * dp, r.cy() - 10.0f * dp, 14.0f * dp, 20.0f * dp,
                        focused ? tv_inverse_on_surface() : tv_on_surface(), gfx::Align::left, 0.1f * dp);
            }
            list.pop_opacity();
            return;
        }
        std::vector<std::string> labels[3];
        std::vector<int> premium;
        wd_labels(labels, premium);
        const WdLayout L = wd_layout(labels);
        const Rect &card = L.card;
        list.bordered_rect(card, 28.0f * dp, tv_surface(), 1.0f * dp, tv_border_variant());
        static const char *kHeaders[3] = {"Выберите озвучку", "Выберите сезон", "Выберите серию"};
        const float left = card.x + 18.0f * dp;
        const float scroll = wd_scroll_.value;
        list.push_clip({card.x, card.y + 4.0f, card.w, L.view_h});
        for (int s = 0; s < 3; ++s)
            if (L.header_y[s] >= 0.0f)
                tv_text(list, tv_medium(), kHeaders[s], left + 14.0f * dp, card.y + L.header_y[s] + 24.0f * dp - scroll, 16.0f * dp, 20.0f * dp,
                        tv_on_surface(), gfx::Align::left, 0.2f * dp);
        for (const WdItem &item : L.items)
        {
            if (item.section == 3)
                continue;
            const Rect r{left + item.r.x, card.y + item.r.y - scroll, item.r.w, item.r.h};
            const bool focused = item.section == wd_focus_section_ && item.index == wd_focus_index_;
            const bool selected = wd_selected(item.section) == item.index;
            const bool prem = item.section == 0 && static_cast<std::size_t>(item.index) < premium.size() && premium[static_cast<std::size_t>(item.index)];
            Color fill = kClear, content = prem ? Color::rgb(0xc8c2ea) : tv_on_surface_variant();
            if (focused && selected)
            {
                fill = prem ? Color::rgb(0xe4dfff) : tv_on_primary_container();
                content = prem ? Color::rgb(0x302d4c) : tv_on_primary();
                list.rounded_rect(r, 8.0f * dp, fill);
            }
            else if (focused)
            {
                content = tv_inverse_on_surface();
                list.rounded_rect(r, 8.0f * dp, tv_on_surface());
            }
            else if (selected)
            {
                content = prem ? Color::rgb(0xe4dfff) : tv_on_secondary_container();
                list.bordered_rect(r, 8.0f * dp, (prem ? Color::rgb(0x464364) : tv_secondary_container()).with_alpha(0.4f), 2.0f * dp,
                                   prem ? Color::rgb(0x7555a7) : tv_primary());
            }
            else
                list.bordered_rect(r, 8.0f * dp, kClear, 1.0f * dp, prem ? Color::rgb(0x7555a7) : tv_border());
            tv_text(list, tv_medium(), labels[item.section][static_cast<std::size_t>(item.index)], r.x + 16.0f * dp, r.y + 8.0f * dp,
                    16.0f * dp, 24.0f * dp, content, gfx::Align::left, 0.5f * dp);
        }
        list.pop_clip();
        float y = card.y + L.view_h;
        if (L.hint)
        {
            tv_icon(list, kTvIconInfoOutlined, card.x + 16.0f * dp, y + 16.0f * dp, 20.0f * dp, tv_on_surface());
            tv_text(list, tv_medium(),
                    labels[1].empty() ? "Нажмите еще раз по уже выбранной озвучке, чтобы сразу перейти к кнопке \"Смотреть\""
                                      : "Нажмите еще раз по уже выбранному элементу, чтобы сразу перейти к следующему выбору",
                    card.x + 44.0f * dp, y + 18.0f * dp, 12.0f * dp, 16.0f * dp, tv_on_surface(), gfx::Align::left, 0.5f * dp);
        }
        const WdItem &button = L.items.back();
        const bool focused = wd_focus_section_ == 3;
        const Rect r = scaled(button.r, focused ? 1.02f : 1.0f);
        list.rounded_rect(r, r.h * 0.5f, focused ? tv_on_surface() : tv_surface_variant().with_alpha(0.8f));
        const Color content = focused ? tv_inverse_on_surface() : tv_on_surface().with_alpha(0.8f);
        const float text_w = tv_medium().measure("Смотреть", 14.0f * dp, 0.1f * dp);
        const float x = r.cx() - (28.0f * dp + text_w) * 0.5f;
        tv_icon(list, kTvIconPlayArrow, x, r.cy() - 10.0f * dp, 20.0f * dp, content);
        tv_text(list, tv_medium(), "Смотреть", x + 28.0f * dp, r.cy() - 10.0f * dp, 14.0f * dp, 20.0f * dp, content, gfx::Align::left, 0.1f * dp);
        list.pop_opacity();
    }

    // ---- the comments: HDrezkaTV's Comments screen ---------------------------
    //
    // A full screen over the title's page: "Комментарии (N) • title", then a
    // column of plates, a reply 20 dp further in a level: the avatar in a
    // circle, the author, up to ten lines of text, the date and the likes.

    struct CmItem
    {
        std::vector<std::string> lines; // the text as shown, ten lines at most
        bool more = false;              // ... of a longer one: OK shows it whole
        float y = 0.0f;
        float h = 0.0f;
    };

    void cm_open(app::Feedback &feedback)
    {
        if (page_model_.post_id.empty())
        {
            tv_refuse(feedback, false);
            return;
        }
        comments_open_ = true;
        cm_focus_ = 0;
        cm_scroll_.snap(0.0f);
        cm_revealed_.clear();
        cm_version_ = ~0u;
        rezka::App::instance().load_comments_async(page_model_.post_id, false);
        feedback.play(audio::Cue::open);
    }
    static float cm_text_width(int indent)
    {
        return kWidth - 2.0f * 48.0f * kTvDp - static_cast<float>(indent) * 20.0f * kTvDp - (16.0f + 60.0f + 16.0f + 16.0f) * kTvDp -
               (indent > 0 ? 17.0f * kTvDp : 0.0f);
    }
    // Wraps the comments again when the list or what is revealed has changed.
    void cm_rebuild()
    {
        auto &app = rezka::App::instance();
        const std::uint32_t version = app.comments_version.load();
        if (version == cm_version_)
            return;
        cm_version_ = version;
        {
            std::lock_guard<std::mutex> lock(app.catalog_mutex);
            cm_list_ = app.comments;
            cm_has_next_ = app.comments_has_next;
        }
        cm_items_.assign(cm_list_.size(), CmItem{});
        float y = 0.0f;
        const float dp = kTvDp;
        for (std::size_t i = 0; i < cm_list_.size(); ++i)
        {
            const rezka::Comment &c = cm_list_[i];
            CmItem &item = cm_items_[i];
            app.request_photo(c.avatar);
            if (c.spoiler && !cm_revealed_.count(c.id))
                item.lines = {"Этот комментарий может содержать спойлеры!", "Нажмите \"Ок\", чтобы показать."};
            else
            {
                const float width = cm_text_width(c.indent);
                std::size_t start = 0;
                while (start <= c.text.size())
                {
                    std::size_t end = c.text.find('\n', start);
                    if (end == std::string::npos)
                        end = c.text.size();
                    for (std::string &line : tv_regular().font->wrap(c.text.substr(start, end - start), 14.0f * dp, width))
                        item.lines.push_back(std::move(line));
                    start = end + 1;
                }
                if (item.lines.size() > 10)
                {
                    item.lines.resize(10);
                    item.more = true;
                }
            }
            item.y = y;
            item.h = 32.0f * dp + std::max(60.0f * dp, 20.0f * dp + 8.0f * dp + static_cast<float>(item.lines.size()) * 20.0f * dp + 8.0f * dp + 16.0f * dp);
            y += item.h + 8.0f * dp;
        }
        cm_focus_ = std::clamp(cm_focus_, 0, std::max(0, static_cast<int>(cm_items_.size()) - 1));
    }
    void cm_update(const InputFrame &input, app::Feedback &feedback)
    {
        cm_rebuild();
        const int count = static_cast<int>(cm_items_.size());
        if (input.is_pressed(Action::back))
        {
            comments_open_ = false;
            feedback.play(audio::Cue::back);
            return;
        }
        if (input.nav == Direction::up || input.nav == Direction::down)
        {
            const int next = cm_focus_ + (input.nav == Direction::down ? 1 : -1);
            if (next < 0 || next >= count)
                tv_refuse(feedback, input.nav_repeat);
            else
            {
                cm_focus_ = next;
                tv_tick(feedback, kWidth * 0.5f);
            }
            if (cm_has_next_ && cm_focus_ + 6 >= count)
                rezka::App::instance().load_comments_async(page_model_.post_id, true);
        }
        if (input.is_pressed(Action::confirm) && cm_focus_ < count)
        {
            const rezka::Comment &c = cm_list_[static_cast<std::size_t>(cm_focus_)];
            if (c.spoiler && !cm_revealed_.count(c.id))
            {
                cm_revealed_.insert(c.id);
                cm_version_ = ~0u;
                feedback.play(audio::Cue::select);
            }
            else if (cm_items_[static_cast<std::size_t>(cm_focus_)].more)
            {
                dp_dialog_title_ = c.author;
                dp_dialog_text_ = c.text;
                dp_open_dialog(DpDialog::text, feedback);
            }
        }
    }
    void cm_animate(float dt)
    {
        comments_in_.target = comments_open_ && page_open_ ? 1.0f : 0.0f;
        comments_in_.update(dt, omega(14.0f));
        if (!comments_open_)
            return;
        cm_rebuild();
        if (!cm_items_.empty())
        {
            const CmItem &item = cm_items_[static_cast<std::size_t>(cm_focus_)];
            const float view = kHeight - 72.0f * kTvDp;
            const float total = cm_items_.back().y + cm_items_.back().h + 48.0f * kTvDp;
            cm_scroll_.target = std::clamp(item.y + 24.0f * kTvDp - 0.3f * view, 0.0f, std::max(0.0f, total - view));
        }
        cm_scroll_.update(dt, omega(12.0f));
    }
    void cm_draw(gfx::DrawList &list) const
    {
        const float shown = comments_in_.value;
        if (shown < 0.01f)
            return;
        const float dp = kTvDp;
        const PageModel &m = page_model_;
        list.push_opacity(tween::clamp01(shown));
        list.rounded_rect({0.0f, 0.0f, kWidth, kHeight}, 0.0f, tv_surface());
        const std::string head = "Комментарии (" + std::to_string(m.comments_count) + ") • ";
        const float head_w = tv_text(list, tv_regular(), head, 48.0f * dp, 24.0f * dp, 24.0f * dp, 32.0f * dp, tv_on_surface());
        tv_text(list, tv_medium(), tv_medium().font->fit(m.title, 16.0f * dp, kWidth - 96.0f * dp - head_w), 48.0f * dp + head_w, 30.0f * dp,
                16.0f * dp, 24.0f * dp, tv_on_surface());
        const float top = 72.0f * dp;
        list.push_clip({0.0f, top, kWidth, kHeight - top});
        if (cm_items_.empty())
        {
            const bool loading = rezka::App::instance().comments_loading;
            if (loading)
                for (int i = 0; i < 3; ++i)
                    list.bordered_rect({48.0f * dp, top + 24.0f * dp + static_cast<float>(i) * 168.0f * dp, kWidth - 96.0f * dp, 160.0f * dp}, 12.0f * dp,
                                       tv_surface_variant().with_alpha(0.25f + 0.1f * std::sin(clock_ * 3.0f)), 1.0f * dp, tv_border_variant());
            else
                tv_text(list, tv_regular(), "Комментариев пока нет", kWidth * 0.5f, kHeight * 0.45f, 22.0f * dp, 28.0f * dp, tv_on_surface(),
                        gfx::Align::center);
        }
        for (std::size_t i = 0; i < cm_items_.size(); ++i)
        {
            const CmItem &item = cm_items_[i];
            const rezka::Comment &c = cm_list_[i];
            const float y = top + 24.0f * dp + item.y - cm_scroll_.value;
            if (y + item.h < top || y > kHeight)
                continue;
            const bool focused = static_cast<int>(i) == cm_focus_ && dp_dialog_ == DpDialog::none;
            const float indent = static_cast<float>(c.indent) * 20.0f * dp;
            const Rect r{48.0f * dp + indent, y, kWidth - 96.0f * dp - indent, item.h};
            if (focused)
                list.rounded_rect(r, 12.0f * dp, tv_on_surface());
            const Color content = focused ? tv_inverse_on_surface() : tv_on_surface();
            const Color accent = focused ? tv_inverse_primary() : tv_primary();
            float x = r.x + 16.0f * dp;
            if (c.indent > 0)
            {
                list.rounded_rect({x, r.y + 16.0f * dp, 1.0f * dp, r.h - 32.0f * dp}, 0.0f, content);
                x += 17.0f * dp;
            }
            list.circle(x + 30.0f * dp, r.y + 46.0f * dp, 30.0f * dp, tv_secondary_container());
            dp_photo(list, c.avatar, {x + 1.0f * dp, r.y + 17.0f * dp, 58.0f * dp, 58.0f * dp}, 29.0f * dp, kWhite, 1.0f);
            x += 76.0f * dp;
            float line_y = r.y + 16.0f * dp;
            tv_text(list, tv_medium(), c.author, x, line_y, 16.0f * dp, 20.0f * dp, c.admin ? accent : content, gfx::Align::left, 0.2f * dp);
            line_y += 28.0f * dp;
            const bool hidden = c.spoiler && !cm_revealed_.count(c.id);
            for (std::size_t k = 0; k < item.lines.size(); ++k, line_y += 20.0f * dp)
            {
                const bool last = item.more && k + 1 == item.lines.size();
                const Color color = hidden ? (focused ? Color::rgb(0x690005) : tv_error()) : content;
                const ui::FontRef &font = hidden && k == 1 ? tv_medium() : tv_regular();
                if (last)
                {
                    // The tenth line gives way to the app's hint
                    const char *hint = "… Нажмите \"ОК\", чтобы показать полный текст";
                    const float room = cm_text_width(c.indent) - tv_medium().measure(hint, 14.0f * dp);
                    const float w = tv_text(list, tv_regular(), tv_regular().font->fit(item.lines[k], 14.0f * dp, std::max(60.0f, room)), x,
                                            line_y, 14.0f * dp, 20.0f * dp, content, gfx::Align::left, 0.2f * dp);
                    tv_text(list, tv_medium(), hint, x + w, line_y, 14.0f * dp, 20.0f * dp, accent);
                }
                else
                    tv_text(list, font, item.lines[k], x, line_y, 14.0f * dp, 20.0f * dp, color, gfx::Align::left, 0.2f * dp);
            }
            const float foot = r.y + r.h - 32.0f * dp;
            tv_text(list, tv_medium(), c.date, x, foot, 11.0f * dp, 16.0f * dp, content, gfx::Align::left, 0.5f * dp);
            const std::string likes = std::to_string(c.likes);
            const float likes_w = tv_medium().measure(likes, 11.0f * dp, 0.5f * dp);
            tv_text(list, tv_medium(), likes, r.x + r.w - 16.0f * dp, foot, 11.0f * dp, 16.0f * dp, content, gfx::Align::right, 0.5f * dp);
            tv_icon(list, kTvIconThumbUpAltOutlined, r.x + r.w - 16.0f * dp - likes_w - 18.0f * dp, foot + 1.0f * dp, 14.0f * dp, content);
        }
        list.pop_clip();
        list.pop_opacity();
    }

    void update_home(const InputFrame &input, app::Feedback &feedback)
    {
        // The home page is HDrezkaTV's; what follows is the store design's
        // input, kept for its parts the other layers still use.
        tv_update_home(input, feedback);
        return;

        if (input.is_pressed(Action::menu) || ((ps5_pad().pressed & PAD_OPTIONS) != 0))
        {
            profile_open_ = true;
            search_open_ = false;
            page_open_ = false;
            key_lang_ = 0; // ENG for email/password
            key_row_ = 0;
            key_col_ = 0;
            login_field_ = 0;
            login_fields_focus_ = true; // start on email so L1/R1 / ↓ are obvious
            feedback.play(audio::Cue::open);
            return;
        }
        if (input.is_pressed(Action::north) || ((ps5_pad().pressed & PAD_TRIANGLE) != 0))
        {
            search_open_ = true;
            profile_open_ = false;
            page_open_ = false;
            feedback.play(audio::Cue::open);
            return;
        }
        if (input.is_pressed(Action::west))
        {
            int idx = -1;
            if (zone_ == Zone::grid && !visible_.empty())
                idx = visible_[static_cast<std::size_t>(focus_)];
            else if (zone_ == Zone::banner && !featured_.empty())
                idx = featured_[static_cast<std::size_t>(banner_)];
            if (idx >= 0 && idx < static_cast<int>(wished_.size()))
            {
                wished_[static_cast<std::size_t>(idx)] = !wished_[static_cast<std::size_t>(idx)];
                feedback.play(audio::Cue::select);
            }
            return;
        }
        // L1/R1 -- top categories (Главная / Фильмы / …); L2/R2 -- chip subcategories.
        if (input.is_pressed(Action::page_prev))
            step_top_tab(-1, false, feedback);
        else if (input.is_pressed(Action::page_next))
            step_top_tab(1, false, feedback);
        else if (input.is_pressed(Action::jump_prev))
            step_filter(-1, false, feedback);
        else if (input.is_pressed(Action::jump_next))
            step_filter(1, false, feedback);

        if (input.is_pressed(Action::north))
        {
            search_open_ = true;
            feedback.play(audio::Cue::open);
            return;
        }
        if (input.is_pressed(Action::menu))
        {
            profile_open_ = true;
            key_lang_ = 0; // ENG for email/password
            key_row_ = 0;
            key_col_ = 0;
            login_field_ = 0;
            login_fields_focus_ = true;
            feedback.play(audio::Cue::open);
            return;
        }
        if (input.is_pressed(Action::back))
        {
            if (zone_ == Zone::grid && focus_ >= grid_columns())
            {
                focus_ %= grid_columns();
                feedback.play(audio::Cue::back);
                return;
            }
            feedback.play(audio::Cue::back);
            return;
        }

        if (input.nav != Direction::none)
        {
            if (zone_ == Zone::grid && !visible_.empty())
                navigate_grid(input, feedback);
            else if (zone_ == Zone::banner)
                navigate_banner(input, feedback);
            else
                navigate_chips(input, feedback);
        }

        if (input.is_pressed(Action::confirm))
        {
            if (zone_ == Zone::grid && !visible_.empty())
                open_page(visible_[static_cast<std::size_t>(focus_)], false, feedback);
            else if (zone_ == Zone::banner && !featured_.empty())
                open_page(featured_[static_cast<std::size_t>(banner_)], true, feedback);
            else if (!visible_.empty())
            {
                zone_ = Zone::grid;
                feedback.play(audio::Cue::select);
            }
            else
                refuse(feedback, false, 0.0f, 1.0f);
        }
    }

    // Home discover: move within a section row (4 cols), then between sections.
    bool navigate_home_grid(const InputFrame &input, app::Feedback &feedback)
    {
        if (!home_layout_ || home_sections_.empty())
            return false;
        const int count = static_cast<int>(visible_.size());
        if (count <= 0)
            return false;

        // Map focus_ → (section, offset)
        int sec = 0, off = focus_, base = 0;
        for (; sec < static_cast<int>(home_sections_.size()); ++sec)
        {
            const int n = home_sections_[static_cast<std::size_t>(sec)].count;
            if (off < n)
                break;
            off -= n;
            base += n;
        }
        if (sec >= static_cast<int>(home_sections_.size()))
            return false;
        const int sec_count = home_sections_[static_cast<std::size_t>(sec)].count;
        const int col = off % kHomeColumns;
        const int sub = off / kHomeColumns;

        switch (input.nav)
        {
        case Direction::left:
            if (col == 0)
                return refuse(feedback, input.nav_repeat, -1.0f, 0.0f), true;
            --focus_;
            break;
        case Direction::right:
            if (col + 1 >= sec_count || focus_ + 1 >= count)
                return refuse(feedback, input.nav_repeat, 1.0f, 0.0f), true;
            if ((off + 1) >= sec_count)
                return refuse(feedback, input.nav_repeat, 1.0f, 0.0f), true;
            ++focus_;
            break;
        case Direction::up:
            if (sub > 0)
            {
                focus_ = base + (sub - 1) * kHomeColumns + col;
                if (focus_ >= base + sec_count)
                    focus_ = base + sec_count - 1;
            }
            else if (sec > 0)
            {
                const int prev_n = home_sections_[static_cast<std::size_t>(sec - 1)].count;
                const int prev_base = base - prev_n;
                focus_ = prev_base + std::min(col, prev_n - 1);
            }
            else
            {
                zone_ = Zone::chips;
                feedback.play(audio::Cue::focus, 1.1f, ui::pan_for_x(ring_.x.value));
                return true;
            }
            break;
        case Direction::down:
            if ((sub + 1) * kHomeColumns < sec_count)
            {
                focus_ = base + std::min(sec_count - 1, (sub + 1) * kHomeColumns + col);
            }
            else if (sec + 1 < static_cast<int>(home_sections_.size()))
            {
                const int next_n = home_sections_[static_cast<std::size_t>(sec + 1)].count;
                focus_ = base + sec_count + std::min(col, next_n - 1);
            }
            else
                return refuse(feedback, input.nav_repeat, 0.0f, 1.0f), true;
            break;
        case Direction::none:
            return true;
        }
        tick_card(feedback);
        return true;
    }

    void navigate_grid(const InputFrame &input, app::Feedback &feedback)
    {
        if (navigate_home_grid(input, feedback))
            return;
        const int count = static_cast<int>(visible_.size());
        const int cols = grid_columns();
        const int column = focus_ % cols;
        const int row = focus_ / cols;
        switch (input.nav)
        {
        case Direction::left:
            if (column == 0)
                return refuse(feedback, input.nav_repeat, -1.0f, 0.0f);
            --focus_;
            break;
        case Direction::right:
            if (column == cols - 1 || focus_ + 1 >= count)
                return refuse(feedback, input.nav_repeat, 1.0f, 0.0f);
            ++focus_;
            break;
        case Direction::up:
            if (row == 0)
            {
                zone_ = Zone::chips;
                feedback.play(audio::Cue::focus, 1.1f, ui::pan_for_x(ring_.x.value));
                return;
            }
            focus_ -= cols;
            break;
        case Direction::down:
            if (focus_ + cols < count)
                focus_ += cols;
            else if ((count - 1) / cols > row)
                focus_ = count - 1;
            else
                return refuse(feedback, input.nav_repeat, 0.0f, 1.0f);
            break;
        case Direction::none:
            return;
        }
        tick_card(feedback);
    }

    void navigate_chips(const InputFrame &input, app::Feedback &feedback)
    {
        switch (input.nav)
        {
        case Direction::left:
            step_filter(-1, input.nav_repeat, feedback);
            break;
        case Direction::right:
            step_filter(1, input.nav_repeat, feedback);
            break;
        case Direction::up:
            if (featured_.empty())
                return refuse(feedback, input.nav_repeat, 0.0f, -1.0f);
            zone_ = Zone::banner;
            feedback.play(audio::Cue::focus, 1.16f);
            break;
        case Direction::down:
            if (visible_.empty())
                return refuse(feedback, input.nav_repeat, 0.0f, 1.0f);
            zone_ = Zone::grid;
            tick_card(feedback);
            break;
        case Direction::none:
            break;
        }
    }

    void navigate_banner(const InputFrame &input, app::Feedback &feedback)
    {
        const int count = static_cast<int>(featured_.size());
        if (count <= 0)
        {
            zone_ = Zone::chips;
            return;
        }
        switch (input.nav)
        {
        case Direction::left:
        case Direction::right:
        {
            // The banner is a loop: it has no ends to refuse at.
            const int step = input.nav == Direction::right ? 1 : -1;
            show_banner((banner_ + step + count) % count, static_cast<float>(step));
            feedback.play(audio::Cue::slider, 1.0f + 0.04f * static_cast<float>(banner_),
                          0.3f * static_cast<float>(step));
            break;
        }
        case Direction::up:
            refuse(feedback, input.nav_repeat, 0.0f, -1.0f);
            break;
        case Direction::down:
            zone_ = Zone::chips;
            feedback.play(audio::Cue::focus, 1.1f);
            break;
        case Direction::none:
            break;
        }
    }

    void step_filter(int delta, bool repeat, app::Feedback &feedback)
    {
        const int n = active_chip_count();
        const int next = filter_ + delta;
        if (next < 0 || next >= n)
        {
            if (!repeat)
            {
                feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
                feedback.rumble(0.25f, 0.05f);
                chip_nudge_.trigger();
                chip_nudge_x_ = static_cast<float>(delta);
            }
            return;
        }
        previous_visible_ = visible_;
        previous_scroll_ = scroll_.value;
        filter_ = next;
        focus_ = 0;
        apply_top_tab_load();
        rebuild_visible();
        swap_.start(calm() ? 0.15f : 0.5f);
        Rect chips[kFilters];
        chip_layout(chips);
        feedback.play(audio::Cue::tab, 0.92f + 0.04f * static_cast<float>(next),
                      ui::pan_for_x(chips[next].cx()));
    }

    void step_top_tab(int delta, bool repeat, app::Feedback &feedback)
    {
        const int next = top_tab_ + delta;
        if (next < 0 || next >= kTopTabs)
        {
            if (!repeat)
            {
                feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
                feedback.rumble(0.25f, 0.05f);
            }
            return;
        }
        top_tab_ = next;
        previous_visible_ = visible_;
        previous_scroll_ = scroll_.value;
        // Reset chip to a sensible default per mode.
        filter_ = (top_tab_ == 0) ? 1 /* Последние */ : 0 /* Популярные */;
        focus_ = 0;
        apply_top_tab_load();
        rebuild_visible();
        swap_.start(calm() ? 0.15f : 0.5f);
        feedback.play(audio::Cue::tab, 1.0f);
    }

    void show_banner(int slot, float direction)
    {
        if (slot == banner_)
            return;
        banner_previous_ = banner_;
        banner_ = slot;
        banner_direction_ = direction;
        banner_fade_.start(calm() ? 0.2f : 0.7f);
        banner_clock_ = 0.0f;
        drift_previous_ = drift_;
        drift_ = 0.0f;
    }

    // ---- input: the product page -------------------------------------------

    // ---- the product page: its model, layout and focus ----------------------

    static bool is_button(PageFocus f)
    {
        return f == PageFocus::watch || f == PageFocus::resume || f == PageFocus::bookmark ||
               f == PageFocus::qr;
    }

    static PageFocus row_focus(int kind)
    {
        return kind == 3 ? PageFocus::genres : kind == 4 ? PageFocus::lists : PageFocus::collections;
    }

    // The plate of a focused fact row: a little wider than its text.
    static Rect row_plate(const PageRow &row)
    {
        return {kPageX - 18.0f, row.base - 30.0f, kPageRowW, 42.0f};
    }

    static Rect focus_plate(const PageLayout &layout, PageFocus f)
    {
        switch (f)
        {
        case PageFocus::resume:
            return layout.resume;
        case PageFocus::bookmark:
            return layout.bookmark;
        case PageFocus::qr:
            return layout.qr;
        case PageFocus::cast:
            if (!layout.cast_cards.empty())
                return layout.cast_cards[layout.cast_focus];
            return layout.watch;
        case PageFocus::trailer:
            return layout.trailer;
        case PageFocus::genres:
        case PageFocus::lists:
        case PageFocus::collections:
            for (const PageRow &row : layout.rows)
                if (row.kind >= 3 && row_focus(row.kind) == f)
                    return row_plate(row);
            return layout.watch;
        case PageFocus::watch:
            break;
        }
        return layout.watch;
    }

    static float focus_radius(PageFocus f)
    {
        if (is_button(f))
            return kButtonRadius;
        return f == PageFocus::trailer ? 16.0f : 14.0f;
    }

    // Copies what the page shows out of the catalogue. Called every frame the
    // page is open, so the resume point and the details stay current.
    void rebuild_page_model()
    {
        PageModel m;
        std::vector<rezka::Translator> voices;
        auto &app = rezka::App::instance();
        {
            std::lock_guard<std::mutex> lock(app.catalog_mutex);
            if (page_item_ >= 0 && page_item_ < static_cast<int>(app.movies.size()))
            {
                const rezka::MovieData &movie = app.movies[page_item_];
                const rezka::VideoDetails &d = movie.details;
                m.url = movie.url;
                m.title = d.name.empty() ? movie.title : d.name;
                m.original = d.original_name;
                m.description = d.description;
                m.slogan = d.slogan;
                m.release_date = d.release_date;
                m.year = d.year;
                m.duration = d.duration;
                m.age = d.age;
                m.countries = d.countries;
                m.genres = d.genres;
                m.lists = d.lists;
                m.collections = d.collections;
                m.kinopoisk = d.rating_kinopoisk;
                m.imdb = d.rating_imdb;
                m.hdrezka = d.rating_hdrezka;
                m.has_trailer = d.has_trailer;
                m.trailer_id = d.trailer_id;
                m.loaded = movie.details_loaded;
                m.is_series = d.is_series;
                m.blocked = d.blocked;
                m.post_id = d.id;
                m.cover = d.cover_url.empty() ? movie.cover_url : d.cover_url;
                m.full_cover = d.full_cover_url;
                m.year_url = d.year_url;
                m.country_urls = d.country_urls;
                m.genre_urls = d.genre_urls;
                m.comments_count = d.comments_count;
                m.franchise_title = d.franchise_title;
                m.franchises = d.franchises;
                m.related = d.related;
                m.schedule = d.schedule;
                m.voice_stats = d.voice_stats;
            m.cast = d.people;
                voices = d.translators;
                m.bookmarks = d.bookmarks;
            }
            m.signed_in = app.account.logged_in;
        }
        rezka::ContinueEntry entry;
        if (!m.url.empty() && app.get_continue(m.url, entry))
        {
            m.has_resume = true;
            m.resume = entry;
            for (const rezka::Translator &tr : voices)
                if (tr.id == entry.translator_id)
                    m.resume_voice = tr.name;
        }
        for (const rezka::Person &p : m.cast)
            rezka::App::instance().request_photo(p.photo);
        if (m.description == page_model_.description && page_model_.description_lines > 0)
            m.description_lines = page_model_.description_lines;
        else
            m.description_lines = static_cast<int>(
                context_.fonts.regular.font->wrap(m.description, 22, kPageTextW).size());
        page_model_ = m;
        dp_prepare();
    }

    // Where everything sits. The buttons row is first; then the fact rows,
    // each opened list pushing the rows under it down; then the description
    // with the trailer beside it.
    PageLayout page_layout() const
    {
        const PageModel &m = page_model_;
        PageLayout L;
        // The poster stands at the top left; the buttons sit under it.
        const float top = 130.0f + 469.0f + 36.0f;
        L.poster = {kPageX, 130.0f, 313.0f, 469.0f};
        float x = kPageX;
        L.watch = {x, top, 270.0f, kPageButtonH};
        x += 270.0f + 18.0f;
        L.has_resume = m.has_resume;
        if (m.has_resume)
        {
            L.resume = {x, top, 330.0f, kPageButtonH};
            x += 330.0f + 18.0f;
        }
        L.bookmark = {x, top, kPageButtonH, kPageButtonH};
        x += kPageButtonH + 18.0f;
        L.qr = {x, top, kPageButtonH, kPageButtonH};

        float y = top + kPageButtonH;
        if (m.has_resume)
        {
            L.hint_y = y + 40.0f;
            y += 52.0f;
        }
        L.title_y = y + 62.0f;
        y = L.title_y;
        if (!m.original.empty())
        {
            L.original_y = y + 44.0f;
            y = L.original_y;
        }
        L.stats_y = y + 26.0f;

        float row_y = L.stats_y + 118.0f;
        const auto add_row = [&](int kind, int lines) {
            PageRow row;
            row.kind = kind;
            row.base = row_y;
            row.lines = lines;
            row.open = kind >= 3 && open_rows_[kind - 3];
            L.rows.push_back(row);
            row_y += kPageRowPitch + (row.open ? 14.0f + lines * kPageLinePitch : 0.0f);
        };
        if (!m.slogan.empty())
            add_row(0, 0);
        if (!m.release_date.empty())
            add_row(1, 0);
        if (!m.countries.empty())
            add_row(2, 0);
        if (!m.genres.empty())
            add_row(3, static_cast<int>(m.genres.size()));
        if (!m.lists.empty())
            add_row(4, static_cast<int>(m.lists.size()));
        if (!m.collections.empty())
            add_row(5, static_cast<int>(m.collections.size()));

        L.desc_y = row_y + 26.0f;
        const int desc_lines = std::min(6, std::max(1, m.description_lines));
        float bottom = L.desc_y + 52.0f + desc_lines * 34.0f + 40.0f;
        if (m.has_trailer)
        {
            L.has_trailer = true;
            L.trailer = {kTrailerX, L.desc_y + 14.0f, kTrailerW, kTrailerH};
            bottom = std::max(bottom, L.trailer.y + L.trailer.h + 40.0f);
        }
        if (!m.cast.empty())
        {
            L.has_cast = true;
            L.cast_y = bottom;
            const int shown = static_cast<int>(std::min<std::size_t>(m.cast.size(), 12));
            const float shift = static_cast<float>(std::max(0, cast_index_ - 2)) * kCastPitch;
            for (int i = 0; i < shown; ++i)
                L.cast_cards.push_back({kPageX + i * kCastPitch - shift, L.cast_y + 64.0f, kCastW, kCastH});
            L.cast_focus = std::clamp(cast_index_, 0, shown - 1);
            bottom += 64.0f + kCastH + 80.0f;
        }
        L.bottom = bottom;
        return L;
    }

    // The focusable things of the page in the order the focus walks them:
    // the buttons row, then the opened-or-not rows, then the trailer.
    std::vector<PageFocus> page_focus_order(const PageLayout &L) const
    {
        std::vector<PageFocus> order{PageFocus::watch};
        if (L.has_resume)
            order.push_back(PageFocus::resume);
        order.push_back(PageFocus::bookmark);
        order.push_back(PageFocus::qr);
        for (const PageRow &row : L.rows)
            if (row.kind >= 3)
                order.push_back(row_focus(row.kind));
        if (L.has_trailer)
            order.push_back(PageFocus::trailer);
        if (L.has_cast)
            order.push_back(PageFocus::cast);
        return order;
    }

    void open_page(int index, bool from_banner, app::Feedback &feedback)
    {
        page_open_ = true;
        page_item_ = index;
        page_from_banner_ = from_banner;
        page_focus_ = PageFocus::watch;
        page_button_ = PageFocus::watch;
        cast_index_ = 0;
        open_rows_[0] = open_rows_[1] = open_rows_[2] = false;
        translator_idx_ = 0;
        quality_idx_ = 0;
        season_idx_ = 0;
        episode_idx_ = 0;
        rezka::App::instance().load_details_async(index);
        rebuild_page_model();
        const PageLayout layout = page_layout();
        page_ring_.snap(focus_plate(layout, page_focus_).inset(-6.0f));
        page_ring_radius_.snap(focus_radius(page_focus_) + 6.0f);
        page_scroll_.snap(0.0f);
        // The app's page starts on the Смотреть button
        dp_row_ = 1;
        dp_col_ = 0;
        dp_cols_.clear();
        dp_lift_ = 1.0f;
        dp_scroll_.snap(0.0f);
        dp_row_x_.clear();
        dp_stack_.clear();
        dp_dialog_ = DpDialog::none;
        comments_open_ = false;
        dp_key_.clear();
        feedback.play(audio::Cue::open);
    }

    void page_refuse(app::Feedback &feedback, bool repeat)
    {
        if (repeat)
            return;
        feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
        feedback.rumble(0.25f, 0.05f);
        page_nudge_.trigger();
    }

    // A short line over the screen, as the purchase toast is.
    void show_message(const std::string &text)
    {
        toast_text_ = text;
        toast_left_ = kToastSeconds;
        toast_age_ = 0.0f;
    }

    // The bookmark. Signed in, the app's title screen opens the account's
    // folders, the title's checked, and a folder chosen puts it in or takes
    // it out on the site (/ajax/favorites/, add_post): the profile's
    // "Закладки" are those folders. Without an account the title is kept
    // in this console's own bookmarks.
    void toggle_bookmark(app::Feedback &feedback)
    {
        if (page_model_.url.empty())
        {
            feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
            return;
        }
        auto &app = rezka::App::instance();
        if (page_model_.signed_in)
        {
            if (!page_model_.loaded || page_model_.post_id.empty())
            {
                tv_refuse(feedback, false);
                return;
            }
            app.load_bookmark_choices_async(page_item_);
            dp_open_dialog(DpDialog::bookmarks, feedback);
            return;
        }
        const bool on = app.toggle_favorite(page_model_.url, page_model_.title);
        show_message(on ? "Добавлено в закладки" : "Убрано из закладок");
        feedback.play(audio::Cue::select);
    }

    // The QR code of an address, for reading with a phone.
    void open_qr(const std::string &url, const std::string &caption, app::Feedback &feedback)
    {
        if (url.empty())
        {
            show_message("Ссылка ещё не загружена");
            return;
        }
        if (!qr::encode(url, qr_code_))
        {
            show_message("Ссылка слишком длинная для QR-кода");
            return;
        }
        qr_url_ = url;
        qr_caption_ = caption;
        qr_open_ = true;
        feedback.play(audio::Cue::modal_open);
    }

    void update_qr(const InputFrame &input, app::Feedback &feedback)
    {
        if (input.is_pressed(Action::back) || input.is_pressed(Action::confirm))
        {
            qr_open_ = false;
            feedback.play(audio::Cue::modal_close);
        }
    }

    // The clock as the page shows it: the time, and the day and month.
    static void page_clock(char *time, std::size_t time_size, char *date, std::size_t date_size)
    {
        static const char *kMonths[] = {"января", "февраля", "марта",   "апреля",
                                        "мая",    "июня",    "июля",    "августа",
                                        "сентября", "октября", "ноября", "декабря"};
        const std::time_t now = std::time(nullptr);
        const std::tm *tm = std::localtime(&now);
        if (!tm)
            return;
        std::strftime(time, time_size, "%H:%M", tm);
        std::snprintf(date, date_size, "%d %s", tm->tm_mday, kMonths[tm->tm_mon]);
    }

    // The part of a 2:3 poster that fills r without being stretched; its upper
    // part is kept, where faces are.
    static Rect cover_uv(const Rect &r)
    {
        const float poster = 2.0f / 3.0f;
        const float aspect = r.w / r.h;
        if (aspect > poster)
        {
            const float h = poster / aspect;
            return {0.0f, (1.0f - h) * 0.25f, 1.0f, h};
        }
        const float w = aspect / poster;
        return {(1.0f - w) * 0.5f, 0.0f, w, 1.0f};
    }

    void update_page(const InputFrame &input, app::Feedback &feedback)
    {
        if (input.is_pressed(Action::west))
        {
            toggle_bookmark(feedback);
            return;
        }
        if (input.is_pressed(Action::back))
        {
            page_open_ = false;
            feedback.play(audio::Cue::back);
            return;
        }

        // Series season / episode navigation
        if (input.is_pressed(Action::page_prev)) // L1: prev season
        {
            auto &app = rezka::App::instance();
            std::lock_guard<std::mutex> lock(app.catalog_mutex);
            if (page_item_ < static_cast<int>(app.movies.size())) {
                const auto &m = app.movies[page_item_];
                if (m.details.is_series && !m.details.seasons.empty()) {
                    season_idx_ = (season_idx_ - 1 + static_cast<int>(m.details.seasons.size())) % static_cast<int>(m.details.seasons.size());
                    episode_idx_ = 0;
                    feedback.play(audio::Cue::tab);
                }
            }
        }
        else if (input.is_pressed(Action::page_next)) // R1: next season
        {
            auto &app = rezka::App::instance();
            std::lock_guard<std::mutex> lock(app.catalog_mutex);
            if (page_item_ < static_cast<int>(app.movies.size())) {
                const auto &m = app.movies[page_item_];
                if (m.details.is_series && !m.details.seasons.empty()) {
                    season_idx_ = (season_idx_ + 1) % static_cast<int>(m.details.seasons.size());
                    episode_idx_ = 0;
                    feedback.play(audio::Cue::tab);
                }
            }
        }

        if (input.is_pressed(Action::jump_prev)) // L2: prev episode
        {
            auto &app = rezka::App::instance();
            std::lock_guard<std::mutex> lock(app.catalog_mutex);
            if (page_item_ < static_cast<int>(app.movies.size())) {
                const auto &m = app.movies[page_item_];
                if (m.details.is_series && !m.details.seasons.empty()) {
                    int s_idx = std::clamp(season_idx_, 0, static_cast<int>(m.details.seasons.size() - 1));
                    const auto &eps = m.details.seasons[s_idx].episodes;
                    if (!eps.empty()) {
                        episode_idx_ = (episode_idx_ - 1 + static_cast<int>(eps.size())) % static_cast<int>(eps.size());
                        feedback.play(audio::Cue::focus);
                    }
                }
            }
        }
        else if (input.is_pressed(Action::jump_next)) // R2: next episode
        {
            auto &app = rezka::App::instance();
            std::lock_guard<std::mutex> lock(app.catalog_mutex);
            if (page_item_ < static_cast<int>(app.movies.size())) {
                const auto &m = app.movies[page_item_];
                if (m.details.is_series && !m.details.seasons.empty()) {
                    int s_idx = std::clamp(season_idx_, 0, static_cast<int>(m.details.seasons.size() - 1));
                    const auto &eps = m.details.seasons[s_idx].episodes;
                    if (!eps.empty()) {
                        episode_idx_ = (episode_idx_ + 1) % static_cast<int>(eps.size());
                        feedback.play(audio::Cue::focus);
                    }
                }
            }
        }

        // The focus walks the buttons row with left and right, and the rows
        // under it with up and down. The scroll follows it in animate().
        const PageLayout layout = page_layout();
        const std::vector<PageFocus> order = page_focus_order(layout);
        const auto position = [&](PageFocus f) {
            return static_cast<int>(std::find(order.begin(), order.end(), f) - order.begin());
        };
        if (position(page_focus_) == static_cast<int>(order.size()))
            page_focus_ = PageFocus::watch;
        if (position(page_button_) == static_cast<int>(order.size()))
            page_button_ = PageFocus::watch;
        const int at = position(page_focus_);
        const bool on_buttons = is_button(page_focus_);
        switch (input.nav)
        {
        case Direction::left:
        case Direction::right:
        {
            if (page_focus_ == PageFocus::cast)
            {
                const int next = cast_index_ + (input.nav == Direction::left ? -1 : 1);
                if (next >= 0 && next < static_cast<int>(layout.cast_cards.size()))
                {
                    cast_index_ = next;
                    feedback.play(audio::Cue::focus, 1.0f, ui::pan_for_x(layout.cast_cards[layout.cast_focus].cx()));
                }
                else
                    page_refuse(feedback, input.nav_repeat);
                break;
            }
            std::vector<PageFocus> row;
            for (const PageFocus f : order)
                if (is_button(f))
                    row.push_back(f);
            const int here = static_cast<int>(std::find(row.begin(), row.end(), page_focus_) - row.begin());
            const int next = here + (input.nav == Direction::left ? -1 : 1);
            if (on_buttons && next >= 0 && next < static_cast<int>(row.size()))
            {
                page_focus_ = row[next];
                page_button_ = page_focus_;
                feedback.play(audio::Cue::focus, 1.0f, ui::pan_for_x(focus_plate(layout, page_focus_).cx()));
            }
            else
                page_refuse(feedback, input.nav_repeat);
            break;
        }
        case Direction::up:
            if (on_buttons || at <= 0)
                page_refuse(feedback, input.nav_repeat);
            else
            {
                // Up from the first row goes back to the button last used.
                PageFocus target = order[at - 1];
                if (is_button(target))
                    target = page_button_;
                page_focus_ = target;
                feedback.play(audio::Cue::focus, 1.0f, ui::pan_for_x(focus_plate(layout, page_focus_).cx()));
            }
            break;
        case Direction::down:
        {
            PageFocus target = page_focus_;
            if (on_buttons)
            {
                for (const PageFocus f : order)
                    if (!is_button(f))
                    {
                        target = f;
                        break;
                    }
            }
            else if (at + 1 < static_cast<int>(order.size()))
                target = order[at + 1];
            if (target != page_focus_)
            {
                page_focus_ = target;
                feedback.play(audio::Cue::focus, 0.96f, ui::pan_for_x(focus_plate(layout, page_focus_).cx()));
            }
            else
                page_refuse(feedback, input.nav_repeat);
            break;
        }
        case Direction::none:
            break;
        }

        if (!input.is_pressed(Action::confirm))
            return;
        switch (page_focus_)
        {
        case PageFocus::watch:
            dialog_open_ = true;
            dialog_step_ = 0;
            dialog_choice_ = std::max(0, translator_idx_);
            dialog_position_.snap(static_cast<float>(dialog_choice_));
            dialog_scroll_.position.snap(0.0f);
            feedback.play(audio::Cue::modal_open);
            break;
        case PageFocus::resume:
            if (page_model_.has_resume)
            {
                const rezka::ContinueEntry &e = page_model_.resume;
                translator_idx_ = e.translator_idx;
                quality_idx_ = e.quality_idx;
                season_idx_ = e.season_idx;
                episode_idx_ = e.episode_idx;
                feedback.play(audio::Cue::select);
                rezka::App::instance().play_title_async(page_item_, translator_idx_, quality_idx_,
                                                        season_idx_, episode_idx_, e.t);
            }
            break;
        case PageFocus::bookmark:
            toggle_bookmark(feedback);
            break;
        case PageFocus::qr:
            open_qr(page_model_.url, "Страница фильма", feedback);
            break;
        case PageFocus::genres:
        case PageFocus::lists:
        case PageFocus::collections:
        {
            const int which = page_focus_ == PageFocus::genres ? 0 : page_focus_ == PageFocus::lists ? 1 : 2;
            open_rows_[which] = !open_rows_[which];
            feedback.play(audio::Cue::select);
            break;
        }
        case PageFocus::cast:
            if (cast_index_ < static_cast<int>(page_model_.cast.size()))
            {
                const rezka::Person &person = page_model_.cast[cast_index_];
                open_filmography(person.url, person.name, feedback);
            }
            break;
        case PageFocus::trailer:
            if (page_model_.trailer_id.empty())
            {
                show_message("Трейлер недоступен");
                feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
            }
            else
                open_qr("https://www.youtube.com/watch?v=" + page_model_.trailer_id, "Трейлер на YouTube",
                        feedback);
            break;
        }
    }

    void start_watch(app::Feedback &feedback)
    {
        press_.trigger();
        feedback.play(audio::Cue::select);
        write_log("[DEBUG] start_watch movie=%d tr=%d q=%d s=%d e=%d\n", page_item_, translator_idx_,
                  quality_idx_, season_idx_, episode_idx_);
        // quality_idx_ is the row in the quality dialog (1080p Ultra, 1080p, … or
        // the title's streams once they are known). play_title_async matches by label.
        rezka::App::instance().play_title_async(page_item_, translator_idx_, quality_idx_, season_idx_,
                                               episode_idx_);
    }

    void add_to_cart(app::Feedback &feedback)
    {
        if (owned(page_item_) || in_cart(page_item_))
        {
            // Nothing to buy twice. The line under the buttons says why.
            page_refuse(feedback, false);
            return;
        }
        Line line;
        line.item = page_item_;
        line.open.snap(1.0f);
        lines_.push_back(line);
        press_.trigger();
        // Each title in the cart sounds a step higher than the one before.
        const float step = static_cast<float>(std::min(live_lines(), 7));
        feedback.play(audio::Cue::select, 0.96f + 0.05f * step, ui::pan_for_x(kPageX + 135.0f));
        flight_item_ = page_item_;
        flight_active_ = true;
        flight_.start(calm() ? 0.2f : kFlySeconds);
    }

    // ---- input: the cart drawer and the checkout ---------------------------

    void open_drawer(app::Feedback &feedback)
    {
        drawer_open_ = true;
        // Straight to the point: the focus opens on Checkout, the lines are
        // one step up.
        drawer_focus_ = live_lines();
        drawer_ring_.snap(drawer_focus_target());
        feedback.play(audio::Cue::open, 1.0f, 0.5f);
    }

    void drawer_refuse(app::Feedback &feedback, bool repeat)
    {
        if (repeat)
            return;
        feedback.play(audio::Cue::error, 1.0f, 0.4f, 0.6f);
        feedback.rumble(0.25f, 0.05f);
        drawer_nudge_.trigger();
    }

    void update_drawer(const InputFrame &input, app::Feedback &feedback)
    {
        if (input.is_pressed(Action::back) || input.is_pressed(Action::west))
        {
            drawer_open_ = false;
            feedback.play(audio::Cue::back, 1.0f, 0.5f);
            return;
        }
        const int live = live_lines();
        drawer_focus_ = std::clamp(drawer_focus_, 0, live);
        if (input.nav == Direction::up || input.nav == Direction::down)
        {
            const int next = drawer_focus_ + (input.nav == Direction::down ? 1 : -1);
            if (next < 0 || next > live)
                drawer_refuse(feedback, input.nav_repeat);
            else
            {
                drawer_focus_ = next;
                feedback.play(audio::Cue::focus,
                              1.08f - 0.03f * static_cast<float>(std::min(next, 8)), 0.45f);
            }
        }
        if (input.is_pressed(Action::north))
        {
            const int index = drawer_focus_ < live ? live_line(drawer_focus_) : -1;
            if (index < 0)
                drawer_refuse(feedback, false);
            else
            {
                lines_[static_cast<std::size_t>(index)].leaving = true;
                // The glass set has no "erase": a switch going off, low.
                feedback.play(audio::Cue::toggle, 0.84f, 0.45f);
            }
        }
        if (input.is_pressed(Action::confirm))
        {
            if (drawer_focus_ < live)
            {
                // Cross on a line is "I am done looking": on to the button.
                drawer_focus_ = live;
                feedback.play(audio::Cue::focus, 0.9f, 0.45f);
            }
            else if (live == 0)
                drawer_refuse(feedback, false);
            else
            {
                dialog_open_ = true;
                dialog_choice_ = 0; // a purchase is confirmed, never defaulted
                dialog_position_.snap(0.0f);
                feedback.play(audio::Cue::modal_open);
            }
        }
    }

    static constexpr float kDlgItemH = 48.0f;
    static constexpr float kDlgItemGap = 8.0f;
    static constexpr float kDlgSectionH = 30.0f;
    static constexpr float kDlgBtnH = 54.0f;

    void dialog_list_counts(const rezka::MovieData &m, int &num_tr, int &num_se, int &num_ep, int &num_q) const
    {
        num_tr = static_cast<int>(m.details.translators.size());
        num_se = 0;
        num_ep = 0;
        if (m.details.is_series) {
            num_se = static_cast<int>(m.details.seasons.size());
            if (num_se > 0) {
                const int s_idx = std::clamp(season_idx_, 0, num_se - 1);
                num_ep = static_cast<int>(m.details.seasons[s_idx].episodes.size());
            }
        }
        num_q = m.streams.empty() ? 5 : static_cast<int>(m.streams.size());
    }

    int dialog_focus_count(const rezka::MovieData &m) const
    {
        int num_tr = 0, num_se = 0, num_ep = 0, num_q = 0;
        dialog_list_counts(m, num_tr, num_se, num_ep, num_q);
        if (dialog_step_ == 0)
            return std::max(1, num_tr) + num_se + num_ep + 1; // lists + "Далее"
        return num_q + 2; // qualities + play + back
    }

    void dialog_apply_focus(const rezka::MovieData &m)
    {
        int num_tr = 0, num_se = 0, num_ep = 0, num_q = 0;
        dialog_list_counts(m, num_tr, num_se, num_ep, num_q);
        const int tr_slots = std::max(1, num_tr);
        if (dialog_step_ == 0) {
            int c = dialog_choice_;
            if (c < tr_slots) {
                if (num_tr > 0)
                    translator_idx_ = std::clamp(c, 0, num_tr - 1);
                return;
            }
            c -= tr_slots;
            if (c < num_se) {
                if (season_idx_ != c) {
                    season_idx_ = c;
                    episode_idx_ = 0;
                }
                return;
            }
            c -= num_se;
            if (c < num_ep) {
                episode_idx_ = std::clamp(c, 0, std::max(0, num_ep - 1));
                return;
            }
            return;
        }
        if (dialog_choice_ < num_q)
            quality_idx_ = dialog_choice_;
    }

    float dialog_focus_offset(const rezka::MovieData &m) const
    {
        int num_tr = 0, num_se = 0, num_ep = 0, num_q = 0;
        dialog_list_counts(m, num_tr, num_se, num_ep, num_q);
        const int tr_slots = std::max(1, num_tr);
        float y = kDlgSectionH; // first section header
        auto advance_items = [&](int count, int focus_base) {
            for (int i = 0; i < count; ++i) {
                if (focus_base + i == dialog_choice_)
                    return y;
                y += kDlgItemH + kDlgItemGap;
            }
            return -1.0f;
        };
        if (dialog_step_ == 0) {
            float hit = advance_items(tr_slots, 0);
            if (hit >= 0.0f)
                return hit;
            if (num_se > 0) {
                y += kDlgSectionH;
                hit = advance_items(num_se, tr_slots);
                if (hit >= 0.0f)
                    return hit;
            }
            if (num_ep > 0) {
                y += kDlgSectionH;
                hit = advance_items(num_ep, tr_slots + num_se);
                if (hit >= 0.0f)
                    return hit;
            }
            y += 6.0f;
            return y; // next button
        }
        float hit = advance_items(num_q, 0);
        if (hit >= 0.0f)
            return hit;
        y += 6.0f;
        if (dialog_choice_ == num_q)
            return y; // play
        y += kDlgBtnH + 10.0f;
        return y; // back
    }

    float dialog_content_height(const rezka::MovieData &m) const
    {
        int num_tr = 0, num_se = 0, num_ep = 0, num_q = 0;
        dialog_list_counts(m, num_tr, num_se, num_ep, num_q);
        const int tr_slots = std::max(1, num_tr);
        if (dialog_step_ == 0) {
            float h = kDlgSectionH + tr_slots * (kDlgItemH + kDlgItemGap);
            if (num_se > 0)
                h += kDlgSectionH + num_se * (kDlgItemH + kDlgItemGap);
            if (num_ep > 0)
                h += kDlgSectionH + num_ep * (kDlgItemH + kDlgItemGap);
            h += 6.0f + kDlgBtnH;
            return h;
        }
        return kDlgSectionH + num_q * (kDlgItemH + kDlgItemGap) + 6.0f + kDlgBtnH + 10.0f + 46.0f;
    }

    void update_dialog(const InputFrame &input, app::Feedback &feedback)
    {
        bool start_playback = false;
        {
            auto &app = rezka::App::instance();
            std::lock_guard<std::mutex> lock(app.catalog_mutex);
            if (page_item_ < 0 || page_item_ >= static_cast<int>(app.movies.size())) {
                dialog_open_ = false;
                return;
            }
            auto &m = app.movies[page_item_];
            const bool is_series = m.details.is_series;
            int num_tr = 0, num_se = 0, num_ep = 0, num_q = 0;
            dialog_list_counts(m, num_tr, num_se, num_ep, num_q);
            const int tr_slots = std::max(1, num_tr);
            const int max_rows = dialog_focus_count(m);
            dialog_choice_ = std::clamp(dialog_choice_, 0, std::max(0, max_rows - 1));

            if (input.is_pressed(Action::back))
            {
                if (dialog_step_ == 0) {
                    dialog_open_ = false;
                    feedback.play(audio::Cue::modal_close);
                } else {
                    dialog_step_ = 0;
                    dialog_choice_ = tr_slots + num_se + num_ep; // "Далее"
                    dialog_scroll_.position.snap(0.0f);
                    feedback.play(audio::Cue::back);
                }
                return;
            }

            if (input.nav == Direction::up || input.nav == Direction::down)
            {
                const int delta = (input.nav == Direction::down) ? 1 : -1;
                const int next = dialog_choice_ + delta;
                if (next < 0 || next >= max_rows) {
                    dialog_nudge_.trigger();
                    feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
                } else {
                    dialog_choice_ = next;
                    dialog_apply_focus(m);
                    // Season change can shrink the episode list -- re-clamp.
                    dialog_choice_ = std::clamp(dialog_choice_, 0, std::max(0, dialog_focus_count(m) - 1));
                    dialog_apply_focus(m);
                    feedback.play(audio::Cue::focus);
                }
                return;
            }

            // Left/right ignored -- all options are visible in the list.
            if (input.nav == Direction::left || input.nav == Direction::right)
                return;

            if (!input.is_pressed(Action::confirm))
                return;

            if (dialog_step_ == 0) {
                const int next_btn = tr_slots + num_se + num_ep;
                dialog_apply_focus(m);
                if (dialog_choice_ == next_btn || (!is_series && dialog_choice_ < tr_slots)) {
                    // Next button, or confirm on a voice for a movie → quality step.
                    if (dialog_choice_ < tr_slots && num_tr > 0)
                        translator_idx_ = dialog_choice_;
                    dialog_step_ = 1;
                    dialog_choice_ = std::clamp(quality_idx_, 0, std::max(0, num_q - 1));
                    dialog_scroll_.position.snap(0.0f);
                    feedback.play(audio::Cue::select);
                } else {
                    // Confirm on season/episode (or voice in series): move to next block.
                    dialog_choice_ = std::min(dialog_choice_ + 1, next_btn);
                    dialog_apply_focus(m);
                    feedback.play(audio::Cue::focus);
                }
            } else {
                // Quality step
                dialog_apply_focus(m);
                if (dialog_choice_ < num_q) {
                    quality_idx_ = dialog_choice_;
                    dialog_choice_ = num_q; // jump to Play
                    feedback.play(audio::Cue::focus);
                } else if (dialog_choice_ == num_q) {
                    dialog_open_ = false;
                    start_playback = true;
                } else {
                    dialog_step_ = 0;
                    dialog_choice_ = tr_slots + num_se + num_ep;
                    dialog_scroll_.position.snap(0.0f);
                    feedback.play(audio::Cue::back);
                }
            }
        }
        if (start_playback)
            start_watch(feedback);
    }

    void purchase(app::Feedback &feedback)
    {
        dialog_open_ = false;
        bought_ = 0;
        for (Line &line : lines_)
        {
            if (line.leaving)
                continue;
            if (line.item >= 0 && line.item < static_cast<int>(owned_.size()))
                owned_[static_cast<std::size_t>(line.item)] = true;
            line.leaving = true;
            line.sold = true;
            // The rows leave one after the other, top first.
            line.delay = calm() ? 0.0f : 0.07f * static_cast<float>(bought_);
            ++bought_;
        }
        settling_ = true;
        drawer_focus_ = 0;
        // Ownership changed what the filters hold: the shelves follow.
        previous_visible_ = visible_;
        previous_scroll_ = scroll_.value;
        rebuild_visible();
        swap_.start(calm() ? 0.15f : 0.5f);
        feedback.play(audio::Cue::saved);
        feedback.rumble(0.5f, 0.12f);
    }

    // ---- animation ---------------------------------------------------------

    void apply_palette(bool snap)
    {
        // The page takes its own title's colours while it is open.
        int shown = -1;
        if (!featured_.empty())
        {
            const int slot = std::clamp(banner_, 0, static_cast<int>(featured_.size()) - 1);
            shown = page_open_ ? page_item_ : featured_[static_cast<std::size_t>(slot)];
        }
        const demo::Item &it = item(shown);
        const Color targets[3] = {gfx::mix(kCoal, it.dark, 0.3f),
                                  gfx::mix(kCoalLow, it.dark, 0.12f),
                                  gfx::mix(it.mid, it.accent, 0.15f)};
        for (int i = 0; i < 3; ++i)
        {
            if (snap)
                palette_[i].snap(targets[i]);
            else
                palette_[i].target(targets[i]);
        }
        const demo::Item &tone_item = item(shown);
        const Color tone = gfx::mix(kPanel, tone_item.dark, 0.5f);
        if (snap)
            tone_.snap(tone);
        else
            tone_.target(tone);
    }

    void animate(float dt, app::Feedback &feedback)
    {
        // The banner turns its own pages, except while the player is on it
        // or busy with something in front of it.
        const bool covered =
            page_open_ || drawer_open_ || dialog_open_ || search_open_ || profile_open_ ||
            person_open_;
        if (!featured_.empty() && zone_ != Zone::banner && !covered)
        {
            banner_clock_ += dt;
            if (banner_clock_ >= kBannerSeconds)
                show_banner((banner_ + 1) % static_cast<int>(featured_.size()), 1.0f);
        }
        if (!covered && !calm())
        {
            drift_ += dt;
            drift_previous_ += dt;
        }
        tv_animate(dt);
        dp_animate(dt);
        wd_animate(dt);
        cm_animate(dt);
        banner_fade_.update(dt);
        banner_focus_.target = zone_ == Zone::banner ? 1.0f : 0.0f;
        banner_focus_.update(dt, 16.0f);
        apply_palette(false);
        for (ui::SpringColor &colour : palette_)
            colour.update(dt, 3.0f);
        tone_.update(dt, 4.0f);

        // The page scroll, then everything that rides on it.
        scroll_.target = scroll_target();
        scroll_.update(dt, omega(11.0f));
        swap_.update(dt);
        const int focused = zone_ == Zone::grid && !visible_.empty()
                                ? visible_[static_cast<std::size_t>(focus_)]
                                : -1;
        for (int i = 0; i < static_cast<int>(lift_.size()); ++i)
        {
            lift_[static_cast<std::size_t>(i)].target = i == focused ? 1.0f : 0.0f;
            lift_[static_cast<std::size_t>(i)].update(dt, 18.0f);
        }
        ring_.target(focus_target());
        ring_.update(dt, 20.0f);
        ring_radius_.target = focus_radius();
        ring_radius_.update(dt, 20.0f);
        plate_.target = focused >= 0 ? 1.0f : 0.0f;
        plate_.update(dt, 16.0f);
        glow_.target(focused >= 0 ? item(focused).accent : kAccent);
        glow_.update(dt, 10.0f);
        Rect chips[kFilters];
        chip_layout(chips);
        filter_ = std::clamp(filter_, 0, std::max(0, active_chip_count() - 1));
        chip_pill_.target(chips[filter_]);
        chip_pill_.update(dt, 20.0f);
        // The chips stick instantly: their pill must not lag behind them.
        chip_pill_.y.snap(chips[filter_].y);
        nudge_.update(dt, 9.0f);
        chip_nudge_.update(dt, 9.0f);

        // The product page opens; its focus ring glides to the focus, and its
        // scroll keeps the focus in view. The QR layer fades in and out.
        page_.target = page_open_ ? 1.0f : 0.0f;
        page_.update(dt, omega(page_open_ ? 9.5f : 12.0f));
        qr_.target = qr_open_ ? 1.0f : 0.0f;
        qr_.update(dt, omega(12.0f));
        const PageLayout layout = page_layout();
        const Rect plate = focus_plate(layout, page_focus_);
        if (page_open_)
        {
            const float max_scroll = std::max(0.0f, layout.bottom - kHeight + 40.0f);
            float want = page_scroll_.target;
            if (plate.y - want < kPageTopKeep)
                want = plate.y - kPageTopKeep;
            if (plate.y + plate.h - want > kPageViewBottom)
                want = plate.y + plate.h - kPageViewBottom;
            page_scroll_.target = std::clamp(want, 0.0f, max_scroll);
            page_scroll_.update(dt, 14.0f);
        }
        page_ring_.target(plate.inset(-6.0f));
        page_ring_.update(dt, 20.0f);
        page_ring_radius_.target = focus_radius(page_focus_) + 6.0f;
        page_ring_radius_.update(dt, 20.0f);
        press_.update(dt, 9.0f);
        page_nudge_.update(dt, 9.0f);
        wish_pop_.update(dt, 6.0f);

        // The flight into the cart, and the counter it lands in.
        if (flight_active_)
        {
            flight_.update(dt);
            if (!flight_.running)
            {
                flight_active_ = false;
                bump_.kick(calm() ? 0.0f : 9.0f);
                const float step = static_cast<float>(std::min(live_lines(), 7));
                feedback.play(audio::Cue::notify, 0.96f + 0.05f * step, ui::pan_for_x(kCartX));
            }
        }
        const int count = live_lines();
        if (!flight_active_ && count != count_shown_)
        {
            count_previous_ = count_shown_;
            count_shown_ = count;
            count_roll_.start(0.28f);
        }
        count_roll_.update(dt);
        bump_.update(dt, 20.0f, 0.42f);
        badge_.target = count_shown_ > 0 ? 1.0f : 0.0f;
        badge_.update(dt, 16.0f);

        // The drawer: rows close, the list keeps the focus in view, the
        // subtotal counts while somebody can watch it.
        drawer_.target = drawer_open_ ? 1.0f : 0.0f;
        drawer_.update(dt, omega(12.0f));
        for (Line &line : lines_)
        {
            if (line.leaving && line.delay > 0.0f)
                line.delay -= dt;
            line.open.target = line.leaving && line.delay <= 0.0f ? 0.0f : 1.0f;
            line.open.update(dt, omega(10.0f));
        }
        std::erase_if(lines_,
                      [](const Line &line) { return line.leaving && line.open.value < 0.01f; });
        const int live = live_lines();
        drawer_focus_ = std::clamp(drawer_focus_, 0, live);
        if (drawer_focus_ < live)
        {
            const float start = line_offset(live_line(drawer_focus_));
            drawer_scroll_.reveal(start, start + kLineH, kLinesView, 0.0f,
                                  line_offset(static_cast<int>(lines_.size())));
        }
        else
        {
            // On the button: show the end of the list, where the total is.
            const float end = line_offset(static_cast<int>(lines_.size()));
            drawer_scroll_.position.target = std::max(0.0f, end - kLinesView);
        }
        drawer_scroll_.update(dt, omega(14.0f));
        empty_.target = lines_.empty() ? 1.0f : 0.0f;
        empty_.update(dt, omega(10.0f));
        drawer_ring_.target(drawer_focus_target());
        drawer_ring_.update(dt, 20.0f);
        drawer_nudge_.update(dt, 9.0f);
        // A one-shot count (not a spring): it must arrive on the exact
        // number at a known moment, not creep toward it.
        if (drawer_.value > 0.5f)
        {
            if (subtotal() != subtotal_to_)
            {
                subtotal_from_ = subtotal_shown();
                subtotal_to_ = subtotal();
                subtotal_count_.start(calm() ? 0.15f : 0.55f);
            }
            subtotal_count_.update(dt);
        }

        // The checkout: when the last bought row has left, say so.
        dialog_.target = dialog_open_ ? 1.0f : 0.0f;
        dialog_.update(dt, omega(14.0f));
        dialog_position_.target = static_cast<float>(dialog_choice_);
        dialog_position_.update(dt, 22.0f);
        dialog_nudge_.update(dt, 9.0f);
        if (dialog_open_ && page_item_ >= 0) {
            auto &app = rezka::App::instance();
            std::lock_guard<std::mutex> lock(app.catalog_mutex);
            if (page_item_ < static_cast<int>(app.movies.size())) {
                const auto &m = app.movies[page_item_];
                int num_tr = 0, num_se = 0, num_ep = 0, num_q = 0;
                dialog_list_counts(m, num_tr, num_se, num_ep, num_q);
                const float view = 720.0f - 118.0f - 44.0f;
                const bool on_action = (dialog_step_ == 0)
                                          ? (dialog_choice_ >= std::max(1, num_tr) + num_se + num_ep)
                                          : (dialog_choice_ >= num_q);
                const float item_h = on_action ? kDlgBtnH : kDlgItemH;
                const float start = dialog_focus_offset(m);
                const float content = dialog_content_height(m);
                dialog_scroll_.reveal(start, start + item_h, view, 12.0f, content);
            }
        }
        dialog_scroll_.update(dt, omega(14.0f));
        if (settling_)
        {
            bool busy = false;
            for (const Line &line : lines_)
                busy = busy || line.sold;
            if (!busy)
            {
                settling_ = false;
                toast_left_ = kToastSeconds;
                toast_age_ = 0.0f;
                toast_count_ = bought_;
                toast_text_.clear();
                feedback.play(audio::Cue::notify, 1.12f);
            }
        }
        toast_left_ = std::max(0.0f, toast_left_ - dt);
        toast_age_ += dt;
        toast_.target = toast_left_ > 0.0f ? 1.0f : 0.0f;
        toast_.update(dt, omega(13.0f));
    }

    Rect drawer_focus_target() const
    {
        const int live = live_lines();
        if (drawer_focus_ >= live)
            return checkout_rect().inset(-6.0f);
        const float x = kWidth - kDrawerW;
        const float y = kLinesTop + line_offset(live_line(drawer_focus_)) - drawer_scroll_.offset();
        return {x + 24.0f, y + 4.0f, kDrawerW - 48.0f, kLineH - 8.0f};
    }

    // ---- drawing: prices ---------------------------------------------------

    // A price: the coin mark, then the number in the mono face, so the
    // digits keep their places while a total counts. Returns the width.
    //
    // Like every price routine it takes the layers to draw (see Layer): the
    // geometry is the same in each pass, only some of it is recorded.
    float amount(gfx::DrawList &list, int credits, float x, float baseline, float size,
                 Color colour, bool right_align = false, unsigned layers = kAllLayers) const
    {
        char text[24];
        format_credits(text, sizeof(text), credits);
        const float radius = size * 0.28f;
        const float gap = size * 0.24f;
        const float width = radius * 2.0f + gap + context_.fonts.mono.measure(text, size);
        const float left = right_align ? x - width : x;
        if (layers & kShapes)
            draw_coin(list, left + radius, baseline - size * 0.35f, radius, colour);
        if (layers & kMono)
            ui::text(list, context_.fonts.mono, text, left + radius * 2.0f + gap, baseline, size,
                     colour);
        return width;
    }

    // The price that no longer applies: dimmer, with a thin line through it.
    float struck(gfx::DrawList &list, int credits, float x, float baseline, float size,
                 Color colour, bool right_align = false, unsigned layers = kAllLayers) const
    {
        char text[24];
        format_credits(text, sizeof(text), credits);
        const float width = context_.fonts.mono.measure(text, size);
        const float left = right_align ? x - width : x;
        if (layers & kMono)
            ui::text(list, context_.fonts.mono, text, left, baseline, size, colour);
        const float y = baseline - size * 0.34f;
        if (layers & kShapes)
            list.line(left - 3.0f, y, left + width + 3.0f, y, std::max(1.6f, size * 0.08f),
                      colour.with_alpha(1.4f));
        return width;
    }

    // "-30%" on the accent. Returns the width.
    float discount_badge(gfx::DrawList &list, int percent, float x, float cy, float height,
                         float alpha, unsigned layers = kAllLayers) const
    {
        char text[8];
        std::snprintf(text, sizeof(text), "-%d%%", percent);
        const float size = height * 0.6f;
        const float width = context_.fonts.semibold.measure(text, size) + height * 0.8f;
        if (layers & kShapes)
            list.rounded_rect({x, cy - height * 0.5f, width, height}, height * 0.32f,
                              kAccent.with_alpha(alpha));
        if (layers & kSemibold)
            ui::text(list, context_.fonts.semibold, text, x + width * 0.5f, centred(cy, size), size,
                     kOnAccent.with_alpha(alpha), gfx::Align::center);
        return width;
    }

    // What a title costs, the same way everywhere: "Owned" for what the
    // player has, "Free", a plain price, or the new price in the accent
    float price_line(gfx::DrawList &list, int index, float x, float baseline, float size,
                     float alpha, bool badge, unsigned layers = kAllLayers) const
    {
        const ui::Fonts &fonts = context_.fonts;
        const demo::Item &it = item(index);
        char buf[64];
        float score = 7.0f + static_cast<float>((index * 13) % 28) * 0.1f;
        std::snprintf(buf, sizeof(buf), "%.1f", score);
        if (layers & kShapes)
        {
            list.star(x + 10.0f, baseline - size * 0.35f, size * 0.35f, kStar.with_alpha(alpha));
        }
        if (layers & kSemibold)
        {
            ui::text(list, fonts.semibold, buf, x + 28.0f, baseline, size, kStar.with_alpha(alpha));
            char ybuf[32];
            std::snprintf(ybuf, sizeof(ybuf), " •  %d", it.year > 0 ? it.year : 2026);
            ui::text(list, fonts.regular, ybuf, x + 28.0f + fonts.semibold.measure(buf, size), baseline, size * 0.85f, kInk.with_alpha(0.7f * alpha));
        }
        return 160.0f;
    }

    // ---- drawing: the home page --------------------------------------------

    void draw_top_bar(gfx::DrawList &list) const
    {
        const ui::Fonts &fonts = context_.fonts;
        const float in = tween::stagger(age_, 0, 0.07f, 0.5f);
        list.push_opacity(in);
        const float cy = 52.0f - 8.0f * (1.0f - in) * travel();

        // 1. Brand Logo: "HD" in white + "REZKA" in red
        float hdw = ui::text(list, fonts.display, "HD", kMargin, centred(cy, 28), 28, kWhite, gfx::Align::left);
        ui::text(list, fonts.display, "REZKA", kMargin + hdw + 2.0f, centred(cy, 28), 28, kAccent, gfx::Align::left);

        // 2. Navigation Tabs in center (L1/R1)
        const char *tabs[] = {"Главная", "Фильмы", "Сериалы", "Мультфильмы", "Аниме"};
        float tab_widths[kTopTabs];
        float total_w = 0.0f;
        for (int i = 0; i < kTopTabs; ++i)
        {
            tab_widths[i] = fonts.semibold.measure(tabs[i], 18) + 26.0f;
            total_w += tab_widths[i] + 8.0f;
        }
        total_w -= 8.0f;
        float tx = (kWidth - total_w) * 0.5f;
        const ui::GlyphStyle tab_glyphs = ui::GlyphStyle::dark();
        ui::draw_button(list, fonts, tab_glyphs, ui::Button::l1, tx - 40.0f, cy, 24);
        for (int i = 0; i < kTopTabs; ++i)
        {
            Rect tr{tx, cy - 16.0f, tab_widths[i], 32.0f};
            if (i == top_tab_)
            {
                list.rounded_rect(tr, 16.0f, kPanel);
                list.bordered_rect(tr, 16.0f, kClear, 1.5f, kAccent);
                ui::text(list, fonts.semibold, tabs[i], tr.cx(), centred(cy, 18), 18, kWhite,
                         gfx::Align::center);
            }
            else
            {
                ui::text(list, fonts.regular, tabs[i], tr.cx(), centred(cy, 18), 18,
                         kInk.with_alpha(0.6f), gfx::Align::center);
            }
            tx += tab_widths[i] + 8.0f;
        }
        ui::draw_button(list, fonts, tab_glyphs, ui::Button::r1, tx + 16.0f, cy, 24);

        // 3. Right side: △ Поиск + Clock
        float rx = kRight;
        // Real-time Clock
        std::time_t rawtime = std::time(nullptr);
        std::tm *timeinfo = std::localtime(&rawtime);
        char clock_str[16] = "19:45";
        if (timeinfo)
            std::strftime(clock_str, sizeof(clock_str), "%H:%M", timeinfo);
        ui::text(list, fonts.semibold, clock_str, rx, centred(cy, 20), 20, kInk, gfx::Align::right);

        // Search shortcut
        float clock_w = fonts.semibold.measure(clock_str, 20);
        float search_x = rx - clock_w - 28.0f;
        const ui::GlyphStyle glyphs = ui::GlyphStyle::dark();
        ui::draw_button(list, fonts, glyphs, ui::Button::triangle, search_x - 68.0f, cy, 24);
        ui::text(list, fonts.regular, "Поиск", search_x, centred(cy, 18), 18, kInk.with_alpha(0.7f), gfx::Align::right);

        // Options shortcut: Profile
        float prof_x = search_x - 100.0f;
        ui::draw_button(list, fonts, glyphs, ui::Button::options, prof_x - 84.0f, cy, 24);
        auto &app = rezka::App::instance();
        std::string acc_name = "Профиль";
        {
            std::lock_guard<std::mutex> lock(app.catalog_mutex);
            if (app.account.logged_in && !app.account.name.empty())
                acc_name = app.account.name;
        }
        ui::text(list, fonts.regular, acc_name.c_str(), prof_x, centred(cy, 18), 18, kInk.with_alpha(0.7f), gfx::Align::right);

        list.pop_opacity();
    }

    // One featured title's showcase banner matching the web app hero
    void draw_banner_text(gfx::DrawList &list, int slot, const Rect &b, float alpha,
                          float slide) const
    {
        if (alpha <= 0.01f)
            return;
        const ui::Fonts &fonts = context_.fonts;
        const int index = featured_[static_cast<std::size_t>(slot)];
        const float x = b.x + 56.0f + slide;
        list.push_opacity(alpha);

        // Kicker: entity badge from HDrezkaTV (Фильм / Сериал) or "Новинка"
        std::string kicker = "Новинка";
        {
            auto &app = rezka::App::instance();
            std::lock_guard<std::mutex> lk(app.catalog_mutex);
            if (index >= 0 && index < static_cast<int>(app.movies.size()))
            {
                if (!app.movies[static_cast<std::size_t>(index)].entity.empty())
                    kicker = app.movies[static_cast<std::size_t>(index)].entity;
                else if (!app.movies[static_cast<std::size_t>(index)].genre.empty())
                    kicker = app.movies[static_cast<std::size_t>(index)].genre;
            }
        }
        ui::text(list, fonts.semibold, kicker.c_str(), x, centred(b.y + 72.0f, 15), 15,
                 kInk.with_alpha(0.85f), gfx::Align::left, 3.5f);

        // Main Title (huge display font)
        std::string title = item_title(index);
        ui::text(list, fonts.display, fonts.display.font->fit(title.c_str(), 72, 900.0f), x - 2.0f,
                 b.y + 118.0f, 72, kInk);

        // Metadata badges: Year, Rating, quality
        float meta_x = x;
        const float meta_y = b.y + 210.0f;
        char year_str[32];
        int year = 2024;
        float rating = item_rating(index);
        std::string quality_badge;
        std::string info_line;
        const int quality_src = center_poster_index();
        const int q_index = quality_src >= 0 ? quality_src : index;
        {
            auto &app = rezka::App::instance();
            std::lock_guard<std::mutex> lk(app.catalog_mutex);
            if (index >= 0 && index < static_cast<int>(app.movies.size()))
            {
                year = app.movies[index].year;
                rating = app.movies[index].rating;
                info_line = app.movies[index].info;
            }
            if (q_index >= 0 && q_index < static_cast<int>(app.movies.size()))
            {
                quality_badge = app.movies[q_index].quality;
                if (quality_badge.empty() && !app.movies[q_index].streams.empty())
                    quality_badge = app.movies[q_index].streams.front().quality;
            }
        }
        // Rating star + score
        list.star(meta_x + 8.0f, meta_y, 11.0f, kStar);
        meta_x += 24.0f;
        char rat_buf[16];
        std::snprintf(rat_buf, sizeof(rat_buf), "%.1f", static_cast<double>(rating));
        meta_x += ui::text(list, fonts.semibold, rat_buf, meta_x, centred(meta_y, 20), 20, kInk) + 16.0f;

        // Year
        std::snprintf(year_str, sizeof(year_str), "%d", year > 0 ? year : 2024);
        meta_x += ui::text(list, fonts.regular, year_str, meta_x, centred(meta_y, 20), 20,
                           kInk.with_alpha(0.75f)) +
                  18.0f;

        // Quality from center poster (+ HD fallback)
        std::vector<std::string> badges;
        if (!quality_badge.empty())
            badges.push_back(quality_badge);
        else
            badges.push_back("HD");
        if (quality_badge.find("2160") != std::string::npos ||
            quality_badge.find("4K") != std::string::npos ||
            quality_badge.find("Ultra") != std::string::npos)
            badges.push_back("HDR");
        for (const auto &badge : badges)
        {
            float bw = fonts.semibold.measure(badge.c_str(), 14) + 16.0f;
            Rect br{meta_x, meta_y - 12.0f, bw, 24.0f};
            list.bordered_rect(br, 5.0f, kClear, 1.2f, kInk.with_alpha(0.35f));
            ui::text(list, fonts.semibold, badge.c_str(), br.cx(), centred(meta_y, 14), 14,
                     kInk.with_alpha(0.85f), gfx::Align::center);
            meta_x += bw + 8.0f;
        }

        // Blurb / Description
        std::string blurb = info_line.empty() ? item_blurb(index) : info_line;
        ui::text(list, fonts.regular, fonts.regular.font->fit(blurb.c_str(), 24, 880.0f), x,
                 b.y + 268.0f, 24, kInk.with_alpha(0.82f));

        // Action Buttons (www: Смотреть + В мой список)
        const float btn_y = b.y + 340.0f;
        const Rect play_btn{x, btn_y, 196.0f, 52.0f};
        const float lit = banner_focus_.value;
        list.glow(play_btn, 12.0f, 20.0f, kAccent.with_alpha(0.4f * (0.8f + 0.2f * lit)));
        list.rounded_rect(play_btn, 12.0f, kAccent);
        // Play triangle icon
        draw_cart(list, play_btn.x + 32.0f, play_btn.cy(), 22.0f, kOnAccent);
        ui::text(list, fonts.semibold, "Смотреть", play_btn.x + 58.0f, centred(play_btn.cy(), 22), 22, kOnAccent, gfx::Align::left);

        // [ + В закладки ] - Dark pill button
        const Rect fav_btn{x + 214.0f, btn_y, 220.0f, 52.0f};
        bool is_wished = (index >= 0 && index < static_cast<int>(wished_.size())) ? wished_[static_cast<std::size_t>(index)] : false;
        list.rounded_rect(fav_btn, 12.0f, kPanel);
        list.bordered_rect(fav_btn, 12.0f, kClear, 1.5f, kInk.with_alpha(0.2f));
        list.star(fav_btn.x + 32.0f, fav_btn.cy(), 11.0f, is_wished ? kStar : kInk.with_alpha(0.85f), is_wished ? 0.0f : 2.0f);
        ui::text(list, fonts.semibold, is_wished ? "В списке" : "В мой список", fav_btn.x + 56.0f,
                 centred(fav_btn.cy(), 22), 22, kInk.with_alpha(0.9f), gfx::Align::left);

        list.pop_opacity();
    }

    void draw_banner(gfx::DrawList &list) const
    {
        // The banner fades as the page scrolls it under the top bar; once it
        // is gone it costs nothing.
        const float scroll = scroll_.value;
        const float in = tween::stagger(age_, 1, 0.07f, 0.6f);
        const float visible = in * tween::clamp01(1.0f - scroll / 360.0f);
        if (visible <= 0.004f)
            return;
        if (featured_.empty())
        {
            Rect b = banner_rect();
            b.y -= scroll;
            list.push_opacity(visible);
            list.rounded_rect(b, kBannerRadius, kCoal);
            list.bordered_rect(b, kBannerRadius, kClear, 1.5f, kAccent.with_alpha(0.45f));
            std::string status = "Завантаження каталогу HDRezka...";
            {
                auto &app = rezka::App::instance();
                std::lock_guard<std::mutex> lock(app.catalog_mutex);
                if (!app.catalog_status.empty())
                    status = app.catalog_status;
            }
            ui::text(list, context_.fonts.semibold, status.c_str(), b.cx(), centred(b.cy(), 32), 32, kInk,
                     gfx::Align::center);
            list.pop_opacity();
            return;
        }
        Rect b = banner_rect();
        b.y += 28.0f * (1.0f - in) * travel() - scroll;
        const int index = featured_[static_cast<std::size_t>(banner_)];
        const int before = featured_[static_cast<std::size_t>(banner_previous_)];
        const float fade = banner_fade_.running ? banner_fade_.progress() : 1.0f;
        const Color tone = tone_.value();

        list.push_opacity(visible);
        list.rounded_rect(b, kBannerRadius, tone);

        // Full-bleed artwork with left shade (www .home-hero-shade)
        const Rect art{b.x + b.w - kBannerArtW, b.y, kBannerArtW, b.h};
        if (fade < 1.0f)
        {
            const std::uint32_t before_cover = item_cover(before);
            if (before_cover != 0)
                list.image(before_cover, art, banner_uv(banner_previous_, drift_previous_), kWhite,
                           kBannerRadius);
        }
        const std::uint32_t cover_id = item_cover(index);
        if (cover_id != 0)
            list.image(cover_id, art, banner_uv(banner_, drift_),
                       kWhite.with_alpha(tween::smoothstep(fade)), kBannerRadius);
        else
            list.rounded_rect(art, kBannerRadius, item(index).mid.with_alpha(0.85f));
        list.gradient_rect_h({b.x, b.y, 720.0f, b.h}, 0, tone.with_alpha(0.92f),
                             tone.with_alpha(0.0f));
        list.gradient_rect({b.x, b.y + b.h - 160.0f, b.w, 160.0f}, 0, tone.with_alpha(0.0f),
                           tone.with_alpha(0.95f));
        list.glow({b.x + 180.0f, b.y + 170.0f, 420.0f, 80.0f}, 40, 130,
                  item(index).accent.with_alpha(0.07f * tween::smoothstep(fade)));

        if (fade < 1.0f)
        {
            // The old words leave quickly; the new ones arrive a beat later.
            const float side = banner_direction_ * travel();
            draw_banner_text(list, banner_previous_, b, 1.0f - tween::smoothstep(fade * 2.4f),
                             -40.0f * side * tween::cubic_in(tween::clamp01(fade * 2.4f)));
            const float arrive = tween::clamp01((fade - 0.3f) / 0.7f);
            draw_banner_text(list, banner_, b, tween::smoothstep(arrive),
                             48.0f * side * (1.0f - tween::quint_out(arrive)));
        }
        else
        {
            draw_banner_text(list, banner_, b, 1.0f, 0.0f);
        }

        // Page dots (www .home-hero-dots), above the filter chips.
        const int count = static_cast<int>(featured_.size());
        const float dots_w = static_cast<float>(count - 1) * 20.0f + 44.0f;
        float x = b.x + b.w - 44.0f - dots_w;
        const float cy = b.y + b.h - 78.0f;
        list.rounded_rect({x - 14.0f, cy - 13.0f, dots_w + 28.0f, 26.0f}, 13,
                          kBlack.with_alpha(0.38f));
        for (int i = 0; i < count; ++i)
        {
            if (i == banner_)
            {
                list.rounded_rect({x, cy - 4.0f, 44.0f, 8.0f}, 4, kAccent.with_alpha(0.4f));
                const float filled = 8.0f + 36.0f * tween::clamp01(banner_clock_ / kBannerSeconds);
                list.rounded_rect({x, cy - 4.0f, filled, 8.0f}, 4, kAccent);
                x += 56.0f;
            }
            else
            {
                list.circle(x + 4.0f, cy, 4.0f, kInk.with_alpha(0.42f));
                x += 20.0f;
            }
        }
        list.pop_opacity();
    }

    void draw_chips(gfx::DrawList &list) const
    {
        const ui::Fonts &fonts = context_.fonts;
        const ui::GlyphStyle glyphs = ui::GlyphStyle::dark();
        const float in = tween::stagger(age_, 3, 0.07f, 0.5f);
        Rect chips[kFilters];
        chip_layout(chips);
        const int n = active_chip_count();
        const float cy = chips_y() + kChipH * 0.5f;
        list.push_opacity(in);
        list.push_transform(1.0f, 0, 0, 0, 16.0f * (1.0f - in) * travel());
        ui::draw_button(list, fonts, glyphs, ui::Button::l2, kMargin, cy, 30);

        // Dark glass strip under chips on the hero (www .home-filters look).
        if (home_layout_ && scroll_.value < 40.0f)
        {
            list.rounded_rect({kMargin - 8.0f, chips_y() - 6.0f, kWidth - 2.0f * kMargin + 16.0f,
                               kChipH + 12.0f},
                              18.0f, kBlack.with_alpha(0.45f));
        }

        Rect pill = chip_pill_.value();
        pill.x += ui::shake(chip_nudge_.value, clock_) * chip_nudge_x_;
        list.glow(pill, kChipH * 0.5f, 16.0f, kAccent.with_alpha(0.35f));
        list.rounded_rect(pill, kChipH * 0.5f, kAccent);

        for (int pass = 0; pass < 2; ++pass)
        {
            for (int i = 0; i < n; ++i)
            {
                const Rect &chip = chips[i];
                if (chip.w <= 1.0f)
                    continue;
                const float overlap =
                    std::min(pill.x + pill.w, chip.x + chip.w) - std::max(pill.x, chip.x);
                const float on = tween::clamp01(overlap / chip.w);
                const Color ink = gfx::mix(kInk.with_alpha(0.75f), kWhite, on);
                if (pass == 0)
                {
                    list.bordered_rect(chip, kChipH * 0.5f, kClear, 1.5f,
                                       kInk.with_alpha(0.25f * (1.0f - on)));
                }
                else
                {
                    ui::text(list, fonts.semibold, chip_name(i), chip.cx(), centred(cy, 20), 20,
                             ink, gfx::Align::center);
                }
            }
        }
        const Rect &last = chips[std::max(0, n - 1)];
        ui::draw_button(list, fonts, glyphs, ui::Button::r2, last.x + last.w + 16.0f, cy, 30);
        list.pop_transform();
        list.pop_opacity();
    }

    // The grid's window: from just under the chips to above the hint row.
    float window_top() const
    {
        return chips_y() + kChipH + 4.0f;
    }

    // How visible something at screen height y is inside the grid's window:
    // it fades in under the chips and out toward the hint row.
    float fade_at(float y) const
    {
        return std::min(tween::clamp01((y - window_top()) / kFadeHead),
                        tween::clamp01((kViewBottom - y) / kFadeFoot));
    }
    // A line of text: gone when its top reaches the window's top edge or its
    // foot the bottom one.
    float fade_text(float baseline, float size) const
    {
        return std::min(fade_at(baseline - size * 0.8f), fade_at(baseline + 4.0f));
    }
    // The same for a picture spanning y0..y1, as the opacity at its two ends.
    // An image's tint can only run linearly from top to bottom, so the line
    // is laid through "nothing at the window's edge": the far end may come
    // out below zero, but that part lies outside the clip and is never drawn.
    void fade_span(float y0, float y1, float *a0, float *a1) const
    {
        const float top = window_top();
        *a0 = *a1 = 1.0f;
        if (y0 < top + kFadeHead)
        {
            const float full = std::max(y1, top + kFadeHead);
            *a0 = (y0 - top) / (full - top);
            *a1 = (y1 - top) / (full - top);
        }
        else if (y1 > kViewBottom - kFadeFoot)
        {
            const float full = std::min(y0, kViewBottom - kFadeFoot);
            *a0 = (kViewBottom - y0) / (kViewBottom - full);
            *a1 = (kViewBottom - y1) / (kViewBottom - full);
        }
    }

    // One product card: cover, badges, title, price. The cover's tint runs
    // from the visibility at its top to the one at its bottom, which gives
    // the grid a per-pixel soft edge without painting over the backdrop.
    void draw_card(gfx::DrawList &list, int index, const Rect &r, float alpha,
                   unsigned layers) const
    {
        float top = 1.0f, foot = 1.0f;
        fade_span(r.y, r.y + r.h, &top, &foot);
        if (top <= 0.01f && foot <= 0.01f)
            return;
        top *= alpha;
        foot *= alpha;
        const ui::Fonts &fonts = context_.fonts;
        const float lift = (index >= 0 && index < static_cast<int>(lift_.size())) ? lift_[static_cast<std::size_t>(index)].value : 0.0f;
        const Rect cover{r.x, r.y, r.w, r.h};

        // While the page is open the cover has left its card: an empty slot.
        const bool travelling =
            page_.value > 0.01f && !page_from_banner_ && index == page_item_ && !calm();
        if (travelling && (layers & kShapes))
        {
            list.rounded_rect(cover, kCardRadius, kCoalLow.with_alpha(0.5f * top));
        }
        else if (!travelling && (layers & kImages))
        {
            const float light = 0.88f + 0.12f * lift; // resting covers sit back a little
            const std::uint32_t poster = item_cover(index);
            if (poster != 0)
                list.image_gradient(poster, cover, kCardCrop, Color{light, light, light, top},
                                    Color{light, light, light, foot}, kCardRadius);
            else
                list.rounded_rect(cover, kCardRadius, item(index).mid.with_alpha(top));
        }

        // Bottom cinematic shadow inside the card for readability
        if (!travelling && (layers & kShapes))
        {
            const Rect shade{cover.x, cover.y + cover.h * 0.45f, cover.w, cover.h * 0.55f};
            list.gradient_rect(shade, kCardRadius, kClear, kBlack.with_alpha(0.85f * foot));
        }

        const float marks = alpha * fade_at(r.y + 8.0f);
        if (marks > 0.01f && (layers & kShapes))
        {
            // Removed demo badges: TOP 1..3, 4K/HD, and Bookmark.
        }

        // Title text inside the card at bottom
        const float title_a = alpha * fade_text(r.y + r.h - 14.0f, 16);
        if (title_a > 0.01f && (layers & kSemibold))
        {
            std::string ct = item_title(index);
            ui::text(list, fonts.semibold, fonts.semibold.font->fit(ct.c_str(), 16, r.w - 56.0f),
                     r.x + 46.0f, r.y + r.h - 14.0f, 16, kWhite.with_alpha(title_a * (0.85f + 0.15f * lift)));
        }

        // Active card glow and red border
        if (lift > 0.01f && (layers & kShapes))
        {
            list.glow(cover, kCardRadius, 18.0f, kAccent.with_alpha(0.5f * lift * alpha));
            list.bordered_rect(cover.inset(-3.0f), kCardRadius + 3.0f, kClear, 3.0f,
                               kAccent.with_alpha(lift * alpha));
        }
    }

    void draw_grid(gfx::DrawList &list) const
    {
        const ui::Fonts &fonts = context_.fonts;
        const float scroll = scroll_.value;
        const Rect window{0, window_top(), kWidth, kViewBottom - window_top()};
        const int count = static_cast<int>(visible_.size());
        const int focused = zone_ == Zone::grid && count > 0 ? focus_ : -1;
        const bool swapping = swap_.running;
        const float swap_age = swapping ? swap_.elapsed - 0.1f : 10.0f;

        list.push_clip(window);

        // HDrezkaTV / www home rows: Фильмы / Сериалы / Мультфильмы
        if (home_layout_ && !home_sections_.empty())
        {
            for (int s = 0; s < static_cast<int>(home_sections_.size()); ++s)
            {
                const float ty =
                    kGridY + static_cast<float>(s) * kHomeRowPitch + 4.0f - scroll;
                if (ty > window_top() - 10.0f && ty < kViewBottom)
                {
                    const float a = fade_at(ty);
                    if (a > 0.01f)
                    {
                        ui::text(list, fonts.semibold,
                                 home_sections_[static_cast<std::size_t>(s)].title.c_str(),
                                 kMargin + 2.0f, ty, 24, kInk.with_alpha(a * 0.95f),
                                 gfx::Align::left, 1.0f);
                    }
                }
            }
        }

        if (swapping)
        {
            const float leave = 1.0f - tween::smoothstep(swap_.progress() * 3.0f);
            for (int k = 0; k < static_cast<int>(previous_visible_.size()); ++k)
            {
                Rect r = card_rect(k);
                r.y -= previous_scroll_;
                if (r.y < kViewBottom && r.y + r.h > window.y)
                    draw_card(list, previous_visible_[static_cast<std::size_t>(k)], r, leave,
                              kAllLayers);
            }
        }
        // Layer by layer across the cards
        for (const unsigned layer : {kImages, kShapes, kSemibold, kMono})
        {
            for (int k = 0; k < count; ++k)
            {
                if (k != focused)
                    draw_grid_card(list, k, scroll, swap_age, layer);
            }
        }
        if (count == 0)
        {
            const float in = tween::clamp01(swap_age / 0.3f) * tween::stagger(age_, 5, 0.07f, 0.5f);
            auto &app = rezka::App::instance();
            std::string status;
            {
                std::lock_guard<std::mutex> lock(app.catalog_mutex);
                status = app.catalog_status;
            }
            if (status.empty())
                status = app.catalog_loading ? "Загрузка каталога HDRezka..."
                                             : "Нет элементов в этом разделе";
            ui::text(list, fonts.semibold, status.c_str(), 960, 760, 30,
                     kInk.with_alpha(0.86f * in), gfx::Align::center);
        }
        list.pop_clip();

        // The focus highlight: signature red glowing border
        const float in = tween::stagger(age_, 4, 0.07f, 0.5f);
        Rect ring = ring_.value();
        const float shake = ui::shake(nudge_.value, clock_);
        ring.x += shake * nudge_x_;
        ring.y += shake * nudge_y_ - scroll;
        const float radius = ring_radius_.value;
        const float plate = plate_.value * in;
        if (plate > 0.01f)
        {
            list.shadow({ring.x, ring.y + 14.0f, ring.w, ring.h}, radius, 34,
                        kBlack.with_alpha(0.5f * plate));
            list.glow(ring, radius, 24, kAccent.with_alpha((0.35f + 0.15f * breathe()) * plate));
        }
        list.bordered_rect(ring, radius, kClear, 3.0f, kAccent.with_alpha(0.95f * in));
        if (focused >= 0)
        {
            list.push_clip(window);
            draw_grid_card(list, focused, scroll, swap_age, kAllLayers);
            list.pop_clip();
        }
    }

    void draw_grid_card(gfx::DrawList &list, int k, float scroll, float swap_age,
                        unsigned layers) const
    {
        Rect r = card_rect(k);
        r.y -= scroll;
        if (r.y > kViewBottom || r.y + r.h < kPageTop)
            return;
        const int index = visible_[static_cast<std::size_t>(k)];
        const float in =
            tween::stagger(age_ - 0.3f, k % kColumns + 2 * (k / kColumns), 0.05f, 0.45f) *
            tween::stagger(swap_age, k, 0.03f, 0.3f);
        const float lift = (index >= 0 && index < static_cast<int>(lift_.size()))
                               ? lift_[static_cast<std::size_t>(index)].value
                               : 0.0f;
        const float nudge =
            k == focus_ && zone_ == Zone::grid ? ui::shake(nudge_.value, clock_) : 0.0f;
        list.push_transform(1.0f + kLift * lift, r.cx(), r.cy(), nudge * nudge_x_,
                            nudge * nudge_y_ + 26.0f * (1.0f - in) * travel());
        draw_card(list, index, r, in, layers);
        list.pop_transform();
    }

    // ---- drawing: the product page -----------------------------------------

    void draw_page(gfx::DrawList &list, std::uint32_t glass) const
    {
        const float t = page_.value;
        if (t <= 0.004f)
            return;
        const ui::Fonts &fonts = context_.fonts;
        if (page_item_ < 0 || page_item_ >= static_cast<int>(offers_.size()))
            return;
        const std::uint32_t page_cover = item_cover(page_item_);
        const float veil = tween::clamp01(t * 1.7f);
        // Everything but the wall fades in once the page is well on its way.
        const float content = tween::smoothstep((t - 0.45f) / 0.55f);
        const PageLayout L = page_layout();
        const PageModel &m = page_model_;
        const bool row_focused = page_focus_ == PageFocus::genres || page_focus_ == PageFocus::lists ||
                                 page_focus_ == PageFocus::collections;

        // The wall: the home page, blurred and darkened. The cover fills the
        // right of the screen and fades into the charcoal on its left edge.
        const Rect full{0, 0, kWidth, kHeight};
        list.glass(glass, full, 0, kWhite.with_alpha(veil));
        list.rounded_rect(full, 0, kCoal.with_alpha(0.7f * veil));
        if (page_cover != 0)
        {
            const Rect art{700.0f, 0.0f, kWidth - 700.0f, kHeight};
            list.image(page_cover, art, cover_uv(art), kWhite.with_alpha(0.62f * content));
            list.gradient_rect_h({700.0f, 0.0f, 500.0f, kHeight}, 0.0f, kCoal.with_alpha(veil),
                                 kCoal.with_alpha(0.0f));
            list.gradient_rect({0.0f, 820.0f, kWidth, kHeight - 820.0f}, 0.0f, kCoal.with_alpha(0.0f),
                               kCoal.with_alpha(0.9f * veil));
        }

        // The clock, in its plate at the top right.
        char clock_time[16] = "--:--";
        char clock_date[48] = "";
        page_clock(clock_time, sizeof(clock_time), clock_date, sizeof(clock_date));
        list.push_opacity(content);
        list.rounded_rect(kClockBox, 20.0f, kClockBlue.with_alpha(0.82f));
        ui::text(list, fonts.semibold, clock_time, kClockBox.cx(), 66.0f, 46, kInk, gfx::Align::center);
        ui::text(list, fonts.regular, clock_date, kClockBox.cx(), 112.0f, 22, kInk.with_alpha(0.85f),
                 gfx::Align::center);
        list.pop_opacity();

        // The content scrolls under the clock.
        list.push_opacity(content);
        list.push_transform(1.0f, 0.0f, 0.0f, 0.0f, -page_scroll_.value);

        // The poster, as the catalogue holds it.
        if (page_cover != 0)
        {
            list.shadow({L.poster.x, L.poster.y + 12.0f, L.poster.w, L.poster.h}, 16.0f, 30.0f,
                        kBlack.with_alpha(0.5f));
            list.image(page_cover, L.poster, cover_uv(L.poster), kWhite.with_alpha(content), 16.0f);
        }

        // The buttons: Смотреть, Продолжить, the bookmark and the QR code.
        const bool bookmarked = rezka::App::instance().is_favorite(m.url);
        {
            const Rect b = L.watch;
            list.shadow({b.x, b.y + 10.0f, b.w, b.h}, b.h * 0.5f, 26.0f, kBlack.with_alpha(0.45f));
            list.gradient_rect(b, b.h * 0.5f, kPlayTop, kPlayBottom);
            list.triangle({b.x + 40.0f, b.cy() - 11.0f, 20.0f, 22.0f}, kInk, 0.0f, 1.5708f);
            ui::text(list, fonts.semibold, "Смотреть", b.x + 74.0f, centred(b.cy(), 26), 26, kInk);
        }
        if (L.has_resume)
        {
            const Rect b = L.resume;
            list.rounded_rect(b, b.h * 0.5f, kCoal.with_alpha(0.5f));
            list.bordered_rect(b, b.h * 0.5f, kClear, 2.0f, kInk.with_alpha(0.55f));
            list.triangle({b.x + 40.0f, b.cy() - 11.0f, 20.0f, 22.0f}, kInk, 0.0f, 1.5708f);
            ui::text(list, fonts.semibold, "Продолжить *", b.x + 74.0f, centred(b.cy(), 26), 26, kInk);
        }
        {
            const Rect b = L.bookmark;
            const float cx = b.cx();
            const float cy = b.cy();
            list.rounded_rect(b, b.h * 0.5f, bookmarked ? kPlayBottom.with_alpha(0.85f) : kCoal.with_alpha(0.5f));
            list.bordered_rect(b, b.h * 0.5f, kClear, 2.0f, kInk.with_alpha(0.55f));
            const float outline[] = {cx - 10.0f, cy - 13.0f, cx + 10.0f, cy - 13.0f, cx + 10.0f,
                                     cy + 13.0f, cx,         cy + 6.0f,  cx - 10.0f, cy + 13.0f};
            if (bookmarked)
                list.polygon(outline, 5, kInk);
            else
                for (int i = 0; i < 5; ++i)
                {
                    const int j = (i + 1) % 5;
                    list.line(outline[2 * i], outline[2 * i + 1], outline[2 * j], outline[2 * j + 1], 2.4f,
                              kInk);
                }
        }
        {
            const Rect b = L.qr;
            list.rounded_rect(b, b.h * 0.5f, kCoal.with_alpha(0.5f));
            list.bordered_rect(b, b.h * 0.5f, kClear, 2.0f, kInk.with_alpha(0.55f));
            const float x0 = b.cx() - 14.0f;
            const float y0 = b.cy() - 14.0f;
            list.bordered_rect({x0, y0, 11.0f, 11.0f}, 1.5f, kClear, 2.4f, kInk);
            list.bordered_rect({x0 + 17.0f, y0, 11.0f, 11.0f}, 1.5f, kClear, 2.4f, kInk);
            list.bordered_rect({x0, y0 + 17.0f, 11.0f, 11.0f}, 1.5f, kClear, 2.4f, kInk);
            list.rounded_rect({x0 + 17.0f, y0 + 17.0f, 4.0f, 4.0f}, 0.0f, kInk);
            list.rounded_rect({x0 + 24.0f, y0 + 24.0f, 4.0f, 4.0f}, 0.0f, kInk);
        }
        if (L.has_resume)
        {
            char hint[256];
            const int mm = static_cast<int>(m.resume.t) / 60;
            const int ss = static_cast<int>(m.resume.t) % 60;
            std::snprintf(hint, sizeof(hint), "* Продолжить смотреть %s с %d:%02d", m.resume_voice.c_str(), mm, ss);
            ui::text(list, fonts.regular, hint, L.resume.x, L.hint_y, 20, kInk.with_alpha(0.85f));
        }

        // The title, its original name, and the ratings and facts in a row.
        const std::string title = fonts.semibold.font->fit(m.title, 60, kPageRowW);
        ui::text(list, fonts.semibold, title, kPageX - 2.0f, L.title_y, 60, kInk);
        if (!m.original.empty())
            ui::text(list, fonts.regular, fonts.regular.font->fit(m.original, 28, kPageRowW), kPageX, L.original_y,
                     28, kInk.with_alpha(0.7f));
        {
            float x = kPageX;
            const float y = L.stats_y;
            const auto tile = [&](const std::string &value, const std::string &votes, const char *label,
                                  Color color) {
                const float value_w = fonts.semibold.measure(value, 34);
                ui::text(list, fonts.semibold, value, x, y + 40.0f, 34, color);
                float width = value_w;
                if (!votes.empty())
                {
                    const std::string shown = "(" + votes + ")";
                    ui::text(list, fonts.regular, shown, x + value_w + 10.0f, y + 40.0f, 20,
                             kInk.with_alpha(0.55f));
                    width += 10.0f + fonts.regular.measure(shown, 20);
                }
                const float label_w = fonts.regular.measure(label, 18);
                ui::text(list, fonts.regular, label, x, y + 68.0f, 18, kInk.with_alpha(0.6f));
                x += std::max(width, label_w) + 52.0f;
            };
            char number[24];
            if (m.kinopoisk.valid)
            {
                std::snprintf(number, sizeof(number), "%g", static_cast<double>(m.kinopoisk.value));
                tile(number, m.kinopoisk.votes, "КиноПоиск", kGood);
            }
            if (m.imdb.valid)
            {
                std::snprintf(number, sizeof(number), "%g", static_cast<double>(m.imdb.value));
                tile(number, m.imdb.votes, "IMDb", kGood);
            }
            if (m.hdrezka.valid)
            {
                std::snprintf(number, sizeof(number), "%g", static_cast<double>(m.hdrezka.value));
                tile(number, m.hdrezka.votes, "HDrezka", kGood);
            }
            if (!m.duration.empty())
                tile(m.duration, "", "Длительность", kInk);
            if (!m.age.empty())
                tile(m.age, "", "Возраст", kInk);
            if (!m.year.empty())
                tile(m.year, "", "Год выхода", kInk);
        }

        // The fact rows: slogan, release date and country, then the three
        // lists that open under their rows.
        for (const PageRow &row : L.rows)
        {
            if (row.kind == 0 || row.kind == 1)
            {
                const char *label = row.kind == 0 ? "Слоган:" : "Дата выхода:";
                const std::string value = row.kind == 0 ? "«" + m.slogan + "»" : m.release_date;
                const float w = ui::text(list, fonts.semibold, label, kPageX, row.base, 24, kInk);
                ui::text(list, fonts.regular, fonts.regular.font->fit(value, 24, kPageRowW - w - 14.0f),
                         kPageX + w + 14.0f, row.base, 24, kInk.with_alpha(0.9f));
            }
            else if (row.kind == 2)
            {
                float x = kPageX + ui::text(list, fonts.semibold, "Страна:", kPageX, row.base, 24, kInk) + 14.0f;
                for (const std::string &country : m.countries)
                {
                    const float w = fonts.regular.measure(country, 22) + 36.0f;
                    const Rect chip{x, row.base - 27.0f, w, 40.0f};
                    list.bordered_rect(chip, 12.0f, kClear, 2.0f, kInk.with_alpha(0.55f));
                    ui::text(list, fonts.regular, country, chip.cx(), centred(chip.cy(), 22), 22, kInk,
                             gfx::Align::center);
                    x += w + 10.0f;
                }
            }
            else
                draw_fact_row(list, fonts, m, row);
        }

        // The description, with the trailer beside it.
        ui::text(list, fonts.semibold, "Описание", kPageX, L.desc_y, 28, kInk);
        ui::paragraph(list, fonts.regular, m.description, kPageX, L.desc_y + 44.0f, 22, kPageTextW, 34.0f,
                      kInk.with_alpha(0.86f), 6);
        if (L.has_trailer)
        {
            ui::text(list, fonts.semibold, "Трейлер", kTrailerX, L.desc_y, 28, kInk);
            const Rect card = L.trailer;
            if (page_cover != 0)
                list.image(page_cover, card, cover_uv(card), kWhite.with_alpha(0.9f * content), 16.0f);
            else
                list.rounded_rect(card, 16.0f, kPanel.with_alpha(content));
            list.rounded_rect(card, 16.0f, kCoal.with_alpha(0.3f));
            list.circle(card.cx(), card.cy(), 38.0f, kInk.with_alpha(0.92f));
            list.triangle({card.cx() - 11.0f, card.cy() - 14.0f, 24.0f, 28.0f}, kCoal, 0.0f, 1.5708f);
        }

        // The cast: a row of cards that slides to keep the focused one in view.
        if (L.has_cast)
        {
            ui::text(list, fonts.semibold, "Актерский состав", kPageX, L.cast_y + 36.0f, 28, kInk);
            list.push_clip({kPageX - 18.0f, L.cast_y + 56.0f, kWidth - kPageX, kCastH + 90.0f});
            for (std::size_t i = 0; i < L.cast_cards.size() && i < m.cast.size(); ++i)
            {
                const Rect card = L.cast_cards[i];
                const rezka::Person &p = m.cast[i];
                const std::uint32_t photo = rezka::App::instance().photo_texture(p.photo);
                if (photo != 0)
                    list.image(photo, card, cover_uv(card), kWhite.with_alpha(content), 14.0f);
                else
                    list.rounded_rect(card, 14.0f, kPanel.with_alpha(content));
                ui::text(list, fonts.semibold, fonts.semibold.font->fit(p.name, 24, card.w),
                         card.x + 4.0f, card.y + card.h + 34.0f, 24, kInk);
                ui::text(list, fonts.regular, fonts.regular.font->fit(p.job, 20, card.w),
                         card.x + 4.0f, card.y + card.h + 62.0f, 20, kInk.with_alpha(0.6f));
            }
            list.pop_clip();
        }

        // The focus ring glides round a button or the trailer; a fact row
        // lights its own plate instead.
        if (!row_focused)
            list.bordered_rect(page_ring_.value(), page_ring_radius_.value, kClear, 3.0f, kInk.with_alpha(0.95f));
        list.pop_transform();
        list.pop_opacity();
    }

    // One fact row that can open: its label, its names in one line, and the
    // hint to open it. Under the row, an opened list.
    void draw_fact_row(gfx::DrawList &list, const ui::Fonts &fonts, const PageModel &m, const PageRow &row) const
    {
        const bool focused = page_focus_ == row_focus(row.kind);
        const char *label = row.kind == 3 ? "Жанр:" : row.kind == 4 ? "Входит в списки:" : "Из серии:";
        std::vector<std::string> items;
        if (row.kind == 3)
            items = m.genres;
        else if (row.kind == 4)
            for (const rezka::ListRef &ref : m.lists)
                items.push_back(ref.place.empty() ? ref.title : ref.title + " (" + ref.place + ")");
        else
            for (const rezka::ListRef &ref : m.collections)
                items.push_back(ref.title);
        std::string joined;
        for (std::size_t i = 0; i < items.size(); ++i)
        {
            if (i)
                joined += ", ";
            joined += items[i];
        }
        const std::size_t more_than = row.kind == 4 ? 2 : 3;
        const bool expandable = items.size() > more_than;
        const char *hint = !expandable ? nullptr : row.open ? "Свернуть" : "Нажмите, чтобы развернуть";

        if (focused)
            list.rounded_rect(row_plate(row), 14.0f, kInk.with_alpha(0.94f));
        const Color label_color = focused ? kFocusText : kInk;
        const Color value_color = focused ? kFocusText.with_alpha(0.85f) : kInk.with_alpha(0.9f);
        const float label_w = ui::text(list, fonts.semibold, label, kPageX, row.base, 24, label_color);
        const float value_x = kPageX + label_w + 14.0f;
        const float hint_w = hint ? fonts.regular.measure(hint, 22) + 16.0f : 0.0f;
        const std::string shown = fonts.regular.font->fit(joined, 24, kPageRowW - 36.0f - label_w - 14.0f - hint_w);
        const float shown_w = ui::text(list, fonts.regular, shown, value_x, row.base, 24, value_color);
        if (hint)
            ui::text(list, fonts.regular, hint, value_x + shown_w + 16.0f, row.base, 22,
                     focused ? kFocusHint : kLink);

        if (row.open)
        {
            list.rounded_rect({kPageX - 18.0f, row.base + 14.0f, kPageRowW, 10.0f + items.size() * kPageLinePitch},
                              16.0f, kInk.with_alpha(0.07f));
            for (std::size_t i = 0; i < items.size(); ++i)
                ui::text(list, fonts.regular, fonts.regular.font->fit(items[i], 22, kPageRowW - 60.0f),
                         kPageX + 12.0f, row.base + 40.0f + static_cast<float>(i) * kPageLinePitch, 22,
                         kInk.with_alpha(0.88f));
        }
    }

    // The QR code of the open address, over the page.
    void draw_qr(gfx::DrawList &list) const
    {
        const float t = qr_.value;
        if (t <= 0.01f)
            return;
        const ui::Fonts &fonts = context_.fonts;
        list.push_opacity(tween::clamp01(t * 1.4f));
        list.rounded_rect({0.0f, 0.0f, kWidth, kHeight}, 0.0f, kCoal.with_alpha(0.74f));
        const Rect card{kWidth * 0.5f - 300.0f, 140.0f, 600.0f, 680.0f};
        list.shadow({card.x, card.y + 24.0f, card.w, card.h}, 36.0f, 50.0f, kBlack.with_alpha(0.5f));
        list.rounded_rect(card, 36.0f, kInk);

        // The modules, with the quiet zone of four modules round them.
        const int n = qr_code_.size;
        const float side = 440.0f;
        const float module = side / static_cast<float>(n + 8);
        const float ax = card.cx() - side * 0.5f;
        const float ay = card.y + 56.0f;
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x)
                if (qr_code_.at(x, y))
                    list.rounded_rect({ax + (x + 4) * module, ay + (y + 4) * module, module + 0.6f, module + 0.6f},
                                      0.0f, kCoal);

        const float text_y = ay + side + 56.0f;
        ui::text(list, fonts.semibold, qr_caption_, card.cx(), text_y, 30, kCoal, gfx::Align::center);
        ui::text(list, fonts.regular, fonts.regular.font->fit(qr_url_, 19, card.w - 60.0f), card.cx(), text_y + 40.0f,
                 19, kCoal.with_alpha(0.6f), gfx::Align::center);
        ui::text(list, fonts.regular, "Наведите камеру телефона на код", card.cx(), text_y + 84.0f, 22,
                 kCoal.with_alpha(0.8f), gfx::Align::center);
        const ui::Hint hints[] = {{ui::Button::circle, "Назад"}};
        ui::draw_hints(list, fonts, ui::GlyphStyle::dark(), hints, 1, kRight, true);
        list.pop_opacity();
    }

    // ---- drawing: the cart -------------------------------------------------

    void draw_cart_counter(gfx::DrawList &list) const
    {
        const ui::Fonts &fonts = context_.fonts;
        const float in = tween::stagger(age_, 0, 0.07f, 0.5f);
        char text[8];
        list.push_opacity(in);
        // The bump: a kicked, underdamped spring, so it overshoots and rings.
        list.push_transform(1.0f + bump_.value, kCartX, kCartY, 0, 0);
        draw_cart(list, kCartX, kCartY, 30.0f, kInk.with_alpha(0.92f));
        const float badge = badge_.value;
        if (badge > 0.01f)
        {
            const float bx = kCartX + 20.0f;
            const float by = kCartY - 18.0f;
            list.circle(bx, by, 15.0f * badge, kAccent);
            // The number rolls: the old one leaves upward as the new one
            // comes up from below. Mono digits keep the roll in place.
            const float roll =
                count_roll_.running ? tween::cubic_out(count_roll_.progress()) : 1.0f;
            const float base = centred(by, 18);
            if (roll < 1.0f && count_previous_ > 0)
            {
                std::snprintf(text, sizeof(text), "%d", count_previous_);
                ui::text(list, fonts.mono, text, bx, base - 9.0f * roll, 18,
                         kOnAccent.with_alpha((1.0f - roll) * badge), gfx::Align::center);
            }
            if (count_shown_ > 0)
            {
                std::snprintf(text, sizeof(text), "%d", count_shown_);
                ui::text(list, fonts.mono, text, bx, base + 9.0f * (1.0f - roll), 18,
                         kOnAccent.with_alpha(roll * badge), gfx::Align::center);
            }
        }
        list.pop_transform();
        list.pop_opacity();
    }

    // The small copy of the cover on its way to the cart: a quadratic curve
    // that rises first, shrinking as it goes, with two fainter echoes behind.
    void draw_flight(gfx::DrawList &list) const
    {
        if (!flight_active_)
            return;
        const demo::Item &it = item(flight_item_);
        const float t = flight_.progress();
        if (calm())
        {
            // No flight: the copy just fades where it stands.
            const Rect still = scaled(kFlightFrom, 0.3f);
            const std::uint32_t flight_cover = item_cover(flight_item_);
            if (flight_cover != 0)
                list.image(flight_cover, still, kCoverUv, kWhite.with_alpha(1.0f - t), 16);
            return;
        }
        const float from_x = kFlightFrom.cx(), from_y = kFlightFrom.cy();
        const float bend_x = tween::lerp(from_x, kCartX, 0.45f), bend_y = kCartY - 30.0f;
        for (int echo = 3; echo >= 0; --echo)
        {
            const float u =
                tween::cubic_in_out(tween::clamp01(t - 0.014f * static_cast<float>(echo)));
            const float v = 1.0f - u;
            const float x = v * v * from_x + 2.0f * v * u * bend_x + u * u * kCartX;
            const float y = v * v * from_y + 2.0f * v * u * bend_y + u * u * kCartY;
            const float size = tween::lerp(190.0f, 26.0f, u);
            const float alpha = tween::clamp01(t * 10.0f) * tween::clamp01((1.0f - u) * 9.0f) *
                                (echo == 0 ? 1.0f : 0.26f / static_cast<float>(echo));
            const Rect r{x - size * 0.5f, y - size * 0.5f, size, size};
            if (echo == 0)
            {
                list.shadow({r.x, r.y + size * 0.1f, r.w, r.h}, size * 0.16f, size * 0.3f,
                            kBlack.with_alpha(0.5f * alpha));
                list.glow(r, size * 0.16f, size * 0.25f, it.accent.with_alpha(0.3f * alpha));
            }
            const std::uint32_t flight_cover = item_cover(flight_item_);
            if (flight_cover != 0)
                list.image(flight_cover, r, kCoverUv, kWhite.with_alpha(alpha), size * 0.16f);
        }
    }

    // One cart row centred on cy: cover, title, genre and the price at the
    // right edge. A removed row closes like a drawer; a bought one also
    // slides away to the right, toward the library.
    void draw_line(gfx::DrawList &list, const Line &line, float x0, float cy, unsigned layers) const
    {
        const ui::Fonts &fonts = context_.fonts;
        const demo::Item &it = item(line.item);
        const Offer &o = offer(line.item);
        const float open = line.open.value;
        const float slide = line.sold ? 90.0f * (1.0f - open) * travel() : 0.0f;
        const float x = x0 + 44.0f + slide;
        const float right = x0 + kDrawerW - 44.0f + slide;
        list.push_opacity(tween::clamp01(open * 1.4f - 0.4f));
        if (layers & kImages)
        {
            const std::uint32_t row_cover = item_cover(line.item);
            if (row_cover != 0)
                list.image(row_cover, {x, cy - 38.0f, 76, 76}, kCoverUv, kWhite, 12);
        }
        if ((layers & kSemibold) && line.item >= 0 &&
            line.item < static_cast<int>(line_titles_.size()))
            ui::text(list, fonts.semibold, line_titles_[static_cast<std::size_t>(line.item)],
                     x + 96.0f, cy - 4.0f, 24, kInk);
        if (layers & kRegular)
            ui::text(list, fonts.regular, item_meta(line.item).c_str(), x + 96.0f, cy + 26.0f, 20,
                     kInk.with_alpha(0.58f));
        if (o.free())
        {
            if (layers & kSemibold)
                ui::text(list, fonts.semibold, "Free", right, cy + 8.0f, 24, kInk,
                         gfx::Align::right);
        }
        else if (o.percent > 0)
        {
            amount(list, o.price(), right, cy - 2.0f, 24, kAccent, true, layers);
            struck(list, o.base, right, cy + 26.0f, 17, kInk.with_alpha(0.5f), true, layers);
        }
        else
        {
            amount(list, o.price(), right, cy + 8.0f, 24, kInk, true, layers);
        }
        list.pop_opacity();
    }

    void draw_drawer(gfx::DrawList &list, std::uint32_t glass) const
    {
        const float t = drawer_.value;
        if (t <= 0.004f)
            return;
        const ui::Fonts &fonts = context_.fonts;
        char text[48];
        const float x0 = kWidth - kDrawerW * (calm() ? 1.0f : t);
        const Rect panel{x0, 0, kDrawerW + 80.0f, kHeight};
        const float right = x0 + kDrawerW - 44.0f;

        list.rounded_rect({0, 0, kWidth, kHeight}, 0, kBlack.with_alpha(0.5f * t));
        list.push_opacity(tween::clamp01(t * 1.5f));
        list.shadow({panel.x - 10.0f, panel.y, panel.w, panel.h}, 0, 70, kBlack.with_alpha(0.55f));
        list.glass(glass, panel, 0, kWhite);
        list.rounded_rect(panel, 0, gfx::mix(kCoal, kPanel, 0.6f).with_alpha(0.74f));
        list.rounded_rect({x0, 0, 1.5f, kHeight}, 0, kInk.with_alpha(0.22f));

        const int live = live_lines();
        ui::text(list, fonts.display, "Your cart", x0 + 44.0f, 130, 42, kInk);
        if (live == 1)
            std::snprintf(text, sizeof(text), "One title");
        else if (live == 0)
            std::snprintf(text, sizeof(text), "Empty");
        else
            std::snprintf(text, sizeof(text), "%d titles", live);
        ui::text(list, fonts.regular, text, x0 + 46.0f, 168, 22, kInk.with_alpha(0.6f));

        // The focus highlight, under the rows it lights.
        Rect ring = drawer_ring_.value();
        ring.x += x0 - (kWidth - kDrawerW) + ui::shake(drawer_nudge_.value, clock_, 10.0f);
        const bool on_button = drawer_focus_ >= live;
        if (!on_button)
            list.rounded_rect(ring, 18, kInk.with_alpha(0.08f));

        // The rows, layer by layer like the grid's cards (see Layer).
        list.push_clip({x0, kLinesTop, kDrawerW, kLinesView});
        for (const unsigned layer : {kImages, kShapes, kSemibold, kRegular, kMono})
        {
            float y = kLinesTop - drawer_scroll_.offset();
            for (const Line &line : lines_)
            {
                const float height = kLineH * line.open.value;
                if (y + height > kLinesTop && y < kLinesTop + kLinesView && line.open.value > 0.02f)
                    draw_line(list, line, x0, y + height * 0.5f, layer);
                y += height;
            }
        }
        list.pop_clip();
        if (!on_button)
            list.bordered_rect(ring, 18, kClear, 2.5f, kInk.with_alpha(0.9f));

        const float empty = empty_.value;
        if (empty > 0.01f)
        {
            const float cx = x0 + kDrawerW * 0.5f;
            const float rise = 16.0f * (1.0f - empty) * travel();
            draw_cart(list, cx, 420 + rise, 64, kInk.with_alpha(0.3f * empty));
            ui::text(list, fonts.semibold, "Your cart is empty", cx, 520 + rise, 28,
                     kInk.with_alpha(0.9f * empty), gfx::Align::center);
            ui::text(list, fonts.regular, "Titles you add will wait here.", cx, 560 + rise, 24,
                     kInk.with_alpha(0.6f * empty), gfx::Align::center);
        }

        // The total counts toward its new value instead of jumping to it.
        list.rounded_rect({x0 + 44.0f, 820, kDrawerW - 88.0f, 1.5f}, 0, kInk.with_alpha(0.14f));
        ui::text(list, fonts.semibold, "SUBTOTAL", x0 + 44.0f, 872, 17, kInk.with_alpha(0.6f),
                 gfx::Align::left, 3.5f);
        amount(list, subtotal_shown(), right, 878, 40, kInk, true);

        Rect button = checkout_rect();
        button.x += x0 - (kWidth - kDrawerW);
        if (on_button)
            button.x += ui::shake(drawer_nudge_.value, clock_, 10.0f);
        if (live > 0)
        {
            if (on_button)
                list.glow(button, kButtonRadius, 22, kAccent.with_alpha(0.22f + 0.1f * breathe()));
            list.rounded_rect(button, kButtonRadius, kAccent);
            ui::text(list, fonts.semibold, "Checkout", button.cx(), centred(button.cy(), 28), 28,
                     kOnAccent, gfx::Align::center);
        }
        else
        {
            list.rounded_rect(button, kButtonRadius, kInk.with_alpha(0.08f));
            ui::text(list, fonts.semibold, "Checkout", button.cx(), centred(button.cy(), 28), 28,
                     kInk.with_alpha(0.4f), gfx::Align::center);
        }
        if (on_button)
            list.bordered_rect(button.inset(-6.0f), kButtonRadius + 6.0f, kClear, 3.0f,
                               kInk.with_alpha(0.95f));
        list.pop_opacity();
    }

    void draw_dialog(gfx::DrawList &list, std::uint32_t glass) const
    {
        const float t = dialog_.value;
        if (t <= 0.004f)
            return;
        const ui::Fonts &fonts = context_.fonts;
        list.rounded_rect({0, 0, kWidth, kHeight}, 0, kBlack.with_alpha(0.65f * t));

        auto &app = rezka::App::instance();
        std::string title_str;
        std::vector<std::string> translators;
        std::vector<std::string> seasons;
        std::vector<std::string> episodes;
        std::vector<std::string> qualities;
        int sel_tr = translator_idx_;
        int sel_se = season_idx_;
        int sel_ep = episode_idx_;
        int sel_q = quality_idx_;

        {
            std::lock_guard<std::mutex> lock(app.catalog_mutex);
            if (page_item_ >= 0 && page_item_ < static_cast<int>(app.movies.size())) {
                const auto &m = app.movies[page_item_];
                title_str = m.title;
                for (const auto &tr : m.details.translators)
                    translators.push_back(tr.name);
                if (m.details.is_series) {
                    for (const auto &s : m.details.seasons)
                        seasons.push_back(s.title);
                    if (!m.details.seasons.empty()) {
                        const int s_idx = std::clamp(season_idx_, 0, static_cast<int>(m.details.seasons.size()) - 1);
                        for (const auto &e : m.details.seasons[s_idx].episodes)
                            episodes.push_back(e.title);
                    }
                }
                if (!m.streams.empty()) {
                    for (const auto &s : m.streams)
                        qualities.push_back(s.quality);
                } else {
                    qualities = {"1080p Ultra", "1080p", "720p", "480p", "360p"};
                }
            }
        }
        if (translators.empty())
            translators.push_back("HDRezka Studio");

        const int num_tr = static_cast<int>(translators.size());
        const int num_se = static_cast<int>(seasons.size());
        const int num_ep = static_cast<int>(episodes.size());
        const int num_q = static_cast<int>(qualities.size());
        const int tr_slots = std::max(1, num_tr);
        sel_tr = std::clamp(sel_tr, 0, tr_slots - 1);
        if (num_se > 0)
            sel_se = std::clamp(sel_se, 0, num_se - 1);
        if (num_ep > 0)
            sel_ep = std::clamp(sel_ep, 0, num_ep - 1);
        sel_q = std::clamp(sel_q, 0, std::max(0, num_q - 1));

        const float dw = 920.0f;
        const float dh = 720.0f;
        const Rect d{(kWidth - dw) * 0.5f, (kHeight - dh) * 0.5f, dw, dh};
        const float list_top = d.y + 118.0f;
        const float list_view = dh - 118.0f - 44.0f;
        const float scroll = dialog_scroll_.offset();

        list.push_opacity(tween::clamp01(t * 1.4f));
        list.push_transform(calm() ? 1.0f : 0.94f + 0.06f * t, d.cx(), d.cy(), 0, 0);
        list.shadow({d.x, d.y + 24.0f, d.w, d.h}, 30, 60, kBlack.with_alpha(0.6f));
        list.glass(glass, d, 30, kWhite);
        list.rounded_rect(d, 30, gfx::mix(kCoal, kPanel, 0.75f).with_alpha(0.92f));
        list.bordered_rect(d, 30, kClear, 2.0f, kAccent.with_alpha(0.8f));

        const char *step_header = (dialog_step_ == 0) ? "ШАГ 1: ВЫБОР ОЗВУЧКИ" : "ШАГ 2: ВЫБОР КАЧЕСТВА";
        ui::text(list, fonts.semibold, step_header, d.x + 48.0f, d.y + 44.0f, 16, kAccent, gfx::Align::left, 3.5f);
        const std::string title_fit = fonts.display.font->fit(title_str.c_str(), 30, d.w - 96.0f);
        ui::text(list, fonts.display, title_fit, d.x + 48.0f, d.y + 82.0f, 30, kInk);
        list.rounded_rect({d.x + 48.0f, d.y + 100.0f, d.w - 96.0f, 1.5f}, 0, kInk.with_alpha(0.12f));

        auto draw_section = [&](float &y, const char *label) {
            ui::text(list, fonts.semibold, label, d.x + 48.0f, y + 4.0f, 15, kInk.with_alpha(0.55f),
                     gfx::Align::left, 2.5f);
            y += kDlgSectionH;
        };

        auto draw_pick_row = [&](float y, int focus_id, bool selected, const std::string &label) {
            const Rect rr{d.x + 48.0f, y, d.w - 96.0f, kDlgItemH};
            if (rr.y + rr.h < list_top - 4.0f || rr.y > list_top + list_view + 4.0f)
                return;
            const bool focused = (dialog_choice_ == focus_id);
            if (focused) {
                list.glow(rr, 12.0f, 18.0f, kAccent.with_alpha(0.35f));
                list.rounded_rect(rr, 12.0f, kAccent);
            } else if (selected) {
                list.rounded_rect(rr, 12.0f, kAccent.with_alpha(0.22f));
                list.bordered_rect(rr, 12.0f, kClear, 1.5f, kAccent.with_alpha(0.85f));
            } else {
                list.rounded_rect(rr, 12.0f, kCoal.with_alpha(0.45f));
                list.bordered_rect(rr, 12.0f, kClear, 1.0f, kInk.with_alpha(0.12f));
            }
            const Color ink = focused ? kOnAccent : kInk;
            const float mx = rr.x + 28.0f;
            const float my = rr.cy();
            if (selected) {
                list.circle(mx, my, 8.0f, ink);
            } else {
                list.ring(mx, my, 8.0f, 2.0f, ink.with_alpha(0.55f));
            }
            const std::string fit = fonts.semibold.font->fit(label.c_str(), 22, rr.w - 64.0f);
            ui::text(list, fonts.semibold, fit, rr.x + 48.0f, centred(rr.cy(), 22), 22, ink,
                     gfx::Align::left);
        };

        auto draw_action = [&](float y, int focus_id, float height, const char *label, bool primary) {
            const Rect rr{d.x + 48.0f, y, d.w - 96.0f, height};
            if (rr.y + rr.h < list_top - 4.0f || rr.y > list_top + list_view + 4.0f)
                return;
            const bool focused = (dialog_choice_ == focus_id);
            if (primary) {
                if (focused)
                    list.glow(rr, 14.0f, 22.0f, kAccent.with_alpha(0.45f));
                list.rounded_rect(rr, 14.0f, focused ? kAccent : kAccent.with_alpha(0.85f));
                ui::text(list, fonts.semibold, label, rr.cx(), centred(rr.cy(), 24), 24, kOnAccent, gfx::Align::center);
            } else {
                list.rounded_rect(rr, 12.0f, focused ? kPanel : kClear);
                list.bordered_rect(rr, 12.0f, kClear, focused ? 1.5f : 1.0f,
                                   focused ? kAccent : kInk.with_alpha(0.2f));
                ui::text(list, fonts.semibold, label, rr.cx(), centred(rr.cy(), 20), 20,
                         focused ? kInk : kInk.with_alpha(0.7f), gfx::Align::center);
            }
        };

        list.push_clip({d.x + 24.0f, list_top, d.w - 48.0f, list_view});
        float y = list_top - scroll;

        if (dialog_step_ == 0) {
            draw_section(y, "ОЗВУЧКА / ПЕРЕВОД");
            for (int i = 0; i < tr_slots; ++i) {
                draw_pick_row(y, i, i == sel_tr, translators[static_cast<size_t>(i)]);
                y += kDlgItemH + kDlgItemGap;
            }
            if (num_se > 0) {
                draw_section(y, "СЕЗОН");
                for (int i = 0; i < num_se; ++i) {
                    draw_pick_row(y, tr_slots + i, i == sel_se, seasons[static_cast<size_t>(i)]);
                    y += kDlgItemH + kDlgItemGap;
                }
            }
            if (num_ep > 0) {
                draw_section(y, "СЕРИЯ");
                for (int i = 0; i < num_ep; ++i) {
                    draw_pick_row(y, tr_slots + num_se + i, i == sel_ep, episodes[static_cast<size_t>(i)]);
                    y += kDlgItemH + kDlgItemGap;
                }
            }
            y += 6.0f;
            draw_action(y, tr_slots + num_se + num_ep, kDlgBtnH, "Далее: выбор качества", true);
        } else {
            draw_section(y, "КАЧЕСТВО ВИДЕО");
            for (int i = 0; i < num_q; ++i) {
                draw_pick_row(y, i, i == sel_q, qualities[static_cast<size_t>(i)]);
                y += kDlgItemH + kDlgItemGap;
            }
            y += 6.0f;
            draw_action(y, num_q, kDlgBtnH, "Начать просмотр", true);
            y += kDlgBtnH + 10.0f;
            draw_action(y, num_q + 1, 46.0f, "Назад к выбору озвучки", false);
        }
        list.pop_clip();

        {
            const ui::GlyphStyle glyphs = ui::GlyphStyle::dark();
            const ui::Hint hints[] = {
                {ui::Button::dpad, "выбор"},
                {ui::Button::cross, "подтвердить"},
                {ui::Button::circle, "назад"},
            };
            ui::HintLayout hint_layout;
            hint_layout.size = 26.0f;
            hint_layout.text_size = 18.0f;
            hint_layout.cy = d.y + d.h - 28.0f;
            hint_layout.item_gap = 28.0f;
            const float hw = ui::measure_hints(fonts, hints, 3, hint_layout);
            ui::draw_hints(list, fonts, glyphs, hints, 3, d.cx() + hw * 0.5f, true, hint_layout);
        }

        list.pop_transform();
        list.pop_opacity();
    }

    // "Purchase complete": a frosted pill that drops in under the top edge,
    // with a check mark that draws itself.
    void draw_toast(gfx::DrawList &list, std::uint32_t glass) const
    {
        const float t = toast_.value;
        if (t <= 0.01f)
            return;
        const ui::Fonts &fonts = context_.fonts;
        char text[64];
        if (toast_count_ == 1)
            std::snprintf(text, sizeof(text), "One title added to your library");
        else
            std::snprintf(text, sizeof(text), "%d titles added to your library", toast_count_);
        const float w = 600.0f;
        const Rect pill{960.0f - w * 0.5f, 34.0f - (calm() ? 0.0f : 110.0f * (1.0f - t)), w, 84.0f};
        list.push_opacity(tween::clamp01(t * 1.3f));
        list.shadow({pill.x, pill.y + 14.0f, pill.w, pill.h}, 42, 36, kBlack.with_alpha(0.5f));
        list.glass(glass, pill, 42, kWhite);
        list.rounded_rect(pill, 42, gfx::mix(kCoal, kPanel, 0.7f).with_alpha(0.8f));
        list.bordered_rect(pill, 42, kClear, 1.5f, kOwned.with_alpha(0.5f));
        const float cx = pill.x + 46.0f;
        const float cy = pill.cy();
        list.circle(cx, cy, 22, kOwned);
        const float stroke = tween::clamp01(toast_age_ / 0.3f);
        const float first = tween::clamp01(stroke / 0.4f);
        const float second = tween::clamp01((stroke - 0.4f) / 0.6f);
        list.line(cx - 9.5f, cy + 0.5f, cx - 9.5f + 6.5f * first, cy + 0.5f + 6.5f * first, 3.6f,
                  kCoal);
        if (second > 0.0f)
            list.line(cx - 3.0f, cy + 7.0f, cx - 3.0f + 12.5f * second, cy + 7.0f - 13.5f * second,
                      3.6f, kCoal);
        if (toast_text_.empty())
        {
            ui::text(list, fonts.semibold, "Purchase complete", pill.x + 86.0f, cy - 4.0f, 26, kInk);
            ui::text(list, fonts.regular, text, pill.x + 86.0f, cy + 24.0f, 20, kInk.with_alpha(0.66f));
        }
        else
            ui::text(list, fonts.semibold, toast_text_, pill.x + 86.0f, cy + 8.0f, 26, kInk);
        list.pop_opacity();
    }

    // The hint row of one layer: 0 home, 1 product page, 2 cart, 3 dialog.
    void draw_hints(gfx::DrawList &list, int layer, float alpha) const
    {
        if (alpha <= 0.01f)
            return;
        const ui::Fonts &fonts = context_.fonts;
        const ui::GlyphStyle style = ui::GlyphStyle::dark();
        list.push_opacity(alpha);
        if (layer == 0)
        {
            const ui::Hint hints[] = {
                {ui::Button::cross, "Выбрать"},
                {ui::Button::circle, "Назад"},
                {ui::Button::triangle, "Поиск"},
                {ui::Button::options, "Профиль"},
                {ui::Button::l1, "Категории", ui::Button::r1},
                {ui::Button::l2, "Разделы", ui::Button::r2}
            };
            ui::draw_hints(list, fonts, style, hints, 6, kRight, true);

            // Left side status: DualSense indicator
            list.circle(kMargin + 8.0f, 1010.0f, 5.0f, Color::rgb(0x30d158));
            ui::text(list, fonts.regular, "DualSense подключен", kMargin + 22.0f, centred(1010.0f, 18), 18, kInk.with_alpha(0.65f));
        }
        else if (layer == 1)
        {
            const ui::Hint hints[] = {
                {ui::Button::cross, "Смотреть"},
                {ui::Button::square, "В закладки"},
                {ui::Button::triangle, "Поиск"},
                {ui::Button::circle, "Назад"}
            };
            ui::draw_hints(list, fonts, style, hints, 4, kRight, true);
        }
        else if (layer == 3)
        {
            const ui::Hint hints[] = {
                {ui::Button::cross, "Подтвердить"},
                {ui::Button::circle, "Назад"}
            };
            ui::draw_hints(list, fonts, style, hints, 2, kRight, true);
        }
        else
        {
            const ui::Hint hints[] = {{ui::Button::cross, "Выбрать"},
                                      {ui::Button::circle, "Назад"}};
            ui::draw_hints(list, fonts, style, hints, 2, kRight, true);
        }
        list.pop_opacity();
    }

    // ---- state -------------------------------------------------------------

    app::Context &context_;
    std::vector<Offer> offers_; // per catalogue entry
    std::vector<bool> owned_;
    std::vector<bool> wished_;
    std::vector<std::string> card_titles_; // fitted to a card once
    std::vector<std::string> line_titles_; // ... and to a cart row
    std::vector<Copy> copy_;               // how each title's text wraps on its page
    std::vector<int> featured_;            // catalogue indices the banner shows
    int newest_year_ = 0;
    float age_ = 0.0f;   // seconds since enter(): drives the entrance
    float clock_ = 0.0f; // free-running: idle glows and shakes
    uint32_t last_catalog_version_ = 0;

    // The home page.
    Zone zone_ = Zone::banner;
    int top_tab_ = 0; // Главная / Фильмы / Сериалы / Мультфильмы / Аниме
    int filter_ = 1;  // home: Последние; catalog: Популярные
    bool home_layout_ = true;
    std::vector<rezka::HomeSection> home_sections_;
    float buffer_clock_ = 0.0f;
    int counts_[kFilters] = {};
    std::vector<int> visible_;          // what the grid shows, in order
    std::vector<int> previous_visible_; // ... and what it showed before the filter changed
    float previous_scroll_ = 0.0f;
    tween::Timer swap_;
    int focus_ = 0; // index into visible_
    tween::Spring scroll_;
    ui::SpringRect ring_; // the focus highlight, in page coordinates
    tween::Spring ring_radius_;
    tween::Spring plate_; // 1 while the highlight is a card's plate
    ui::SpringColor glow_;
    std::vector<tween::Spring> lift_; // per catalogue entry: 1 when its card is focused
    ui::SpringRect chip_pill_;
    ui::Pulse nudge_;
    float nudge_x_ = 0.0f;
    float nudge_y_ = 0.0f;
    ui::Pulse chip_nudge_;
    float chip_nudge_x_ = 0.0f;
    ui::SpringColor palette_[3];
    ui::SpringColor tone_; // the banner's plate

    // The banner.
    int banner_ = 0; // index into featured_
    int banner_previous_ = 0;
    float banner_direction_ = 1.0f;
    float banner_clock_ = 0.0f; // seconds this banner has been up
    tween::Timer banner_fade_;
    float drift_ = 0.0f;          // seconds of artwork drift, current banner
    float drift_previous_ = 0.0f; // ... and the one fading out
    tween::Spring banner_focus_;

    // The home page (HDrezkaTV's MovieList screen).
    std::vector<TvCard> tv_cards_;
    std::string tv_status_;       // what the catalogue says when it has no cards
    TvZone tv_zone_ = TvZone::tabs;
    TvZone tv_return_ = TvZone::tabs; // where the focus was when the drawer opened
    int tv_kind_ = 0;             // 0 the home tabs, 1 Новинки, 2 Анонсы
    int tv_tab_ = 0;              // Смотрят сейчас, as the app starts
    int tv_type_ = 0;             // the selected type chip
    int tv_chip_focus_ = 0;
    int tv_focus_ = 0;            // the focused card
    int tv_drawer_focus_ = kTvHomeItem;
    float tv_tab_delay_ = -1.0f;  // seconds until the focused tab's list is asked for
    tween::Spring tv_open_;       // the drawer
    tween::Spring tv_scroll_;     // the grid
    tween::Spring tv_clock_;
    tween::Spring tv_tabs_lit_;   // the tab row has the focus
    ui::SpringRect tv_pill_;
    bool tv_pill_set_ = false;
    std::uint32_t tv_icons_ = 0;
    bool tv_icons_tried_ = false;

    std::string tv_list_title_;   // heads a list opened from a title's page
    // The home banner (App::hero_slides, measured once per change).
    std::vector<TvHero> tv_hero_;
    int tv_hero_state_ = 0;         // App::hero_state when last looked at
    std::uint32_t tv_hero_version_ = 0;
    std::uint32_t bookmark_error_version_ = 0;
    bool dp_bookmarks_asked_ = false; // the folders dialog asked for the account's folders
    int tv_hero_index_ = 0;         // the slide shown
    int tv_hero_prev_ = -1;         // ... and the one fading out under it
    float tv_hero_fade_ = 1.0f;     // 0 to 1 over the crossfade
    float tv_hero_timer_ = 0.0f;    // seconds the slide has been left alone
    int tv_hero_focus_ = 0;         // 0 Смотреть, 1 Мой список
    float tv_hero_lift_[2] = {0.0f, 0.0f};
    std::uint32_t tv_youtube_ = 0;
    // The profile
    std::uint32_t tv_ill_[5] = {0, 0, 0, 0, 0}; // tv/ill-*.png, by ProfileIll
    float tv_ill_aspect_[5] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    qr::Code profile_qr_;
    int profile_view_ = 0;
    int profile_focus_ = 0;
    int profile_list_focus_ = 0;
    bool profile_was_logged_ = false;
    // The search
    int search_zone_ = 0;
    int search_row_ = 0;
    bool search_was_open_ = false;
    std::string search_suggested_;   // the query the suggestions are for
    float search_debounce_ = 0.0f;
    bool search_kb_pending_ = false; // the keyboard was opened by the search
    std::string search_kb_before_;
    float search_voice_timer_ = 0.0f;

    // A title's page (HDrezkaTV's MovieDetails screen).
    int dp_row_ = 1;
    int dp_col_ = 0;
    std::vector<int> dp_cols_;    // the item each row was left on
    float dp_lift_ = 1.0f;        // how far the focus has arrived
    tween::Spring dp_scroll_;
    std::vector<tween::Spring> dp_row_x_;
    std::vector<int> dp_stack_;   // the titles under this one
    std::string dp_key_;          // what the wrapped texts were made from
    std::vector<std::string> dp_title_lines_;
    std::vector<std::string> dp_desc_lines_;
    bool dp_desc_more_ = false;
    std::vector<std::string> dp_list_names_;
    std::vector<std::string> dp_collection_names_;
    std::vector<std::string> dp_cast_names_;    // two lines a person
    std::vector<std::string> dp_related_lines_; // four lines a title
    DpDialog dp_dialog_ = DpDialog::none;
    tween::Spring dp_dialog_in_;
    tween::Spring dp_dialog_scroll_;
    mutable float dp_dialog_content_ = 0.0f; // measured as the dialog is drawn
    mutable float dp_dialog_view_ = 0.0f;
    int dp_dialog_focus_ = 0;
    int dp_dialog_list_ = 0;
    std::string dp_dialog_title_;
    std::string dp_dialog_text_;
    // Text entry.
    std::string *kb_target_ = nullptr; // the field being typed into
    std::string kb_title_;
    bool kb_password_ = false;
    bool kb_open_ = false;             // the sheet of keys is up
    bool kb_system_ = false;           // the console's own keyboard is
    // The player's controls.
    bool pl_controls_ = true;
    float pl_timer_ = 4.0f;
    int pl_focus_ = kPlPlay;
    bool pl_scrub_ = false;       // the focus is on the progress line
    float pl_fraction_ = 0.0f;    // where its preview is
    tween::Spring pl_bar_;
    tween::Spring pl_in_;
    int pl_flash_icon_ = 0;       // what the last press did, shown for two seconds
    std::string pl_flash_text_;
    int pl_flash_side_ = 0;
    float pl_flash_left_ = 0.0f;
    bool pl_quality_open_ = false;
    bool pl_sub_open_ = false;
    int pl_sub_focus_ = 0;
    int pl_quality_focus_ = 0;
    // The watch dialogs.
    bool wd_quality_ = false;
    int wd_quality_focus_ = 0;
    int wd_focus_section_ = -1;
    int wd_focus_index_ = 0;
    tween::Spring wd_scroll_;
    // The comments.
    bool comments_open_ = false;
    tween::Spring comments_in_;
    std::vector<rezka::Comment> cm_list_;
    std::vector<CmItem> cm_items_;
    std::set<std::string> cm_revealed_;
    bool cm_has_next_ = false;
    std::uint32_t cm_version_ = ~0u;
    int cm_focus_ = 0;
    tween::Spring cm_scroll_;

    // The product page.
    bool page_open_ = false;
    bool page_from_banner_ = false;
    int page_item_ = 0;
    int translator_idx_ = 0;
    int quality_idx_ = 0;
    int season_idx_ = 0;
    int episode_idx_ = 0;
    tween::Spring page_;
    PageFocus page_focus_ = PageFocus::watch;
    PageFocus page_button_ = PageFocus::watch;
    int cast_index_ = 0; // the actor the focus is on  // the button the focus last left the row on
    bool open_rows_[3] = {false, false, false}; // genres, lists, collections
    PageModel page_model_;
    tween::Spring page_scroll_;
    ui::SpringRect page_ring_;
    tween::Spring page_ring_radius_;
    ui::Pulse press_;
    ui::Pulse page_nudge_;
    ui::Pulse wish_pop_;
    // The QR code of an address, over the page.
    bool qr_open_ = false;
    tween::Spring qr_;
    std::string qr_url_;
    std::string qr_caption_;
    qr::Code qr_code_;

    // The cart.
    std::vector<Line> lines_;
    bool flight_active_ = false;
    int flight_item_ = 0;
    tween::Timer flight_;
    int count_shown_ = 0; // what the counter says (it waits for the flight)
    int count_previous_ = 0;
    tween::Timer count_roll_;
    tween::Bounce bump_;
    tween::Spring badge_;
    bool drawer_open_ = false;
    tween::Spring drawer_;
    int drawer_focus_ = 0; // nth live line; the line count means the button
    ui::Scroller drawer_scroll_;
    ui::SpringRect drawer_ring_;
    ui::Pulse drawer_nudge_;
    tween::Spring empty_;   // the "cart is empty" note
    int subtotal_from_ = 0; // the count the drawer shows runs from here ...
    int subtotal_to_ = 0;   // ... to here
    tween::Timer subtotal_count_;

    // The checkout.
    bool search_open_ = false;
    bool search_row_del_ = false;          // a history row's delete button has the focus
    SxScreen sx_screen_ = SxScreen::none;  // Расширенный поиск or the countries, over the search
    int sx_focus_ = 0;
    int sx_cat_ = 0, sx_genre_ = 0, sx_year_ = 0; // Расширенный поиск's choices
    std::string sx_country_query_;
    tween::Spring sx_scroll_;
    tween::Spring sx_year_scroll_;
    SearchBack tv_results_back_ = SearchBack::home; // where Circle takes the list of results
    bool profile_open_ = false;
    // The settings screen
    bool settings_open_ = false;
    int st_zone_ = 0;  // 0 the groups, 1 the preferences
    int st_group_ = 0;
    int st_item_ = 0;
    bool st_editing_ = false; // a number takes left and right
    bool st_sheet_ = false;   // a list's side sheet is up
    int st_sheet_key_ = 0;
    int st_sheet_focus_ = 0;
    int st_kb_ = 0;           // the keyboard is up for: 1 the mirror, 2 the countries' search
    std::string st_text_;
    bool st_filter_dirty_ = false;
    StModal st_modal_ = StModal::none; // О приложении's QR card
    qr::Code st_qr_;
    // "Доступно обновление!": over everything, until it is answered
    bool upd_open_ = false;
    int upd_focus_ = 0; // 0 Обновить, 1 Пропустить
    std::uint32_t upd_version_ = 0;
    bool st_reconnect_ = false;
    mutable tween::Spring st_scroll_;
    mutable tween::Spring st_sheet_scroll_;
    bool person_open_ = false;
    int person_zone_ = 0; // 0 grid, 1 career chips, 2 close
    int person_focus_ = 0;
    int person_return_page_item_ = -1;
    float person_scroll_ = 0.0f;
    std::string person_nav_key_;
    std::string search_query_;
    std::string login_email_;
    std::string login_password_;
    int login_field_ = 0;
    bool login_fields_focus_ = false;
    int key_row_ = 0;
    int key_col_ = 0;
    int key_lang_ = 0;
    float seek_send_cooldown_ = 0.0f;

    bool dialog_open_ = false;
    int dialog_step_ = 0; // 0 Voiceover, 1 Quality
    tween::Spring dialog_;
    int dialog_choice_ = 0;
    tween::Spring dialog_position_;
    ui::Pulse dialog_nudge_;
    ui::Scroller dialog_scroll_;
    double seek_cursor_ = 0.0;
    float seek_hold_time_ = 0.0f;
    int seek_dir_ = 0; // -1 left, +1 right, 0 idle
    bool settling_ = false; // bought rows are still leaving
    int bought_ = 0;
    int toast_count_ = 0;
    std::string toast_text_; // a message in place of the purchase's
    float toast_left_ = 0.0f;
    float toast_age_ = 10.0f;
    tween::Spring toast_;
};

} // namespace

std::unique_ptr<app::Concept> make_store(app::Context &context)
{
    return std::make_unique<Store>(context);
}

} // namespace hui::screen

// The title's screen, for its program (ps5/ui/kit.hpp declares it)
namespace ps5ui
{
std::unique_ptr<hui::app::Concept> make_screen(hui::app::Context &context)
{
    return hui::screen::make_store(context);
}
} // namespace ps5ui
