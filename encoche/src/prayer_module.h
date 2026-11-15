#pragma once
// Module Prière : horaires d'une mosquée Mawaqit.
//
// Mawaqit n'a pas d'API publique documentée : on lit la page publique de la mosquée
// (https://mawaqit.net/fr/<identifiant>), qui embarque ses données dans une variable JavaScript
// « confData » (champs "times" = 6 horaires du jour, "calendar" = mois/jours, "name").
// Si Mawaqit change ce format, seule la fonction ParseConfData() est à adapter.

#include "module.h"
#include "config.h"
#include "http_util.h"
#include <array>
#include <atomic>
#include <chrono>
#include <ctime>
#include <memory>
#include <mutex>
#include <thread>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Data.Json.h>

class PrayerModule final : public INotchModule {
public:
    const wchar_t* Name() const override { return L"Prière"; }

    bool Tick() override {
        auto now = Clock::now();
        SYSTEMTIME st;
        GetLocalTime(&st);
        int day = st.wYear * 400 + st.wMonth * 32 + st.wDay;
        const std::wstring slug = Cfg().mosque;

        if (slug != loadedSlug_ || day != loadedDay_) {
            if (slug.empty()) {
                std::lock_guard<std::mutex> lk(d_->mx);
                d_->ok = false;
                d_->failed = false;
                loadedSlug_ = slug;
                loadedDay_ = day;
                d_->dirty = true;
            } else if (!d_->busy) {
                Start(slug, day, now);
            }
        } else if (d_->failed && !d_->busy && now - lastTry_ > std::chrono::minutes(10)) {
            Start(slug, day, now);  // réseau absent / erreur : on réessaie toutes les 10 min
        }

        bool dirty = d_->dirty.exchange(false);
        int minute = st.wHour * 60 + st.wMinute;
        if (minute != lastMinute_) {
            lastMinute_ = minute;
            dirty = true;
        }
        return dirty;
    }

    void DrawCompact(Gdiplus::Graphics& g, Gdiplus::RectF a, float al) override {
        Next n = ComputeNext();
        std::wstring text;
        if (n.state == Next::Ok)
            text = std::wstring(kNames[n.idx]) + L"  " + (n.delta <= 30 ? Countdown(n.delta) : Hhmm(n.time));
        else
            text = L"Prière";
        PutText(g, text, 13, true, Rgba(255, 255, 255, al), Gdiplus::RectF(a.X, a.Y + 2, a.Width, a.Height),
                Gdiplus::StringAlignmentCenter);
    }

    void DrawExpanded(Gdiplus::Graphics& g, Gdiplus::RectF a, float al) override {
        Next n = ComputeNext();
        if (n.state != Next::Ok) {
            const wchar_t* msg =
                n.state == Next::NoMosque ? L"Choisis ta mosquée dans les réglages (engrenage)"
                : n.state == Next::Failed ? L"Impossible de charger les horaires Mawaqit"
                                          : L"Chargement des horaires…";
            PutText(g, msg, 14, false, Rgba(160, 160, 160, al),
                    Gdiplus::RectF(a.X, a.Y + 50, a.Width, 24), Gdiplus::StringAlignmentCenter);
            return;
        }
        PutText(g, n.mosque, 12, false, Rgba(150, 150, 150, al),
                Gdiplus::RectF(a.X, a.Y - 2, a.Width - 30, 18));
        PutText(g, std::wstring(kNames[n.idx]) + L"  " + Hhmm(n.time), 26, true,
                Rgba(255, 255, 255, al), Gdiplus::RectF(a.X, a.Y + 22, a.Width, 36),
                Gdiplus::StringAlignmentCenter);
        PutText(g, Countdown(n.delta), 14, false, Accent(al),
                Gdiplus::RectF(a.X, a.Y + 62, a.Width, 22), Gdiplus::StringAlignmentCenter);

        float colW = a.Width / 6.f;
        for (int i = 0; i < 6; ++i) {
            bool isNext = (i == n.idx);
            float x = a.X + i * colW;
            Gdiplus::Color nameC = isNext ? Accent(al) : Rgba(150, 150, 150, al);
            Gdiplus::Color timeC = isNext ? Accent(al) : Rgba(i == 1 ? 140 : 235, i == 1 ? 140 : 235,
                                                              i == 1 ? 140 : 235, al);
            PutText(g, kNames[i], 11, false, nameC, Gdiplus::RectF(x, a.Y + 98, colW, 16),
                    Gdiplus::StringAlignmentCenter);
            PutText(g, Hhmm(n.times[i]), 15, isNext, timeC, Gdiplus::RectF(x, a.Y + 114, colW, 22),
                    Gdiplus::StringAlignmentCenter);
        }
    }

    void OnClick(Gdiplus::PointF, Gdiplus::RectF) override {}

private:
    using Clock = std::chrono::steady_clock;
    static constexpr const wchar_t* kNames[6] = {L"Fajr", L"Chourouk", L"Dhuhr", L"Asr", L"Maghrib", L"Isha"};

    struct Data {
        std::mutex mx;
        bool ok = false;
        std::array<int, 6> t{};      // minutes depuis minuit
        int tomorrowFajr = -1;       // Fajr de demain (calendrier), -1 si inconnu
        std::wstring mosque;
        std::atomic<bool> dirty{true};
        std::atomic<bool> failed{false};
        std::atomic<bool> busy{false};
        std::atomic<int> gen{0};
    };

    struct Next {
        enum State { NoMosque, Loading, Failed, Ok } state = NoMosque;
        int idx = 0;                 // prochaine prière (0, 2, 3, 4 ou 5)
        int time = 0;                // son horaire (minutes)
        int delta = 0;               // minutes restantes
        std::array<int, 6> times{};
        std::wstring mosque;
    };

    Next ComputeNext() {
        Next n;
        if (Cfg().mosque.empty()) return n;
        SYSTEMTIME st;
        GetLocalTime(&st);
        int nowMin = st.wHour * 60 + st.wMinute;

        std::lock_guard<std::mutex> lk(d_->mx);
        if (!d_->ok) {
            n.state = d_->failed ? Next::Failed : Next::Loading;
            return n;
        }
        n.state = Next::Ok;
        n.times = d_->t;
        n.mosque = d_->mosque;
        static const int prayers[5] = {0, 2, 3, 4, 5};  // le lever du soleil n'est pas une prière
        for (int i : prayers) {
            if (d_->t[i] > nowMin) {
                n.idx = i;
                n.time = d_->t[i];
                n.delta = d_->t[i] - nowMin;
                return n;
            }
        }
        n.idx = 0;  // après Isha : Fajr de demain
        n.time = d_->tomorrowFajr >= 0 ? d_->tomorrowFajr : d_->t[0];
        n.delta = n.time + 1440 - nowMin;
        return n;
    }

    static std::wstring Hhmm(int m) {
        wchar_t b[16];
        swprintf_s(b, L"%02d:%02d", (m / 60) % 24, m % 60);
        return b;
    }

    static std::wstring Countdown(int m) {
        wchar_t b[32];
        if (m >= 60) swprintf_s(b, L"dans %d h %02d", m / 60, m % 60);
        else swprintf_s(b, L"dans %d min", m);
        return b;
    }

    void Start(const std::wstring& slug, int day, Clock::time_point now) {
        loadedSlug_ = slug;
        loadedDay_ = day;
        lastTry_ = now;
        d_->busy = true;
        d_->failed = false;
        int gen = ++d_->gen;
        std::thread(&PrayerModule::Fetch, d_, gen, slug).detach();
    }

    static bool ParseHm(const std::wstring& s, int& out) {
        int h = 0, m = 0;
        if (swscanf_s(s.c_str(), L"%d:%d", &h, &m) != 2) return false;
        out = h * 60 + m;
        return true;
    }

    // Extrait l'objet JSON qui suit « confData » dans la page (accolades appariées, chaînes ignorées).
    static bool ExtractConfJson(const std::wstring& html, std::wstring& json) {
        size_t p = html.find(L"confData");
        if (p == std::wstring::npos) return false;
        size_t b = html.find(L'{', p);
        if (b == std::wstring::npos) return false;
        int depth = 0;
        bool inStr = false, esc = false;
        for (size_t i = b; i < html.size(); ++i) {
            wchar_t c = html[i];
            if (inStr) {
                if (esc) esc = false;
                else if (c == L'\\') esc = true;
                else if (c == L'"') inStr = false;
                continue;
            }
            if (c == L'"') inStr = true;
            else if (c == L'{') ++depth;
            else if (c == L'}' && --depth == 0) {
                json = html.substr(b, i - b + 1);
                return true;
            }
        }
        return false;
    }

    static bool ParseConfData(const std::wstring& html, std::array<int, 6>& t, int& tomorrowFajr,
                              std::wstring& name) {
        using namespace winrt::Windows::Data::Json;
        std::wstring js;
        if (!ExtractConfJson(html, js)) return false;
        JsonObject o{nullptr};
        if (!JsonObject::TryParse(js, o)) return false;
        if (!o.HasKey(L"times")) return false;
        // "times" = 5 horaires (Fajr, Dhuhr, Asr, Maghrib, Isha) ; le lever du soleil est à part
        // dans "shuruq" (format confirmé par le code de github.com/mrsofiane/mawaqit-api).
        auto times = o.GetNamedArray(L"times");
        if (times.Size() >= 6) {
            for (uint32_t i = 0; i < 6; ++i)
                if (!ParseHm(times.GetStringAt(i).c_str(), t[i])) return false;
        } else if (times.Size() == 5 && o.HasKey(L"shuruq")) {
            static const int slot[5] = {0, 2, 3, 4, 5};
            for (uint32_t i = 0; i < 5; ++i)
                if (!ParseHm(times.GetStringAt(i).c_str(), t[slot[i]])) return false;
            if (!ParseHm(o.GetNamedString(L"shuruq").c_str(), t[1])) return false;
        } else {
            return false;
        }
        name = o.HasKey(L"name") ? o.GetNamedString(L"name").c_str() : L"";

        // Fajr de demain, depuis le calendrier annuel (calendar[mois-1]["jour"][0]).
        tomorrowFajr = -1;
        try {
            std::time_t tt = std::time(nullptr) + 24 * 3600;
            std::tm tm{};
            localtime_s(&tm, &tt);
            if (o.HasKey(L"calendar")) {
                auto cal = o.GetNamedArray(L"calendar");
                if ((uint32_t)tm.tm_mon < cal.Size()) {
                    auto month = cal.GetObjectAt(tm.tm_mon);
                    winrt::hstring key = winrt::hstring(std::to_wstring(tm.tm_mday));
                    if (month.HasKey(key)) {
                        auto day = month.GetNamedArray(key);
                        int f;
                        if (day.Size() > 0 && ParseHm(day.GetStringAt(0).c_str(), f)) tomorrowFajr = f;
                    }
                }
            }
        } catch (...) {}
        return true;
    }

    static void Fetch(std::shared_ptr<Data> d, int gen, std::wstring slug) {
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
        } catch (...) {}
        std::wstring html;
        std::array<int, 6> t{};
        int tomorrow = -1;
        std::wstring name;
        bool ok = HttpGetString(L"https://mawaqit.net/fr/" + UrlEncode(slug), html,
                                L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) Encoche/1.1") &&
                  ParseConfData(html, t, tomorrow, name);
        if (d->gen.load() == gen) {
            std::lock_guard<std::mutex> lk(d->mx);
            if (ok) {
                d->t = t;
                d->tomorrowFajr = tomorrow;
                d->mosque = name;
            }
            d->ok = ok;
            d->failed = !ok;
            d->dirty = true;
        }
        d->busy = false;
    }

    std::shared_ptr<Data> d_ = std::make_shared<Data>();
    std::wstring loadedSlug_;
    int loadedDay_ = -1;
    int lastMinute_ = -1;
    Clock::time_point lastTry_ = Clock::now();
};
