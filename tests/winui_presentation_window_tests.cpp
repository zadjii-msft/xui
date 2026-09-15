#include "xui/application.hpp"
#include "xui/documents.hpp"
#include "../src/drawing.hpp"
#include <windows.h>
#include <array>
#include <cmath>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace xui;
constexpr wchar_t title[] = L"XUI WinUI presentation contracts";
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void close_to(float actual, float expected, const char* message) {
    require(std::abs(actual - expected) < 0.01f, message);
}
void flush(HWND hwnd) {
    SendMessageW(hwnd, WM_APP + 12, 0, 0);
    require(RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW) != FALSE,
        "Paint the current presentation synchronously");
}
HWND owned_window() {
    HWND result{};
    EnumWindows([](HWND hwnd, LPARAM data) -> BOOL {
        DWORD process{};
        GetWindowThreadProcessId(hwnd, &process);
        wchar_t text[100]{};
        GetWindowTextW(hwnd, text, 100);
        if (process == GetCurrentProcessId() && std::wstring_view(text) == title && IsWindowVisible(hwnd)) {
            *reinterpret_cast<HWND*>(data) = hwnd;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&result));
    return result;
}
struct StartFixture {
    static inline thread_local StartFixture* current{};
    UINT_PTR timer{};
    ULONGLONG deadline{GetTickCount64() + 10000};
    bool posted{}, failed{};
    StartFixture() {
        current = this;
        timer = SetTimer(nullptr, 0, 10, [](HWND, UINT, UINT_PTR id, DWORD) {
            const auto hwnd = owned_window();
            if (hwnd) {
                KillTimer(nullptr, id);
                current->timer = 0;
                current->posted = PostMessageW(hwnd, WM_KEYDOWN, VK_F12, 0) != FALSE;
                if (!current->posted) { current->failed = true; PostQuitMessage(1); }
            } else if (GetTickCount64() >= current->deadline) {
                KillTimer(nullptr, id);
                current->timer = 0;
                current->failed = true;
                PostQuitMessage(1);
            }
        });
        require(timer != 0, "Schedule the bounded presentation fixture");
    }
    ~StartFixture() { if (timer) KillTimer(nullptr, timer); current = nullptr; }
};
std::wstring native_text(HWND hwnd) {
    std::wstring text(static_cast<std::size_t>(GetWindowTextLengthW(hwnd)) + 1, L'\0');
    text.resize(GetWindowTextW(hwnd, text.data(), static_cast<int>(text.size())));
    return text;
}
HWND focus_peer(Window& window, Control& control) {
    require(window.focus(control) && control.focused(), "Focus the retained control on its owner thread");
    const auto hwnd = GetFocus();
    require(hwnd != nullptr, "The control has a focused native peer");
    return hwnd;
}
struct NativeInput {
    HWND hwnd{};
    std::wstring name;
    std::wstring text;
    DWORD first{1}, last{3};
    NativeInput(Window& window, TextInput& input) : hwnd(focus_peer(window, input)), name(input.name()), text(input.text()) {
        wchar_t cls[32]{};
        require(GetClassNameW(hwnd, cls, 32) != 0, "Read the focused native input class");
        if (_wcsicmp(cls, L"EDIT") != 0 || text.size() < last)
            std::wcerr << L"Native input fixture: " << input.name() << L"; class=" << cls
                << L"; text length=" << text.size() << L'\n';
        require(_wcsicmp(cls, L"EDIT") == 0, "Use a real single-line EDIT fixture");
        require(text.size() >= last, "Native input fixture contains the requested selection range");
        SendMessageW(hwnd, EM_SETSEL, first, last);
        verify();
    }
    void verify() const {
        require(IsWindow(hwnd) && native_text(hwnd) == text, "Presentation preserves the native EDIT HWND and text");
        DWORD start{}, end{};
        SendMessageW(hwnd, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
        if (start != first || end != last)
            std::wcerr << L"Selection mismatch: " << name << L"; HWND=" << hwnd
                << L"; expected=" << first << L"," << last << L"; actual=" << start << L"," << end << L'\n';
        require(start == first && end == last, "Presentation preserves native EDIT selection endpoints");
    }
};
void centered(HWND hwnd, Rect popup, UINT dpi) {
    RECT client{};
    require(GetClientRect(hwnd, &client) != FALSE, "Read dialog owner client bounds");
    const float scale = dpi / 96.0f;
    const float center_x = (client.left + client.right) / (2 * scale);
    const float center_y = (client.top + client.bottom) / (2 * scale);
    require(std::abs(popup.x + popup.width / 2 - center_x) <= 1 / scale &&
        std::abs(popup.y + popup.height / 2 - center_y) <= 1 / scale,
        "WinUI dialog is centered in the full host client within one pixel");
}
void choice_pointer_contract(Window& window, HWND host, RadioGroup& choices, UINT dpi) {
    const auto original_style = window.visual_style();
    const auto hwnd = focus_peer(window, choices);
    int changes{}, accepts{};
    std::uint64_t changed{}, accepted{};
    choices.on_change([&](std::uint64_t id) { ++changes; changed = id; });
    choices.on_accept([&](std::uint64_t id) { ++accepts; accepted = id; });
    struct ClearCallbacks {
        RadioGroup& choices;
        ~ClearCallbacks() { choices.on_change({}); choices.on_accept({}); }
    } clear{choices};
    const bool list = choices.role() == ControlRole::choice_list;
    const auto row = [&](std::size_t index) {
        const auto bounds = choices.item_bounds(index);
        require(bounds.width > 0 && bounds.height > 0, "Pointer fixture row has native layout bounds");
        return MAKELPARAM(static_cast<short>(std::lround((bounds.x + bounds.width / 2) * dpi / 96.0f)),
            static_cast<short>(std::lround((bounds.y + bounds.height / 2) * dpi / 96.0f)));
    };
    const auto pointer = [&](UINT message, LPARAM position) {
        SendMessageW(hwnd, message, message == WM_LBUTTONUP ? 0 : MK_LBUTTON, position);
        flush(host);
    };
    const auto unchanged = [&](int before_changes, int before_accepts) {
        require(choices.selected() == 11 && changes == before_changes && accepts == before_accepts,
            "Uncommitted or cancelled pointer input leaves selection and callbacks unchanged");
    };
    choices.set_selected(11);
    window.set_visual_style(VisualStyle::winui); flush(host);
    pointer(WM_LBUTTONDOWN, row(2));
    unchanged(0, 0);
    require(GetCapture() == hwnd, "WinUI row press retains capture through paint and layout updates");
    pointer(WM_LBUTTONUP, row(2));
    require(choices.selected() == 33 && changes == 1 && changed == 33 &&
        accepts == (list ? 1 : 0) && (!list || accepted == 33) && GetCapture() != hwnd,
        "WinUI release on the pressed row selects once and accepts a choice list once");
    pointer(WM_LBUTTONUP, row(2));
    require(changes == 1 && accepts == (list ? 1 : 0), "A second release cannot repeat row activation");

    choices.set_selected(11);
    const auto cancelled_changes = changes, cancelled_accepts = accepts;
    const auto outside = MAKELPARAM(-10, -10);
    pointer(WM_LBUTTONDOWN, row(2));
    pointer(WM_MOUSEMOVE, outside);
    unchanged(cancelled_changes, cancelled_accepts);
    pointer(WM_LBUTTONUP, outside);
    unchanged(cancelled_changes, cancelled_accepts);
    require(GetCapture() != hwnd, "Outside release cancels the pressed row and releases capture");
    pointer(WM_LBUTTONDOWN, row(2));
    pointer(WM_MOUSEMOVE, row(0));
    pointer(WM_LBUTTONUP, row(0));
    unchanged(cancelled_changes, cancelled_accepts);
    require(GetCapture() != hwnd, "Release on another enabled row cancels rather than selecting it");
    pointer(WM_LBUTTONDOWN, row(1));
    pointer(WM_LBUTTONUP, row(1));
    unchanged(cancelled_changes, cancelled_accepts);
    require(GetCapture() != hwnd, "A disabled row never retains capture or activates");
    pointer(WM_LBUTTONDOWN, row(2));
    SendMessageW(hwnd, WM_CANCELMODE, 0, 0);
    pointer(WM_LBUTTONUP, row(2));
    unchanged(cancelled_changes, cancelled_accepts);
    require(GetCapture() != hwnd, "Cancelled capture cannot leave a pending row activation");

    pointer(WM_LBUTTONDOWN, row(2));
    for (const auto style : {VisualStyle::classic, VisualStyle::winui}) {
        window.set_visual_style(style); flush(host);
        unchanged(cancelled_changes, cancelled_accepts);
        require(GetCapture() == hwnd && GetFocus() == hwnd && choices.focused(),
            "Style relayout preserves the same native choice peer, pending row and capture");
    }
    pointer(WM_LBUTTONUP, row(2));
    require(choices.selected() == 33 && changes == cancelled_changes + 1 && changed == 33 &&
        accepts == cancelled_accepts + (list ? 1 : 0) && (!list || accepted == 33) && GetCapture() != hwnd,
        "Pending WinUI press commits exactly once after a style roundtrip");

    window.set_visual_style(VisualStyle::classic); flush(host);
    choices.set_selected(11);
    const auto classic_changes = changes, classic_accepts = accepts;
    pointer(WM_LBUTTONDOWN, row(2));
    require(choices.selected() == 33 && changes == classic_changes + 1 && changed == 33 &&
        accepts == classic_accepts + (list ? 1 : 0) && (!list || accepted == 33),
        "Classic retains selection and choice acceptance on pointer down");
    pointer(WM_LBUTTONUP, row(2));
    require(changes == classic_changes + 1 && accepts == classic_accepts + (list ? 1 : 0),
        "Classic pointer release does not repeat activation");

    for (const auto style : {VisualStyle::winui, VisualStyle::classic}) {
        window.set_visual_style(style); flush(host);
        choices.set_selected(11);
        require(focus_peer(window, choices) == hwnd, "Keyboard navigation uses the original native choice peer");
        const auto before_changes = changes, before_accepts = accepts;
        SendMessageW(hwnd, WM_KEYDOWN, VK_DOWN, 0);
        require(choices.selected() == 33 && changes == before_changes + 1 && changed == 33 &&
            accepts == before_accepts, "Arrow selection still commits immediately and skips disabled rows in either style");
        SendMessageW(hwnd, WM_KEYUP, VK_DOWN, 0);
        SendMessageW(hwnd, WM_KEYDOWN, VK_RETURN, 0);
        require(changes == before_changes + 1 && accepts == before_accepts + 1 && accepted == 33,
            "Enter still accepts the keyboard-selected row exactly once");
        SendMessageW(hwnd, WM_KEYUP, VK_RETURN, 0);
        require(changes == before_changes + 1 && accepts == before_accepts + 1,
            "Keyboard release does not duplicate selection or acceptance");
    }
    choices.set_selected(11);
    window.set_visual_style(original_style); flush(host);
    require(GetCapture() != hwnd && Drawing::live_targets() == 1, "Choice gestures leave no capture or additional render target");
}
void dialog_overflow_contract(Window& window, HWND host, Button& anchor, UINT dpi) {
    auto content = std::make_shared<Stack>(Axis::vertical);
    content->set_spacing(8);
    std::vector<std::shared_ptr<TextInput>> fields;
    for (int i = 0; i < 8; ++i) {
        auto field = std::make_shared<TextInput>(L"Overflow field " + std::to_wstring(i));
        field->set_text(L"Retained overflow text " + std::to_wstring(i));
        content->add(field);
        fields.push_back(std::move(field));
    }
    auto dialog = std::make_shared<ContentDialog>(L"Constrained native dialog", content);
    dialog->popup()->set_preferred_size({440, 320});
    window.set_visual_style(VisualStyle::winui); flush(host);
    const auto anchor_hwnd = focus_peer(window, anchor);
    window.show_dialog(dialog, anchor); flush(host);
    auto layout = std::dynamic_pointer_cast<Stack>(dialog->popup()->content());
    require(layout && layout->child_count() == 2, "Dialog retains separate scrolling body and action row");
    auto scroll = std::dynamic_pointer_cast<ScrollView>(layout->child_at(0));
    require(scroll && !scroll->passthrough() && scroll->overlay_scrollbar() && !scroll->tab_stop() &&
        scroll->enabled(), "WinUI dialog body scrolls without adding a Tab stop or disabling its content");
    close_to(scroll->viewport().width, scroll->bounds().width, "Overlay scrolling does not reserve a body gutter");
    auto body = std::dynamic_pointer_cast<Stack>(scroll->content());
    require(body && body->child_count() == 3 && body->child_at(1) == content,
        "Scrolling body retains its title, original content and validation");
    require(fields.front()->focused() && !scroll->focused() && GetFocus() != nullptr,
        "Automatic dialog focus skips the non-Tab-stop wrapper and reaches the first field");
    const auto initial_focus = GetFocus();
    const auto footer = dialog->footer_bounds(), primary_bounds = dialog->primary()->bounds();
    const auto fixed_footer = [&] {
        const auto current = dialog->footer_bounds(), action = dialog->primary()->bounds();
        close_to(current.x, footer.x, "Scrolling leaves the footer x position fixed");
        close_to(current.y, footer.y, "Scrolling leaves the footer y position fixed");
        close_to(current.width, footer.width, "Scrolling leaves the footer width fixed");
        close_to(current.height, footer.height, "Scrolling leaves the footer height fixed");
        close_to(action.x, primary_bounds.x, "Scrolling leaves the primary action x position fixed");
        close_to(action.y, primary_bounds.y, "Scrolling leaves the primary action y position fixed");
        require(layout->child_at(0) == scroll && scroll->content() == body && body->child_at(1) == content,
            "Scrolling and focus reveal preserve retained dialog content identities");
    };
    const auto fully_revealed = [&](HWND hwnd) {
        RECT bounds{};
        require(IsWindow(hwnd) && IsWindowVisible(hwnd) && GetWindowRect(hwnd, &bounds),
            "Revealed native field retains a visible HWND");
        MapWindowPoints(nullptr, host, reinterpret_cast<POINT*>(&bounds), 2);
        const auto viewport = scroll->viewport();
        const float scale = dpi / 96.0f;
        require(bounds.left >= std::lround(viewport.x * scale) - 1 &&
            bounds.top >= std::lround(viewport.y * scale) - 1 &&
            bounds.right <= std::lround((viewport.x + viewport.width) * scale) + 1 &&
            bounds.bottom <= std::lround((viewport.y + viewport.height) * scale) + 1,
            "Focus or wheel reveal places the entire native EDIT inside the body viewport");
    };
    std::vector<NativeInput> native;
    for (const auto& field : fields) {
        require(field->enabled() && field->tab_stop(), "Overflow fields remain enabled Tab stops");
        native.emplace_back(window, *field);
        flush(host); fully_revealed(native.back().hwnd); fixed_footer();
    }
    require(native.front().hwnd == initial_focus, "Automatic initial focus uses the first field's actual native peer");
    const auto scroll_hwnd = focus_peer(window, *scroll);
    require((GetWindowLongPtrW(scroll_hwnd, GWL_STYLE) & WS_TABSTOP) == 0,
        "The programmatically focusable wrapper has no native Tab-stop style");
    const auto primary_hwnd = focus_peer(window, *dialog->primary());
    const auto cancel_hwnd = focus_peer(window, *dialog->cancel_button());
    focus_peer(window, *dialog->primary());
    scroll->set_offset(0); flush(host);
    require(scroll->maximum_offset() > 0 && fields.back()->bounds().y >=
        scroll->viewport().y + scroll->viewport().height, "Constrained dialog starts with the last field offscreen");
    const auto peers = SendMessageW(host, WM_APP + 60, 14, 0);
    const auto generation = dialog->popup()->generation();
    POINT wheel{static_cast<LONG>(std::lround((scroll->viewport().x + 8) * dpi / 96.0f)),
        static_cast<LONG>(std::lround((scroll->viewport().y + 8) * dpi / 96.0f))};
    require(ClientToScreen(host, &wheel), "Map the owned body wheel position");
    for (int tick = 0; tick < 32 && fields.back()->bounds().y + fields.back()->bounds().height >
        scroll->viewport().y + scroll->viewport().height; ++tick) {
        const auto before = scroll->offset();
        SendMessageW(scroll_hwnd, WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(-WHEEL_DELTA)),
            MAKELPARAM(wheel.x, wheel.y));
        flush(host); fixed_footer();
        require(scroll->offset() > before, "Native wheel input advances the overflowing dialog body");
    }
    require(scroll->offset() > 0, "Wheel scrolling changes the body offset");
    fully_revealed(native.back().hwnd);
    scroll->set_offset(0); flush(host);
    require(fields.back()->bounds().y >= scroll->viewport().y + scroll->viewport().height,
        "Resetting the fixture makes the last field offscreen before the focus-reveal test");
    require(focus_peer(window, *fields.back()) == native.back().hwnd, "Window focus reuses the offscreen field's native HWND");
    flush(host);
    require(scroll->offset() > 0, "Window focus reveals the offscreen field through its body ScrollView");
    fully_revealed(native.back().hwnd); fixed_footer();

    std::vector<HWND> tab_order;
    for (const auto& field : native) tab_order.push_back(field.hwnd);
    tab_order.push_back(primary_hwnd); tab_order.push_back(cancel_hwnd);
    require(focus_peer(window, *fields.front()) == tab_order.front(), "Start native dialog Tab traversal at the first field");
    for (std::size_t step = 1; step <= tab_order.size(); ++step) {
        SendMessageW(host, WM_NEXTDLGCTL, FALSE, FALSE); flush(host);
        require(GetFocus() == tab_order[step % tab_order.size()] && GetFocus() != scroll_hwnd && !scroll->focused(),
            "Native forward Tab traversal visits each field and action once, excluding the body wrapper");
        for (const auto& field : fields) require(field->enabled(), "Tab traversal never disables overflow fields");
        fixed_footer();
    }
    SendMessageW(host, WM_NEXTDLGCTL, TRUE, FALSE); flush(host);
    require(GetFocus() == cancel_hwnd, "Reverse Tab wraps from the first field to Cancel without visiting the wrapper");
    for (std::size_t i = 0; i < native.size(); ++i) {
        native[i].verify();
        require(fields[i]->text() == native[i].text, "Overflow scrolling and navigation preserve each retained text model");
    }
    require(dialog->popup()->is_open() && dialog->popup()->generation() == generation &&
        SendMessageW(host, WM_APP + 60, 14, 0) == peers && Drawing::live_targets() == 1,
        "Wheel, focus reveal and Tab traversal retain the open dialog, native peers and shared target");
    dialog->cancel(); flush(host);
    require(!dialog->popup()->is_open() && GetFocus() == anchor_hwnd && IsWindowEnabled(anchor_hwnd),
        "Closing the overflow fixture restores its original owner anchor");
}
void presentation_contract() {
    Window window({title, {680, 620}});
    auto root = std::make_shared<Stack>(Axis::horizontal);
    root->set_padding({16, 16, 16, 16});
    root->set_spacing(16);
    auto defaults = std::make_shared<Stack>(Axis::vertical);
    auto explicit_sizes = std::make_shared<Stack>(Axis::vertical);
    defaults->set_spacing(8);
    explicit_sizes->set_spacing(8);
    root->add(defaults, 1);
    root->add(explicit_sizes, 1);
    auto button = std::make_shared<Button>(L"Open presentation dialog");
    auto input = std::make_shared<TextInput>(L"Retained presentation text");
    auto captionless = std::make_shared<TextInput>(L"Captionless field");
    captionless->set_caption_visible(false);
    auto number = std::make_shared<NumericInput>(L"Retained number");
    auto combo = std::make_shared<ComboBox>(L"Retained choice");
    auto editable = std::make_shared<ComboBox>(L"Editable choice", true);
    defaults->add(button); defaults->add(input); defaults->add(captionless);
    defaults->add(number); defaults->add(combo); defaults->add(editable);
    auto sized_button = std::make_shared<Button>(L"Preferred height");
    auto sized_input = std::make_shared<TextInput>(L"Fixed input height");
    auto sized_number = std::make_shared<NumericInput>(L"Preferred number height");
    auto sized_combo = std::make_shared<ComboBox>(L"Fixed choice height");
    sized_button->set_preferred_size({220, 53});
    sized_input->set_fixed_size({220, 79});
    sized_number->set_preferred_size({220, 51});
    sized_combo->set_fixed_size({220, 57});
    explicit_sizes->add(sized_button); explicit_sizes->add(sized_input);
    explicit_sizes->add(sized_number); explicit_sizes->add(sized_combo);
    auto radio = std::make_shared<RadioGroup>(L"Pointer radio choices");
    auto list = std::make_shared<RadioGroup>(L"Pointer list choices", true);
    for (const auto& choices : {radio, list}) {
        choices->set_items({{11, L"First enabled row"}, {22, L"Disabled row", false}, {33, L"Last enabled row"}}, 11);
        explicit_sizes->add(choices);
    }
    input->set_text(L"Retained native text");
    captionless->set_text(L"Captionless text");
    sized_input->set_text(L"Explicit native text");
    number->set_value(42.5); sized_number->set_value(23.5);
    for (const auto& choice : {combo, editable, sized_combo})
        choice->set_items({{11, L"First choice"}, {22, L"Second choice"}}, 22);
    int text_events{}, value_events{}, selection_events{}, edit_events{}, button_events{}, results{};
    for (const auto& text : {input, captionless, sized_input})
        text->on_change([&](const auto&) { ++text_events; });
    for (const auto& numeric : {number, sized_number})
        numeric->on_change([&](double) { ++value_events; });
    for (const auto& choice : {combo, editable, sized_combo})
        choice->on_change([&](std::uint64_t) { ++selection_events; });
    editable->on_edit([&](const auto&) { ++edit_events; });
    button->on_click([&] { ++button_events; });
    auto content = std::make_shared<Stack>(Axis::vertical);
    auto modal_input = std::make_shared<TextInput>(L"Dialog retained input");
    modal_input->set_text(L"Dialog native content");
    modal_input->on_change([&](const auto&) { ++text_events; });
    content->add(modal_input);
    auto dialog = std::make_shared<ContentDialog>(L"Presentation dialog", content);
    DialogResult last_result{DialogResult::cancel};
    dialog->on_result([&](DialogResult result) {
        last_result = result;
        ++results;
    });
    window.set_content(root);
    bool completed{};
    std::exception_ptr failure;
    std::string phase{"startup"};
    UINT active_dpi{};
    const auto check_presentation = [&] {
        phase = "native fixture setup";
        const auto hwnd = owned_window();
        require(hwnd != nullptr, "Run presentation checks in the owned native window");
        flush(hwnd);
        const std::array<Control*, 6> controls{button.get(), input.get(), captionless.get(),
            number.get(), combo.get(), editable.get()};
        const std::array<float, 6> classic{36, 68, 68, 44, 42, 42};
        const auto button_hwnd = focus_peer(window, *button);
        std::vector<NativeInput> native;
        for (const auto& text : {input, captionless, number->editor(), editable->editor(),
            sized_input, sized_number->editor()}) native.emplace_back(window, *text);
        require(focus_peer(window, *input) == native.front().hwnd, "Restore the original EDIT focus");
        const auto counts = std::array{text_events, value_events, selection_events, edit_events, button_events};
        const auto check_values = [&] {
            for (const auto& retained : native) retained.verify();
            require(input->text() == L"Retained native text" && captionless->text() == L"Captionless text" &&
                sized_input->text() == L"Explicit native text" && modal_input->text() == L"Dialog native content",
                "Presentation preserves all retained text models");
            require(number->value() == 42.5 && sized_number->value() == 23.5 &&
                combo->selected() == 22 && editable->selected() == 22 && sized_combo->selected() == 22 &&
                editable->editor()->text() == L"Second choice", "Presentation preserves numeric values and committed choices");
            require(std::array{text_events, value_events, selection_events, edit_events, button_events} == counts,
                "Style, theme and DPI changes emit no text, value, selection, edit or button callbacks");
        };
        const auto check_sizes = [&](VisualStyle style, UINT dpi) {
            require(window.visual_style() == style, "Window exposes the selected presentation");
            for (std::size_t i = 0; i < controls.size(); ++i) {
                require(controls[i]->visual_style() == style, "Window propagates presentation to retained controls");
                const float height = style == VisualStyle::classic ? classic[i] : i == 1 ? 60.0f : 32.0f;
                close_to(controls[i]->bounds().height, height, "Native layout uses the requested default control height");
            }
            close_to(input->caption_extent(), style == VisualStyle::winui ? 28.0f : 24.0f, "Caption uses the style-specific extent");
            if (style == VisualStyle::winui)
                close_to(input->bounds().height - input->caption_extent(), 32, "Captioned WinUI field is 32 DIPs tall");
            close_to(captionless->caption_extent(), 0, "Captionless field has no header space");
            close_to(sized_button->bounds().height, 53, "Explicit button preferred height survives presentation");
            close_to(sized_input->bounds().height, 79, "Explicit input fixed height survives presentation");
            close_to(sized_number->bounds().height, 51, "Explicit numeric preferred height survives presentation");
            close_to(sized_combo->bounds().height, 57, "Explicit combo fixed height survives presentation");
            RECT physical{};
            require(IsWindow(button_hwnd) && GetWindowRect(button_hwnd, &physical), "Default button retains its native peer");
            require(std::abs((physical.bottom - physical.top) -
                std::lround(button->bounds().height * dpi / 96.0f)) <= 1,
                "Actual button HWND height follows DIP layout at the selected DPI");
        };
        check_sizes(VisualStyle::classic, GetDpiForWindow(hwnd));
        RECT initial{};
        require(GetWindowRect(hwnd, &initial), "Read initial outer bounds for synthetic DPI");
        const auto initial_dpi = GetDpiForWindow(hwnd);
        for (const UINT dpi : {96u, 144u, 192u}) {
            active_dpi = dpi;
            phase = "DPI transition";
            RECT outer{initial.left, initial.top,
                initial.left + MulDiv(initial.right - initial.left, dpi, initial_dpi),
                initial.top + MulDiv(initial.bottom - initial.top, dpi, initial_dpi)};
            SendMessageW(hwnd, WM_DPICHANGED, MAKEWPARAM(dpi, dpi), reinterpret_cast<LPARAM>(&outer));
            flush(hwnd);
            for (const auto theme : {ThemeMode::light, ThemeMode::dark, ThemeMode::high_contrast}) {
                phase = "root style comparison";
                window.set_theme(theme);
                for (const auto style : {VisualStyle::winui, VisualStyle::classic, VisualStyle::winui, VisualStyle::classic}) {
                    window.set_visual_style(style); flush(hwnd);
                    check_sizes(style, dpi); check_values();
                    require(GetFocus() == native.front().hwnd && input->focused(), "Root relayout preserves native EDIT focus");
                    require(Drawing::live_targets() == 1, "Presentation changes paint with one shared target");
                }
                phase = "choice pointer and keyboard";
                for (const auto& choices : {radio, list}) choice_pointer_contract(window, hwnd, *choices, dpi);
                require(focus_peer(window, *input) == native.front().hwnd, "Choice gestures preserve the original EDIT peer");
                check_values();
                phase = "retained dialog setup";
                window.set_visual_style(VisualStyle::winui); flush(hwnd);
                focus_peer(window, *button);
                window.show_dialog(dialog, *button, modal_input.get()); flush(hwnd);
                require(dialog->popup()->is_open() && !IsWindowEnabled(native.front().hwnd), "Open dialog disables native owner input");
                const auto popup_content = dialog->popup()->content();
                const auto primary = dialog->primary();
                const auto primary_hwnd = focus_peer(window, *primary);
                NativeInput modal(window, *modal_input);
                const auto generation = dialog->popup()->generation();
                const auto peers = SendMessageW(hwnd, WM_APP + 60, 14, 0);
                const auto prior_results = results;
                phase = "retained dialog style comparison";
                for (const auto style : {VisualStyle::classic, VisualStyle::winui, VisualStyle::classic, VisualStyle::winui}) {
                    window.set_visual_style(style); flush(hwnd);
                    require(dialog->popup()->is_open() && dialog->popup()->generation() == generation &&
                        dialog->popup()->content() == popup_content && dialog->primary() == primary &&
                        content->child_at(0) == modal_input && IsWindow(primary_hwnd),
                        "Dialog relayout retains popup generation, primary action, content and native peers");
                    modal.verify(); check_values();
                    require(GetFocus() == modal.hwnd && modal_input->focused() && !IsWindowEnabled(native.front().hwnd),
                        "Dialog style changes retain native editor focus and modal owner isolation");
                    require(results == prior_results && SendMessageW(hwnd, WM_APP + 60, 14, 0) == peers &&
                        Drawing::live_targets() == 1, "Dialog style changes neither complete the dialog nor allocate extra peers or targets");
                    if (style == VisualStyle::winui) {
                        const auto bounds = dialog->popup()->bounds(), footer = dialog->footer_bounds();
                        centered(hwnd, bounds, dpi);
                        close_to(footer.x, bounds.x, "WinUI dialog footer starts at the surface edge");
                        close_to(footer.width, bounds.width, "WinUI dialog footer spans the surface width");
                        close_to(footer.y + footer.height, bounds.y + bounds.height, "WinUI dialog footer ends at the surface edge");
                        require(footer.height > 0 && footer.y >= bounds.y, "WinUI dialog has a separate footer inside its surface");
                        for (const auto& action : {primary, dialog->cancel_button()}) {
                            const auto action_bounds = action->bounds();
                            close_to(action_bounds.height, 32, "WinUI dialog actions use the 32-DIP button height");
                            require(action_bounds.x >= footer.x && action_bounds.y >= footer.y &&
                                action_bounds.x + action_bounds.width <= footer.x + footer.width &&
                                action_bounds.y + action_bounds.height <= footer.y + footer.height,
                                "WinUI dialog actions remain inside the footer after relayout");
                        }
                        require(primary->appearance() == ButtonAppearance::accent, "Dialog primary action retains accent appearance");
                    }
                }
                phase = "explicit primary completion";
                dialog->accept(); flush(hwnd);
                require(!dialog->popup()->is_open() && results == prior_results + 1 &&
                    last_result == DialogResult::primary &&
                    IsWindowEnabled(native.front().hwnd) && GetFocus() == button_hwnd,
                    "Explicit primary completion restores owner input and anchor focus");
                require(focus_peer(window, *input) == native.front().hwnd, "Restore the same root EDIT after the dialog");
                check_values();
            }
            phase = "overflow dialog";
            dialog_overflow_contract(window, hwnd, *button, dpi);
            require(focus_peer(window, *input) == native.front().hwnd, "Overflow dialog preserves the original owner EDIT peer");
            check_values();
        }
        completed = true;
        window.close();
        return true;
    };
    window.on_key([&](const KeyEvent& event) {
        if (event.key != Key::f12) return false;
        try {
            return check_presentation();
        } catch (...) {
            failure = std::current_exception();
            std::cerr << "Presentation failure phase=" << phase << " dpi=" << active_dpi
                << " theme=" << static_cast<int>(window.theme()) << " style=" << static_cast<int>(window.visual_style()) << '\n';
            throw;
        }
    });
    StartFixture start;
    const auto result = Application::run(window);
    if (failure) std::rethrow_exception(failure);
    if (result) std::wcerr << window.error() << L'\n';
    require(result == 0 && start.posted && !start.failed && completed, "All native presentation checks complete");
    require(Drawing::live_targets() == 0, "Closing the presentation fixture releases its shared target");
}
}
int main() {
    try {
        presentation_contract();
        std::cout << "WinUI native presentation, choice pointer/keyboard, explicit sizes, DPI, theme and retained/overflow dialog contracts passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
