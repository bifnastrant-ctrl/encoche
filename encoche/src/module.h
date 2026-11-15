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
#include <vector>

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

    // Appelé à chaque Tick avec le degré d'ouverture de l'encoche (0 = fermée, 1 = ouverte).
    // Permet par ex. de n'animer une barre de progression que quand elle est visible.
    virtual void SetOpenness(float /*t*/) {}
};

inline Gdiplus::Color Rgba(int r, int g, int b, float a) {
    return Gdiplus::Color((BYTE)(std::clamp(a, 0.f, 1.f) * 255.f), (BYTE)r, (BYTE)g, (BYTE)b);
}

// Police de l'interface : Inter (intégrée à l'exe, ressources 101 = Regular, 102 = SemiBold).
// Si elle ne peut pas être chargée, on retombe sur Segoe UI. Les objets ne sont jamais libérés
// exprès : GDI+ est arrêté avant la fin du processus.
struct AppFonts {
    Gdiplus::PrivateFontCollection* pfc = nullptr;
    Gdiplus::FontFamily* regular = nullptr;
    Gdiplus::FontFamily* semibold = nullptr;
    bool custom = false;
};

inline AppFonts& Fonts() {
    static AppFonts* f = [] {
        auto* af = new AppFonts;
        af->pfc = new Gdiplus::PrivateFontCollection();
        bool ok = true;
        for (int id : {101, 102}) {
            HRSRC r = FindResourceW(nullptr, MAKEINTRESOURCEW(id), RT_RCDATA);
            HGLOBAL h = r ? LoadResource(nullptr, r) : nullptr;
            void* data = h ? LockResource(h) : nullptr;
            if (!data || af->pfc->AddMemoryFont(data, (INT)SizeofResource(nullptr, r)) != Gdiplus::Ok)
                ok = false;
        }
        if (ok) {
            INT n = af->pfc->GetFamilyCount();
            if (n > 0) {
                std::vector<Gdiplus::FontFamily> fam((size_t)n);
                INT found = 0;
                if (af->pfc->GetFamilies(n, fam.data(), &found) == Gdiplus::Ok) {
                    for (INT i = 0; i < found; ++i) {
                        WCHAR name[LF_FACESIZE]{};
                        fam[(size_t)i].GetFamilyName(name);
                        if (wcscmp(name, L"Inter") == 0) af->regular = fam[(size_t)i].Clone();
                        else if (wcscmp(name, L"Inter SemiBold") == 0) af->semibold = fam[(size_t)i].Clone();
                    }
                }
            }
        }
        if (af->regular && af->semibold) {
            af->custom = true;
        } else {
            af->regular = new Gdiplus::FontFamily(L"Segoe UI");
            af->semibold = af->regular;
        }
        return af;
    }();
    return *f;
}

inline void PutText(Gdiplus::Graphics& g, const std::wstring& s, float px, bool bold,
                    Gdiplus::Color c, Gdiplus::RectF box,
                    Gdiplus::StringAlignment h = Gdiplus::StringAlignmentNear) {
    AppFonts& af = Fonts();
    const Gdiplus::FontFamily* fam = bold ? af.semibold : af.regular;
    Gdiplus::Font font(fam, px,
                       (!af.custom && bold) ? Gdiplus::FontStyleBold : Gdiplus::FontStyleRegular,
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
