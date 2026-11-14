#pragma once
#include "module.h"

// Module d'exemple sans dépendance : l'heure. Sert aussi de modèle pour vos futurs modules.
class ClockModule final : public INotchModule {
public:
    const wchar_t* Name() const override { return L"Horloge"; }

    bool Tick() override {
        SYSTEMTIME st;
        GetLocalTime(&st);
        if (st.wMinute != lastMinute_) {
            lastMinute_ = st.wMinute;
            return true;
        }
        return false;
    }

    void DrawCompact(Gdiplus::Graphics& g, Gdiplus::RectF a, float al) override {
        PutText(g, TimeStr(), 14, true, Rgba(255, 255, 255, al), RectF2(a, 4), Gdiplus::StringAlignmentCenter);
    }

    void DrawExpanded(Gdiplus::Graphics& g, Gdiplus::RectF a, float al) override {
        PutText(g, TimeStr(), 52, true, Rgba(255, 255, 255, al),
                Gdiplus::RectF(a.X, a.Y + 6, a.Width, 70), Gdiplus::StringAlignmentCenter);
        wchar_t buf[96]{};
        SYSTEMTIME st;
        GetLocalTime(&st);
        GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_LONGDATE, &st, nullptr, buf, 96, nullptr);
        PutText(g, buf, 14, false, Rgba(170, 170, 170, al),
                Gdiplus::RectF(a.X, a.Y + 78, a.Width, 22), Gdiplus::StringAlignmentCenter);
    }

    void OnClick(Gdiplus::PointF, Gdiplus::RectF) override {}

private:
    static Gdiplus::RectF RectF2(Gdiplus::RectF a, float dy) {
        return Gdiplus::RectF(a.X, a.Y + dy, a.Width, a.Height);
    }
    static std::wstring TimeStr() {
        SYSTEMTIME st;
        GetLocalTime(&st);
        wchar_t b[16];
        swprintf_s(b, L"%02d:%02d", st.wHour, st.wMinute);
        return b;
    }
    WORD lastMinute_ = 0xFFFF;
};
