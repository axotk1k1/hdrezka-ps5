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

    void update_player(const InputFrame &input, float dt, app::Feedback &feedback)
    {
        auto &app = rezka::App::instance();
        buffer_clock_ += dt;

        if (app.is_buffering && !app.is_playing)
        {
            if (input.is_pressed(Action::back))
            {
                app.stop_playback();
                feedback.play(audio::Cue::back);
            }
            return;
        }

        const bool left_held =
            input.is_held(Action::left) || input.stick_x < -0.55f;
        const bool right_held =
            input.is_held(Action::right) || input.stick_x > 0.55f;

        if (input.is_pressed(Action::confirm) || input.is_pressed(Action::back) ||
            input.is_pressed(Action::west) || input.is_pressed(Action::north) ||
            input.nav != Direction::none || left_held || right_held)
        {
            app.osd_timer = 4.0f;
            app.osd_visible = true;
        }

        if (input.is_pressed(Action::back))
        {
            seek_dir_ = 0;
            seek_hold_time_ = 0.0f;
            app.stop_playback();
            feedback.play(audio::Cue::back);
            return;
        }

        if (input.is_pressed(Action::confirm))
        {
            app.player.toggle_pause();
            feedback.play(audio::Cue::toggle);
        }

        // Tap: ±15 sec. Hold: scrub a virtual cursor; only push seeks to the
        // decoder every ~250ms so network av_seek_frame cannot stall the loop.
        seek_send_cooldown_ = std::max(0.0f, seek_send_cooldown_ - dt);
        if ((input.nav == Direction::left || input.nav == Direction::right) && !input.nav_repeat)
        {
            seek_dir_ = (input.nav == Direction::left) ? -1 : 1;
            seek_hold_time_ = 0.0f;
            seek_cursor_ = app.player.get_position();
            seek_player_to(app, seek_cursor_ + static_cast<double>(seek_dir_) * 15.0);
            seek_send_cooldown_ = 0.2f;
            feedback.play(audio::Cue::select);
        }

        if (left_held || right_held)
        {
            const int dir = left_held ? -1 : 1;
            if (seek_dir_ == 0) {
                seek_dir_ = dir;
                seek_hold_time_ = 0.0f;
                seek_cursor_ = app.player.get_position();
            }
            seek_hold_time_ += dt;
            if (seek_hold_time_ > 0.35f) {
                float rate = 12.0f; // video-seconds per real second
                if (seek_hold_time_ > 1.0f)
                    rate = 45.0f;
                if (seek_hold_time_ > 2.0f)
                    rate = 120.0f;
                if (seek_hold_time_ > 4.0f)
                    rate = 300.0f;
                seek_cursor_ += static_cast<double>(dir) * static_cast<double>(rate) *
                                static_cast<double>(dt);
                const double dur = app.player.get_duration();
                if (dur > 0.0)
                    seek_cursor_ = std::clamp(seek_cursor_, 0.0, dur);
                else
                    seek_cursor_ = std::max(0.0, seek_cursor_);
                if (seek_send_cooldown_ <= 0.0f) {
                    app.player.seek(seek_cursor_);
                    seek_send_cooldown_ = 0.25f;
                }
            }
        }
        else
        {
            if (seek_dir_ != 0 && seek_hold_time_ > 0.35f)
                app.player.seek(seek_cursor_);
            seek_dir_ = 0;
            seek_hold_time_ = 0.0f;
            seek_send_cooldown_ = 0.0f;
        }
    }

    void draw_buffering_spinner(gfx::DrawList &list, float cx, float cy) const
    {
        constexpr int kDots = 12;
        constexpr float kRadius = 42.0f;
        const float spin = buffer_clock_ * 2.6f;
        for (int i = 0; i < kDots; ++i)
        {
            const float a = spin + static_cast<float>(i) * (6.2831853f / static_cast<float>(kDots));
            const float fade = 0.18f + 0.82f * (static_cast<float>(i) / static_cast<float>(kDots));
            const float r = 4.0f + 2.5f * fade;
            list.circle(cx + std::cos(a) * kRadius, cy + std::sin(a) * kRadius, r,
                        kAccent.with_alpha(fade));
        }
        list.circle(cx, cy, 10.0f, kAccent.with_alpha(0.35f));
    }

    void draw_player(app::Frame &frame) const
    {
        auto &app = rezka::App::instance();
        frame.scene.clear();
        frame.overlay.clear();
        frame.glass = false;
        frame.backdrop.mode = gfx::BackdropMode::none;

        const bool has_frame = app.video_texture.hui_handle != 0 && !app.is_buffering;
        if (has_frame)
        {
            frame.scene.image(app.video_texture.hui_handle, {0.0f, 0.0f, kWidth, kHeight},
                              gfx::kFullUv, kWhite, 0.0f);
        }
        else
        {
            frame.scene.rounded_rect({0.0f, 0.0f, kWidth, kHeight}, 0.0f, kCoal);
            draw_buffering_spinner(frame.scene, kWidth * 0.5f, kHeight * 0.5f - 28.0f);
            ui::text(frame.scene, context_.fonts.semibold, "Буферизация...", kWidth * 0.5f,
                     kHeight * 0.5f + 48.0f, 28, kInk, gfx::Align::center);
            if (!app.playing_title.empty())
                ui::text(frame.scene, context_.fonts.regular, app.playing_title.c_str(),
                         kWidth * 0.5f, kHeight * 0.5f + 86.0f, 22, kInk.with_alpha(0.65f),
                         gfx::Align::center);
            const ui::GlyphStyle glyphs = ui::GlyphStyle::dark();
            const ui::Hint hints[] = {{ui::Button::circle, "Отмена"}};
            ui::draw_hints(frame.overlay, context_.fonts, glyphs, hints, 1, kWidth - 72.0f, true);
            return;
        }

        if (app.osd_visible)
        {
            gfx::DrawList &list = frame.overlay;
            const ui::Fonts &fonts = context_.fonts;

            list.gradient_rect({0.0f, 0.0f, kWidth, 160.0f}, 0.0f,
                               Color::rgb(0x000000, 0.88f), Color::rgb(0x000000, 0.0f));

            ui::text(list, fonts.semibold, app.playing_title.c_str(), 96.0f, 60.0f, 32, kWhite);

            std::string sub = app.playing_voice + "  •  " +
                              (app.playing_quality.empty() ? "HD" : app.playing_quality);
            ui::text(list, fonts.regular, sub.c_str(), 96.0f, 100.0f, 22, kAccent);

            list.gradient_rect({0.0f, kHeight - 180.0f, kWidth, 180.0f}, 0.0f,
                               Color::rgb(0x000000, 0.0f), Color::rgb(0x000000, 0.92f));

            double pos = (seek_dir_ != 0) ? seek_cursor_ : app.player.get_position();
            double dur = app.player.get_duration();
            float prog = (dur > 1.0) ? static_cast<float>(pos / dur) : 0.0f;
            prog = std::clamp(prog, 0.0f, 1.0f);

            const float bar_x = 96.0f;
            const float bar_y = kHeight - 100.0f;
            const float bar_w = kWidth - 192.0f;
            const float bar_h = 8.0f;

            list.rounded_rect({bar_x, bar_y, bar_w, bar_h}, 4.0f, Color::rgb(0xffffff, 0.22f));
            if (prog > 0.001f) {
                list.rounded_rect({bar_x, bar_y, bar_w * prog, bar_h}, 4.0f, kAccent);
                list.circle(bar_x + bar_w * prog, bar_y + bar_h * 0.5f, 9.0f, kWhite);
            }

            int cur_s = static_cast<int>(pos);
            int tot_s = static_cast<int>(dur);
            char time_buf[64];
            std::snprintf(time_buf, sizeof(time_buf), "%02d:%02d:%02d / %02d:%02d:%02d",
                          cur_s / 3600, (cur_s % 3600) / 60, cur_s % 60,
                          tot_s / 3600, (tot_s % 3600) / 60, tot_s % 60);
            const float row_cy = kHeight - 48.0f;
            const float time_w =
                ui::text(list, fonts.mono, time_buf, bar_x, kHeight - 58.0f, 20, kInk);

            bool playing = app.player.is_playing();
            const char *status_str = playing ? "ИГРАЕТ" : "ПАУЗА";
            Color status_col = playing ? kOwned : kAccent;
            const float status_x = bar_x + time_w + 28.0f;
            if (playing) {
                list.triangle({status_x, row_cy - 9.0f, 16.0f, 18.0f}, status_col, 0.0f, 1.5707963f);
            } else {
                list.rounded_rect({status_x, row_cy - 8.0f, 5.0f, 16.0f}, 1.5f, status_col);
                list.rounded_rect({status_x + 9.0f, row_cy - 8.0f, 5.0f, 16.0f}, 1.5f, status_col);
            }
            ui::text(list, fonts.semibold, status_str, status_x + 26.0f, kHeight - 58.0f, 20,
                     status_col);

            // Button hints stay on the right -- never overlap status.
            const ui::GlyphStyle glyphs = ui::GlyphStyle::dark();
            const ui::Hint hints[] = {
                {ui::Button::cross, playing ? "Пауза" : "Пуск"},
                {ui::Button::dpad, "±15с / удерж."},
                {ui::Button::circle, "Выход"},
            };
            ui::HintLayout hint_layout;
            hint_layout.size = 30.0f;
            hint_layout.text_size = 20.0f;
            hint_layout.cy = row_cy;
            hint_layout.item_gap = 32.0f;
            ui::draw_hints(list, fonts, glyphs, hints, 3, kWidth - 72.0f, true, hint_layout);
        }
    }

#if defined(PS5_HOST_REFERENCE)
    // The reference build has no pad. HDREZKA_HOST_KEYS scripts one for its
    // pictures: "2.0:left,2.5:down,3:confirm" presses each at its second.
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
        if (!search_open_ && triangle_pressed)
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

        age_ += dt;
        clock_ += dt;
        if (page_open_)
            rebuild_page_model();
        // The topmost layer takes the input; the layers under it only animate.
        if (search_open_)
            update_search(input, feedback);
        else if (profile_open_)
            update_profile(input, feedback);
        else if (person_open_)
            update_person(input, feedback);
        else if (qr_open_)
            update_qr(input, feedback);
        else if (dialog_open_)
            update_dialog(input, feedback);
        else if (drawer_open_)
            update_drawer(input, feedback);
        else if (page_open_)
            update_page(input, feedback);
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
        frame.glass = page > 0.004f || dialog > 0.004f || toast_.value > 0.01f || qr_.value > 0.01f ||
                      person_open_;
        draw_page(overlay, frame.glass_texture);
        draw_person(overlay);
        draw_qr(overlay);
        draw_search(overlay);
        draw_profile(overlay);
        draw_dialog(overlay, frame.glass_texture);
        draw_toast(overlay, frame.glass_texture);
        draw_hints(overlay, 1, arriving(page) * (person_open_ ? 0.0f : 1.0f));
        draw_hints(overlay, 3, arriving(dialog));
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

    // Separate actor page (HDrezkaTV-style): photo, careers, film grid.
    // Does not replace the home catalogue; Circle returns to the movie page.
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
        person_zone_chips_ = false;
        person_scroll_ = 0.0f;
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
        const uint32_t ver = app.person_version.load();
        if (ver != last_person_version_)
        {
            last_person_version_ = ver;
            person_focus_ = 0;
            person_scroll_ = 0.0f;
        }

        if (input.is_pressed(Action::back))
        {
            close_person(feedback);
            return;
        }

        int career_count = 0;
        int film_count = 0;
        bool loading = app.person_loading.load();
        {
            std::lock_guard<std::mutex> lock(app.catalog_mutex);
            career_count = static_cast<int>(app.person_career_titles.size());
            film_count = static_cast<int>(app.person_movies.size());
        }

        if (input.is_pressed(Action::page_prev) || input.is_pressed(Action::jump_prev))
        {
            if (career_count > 1)
            {
                int cur = 0;
                {
                    std::lock_guard<std::mutex> lock(app.catalog_mutex);
                    cur = app.person_career_idx;
                }
                app.person_select_career((cur - 1 + career_count) % career_count);
                person_focus_ = 0;
                person_zone_chips_ = true;
                feedback.play(audio::Cue::tab);
            }
            return;
        }
        if (input.is_pressed(Action::page_next) || input.is_pressed(Action::jump_next))
        {
            if (career_count > 1)
            {
                int cur = 0;
                {
                    std::lock_guard<std::mutex> lock(app.catalog_mutex);
                    cur = app.person_career_idx;
                }
                app.person_select_career((cur + 1) % career_count);
                person_focus_ = 0;
                person_zone_chips_ = true;
                feedback.play(audio::Cue::tab);
            }
            return;
        }

        if (loading || film_count <= 0)
            return;

        constexpr int kPersonCols = 6;
        if (person_zone_chips_)
        {
            if (input.nav == Direction::down)
            {
                person_zone_chips_ = false;
                feedback.play(audio::Cue::focus);
            }
            else if (input.nav == Direction::left || input.nav == Direction::right)
            {
                int cur = 0;
                {
                    std::lock_guard<std::mutex> lock(app.catalog_mutex);
                    cur = app.person_career_idx;
                }
                const int next =
                    (cur + (input.nav == Direction::right ? 1 : -1) + career_count) % career_count;
                app.person_select_career(next);
                feedback.play(audio::Cue::tab);
            }
            return;
        }

        if (input.nav == Direction::up && person_focus_ < kPersonCols && career_count > 1)
        {
            person_zone_chips_ = true;
            feedback.play(audio::Cue::focus);
            return;
        }

        int row = person_focus_ / kPersonCols;
        int col = person_focus_ % kPersonCols;
        const int rows = (film_count + kPersonCols - 1) / kPersonCols;
        if (input.nav == Direction::left)
            col = (col + kPersonCols - 1) % kPersonCols;
        else if (input.nav == Direction::right)
            col = (col + 1) % kPersonCols;
        else if (input.nav == Direction::up)
            row = std::max(0, row - 1);
        else if (input.nav == Direction::down)
            row = std::min(rows - 1, row + 1);
        if (input.nav != Direction::none)
        {
            int next = row * kPersonCols + col;
            if (next >= film_count)
                next = film_count - 1;
            if (next != person_focus_)
            {
                person_focus_ = next;
                feedback.play(audio::Cue::focus);
            }
        }

        // Keep the focused poster in the clipped grid window.
        {
            constexpr float kGap = 18.0f;
            constexpr float kPanelW = kWidth - 144.0f;
            const float card_w =
                (kPanelW - 80.0f - (kPersonCols - 1) * kGap) / static_cast<float>(kPersonCols);
            const float card_h = card_w * 1.5f;
            const float view_h = kHeight - 112.0f - (56.0f + 36.0f + 292.0f + 64.0f) - 48.0f;
            const int frow = person_focus_ / kPersonCols;
            const float y0 = static_cast<float>(frow) * (card_h + 56.0f);
            if (y0 - person_scroll_ < 0.0f)
                person_scroll_ = y0;
            if (y0 + card_h - person_scroll_ > view_h)
                person_scroll_ = y0 + card_h - view_h;
            if (person_scroll_ < 0.0f)
                person_scroll_ = 0.0f;
        }

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
        }
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
        bool loading = app.person_loading.load();
        {
            std::lock_guard<std::mutex> lock(app.catalog_mutex);
            person = app.person;
            films = app.person_movies;
            careers = app.person_career_titles;
            career_idx = app.person_career_idx;
            status = app.person_status;
        }

        list.rounded_rect({0, 0, kWidth, kHeight}, 0, Color::rgb(0x000000, 0.82f));
        const Rect panel{72.0f, 56.0f, kWidth - 144.0f, kHeight - 112.0f};
        list.rounded_rect(panel, 28.0f, kCoal);
        list.bordered_rect(panel, 28.0f, kClear, 2.0f, kAccent.with_alpha(0.55f));

        const ui::Fonts &fonts = context_.fonts;
        const float head_y = panel.y + 36.0f;
        const Rect photo{panel.x + 40.0f, head_y, 180.0f, 270.0f};
        const std::uint32_t photo_tex =
            app.photo_texture(person.photo_hq.empty() ? person.photo : person.photo_hq);
        if (photo_tex != 0)
            list.image(photo_tex, photo, cover_uv(photo), kWhite, 16.0f);
        else
            list.rounded_rect(photo, 16.0f, kPanel);

        float tx = photo.x + photo.w + 36.0f;
        ui::text(list, fonts.display, fonts.display.font->fit(person.name.c_str(), 48, 900.0f), tx,
                 head_y + 36.0f, 48, kInk);
        if (!person.name_orig.empty())
            ui::text(list, fonts.regular, person.name_orig.c_str(), tx, head_y + 92.0f, 24,
                     kInk.with_alpha(0.65f));
        float meta_y = head_y + 140.0f;
        auto meta_line = [&](const char *label, const std::string &value) {
            if (value.empty())
                return;
            ui::text(list, fonts.regular, (std::string(label) + value).c_str(), tx, meta_y, 22,
                     kInk.with_alpha(0.8f));
            meta_y += 32.0f;
        };
        meta_line("Дата рождения: ", person.date_of_birth);
        meta_line("Место рождения: ", person.place_of_birth);
        meta_line("Дата смерти: ", person.date_of_death);
        meta_line("Рост: ", person.height);

        float chip_y = head_y + 292.0f;
        float chip_x = panel.x + 40.0f;
        for (int i = 0; i < static_cast<int>(careers.size()); ++i)
        {
            const float w = fonts.semibold.measure(careers[static_cast<std::size_t>(i)].c_str(), 20) + 28.0f;
            const Rect chip{chip_x, chip_y, w, 40.0f};
            const bool on = (i == career_idx);
            const bool focus = person_zone_chips_ && on;
            list.rounded_rect(chip, 20.0f, on ? kAccent : kPanel);
            if (focus)
                list.bordered_rect(chip, 20.0f, kClear, 2.0f, kWhite);
            ui::text(list, fonts.semibold, careers[static_cast<std::size_t>(i)].c_str(), chip.cx(),
                     centred(chip.cy(), 20), 20, on ? kOnAccent : kInk.with_alpha(0.8f),
                     gfx::Align::center);
            chip_x += w + 12.0f;
        }

        const float grid_top = chip_y + 64.0f;
        if (loading || (!status.empty() && films.empty()))
        {
            ui::text(list, fonts.semibold, loading ? "Загрузка фильмографии..." : status.c_str(),
                     panel.cx(), grid_top + 120.0f, 28, kInk, gfx::Align::center);
        }
        else
        {
            constexpr int kPersonCols = 6;
            constexpr float kGap = 18.0f;
            const float card_w =
                (panel.w - 80.0f - (kPersonCols - 1) * kGap) / static_cast<float>(kPersonCols);
            const float card_h = card_w * 1.5f;
            const float ox = panel.x + 40.0f;
            list.push_clip({panel.x + 20.0f, grid_top - 8.0f, panel.w - 40.0f,
                            panel.y + panel.h - grid_top - 48.0f});
            for (int i = 0; i < static_cast<int>(films.size()); ++i)
            {
                const int row = i / kPersonCols;
                const int col = i % kPersonCols;
                const Rect card{ox + col * (card_w + kGap),
                                grid_top + row * (card_h + 56.0f) - person_scroll_, card_w, card_h};
                const bool on = !person_zone_chips_ && i == person_focus_;
                const auto &m = films[static_cast<std::size_t>(i)];
                if (m.texture_id != 0)
                    list.image(m.texture_id, card, cover_uv(card), kWhite, 12.0f);
                else
                    list.rounded_rect(card, 12.0f, kPanel);
                if (on)
                {
                    list.bordered_rect(card.inset(-4.0f), 14.0f, kClear, 3.0f, kAccent);
                    list.glow(card, 12.0f, 18.0f, kAccent.with_alpha(0.35f));
                }
                ui::text(list, fonts.regular,
                         fonts.regular.font->fit(m.title.c_str(), 18, card.w), card.x,
                         card.y + card.h + 26.0f, 18, kInk.with_alpha(0.9f));
            }
            list.pop_clip();
        }

        ui::text(list, fonts.regular, "○ Назад     ✕ Открыть     L1/R1 амплуа", panel.cx(),
                 panel.y + panel.h - 28.0f, 20, kInk.with_alpha(0.7f), gfx::Align::center);
    }

    void submit_search(app::Feedback &feedback)
    {
        if (search_query_.empty())
        {
            feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
            return;
        }
        search_open_ = false;
        page_open_ = false;
        zone_ = Zone::grid;
        focus_ = 0;
        scroll_.snap(0.0f);
        tv_zone_ = TvZone::grid;
        tv_focus_ = 0;
        tv_tab_delay_ = -1.0f;
        tv_scroll_.snap(0.0f);
        tv_cards_.clear();
        home_layout_ = false;
        {
            auto &app = rezka::App::instance();
            std::lock_guard<std::mutex> lock(app.catalog_mutex);
            app.catalog_status = "Поиск: " + search_query_;
        }
        rezka::App::instance().search_async(search_query_);
        feedback.play(audio::Cue::select);
    }

    void update_search(const InputFrame &input, app::Feedback &feedback)
    {
        if (input.is_pressed(Action::back))
        {
            search_open_ = false;
            feedback.play(audio::Cue::back);
            return;
        }
        if (input.is_pressed(Action::west))
        {
            erase_from(search_query_);
            feedback.play(audio::Cue::toggle);
            return;
        }
        if (input.is_pressed(Action::north) || input.is_pressed(Action::menu) || ((ps5_pad().pressed & PAD_TRIANGLE) != 0))
        {
            submit_search(feedback);
            return;
        }
        navigate_key_grid(input, feedback);

        if (!input.is_pressed(Action::confirm))
            return;

        if (key_row_ < 4)
        {
            const char **cur_keys = keys_for_lang(key_lang_);
            const int idx = key_row_ * kKeyCols + key_col_;
            if (idx >= 0 && idx < 48) {
                search_query_ += cur_keys[idx];
                if (search_query_.size() > 64) search_query_.resize(64);
                feedback.play(audio::Cue::select);
            }
        }
        else
        {
            if (key_col_ <= 2)
            {
                cycle_key_lang();
                feedback.play(audio::Cue::toggle);
            }
            else if (key_col_ >= 3 && key_col_ <= 6)
            {
                search_query_.push_back(' ');
                feedback.play(audio::Cue::select);
            }
            else if (key_col_ >= 7 && key_col_ <= 8)
            {
                erase_from(search_query_);
                feedback.play(audio::Cue::toggle);
            }
            else
            {
                submit_search(feedback);
            }
        }
    }

    void draw_search(gfx::DrawList &list) const
    {
        if (!search_open_)
            return;
        const ui::Fonts &fonts = context_.fonts;
        list.rounded_rect({0, 0, kWidth, kHeight}, 0, Color::rgb(0x000000, 0.78f));
        const Rect panel{260.0f, 100.0f, 1400.0f, 880.0f};
        list.rounded_rect(panel, 28.0f, kCoal);
        list.bordered_rect(panel, 28.0f, kClear, 2.0f, kAccent);
        ui::text(list, fonts.semibold, "ПОИСК HDREZKA", panel.x + 48.0f, panel.y + 54.0f, 26, kAccent, gfx::Align::left, 2.0f);

        const Rect field{panel.x + 48.0f, panel.y + 78.0f, panel.w - 96.0f, 64.0f};
        list.rounded_rect(field, 14.0f, kPanel);
        list.bordered_rect(field, 14.0f, kClear, 1.5f, kAccent.with_alpha(0.7f));
        std::string shown = search_query_.empty() ? std::string("Введите название фильма или сериала...") : search_query_;
        if (!search_query_.empty()) shown += " |";
        ui::text(list, fonts.regular, shown.c_str(), field.x + 22.0f, centred(field.cy(), 26), 26,
                 search_query_.empty() ? kInk.with_alpha(0.45f) : kInk);

        constexpr float kCell = 82.0f;
        constexpr float kGap = 12.0f;
        const float grid_w = kKeyCols * kCell + (kKeyCols - 1) * kGap;
        const float ox = panel.cx() - grid_w * 0.5f;
        const float oy = panel.y + 172.0f;

        const char **cur_keys = keys_for_lang(key_lang_);

        for (int r = 0; r < 4; ++r)
        {
            for (int c = 0; c < kKeyCols; ++c)
            {
                const int i = r * kKeyCols + c;
                const Rect key{ox + static_cast<float>(c) * (kCell + kGap),
                               oy + static_cast<float>(r) * (kCell * 0.82f + kGap), kCell, 58.0f};
                const bool on = (key_row_ == r && key_col_ == c);
                list.rounded_rect(key, 12.0f, on ? kAccent : kPanel);
                if (on) list.bordered_rect(key, 12.0f, kClear, 2.0f, kWhite);
                ui::text(list, fonts.semibold, cur_keys[i], key.cx(), centred(key.cy(), 24), 24,
                         on ? kOnAccent : kInk, gfx::Align::center);
            }
        }

        const float ay = oy + 4.0f * (kCell * 0.82f + kGap) + 8.0f;
        draw_key_action_row(list, ox, ay, kCell, kGap, 64.0f, "▶ НАЙТИ");

        ui::text(list, fonts.regular, "ENG / РУС / #!     □ Стереть     △ Искать     ○ Закрыть",
                 panel.cx(), panel.y + panel.h - 36.0f, 20, kInk.with_alpha(0.7f), gfx::Align::center);
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

    void update_profile(const InputFrame &input, app::Feedback &feedback)
    {
        auto &app = rezka::App::instance();
        rezka::Account acc;
        {
            std::lock_guard<std::mutex> lock(app.catalog_mutex);
            acc = app.account;
        }
        if (input.is_pressed(Action::back))
        {
            profile_open_ = false;
            feedback.play(audio::Cue::back);
            return;
        }
        if (acc.logged_in)
        {
            if (input.is_pressed(Action::confirm))
            {
                rezka::BigThread([]() { rezka::App::instance().client.logout(); }).detach();
                std::lock_guard<std::mutex> lock(app.catalog_mutex);
                app.account = {};
                app.account.status = "Вы вышли из профиля";
                app.catalog_version++;
                feedback.play(audio::Cue::select);
            }
            return;
        }
        const bool bumpers =
            input.is_pressed(Action::page_prev) || input.is_pressed(Action::page_next) ||
            ((ps5_pad().pressed & (PAD_L1 | PAD_R1)) != 0);
        if (bumpers)
        {
            login_field_ = 1 - login_field_;
            login_fields_focus_ = true;
            feedback.play(audio::Cue::tab);
            return;
        }
        if (input.is_pressed(Action::west))
        {
            erase_from(login_field_ == 0 ? login_email_ : login_password_);
            feedback.play(audio::Cue::toggle);
            return;
        }
        if (input.is_pressed(Action::north))
        {
            if (login_email_.empty() || login_password_.empty())
            {
                feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
                return;
            }
            app.login_async(login_email_, login_password_);
            feedback.play(audio::Cue::select);
            return;
        }

        // Focus can sit on the email/password fields (↑ from the top key row /
        // L1·R1). ↓ returns to the keyboard.
        if (login_fields_focus_)
        {
            if (input.nav == Direction::down)
            {
                login_fields_focus_ = false;
                key_row_ = 0;
                feedback.play(audio::Cue::focus);
            }
            else if (input.nav == Direction::up || input.nav == Direction::left ||
                     input.nav == Direction::right)
            {
                login_field_ = 1 - login_field_;
                feedback.play(audio::Cue::tab);
            }
            return;
        }
        if (input.nav == Direction::up && key_row_ == 0)
        {
            login_fields_focus_ = true;
            feedback.play(audio::Cue::tab);
            return;
        }

        navigate_key_grid(input, feedback);
        if (input.is_pressed(Action::confirm))
        {
            if (key_row_ == 4 && key_col_ >= 9)
            {
                if (login_email_.empty() || login_password_.empty())
                {
                    feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
                    return;
                }
                app.login_async(login_email_, login_password_);
                feedback.play(audio::Cue::select);
                return;
            }
            const bool toggled = (key_row_ == 4 && key_col_ <= 2);
            type_into(login_field_ == 0 ? login_email_ : login_password_);
            feedback.play(toggled ? audio::Cue::toggle : audio::Cue::select);
        }
    }

    void draw_profile(gfx::DrawList &list) const
    {
        if (!profile_open_)
            return;
        const ui::Fonts &fonts = context_.fonts;
        rezka::Account acc;
        {
            auto &app = rezka::App::instance();
            std::lock_guard<std::mutex> lock(app.catalog_mutex);
            acc = app.account;
        }
        list.rounded_rect({0, 0, kWidth, kHeight}, 0, Color::rgb(0x000000, 0.72f));
        const Rect panel{280.0f, 120.0f, 1360.0f, 840.0f};
        list.rounded_rect(panel, 28.0f, kCoal);
        list.bordered_rect(panel, 28.0f, kClear, 2.0f, kAccent);
        ui::text(list, fonts.semibold, "Профиль HDRezka", panel.x + 48.0f, panel.y + 64.0f, 32, kInk);

        if (acc.logged_in)
        {
            ui::text(list, fonts.display, acc.name.empty() ? "Пользователь" : acc.name.c_str(),
                     panel.x + 48.0f, panel.y + 180.0f, 48, kInk);
            ui::text(list, fonts.regular, acc.email.c_str(), panel.x + 48.0f, panel.y + 240.0f, 24,
                     kInk.with_alpha(0.7f));
            ui::text(list, fonts.semibold, acc.premium ? "Premium" : "Обычный аккаунт",
                     panel.x + 48.0f, panel.y + 300.0f, 28, acc.premium ? kAccent : kInk);
            if (!acc.premium_text.empty())
                ui::text(list, fonts.regular, acc.premium_text.c_str(), panel.x + 48.0f, panel.y + 350.0f, 22,
                         kInk.with_alpha(0.7f));
            ui::text(list, fonts.regular, "✕ Выйти     ○ Закрыть", panel.cx(), panel.y + panel.h - 36.0f, 22,
                     kInk.with_alpha(0.7f), gfx::Align::center);
            return;
        }

        const Rect email_r{panel.x + 48.0f, panel.y + 88.0f, panel.w - 96.0f, 52.0f};
        const Rect pass_r{panel.x + 48.0f, panel.y + 152.0f, panel.w - 96.0f, 52.0f};
        list.rounded_rect(email_r, 12.0f, kPanel);
        list.rounded_rect(pass_r, 12.0f, kPanel);
        const Rect &active_field = login_field_ == 0 ? email_r : pass_r;
        list.bordered_rect(active_field, 12.0f, kClear, login_fields_focus_ ? 2.5f : 1.5f, kAccent);
        if (login_fields_focus_)
            list.glow(active_field, 12.0f, 14.0f, kAccent.with_alpha(0.35f));
        ui::text(list, fonts.regular, login_email_.empty() ? "Email или логин" : login_email_.c_str(),
                 email_r.x + 20.0f, centred(email_r.cy(), 24), 24,
                 login_email_.empty() ? kInk.with_alpha(0.45f) : kInk);
        std::string dots(login_password_.size(), '*');
        ui::text(list, fonts.regular, login_password_.empty() ? "Пароль" : dots.c_str(),
                 pass_r.x + 20.0f, centred(pass_r.cy(), 24), 24,
                 login_password_.empty() ? kInk.with_alpha(0.45f) : kInk);
        if (!acc.status.empty())
            ui::text(list, fonts.regular, acc.status.c_str(), panel.x + 48.0f, panel.y + 220.0f, 20, kAccent);

        constexpr float kCell = 68.0f;
        constexpr float kGap = 8.0f;
        const float grid_w = kKeyCols * kCell + (kKeyCols - 1) * kGap;
        const float ox = panel.cx() - grid_w * 0.5f;
        const float oy = panel.y + 236.0f;
        const char **cur_keys = keys_for_lang(key_lang_);
        for (int i = 0; i < 48; ++i)
        {
            const int row = i / kKeyCols;
            const int col = i % kKeyCols;
            const Rect key{ox + static_cast<float>(col) * (kCell + kGap),
                           oy + static_cast<float>(row) * (kCell * 0.86f + kGap), kCell, 52.0f};
            const bool on = row == key_row_ && col == key_col_;
            list.rounded_rect(key, 10.0f, on ? kAccent : kPanel);
            if (on)
                list.bordered_rect(key, 10.0f, kClear, 2.0f, kWhite);
            ui::text(list, fonts.semibold, cur_keys[i], key.cx(), centred(key.cy(), 20), 20,
                     on ? kOnAccent : kInk, gfx::Align::center);
        }
        const float ay = oy + 4.0f * (kCell * 0.86f + kGap) + 6.0f;
        draw_key_action_row(list, ox, ay, kCell, kGap, 56.0f, "▶ ВОЙТИ");
        ui::text(list, fonts.regular,
                 "L1/R1 или ↑↓ поле     ENG/РУС/#!     □ стереть     △ войти     ○ закрыть",
                 panel.cx(), panel.y + panel.h - 36.0f, 20, kInk.with_alpha(0.7f),
                 gfx::Align::center);
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
    static constexpr int kTvColumns = 7;                   // the app's default "Столбцы в списках"
    static constexpr float kTvCardW =
        (kWidth - kTvRail - 2.0f * kTvPad - (kTvColumns - 1) * kTvGap) / kTvColumns;
    static constexpr float kTvCoverH = kTvCardW * 1.5f;
    static constexpr float kTvTitleSize = 14.0f * kTvDp;
    static constexpr float kTvSubSize = 12.0f * kTvDp;
    static constexpr float kTvCardH =
        kTvCoverH + 12.0f * kTvDp + 2.0f * kTvTitleSize + 4.0f * kTvDp + 2.0f * kTvSubSize;
    static constexpr float kTvRowPitch = kTvCardH + kTvGap;
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
    static Color tv_surface() { return Color::rgb(0x191c1e); }
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
        grid,
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
                tv_two_lines(*tv_medium().font, m.title, kTvTitleSize, kTvCardW, card.title);
                tv_two_lines(*tv_regular().font, m.info, kTvSubSize, kTvCardW, card.sub);
            }
            card.texture = m.texture_ready ? m.texture_id : 0;
        }
        tv_cards_ = std::move(cards);
        const int count = static_cast<int>(tv_cards_.size());
        tv_focus_ = std::clamp(tv_focus_, 0, std::max(0, count - 1));
        if (count == 0 && tv_zone_ == TvZone::grid)
            tv_zone_ = home_layout_ ? TvZone::chips : TvZone::drawer;
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

    // ---- layout ------------------------------------------------------------

    // The page's left edge: it slides aside as the drawer opens.
    float tv_page_x() const
    {
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
    static Rect tv_card_rect(int index)
    {
        const int col = index % kTvColumns;
        const int row = index / kTvColumns;
        return {kTvPad + static_cast<float>(col) * (kTvCardW + kTvGap),
                kTvGridPad + static_cast<float>(row) * kTvRowPitch, kTvCardW, kTvCardH};
    }
    // Compose's bring-into-view on a TV: the focused row's top rests 30% down
    // the viewport, as far as the content allows.
    float tv_scroll_target() const
    {
        const int count = static_cast<int>(tv_cards_.size());
        if (tv_zone_ != TvZone::grid || count == 0)
            return tv_scroll_.target;
        const float view = kHeight - kTvGridTop;
        const int rows = (count + kTvColumns - 1) / kTvColumns;
        const float content = 2.0f * kTvGridPad + static_cast<float>(rows) * kTvRowPitch - kTvGap;
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
        default:
            // Подборки and Настройки have no screen here yet; the app's own word for that.
            show_message("В разработке");
            feedback.play(audio::Cue::select);
            break;
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

        if (input.is_pressed(Action::back))
        {
            if (!listing)
            {
                tv_zone_ = TvZone::tabs;
                tv_load(0);
                feedback.play(audio::Cue::back);
            }
            else if (tv_zone_ == TvZone::grid && tv_focus_ >= kTvColumns)
            {
                tv_focus_ %= kTvColumns;
                feedback.play(audio::Cue::back);
            }
            else if (tv_zone_ == TvZone::grid)
            {
                tv_zone_ = TvZone::chips;
                tv_chip_focus_ = tv_type_;
                feedback.play(audio::Cue::back);
            }
            else
                tv_open_drawer(feedback);
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
        else if (tv_zone_ == TvZone::grid && nav != Direction::none && count > 0)
        {
            const int col = tv_focus_ % kTvColumns;
            int next = tv_focus_;
            if (nav == Direction::left)
                next = col == 0 ? -1 : tv_focus_ - 1;
            else if (nav == Direction::right)
                next = (col == kTvColumns - 1 || tv_focus_ + 1 >= count) ? tv_focus_ : tv_focus_ + 1;
            else if (nav == Direction::up)
                next = tv_focus_ - kTvColumns;
            else
            {
                // Down into a short last row lands on its last card.
                next = tv_focus_ + kTvColumns;
                if (next >= count && next / kTvColumns == (count - 1) / kTvColumns)
                    next = count - 1;
                else if (next >= count)
                    next = tv_focus_;
            }
            if (nav == Direction::left && next < 0)
                tv_open_drawer(feedback);
            else if (nav == Direction::up && next < 0)
            {
                if (listing)
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
            // The app asks for the next page a page ahead of the focus.
            if (listing && tv_focus_ + 2 * kTvColumns >= count)
                rezka::App::instance().load_movie_list_more_async();
        }

        if (!input.is_pressed(Action::confirm))
            return;
        if (tv_zone_ == TvZone::grid && count > 0)
            open_page(tv_focus_, false, feedback);
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
            std::max(0, (tv_focus_ / kTvColumns - 1) * kTvColumns);
        tv_open_.target = tv_zone_ == TvZone::drawer ? 1.0f : 0.0f;
        tv_open_.update(dt, omega(14.0f));
        tv_scroll_.target = tv_scroll_target();
        tv_scroll_.update(dt, omega(12.0f));
        const bool covered = page_open_ || search_open_ || profile_open_ || person_open_;
        tv_clock_.target = (tv_zone_ != TvZone::drawer && home_layout_ && !covered) ? 1.0f : 0.0f;
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
    }

    // ---- drawing -----------------------------------------------------------

    void tv_draw_tabs(gfx::DrawList &list) const
    {
        const float size = 16.0f * kTvDp;
        if (!home_layout_ || tv_kind_ != 0)
        {
            // Other lists are headed by their name where the tabs would be.
            const char *title = !home_layout_ ? "Поиск" : tv_kind_ == 1 ? "Новинки" : "Анонсы";
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
                for (int i = 0; i < 2 * kTvColumns; ++i)
                {
                    const Rect r = tv_card_rect(i);
                    list.rounded_rect({r.x, kTvGridTop + r.y, r.w, kTvCoverH}, radius,
                                      tv_surface_variant().with_alpha(pulse));
                }
            }
            else if (!tv_status_.empty())
                tv_text(list, tv_regular(), tv_status_, (kWidth - kTvRail) * 0.5f,
                        kTvGridTop + view * 0.4f, 22.0f * kTvDp, 28.0f * kTvDp, tv_on_surface(),
                        gfx::Align::center);
            return;
        }

        // Only the rows in the viewport; the focused card goes last, over its neighbours.
        const int first_row = std::max(0, static_cast<int>((scroll - kTvGridPad) / kTvRowPitch) - 1);
        const int last_row = static_cast<int>((scroll + view) / kTvRowPitch) + 1;
        const int first = first_row * kTvColumns;
        const int last = std::min(count, (last_row + 1) * kTvColumns);
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
                const Rect cover{r.x, r.y, r.w, kTvCoverH};
                const Rect grown = scaled(cover, scale);
                const float grown_radius = radius * scale;
                if (card.texture != 0)
                    list.image(card.texture, grown, cover_uv(grown), kWhite, grown_radius);
                else
                    list.rounded_rect(grown, grown_radius, tv_surface_variant());
                if (card.lift > 0.01f)
                    list.bordered_rect(grown, grown_radius, kClear, 3.0f * kTvDp * scale,
                                       tv_border().with_alpha(card.lift));
                float y = cover.y + kTvCoverH + 12.0f * kTvDp;
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

    void tv_draw_home(gfx::DrawList &list) const
    {
        list.push_transform(1.0f, 0.0f, 0.0f, tv_page_x(), 0.0f);
        tv_draw_tabs(list);
        tv_draw_chips(list);
        tv_draw_grid(list);
        list.pop_transform();
        tv_draw_clock(list);
        tv_draw_drawer(list);
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
                if (!movie.details_loaded)
                    m.description = "Загрузка…";
                else
                    m.description = d.description.empty() ? "Описание недоступно." : d.description;
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
            m.cast = d.people;
                voices = d.translators;
            }
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

    // The bookmark: adds the open title to the bookmarks, or takes it out.
    void toggle_bookmark(app::Feedback &feedback)
    {
        if (page_model_.url.empty())
        {
            feedback.play(audio::Cue::error, 1.0f, 0.0f, 0.6f);
            return;
        }
        const bool on = rezka::App::instance().toggle_favorite(page_model_.url, page_model_.title);
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
        // Streams are not known yet, so quality_idx_ is passed on as the
        // default-list index (1080p Ultra, 1080p, ...); play_title_async maps it.
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
    bool profile_open_ = false;
    bool person_open_ = false;
    bool person_zone_chips_ = false;
    int person_focus_ = 0;
    int person_return_page_item_ = -1;
    float person_scroll_ = 0.0f;
    uint32_t last_person_version_ = 0;
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
