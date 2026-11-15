#pragma once
#include "module.h"
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <vector>

// Paroles synchronisées (LRCLIB). Partagé avec le thread de téléchargement : si le module est
// détruit pendant une requête, le thread garde l'objet en vie via son shared_ptr.
struct LyricsState {
    std::mutex mx;
    std::vector<std::pair<long long, std::wstring>> lines;  // (ms, texte), triées
    std::atomic<int> gen{0};      // identifiant de la requête en cours (les anciennes sont ignorées)
    std::atomic<bool> dirty{false};
};

// Lit le contrôle média système de Windows (SMTC) : fonctionne avec Spotify,
// YouTube dans le navigateur, Apple Music, etc., sans clé d'API.
class MusicModule final : public INotchModule {
public:
    MusicModule();
    ~MusicModule() override;

    const wchar_t* Name() const override { return L"Musique"; }
    bool Tick() override;
    void DrawCompact(Gdiplus::Graphics& g, Gdiplus::RectF area, float alpha) override;
    void DrawExpanded(Gdiplus::Graphics& g, Gdiplus::RectF area, float alpha) override;
    void OnClick(Gdiplus::PointF p, Gdiplus::RectF area) override;
    void SetOpenness(float t) override { open_ = t; }

private:
    using Manager = winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager;
    using Session = winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession;
    using SteadyClock = std::chrono::steady_clock;

    struct ButtonLayout { Gdiplus::PointF prev, play, next; };
    static ButtonLayout Layout(Gdiplus::RectF a);
    static Gdiplus::RectF BarRect(Gdiplus::RectF a);

    void Attach(Session const& s);
    void RefreshData();
    void DrawArt(Gdiplus::Graphics& g, Gdiplus::RectF r, float radius, float alpha);
    long long CurrentPosMs() const;  // stateMx_ doit être tenu

    Manager mgr_{nullptr};
    Session session_{nullptr};
    winrt::event_token sessionChangedTok_{};
    winrt::event_token mediaTok_{};
    winrt::event_token playTok_{};
    winrt::event_token timelineTok_{};

    std::mutex sessMx_;     // protège session_
    std::mutex refreshMx_;  // sérialise RefreshData
    std::mutex stateMx_;    // protège l'état affiché ci-dessous
    bool hasSession_ = false;
    bool playing_ = false;
    std::wstring title_, artist_;
    std::unique_ptr<Gdiplus::Bitmap> art_;
    long long durMs_ = 0;           // 0 = inconnue (direct, etc.)
    long long posMs_ = 0;           // position à l'instant posStamp_
    SteadyClock::time_point posStamp_ = SteadyClock::now();
    std::wstring lyricsKey_;        // titre|artiste|durée de la dernière requête LRCLIB

    std::shared_ptr<LyricsState> lyrics_ = std::make_shared<LyricsState>();
    std::atomic<bool> dirty_{true};
    float open_ = 0.f;
    SteadyClock::time_point lastProg_ = SteadyClock::now();
};
