#pragma once
// Fenêtre de réglages d'Encoche : une vraie fenêtre (menu à gauche, contenu à droite),
// dessinée en GDI+ comme le reste. Remplace l'ancienne page « Réglages » de l'encoche.

#include "config.h"
#include "clock_module.h"
#include <dwmapi.h>
#include <shobjidl.h>
#include <cwctype>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

// ---------------------------------------------------------------------------------------------
// Petite boîte de saisie modale (une ligne de texte).
// ---------------------------------------------------------------------------------------------
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

// Retourne true si l'utilisateur valide.
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

// ---------------------------------------------------------------------------------------------
// Fenêtre de réglages.
// ---------------------------------------------------------------------------------------------
class SettingsWindow {
public:
    // Appelé après chaque modification enregistrée (l'encoche se redessine).
    std::function<void()> onChange;

    bool IsOpen() const { return hwnd_ != nullptr; }

    void Show() {
        startup_ = IsStartupEnabled();
        if (hwnd_) {
            if (IsIconic(hwnd_)) ShowWindow(hwnd_, SW_RESTORE);
            ShowWindow(hwnd_, SW_SHOW);
            SetForegroundWindow(hwnd_);
            return;
        }
        HINSTANCE hi = GetModuleHandleW(nullptr);
        static bool registered = false;
        if (!registered) {
            WNDCLASSEXW wc{sizeof(wc)};
            wc.lpfnWndProc = &SettingsWindow::Proc;
            wc.hInstance = hi;
            wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
            wc.hbrBackground = nullptr;
            wc.lpszClassName = L"EncocheSettings";
            RegisterClassExW(&wc);
            registered = true;
        }
        const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
        UINT dpi = GetDpiForSystem();
        RECT r{0, 0, (LONG)(kW * dpi / 96.f), (LONG)(kH * dpi / 96.f)};
        AdjustWindowRectExForDpi(&r, style, FALSE, 0, dpi);
        hwnd_ = CreateWindowExW(0, L"EncocheSettings", L"Encoche - Réglages", style, CW_USEDEFAULT,
                                CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, nullptr, nullptr,
                                hi, this);
        if (!hwnd_) return;
        BOOL dark = TRUE;
        DwmSetWindowAttribute(hwnd_, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));

        UINT wd = GetDpiForWindow(hwnd_);
        scale_ = (wd ? wd : 96) / 96.f;
        if (wd && wd != dpi) {  // l'écran n'a pas le DPI du système : on ajuste la taille
            RECT r2{0, 0, (LONG)(kW * scale_), (LONG)(kH * scale_)};
            AdjustWindowRectExForDpi(&r2, style, FALSE, 0, wd);
            SetWindowPos(hwnd_, nullptr, 0, 0, r2.right - r2.left, r2.bottom - r2.top,
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
        ShowWindow(hwnd_, SW_SHOW);
        UpdateWindow(hwnd_);
        SetForegroundWindow(hwnd_);
    }

private:
    static constexpr float kW = 780.f, kH = 520.f, kSide = 200.f;
    static constexpr int kTabCount = 5;
    static constexpr UINT kMsgPicked = WM_APP + 10;

    enum Act {
        ActTab, ActPageToggle, ActPageUp, ActPageDown, ActClock24, ActStartup, ActAccent,
        ActRadius, ActLyrics, ActMosque, ActAddFolder, ActAddFile, ActAddUrl, ActScUp, ActScDown,
        ActScRename, ActScDelete
    };
    struct Hit {
        Gdiplus::RectF r;
        int act;
        int arg;
    };

    HWND hwnd_ = nullptr;
    float scale_ = 1.f;
    int tab_ = 0;
    bool startup_ = false;
    std::vector<Hit> hits_;

    // ---------------------------------------------------------------- fenêtre -------------
    static LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM w, LPARAM l) {
        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(l);
            auto* self = reinterpret_cast<SettingsWindow*>(cs->lpCreateParams);
            SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)self);
            if (self) self->hwnd_ = h;
            return DefWindowProcW(h, msg, w, l);
        }
        auto* self = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (!self) return DefWindowProcW(h, msg, w, l);

        switch (msg) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(h, &ps);
            self->Paint(hdc);
            EndPaint(h, &ps);
            return 0;
        }
        case WM_LBUTTONUP:
            self->OnClick(GET_X_LPARAM_(l) / self->scale_, GET_Y_LPARAM_(l) / self->scale_);
            return 0;
        case WM_DPICHANGED: {
            self->scale_ = HIWORD(w) / 96.f;
            const RECT* r = reinterpret_cast<const RECT*>(l);
            SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        case kMsgPicked: {
            std::unique_ptr<std::wstring> path(reinterpret_cast<std::wstring*>(l));
            EnableWindow(h, TRUE);
            SetForegroundWindow(h);
            if (path && !path->empty()) self->AddShortcut(NameFromPath(*path), *path);
            return 0;
        }
        case WM_CLOSE:
            DestroyWindow(h);
            return 0;
        case WM_DESTROY:
            self->hwnd_ = nullptr;
            return 0;
        }
        return DefWindowProcW(h, msg, w, l);
    }

    static int GET_X_LPARAM_(LPARAM l) { return (int)(short)LOWORD(l); }
    static int GET_Y_LPARAM_(LPARAM l) { return (int)(short)HIWORD(l); }

    void Invalidate() {
        if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
    }

    void Commit() {
        SaveConfig();
        if (onChange) onChange();
        Invalidate();
    }

    // ---------------------------------------------------------------- actions -------------
    void OnClick(float x, float y) {
        Hit hit{};
        bool found = false;
        for (const Hit& h : hits_) {
            if (h.r.Contains(Gdiplus::PointF(x, y))) {
                hit = h;
                found = true;
                break;
            }
        }
        if (!found) return;

        Config& c = Cfg();
        switch (hit.act) {
        case ActTab:
            tab_ = hit.arg;
            Invalidate();
            break;
        case ActPageToggle:
            c.pageOn[hit.arg] = !c.pageOn[hit.arg];
            Commit();
            break;
        case ActPageUp:
            if (hit.arg > 0) std::swap(c.pageOrder[hit.arg], c.pageOrder[hit.arg - 1]);
            Commit();
            break;
        case ActPageDown:
            if (hit.arg < kPageCount - 1) std::swap(c.pageOrder[hit.arg], c.pageOrder[hit.arg + 1]);
            Commit();
            break;
        case ActClock24:
            c.clock24 = !c.clock24;
            Commit();
            break;
        case ActStartup:
            SetStartup(!IsStartupEnabled());
            startup_ = IsStartupEnabled();
            Invalidate();
            break;
        case ActAccent:
            c.accent = hit.arg;
            Commit();
            break;
        case ActRadius:
            c.radius = hit.arg;
            Commit();
            break;
        case ActLyrics:
            c.lyrics = !c.lyrics;
            Commit();
            break;
        case ActMosque: {
            std::wstring v = c.mosque;
            EnableWindow(hwnd_, FALSE);
            bool ok = PromptText(L"Encoche - Mosquée",
                                 L"Colle l'adresse Mawaqit de ta mosquée (ex. https://mawaqit.net/fr/nom-de-la-mosquee) :",
                                 v);
            EnableWindow(hwnd_, TRUE);
            SetForegroundWindow(hwnd_);
            if (ok) {
                c.mosque = ExtractSlug(v);
                Commit();
            }
            break;
        }
        case ActAddFolder:
            StartPick(true);
            break;
        case ActAddFile:
            StartPick(false);
            break;
        case ActAddUrl: {
            if (c.shortcuts.size() >= kMaxShortcuts) break;
            std::wstring v;
            EnableWindow(hwnd_, FALSE);
            bool ok = PromptText(L"Encoche - Site web", L"Adresse du site (ex. https://youtube.com) :", v);
            EnableWindow(hwnd_, TRUE);
            SetForegroundWindow(hwnd_);
            v = Trim(v);
            if (ok && !v.empty()) {
                if (v.find(L"://") == std::wstring::npos) v = L"https://" + v;
                AddShortcut(HostName(v), v);
            }
            break;
        }
        case ActScUp:
            if (hit.arg > 0 && (size_t)hit.arg < c.shortcuts.size())
                std::swap(c.shortcuts[hit.arg], c.shortcuts[hit.arg - 1]);
            Commit();
            break;
        case ActScDown:
            if ((size_t)hit.arg + 1 < c.shortcuts.size())
                std::swap(c.shortcuts[hit.arg], c.shortcuts[hit.arg + 1]);
            Commit();
            break;
        case ActScRename:
            if ((size_t)hit.arg < c.shortcuts.size()) {
                std::wstring v = c.shortcuts[hit.arg].name;
                EnableWindow(hwnd_, FALSE);
                bool ok = PromptText(L"Encoche - Raccourci", L"Nouveau nom :", v);
                EnableWindow(hwnd_, TRUE);
                SetForegroundWindow(hwnd_);
                v = Trim(v);
                if (ok && !v.empty() && (size_t)hit.arg < c.shortcuts.size()) {
                    c.shortcuts[hit.arg].name = v;
                    Commit();
                }
            }
            break;
        case ActScDelete:
            if ((size_t)hit.arg < c.shortcuts.size()) c.shortcuts.erase(c.shortcuts.begin() + hit.arg);
            Commit();
            break;
        }
    }

    void AddShortcut(const std::wstring& name, const std::wstring& target) {
        Config& c = Cfg();
        if (c.shortcuts.size() >= kMaxShortcuts) return;
        c.shortcuts.push_back({name.empty() ? target : name, target});
        Commit();
    }

    // Sélecteur de fichier/dossier : lancé dans un thread à part (apartment STA, requis par la boîte
    // de dialogue). Le résultat revient par un message, la fenêtre est désactivée en attendant.
    void StartPick(bool folder) {
        if (Cfg().shortcuts.size() >= kMaxShortcuts || !hwnd_) return;
        EnableWindow(hwnd_, FALSE);
        std::thread(&SettingsWindow::PickThread, hwnd_, folder).detach();
    }

    static void PickThread(HWND owner, bool folder) {
        std::wstring* result = nullptr;
        if (SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {
            IFileOpenDialog* dlg = nullptr;
            if (SUCCEEDED(CoCreateInstance(__uuidof(FileOpenDialog), nullptr, CLSCTX_INPROC_SERVER,
                                           IID_PPV_ARGS(&dlg)))) {
                DWORD opt = 0;
                dlg->GetOptions(&opt);
                opt |= FOS_FORCEFILESYSTEM | FOS_NODEREFERENCELINKS | (folder ? FOS_PICKFOLDERS : 0);
                dlg->SetOptions(opt);
                dlg->SetTitle(folder ? L"Choisir un dossier" : L"Choisir une application ou un fichier");
                if (SUCCEEDED(dlg->Show(owner))) {
                    IShellItem* item = nullptr;
                    if (SUCCEEDED(dlg->GetResult(&item))) {
                        PWSTR path = nullptr;
                        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                            result = new std::wstring(path);
                            CoTaskMemFree(path);
                        }
                        item->Release();
                    }
                }
                dlg->Release();
            }
            CoUninitialize();
        }
        if (!PostMessageW(owner, kMsgPicked, 0, reinterpret_cast<LPARAM>(result))) delete result;
    }

    // ---------------------------------------------------------------- texte ---------------
    static std::wstring Trim(std::wstring s) {
        while (!s.empty() && iswspace(s.back())) s.pop_back();
        size_t i = 0;
        while (i < s.size() && iswspace(s[i])) ++i;
        return s.substr(i);
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

    static std::wstring HostName(const std::wstring& url) {
        size_t p = url.find(L"://");
        std::wstring h = p == std::wstring::npos ? url : url.substr(p + 3);
        size_t e = h.find_first_of(L"/?#:");
        if (e != std::wstring::npos) h.erase(e);
        if (h.rfind(L"www.", 0) == 0) h.erase(0, 4);
        return h.empty() ? url : h;
    }

    static std::wstring NameFromPath(std::wstring p) {
        while (p.size() > 3 && (p.back() == L'\\' || p.back() == L'/')) p.pop_back();
        size_t sl = p.find_last_of(L"\\/");
        std::wstring n = (sl == std::wstring::npos) ? p : p.substr(sl + 1);
        if (n.empty()) return p;
        size_t dot = n.find_last_of(L'.');
        if (dot != std::wstring::npos && dot > 0) {
            std::wstring ext = n.substr(dot + 1);
            if (_wcsicmp(ext.c_str(), L"exe") == 0 || _wcsicmp(ext.c_str(), L"lnk") == 0 ||
                _wcsicmp(ext.c_str(), L"url") == 0 || _wcsicmp(ext.c_str(), L"bat") == 0 ||
                _wcsicmp(ext.c_str(), L"cmd") == 0)
                n.erase(dot);
        }
        return n;
    }

    // ---------------------------------------------------------------- dessin --------------
    void Paint(HDC hdc) {
        RECT rc;
        GetClientRect(hwnd_, &rc);
        int W = rc.right, H = rc.bottom;
        if (W <= 0 || H <= 0) return;
        Gdiplus::Bitmap back(W, H, PixelFormat32bppPARGB);
        {
            Gdiplus::Graphics g(&back);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
            g.Clear(Gdiplus::Color(255, 24, 24, 27));
            g.ScaleTransform(scale_, scale_);
            hits_.clear();
            DrawAll(g);
        }
        Gdiplus::Graphics out(hdc);
        out.DrawImage(&back, 0, 0, W, H);
    }

    void AddHit(Gdiplus::RectF r, int act, int arg = 0) { hits_.push_back({r, act, arg}); }

    static Gdiplus::Color White(float a = 1.f) { return Rgba(255, 255, 255, a); }
    static Gdiplus::Color Muted() { return Rgba(150, 150, 156, 1.f); }

    static void Card(Gdiplus::Graphics& g, Gdiplus::RectF r) {
        Gdiplus::GraphicsPath p;
        AddRoundRect(p, r.X, r.Y, r.Width, r.Height, 10.f);
        Gdiplus::SolidBrush b(Rgba(38, 38, 43, 1.f));
        g.FillPath(&b, &p);
        Gdiplus::Pen border(Rgba(255, 255, 255, 0.07f), 1.f);
        g.DrawPath(&border, &p);
    }

    static void Toggle(Gdiplus::Graphics& g, float x, float y, bool on) {
        Gdiplus::GraphicsPath p;
        AddRoundRect(p, x, y, 40.f, 22.f, 11.f);
        Gdiplus::SolidBrush bg(on ? Accent(1.f) : Rgba(85, 85, 92, 1.f));
        g.FillPath(&bg, &p);
        Gdiplus::SolidBrush knob(White());
        g.FillEllipse(&knob, x + (on ? 20.f : 2.f), y + 2.f, 18.f, 18.f);
    }

    static void Chip(Gdiplus::Graphics& g, Gdiplus::RectF r, const std::wstring& text, bool on,
                     bool enabled = true) {
        Gdiplus::GraphicsPath p;
        AddRoundRect(p, r.X, r.Y, r.Width, r.Height, 8.f);
        Gdiplus::SolidBrush bg(on ? Accent(0.95f) : Rgba(58, 58, 64, enabled ? 1.f : 0.5f));
        g.FillPath(&bg, &p);
        Gdiplus::Color tc = on ? Rgba(0, 0, 0, 1.f) : Rgba(235, 235, 240, enabled ? 1.f : 0.4f);
        PutText(g, text, 12, on, tc, Gdiplus::RectF(r.X + 4, r.Y + (r.Height - 16.f) / 2.f, r.Width - 8, 18),
                Gdiplus::StringAlignmentCenter);
    }

    static void Arrow(Gdiplus::Graphics& g, Gdiplus::RectF r, bool up, bool enabled) {
        Gdiplus::GraphicsPath p;
        AddRoundRect(p, r.X, r.Y, r.Width, r.Height, 7.f);
        Gdiplus::SolidBrush bg(Rgba(58, 58, 64, 1.f));
        g.FillPath(&bg, &p);
        float cx = r.X + r.Width / 2.f, cy = r.Y + r.Height / 2.f;
        Gdiplus::PointF pts[3];
        if (up) {
            pts[0] = Gdiplus::PointF(cx - 5, cy + 3);
            pts[1] = Gdiplus::PointF(cx + 5, cy + 3);
            pts[2] = Gdiplus::PointF(cx, cy - 4);
        } else {
            pts[0] = Gdiplus::PointF(cx - 5, cy - 3);
            pts[1] = Gdiplus::PointF(cx + 5, cy - 3);
            pts[2] = Gdiplus::PointF(cx, cy + 4);
        }
        Gdiplus::SolidBrush fg(enabled ? Rgba(235, 235, 240, 1.f) : Rgba(100, 100, 106, 1.f));
        g.FillPolygon(&fg, pts, 3);
    }

    static void Cross(Gdiplus::Graphics& g, Gdiplus::RectF r) {
        Gdiplus::GraphicsPath p;
        AddRoundRect(p, r.X, r.Y, r.Width, r.Height, 7.f);
        Gdiplus::SolidBrush bg(Rgba(58, 58, 64, 1.f));
        g.FillPath(&bg, &p);
        float cx = r.X + r.Width / 2.f, cy = r.Y + r.Height / 2.f;
        Gdiplus::Pen pen(Rgba(255, 110, 100, 1.f), 2.f);
        g.DrawLine(&pen, cx - 4, cy - 4, cx + 4, cy + 4);
        g.DrawLine(&pen, cx - 4, cy + 4, cx + 4, cy - 4);
    }

    static void Title(Gdiplus::Graphics& g, const wchar_t* t, float x, float y) {
        PutText(g, t, 24, true, White(), Gdiplus::RectF(x, y, 500, 34));
    }

    void ToggleRow(Gdiplus::Graphics& g, float x, float y, float w, const wchar_t* label, bool on,
                   int act) {
        Gdiplus::RectF row(x, y, w, 40.f);
        Card(g, row);
        PutText(g, label, 13, false, White(), Gdiplus::RectF(x + 14, y + 11, w - 80, 20));
        Toggle(g, x + w - 54, y + 9, on);
        AddHit(row, act);
    }

    void DrawAll(Gdiplus::Graphics& g) {
        // Menu de gauche
        Gdiplus::SolidBrush side(Rgba(18, 18, 20, 1.f));
        g.FillRectangle(&side, Gdiplus::RectF(0, 0, kSide, kH));
        {   // petit logo : une encoche
            Gdiplus::GraphicsPath logo;
            AddRoundRect(logo, 22.f, 27.f, 24.f, 14.f, 5.f);
            Gdiplus::SolidBrush lb(Accent(1.f));
            g.FillPath(&lb, &logo);
            Gdiplus::SolidBrush lk(Rgba(0, 0, 0, 1.f));
            Gdiplus::GraphicsPath inner;
            AddRoundRect(inner, 29.f, 27.f, 10.f, 8.f, 3.f);
            g.FillPath(&lk, &inner);
        }
        PutText(g, L"Encoche", 17, true, White(), Gdiplus::RectF(56, 24, kSide - 64, 26));
        static const wchar_t* tabs[kTabCount] = {L"Général", L"Apparence", L"Raccourcis", L"Musique",
                                                 L"Prière"};
        for (int i = 0; i < kTabCount; ++i) {
            Gdiplus::RectF r(12, 72.f + i * 40.f, kSide - 24, 34);
            if (i == tab_) {
                Gdiplus::GraphicsPath p;
                AddRoundRect(p, r.X, r.Y, r.Width, r.Height, 9.f);
                Gdiplus::SolidBrush sel(Accent(0.2f));
                g.FillPath(&sel, &p);
            }
            PutText(g, tabs[i], 13, i == tab_, i == tab_ ? Accent(1.f) : Rgba(205, 205, 210, 1.f),
                    Gdiplus::RectF(r.X + 14, r.Y + 8, r.Width - 20, 20));
            AddHit(r, ActTab, i);
        }

        switch (tab_) {
        case 0: TabGeneral(g); break;
        case 1: TabLook(g); break;
        case 2: TabShortcuts(g); break;
        case 3: TabMusic(g); break;
        case 4: TabPrayer(g); break;
        }
    }

    void TabGeneral(Gdiplus::Graphics& g) {
        const Config& c = Cfg();
        float x = kSide + 28, w = kW - kSide - 56, y = 26;
        Title(g, L"Général", x, y);
        y += 46;
        PutText(g, L"Pages de l'encoche : ordre et visibilité (molette sur l'encoche ouverte)", 12, false,
                Muted(), Gdiplus::RectF(x, y, w, 20));
        y += 26;
        for (int pos = 0; pos < kPageCount; ++pos) {
            int id = c.pageOrder[pos];
            Gdiplus::RectF row(x, y, w, 40.f);
            Card(g, row);
            PutText(g, PageName(id), 13, true, c.pageOn[id] ? White() : Muted(),
                    Gdiplus::RectF(x + 14, y + 11, 200, 20));
            Gdiplus::RectF tg(x + w - 54, y + 6, 44, 28);
            Toggle(g, x + w - 54, y + 9, c.pageOn[id]);
            AddHit(tg, ActPageToggle, id);
            Gdiplus::RectF dn(x + w - 54 - 44, y + 6, 32, 28), up(x + w - 54 - 82, y + 6, 32, 28);
            Arrow(g, up, true, pos > 0);
            Arrow(g, dn, false, pos < kPageCount - 1);
            if (pos > 0) AddHit(up, ActPageUp, pos);
            if (pos < kPageCount - 1) AddHit(dn, ActPageDown, pos);
            y += 46;
        }
        y += 14;
        PutText(g, L"Options", 12, false, Muted(), Gdiplus::RectF(x, y, w, 20));
        y += 26;
        ToggleRow(g, x, y, w, L"Heure au format 24 h", c.clock24, ActClock24);
        y += 46;
        ToggleRow(g, x, y, w, L"Lancer Encoche avec Windows", startup_, ActStartup);
        y += 54;
        PutText(g, L"L'heure s'affiche en permanence au centre de l'encoche fermée.", 12, false, Muted(),
                Gdiplus::RectF(x, y, w, 20));
    }

    void TabLook(Gdiplus::Graphics& g) {
        const Config& c = Cfg();
        float x = kSide + 28, w = kW - kSide - 56, y = 26;
        Title(g, L"Apparence", x, y);
        y += 46;

        // Aperçu : encoche fermée + encoche ouverte
        Gdiplus::RectF card(x, y, w, 96.f);
        Gdiplus::GraphicsPath cp;
        AddRoundRect(cp, card.X, card.Y, card.Width, card.Height, 10.f);
        Gdiplus::SolidBrush wall(Rgba(62, 62, 72, 1.f));
        g.FillPath(&wall, &cp);
        Gdiplus::GraphicsState st = g.Save();
        g.SetClip(&cp, Gdiplus::CombineModeIntersect);
        {
            float r1 = 14.f;
            Gdiplus::GraphicsPath p1;
            AddRoundRect(p1, card.X + 40, card.Y - r1, 140, 34.f + r1, r1);
            Gdiplus::SolidBrush blk(Rgba(0, 0, 0, 1.f));
            g.FillPath(&blk, &p1);
            PutText(g, ClockModule::TimeStr(), 14, true, White(),
                    Gdiplus::RectF(card.X + 40, card.Y + 8, 140, 20), Gdiplus::StringAlignmentCenter);

            float r2 = OpenRadius() * 0.6f;
            Gdiplus::GraphicsPath p2;
            AddRoundRect(p2, card.X + 240, card.Y - r2, 260, 70.f + r2, r2);
            g.FillPath(&blk, &p2);
            Gdiplus::SolidBrush bar(Accent(1.f));
            g.FillRectangle(&bar, Gdiplus::RectF(card.X + 262, card.Y + 40, 150, 5));
            Gdiplus::SolidBrush dim(Rgba(255, 255, 255, 0.2f));
            g.FillRectangle(&dim, Gdiplus::RectF(card.X + 412, card.Y + 40, 66, 5));
            PutText(g, L"Aperçu", 13, true, White(), Gdiplus::RectF(card.X + 262, card.Y + 14, 200, 20));
        }
        g.Restore(st);
        y += 118;

        PutText(g, L"Couleur d'accent", 12, false, Muted(), Gdiplus::RectF(x, y, w, 20));
        y += 28;
        for (int i = 0; i < kAccentCount; ++i) {
            Gdiplus::RectF sw(x + i * 42.f + 4, y + 4, 26, 26);
            Gdiplus::SolidBrush br(AccentColor(i, 1.f));
            g.FillEllipse(&br, sw);
            if (i == c.accent) {
                Gdiplus::Pen pen(White(), 2.f);
                g.DrawEllipse(&pen, sw.X - 4, sw.Y - 4, sw.Width + 8, sw.Height + 8);
            }
            AddHit(Gdiplus::RectF(sw.X - 5, sw.Y - 5, sw.Width + 10, sw.Height + 10), ActAccent, i);
        }
        y += 56;
        PutText(g, L"Arrondi des coins de l'encoche ouverte", 12, false, Muted(),
                Gdiplus::RectF(x, y, w, 20));
        y += 28;
        static const wchar_t* names[3] = {L"Doux", L"Rond", L"Maximum"};
        for (int i = 0; i < 3; ++i) {
            Gdiplus::RectF r(x + i * 106.f, y, 96, 30);
            Chip(g, r, names[i], c.radius == i);
            AddHit(r, ActRadius, i);
        }
    }

    void TabShortcuts(Gdiplus::Graphics& g) {
        const Config& c = Cfg();
        float x = kSide + 28, w = kW - kSide - 56, y = 26;
        Title(g, L"Raccourcis", x, y);
        y += 40;
        PutText(g, L"Un clic dans l'encoche ouvre un dossier, une application, un fichier ou un site web (8 maximum).",
                12, false, Muted(), Gdiplus::RectF(x, y, w, 20));
        y += 30;
        const size_t n = c.shortcuts.size();
        if (n == 0) {
            PutText(g, L"Aucun raccourci pour le moment.", 13, false, Muted(), Gdiplus::RectF(x, y + 6, w, 22));
            y += 40;
        }
        for (size_t i = 0; i < n; ++i) {
            Gdiplus::RectF row(x, y, w, 44.f);
            Card(g, row);
            Gdiplus::RectF del(x + w - 42, y + 8, 28, 28);
            Gdiplus::RectF ren(del.X - 8 - 78, y + 8, 78, 28);
            Gdiplus::RectF dn(ren.X - 6 - 30, y + 8, 30, 28);
            Gdiplus::RectF up(dn.X - 4 - 30, y + 8, 30, 28);
            float tw = up.X - (x + 14) - 8;
            PutText(g, c.shortcuts[i].name, 13, true, White(), Gdiplus::RectF(x + 14, y + 5, tw, 20));
            PutText(g, c.shortcuts[i].target, 11, false, Muted(), Gdiplus::RectF(x + 14, y + 24, tw, 16));
            Arrow(g, up, true, i > 0);
            Arrow(g, dn, false, i + 1 < n);
            Chip(g, ren, L"Renommer", false);
            Cross(g, del);
            if (i > 0) AddHit(up, ActScUp, (int)i);
            if (i + 1 < n) AddHit(dn, ActScDown, (int)i);
            AddHit(ren, ActScRename, (int)i);
            AddHit(del, ActScDelete, (int)i);
            y += 50;
        }
        y += 8;
        bool can = n < kMaxShortcuts;
        Gdiplus::RectF b1(x, y, 110, 32), b2(x + 120, y, 160, 32), b3(x + 290, y, 120, 32);
        Chip(g, b1, L"+ Dossier", false, can);
        Chip(g, b2, L"+ Application / fichier", false, can);
        Chip(g, b3, L"+ Site web", false, can);
        if (can) {
            AddHit(b1, ActAddFolder);
            AddHit(b2, ActAddFile);
            AddHit(b3, ActAddUrl);
        }
        if (!can) {
            PutText(g, L"Limite de 8 raccourcis atteinte : supprime-en un pour en ajouter.", 12, false,
                    Muted(), Gdiplus::RectF(x, y + 42, w, 20));
        }
    }

    void TabMusic(Gdiplus::Graphics& g) {
        float x = kSide + 28, w = kW - kSide - 56, y = 26;
        Title(g, L"Musique", x, y);
        y += 46;
        ToggleRow(g, x, y, w, L"Paroles synchronisées (LRCLIB)", Cfg().lyrics, ActLyrics);
        y += 54;
        PutText(g, L"Fonctionne avec Spotify, YouTube, Apple Music... via le contrôle média de Windows.",
                12, false, Muted(), Gdiplus::RectF(x, y, w, 20));
    }

    void TabPrayer(Gdiplus::Graphics& g) {
        const Config& c = Cfg();
        float x = kSide + 28, w = kW - kSide - 56, y = 26;
        Title(g, L"Prière", x, y);
        y += 46;
        Gdiplus::RectF row(x, y, w, 48.f);
        Card(g, row);
        PutText(g, L"Mosquée (Mawaqit)", 13, false, White(), Gdiplus::RectF(x + 14, y + 14, 200, 20));
        Gdiplus::RectF chip(x + 220, y + 9, w - 234, 30);
        Chip(g, chip, c.mosque.empty() ? std::wstring(L"Choisir…") : c.mosque, false);
        AddHit(row, ActMosque);
        y += 64;
        PutText(g, L"Ouvre la page de ta mosquée sur mawaqit.net, copie l'adresse puis colle-la ici.", 12,
                false, Muted(), Gdiplus::RectF(x, y, w, 20));
    }
};
