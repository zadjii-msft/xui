#include "xui/adaptive_layout.hpp"
#include "xui/foundation.hpp"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>

namespace {
thread_local bool count_allocations{};
thread_local std::size_t allocation_count{};
}
void* operator new(std::size_t size) {
    if (auto* value = std::malloc(size ? size : 1)) {
        if (count_allocations) ++allocation_count;
        return value;
    }
    throw std::bad_alloc{};
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* value) noexcept { std::free(value); }
void operator delete[](void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }
void operator delete[](void* value, std::size_t) noexcept { std::free(value); }

namespace {
using namespace xui;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void near(float actual, float expected) {
    if (!(std::abs(actual - expected) < 0.01f))
        throw std::runtime_error("Layout metric mismatch: " + std::to_string(actual) + " != " + std::to_string(expected));
}
void rect(Rect actual, Rect expected) {
    near(actual.x, expected.x); near(actual.y, expected.y);
    near(actual.width, expected.width); near(actual.height, expected.height);
}
template<class F> void rejects(F action) {
    try { action(); } catch (const std::invalid_argument&) { return; }
    throw std::runtime_error("Unsupported layout style was accepted");
}
std::shared_ptr<Element> child(float width = 20, float height = 10) {
    auto result = std::make_shared<Element>(); result->set_preferred_size({width, height}); return result;
}
PartStyleValues chrome() {
    PartStyleValues values;
    values.padding = Insets{2, 3, 5, 7};
    values.border_thickness = Insets{1, 2, 3, 4};
    values.background = ThemeColor{0x123456};
    values.border_brush = ThemeColor{0xabcdef};
    return values;
}
void apply(Element& element, StyleTarget target, PartStyleValues values) {
    element.set_control_style(ControlStyle::create(target, {{StylePart::root, values}}, {}));
}
void schemas_and_states() {
    PartStyleValues stretch; stretch.vertical_alignment = StyleAlignment::stretch;
    rejects([&] { ControlStyle::create(StyleTarget::expander, {{StylePart::text, stretch}}, {}); });
    ControlStyle::create(StyleTarget::content_view, {{StylePart::root, stretch}}, {});
    const StyleTarget targets[]{StyleTarget::stack, StyleTarget::grid, StyleTarget::wrap, StyleTarget::adaptive_layout,
        StyleTarget::page_view, StyleTarget::content_view, StyleTarget::scroll_view, StyleTarget::split_view,
        StyleTarget::expander, StyleTarget::popup};
    for (auto target : targets) {
        ControlStyle::create(target, {{StylePart::root, chrome()}}, {});
        rejects([&] { ControlStyle::create(target, {}, {{StylePart::root, style_states::checked, {}}}); });
        if (target != StyleTarget::expander) {
            PartStyleValues text; text.foreground = ThemeColor{0};
            rejects([&] { ControlStyle::create(target, {{StylePart::root, text}}, {}); });
        }
    }
    PartStyleValues metric; metric.width = 30;
    rejects([&] { ControlStyle::create(StyleTarget::scroll_view, {},
        {{StylePart::scrollbar_track, style_states::scrollable, metric}}); });
    PartStyleValues fill; fill.background = ThemeColor{0x234567};
    auto scroll_style = ControlStyle::create(StyleTarget::scroll_view, {},
        {{StylePart::scrollbar_thumb, style_states::scrollable, fill}});
    ScrollView scroll(child(100, 300));
    scroll.set_control_style(scroll_style);
    scroll.arrange({0, 0, 200, 100});
    require(scroll.effective_control_style_values(StylePart::scrollbar_thumb)->background.has_value(), "Scrollable state missing");
    scroll.arrange({0, 0, 200, 500});
    require(!scroll.effective_control_style_values(StylePart::scrollbar_thumb)->background, "Scrollable state remained after resize");
    Popup popup(child());
    popup.set_control_style(ControlStyle::create(StyleTarget::popup, {}, {{StylePart::root, style_states::open, fill}}));
    require(!popup.is_open() && !popup.effective_control_style_values(StylePart::root)->background, "Style synthesized an open state");
    popup.opened();
    require(popup.effective_control_style_values(StylePart::root)->background.has_value(), "Popup open state missing");
    popup.closed(PopupDismissReason::cancel);
    require(!popup.effective_control_style_values(StylePart::root)->background, "Popup open style survived closing");

    Expander expander(L"State", child());
    PartStyleValues foreground; foreground.foreground = ThemeColor{0x112233};
    expander.set_control_style(ControlStyle::create(StyleTarget::expander, {},
        {{StylePart::root, style_states::disabled, foreground}}));
    expander.set_enabled(false);
    require(expander.effective_control_style_values(StylePart::text)->foreground == foreground.foreground &&
        expander.effective_control_style_values(StylePart::disclosure)->foreground == foreground.foreground,
        "Expander owned text parts did not inherit disabled foreground");
}
void stack_geometry_and_precedence() {
    Stack stack(Axis::vertical);
    stack.set_default_padding({20, 20, 20, 20});
    stack.set_default_spacing(30);
    auto first = child(), second = child(30, 12);
    stack.add(first); stack.add(second);
    auto style = chrome(); style.spacing = 9;
    apply(stack, StyleTarget::stack, style);
    const auto measured = stack.measure({500, 500});
    near(measured.width, 41); near(measured.height, 47);
    stack.arrange({100, 200, 100, 100});
    rect(first->bounds(), {103, 205, 89, 10});
    rect(second->bounds(), {103, 224, 89, 12});
    stack.set_padding({});
    stack.set_spacing(0);
    stack.set_default_padding({40, 40, 40, 40});
    stack.set_default_spacing(50);
    near(stack.measure({500, 500}).height, 28);
    stack.set_control_style(nullptr);
    near(stack.measure({500, 500}).height, 22);
    require(!stack.has_control_styling(), "Cleared Stack retained its style attachment");
    require(stack.child_at(0) == first && stack.child_at(1) == second, "Stack replaced a child");
    PartStyleValues separator; separator.thickness = 5;
    stack.set_control_style_values(StylePart::separator, separator);
    near(stack.measure({500, 500}).height, 27);
    stack.set_separator_inset_enabled(false);
    near(stack.measure({500, 500}).height, 22);
    stack.set_separator_inset_enabled(true);
    near(stack.measure({500, 500}).height, 27);
    stack.set_control_style_values(StylePart::separator, {});
    require(!stack.has_control_styling(), "Cleared separator retained an attachment");

    Stack centered(Axis::horizontal); centered.add(child(20, 10));
    PartStyleValues alignment; alignment.horizontal_alignment = StyleAlignment::center;
    alignment.vertical_alignment = StyleAlignment::end;
    apply(centered, StyleTarget::stack, alignment);
    centered.arrange({0, 0, 100, 40});
    rect(centered.child_at(0)->bounds(), {40, 30, 20, 10});
    Stack defaults(Axis::vertical); defaults.add(child());
    defaults.set_default_padding({1, 2, 3, 4}); defaults.set_default_spacing(8);
    near(defaults.measure({500, 500}).width, 24);
    PartStyleValues style_default; style_default.padding = Insets{5, 6, 7, 8};
    apply(defaults, StyleTarget::stack, style_default);
    near(defaults.measure({500, 500}).width, 32);
    defaults.set_control_style(nullptr);
    near(defaults.measure({500, 500}).width, 24);
    SplitButton split_button(L"Primary", L"More");
    const auto button_height = split_button.measure({500, 500}).height;
    PartStyleValues button_separator; button_separator.background = ThemeColor{0x112233};
    split_button.set_control_style_values(StylePart::separator, button_separator);
    near(split_button.measure({500, 500}).height, button_height);
}
void grid_and_wrap_geometry() {
    Grid grid;
    grid.set_tracks({{TrackSizing::fixed, 20}, {TrackSizing::fixed, 30}},
        {{TrackSizing::fixed, 40}, {TrackSizing::fixed, 50}});
    auto item = child(); grid.add(item, 1, 1);
    auto values = chrome(); values.column_gap = 8; values.row_gap = 6;
    apply(grid, StyleTarget::grid, values);
    auto desired = grid.measure({500, 500});
    near(desired.width, 109); near(desired.height, 72);
    grid.arrange({10, 20, 200, 150});
    rect(item->bounds(), {61, 51, 50, 30});
    grid.set_gap(0, 0);
    grid.arrange({10, 20, 200, 150});
    rect(item->bounds(), {53, 45, 50, 30});
    grid.set_control_style(nullptr);
    grid.arrange({10, 20, 200, 150});
    rect(item->bounds(), {50, 40, 50, 30});

    Wrap wrap; wrap.set_item_width(30);
    for (int i = 0; i < 3; ++i) wrap.add(child());
    values = chrome(); values.spacing = 7;
    apply(wrap, StyleTarget::wrap, values);
    desired = wrap.measure({100, 200});
    near(desired.height, 43);
    wrap.arrange({10, 20, 100, 100});
    require(wrap.columns() == 2, "Styled Wrap column count");
    rect(wrap.child_at(2)->bounds(), {13, 42, 41, 10});
    wrap.set_spacing(0); wrap.set_padding({});
    wrap.arrange({0, 0, 100, 100});
    require(wrap.columns() == 3, "Explicit Wrap metrics lost precedence");
}
void adaptive_and_pages() {
    auto navigation = child(100, 100), content = child(200, 150);
    AdaptiveLayout layout(navigation, content);
    layout.set_breakpoint(600); layout.set_navigation_extent(100);
    auto values = chrome(); values.spacing = 10;
    PartStyleValues compact; compact.background = ThemeColor{0x555555};
    layout.set_control_style(ControlStyle::create(StyleTarget::adaptive_layout, {{StylePart::root, values}},
        {{StylePart::root, style_states::compact, compact}}));
    layout.arrange({10, 20, 800, 300});
    rect(navigation->bounds(), {13, 25, 100, 284});
    rect(content->bounds(), {123, 25, 679, 284});
    layout.set_compact_navigation(CompactNavigation::overlay);
    layout.set_navigation_open(false);
    layout.arrange({10, 20, 400, 300});
    require(layout.compact() && !layout.overlay_active(), "Compact closed state changed");
    near(navigation->bounds().width, 0);
    rect(content->bounds(), {13, 25, 389, 284});
    require(layout.effective_control_style_values(StylePart::root)->background->light == 0x555555, "Compact style missing");
    layout.set_control_style(nullptr);
    layout.arrange({10, 20, 400, 300});
    near(navigation->bounds().width, 0);
    require(layout.navigation() == navigation && layout.content() == content, "Adaptive children replaced");

    PageView pages; auto a = child(), b = child();
    pages.add_page(a); pages.add_page(b);
    apply(pages, StyleTarget::page_view, chrome());
    pages.arrange({10, 20, 100, 100});
    rect(a->bounds(), {13, 25, 89, 84}); near(b->bounds().width, 0);
    auto retained = pages.child_at(0);
    pages.select(1); pages.arrange({10, 20, 100, 100});
    near(a->bounds().width, 0); rect(b->bounds(), {13, 25, 89, 84});
    require(pages.child_at(0) == retained, "Page host replaced");
}
void content_and_scroll() {
    auto item = child(20, 10); ContentView content(item);
    content.set_auto_size(true);
    auto values = chrome(); values.horizontal_alignment = StyleAlignment::end;
    values.vertical_alignment = StyleAlignment::center;
    apply(content, StyleTarget::content_view, values);
    auto desired = content.measure({300, 300});
    near(desired.width, 31); near(desired.height, 26);
    content.arrange({100, 200, 100, 100});
    rect(item->bounds(), {172, 242, 20, 10});
    content.set_control_style(nullptr); content.arrange({100, 200, 100, 100});
    rect(item->bounds(), {100, 200, 100, 100});

    auto tall = child(100, 500); ScrollView scroll(tall);
    PartStyleValues track; track.width = 26;
    scroll.set_control_style(ControlStyle::create(StyleTarget::scroll_view,
        {{StylePart::root, chrome()}, {StylePart::scrollbar_track, track}}, {}));
    scroll.arrange({100, 200, 200, 150});
    rect(scroll.viewport(), {103, 205, 163, 134});
    rect(scroll.scrollbar_track(), {266, 205, 26, 134});
    near(scroll.maximum_offset(), 366);
    near(scroll.thumb().width, 22);
    track.border_thickness = Insets{3, 5, 7, 9};
    scroll.set_control_style_values(StylePart::scrollbar_track, track);
    scroll.arrange(scroll.bounds());
    rect(scroll.scrollbar_thumb_track(), {271, 210, 12, 120});
    near(scroll.thumb().width, 12);
    scroll.set_control_style_values(StylePart::scrollbar_track, {});
    scroll.set_offset(100); scroll.arrange(scroll.bounds());
    near(tall->bounds().y, 105);
    scroll.set_control_style(nullptr); scroll.arrange(scroll.bounds());
    near(scroll.offset(), 100);
    near(scroll.maximum_offset(), 350);
    scroll.set_overlay_scrollbar(true); scroll.arrange(scroll.bounds());
    near(scroll.viewport().width, 200);
    scroll.arrange({100, 200, 200, 600});
    near(scroll.offset(), 0); near(scroll.thumb().height, 0);
    require(scroll.content() == tall, "Scroll content replaced");
}
void split_and_expander() {
    auto a = child(), b = child();
    SplitView split(a, b);
    PartStyleValues divider; divider.width = 24;
    PartStyleValues pane; pane.padding = Insets{4, 6, 8, 10};
    split.set_control_style(ControlStyle::create(StyleTarget::split_view,
        {{StylePart::root, chrome()}, {StylePart::divider, divider}, {StylePart::first_pane, pane}}, {}));
    split.arrange({10, 20, 800, 200});
    rect(split.divider(), {395.5f, 25, 24, 184});
    rect(a->bounds(), {17, 31, 370.5f, 168});
    auto left = split.first(), right = split.second();
    split.arrange({10, 20, 620, 200});
    require(!split.expanded(), "Styled divider collapse threshold");
    near(b->bounds().width, 0);
    split.set_control_style(nullptr);
    split.arrange({10, 20, 620, 200});
    require(split.expanded(), "Cleared divider threshold");
    require(split.first() == left && split.second() == right, "Split pane hosts replaced");

    Expander expander(L"Header", child(50, 60));
    PartStyleValues header; header.height = 60; header.padding = Insets{4, 5, 6, 7};
    PartStyleValues disclosure; disclosure.size = 28;
    expander.set_control_style(ControlStyle::create(StyleTarget::expander,
        {{StylePart::root, chrome()}, {StylePart::header, header}, {StylePart::disclosure, disclosure},
            {StylePart::content, pane}}, {}));
    expander.arrange({100, 200, 200, 180});
    rect(expander.header_bounds(), {103, 205, 189, 60});
    rect(expander.content_bounds(), {107, 271, 177, 88});
    near(expander.disclosure_bounds().width, 28);
    auto retained = expander.content();
    expander.set_expanded(false); expander.arrange(expander.bounds());
    near(retained->bounds().width, 0);
    near(expander.measure({500, 500}).height, 76);
    expander.set_control_style(nullptr); expander.arrange(expander.bounds());
    require(!expander.expanded() && expander.content() == retained, "Styling expanded or replaced collapsed content");
    near(retained->bounds().height, 0);
    expander.set_expanded(true); expander.arrange(expander.bounds());
    near(retained->bounds().height, 142);
}
void popup_lifecycle_and_default_allocations() {
    auto item = child(); Popup popup(item);
    apply(popup, StyleTarget::popup, chrome());
    require(!popup.is_open(), "Style opened Popup");
    popup.opened(); popup.arrange({100, 200, 200, 100});
    rect(item->bounds(), {103, 205, 189, 84});
    popup.closed(PopupDismissReason::cancel);
    popup.set_control_style(nullptr);
    require(!popup.is_open() && popup.content() == item, "Style clear changed Popup lifecycle");
    near(item->bounds().width, 0); near(item->bounds().height, 0);

    Stack stack(Axis::vertical); Grid grid; Wrap wrap;
    auto first = child(), second = child(); AdaptiveLayout adaptive(first, second);
    allocation_count = 0; count_allocations = true;
    for (int i = 0; i < 100; ++i) {
        stack.set_default_spacing(float(i)); stack.set_default_padding({float(i), 0, 0, 0});
        stack.set_spacing(float(i)); stack.set_padding({float(i), 0, 0, 0});
        grid.set_gap(float(i), float(i)); grid.set_padding({0, float(i), 0, 0});
        wrap.set_spacing(float(i)); wrap.set_padding({0, 0, float(i), 0});
        adaptive.set_navigation_extent(float(i)); adaptive.set_navigation_open(i % 2 == 0);
    }
    count_allocations = false;
    require(allocation_count == 0, "Ordinary layout setters allocated");
    require(!stack.has_control_styling() && !grid.has_control_styling() && !wrap.has_control_styling() &&
        !adaptive.has_control_styling(), "Ordinary layout setters attached styles");
}
void expander_text_metrics() {
    Expander expander(L"Styled metrics", child());
    expander.set_expanded(false);
    expander.set_text_measurer([&](std::wstring_view, TextStyle) {
        const auto* values = expander.effective_control_style_values(StylePart::text);
        const auto height = values ? values->font_size.value_or(16) : 16;
        return Size{height * 4, height};
    });
    PartStyleValues text; text.font_size = 44;
    expander.set_control_style_values(StylePart::text, text);
    near(expander.measure({500, 500}).height, 44);
    text.font_size = 64;
    expander.set_control_style_values(StylePart::text, text);
    near(expander.measure({500, 500}).height, 64);
    expander.set_control_style_values(StylePart::text, {});
    near(expander.measure({500, 500}).height, Expander::header_height);
}
}
int main() {
    try {
        schemas_and_states(); stack_geometry_and_precedence(); grid_and_wrap_geometry();
        adaptive_and_pages(); content_and_scroll(); split_and_expander(); popup_lifecycle_and_default_allocations();
        expander_text_metrics();
        std::cout << "Layout style tests passed\n"; return 0;
    } catch (const std::exception& error) {
        count_allocations = false;
        std::cerr << error.what() << '\n'; return 1;
    }
}
