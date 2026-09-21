#include "collections_fixture.hpp"
#include "xui/application.hpp"
#include "xui/documents.hpp"
#include "xui/reveal.hpp"
#include "../src/drawing.hpp"
#include "../src/images.hpp"
#include <windows.h>
#include <richedit.h>
#include <cmath>
#include <iostream>

namespace xui {
struct DrawingTestAccess {
    static void software_target(Drawing& drawing, HWND hwnd) {
        auto status = drawing.factory_->CreateHwndRenderTarget(
            D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
                96, 96, D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE),
            D2D1::HwndRenderTargetProperties(hwnd, D2D1::SizeU(160, 160)), &drawing.target_);
        collections_test::require(SUCCEEDED(status), "Create presentation software target");
        ++Drawing::live_targets_;
        collections_test::require(SUCCEEDED(drawing.target_->CreateSolidColorBrush(D2D1::ColorF(0), &drawing.brush_)),
            "Create presentation brush");
    }
    static COLORREF pixel(Drawing& drawing, int x, int y) {
        Microsoft::WRL::ComPtr<ID2D1GdiInteropRenderTarget> target;
        collections_test::require(SUCCEEDED(drawing.target_.As(&target)), "Get presentation pixel target");
        HDC dc{};
        collections_test::require(SUCCEEDED(target->GetDC(D2D1_DC_INITIALIZE_MODE_COPY, &dc)), "Read presentation pixels");
        const auto color = GetPixel(dc, x, y);
        const RECT unchanged{};
        target->ReleaseDC(&unchanged);
        return color;
    }
};
}

namespace {
using namespace xui;
using collections_test::require;
HWND child(HWND owner, const wchar_t* name) {
    struct Search { const wchar_t* name; HWND result{}; } search{name};
    EnumChildWindows(owner, [](HWND hwnd, LPARAM data) -> BOOL {
        auto& search = *reinterpret_cast<Search*>(data);
        wchar_t text[128]{}; GetWindowTextW(hwnd, text, 128);
        if (std::wstring_view(text) == search.name) { search.result = hwnd; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    require(search.result != nullptr, "Presentation peer exists");
    return search.result;
}
void flush(HWND hwnd) { SendMessageW(hwnd, WM_APP + 12, 0, 0); }
void click(HWND hwnd, float y, UINT modifiers = 0) {
    const float scale = GetDpiForWindow(hwnd) / 96.0f;
    const auto point = MAKELPARAM(int(42 * scale), int(y * scale));
    SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON | modifiers, point);
    SendMessageW(hwnd, WM_LBUTTONUP, modifiers, point);
}
void image_fit_fill() {
    HWND hwnd = CreateWindowExW(WS_EX_NOACTIVATE, L"STATIC", L"Presentation pixels", WS_POPUP,
        0, 0, 160, 160, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    require(hwnd != nullptr, "Create hidden presentation raster fixture");
    Drawing drawing;
    struct Cleanup { Drawing& drawing; HWND hwnd; ~Cleanup() { drawing.release(); DestroyWindow(hwnd); } } cleanup{drawing, hwnd};
    drawing.initialize(); DrawingTestAccess::software_target(drawing, hwnd);
    auto pixels = std::make_shared<ImagePixels>();
    pixels->id = UINT64_MAX - 7; pixels->size = {8, 4};
    pixels->pixels.resize(8 * 4 * 4);
    for (size_t i = 0; i < pixels->pixels.size(); i += 4)
        pixels->pixels[i + 2] = pixels->pixels[i + 3] = std::byte{255};
    for (bool fill : {false, true}) {
        require(drawing.begin(hwnd, 96, D2D1::ColorF(0)), "Begin image fit/fill frame");
        require(drawing.image(pixels, {10, 10, 100, 100}, fill), "Draw fit/fill image");
        require(DrawingTestAccess::pixel(drawing, 60, 60) == RGB(255, 0, 0), "Image center remains visible");
        require(DrawingTestAccess::pixel(drawing, 60, 15) == (fill ? RGB(255, 0, 0) : RGB(0, 0, 0)),
            "Fit letterboxes while fill covers the destination");
        require(DrawingTestAccess::pixel(drawing, 5, 60) == RGB(0, 0, 0), "Fill clips the image to its bounds");
        require(drawing.end(), "End image fit/fill frame");
    }
}
}
int main() {
    try {
        image_fit_fill();
        Application app;
        auto window = app.create_window({L"XUI presentation regression", {700, 650}});
        window->set_show_activated(false);
        window->set_theme(ThemeMode::system);
        window->set_presentation("Consolas", 18, true, false);
        auto root = std::make_shared<Stack>(Axis::vertical);
        auto text = std::make_shared<TextInput>(L"Native presentation input");
        require(!text->presentation_font_family(), "Presentation family inherits by default");
        text->set_presentation_font_family("Cascadia Mono");
        const auto retained_family = text->presentation_font_family();
        text->set_presentation_font_family("Cascadia Mono");
        require(text->presentation_font_family() == retained_family, "An unchanged family keeps shared ownership");
        for (const auto& invalid : {std::string(129, 'a'), std::string("\xed\xa0\x80", 3), std::string("a\0b", 3)}) {
            bool rejected{};
            try { text->set_presentation_font_family(invalid); }
            catch (const std::invalid_argument&) { rejected = true; }
            require(rejected && text->presentation_font_family() == retained_family,
                "Invalid native font families leave the current override unchanged");
        }
        auto deferred = std::make_shared<MultilineText>(L"Deferred preview");
        deferred->set_read_only(true);
        deferred->set_presentation_font_family("Cascadia Mono");
        auto preview_host = std::make_shared<ContentHost>(deferred);
        auto reveal = std::make_shared<Reveal>(preview_host);
        reveal->set_duration(0);
        auto items = std::make_shared<ItemsView>(L"Presentation items");
        items->set_items(std::make_shared<collections_test::DetailItems>(100));
        auto grid = std::make_shared<DataGrid>(L"Presentation grid");
        grid->set_columns({{L"Name", 400}});
        grid->set_source(std::make_shared<collections_test::Rows>(100));
        auto tree = std::make_shared<TreeView>(L"Compact presentation tree");
        PartStyleValues compact; compact.row_height = 24.0f; compact.font_size = 12.0f;
        tree->set_control_style_values(StylePart::root, compact);
        tree->set_presentation_font_size(12);
        PartStyleValues density; density.row_height = 42.0f;
        items->set_control_style_values(StylePart::root, density);
        grid->set_control_style_values(StylePart::root, density);
        items->set_single_click_activation(true);
        grid->set_single_click_activation(true);
        int item_activations{}, grid_activations{};
        items->on_activate([&](ItemKey) { ++item_activations; });
        grid->on_activate([&] { ++grid_activations; });
        root->add(text); root->add(items, 1); root->add(grid, 1); root->add(tree);
        root->add(reveal);
        window->set_content(root);
        app.show(*window);
        HWND hwnd = FindWindowW(nullptr, L"XUI presentation regression");
        require(hwnd != nullptr, "Presentation window exists");
        window->post([&] {
            flush(hwnd);
            require(text->control_style_values(StylePart::text).font_family == retained_family &&
                text->control_style_values(StylePart::text).font_size == 18 &&
                deferred->control_style_values(StylePart::text).font_family == deferred->presentation_font_family(),
                "Current and deferred controls preserve family overrides while inheriting window font size");
            require(items->item_size().height == 42 && grid->effective_row_height() == 42,
                "Typography does not erase collection density");
            const auto& cell = grid->control_style_values(StylePart::cell);
            require(cell.font_size == 18 && cell.font_family->name == L"Consolas",
                "Window typography reaches grid cells");
            require(tree->control_style_values(StylePart::root).font_size == 12 &&
                tree->control_style_values(StylePart::primary_text).font_size == 12 && tree->item_size().height == 24,
                "Compact typography survives the window policy on root and text parts");
            const HWND item_peer = child(hwnd, L"Presentation items");
            const HWND grid_peer = child(hwnd, L"Presentation grid");
            click(item_peer, 21);
            click(grid_peer, grid->geometry().header_bottom() + 21);
            require(item_activations == 1 && grid_activations == 1, "Single pointer clicks activate file controls");
            click(item_peer, 63, MK_CONTROL);
            click(grid_peer, grid->geometry().header_bottom() + 63, MK_CONTROL);
            require(item_activations == 1 && grid_activations == 1, "Modifier selection does not activate");
            SendMessageW(item_peer, WM_LBUTTONDBLCLK, MK_LBUTTON, MAKELPARAM(42, 21));
            SendMessageW(grid_peer, WM_LBUTTONDBLCLK, MK_LBUTTON, MAKELPARAM(42, 63));
            require(item_activations == 1 && grid_activations == 1, "Double-click messages do not activate twice");
            grid->on_file_drag([] { return std::vector<std::wstring>{}; }, {});
            const auto original_source = grid->source();
            const float scale = GetDpiForWindow(grid_peer) / 96.0f;
            const auto press_point = MAKELPARAM(int(42 * scale),
                int((grid->geometry().header_bottom() + 21) * scale));
            SendMessageW(grid_peer, WM_LBUTTONDOWN, MK_LBUTTON, press_point);
            require(grid_activations == 1, "File-drag-enabled single click waits for release");
            grid->set_source(std::make_shared<collections_test::Rows>(100, false, 10));
            grid->select({20, 1}, false);
            SendMessageW(grid_peer, WM_LBUTTONUP, 0, press_point);
            require(grid_activations == 1 && grid->selected() == RowKey{20, 1},
                "Release after source replacement must not open or reselect an unclicked replacement");
            grid->set_source(original_source);
            grid->select({1, 1}, false);
            SendMessageW(grid_peer, WM_LBUTTONDOWN, MK_LBUTTON, press_point);
            grid->select({2, 1}, false);
            SendMessageW(grid_peer, WM_LBUTTONUP, 0, press_point);
            require(grid_activations == 1 && grid->selected() == RowKey{2, 1},
                "Release after selection replacement must not activate or restore the pressed item");
            click(grid_peer, grid->geometry().header_bottom() + 21);
            require(grid_activations == 2 && grid->selected() == RowKey{1, 1},
                "An unchanged deferred press activates the pressed item once");
            window->set_presentation("Segoe UI", 16, false, false);
            tree->set_presentation_font_size(14);
            flush(hwnd);
            require(grid->control_style_values(StylePart::cell).font_size == 16,
                "Live font changes reach existing controls");
            require(grid->control_style_values(StylePart::cell).font_family->name == L"Segoe UI" &&
                text->control_style_values(StylePart::text).font_family == retained_family &&
                deferred->control_style_values(StylePart::text).font_family->name == L"Cascadia Mono" &&
                deferred->control_style_values(StylePart::text).font_size == 16,
                "Global customization changes inherited fonts without replacing explicit families");
            text->set_presentation_font_family("Consolas");
            flush(hwnd);
            require(text->control_style_values(StylePart::text).font_family->name == L"Consolas",
                "A live family change updates the current control");
            text->set_presentation_font_family({});
            deferred->set_presentation_font_family({});
            flush(hwnd);
            require(!text->presentation_font_family() && !deferred->presentation_font_family() &&
                text->control_style_values(StylePart::text).font_family->name == L"Segoe UI" &&
                deferred->control_style_values(StylePart::text).font_family->name == L"Segoe UI" &&
                retained_family->name == L"Cascadia Mono",
                "Clearing current and deferred overrides restores inheritance without invalidating retained families");
            require(tree->control_style_values(StylePart::root).font_size == 14 &&
                tree->control_style_values(StylePart::primary_text).font_size == 14,
                "Live compact font changes survive repeated collection");
            tree->set_presentation_font_size(0);
            flush(hwnd);
            require(tree->control_style_values(StylePart::primary_text).font_size == 16,
                "Clearing the compact override restores window typography");
            items->set_offset(0); grid->set_offset(0, 0);
            const auto wheel = MAKEWPARAM(0, static_cast<WORD>(-WHEEL_DELTA));
            SendMessageW(item_peer, WM_MOUSEWHEEL, wheel, 0);
            SendMessageW(grid_peer, WM_MOUSEWHEEL, wheel, 0);
            require(items->offset() == 126 && grid->offset() == 126, "Disabled smooth scrolling is immediate");
            window->set_theme(ThemeMode::light);
            window->set_presentation("Segoe UI", 16, true, false);
            items->set_offset(0); grid->set_offset(0, 0);
            BOOL motion{}; SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &motion, 0);
            HIGHCONTRASTW contrast{sizeof(contrast)};
            SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0);
            SendMessageW(item_peer, WM_MOUSEWHEEL, wheel, 0);
            SendMessageW(grid_peer, WM_MOUSEWHEEL, wheel, 0);
            if (motion && !(contrast.dwFlags & HCF_HIGHCONTRASTON)) {
                require(items->offset() == 0 && grid->offset() == 0, "Smooth wheel input starts without a jump");
                flush(hwnd);
                Sleep(40); SendMessageW(hwnd, WM_TIMER, 43, 0);
                require(items->offset() > 0 && items->offset() < 126 && grid->offset() > 0 && grid->offset() < 126,
                    "Smooth wheel motion advances through an intermediate offset");
                Sleep(160); SendMessageW(hwnd, WM_TIMER, 43, 0);
                require(items->offset() == 126 && grid->offset() == 126, "Smooth wheel motion settles");
            } else require(items->offset() == 126 && grid->offset() == 126, "Windows reduced motion overrides smooth scrolling");
            window->set_theme(ThemeMode::high_contrast);
            items->set_offset(0);
            SendMessageW(item_peer, WM_MOUSEWHEEL, wheel, 0);
            require(items->offset() == 126, "High contrast disables smooth scrolling");
            auto future = std::make_shared<MultilineText>(L"Future preview");
            future->set_text(L"Future preview contents");
            future->set_read_only(true);
            future->set_presentation_font_family("Cascadia Mono");
            window->replace_content(*preview_host, future);
            flush(hwnd);
            require(!reveal->open() && future->control_style_values(StylePart::text).font_family->name == L"Cascadia Mono" &&
                future->control_style_values(StylePart::text).font_size == 16,
                "Future controls receive explicit families while their native peers remain deferred");
            reveal->set_open(true);
            flush(hwnd);
            const HWND preview_peer = child(hwnd, L"Future preview contents");
            require(future->control_style_values(StylePart::text).font_family == future->presentation_font_family(),
                "Realizing a deferred preview preserves its family override");
            CHARFORMAT2W format{sizeof(format)};
            SendMessageW(preview_peer, EM_GETCHARFORMAT, SCF_DEFAULT, reinterpret_cast<LPARAM>(&format));
            require(std::wstring_view(format.szFaceName) == L"Cascadia Mono" && format.yHeight == 16 * 15,
                "The read-only native preview uses the explicit family and customized window font size");
            future->set_presentation_font_family({});
            window->set_presentation("Consolas", 14, true, false);
            flush(hwnd);
            require(future->control_style_values(StylePart::text).font_family->name == L"Consolas" &&
                future->control_style_values(StylePart::text).font_size == 14 &&
                text->control_style_values(StylePart::text).font_family->name == L"Consolas",
                "Restored current and future controls follow subsequent window policy changes");
            SendMessageW(preview_peer, EM_GETCHARFORMAT, SCF_DEFAULT, reinterpret_cast<LPARAM>(&format));
            require(std::wstring_view(format.szFaceName) == L"Consolas" && format.yHeight == 14 * 15,
                "Restoring inheritance updates the retained native preview font");
            window->close();
        });
        const int result = app.run();
        if (result != 0) std::wcerr << window->error() << L'\n';
        require(result == 0, "Presentation window completes without errors");
        std::cout << "Presentation window checks passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
