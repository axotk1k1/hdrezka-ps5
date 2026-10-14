#pragma once
// The app's preferences, as HDrezkaTV 1.4.0 has them (R8.c in the APK): the
// same keys, the same choices in the same order, the same defaults. They are
// kept in data/settings.cfg, one "key=value" a line, a list value's name as
// the app stores it ("QualityAskAlways", "BottomCenter", ...).

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>

namespace rezka
{

// One choice of a list preference: the name stored and the words shown.
struct SettingChoice
{
    const char *name;
    const char *label;
};

// Внешний вид
inline constexpr SettingChoice kAnimNavDrawerChoices[] = {
    {"ScreenSliding", "Сдвиг экрана"},
    {"ScreenShrinking", "Сжатие экрана"},
};
// Подключение и предпочтения. "Свой домен" is this port's: the app has the
// two mirrors only, a console has no other way to reach a third.
inline constexpr SettingChoice kSourceChoices[] = {
    {"Default", "По умолчанию"},
    {"Hdrezka", "hdrezka.ag"},
    {"Custom", "Свой домен…"},
};
// Плеер
inline constexpr SettingChoice kVideoQualityChoices[] = {
    {"QualityAskAlways", "Всегда спрашивать"},
    {"QualityHighestAvailable", "Лучшее из доступных"},
    {"Quality4k", "4K"},
    {"Quality2k", "2K"},
    {"Quality1080pUltra", "1080p Ultra"},
    {"Quality1080p", "1080p"},
    {"Quality720p", "720p"},
    {"Quality480p", "480p"},
    {"Quality360p", "360p"},
};
inline constexpr SettingChoice kPlaySpeedChoices[] = {
    {"Speed025", "0.25"}, {"Speed050", "0.5"}, {"Speed075", "0.75"}, {"Speed100", "Нормальная"},
    {"Speed125", "1.25"}, {"Speed150", "1.5"}, {"Speed175", "1.75"}, {"Speed200", "2.0"},
};
inline constexpr float kPlaySpeeds[] = {0.25f, 0.5f, 0.75f, 1.0f, 1.25f, 1.5f, 1.75f, 2.0f};
inline constexpr SettingChoice kStreamBufferSizeChoices[] = {
    {"BufferSizeAuto", "Автовыбор"}, {"BufferSize32Mb", "32 МБ"},   {"BufferSize64Mb", "64 МБ"},
    {"BufferSize128Mb", "128 МБ"},   {"BufferSize256Mb", "256 МБ"}, {"BufferSize512Mb", "512 МБ"},
    {"BufferSize1024Mb", "1024 МБ"},
};
inline constexpr int kStreamBufferSizeMb[] = {-1, 32, 64, 128, 256, 512, 1024};
inline constexpr SettingChoice kBufferDurationChoices[] = {
    {"BufferDuration1Min", "1 мин"}, {"BufferDuration2Min", "2 мин"}, {"BufferDuration3Min", "3 мин"},
    {"BufferDuration5Min", "5 мин"}, {"BufferDuration7Min", "7 мин"}, {"BufferDuration10Min", "10 мин"},
};
inline constexpr int kBufferDurationSeconds[] = {60, 120, 180, 300, 420, 600};
inline constexpr SettingChoice kBackBufferDurationChoices[] = {
    {"BackBufferDuration0Min", "Без обратного буфера (0 мин)"},
    {"BackBufferDuration1Min", "1 мин"},
    {"BackBufferDuration2Min", "2 мин"},
    {"BackBufferDuration3Min", "3 мин"},
};
// Субтитры
inline constexpr SettingChoice kSubtitleArrangementChoices[] = {
    {"TopCenter", "Сверху по центру"},    {"TopStart", "Сверху слева"},    {"TopEnd", "Сверху справа"},
    {"BottomCenter", "Снизу по центру"}, {"BottomStart", "Снизу слева"}, {"BottomEnd", "Снизу справа"},
};
inline constexpr SettingChoice kSubtitleFontSizeChoices[] = {
    {"Small", "Мелкий"},
    {"Medium", "Средний"},
    {"Large", "Крупный"},
    {"ExtraLarge", "Очень большой"},
};
inline constexpr int kSubtitleFontSp[] = {16, 20, 24, 32};

// "Фильтры контента": the countries a title can be hidden for (N8.a).
inline constexpr const char *kCountryFilterChoices[] = {
    "Россия", "СССР", "США", "Франция", "Италия", "Испания", "Великобритания", "Германия", "Турция",
    "Корея Южная", "Япония", "Австралия", "Австрия", "Азербайджан", "Албания", "Алжир", "Ангола",
    "Андорра", "Аргентина", "Армения", "Аруба", "Афганистан", "Багамы", "Бангладеш", "Барбадос",
    "Бахрейн", "Беларусь", "Белиз", "Бельгия", "Бермуды", "Болгария", "Боливия", "Босния и Герцеговина",
    "Ботсвана", "Бразилия", "Буркина-Фасо", "Бутан", "Вануату", "Ватикан", "Венгрия", "Венесуэла",
    "Вьетнам", "Вьетнам Северный", "Гаити", "Гана", "Гваделупа", "Гватемала", "Германия (ГДР)",
    "Германия (ФРГ)", "Гондурас", "Гонконг", "Гренландия", "Греция", "Грузия", "Дания", "Доминикана",
    "Египет", "Замбия", "Зимбабве", "Израиль", "Индия", "Индонезия", "Иордания", "Ирак", "Иран",
    "Ирландия", "Исландия", "Йемен", "Казахстан", "Каймановы острова", "Камбоджа", "Камерун", "Канада",
    "Катар", "Кения", "Кипр", "Китай", "Колумбия", "Конго", "Корея", "Корея Северная", "Косово",
    "Коста-Рика", "Кот-д’Ивуар", "Куба", "Кувейт", "Кыргызстан", "Лаос", "Латвия", "Лесото", "Ливан",
    "Ливия", "Литва", "Лихтенштейн", "Люксембург", "Маврикий", "Мавритания", "Мадагаскар", "Макао",
    "Македония", "Малайзия", "Мальта", "Марокко", "Мексика", "Молдова", "Монако", "Монголия", "Намибия",
    "Непал", "Нигерия", "Нидерланды", "Никарагуа", "Новая Зеландия", "Норвегия", "ОАЭ", "Остров Мэн",
    "Остров Святой Елены", "Пакистан", "Палестина", "Панама", "Папуа - Новая Гвинея", "Парагвай", "Перу",
    "Польша", "Португалия", "Пуэрто Рико", "Реюньон", "Российская империя", "Руанда", "Румыния",
    "Сальвадор", "Саудовская Аравия", "Северная Македония", "Сенегал", "Сент-Китс и Невис", "Сербия",
    "Сербия и Черногория", "Сингапур", "Сирия", "Словакия", "Словения", "Сомали", "Судан", "Таджикистан",
    "Таиланд", "Тайвань", "Танзания", "Тринидад и Тобаго", "Тунис", "Туркменистан", "Уганда",
    "Узбекистан", "Украина", "Уругвай", "Фарерские острова", "Фиджи", "Филиппины", "Финляндия",
    "Французская Полинезия", "Хорватия", "Чад", "Черногория", "Чехия", "Чехословакия", "Чили",
    "Швейцария", "Швеция", "Шри-Ланка", "Эквадор", "Эстония", "Эфиопия", "ЮАР", "Югославия",
    "Югославия (ФР)", "Ямайка",
};

template <std::size_t N>
constexpr int setting_count(const SettingChoice (&)[N])
{
    return static_cast<int>(N);
}

struct AppSettings
{
    // Внешний вид
    bool black_mode = false;          // isBlackMode
    int columns = 7;                  // columnsCount, 5..9
    bool show_rating_voices = true;   // isShowRatingVoices
    bool show_recommendations = true; // isShowRecommendations
    bool show_clock = true;           // isShowClock
    bool rainbow_progress = false;    // isRainbowProgressBar
    int anim_nav_drawer = 0;          // animNavDrawer: kAnimNavDrawerChoices
    // Подключение и предпочтения
    int source = 0;                   // source: kSourceChoices
    std::string custom_mirror;        // source=Custom: "https://…"
    // isEnableBypassSsl. The app starts with it off; here it starts on, as
    // the title has always run (the console's certificate list is not the
    // phone's, and a mirror with a bad certificate would stop every request).
    bool bypass_ssl = true;
    std::set<std::string> country_filter; // countryFilter
    // Плеер
    int video_quality = 0;            // videoQuality: kVideoQualityChoices
    bool auto_next_episode = true;    // isAutoNextEpisode
    int step_forward = 10;            // stepForward, 5..60 by 5
    int step_replay = 10;             // stepReplay, 5..60 by 5
    int play_speed = 3;               // playSpeed: kPlaySpeedChoices
    int stream_buffer_size = 0;       // streamBufferSize
    int buffer_duration = 0;          // bufferDuration
    int back_buffer_duration = 1;     // backBufferDuration
    // Субтитры
    int subtitle_arrangement = 3;     // subtitleArrangement: BottomCenter
    int subtitle_font_size = 1;       // subtitleFontSize: Medium

    // The mirror the source names; empty for the default (probe the known ones).
    std::string mirror() const
    {
        if (source == 1)
            return "https://hdrezka.ag";
        if (source == 2)
            return custom_mirror;
        return {};
    }
    // The quality label to play by default, "" for the best one, nullptr to ask.
    const char *quality_label() const
    {
        static const char *const kLabels[] = {nullptr, "", "4K", "2K", "1080p Ultra", "1080p", "720p", "480p", "360p"};
        return kLabels[std::clamp(video_quality, 0, 8)];
    }

    bool load(const std::string &path)
    {
        FILE *f = std::fopen(path.c_str(), "r");
        if (!f)
            return false;
        char line[4096];
        while (std::fgets(line, sizeof(line), f)) {
            std::string s(line);
            while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
                s.pop_back();
            const auto eq = s.find('=');
            if (eq == std::string::npos)
                continue;
            set_value(s.substr(0, eq), s.substr(eq + 1));
        }
        std::fclose(f);
        return true;
    }

    bool save(const std::string &path) const
    {
        FILE *f = std::fopen(path.c_str(), "w");
        if (!f)
            return false;
        auto put_bool = [&](const char *key, bool v) { std::fprintf(f, "%s=%s\n", key, v ? "true" : "false"); };
        auto put_int = [&](const char *key, int v) { std::fprintf(f, "%s=%d\n", key, v); };
        auto put_choice = [&](const char *key, const SettingChoice *choices, int count, int v) {
            std::fprintf(f, "%s=%s\n", key, choices[std::clamp(v, 0, count - 1)].name);
        };
        put_bool("isBlackMode", black_mode);
        put_int("columnsCount", columns);
        put_bool("isShowRatingVoices", show_rating_voices);
        put_bool("isShowRecommendations", show_recommendations);
        put_bool("isShowClock", show_clock);
        put_bool("isRainbowProgressBar", rainbow_progress);
        put_choice("animNavDrawer", kAnimNavDrawerChoices, setting_count(kAnimNavDrawerChoices), anim_nav_drawer);
        put_choice("source", kSourceChoices, setting_count(kSourceChoices), source);
        std::fprintf(f, "customMirror=%s\n", custom_mirror.c_str());
        put_bool("isEnableBypassSsl", bypass_ssl);
        std::string countries;
        for (const std::string &c : country_filter)
            countries += (countries.empty() ? "" : "|") + c;
        std::fprintf(f, "countryFilter=%s\n", countries.c_str());
        put_choice("videoQuality", kVideoQualityChoices, setting_count(kVideoQualityChoices), video_quality);
        put_bool("isAutoNextEpisode", auto_next_episode);
        put_int("stepForward", step_forward);
        put_int("stepReplay", step_replay);
        put_choice("playSpeed", kPlaySpeedChoices, setting_count(kPlaySpeedChoices), play_speed);
        put_choice("streamBufferSize", kStreamBufferSizeChoices, setting_count(kStreamBufferSizeChoices),
                   stream_buffer_size);
        put_choice("bufferDuration", kBufferDurationChoices, setting_count(kBufferDurationChoices), buffer_duration);
        put_choice("backBufferDuration", kBackBufferDurationChoices, setting_count(kBackBufferDurationChoices),
                   back_buffer_duration);
        put_choice("subtitleArrangement", kSubtitleArrangementChoices, setting_count(kSubtitleArrangementChoices),
                   subtitle_arrangement);
        put_choice("subtitleFontSize", kSubtitleFontSizeChoices, setting_count(kSubtitleFontSizeChoices),
                   subtitle_font_size);
        std::fclose(f);
        return true;
    }

  private:
    template <std::size_t N>
    static void set_choice(int &out, const SettingChoice (&choices)[N], const std::string &value)
    {
        for (std::size_t i = 0; i < N; ++i)
            if (value == choices[i].name)
                out = static_cast<int>(i);
    }
    static void set_int(int &out, const std::string &value, int lo, int hi)
    {
        char *end = nullptr;
        const long v = std::strtol(value.c_str(), &end, 10);
        if (end != value.c_str())
            out = std::clamp(static_cast<int>(v), lo, hi);
    }

    void set_value(const std::string &key, const std::string &value)
    {
        const bool on = value == "true" || value == "1";
        if (key == "isBlackMode") black_mode = on;
        else if (key == "columnsCount") set_int(columns, value, 5, 9);
        else if (key == "isShowRatingVoices") show_rating_voices = on;
        else if (key == "isShowRecommendations") show_recommendations = on;
        else if (key == "isShowClock") show_clock = on;
        else if (key == "isRainbowProgressBar") rainbow_progress = on;
        else if (key == "animNavDrawer") set_choice(anim_nav_drawer, kAnimNavDrawerChoices, value);
        else if (key == "source") set_choice(source, kSourceChoices, value);
        else if (key == "customMirror") custom_mirror = value;
        else if (key == "isEnableBypassSsl") bypass_ssl = on;
        else if (key == "countryFilter") {
            country_filter.clear();
            std::size_t at = 0;
            while (at <= value.size()) {
                const std::size_t bar = value.find('|', at);
                const std::string c = value.substr(at, bar == std::string::npos ? std::string::npos : bar - at);
                if (!c.empty())
                    country_filter.insert(c);
                if (bar == std::string::npos)
                    break;
                at = bar + 1;
            }
        }
        else if (key == "videoQuality") set_choice(video_quality, kVideoQualityChoices, value);
        else if (key == "isAutoNextEpisode") auto_next_episode = on;
        else if (key == "stepForward") set_int(step_forward, value, 5, 60);
        else if (key == "stepReplay") set_int(step_replay, value, 5, 60);
        else if (key == "playSpeed") set_choice(play_speed, kPlaySpeedChoices, value);
        else if (key == "streamBufferSize") set_choice(stream_buffer_size, kStreamBufferSizeChoices, value);
        else if (key == "bufferDuration") set_choice(buffer_duration, kBufferDurationChoices, value);
        else if (key == "backBufferDuration") set_choice(back_buffer_duration, kBackBufferDurationChoices, value);
        else if (key == "subtitleArrangement") set_choice(subtitle_arrangement, kSubtitleArrangementChoices, value);
        else if (key == "subtitleFontSize") set_choice(subtitle_font_size, kSubtitleFontSizeChoices, value);
    }
};

} // namespace rezka
