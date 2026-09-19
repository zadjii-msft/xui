#if defined(_WIN32) && !defined(NOMINMAX)
#define NOMINMAX
#endif
#include "collections_fixture.hpp"
#include "xui/file_list.hpp"
#include "xui/navigation.hpp"
#ifdef _WIN32
#include "../src/drawing.hpp"
#include "../src/images.hpp"
#include "../src/control_accessibility.hpp"
#include <UIAutomation.h>
#endif
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <new>
#include <string_view>

namespace allocation_probe {
thread_local bool active{};
thread_local std::size_t bytes{}, calls{};
}
void* operator new(std::size_t size) {
    if (auto* value = std::malloc(size ? size : 1)) {
        if (allocation_probe::active) { allocation_probe::bytes += size; ++allocation_probe::calls; }
        return value;
    }
    throw std::bad_alloc{};
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* value) noexcept { std::free(value); }
void operator delete[](void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }
void operator delete[](void* value, std::size_t) noexcept { std::free(value); }

#ifdef _WIN32
namespace xui {
struct DrawingTestAccess {
    static void software_target(Drawing& drawing, HWND window) {
        auto result = drawing.factory_->CreateHwndRenderTarget(
            D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_SOFTWARE,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
                96, 96, D2D1_RENDER_TARGET_USAGE_GDI_COMPATIBLE),
            D2D1::HwndRenderTargetProperties(window, D2D1::SizeU(320, 180)), &drawing.target_);
        if (FAILED(result)) throw std::runtime_error("Create collection software target");
        ++Drawing::live_targets_;
        if (FAILED(drawing.target_->CreateSolidColorBrush(D2D1::ColorF(0), &drawing.brush_)))
            throw std::runtime_error("Create collection software brush");
    }
    static uint32_t pixel(Drawing& drawing, int x, int y) {
        Microsoft::WRL::ComPtr<ID2D1GdiInteropRenderTarget> interop;
        if (FAILED(drawing.target_.As(&interop))) throw std::runtime_error("Read collection target");
        HDC dc{};
        if (FAILED(interop->GetDC(D2D1_DC_INITIALIZE_MODE_COPY, &dc))) throw std::runtime_error("Read collection pixels");
        const auto pixel = GetPixel(dc, x, y);
        const RECT unchanged{};
        interop->ReleaseDC(&unchanged);
        return (uint32_t(GetRValue(pixel)) << 16) | (uint32_t(GetGValue(pixel)) << 8) | GetBValue(pixel);
    }
};
}
#endif

namespace {
using namespace xui;
using collections_test::require;
PartStyleValues ink(uint32_t value) { PartStyleValues result; result.foreground = ThemeColor{value}; return result; }
PartStyleValues fill(uint32_t light, uint32_t dark) {
    PartStyleValues result; result.background = ThemeColor{light, dark}; return result;
}
template<class F> void rejects(F action) {
    try { action(); } catch (const std::invalid_argument&) { return; }
    throw std::runtime_error("Unsupported collection style must fail");
}
void sparse_state_and_locals() {
    ItemsView view;
    auto source = std::make_shared<collections_test::Items>(100000);
    view.set_items(source); view.arrange({0, 0, 600, 280});
    require(!view.has_control_styling(), "Default collection has no style attachment");
    const auto style = ControlStyle::create(StyleTarget::items_view, {{StylePart::root, ink(10)}},
        {{StylePart::row, style_states::selected, ink(20)},
         {StylePart::row, style_states::hovered, ink(30)},
         {StylePart::row, style_states::disabled, ink(40)}});
    view.set_control_style(style);
    const auto key = source->key(1);
    view.select(key); view.set_focused(true); view.pointer_move(true);
    auto rows = view.visible_content();
    const auto state = collection_row_style_state(rows[1], true, true, true, false);
    require((state & style_states::selected) && (state & style_states::focused) && !(state & style_states::hovered),
        "Container hover does not spread across rows");
    require(view.resolve_control_style_part(StylePart::primary_text, state).foreground == ThemeColor{20},
        "Primary text inherits selected row foreground");
    require(view.resolve_control_style_part(StylePart::primary_text, 0).foreground == ThemeColor{10},
        "Other rows inherit root instead of focused item state");
    view.set_control_style_values(StylePart::row, ink(50));
    require(view.resolve_control_style_part(StylePart::primary_text, state).foreground == ThemeColor{50},
        "Row local foreground inherits into primary text");
    view.set_control_style_values(StylePart::primary_text, ink(60));
    view.set_control_style(nullptr);
    require(view.resolve_control_style_part(StylePart::primary_text, state).foreground == ThemeColor{60}, "Locals survive style removal");
    view.set_control_style_values(StylePart::primary_text, {});
    require(view.resolve_control_style_part(StylePart::primary_text, state).foreground == ThemeColor{50}, "Clear exposes row local");
    view.set_control_style_values(StylePart::row, {});
    require(!view.has_control_styling(), "Clearing styles and locals releases attachment");
    view.set_control_style(style);
    rows[0].content.enabled = false; rows[0].content.checked = true;
    const auto disabled = collection_row_style_state(rows[0], true, false, true, true);
    require((disabled & style_states::disabled) && (disabled & style_states::checked) && !(disabled & style_states::hovered),
        "Item disabled suppresses pointer state but preserves checked");
    require(view.resolve_control_style_part(StylePart::primary_text, disabled).foreground == ThemeColor{40},
        "Disabled rule has final priority");
    view.set_enabled(false);
    require(view.resolve_control_style_part(StylePart::primary_text, style_states::hovered).foreground == ThemeColor{40},
        "Global disabled applies to transient rows");
    require(view.source() == source && view.selection().focused() == key, "Styles retain source and stable focus identity");
    view.set_enabled(true); view.set_presentation(ItemsPresentation::tiles);
    view.set_control_style_values(StylePart::tile, ink(70));
    const auto tile = view.visible_content().front();
    require(view.row_style_values(StylePart::icon, tile, 0).foreground == ThemeColor{70} &&
        view.row_style_values(StylePart::primary_text, tile, 0).foreground == ThemeColor{70},
        "Tile-local foreground inherits into icon and primary text");
    view.set_presentation(ItemsPresentation::gallery);
    require(view.row_style_values(StylePart::icon, tile, 0).foreground == ThemeColor{70},
        "Gallery shares tile style inheritance");
    view.set_control_style_values(StylePart::primary_text, ink(80));
    require(view.row_style_values(StylePart::primary_text, tile, 0).foreground == ThemeColor{80},
        "Text local overrides tile inheritance");
    view.set_control_style_values(StylePart::primary_text, {});
    view.set_control_style_values(StylePart::tile, {});
    require(view.row_style_values(StylePart::primary_text, tile, 0).foreground == ThemeColor{10},
        "Clearing tile and text locals exposes inherited root color");
    rejects([] { ControlStyle::create(StyleTarget::file_list, {}, {{StylePart::row, style_states::pressed, {}}}); });
    rejects([] { ControlStyle::create(StyleTarget::items_view, {}, {{StylePart::root, style_states::pressed, {}}}); });
    PartStyleValues metric; metric.row_height = 80;
    rejects([&] { validate_part_values(StyleTarget::items_view, StylePart::row, metric); });
    PartStyleValues width; width.width = 200;
    rejects([&] { validate_part_values(StyleTarget::items_view, StylePart::root, width); });
    const auto& schema = control_style_schema(StyleTarget::items_view);
    const auto tile_schema = std::find_if(schema.parts.begin(), schema.parts.end(),
        [](const auto& part) { return part.part == StylePart::tile; });
    require(tile_schema != schema.parts.end() && (tile_schema->allowed & style_property(StyleProperty::width)) &&
        !(tile_schema->state_allowed & style_property(StyleProperty::width)) &&
        (tile_schema->state_allowed & style_property(StyleProperty::padding)),
        "Public schema distinguishes base/local tile width from state-capable internal padding");
    for (const auto state : {style_states::selected, style_states::focused, style_states::hovered,
        style_states::disabled, style_states::checked, style_states::expanded})
        rejects([&] { ControlStyle::create(StyleTarget::items_view, {}, {{StylePart::tile, state, width}}); });
    PartStyleValues internal; internal.padding = Insets{20, 4, 12, 8};
    PartStyleValues typography; typography.font_size = 24;
    const auto supported = ControlStyle::create(StyleTarget::items_view, {{StylePart::tile, width}},
        {{StylePart::tile, style_states::hovered, internal}, {StylePart::primary_text, style_states::hovered, typography}});
    view.set_control_style(supported);
    require(view.row_style_values(StylePart::tile, tile, style_states::hovered).padding->left == 20 &&
        view.row_style_values(StylePart::primary_text, tile, style_states::hovered).font_size == 24,
        "Internal item padding and typography remain available in state rules");
    require(view.item_size().width == 200, "State-independent tile width remains available in base definitions");
}
void paragraph_alignment_contract() {
    for (const auto target : {StyleTarget::file_list, StyleTarget::items_view, StyleTarget::tree_view,
        StyleTarget::navigation_list, StyleTarget::command_menu}) {
        for (const auto& part : control_style_schema(target).parts) {
            if (!(part.allowed & style_property(StyleProperty::vertical_alignment))) continue;
            require(part.limits.vertical_alignments == 7 && part.limits.horizontal_alignments == 15,
                "Collection paragraph schema permits vertical start/center/end and horizontal justification");
            PartStyleValues values;
            values.horizontal_alignment = StyleAlignment::stretch;
            for (const auto alignment : {StyleAlignment::start, StyleAlignment::center, StyleAlignment::end}) {
                values.vertical_alignment = alignment;
                validate_part_values(target, part.part, values);
                ControlStyle::create(target, {{part.part, values}}, {{part.part, style_states::disabled, values}});
            }
            values.vertical_alignment = StyleAlignment::stretch;
            rejects([&] { validate_part_values(target, part.part, values); });
            rejects([&] { ControlStyle::create(target, {{part.part, values}}, {}); });
            rejects([&] { ControlStyle::create(target, {}, {{part.part, style_states::disabled, values}}); });
        }
    }
}
void owner_root_inheritance() {
    const auto check = [](Control& control, StyleTarget target) {
        auto root_values = ink(10);
        root_values.font_family = make_style_font_family("Segoe UI");
        root_values.font_size = 17; root_values.font_weight = 500; root_values.font_style = StyleFontStyle::normal;
        auto root_hover = ink(30); root_hover.font_size = 21;
        control.set_control_style(ControlStyle::create(target, {{StylePart::root, root_values}},
            {{StylePart::root, style_states::focused, ink(20)},
             {StylePart::root, style_states::hovered, root_hover},
             {StylePart::row, style_states::hovered, ink(40)},
             {StylePart::row, style_states::disabled, ink(50)}}));
        control.set_focused(true); control.pointer_move(true);
        require(control.resolve_control_style_part(StylePart::primary_text, 0).foreground == ThemeColor{30} &&
            control.resolve_control_style_part(StylePart::secondary_text, 0).foreground == ThemeColor{30},
            "Deliberate root hover foreground inherits using actual owner state");
        require(control.resolve_control_style_part(StylePart::primary_text, style_states::hovered).foreground == ThemeColor{40},
            "Actual row hover overrides inherited root foreground only for that row");
        require(control.resolve_control_style_part(StylePart::primary_text, 0).foreground == ThemeColor{30},
            "Resolving one hovered row does not activate hover rules on another row");
        const auto inherited_font = control.resolve_control_style_part(StylePart::primary_text, 0);
        require(inherited_font.font_family == root_values.font_family && inherited_font.font_size == 21 &&
            inherited_font.font_weight == 500 && inherited_font.font_style == StyleFontStyle::normal,
            "Text inherits all four root font fields using actual owner state");
        PartStyleValues text_local; text_local.font_weight = 700; text_local.font_style = StyleFontStyle::italic;
        control.set_control_style_values(StylePart::primary_text, text_local);
        const auto explicit_font = control.resolve_control_style_part(StylePart::primary_text, 0);
        require(explicit_font.font_family == root_values.font_family && explicit_font.font_size == 21 &&
            explicit_font.font_weight == 700 && explicit_font.font_style == StyleFontStyle::italic &&
            control.resolve_control_style_part(StylePart::secondary_text, 0).font_weight == 500,
            "Explicit destination font fields override inheritance without changing sibling text");
        control.set_control_style_values(StylePart::primary_text, ink(60));
        require(control.resolve_control_style_part(StylePart::primary_text, 0).foreground == ThemeColor{60},
            "Explicit text local overrides inherited owner-root interaction color");
        control.set_control_style_values(StylePart::primary_text, {});
        control.pointer_move(false);
        require(control.resolve_control_style_part(StylePart::primary_text, 0).foreground == ThemeColor{20},
            "Root focus foreground still inherits after owner hover leaves");
        control.set_focused(false);
        require(control.resolve_control_style_part(StylePart::primary_text, 0).foreground == ThemeColor{10},
            "Clearing owner interaction restores root base foreground");
        require(control.resolve_control_style_part(StylePart::primary_text, style_states::disabled).foreground == ThemeColor{50},
            "Item disabled combines independently with an enabled owner");
        control.set_enabled(false);
        require(control.resolve_control_style_part(StylePart::primary_text, 0).foreground == ThemeColor{50},
            "Owner disabled combines with otherwise normal row state");
        control.set_control_style(nullptr);
        control.set_control_style_values(StylePart::root, root_values);
        require(control.resolve_control_style_part(StylePart::primary_text, 0).font_family == root_values.font_family &&
            control.resolve_control_style_part(StylePart::primary_text, 0).font_size == 17,
            "Local-only root typography inherits without an attached definition");
        control.set_control_style_values(StylePart::root, {});
        require(!control.has_control_styling(), "Clearing the last local typography source releases its attachment");
    };
    FileList files; ItemsView items; TreeView tree; CommandMenu menu; NavigationView navigation;
    check(files, StyleTarget::file_list); check(items, StyleTarget::items_view); check(tree, StyleTarget::tree_view);
    check(menu, StyleTarget::command_menu); check(*navigation.items(), StyleTarget::navigation_list);
}
void geometry() {
    auto source = std::make_shared<collections_test::Items>(100000);
    ItemsView view; view.set_items(source); view.arrange({0, 0, 640, 280});
    PartStyleValues root; root.row_height = 70;
    root.padding = Insets{10, 12, 14, 16}; root.border_thickness = Insets{1, 2, 3, 4};
    PartStyleValues tile; tile.width = 200;
    view.set_control_style(ControlStyle::create(StyleTarget::items_view, {{StylePart::tile, tile}}, {}));
    PartStyleValues scrollbar; scrollbar.width = 20;
    view.set_control_style_values(StylePart::root, root);
    view.set_control_style_values(StylePart::scrollbar, scrollbar);
    const auto viewport = view.content_viewport(), first = view.item_bounds(0);
    require(viewport.x == 11 && viewport.y == 14 && viewport.width == 592 && viewport.height == 246, "Root insets reserve virtual viewport");
    require(first.x == viewport.x && first.y == viewport.y && first.height == 70 && first.width == viewport.width,
        "Rows use the content viewport");
#ifdef _WIN32
    auto accessibility = std::make_shared<ControlAccessibility>();
    publish_control(accessibility, nullptr, view, nullptr);
    const auto& snapshot = accessibility->snapshot;
    require(snapshot.collection == source && snapshot.collection_item_height == first.height &&
        snapshot.collection_viewport_x == first.x && snapshot.collection_viewport_y == first.y &&
        snapshot.collection_width == first.width && snapshot.collection_height == viewport.height,
        "UIA publishes identical metrics and retains the immutable provider source");
#endif
    require(view.hit_test({12, 85}) == 1 && !view.hit_test({10, 85}) && !view.hit_test({605, 85}), "Hit test excludes padding and scrollbar");
    require(view.maximum_offset() == source->size() * 70.0 - 246, "Scroll extent uses styled row height");
    view.set_offset(view.maximum_offset());
    require(view.visible_items().end == source->size(), "Styled final row is visible");
    view.set_presentation(ItemsPresentation::tiles);
    require(view.columns() == 2 && view.item_size().width == 200, "Tile width controls column count");
    view.set_offset(0);
    require(view.hit_test({first.x + 300, first.y + 1}) == 1, "Tile hit testing uses the same width");
    require(view.item_bounds(2).y == viewport.y + 70, "Tile row origins use the same height");
    tile.width = 150; view.set_control_style_values(StylePart::tile, tile);
    require(view.columns() == 3 && view.item_size().width == 150, "Tile local width updates uniform columns");
    view.set_control_style_values(StylePart::tile, {});
    require(view.columns() == 2 && view.item_size().width == 200, "Clearing tile width restores its base definition");
    root.row_height = 35; view.set_control_style_values(StylePart::root, root);
    require(view.item_bounds(2).height == 35, "Style metric replacement updates geometry immediately");
    view.set_control_style_values(StylePart::root, {}); view.set_control_style_values(StylePart::scrollbar, {});
    view.set_control_style(nullptr);
    require(view.item_size().height == 56 && view.scrollbar_width() == VirtualCollection::bar_width, "Clear restores model metrics");
    PartStyleValues disabled_height; disabled_height.row_height = 40;
    view.set_control_style(ControlStyle::create(StyleTarget::items_view, {},
        {{StylePart::root, style_states::disabled, disabled_height}}));
    view.set_enabled(false);
    require(view.item_size().height == 40 && view.item_bounds(0).height == 40,
        "Control-wide disabled row height updates actual uniform geometry");
    view.set_enabled(true);
    require(view.item_size().height == 56, "Control-wide metric state clears consistently");

    FileList files; files.set_items(std::make_shared<const std::vector<FileItem>>(
        std::vector<FileItem>{{1, L"A", L"A", false}, {2, L"B", L"B", true}}));
    files.set_viewport_height(100); root.row_height = 50;
    files.set_control_style_values(StylePart::root, root); files.set_control_style_values(StylePart::scrollbar, scrollbar);
    require(files.row_height() == 50 && files.content_height() == 66, "File rows share style metrics with scroll geometry");
    files.select(1);
    require(files.offset() == 34 && files.model().selected_id() == 2, "File reveal uses content viewport and stable ItemId");
}
void hierarchy_and_commands() {
    ItemsView groups; auto source = std::make_shared<collections_test::Items>(10);
    groups.set_items(source); groups.set_presentation(ItemsPresentation::grouped); groups.arrange({0, 0, 400, 300});
    groups.set_control_style(ControlStyle::create(StyleTarget::items_view, {{StylePart::root, ink(11)}},
        {{StylePart::group_header, style_states::expanded, ink(77)},
         {StylePart::root, style_states::hovered, ink(12)}}));
    groups.pointer_move(true);
    const auto group_key = source->groups().front().key;
    groups.reveal(group_key);
    auto header = groups.visible_content().front();
    require(header.key == group_key, "Expansion styling fixture reveals the source group identity");
    require(header.group && header.expanded && groups.resolve_control_style_part(StylePart::group_header,
        collection_row_style_state(header, false, true, true)).foreground == ThemeColor{77}, "Group expansion supplies header state");
    const auto header_state = collection_row_style_state(header, false, true, true);
    require(groups.row_style_values(StylePart::root, header, header_state).foreground == ThemeColor{12} &&
        groups.row_style_values(StylePart::primary_text, header, header_state).foreground == ThemeColor{77},
        "Expanded header rendering keeps owner root state separate from row foreground inheritance");
    require(groups.disclose(group_key, false), "Collapse the styled source group");
    const auto collapsed_header = groups.visible_content().front();
    require(collapsed_header.key == group_key && !collapsed_header.expanded, "Style cannot reopen a collapsed group");
    require(!(collection_row_style_state(collapsed_header, false, true, true) & style_states::expanded) &&
        groups.resolve_control_style_part(StylePart::group_header,
            collection_row_style_state(collapsed_header, false, true, true)).foreground != ThemeColor{77},
        "Collapsed group removes the expanded state and its authored foreground");

    TreeView tree; tree.set_tree(std::make_shared<collections_test::Tree>()); tree.arrange({0, 0, 400, 240});
    TreeRequest request{}; tree.on_request([&](TreeRequest value) { request = value; });
    tree.disclose({1, 1}, true);
    auto row = tree.visible_content().front();
    require(collection_row_style_state(row, false, false, true) & style_states::loading, "Pending branch supplies loading");
    tree.complete(request, {}, L"Denied");
    row = tree.visible_content().front();
    require(row.error && !row.pending && (collection_row_style_state(row, false, false, true) & style_states::error), "Tree failure supplies error");
    PartStyleValues secondary_font; secondary_font.font_size = 19; secondary_font.font_weight = 600;
    tree.set_control_style_values(StylePart::secondary_text, secondary_font);
    require(tree.resolve_control_style_part(StylePart::pending, style_states::loading).font_size == 19 &&
        tree.resolve_control_style_part(StylePart::error, style_states::error).font_weight == 600,
        "Tree status text inherits the secondary text typography source");
    PartStyleValues pending_font; pending_font.font_size = 23;
    tree.set_control_style_values(StylePart::pending, pending_font);
    require(tree.resolve_control_style_part(StylePart::pending, style_states::loading).font_size == 23 &&
        tree.resolve_control_style_part(StylePart::error, style_states::error).font_size == 19,
        "Explicit pending typography does not change error text");
    tree.set_control_style_values(StylePart::pending, {});
    require(tree.resolve_control_style_part(StylePart::pending, style_states::loading).font_size == 19,
        "Clearing pending typography restores its secondary source");
    PartStyleValues indent; indent.indentation = 36;
    tree.set_control_style_values(StylePart::root, indent);
    PartStyleValues padding; padding.padding = Insets{18, 3, 2, 4};
    tree.set_control_style_values(StylePart::row, padding);
    row = tree.visible_content().front();
    const auto disclosure = tree.disclosure_bounds(row, true);
    require(tree.disclosure_hit(0, {disclosure.x + 1, disclosure.y + 1}) &&
        !tree.disclosure_hit(0, {row.bounds.x + 1, disclosure.y + 1}), "Disclosure geometry and hit testing share row padding");
    tree.disclose({1, 1}, true); const auto stale = request; tree.disclose({1, 1}, false);
    require(!tree.complete(stale, source), "Style changes do not alter cancellation");

    NavigationView nav;
    NavigationItem parent{{1, 1}, {}, L"Parent"}, child{{2, 1}, ItemKey{1, 1}, L"Child"};
    nav.set_items({parent, child}); nav.select(child.key); nav.set_item_expanded(parent.key, false); nav.set_expanded(false);
    nav.items()->arrange({0, 0, 64, 200});
    const auto navigation = nav.items()->visible_content().front();
    const auto state = collection_row_style_state(navigation, false, false, true);
    require((state & style_states::selected_descendant) && (state & style_states::compact),
        "Navigation uses actual selected descendant and compact model state");
    nav.items()->set_control_style_values(StylePart::badge, ink(15));
    PartStyleValues compact; compact.row_height = 28; compact.font_size = 12; compact.indentation = 12;
    PartStyleValues icon; icon.size = 16;
    nav.items()->set_control_style(ControlStyle::create(StyleTarget::navigation_list,
        {{StylePart::root, compact}, {StylePart::icon, icon}}, {}));
    require(nav.items()->item_bounds(0).height == 28 &&
        nav.items()->resolve_control_style_part(StylePart::primary_text, 0).font_size == 12 &&
        nav.items()->resolve_control_style_part(StylePart::icon, 0).size == 16,
        "Navigation styles control row geometry, text, and icon size together");
    nav.items()->set_control_style(nullptr);
    require(nav.items()->item_bounds(0).height == 40 &&
        !nav.items()->resolve_control_style_part(StylePart::icon, 0).size,
        "Clearing compact metrics restores the default navigation geometry");

    CommandRecord command{1, 0, L"Disabled", [] {}}; command.enabled = false; command.checked = true;
    CommandRecord submenu{2, 0, L"More"}; submenu.kind = CommandKind::submenu;
    CommandRecord section{3, 0, L"Section"}; section.kind = CommandKind::section;
    CommandRecord action{4, 0, L"Run", [] {}};
    CommandMenu menu; menu.set_commands(std::make_shared<CommandSet>(std::vector{command, submenu, section, action}));
    menu.arrange({0, 0, 400, 300}); menu.set_expanded(2);
    const auto rows = menu.visible_content();
    require((collection_row_style_state(rows[0], false, false, true, true) & (style_states::disabled | style_states::checked)) ==
        (style_states::disabled | style_states::checked), "Menu disabled and checked remain separate");
    require(collection_row_style_state(rows[1], false, false, true) & style_states::open, "Menu open uses expanded command identity");
    menu.set_control_style(ControlStyle::create(StyleTarget::command_menu, {{StylePart::root, ink(91)}},
        {{StylePart::root, style_states::hovered, ink(92)}}));
    menu.pointer_move(true);
    require(rows[2].group && menu.row_style_values(StylePart::group_header, rows[2],
        collection_row_style_state(rows[2], false, false, true, true)).foreground == ThemeColor{92},
        "Hovered menu section masks unsupported part states while retaining owner root inheritance");
    const auto before = menu.source();
    PartStyleValues height; height.row_height = 60; menu.set_control_style_values(StylePart::root, height);
    require(menu.source() == before && menu.source()->key(1) == ItemKey{2, 1}, "Menu styles retain command snapshot/provider keys");
    require(menu.item_bounds(1).y == 60 && menu.item_bounds(2).height == 32 && menu.hit_test({10, 121}) == 2,
        "Menu fixed section decoration and styled action rows share geometry");
}
void allocation_contract() {
    PartStyleValues typography; typography.font_family = make_style_font_family("Segoe UI");
    typography.font_size = 18; typography.font_weight = 600; typography.font_style = StyleFontStyle::italic;
    const auto style = ControlStyle::create(StyleTarget::items_view, {{StylePart::row, ink(3)}, {StylePart::root, typography}},
        {{StylePart::row, style_states::selected, ink(9)}});
    const auto measure = [&](std::size_t count) {
        ItemsView view; auto source = std::make_shared<collections_test::Items>(count);
        view.set_items(source); view.arrange({0, 0, 600, 280}); view.set_control_style(style);
        view.set_presentation(ItemsPresentation::tiles);
        auto rows = view.visible_content();
        allocation_probe::bytes = allocation_probe::calls = 0; allocation_probe::active = true;
        for (int frame = 0; frame < 100; ++frame) for (const auto& row : rows) {
            const auto values = view.row_style_values(StylePart::primary_text, row,
                collection_row_style_state(row, frame % 2 == 0, false, true));
            if (!values.foreground || values.font_family != typography.font_family || values.font_size != 18) std::abort();
        }
        allocation_probe::active = false;
        require(allocation_probe::calls == 0, "Warmed transient resolution allocates nothing");
        allocation_probe::active = true;
        auto visible = view.visible_content();
        allocation_probe::active = false;
        return std::pair{allocation_probe::bytes, allocation_probe::calls};
    };
    const auto baseline_size = measure(100000), larger_size = measure(1000000);
    require(baseline_size == larger_size, "Visible content allocations are independent of source size");
}
void provider_geometry_contract() {
#ifdef _WIN32
    using Microsoft::WRL::ComPtr;
    const auto window = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, L"STATIC", L"Collection provider geometry",
        WS_POPUP | WS_VISIBLE, -32000, -32000, 640, 280, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    require(window != nullptr, "Create offscreen no-activate provider fixture");
    struct Cleanup { HWND window; ~Cleanup() { DestroyWindow(window); } } cleanup{window};
    RECT screen{};
    require(GetWindowRect(window, &screen) != FALSE, "Read owned provider fixture bounds");
    const auto scale = GetDpiForWindow(window) / 96.0;
    const Rect arranged{0, 0, static_cast<float>((screen.right - screen.left) / scale),
        static_cast<float>((screen.bottom - screen.top) / scale)};
    auto source = std::make_shared<collections_test::Items>(100);
    ItemsView view; view.set_items(source); view.set_automation_id(L"collection-root"); view.arrange(arranged);
    PartStyleValues root; root.row_height = 70; root.padding = Insets{10, 12, 14, 16};
    root.border_thickness = Insets{1, 2, 3, 4};
    PartStyleValues scrollbar; scrollbar.width = 20;
    view.set_control_style_values(StylePart::root, root);
    view.set_control_style_values(StylePart::scrollbar, scrollbar);
    auto offset = 17.5;
    if (std::fmod(offset + view.content_viewport().height, view.item_size().height) < 0.5) offset += 1;
    view.set_offset(offset);
    auto state = std::make_shared<ControlAccessibility>();
    publish_control(state, nullptr, view, window);
    ComPtr<IRawElementProviderSimple> provider; provider.Attach(create_collection_provider(state));
    ComPtr<IRawElementProviderFragmentRoot> fragment_root;
    ComPtr<IRawElementProviderFragment> root_fragment;
    ComPtr<IItemContainerProvider> container;
    ComPtr<IScrollProvider> scroll;
    require(SUCCEEDED(provider.As(&fragment_root)) && SUCCEEDED(provider.As(&root_fragment)) &&
        SUCCEEDED(provider.As(&container)) && SUCCEEDED(provider.As(&scroll)), "Query real collection provider interfaces");
    const auto find = [&](std::size_t index) {
        const auto key = source->key(index);
        const auto text = std::to_wstring(key.id) + L":" + std::to_wstring(key.version);
        VARIANT match{}; match.vt = VT_BSTR; match.bstrVal = SysAllocString(text.c_str());
        require(match.bstrVal != nullptr, "Allocate provider identity query");
        ComPtr<IRawElementProviderSimple> result;
        const auto status = container->FindItemByProperty(nullptr, UIA_AutomationIdPropertyId, match, &result);
        VariantClear(&match);
        require(SUCCEEDED(status) && result, "Find virtual provider by stable key");
        ComPtr<IRawElementProviderFragment> fragment;
        require(SUCCEEDED(result.As(&fragment)), "Query virtual item fragment");
        return fragment;
    };
    const auto coordinates_match = [](double a, double b) { return std::abs(a - b) < 0.01; };
    const auto expect_bounds = [&](IRawElementProviderFragment* fragment, std::size_t index) {
        const auto row = view.item_bounds(index), viewport = view.content_viewport();
        const auto left = (std::max)(row.x, viewport.x), top = (std::max)(row.y, viewport.y);
        const auto right = (std::min)(row.x + row.width, viewport.x + viewport.width);
        const auto bottom = (std::min)(row.y + row.height, viewport.y + viewport.height);
        UiaRect actual{};
        require(SUCCEEDED(fragment->get_BoundingRectangle(&actual)), "Read actual virtual provider rectangle");
        require(coordinates_match(actual.left, screen.left + left * scale) && coordinates_match(actual.top, screen.top + top * scale) &&
            coordinates_match(actual.width, (std::max)(0.0f, right - left) * scale) && coordinates_match(actual.height, (std::max)(0.0f, bottom - top) * scale),
            "UIA rectangle equals painted row clipped to the shared content viewport");
        return actual;
    };
    const auto expect_hit = [&](Point point) {
        const auto index = view.hit_test(point);
        ComPtr<IRawElementProviderFragment> hit;
        require(SUCCEEDED(fragment_root->ElementProviderFromPoint(screen.left + point.x * scale,
            screen.top + point.y * scale, &hit)) && hit, "Hit test actual provider tree");
        ComPtr<IRawElementProviderSimple> simple;
        require(SUCCEEDED(hit.As(&simple)), "Query hit provider identity");
        VARIANT identity{};
        const auto status = simple->GetPropertyValue(UIA_AutomationIdPropertyId, &identity);
        const std::wstring actual = identity.vt == VT_BSTR && identity.bstrVal ? identity.bstrVal : L"";
        VariantClear(&identity);
        const auto key = index ? source->key(*index) : ItemKey{};
        const auto expected = index ? std::to_wstring(key.id) + L":" + std::to_wstring(key.version) : L"collection-root";
        require(SUCCEEDED(status) && actual == expected, "UIA hit identity matches model geometry and excludes root padding/scrollbar");
    };
    const auto runtime_id = [&](IRawElementProviderFragment* fragment) {
        SAFEARRAY* array{};
        require(SUCCEEDED(fragment->GetRuntimeId(&array)) && array, "Read stable virtual provider runtime ID");
        std::array<LONG, 8> result{};
        bool valid = SafeArrayGetDim(array) == 1;
        LONG upper{}; valid = valid && SUCCEEDED(SafeArrayGetUBound(array, 1, &upper)) && upper == 7;
        for (LONG i = 0; i < 8 && valid; ++i) valid = SUCCEEDED(SafeArrayGetElement(array, &i, &result[i]));
        SafeArrayDestroy(array);
        require(valid, "Read complete virtual provider identity");
        return result;
    };
    const auto visible = view.visible_items();
    auto first = find(visible.begin), last = find(visible.end - 1);
    const auto first_bounds = expect_bounds(first.Get(), visible.begin);
    const auto last_bounds = expect_bounds(last.Get(), visible.end - 1);
    require(first_bounds.height > 0 && first_bounds.height < view.item_size().height * scale &&
        last_bounds.height > 0 && last_bounds.height < view.item_size().height * scale, "Both viewport boundary rows are clipped");
    auto viewport = view.content_viewport();
    expect_hit({viewport.x + 5, viewport.y + 2});
    expect_hit({viewport.x + 5, viewport.y + viewport.height - 2});
    expect_hit({viewport.x - 1, viewport.y + 2});
    expect_hit({viewport.x + viewport.width + 1, viewport.y + 2});
    expect_hit({viewport.x + 5, viewport.y + viewport.height + 1});
    double percent{}, size{};
    require(SUCCEEDED(scroll->get_VerticalScrollPercent(&percent)) && SUCCEEDED(scroll->get_VerticalViewSize(&size)) &&
        coordinates_match(percent, view.offset() * 100 / view.maximum_offset()) &&
        coordinates_match(size, viewport.height * 100 / (source->size() * view.item_size().height)), "UIA scroll percentages use styled viewport and row extent");
    UiaRect root_bounds{};
    require(SUCCEEDED(root_fragment->get_BoundingRectangle(&root_bounds)) && coordinates_match(root_bounds.left, screen.left) &&
        coordinates_match(root_bounds.top, screen.top) && coordinates_match(root_bounds.width, screen.right - screen.left) &&
        coordinates_match(root_bounds.height, screen.bottom - screen.top), "Root provider bounds are not inset with row padding");
    const auto identity = runtime_id(first.Get());
    view.set_offset(0); root.row_height = 56; root.padding = Insets{5, 7, 9, 11}; scrollbar.width = 44;
    view.set_control_style_values(StylePart::root, root); view.set_control_style_values(StylePart::scrollbar, scrollbar);
    publish_control(state, nullptr, view, window);
    require(runtime_id(first.Get()) == identity && state->snapshot.collection == source, "Style replacement retains existing provider and source identity");
    expect_bounds(first.Get(), visible.begin);
    viewport = view.content_viewport(); expect_hit({viewport.x + 5, viewport.y + 2});
    view.reveal(source->key(source->size() - 1)); publish_control(state, nullptr, view, window);
    auto final = find(source->size() - 1); expect_bounds(final.Get(), source->size() - 1);
    require(SUCCEEDED(scroll->get_VerticalScrollPercent(&percent)) && coordinates_match(percent, 100), "Final item reveal agrees with UIA scroll range");
    view.set_presentation(ItemsPresentation::gallery);
    view.set_item_size({96, 128});
    view.set_control_style_values(StylePart::root, {});
    for (const float width : {arranged.width, 100.0f}) {
        view.arrange({0, 0, width, arranged.height}); view.set_offset(0);
        publish_control(state, nullptr, view, window);
        auto gallery = find(0);
        expect_bounds(gallery.Get(), 0);
        viewport = view.content_viewport();
        expect_hit({viewport.x + 5, viewport.y + 5});
        require(runtime_id(gallery.Get()) == identity, "Gallery keeps stable UIA identity across presentation changes");
        view.reveal(source->key(source->size() - 1)); publish_control(state, nullptr, view, window);
        expect_bounds(final.Get(), source->size() - 1);
        require(SUCCEEDED(scroll->get_VerticalScrollPercent(&percent)) && coordinates_match(percent, 100),
            "Gallery last-item reveal agrees with UIA in wrapped and single-column layouts");
    }
#endif
}
void tree_details_provider_contract() {
#ifdef _WIN32
    using Microsoft::WRL::ComPtr;
    const auto window = CreateWindowExW(WS_EX_NOACTIVATE, L"STATIC", L"Tree details provider", WS_POPUP,
        0, 0, 700, 240, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    require(window != nullptr, "Create tree details provider fixture");
    struct Cleanup { HWND window; ~Cleanup() { DestroyWindow(window); } } cleanup{window};
    TreeView tree; tree.set_tree(std::make_shared<collections_test::DetailTree>()); collections_test::compact_tree(tree);
    tree.arrange({0, 0, 700, 240});
    tree.on_request([&](TreeRequest request) { tree.complete(request, std::make_shared<collections_test::DetailItems>(2, 2000001)); });
    tree.disclose({1, 1}, true);
    auto state = std::make_shared<ControlAccessibility>(); publish_control(state, nullptr, tree, window);
    ComPtr<IRawElementProviderSimple> root; root.Attach(create_collection_provider(state));
    ComPtr<IItemContainerProvider> container; require(SUCCEEDED(root.As(&container)), "Details retains virtual item container");
    VARIANT match{}; match.vt = VT_BSTR; match.bstrVal = SysAllocString(L"2000002:1");
    ComPtr<IRawElementProviderSimple> item;
    const auto found = container->FindItemByProperty(nullptr, UIA_AutomationIdPropertyId, match, &item); VariantClear(&match);
    require(SUCCEEDED(found) && item, "Find lazy detail child by stable identity");
    const auto text = [&](PROPERTYID property) {
        VARIANT value{}; require(SUCCEEDED(item->GetPropertyValue(property, &value)), "Read detail tree accessibility text");
        const std::wstring result = value.vt == VT_BSTR && value.bstrVal ? value.bstrVal : L"";
        VariantClear(&value); return result;
    };
    require(text(UIA_NamePropertyId) == L"Entry 2000002" &&
        text(UIA_HelpTextPropertyId).find(L"Date modified: C1:2000002; Type: C2:2000002; Size: C3:2000002") != std::wstring::npos,
        "Tree items retain their name and expose labeled metadata in HelpText");
    VARIANT level{}, role{};
    require(SUCCEEDED(item->GetPropertyValue(UIA_LevelPropertyId, &level)) && level.lVal == 2 &&
        SUCCEEDED(item->GetPropertyValue(UIA_ControlTypePropertyId, &role)) && role.lVal == UIA_TreeItemControlTypeId,
        "Detail metadata does not replace native tree hierarchy semantics");
    VariantClear(&level); VariantClear(&role);
    ComPtr<IUnknown> selection;
    require(SUCCEEDED(item->GetPatternProvider(UIA_SelectionItemPatternId, &selection)) && selection,
        "Detail tree items retain selection patterns");
    ComPtr<IUnknown> table;
    require(SUCCEEDED(root->GetPatternProvider(UIA_GridPatternId, &table)) && !table, "Headless tree details do not advertise a table");
    tree.set_columns({}); publish_control(state, nullptr, tree, window);
    require(text(UIA_HelpTextPropertyId) == L"Ordinary subtitle", "Clearing columns updates metadata on retained tree providers");
#endif
}
void render_contract() {
#ifdef _WIN32
    const auto window = CreateWindowExW(WS_EX_NOACTIVATE, L"STATIC", L"Collection style pixels", WS_POPUP,
        0, 0, 320, 180, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    require(window != nullptr, "Create hidden collection raster fixture");
    Drawing drawing;
    struct Cleanup { Drawing& drawing; HWND window; ~Cleanup() { drawing.release(); DestroyWindow(window); } } cleanup{drawing, window};
    drawing.initialize(); DrawingTestAccess::software_target(drawing, window);
    ItemsView view; view.set_items(std::make_shared<collections_test::Items>(10)); view.arrange({0, 0, 320, 180});
    view.set_control_style(ControlStyle::create(StyleTarget::items_view, {{StylePart::row, fill(0x123456, 0x654321)}},
        {{StylePart::row, style_states::selected, fill(0xaabbcc, 0xccbbaa)},
         {StylePart::row, style_states::disabled, fill(0x445566, 0x665544)}}));
    for (const auto mode : {ThemeMode::light, ThemeMode::dark, ThemeMode::high_contrast}) {
        auto palette = Palette::system(mode);
        if (mode != ThemeMode::high_contrast) palette.high_contrast = false;
        auto rows = view.visible_content();
        rows[1].content.enabled = false;
        require(drawing.begin(window, 96, palette.background), "Begin collection frame");
        drawing.collection_row(rows[0], true, false, true, palette, false, {}, false, false, &view);
        drawing.collection_row(rows[1], false, false, true, palette, true, {}, false, false, &view);
        const auto pixel = DrawingTestAccess::pixel(drawing, 200, 20);
        const auto disabled = DrawingTestAccess::pixel(drawing, 200, 76);
        if (!palette.high_contrast) {
            require(pixel == (mode == ThemeMode::light ? 0xaabbcc : 0xccbbaa), "Selected row paints theme-aware authored fill");
            require(disabled == (mode == ThemeMode::light ? 0x445566 : 0x665544), "Disabled item paint overrides hover");
        } else require(pixel != 0xaabbcc && pixel != 0xccbbaa && disabled != 0x445566 && disabled != 0x665544,
            "High contrast rejects authored row colors");
        require(drawing.end(), "End collection frame");
    }
    PartStyleValues track = fill(0xcc1100, 0xcc1100); track.thickness = 6;
    PartStyleValues segment = fill(0x11cc00, 0x11cc00);
    PartStyleValues marker = fill(0x0011cc, 0x0011cc); marker.size = 5;
    view.set_control_style(ControlStyle::create(StyleTarget::items_view,
        {{StylePart::track, track}, {StylePart::fill, segment}, {StylePart::selected_marker, marker}}, {}));
    auto row = view.visible_content().front();
    row.content.primary.clear(); row.content.secondary.clear(); row.content.action.clear();
    row.content.icon = ButtonIcon::none; row.content.progress = 0.5;
    auto palette = Palette::system(ThemeMode::light); palette.high_contrast = false;
    require(drawing.begin(window, 96, palette.background), "Begin collection part frame");
    drawing.collection_row(row, true, false, true, palette, false, {}, false, false, &view);
    require(DrawingTestAccess::pixel(drawing, 20, 50) == 0x11cc00 &&
        DrawingTestAccess::pixel(drawing, 200, 50) == 0xcc1100, "Inline progress track and fill use independent authored parts");
    require(DrawingTestAccess::pixel(drawing, 4, 20) == 0x0011cc, "Selection marker uses authored width and fill");
    require(drawing.end(), "End collection part frame");

    view.set_presentation(ItemsPresentation::grouped);
    view.set_control_style(ControlStyle::create(StyleTarget::items_view, {},
        {{StylePart::group_header, style_states::expanded, fill(0x7755aa, 0x7755aa)}}));
    const auto group = view.visible_content().front();
    require(group.group && group.expanded, "Render real expanded group header");
    require(drawing.begin(window, 96, palette.background), "Begin group part frame");
    drawing.collection_row(group, false, false, true, palette, false, {}, false, false, &view);
    require(DrawingTestAccess::pixel(drawing, 200, 20) == 0x7755aa, "Expanded group header paints its own part");
    require(drawing.end(), "End group part frame");

    view.set_presentation(ItemsPresentation::gallery); view.set_item_size({140, 160}); view.set_offset(0);
    auto thumbnail = std::make_shared<ImagePixels>();
    thumbnail->id = UINT64_MAX - 1; thumbnail->size = {4, 4};
    thumbnail->pixels.resize(4 * 4 * 4);
    for (std::size_t i = 0; i < thumbnail->pixels.size(); i += 4) {
        thumbnail->pixels[i] = thumbnail->pixels[i + 2] = thumbnail->pixels[i + 3] = std::byte{255};
    }
    for (const bool styled : {false, true}) {
        view.set_control_style(nullptr);
        if (styled) view.set_control_style(ControlStyle::create(StyleTarget::items_view,
            {{StylePart::tile, fill(0x102030, 0x102030)}}, {}));
        auto gallery = view.visible_content().front();
        gallery.content = {L"Gallery\nfilename", L"", ButtonIcon::folder};
        gallery.content.image_path = L"gallery-image";
        const auto geometry = view.gallery_layout(gallery);
        require(drawing.begin(window, 96, palette.background), "Begin gallery image frame");
        drawing.collection_row(gallery, false, false, true, palette, false, thumbnail, false, false, &view);
        const auto center = static_cast<int>(geometry.image.x + geometry.image.width / 2);
        const auto image_y = static_cast<int>(geometry.image.y + geometry.image.height / 2);
        require(DrawingTestAccess::pixel(drawing, center, image_y) == 0xff00ff &&
            DrawingTestAccess::pixel(drawing, center - 30, image_y) == 0xff00ff,
            "Gallery paints a large centered thumbnail in styled and default presentations");
        const auto background = DrawingTestAccess::pixel(drawing, 5, 80);
        int text_left = 320, text_right = -1;
        bool second_line{};
        for (int y = static_cast<int>(geometry.primary.y); y < static_cast<int>(geometry.primary.y + geometry.primary.height); ++y)
            for (int x = static_cast<int>(geometry.primary.x); x < static_cast<int>(geometry.primary.x + geometry.primary.width); ++x)
                if (DrawingTestAccess::pixel(drawing, x, y) != background) {
                    text_left = std::min(text_left, x); text_right = std::max(text_right, x);
                    second_line = second_line || y >= geometry.primary.y + 20;
                }
        require(text_right >= text_left && std::abs((text_left + text_right) / 2 - center) <= 3,
            "Gallery paints the filename centered beneath the thumbnail");
        require(second_line, "Gallery reserves a second filename line below the image");
        require(drawing.end(), "End gallery image frame");
        require(drawing.begin(window, 96, palette.background), "Begin gallery fallback frame");
        drawing.collection_row(gallery, false, false, true, palette, false, {}, false, false, &view);
        bool fallback{};
        for (int x = center - 35; x < center + 35; ++x)
            for (int y = static_cast<int>(geometry.image.y); y < static_cast<int>(geometry.image.y + geometry.image.height); ++y)
                fallback = fallback || DrawingTestAccess::pixel(drawing, x, y) != background;
        require(fallback, "Gallery retains the fallback icon while image work is pending or failed");
        require(drawing.end(), "End gallery fallback frame");
    }
    drawing.keep_images({});

    TreeView details; details.set_tree(std::make_shared<collections_test::DetailTree>()); collections_test::compact_tree(details);
    details.set_columns({{L"Name", 120}, {L"Date", 60}, {L"Type", 60}, {L"Size", 60, true}});
    details.arrange({0, 0, 320, 180});
    details.on_request([&](TreeRequest request) { details.complete(request, std::make_shared<collections_test::DetailItems>(2, 2000001)); });
    details.disclose({1, 1}, true);
    const auto detail_rows = details.visible_content();
    const auto rgb = [](D2D1_COLOR_F color) {
        return (uint32_t(color.r * 255 + .5f) << 16) | (uint32_t(color.g * 255 + .5f) << 8) | uint32_t(color.b * 255 + .5f);
    };
    for (const auto mode : {ThemeMode::light, ThemeMode::dark, ThemeMode::high_contrast}) {
        auto colors = Palette::system(mode);
        if (mode != ThemeMode::high_contrast) colors.high_contrast = false;
        require(drawing.begin(window, 96, colors.background), "Begin compact tree frame");
        for (const auto& detail : detail_rows)
            drawing.collection_row(detail, detail.index == 3, false, true, colors, false, thumbnail, false, false, &details);
        require(DrawingTestAccess::pixel(drawing, 306, 12) == rgb(colors.background) &&
            DrawingTestAccess::pixel(drawing, 306, 36) == rgb(colors.high_contrast ? colors.background : colors.surface) &&
            DrawingTestAccess::pixel(drawing, 306, 84) == rgb(colors.selection),
            "Compact detail rows have full-width 24-DIP stripes and visible selection");
        const auto first = details.details_layout(detail_rows[0]);
        const auto child = details.details_layout(detail_rows[2]);
        require(first.icon.width == 16 && first.icon.y == 4 && child.icon.x == 20 &&
            first.columns[1].x == child.columns[1].x, "Compact name geometry reserves no file chevron and keeps metadata aligned");
        require(DrawingTestAccess::pixel(drawing, int(first.icon.x + 8), 12) == 0xff00ff &&
            DrawingTestAccess::pixel(drawing, int(first.icon.x + 8), 2) == rgb(colors.background),
            "Tree thumbnails use a 16-DIP slot without a header or second text line");
        const auto size = first.columns.back();
        int right = -1;
        for (int y = 0; y < 24; ++y)
            for (int x = int(size.x + 6); x < int(size.x + size.width - 6); ++x)
                if (DrawingTestAccess::pixel(drawing, x, y) != rgb(colors.background)) right = std::max(right, x);
        require(right >= int(size.x + size.width - 10) && right < int(size.x + size.width - 6),
            "Numeric tree metadata aligns to the right edge of its column");
        require(drawing.end(), "End compact tree frame");
    }
    details.arrange({0, 0, 80, 180});
    require(drawing.begin(window, 96, palette.background), "Begin narrow compact tree frame");
    for (const auto& detail : details.visible_content())
        drawing.collection_row(detail, false, false, true, palette, false, {}, false, false, &details);
    require(DrawingTestAccess::pixel(drawing, 100, 36) == rgb(palette.background),
        "Narrow tree metadata cannot paint outside the row viewport");
    require(drawing.end(), "End narrow compact tree frame");
    drawing.keep_images({});

    NavigationView navigation;
    NavigationItem folder{{1, 1}, {}, L"Folder"};
    folder.icon = ButtonIcon::folder;
    navigation.set_items({folder});
    navigation.items()->arrange({0, 0, 320, 180});
    for (const bool shell_image : {false, true}) {
        int previous_width = 0;
        for (const float size : {12.0f, 20.0f}) {
            PartStyleValues metrics; metrics.row_height = 28; metrics.font_size = 12;
            PartStyleValues icon; icon.size = size;
            navigation.items()->set_control_style(ControlStyle::create(StyleTarget::navigation_list,
                {{StylePart::root, metrics}, {StylePart::icon, icon}}, {}));
            auto icon_row = navigation.items()->visible_content().front();
            icon_row.content.primary.clear();
            if (shell_image) icon_row.content.image_path = L"folder-image";
            require(drawing.begin(window, 96, palette.background), "Begin navigation icon frame");
            drawing.collection_row(icon_row, false, false, true, palette, false, {}, false, false, navigation.items().get());
            int left = 320, right = -1;
            for (int y = 0; y < 28; ++y) for (int x = 0; x < 60; ++x) {
                if (DrawingTestAccess::pixel(drawing, x, y) != DrawingTestAccess::pixel(drawing, 100, y)) {
                    left = std::min(left, x); right = std::max(right, x);
                }
            }
            const int width = right - left + 1;
            require(width > previous_width && width <= static_cast<int>(size) + 2,
                "Navigation icon paint follows authored size, including Shell-image fallback slots");
            previous_width = width;
            require(drawing.end(), "End navigation icon frame");
        }
    }
#endif
}
}
int main(int argc, char* argv[]) {
    const char* stage = "model";
    try {
        const bool desktop = argc == 2 && std::string_view(argv[1]) == "--desktop";
        require(argc == 1 || desktop, "Usage: xui_style_collections_tests [--desktop]");
        sparse_state_and_locals(); paragraph_alignment_contract(); owner_root_inheritance(); geometry(); hierarchy_and_commands(); allocation_contract();
        if (desktop) {
            stage = "provider_geometry_contract";
            provider_geometry_contract();
            stage = "tree_details_provider_contract";
            tree_details_provider_contract();
            stage = "render_contract";
            render_contract();
        }
        std::cout << "Collection styling contracts passed\n"; return 0;
    } catch (const std::exception& error) {
        allocation_probe::active = false; std::cerr << stage << ": " << error.what() << '\n'; return 1;
    }
}
