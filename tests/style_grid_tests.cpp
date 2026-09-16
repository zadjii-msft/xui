#include "xui/data_grid.hpp"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string_view>

#ifdef _WIN32
#include "xui/application.hpp"
#include "../src/drawing.hpp"
#include "../src/control_accessibility.hpp"
#endif

namespace allocations {
thread_local bool active{};
thread_local std::size_t calls{};
}
void* operator new(std::size_t size) {
    if (auto* p = std::malloc(size ? size : 1)) {
        if (allocations::active) ++allocations::calls;
        return p;
    }
    throw std::bad_alloc{};
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

#ifdef _WIN32
namespace xui {
IRawElementProviderSimple* create_grid_provider(std::shared_ptr<ControlAccessibility> state);
}
#endif
namespace {
using namespace xui;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F action) {
    try { action(); } catch (const std::invalid_argument&) { return; }
    throw std::runtime_error("Invalid style must be rejected");
}
class Source final : public GridSource {
public:
    explicit Source(std::size_t count) : count_(count) {}
    std::size_t size() const override { return count_; }
    RowKey key(std::size_t row) const override { return {row + 100, 7}; }
    std::optional<std::size_t> find(RowKey key) const override {
        return key.version == 7 && key.id >= 100 && key.id - 100 < count_ ? std::optional<std::size_t>(key.id - 100) : std::nullopt;
    }
    std::wstring text(std::size_t, std::size_t) const override { return L"Cell"; }
private:
    std::size_t count_;
};
PartStyleValues fill(std::uint32_t color) { PartStyleValues result; result.background = ThemeColor{color}; return result; }
PartStyleValues foreground(std::uint32_t color) { PartStyleValues result; result.foreground = ThemeColor{color}; return result; }
std::shared_ptr<const ControlStyle> grid_style() {
    PartStyleValues root; root.row_height = 44; root.header_height = 52; root.padding = Insets{7, 9, 11, 13};
    PartStyleValues scrollbar; scrollbar.width = 20;
    return ControlStyle::create(StyleTarget::data_grid,
        {{StylePart::root, root}, {StylePart::scrollbar, scrollbar}, {StylePart::row, fill(0x123456)},
         {StylePart::alternating_row, fill(0x654321)}},
        {{StylePart::row, style_states::hovered, fill(0x112233)},
         {StylePart::row, style_states::selected, fill(0x334455)},
         {StylePart::row, style_states::disabled, fill(0x555555)},
         {StylePart::indicator, style_states::checked, fill(0xff0000)},
         {StylePart::indicator, style_states::mixed, fill(0x00ff00)},
         {StylePart::header, style_states::focused, fill(0x010203)},
         {StylePart::sort_icon, style_states::descending, foreground(0x135724)},
         {StylePart::filter_icon, style_states::filtered, foreground(0x246813)},
         {StylePart::filter_icon, style_states::filter_pending, foreground(0xabcdef)},
         {StylePart::reorder_marker, style_states::dragging, foreground(0x987654)}});
}
void styled_transfer_geometry() {
    DataGrid grid;
    const auto source = std::make_shared<Source>(5);
    grid.set_columns({{L"Name", 160}});
    grid.set_source(source);
    grid.set_control_style(grid_style());
    grid.arrange({30, 40, 250, 230});
    const auto view = grid.geometry().viewport();
    const Point row{view.x + 5, view.y + 5};
    std::optional<RowKey> key;
    require(grid.file_drop_hit(row, key) && key == source->key(0),
        "File-drop lookup uses the styled row viewport in local coordinates");
    require(!grid.file_drop_hit({view.x - 1, row.y}, key) &&
        !grid.file_drop_hit({row.x, view.y - 1}, key) &&
        !grid.file_drop_hit({view.x + view.width, row.y}, key),
        "Authored padding, headers, and scrollbars reject file drops");
    grid.select(source->key(0), false);
    grid.select(source->key(1), SelectionGesture::toggle, false);
    grid.prepare_context_menu(row);
    require(grid.selection().contains(source->key(0)) && grid.selection().contains(source->key(1)),
        "A context menu on a styled selected row preserves multiple selection");
}
void states_and_geometry() {
    DataGrid grid;
    grid.set_columns({{L"Same", 120, false, true, true}, {L"Same", 160, false, true}});
    const auto source = std::make_shared<Source>(1000000);
    grid.set_source(source);
    grid.arrange({0, 0, 250, 230});
    grid.set_control_style(grid_style());
    grid.arrange(grid.bounds());
    const auto g = grid.geometry();
    require(g.left == 7 && g.top == 9 && g.viewport_width() == 212 && g.viewport_height() == 136,
        "Root padding and scroll metrics define one viewport");
    require(g.row_height == 44 && g.header_height == 52 && grid.row_at(61) == 0 && !grid.row_at(60),
        "Styled header and row sizes govern hit testing");
    require(grid.maximum_offset() == 44000000 - 136 && grid.maximum_horizontal() == 68,
        "Styled sizes govern source extent and scroll ranges");
    grid.hover_pointer(Point{20, 70});
    grid.pointer_move(true);
    require(grid.row_style(StylePart::row, 0).background->light == 0x112233 &&
        grid.row_style(StylePart::row, 2).background->light == 0x123456,
        "Whole-grid pointer state does not hover all rows");
    require(grid.row_style(StylePart::row, 1).background->light == 0x654321, "Alternating rows resolve transiently");
    grid.select(source->key(2), false);
    grid.set_focused(true);
    require((grid.row_style_states(2) & style_states::focused) && !(grid.row_style_states(0) & style_states::focused),
        "Only the focused stable row key acquires focus");
    require(grid.row_style(StylePart::indicator, 2).background->light == 0xff0000,
        "Row checks use selection membership");
    grid.focus_header(true); grid.step_header(1);
    require(!(grid.row_style_states(2) & style_states::focused) &&
        (grid.header_style_states(1) & style_states::focused) && !(grid.header_style_states(0) & style_states::focused),
        "Header focus belongs to a single source column");
    require(grid.header_style(StylePart::indicator, 0).background->light == 0x00ff00, "Header mixed check state is model-owned");
    grid.set_sort(1, true);
    require(grid.header_style(StylePart::sort_icon, 1).foreground->light == 0x135724 &&
        !(grid.header_style_states(0) & style_states::sorted), "Sorting uses source column identity");
    grid.set_filter(1, L"filter");
    require(grid.header_style(StylePart::filter_icon, 1).foreground->light == 0x246813, "Filtered state follows filter text");
    std::optional<GridFilterRequest> pending;
    grid.on_filter([&](GridFilterRequest request) { pending = request; });
    grid.filter(1, L"pending");
    require(grid.header_style(StylePart::filter_icon, 1).foreground->light == 0xabcdef &&
        !(grid.header_style_states(0) & style_states::filter_pending), "Pending state belongs only to the request column");
    grid.cancel();
    require(pending->cancellation.stop_requested() && !grid.complete_filter(*pending, source),
        "Styling preserves filter cancellation");
    grid.set_column_order({1, 0});
    grid.set_offset(88, 40);
    const auto cell = grid.cell_bounds(2, 0);
    require(cell.x == 127 && cell.y == 61 && cell.height == 44 && cell.width == 120,
        "Cell bounds use model rows, source columns, visual order, and styled offsets");
    require(grid.column_at(130) == 1 && grid.source_column(1) == 0 && grid.row_at(62) == 2,
        "Hit testing matches reordered cell geometry");
    const auto filter = grid.header_part_bounds(1, GridHeaderPart::filter);
    require(grid.header_part_at(filter.x + 1) == GridHeaderPart::filter &&
        filter.y == 9 && filter.height == 52, "Filter hit regions use styled header geometry");
    const auto thumb = grid.vertical_thumb(), track = grid.geometry().vertical_track();
    require(thumb.x == track.x + 5 && thumb.width == 10 && thumb.y >= track.y, "Scrollbar width reaches thumb geometry");
    require(grid.header_style_states(1, true, true) & style_states::dragging, "Native column drag supplies transient dragging");
    require(grid.row_style(StylePart::row, 2, false).background->light == 0x555555,
        "Disabled ancestor context overrides interaction colors");
    PartStyleValues local; local.row_height = 48;
    grid.set_control_style_values(StylePart::root, local);
    grid.set_control_style(nullptr);
    require(grid.effective_row_height() == 48, "Local metrics survive definition removal");
    grid.set_control_style(grid_style());
    require(grid.effective_row_height() == 48, "Local metrics survive replacement");
    PartStyleValues invalid; invalid.row_height = 0;
    rejects([&] { grid.set_control_style_values(StylePart::root, invalid); });
    rejects([&] { ControlStyle::create(StyleTarget::data_grid, {}, {{StylePart::root, style_states::pressed, fill(1)}}); });
    rejects([&] { grid.set_control_style_values(StylePart::sort_icon, fill(1)); });
}
void bounded_styles_and_gaps() {
    DataGrid grid;
    grid.set_columns({{L"Value", 150}});
    grid.arrange({0, 0, 220, 200});
    require(!grid.has_control_styling(), "Unstyled grid has no style attachment");
    allocations::calls = 0; allocations::active = true;
    for (int i = 0; i < 1000; ++i) { const auto geometry = grid.geometry(); (void)geometry; }
    allocations::active = false;
    require(allocations::calls == 0 && !grid.has_control_styling(), "Unstyled geometry allocates no style storage");
    grid.set_source(std::make_shared<Source>(1000000));
    const auto definition = grid_style();
    allocations::calls = 0; allocations::active = true;
    grid.set_control_style(definition);
    allocations::active = false;
    const auto large_source_calls = allocations::calls;
    DataGrid small_grid; small_grid.set_source(std::make_shared<Source>(2));
    allocations::calls = 0; allocations::active = true;
    small_grid.set_control_style(definition);
    allocations::active = false;
    require(allocations::calls == large_source_calls, "Style attachment allocation does not depend on source row count");
    grid.hover_pointer(Point{20, 70});
    allocations::calls = 0; allocations::active = true;
    for (std::size_t i = 0; i < 10000; ++i) {
        const auto values = grid.row_style(StylePart::cell, i);
        const auto box = grid.cell_bounds(i, 0);
        (void)values; (void)box;
    }
    allocations::active = false;
    require(allocations::calls == 0, "Transient row styles and geometry allocate no per-source-row storage");
    HistoryChart chart;
    chart.arrange({0, 0, 400, 160});
    PartStyleValues line; line.foreground = ThemeColor{0xff0000, 0x00ff00}; line.thickness = 5;
    chart.set_control_style(ControlStyle::create(StyleTarget::history_chart, {{StylePart::plot, line}},
        {{StylePart::root, style_states::empty, fill(0xabcdef)}}));
    require(chart.empty() && chart.part_style(StylePart::root).background->light == 0xabcdef, "Empty chart state paints a real surface");
    chart.append(20); chart.append({}); chart.append(40);
    require(chart.size() == 3 && chart.at(0) == 20 && !chart.at(1) && chart.at(2) == 40 && !chart.empty(),
        "Style resolution preserves samples and missing-sample gaps");
    chart.set_control_style(nullptr);
    require(chart.at(0) == 20 && !chart.at(1) && chart.at(2) == 40, "Definition removal never rewrites series data");
    const auto before = chart.plot_bounds();
    PartStyleValues root; root.padding = Insets{20, 10, 30, 12};
    chart.set_control_style_values(StylePart::root, root);
    const auto after = chart.plot_bounds();
    require(before.x == 12 && before.y == 38 && before.height == 102 &&
        after.x == 20 && after.y == 46 && after.width == 350 && after.height == 82, "Chart padding reaches title, plot, and caption geometry");
    require(line.foreground->resolve(ThemeMode::light) == 0xff0000 &&
        line.foreground->resolve(ThemeMode::dark) == 0x00ff00, "Chart color resources retain both theme values");
#ifdef _WIN32
    for (const auto mode : {ThemeMode::light, ThemeMode::dark, ThemeMode::high_contrast}) {
        const auto palette = Palette::system(mode);
        const auto color = Drawing::style_foreground(line, palette, palette.accent);
        const auto expected = palette.high_contrast ? palette.accent : D2D1::ColorF(line.foreground->resolve(mode));
        require(color.r == expected.r && color.g == expected.g && color.b == expected.b, "Chart ink follows theme and high-contrast policy");
    }
#endif
}
void uniform_metric_policy() {
    PartStyleValues row_height; row_height.row_height = 60;
    PartStyleValues header_height; header_height.header_height = 64;
    PartStyleValues width; width.width = 24;
    PartStyleValues spacing; spacing.spacing = 8;
    PartStyleValues size; size.size = 22;
    for (const auto part : {StylePart::row, StylePart::alternating_row, StylePart::cell}) {
        rejects([&] { ControlStyle::create(StyleTarget::data_grid, {}, {{part, style_states::selected, row_height}}); });
        rejects([&] { ControlStyle::create(StyleTarget::data_grid, {}, {{part, style_states::hovered, width}}); });
        rejects([&] { ControlStyle::create(StyleTarget::data_grid, {}, {{part, style_states::selected, spacing}}); });
    }
    rejects([&] { ControlStyle::create(StyleTarget::data_grid, {}, {{StylePart::header, style_states::focused, header_height}}); });
    rejects([&] { ControlStyle::create(StyleTarget::data_grid, {}, {{StylePart::indicator, style_states::checked, size}}); });
    rejects([&] { ControlStyle::create(StyleTarget::data_grid, {}, {{StylePart::scrollbar, style_states::dragging, width}}); });

    DataGrid grid;
    grid.set_columns({{L"Value", 120}});
    grid.set_source(std::make_shared<Source>(100));
    grid.arrange({0, 0, 250, 230});
    PartStyleValues disabled; disabled.row_height = 72; disabled.header_height = 80;
    PartStyleValues disabled_scrollbar; disabled_scrollbar.width = 32;
    PartStyleValues hovered; hovered.header_height = 70;
    PartStyleValues padding; padding.padding = Insets{20, 2, 24, 3};
    grid.set_control_style(ControlStyle::create(StyleTarget::data_grid,
        {{StylePart::root, merge_part_values(row_height, header_height)}, {StylePart::scrollbar, width}},
        {{StylePart::root, style_states::disabled, disabled},
         {StylePart::root, style_states::hovered, hovered},
         {StylePart::scrollbar, style_states::disabled, disabled_scrollbar},
         {StylePart::row, style_states::selected, padding}}));
    require(grid.geometry().row_height == 60 && grid.geometry().header_height == 64 && grid.geometry().scrollbar_width == 24,
        "Base metrics configure uniform geometry");
    grid.pointer_move(true);
    grid.arrange(grid.bounds());
    require(grid.geometry().header_height == 70 && grid.row_at(70) == 0, "Owner-hover metrics change actual grid geometry");
    grid.set_control_style_values(StylePart::root, row_height);
    grid.select(grid.source()->key(0), false);
    require(grid.row_style(StylePart::row, 0).padding->left == 20 && !grid.row_style(StylePart::row, 1).padding,
        "State-dependent internal padding remains per row");
    grid.set_enabled(false);
    grid.arrange(grid.bounds());
    require(grid.geometry().row_height == 60 && grid.geometry().header_height == 80 &&
        grid.geometry().scrollbar_width == 32 && grid.vertical_thumb().width == 16 && grid.row_at(80) == 0,
        "Control-wide disabled metrics update geometry while local row height retains precedence");
}
void owner_inheritance_and_item_states() {
    DataGrid grid;
    grid.set_columns({{L"Value", 120}});
    grid.set_source(std::make_shared<Source>(3));
    grid.arrange({0, 0, 250, 230});
    grid.set_control_style(ControlStyle::create(StyleTarget::data_grid, {},
        {{StylePart::root, style_states::hovered, foreground(0xff0000)},
         {StylePart::row, style_states::hovered, foreground(0x00ff00)},
         {StylePart::row, style_states::disabled, foreground(0x555555)}}));
    grid.pointer_move(true);
    require(grid.row_style(StylePart::cell, 0).foreground->light == 0xff0000 &&
        grid.row_style(StylePart::cell, 1).foreground->light == 0xff0000 &&
        grid.header_style(StylePart::header, 0).foreground->light == 0xff0000,
        "Root owner-hover foreground deliberately inherits into all unoverridden child text");
    require(!(grid.row_style_states(0) & style_states::hovered) && !(grid.row_style_states(1) & style_states::hovered),
        "Inherited owner values do not activate per-row hover rules");
    grid.hover_pointer(Point{20, 40});
    require(grid.row_style(StylePart::cell, 0).foreground->light == 0x00ff00 &&
        grid.row_style(StylePart::cell, 1).foreground->light == 0xff0000,
        "Only the hit-tested row overrides inherited foreground with its own hover rule");
    require(grid.row_style(StylePart::cell, 0, false).foreground->light == 0x555555,
        "Item disabled context overrides row hover without changing owner state");
    grid.set_enabled(false);
    require(grid.row_style(StylePart::cell, 1).foreground->light == 0x555555,
        "Owner disabled contributes to every transient row");
}
void paragraph_alignment_policy() {
    for (const auto [target, part] : {
        std::pair{StyleTarget::data_grid, StylePart::cell}, std::pair{StyleTarget::data_grid, StylePart::header},
        std::pair{StyleTarget::history_chart, StylePart::title}, std::pair{StyleTarget::history_chart, StylePart::caption}}) {
        PartStyleValues values;
        values.horizontal_alignment = StyleAlignment::stretch;
        for (const auto alignment : {StyleAlignment::start, StyleAlignment::center, StyleAlignment::end}) {
            values.vertical_alignment = alignment;
            validate_part_values(target, part, values);
        }
        values.vertical_alignment = StyleAlignment::stretch;
        rejects([&] { validate_part_values(target, part, values); });
        rejects([&] { ControlStyle::create(target, {{part, values}}, {}); });
        rejects([&] { ControlStyle::create(target, {}, {{part, style_states::disabled, values}}); });
    }
    DataGrid grid;
    PartStyleValues valid; valid.vertical_alignment = StyleAlignment::center;
    grid.set_control_style_values(StylePart::cell, valid);
    PartStyleValues invalid; invalid.vertical_alignment = StyleAlignment::stretch;
    rejects([&] { grid.set_control_style_values(StylePart::cell, invalid); });
    require(grid.control_style_values(StylePart::cell).vertical_alignment == StyleAlignment::center,
        "Unsupported paragraph alignment rejects without replacing local values");
}
#ifdef _WIN32
void desktop_pixels_and_provider() {
    for (const auto mode : {ThemeMode::light, ThemeMode::dark, ThemeMode::high_contrast}) {
        const auto title = L"XUI styled grid chart tests";
        Window window({title, {500, 480}});
        window.set_theme(mode);
        auto stack = std::make_shared<Stack>(Axis::vertical);
        auto grid = std::make_shared<DataGrid>(L"Styled grid");
        grid->set_columns({{L"A", 160, false, true, true}, {L"B", 160}});
        grid->set_source(std::make_shared<Source>(1000000));
        grid->set_control_style(grid_style());
        auto chart = std::make_shared<HistoryChart>(L"Styled history");
        PartStyleValues plot; plot.foreground = ThemeColor{0xff0000}; plot.thickness = 6;
        PartStyleValues no_grid; no_grid.thickness = 0;
        chart->set_control_style(ControlStyle::create(StyleTarget::history_chart,
            {{StylePart::root, fill(0x000000)}, {StylePart::plot, plot}, {StylePart::grid_line, no_grid}}, {}));
        for (std::size_t i = 0; i < HistoryChart::capacity; ++i) chart->append(i == 30 ? std::nullopt : std::optional<double>(50));
        stack->add(grid, 1); stack->add(chart); window.set_content(stack);
        require(window.post([&] {
            const auto hwnd = FindWindowW(L"Xui.Window.1", title);
            const auto grid_hwnd = FindWindowExW(hwnd, nullptr, L"Xui.Control.1", L"Styled grid");
            const auto chart_hwnd = FindWindowExW(hwnd, nullptr, L"Xui.Control.1", L"Styled history");
            DWORD pid{}; GetWindowThreadProcessId(hwnd, &pid);
            require(pid == GetCurrentProcessId() && grid_hwnd && chart_hwnd, "Pixel test uses only owned windows");
            const auto repaint = [&] {
                SendMessageW(hwnd, WM_APP + 12, 0, 0);
                RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
            };
            repaint();
            const auto sample = [&](HWND peer, float x, float y) {
                const auto dpi = GetDpiForWindow(peer);
                POINT p{static_cast<LONG>(std::lround(x * dpi / 96)), static_cast<LONG>(std::lround(y * dpi / 96))};
                MapWindowPoints(peer, hwnd, &p, 1);
                const auto dc = GetDC(hwnd); const auto result = GetPixel(dc, p.x, p.y); ReleaseDC(hwnd, dc);
                require(result != CLR_INVALID, "Read an owned rendered pixel"); return result;
            };
            const auto view = grid->geometry().viewport();
            const auto palette = Palette::system(mode);
            const auto rgb = [](D2D1_COLOR_F c) { return RGB(std::lround(c.r * 255), std::lround(c.g * 255), std::lround(c.b * 255)); };
            require(sample(grid_hwnd, view.x + view.width - 8, view.y + 20) == (palette.high_contrast ? rgb(palette.background) : RGB(0x12, 0x34, 0x56)) &&
                sample(grid_hwnd, view.x + view.width - 8, view.y + 64) == (palette.high_contrast ? rgb(palette.surface) : RGB(0x65, 0x43, 0x21)),
                "Styled row and alternating-row fills are drawn");
            const auto bounds = chart->plot_bounds();
            const auto line_color = palette.high_contrast ? rgb(palette.accent) : RGB(255, 0, 0);
            require(sample(chart_hwnd, bounds.x + bounds.width * 10 / 59, bounds.y + bounds.height / 2) == line_color,
                "Authored plot color reaches line pixels");
            require(sample(chart_hwnd, bounds.x + bounds.width * 30 / 59, bounds.y + bounds.height / 2) == (palette.high_contrast ? rgb(palette.surface) : RGB(0, 0, 0)),
                "Missing sample remains a rendered gap");
            require(sample(chart_hwnd, bounds.x + bounds.width * 10 / 59, bounds.y + bounds.height / 2 + 2) == line_color,
                "Authored thickness reaches line pixels");
            auto state = std::make_shared<ControlAccessibility>();
            Microsoft::WRL::ComPtr<IRawElementProviderSimple> provider; provider.Attach(create_grid_provider(state));
            publish_control(state, provider.Get(), *grid, grid_hwnd);
            Microsoft::WRL::ComPtr<IGridProvider> table; require(SUCCEEDED(provider.As(&table)), "Grid provider exposes table identity");
            Microsoft::WRL::ComPtr<IRawElementProviderSimple> cell;
            require(SUCCEEDED(table->GetItem(1, 0, &cell)), "Virtual cell provider exists without a row visual");
            Microsoft::WRL::ComPtr<IRawElementProviderFragment> fragment; require(SUCCEEDED(cell.As(&fragment)), "Virtual cell has fragment identity");
            SAFEARRAY* before{}; require(SUCCEEDED(fragment->GetRuntimeId(&before)), "Read initial cell runtime identity");
            grid->set_column_order({1, 0}); grid->set_offset(20, 0); repaint();
            publish_control(state, provider.Get(), *grid, grid_hwnd);
            Microsoft::WRL::ComPtr<IGridItemProvider> item; require(SUCCEEDED(cell.As(&item)), "Virtual cell retains grid-item provider");
            int column{}; require(SUCCEEDED(item->get_Column(&column)) && column == 1, "Existing provider follows source identity through reorder");
            SAFEARRAY* after{}; require(SUCCEEDED(fragment->GetRuntimeId(&after)), "Read reordered cell runtime identity");
            for (LONG i = 0; i < 9; ++i) {
                int a{}, b{}; SafeArrayGetElement(before, &i, &a); SafeArrayGetElement(after, &i, &b);
                require(a == b, "Provider runtime identity does not depend on visual column position");
            }
            SafeArrayDestroy(before); SafeArrayDestroy(after);
            UiaRect actual{}; require(SUCCEEDED(fragment->get_BoundingRectangle(&actual)) && actual.width > 0, "Provider uses styled visible geometry");
            disconnect_control(state, provider.Get());
            window.close();
        }), "Queue native style checks");
        require(Application::run(window) == 0, "Styled grid/chart window completes");
    }
}
#endif
}
int main(int argc, char** argv) {
    try {
        const bool desktop = argc > 1 && std::string_view(argv[1]) == "--desktop";
#ifndef _WIN32
        if (desktop) throw std::invalid_argument("--desktop requires Windows");
#endif
        styled_transfer_geometry(); states_and_geometry(); bounded_styles_and_gaps(); uniform_metric_policy(); owner_inheritance_and_item_states(); paragraph_alignment_policy();
#ifdef _WIN32
        if (desktop) desktop_pixels_and_provider();
#endif
        std::cout << "Grid and chart style tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        allocations::active = false;
        std::cerr << error.what() << '\n'; return 1;
    }
}
