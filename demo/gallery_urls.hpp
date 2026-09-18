#pragma once
#include <stdexcept>
#include <string>
#include <string_view>

namespace gallery {
inline std::wstring documentation_url(std::wstring_view path) {
    const std::wstring root = L"https://zadjii-msft.github.io/xui/";
    if (path == L"docs/specs/README.md") return root;
    if (!path.ends_with(L".md")) throw std::invalid_argument("Expected a handbook Markdown page");
    path.remove_suffix(3);
    constexpr std::wstring_view controls = L"docs/specs/controls/", languages = L"docs/specs/languages/";
    if (path.starts_with(controls))
        return root + L"choose-a-control/controls/" + std::wstring(path.substr(controls.size())) + L"/";
    if (path.starts_with(languages))
        return root + L"learn/languages/" + std::wstring(path.substr(languages.size())) + L"/";
    if (path == L"docs/specs/menus-and-input" || path == L"docs/specs/winui-style" || path == L"docs/specs/animations")
        return root + L"contracts/" + std::wstring(path.substr(std::wstring_view(L"docs/specs/").size())) + L"/";
    throw std::invalid_argument("Gallery documentation page has no handbook route");
}
}
