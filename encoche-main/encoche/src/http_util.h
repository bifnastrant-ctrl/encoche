#pragma once
// Petit GET HTTP(S) synchrone via C++/WinRT. À appeler depuis un thread de travail
// (jamais depuis le thread de l'interface).

#include <string>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Web.Http.h>
#include <winrt/Windows.Web.Http.Headers.h>

inline bool HttpGetString(const std::wstring& url, std::wstring& out, const wchar_t* userAgent) {
    try {
        winrt::Windows::Web::Http::HttpClient client;
        client.DefaultRequestHeaders().UserAgent().ParseAdd(userAgent);
        auto resp = client.GetAsync(winrt::Windows::Foundation::Uri(url)).get();
        if (!resp.IsSuccessStatusCode()) return false;
        out = resp.Content().ReadAsStringAsync().get().c_str();
        return true;
    } catch (...) {
        return false;
    }
}

inline std::wstring UrlEscape(const std::wstring& s) {
    return winrt::Windows::Foundation::Uri::EscapeComponent(winrt::hstring(s)).c_str();
}
