#include "xui/application.hpp"
#include "../src/drawing.hpp"
#include "../src/window_host.hpp"
#include "owned_window_capture.hpp"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <iostream>
#include <string>
#include <thread>

namespace xui {
struct DrawingTestAccess {
    static std::uint32_t pixel(const Drawing& drawing, unsigned x, unsigned y) {
        DIBSECTION section{};
        if (GetObjectW(drawing.layered_bitmap_, sizeof(section), &section) != sizeof(section))
            throw std::runtime_error("Read layered bitmap");
        return static_cast<const std::uint32_t*>(section.dsBm.bmBits)[y * drawing.layered_size_.width + x];
    }
};
}

namespace {
using namespace xui;
constexpr COLORREF parent_color = 0xd03030;
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void pump(int milliseconds = 120) {
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
    try { action(); } catch (const std::invalid_argument&) { return true; }
    return false;
}
HWND titled(const wchar_t* title) {
    struct Search { const wchar_t* title; HWND result{}; } search{title};
    EnumWindows([](HWND hwnd, LPARAM data) -> BOOL {
        auto& search = *reinterpret_cast<Search*>(data);
        DWORD process{};
        GetWindowThreadProcessId(hwnd, &process);
        wchar_t text[128]{};
        GetWindowTextW(hwnd, text, 128);
        if (process == GetCurrentProcessId() && std::wstring_view(text) == search.title) {
            search.result = hwnd;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    require(search.result != nullptr, "Find the owned test window");
    return search.result;
}
void save(const owned_window_capture::Pixels& pixels, const std::wstring& path) {
    BITMAPINFOHEADER info{sizeof(info), pixels.width, -pixels.height, 1, 32, BI_RGB};
    BITMAPFILEHEADER file{0x4d42, static_cast<DWORD>(sizeof(file) + sizeof(info) + pixels.data.size() * 4), 0, 0,
        sizeof(file) + sizeof(info)};
    FILE* stream{};
    require(_wfopen_s(&stream, path.c_str(), L"wb") == 0 && stream, "Open capture output");
    fwrite(&file, sizeof(file), 1, stream);
    fwrite(&info, sizeof(info), 1, stream);
    fwrite(pixels.data.data(), 4, pixels.data.size(), stream);
    fclose(stream);
}
DWORD backdrop_attribute(HWND hwnd) {
    DWORD value{};
    require(SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &value, sizeof(value))),
        "Read the applied system backdrop");
    return value;
}
bool red_dominant(COLORREF color) {
    const int red = (color >> 16) & 255, green = (color >> 8) & 255, blue = color & 255;
    return red > green + 16 && red > blue + 16;
}

// The software layered target exposes its pixels, so the material is checked without desktop capture.
void layered_acrylic() {
    WNDCLASSW cls{};
    cls.hInstance = GetModuleHandleW(nullptr);
    cls.lpfnWndProc = DefWindowProcW;
    cls.lpszClassName = L"Xui.AcrylicMaterialTest";
    require(RegisterClassW(&cls) != 0, "Register acrylic material window");
    const HWND hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_NOACTIVATE, cls.lpszClassName, L"XUI acrylic material",
        WS_POPUP, 40, 40, 160, 120, nullptr, nullptr, cls.hInstance, nullptr);
    require(hwnd != nullptr, "Create acrylic material window");
    {
        Drawing drawing;
        drawing.initialize(VisualStyle::winui);
        for (const auto mode : {ThemeMode::dark, ThemeMode::light}) {
            const auto palette = Palette::system(mode, VisualStyle::winui);
            const auto paint = [&](bool effects, Rect clip, COLORREF behind = parent_color) {
                require(drawing.begin(hwnd, 96, D2D1::ColorF(0, 0.0f), {}, true), "Begin acrylic frame");
                drawing.fill({0, 0, 160, 60}, D2D1::ColorF(behind));
                // Suspending and restoring enclosing clips must not let the material escape them.
                drawing.push_clip(clip);
                drawing.origin(10, 10);
                const bool blurred = drawing.acrylic({10, 10, 120, 80}, palette, 8, effects);
                drawing.origin(0, 0);
                drawing.fill({0, 100, 160, 20}, D2D1::ColorF(0x2040c0));
                drawing.pop_clip();
                require(drawing.end(), "Present acrylic frame");
                return blurred;
            };
            const auto before = Drawing::acrylic_draws();
            require(!paint(false, {0, 0, 160, 120}), "Disabled effects do not report a blurred material");
            require(Drawing::acrylic_draws() == before, "Disabled effects skip the blur graph");
            const auto fallback = Drawing::acrylic_fallback(palette);
            const auto expected = 0xff000000u | (static_cast<std::uint32_t>(std::lround(fallback.r * 255)) << 16) |
                (static_cast<std::uint32_t>(std::lround(fallback.g * 255)) << 8) | static_cast<std::uint32_t>(std::lround(fallback.b * 255));
            require(DrawingTestAccess::pixel(drawing, 80, 50) == expected, "Disabled effects paint the solid fallback");
            require(DrawingTestAccess::pixel(drawing, 5, 5) == (0xff000000u | parent_color), "The fallback stays inside its bounds");
            require(DrawingTestAccess::pixel(drawing, 21, 21) != expected, "The fallback keeps rounded corners");

            require(paint(true, {0, 0, 160, 120}), "The graphics target supplies the acrylic effect graph");
            require(Drawing::acrylic_draws() == before + 1, "Count one blurred material draw");
            const auto over_red = DrawingTestAccess::pixel(drawing, 80, 40);
            const auto over_clear = DrawingTestAccess::pixel(drawing, 80, 85);
            const auto reddish = [&](std::uint32_t color) {
                // The light luminosity layer is nearly white, so only the dark material keeps a strong hue.
                const int r = (color >> 16) & 255, g = (color >> 8) & 255, b = color & 255;
                return mode == ThemeMode::light ? r > 200 && g > 200 && b > 200 && r >= g && r >= b : r > g + 16 && r > b + 16;
            };
            require((over_red >> 24) == 255 && (over_red & 0xffffff) != parent_color && reddish(over_red),
                "Acrylic tints the red pixels behind it");
            require((over_clear >> 24) > 200 && over_red != over_clear,
                "Acrylic over transparent pixels uses the luminosity tint");
            require(DrawingTestAccess::pixel(drawing, 5, 5) == (0xff000000u | parent_color), "The material stays inside its bounds");
            require(DrawingTestAccess::pixel(drawing, 80, 110) == 0xff2040c0u, "Resumed clips accept later drawing");

            require(paint(true, {0, 0, 80, 120}), "Draw a clipped material");
            require(DrawingTestAccess::pixel(drawing, 120, 40) == (0xff000000u | parent_color),
                "Acrylic respects an enclosing clip");
            require(reddish(DrawingTestAccess::pixel(drawing, 60, 40)), "Clipped acrylic remains visible");
            require(DrawingTestAccess::pixel(drawing, 120, 110) == 0, "The restored clip still limits later drawing");
            // Light luminosity is near white, where a direct luminosity blend would invert the hue.
            require(paint(true, {0, 0, 160, 120}, 0x0078d4), "Draw acrylic over blue");
            const auto over_blue = DrawingTestAccess::pixel(drawing, 80, 40);
            const int red = (over_blue >> 16) & 255, blue = over_blue & 255;
            require(blue > red + (mode == ThemeMode::dark ? 16 : 2), "Acrylic preserves the hue behind it");
        }
    }
    DestroyWindow(hwnd);
    UnregisterClassW(cls.lpszClassName, cls.hInstance);
}
void run(const std::wstring& output) {
    owned_window_capture::Device device;
    layered_acrylic();

    WindowOptions invalid;
    invalid.transparent = true;
    invalid.backdrop = WindowBackdrop::mica;
    require(rejects([&] { Window rejected(invalid); }), "Layered transparency and a system backdrop are exclusive");
    invalid.transparent = false;
    invalid.backdrop = static_cast<WindowBackdrop>(9);
    require(rejects([&] { Window rejected(invalid); }), "Reject an unknown backdrop");
    {
        Window window;
        window.set_transparent(true);
        require(rejects([&] { window.set_backdrop(WindowBackdrop::acrylic); }), "A transparent window rejects a backdrop");
        window.set_transparent(false);
        window.set_backdrop(WindowBackdrop::mica_alt);
        require(window.backdrop() == WindowBackdrop::mica_alt && !window.backdrop_active(),
            "A requested backdrop is inactive before the window exists");
        require(rejects([&] { window.set_transparent(true); }), "A backdrop window rejects layered transparency");
        require(rejects([&] { window.set_backdrop(static_cast<WindowBackdrop>(4)); }), "Reject an unknown backdrop setter value");
        Stack stack(Axis::vertical);
        require(rejects([&] { stack.set_material(static_cast<SurfaceMaterial>(2)); }), "Reject an unknown surface material");
        stack.set_material(SurfaceMaterial::acrylic);
        require(stack.surface(), "An acrylic material paints a surface");
    }

    Application app;
    WindowOptions options;
    options.title = L"XUI backdrop regression";
    options.size = {560, 380};
    options.visual_style = VisualStyle::winui;
    options.show_activated = false;
    options.backdrop = WindowBackdrop::mica;
    auto owned = app.create_window(options);
    auto& window = *owned;
    auto root = std::make_shared<Stack>(Axis::vertical);
    root->set_padding({24, 24, 24, 24});
    root->set_spacing(16);
    root->add(std::make_shared<Label>(L"Backdrop region"));
    auto card = std::make_shared<Stack>(Axis::vertical);
    card->set_surface(true);
    card->set_padding({16, 16, 16, 16});
    card->set_spacing(8);
    card->add(std::make_shared<Label>(L"Opaque content"));
    auto editor = std::make_shared<TextInput>(L"Backdrop editor");
    editor->set_caption_visible(false);
    editor->set_text(L"Native text");
    editor->set_preferred_size({260, 32});
    card->add(editor);
    root->add(card);
    auto parent = std::make_shared<Stack>(Axis::vertical);
    parent->set_padding({16, 16, 16, 16});
    PartStyleValues red;
    red.background = ThemeColor{parent_color};
    parent->set_control_style_values(StylePart::root, red);
    auto acrylic = std::make_shared<Stack>(Axis::vertical);
    acrylic->set_material(SurfaceMaterial::acrylic);
    acrylic->set_padding({12, 12, 12, 12});
    acrylic->set_preferred_size({0, 64});
    acrylic->add(std::make_shared<Label>(L"In-app acrylic"));
    parent->add(acrylic);
    root->add(parent);
    window.set_content(root);
    app.show(window);
    pump(400);
    const auto hwnd = titled(options.title.c_str());

    const bool supported = platform::system_backdrop_supported() && platform::transparency_effects_enabled() &&
        !Palette::system(ThemeMode::dark).high_contrast;
    require(window.backdrop_active() == supported, "Present the material only where Windows supports it");
    if (supported) require(backdrop_attribute(hwnd) == DWMSBT_MAINWINDOW, "Mica selects the main-window backdrop");

    const auto image = [&] {
        InvalidateRect(hwnd, nullptr, FALSE);
        UpdateWindow(hwnd);
        pump(250);
        return owned_window_capture::capture(hwnd, device);
    };
    const auto pixel = [&](const owned_window_capture::Pixels& pixels, Point point) {
        const auto dpi = GetDpiForWindow(hwnd);
        const auto x = static_cast<int>(std::lround(point.x * dpi / 96));
        const auto y = static_cast<int>(std::lround(point.y * dpi / 96));
        require(x >= 0 && y >= 0 && x < pixels.width && y < pixels.height, "Read only owned client pixels");
        return static_cast<COLORREF>(pixels.data[static_cast<std::size_t>(y) * pixels.width + x] & 0xffffff);
    };
    const Point region{8, 8};
    const auto card_point = [&] { const auto b = card->bounds(); return Point{b.x + b.width - 12, b.y + b.height - 6}; };
    const auto edit_point = [&] { const auto b = editor->bounds(); return Point{b.x + b.width - 24, b.y + b.height - 24}; };
    const auto acrylic_point = [&] { const auto b = acrylic->bounds(); return Point{b.x + b.width - 12, b.y + b.height - 10}; };
    HWND edit_hwnd{};
    EnumChildWindows(hwnd, [](HWND child, LPARAM data) -> BOOL {
        wchar_t name[16]{};
        GetClassNameW(child, name, 16);
        if (std::wstring_view(name) != L"Edit") return TRUE;
        *reinterpret_cast<HWND*>(data) = child;
        return FALSE;
    }, reinterpret_cast<LPARAM>(&edit_hwnd));
    require(edit_hwnd != nullptr, "Find the native editor");

    const auto acrylic_before = Drawing::acrylic_draws();
    const auto material = image();
    require(Drawing::acrylic_draws() > acrylic_before, "Paint the in-app acrylic material");
    if (!output.empty()) save(material, output + L"\\backdrop-mica-dark.bmp");
    window.set_backdrop(WindowBackdrop::none);
    pump();
    require(!window.backdrop_active(), "Removing the request stops presenting the material");
    if (platform::system_backdrop_supported()) require(backdrop_attribute(hwnd) == DWMSBT_AUTO, "Restore the default backdrop");
    const auto solid = image();
    if (!output.empty()) save(solid, output + L"\\backdrop-none-dark.bmp");
    const auto background = Palette::system(ThemeMode::dark, VisualStyle::winui).background;
    const auto background_color = (static_cast<COLORREF>(std::lround(background.r * 255)) << 16) |
        (static_cast<COLORREF>(std::lround(background.g * 255)) << 8) | static_cast<COLORREF>(std::lround(background.b * 255));
    require(pixel(solid, region) == background_color, "Without a backdrop the client is the solid theme background");
    require(pixel(material, card_point()) == pixel(solid, card_point()), "Opaque surfaces keep their exact color over the material");
    require(pixel(material, edit_point()) == pixel(solid, edit_point()), "Native GDI pixels stay opaque over the material");
    require(red_dominant(pixel(material, acrylic_point())) && pixel(material, acrylic_point()) != parent_color,
        "In-app acrylic tints the pixels painted behind it");
    // Inactive windows can receive a solid material, so material pixels are a visual review item.

    window.set_backdrop(WindowBackdrop::acrylic);
    pump();
    require(window.backdrop_active() == supported, "Switch materials while open");
    if (supported) require(backdrop_attribute(hwnd) == DWMSBT_TRANSIENTWINDOW, "Acrylic selects the transient backdrop");
    SendMessageW(edit_hwnd, EM_SETSEL, 11, 11);
    for (const wchar_t ch : std::wstring(L" typed")) SendMessageW(edit_hwnd, WM_CHAR, ch, 0);
    const auto typed = image();
    if (!output.empty()) save(typed, output + L"\\backdrop-acrylic-typed.bmp");
    require(editor->text() == L"Native text typed", "Native text input still edits the field");
    require(pixel(typed, edit_point()) == pixel(solid, edit_point()), "Typing keeps native pixels opaque");

    window.set_backdrop(WindowBackdrop::mica_alt);
    window.set_theme(ThemeMode::light);
    pump();
    require(window.backdrop_active() == supported, "Theme changes retain the material");
    if (supported) require(backdrop_attribute(hwnd) == DWMSBT_TABBEDWINDOW, "Mica Alt selects the tabbed backdrop");
    const auto light = image();
    if (!output.empty()) save(light, output + L"\\backdrop-mica-alt-light.bmp");
    window.set_backdrop(WindowBackdrop::none);
    pump();
    const auto light_solid = image();
    require(pixel(light, card_point()) == pixel(light_solid, card_point()), "Light surfaces stay opaque over the material");
    require(pixel(light, edit_point()) == pixel(light_solid, edit_point()), "Light native pixels stay opaque over the material");
    window.close();
    pump();
}
}

int main(int argc, char** argv) {
    try {
        std::wstring output;
        if (argc > 1) {
            const std::string path = argv[1];
            output.assign(path.begin(), path.end());
            CreateDirectoryW(output.c_str(), nullptr);
        }
        run(output);
        std::cout << "System backdrop materials, opaque native pixels, and in-app acrylic passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
