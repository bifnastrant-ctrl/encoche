#include "music_module.h"
#include <shlwapi.h>
#include <vector>
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
    }
    session_ = s;
    if (session_) {
        mediaTok_ = session_.MediaPropertiesChanged([this](auto&&, auto&&) { RefreshData(); });
        playTok_ = session_.PlaybackInfoChanged([this](auto&&, auto&&) { RefreshData(); });
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
        std::unique_ptr<Gdiplus::Bitmap> art;

        if (s) {
            auto props = s.TryGetMediaPropertiesAsync().get();
            title = props.Title().c_str();
            artist = props.Artist().c_str();
            playing = s.GetPlaybackInfo().PlaybackStatus() ==
                      GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;
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
        if (!keepArt) art_ = std::move(art);
        dirty_ = true;
    } catch (...) {
        // On garde l'ancien état.
    }
}

MusicModule::ButtonLayout MusicModule::Layout(RectF a) {
    float x0 = a.X + 100, y = a.Y + 84;
    return {{x0 + 20, y}, {x0 + 70, y}, {x0 + 120, y}};
}

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
    DrawArt(g, RectF(a.X, a.Y + 12, 84, 84), 12, al);

    float tx = a.X + 100, tw = a.Width - 100;
    PutText(g, title_.empty() ? std::wstring(L"(sans titre)") : title_, 16, true,
            Rgba(255, 255, 255, al), RectF(tx, a.Y + 8, tw, 24));
    PutText(g, artist_, 13, false, Rgba(170, 170, 170, al), RectF(tx, a.Y + 34, tw, 20));

    auto L = Layout(a);
    Gdiplus::SolidBrush white(Rgba(255, 255, 255, al));
    g.FillEllipse(&white, L.play.X - 18, L.play.Y - 18, 36.f, 36.f);
    DrawPlayPause(g, L.play, playing_, Rgba(0, 0, 0, al));
    DrawSkip(g, L.prev, false, Rgba(255, 255, 255, al));
    DrawSkip(g, L.next, true, Rgba(255, 255, 255, al));
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
        if (hit(L.play, 20)) s.TryTogglePlayPauseAsync();
        else if (hit(L.prev, 16)) s.TrySkipPreviousAsync();
        else if (hit(L.next, 16)) s.TrySkipNextAsync();
    } catch (...) {}
}
