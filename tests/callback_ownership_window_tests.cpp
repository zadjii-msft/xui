#include "xui/application.hpp"
#include <windows.h>
#include <iostream>

namespace {
using namespace xui;
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
HWND named(HWND parent, const wchar_t* name) {
    struct Search { const wchar_t* name; HWND result{}; } search{name};
    EnumChildWindows(parent, [](HWND child, LPARAM data) -> BOOL {
        auto& search = *reinterpret_cast<Search*>(data);
        wchar_t text[256]{};
        GetWindowTextW(child, text, 256);
        if (std::wstring_view(text) == search.name) { search.result = child; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    require(search.result != nullptr, "The owned composite surface exists");
    return search.result;
}
HWND edit(HWND parent) {
    HWND result{};
    EnumChildWindows(parent, [](HWND child, LPARAM data) -> BOOL {
        wchar_t klass[32]{};
        GetClassNameW(child, klass, 32);
        if (_wcsicmp(klass, L"EDIT") == 0) { *reinterpret_cast<HWND*>(data) = child; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&result));
    require(result != nullptr, "The composite owns an actual OS EDIT");
    return result;
}
void run(bool failure) {
    WindowOptions options{L"XUI callback ownership", {700, 500}, ThemeMode::light};
    options.show_activated = false;
    Window window(options);
    auto root = std::make_shared<Stack>(Axis::vertical);
    auto number = std::make_shared<NumericInput>(L"Native numeric owner");
    auto combo = std::make_shared<ComboBox>(L"Native editable owner", true);
    root->add(number); root->add(combo);
    window.set_content(root);
    unsigned numeric_events{}, child_events{}, combo_events{}, combo_child_events{};
    number->on_change([&](double value) { require(number->value() == value, "Numeric observers see committed state"); ++numeric_events; });
    combo->on_edit([&](const std::wstring&) { ++combo_events; });
    bool ran{};
    window.post([&] {
        const auto hwnd = FindWindowW(L"Xui.Window.1", options.title.c_str());
        require(hwnd != nullptr, "The callback fixture owns its window");
        const auto numeric = edit(named(hwnd, L"Native numeric owner"));
        const auto editable = edit(named(hwnd, L"Native editable owner"));
        number->editor()->on_change([&](const std::wstring&) {
            require(number->value() == 12 && numeric_events == 1, "Real EDIT composition precedes the child observer");
            ++child_events;
        });
        SetWindowTextW(numeric, L"12");
        require(number->value() == 12 && child_events == 1, "Observing native input does not replace numeric parsing");
        number->editor()->on_change({});
        SetWindowTextW(numeric, L"13");
        require(number->value() == 13 && numeric_events == 2 && child_events == 1,
            "Removing native input observers leaves the composite functioning");
        combo->editor()->on_change([&](const std::wstring&) {
            require(combo_events == combo_child_events + 1, "Real ComboBox composition precedes its child observer");
            ++combo_child_events;
        });
        SetWindowTextW(editable, L"first");
        combo->editor()->on_change({});
        SetWindowTextW(editable, L"second");
        require(combo_events == 2 && combo_child_events == 1, "Editable ComboBox retains internal native edit delivery");
        require(edit(named(hwnd, L"Native numeric owner")) == numeric &&
            edit(named(hwnd, L"Native editable owner")) == editable, "Observer changes never recreate native editors");
        number->editor()->on_change([&](const std::wstring&) {
            require(number->value() == 14 && numeric_events == 3, "Composition commits before native close/failure");
            ran = true;
            if (failure) throw std::runtime_error("Controlled scoped observer failure");
            window.close();
        });
        SetWindowTextW(numeric, L"14");
    });
    const auto result = Application::run(window);
    if (!ran || result != (failure ? 1 : 0)) {
        std::cerr << "result=" << result << " expected_failure=" << failure << " reached_final=" << ran
            << " parent=" << numeric_events << " child=" << child_events << " combo=" << combo_events << '\n';
        std::wcerr << window.error() << L'\n';
    }
    require(ran && result == (failure ? 1 : 0), "Native callback close/failure preserves the explicit loop outcome");
    require(!failure || !window.error().empty(), "Failed observers report a real native callback error");
}
}
int main(int argc, char**) {
    if (argc != 1) { std::cerr << "The native callback fixture accepts no selectors\n"; return 2; }
    try {
        run(false);
        run(true);
        std::cout << "Native EDIT composition, observer replacement, identity and callback close/failure passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
