#pragma once
// Module Raccourcis : une grille de boutons (dossiers, applications, fichiers, sites web).
// La liste se gère dans la fenêtre de réglages ; un clic ouvre la cible avec le programme associé.

#include "module.h"
#include "config.h"
#include <cwctype>
#include <memory>
#include <shellapi.h>
#include <vector>

class ShortcutsModule final : public INotchModule {
public:
    const wchar_t* Name() const override { return L"Raccourcis"; }

    bool Tick() override { return Refresh(); }

    void DrawCompact(Gdiplus::Graphics&, Gdiplus::RectF, float) override {}

    void DrawExpanded(Gdiplus::Graphics& g, Gdiplus::RectF a, float al) override {
        Refresh();
        const std::vector<Shortcut>& list = Cfg().shortcuts;
        if (list.empty()) {
            PutText(g, L"Aucun raccourci. Ajoute-en dans les réglages (engrenage).", 14, false,
                    Rgba(160, 160, 160, al), Gdiplus::RectF(a.X, a.Y + 50, a.Width, 24),
                    Gdiplus::StringAlignmentCenter);
            return;
        }
        size_t n = std::min(list.size(), kMaxShortcuts);
        for (size_t i = 0; i < n; ++i) {
            Gdiplus::RectF c = Cell(i, a);
            Gdiplus::GraphicsPath p;
            AddRoundRect(p, c.X, c.Y, c.Width, c.Height, 12.f);
            Gdiplus::SolidBrush bg(Rgba(38, 38, 42, al));
            g.FillPath(&bg, &p);

            Gdiplus::RectF ir(c.X + (c.Width - 30.f) / 2.f, c.Y + 8.f, 30.f, 30.f);
            Gdiplus::Bitmap* icon = i < icons_.size() ? icons_[i].get() : nullptr;
            if (icon) {
                DrawImageA(g, icon, ir, al);
            } else {
                Gdiplus::SolidBrush ab(Accent(al * 0.9f));
                g.FillEllipse(&ab, ir);
                std::wstring initial(1, list[i].name.empty() ? L'?' : (wchar_t)towupper(list[i].name[0]));
                PutText(g, initial, 16, true, Rgba(0, 0, 0, al),
                        Gdiplus::RectF(ir.X, ir.Y + 5.f, ir.Width, 22.f), Gdiplus::StringAlignmentCenter);
            }
            PutText(g, list[i].name, 11, false, Rgba(230, 230, 230, al),
                    Gdiplus::RectF(c.X + 4.f, c.Y + 44.f, c.Width - 8.f, 18.f),
                    Gdiplus::StringAlignmentCenter);
        }
    }

    void OnClick(Gdiplus::PointF p, Gdiplus::RectF a) override {
        const std::vector<Shortcut>& list = Cfg().shortcuts;
        size_t n = std::min(list.size(), kMaxShortcuts);
        for (size_t i = 0; i < n; ++i) {
            if (Cell(i, a).Contains(p)) {
                ShellExecuteW(nullptr, L"open", list[i].target.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                return;
            }
        }
    }

private:
    // 4 colonnes x 2 lignes ; on laisse la place de l'engrenage à droite.
    static Gdiplus::RectF Cell(size_t i, Gdiplus::RectF a) {
        float cw = (a.Width - 30.f) / 4.f, ch = 70.f;
        return Gdiplus::RectF(a.X + (float)(i % 4) * cw, a.Y + 4.f + (float)(i / 4) * (ch + 6.f),
                              cw - 6.f, ch);
    }

    // Recharge les icônes quand la liste a changé. Retourne true si l'affichage doit être redessiné.
    bool Refresh() {
        if (version_ == Cfg().version) return false;
        version_ = Cfg().version;
        icons_.clear();
        for (const Shortcut& s : Cfg().shortcuts) icons_.push_back(LoadIconBitmap(s.target));
        return true;
    }

    static std::unique_ptr<Gdiplus::Bitmap> LoadIconBitmap(const std::wstring& target) {
        if (target.find(L"://") != std::wstring::npos) return nullptr;  // site web : pas d'icône de fichier
        SHFILEINFOW sfi{};
        if (!SHGetFileInfoW(target.c_str(), 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_LARGEICON) ||
            !sfi.hIcon)
            return nullptr;
        std::unique_ptr<Gdiplus::Bitmap> bmp(Gdiplus::Bitmap::FromHICON(sfi.hIcon));
        DestroyIcon(sfi.hIcon);
        if (bmp && bmp->GetLastStatus() != Gdiplus::Ok) bmp.reset();
        return bmp;
    }

    int version_ = -1;
    std::vector<std::unique_ptr<Gdiplus::Bitmap>> icons_;
};
