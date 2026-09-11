#include "xui/core.hpp"
#include "xui/commands.hpp"
#include "xui/file_list.hpp"
#include "xui/theme.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void close(float actual, float expected, const char* message) {
    require(std::isfinite(actual) && std::abs(actual - expected) < 0.001f, message);
}

std::shared_ptr<xui::Element> element(float width, float height) {
    auto result = std::make_shared<xui::Element>();
    result->set_preferred_size({width, height});
    return result;
}

void layout_tests() {
    xui::Stack column(xui::Axis::vertical);
    column.set_padding({5, 10, 5, 10});
    column.set_spacing(4);
    auto title = element(100, 20);
    auto edit = element(100, 30);
    auto list = element(100, 0);
    auto status = element(100, 10);
    column.add(title);
    column.add(edit);
    column.add(list, 1);
    column.add(status);
    const auto measured = column.measure({200, 300});
    close(measured.width, 110, "column measures content width plus padding");
    close(measured.height, 300, "column flex consumes available height");
    column.arrange({10, 20, 200, 300});
    close(title->bounds().x, 15, "column padded x");
    close(title->bounds().y, 30, "column padded y");
    close(title->bounds().width, 190, "column stretches cross axis");
    close(edit->bounds().y, 54, "column spacing");
    close(list->bounds().y, 88, "list origin");
    close(list->bounds().height, 208, "list flex height");
    close(status->bounds().y, 300, "status remains at bottom");
    close(status->bounds().height, 10, "status preferred height");

    xui::Stack row(xui::Axis::horizontal);
    row.set_padding({10, 5, 10, 5});
    row.set_spacing(5);
    auto fixed = element(50, 20);
    auto first = element(0, 20);
    auto second = element(0, 20);
    row.add(fixed);
    row.add(first, 1);
    row.add(second, 2);
    close(row.measure({380, 80}).width, 380, "row measure flex");
    row.arrange({0, 0, 380, 80});
    close(fixed->bounds().width, 50, "row fixed size");
    close(first->bounds().width, 100, "row first flex share");
    close(second->bounds().width, 200, "row second flex share");
    close(second->bounds().x, 170, "row second position");
    close(second->bounds().height, 70, "row cross stretch");
    row.arrange({0, 0, 25, 8});
    for (const auto& child : {fixed, first, second}) {
        require(child->bounds().width >= 0 && child->bounds().height >= 0,
            "small parent cannot create negative dimensions");
        require(child->bounds().x + child->bounds().width <= 25,
            "small parent clips allocated main axis");
    }

    xui::Stack empty(xui::Axis::vertical);
    empty.set_padding({2, 3, 4, 5});
    const auto empty_size = empty.measure({100, 100});
    close(empty_size.width, 6, "empty padding width");
    close(empty_size.height, 8, "empty padding height");

    auto nested = std::make_shared<xui::Stack>(xui::Axis::horizontal);
    nested->add(element(10, 10));
    xui::Stack root(xui::Axis::vertical);
    root.add(nested, 1);
    root.arrange({0, 0, 100, 100});
    close(nested->bounds().height, 100, "nested flex stack fills allocation");
}

void invalidation_tests() {
    static_assert(!std::is_copy_constructible_v<xui::Element>);
    static_assert(!std::is_move_constructible_v<xui::Element>);
    auto child = element(10, 10);
    const auto id = child->id();
    int parent_notifications = 0;
    int child_notifications = 0;
    xui::Invalidation last = xui::Invalidation::paint;
    child->set_invalidator([&](xui::Invalidation) { ++child_notifications; });
    {
        auto parent = std::make_shared<xui::Stack>(xui::Axis::vertical);
        parent->add(child);
        parent->set_invalidator([&](xui::Invalidation kind) {
            ++parent_notifications;
            last = kind;
        });
        child->set_preferred_size({20, 10});
        require(parent_notifications == 1 && child_notifications == 1,
            "child layout invalidation reaches both listeners");
        require(last == xui::Invalidation::layout, "layout implies downstream paint");
        child->set_preferred_size({20, 10});
        require(parent_notifications == 1, "unchanged property does not invalidate");
        child->invalidate(xui::Invalidation::paint);
        require(parent_notifications == 2 && last == xui::Invalidation::paint,
            "paint-only invalidation propagates");
        parent->set_spacing(3);
        parent->set_padding({1, 1, 1, 1});
        require(parent_notifications == 4, "stack properties invalidate layout");
        parent->arrange({0, 0, 80, 90});
        require(parent_notifications == 4, "layout does not recursively invalidate");
        require(child->id() == id && parent->id() != id, "IDs are stable and unique");
        bool rejected = false;
        try { parent->add(child); } catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "duplicate parent rejected");
        rejected = false;
        try { parent->add(parent); } catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "self cycle rejected");
        rejected = false;
        try { parent->add(nullptr); } catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "null child rejected");
    }
    child->invalidate(xui::Invalidation::paint);
    require(parent_notifications == 4 && child_notifications == 3,
        "surviving child never calls destroyed parent");
    xui::Stack new_parent(xui::Axis::vertical);
    new_parent.add(child);
    require(child->id() == id, "reparenting preserves identity");
    auto ancestor = std::make_shared<xui::Stack>(xui::Axis::vertical);
    auto descendant = std::make_shared<xui::Stack>(xui::Axis::vertical);
    ancestor->add(descendant);
    bool rejected = false;
    try { descendant->add(ancestor); } catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "ancestor cycle rejected");
    int bubbled = 0;
    ancestor->set_invalidator([&](xui::Invalidation) { ++bubbled; });
    auto leaf = element(1, 1);
    descendant->add(leaf);
    leaf->invalidate(xui::Invalidation::layout);
    require(bubbled == 2, "nested invalidation bubbles");
    int destroyed_parent_calls = 0;
    auto transient_parent = std::make_shared<xui::Stack>(xui::Axis::vertical);
    auto surviving_child = element(1, 1);
    transient_parent->add(surviving_child);
    transient_parent->set_invalidator([&](xui::Invalidation) { ++destroyed_parent_calls; });
    surviving_child->set_invalidator([&](xui::Invalidation) { transient_parent.reset(); });
    surviving_child->invalidate(xui::Invalidation::paint);
    require(!transient_parent && destroyed_parent_calls == 0,
        "reentrant parent destruction suppresses stale callbacks");
}

void model_tests() {
    xui::FileListModel model;
    require(model.items() && model.visible_indices().empty(), "default model is empty");
    model.move_selection(1);
    model.select_first();
    model.select_last();
    require(!model.selected_id(), "empty navigation has no selection");
    auto items = std::make_shared<const std::vector<xui::FileItem>>(
        std::vector<xui::FileItem>{{1, L"Alpha.txt", L"C:\\Alpha.txt", false},
            {2, L"BETA.txt", L"C:\\BETA.txt", false},
            {3, L"Alphabet", L"C:\\Alphabet", true}});
    model.set_items(items);
    require(model.items() == items && !model.selected_id(), "items shared without initial selection");
    model.select_index(1);
    require(model.selected_id() == 2 && model.selected_index() == 1, "select visible index");
    model.set_filter(L"ALP");
    require(model.visible_indices() == std::vector<xui::RowIndex>({0, 2}),
        "case-insensitive name substring filter");
    require(model.selected_id() == 2 && !model.selected_index() &&
        model.selected_item() && model.selected_item()->id == 2, "hidden selection retains item");
    model.set_filter(L"");
    require(model.selected_index() == 1, "unfilter restores selection");
    model.set_filter(L"alphabet");
    model.select_index(0);
    require(model.selected_id() == 3 && model.selected_index() == 0,
        "visible position differs from underlying index");
    model.select_index(999);
    require(model.selected_id() == 3, "invalid selection ignored");
    model.set_filter(L"");
    model.move_selection((std::numeric_limits<int>::min)());
    require(model.selected_id() == 1, "minimum delta clamps without overflow");
    model.move_selection((std::numeric_limits<int>::max)());
    require(model.selected_id() == 3, "maximum delta clamps");
    model.clear_selection();
    model.move_selection(0);
    require(!model.selected_id(), "zero delta is a no-op");
    model.move_selection(-1);
    require(model.selected_id() == 3, "negative navigation starts last");
    model.clear_selection();
    model.move_selection(2);
    require(model.selected_id() == 1, "positive navigation starts first");
    model.set_filter(L"BETA");
    model.move_selection(1);
    require(model.selected_id() == 2, "navigation replaces hidden selection");
    model.set_filter(L"missing");
    model.select_first();
    model.select_last();
    model.move_selection(-1);
    require(model.selected_id() == 2, "empty filter results retain selection");
    model.set_filter(L"");
    model.set_items(std::make_shared<const std::vector<xui::FileItem>>(
        std::vector<xui::FileItem>{{2, L"Renamed", L"other", false},
            {9, L"New", L"new", false}}));
    require(model.selected_id() == 2 && model.selected_index() == 0 &&
        model.selected_item()->name == L"Renamed", "replacement preserves matching identity");
    model.set_items(items);
    model.select_last();
    model.set_items(std::make_shared<const std::vector<xui::FileItem>>(
        std::vector<xui::FileItem>{{7, L"Other", L"other", false}}));
    require(!model.selected_id() && !model.selected_item(), "replacement removes stale identity");
    model.set_items(nullptr);
    require(model.items() && model.items()->empty() && model.visible_indices().empty(),
        "null items resets model");
}

void virtualization_tests() {
    auto range = xui::visible_range(0, 20, 0, 100);
    require(range.begin == 0 && range.end == 0, "empty range");
    range = xui::visible_range(100, 20, 0, 100);
    require(range.begin == 0 && range.end == 7, "first viewport overscan");
    range = xui::visible_range(100, 20, 45, 100, 0);
    require(range.begin == 2 && range.end == 8, "partial visible rows");
    range = xui::visible_range(100, 20, 99999, 100);
    require(range.begin == 93 && range.end == 100, "last viewport clamped");
    range = xui::visible_range(2, 20, 999, 100);
    require(range.begin == 0 && range.end == 2, "short list");
    range = xui::visible_range(100, 20, 0, 0);
    require(range.begin == 0 && range.end == 0, "zero viewport");
    close(xui::clamp_scroll(100, 20, -30, 100), 0, "negative scroll");
    close(xui::clamp_scroll(100, 20, 5000, 100), 1900, "maximum scroll");
    close(xui::clamp_scroll(2, 20, 50, 100), 0, "short list scroll");
    close(xui::reveal_row(3, 20, 40, 100), 40, "visible row stays put");
    close(xui::reveal_row(1, 20, 80, 100), 20, "reveal above");
    close(xui::reveal_row(9, 20, 40, 100), 100, "reveal below");
    close(xui::reveal_row(2, 200, 0, 100), 400, "oversized row aligns top");
    close(xui::reveal_row(2, 20, 0, 0), 40, "zero viewport aligns top");

    const auto huge = (std::numeric_limits<std::size_t>::max)();
    range = xui::visible_range(huge, 20, 0, 100);
    require(range.begin == 0 && range.end == 7, "huge list needs no list-sized allocation");
    range = xui::visible_range(huge, 20, 40, 100, huge);
    require(range.begin == 0 && range.end == huge, "overscan saturates without overflow");
    const float infinity = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float maximum = (std::numeric_limits<float>::max)();
    require(std::isfinite(xui::clamp_scroll(huge, maximum, infinity, 10)),
        "huge scroll saturates to finite float");
    require(std::isfinite(xui::reveal_row(huge, maximum, infinity, 10)),
        "huge reveal saturates");
    for (const float invalid : {0.0f, -1.0f, infinity, nan}) {
        range = xui::visible_range(100, invalid, 0, 100);
        require(range.begin == 0 && range.end == 0, "invalid row height range");
        close(xui::clamp_scroll(100, invalid, 10, 100), 0, "invalid row height clamp");
        close(xui::reveal_row(1, invalid, 10, 100), 0, "invalid row height reveal");
    }
    close(xui::clamp_scroll(100, 20, nan, 100), 0, "NaN offset");
    range = xui::visible_range(huge, std::numeric_limits<float>::denorm_min(),
        infinity, 1, 0);
    require(range.begin <= range.end && range.end <= huge, "tiny rows bounded indices");
    range = xui::visible_range(huge, 20, infinity, 100);
    require(range.begin <= range.end && range.end <= huge, "huge end range bounded");
    auto invalid_element = element(infinity, nan);
    const auto measured = invalid_element->measure({100, 100});
    close(measured.width, 100, "infinite preferred dimension saturates");
    close(measured.height, 0, "NaN preferred dimension zero");
    invalid_element->arrange({nan, infinity, -1, infinity});
    const auto bounds = invalid_element->bounds();
    require(bounds.x == 0 && std::isfinite(bounds.y) && bounds.width == 0 &&
        std::isfinite(bounds.height), "invalid rectangle normalizes");
    xui::Stack extreme(xui::Axis::horizontal);
    auto a = element(0, 0);
    auto b = element(0, 0);
    extreme.add(a, maximum);
    extreme.add(b, maximum);
    extreme.arrange({0, 0, 100, 100});
    close(a->bounds().width, 50, "extreme flex sum avoids float overflow");
    close(b->bounds().width, 50, "extreme flex distributes remainder");
}

void file_list_tests() {
    xui::FileList list;
    int paints = 0;
    list.set_invalidator([&](xui::Invalidation kind) {
        require(kind == xui::Invalidation::paint, "list behavior requests paint invalidation");
        ++paints;
    });
    list.set_viewport_height(96);
    for (const auto navigation : {xui::Navigation::previous, xui::Navigation::next,
        xui::Navigation::first, xui::Navigation::last,
        xui::Navigation::page_up, xui::Navigation::page_down}) {
        list.navigate(navigation);
    }
    require(!list.model().selected_id(), "empty file list navigation preserves no selection");
    close(list.offset(), 0, "empty file list cannot scroll");
    auto items = std::make_shared<std::vector<xui::FileItem>>();
    for (std::size_t index = 0; index < 12; ++index) {
        items->push_back({static_cast<xui::ItemId>(index + 1),
            L"Item " + std::to_wstring(index), L"path", false});
    }
    list.set_items(items);
    require(!list.model().selected_id(), "file list items do not select implicitly");
    close(list.row_height(), 32, "file list row height");
    close(list.viewport_height(), 96, "file list viewport height");
    auto visible = list.visible_rows();
    require(visible.begin == 0 && visible.end == 5, "file list visible rows include overscan");
    list.navigate(xui::Navigation::next);
    require(list.model().selected_index() == 0, "next initially selects first row");
    list.navigate(xui::Navigation::next);
    require(list.model().selected_index() == 1, "next advances selection");
    list.navigate(xui::Navigation::previous);
    require(list.model().selected_index() == 0, "previous moves selection backward");
    list.navigate(xui::Navigation::previous);
    require(list.model().selected_index() == 0, "previous clamps first row");
    list.navigate(xui::Navigation::page_down);
    require(list.model().selected_index() == 3, "page down advances viewport row count");
    close(list.offset(), 32, "page navigation reveals bottom row");
    list.navigate(xui::Navigation::page_up);
    require(list.model().selected_index() == 0, "page up reverses viewport row count");
    close(list.offset(), 0, "page up reveals top row");
    list.navigate(xui::Navigation::last);
    require(list.model().selected_index() == 11, "last selects last row");
    close(list.offset(), 288, "last navigation reveals final row");
    list.navigate(xui::Navigation::page_down);
    require(list.model().selected_index() == 11, "page down clamps last row");
    visible = list.visible_rows();
    require(visible.begin == 7 && visible.end == 12, "last viewport has bounded overscan");
    list.navigate(xui::Navigation::first);
    require(list.model().selected_index() == 0, "first selects first row");
    close(list.offset(), 0, "first reveals first row");
    list.select(8, false);
    require(list.model().selected_index() == 8, "selection without reveal still selects");
    close(list.offset(), 0, "selection can preserve viewport");
    list.reveal(8);
    close(list.offset(), 192, "explicit reveal scrolls to selected row");
    list.select(2);
    close(list.offset(), 64, "default selection reveals above viewport");
    list.scroll_to(10000);
    close(list.offset(), 288, "file list scroll clamps maximum");
    list.scroll_to(-1);
    close(list.offset(), 0, "file list scroll clamps minimum");
    list.scroll_to(std::numeric_limits<float>::quiet_NaN());
    close(list.offset(), 0, "file list scroll normalizes NaN");
    list.scroll_to(288);
    list.set_viewport_height(192);
    close(list.offset(), 192, "viewport growth reclamps current scroll");
    list.set_viewport_height(96);
    list.select(11);
    list.set_filter(L"ITEM 0");
    require(list.model().visible_indices() == std::vector<xui::RowIndex>({0}),
        "file list delegates case-insensitive filtering");
    require(list.model().selected_id() == 12 && !list.model().selected_index(),
        "file list filter retains hidden selection identity");
    close(list.offset(), 0, "file list filter resets viewport");
    list.set_filter(L"");
    require(list.model().selected_index() == 11, "file list unfilter restores selected row");
    close(list.offset(), 0, "unfilter does not automatically reveal hidden selection");
    list.set_filter(L"Item 0");
    list.navigate(xui::Navigation::next);
    require(list.model().selected_id() == 1 && list.model().selected_index() == 0,
        "filtered navigation selects a visible item");
    list.set_filter(L"no results");
    list.navigate(xui::Navigation::last);
    require(list.model().selected_id() == 1 && !list.model().selected_index(),
        "empty filtered navigation keeps hidden identity");
    list.set_filter(L"");
    list.set_viewport_height(16);
    list.navigate(xui::Navigation::first);
    list.navigate(xui::Navigation::page_down);
    require(list.model().selected_index() == 1, "partial viewport navigates at least one row");
    close(list.offset(), 32, "oversized row reveal aligns its top");
    list.set_viewport_height(-1);
    close(list.viewport_height(), 0, "negative viewport becomes zero");
    visible = list.visible_rows();
    require(visible.begin == 0 && visible.end == 0, "zero viewport has no visible rows");
    list.navigate(xui::Navigation::page_down);
    require(list.model().selected_index() == 2, "zero viewport pages by one row");
    list.set_viewport_height(96);
    list.navigate(xui::Navigation::last);
    list.set_viewport_height(std::numeric_limits<float>::infinity());
    require(std::isfinite(list.viewport_height()), "infinite viewport saturates to finite height");
    list.navigate(xui::Navigation::page_up);
    require(list.model().selected_index() == 0, "huge page-up clamps without integer overflow");
    list.navigate(xui::Navigation::page_down);
    require(list.model().selected_index() == 11, "huge page-down clamps without integer overflow");
    list.set_viewport_height(std::numeric_limits<float>::quiet_NaN());
    close(list.viewport_height(), 0, "NaN viewport becomes zero");
    list.set_viewport_height(96);
    list.set_items(std::make_shared<const std::vector<xui::FileItem>>(
        std::vector<xui::FileItem>{{99, L"Replacement", L"path", false}}));
    require(!list.model().selected_id(), "file list replacement clears stale selection");
    close(list.offset(), 0, "file list replacement reclamps scroll");
    list.navigate(xui::Navigation::previous);
    require(list.model().selected_id() == 99, "previous initially selects last item");
    list.clear_selection();
    require(!list.model().selected_id(), "clear selection removes the model identity");
    require(paints > 0, "file list behavior invalidates rendering");

    xui::FileList focus_list;
    focus_list.set_items(items);
    focus_list.set_viewport_height(96);
    focus_list.select(2);
    focus_list.focus_item(8);
    require(focus_list.model().selected_index() == 2 && focus_list.focused_index() == 8,
        "focus does not change selection");
    focus_list.navigate(xui::Navigation::next);
    require(focus_list.model().selected_index() == 9 && focus_list.focused_index() == 9,
        "keyboard navigation starts at the focused row");
    focus_list.clear_selection();
    require(!focus_list.model().selected_id() && focus_list.focused_index() == 9,
        "selection removal does not remove focus");
    focus_list.focus_list();
    require(!focus_list.focused_id(), "list focus clears item focus");
    bool rejected_focus = false;
    try { focus_list.focus_item(999); }
    catch (const std::out_of_range&) { rejected_focus = true; }
    require(rejected_focus, "invalid focus indices report an error");
}

void command_tests() {
    xui::Commands commands;
    bool enabled = false;
    int executions = 0;
    commands.add(7, {L"Open", [&] { return enabled; }, [&] { ++executions; }});
    require(commands.get(7).label == L"Open", "command metadata is retrievable");
    require(!commands.invoke(7) && executions == 0, "disabled command rejects action");
    enabled = true;
    require(commands.invoke(7) && executions == 1, "enabled command executes exactly once");
    enabled = false;
    require(!commands.invoke(7) && executions == 1, "command enablement is evaluated per invocation");
    bool rejected = false;
    try {
        commands.add(7, {L"Replacement", [] { return true; }, [&] { executions += 100; }});
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected && commands.get(7).label == L"Open", "duplicate ID rejects replacement");
    enabled = true;
    require(commands.invoke(7) && executions == 2, "duplicate rejection preserves original action");
    commands.add(8, {L"Other", [] { return true; }, [&] { executions += 10; }});
    require(commands.invoke(8) && executions == 12, "distinct command dispatches its own action");
    rejected = false;
    try { commands.invoke(999); } catch (const std::out_of_range&) { rejected = true; }
    require(rejected && executions == 12, "unknown command rejects without action");
}

void visual_tests() {
    auto luminance = [](uint32_t color) {
        auto channel = [](uint32_t value) {
            const double s = value / 255.0;
            return s <= 0.04045 ? s / 12.92 : std::pow((s + 0.055) / 1.055, 2.4);
        };
        return 0.2126 * channel((color >> 16) & 255) +
               0.7152 * channel((color >> 8) & 255) + 0.0722 * channel(color & 255);
    };
    auto contrast = [&](uint32_t a, uint32_t b) {
        const double first = luminance(a), second = luminance(b);
        return (std::max(first, second) + 0.05) / (std::min(first, second) + 0.05);
    };
    for (auto mode : {xui::ThemeMode::dark, xui::ThemeMode::light}) {
        const auto colors = xui::theme_colors(mode);
        require(contrast(colors.text, colors.surface) >= 4.5, "body text contrast");
        require(contrast(colors.text, colors.field) >= 4.5, "search text contrast");
        require(contrast(colors.secondary, colors.surface) >= 4.5, "secondary text contrast");
        require(contrast(colors.secondary, colors.background) >= 4.5, "status text contrast");
        require(contrast(colors.selection_text, colors.selection) >= 4.5, "selection text contrast");
        require(contrast(colors.accent, colors.surface) >= 3, "keyboard focus contrast");
        require(contrast(colors.error, colors.background) >= 4.5, "error text contrast");
    }
    require(xui::scroll_thumb(100, 100, 0, 92).height == 0, "no scrollbar when items fit");
    const auto top = xui::scroll_thumb(10000, 200, 0, 192);
    close(top.height, 28, "scroll thumb minimum size");
    close(top.top, 0, "scroll thumb at top");
    const auto bottom = xui::scroll_thumb(10000, 200, 9800, 192);
    close(bottom.top, bottom.travel, "scroll thumb at bottom");
    close(xui::scroll_from_thumb(bottom, bottom.travel), 9800, "drag maps to end");
    close(xui::scroll_from_thumb(bottom, -100), 0, "drag clamps above track");
    close(xui::scroll_from_thumb(bottom, bottom.travel * 2), 9800, "drag clamps below track");
    close(xui::scroll_from_thumb(bottom, bottom.travel / 2), 4900, "drag maps to middle");
    const auto short_track = xui::scroll_thumb(10000, 200, 300, 10);
    close(short_track.height, 5, "short tracks retain drag travel");
    close(xui::scroll_from_thumb(short_track, 5), 9800, "short-track drag reaches the end");
    close(xui::scroll_from_thumb({}, 5), 0, "zero-travel drag is finite");
    require(xui::scroll_thumb(std::numeric_limits<float>::infinity(), 200, 0, 192).height == 0,
        "invalid content extent does not produce invalid paint geometry");
}

} // namespace

int main() {
    layout_tests();
    invalidation_tests();
    model_tests();
    virtualization_tests();
    file_list_tests();
    command_tests();
    visual_tests();
    std::cout << "All core tests passed\n";
    return EXIT_SUCCESS;
}
