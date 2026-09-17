#include "xui/file_dialog.hpp"
#include <iostream>
#include <stdexcept>
using namespace xui;
namespace {
void require(bool value) { if (!value) throw std::runtime_error("File dialog validation assertion failed"); }
template<class F> void rejects(F action) {
    bool rejected{};
    try { action(); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected);
}
}
int main() {
    try {
        FileDialogOptions options;
        options.validate();
        options = {L"Open source", {{L"Components", L"*.xui;*.xml"}, {L"All files", L"*.*"}}, L"xui",
            L"Component.xui", L"C:\\Projects"};
        options.validate();
        options.initial_directory = L"\\\\server\\share\\folder"; options.validate();
        const auto valid = options;
        const auto invalid = [&](auto mutate) {
            auto value = valid; mutate(value); rejects([&] { value.validate(); });
        };
        invalid([](auto& v) { v.title = std::wstring(257, L'X'); });
        invalid([](auto& v) { v.title = std::wstring(L"a\0b", 3); });
        invalid([](auto& v) { v.title = std::wstring(1, 0xd800); });
        invalid([](auto& v) { v.title = std::wstring(1, 0xdc00); });
        invalid([](auto& v) { v.title = std::wstring(1, 0x7f); });
        invalid([](auto& v) { v.title = std::wstring(1, 0x85); });
        invalid([](auto& v) { v.filters.resize(33, {L"Source", L"*.txt"}); });
        invalid([](auto& v) { v.filters[0].name.clear(); });
        invalid([](auto& v) { v.filters[0].name = std::wstring(129, L'X'); });
        invalid([](auto& v) { v.filters[0].pattern = L"*." + std::wstring(511, L'x'); });
        invalid([](auto& v) { v.filters[0].pattern.clear(); });
        invalid([](auto& v) { v.filters[0].pattern = L"*.xui;"; });
        invalid([](auto& v) { v.filters[0].pattern = L"xui"; });
        invalid([](auto& v) { v.filters[0].pattern = L"*.xui; *.xml"; });
        invalid([](auto& v) { v.filters[0].pattern = L"*.xui\\evil"; });
        invalid([](auto& v) { v.default_extension = L".xui"; });
        invalid([](auto& v) { v.default_extension = L"*"; });
        invalid([](auto& v) { v.default_extension = std::wstring(65, L'x'); });
        invalid([](auto& v) { v.suggested_name = L"..\\source.xui"; });
        invalid([](auto& v) { v.suggested_name = L"C:\\source.xui"; });
        invalid([](auto& v) { v.suggested_name = L"source."; });
        invalid([](auto& v) { v.suggested_name = std::wstring(256, L'x'); });
        invalid([](auto& v) { v.initial_directory = L"folder"; });
        invalid([](auto& v) { v.initial_directory = L"C:folder"; });
        invalid([](auto& v) { v.initial_directory = L"\\\\server"; });
        invalid([](auto& v) { v.initial_directory = L"\\\\.\\device"; });
        invalid([](auto& v) { v.initial_directory = L"C:\\" + std::wstring(32765, L'x'); });
        options.title = std::wstring(256, L'X');
        options.filters.assign(32, {std::wstring(128, L'X'), L"*." + std::wstring(510, L'x')});
        options.default_extension = std::wstring(64, L'x');
        options.suggested_name = std::wstring(255, L'x');
        options.initial_directory = L"C:\\" + std::wstring(32764, L'x');
        options.validate();
        options.title = L"Source \u65e5\U0001f600";
        options.validate();
        std::cout << "File dialog options validation passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
