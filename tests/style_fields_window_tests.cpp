#include "xui/application.hpp"
#include "xui/documents.hpp"
#include "xui/native_edit.hpp"
#include "../src/native_document.hpp"
#include "owned_window_capture.hpp"
#include <richedit.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <iostream>
#include <map>
#include <sstream>
#include <thread>

namespace {
using namespace xui;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void flush(HWND hwnd) {
    SendMessageW(hwnd, WM_APP + 12, 0, 0);
    require(RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN) != FALSE,
        "Refresh owned field window and native children");
}
HWND child(HWND root, const wchar_t* name, int ordinal = 0) {
    struct Find { const wchar_t* name; int ordinal; HWND result{}; } find{name, ordinal};
    EnumChildWindows(root, [](HWND hwnd, LPARAM data) -> BOOL {
        auto& find = *reinterpret_cast<Find*>(data);
        wchar_t cls[128]{}; GetClassNameW(hwnd, cls, 128);
        if (_wcsicmp(cls, find.name) == 0 && find.ordinal-- == 0) { find.result = hwnd; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&find));
    require(find.result != nullptr, "Expected native field window exists");
    return find.result;
}
std::wstring read(HWND hwnd) {
    std::wstring value(static_cast<size_t>(GetWindowTextLengthW(hwnd)) + 1, L'\0');
    value.resize(GetWindowTextW(hwnd, value.data(), static_cast<int>(value.size()))); return value;
}
PartStyleValues font_values(float size) {
    PartStyleValues font; font.font_family = make_style_font_family("Consolas");
    font.font_size = size; font.font_weight = 600; font.font_style = StyleFontStyle::italic; return font;
}
HWND clear_button(HWND root) {
    HWND result{};
    EnumChildWindows(root, [](HWND hwnd, LPARAM data) -> BOOL {
        wchar_t cls[64]{}; GetClassNameW(hwnd, cls, 64);
        if (std::wstring_view(cls) != L"Xui.Control.1") return TRUE;
        wchar_t name[64]{}; GetWindowTextW(hwnd, name, 64);
        if (std::wstring_view(name) != L"Clear Native input") return TRUE;
        *reinterpret_cast<HWND*>(data) = hwnd; return FALSE;
    }, reinterpret_cast<LPARAM>(&result));
    require(result != nullptr, "Existing native clear action is retained"); return result;
}
void clear_pixels(HWND root, HWND clear, unsigned background, unsigned foreground, const char* state) {
    flush(root);
    const auto pixels = owned_window_capture::capture(root);
    RECT rect{}; GetWindowRect(clear, &rect); MapWindowPoints(nullptr, root, reinterpret_cast<POINT*>(&rect), 2);
    std::size_t surface{}, glyph{};
    double glyph_coverage{};
    std::map<unsigned, std::size_t> observed;
    unsigned nearest_background = 0xffffff, nearest_foreground = 0xffffff;
    int background_distance = 256, foreground_distance = 256;
    const auto distance = [](unsigned value, unsigned color) {
        return std::max({std::abs(int(value & 255) - int(color & 255)),
            std::abs(int((value >> 8) & 255) - int((color >> 8) & 255)),
            std::abs(int((value >> 16) & 255) - int((color >> 16) & 255))});
    };
    const auto matches = [](unsigned value, unsigned color) {
        return std::abs(int(value & 255) - int(color & 255)) <= 4 &&
            std::abs(int((value >> 8) & 255) - int((color >> 8) & 255)) <= 4 &&
            std::abs(int((value >> 16) & 255) - int((color >> 16) & 255)) <= 4;
    };
    unsigned coverage_channel{};
    int coverage_delta{};
    for (const unsigned shift : {0u, 8u, 16u}) {
        const int delta = int((foreground >> shift) & 255) - int((background >> shift) & 255);
        if (std::abs(delta) > std::abs(coverage_delta)) {
            coverage_channel = shift;
            coverage_delta = delta;
        }
    }
    require(std::abs(coverage_delta) >= 64, "Clear glyph fixture requires contrasting authored colors");
    const auto foreground_coverage = [&](unsigned color) {
        const double coverage = double(int((color >> coverage_channel) & 255) -
            int((background >> coverage_channel) & 255)) / coverage_delta;
        if (coverage < 0.2 || coverage > 1.0) return 0.0;
        for (const unsigned shift : {0u, 8u, 16u}) {
            const int channel_background = int((background >> shift) & 255);
            const int channel_foreground = int((foreground >> shift) & 255);
            const double expected = channel_background + coverage * (channel_foreground - channel_background);
            if (std::abs(double((color >> shift) & 255) - expected) > 4) return 0.0;
        }
        return coverage;
    };
    require(foreground_coverage(background) == 0 && foreground_coverage(foreground) == 1 &&
        foreground_coverage(0xffffff) == 0,
        "Clear glyph coverage excludes background and unrelated foreground colors");
    for (const double coverage : {0.31, 0.50}) {
        unsigned blended{};
        for (const unsigned shift : {0u, 8u, 16u}) {
            const int start = int((background >> shift) & 255);
            const int end = int((foreground >> shift) & 255);
            blended |= static_cast<unsigned>(std::lround(start + coverage * (end - start))) << shift;
        }
        require(std::abs(foreground_coverage(blended) - coverage) <= 1.0 / 64,
            "Clear glyph coverage retains authored antialiased foreground contributions");
    }
    for (int y = std::max(0L, rect.top + 3); y < std::min(static_cast<LONG>(pixels.height), rect.bottom - 3); ++y)
        for (int x = std::max(0L, rect.left + 3); x < std::min(static_cast<LONG>(pixels.width), rect.right - 3); ++x) {
            const auto color = pixels.data[static_cast<size_t>(y) * pixels.width + x];
            surface += matches(color, background); glyph += matches(color, foreground);
            glyph_coverage += foreground_coverage(color);
            ++observed[color & 0xffffff];
            if (const auto delta = distance(color, background); delta < background_distance) {
                background_distance = delta; nearest_background = color & 0xffffff;
            }
            if (const auto delta = distance(color, foreground); delta < foreground_distance) {
                foreground_distance = delta; nearest_foreground = color & 0xffffff;
            }
        }
    // Antialiased glyphs still need more than three full pixels of foreground coverage.
    if (surface <= 30 || glyph_coverage <= 3) {
        std::vector<std::pair<unsigned, std::size_t>> colors(observed.begin(), observed.end());
        std::sort(colors.begin(), colors.end(), [](const auto& first, const auto& second) { return first.second > second.second; });
        std::ostringstream detail;
        detail << "Clear pixel failure: state=" << state << " rect=" << rect.left << ',' << rect.top << ','
            << rect.right << ',' << rect.bottom << " capture=" << pixels.width << 'x' << pixels.height
            << " dpi=" << GetDpiForWindow(clear) << " visible=" << IsWindowVisible(clear)
            << " enabled=" << IsWindowEnabled(clear) << " captured=" << (GetCapture() == clear)
            << " focused=" << (GetFocus() == clear) << " surface_matches=" << surface << " glyph_matches=" << glyph
            << " glyph_coverage=" << glyph_coverage
            << std::hex << " expected_bg=0x" << background << " expected_fg=0x" << foreground
            << " nearest_bg=0x" << nearest_background << " nearest_fg=0x" << nearest_foreground
            << std::dec << " bg_distance=" << background_distance << " fg_distance=" << foreground_distance << " top_colors=";
        for (std::size_t i = 0; i < std::min<std::size_t>(colors.size(), 8); ++i)
            detail << "0x" << std::hex << colors[i].first << std::dec << ':' << colors[i].second << ' ';
        throw std::runtime_error(detail.str());
    }
}
std::shared_ptr<const ControlStyle> field_style(StyleTarget target, float size = 22) {
    PartStyleValues root; root.background = ThemeColor{0x123456, 0x654321};
    root.border_brush = ThemeColor{0x765432}; root.border_thickness = Insets{2, 3, 4, 5};
    root.padding = Insets{18, 7, 16, 8}; root.corner_radius = 8;
    auto text = font_values(size);
    if (target != StyleTarget::date_time_picker) text.foreground = ThemeColor{0x246813, 0x315724};
    PartStyleValues disabled; disabled.background = ThemeColor{0x987654};
    return ControlStyle::create(target, {{StylePart::root, root}, {StylePart::text, text}},
        {{StylePart::root, style_states::disabled, disabled}});
}
void native_font(HWND hwnd, float size) {
    LOGFONTW font{};
    require(GetObjectW(reinterpret_cast<HFONT>(SendMessageW(hwnd, WM_GETFONT, 0, 0)), sizeof(font), &font) != 0,
        "Native font handle remains valid");
    require(std::wstring_view(font.lfFaceName) == L"Consolas" && font.lfWeight == 600 && font.lfItalic &&
        std::abs(font.lfHeight + std::lround(size * GetDpiForWindow(hwnd) / 96.0f)) <= 1,
        "Native EDIT and date controls consume authored font properties");
}
void native_colors(HWND host, HWND edit, COLORREF background, COLORREF foreground) {
    const auto dc = GetDC(edit); require(dc != nullptr, "Acquire field color context");
    const auto brush = reinterpret_cast<HBRUSH>(SendMessageW(host, WM_CTLCOLOREDIT,
        reinterpret_cast<WPARAM>(dc), reinterpret_cast<LPARAM>(edit)));
    const bool match = brush && GetBkColor(dc) == background && GetTextColor(dc) == foreground;
    ReleaseDC(edit, dc); require(match, "Native field colors agree with owned surface values");
}
void document_high_contrast() {
    const auto host = CreateWindowExW(0, L"STATIC", L"Owned field palette test", WS_POPUP,
        0, 0, 400, 240, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    require(host != nullptr, "Create hidden native document host");
    struct Destroy { HWND value; ~Destroy() { DestroyWindow(value); } } destroy{host};
    auto model = std::make_shared<MultilineText>(); model->set_text(L"retained");
    NativeDocumentBridge bridge(model); bridge.attach(host, 101);
    Palette palette{}; palette.text = D2D1::ColorF(0xffffff); palette.field = D2D1::ColorF(0);
    palette.disabled = palette.text; palette.mode = ThemeMode::dark;
    bridge.update(96, palette);
    const auto edit = bridge.window();
    SendMessageW(edit, EM_SETSEL, 0, 2); SendMessageW(edit, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"AB"));
    CHARRANGE before{}; SendMessageW(edit, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&before));
    const auto content = read(edit); const auto undo = SendMessageW(edit, EM_CANUNDO, 0, 0);
    model->set_control_style(field_style(StyleTarget::multiline_text)); bridge.update(96, palette);
    CHARFORMAT2W format{sizeof(format)}; SendMessageW(edit, EM_GETCHARFORMAT, SCF_DEFAULT, reinterpret_cast<LPARAM>(&format));
    require(format.yHeight == 330 && format.wWeight == 600 && (format.dwEffects & CFE_ITALIC),
        "RichEdit consumes supported native default font properties");
    palette.high_contrast = true; bridge.update(96, palette);
    SendMessageW(edit, EM_GETCHARFORMAT, SCF_DEFAULT, reinterpret_cast<LPARAM>(&format));
    require(format.crTextColor == RGB(255, 255, 255), "System-color high contrast overrides authored document color");
    model->set_control_style(nullptr); bridge.update(96, palette);
    CHARRANGE after{}; SendMessageW(edit, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&after));
    require(bridge.window() == edit && read(edit) == content && before.cpMin == after.cpMin && before.cpMax == after.cpMax &&
        SendMessageW(edit, EM_CANUNDO, 0, 0) == undo, "RichEdit style changes preserve HWND, text, selection, and undo");
}
void rich_defaults() {
    const auto host = CreateWindowExW(0, L"STATIC", L"Owned rich default test", WS_POPUP,
        0, 0, 400, 240, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    require(host != nullptr, "Create hidden rich document host");
    struct Destroy { HWND value; ~Destroy() { DestroyWindow(value); } } destroy{host};
    Palette palette{}; palette.text = D2D1::ColorF(0xffffff); palette.field = D2D1::ColorF(0);
    palette.disabled = palette.text; palette.mode = ThemeMode::dark;
    auto model = std::make_shared<RichText>();
    model->set_runs({{L"bold", true}, {L"italic", false, true}});
    NativeDocumentBridge bridge(model); bridge.attach(host, 102); bridge.update(96, palette);
    const auto edit = bridge.window();
    SendMessageW(edit, EM_SETSEL, -1, -1);
    SendMessageW(edit, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L" native"));
    bridge.changed();
    SendMessageW(edit, EM_SETSEL, 0, 4);
    bridge.notify(SELCHANGE{{edit, 102, EN_SELCHANGE}, {0, 4}, SEL_TEXT}.nmhdr);
    CHARFORMAT2W original{sizeof(original)};
    SendMessageW(edit, EM_GETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&original));
    const auto text = read(edit); const auto runs = model->runs();
    const auto undo = SendMessageW(edit, EM_CANUNDO, 0, 0);
    const auto check_run = [&] {
        CHARRANGE selection{}; SendMessageW(edit, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&selection));
        CHARFORMAT2W format{sizeof(format)}; SendMessageW(edit, EM_GETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&format));
        require(bridge.window() == edit && read(edit) == text && model->runs() == runs &&
            selection.cpMin == 0 && selection.cpMax == 4 && SendMessageW(edit, EM_CANUNDO, 0, 0) == undo &&
            format.dwEffects == original.dwEffects && format.yHeight == original.yHeight &&
            format.wWeight == original.wWeight && std::wstring_view(format.szFaceName) == original.szFaceName,
            "Rich style replacement preserves existing character runs, text, selection, undo, and native identity");
    };
    for (float size : {22.0f, 28.0f}) {
        model->set_control_style(field_style(StyleTarget::rich_text, size)); bridge.update(96, palette);
        CHARFORMAT2W defaults{sizeof(defaults)}; SendMessageW(edit, EM_GETCHARFORMAT, SCF_DEFAULT, reinterpret_cast<LPARAM>(&defaults));
        require(defaults.yHeight == std::lround(size * 15) && defaults.wWeight == 600 &&
            (defaults.dwEffects & CFE_ITALIC) && std::wstring_view(defaults.szFaceName) == L"Consolas",
            "Rich styles update native default formatting, not existing runs");
        check_run();
    }
    model->set_control_style(nullptr); bridge.update(96, palette); check_run();

    auto empty = std::make_shared<RichText>(); empty->set_control_style(field_style(StyleTarget::rich_text));
    NativeDocumentBridge insertion(empty); insertion.attach(host, 103); insertion.update(96, palette);
    SendMessageW(insertion.window(), EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"new text"));
    SendMessageW(insertion.window(), EM_SETSEL, 0, -1);
    CHARFORMAT2W inserted{sizeof(inserted)};
    SendMessageW(insertion.window(), EM_GETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&inserted));
    require(inserted.yHeight == 330 && inserted.wWeight == 600 && (inserted.dwEffects & CFE_ITALIC) &&
        std::wstring_view(inserted.szFaceName) == L"Consolas",
        "New text in an empty rich document uses authored native default typography");
}
void window_case() {
    Window window({L"XUI native field style contracts", {850, 1000}, ThemeMode::light});
    window.set_visual_style(VisualStyle::winui);
    auto root = std::make_shared<Stack>(Axis::vertical);
    auto fields = std::make_shared<Stack>(Axis::vertical);
    auto input = std::make_shared<TextInput>(L"Native input"); input->set_text(L"retained input");
    auto search = std::make_shared<TextInput>(L"Search"); search->set_search_style(true); search->set_placeholder(L"Search");
    auto plain = std::make_shared<MultilineText>(); plain->set_text(L"retained document");
    auto rich = std::make_shared<RichText>(); rich->set_runs({{L"bold", true}, {L"italic", false, true}});
    auto password = std::make_shared<PasswordInput>(); password->set_password(L"private fixture");
    auto date = std::make_shared<DateTimePicker>();
    auto time = std::make_shared<DateTimePicker>(L"Time", DateTimePresentation::time);
    auto calendar = std::make_shared<DateTimePicker>(L"Calendar", DateTimePresentation::calendar);
    fields->add(input); fields->add(search); fields->add(plain); fields->add(rich); fields->add(password);
    fields->add(date); fields->add(time); fields->add(calendar);
    auto owner = std::make_shared<ContentView>(fields); root->add(owner); window.set_content(root);
    std::atomic<bool> ran{}; std::string driver_error;
    std::thread driver([&] {
        HWND host{};
        for (int i = 0; i < 400 && !host; ++i) { host = FindWindowW(L"Xui.Window.1", window.title().c_str()); Sleep(20); }
        if (!host) { driver_error = "Field test window did not start"; return; }
        if (!window.post([&, host] {
            const auto edit = child(host, L"EDIT"), search_edit = child(host, L"EDIT", 1), secret = child(host, L"EDIT", 2);
            const auto plain_edit = child(host, MSFTEDIT_CLASS), rich_edit = child(host, MSFTEDIT_CLASS, 1);
            const auto date_edit = child(host, DATETIMEPICK_CLASSW), time_edit = child(host, DATETIMEPICK_CLASSW, 1);
            const auto calendar_edit = child(host, MONTHCAL_CLASSW);
            flush(host); flush(host);
            const auto default_gdi = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
            const auto default_user = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
            for (int i = 0; i < 32; ++i) flush(host);
            require(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) == default_gdi &&
                GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS) == default_user &&
                !input->has_control_styling() && !plain->has_control_styling() && !password->has_control_styling(),
                "Default native updates allocate no style attachment or additional handles");
            window.focus(*input); SendMessageW(edit, EM_SETSEL, 0, 2);
            SendMessageW(edit, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"AB"));
            const auto content = input->text(); const auto selection = input->selection();
            const auto undo = SendMessageW(edit, EM_CANUNDO, 0, 0);
            input->set_control_style(field_style(StyleTarget::text_input)); input->set_control_style_values(StylePart::header, font_values(19));
            search->set_control_style(field_style(StyleTarget::text_input));
            plain->set_control_style(field_style(StyleTarget::multiline_text));
            rich->set_control_style(field_style(StyleTarget::rich_text));
            password->set_control_style(field_style(StyleTarget::password_input));
            date->set_control_style(field_style(StyleTarget::date_time_picker));
            time->set_control_style(field_style(StyleTarget::date_time_picker));
            calendar->set_control_style(field_style(StyleTarget::date_time_picker)); flush(host);
            for (auto hwnd : {edit, search_edit, secret, date_edit, time_edit, calendar_edit}) native_font(hwnd, 22);
            SendMessageW(edit, WM_IME_STARTCOMPOSITION, 0, 0);
            input->set_control_style(field_style(StyleTarget::text_input, 23)); flush(host); native_font(edit, 22);
            SendMessageW(edit, WM_IME_ENDCOMPOSITION, 0, 0); flush(host); native_font(edit, 23);
            input->set_control_style(field_style(StyleTarget::text_input)); flush(host);
            HIGHCONTRASTW contrast{sizeof(contrast)}; SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0);
            if (!(contrast.dwFlags & HCF_HIGHCONTRASTON)) {
                native_colors(host, edit, RGB(0x12, 0x34, 0x56), RGB(0x24, 0x68, 0x13));
                window.set_theme(ThemeMode::dark); flush(host);
                native_colors(host, edit, RGB(0x65, 0x43, 0x21), RGB(0x31, 0x57, 0x24));
            }
            RECT rect{}; GetWindowRect(edit, &rect); MapWindowPoints(nullptr, host, reinterpret_cast<POINT*>(&rect), 2);
            const auto scale = GetDpiForWindow(edit) / 96.0f;
            require(std::abs(rect.left - std::lround((input->bounds().x + 20) * scale)) <= 1,
                "Native field rectangle includes styled padding and border");
            owner->set_enabled(false); flush(host);
            require(!IsWindowEnabled(edit) && input->effective_control_style_values(StylePart::root)->background == ThemeColor{0x987654},
                "Disabled ancestors drive native state without changing child model");
            owner->set_enabled(true); flush(host);
            const auto handles = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
            for (int i = 0; i < 12; ++i) {
                input->set_control_style(field_style(StyleTarget::text_input, 22 + (i % 2))); flush(host);
                input->set_control_style(nullptr); flush(host);
            }
            require(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) <= handles + 1, "Native style font handles stay bounded");
            require(child(host, L"EDIT") == edit && child(host, MSFTEDIT_CLASS) == plain_edit &&
                child(host, MSFTEDIT_CLASS, 1) == rich_edit && input->text() == content && input->selection() == selection &&
                SendMessageW(edit, EM_CANUNDO, 0, 0) == undo, "Replace and clear preserve native identity, text, selection, and undo");
            require((GetWindowLongPtrW(secret, GWL_STYLE) & ES_PASSWORD) != 0, "Native password remains masked after styling");
            CHARFORMAT2W run{sizeof(run)}; SendMessageW(rich_edit, EM_SETSEL, 0, 4);
            SendMessageW(rich_edit, EM_GETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&run));
            require(run.dwEffects & CFE_BOLD, "Rich run formatting stays native-owned");
            PartStyleValues normal; normal.background = ThemeColor{0x220044}; normal.foreground = ThemeColor{0x00ff00};
            normal.corner_radius = 4;
            PartStyleValues hover; hover.background = ThemeColor{0x003388};
            PartStyleValues pressed; pressed.background = ThemeColor{0x882200};
            input->set_control_style(ControlStyle::create(StyleTarget::text_input, {{StylePart::clear_action, normal}},
                {{StylePart::clear_action, style_states::hovered, hover}, {StylePart::clear_action, style_states::pressed, pressed}}));
            window.focus(*input); flush(host);
            const auto clear = clear_button(host);
            require(IsWindowVisible(clear) && GetFocus() == edit, "Style projection retains native clear visibility and editor focus");
            RECT clear_bounds{}; GetWindowRect(clear, &clear_bounds);
            RECT editor_bounds{}; GetWindowRect(edit, &editor_bounds);
            SendMessageW(clear, WM_MOUSELEAVE, 0, 0); flush(host);
            if (!(contrast.dwFlags & HCF_HIGHCONTRASTON)) clear_pixels(host, clear, 0x220044, 0x00ff00, "normal");
            SendMessageW(clear, WM_MOUSEMOVE, 0, MAKELPARAM(10, 10)); flush(host);
            if (!(contrast.dwFlags & HCF_HIGHCONTRASTON)) clear_pixels(host, clear, 0x003388, 0x00ff00, "hovered");
            SendMessageW(clear, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(10, 10)); flush(host);
            if (!(contrast.dwFlags & HCF_HIGHCONTRASTON)) clear_pixels(host, clear, 0x882200, 0x00ff00, "pressed");
            SendMessageW(clear, WM_LBUTTONUP, 0, MAKELPARAM(10, 10)); flush(host);
            require(input->text().empty() && child(host, L"EDIT") == edit && SendMessageW(edit, EM_CANUNDO, 0, 0),
                "Styled clear uses the same native undoable edit action");
            SendMessageW(edit, EM_UNDO, 0, 0); flush(host);
            require(input->text() == content, "Native undo restores content after styled clear");
            input->set_control_style(nullptr); flush(host);
            RECT restored{}; GetWindowRect(clear, &restored);
            require(clear_button(host) == clear && EqualRect(&clear_bounds, &restored), "Clear styles preserve retained HWND and real action bounds");
            RECT restored_editor{}; GetWindowRect(edit, &restored_editor);
            require(EqualRect(&editor_bounds, &restored_editor), "Paint-only clear styles preserve native editor reservation and hit geometry");
            ran = true; window.close();
        })) driver_error = "Could not post native field tests";
    });
    const auto result = Application::run(window); driver.join();
    require(driver_error.empty(), "Native field driver completed");
    if (result) std::wcerr << window.error() << '\n';
    require(result == 0 && ran, "Native field window contracts completed");
}
}
int main() {
    try {
        require(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)), "Initialize native document COM");
        document_high_contrast(); rich_defaults(); CoUninitialize();
        window_case(); std::cout << "Native field window contracts passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
