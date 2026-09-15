#include "xui/application.hpp"
#include "xui/foundation.hpp"
#include "xui/vector_canvas.hpp"
#include "../src/drawing.hpp"
#include "../src/workspace_accessibility.hpp"
#include "../src/list_peer.hpp"
#include <windows.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <ole2.h>
#include <UIAutomation.h>
#include <wrl/client.h>
#include <psapi.h>
#include "resource_probe.hpp"
#include "uia_events.hpp"

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void text_presentation() {
    using namespace xui;
    const auto targets = Drawing::live_targets();
    Drawing drawing;
    const auto symbol_faces = Drawing::created_symbol_faces();
    drawing.initialize(VisualStyle::winui);
    Microsoft::WRL::ComPtr<IDWriteFactory> symbol_factory;
    require(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(symbol_factory.GetAddressOf()))), "Create independent symbol font probe");
    Microsoft::WRL::ComPtr<IDWriteFontCollection> symbol_fonts;
    require(SUCCEEDED(symbol_factory->GetSystemFontCollection(&symbol_fonts)), "Read installed symbol fonts");
    UINT32 symbol_index{};
    BOOL fluent_installed{};
    require(SUCCEEDED(symbol_fonts->FindFamilyName(L"Segoe Fluent Icons", &symbol_index, &fluent_installed)),
        "Probe installed Segoe Fluent Icons");
    require(std::wstring_view(drawing.symbol_font_family()) == (fluent_installed ? L"Segoe Fluent Icons" : L"Segoe MDL2 Assets"),
        "WinUI uses installed Segoe Fluent Icons; only missing-family systems use the documented fallback");
    for (std::size_t i = 1; i < symbol_codepoints.size(); ++i)
        require(drawing.has_symbol(static_cast<Symbol>(i)), "Every WinUI symbol resolves to a nonzero native glyph");
    for (int i = static_cast<int>(ButtonIcon::back); i <= static_cast<int>(ButtonIcon::drive); ++i)
        require(drawing.has_symbol(button_symbol(static_cast<ButtonIcon>(i))),
            "Every nonempty button icon has an available Fluent glyph mapping");
    require(!drawing.has_symbol(Symbol::none) && !drawing.has_symbol(Symbol::count),
        "Empty and invalid symbols never resolve to the missing-character glyph");
    require(Drawing::created_symbol_faces() == symbol_faces + 1, "Symbols share one font face, not one resource per control");
    Size strong_size{};
    const auto strong = drawing.layout(L"Explore", TextStyle::body_strong, strong_size);
    require(strong->GetFontSize() == 14 && strong_size.height > 0 && strong_size.height < 24,
        "Navigation pane titles use a native 14-DIP text line instead of a page heading");
    Label title(L"A deliberately long dialog title that must wrap across several lines in a narrow window");
    title.set_subtitle(true);
    title.set_visual_style(VisualStyle::winui);
    title.set_wrapping(true, 2);
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    unsigned measures{};
    title.set_wrapped_text_measurer([&](std::wstring_view text, TextStyle style, float width, std::size_t lines) {
        Size result;
        layout = drawing.layout(text, style, result, width, lines);
        ++measures;
        return result;
    });
    const auto measured = title.measure({120, 1000});
    UINT32 count{};
    const auto counted = layout->GetLineMetrics(nullptr, 0, &count);
    require((counted == E_NOT_SUFFICIENT_BUFFER || SUCCEEDED(counted)) && count > 2, "The native wrapping fixture has more than two lines");
    std::vector<DWRITE_LINE_METRICS> lines(count);
    require(SUCCEEDED(layout->GetLineMetrics(lines.data(), count, &count)), "Read native title line metrics");
    require(measured.width <= 120 && measured.height == std::ceil(lines[0].height + lines[1].height),
        "The title cap uses two actual DirectWrite line heights");
    title.measure({120, 1000});
    require(measures == 1, "Unchanged wrapped text reuses the native layout");
    title.measure({160, 1000});
    require(measures == 2, "A different wrap width recreates the native layout once");
    IDWriteTextFormat* format = layout.Get();
    wchar_t family[100]{};
    require(SUCCEEDED(format->GetFontFamilyName(family, 100)), "Read native WinUI text family");
    require(std::wstring_view(family) == (std::wstring_view(drawing.edit_font_family()) == L"Segoe UI Variable Text" ?
        L"Segoe UI Variable" : L"Segoe UI"), "WinUI layout and native editing use the same resolved font policy");
    drawing.set_visual_style(VisualStyle::classic);
    title.set_visual_style(VisualStyle::classic);
    title.measure({160, 1000});
    require(measures == 3, "A style change invalidates wrapped text even when width and text role stay unchanged");
    format = layout.Get();
    require(SUCCEEDED(format->GetFontFamilyName(family, 100)) && std::wstring_view(family) == L"Segoe UI",
        "Classic restores its original text family");
    require(Drawing::live_targets() == targets, "Typography and wrapping do not allocate a graphics target");
    drawing.set_visual_style(VisualStyle::winui);
    drawing.discard();
    require(drawing.has_symbol(Symbol::check) && Drawing::created_symbol_faces() == symbol_faces + 1,
        "Style roundtrips and render-target loss retain the cached symbol face and glyph metrics");
    drawing.release();
    require(!drawing.has_symbol(Symbol::check) && std::wstring_view(drawing.symbol_font_family()).empty(),
        "Full renderer release frees symbol resources");
}
struct Search { const wchar_t* title; HWND result{}; DWORD process{GetCurrentProcessId()}; };
BOOL CALLBACK find(HWND window, LPARAM data) {
    auto& search = *reinterpret_cast<Search*>(data);
    DWORD process{};
    GetWindowThreadProcessId(window, &process);
    if (process != search.process) return TRUE;
    wchar_t text[100]{};
    GetWindowTextW(window, text, 100);
    if (std::wstring(text) == search.title && IsWindowVisible(window)) {
        search.result = window;
        return FALSE;
    }
    return TRUE;
}
int drive(xui::Window& window, const wchar_t* title) {
    std::atomic<bool> clicked{};
    std::jthread input([&] {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        do {
            Search search{title};
            EnumWindows(find, reinterpret_cast<LPARAM>(&search));
            if (search.result) {
                const auto button = FindWindowExW(search.result, nullptr, L"Xui.Control.1", L"Close");
                if (button) {
                    PostMessageW(button, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(10, 10));
                    PostMessageW(button, WM_LBUTTONUP, 0, MAKELPARAM(10, 10));
                    clicked = true;
                    return;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        } while (std::chrono::steady_clock::now() < deadline);
    });
    const auto result = xui::Application::run(window);
    input.join();
    if (!clicked) std::wcerr << title << L": " << window.error() << L'\n';
    require(clicked, "A new window must run after prior shutdown or startup failure");
    return result;
}
void window_lifecycle() {
    auto retained = std::make_shared<xui::Button>(L"Close");
    auto retained_root = std::make_shared<xui::Stack>(xui::Axis::vertical);
    int later_invalidations{};
    const auto identity = retained->id();
    {
        xui::Window window({L"XUI lifecycle normal"});
        require(xui::WindowOptions{}.visual_style == xui::VisualStyle::classic &&
            window.visual_style() == xui::VisualStyle::classic, "Visual style defaults to classic");
        window.set_visual_style(xui::VisualStyle::winui);
        require(window.visual_style() == xui::VisualStyle::winui, "Visual style updates before run");
        bool invalid_style{};
        try { window.set_visual_style(static_cast<xui::VisualStyle>(-1)); }
        catch (const std::invalid_argument&) { invalid_style = true; }
        require(invalid_style && window.visual_style() == xui::VisualStyle::winui,
            "Invalid visual style is rejected without changing the property");
        window.set_title(L"Before run");
        require(window.title() == L"Before run", "Title property updates before run");
        window.set_title(L"XUI lifecycle normal");
        bool wrong_thread_style{};
        std::jthread wrong_thread([&] {
            bool rejected{};
            try { window.set_title(L"Wrong thread"); } catch (const std::logic_error&) { rejected = true; }
            require(rejected, "Title rejects non-owner thread");
            try { window.set_visual_style(xui::VisualStyle::classic); }
            catch (const std::logic_error&) { wrong_thread_style = true; }
        });
        wrong_thread.join();
        require(wrong_thread_style && window.visual_style() == xui::VisualStyle::winui,
            "Visual style rejects non-owner thread without changing the property");
        auto root = retained_root;
        auto input = std::make_shared<xui::TextInput>(L"Name");
        const auto input_identity = input->id(), root_identity = root->id();
        input->set_text(L"Style selection stays");
        require(retained->appearance() == xui::ButtonAppearance::standard, "Button appearance defaults to standard");
        retained->set_appearance(xui::ButtonAppearance::accent);
        require(retained->appearance() == xui::ButtonAppearance::accent, "Button appearance updates before run");
        root->add(retained);
        root->add(input);
        window.set_content(root);
        retained->on_click([&] {
            require(window.visual_style() == xui::VisualStyle::winui, "Pre-run visual style survives native creation");
            window.set_title(L"Updated during run");
            window.set_title(L"Updated during run");
            Search titled{L"Updated during run"};
            EnumWindows(find, reinterpret_cast<LPARAM>(&titled));
            require(titled.result && window.title() == L"Updated during run", "Title updates native caption during run");
            input->set_enabled(false);
            require(!window.focus(*input), "Focus rejects a disabled control");
            input->set_enabled(true);
            require(window.focus(*input) && input->focused(), "Application focus reaches native EDIT");
            const auto edit = GetFocus();
            const auto button = FindWindowExW(titled.result, nullptr, L"Xui.Control.1", L"Close");
            require(edit && button && FindWindowExW(titled.result, nullptr, L"EDIT", nullptr) == edit,
                "Visual style fixture has native button and EDIT peers");
            SendMessageW(edit, EM_SETSEL, 2, 8);
            const auto paint = [&] {
                SendMessageW(titled.result, WM_APP + 12, 0, 0);
                require(RedrawWindow(titled.result, nullptr, nullptr,
                    RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW) != FALSE, "Paint visual style synchronously");
            };
            paint();
            const auto targets = xui::Drawing::live_targets();
            require(targets > 0, "Visual style fixture paints a Direct2D target");
            for (const auto theme : {xui::ThemeMode::light, xui::ThemeMode::dark, xui::ThemeMode::high_contrast}) {
                window.set_theme(theme);
                for (const auto style : {xui::VisualStyle::classic, xui::VisualStyle::winui,
                    xui::VisualStyle::classic, xui::VisualStyle::winui}) {
                    window.set_visual_style(style);
                    require(window.visual_style() == style && window.theme() == theme,
                        "Live visual style switches independently of the selected theme");
                    for (const auto appearance : {xui::ButtonAppearance::standard,
                        xui::ButtonAppearance::accent, xui::ButtonAppearance::subtle}) {
                        retained->set_appearance(appearance);
                        require(retained->appearance() == appearance, "Live button appearance updates");
                        paint();
                    }
                    DWORD start{}, end{};
                    SendMessageW(edit, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
                    wchar_t text[100]{};
                    GetWindowTextW(edit, text, 100);
                    require(root == retained_root && root->id() == root_identity && root->child_count() == 2 &&
                        root->child_at(0) == retained && root->child_at(1) == input &&
                        retained->id() == identity && input->id() == input_identity,
                        "Visual style switches preserve retained content and control identities");
                    require(FindWindowExW(titled.result, nullptr, L"Xui.Control.1", L"Close") == button &&
                        FindWindowExW(titled.result, nullptr, L"EDIT", nullptr) == edit &&
                        GetFocus() == edit && input->focused() && start == 2 && end == 8 &&
                        std::wstring_view(text) == L"Style selection stays" && input->text() == text,
                        "Visual style switches preserve native peers, EDIT text, selection and focus");
                    require(xui::Drawing::live_targets() == targets,
                        "Visual style switches and painting do not add live Direct2D targets");
                }
            }
            window.set_theme(xui::ThemeMode::light);
            paint();
            const auto clear = FindWindowExW(titled.result, nullptr, L"Xui.Control.1", L"Clear Name");
            require(clear && IsWindowVisible(clear) && (GetWindowLongPtrW(clear, GWL_STYLE) & WS_TABSTOP) == 0,
                "A focused nonempty WinUI field exposes an accessible clear action without an extra Tab stop");
            window.set_visual_style(xui::VisualStyle::classic);
            paint();
            require(!IsWindowVisible(clear) && GetFocus() == edit, "Classic hides the clear affordance without moving native focus");
            window.set_visual_style(xui::VisualStyle::winui);
            paint();
            require(FindWindowExW(titled.result, nullptr, L"Xui.Control.1", L"Clear Name") == clear && IsWindowVisible(clear),
                "Returning to WinUI reuses the same clear-button peer");
            SendMessageW(edit, WM_IME_STARTCOMPOSITION, 0, 0);
            paint();
            require(!IsWindowVisible(clear), "Active IME composition hides the clear action");
            SendMessageW(edit, WM_IME_ENDCOMPOSITION, 0, 0);
            paint();
            require(IsWindowVisible(clear), "The clear action returns after native composition ends");
            int changes{};
            input->on_change([&](const std::wstring&) { ++changes; });
            SendMessageW(clear, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(12, 12));
            SendMessageW(clear, WM_LBUTTONUP, 0, MAKELPARAM(12, 12));
            paint();
            require(input->text().empty() && GetWindowTextLengthW(edit) == 0 && changes == 1 &&
                GetFocus() == edit && !IsWindowVisible(clear), "Clear edits native text once and retains input focus");
            require(SendMessageW(edit, EM_UNDO, 0, 0) != 0, "The native clear operation supports undo");
            paint();
            require(input->text() == L"Style selection stays" && changes == 2 && IsWindowVisible(clear),
                "Native undo restores the cleared value and affordance");
            input->on_change({});
            const auto native_fill = [&] {
                const auto dc = GetDC(edit);
                require(dc != nullptr, "Acquire a native field color context");
                SendMessageW(titled.result, WM_CTLCOLOREDIT, reinterpret_cast<WPARAM>(dc), reinterpret_cast<LPARAM>(edit));
                const auto color = GetBkColor(dc);
                ReleaseDC(edit, dc);
                return color;
            };
            const auto expected_fill = [&](bool focused, bool hovered) {
                const auto palette = xui::Palette::system(xui::ThemeMode::light, xui::VisualStyle::winui);
                const auto fill = palette.input_fill(true, focused, hovered, false);
                return RGB(std::lround(fill.r * 255), std::lround(fill.g * 255), std::lround(fill.b * 255));
            };
            require(native_fill() == expected_fill(true, false), "Native focused text uses the same resolved fill as its Direct2D field");
            require(window.focus(*retained), "Move focus away from the field");
            paint();
            SendMessageW(edit, WM_MOUSELEAVE, 0, 0);
            require(native_fill() == expected_fill(false, false), "Native unfocused text uses its parent-composited fill");
            SendMessageW(edit, WM_MOUSEMOVE, 0, MAKELPARAM(10, 10));
            require(native_fill() == expected_fill(false, true), "Native hover and field chrome share the pointer-over fill");
            require(window.focus(*input), "Restore input focus after color checks");
            paint();
            const auto field_brushes = xui::Drawing::created_field_brushes();
            for (int i = 0; i < 4; ++i) {
                require(window.focus(*retained), "Blur the field for its normal border");
                paint();
                require(window.focus(*input), "Focus the field for its accent border");
                paint();
            }
            require(xui::Drawing::created_field_brushes() == field_brushes,
                "Normal and focused field borders reuse their gradient brushes across paints");
            auto number = std::make_shared<xui::NumericInput>(L"Focus selection");
            number->set_range({0, 10000});
            number->set_value(1234);
            root->add(number);
            paint();
            require(window.focus(*number->editor()), "Programmatic focus reaches the numeric editor");
            const auto number_edit = GetFocus();
            const auto selected_all = [&] {
                DWORD first{}, last{};
                SendMessageW(number_edit, EM_GETSEL, reinterpret_cast<WPARAM>(&first), reinterpret_cast<LPARAM>(&last));
                return first == 0 && last == static_cast<DWORD>(GetWindowTextLengthW(number_edit));
            };
            const auto selection_empty = [&] {
                DWORD first{}, last{};
                SendMessageW(number_edit, EM_GETSEL, reinterpret_cast<WPARAM>(&first), reinterpret_cast<LPARAM>(&last));
                return first == last;
            };
            require(selected_all(), "WinUI numeric focus selects the entire value");
            require(window.focus(*retained), "Leave the numeric editor before pointer focus");
            SendMessageW(number_edit, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(5, 5));
            SendMessageW(number_edit, WM_LBUTTONUP, 0, MAKELPARAM(5, 5));
            require(GetFocus() == number_edit && selected_all(), "The first pointer focus selects the numeric value after native caret placement");
            SendMessageW(number_edit, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(10, 5));
            SendMessageW(number_edit, WM_LBUTTONUP, 0, MAKELPARAM(10, 5));
            require(selection_empty(), "A later click in the focused numeric editor places the caret normally");
            window.set_visual_style(xui::VisualStyle::classic);
            paint();
            require(window.focus(*retained) && window.focus(*number->editor()) && selection_empty(),
                "Classic numeric focus retains the native caret instead of selecting all");
            window.set_visual_style(xui::VisualStyle::winui);
            paint();
            require(GetFocus() == number_edit && selection_empty(), "A live style change does not replace an existing numeric selection");
            auto details = std::make_shared<xui::Expander>(L"Pointer details", std::make_shared<xui::Label>(L"Body content"));
            details->set_fixed_size({300, 150});
            root->add(details);
            paint();
            const auto expander = FindWindowExW(titled.result, nullptr, L"Xui.Control.1", L"Pointer details");
            require(expander && details->expanded(), "The native expander fixture starts with a visible body");
            const auto pointer = [&](UINT message, float y, WPARAM keys = 0) {
                const auto scale = GetDpiForWindow(expander) / 96.0f;
                SendMessageW(expander, message, keys, MAKELPARAM(std::lround(10 * scale), std::lround(y * scale)));
            };
            const auto header_y = details->effective_header_height() / 2;
            const auto body_y = details->effective_header_height() + 8;
            pointer(WM_MOUSEMOVE, body_y);
            require(!details->hovered(), "The expanded body does not hover the WinUI header");
            pointer(WM_MOUSEMOVE, header_y);
            require(details->hovered(), "The whole WinUI header accepts pointer hover");
            int disclosures{};
            details->on_change([&](bool) { ++disclosures; });
            pointer(WM_LBUTTONDOWN, header_y, MK_LBUTTON);
            pointer(WM_MOUSEMOVE, body_y, MK_LBUTTON);
            pointer(WM_LBUTTONUP, body_y);
            require(details->expanded() && !details->captured() && disclosures == 0,
                "Releasing a header press over the expanded body cancels disclosure");
            pointer(WM_LBUTTONDOWN, header_y, MK_LBUTTON);
            pointer(WM_LBUTTONUP, header_y);
            require(!details->expanded() && disclosures == 1, "Releasing over the header commits disclosure once");
            details->on_change({});
            auto choices = std::make_shared<xui::RadioGroup>(L"Choice geometry", true);
            choices->set_items({{1, L"One"}, {2, L"Two"}});
            root->add(choices);
            paint();
            const auto choices_window = FindWindowExW(titled.result, nullptr, L"Xui.Control.1", L"Choice geometry");
            require(choices_window != nullptr, "The choice geometry fixture has a native peer");
            auto snapshot = std::make_shared<xui::ControlAccessibility>();
            Microsoft::WRL::ComPtr<IRawElementProviderSimple> provider;
            provider.Attach(xui::create_workspace_provider(snapshot));
            Microsoft::WRL::ComPtr<IRawElementProviderFragment> fragment, row;
            require(SUCCEEDED(provider.As(&fragment)), "Read the choice root fragment");
            for (const auto style : {xui::VisualStyle::winui, xui::VisualStyle::classic, xui::VisualStyle::winui}) {
                window.set_visual_style(style);
                paint();
                xui::publish_control(snapshot, nullptr, *choices, choices_window);
                row.Reset();
                require(SUCCEEDED(fragment->Navigate(NavigateDirection_FirstChild, &row)) && row,
                    "Read the first accessible choice");
                UiaRect actual{};
                RECT native{};
                require(SUCCEEDED(row->get_BoundingRectangle(&actual)) && GetWindowRect(choices_window, &native),
                    "Read native and accessible choice geometry");
                const auto body = choices->item_bounds(0);
                const auto scale = GetDpiForWindow(choices_window) / 96.0f;
                require(actual.left == native.left + std::lround(body.x * scale) &&
                    actual.width == std::lround((body.x + body.width) * scale) - std::lround(body.x * scale) &&
                    actual.top == native.top + std::lround(body.y * scale) &&
                    actual.height == std::lround((body.y + body.height) * scale) - std::lround(body.y * scale),
                    "UI Automation exposes the painted choice body, including its style-specific margins");
            }
            choices->set_selected(1);
            const auto second_choice = choices->item_bounds(1);
            const auto click_choice = [&](float x) {
                const auto scale = GetDpiForWindow(choices_window) / 96.0f;
                const auto point = MAKELPARAM(std::lround(x * scale),
                    std::lround((second_choice.y + second_choice.height / 2) * scale));
                SendMessageW(choices_window, WM_LBUTTONDOWN, MK_LBUTTON, point);
                SendMessageW(choices_window, WM_LBUTTONUP, 0, point);
            };
            click_choice(second_choice.x / 2);
            require(choices->selected() == 1, "A click in the WinUI item margin does not select the row");
            click_choice(second_choice.x + 2);
            require(choices->selected() == 2, "A click in the WinUI item body selects the row");
            bool invalid_live_style{};
            try { window.set_visual_style(static_cast<xui::VisualStyle>(-1)); }
            catch (const std::invalid_argument&) { invalid_live_style = true; }
            require(invalid_live_style && window.visual_style() == xui::VisualStyle::winui,
                "Live invalid visual style is rejected without changing the property");
            window.close();
        });
        require(drive(window, L"XUI lifecycle normal") == 0, "Callback closes the window");
        require(window.title() == L"Updated during run", "Title remains readable after run");
        require(window.visual_style() == xui::VisualStyle::winui, "Visual style remains readable after run");
        bool closed_style{};
        try { window.set_visual_style(xui::VisualStyle::classic); }
        catch (const std::logic_error&) { closed_style = true; }
        require(closed_style && window.visual_style() == xui::VisualStyle::winui,
            "Closed window rejects visual style mutation");
        require(xui::Drawing::live_targets() == 0, "Visual style window closes without retained Direct2D targets");
        bool closed_title{};
        try { window.set_title(L"After close"); } catch (const std::logic_error&) { closed_title = true; }
        require(closed_title, "Closed window rejects title mutation");
        retained->on_click({});
        require(xui::Application::run(window) == 1, "Window cannot run twice");
        root->set_invalidator([&](xui::Invalidation) { ++later_invalidations; });
    }
    retained->set_name(L"Retained after window destruction");
    retained->set_preferred_size({200, 44});
    require(retained->id() == identity && !retained->focused() && !retained->captured(),
        "Retained control is detached after window destruction");
    require(later_invalidations == 2, "A detached window must not clear a later invalidator");
    {
        xui::Window window({L"XUI lifecycle invalid"});
        auto root = std::make_shared<xui::Stack>(xui::Axis::vertical);
        root->add(std::make_shared<xui::Element>());
        window.set_content(root);
        require(xui::Application::run(window) == 1, "Unsupported content fails without a leaked window");
    }
    for (const bool throws : {true, false}) {
        const auto title = throws ? L"XUI lifecycle callback failure" : L"XUI lifecycle recovered";
        xui::Window window({title});
        auto root = std::make_shared<xui::Stack>(xui::Axis::vertical);
        auto button = std::make_shared<xui::Button>(L"Close");
        button->on_click([&] {
            if (throws) throw std::runtime_error("Test callback failure");
            window.close();
        });
        root->add(button);
        window.set_content(root);
        require(drive(window, title) == (throws ? 1 : 0), "Callback failure stops only its own run");
    }
}
void navigation_input() {
    using namespace xui;
    Window window({L"XUI navigation input", {900, 500}});
    auto left = std::make_shared<Stack>(Axis::vertical), right = std::make_shared<Stack>(Axis::vertical);
    auto address = std::make_shared<TextInput>(L"Navigation address");
    auto search = std::make_shared<TextInput>(L"Navigation search");
    auto list = std::make_shared<FileList>(L"Navigation list");
    auto tabs = std::make_shared<TabStrip>(L"Navigation tabs");
    auto button = std::make_shared<Button>(L"Navigation button");
    address->set_text(L"Selection stays");
    left->add(address); left->add(list, 1);
    right->add(tabs); right->add(search); right->add(button);
    auto split = std::make_shared<SplitView>(left, right);
    auto root = std::make_shared<Stack>(Axis::vertical);
    root->add(split, 1); window.set_content(root);
    std::vector<NavigationEvent> events;
    window.on_navigation([&](const NavigationEvent& event) { events.push_back(event); return true; });
    bool completed{};
    window.on_key([&](const KeyEvent& event) {
        if (event.key != Key::f12) return false;
        Search host{L"XUI navigation input"}; EnumWindows(find, reinterpret_cast<LPARAM>(&host));
        require(host.result != nullptr, "Navigation host exists");
        const auto native = [&](const wchar_t* name) {
            struct Match { const wchar_t* name; HWND window{}; } match{name};
            EnumChildWindows(host.result, [](HWND hwnd, LPARAM data) -> BOOL {
                auto& match = *reinterpret_cast<Match*>(data);
                wchar_t title[128]{}; GetWindowTextW(hwnd, title, 128);
                if (std::wstring_view(title) == match.name) { match.window = hwnd; return FALSE; }
                return TRUE;
            }, reinterpret_cast<LPARAM>(&match));
            require(match.window != nullptr, "Navigation native peer exists"); return match.window;
        };
        require(window.focus(*address), "Focus navigation EDIT");
        const auto edit = GetFocus();
        SendMessageW(edit, EM_SETSEL, 2, 6);
        const auto gesture = [&](HWND hwnd, Control* target) {
            for (const WORD button : {WORD(XBUTTON1), WORD(XBUTTON2)}) {
                const auto count = events.size();
                const auto flags = MAKEWPARAM(button == XBUTTON1 ? MK_XBUTTON1 : MK_XBUTTON2, button);
                require(SendMessageW(hwnd, WM_XBUTTONDOWN, flags, MAKELPARAM(12, 12)) == TRUE &&
                    events.size() == count, "Button-down is consumed without navigation");
                require(SendMessageW(hwnd, WM_XBUTTONUP, MAKEWPARAM(0, button), MAKELPARAM(12, 12)) == TRUE,
                    "Button-up reports handled");
                require(events.size() == count + 1 && events.back().target == target && events.back().position &&
                    events.back().direction == (button == XBUTTON1 ? NavigationDirection::back : NavigationDirection::forward),
                    "Each native click dispatches once with the owning target");
                SendMessageW(GetParent(hwnd), WM_PARENTNOTIFY, MAKEWPARAM(WM_XBUTTONDOWN, button), MAKELPARAM(12, 12));
                require(events.size() == count + 1, "Parent notification never duplicates a navigation");
            }
        };
        gesture(edit, address.get());
        require(window.focus(*search), "Find native search EDIT");
        const auto search_edit = GetFocus();
        require(window.focus(*address), "Restore EDIT focus");
        SendMessageW(edit, EM_SETSEL, 2, 6);
        gesture(search_edit, search.get());
        gesture(native(L"Navigation list"), list.get());
        gesture(native(L"Navigation tabs"), tabs.get());
        gesture(native(L"Navigation button"), button.get());
        const auto double_count = events.size();
        require(SendMessageW(edit, WM_XBUTTONDBLCLK, MAKEWPARAM(MK_XBUTTON1, XBUTTON1), MAKELPARAM(12, 12)) == TRUE &&
            events.size() == double_count, "Double-click down does not dispatch a second command");
        SendMessageW(edit, WM_XBUTTONUP, MAKEWPARAM(0, XBUTTON1), MAKELPARAM(12, 12));
        require(events.size() == double_count + 1, "Second click dispatches once on release");
        DWORD start{}, end{};
        SendMessageW(edit, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
        require(GetFocus() == edit && start == 2 && end == 6 && address->text() == L"Selection stays",
            "Mouse browser navigation preserves native EDIT focus, selection and text");
        POINT point{12, 12}; MapWindowPoints(search_edit, host.result, &point, 1);
        const auto count = events.size();
        SendMessageW(host.result, WM_XBUTTONDOWN, MAKEWPARAM(MK_XBUTTON1, XBUTTON1), MAKELPARAM(point.x, point.y));
        SendMessageW(host.result, WM_XBUTTONUP, MAKEWPARAM(0, XBUTTON1), MAKELPARAM(point.x, point.y));
        require(events.size() == count + 1 && events.back().target == search.get(),
            "Root coordinates identify a control in the inactive pane");
        for (const auto hwnd : {search_edit, native(L"Navigation list"), native(L"Navigation tabs")}) {
            const auto before = events.size();
            require(SendMessageW(hwnd, WM_APPCOMMAND, reinterpret_cast<WPARAM>(hwnd),
                MAKELPARAM(0, APPCOMMAND_BROWSER_FORWARD)) == TRUE, "Application command is consumed at the peer");
            require(events.size() == before + 1 && events.back().direction == NavigationDirection::forward &&
                !events.back().position, "Application command dispatches once without mouse coordinates");
        }
        completed = true;
        window.close();
        return true;
    });
    std::jthread driver([&] {
        for (int i = 0; i < 250; ++i) {
            Search host{L"XUI navigation input"}; EnumWindows(find, reinterpret_cast<LPARAM>(&host));
            if (host.result) { PostMessageW(host.result, WM_KEYDOWN, VK_F12, 0); return; }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    });
    const auto result = Application::run(window);
    driver.join();
    if (result) std::wcerr << window.error() << L'\n';
    require(result == 0 && completed, "Navigation input checks complete");
}
void retained_page_resources() {
    using namespace xui;
    Window window({L"XUI retained page resources", {780, 620}});
    auto root = std::make_shared<Stack>(Axis::vertical);
    auto anchor = std::make_shared<Button>(L"Page anchor"); root->add(anchor);
    auto pages = std::make_shared<PageView>(); root->add(pages, 1);
    auto home = std::make_shared<Stack>(Axis::vertical);
    auto edit = std::make_shared<TextInput>(L"Retained editor"); edit->set_text(L"original");
    home->add(edit); home->add(std::make_shared<Label>(L"Home page")); pages->add_page(home);
    auto large = std::make_shared<Stack>(Axis::vertical);
    auto tall = std::make_shared<TextInput>(L"Tall editor"); tall->set_preferred_size({700, 300}); large->add(tall);
    for (int i = 0; i < 20; ++i) large->add(std::make_shared<Label>(L"Measured page label " + std::to_wstring(i)));
    pages->add_page(large);
    auto canvas = std::make_shared<VectorCanvas>();
    std::vector<VectorShape> shapes;
    for (int i = 0; i < 40; ++i) shapes.push_back(VectorShape::rectangle(i + 1, {float(i * 10), 10, 8, 80}));
    const auto scene = std::make_shared<const VectorScene>(std::move(shapes)); canvas->set_scene(scene); pages->add_page(canvas);
    window.set_content(root);
    bool completed{};
    window.on_key([&](const KeyEvent& event) {
        if (event.key != Key::f12) return false;
        Search host{L"XUI retained page resources"}; EnumWindows(find, reinterpret_cast<LPARAM>(&host));
        require(host.result != nullptr, "Retained page host exists");
        const auto metric = [&](int key) { return SendMessageW(host.result, WM_APP + 60, key, 0); };
        const auto flush = [&] {
            SendMessageW(host.result, WM_APP + 12, 0, 0);
            InvalidateRect(host.result, nullptr, FALSE); UpdateWindow(host.result);
        };
        flush();
        const auto initial_peers = metric(14), initial_layouts = metric(13), initial_buffer = metric(30);
        require(initial_peers < 10 && initial_buffer > 0, "Inactive pages do not eagerly create native peers");
        require(window.focus(*edit), "Focus retained native editor");
        const auto native = GetFocus();
        SendMessageW(native, EM_SETSEL, 8, 8);
        SendMessageW(native, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"-edit"));
        SendMessageW(native, EM_SETSEL, 2, 6);
        require(edit->text() == L"original-edit" && SendMessageW(native, EM_CANUNDO, 0, 0), "Native editor owns text and undo");
        pages->select(1); flush();
        const auto warm_peers = metric(14);
        require(warm_peers >= initial_peers + 20 && metric(30) > initial_buffer,
            "First page visit materializes peers and a larger native composition buffer");
        require(metric(13) > initial_layouts, "Visible page creates measured label layouts");
        pages->select(2); flush();
        require(metric(32) == 40, "Visible scene caches its native paths");
        const auto all_peers = metric(14);
        for (int cycle = 0; cycle < 3; ++cycle) {
            pages->select(0); flush();
            require(metric(13) == initial_layouts && metric(30) == initial_buffer && metric(32) == 0,
                "Inactive text layouts, scene paths and oversized native buffer are reclaimed");
            require(metric(14) == all_peers && IsWindow(native), "Visited native editor and retained peer identities survive hiding");
            pages->select(1); flush();
            pages->select(2); flush();
            require(metric(32) == 40 && canvas->scene() == scene, "Native geometry rebuild preserves the retained scene model");
        }
        pages->select(0); flush();
        require(window.focus(*edit) && GetFocus() == native, "Page return restores the same EDIT HWND");
        DWORD start{}, end{};
        SendMessageW(native, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
        require(start == 2 && end == 6 && SendMessageW(native, EM_CANUNDO, 0, 0),
            "Page reclamation preserves native selection and undo history");
        SendMessageW(native, EM_UNDO, 0, 0);
        require(edit->text() == L"original", "Undo after hiding restores the original model text");
        completed = true; window.close(); return true;
    });
    std::jthread driver([&](std::stop_token stop) {
        while (!stop.stop_requested()) {
            Search host{L"XUI retained page resources"}; EnumWindows(find, reinterpret_cast<LPARAM>(&host));
            if (host.result) { PostMessageW(host.result, WM_KEYDOWN, VK_F12, 0); return; }
            Sleep(10);
        }
    });
    const auto result = Application::run(window);
    driver.request_stop(); driver.join();
    if (result) std::wcerr << window.error() << '\n';
    require(result == 0 && completed && Drawing::live_targets() == 0, "Retained page resources close without a target leak");
}
void resource_lifecycle(bool diagnostics = false, bool scroll_content = false, bool split_content = false) {
    std::vector<HANDLE> previous_handles;
    DWORD handles_after_warmup{}, gdi_after_warmup{}, user_after_warmup{};
    for (int cycle = 0; cycle < 8; ++cycle) {
        {
            xui::Window window({L"XUI resource lifecycle", split_content ? xui::Size{924, 641} : xui::Size{640, 640}});
            auto root = std::make_shared<xui::Stack>(xui::Axis::vertical);
            for (int i = 0; i < 16; ++i) {
                auto label = std::make_shared<xui::Label>(L"Shared renderer label");
                label->set_preferred_size({0, 12});
                root->add(label);
            }
            auto input = std::make_shared<xui::TextInput>(L"Native input");
            auto button = std::make_shared<xui::Button>(L"Focus");
            auto toggle = std::make_shared<xui::Toggle>(L"Toggle");
            auto list = std::make_shared<xui::FileList>();
            root->add(input);
            root->add(button);
            root->add(toggle);
            root->add(list, 1);
            std::shared_ptr<xui::ScrollView> viewport;
            std::shared_ptr<xui::SplitView> split;
            std::shared_ptr<xui::TabStrip> tabs;
            std::shared_ptr<xui::FileList> second_list;
            if (split_content) {
                auto secondary = std::make_shared<xui::Stack>(xui::Axis::vertical);
                tabs = std::make_shared<xui::TabStrip>(L"Resource tabs");
                tabs->set_tabs({{1, L"One"}, {2, L"Two"}}, 1);
                secondary->add(tabs);
                secondary->add(std::make_shared<xui::TextInput>(L"Secondary input"));
                second_list = std::make_shared<xui::FileList>(L"Secondary list");
                secondary->add(second_list, 1);
                split = std::make_shared<xui::SplitView>(root, secondary, L"Resource divider");
                auto outer = std::make_shared<xui::Stack>(xui::Axis::vertical);
                outer->add(split, 1);
                window.set_content(outer);
            } else if (scroll_content) {
                viewport = std::make_shared<xui::ScrollView>(root, L"Resource viewport");
                auto outer = std::make_shared<xui::Stack>(xui::Axis::vertical);
                outer->add(viewport, 1);
                window.set_content(outer);
            } else window.set_content(root);
            auto task = window.create_view_task(
                [](const xui::CancelCheck &cancel) {
                    auto items = std::make_shared<std::vector<xui::FileItem>>();
                    for (int i = 0; i < 60; ++i)
                        items->push_back(
                            {static_cast<xui::ItemId>(i + 1), L"Entry " + std::to_wstring(i), L"path", false});
                    return xui::SourceResult{xui::FileSnapshot::build(std::move(items), cancel), {}};
                },
                [list, second_list](xui::ViewResult result) {
                    if (result.view) {
                        if (second_list) second_list->set_view(result.view);
                        list->set_view(std::move(result.view));
                    }
                });
            task->request(L"");
            int resize_step{};
            window.on_key([&](const xui::KeyEvent &key) {
                if (key.key == xui::Key::f5) {
                    for (int i = 0; i < 100; ++i)
                        task->request(L"obsolete", true);
                    task->request(L"", true);
                    return true;
                }
                if (key.key == xui::Key::f6) {
                    window.set_theme(window.theme() == xui::ThemeMode::dark ? xui::ThemeMode::light
                                                                            : xui::ThemeMode::dark);
                    window.focus(*button);
                    if (viewport) viewport->scroll_by(32);
                    toggle->set_checked(!toggle->checked());
                    Search search{L"XUI resource lifecycle"};
                    EnumWindows(find, reinterpret_cast<LPARAM>(&search));
                    HWND divider{};
                    if (split) {
                        split->set_ratio(0.3f + resize_step * 0.1f);
                        tabs->step(1);
                        divider = FindWindowExW(search.result, nullptr, L"Xui.Control.1", L"Resource divider");
                        require(divider != nullptr, "SplitView has one native input peer");
                        SetCapture(divider);
                    }
                    SetWindowPos(search.result, nullptr, 0, 0, 650 + resize_step * 11, 660 + resize_step * 13,
                                 SWP_NOMOVE | SWP_NOZORDER);
                    const UINT scale[] = {96, 120, 144, 192};
                    RECT suggested{40, 40, 720, 940};
                    SendMessageW(search.result, WM_DPICHANGED,
                                 MAKEWPARAM(scale[resize_step % 4], scale[resize_step % 4]),
                                 reinterpret_cast<LPARAM>(&suggested));
                    if (split) {
                        require(GetCapture() != divider, "DPI transition releases splitter capture");
                        SendMessageW(search.result, WM_APP + 12, 0, 0);
                        const auto second = FindWindowExW(divider, nullptr, L"Xui.Control.1", L"Right pane");
                        const auto edit = FindWindowExW(second, nullptr, L"EDIT", nullptr);
                        require(edit != nullptr, "SplitView retains a native secondary EDIT");
                        require((IsWindowVisible(edit) != FALSE) == split->expanded(),
                            "DPI layout clips and hides native children in a collapsed pane");
                        if (split->expanded()) {
                            RECT pane_bounds{}, edit_bounds{};
                            GetWindowRect(second, &pane_bounds); GetWindowRect(edit, &edit_bounds);
                            require(second_list->bounds().width > 0 && edit_bounds.left >= pane_bounds.left &&
                                edit_bounds.right <= pane_bounds.right && edit_bounds.top >= pane_bounds.top &&
                                edit_bounds.bottom <= pane_bounds.bottom, "DPI layout contains native EDIT within its pane");
                        }
                    }
                    SendMessageW(search.result, WM_DISPLAYCHANGE, 0, 0);
                    ++resize_step;
                    return true;
                }
                return false;
            });
            std::exception_ptr failure;
            std::jthread driver([&] {
                HWND hwnd{};
                try {
                    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
                    const auto wait = [&](auto predicate) {
                        while (!predicate()) {
                            require(std::chrono::steady_clock::now() < deadline, "Resource lifecycle timed out");
                            std::this_thread::sleep_for(std::chrono::milliseconds(5));
                        }
                    };
                    wait([&] {
                        Search search{L"XUI resource lifecycle"};
                        EnumWindows(find, reinterpret_cast<LPARAM>(&search));
                        hwnd = search.result;
                        return hwnd != nullptr;
                    });
                    const auto metric = [&](int key) { return SendMessageW(hwnd, WM_APP + 60, key, 0); };
                    wait([&] { return metric(9) == 60 && metric(4) == 0; });
                    for (int step = 0; step < 4; ++step) {
                        const auto generation = metric(3);
                        const auto prior_paints = metric(0);
                        PostMessageW(hwnd, WM_KEYDOWN, VK_F5, 0);
                        PostMessageW(hwnd, WM_KEYDOWN, VK_F6, 0);
                        wait([&] { return metric(3) > generation && metric(4) == 0; });
                        wait([&] { return metric(11) == 1 && metric(0) > prior_paints; });
                        std::this_thread::sleep_for(std::chrono::milliseconds(50));
                        require(metric(11) == 1, "Labels, buttons, toggles and lists must share one host target");
                        require(metric(8) == 60, "Repeated cancelled refreshes retain the latest complete view");
                    }
                    auto paints = metric(0);
                    auto quiet_since = std::chrono::steady_clock::now();
                    wait([&] {
                        const auto now = std::chrono::steady_clock::now();
                        const auto current = metric(0);
                        if (current != paints) {
                            paints = current;
                            quiet_since = now;
                        }
                        return now - quiet_since >= std::chrono::milliseconds(200);
                    });
                    if (diagnostics && cycle == 2) {
                        std::cout << "open_window ";
                        resource_probe::heaps();
                    }
                } catch (...) {
                    failure = std::current_exception();
                }
                if (hwnd) PostMessageW(hwnd, WM_CLOSE, 0, 0);
            });
            require(xui::Application::run(window) == 0, "Resource stress window completes");
            require(xui::Drawing::live_targets() == 0, "A retained closed Window must not retain graphics targets");
            driver.join();
            if (failure) std::rethrow_exception(failure);
        }
        std::atomic<bool> drained{};
        xui::dispose_later(std::shared_ptr<const void>(new int, [&](const void *value) {
            delete static_cast<const int *>(value);
            drained = true;
        }));
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!drained) {
            require(std::chrono::steady_clock::now() < deadline, "Resource cleanup must finish off the UI thread");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        require(xui::Drawing::live_targets() == 0, "Window destruction releases every XUI render target");
        Search search{L"XUI resource lifecycle"};
        EnumWindows(find, reinterpret_cast<LPARAM>(&search));
        require(!search.result, "Window destruction releases native hosts");
        DWORD handles{};
        require(GetProcessHandleCount(GetCurrentProcess(), &handles) != 0, "Read lifecycle handle count");
        const auto gdi = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
        const auto user = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
        if (cycle == 2) {
            handles_after_warmup = handles;
            gdi_after_warmup = gdi;
            user_after_warmup = user;
        } else if (cycle > 2) {
            require(gdi <= gdi_after_warmup + 2 && user <= user_after_warmup + 2,
                    "Repeated windows must not retain GDI or USER objects");
            require(handles <= handles_after_warmup + 2, "Repeated windows must not retain process handles");
        }
        std::cout << "resource_cycle=" << cycle << " scroll=" << scroll_content << " split=" << split_content << " targets=" << xui::Drawing::live_targets() << " handles=" << handles
                  << " gdi=" << gdi << " user=" << user << '\n';
        if (diagnostics) {
            resource_probe::heaps();
            previous_handles = resource_probe::handle_difference(previous_handles);
        }
    }
}
void public_list_delivery() {
    const auto ui_thread = GetCurrentThreadId();
    std::atomic<DWORD> disposed_thread{};
    int deliveries{}, selections{};
    {
        xui::Window window({L"XUI public list delivery"});
        auto root = std::make_shared<xui::Stack>(xui::Axis::vertical);
        auto label = std::make_shared<xui::Label>(L"Results");
        auto input = std::make_shared<xui::TextInput>(L"Filter");
        auto button = std::make_shared<xui::Button>(L"Next");
        auto list = std::make_shared<xui::FileList>(L"Public results");
        root->add(label);
        root->add(input);
        root->add(button);
        root->add(list, 1);
        window.set_content(root);
        std::shared_ptr<xui::ViewTask> task;
        list->on_selection_change([&] { ++selections; label->set_text(L"Selected"); });
        button->on_click([list] { list->navigate(xui::Navigation::next); });
        input->on_submit([&] { window.focus(*list); });
        task = window.create_view_task([&](const xui::CancelCheck&) {
            require(GetCurrentThreadId() != ui_thread, "Loader must run off the UI thread");
            auto items = std::shared_ptr<const std::vector<xui::FileItem>>(
                new std::vector<xui::FileItem>{{1, L"Alpha", L"A", false}, {2, L"Beta", L"B", false}},
                [&](const std::vector<xui::FileItem>* value) { disposed_thread = GetCurrentThreadId(); delete value; });
            return xui::SourceResult{xui::FileSnapshot::build(std::move(items)), {}};
        }, [&](xui::ViewResult result) {
            require(GetCurrentThreadId() == ui_thread, "Result delivery must use the Window thread");
            ++deliveries;
            list->set_view(std::move(result.view));
            require(result.generation == task->generation(), "Only the latest generation is delivered");
            if (deliveries == 1) {
                require(list->model().view()->query() == L"alpha", "Initial asynchronous filter is applied");
                require(window.focus(*input), "Window hosts a native input beside the list");
                input->submit();
                require(list->focused() && list->viewport_height() > 0, "Window hosts the public list and its viewport");
                button->invoke();
                require(selections == 1 && label->text() == L"Selected", "List events update a sibling label");
                task->request(L"missing");
                task->request(L"Beta");
            } else {
                require(deliveries == 2 && list->model().view()->query() == L"beta" &&
                    list->model().visible_indices().size() == 1, "Superseded filters cannot reach application callbacks");
                require(!list->model().selected_index() && list->model().selected_id() == 1,
                    "Delivery preserves the hidden selection identity");
                window.close();
                require(task->request(L"late") == 0, "Reentrant close immediately revokes new requests");
                require(!window.focus(*button), "Closing windows reject new focus requests");
            }
        });
        task->request(L"Alpha");
        const auto result = xui::Application::run(window);
        if (result) std::wcerr << L"Public delivery: " << window.error() << L'\n';
        require(result == 0 && deliveries == 2, "Public asynchronous composition completes");
        require(!list->focused() && list->model().selected_id() == 1, "Retained list is detached with its model intact");
        list->on_selection_change({});
        input->on_submit({});
    }
    const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!disposed_thread && std::chrono::steady_clock::now() < limit) std::this_thread::yield();
    require(disposed_thread && disposed_thread != ui_thread, "Final list snapshot disposal stays off the UI thread");
}

void cancellation_without_join() {
    std::atomic<bool> entered{}, cancelled{}, release{}, finished{};
    std::atomic<int> callbacks{};
    bool returned{};
    {
        xui::Window window({L"XUI cancellation without UI join"});
        auto root = std::make_shared<xui::Stack>(xui::Axis::vertical);
        root->add(std::make_shared<xui::Label>(L"Cancellation"));
        window.set_content(root);
        auto blocked = window.create_view_task([&](const xui::CancelCheck& cancel) -> xui::SourceResult {
            entered = true;
            while (!cancel()) std::this_thread::yield();
            cancelled = true;
            while (!release) std::this_thread::yield();
            finished = true;
            return {};
        }, [&](xui::ViewResult) { ++callbacks; });
        blocked->request(L"", true);
        auto closer = window.create_view_task([&](const xui::CancelCheck&) {
            while (!entered) std::this_thread::yield();
            return xui::SourceResult{xui::FileSnapshot::build(nullptr), {}};
        }, [&](xui::ViewResult) { window.close(); });
        closer->request(L"");
        // The watchdog prevents a broken join implementation from hanging the suite.
        std::jthread watchdog([&](std::stop_token stop) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (!stop.stop_requested() && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            if (!stop.stop_requested()) release = true;
        });
        const auto result = xui::Application::run(window);
        returned = !release && !finished;
        release = true;
        watchdog.request_stop();
        require(result == 0, "Close from result delivery succeeds");
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!finished && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
    require(returned && cancelled && finished && callbacks == 0,
        "Window closure cancels and revokes results without joining a blocked loader");
}
int run_public_list_server() {
    xui::Window window({L"XUI public provider lifetime"});
    auto root = std::make_shared<xui::Stack>(xui::Axis::vertical);
    auto list = std::make_shared<xui::FileList>(L"Public result list");
    list->set_automation_id(L"public-result-list");
    auto items = std::make_shared<std::vector<xui::FileItem>>(
        std::vector<xui::FileItem>{{811, L"One", L"one", false}, {922, L"Two", L"two", false}});
    for (int i = 0; i < 58; ++i) items->push_back({static_cast<xui::ItemId>(1000 + i), L"Extra", L"extra", false});
    list->set_items(std::move(items));
    bool filtered{};
    window.on_key([&](const xui::KeyEvent& event) {
        if (event.key == xui::Key::f1) { list->set_name(L"Renamed results"); return true; }
        if (event.key == xui::Key::f2) { list->set_enabled(!list->enabled()); return true; }
        if (event.key == xui::Key::f3) { filtered = !filtered; list->set_filter(filtered ? L"Two" : L""); return true; }
        return false;
    });
    root->add(std::make_shared<xui::Label>(L"Provider lifetime"));
    root->add(std::make_shared<xui::TextInput>(L"Query"));
    root->add(std::make_shared<xui::Button>(L"Action"));
    root->add(list, 1);
    window.set_content(root);
    return xui::Application::run(window);
}
void public_list_provider_lifetime() {
    using Microsoft::WRL::ComPtr;
    struct Process {
        PROCESS_INFORMATION info{};
        ~Process() {
            if (info.hProcess) {
                if (WaitForSingleObject(info.hProcess, 5000) == WAIT_TIMEOUT) TerminateProcess(info.hProcess, 1);
                CloseHandle(info.hProcess);
                CloseHandle(info.hThread);
            }
        }
    } process;
    wchar_t executable[32768]{};
    require(GetModuleFileNameW(nullptr, executable, 32768) != 0, "Locate public provider server");
    std::wstring command = L"\"" + std::wstring(executable) + L"\" --public-list-server";
    STARTUPINFOW startup{sizeof(startup)};
    require(CreateProcessW(executable, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr,
        &startup, &process.info) != 0, "Start isolated public Window provider server");
    std::exception_ptr failure;
    require(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)), "Initialize public UIA client");
    HWND hwnd{};
    try {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!hwnd && std::chrono::steady_clock::now() < deadline) {
            Search search{L"XUI public provider lifetime"};
            search.process = process.info.dwProcessId;
            EnumWindows(find, reinterpret_cast<LPARAM>(&search));
            hwnd = search.result;
            if (!hwnd) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        require(hwnd != nullptr, "Public list window appears");
        ComPtr<IUIAutomation> automation;
        require(SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&automation))), "Create public control UIA client");
        ComPtr<IUIAutomationElement> element, host;
        require(SUCCEEDED(automation->ElementFromHandle(hwnd, &host)), "Read public Window accessibility tree");
        VARIANT id{};
        id.vt = VT_BSTR;
        id.bstrVal = SysAllocString(L"public-result-list");
        ComPtr<IUIAutomationCondition> condition;
        const auto created = automation->CreatePropertyCondition(UIA_AutomationIdPropertyId, id, &condition);
        VariantClear(&id);
        require(SUCCEEDED(created), "Create public identity condition");
        const auto found = host->FindFirst(TreeScope_Descendants, condition.Get(), &element);
        require(SUCCEEDED(found) && element, "Public list appears in the Window tree by its public identity");
        BSTR text{};
        require(SUCCEEDED(element->get_CurrentName(&text)), "Read public list name");
        const bool named = std::wstring(text ? text : L"") == L"Public result list";
        SysFreeString(text);
        require(named, "Public list name is not a browser-specific caption");
        require(SUCCEEDED(element->get_CurrentAutomationId(&text)), "Read public list automation ID");
        const bool identified = std::wstring(text ? text : L"") == L"public-result-list";
        SysFreeString(text);
        require(identified, "Public list uses its own automation identity");
        const auto wait_for = [&](auto predicate, const char* message) {
            const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(4);
            while (!predicate()) {
                require(std::chrono::steady_clock::now() < end, message);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        };
        uia_test::Subscription subscription{automation};
        ComPtr<uia_test::Events> events;
        events.Attach(new uia_test::Events(process.info.dwProcessId));
        ComPtr<IUIAutomationCacheRequest> cache;
        require(SUCCEEDED(automation->CreateCacheRequest(&cache)) &&
            SUCCEEDED(cache->AddProperty(UIA_ProcessIdPropertyId)) &&
            SUCCEEDED(cache->AddProperty(UIA_NamePropertyId)), "Cache event identities");
        PROPERTYID properties[]{UIA_NamePropertyId, UIA_IsEnabledPropertyId, UIA_HasKeyboardFocusPropertyId,
            UIA_SelectionItemIsSelectedPropertyId, UIA_ScrollVerticalScrollPercentPropertyId,
            UIA_ScrollVerticalViewSizePropertyId, UIA_ScrollVerticallyScrollablePropertyId};
        require(SUCCEEDED(automation->AddPropertyChangedEventHandlerNativeArray(element.Get(), TreeScope_Subtree,
            cache.Get(), events.Get(), properties, static_cast<int>(std::size(properties)))), "Subscribe list property events");
        for (const auto event : {UIA_SelectionItem_ElementSelectedEventId, UIA_SelectionItem_ElementRemovedFromSelectionEventId})
            require(SUCCEEDED(automation->AddAutomationEventHandler(event, element.Get(), TreeScope_Subtree,
                cache.Get(), events.Get())), "Subscribe list selection events");
        require(SUCCEEDED(automation->AddStructureChangedEventHandler(element.Get(), TreeScope_Subtree,
            cache.Get(), events.Get())), "Subscribe list structure events");
        ComPtr<IUIAutomationTreeWalker> walker;
        require(SUCCEEDED(automation->get_ControlViewWalker(&walker)), "Read public list fragments");
        ComPtr<IUIAutomationElement> first, second;
        require(SUCCEEDED(walker->GetFirstChildElement(element.Get(), &first)) && first, "Read first public row");
        require(SUCCEEDED(walker->GetNextSiblingElement(first.Get(), &second)) && second, "Read second public row");
        ComPtr<IUIAutomationSelectionItemPattern> selection;
        require(SUCCEEDED(first->GetCurrentPatternAs(UIA_SelectionItemPatternId, IID_PPV_ARGS(&selection))),
            "Public row exposes selection");
        require(SUCCEEDED(selection->Select()) && SUCCEEDED(second->SetFocus()), "Public list supports independent UIA actions");
        BOOL selected{}, first_focus{}, second_focus{};
        require(SUCCEEDED(selection->get_CurrentIsSelected(&selected)) && selected &&
            SUCCEEDED(first->get_CurrentHasKeyboardFocus(&first_focus)) && !first_focus &&
            SUCCEEDED(second->get_CurrentHasKeyboardFocus(&second_focus)) && second_focus,
            "Keyboard focus is separate from selection on the public control");
        wait_for([&] {
            return events->boolean(UIA_SelectionItemIsSelectedPropertyId, L"One", true) &&
                events->boolean(UIA_HasKeyboardFocusPropertyId, L"Two", true) &&
                events->count(UIA_SelectionItem_ElementSelectedEventId, L"One");
        }, "External client receives independent selection and focus properties");
        require(SUCCEEDED(selection->RemoveFromSelection()), "Optional selection supports removal");
        wait_for([&] {
            return events->boolean(UIA_SelectionItemIsSelectedPropertyId, L"One", false) &&
                events->count(UIA_SelectionItem_ElementRemovedFromSelectionEventId, L"One");
        }, "External client receives explicit selection removal");
        require(SUCCEEDED(selection->Select()), "Restore stable selection before filtering");
        ComPtr<IUIAutomationScrollPattern> scroll;
        require(SUCCEEDED(element->GetCurrentPatternAs(UIA_ScrollPatternId, IID_PPV_ARGS(&scroll))) && scroll,
            "Public list exposes scroll");
        require(SUCCEEDED(scroll->SetScrollPercent(-1, 100)), "Scroll public list");
        wait_for([&] { return events->count(UIA_ScrollVerticalScrollPercentPropertyId); }, "External scroll event arrives");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        const auto scroll_events = events->count(UIA_ScrollVerticalScrollPercentPropertyId);
        for (int repeat = 0; repeat < 8; ++repeat)
            require(SUCCEEDED(scroll->SetScrollPercent(-1, 100)), "Repeated scroll endpoint succeeds");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        require(events->count(UIA_ScrollVerticalScrollPercentPropertyId) == scroll_events,
            "Unchanged scroll endpoints must not raise redundant property events");
        PostMessageW(hwnd, WM_KEYDOWN, VK_F3, 0);
        wait_for([&] { return events->count(UIA_StructureChangedEventId); }, "Filtering raises structure event");
        wait_for([&] {
            return events->boolean(UIA_ScrollVerticallyScrollablePropertyId, L"Public result list", false) &&
                events->count(UIA_ScrollVerticalViewSizePropertyId, L"Public result list");
        }, "Filtering publishes changed scroll availability and viewport fraction");
        require(selection->Select() == UIA_E_ELEMENTNOTAVAILABLE, "Hidden provider actions reject stale visible positions");
        PostMessageW(hwnd, WM_KEYDOWN, VK_F3, 0);
        wait_for([&] {
            BOOL restored{};
            return SUCCEEDED(selection->get_CurrentIsSelected(&restored)) && restored;
        }, "Filtering restores the hidden stable selection");
        PostMessageW(hwnd, WM_KEYDOWN, VK_F1, 0);
        wait_for([&] { return events->count(UIA_NamePropertyId, L"Renamed results"); }, "List rename raises name event");
        PostMessageW(hwnd, WM_KEYDOWN, VK_F2, 0);
        wait_for([&] { return events->boolean(UIA_IsEnabledPropertyId, L"Renamed results", false); },
            "List disable raises enabled event");
        require(selection->Select() == UIA_E_ELEMENTNOTENABLED, "Disabled list rejects retained selection actions");
        PostMessageW(hwnd, WM_KEYDOWN, VK_F2, 0);
        wait_for([&] { return events->boolean(UIA_IsEnabledPropertyId, L"Renamed results", true); },
            "List enable raises enabled event");
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
        require(WaitForSingleObject(process.info.hProcess, 5000) == WAIT_OBJECT_0, "Public provider server stops");
        DWORD code{};
        require(GetExitCodeProcess(process.info.hProcess, &code) && code == 0, "Public provider window closes normally");
        require(FAILED(selection->Select()) && FAILED(second->SetFocus()),
            "Retained public row providers reject actions after close");
    } catch (...) {
        failure = std::current_exception();
        if (hwnd) PostMessageW(hwnd, WM_CLOSE, 0, 0);
    }
    CoUninitialize();
    if (failure) std::rethrow_exception(failure);
}
}
int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--public-list-server") return run_public_list_server();
        if (argc == 2 && std::string(argv[1]) == "--style-lifecycle") {
            text_presentation();
            window_lifecycle();
            std::cout << "Visual style window lifecycle tests passed\n";
            return 0;
        }
        if (argc == 2 && std::string(argv[1]) == "--resources") { resource_lifecycle(true); return 0; }
        if (argc == 2 && std::string(argv[1]) == "--split-resources") { resource_lifecycle(false, false, true); return 0; }
        if (argc == 2 && std::string(argv[1]) == "--navigation-input") {
            navigation_input();
            std::cout << "Mouse and application-command navigation tests passed\n";
            return 0;
        }
        text_presentation();
        window_lifecycle();
        retained_page_resources();
        navigation_input();
        public_list_delivery();
        cancellation_without_join();
        resource_lifecycle();
        resource_lifecycle(false, true);
        public_list_provider_lifetime();
        std::cout << "Window lifecycle, public list, asynchronous delivery and disposal tests passed\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
