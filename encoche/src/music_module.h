#pragma once
#include "module.h"
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>
#include <atomic>
#include <memory>
#include <mutex>

// Lit le contrôle média système de Windows (SMTC) : fonctionne avec Spotify,
// YouTube dans le navigateur, Apple Music, etc., sans clé d'API.
class MusicModule final : public INotchModule {
public:
    MusicModule();
    ~MusicModule() override;

    const wchar_t* Name() const override { return L"Musique"; }
    bool Tick() override { return dirty_.exchange(false); }
    void DrawCompact(Gdiplus::Graphics& g, Gdiplus::RectF area, float alpha) override;
    void DrawExpanded(Gdiplus::Graphics& g, Gdiplus::RectF area, float alpha) override;
    void OnClick(Gdiplus::PointF p, Gdiplus::RectF area) override;

private:
    using Manager = winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager;
    using Session = winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession;

    struct ButtonLayout { Gdiplus::PointF prev, play, next; };
    static ButtonLayout Layout(Gdiplus::RectF a);

    void Attach(Session const& s);
    void RefreshData();
    void DrawArt(Gdiplus::Graphics& g, Gdiplus::RectF r, float radius, float alpha);

    Manager mgr_{nullptr};
    Session session_{nullptr};
    winrt::event_token sessionChangedTok_{};
    winrt::event_token mediaTok_{};
    winrt::event_token playTok_{};

    std::mutex sessMx_;     // protège session_
    std::mutex refreshMx_;  // sérialise RefreshData
    std::mutex stateMx_;    // protège l'état affiché ci-dessous
    bool hasSession_ = false;
    bool playing_ = false;
    std::wstring title_, artist_;
    std::unique_ptr<Gdiplus::Bitmap> art_;
    std::atomic<bool> dirty_{true};
};
