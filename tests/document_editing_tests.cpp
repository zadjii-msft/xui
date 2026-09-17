#include "xui/documents.hpp"
#include <iostream>
#include <stdexcept>
using namespace xui;
namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F action) {
    try { action(); } catch (const std::exception&) { return; }
    throw std::runtime_error("Expected rejected range replacement");
}
}
int main() {
    try {
        MultilineText document;
        document.set_text(L"A\U0001f600Z\rEnd");
        document.set_selection({1, 3});
        const auto original = document.text();
        int calls{};
        rejects([&] { document.replace_range({1, 3}, original, L"B"); });
        document.bind_range_replacement([&](TextSelection range, const std::wstring& expected, const std::wstring& replacement) {
            ++calls;
            require(range == TextSelection{1, 3} && expected == original, "Exact range and snapshot");
            require(replacement == L"B\rC\rD", "Replacement paragraphs normalized");
            return TextSelection{6, 6};
        });
        const auto invalid = [&](TextSelection range, const std::wstring& expected, std::wstring replacement) {
            rejects([&] { document.replace_range(range, expected, std::move(replacement)); });
            require(document.text() == original && document.selection() == TextSelection{1, 3} && !calls,
                "Rejected replacement leaves model and adapter untouched");
        };
        invalid({2, 3}, original, L"B");
        invalid({1, 2}, original, L"B");
        invalid({4, 3}, original, L"B");
        invalid({0, 100}, original, L"B");
        invalid({1, 3}, L"stale", L"B");
        invalid({1, 3}, original, std::wstring(1, 0xd800));
        invalid({1, 3}, original, std::wstring(1, 0xdc00));
        invalid({1, 3}, original, std::wstring(L"B\0C", 3));
        invalid({1, 3}, original, L"\U0001f600");
        document.set_maximum_length(original.size());
        invalid({1, 3}, original, L"longer");
        document.set_read_only(true); invalid({1, 3}, original, L"B");
        document.set_read_only(false);
        document.set_enabled(false); invalid({1, 3}, original, L"B");
        document.set_enabled(true);
        document.set_visible(false); invalid({1, 3}, original, L"B");
        document.set_visible(true);
        document.set_maximum_length(100);
        require(document.replace_range({1, 3}, original, L"B\nC\r\nD") == TextSelection{6, 6} && calls == 1,
            "Adapter returns resulting selection");
        RichText rich;
        rich.bind_range_replacement([](auto, auto&, auto&) { return TextSelection{}; });
        rejects([&] { rich.replace_range({}, L"", L"X"); });
        std::cout << "Document range validation passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
