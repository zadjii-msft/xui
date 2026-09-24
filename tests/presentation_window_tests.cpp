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
void axis_native_sizing() {
    for (const auto style : {VisualStyle::classic, VisualStyle::winui}) {
        WindowOptions options; options.title = L"XUI native axis sizing"; options.size = {700, 650}; options.visual_style = style;
        Window window(options);
        auto root = std::make_shared<Stack>(Axis::vertical);
        auto input = std::make_shared<TextInput>(L"Axis caption");
        input->set_text(L"Retained axis input");
        input->set_fixed_size({260, 96});
        input->set_axis_constraints(AxisConstraints{280.0f}, AxisConstraints{});
        auto wrapped = std::make_shared<Label>(L"Wrapped text must measure height using the independently fixed width.");
        wrapped->set_wrapping(true);
        wrapped->set_fixed_size({400, 20});
        wrapped->set_axis_constraints(AxisConstraints{100.0f}, AxisConstraints{});
        root->add(input); root->add(wrapped);
        window.set_content(root);
        unsigned changes{};
        input->on_change([&](const auto&) { ++changes; });
        bool ran{};
        window.post([&] {
            const auto hwnd = FindWindowW(L"Xui.Window.1", options.title.c_str());
            require(hwnd != nullptr, "Axis native window exists");
            const auto edit = child(hwnd, L"Retained axis input"), caption = child(hwnd, L"Axis caption");
            const auto id = input->id(); const auto native_id = GetDlgCtrlID(edit);
            SetFocus(edit); SendMessageW(edit, EM_SETSEL, 2, 6);
            const auto focus = GetFocus();
            require(focus == edit, "Axis fixture uses real native input focus");
            const auto retained = [&] {
                DWORD start{}, end{};
                SendMessageW(edit, EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
                require(IsWindow(edit) && child(hwnd, L"Retained axis input") == edit && input->id() == id &&
                    GetDlgCtrlID(edit) == native_id && GetFocus() == focus && start == 2 && end == 6 && changes == 0,
                    "Axis changes preserve HWND/id/focus/selection and emit no text changes");
            };
            require(input->bounds().width == 280 && wrapped->bounds().width == 100 &&
                wrapped->bounds().height > 20, "Fixed width precedes native wrapped-height measurement");
            const auto baseline_height = input->bounds().height;
            PartStyleValues font; font.font_size = 32.0f;
            input->set_control_style_values(StylePart::text, font);
            input->set_control_style_values(StylePart::header, font);
            flush(hwnd);
            require(input->bounds().height > baseline_height && input->bounds().width == 280,
                "Axis Auto height uses current field and caption typography");
            RECT edit_bounds{}, caption_bounds{};
            GetWindowRect(edit, &edit_bounds); GetWindowRect(caption, &caption_bounds);
            const float scale = GetDpiForWindow(hwnd) / 96.0f;
            const auto dc = GetDC(edit);
            require(dc != nullptr, "Read native EDIT font metrics");
            const auto previous = SelectObject(dc, reinterpret_cast<HFONT>(SendMessageW(edit, WM_GETFONT, 0, 0)));
            TEXTMETRICW metrics{};
            const auto measured = GetTextMetricsW(dc, &metrics);
            SelectObject(dc, previous); ReleaseDC(edit, dc);
            require(measured && metrics.tmHeight / scale >= 32 &&
                edit_bounds.bottom - edit_bounds.top >= metrics.tmHeight &&
                (caption_bounds.bottom - caption_bounds.top) / scale >= 48 && caption_bounds.bottom <= edit_bounds.top,
                "Large native EDIT and caption have separate, unclipped vertical allocations");
            retained();
            input->set_axis_constraints(AxisConstraints{}, std::nullopt); flush(hwnd);
            require(input->bounds().height == 96 && input->measure({1000, 1000}).width == 320,
                "Auto width leaves legacy fixed height intact");
            input->set_fixed_size({210, 110}); flush(hwnd);
            require(input->bounds().height == 110 && input->measure({1000, 1000}).width == 320,
                "Legacy edits remain masked only on the overridden axis");
            input->set_axis_constraints(std::nullopt, std::nullopt); flush(hwnd);
            require(input->bounds().width == 210 && input->bounds().height == 110,
                "Clearing overrides restores the most recent legacy size");
            input->set_axis_constraints(AxisConstraints{{}, 400, 500.0f}, AxisConstraints{});
            root->set_maximum_size({260, 1000}); flush(hwnd);
            require(root->bounds().width == 260 && input->bounds().width == 260,
                "Actual parent allocation wins over axis minimum");
            retained();
            ran = true; window.close();
        });
        const auto result = Application::run(window);
        if (result) std::wcerr << window.error() << L'\n';
        require(result == 0 && ran, "Native per-axis sizing completes");
    }
}
void hidden_stack_spacing() {
    WindowOptions options; options.title = L"XUI hidden Stack spacing"; options.size = {700, 650}; options.show_activated = false;
    Window window(options);
    auto root = std::make_shared<Stack>(Axis::vertical);
    auto group = std::make_shared<Stack>(Axis::vertical); group->set_spacing(10);
    std::array<std::shared_ptr<Button>, 3> children;
    for (std::size_t i = 0; i < children.size(); ++i) {
        children[i] = std::make_shared<Button>(L"Hidden spacing " + std::to_wstring(i));
        children[i]->set_preferred_size({100, 20.0f + 10 * i}); group->add(children[i]);
    }
    auto flex = std::make_shared<Stack>(Axis::horizontal); flex->set_spacing(10); flex->set_fixed_size({200, 40});
    std::array<std::shared_ptr<Button>, 3> flexible;
    for (std::size_t i = 0; i < flexible.size(); ++i) {
        flexible[i] = std::make_shared<Button>(L"Hidden flex " + std::to_wstring(i));
        flexible[i]->set_preferred_size({30, 20}); flex->add(flexible[i], i == 1 ? 98.0f : 1.0f);
    }
    flexible[1]->set_visible(false);
    auto zero = std::make_shared<Stack>(Axis::vertical); zero->set_spacing(10);
    auto before = std::make_shared<Button>(L"Before visible empty Stack"), after = std::make_shared<Button>(L"After visible empty Stack");
    before->set_preferred_size({100, 20}); after->set_preferred_size({100, 30});
    zero->add(before); zero->add(std::make_shared<Stack>(Axis::vertical)); zero->add(after);
    root->add(group); root->add(flex); root->add(zero); window.set_content(root);
    bool ran{};
    window.post([&] {
        const auto hwnd = FindWindowW(L"Xui.Window.1", options.title.c_str());
        require(hwnd != nullptr, "Hidden Stack native window exists");
        const float scale = GetDpiForWindow(hwnd) / 96.0f;
        std::array<HWND, 3> original{};
        for (std::size_t i = 0; i < children.size(); ++i) original[i] = child(hwnd, children[i]->name().c_str());
        for (const auto mask : {5u, 6u, 3u, 0u, 7u, 5u, 7u}) {
            unsigned count{}; float height{};
            for (std::size_t i = 0; i < children.size(); ++i) {
                children[i]->set_visible((mask & (1u << i)) != 0);
                if (children[i]->visible()) { ++count; height += 20.0f + 10 * i; }
            }
            if (count) height += (count - 1) * 10.0f;
            flush(hwnd);
            require(std::abs(group->bounds().height - height) < 0.01f, "Hidden children reserve no native Stack gaps");
            float position = group->bounds().y;
            bool previous{};
            for (std::size_t i = 0; i < children.size(); ++i) {
                const auto bounds = children[i]->bounds();
                require(group->child_at(i) == children[i] && child(hwnd, children[i]->name().c_str()) == original[i],
                    "Visibility preserves authored indices and native HWND identity");
                if (!children[i]->visible()) {
                    require(bounds.width == 0 && bounds.height == 0 && !IsWindowVisible(original[i]),
                        "Hidden native child has zero model bounds and hidden HWND");
                    continue;
                }
                if (previous) position += 10;
                require(bounds.y == position && IsWindowVisible(original[i]), "Visible native child has one preceding gap");
                RECT actual{}; GetWindowRect(original[i], &actual);
                MapWindowPoints(nullptr, hwnd, reinterpret_cast<POINT*>(&actual), 2);
                require(actual.top == std::lround(bounds.y * scale) &&
                    actual.bottom - actual.top == std::lround(bounds.height * scale),
                    "Actual native child geometry matches collapsed layout");
                position += bounds.height; previous = true;
            }
            require(flexible[0]->bounds().width == 95 && flexible[2]->bounds().width == 95 &&
                flexible[2]->bounds().x - flexible[0]->bounds().x == 105 &&
                flexible[1]->bounds().width == 0 && flexible[1]->bounds().height == 0,
                "Hidden native flex child consumes neither weight nor spacing");
            require(after->bounds().y - before->bounds().y == 40, "Visible empty Stack still participates in spacing");
        }
        window.stack_move(*group, *children[0], 2);
        require(group->child_at(0) == children[1] && group->child_at(2) == children[0],
            "Native move retains authored child ordering");
        children[2]->set_visible(false); flush(hwnd);
        require(children[0]->bounds().y - children[1]->bounds().y == 40 && group->bounds().height == 60,
            "Reordered hidden middle child adds no gap");
        for (std::size_t i = 0; i < children.size(); ++i)
            require(child(hwnd, children[i]->name().c_str()) == original[i], "Reorder and visibility keep native peers");
        ran = true; window.close();
    });
    const auto result = Application::run(window);
    if (result) std::wcerr << window.error() << L'\n';
    require(result == 0 && ran, "Hidden Stack native geometry completes");
}
}
int main() {
    try {
        image_fit_fill();
        axis_native_sizing();
        hidden_stack_spacing();
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
