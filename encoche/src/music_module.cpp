#include "music_module.h"
#include "config.h"
#include "http_util.h"
#include <algorithm>
#include <cwctype>
#include <shlwapi.h>
#include <thread>
#include <vector>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Storage.Streams.h>

using namespace winrt::Windows::Media::Control;
using namespace winrt::Windows::Storage::Streams;
using Gdiplus::PointF;
using Gdiplus::RectF;

namespace {

// Décode la pochette et la réduit à 128x128 (le flux mémoire peut ensuite être libéré).
std::unique_ptr<Gdiplus::Bitmap> LoadArt(IRandomAccessStreamReference const& ref) {
    try {
        auto stream = ref.OpenReadAsync().get();
        uint64_t size = stream.Size();
        if (size == 0 || size > 8 * 1024 * 1024) return nullptr;

        DataReader reader(stream);
        reader.LoadAsync(static_cast<uint32_t>(size)).get();
        std::vector<uint8_t> buf(static_cast<size_t>(size));
        reader.ReadBytes(buf);

        IStream* is = SHCreateMemStream(buf.data(), static_cast<UINT>(buf.size()));
        if (!is) return nullptr;

        auto dst = std::make_unique<Gdiplus::Bitmap>(128, 128, PixelFormat32bppPARGB);
        {
            Gdiplus::Bitmap src(is);
            if (src.GetLastStatus() == Gdiplus::Ok) {
                Gdiplus::Graphics g(dst.get());
                g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
                g.DrawImage(&src, 0, 0, 128, 128);
            }
        }
        is->Release();
        return dst;
    } catch (...) {
        return nullptr;
    }
}

void DrawPlayPause(Gdiplus::Graphics& g, PointF c, bool playing, Gdiplus::Color col) {
    Gdiplus::SolidBrush br(col);
    if (playing) {
        g.FillRectangle(&br, c.X - 7, c.Y - 9, 5.f, 18.f);
        g.FillRectangle(&br, c.X + 2, c.Y - 9, 5.f, 18.f);
    } else {
        PointF pts[3] = {{c.X - 5, c.Y - 9}, {c.X - 5, c.Y + 9}, {c.X + 9, c.Y}};
        g.FillPolygon(&br, pts, 3);
    }
}

void DrawSkip(Gdiplus::Graphics& g, PointF c, bool next, Gdiplus::Color col) {
    Gdiplus::SolidBrush br(col);
    if (next) {
        PointF pts[3] = {{c.X - 8, c.Y - 8}, {c.X - 8, c.Y + 8}, {c.X + 4, c.Y}};
        g.FillPolygon(&br, pts, 3);
        g.FillRectangle(&br, c.X + 5, c.Y - 8, 3.f, 16.f);
    } else {
        PointF pts[3] = {{c.X + 8, c.Y - 8}, {c.X + 8, c.Y + 8}, {c.X - 4, c.Y}};
        g.FillPolygon(&br, pts, 3);
        g.FillRectangle(&br, c.X - 8, c.Y - 8, 3.f, 16.f);
    }
}


std::wstring FormatTime(long long ms) {
    long long s = std::max(0LL, ms) / 1000;
    wchar_t b[24];
    swprintf_s(b, L"%lld:%02lld", s / 60, s % 60);
    return b;
}

// Format LRC : "[mm:ss.xx] texte" (plusieurs horodatages possibles par ligne).
std::vector<std::pair<long long, std::wstring>> ParseLrc(const std::wstring& src) {
    std::vector<std::pair<long long, std::wstring>> out;
    size_t pos = 0;
    while (pos < src.size()) {
        size_t end = src.find(L'\n', pos);
        if (end == std::wstring::npos) end = src.size();
        std::wstring line = src.substr(pos, end - pos);
        pos = end + 1;
        if (!line.empty() && line.back() == L'\r') line.pop_back();

        std::vector<long long> stamps;
        size_t i = 0;
        while (i < line.size() && line[i] == L'[') {
            size_t close = line.find(L']', i);
            if (close == std::wstring::npos) break;
            std::wstring tag = line.substr(i + 1, close - i - 1);
            size_t colon = tag.find(L':');
            if (colon == std::wstring::npos || colon == 0 || !iswdigit(tag[0])) { i = close + 1; continue; }
            try {
                long long mm = std::stoll(tag.substr(0, colon));
                double ss = std::stod(tag.substr(colon + 1));
                stamps.push_back(mm * 60000 + (long long)(ss * 1000.0));
            } catch (...) {}
            i = close + 1;
        }
        std::wstring text = line.substr(i);
        while (!text.empty() && text.front() == L' ') text.erase(text.begin());
        for (long long t : stamps) out.emplace_back(t, text);
    }
    std::stable_sort(out.begin(), out.end(),
                     [](auto& x, auto& y) { return x.first < y.first; });
    return out;
}

bool SyncedFrom(winrt::Windows::Data::Json::JsonObject const& o, std::wstring& lrc) {
    using namespace winrt::Windows::Data::Json;
    if (!o.HasKey(L"syncedLyrics")) return false;
    auto v = o.GetNamedValue(L"syncedLyrics");
    if (v.ValueType() != JsonValueType::String) return false;
    lrc = v.GetString().c_str();
    return !lrc.empty();
}

// Thread de travail : télécharge les paroles puis les publie si la requête est toujours d'actualité.
void FetchLyrics(std::shared_ptr<LyricsState> st, int gen, std::wstring title, std::wstring artist,
                 int durSec) {
    using namespace winrt::Windows::Data::Json;
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
    } catch (...) {}
    std::wstring lrc, body;
    const wchar_t* ua = L"Encoche/1.1";
    std::wstring q = L"track_name=" + UrlEscape(title) + L"&artist_name=" + UrlEscape(artist);

    // 1) correspondance exacte (titre + artiste + durée à +-2 s)
    std::wstring url = L"https://lrclib.net/api/get?" + q;
    if (durSec > 0) url += L"&duration=" + std::to_wstring(durSec);
    if (HttpGetString(url, body, ua)) {
        JsonObject o{nullptr};
        if (JsonObject::TryParse(body, o)) SyncedFrom(o, lrc);
    }
    // 2) recherche plus souple
    if (lrc.empty() && HttpGetString(L"https://lrclib.net/api/search?" + q, body, ua)) {
        JsonArray arr{nullptr};
        if (JsonArray::TryParse(body, arr)) {
            for (uint32_t i = 0; i < arr.Size() && lrc.empty(); ++i) {
                if (arr.GetAt(i).ValueType() == JsonValueType::Object)
                    SyncedFrom(arr.GetObjectAt(i), lrc);
            }
        }
    }
    auto lines = ParseLrc(lrc);
    if (st->gen.load() != gen) return;  // une requête plus récente a pris le relais
    std::lock_guard<std::mutex> lk(st->mx);
    if (st->gen.load() != gen) return;
    st->lines = std::move(lines);
    st->dirty = true;
}

}  // namespace

MusicModule::MusicModule() {
    try {
        mgr_ = Manager::RequestAsync().get();
        sessionChangedTok_ = mgr_.CurrentSessionChanged([this](auto&&, auto&&) {
            Attach(mgr_.GetCurrentSession());
            RefreshData();
        });
        Attach(mgr_.GetCurrentSession());
        RefreshData();
    } catch (...) {
        // Pas de contrôle média disponible : le module affichera "Aucun média".
    }
}

MusicModule::~MusicModule() {
    try {
        if (mgr_) mgr_.CurrentSessionChanged(sessionChangedTok_);
        Attach(nullptr);
    } catch (...) {}
}

void MusicModule::Attach(Session const& s) {
    std::lock_guard<std::mutex> lk(sessMx_);
    if (session_) {
        session_.MediaPropertiesChanged(mediaTok_);
        session_.PlaybackInfoChanged(playTok_);
        session_.TimelinePropertiesChanged(timelineTok_);
    }
    session_ = s;
    if (session_) {
        mediaTok_ = session_.MediaPropertiesChanged([this](auto&&, auto&&) { RefreshData(); });
        playTok_ = session_.PlaybackInfoChanged([this](auto&&, auto&&) { RefreshData(); });
        timelineTok_ = session_.TimelinePropertiesChanged([this](auto&&, auto&&) { RefreshData(); });
    }
}

void MusicModule::RefreshData() {
    std::lock_guard<std::mutex> rl(refreshMx_);
    try {
        Session s{nullptr};
        {
            std::lock_guard<std::mutex> lk(sessMx_);
            s = session_;
        }

        std::wstring title, artist;
        bool playing = false;
        bool keepArt = false;
        long long durMs = 0, posMs = 0;
        std::unique_ptr<Gdiplus::Bitmap> art;

        if (s) {
            auto props = s.TryGetMediaPropertiesAsync().get();
            title = props.Title().c_str();
            artist = props.Artist().c_str();
            playing = s.GetPlaybackInfo().PlaybackStatus() ==
                      GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;
            try {
                auto tl = s.GetTimelineProperties();
                durMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                            tl.EndTime() - tl.StartTime()).count();
                posMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                            tl.Position() - tl.StartTime()).count();
                if (playing) {  // la position date de LastUpdatedTime : on rattrape le temps écoulé
                    auto el = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  winrt::clock::now() - tl.LastUpdatedTime()).count();
                    if (el > 0 && el < 6LL * 3600 * 1000) posMs += el;
                }
                if (durMs < 0 || durMs > 24LL * 3600 * 1000) durMs = 0;
                posMs = std::clamp(posMs, 0LL, durMs > 0 ? durMs : posMs);
            } catch (...) {}
            {
                std::lock_guard<std::mutex> lk(stateMx_);
                keepArt = (title == title_ && artist == artist_ && art_ != nullptr);
            }
            if (!keepArt) {
                if (auto th = props.Thumbnail()) art = LoadArt(th);
            }
        }

        std::lock_guard<std::mutex> lk(stateMx_);
        hasSession_ = static_cast<bool>(s);
        playing_ = playing;
        title_ = std::move(title);
        artist_ = std::move(artist);
        durMs_ = durMs;
        posMs_ = posMs;
        posStamp_ = SteadyClock::now();
        if (!keepArt) art_ = std::move(art);

        // Nouveau morceau (ou durée enfin connue) : on (re)demande les paroles.
        std::wstring key = title_ + L"|" + artist_ + L"|" + std::to_wstring(durMs_ / 1000);
        if (key != lyricsKey_) {
            lyricsKey_ = key;
            int gen = ++lyrics_->gen;
            {
                std::lock_guard<std::mutex> ll(lyrics_->mx);
                lyrics_->lines.clear();
            }
            if (!title_.empty()) {
                std::thread(FetchLyrics, lyrics_, gen, title_, artist_, (int)(durMs_ / 1000)).detach();
            }
        }
        dirty_ = true;
    } catch (...) {
        // On garde l'ancien état.
    }
}

bool MusicModule::Tick() {
    bool d = dirty_.exchange(false);
    if (lyrics_->dirty.exchange(false)) d = true;
    if (open_ > 0.9f) {  // barre de progression / ligne de paroles : ~4 fois par seconde, seulement si visible
        auto now = SteadyClock::now();
        if (now - lastProg_ >= std::chrono::milliseconds(250)) {
            lastProg_ = now;
            std::lock_guard<std::mutex> lk(stateMx_);
            if (playing_ && durMs_ > 0) d = true;
        }
    }
    return d;
}

long long MusicModule::CurrentPosMs() const {
    long long p = posMs_;
    if (playing_)
        p += std::chrono::duration_cast<std::chrono::milliseconds>(SteadyClock::now() - posStamp_).count();
    return std::clamp(p, 0LL, durMs_ > 0 ? durMs_ : p);
}

MusicModule::ButtonLayout MusicModule::Layout(RectF a) {
    float x0 = a.X + 100, y = a.Y + 98;
    return {{x0 + 20, y}, {x0 + 70, y}, {x0 + 120, y}};
}

RectF MusicModule::BarRect(RectF a) { return RectF(a.X, a.Y + 126, a.Width, 5); }

void MusicModule::DrawArt(Gdiplus::Graphics& g, RectF r, float radius, float alpha) {
    Gdiplus::GraphicsPath path;
    AddRoundRect(path, r.X, r.Y, r.Width, r.Height, radius);
    auto st = g.Save();
    g.SetClip(&path, Gdiplus::CombineModeIntersect);
    if (art_) {
        DrawImageA(g, art_.get(), r, alpha);
    } else {
        Gdiplus::SolidBrush br(Rgba(60, 60, 60, alpha));
        g.FillRectangle(&br, r);
    }
    g.Restore(st);
}

void MusicModule::DrawCompact(Gdiplus::Graphics& g, RectF a, float al) {
    std::lock_guard<std::mutex> lk(stateMx_);
    if (!hasSession_) return;
    DrawArt(g, RectF(a.X + 2, a.Y + 1, 20, 20), 5, al);
    if (playing_) {  // trois petites barres "equalizer" statiques
        Gdiplus::SolidBrush br(Rgba(255, 255, 255, al));
        float x = a.GetRight() - 18, base = a.Y + 20;
        g.FillRectangle(&br, x, base - 7, 3.f, 7.f);
        g.FillRectangle(&br, x + 5, base - 13, 3.f, 13.f);
        g.FillRectangle(&br, x + 10, base - 9, 3.f, 9.f);
    }
}

void MusicModule::DrawExpanded(Gdiplus::Graphics& g, RectF a, float al) {
    std::lock_guard<std::mutex> lk(stateMx_);
    if (!hasSession_) {
        PutText(g, L"Aucun média en lecture", 14, false, Rgba(160, 160, 160, al),
                RectF(a.X, a.Y + 40, a.Width, 24), Gdiplus::StringAlignmentCenter);
        return;
    }
    DrawArt(g, RectF(a.X, a.Y + 2, 84, 84), 12, al);

    float tx = a.X + 100, tw = a.Width - 100 - 24;  // 24 px réservés à l'engrenage
    PutText(g, title_.empty() ? std::wstring(L"(sans titre)") : title_, 16, true,
            Rgba(255, 255, 255, al), RectF(tx, a.Y + 2, tw, 24));
    PutText(g, artist_, 13, false, Rgba(170, 170, 170, al), RectF(tx, a.Y + 27, tw, 20));

    long long pos = CurrentPosMs();
    if (Cfg().lyrics) {  // ligne de paroles courante
        std::lock_guard<std::mutex> ll(lyrics_->mx);
        auto& L = lyrics_->lines;
        if (!L.empty()) {
            auto it = std::upper_bound(L.begin(), L.end(), pos,
                                       [](long long v, auto& e) { return v < e.first; });
            if (it != L.begin()) {
                const std::wstring& txt = std::prev(it)->second;
                if (!txt.empty())
                    PutText(g, txt, 13, true, Accent(al), RectF(tx, a.Y + 52, a.Width - 100, 20));
            }
        }
    }

    auto L = Layout(a);
    Gdiplus::SolidBrush white(Rgba(255, 255, 255, al));
    g.FillEllipse(&white, L.play.X - 18, L.play.Y - 18, 36.f, 36.f);
    DrawPlayPause(g, L.play, playing_, Rgba(0, 0, 0, al));
    DrawSkip(g, L.prev, false, Rgba(255, 255, 255, al));
    DrawSkip(g, L.next, true, Rgba(255, 255, 255, al));

    if (durMs_ > 0) {  // barre de progression + temps
        RectF bar = BarRect(a);
        float frac = std::clamp((float)pos / (float)durMs_, 0.f, 1.f);
        Gdiplus::GraphicsPath track;
        AddRoundRect(track, bar.X, bar.Y, bar.Width, bar.Height, 2.5f);
        Gdiplus::SolidBrush bg(Rgba(255, 255, 255, al * 0.2f));
        g.FillPath(&bg, &track);
        if (frac > 0.f) {
            Gdiplus::GraphicsPath fill;
            float fw = std::max(bar.Width * frac, 5.f);
            AddRoundRect(fill, bar.X, bar.Y, fw, bar.Height, 2.5f);
            Gdiplus::SolidBrush fg(Accent(al));
            g.FillPath(&fg, &fill);
        }
        PutText(g, FormatTime(pos), 11, false, Rgba(150, 150, 150, al),
                RectF(a.X, a.Y + 134, 60, 16));
        PutText(g, FormatTime(durMs_), 11, false, Rgba(150, 150, 150, al),
                RectF(a.GetRight() - 60, a.Y + 134, 60, 16), Gdiplus::StringAlignmentFar);
    }
}

void MusicModule::OnClick(PointF p, RectF a) {
    auto L = Layout(a);
    auto hit = [&](PointF c, float r) {
        float dx = p.X - c.X, dy = p.Y - c.Y;
        return dx * dx + dy * dy <= r * r;
    };
    Session s{nullptr};
    {
        std::lock_guard<std::mutex> lk(sessMx_);
        s = session_;
    }
    if (!s) return;
    try {
        RectF bar = BarRect(a);
        long long dur;
        {
            std::lock_guard<std::mutex> lk(stateMx_);
            dur = durMs_;
        }
        if (dur > 0 && p.X >= bar.X && p.X <= bar.GetRight() && p.Y >= bar.Y - 8 && p.Y <= bar.Y + 12) {
            float frac = std::clamp((p.X - bar.X) / bar.Width, 0.f, 1.f);
            long long target = (long long)(frac * (float)dur);
            s.TryChangePlaybackPositionAsync(target * 10000);  // en ticks de 100 ns
            std::lock_guard<std::mutex> lk(stateMx_);
            posMs_ = target;
            posStamp_ = SteadyClock::now();
            dirty_ = true;
            return;
        }
        if (hit(L.play, 20)) s.TryTogglePlayPauseAsync();
        else if (hit(L.prev, 16)) s.TrySkipPreviousAsync();
        else if (hit(L.next, 16)) s.TrySkipNextAsync();
    } catch (...) {}
}
