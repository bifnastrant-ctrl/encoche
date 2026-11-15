#pragma once
// Page « Réglages » (affichée quand on clique sur l'engrenage) + petite boîte de saisie.

#include "config.h"
#include <vector>

namespace settings_detail {

struct PromptCtx {
    std::wstring* value;
    bool ok = false;
    bool done = false;
};

inline LRESULT CALLBACK PromptProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    auto* ctx = reinterpret_cast<PromptCtx*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    switch (msg) {
    case WM_NCCREATE: {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(l);
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        return DefWindowProcW(h, msg, w, l);
    }
    case WM_COMMAND:
        if (LOWORD(w) == IDOK && ctx) {
            wchar_t buf[512]{};
            GetWindowTextW(GetDlgItem(h, 100), buf, 512);
            *ctx->value = buf;
            ctx->ok = true;
            DestroyWindow(h);
            return 0;
        }
        if (LOWORD(w) == IDCANCEL) {
            DestroyWindow(h);
            return 0;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        if (ctx) ctx->done = true;
        return 0;
    }
    return DefWindowProcW(h, msg, w, l);
}

}  // namespace settings_detail

// Boîte de saisie modale minimale. Retourne true si l'utilisateur valide.
inline bool PromptText(const wchar_t* title, const wchar_t* label, std::wstring& value) {
    using namespace settings_detail;
    static bool registered = false;
    HINSTANCE hi = GetModuleHandleW(nullptr);
    if (!registered) {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc = PromptProc;
        wc.hInstance = hi;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"EnclochePrompt";
        RegisterClassExW(&wc);
        registered = true;
    }
    PromptCtx ctx;
    std::wstring initial = value;
    ctx.value = &value;

    const int W = 460, H = 170;
    int x = (GetSystemMetrics(SM_CXSCREEN) - W) / 2;
    int y = (GetSystemMetrics(SM_CYSCREEN) - H) / 3;
    HWND hw = CreateWindowExW(WS_EX_TOPMOST | WS_EX_DLGMODALFRAME, L"EnclochePrompt", title,
                              WS_POPUP | WS_CAPTION | WS_SYSMENU, x, y, W, H, nullptr, nullptr,
                              hi, &ctx);
    if (!hw) return false;
    HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    auto mk = [&](const wchar_t* cls, const wchar_t* text, DWORD style, int cx, int cy, int cw,
                  int ch, int id) {
        HWND c = CreateWindowExW(cls[0] == L'E' ? WS_EX_CLIENTEDGE : 0, cls, text,
                                 WS_CHILD | WS_VISIBLE | style, cx, cy, cw, ch, hw,
                                 (HMENU)(INT_PTR)id, hi, nullptr);
        SendMessageW(c, WM_SETFONT, (WPARAM)font, TRUE);
        return c;
    };
    mk(L"STATIC", label, 0, 12, 10, W - 40, 40, 101);
    HWND edit = mk(L"EDIT", initial.c_str(), WS_TABSTOP | ES_AUTOHSCROLL, 12, 56, W - 40, 24, 100);
    mk(L"BUTTON", L"OK", WS_TABSTOP | BS_DEFPUSHBUTTON, W - 200, 94, 84, 28, IDOK);
    mk(L"BUTTON", L"Annuler", WS_TABSTOP | BS_PUSHBUTTON, W - 108, 94, 84, 28, IDCANCEL);
    ShowWindow(hw, SW_SHOW);
    SetForegroundWindow(hw);
    SetFocus(edit);
    SendMessageW(edit, EM_SETSEL, 0, -1);

    MSG msg;
    while (!ctx.done && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(hw, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    if (!ctx.ok) value = initial;
    return ctx.ok;
}

class SettingsPage final : public INotchModule {
public:
    const wchar_t* Name() const override { return L"Réglages"; }
    bool Tick() override { return false; }
    void DrawCompact(Gdiplus::Graphics&, Gdiplus::RectF, float) override {}

    void DrawExpanded(Gdiplus::Graphics& g, Gdiplus::RectF a, float al) override {
        PutText(g, L"Réglages", 15, true, Rgba(255, 255, 255, al),
                Gdiplus::RectF(a.X, a.Y - 2, 200, 22));
        const Config& c = Cfg();
        auto label = [&](const wchar_t* s, float y) {
            PutText(g, s, 12, false, Rgba(170, 170, 170, al),
                    Gdiplus::RectF(a.X, a.Y + y + 3, 80, 18));
        };
        label(L"Couleur", 28);
        label(L"Coins", 56);
        label(L"Options", 84);

        for (const Item& it : Items(a)) {
            switch (it.kind) {
            case Swatch: {
                Gdiplus::SolidBrush br(AccentColor(it.arg, al));
                g.FillEllipse(&br, it.r.X, it.r.Y, it.r.Width, it.r.Height);
                if (it.arg == c.accent) {
                    Gdiplus::Pen pen(Rgba(255, 255, 255, al), 2.f);
                    g.DrawEllipse(&pen, it.r.X - 3, it.r.Y - 3, it.r.Width + 6, it.r.Height + 6);
                }
                break;
            }
            case Radius: {
                static const wchar_t* names[3] = {L"Doux", L"Rond", L"Max"};
                Chip(g, it.r, names[it.arg], c.radius == it.arg, al);
                break;
            }
            case ToggleLyrics: Toggle(g, it.r, L"Paroles", c.lyrics, al); break;
            case Toggle24: Toggle(g, it.r, L"Heure 24 h", c.clock24, al); break;
            case ToggleStartup: Toggle(g, it.r, L"Lancer avec Windows", startup_, al); break;
            case Mosque: {
                std::wstring t = c.mosque.empty() ? L"Mosquée : choisir…" : L"Mosquée : " + c.mosque;
                Chip(g, it.r, t.c_str(), false, al);
                break;
            }
            }
        }
    }

    void OnClick(Gdiplus::PointF p, Gdiplus::RectF a) override {
        for (const Item& it : Items(a)) {
            if (!it.r.Contains(p)) continue;
            Config& c = Cfg();
            switch (it.kind) {
            case Swatch: c.accent = it.arg; break;
            case Radius: c.radius = it.arg; break;
            case ToggleLyrics: c.lyrics = !c.lyrics; break;
            case Toggle24: c.clock24 = !c.clock24; break;
            case ToggleStartup:
                startup_ = !IsStartupEnabled();
                SetStartup(startup_);
                startup_ = IsStartupEnabled();
                break;
            case Mosque: {
                std::wstring v = c.mosque;
                if (PromptText(L"Encoche - Mosquée",
                               L"Colle l'adresse Mawaqit de ta mosquée (ex. https://mawaqit.net/fr/nom-de-la-mosquee) :",
                               v)) {
                    c.mosque = ExtractSlug(v);
                }
                break;
            }
            }
            SaveConfig();
            return;
        }
    }

    void SetOpenness(float t) override {
        if (t > 0.5f && !wasOpen_) startup_ = IsStartupEnabled();  // relit le registre à l'ouverture
        wasOpen_ = t > 0.5f;
    }

private:
    enum Kind { Swatch, Radius, ToggleLyrics, Toggle24, ToggleStartup, Mosque };
    struct Item { Kind kind; int arg; Gdiplus::RectF r; };

    static std::vector<Item> Items(Gdiplus::RectF a) {
        std::vector<Item> v;
        for (int i = 0; i < kAccentCount; ++i)
            v.push_back({Swatch, i, {a.X + 90 + i * 30.f, a.Y + 30, 16, 16}});
        for (int i = 0; i < 3; ++i)
            v.push_back({Radius, i, {a.X + 90 + i * 66.f, a.Y + 56, 60, 22}});
        v.push_back({ToggleLyrics, 0, {a.X + 90, a.Y + 84, 100, 22}});
        v.push_back({Toggle24, 0, {a.X + 196, a.Y + 84, 110, 22}});
        v.push_back({ToggleStartup, 0, {a.X, a.Y + 114, 190, 22}});
        v.push_back({Mosque, 0, {a.X + 196, a.Y + 114, a.Width - 196, 22}});
        return v;
    }

    static std::wstring ExtractSlug(std::wstring s) {
        size_t q = s.find_first_of(L"?#");
        if (q != std::wstring::npos) s.erase(q);
        while (!s.empty() && (s.back() == L'/' || s.back() == L' ')) s.pop_back();
        size_t sl = s.find_last_of(L'/');
        if (sl != std::wstring::npos) s = s.substr(sl + 1);
        while (!s.empty() && s.front() == L' ') s.erase(s.begin());
        return s;
    }

    static void Chip(Gdiplus::Graphics& g, Gdiplus::RectF r, const wchar_t* text, bool on, float al) {
        Gdiplus::GraphicsPath p;
        AddRoundRect(p, r.X, r.Y, r.Width, r.Height, 8);
        Gdiplus::SolidBrush bg(on ? Accent(al * 0.9f) : Rgba(50, 50, 50, al));
        g.FillPath(&bg, &p);
        PutText(g, text, 12, on, on ? Rgba(0, 0, 0, al) : Rgba(230, 230, 230, al),
                Gdiplus::RectF(r.X + 4, r.Y + 3, r.Width - 8, r.Height - 4),
                Gdiplus::StringAlignmentCenter);
    }

    static void Toggle(Gdiplus::Graphics& g, Gdiplus::RectF r, const wchar_t* text, bool on, float al) {
        Gdiplus::GraphicsPath p;
        AddRoundRect(p, r.X, r.Y + 3, 30, 16, 8);
        Gdiplus::SolidBrush bg(on ? Accent(al) : Rgba(70, 70, 70, al));
        g.FillPath(&bg, &p);
        Gdiplus::SolidBrush knob(Rgba(255, 255, 255, al));
        g.FillEllipse(&knob, r.X + (on ? 16.f : 2.f), r.Y + 5.f, 12.f, 12.f);
        PutText(g, text, 12, false, Rgba(230, 230, 230, al),
                Gdiplus::RectF(r.X + 38, r.Y + 3, r.Width - 38, 18));
    }

    bool startup_ = false;
    bool wasOpen_ = false;
};
