#pragma once
// Réglages persistants (%APPDATA%\Encoche\settings.ini) + démarrage avec Windows.

#include "module.h"
#include <string>

struct Config {
    int accent = 0;          // index dans kAccents
    int radius = 1;          // 0 = doux, 1 = rond, 2 = max
    bool lyrics = true;      // paroles synchronisées
    bool clock24 = true;     // horloge 24 h
    std::wstring mosque;     // identifiant Mawaqit (ex. "mosquee-de-paris")
};

inline Config& Cfg() {
    static Config c;
    return c;
}

constexpr int kAccentCount = 6;
inline Gdiplus::Color AccentColor(int idx, float alpha) {
    static const int rgb[kAccentCount][3] = {
        {30, 215, 96}, {10, 132, 255}, {255, 159, 10},
        {255, 69, 58}, {191, 90, 242}, {235, 235, 235}};
    idx = std::clamp(idx, 0, kAccentCount - 1);
    return Rgba(rgb[idx][0], rgb[idx][1], rgb[idx][2], alpha);
}
inline Gdiplus::Color Accent(float alpha) { return AccentColor(Cfg().accent, alpha); }

inline float OpenRadius() {
    static const float r[3] = {22.f, 30.f, 40.f};
    return r[std::clamp(Cfg().radius, 0, 2)];
}

inline std::wstring ConfigPath() {
    wchar_t buf[MAX_PATH]{};
    DWORD n = GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH);
    std::wstring dir = (n > 0 && n < MAX_PATH) ? std::wstring(buf) : L".";
    dir += L"\\Encoche";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir + L"\\settings.ini";
}

inline void LoadConfig() {
    std::wstring p = ConfigPath();
    Config& c = Cfg();
    c.accent = GetPrivateProfileIntW(L"ui", L"accent", 0, p.c_str());
    c.radius = GetPrivateProfileIntW(L"ui", L"radius", 1, p.c_str());
    c.lyrics = GetPrivateProfileIntW(L"ui", L"lyrics", 1, p.c_str()) != 0;
    c.clock24 = GetPrivateProfileIntW(L"ui", L"clock24", 1, p.c_str()) != 0;
    c.accent = std::clamp(c.accent, 0, kAccentCount - 1);
    c.radius = std::clamp(c.radius, 0, 2);
    wchar_t m[256]{};
    GetPrivateProfileStringW(L"prayer", L"mosque", L"", m, 256, p.c_str());
    c.mosque = m;
}

inline void SaveConfig() {
    std::wstring p = ConfigPath();
    const Config& c = Cfg();
    WritePrivateProfileStringW(L"ui", L"accent", std::to_wstring(c.accent).c_str(), p.c_str());
    WritePrivateProfileStringW(L"ui", L"radius", std::to_wstring(c.radius).c_str(), p.c_str());
    WritePrivateProfileStringW(L"ui", L"lyrics", c.lyrics ? L"1" : L"0", p.c_str());
    WritePrivateProfileStringW(L"ui", L"clock24", c.clock24 ? L"1" : L"0", p.c_str());
    WritePrivateProfileStringW(L"prayer", L"mosque", c.mosque.c_str(), p.c_str());
}

// --- Démarrage avec Windows (même valeur de registre que l'installateur) ---
constexpr const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

inline bool IsStartupEnabled() {
    return RegGetValueW(HKEY_CURRENT_USER, kRunKey, L"Encoche", RRF_RT_REG_SZ,
                        nullptr, nullptr, nullptr) == ERROR_SUCCESS;
}

inline void SetStartup(bool on) {
    if (on) {
        wchar_t exe[MAX_PATH]{};
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        std::wstring v = L"\"" + std::wstring(exe) + L"\"";
        RegSetKeyValueW(HKEY_CURRENT_USER, kRunKey, L"Encoche", REG_SZ, v.c_str(),
                        (DWORD)((v.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteKeyValueW(HKEY_CURRENT_USER, kRunKey, L"Encoche");
    }
}
