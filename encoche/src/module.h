#pragma once
// Interface commune à tous les "modules" (pages) de l'encoche + petits outils de dessin.
// Pour brancher une nouvelle appli (Mawaqit, météo, calendrier...) : créer une classe
// qui hérite de INotchModule et l'ajouter dans main.cpp.

#include <algorithm>
using std::min;   // gdiplus.h a besoin de min/max globaux quand NOMINMAX est défini
using std::max;

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <string>
#include <cmath>

struct INotchModule {
    virtual ~INotchModule() = default;
    virtual const wchar_t* Name() const = 0;

    // Appelé régulièrement. Retourne true si l'affichage doit être redessiné.
    virtual bool Tick() = 0;

    // Dessin en coordonnées logiques (indépendantes du DPI). alpha : 0..1 (fondu).
    virtual void DrawCompact(Gdiplus::Graphics& g, Gdiplus::RectF area, float alpha) = 0;
    virtual void DrawExpanded(Gdiplus::Graphics& g, Gdiplus::RectF area, float alpha) = 0;

    // Clic dans la vue étendue. p est relatif au coin haut-gauche de "area".
    virtual void OnClick(Gdiplus::PointF p, Gdiplus::RectF area) = 0;
};

inline Gdiplus::Color Rgba(int r, int g, int b, float a) {
    return Gdiplus::Color((BYTE)(std::clamp(a, 0.f, 1.f) * 255.f), (BYTE)r, (BYTE)g, (BYTE)b);
}

inline void PutText(Gdiplus::Graphics& g, const std::wstring& s, float px, bool bold,
                    Gdiplus::Color c, Gdiplus::RectF box,
                    Gdiplus::StringAlignment h = Gdiplus::StringAlignmentNear) {
    Gdiplus::FontFamily fam(L"Segoe UI");
    Gdiplus::Font font(&fam, px, bold ? Gdiplus::FontStyleBold : Gdiplus::FontStyleRegular,
                       Gdiplus::UnitPixel);
    Gdiplus::StringFormat fmt;
    fmt.SetAlignment(h);
    fmt.SetLineAlignment(Gdiplus::StringAlignmentNear);
    fmt.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
    fmt.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);
    Gdiplus::SolidBrush br(c);
    g.DrawString(s.c_str(), -1, &font, box, &fmt, &br);
}

inline void AddRoundRect(Gdiplus::GraphicsPath& p, float x, float y, float w, float h, float r) {
    float d = r * 2;
    p.AddArc(x, y, d, d, 180, 90);
    p.AddArc(x + w - d, y, d, d, 270, 90);
    p.AddArc(x + w - d, y + h - d, d, d, 0, 90);
    p.AddArc(x, y + h - d, d, d, 90, 90);
    p.CloseFigure();
}

inline void DrawImageA(Gdiplus::Graphics& g, Gdiplus::Image* img, Gdiplus::RectF dst, float alpha) {
    Gdiplus::ColorMatrix cm = {1, 0, 0, 0, 0,
                               0, 1, 0, 0, 0,
                               0, 0, 1, 0, 0,
                               0, 0, 0, alpha, 0,
                               0, 0, 0, 0, 1};
    Gdiplus::ImageAttributes ia;
    ia.SetColorMatrix(&cm);
    g.DrawImage(img, dst, 0.f, 0.f, (float)img->GetWidth(), (float)img->GetHeight(),
                Gdiplus::UnitPixel, &ia);
}
