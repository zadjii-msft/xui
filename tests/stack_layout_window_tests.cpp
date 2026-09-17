#include "xui/application.hpp"
#include "xui/documents.hpp"
#include <windows.h>
#include <richedit.h>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace xui;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void close(float actual, float expected, const char* message) {
    require(std::isfinite(actual) && std::abs(actual - expected) < 0.01f, message);
}
void layout(Axis axis) {
    const bool horizontal = axis == Axis::horizontal;
    Window window({horizontal ? L"Horizontal preferred stack" : L"Vertical preferred stack", {800, 800}});
    const auto size = [=](float main, float cross) { return horizontal ? Size{main, cross} : Size{cross, main}; };
    const auto extent = [=](const Element& value) { return horizontal ? value.bounds().width : value.bounds().height; };
    auto root = std::make_shared<Stack>(axis);
    auto workspace = std::make_shared<Stack>(axis);
    auto source = std::make_shared<MultilineText>();
    source->set_text(L"Native source");
    workspace->add(source, 1);
    auto diagnostics = std::make_shared<Stack>(axis);
    auto details = std::make_shared<MultilineText>();
    details->set_text(L"Diagnostics");
    diagnostics->add(details, 1);
    diagnostics->set_preferred_size(size(140, 400));
    root->set_spacing(7);
    root->add(workspace, 1);
    root->add(diagnostics);
    window.set_content(root);
    bool ran{};
    window.post([&] {
        close(extent(*diagnostics), 140, "Native diagnostics stack honors explicit preference");
        close(extent(*source), extent(*root) - 147, "Native source retains remaining main-axis space");
        require(window.focus(*source), "Native source remains focusable");
        const auto editor = GetFocus();
        wchar_t kind[32]{};
        GetClassNameW(editor, kind, 32);
        require(_wcsicmp(kind, L"RICHEDIT50W") == 0, "Focused source uses actual RichEdit peer");
        const auto geometry = [&] {
            RECT bounds{};
            require(GetWindowRect(editor, &bounds) != FALSE && bounds.right > bounds.left && bounds.bottom > bounds.top,
                "Native source HWND has nonzero geometry");
            const auto dpi = GetDpiForWindow(editor);
            const float expected = extent(*source) * dpi / 96.0f;
            require(std::abs((horizontal ? bounds.right - bounds.left : bounds.bottom - bounds.top) - expected) <= 1.0f,
                "Native HWND extent matches retained source layout");
        };
        geometry();
        SendMessageW(editor, EM_SETSEL, 0, -1);
        SendMessageW(editor, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"Edited source"));
        require(source->text() == L"Edited source" && SendMessageW(editor, EM_CANUNDO, 0, 0), "Native source edit creates undo history");
        diagnostics->set_preferred_size(size(200, 400));
        require(window.focus(*source) && GetFocus() == editor, "Relayout retains native source identity and focus");
        close(extent(*diagnostics), 200, "Changed preference reaches native layout");
        close(extent(*source), extent(*root) - 207, "Changed preference preserves source remainder");
        geometry();
        require(source->command(TextCommand::undo) && source->text() == L"Native source", "Relayout preserves native undo");
        require(source->command(TextCommand::redo) && source->text() == L"Edited source", "Relayout preserves native redo");
        diagnostics->set_auto_size(true);
        window.focus(*details);
        close(extent(*diagnostics), extent(*root) - 7, "Explicit auto sizing restores natural flex allocation");
        diagnostics->set_auto_size(false);
        require(window.focus(*source) && GetFocus() == editor, "Disabling auto sizing restores visible native source");
        close(extent(*source), extent(*root) - 207, "Stored preference restored after auto override");
        geometry();
        ran = true;
        window.close();
    });
    const auto result = Application::run(window);
    if (result) std::wcerr << window.error() << L'\n';
    require(result == 0 && ran, "Preferred stack native fixture completed");
}
}
int main() {
    try {
        layout(Axis::vertical);
        layout(Axis::horizontal);
        std::cout << "Preferred stacks preserve native source geometry, focus, identity and undo/redo on both axes\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
