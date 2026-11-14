// Encoche : une "notch" en haut de l'écran Windows.
// Fenêtre layered (alpha par pixel), dessinée avec GDI+, redessinée seulement quand
// quelque chose bouge (animation, changement de morceau, minute qui change).

#include "module.h"
#include "music_module.h"
#include "clock_module.h"

#include <windowsx.h>
#include <shellapi.h>
#include <winrt/base.h>
#include <chrono>
#include <memory>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

constexpr float kClosedW = 200, kClosedH = 34, kClosedR = 14;
constexpr float kOpenW = 440, kOpenH = 150, kOpenR = 30;
constexpr UINT_PTR kTimerId = 1;
constexpr UINT kCmdExit = 1001;

float Lerp(float a, float b, float t) { return a + (b - a) * t; }

struct App {
    HWND hwnd{};
    float scale = 1.f;   // facteur DPI
    float t = 0.f;       // 0 = fermé, 1 = ouvert (valeur lissée)
    float target = 0.f;
    bool visible = true;
    UINT interval = 0;

    HDC memDC{};
    HBITMAP dib{};
    HBITMAP oldBmp{};
    void* bits{};
    int surfW = 0, surfH = 0;

    Clock::time_point last = Clock::now();
    Clock::time_point lastInside = Clock::now() - std::chrono::seconds(1);
    Clock::time_point lastFs = Clock::now();

    std::vector<std::unique_ptr<INotchModule>> modules;
    size_t cur = 0;

    ~App() { DestroySurface(); }

    void DestroySurface() {
        if (memDC) {
            if (oldBmp) SelectObject(memDC, oldBmp);
            DeleteDC(memDC);
        }
        if (dib) DeleteObject(dib);
        memDC = nullptr; dib = nullptr; oldBmp = nullptr; bits = nullptr;
    }

    // Une seule surface à la taille maximale : on n'en affiche que la partie utile.
    void CreateSurface() {
        DestroySurface();
        UINT dpi = GetDpiForWindow(hwnd);
        scale = (dpi ? dpi : 96) / 96.f;
        surfW = (int)std::ceil(kOpenW * scale);
        surfH = (int)std::ceil(kOpenH * scale);

        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = surfW;
        bi.bmiHeader.biHeight = -surfH;  // top-down
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;

        HDC screen = GetDC(nullptr);
        memDC = CreateCompatibleDC(screen);
        dib = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        ReleaseDC(nullptr, screen);
        oldBmp = (HBITMAP)SelectObject(memDC, dib);
    }

    static RECT MonitorRect() {
        POINT o{0, 0};
        MONITORINFO mi{sizeof(mi)};
        GetMonitorInfo(MonitorFromPoint(o, MONITOR_DEFAULTTOPRIMARY), &mi);
        return mi.rcMonitor;
    }

    RECT WindowRect() const {
        RECT m = MonitorRect();
        int w = (int)std::ceil(Lerp(kClosedW, kOpenW, t) * scale);
        int h = (int)std::ceil(Lerp(kClosedH, kOpenH, t) * scale);
        int x = m.left + ((m.right - m.left) - w) / 2;
        return RECT{x, m.top, x + w, m.top + h};
    }

    void SetInterval(UINT ms) {
        if (ms != interval) {
            interval = ms;
            SetTimer(hwnd, kTimerId, ms, nullptr);
        }
    }

    void Render() {
        if (!bits || modules.empty()) return;
        RECT wr = WindowRect();
        float lw = Lerp(kClosedW, kOpenW, t);
        float lh = Lerp(kClosedH, kOpenH, t);
        float r = Lerp(kClosedR, kOpenR, t);
        {
            Gdiplus::Bitmap bmp(surfW, surfH, surfW * 4, PixelFormat32bppPARGB, (BYTE*)bits);
            Gdiplus::Graphics g(&bmp);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
            g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            g.Clear(Gdiplus::Color(0, 0, 0, 0));
            g.ScaleTransform(scale, scale);

            // Forme : collée en haut, seuls les coins du bas sont arrondis.
            Gdiplus::GraphicsPath shape;
            AddRoundRect(shape, 0, -r, lw, lh + r, r);
            Gdiplus::SolidBrush black(Gdiplus::Color(255, 0, 0, 0));
            g.FillPath(&black, &shape);
            g.SetClip(&shape);

            float aC = std::clamp(1.f - t * 3.f, 0.f, 1.f);
            float aE = std::clamp((t - 0.55f) * 2.2f, 0.f, 1.f);
            INotchModule& m = *modules[cur];

            if (aC > 0.01f) {
                Gdiplus::GraphicsState s = g.Save();
                g.TranslateTransform((lw - kClosedW) / 2, 0);
                m.DrawCompact(g, Gdiplus::RectF(8, 6, kClosedW - 16, 22), aC);
                g.Restore(s);
            }
            if (aE > 0.01f) {
                Gdiplus::GraphicsState s = g.Save();
                g.TranslateTransform((lw - kOpenW) / 2, 0);
                m.DrawExpanded(g, Gdiplus::RectF(20, 18, kOpenW - 40, kOpenH - 32), aE);

                size_t n = modules.size();
                if (n > 1) {  // points de pagination
                    for (size_t i = 0; i < n; ++i) {
                        float cx = kOpenW / 2 + ((float)i - (n - 1) / 2.f) * 12.f;
                        Gdiplus::SolidBrush dot(Rgba(255, 255, 255, aE * (i == cur ? 0.9f : 0.3f)));
                        g.FillEllipse(&dot, cx - 3, kOpenH - 12.f, 6.f, 6.f);
                    }
                }
                g.Restore(s);
            }
        }  // GDI+ libéré ici : les pixels sont écrits

        POINT dst{wr.left, wr.top};
        SIZE sz{wr.right - wr.left, wr.bottom - wr.top};
        POINT src{0, 0};
        BLENDFUNCTION bf{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        UpdateLayeredWindow(hwnd, nullptr, &dst, &sz, memDC, &src, 0, &bf, ULW_ALPHA);
    }

    void CheckFullscreen(Clock::time_point now) {
        if (now - lastFs < std::chrono::seconds(1)) return;
        lastFs = now;
        QUERY_USER_NOTIFICATION_STATE st;
        if (SUCCEEDED(SHQueryUserNotificationState(&st))) {
            bool hide = (st == QUNS_BUSY || st == QUNS_RUNNING_D3D_FULL_SCREEN);
            if (hide == visible) {
                visible = !hide;
                ShowWindow(hwnd, visible ? SW_SHOWNOACTIVATE : SW_HIDE);
            }
        }
    }

    void Tick() {
        auto now = Clock::now();
        float dt = std::min(std::chrono::duration<float>(now - last).count(), 0.1f);
        last = now;

        CheckFullscreen(now);
        if (!visible) { SetInterval(1000); return; }

        // Survol : zone de la fenêtre actuelle, un peu élargie, + délai avant fermeture.
        POINT c;
        GetCursorPos(&c);
        RECT wr = WindowRect();
        bool inside = c.x >= wr.left - 12 && c.x < wr.right + 12 &&
                      c.y >= wr.top - 2 && c.y < wr.bottom;
        if (inside) lastInside = now;
        target = (inside || now - lastInside < std::chrono::milliseconds(250)) ? 1.f : 0.f;

        bool dirty = false;
        if (std::fabs(target - t) > 0.0005f) {
            t += (target - t) * (1.f - std::exp(-14.f * dt));
            if (std::fabs(target - t) < 0.002f) t = target;
            dirty = true;
            SetInterval(16);   // animation ~60 fps
        } else {
            SetInterval(100);  // repos : on ne fait que surveiller la souris
        }

        for (auto& m : modules)
            if (m->Tick()) dirty = true;
        if (dirty) Render();
    }
};

App* g_app = nullptr;

LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    App* a = g_app;
    switch (msg) {
    case WM_TIMER:
        if (a) a->Tick();
        return 0;

    case WM_LBUTTONUP:
        if (a && a->t > 0.9f && !a->modules.empty()) {
            float x = GET_X_LPARAM(l) / a->scale;
            float y = GET_Y_LPARAM(l) / a->scale;
            a->modules[a->cur]->OnClick({x - 20, y - 18}, {0, 0, kOpenW - 40, kOpenH - 32});
        }
        return 0;

    case WM_MOUSEWHEEL:
        if (a && a->modules.size() > 1) {  // molette : changer de page
            size_t n = a->modules.size();
            a->cur = GET_WHEEL_DELTA_WPARAM(w) > 0 ? (a->cur + n - 1) % n : (a->cur + 1) % n;
            a->Render();
        }
        return 0;

    case WM_RBUTTONUP: {
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING, kCmdExit, L"Quitter Encoche");
        POINT p;
        GetCursorPos(&p);
        SetForegroundWindow(h);
        UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, p.x, p.y, 0, h, nullptr);
        DestroyMenu(menu);
        if (cmd == kCmdExit) DestroyWindow(h);
        return 0;
    }

    case WM_DPICHANGED:
    case WM_DISPLAYCHANGE:
        if (a) { a->CreateSurface(); a->Render(); }
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, msg, w, l);
}

}  // namespace

int WINAPI WinMain(HINSTANCE hi, HINSTANCE, LPSTR, int) {
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Encoche.SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;

    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    ULONG_PTR gdiTok;
    Gdiplus::GdiplusStartupInput gin;
    Gdiplus::GdiplusStartup(&gdiTok, &gin, nullptr);
    {
        App app;
        g_app = &app;

        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc = WndProc;
        wc.hInstance = hi;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = L"EncocheWnd";
        RegisterClassExW(&wc);

        app.hwnd = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            wc.lpszClassName, L"Encoche", WS_POPUP, 0, 0, 10, 10, nullptr, nullptr, hi, nullptr);

        app.CreateSurface();

        // --- Ajoutez vos modules ici (une page chacun, molette pour naviguer) ---
        app.modules.push_back(std::make_unique<MusicModule>());
        app.modules.push_back(std::make_unique<ClockModule>());
        // app.modules.push_back(std::make_unique<PrayerModule>());  // à venir

        ShowWindow(app.hwnd, SW_SHOWNOACTIVATE);
        app.Render();
        app.SetInterval(100);

        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        g_app = nullptr;
    }  // modules et surface détruits avant GDI+
    Gdiplus::GdiplusShutdown(gdiTok);
    if (mutex) CloseHandle(mutex);
    return 0;
}
