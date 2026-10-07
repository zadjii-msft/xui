#include "xui/application.hpp"
#include "xui/styling.hpp"
#include "xui/control_styling.hpp"
#include "xui/commands.hpp"
#include "xui/documents.hpp"
#include "xui/menu_bar.hpp"
#include "xui/suggestions.hpp"
#include "../src/drawing.hpp"
#include <windows.h>
#include <dwmapi.h>
#include <atomic>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <string>
#include <thread>

namespace xui {
struct DrawingTestAccess {
    static void fail_next_frame(HRESULT result) { Drawing::end_result_override_ = result; }
};
}

// Experimental composition presentation: backdrop acrylic in arbitrary shapes, layered native
// children, and click-through transparent cards. Requires an unlocked interactive desktop.
namespace {
using namespace xui;
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void pump(int milliseconds = 150) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
    do {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    } while (std::chrono::steady_clock::now() < end);
}
template<class F> bool rejects(F&& action) {
    try { action(); } catch (const std::invalid_argument&) { return true; } catch (const std::logic_error&) { return true; }
    return false;
}
constexpr COLORREF stripe_colors[]{RGB(220, 30, 30), RGB(30, 200, 30), RGB(30, 30, 230), RGB(240, 220, 20)};
LRESULT CALLBACK stripes(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{};
        const HDC dc = BeginPaint(hwnd, &paint);
        RECT client{};
        GetClientRect(hwnd, &client);
        for (int i = 0; i < 4; ++i) {
            RECT band{client.right * i / 4, 0, client.right * (i + 1) / 4, client.bottom};
            const HBRUSH brush = CreateSolidBrush(stripe_colors[i]);
            FillRect(dc, &band, brush);
            DeleteObject(brush);
        }
        EndPaint(hwnd, &paint);
        return 0;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}
COLORREF screen(POINT point) {
    const HDC dc = GetDC(nullptr);
    const COLORREF color = GetPixel(dc, point.x, point.y);
    ReleaseDC(nullptr, dc);
    return color;
}
void save_screen(RECT area, const std::wstring& path) {
    const int width = area.right - area.left, height = area.bottom - area.top;
    const HDC source = GetDC(nullptr);
    const HDC memory = CreateCompatibleDC(source);
    const HBITMAP bitmap = CreateCompatibleBitmap(source, width, height);
    SelectObject(memory, bitmap);
    BitBlt(memory, 0, 0, width, height, source, area.left, area.top, SRCCOPY);
    BITMAPINFOHEADER info{sizeof(info), width, -height, 1, 32, BI_RGB};
    std::string pixels(static_cast<std::size_t>(width) * height * 4, '\0');
    GetDIBits(memory, bitmap, 0, height, pixels.data(), reinterpret_cast<BITMAPINFO*>(&info), DIB_RGB_COLORS);
    BITMAPFILEHEADER file{0x4d42, static_cast<DWORD>(sizeof(file) + sizeof(info) + pixels.size()), 0, 0, sizeof(file) + sizeof(info)};
    FILE* stream{};
    if (_wfopen_s(&stream, path.c_str(), L"wb") == 0 && stream) {
        fwrite(&file, sizeof(file), 1, stream);
        fwrite(&info, sizeof(info), 1, stream);
        fwrite(pixels.data(), 1, pixels.size(), stream);
        fclose(stream);
    }
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(nullptr, source);
}
LRESULT metric(HWND hwnd, WPARAM key) { return SendMessageW(hwnd, WM_APP + 60, key, 0); }
int channel_difference(COLORREF a, COLORREF b) {
    return std::abs(GetRValue(a) - GetRValue(b)) + std::abs(GetGValue(a) - GetGValue(b)) + std::abs(GetBValue(a) - GetBValue(b));
}
bool transparency_effects() {
    DWORD value{1}, size{sizeof(value)};
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"EnableTransparency", RRF_RT_REG_DWORD, nullptr, &value, &size);
    return value != 0;
}
HWND child_of_class(HWND parent, const wchar_t* name) {
    struct Search { const wchar_t* name; HWND result{}; } search{name};
    EnumChildWindows(parent, [](HWND child, LPARAM data) -> BOOL {
        auto& search = *reinterpret_cast<Search*>(data);
        wchar_t text[32]{};
        GetClassNameW(child, text, 32);
        if (_wcsicmp(text, search.name) != 0) return TRUE;
        search.result = child;
        return FALSE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.result;
}

void contracts() {
    WindowOptions options;
    options.composition = true;
    options.backdrop = WindowBackdrop::mica;
    require(rejects([&] { Window rejected(options); }), "A composition window rejects a system backdrop");
    Window window;
    window.set_composition(true);
    require(window.composition(), "Composition is requested before show");
    require(rejects([&] { window.set_backdrop(WindowBackdrop::acrylic); }), "A composition window rejects a backdrop later");
    window.set_composition(false);
    Stack stack(Axis::vertical);
    stack.set_material(SurfaceMaterial::backdrop_acrylic);
    require(stack.surface() && stack.material() == SurfaceMaterial::backdrop_acrylic, "Backdrop acrylic paints a surface");
}

void transparent_card(const std::wstring& output) {
    WNDCLASSW cls{};
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpfnWndProc = stripes;
    cls.lpszClassName = L"Xui.CompositionStripes";
    RegisterClassW(&cls);
    const int dpi = static_cast<int>(GetDpiForSystem());
    const auto px = [&](float dips) { return static_cast<int>(std::lround(dips * dpi / 96.0f)); };
    const POINT origin{px(200), px(200)};
    const HWND back = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, cls.lpszClassName, L"",
        WS_POPUP, origin.x - px(40), origin.y - px(40), px(520), px(320), nullptr, nullptr, cls.hInstance, nullptr);
    require(back != nullptr, "Create the stripe background");
    ShowWindow(back, SW_SHOWNA);
    UpdateWindow(back);

    Application app;
    WindowOptions options;
    options.title = L"XUI composition card";
    options.size = {440, 240};
    options.visual_style = VisualStyle::winui;
    options.transparent = true;
    options.composition = true;
    auto owned = app.create_window(options);
    auto& window = *owned;
    window.on_native_created([](HWND hwnd) {
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    });
    window.set_placement({origin.x, origin.y, px(440), px(240), false});
    window.set_drag_region({20, 20, 400, 16});
    auto root = std::make_shared<Stack>(Axis::vertical);
    root->set_padding({20, 20, 20, 20});
    // An opaque rounded outline around the card: its rounded clip is a Direct2D layer.
    auto outline = std::make_shared<Stack>(Axis::vertical);
    outline->set_padding({4, 4, 4, 4});
    PartStyleValues outline_style;
    outline_style.background = ThemeColor{0x111827};
    outline_style.corner_radius = 32.0f;
    outline->set_control_style_values(StylePart::root, outline_style);
    auto card = std::make_shared<Stack>(Axis::vertical);
    card->set_material(SurfaceMaterial::backdrop_acrylic);
    card->set_padding({16, 24, 16, 16});
    card->set_spacing(10);
    PartStyleValues shape;
    shape.corner_radius = 28.0f;
    card->set_control_style_values(StylePart::root, shape);
    card->add(std::make_shared<Label>(L"Composition acrylic card"));
    auto editor = std::make_shared<TextInput>(L"Composition editor");
    editor->set_text(L"Native text");
    card->add(editor);
    card->add(std::make_shared<Button>(L"Card action"));
    outline->add(card, 1);
    root->add(outline, 1);
    window.set_content(root);
    app.show(window);
    pump(600);
    const HWND hwnd = window.native_window();
    RECT bounds{};
    GetWindowRect(hwnd, &bounds);
    require((GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_NOREDIRECTIONBITMAP) != 0, "The card has no redirection surface");
    require((GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_LAYERED) == 0, "The card is not layered");
    require(metric(hwnd, 40) == 1, "The window reports composition presentation");
    const bool effects = transparency_effects();
    require(metric(hwnd, 39) == (effects ? 1 : 0), "One backdrop visual mirrors the acrylic card");
    if (!output.empty()) save_screen({bounds.left - px(40), bounds.top - px(40), bounds.right + px(40), bounds.bottom + px(40)},
        output + L"\\composition-card.bmp");

    const auto client = [&](float x, float y) { return POINT{bounds.left + px(x), bounds.top + px(y)}; };
    // The transparent margin shows the stripes unchanged and passes input through.
    const auto margin = client(6, 120);
    require(channel_difference(screen(margin), screen({bounds.left - px(6), margin.y})) < 24,
        "The transparent margin shows the windows behind the card");
    require(WindowFromPoint(margin) != hwnd, "The transparent margin passes pointer input through");
    require(WindowFromPoint(client(220, 210)) == hwnd || IsChild(hwnd, WindowFromPoint(client(220, 210))),
        "The card body is hit-testable");
    // A rounded corner of the card is outside its shape.
    require(WindowFromPoint(client(22, 22)) != hwnd, "The card's rounded corner passes pointer input through");
    if (effects) {
        // Find a sharp stripe boundary just below the card, then compare the same columns inside it.
        RECT stripe_bounds{};
        GetWindowRect(back, &stripe_bounds);
        const int boundary = stripe_bounds.left + (stripe_bounds.right - stripe_bounds.left) / 4;
        const int below = bounds.bottom + px(16), inside = bounds.top + px(200);
        const POINT raw_left{boundary - px(6), below}, raw_right{boundary + px(6), below};
        const POINT left{boundary - px(6), inside}, right{boundary + px(6), inside};
        const int raw = channel_difference(screen(raw_left), screen(raw_right));
        const int blurred = channel_difference(screen(left), screen(right));
        std::cout << "stripe edge difference raw=" << raw << " card=" << blurred << '\n';
        require(raw > 200 && blurred < raw / 2, "The card blurs the stripes behind the window");
    }
    const HWND edit = child_of_class(hwnd, L"Edit");
    require(edit != nullptr, "Find the native editor");
    require((GetWindowLongPtrW(edit, GWL_EXSTYLE) & WS_EX_LAYERED) != 0, "Native editors are layered children");
    SendMessageW(edit, EM_SETSEL, 11, 11);
    for (const wchar_t ch : std::wstring(L" typed")) SendMessageW(edit, WM_CHAR, ch, 0);
    pump();
    require(editor->text() == L"Native text typed", "The native editor still edits text");
    RECT edit_bounds{};
    GetWindowRect(edit, &edit_bounds);
    const POINT edit_point{edit_bounds.right - px(8), (edit_bounds.top + edit_bounds.bottom) / 2};
    const COLORREF behind = screen({edit_point.x, bounds.bottom + px(20)});
    require(channel_difference(screen(edit_point), behind) > 60, "The native editor is opaque over the backdrop");

    // A larger frame recreates nothing but the swap chain buffers and keeps presenting.
    SetWindowPos(hwnd, nullptr, 0, 0, px(480), px(260), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    pump(400);
    GetWindowRect(hwnd, &bounds);
    require(metric(hwnd, 39) == (effects ? 1 : 0), "The resized card keeps its backdrop visual");
    if (!output.empty()) save_screen({bounds.left - px(40), bounds.top - px(40), bounds.right + px(40), bounds.bottom + px(40)},
        output + L"\\composition-card-resized.bmp");
    window.close();
    pump();
    DestroyWindow(back);
    UnregisterClassW(cls.lpszClassName, cls.hInstance);
}

void classic_unchanged() {
    Application app;
    WindowOptions options;
    options.title = L"XUI classic presentation";
    options.size = {320, 200};
    options.show_activated = false;
    auto owned = app.create_window(options);
    auto root = std::make_shared<Stack>(Axis::vertical);
    auto card = std::make_shared<Stack>(Axis::vertical);
    card->set_material(SurfaceMaterial::backdrop_acrylic);
    card->add(std::make_shared<Label>(L"Falls back to in-app acrylic"));
    root->add(card, 1);
    owned->set_content(root);
    app.show(*owned);
    pump();
    const HWND hwnd = owned->native_window();
    require((GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_NOREDIRECTIONBITMAP) == 0, "Classic windows keep a redirection surface");
    require(metric(hwnd, 40) == 0 && metric(hwnd, 39) == 0, "Classic windows create no composition tree");
    owned->close();
    pump();
}

// A topmost stripe window that sits behind the measured windows.
class StripeBackground {
public:
    StripeBackground(int x, int y, int width, int height) {
        WNDCLASSW cls{};
        cls.hInstance = GetModuleHandleW(nullptr);
        cls.lpfnWndProc = stripes;
        cls.lpszClassName = L"Xui.CompositionStripes2";
        RegisterClassW(&cls);
        hwnd_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, cls.lpszClassName, L"",
            WS_POPUP, x, y, width, height, nullptr, nullptr, cls.hInstance, nullptr);
        require(hwnd_ != nullptr, "Create the stripe background");
        ShowWindow(hwnd_, SW_SHOWNA);
        UpdateWindow(hwnd_);
    }
    ~StripeBackground() { DestroyWindow(hwnd_); }
    HWND hwnd() const { return hwnd_; }
private:
    HWND hwnd_{};
};
HWND named_child(HWND parent, std::wstring_view name) {
    struct Search { std::wstring_view name; HWND result{}; } search{name};
    EnumChildWindows(parent, [](HWND child, LPARAM data) -> BOOL {
        auto& search = *reinterpret_cast<Search*>(data);
        wchar_t text[128]{};
        GetWindowTextW(child, text, 128);
        if (std::wstring_view(text) != search.name) return TRUE;
        search.result = child;
        return FALSE;
    }, reinterpret_cast<LPARAM>(&search));
    require(search.result != nullptr, "Find a named native peer");
    return search.result;
}
void click(HWND peer) {
    RECT client{};
    GetClientRect(peer, &client);
    const auto position = MAKELPARAM(client.right / 2, client.bottom / 2);
    SendMessageW(peer, WM_MOUSEMOVE, 0, position);
    SendMessageW(peer, WM_LBUTTONDOWN, MK_LBUTTON, position);
    SendMessageW(peer, WM_LBUTTONUP, 0, position);
}
// Physical screen point for a client DIP position.
POINT screen_point(HWND hwnd, Point point) {
    const int dpi = static_cast<int>(GetDpiForWindow(hwnd));
    POINT result{MulDiv(static_cast<int>(point.x * 16), dpi, 96 * 16), MulDiv(static_cast<int>(point.y * 16), dpi, 96 * 16)};
    ClientToScreen(hwnd, &result);
    return result;
}
Point center(Rect bounds) { return {bounds.x + bounds.width / 2, bounds.y + bounds.height / 2}; }
Rect intersect(Rect a, Rect b) {
    const float left = std::max(a.x, b.x), top = std::max(a.y, b.y);
    const float right = std::min(a.x + a.width, b.x + b.width), bottom = std::min(a.y + a.height, b.y + b.height);
    return {left, top, std::max(0.0f, right - left), std::max(0.0f, bottom - top)};
}
// Mean absolute channel difference between two screen rectangles of the same size.
struct Capture { int width{}, height{}; std::vector<COLORREF> pixels; };
Capture capture(RECT area) {
    Capture result{area.right - area.left, area.bottom - area.top};
    result.pixels.resize(static_cast<std::size_t>(result.width) * result.height);
    const HDC source = GetDC(nullptr);
    const HDC memory = CreateCompatibleDC(source);
    const HBITMAP bitmap = CreateCompatibleBitmap(source, result.width, result.height);
    SelectObject(memory, bitmap);
    BitBlt(memory, 0, 0, result.width, result.height, source, area.left, area.top, SRCCOPY);
    BITMAPINFOHEADER info{sizeof(info), result.width, -result.height, 1, 32, BI_RGB};
    GetDIBits(memory, bitmap, 0, result.height, result.pixels.data(), reinterpret_cast<BITMAPINFO*>(&info), DIB_RGB_COLORS);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(nullptr, source);
    return result;
}
RECT screen_rect(HWND hwnd, Rect bounds) {
    const auto a = screen_point(hwnd, {bounds.x, bounds.y});
    const auto b = screen_point(hwnd, {bounds.x + bounds.width, bounds.y + bounds.height});
    return {a.x, a.y, b.x, b.y};
}
double difference(const Capture& a, const Capture& b) {
    if (a.pixels.size() != b.pixels.size() || a.pixels.empty()) return 0;
    double total{};
    for (std::size_t i = 0; i < a.pixels.size(); ++i) {
        const auto x = a.pixels[i], y = b.pixels[i];
        total += std::abs(int(x & 255) - int(y & 255)) + std::abs(int((x >> 8) & 255) - int((y >> 8) & 255)) +
            std::abs(int((x >> 16) & 255) - int((y >> 16) & 255));
    }
    return total / a.pixels.size();
}
// GetDIBits stores BGRA; return the pixel as a COLORREF.
COLORREF at(const Capture& image, int x, int y) {
    const auto value = image.pixels[static_cast<std::size_t>(y) * image.width + x];
    return RGB((value >> 16) & 255, (value >> 8) & 255, value & 255);
}
HWND process_window(const wchar_t* class_name) {
    struct Search { const wchar_t* name; HWND result{}; } search{class_name};
    EnumWindows([](HWND hwnd, LPARAM data) -> BOOL {
        auto& search = *reinterpret_cast<Search*>(data);
        DWORD process{};
        GetWindowThreadProcessId(hwnd, &process);
        wchar_t name[64]{};
        GetClassNameW(hwnd, name, 64);
        if (process != GetCurrentProcessId() || std::wstring_view(name) != search.name || !IsWindowVisible(hwnd)) return TRUE;
        search.result = hwnd;
        return FALSE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.result;
}
template<class F> bool wait_for(F&& done, int milliseconds = 3000) {
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
    while (!done()) {
        if (std::chrono::steady_clock::now() > end) return false;
        pump(20);
    }
    return true;
}
class Suggestions final : public SuggestionSource {
public:
    SuggestionResult suggest(const SuggestionRequest&, const std::function<bool()>&) override {
        return {{L"Documents", L"Downloads", L"Desktop"}, {}};
    }
};
constexpr std::uint32_t popup_color = 0x3060c0;
std::shared_ptr<Stack> solid_stack(Size size) {
    auto stack = std::make_shared<Stack>(Axis::vertical);
    stack->set_padding({16, 16, 16, 16});
    stack->set_spacing(8);
    PartStyleValues style;
    style.background = ThemeColor{popup_color};
    style.corner_radius = 8.0f;
    stack->set_control_style_values(StylePart::root, style);
    stack->set_fixed_size(size);
    return stack;
}
bool near_color(COLORREF a, std::uint32_t rgb, int tolerance = 24) {
    return channel_difference(a, RGB((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255)) <= tolerance;
}

// Root-drawn popups, menus, tooltips, dialogs, and separate native popup windows over a composition window.
void overlays(const std::wstring& output, bool composition_window = true) {
    const int dpi = static_cast<int>(GetDpiForSystem());
    const auto px = [&](float dips) { return static_cast<int>(std::lround(dips * dpi / 96.0f)); };
    StripeBackground stripes_behind(px(120), px(120), px(800), px(560));

    Application app;
    WindowOptions options;
    options.title = L"XUI composition overlays";
    options.size = {680, 440};
    options.visual_style = VisualStyle::winui;
    options.composition = composition_window;
    auto owned = app.create_window(options);
    auto& window = *owned;
    window.on_native_created([](HWND hwnd) {
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    });
    window.set_placement({px(160), px(160), px(700), px(500), false});

    auto root = std::make_shared<Stack>(Axis::vertical);
    root->set_padding({20, 12, 20, 20});
    root->set_spacing(12);
    int opened{};
    auto menu = std::make_shared<MenuBar>(L"Composition menu");
    menu->set_commands(std::make_shared<CommandSet>(std::vector<CommandRecord>{
        {1, 0, L"File", {}, true, {}, ButtonIcon::none, {}, {}, {}, CommandKind::submenu},
        {2, 1, L"Open", [&] { ++opened; }}, {3, 1, L"Save", [] {}}}));
    root->add(menu);
    auto panel = std::make_shared<Stack>(Axis::vertical);
    panel->set_material(SurfaceMaterial::backdrop_acrylic);
    panel->set_padding({16, 16, 16, 16});
    panel->set_spacing(10);
    PartStyleValues rounded;
    rounded.corner_radius = 16.0f;
    panel->set_control_style_values(StylePart::root, rounded);
    auto underlying = std::make_shared<TextInput>(L"Underlying editor");
    underlying->set_caption_visible(false);
    underlying->set_text(L"Underlying native text");
    panel->add(underlying);
    auto row = std::make_shared<Stack>(Axis::horizontal);
    row->set_spacing(8);
    auto combo = std::make_shared<ComboBox>(L"Composition choice");
    combo->set_items({{1, L"First"}, {2, L"Second"}, {3, L"Third"}}, 1);
    row->add(combo);
    auto anchor = std::make_shared<Button>(L"Popup anchor");
    row->add(anchor);
    auto help = std::make_shared<Button>(L"Help");
    help->set_help_text(L"Tooltip over composition content");
    help->set_tooltip_delay(100);
    row->add(help);
    int context_invoked{};
    auto context = std::make_shared<Button>(L"Context target");
    context->on_context_menu([&] { return std::vector<MenuItem>{{L"Run", [&] { ++context_invoked; }}, {L"Other", [] {}}}; });
    row->add(context);
    panel->add(row);
    auto suggested = std::make_shared<TextInput>(L"Suggested editor");
    suggested->set_caption_visible(false);
    suggested->set_suggestions(std::make_shared<Suggestions>());
    panel->add(suggested);
    root->add(panel, 1);
    window.set_content(root);
    app.show(window);
    pump(600);
    const HWND hwnd = window.native_window();
    require((metric(hwnd, 40) != 0) == composition_window, "The overlay window uses the requested presentation");
    const auto popups = [&] { return metric(hwnd, 24); };
    RECT client{};
    GetClientRect(hwnd, &client);
    const Rect whole{0, 0, client.right * 96.0f / dpi, client.bottom * 96.0f / dpi};
    const auto full = [&] { return capture(screen_rect(hwnd, whole)); };
    const auto save = [&](const wchar_t* name) {
        if (output.empty()) return;
        RECT bounds{};
        GetWindowRect(hwnd, &bounds);
        save_screen(bounds, output + L"\\" + name + L".bmp");
    };
    const auto settled = full();

    // 5. Tooltip. Like foundation_window_tests, drive the delay timer directly: TrackMouseEvent
    // reports a leave when the real cursor is elsewhere, which cancels a waiting tooltip.
    const HWND help_peer = named_child(hwnd, L"Help");
    window.focus(*help);
    SendMessageW(help_peer, WM_MOUSEMOVE, 0, MAKELPARAM(6, 6));
    SendMessageW(hwnd, WM_TIMER, 41, 0);
    UpdateWindow(hwnd);
    if (metric(hwnd, 25) == 0)
        std::cerr << "help visible=" << IsWindowVisible(help_peer) << " enabled=" << IsWindowEnabled(help_peer)
            << " focus=" << (GetFocus() == help_peer) << " popups=" << metric(hwnd, 24) << "\n";
    require(metric(hwnd, 25) != 0, "Show a tooltip in a composition window");
    RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
    DwmFlush();
    // The tooltip appears near its anchor; compare that area with the settled frame.
    const auto help_bounds = help->bounds();
    const Rect tooltip_strip{std::max(0.0f, help_bounds.x - 120), help_bounds.y - 40, 360, help_bounds.height + 80};
    const auto strip_area = screen_rect(hwnd, tooltip_strip);
    const auto with_tooltip = capture(strip_area);
    const bool still_shown = metric(hwnd, 25) != 0;
    save(L"overlay-tooltip");
    SendMessageW(help_peer, WM_MOUSELEAVE, 0, 0);
    pump(300);
    const double tooltip_change = difference(with_tooltip, capture(strip_area));
    if (tooltip_change <= 3) std::cerr << "tooltip still_shown=" << still_shown << " change=" << tooltip_change << "\n";
    require(metric(hwnd, 25) == 0 && tooltip_change > 3, "The tooltip paints and clears");

    // 1. A popup with a solid face and its own native editor, placed below the anchor so it covers
    // the suggested editor.
    auto popup_face = solid_stack({320, 120});
    auto popup_editor = std::make_shared<TextInput>(L"Popup editor");
    popup_editor->set_caption_visible(false);
    popup_editor->set_text(L"Popup text");
    popup_face->add(popup_editor);
    auto popup = std::make_shared<Popup>(popup_face, L"Composition popup");
    popup->set_placement(PopupPlacement::below);
    window.show_popup(popup, *anchor, popup_editor.get());
    pump(400);
    require(popup->is_open() && popups() == 1, "Open a popup in a composition window");
    save(L"overlay-popup");
    const auto overlap = intersect(popup_face->bounds(), suggested->bounds());
    require(overlap.width > 20 && overlap.height > 4, "The popup overlaps the underlying native editor");
    const auto overlap_pixel = capture(screen_rect(hwnd, {overlap.x + 4, overlap.y + overlap.height / 2, 1, 1}));
    if (composition_window)
        require(near_color(at(overlap_pixel, 0, 0), popup_color), "The popup covers the layered native editor beneath it");
    const auto face_pixel = capture(screen_rect(hwnd, {popup_face->bounds().x + 6, popup_face->bounds().y + popup_face->bounds().height - 8, 1, 1}));
    require(near_color(at(face_pixel, 0, 0), popup_color), "The popup face is opaque over the acrylic panel");
    // Find the popup editor's EDIT by position; keyboard focus depends on foreground activation.
    HWND popup_edit{};
    {
        const auto target = screen_point(hwnd, center(popup_editor->bounds()));
        struct Search { POINT point; HWND result{}; } search{target};
        EnumChildWindows(hwnd, [](HWND child, LPARAM data) -> BOOL {
            auto& search = *reinterpret_cast<Search*>(data);
            wchar_t name[16]{};
            GetClassNameW(child, name, 16);
            RECT bounds{};
            GetWindowRect(child, &bounds);
            if (_wcsicmp(name, L"Edit") != 0 || !IsWindowVisible(child) || !PtInRect(&bounds, search.point)) return TRUE;
            search.result = child;
            return FALSE;
        }, reinterpret_cast<LPARAM>(&search));
        popup_edit = search.result;
    }
    require(popup_edit && (!composition_window || (GetWindowLongPtrW(popup_edit, GWL_EXSTYLE) & WS_EX_LAYERED) != 0),
        "The popup editor is a layered child");
    SendMessageW(popup_edit, EM_SETSEL, 10, 10);
    for (const wchar_t ch : std::wstring(L" typed")) SendMessageW(popup_edit, WM_CHAR, ch, 0);
    pump();
    require(popup_editor->text() == L"Popup text typed", "The popup editor edits text");
    const auto field = popup_editor->bounds();
    const auto field_pixel = capture(screen_rect(hwnd, {field.x + field.width - 10, field.y + field.height / 2, 1, 1}));
    require(!near_color(at(field_pixel, 0, 0), popup_color, 12), "The popup editor paints above the popup face");
    window.dismiss_popup(*popup);
    pump(300);
    require(!popup->is_open() && popups() == 0, "Dismiss the popup");
    {
        const auto restored = full();
        const double changed = difference(restored, settled);
        if (changed >= 2 && !output.empty()) {
            RECT bounds{};
            GetWindowRect(hwnd, &bounds);
            save_screen(bounds, output + L"\\overlay-dismissed.bmp");
            std::cerr << "dismissal difference " << changed << "\n";
        }
        require(changed < 2, "Dismissal restores the window pixels");
    }

    // 2. Backdrop acrylic inside a popup paints the in-app material instead of a hole.
    auto acrylic_face = std::make_shared<Stack>(Axis::vertical);
    acrylic_face->set_material(SurfaceMaterial::backdrop_acrylic);
    acrylic_face->set_fixed_size({240, 100});
    acrylic_face->add(std::make_shared<Label>(L"Acrylic popup"));
    auto acrylic_popup = std::make_shared<Popup>(acrylic_face, L"Acrylic popup");
    acrylic_popup->set_placement(PopupPlacement::center);
    window.show_popup(acrylic_popup, *anchor);
    pump(300);
    require(acrylic_popup->is_open() && metric(hwnd, 39) == (composition_window ? 1 : 0), "A popup does not add a desktop backdrop hole");
    window.dismiss_popup(*acrylic_popup);
    pump(300);

    // 3. Combo box popup.
    click(named_child(hwnd, L"Composition choice"));
    pump(400);
    require(combo->popup()->is_open(), "Open the combo box popup");
    const auto combo_area = screen_rect(hwnd, combo->popup()->bounds());
    const auto combo_open = capture(combo_area);
    save(L"overlay-combo");
    window.dismiss_popup(*combo->popup());
    pump(300);
    require(difference(combo_open, capture(combo_area)) > 4, "The combo box popup paints over the panel");

    // 4. Menu bar submenu and command surface.
    click(named_child(hwnd, L"File"));
    pump(400);
    require(popups() == 1 && menu->expanded() == 1, "Open the menu bar submenu");
    save(L"overlay-menu-bar");
    SendMessageW(GetFocus(), WM_KEYDOWN, VK_ESCAPE, 0);
    pump(300);
    require(popups() == 0, "Escape closes the menu bar submenu");
    auto surface = std::make_shared<CommandSurface>(L"Composition commands");
    surface->set_commands(std::make_shared<CommandSet>(std::vector<CommandRecord>{{1, 0, L"Rename"}, {2, 0, L"Delete"}}));
    window.show_commands(surface, *anchor);
    pump(400);
    require(surface->popup()->is_open(), "Open the command surface");
    require(difference(full(), settled) > 1, "The command surface paints");
    save(L"overlay-commands");
    window.dismiss_popup(*surface->popup());
    pump(300);

    // 6. Content dialog with its dimming layer.
    auto body = std::make_shared<Stack>(Axis::vertical);
    body->add(std::make_shared<Label>(L"Dialog over composition content"));
    auto dialog = std::make_shared<ContentDialog>(L"Composition dialog", body);
    const auto corner_before = capture(screen_rect(hwnd, {4, whole.height - 6, 1, 1}));
    window.show_dialog(dialog, *anchor);
    pump(400);
    require(dialog->popup()->is_open(), "Open a content dialog");
    const auto corner_after = capture(screen_rect(hwnd, {4, whole.height - 6, 1, 1}));
    const auto luminance = [](COLORREF c) { return GetRValue(c) + GetGValue(c) + GetBValue(c); };
    require(luminance(at(corner_after, 0, 0)) < luminance(at(corner_before, 0, 0)), "The dialog dims the composition content");
    save(L"overlay-dialog");
    dialog->cancel();
    pump(300);
    require(popups() == 0, "Cancel the dialog");

    // 7. Suggestions use a separate top-level popup window.
    window.focus(*suggested);
    const HWND suggested_edit = GetFocus();
    SendMessageW(suggested_edit, WM_CHAR, L'D', 0);
    require(wait_for([&] { return process_window(L"Xui.Suggestions.1") != nullptr; }), "Show the suggestion list");
    pump(200);
    RECT list{};
    GetWindowRect(process_window(L"Xui.Suggestions.1"), &list);
    const auto list_pixels = capture(list);
    Capture flat_list = list_pixels;
    std::fill(flat_list.pixels.begin(), flat_list.pixels.end(), list_pixels.pixels.front());
    require(list.right - list.left > 20 && list.bottom - list.top > 20 && difference(list_pixels, flat_list) > 2,
        "The suggestion list is visible");
    SendMessageW(suggested_edit, WM_KEYDOWN, VK_ESCAPE, 0);
    pump(300);

    // 8. Native context menus are separate top-level windows with their own modal loop.
    const HWND context_peer = named_child(hwnd, L"Context target");
    const DWORD ui_thread = GetCurrentThreadId();
    std::atomic<bool> seen{};
    double menu_contrast{};
    std::jthread driver([&] {
        HWND menu_window{};
        for (int i = 0; i < 200 && !menu_window; ++i) {
            Sleep(20);
            const HWND candidate = FindWindowW(L"#32768", nullptr);
            if (candidate && IsWindowVisible(candidate) && GetWindowThreadProcessId(candidate, nullptr) == ui_thread)
                menu_window = candidate;
        }
        if (menu_window) {
            Sleep(150);
            RECT bounds{};
            GetWindowRect(menu_window, &bounds);
            const auto image = capture(bounds);
            Capture flat = image;
            std::fill(flat.pixels.begin(), flat.pixels.end(), image.pixels.front());
            menu_contrast = difference(image, flat);
            seen = true;
        }
        PostMessageW(hwnd, WM_KEYDOWN, VK_HOME, 0);
        PostMessageW(hwnd, WM_KEYDOWN, VK_RETURN, 0);
    });
    const auto before_menu = full();
    PostMessageW(context_peer, WM_CONTEXTMENU, reinterpret_cast<WPARAM>(context_peer), -1);
    pump(1500);
    driver.join();
    pump(300);
    require(seen && menu_contrast > 2, "The native context menu is visible above the composition window");
    require(context_invoked == 1, "The context menu command runs once");
    // Compare with the frame just before the menu opened: earlier steps leave focus on other controls.
    {
        const double changed = difference(full(), before_menu);
        if (changed >= 2) { std::cerr << "after-menu difference " << changed << "\n"; save(L"overlay-after-menu"); }
        require(changed < 2, "The window repaints after the native menu closes");
    }
    window.close();
    pump();
}

// Popups and dialogs extend the hit region of a transparent composition card.
void card_overlays() {
    const int dpi = static_cast<int>(GetDpiForSystem());
    const auto px = [&](float dips) { return static_cast<int>(std::lround(dips * dpi / 96.0f)); };
    StripeBackground stripes_behind(px(160), px(160), px(560), px(400));
    Application app;
    WindowOptions options;
    options.title = L"XUI composition card overlays";
    options.size = {480, 320};
    options.visual_style = VisualStyle::winui;
    options.transparent = true;
    options.composition = true;
    auto owned = app.create_window(options);
    auto& window = *owned;
    window.on_native_created([](HWND hwnd) {
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    });
    window.set_placement({px(200), px(200), px(480), px(320), false});
    auto root = std::make_shared<Stack>(Axis::vertical);
    auto card = std::make_shared<Stack>(Axis::vertical);
    card->set_material(SurfaceMaterial::backdrop_acrylic);
    card->set_fixed_size({200, 80});
    card->set_padding({12, 12, 12, 12});
    auto anchor = std::make_shared<Button>(L"Card anchor");
    card->add(anchor);
    root->add(card);
    window.set_content(root);
    app.show(window);
    pump(500);
    const HWND hwnd = window.native_window();
    const auto hit = [&](Point point) {
        const auto target = WindowFromPoint(screen_point(hwnd, point));
        return target == hwnd || IsChild(hwnd, target);
    };
    const Point margin{380, 260};
    require(!hit(margin), "The empty card margin passes input through");
    auto face = solid_stack({180, 90});
    auto popup = std::make_shared<Popup>(face, L"Card popup");
    popup->set_placement(PopupPlacement::center);
    window.show_popup(popup, *anchor);
    pump(400);
    require(popup->is_open() && hit(center(face->bounds())), "A popup over the transparent margin receives input");
    window.dismiss_popup(*popup);
    pump(300);
    require(!hit(center(face->bounds())), "Dismissal restores the click-through margin");
    auto body = std::make_shared<Stack>(Axis::vertical);
    body->add(std::make_shared<Label>(L"Card dialog"));
    auto dialog = std::make_shared<ContentDialog>(L"Card dialog", body);
    window.show_dialog(dialog, *anchor);
    pump(400);
    require(dialog->popup()->is_open() && hit(margin), "A modal dialog dim blocks input through the card margin");
    dialog->cancel();
    pump(300);
    require(!hit(margin), "Closing the dialog restores the click-through margin");
    window.close();
    pump();
}

// Lost targets recreate the swap chain, rebind the composition content, and keep the backdrop.
void device_loss(const std::wstring& output) {
    const int dpi = static_cast<int>(GetDpiForSystem());
    const auto px = [&](float dips) { return static_cast<int>(std::lround(dips * dpi / 96.0f)); };
    StripeBackground stripes_behind(px(160), px(160), px(600), px(400));
    Application app;
    WindowOptions options;
    options.title = L"XUI composition device loss";
    options.size = {500, 300};
    options.visual_style = VisualStyle::winui;
    options.composition = true;
    auto owned = app.create_window(options);
    auto& window = *owned;
    window.on_native_created([](HWND hwnd) {
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    });
    window.set_placement({px(200), px(200), px(520), px(340), false});
    auto root = std::make_shared<Stack>(Axis::vertical);
    root->set_padding({20, 20, 20, 20});
    auto panel = std::make_shared<Stack>(Axis::vertical);
    panel->set_material(SurfaceMaterial::backdrop_acrylic);
    panel->set_padding({16, 16, 16, 16});
    auto editor = std::make_shared<TextInput>(L"Device loss editor");
    editor->set_caption_visible(false);
    editor->set_text(L"Survives device loss");
    panel->add(editor);
    auto anchor = std::make_shared<Button>(L"Loss anchor");
    panel->add(anchor);
    root->add(panel, 1);
    window.set_content(root);
    app.show(window);
    pump(500);
    const HWND hwnd = window.native_window();
    RECT client{};
    GetClientRect(hwnd, &client);
    const Rect whole{0, 0, client.right * 96.0f / dpi, client.bottom * 96.0f / dpi};
    const auto settled = capture(screen_rect(hwnd, whole));
    const auto targets = Drawing::live_targets();
    const auto recovers = [&](const char* what, auto&& lose) {
        const auto generation = metric(hwnd, 41);
        lose();
        require(wait_for([&] { return metric(hwnd, 41) != generation; }), what);
        pump(300);
        require(Drawing::live_targets() == targets, "Recovery does not leak graphics targets");
        require(metric(hwnd, 39) == 1, "Recovery keeps exactly one backdrop visual");
        require(difference(capture(screen_rect(hwnd, whole)), settled) < 2, "Recovery restores the composed pixels");
    };
    const auto lose_frame = [&](HRESULT result) {
        return [&, result] {
            DrawingTestAccess::fail_next_frame(result);
            InvalidateRect(hwnd, nullptr, FALSE);
            UpdateWindow(hwnd);
        };
    };
    recovers("Recover from D2DERR_RECREATE_TARGET", lose_frame(D2DERR_RECREATE_TARGET));
    recovers("Recover from DXGI_ERROR_DEVICE_REMOVED", lose_frame(DXGI_ERROR_DEVICE_REMOVED));
    recovers("Recover from a display change", [&] { SendMessageW(hwnd, WM_DISPLAYCHANGE, 32, MAKELPARAM(1920, 1080)); });
    // Losing the device while a popup is open repaints the popup after recovery.
    auto face = solid_stack({200, 80});
    auto popup = std::make_shared<Popup>(face, L"Loss popup");
    popup->set_placement(PopupPlacement::center);
    window.show_popup(popup, *anchor);
    pump(400);
    const auto with_popup = capture(screen_rect(hwnd, whole));
    const auto generation = metric(hwnd, 41);
    lose_frame(DXGI_ERROR_DEVICE_REMOVED)();
    require(wait_for([&] { return metric(hwnd, 41) != generation; }), "Recover with an open popup");
    pump(300);
    require(popup->is_open() && difference(capture(screen_rect(hwnd, whole)), with_popup) < 2, "The popup is repainted after recovery");
    if (!output.empty()) {
        RECT bounds{};
        GetWindowRect(hwnd, &bounds);
        save_screen(bounds, output + L"\\device-loss-recovered.bmp");
    }
    window.dismiss_popup(*popup);
    pump(200);
    require(editor->text() == L"Survives device loss", "Native editor state survives device loss");
    window.close();
    pump();
}
}

int main(int argc, char** argv) {
    try {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        // Observed on Windows 11 build 26200: while another application is foreground,
        // SetWindowPos(HWND_TOPMOST) can be ignored unless this process already owns a window
        // created topmost. Screen pixel checks need the owned windows above other applications.
        const HWND topmost = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"STATIC", L"",
            WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        std::wstring output;
        std::string only;
        for (int i = 1; i < argc; ++i) {
            const std::string value = argv[i];
            if (value.starts_with("--")) only = value.substr(2);
            else {
                output.assign(value.begin(), value.end());
                CreateDirectoryW(output.c_str(), nullptr);
            }
        }
        const auto run = [&](const char* name) { return only.empty() || only == name; };
        if (run("contracts")) contracts();
        if (run("classic")) classic_unchanged();
        if (run("card")) transparent_card(output);
        if (run("overlays")) overlays(output);
        if (only == "overlays-classic") overlays(output, false);
        if (run("card-overlays")) card_overlays();
        if (run("device-loss")) device_loss(output);
        DestroyWindow(topmost);
        std::cout << "Composition presentation, backdrop acrylic, overlays, menus, device loss, layered native input, and click-through passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
