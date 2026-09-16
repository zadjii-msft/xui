#include "xui/application.hpp"
#include "xui/documents.hpp"
#include "../src/drawing.hpp"
#include "owned_window_capture.hpp"
#include <commctrl.h>
#include <UIAutomation.h>
#include <atomic>
#include <thread>
#include <psapi.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace xui {
struct DrawingTestAccess {
    static void software_target(Drawing& drawing, HWND hwnd) {
        RECT client{};
        if (!GetClientRect(hwnd, &client)) throw std::runtime_error("Read software target size");
        const auto result = drawing.factory_->CreateHwndRenderTarget(
            D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
                96, 96, D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE),
            D2D1::HwndRenderTargetProperties(hwnd, D2D1::SizeU(client.right, client.bottom)),
            &drawing.target_);
        if (FAILED(result)) throw std::runtime_error("Create readable Direct2D software HWND target");
        ++Drawing::live_targets_;
        if (FAILED(drawing.target_->CreateSolidColorBrush(D2D1::ColorF(0), &drawing.brush_)))
            throw std::runtime_error("Create software target brush");
        drawing.target_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_ALIASED);
    }
    static Microsoft::WRL::ComPtr<ID2D1GdiInteropRenderTarget> interop(Drawing& drawing) {
        Microsoft::WRL::ComPtr<ID2D1GdiInteropRenderTarget> result;
        if (FAILED(drawing.target_.As(&result))) throw std::runtime_error("Query Direct2D pixel readback");
        return result;
    }
    static void observe(void (*callback)(HWND)) { Drawing::present_observer_ = callback; }
    static void lose_target() { Drawing::end_result_override_ = D2DERR_RECREATE_TARGET; }
    static void flush(Drawing& drawing) {
        if (FAILED(drawing.target_->Flush())) throw std::runtime_error("Flush software raster work");
    }
};
}

namespace {
using namespace xui;
constexpr wchar_t window_title[] = L"XUI authored Button style contracts";
constexpr uint32_t sentinel = 0x132537;
constexpr Rect face{12, 12, 120, 56};
bool trace_resources_enabled{};
bool benchmark_styled_first{};
constexpr int benchmark_frames = 600;

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void success(HRESULT result, const char* message) {
    if (FAILED(result)) throw std::runtime_error(std::string(message) + ": " + std::to_string(result));
}
uint32_t rgb(COLORREF value) {
    return (uint32_t(GetRValue(value)) << 16) | (uint32_t(GetGValue(value)) << 8) | GetBValue(value);
}
int color_distance(uint32_t first, uint32_t second) {
    return std::max({std::abs(int(first & 255) - int(second & 255)),
        std::abs(int((first >> 8) & 255) - int((second >> 8) & 255)),
        std::abs(int((first >> 16) & 255) - int((second >> 16) & 255))});
}
#ifndef XUI_STYLING_BASELINE
struct Pixels {
    int width{}, height{};
    std::vector<DWORD> data;
    uint32_t at(int x, int y) const {
        require(x >= 0 && y >= 0 && x < width && y < height, "Pixel sample lies inside captured target");
        return data[static_cast<std::size_t>(y) * width + x] & 0xffffff;
    }
    void expect(int x, int y, uint32_t expected, const char* message) const {
        const auto actual = at(x, y);
        if (color_distance(actual, expected) > 2) {
            std::cerr << message << " at " << x << ',' << y << ": actual=0x" << std::hex << actual
                << " expected=0x" << expected << std::dec << '\n';
            throw std::runtime_error(message);
        }
    }
    std::size_t matches(RECT rect, uint32_t expected) const {
        std::size_t count{};
        for (auto y = rect.top; y < rect.bottom; ++y)
            for (auto x = rect.left; x < rect.right; ++x)
                count += color_distance(at(x, y), expected) <= 3;
        return count;
    }
};

// Read the actual Direct2D backing surface before EndDraw, not the desktop or a simulated rasterizer.
Pixels readback(Drawing& drawing) {
    auto interop = DrawingTestAccess::interop(drawing);
    HDC source{};
    success(interop->GetDC(D2D1_DC_INITIALIZE_MODE_COPY, &source), "Read painted Direct2D pixels");
    struct Release {
        ID2D1GdiInteropRenderTarget* target;
        ~Release() { const RECT unchanged{}; target->ReleaseDC(&unchanged); }
    } release{interop.Get()};
    Pixels pixels{160, 96};
    pixels.data.resize(static_cast<std::size_t>(pixels.width) * pixels.height);
    HDC dc = CreateCompatibleDC(source);
    require(dc != nullptr, "Create pixel readback DC");
    struct DeleteDCGuard { HDC dc; ~DeleteDCGuard() { DeleteDC(dc); } } delete_dc{dc};
    BITMAPINFO info{};
    info.bmiHeader = {sizeof(BITMAPINFOHEADER), pixels.width, -pixels.height, 1, 32, BI_RGB};
    void* data{};
    const auto bitmap = CreateDIBSection(source, &info, DIB_RGB_COLORS, &data, nullptr, 0);
    require(bitmap && data, "Create pixel readback bitmap");
    struct DeleteBitmap { HBITMAP bitmap; ~DeleteBitmap() { DeleteObject(bitmap); } } delete_bitmap{bitmap};
    const auto previous = SelectObject(dc, bitmap);
    require(previous && previous != HGDI_ERROR, "Select pixel readback bitmap");
    const auto copied = BitBlt(dc, 0, 0, pixels.width, pixels.height, source, 0, 0, SRCCOPY);
    const auto flushed = GdiFlush();
    if (copied && flushed) memcpy(pixels.data.data(), data, pixels.data.size() * sizeof(DWORD));
    SelectObject(dc, previous);
    require(copied && flushed, "Copy actual Direct2D pixel data");
    return pixels;
}

struct SoftwareFixture {
    HWND hwnd{};
    Drawing drawing;
    SoftwareFixture() {
        hwnd = CreateWindowExW(WS_EX_NOACTIVATE, L"STATIC", L"XUI software style pixels",
            WS_POPUP, 0, 0, 160, 96, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        require(hwnd != nullptr, "Create hidden software drawing fixture");
        try {
            drawing.initialize();
            DrawingTestAccess::software_target(drawing, hwnd);
        } catch (...) {
            DestroyWindow(hwnd);
            throw;
        }
    }
    ~SoftwareFixture() { drawing.release(); DestroyWindow(hwnd); }
    Pixels render(const Palette& palette, const ButtonStyleValues* values, bool enabled = true,
        bool hovered = false, bool pressed = false, bool checked = false,
        ButtonAppearance appearance = ButtonAppearance::standard) {
        require(drawing.begin(hwnd, 96, D2D1::ColorF(sentinel)), "Begin software button frame");
        D2D1_COLOR_F ink{};
        if (values)
            ink = drawing.styled_button_face(face, palette, appearance, enabled, hovered, pressed, checked, *values);
        else if (palette.style == VisualStyle::winui)
            ink = drawing.button_face(face, palette, appearance, enabled, hovered, pressed, checked);
        else {
            drawing.rounded(face, pressed || checked ? palette.selection : hovered ? palette.hover : palette.surface);
            drawing.rounded(face, palette.border, 6, true);
            ink = !enabled ? palette.disabled : pressed || checked ? palette.selection_text : palette.text;
        }
        drawing.text(L"MMMM", {36, 22, 80, 32}, ink);
        auto pixels = readback(drawing);
        require(drawing.end(), "Complete actual software button paint");
        return pixels;
    }
};

ButtonStyleValues authored_values() {
    ButtonStyleValues values;
    values.background = ThemeColor{0xe13457, 0x2496c3};
    values.foreground = ThemeColor{0x17420b, 0xffed34};
    values.border_brush = ThemeColor{0x2468ed, 0xda42ef};
    values.border_thickness = Insets{3, 0, 0, 0};
    values.corner_radius = 0.0f;
    return values;
}
Palette regular_palette(ThemeMode mode, VisualStyle style) {
    auto palette = Palette::system(mode, style);
    // Do not modify the user's system settings. Explicit regular palettes isolate non-HC paint tests.
    if (palette.high_contrast) {
        const auto colors = theme_colors(mode, style);
        palette.background = D2D1::ColorF(colors.background);
        palette.surface = D2D1::ColorF(colors.surface);
        palette.border = D2D1::ColorF(colors.border);
        palette.hover = D2D1::ColorF(colors.hover);
        palette.text = D2D1::ColorF(colors.text);
        palette.selection = D2D1::ColorF(colors.selection);
        palette.selection_text = D2D1::ColorF(colors.selection_text);
        palette.high_contrast = false;
    }
    return palette;
}
void toggle_pixel_contracts() {
    SoftwareFixture fixture;
    Toggle toggle(L"MMMM");
    PartStyleValues root; root.background = ThemeColor{0x213141, 0x415161};
    root.foreground = ThemeColor{0xf1e2d3, 0xabcdef}; root.corner_radius = 0.0f;
    PartStyleValues indicator; indicator.size = 20.0f; indicator.background = ThemeColor{0x236745, 0x896745};
    indicator.border_brush = ThemeColor{0xfc1234}; indicator.border_thickness = Insets{2, 2, 2, 2}; indicator.corner_radius = 0.0f;
    PartStyleValues checked; checked.background = ThemeColor{0x765432};
    PartStyleValues mark; mark.foreground = ThemeColor{0x12fe34};
    auto style = ControlStyle::create(StyleTarget::toggle,
        {{StylePart::root, root}, {StylePart::indicator, indicator}, {StylePart::mark, mark}},
        {{StylePart::indicator, style_states::checked, checked}});
    toggle.set_style(style);
    const auto render = [&](Palette palette, bool focused = false) {
        require(fixture.drawing.begin(fixture.hwnd, 96, D2D1::ColorF(sentinel)), "Begin styled Toggle pixel frame");
        Size size{};
        auto label = fixture.drawing.layout(toggle.name(), toggle.text_style(), size);
        fixture.drawing.styled_toggle(toggle, face, palette, true, label.Get(), focused);
        auto pixels = readback(fixture.drawing);
        require(fixture.drawing.end(), "End styled Toggle pixel frame");
        return pixels;
    };
    for (const auto visual : {VisualStyle::classic, VisualStyle::winui}) {
        toggle.set_visual_style(visual);
        auto palette = regular_palette(ThemeMode::light, visual);
        auto pixels = render(palette);
        const auto b = toggle.indicator_bounds(face);
        pixels.expect(120, 60, root.background->light, "Toggle root background reaches pixels");
        pixels.expect(static_cast<int>(b.x + 5), static_cast<int>(b.y + 5), indicator.background->light, "Indicator background reaches pixels");
        pixels.expect(static_cast<int>(b.x), static_cast<int>(b.y + 10), indicator.border_brush->light, "Indicator border reaches pixels");
        require(pixels.matches({static_cast<LONG>(b.x + b.width + toggle.layout_metrics().gap), 12, 132, 68},
            root.foreground->light) > 10, "Inherited root foreground reaches the label");
        toggle.set_checked(true);
        pixels = render(palette);
        pixels.expect(static_cast<int>(b.x + 5), static_cast<int>(b.y + 5), checked.background->light, "Checked rule reaches indicator pixels");
        require(pixels.matches({static_cast<LONG>(b.x), static_cast<LONG>(b.y),
            static_cast<LONG>(b.x + b.width), static_cast<LONG>(b.y + b.height)}, mark.foreground->light) > 5,
            "Authored mark foreground reaches check pixels");
        toggle.set_checked(false);
        palette = regular_palette(ThemeMode::dark, visual);
        pixels = render(palette);
        pixels.expect(120, 60, root.background->dark, "Dark root color remains distinct");
        palette.high_contrast = true;
        const auto high_contrast = render(palette, true);
        require(high_contrast.matches({12, 12, 132, 68}, root.background->dark) == 0 &&
            high_contrast.matches({12, 12, 132, 68}, root.foreground->dark) == 0,
            "High contrast suppresses authored colors and preserves a separate focus outline");
    }
    std::cout << "PASS Toggle software pixels: parts, inheritance, checked, themes, high contrast\n" << std::flush;
}
void pixel_contracts() {
    const auto before = Drawing::live_targets();
    {
        SoftwareFixture fixture;
        require(Drawing::live_targets() == before + 1, "Software fixture owns one real target");
        const auto values = authored_values();
        for (const auto dpi : {96.0f, 120.0f, 144.0f}) {
            for (const auto widths : {Insets{3, 0, 0, 0}, Insets{3.5f, 0, 0, 0}, Insets{2, 3, 4, 5}}) {
                auto square = values;
                square.border_thickness = widths;
                const auto palette = regular_palette(ThemeMode::light, VisualStyle::classic);
                const auto draw = [&](bool reference) {
                    require(fixture.drawing.begin(fixture.hwnd, dpi, D2D1::ColorF(sentinel)), "Begin square-edge comparison");
                    if (reference) {
                        fixture.drawing.rounded(face, D2D1::ColorF(square.background->light), 0);
                        const Rect edges[]{{12, 12, widths.left, 56},
                            {12 + widths.left, 12, 120 - widths.left - widths.right, widths.top},
                            {132 - widths.right, 12, widths.right, 56},
                            {12 + widths.left, 68 - widths.bottom, 120 - widths.left - widths.right, widths.bottom}};
                        for (const auto& edge : edges) if (edge.width > 0 && edge.height > 0) {
                            fixture.drawing.push_clip(edge);
                            fixture.drawing.rounded(face, D2D1::ColorF(square.border_brush->light), 0);
                            fixture.drawing.pop_clip();
                        }
                    } else fixture.drawing.styled_button_face(face, palette, ButtonAppearance::standard,
                        true, false, false, false, square);
                    auto pixels = readback(fixture.drawing);
                    require(fixture.drawing.end(), "Square-edge comparison completes");
                    return pixels;
                };
                const auto reference = draw(true);
                const auto optimized = draw(false);
                require(reference.data == optimized.data,
                    "Square-edge fast path preserves every pixel, including fractional edges and scaled DPI");
            }
        }
        for (const auto style : {VisualStyle::classic, VisualStyle::winui}) {
            for (const auto mode : {ThemeMode::light, ThemeMode::dark}) {
                const auto palette = regular_palette(mode, style);
                const auto pixels = fixture.render(palette, &values);
                const auto fill = values.background->resolve(mode), edge = values.border_brush->resolve(mode);
                for (int x = 12; x < 15; ++x)
                    pixels.expect(x, 40, edge, "The left border covers exactly three DIPs at 96 DPI");
                pixels.expect(15, 40, fill, "The fourth DIP is background, not border");
                for (const auto point : {POINT{20, 12}, POINT{131, 12}, POINT{131, 67}, POINT{20, 67}, POINT{131, 40}})
                    pixels.expect(point.x, point.y, fill, "Square corners and absent top/right/bottom borders");
                pixels.expect(12, 12, edge, "A zero corner radius keeps the border corner square");
                pixels.expect(11, 40, sentinel, "Border never paints outside its authored bounds");
                require(pixels.matches({36, 22, 116, 54}, values.foreground->resolve(mode)) > 12,
                    "Actual DirectWrite glyph pixels use the authored foreground");

                const auto baseline = fixture.render(palette, nullptr);
                ButtonStyleValues padding;
                padding.padding = Insets{17, 8, 23, 9};
                const auto padded = fixture.render(palette, &padding);
                require(baseline.data == padded.data, "Padding-only styles retain the exact default face and glyph paint");
                ButtonStyleValues foreground;
                foreground.foreground = values.foreground;
                const auto recolored = fixture.render(palette, &foreground);
                for (int y = 10; y < 70; ++y)
                    for (int x = 10; x < 134; ++x)
                        if (x < 34 || x >= 118 || y < 20 || y >= 56)
                            require(baseline.at(x, y) == recolored.at(x, y),
                                "Foreground-only styles retain default rounded/elevated face pixels");
                require(recolored.matches({36, 22, 116, 54}, values.foreground->resolve(mode)) > 12,
                    "Foreground-only styles recolor actual glyph pixels");
            }

            const auto high_contrast = Palette::system(ThemeMode::high_contrast, style);
            for (const bool enabled : {true, false}) {
                const auto pixels = fixture.render(high_contrast, &values, enabled);
                pixels.expect(22, 40, rgb(GetSysColor(COLOR_WINDOW)), "High contrast ignores authored background");
                require(pixels.matches({36, 22, 116, 54}, rgb(GetSysColor(enabled ? COLOR_WINDOWTEXT : COLOR_GRAYTEXT))) > 12,
                    "High contrast uses native enabled/disabled system text colors");
                require(pixels.matches({10, 10, 134, 70}, rgb(GetSysColor(enabled ? COLOR_WINDOWTEXT : COLOR_GRAYTEXT))) > 30,
                    "High contrast retains a visible system-colored face outline");
            }
            const auto selected = fixture.render(high_contrast, &values, true, false, true, true);
            selected.expect(22, 40, rgb(GetSysColor(COLOR_HIGHLIGHT)), "High contrast selected fill uses system highlight");
            require(selected.matches({36, 22, 116, 54}, rgb(GetSysColor(COLOR_HIGHLIGHTTEXT))) > 12,
                "High contrast selected glyphs use system highlight text");
        }

        auto base = authored_values();
        constexpr std::array<uint32_t, 5> colors{0xc52525, 0x27b837, 0x314de2, 0x8f2bba, 0xb28317};
        std::vector<ButtonStyleRule> rules;
        for (std::size_t i = 0; i < colors.size(); ++i) {
            ButtonStyleValues value;
            value.background = ThemeColor{colors[i]};
            rules.push_back({static_cast<ButtonStyleState>(i), value});
        }
        Button button(L"State pixels");
        button.set_behavior(ButtonBehavior::toggle);
        button.set_style(ButtonStyle::create(base, rules));
        const auto palette = regular_palette(ThemeMode::light, VisualStyle::classic);
        const auto sample = [&](uint32_t expected) {
            fixture.render(palette, button.effective_style_values(), button.enabled(), button.hovered(),
                button.pressed(), button.checked()).expect(22, 40, expected, "Resolved Button state reaches actual face pixels");
        };
        sample(base.background->light);
        button.set_focused(true); sample(colors[0]);
        button.set_checked(true); sample(colors[1]);
        button.pointer_move(true); sample(colors[2]);
        require(button.pointer_down(), "Begin model pointer press without requiring OS keyboard focus");
        sample(colors[3]);
        button.set_enabled(false); sample(colors[4]);
        ButtonStyleValues local;
        local.background = ThemeColor{0x1dcae1};
        button.set_style_values(local); sample(0x1dcae1);
        button.set_style_values({}); sample(colors[4]);
        button.set_enabled(true); button.cancel(); button.pointer_move(false);
        button.set_checked(false); button.set_focused(false); sample(base.background->light);
    }
    require(Drawing::live_targets() == before, "Software fixture releases its target");
    std::cout << "PASS actual Direct2D pixels: geometry, default face, theme colors, high contrast, state precedence\n";
}
#endif

std::wstring native_text(HWND hwnd) {
    std::wstring text(static_cast<std::size_t>(GetWindowTextLengthW(hwnd)) + 1, L'\0');
    text.resize(GetWindowTextW(hwnd, text.data(), static_cast<int>(text.size())));
    return text;
}
std::vector<HWND> children(HWND host) {
    std::vector<HWND> result;
    EnumChildWindows(host, [](HWND hwnd, LPARAM data) -> BOOL {
        reinterpret_cast<std::vector<HWND>*>(data)->push_back(hwnd);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&result));
    return result;
}
HWND find_host() {
    HWND result{};
    EnumWindows([](HWND hwnd, LPARAM data) -> BOOL {
        DWORD process{};
        GetWindowThreadProcessId(hwnd, &process);
        if (process == GetCurrentProcessId() && native_text(hwnd) == window_title && IsWindowVisible(hwnd)) {
            *reinterpret_cast<HWND*>(data) = hwnd;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&result));
    return result;
}
void trace_module(LPARAM address) {
    HMODULE module{};
    if (address && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(address), &module)) {
        wchar_t path[MAX_PATH]{};
        GetModuleFileNameW(module, path, MAX_PATH);
        std::wcerr << L" module=" << path << L" offset=0x" << std::hex
            << (address - reinterpret_cast<LPARAM>(module)) << std::dec << L'\n';
    }
}
void pump_for(unsigned milliseconds) {
    const auto deadline = GetTickCount64() + milliseconds;
    do {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            require(message.message != WM_QUIT, "The window must remain alive during fixture message delivery");
            const auto before = trace_resources_enabled ? GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS) : 0;
            TranslateMessage(&message);
            DispatchMessageW(&message);
            const auto after = trace_resources_enabled ? GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS) : 0;
            if (before != after) {
                std::cerr << "TRACE dispatch hwnd=" << message.hwnd << " message=" << std::hex << message.message
                    << " wp=" << message.wParam << " lp=" << message.lParam << std::dec
                    << " USER " << before << " -> " << after << '\n';
                // USER totals include timers. Identify native callbacks without guessing from the count.
                if (message.message == WM_TIMER)
                    trace_module(message.lParam ? message.lParam : GetWindowLongPtrW(message.hwnd, GWLP_WNDPROC));
            }
        }
        Sleep(1);
    } while (GetTickCount64() < deadline);
}
void flush(HWND host) {
    SendMessageW(host, WM_APP + 12, 0, 0);
    require(RedrawWindow(host, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW) != FALSE,
        "Paint pending native style presentation");
}
std::size_t presents{}, paints{};
void presented(HWND) { ++presents; }
LRESULT CALLBACK count_paints(HWND hwnd, UINT message, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    if (message == WM_PAINT) ++paints;
    const auto before = trace_resources_enabled ? GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS) : 0;
    const auto result = DefSubclassProc(hwnd, message, wp, lp);
    const auto after = trace_resources_enabled ? GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS) : 0;
    if (before != after)
        std::cerr << "TRACE native hwnd=" << hwnd << " message=" << std::hex << message
            << " wp=" << wp << " lp=" << lp << std::dec << " USER " << before << " -> " << after << '\n';
    return result;
}
struct ObservePaints {
    std::vector<HWND> windows;
    explicit ObservePaints(HWND host) : windows(children(host)) {
        windows.push_back(host);
        DrawingTestAccess::observe(presented);
        for (const auto hwnd : windows)
            require(SetWindowSubclass(hwnd, count_paints, 71, 0) != FALSE, "Observe native paint messages");
    }
    ~ObservePaints() {
        DrawingTestAccess::observe(nullptr);
        for (const auto hwnd : windows) if (IsWindow(hwnd)) RemoveWindowSubclass(hwnd, count_paints, 71);
    }
};
struct Resources {
    SIZE_T private_bytes{};
    DWORD handles{}, gdi{}, user{};
    static Resources read() {
        PROCESS_MEMORY_COUNTERS_EX memory{};
        memory.cb = sizeof(memory);
        require(GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),
            sizeof(memory)) != FALSE, "Read process private bytes");
        Resources result{memory.PrivateUsage};
        require(GetProcessHandleCount(GetCurrentProcess(), &result.handles) != FALSE, "Read process handle count");
        result.gdi = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
        result.user = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
        return result;
    }
    void retained_from(const Resources& baseline) const {
        if (trace_resources_enabled || user != baseline.user || gdi > baseline.gdi + 2 || handles > baseline.handles + 8 ||
            private_bytes > baseline.private_bytes + 8 * 1024 * 1024)
            std::cerr << "Resource retention: USER " << baseline.user << " -> " << user
                << ", GDI " << baseline.gdi << " -> " << gdi << ", handles " << baseline.handles << " -> " << handles
                << ", private bytes " << baseline.private_bytes << " -> " << private_bytes << '\n';
        require(user == baseline.user, "Style/theme cycles retain no additional USER objects");
        require(gdi <= baseline.gdi + 2, "Style/theme cycles retain no growing GDI object set");
        require(handles <= baseline.handles + 8, "Style/theme cycles retain no growing kernel handle set");
        require(private_bytes <= baseline.private_bytes + 8 * 1024 * 1024,
            "Warmed style/theme cycles retain no more than 8 MiB of private allocator/cache growth");
    }
};
void trace_resources(const char* phase, int index) {
    if (!trace_resources_enabled) return;
    const auto resources = Resources::read();
    std::cerr << "TRACE " << phase << ' ' << index << " USER=" << resources.user << '\n';
    const auto print = [](HWND hwnd, LPARAM) -> BOOL {
        DWORD process{};
        const auto thread = GetWindowThreadProcessId(hwnd, &process);
        if (process != GetCurrentProcessId()) return TRUE;
        wchar_t cls[256]{};
        GetClassNameW(hwnd, cls, 256);
        std::wcerr << L" HWND=" << hwnd << L" class=" << cls << L" thread=" << thread
            << L" parent=" << GetAncestor(hwnd, GA_PARENT) << L'\n';
        return TRUE;
    };
    EnumWindows(print, 0);
    EnumWindows([](HWND hwnd, LPARAM data) -> BOOL {
        DWORD process{};
        GetWindowThreadProcessId(hwnd, &process);
        if (process == GetCurrentProcessId()) EnumChildWindows(hwnd, reinterpret_cast<WNDENUMPROC>(data), 0);
        return TRUE;
    }, reinterpret_cast<LPARAM>(static_cast<WNDENUMPROC>(print)));
    for (auto hwnd = FindWindowExW(HWND_MESSAGE, nullptr, nullptr, nullptr); hwnd;
        hwnd = FindWindowExW(HWND_MESSAGE, hwnd, nullptr, nullptr))
        print(hwnd, 0);
}
unsigned long long ticks(FILETIME value) {
    return (static_cast<unsigned long long>(value.dwHighDateTime) << 32) | value.dwLowDateTime;
}
unsigned long long cpu_ticks() {
    FILETIME created{}, exited{}, kernel{}, user{};
    require(GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user) != FALSE, "Read UI thread CPU time");
    return ticks(kernel) + ticks(user);
}
unsigned long long cpu_cycles() {
    ULONG64 value{};
    require(QueryThreadCycleTime(GetCurrentThread(), &value) != FALSE, "Read UI thread cycle count");
    return value;
}
#ifndef XUI_STYLING_BASELINE
void lower_level_benchmarks() {
    const auto palette = regular_palette(ThemeMode::light, VisualStyle::classic);
    const auto colors = theme_colors(ThemeMode::light, VisualStyle::classic);
    auto equivalent = ButtonStyleValues{};
    equivalent.background = ThemeColor{colors.surface};
    equivalent.foreground = ThemeColor{colors.text};
    equivalent.border_brush = ThemeColor{colors.border};
    equivalent.corner_radius = 6.0f;
    ButtonStyleValues hovered;
    hovered.background = ThemeColor{colors.hover};
    const std::array<std::shared_ptr<const ButtonStyle>, 3> styles{
        nullptr, ButtonStyle::create(equivalent, {{ButtonStyleState::hovered, hovered}}),
        ButtonStyle::create(authored_values(), {{ButtonStyleState::hovered, hovered}})};
    SoftwareFixture fixture;
    Size text_size{};
    auto text = fixture.drawing.layout(L"Style pixels MMMM", TextStyle::body, text_size);
    const std::array<const char*, 3> names{"default", "equivalent", "authored"};
    std::uint64_t checksum{};
    for (unsigned repetition = 0; repetition < 6; ++repetition) {
        for (unsigned order = 0; order < 3; ++order) {
            const unsigned index = repetition % 2 ? 2 - order : order;
            Button button(L"Style pixels MMMM");
            button.set_style(styles[index]);
            for (unsigned i = 0; i < 10000; ++i) {
                button.pointer_move(i % 2 != 0);
                (void)button.effective_style_values();
            }
            constexpr unsigned state_iterations = 1000000;
            auto cycles = cpu_cycles();
            auto cpu = cpu_ticks();
            auto start = std::chrono::steady_clock::now();
            for (unsigned i = 0; i < state_iterations; ++i) {
                button.pointer_move(i % 2 != 0);
                const auto* resolved = button.effective_style_values();
                checksum += resolved && resolved->background ? resolved->background->light : unsigned(button.hovered());
            }
            auto wall = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
            auto used_cpu = (cpu_ticks() - cpu) / 10.0;
            auto used_cycles = cpu_cycles() - cycles;
            std::cout << "LOWER state repetition=" << repetition << " order=" << order << " variant=" << names[index]
                << " iterations=" << state_iterations << " wall_ns=" << wall * 1000 / state_iterations
                << " cpu_ns=" << used_cpu * 1000 / state_iterations << " cycles=" << double(used_cycles) / state_iterations << '\n';

            const auto draw = [&](unsigned i) {
                fixture.drawing.fill({0, 0, 160, 96}, D2D1::ColorF(sentinel));
                const bool hot = i % 2 != 0;
                auto ink = palette.text;
                if (styles[index]) {
                    ink = fixture.drawing.styled_button_face(face, palette, ButtonAppearance::standard,
                        true, hot, false, false, styles[index]->values(hot ? 4 : 0));
                } else {
                    fixture.drawing.rounded(face, hot ? palette.hover : palette.surface);
                    fixture.drawing.rounded(face, palette.border, 6, true);
                }
                fixture.drawing.text_layout(text.Get(), {24, 24, 108, 32}, ink);
            };
            require(fixture.drawing.begin(fixture.hwnd, 96, D2D1::ColorF(sentinel)), "Begin isolated software raster workload");
            for (unsigned i = 0; i < 100; ++i) draw(i);
            DrawingTestAccess::flush(fixture.drawing);
            constexpr unsigned raster_iterations = 4000;
            cycles = cpu_cycles(); cpu = cpu_ticks(); start = std::chrono::steady_clock::now();
            for (unsigned i = 0; i < raster_iterations; ++i) {
                draw(i);
                if (i % 32 == 31) DrawingTestAccess::flush(fixture.drawing);
            }
            DrawingTestAccess::flush(fixture.drawing);
            wall = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
            used_cpu = (cpu_ticks() - cpu) / 10.0; used_cycles = cpu_cycles() - cycles;
            require(fixture.drawing.end(), "End isolated software raster workload");
            std::cout << "LOWER raster repetition=" << repetition << " order=" << order << " variant=" << names[index]
                << " iterations=" << raster_iterations << " surface=160x96 face=120x56 text=identical"
                << " wall_us=" << wall / raster_iterations << " cpu_us=" << used_cpu / raster_iterations
                << " cycles=" << double(used_cycles) / raster_iterations << '\n';
        }
    }
    Size fitting_size{};
    const auto fitting_text = fixture.drawing.layout(L"MMMM", TextStyle::body, fitting_size);
    std::optional<Pixels> fitting_pixels;
    for (unsigned repetition = 0; repetition < 6; ++repetition) {
        for (const bool unclipped : {repetition % 2 != 0, repetition % 2 == 0}) {
            const auto draw = [&] {
                fixture.drawing.fill({0, 0, 160, 96}, D2D1::ColorF(sentinel));
                constexpr Rect bounds{24, 24, 108, 32}, clip{12, 12, 140, 72};
                if (unclipped) fixture.drawing.text_layout(fitting_text.Get(), bounds, palette.text);
                else {
                    fixture.drawing.push_clip(clip);
                    fixture.drawing.text_layout(fitting_text.Get(), bounds, palette.text);
                    fixture.drawing.pop_clip();
                }
            };
            require(fixture.drawing.begin(fixture.hwnd, 96, D2D1::ColorF(sentinel)), "Begin isolated content-clip workload");
            for (unsigned i = 0; i < 100; ++i) draw();
            const auto pixels = readback(fixture.drawing);
            if (fitting_pixels) require(pixels.data == fitting_pixels->data, "Text-clip profiling variants paint identical glyph pixels");
            else fitting_pixels = pixels;
            DrawingTestAccess::flush(fixture.drawing);
            constexpr unsigned iterations = 4000;
            const auto cycles = cpu_cycles(), cpu = cpu_ticks();
            const auto start = std::chrono::steady_clock::now();
            for (unsigned i = 0; i < iterations; ++i) {
                draw();
                if (i % 32 == 31) DrawingTestAccess::flush(fixture.drawing);
            }
            DrawingTestAccess::flush(fixture.drawing);
            const auto wall = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count();
            const auto used_cpu = (cpu_ticks() - cpu) / 10.0, used_cycles = double(cpu_cycles() - cycles);
            require(fixture.drawing.end(), "End isolated content-clip workload");
            std::cout << "LOWER textclip repetition=" << repetition << " variant=" << (unclipped ? "unclipped" : "reference")
                << " iterations=" << iterations << " surface=160x96 text=identical"
                << " wall_us=" << wall / iterations << " cpu_us=" << used_cpu / iterations
                << " cycles=" << used_cycles / iterations << '\n';
        }
    }
    std::cout << "LOWER checksum=" << checksum << '\n';
}
void benchmark(HWND host, Button& button, const std::shared_ptr<const ButtonStyle>& style) {
    const auto expected_bounds = button.bounds();
    const auto expected_name = button.name();
    const auto expected_children = children(host);
    DWORD process{};
    require(GetWindowThreadProcessId(host, &process) == GetCurrentThreadId() && process == GetCurrentProcessId(),
        "Benchmark foreground target belongs to this UI thread");
    if (GetForegroundWindow() != host) {
        const auto foreground_thread = GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
        const auto current_thread = GetCurrentThreadId();
        const bool attach = foreground_thread && foreground_thread != current_thread;
        if (attach) require(AttachThreadInput(current_thread, foreground_thread, TRUE) != FALSE,
            "Attach the owned benchmark thread for foreground transfer");
        SetForegroundWindow(host);
        if (attach) require(AttachThreadInput(current_thread, foreground_thread, FALSE) != FALSE,
            "Detach the owned benchmark thread after foreground transfer");
    }
    require(GetForegroundWindow() == host, "Benchmark owns foreground before presentation timing");
    for (const bool styled : {benchmark_styled_first, !benchmark_styled_first}) {
        button.set_style(styled ? style : nullptr);
        for (int i = 0; i < 60; ++i) { button.pointer_move(i % 2 != 0); flush(host); }
        const auto bounds = button.bounds();
        require(bounds.x == expected_bounds.x && bounds.y == expected_bounds.y && bounds.width == expected_bounds.width &&
            bounds.height == expected_bounds.height && button.name() == expected_name && children(host) == expected_children,
            "Styled and default benchmarks use identical content, bounds and native peers");
        const auto before = Resources::read();
        const auto frames = presents, native_paints = paints;
        std::vector<double> latency;
        constexpr int iterations = benchmark_frames;
        latency.reserve(iterations);
        const auto cpu = cpu_ticks(), cycles = cpu_cycles();
        for (int i = 0; i < iterations; ++i) {
            const auto foreground = GetForegroundWindow();
            if (foreground != host) {
                DWORD foreground_process{};
                GetWindowThreadProcessId(foreground, &foreground_process);
                std::cerr << "Foreground guard: styled=" << styled << " frame=" << i
                    << " foreground_pid=" << foreground_process << " owned_pid=" << GetCurrentProcessId() << '\n';
            }
            require(foreground == host, "Benchmark keeps foreground during presentation timing");
            const auto start = std::chrono::steady_clock::now();
            button.pointer_move(i % 2 != 0);
            flush(host);
            latency.push_back(std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count());
        }
        const auto cpu_us = (cpu_ticks() - cpu) / 10.0;
        const auto used_cycles = cpu_cycles() - cycles;
        const auto after = Resources::read();
        std::sort(latency.begin(), latency.end());
        require(presents - frames == iterations && paints - native_paints == 3 * iterations,
            "Both benchmark variants perform exactly the same completed root and native paints");
        require(Drawing::live_targets() == 1, "Benchmark uses only the root render target");
        after.retained_from(before);
        std::cout << "BENCH styled=" << styled << " styled_first=" << benchmark_styled_first << " iterations=" << iterations
            << " ui_thread_cpu_us_per_state_paint=" << cpu_us / iterations
            << " ui_thread_cycles_per_state_paint=" << double(used_cycles) / iterations
            << " button_width=" << bounds.width << " button_height=" << bounds.height
            << " latency_p50_us=" << latency[iterations / 2] << " latency_p95_us=" << latency[iterations * 95 / 100]
            << " presents=" << presents - frames << " wm_paint=" << paints - native_paints
            << " private_before=" << before.private_bytes << " private_after=" << after.private_bytes
            << " handles=" << after.handles << " gdi=" << after.gdi << " user=" << after.user
            << " targets=" << Drawing::live_targets() << '\n';
    }
    button.pointer_move(false);
    button.set_style(nullptr);
}
#endif

struct StartFixture {
    static inline thread_local StartFixture* active{};
    Window& window;
    std::function<void(HWND)> body;
    UINT_PTR timer{};
    ULONGLONG deadline{GetTickCount64() + 10000};
    bool ran{};
    std::exception_ptr error;
    StartFixture(Window& value, std::function<void(HWND)> callback) : window(value), body(std::move(callback)) {
        active = this;
        timer = SetTimer(nullptr, 0, 10, [](HWND, UINT, UINT_PTR id, DWORD) {
            auto& fixture = *active;
            const auto hwnd = find_host();
            if (!hwnd && GetTickCount64() < fixture.deadline) return;
            KillTimer(nullptr, id);
            fixture.timer = 0;
            try {
                require(hwnd != nullptr, "Native styling fixture starts within ten seconds");
                fixture.ran = true;
                fixture.body(hwnd);
            } catch (...) { fixture.error = std::current_exception(); }
            fixture.window.close();
        });
        require(timer != 0, "Schedule same-thread native style fixture");
    }
    ~StartFixture() { if (timer) KillTimer(nullptr, timer); active = nullptr; }
};
#ifndef XUI_STYLING_BASELINE
void button_alignment_contracts() {
    if (Palette::system(ThemeMode::light).high_contrast) {
        std::cout << "SKIP authored-color Button alignment pixels under system high contrast\n";
        return;
    }
    Window window({window_title, {360, 180}, ThemeMode::light});
    auto root = std::make_shared<Stack>(Axis::horizontal);
    auto label = std::make_shared<Label>(L"8");
    PartStyleValues text;
    text.foreground = ThemeColor{0x00ff00};
    text.horizontal_alignment = StyleAlignment::center;
    text.vertical_alignment = StyleAlignment::center;
    label->set_control_style_values(StylePart::root, text);
    label->set_auto_size(false);
    label->set_preferred_size({36, 36});
    label->set_maximum_size({36, 36});
    root->add(label);
    std::array<std::shared_ptr<Button>, 2> buttons;
    for (std::size_t i = 0; i < buttons.size(); ++i) {
        auto button = std::make_shared<Button>(L"8");
        button->set_auto_size(false);
        button->set_preferred_size({36, 36});
        button->set_maximum_size({36, 36});
        ButtonStyleValues values;
        values.background = ThemeColor{0x242424};
        values.foreground = text.foreground;
        values.border_brush = ThemeColor{0xa0a0a0};
        values.border_thickness = i == 0 ? Insets{1, 1, 1, 1} : Insets{};
        values.padding = i == 0 ? Insets{} : Insets{2, 2, 2, 2};
        values.corner_radius = 0.0f;
        button->set_style(ButtonStyle::create(values));
        buttons[i] = button;
        root->add(button);
    }
    window.set_content(root);
    StartFixture start(window, [&](HWND host) {
        for (const auto visual : {VisualStyle::classic, VisualStyle::winui}) {
            window.set_visual_style(visual);
            for (const auto mode : {ThemeMode::light, ThemeMode::dark}) {
                window.set_theme(mode);
                for (const bool enabled : {true, false}) {
                    for (const auto& button : buttons) button->set_enabled(enabled);
                    flush(host);
                    std::vector<RECT> areas;
                    for (const auto peer : children(host)) if (native_text(peer) == L"8") {
                        RECT bounds{};
                        require(GetWindowRect(peer, &bounds) != FALSE, "Read alignment peer bounds");
                        MapWindowPoints(nullptr, host, reinterpret_cast<POINT*>(&bounds), 2);
                        areas.push_back(bounds);
                    }
                    require(areas.size() == 3, "Alignment fixture retains one label and two buttons");
                    std::sort(areas.begin(), areas.end(), [](const RECT& a, const RECT& b) { return a.left < b.left; });
                    const auto image = owned_window_capture::capture(host);
                    std::array<double, 2> reference{};
                    for (std::size_t i = 0; i < areas.size(); ++i) {
                        const auto area = areas[i];
                        require(area.left >= 0 && area.top >= 0 && area.right <= image.width && area.bottom <= image.height,
                            "Alignment peer lies inside the captured client");
                        const auto extent = MulDiv(36, GetDpiForWindow(host), 96);
                        require(area.right - area.left == extent && area.bottom - area.top == extent,
                            "Alignment fixture uses actual 36-DIP square cells");
                        RECT ink{area.right, area.bottom, area.left, area.top};
                        unsigned count{};
                        for (auto y = area.top; y < area.bottom; ++y) for (auto x = area.left; x < area.right; ++x) {
                            const auto pixel = image.data[static_cast<std::size_t>(y) * image.width + x] & 0xffffff;
                            if (((pixel >> 8) & 255) < 160 || (pixel & 255) > 80 || (pixel >> 16) > 80) continue;
                            ink.left = std::min(ink.left, x); ink.right = std::max(ink.right, x);
                            ink.top = std::min(ink.top, y); ink.bottom = std::max(ink.bottom, y);
                            ++count;
                        }
                        require(count > 5, "Every alignment peer paints visible number glyphs");
                        const std::array<double, 2> offset{
                            (ink.left + ink.right + 1 - area.left - area.right) / 2.0,
                            (ink.top + ink.bottom + 1 - area.top - area.bottom) / 2.0};
                        if (i == 0) reference = offset;
                        else {
                            const auto tolerance = GetDpiForWindow(host) / 96.0;
                            if (std::abs(offset[0] - reference[0]) > tolerance ||
                                std::abs(offset[1] - reference[1]) > tolerance) {
                                std::cerr << "Button alignment offset: " << offset[0] << ',' << offset[1]
                                    << "; centered label: " << reference[0] << ',' << reference[1] << '\n';
                                throw std::runtime_error("Covered and cleared button glyphs align with centered coordinate text");
                            }
                        }
                    }
                }
            }
        }
    });
    const auto result = Application::run(window);
    if (start.error) std::rethrow_exception(start.error);
    require(start.ran && result == 0 && window.error().empty(), "Native button alignment completes");
    std::cout << "PASS native Button and coordinate text alignment in both themes and visual styles\n";
}

void toggle_native_contracts() {
    Window window({window_title, {420, 280}, ThemeMode::light});
    auto root = std::make_shared<Stack>(Axis::vertical);
    auto toggle = std::make_shared<Toggle>(L"Styled Toggle");
    auto content = std::make_shared<Stack>(Axis::vertical); content->add(toggle);
    auto ancestor = std::make_shared<Expander>(L"Disabled ancestor", content);
    ancestor->set_expanded(true);
    auto input = std::make_shared<TextInput>(L"Retained input");
    input->set_text(L"Keep native input");
    root->add(ancestor); root->add(input);
    PartStyleValues indicator; indicator.size = 80.0f;
    PartStyleValues surface; surface.background = ThemeColor{0x134679, 0x975431};
    PartStyleValues disabled; disabled.foreground = ThemeColor{0x987654};
    auto style = ControlStyle::create(StyleTarget::toggle, {{StylePart::indicator, indicator}, {StylePart::root, surface}},
        {{StylePart::root, style_states::disabled, disabled}});
    toggle->set_style(style);
    window.set_content(root);
    StartFixture start(window, [&](HWND host) {
        flush(host);
        const auto peers = children(host);
        HWND toggle_peer{}, edit{};
        for (const auto hwnd : peers) {
            if (native_text(hwnd) == toggle->name()) toggle_peer = hwnd;
            wchar_t cls[80]{}; GetClassNameW(hwnd, cls, 80);
            if (_wcsicmp(cls, L"EDIT") == 0) edit = hwnd;
        }
        require(toggle_peer && edit, "Toggle and native editor retain actual peers");
        require(toggle->bounds().height >= 80, "Native layout reserves the authored outer indicator size");
        const auto verify_pixels = [&](ThemeMode mode) {
            window.set_theme(mode); flush(host);
            auto image = owned_window_capture::capture(host);
            Pixels pixels{image.width, image.height, std::move(image.data)};
            RECT bounds{}; require(GetWindowRect(toggle_peer, &bounds) != FALSE, "Read live Toggle bounds");
            MapWindowPoints(nullptr, host, reinterpret_cast<POINT*>(&bounds), 2);
            const int margin = MulDiv(6, GetDpiForWindow(host), 96);
            if (!Palette::system(mode).high_contrast)
                pixels.expect(bounds.right - margin, bounds.bottom - margin, surface.background->resolve(mode),
                    "The actual window adapter paints the Toggle's authored root surface");
        };
        verify_pixels(ThemeMode::light); verify_pixels(ThemeMode::dark);
        window.set_theme(ThemeMode::light); flush(host);
        SendMessageW(edit, EM_SETSEL, 2, 8);
        toggle->set_style(nullptr); flush(host);
        require(children(host) == peers, "Removing a style does not replace peers or create decorative peers");
        toggle->set_style(style); flush(host);
        require(children(host) == peers, "Applying named parts keeps one native Toggle peer");
        SendMessageW(toggle_peer, WM_SETFOCUS, 0, 0);
        SendMessageW(toggle_peer, WM_KEYDOWN, VK_SPACE, 0);
        SendMessageW(toggle_peer, WM_KEYUP, VK_SPACE, 0);
        require(toggle->checked(), "Native Space key toggles the whole styled control");
        flush(host);
        std::atomic<bool> done{};
        std::exception_ptr automation_error;
        std::thread automation([&] {
            const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            try {
                success(initialized, "Initialize Toggle UIA client");
                Microsoft::WRL::ComPtr<IUIAutomation> client;
                success(CoCreateInstance(CLSID_CUIAutomation8, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&client)), "Create Toggle UIA client");
                require(client != nullptr, "UIA client exists");
                Microsoft::WRL::ComPtr<IUIAutomation2> bounded_client;
                success(client.As(&bounded_client), "Configure bounded UIA calls");
                success(bounded_client->put_ConnectionTimeout(5000), "Bound UIA connection time");
                success(bounded_client->put_TransactionTimeout(5000), "Bound UIA transaction time");
                Microsoft::WRL::ComPtr<IUIAutomationElement> element;
                success(client->ElementFromHandle(toggle_peer, &element), "Find styled Toggle UIA element");
                require(element != nullptr, "Toggle HWND host exists in UIA");
                VARIANT type{}; type.vt = VT_I4; type.lVal = UIA_CheckBoxControlTypeId;
                Microsoft::WRL::ComPtr<IUIAutomationCondition> condition;
                success(client->CreatePropertyCondition(UIA_ControlTypePropertyId, type, &condition), "Find Toggle semantic role");
                Microsoft::WRL::ComPtr<IUIAutomationElement> semantic;
                success(element->FindFirst(TreeScope_Subtree, condition.Get(), &semantic), "Find semantic Toggle beneath its HWND host");
                require(semantic != nullptr, "Styled Toggle exposes its semantic checkbox element");
                Microsoft::WRL::ComPtr<IUIAutomationTogglePattern> pattern;
                success(semantic->GetCurrentPatternAs(UIA_TogglePatternId, IID_PPV_ARGS(&pattern)), "Styled Toggle keeps the Toggle pattern");
                require(pattern != nullptr, "Semantic Toggle exposes a non-null Toggle pattern");
                RECT accessible{};
                success(semantic->get_CurrentBoundingRectangle(&accessible), "Read styled Toggle accessible bounds");
                require(accessible.bottom - accessible.top >= 80, "UIA bounds include the styled indicator metric");
                ToggleState state{};
                success(pattern->get_CurrentToggleState(&state), "Read styled Toggle state");
                require(state == ToggleState_On, "UIA exposes the model checked state");
                success(pattern->Toggle(), "UIA activates the existing styled Toggle");
            } catch (...) { automation_error = std::current_exception(); }
            if (SUCCEEDED(initialized)) CoUninitialize();
            done = true;
        });
        while (!done) pump_for(10);
        automation.join();
        if (automation_error) std::rethrow_exception(automation_error);
        pump_for(20);
        require(!toggle->checked(), "UIA toggles the original model");
        ancestor->set_enabled(false); flush(host); flush(host);
        require(toggle->enabled() && !IsWindowEnabled(toggle_peer) &&
            toggle->effective_style_values(StylePart::label)->foreground == disabled.foreground,
            "Disabled ancestor selects generic disabled style without changing local enabled");
        ancestor->set_enabled(true); flush(host);
        auto dialog = std::make_shared<ContentDialog>(L"Modal", std::make_shared<Label>(L"Modal content"));
        window.show_dialog(dialog, *toggle); flush(host); flush(host);
        require(toggle->enabled() && !IsWindowEnabled(toggle_peer) &&
            toggle->effective_style_values(StylePart::label)->foreground == disabled.foreground,
            "Modal context selects generic disabled style");
        dialog->cancel(); flush(host); flush(host);
        require(IsWindowEnabled(toggle_peer), "Closing the modal restores enabled presentation");
        toggle->set_style(nullptr); flush(host);
        require(!toggle->effective_style_values(StylePart::root), "Clearing style restores the default path");
        require(std::find(peers.begin(), peers.end(), toggle_peer) != peers.end() && IsWindow(toggle_peer),
            "Style changes preserve the native Toggle identity");
        DWORD first{}, last{};
        SendMessageW(edit, EM_GETSEL, reinterpret_cast<WPARAM>(&first), reinterpret_cast<LPARAM>(&last));
        require(first == 2 && last == 8 && native_text(edit) == L"Keep native input", "Toggle styles preserve native editor selection and text");
    });
    const auto result = Application::run(window);
    if (start.error) std::rethrow_exception(start.error);
    require(start.ran && result == 0 && window.error().empty(), "Native Toggle integration completes");
    std::cout << "PASS Toggle native metrics, keyboard, UIA, ancestor/modal context, editor retention\n";
}
#endif
void native_contracts(bool measure, int repetition) {
    require(Drawing::live_targets() == 0, "Native fixture begins without retained render targets");
#ifndef XUI_STYLING_BASELINE
    std::weak_ptr<const ButtonStyle> released_style;
#endif
    HWND closed_host{}, closed_edit{}, closed_button{};
    {
        Window window({window_title, {420, 220}, ThemeMode::light});
        auto root = std::make_shared<Stack>(Axis::vertical);
        auto button = std::make_shared<Button>(L"Style pixels MMMM");
        button->set_auto_size(false);
        button->set_preferred_size({280, 56});
        auto input = std::make_shared<TextInput>(L"Retained native input");
        input->set_text(L"Stable native text and selection");
        root->add(button); root->add(input);
        window.set_content(root);
        StartFixture start(window, [&](HWND host) {
            closed_host = host;
            flush(host);
            const auto peers = children(host);
            HWND edit{}, button_peer{};
            for (const auto hwnd : peers) {
                wchar_t cls[80]{};
                require(GetClassNameW(hwnd, cls, 80) != 0, "Read native peer class");
                if (_wcsicmp(cls, L"EDIT") == 0) edit = hwnd;
                if (native_text(hwnd) == button->name()) button_peer = hwnd;
            }
            require(edit && button_peer && edit != button_peer, "Fixture contains actual EDIT and Button peers");
            closed_edit = edit; closed_button = button_peer;
            SendMessageW(edit, EM_SETSEL, 2, 12);
            const auto verify = [&] {
                require(children(host) == peers, "Live style application/clear preserves every native HWND");
                require(Drawing::live_targets() == 1, "Buttons share exactly one root render target");
                require(IsWindow(edit) && native_text(edit) == input->text() &&
                    input->text() == L"Stable native text and selection", "Style/theme changes preserve real EDIT text");
                DWORD first{}, last{};
                SendMessageW(edit, EM_GETSEL, reinterpret_cast<WPARAM>(&first), reinterpret_cast<LPARAM>(&last));
                require(first == 2 && last == 12 && input->selection() == TextInput::Selection{2, 12},
                    "Style/theme changes preserve native and model selection without keyboard focus");
            };
#ifndef XUI_STYLING_BASELINE
            auto scope = ResourceScope::create({{"Fill", ThemeColor{0xe13457, 0x2496c3}},
                {"Alias", std::string{"Fill"}}});
            std::weak_ptr<const ResourceScope> released_scope = scope;
            auto values = authored_values();
            values.background = scope->color("Alias");
            auto style = ButtonStyle::create(values);
            released_style = style;
            scope.reset();
            require(released_scope.expired(), "Resolved color values do not retain their source ResourceScope");
            ObservePaints observed(host);
            ButtonStyleValues local;
            local.background = ThemeColor{0x2abb57};
#else
            ObservePaints observed(host);
#endif
            // Capture service startup is not part of the repeated Window lifetime resource contract.
            if (repetition == 0) {
#ifdef XUI_STYLING_BASELINE
                for (int i = 0; i < 5; ++i) {
                    window.set_theme(i == 3 ? ThemeMode::dark : ThemeMode::light);
                    flush(host);
                    (void)owned_window_capture::capture(host);
                    verify();
                }
#else
                const auto capture = [&] {
                    flush(host);
                    trace_resources("before-capture", repetition);
                    auto image = owned_window_capture::capture(host);
                    trace_resources("after-capture", repetition);
                    return Pixels{image.width, image.height, std::move(image.data)};
                };
                RECT bounds{};
                require(GetWindowRect(button_peer, &bounds) != FALSE, "Read live Button bounds");
                MapWindowPoints(nullptr, host, reinterpret_cast<POINT*>(&bounds), 2);
                const auto dpi = GetDpiForWindow(host) / 96.0f;
                const int x = bounds.left + static_cast<int>(std::lround(10 * dpi));
                const int y = bounds.top + static_cast<int>(std::lround(10 * dpi));
                const auto baseline = capture();
                button->set_style(style);
                auto styled = capture();
                if (!Palette::system(ThemeMode::light).high_contrast)
                    styled.expect(x, y, values.background->light, "A live application Button paints its shared style");
                verify();
                button->set_style_values(local);
                auto overridden = capture();
                if (!Palette::system(ThemeMode::light).high_contrast)
                    overridden.expect(x, y, 0x2abb57, "A live application Button paints local overrides");
                button->set_style_values({});
                window.set_theme(ThemeMode::dark);
                auto dark = capture();
                if (!Palette::system(ThemeMode::dark).high_contrast)
                    dark.expect(x, y, values.background->dark, "Theme changes repaint a retained style with its dark resource color");
                verify();
                window.set_theme(ThemeMode::light);
                button->set_style(nullptr);
                const auto cleared = capture();
                cleared.expect(x, y, baseline.at(x, y), "Clearing a live Button style restores default face paint");
                verify();
#endif
            }

            const auto cycle = [&](int index) {
#ifndef XUI_STYLING_BASELINE
                auto cycle_style = ButtonStyle::create(values);
                std::weak_ptr<const ButtonStyle> retired = cycle_style;
                button->set_style(cycle_style);
#endif
                window.set_visual_style(index % 2 ? VisualStyle::winui : VisualStyle::classic);
                window.set_theme(ThemeMode::dark);
                flush(host); verify();
#ifndef XUI_STYLING_BASELINE
                local.padding = Insets{16, 7, 18, 8};
                button->set_style_values(local);
#endif
                window.set_theme(ThemeMode::high_contrast);
                flush(host); verify();
#ifndef XUI_STYLING_BASELINE
                button->set_style_values({});
                button->set_style(nullptr);
#endif
                window.set_theme(ThemeMode::light);
                window.set_visual_style(VisualStyle::classic);
                flush(host); verify();
#ifndef XUI_STYLING_BASELINE
                cycle_style.reset();
                require(retired.expired(), "Apply/clear cycles do not retain retired immutable style definitions");
#endif
            };
            for (int i = 0; i < 16; ++i) cycle(i);
            pump_for(100);
            const auto warmed = Resources::read();
            trace_resources("warmed", repetition);
            auto last_user = warmed.user;
            for (int i = 0; i < 128; ++i) {
                cycle(i);
                if (trace_resources_enabled) {
                    const auto user = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
                    if (user != last_user) trace_resources("cycles", i + 1);
                    last_user = user;
                }
            }
            pump_for(100);
            const auto after = Resources::read();
            trace_resources("after", repetition);
            after.retained_from(warmed);
            require(presents > 0 && paints > 0, "Paint observers measured real root and native WM_PAINT work");
            pump_for(100);
            const auto idle_presents = presents, idle_paints = paints;
            pump_for(180);
            require(presents == idle_presents && paints == idle_paints, "Settled style/theme state produces no idle paints");
#ifndef XUI_STYLING_BASELINE
            if (measure) benchmark(host, *button, style);
#else
            require(!measure, "The pristine baseline supports lifetime checks, not authored-style benchmarks");
#endif
            DrawingTestAccess::lose_target();
            flush(host); flush(host);
            verify();
#ifndef XUI_STYLING_BASELINE
            button->set_style(style);
            flush(host);
            button->set_style(nullptr);
            style.reset();
            require(released_style.expired(), "Clearing style releases its last shared definition");
#endif
            verify();
            std::cout << "PASS native styling cycle=" << repetition << " retained_hwnds=" << peers.size()
                << " targets=" << Drawing::live_targets() << " style_theme_cycles=144 idle_paints=0\n";
        });
        const auto result = Application::run(window);
        if (start.error) std::rethrow_exception(start.error);
        if (!start.ran || result != 0 || !window.error().empty())
            std::wcerr << L"Native fixture ran=" << start.ran << L" exit=" << result << L" error=" << window.error() << L'\n';
        require(start.ran && result == 0 && window.error().empty(), "Native styling fixture completes without application errors");
    }
    require(!IsWindow(closed_host) && !IsWindow(closed_edit) && !IsWindow(closed_button),
        "Closing destroys the owner, EDIT and Button HWNDs");
#ifndef XUI_STYLING_BASELINE
    require(released_style.expired(), "Window teardown releases styles");
#endif
    require(Drawing::live_targets() == 0,
        "Window teardown releases styles and all render targets");
}
}

int main(int argc, char** argv) {
    try {
        bool measure{}, lower{}, toggle_only{}, alignment_only{};
        for (int i = 1; i < argc; ++i) {
            const std::string_view argument{argv[i]};
            if (argument == "--benchmark" && !measure) measure = true;
            else if (argument == "--trace-resources" && !trace_resources_enabled) trace_resources_enabled = true;
            else if (argument == "--styled-first" && !benchmark_styled_first) benchmark_styled_first = true;
            else if (argument == "--lower-level" && !lower) lower = true;
            else if (argument == "--toggle-only" && !toggle_only) toggle_only = true;
            else if (argument == "--alignment-only" && !alignment_only) alignment_only = true;
            else throw std::runtime_error("Usage: xui_styling_window_tests [--toggle-only] [--alignment-only] [--benchmark] [--styled-first] [--lower-level] [--trace-resources]");
        }
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        success(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED), "Initialize styling fixture COM");
        struct Com { ~Com() { CoUninitialize(); } } com;
#ifndef XUI_STYLING_BASELINE
        if (alignment_only) {
            button_alignment_contracts();
            return 0;
        }
        if (toggle_only) {
            toggle_pixel_contracts();
            toggle_native_contracts();
            return 0;
        }
        pixel_contracts();
        toggle_pixel_contracts();
        if (lower) { lower_level_benchmarks(); return 0; }
        button_alignment_contracts();
        toggle_native_contracts();
#else
        require(!lower && !measure, "Pristine fixture only measures unchanged Window/editor/theme lifetime");
        std::cout << "BASELINE pristine native library; authored style operations omitted; capture/theme/state checks retained.\n";
#endif
        native_contracts(measure, 0);
        const auto closed_resources = Resources::read();
        for (int i = 1; i < 3; ++i) {
            native_contracts(false, i);
            Resources::read().retained_from(closed_resources);
        }
        std::cout << "PASS styling_window_tests\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL styling_window_tests: " << error.what() << '\n';
        return 1;
    }
}
