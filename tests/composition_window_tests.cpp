#include "xui/application.hpp"
#include "xui/styling.hpp"
#include "xui/control_styling.hpp"
#include <windows.h>
#include <cmath>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <string>
#include <thread>

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
}

int main(int argc, char** argv) {
    try {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        std::wstring output;
        if (argc > 1) {
            const std::string path = argv[1];
            output.assign(path.begin(), path.end());
            CreateDirectoryW(output.c_str(), nullptr);
        }
        contracts();
        classic_unchanged();
        transparent_card(output);
        std::cout << "Composition presentation, backdrop acrylic, layered native input, and click-through passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
