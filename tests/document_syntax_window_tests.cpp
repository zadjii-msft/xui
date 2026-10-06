#include "../src/native_document.hpp"
#include "../src/window_host.hpp"
#include "owned_window_capture.hpp"
#include "xui/application.hpp"
#include <richedit.h>
#include <chrono>
#include <iostream>
#include <stdexcept>
using namespace xui;
namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct Fixture {
    HWND parent{};
    std::shared_ptr<MultilineText> document = std::make_shared<MultilineText>();
    std::unique_ptr<NativeDocumentBridge> bridge;
    std::exception_ptr failure;
    int changes{}, tokens{}, formats{}, replacements{}, writable_transitions{}, themes{}, paints{}, prints{}, positions{};
    Palette palette = Palette::system(ThemeMode::light);
    static LRESULT CALLBACK procedure(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) noexcept {
        auto* self = reinterpret_cast<Fixture*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (self && self->bridge) {
            try {
                if (message == WM_COMMAND && HIWORD(wp) == EN_CHANGE) self->bridge->changed();
                if (message == WM_NOTIFY) return self->bridge->notify(*reinterpret_cast<NMHDR*>(lp));
            } catch (...) { self->failure = std::current_exception(); }
        }
        return DefWindowProcW(hwnd, message, wp, lp);
    }
    static LRESULT CALLBACK observe(HWND hwnd, UINT message, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data) noexcept {
        auto& self = *reinterpret_cast<Fixture*>(data);
        if (message == EM_SETCHARFORMAT) ++self.formats;
        if (message == EM_STREAMIN || message == WM_SETTEXT || message == EM_REPLACESEL) ++self.replacements;
        if (message == EM_SETREADONLY && !wp) ++self.writable_transitions;
        if (message == WM_THEMECHANGED) ++self.themes;
        if (message == WM_PAINT) ++self.paints;
        if (message == WM_PRINTCLIENT) ++self.prints;
        if (message == EM_POSFROMCHAR) ++self.positions;
        return DefSubclassProc(hwnd, message, wp, lp);
    }
    explicit Fixture(std::wstring value = L"if \U0001f600\rvalue", bool read_only = false) {
        WNDCLASSW cls{};
        cls.lpfnWndProc = procedure; cls.hInstance = GetModuleHandleW(nullptr); cls.lpszClassName = L"XuiDocumentSyntaxTest";
        RegisterClassW(&cls);
        parent = CreateWindowW(cls.lpszClassName, L"Document syntax fixture", WS_OVERLAPPEDWINDOW,
            0, 0, 500, 250, nullptr, nullptr, cls.hInstance, nullptr);
        require(parent != nullptr, "Create parent");
        SetWindowLongPtrW(parent, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        document->set_text(std::move(value));
        document->set_read_only(read_only);
        if (read_only) enable();
        bridge = std::make_unique<NativeDocumentBridge>(document);
        bridge->attach(parent, 1);
        bridge->on_failure([&] { failure = std::current_exception(); });
        require(SetWindowSubclass(window(), observe, 2, reinterpret_cast<DWORD_PTR>(this)) != FALSE, "Observe native presentation");
        refresh();
        MoveWindow(window(), 0, 0, 480, 150, FALSE);
        ShowWindow(parent, SW_SHOWNOACTIVATE);
        document->on_change([&](auto&) { ++changes; });
    }
    ~Fixture() { bridge.reset(); DestroyWindow(parent); }
    HWND window() const { return bridge->window(); }
    void refresh() {
        bridge->update(96, palette);
        if (failure) std::rethrow_exception(failure);
    }
    void enable() {
        document->set_syntax_highlighter([&](std::wstring_view text) {
            ++tokens;
            std::vector<SyntaxSpan> spans;
            if (text.starts_with(L"if")) spans.push_back({0, 2, SyntaxKind::keyword});
            if (const auto at = text.find(L"\U0001f600"); at != text.npos) spans.push_back({at, at + 2, SyntaxKind::string});
            return spans;
        });
    }
    std::wstring text() {
        GETTEXTLENGTHEX length{GTL_PRECISE | GTL_NUMCHARS, 1200};
        std::wstring result(static_cast<std::size_t>(SendMessageW(window(), EM_GETTEXTLENGTHEX,
            reinterpret_cast<WPARAM>(&length), 0)) + 1, L'\0');
        GETTEXTEX request{static_cast<DWORD>(result.size() * sizeof(wchar_t)), GT_DEFAULT, 1200};
        result.resize(SendMessageW(window(), EM_GETTEXTEX, reinterpret_cast<WPARAM>(&request),
            reinterpret_cast<LPARAM>(result.data())));
        return result;
    }
    TextSelection selection() {
        CHARRANGE range{};
        SendMessageW(window(), EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&range));
        return {static_cast<std::size_t>(range.cpMin), static_cast<std::size_t>(range.cpMax)};
    }
    COLORREF foreground(LONG start, LONG end) {
        const auto selected = selection();
        const auto events = SendMessageW(window(), EM_SETEVENTMASK, 0, 0);
        CHARRANGE range{start, end};
        SendMessageW(window(), EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&range));
        CHARFORMAT2W format{sizeof(format)};
        SendMessageW(window(), EM_GETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&format));
        range = {static_cast<LONG>(selected.start), static_cast<LONG>(selected.end)};
        SendMessageW(window(), EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&range));
        SendMessageW(window(), EM_SETEVENTMASK, 0, events);
        require((format.dwMask & CFM_COLOR) != 0, "Range has a uniform native foreground");
        return format.crTextColor;
    }
    void check(std::wstring_view expected, int count) {
        if (failure) std::rethrow_exception(failure);
        require(text() == expected && document->text() == expected, "Exact retained and native UTF-16 text");
        require(changes == count, "Only real text edits invoke change callbacks");
    }
};
void scrollbar_presentation() {
    for (const auto style : {VisualStyle::classic, VisualStyle::winui}) {
        std::wstring value = L"if \U0001f600";
        for (int i = 0; i < 100; ++i) value += L"\rline";
        Fixture f(value);
        PartStyleValues text;
        text.font_family = make_style_font_family("Consolas");
        text.font_size = 14.0f;
        f.document->set_control_style_values(StylePart::text, text);
        f.enable();
        f.palette = Palette::system(ThemeMode::light, style);
        f.refresh();
        SendMessageW(f.window(), EM_SETSEL, value.size(), value.size());
        SendMessageW(f.window(), WM_CHAR, L'!', 1);
        f.document->set_selection({3, 5});
        f.refresh();
        SendMessageW(f.window(), EM_LINESCROLL, 0, 45);
        POINT scroll{};
        SendMessageW(f.window(), EM_GETSCROLLPOS, 0, reinterpret_cast<LPARAM>(&scroll));
        require(scroll.y > 0, "Scrollbar fixture has native scrolling");
        const auto selected = f.selection();
        const auto replacements = f.replacements;
        const auto color = [&] {
            RedrawWindow(f.parent, nullptr, nullptr, RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&message);
            SCROLLBARINFO bar{sizeof(bar)};
            require(GetScrollBarInfo(f.window(), OBJID_VSCROLL, &bar) != FALSE, "Read native scrollbar geometry");
            POINT point{bar.rcScrollBar.left + 3, bar.rcScrollBar.bottom - bar.dxyLineButton - 3};
            require(ScreenToClient(f.parent, &point) != FALSE, "Map native scrollbar track");
            const auto pixels = owned_window_capture::capture(f.parent);
            require(point.x >= 0 && point.x < pixels.width && point.y >= 0 && point.y < pixels.height,
                "Scrollbar sample is inside the owned capture");
            return pixels.data[point.y * pixels.width + point.x] & 0xffffff;
        };
        const auto light = color();
        f.palette = Palette::system(ThemeMode::dark, style);
        f.refresh();
        const auto dark = color();
        if (!f.palette.high_contrast) {
            require(dark != light && ((dark >> 16) & 255) < 110 && ((dark >> 8) & 255) < 110 && (dark & 255) < 110,
                "Dark document has a dark native scrollbar track");
        }
        const auto themes = f.themes;
        for (int i = 0; i < 20; ++i) f.refresh();
        require(f.themes == themes, "Repeated presentation does not reset the native theme");
        for (const auto mode : {ThemeMode::light, ThemeMode::dark, ThemeMode::high_contrast, ThemeMode::dark}) {
            f.palette = Palette::system(mode, style);
            f.refresh();
            const auto current = color();
            if (mode == ThemeMode::light || f.palette.high_contrast)
                require(current == light, "Light and high-contrast palettes restore the system scrollbar theme");
            else require(current == dark, "Dark theme returns after a palette switch");
            POINT after{};
            SendMessageW(f.window(), EM_GETSCROLLPOS, 0, reinterpret_cast<LPARAM>(&after));
            require(after.x == scroll.x && after.y == scroll.y && f.selection() == selected &&
                f.document->selection() == selected, "Scrollbar theme preserves native scroll and selection");
        }
        SetFocus(f.window());
        require(GetFocus() == f.window(), "Native document owns focus before theme switches");
        POINT focused_scroll{};
        SendMessageW(f.window(), EM_GETSCROLLPOS, 0, reinterpret_cast<LPARAM>(&focused_scroll));
        for (const auto mode : {ThemeMode::light, ThemeMode::dark, ThemeMode::high_contrast}) {
            f.palette = Palette::system(mode, style);
            f.refresh();
            require(GetFocus() == f.window() && f.selection() == selected, "Scrollbar theme preserves native focus and selection");
            POINT after{};
            SendMessageW(f.window(), EM_GETSCROLLPOS, 0, reinterpret_cast<LPARAM>(&after));
            require(after.x == focused_scroll.x && after.y == focused_scroll.y, "Focused theme changes preserve native scroll");
        }
        require(f.replacements == replacements, "Scrollbar theme never replaces native text");
        f.check(value + L"!", 1);
        SendMessageW(f.window(), WM_VSCROLL, SB_LINEDOWN, 0);
        POINT after{};
        SendMessageW(f.window(), EM_GETSCROLLPOS, 0, reinterpret_cast<LPARAM>(&after));
        require(after.y > focused_scroll.y, "The system scrollbar still scrolls the native document");
        require(f.document->command(TextCommand::undo), "Scrollbar theming retains native undo");
        f.check(value, 2);
        require(f.document->command(TextCommand::redo), "Scrollbar theming retains native redo");
        f.check(value + L"!", 3);
        std::cout << "Native scrollbar track: light=" << std::hex << light << " dark=" << dark << std::dec << '\n';
    }
}
void named_argument_colors() {
    for (const bool read_only : {false, true}) {
        Fixture f(L"text: Name", read_only);
        f.document->set_syntax_highlighter([](std::wstring_view) {
            return std::vector<SyntaxSpan>{{0, 4, SyntaxKind::variable}};
        });
        for (const auto theme : {ThemeMode::light, ThemeMode::dark}) {
            f.palette = Palette::system(theme);
            f.refresh();
            require(f.foreground(0, 4) == platform::native_color(f.palette.accent) &&
                f.foreground(6, 10) == platform::native_color(f.palette.text) &&
                f.foreground(0, 4) != f.foreground(6, 10), "Named argument differs from its identifier value");
        }
        f.palette.high_contrast = true;
        f.refresh();
        require(f.foreground(0, 4) == f.foreground(6, 10), "Named arguments preserve high-contrast text colors");
        f.check(L"text: Name", 0);
    }
}
void line_number_presentation() {
    for (const auto style : {VisualStyle::classic, VisualStyle::winui}) {
        std::wstring value = L"if \U0001f600";
        for (int i = 0; i < 120; ++i) value += L"\rline";
        Fixture f(value);
        f.palette = Palette::system(ThemeMode::light, style);
        PartStyleValues font;
        font.font_family = make_style_font_family("Consolas");
        font.font_size = 14.0f;
        f.document->set_control_style_values(StylePart::text, font);
        f.enable();
        f.document->set_line_numbers(true);
        f.refresh();
        const auto hwnd = f.window();
        const auto width = LOWORD(SendMessageW(hwnd, EM_GETMARGINS, 0, 0));
        require(width >= 30, "Line numbers reserve a usable native left margin");
        POINTL position{};
        SendMessageW(hwnd, EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&position), 0);
        require(position.x >= width, "Native source text starts to the right of the gutter");
        const auto gutter = [&] {
            RedrawWindow(f.parent, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
            const auto pixels = owned_window_capture::capture(f.parent);
            std::vector<std::uint32_t> result;
            for (int y = 0; y < 145; ++y)
                for (int x = 0; x < width; ++x)
                    result.push_back(pixels.data[y * pixels.width + x] & 0xffffff);
            require(std::adjacent_find(result.begin(), result.end(), std::not_equal_to<>()) != result.end(),
                "The owned capture contains visible line-number ink, not an empty margin");
            return result;
        };
        const auto top = gutter();
        SendMessageW(hwnd, EM_LINESCROLL, 0, 45);
        const auto scrolled = gutter();
        require(scrolled != top, "Scrolling paints the new visible logical line numbers");
        SendMessageW(hwnd, EM_SETSEL, value.size(), value.size());
        SendMessageW(hwnd, WM_CHAR, L'!', 1);
        f.document->set_selection({3, 5});
        f.refresh();
        POINT scroll{};
        SendMessageW(hwnd, EM_GETSCROLLPOS, 0, reinterpret_cast<LPARAM>(&scroll));
        const auto selected = f.selection();
        const auto replacements = f.replacements;
        for (const auto theme : {ThemeMode::dark, ThemeMode::high_contrast, ThemeMode::light}) {
            f.palette = Palette::system(theme, style);
            f.refresh();
            gutter();
            POINT after{};
            SendMessageW(hwnd, EM_GETSCROLLPOS, 0, reinterpret_cast<LPARAM>(&after));
            require(f.window() == hwnd && f.selection() == selected && scroll.x == after.x && scroll.y == after.y,
                "Gutter theme changes preserve native identity, scroll and selection");
        }
        f.document->set_line_numbers(false);
        f.refresh();
        require(LOWORD(SendMessageW(hwnd, EM_GETMARGINS, 0, 0)) == 0 && f.replacements == replacements,
            "Hiding line numbers restores the source margin without replacing text");
        require(f.document->command(TextCommand::undo), "Line-number presentation preserves native undo");
        f.check(value, 2);
        require(f.document->command(TextCommand::redo), "Line-number presentation preserves native redo");
        f.check(value + L"!", 3);
        f.document->set_line_numbers(true);
        f.refresh();
        const auto before_dpi = f.selection();
        f.bridge->update(144, f.palette);
        require(LOWORD(SendMessageW(hwnd, EM_GETMARGINS, 0, 0)) > width && f.selection() == before_dpi,
            "The native gutter scales with DPI without moving the selection");
    }
    Fixture f(std::wstring(100, L'W') + L"\r\r");
    f.document->set_line_numbers(true);
    f.refresh();
    const auto hwnd = f.window();
    const int width = LOWORD(SendMessageW(hwnd, EM_GETMARGINS, 0, 0));
    RedrawWindow(f.parent, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    const auto pixels = owned_window_capture::capture(f.parent);
    const auto ink_at = [&](LONG offset) {
        POINTL position{};
        SendMessageW(hwnd, EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&position), offset);
        int ink = 0;
        const auto background = pixels.data[position.y * pixels.width] & 0xffffff;
        for (int y = position.y; y < position.y + 12 && y < pixels.height; ++y)
            for (int x = 0; x < width - 4; ++x)
                if ((pixels.data[y * pixels.width + x] & 0xffffff) != background) ++ink;
        return ink;
    };
    const auto continuation = static_cast<LONG>(SendMessageW(hwnd, EM_LINEINDEX, 1, 0));
    require(continuation > 0 && continuation < 100 && ink_at(continuation) == 0,
        "Wrapped continuation rows do not acquire extra logical line numbers");
    require(ink_at(0) > 0 && ink_at(101) > 0 && ink_at(102) > 0,
        "The first line, blank line and final empty line have visible numbers");
}
void line_number_live_updates() {
    std::wstring value;
    for (int i = 0; i < 998; ++i) value += L"line\r";
    Fixture f(value);
    f.document->set_line_numbers(true);
    f.document->set_syntax_highlighter([](std::wstring_view source) {
        std::vector<SyntaxSpan> spans;
        for (auto start = source.find(L"line"); start != source.npos; start = source.find(L"line", start + 4))
            spans.push_back({start, start + 4, SyntaxKind::keyword});
        return spans;
    });
    f.refresh();
    const auto hwnd = f.window();
    RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
    const int width = LOWORD(SendMessageW(hwnd, EM_GETMARGINS, 0, 0));
    const auto before = f.selection();
    const auto text = f.text();
    const auto gdi = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    const auto started = std::chrono::steady_clock::now();
    for (int i = 0; i < 80; ++i) {
        const auto paints = f.paints, prints = f.prints, positions = f.positions;
        SendMessageW(hwnd, WM_VSCROLL, i % 2 ? SB_LINEUP : SB_LINEDOWN, 0);
        UpdateWindow(hwnd);
        require(f.prints > prints && f.prints - prints == f.paints - paints,
            "Each live scroll paint composes RichEdit and its gutter into one buffer");
        require(f.positions - positions < 40, "Gutter paint work is bounded by visible rows, not total source lines");
        require(!GetUpdateRect(hwnd, nullptr, FALSE), "A completed scroll frame leaves no delayed gutter redraw");
        POINT scroll{};
        SendMessageW(hwnd, EM_GETSCROLLPOS, 0, reinterpret_cast<LPARAM>(&scroll));
        SendMessageW(hwnd, EM_SETSCROLLPOS, 0, reinterpret_cast<LPARAM>(&scroll));
        require(!GetUpdateRect(hwnd, nullptr, FALSE), "An unchanged scroll offset does not schedule another gutter frame");
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
    require(elapsed < 2000, "Eighty synchronous gutter scroll frames complete within two seconds");
    require(f.selection() == before && f.text() == text && f.changes == 0,
        "Buffered scroll painting preserves native text, selection and callbacks");
    require(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) <= gdi + 2, "Repeated buffered paints do not retain GDI objects");
    SendMessageW(hwnd, EM_SETSEL, value.size(), value.size());
    SendMessageW(hwnd, EM_SCROLLCARET, 0, 0);
    SetFocus(hwnd);
    GUITHREADINFO caret{sizeof(caret)};
    require(GetGUIThreadInfo(GetCurrentThreadId(), &caret) && caret.hwndCaret == hwnd && !IsRectEmpty(&caret.rcCaret),
        "The native editor still owns a visible caret");
    bool checked_in_callback = false;
    std::vector<DWORD> immediate;
    const auto gutter = [&] {
        RECT bounds{}; GetClientRect(hwnd, &bounds);
        const int columns = LOWORD(SendMessageW(hwnd, EM_GETMARGINS, 0, 0));
        auto dc = CreateCompatibleDC(nullptr);
        require(dc != nullptr, "Create live gutter snapshot DC");
        struct DeleteDc { HDC dc; ~DeleteDc() { DeleteDC(dc); } } delete_dc{dc};
        BITMAPINFO info{};
        info.bmiHeader = {sizeof(BITMAPINFOHEADER), bounds.right, -bounds.bottom, 1, 32, BI_RGB};
        void* bytes{};
        auto bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bytes, nullptr, 0);
        require(bitmap != nullptr, "Create live gutter snapshot bitmap");
        struct DeleteBitmap { HBITMAP value; ~DeleteBitmap() { DeleteObject(value); } } delete_bitmap{bitmap};
        auto previous = SelectObject(dc, bitmap);
        struct Restore { HDC dc; HGDIOBJ value; ~Restore() { SelectObject(dc, value); } } restore{dc, previous};
        SendMessageW(hwnd, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(dc), PRF_CLIENT | PRF_ERASEBKGND);
        GdiFlush();
        const auto data = static_cast<DWORD*>(bytes);
        const std::vector<DWORD> frame(data, data + bounds.right * bounds.bottom);
        require(SendMessageW(hwnd, WM_ERASEBKGND, reinterpret_cast<WPARAM>(dc), 0) != 0,
            "Buffered document painting consumes background erase");
        GdiFlush();
        require(std::equal(frame.begin(), frame.end(), data),
            "Background erase cannot publish a blank gutter or editor between complete frames");
        std::vector<DWORD> pixels;
        for (int y = 0; y < bounds.bottom; ++y)
            for (int x = 0; x < columns; ++x) pixels.push_back(data[y * bounds.right + x]);
        return pixels;
    };
    f.document->on_change([&](auto&) {
        ++f.changes;
        if (checked_in_callback) return;
        checked_in_callback = true;
        require(LOWORD(SendMessageW(hwnd, EM_GETMARGINS, 0, 0)) > width,
            "The 999-to-1000-line gutter grows before application change callbacks");
        UpdateWindow(hwnd);
        immediate = gutter();
        require(!GetUpdateRect(hwnd, nullptr, FALSE), "Newline gutter is painted before the application's change callback completes");
    });
    SendMessageW(hwnd, WM_KEYDOWN, VK_RETURN, 1);
    SendMessageW(hwnd, WM_CHAR, L'\r', 1);
    SendMessageW(hwnd, WM_KEYUP, VK_RETURN, 1);
    if (f.failure) std::rethrow_exception(f.failure);
    require(checked_in_callback && f.document->text() == value + L"\r",
        "A native newline updates the gutter without waiting for an application refresh");
    const auto after_edit = gutter();
    UpdateWindow(hwnd);
    const auto visible = owned_window_capture::capture(f.parent);
    const int columns = LOWORD(SendMessageW(hwnd, EM_GETMARGINS, 0, 0));
    for (int y = 0; y < static_cast<int>(after_edit.size()) / columns; ++y)
        for (int x = 0; x < columns; ++x)
            require((visible.data[y * visible.width + x] & 0xffffff) == (after_edit[y * columns + x] & 0xffffff),
                "Ordinary newline painting publishes the complete current gutter to the owned window");
    f.refresh();
    UpdateWindow(hwnd);
    require(!immediate.empty() && after_edit == gutter(), "Deferred syntax presentation does not repair stale line-number text");
    GUITHREADINFO after{sizeof(after)};
    require(GetGUIThreadInfo(GetCurrentThreadId(), &after) && after.hwndCaret == hwnd &&
        !IsRectEmpty(&after.rcCaret) && GetFocus() == hwnd, "Buffered newline painting preserves the native caret and focus");
    require(f.document->command(TextCommand::undo), "Buffered painting preserves newline undo");
    f.check(value, 2);
    require(LOWORD(SendMessageW(hwnd, EM_GETMARGINS, 0, 0)) == width, "Undo immediately shrinks the gutter at a digit boundary");
    require(f.document->command(TextCommand::redo), "Buffered painting preserves newline redo");
    f.check(value + L"\r", 3);
    const auto edit_started = std::chrono::steady_clock::now();
    for (int i = 0; i < 20; ++i) {
        SendMessageW(hwnd, WM_KEYDOWN, VK_RETURN, 1);
        SendMessageW(hwnd, WM_CHAR, L'\r', 1);
        SendMessageW(hwnd, WM_KEYUP, VK_RETURN, 1);
        UpdateWindow(hwnd);
        const auto current = gutter();
        f.refresh();
        UpdateWindow(hwnd);
        require(current == gutter(), "Repeated newlines paint current numbering without a delayed repair");
    }
    f.check(value + std::wstring(21, L'\r'), 23);
    const auto edit_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - edit_started).count();
    require(edit_elapsed < 2000, "Twenty synchronous newline frames and syntax refreshes complete within two seconds");
    std::cout << "Live gutter: 80 scroll frames in " << elapsed << " ms, 20 newline frames in "
        << edit_elapsed << " ms; synchronous numbering and bounded GDI resources\n";
}
void caret_presentation(VisualStyle style) {
    Window window({L"XUI document caret fixture", {600, 380}});
    window.set_visual_style(style);
    auto editor = std::make_shared<MultilineText>();
    const std::wstring source = L"first line\rshort\r\r    \rlast line";
    editor->set_text(source);
    editor->set_monospace(true);
    auto other = std::make_shared<Button>(L"Other focus");
    auto root = std::make_shared<Stack>(Axis::vertical);
    editor->set_fixed_size({520, 240});
    root->add(std::make_shared<ScrollView>(editor), 1); root->add(other);
    window.set_content(root);
    int changes{};
    editor->on_change([&](auto&) { ++changes; });
    bool completed{};
    require(window.post([&] {
        const auto host = FindWindowW(L"Xui.Window.1", L"XUI document caret fixture");
        require(host != nullptr, "Find native caret host");
        require(window.focus(*editor), "Focus hosted document");
        const auto hwnd = GetFocus();
        owned_window_capture::Device capture;
        const auto present = [&] {
            require(RedrawWindow(host, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOCHILDREN) != FALSE,
                "Present the parent frame without repairing native children");
        };
        for (const auto theme : {ThemeMode::light, ThemeMode::dark, ThemeMode::high_contrast}) {
            window.set_theme(theme);
            for (const bool numbered : {false, true}) {
                editor->set_line_numbers(numbered);
                require(window.focus(*other), "Focus outside the baseline document");
                SendMessageW(host, WM_APP + 12, 0, 0);
                RedrawWindow(host, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
                const auto baseline = owned_window_capture::capture(host, capture);
                const auto difference = [&](const owned_window_capture::Pixels& image, const RECT& bounds) {
                    require(bounds.left >= 0 && bounds.top >= 0 && bounds.right <= image.width && bounds.bottom <= image.height,
                        "The observed native caret is inside the owned capture");
                    int count{};
                    for (int y = bounds.top; y < bounds.bottom; ++y)
                        for (int x = bounds.left; x < bounds.right; ++x)
                            count += (image.data[y * image.width + x] & 0xffffff) != (baseline.data[y * baseline.width + x] & 0xffffff);
                    return count;
                };
                require(window.focus(*editor), "Restore native document focus");
                for (const auto position : {10, 16, 17, 22, 32}) {
                    SendMessageW(hwnd, EM_SETSEL, position, position);
                    HideCaret(hwnd); ShowCaret(hwnd);
                    GUITHREADINFO caret{sizeof(caret)};
                    require(GetGUIThreadInfo(GetCurrentThreadId(), &caret) && caret.hwndCaret == hwnd &&
                        (caret.flags & GUI_CARETBLINKING), "Hosted RichEdit owns a shown line-end caret");
                    auto bounds = caret.rcCaret;
                    MapWindowPoints(hwnd, host, reinterpret_cast<POINT*>(&bounds), 2);
                    present();
                    require(difference(owned_window_capture::capture(host, capture), bounds) > 0,
                        "Parent composition preserves visible native caret pixels");
                    GUITHREADINFO after{sizeof(after)};
                    require(GetGUIThreadInfo(GetCurrentThreadId(), &after) && after.hwndCaret == hwnd &&
                        EqualRect(&caret.rcCaret, &after.rcCaret) && GetFocus() == hwnd &&
                        editor->selection() == TextSelection{static_cast<std::size_t>(position), static_cast<std::size_t>(position)},
                        "Composing the native caret preserves its owner, position, selection and focus");
                    require(HideCaret(hwnd) != FALSE, "Hide the native caret for a parent frame");
                    present();
                    require(difference(owned_window_capture::capture(host, capture), bounds) == 0,
                        "Parent painting does not reveal an already hidden caret");
                    require(ShowCaret(hwnd) != FALSE, "Restore the original native caret visibility");
                    if (numbered) {
                        POINTL start{};
                        SendMessageW(hwnd, EM_POSFROMCHAR, reinterpret_cast<WPARAM>(&start), 0);
                        SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(start.x + 1, start.y + 1));
                        SendMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(start.x + 1, start.y + 1));
                    } else {
                        const auto key = position == 17 ? VK_DOWN : VK_HOME;
                        SendMessageW(hwnd, WM_KEYDOWN, key, 1);
                        SendMessageW(hwnd, WM_KEYUP, key, 1);
                    }
                    UpdateWindow(hwnd);
                    require(HideCaret(hwnd) != FALSE, "Hide only the current caret to inspect its previous position");
                    const auto moved = owned_window_capture::capture(host, capture);
                    require(ShowCaret(hwnd) != FALSE, "Restore caret after stale-pixel inspection");
                    require(difference(moved, bounds) == 0,
                        "Keyboard and mouse movement leave no stale line-end caret pixels after parent composition");
                }
            }
        }
        require(editor->text() == source && changes == 0, "Caret presentation does not edit the document");
        SendMessageW(hwnd, EM_SETSEL, source.size(), source.size());
        SendMessageW(hwnd, WM_CHAR, L'!', 1);
        present();
        require(editor->command(TextCommand::undo) && editor->text() == source,
            "Caret presentation preserves native typing and undo");
        require(editor->command(TextCommand::redo) && editor->text() == source + L"!",
            "Caret presentation preserves native redo");
        completed = true;
        window.close();
    }), "Queue hosted caret test");
    const auto result = Application::run(window);
    if (!window.error().empty()) std::wcerr << window.error() << '\n';
    require(result == 0 && completed, "Hosted caret regression completes");
}
void presentation_and_history() {
    Fixture f;
    const auto initial = f.document->text();
    const auto hwnd = f.window();
    require(!f.document->rich() && (SendMessageW(hwnd, EM_GETTEXTMODE, 0, 0) & TM_RICHTEXT), "Plain contract with rich native presentation");
    SendMessageW(hwnd, EM_SETSEL, initial.size(), initial.size());
    SendMessageW(hwnd, WM_CHAR, L'!', 1);
    f.check(initial + L"!", 1);
    f.document->set_selection({3, 5});
    f.refresh();
    const auto selected = f.selection();
    const auto modified = SendMessageW(hwnd, EM_GETMODIFY, 0, 0);
    const auto events = SendMessageW(hwnd, EM_GETEVENTMASK, 0, 0);
    const auto replacements = f.replacements;
    const auto clipboard = GetClipboardSequenceNumber();
    f.enable();
    f.refresh();
    require(f.selection() == selected && f.document->selection() == selected, "Syntax preserves both selections");
    require(SendMessageW(hwnd, EM_GETMODIFY, 0, 0) == modified && SendMessageW(hwnd, EM_GETEVENTMASK, 0, 0) == events,
        "Syntax restores modified state and events");
    require(f.replacements == replacements && GetClipboardSequenceNumber() == clipboard, "Syntax never replaces text or uses clipboard");
    require(f.foreground(0, 2) == platform::native_color(f.palette.accent) &&
        f.foreground(3, 5) == platform::native_color(f.palette.folder) &&
        f.foreground(6, 7) == platform::native_color(f.palette.text), "Token and unclassified native foregrounds");
    const auto formats = f.formats, tokens = f.tokens;
    f.refresh(); f.refresh(); f.refresh();
    require(f.formats == formats && f.tokens == tokens, "Repeated updates do not restyle or retokenize");
    require(f.document->command(TextCommand::undo), "Syntax installation preserves previous typing undo");
    f.check(initial, 2);
    f.refresh();
    require(f.document->command(TextCommand::redo), "Syntax refresh preserves redo");
    f.check(initial + L"!", 3);
    f.refresh();
    const auto revision = f.document->revision(), syntax = f.document->syntax_revision();
    f.document->replace_range({0, 2}, f.document->text(), L"IF");
    f.check(L"IF \U0001f600\rvalue!", 4);
    require(f.document->revision() == revision && f.document->syntax_revision() > syntax, "Native edit advances syntax, not property revision");
    f.refresh();
    require(f.foreground(0, 2) == platform::native_color(f.palette.text), "Removed token resets default foreground");
    require(f.document->command(TextCommand::undo), "One undo reverses semantic edit, not formatting");
    f.check(initial + L"!", 5);
    f.refresh();
    require(f.foreground(0, 2) == platform::native_color(f.palette.accent), "Undo retokenizes original text");
    f.document->set_syntax_highlighter({});
    f.refresh();
    require(f.foreground(0, 2) == platform::native_color(f.palette.text) &&
        f.foreground(3, 5) == platform::native_color(f.palette.text), "Disable resets every token");
    require(f.document->command(TextCommand::redo), "Disable preserves native redo");
    f.check(L"IF \U0001f600\rvalue!", 6);
}
void colors_and_composition(bool line_numbers) {
    Fixture f;
    f.document->set_line_numbers(line_numbers);
    f.enable(); f.refresh();
    const auto hwnd = f.window();
    const auto initial = f.document->text();
    const auto tokens = f.tokens;
    f.palette = Palette::system(ThemeMode::dark);
    f.refresh();
    require(f.tokens == tokens && f.foreground(0, 2) == platform::native_color(f.palette.accent), "Dark palette recolors without tokenization");
    auto contrast = f.palette;
    contrast.high_contrast = true;
    contrast.text = D2D1::ColorF(0xffff00);
    f.palette = contrast; f.refresh();
    require(f.foreground(0, 2) == platform::native_color(contrast.text) &&
        f.foreground(3, 5) == platform::native_color(contrast.text), "High contrast suppresses syntax colors");
    f.palette = Palette::system(ThemeMode::light);
    f.document->set_enabled(false); EnableWindow(hwnd, FALSE); f.refresh();
    require(f.foreground(0, 2) == platform::native_color(f.palette.disabled) &&
        f.foreground(3, 5) == platform::native_color(f.palette.disabled), "Disabled ink suppresses syntax colors");
    f.document->set_enabled(true); EnableWindow(hwnd, TRUE); f.refresh();
    require(f.foreground(0, 2) == platform::native_color(f.palette.accent), "Reenable restores tokens");
    SendMessageW(hwnd, EM_SETMODIFY, FALSE, 0);
    f.enable(); f.refresh();
    require(!SendMessageW(hwnd, EM_GETMODIFY, 0, 0), "Rehighlight keeps clean document clean");
    const auto formats = f.formats, before = f.tokens, themes = f.themes;
    SendMessageW(hwnd, WM_IME_STARTCOMPOSITION, 0, 0);
    SendMessageW(hwnd, EM_SETSEL, initial.size(), initial.size());
    SendMessageW(hwnd, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"!"));
    f.palette = Palette::system(ThemeMode::dark);
    f.refresh();
    RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
    require(f.bridge->composing() && f.tokens == before && f.formats == formats && f.themes == themes && f.changes == 0 &&
        f.document->text() == initial && f.text() == initial + L"!", "Composition painting does not publish or style intermediate text");
    SendMessageW(hwnd, WM_IME_ENDCOMPOSITION, 0, 0);
    f.refresh();
    f.check(initial + L"!", 1);
    require(f.tokens == before + 1 && f.foreground(0, 2) == platform::native_color(f.palette.accent),
        "Committed composition refreshes syntax and deferred palette");
}
void scrolling_and_errors() {
    std::wstring value = L"if \U0001f600";
    for (int i = 0; i < 100; ++i) value += L"\rline";
    Fixture f(value);
    SendMessageW(f.window(), EM_LINESCROLL, 0, 45);
    POINT before{}, after{};
    SendMessageW(f.window(), EM_GETSCROLLPOS, 0, reinterpret_cast<LPARAM>(&before));
    require(before.y > 0, "Fixture is scrolled");
    f.enable(); f.refresh();
    SendMessageW(f.window(), EM_GETSCROLLPOS, 0, reinterpret_cast<LPARAM>(&after));
    require(before.x == after.x && before.y == after.y, "Token formatting preserves scroll position");
    bool fail{};
    f.document->set_syntax_highlighter([&](std::wstring_view text) {
        if (fail) throw std::runtime_error("failed native tokenizer");
        return std::vector<SyntaxSpan>{{0, text.size(), SyntaxKind::keyword}};
    });
    f.refresh(); fail = true;
    bool rejected{};
    try { f.document->replace_range({0, 2}, value, L"IF"); } catch (const std::runtime_error&) { rejected = true; }
    require(rejected, "Native tokenizer failure is explicit");
    value.replace(0, 2, L"IF");
    f.check(value, 1);
    f.refresh();
    require(f.document->syntax_enabled() && f.document->syntax_spans().empty() &&
        f.foreground(0, 2) == platform::native_color(f.palette.text), "Native error clears stale presentation without reverting text");
    fail = false;
    require(f.document->command(TextCommand::undo), "Failed tokenizer does not lose edit undo");
    value.replace(0, 2, L"if");
    f.check(value, 2); f.refresh();
    require(f.foreground(0, 2) == platform::native_color(f.palette.accent), "Native tokenizer recovers after error");
}
void typing_and_properties() {
    Fixture f;
    f.enable(); f.refresh();
    const auto initial = f.document->text();
    SendMessageW(f.window(), EM_SETSEL, initial.size(), initial.size());
    for (const auto c : std::wstring_view(L"abc")) {
        SendMessageW(f.window(), WM_CHAR, c, 1);
        f.refresh();
    }
    f.check(initial + L"abc", 3);
    require(f.document->command(TextCommand::undo), "Undo consecutive typing");
    f.check(initial, 4);
    f.refresh();
    require(f.document->command(TextCommand::redo), "Redo consecutive typing");
    f.check(initial + L"abc", 5);
    f.document->set_text(L"if\n\U0001f600");
    f.refresh();
    f.check(L"if\r\U0001f600", 5);
    require(f.foreground(0, 2) == platform::native_color(f.palette.accent) &&
        f.foreground(3, 5) == platform::native_color(f.palette.folder), "New text property uses its normalized syntax snapshot");
}
void plain_shortcuts() {
    Fixture f;
    f.enable(); f.refresh();
    f.document->set_selection({0, 2}); f.refresh();
    BYTE original[256]{}, keys[256]{};
    require(GetKeyboardState(original) != FALSE, "Read keyboard state");
    std::copy(std::begin(original), std::end(original), std::begin(keys));
    keys[VK_CONTROL] = keys[VK_LCONTROL] = 0x80;
    require(SetKeyboardState(keys) != FALSE, "Set fixture modifier state");
    SendMessageW(f.window(), WM_KEYDOWN, L'B', 1);
    CHARFORMAT2W key_format{sizeof(key_format)};
    SendMessageW(f.window(), EM_GETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&key_format));
    SendMessageW(f.window(), WM_CHAR, 2, 1);
    SetKeyboardState(original);
    CHARFORMAT2W format{sizeof(format)};
    SendMessageW(f.window(), EM_GETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&format));
    require(!(key_format.dwEffects & CFE_BOLD) && !(format.dwEffects & CFE_BOLD),
        "Plain document ignores native rich formatting shortcuts");
    f.check(L"if \U0001f600\rvalue", 0);
}
void readonly_presentation() {
    const std::wstring initial = L"if \U0001f600\rvalue";
    {
        Fixture f(initial, true);
        require(f.foreground(0, 2) == platform::native_color(f.palette.accent) &&
            f.foreground(3, 5) == platform::native_color(f.palette.folder), "Initially read-only document receives syntax colors");
        f.document->set_selection({3, 5}); f.refresh();
        const auto modified = SendMessageW(f.window(), EM_GETMODIFY, 0, 0);
        const auto events = SendMessageW(f.window(), EM_GETEVENTMASK, 0, 0);
        const auto replacements = f.replacements;
        f.palette = Palette::system(ThemeMode::dark); f.refresh();
        require(f.foreground(0, 2) == platform::native_color(f.palette.accent) &&
            f.selection() == TextSelection{3, 5} && f.document->selection() == TextSelection{3, 5},
            "Read-only palette refresh preserves native and model selection");
        require(f.document->read_only() && (GetWindowLongPtrW(f.window(), GWL_STYLE) & ES_READONLY) &&
            f.writable_transitions == 0, "Presentation never makes a read-only control writable");
        require(modified == SendMessageW(f.window(), EM_GETMODIFY, 0, 0) &&
            events == SendMessageW(f.window(), EM_GETEVENTMASK, 0, 0) && f.replacements == replacements,
            "Read-only presentation preserves modified state and events without replacing text");
        require(!f.document->command(TextCommand::undo) && !f.document->command(TextCommand::cut) &&
            !f.document->command(TextCommand::paste), "Read-only commands stay restricted");
        SendMessageW(f.window(), WM_CHAR, L'X', 1);
        bool rejected{};
        try { f.document->replace_range({0, 2}, initial, L"IF"); } catch (const std::logic_error&) { rejected = true; }
        require(rejected, "Read-only semantic replacement stays restricted");
        f.check(initial, 0);
    }
    Fixture f(initial);
    SendMessageW(f.window(), EM_SETSEL, initial.size(), initial.size());
    SendMessageW(f.window(), WM_CHAR, L'!', 1);
    f.document->set_read_only(true); f.enable(); f.refresh();
    require(SendMessageW(f.window(), EM_CANUNDO, 0, 0) != 0 && f.writable_transitions == 0,
        "Read-only syntax formatting retains earlier undo without dropping read-only");
    f.document->set_read_only(false); f.refresh();
    require(f.document->command(TextCommand::undo), "Undo remains available after leaving read-only");
    f.check(initial, 2); f.refresh();
    f.document->set_read_only(true); f.enable(); f.refresh();
    require(SendMessageW(f.window(), EM_CANREDO, 0, 0) != 0, "Read-only syntax formatting preserves redo");
    f.document->set_read_only(false); f.refresh();
    require(f.document->command(TextCommand::redo), "Redo remains available after leaving read-only");
    f.check(initial + L"!", 3);
}
}
int main(int argc, char** argv) {
    try {
        require(SUCCEEDED(OleInitialize(nullptr)), "Initialize native document COM");
        struct Com { ~Com() { OleUninitialize(); } } com;
        if (argc == 2 && std::string_view(argv[1]) == "--theme") {
            scrollbar_presentation();
            return 0;
        }
        if (argc == 2 && std::string_view(argv[1]) == "--caret") {
            caret_presentation(VisualStyle::classic);
            caret_presentation(VisualStyle::winui);
            return 0;
        }
        readonly_presentation();
        line_number_presentation();
        line_number_live_updates();
        caret_presentation(VisualStyle::classic);
        caret_presentation(VisualStyle::winui);
        named_argument_colors();
        presentation_and_history();         colors_and_composition(false);
        colors_and_composition(true); scrolling_and_errors(); typing_and_properties(); plain_shortcuts();
        std::cout << "Native syntax colors, undo/redo, selection, scroll, composition and error handling passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
