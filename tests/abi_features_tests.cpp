#include "xui/xui.h"
#include "xui/xui_layout.h"
#include "xui/foundation.hpp"
#include <windows.h>
#include <cassert>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <vector>
#include <bit>
#include <source_location>
namespace {
unsigned assertions{};
void expect(bool condition, const std::source_location where = std::source_location::current()) {
    if (!condition) { std::cerr << "Feature assertion failed at line " << where.line() << '\n'; std::exit(EXIT_FAILURE); }
    ++assertions;
}
void ok(xui_status status, const std::source_location where = std::source_location::current()) {
    if (status) {
        char error[1024]{}; uint32_t size{}; xui_status code{};
        xui_error_copy(error,1024,&size,&code);
        std::cerr.write(error,size); std::cerr << '\n';
    }
    expect(status == 0, where);
}
xui_string text(const char* v) { return {v, static_cast<uint32_t>(std::strlen(v)), 0}; }
xui_feature_value value() { xui_feature_value v{}; v.size=sizeof(v); v.version=XUI_FEATURE_VERSION; return v; }
xui_feature_value read_value(xui_handle target, uint32_t property,
    const std::source_location where = std::source_location::current()) {
    auto result = value();
    ok(xui_feature_get(target, property, &result), where);
    return result;
}
xui_handle create(xui_handle w,uint32_t kind,xui_handle content=0,xui_handle second=0) {
    xui_feature_options o{sizeof(o),XUI_FEATURE_VERSION,text("Feature"),content,second};
    xui_handle h{};ok(xui_feature_create(w,kind,&o,&h));return h;
}
struct Source {unsigned refs{1}, queries{}, items{}, visuals{}; bool fail{}; uint64_t count{1000000}; xui_handle mutation_target{}; bool mutation_blocked{};};
void XUI_CALL retain(void* c) {++static_cast<Source*>(c)->refs;}
void XUI_CALL release(void* c) {--static_cast<Source*>(c)->refs;}
xui_status XUI_CALL query(void* c,uint32_t op,uint64_t first,uint64_t second,xui_source_row* row) {
    auto& source=*static_cast<Source*>(c);++source.queries;
    if(source.mutation_target) { auto v=value(); v.a=9; source.mutation_blocked=xui_feature_set(source.mutation_target,XUI_F_VALUE,&v)==XUI_BUSY; }
    if(source.fail) return 83;
    if(op==0){row->id=first+1;row->version=7;}
    if(op==1){++source.items;row->primary_length=3;std::memcpy(row->primary,"row",3);}
    if(op==2) row->index=first && first<=source.count && second==7 ? first-1 : UINT64_MAX;
    if(op==3) row->index=first==1;
    return 0;
}
xui_status XUI_CALL secret(void* c,const char* bytes,uint32_t size) {
    *static_cast<bool*>(c)=size==4 && std::memcmp(bytes,"safe",4)==0;return 0;
}
xui_status XUI_CALL visual_query(void* context, uint64_t, uint64_t, uint32_t* icon, char*, uint32_t, uint32_t* required) {
    ++static_cast<Source*>(context)->visuals; *icon = 18; *required = 0; return XUI_OK;
}
xui_status XUI_CALL event(void* c,const xui_event* e) { *static_cast<xui_event*>(c)=*e; return 0; }
xui_status XUI_CALL count_event(void* c, const xui_event*) { ++*static_cast<unsigned*>(c); return XUI_OK; }
xui_status XUI_CALL miller_event(void* context, const xui_miller_event* e) {
    *static_cast<xui_miller_event*>(context) = *e; return XUI_OK;
}
void miller_contracts() {
    static_assert(XUI_MILLER_COLUMNS == 46 && XUI_RETAINED_ELEMENT == 47);
    static_assert(sizeof(xui_miller_column) == 48);
    static_assert(sizeof(xui_miller_event) == 32);
    xui_window_options options{sizeof(options), XUI_ABI_VERSION, text("Miller contracts"), 600, 400};
    xui_handle window{}, other{}; ok(xui_window_create(&options, &window)); ok(xui_window_create(&options, &other));
    const auto columns = create(window, XUI_MILLER_COLUMNS);
    xui_handle reserved{}, invalid_slot{};
    ok(xui_feature_child(columns, 31, &reserved));
    expect(xui_feature_child(columns, 32, &invalid_slot) == XUI_INVALID_ARGUMENT);
    Source data;
    xui_source_options source_options{sizeof(source_options), XUI_FEATURE_VERSION, data.count, &data, query, retain, release};
    xui_handle source{}, foreign{};
    ok(xui_source_create(window, &source_options, &source));
    ok(xui_source_create(other, &source_options, &foreign));
    uint32_t count{}, active{}; double width{};
    ok(xui_miller_state(columns, &count, &active, &width)); expect(count == 0);
    expect(xui_miller_active(columns, 0) == XUI_INVALID_ARGUMENT);
    xui_miller_column path[]{ {sizeof(xui_miller_column), 1, text("Root"), source, 1, 7},
        {sizeof(xui_miller_column), 0, text("Child"), source} };
    ok(xui_miller_set_columns(columns, path, 2));
    ok(xui_miller_active(columns, 1));
    ok(xui_miller_width(columns, 320));
    ok(xui_miller_state(columns, &count, &active, &width)); expect(count == 2 && active == 1 && width == 320);
    double offset{}, maximum{};
    ok(xui_miller_scroll_state(columns, &offset, &maximum)); expect(offset == 0 && maximum == 0);
    ok(xui_miller_scroll(columns, 0));
    expect(xui_miller_scroll_state(columns, nullptr, &maximum) == XUI_INVALID_ARGUMENT);
    expect(xui_miller_scroll_state(columns, &offset, nullptr) == XUI_INVALID_ARGUMENT);
    expect(xui_miller_scroll(columns, -1) == XUI_INVALID_ARGUMENT);
    expect(xui_miller_scroll(columns, 1) == XUI_INVALID_ARGUMENT);
    expect(xui_miller_scroll(columns, NAN) == XUI_INVALID_ARGUMENT);
    expect(xui_miller_scroll(columns, INFINITY) == XUI_INVALID_ARGUMENT);
    expect(xui_miller_scroll(window, 0) == XUI_WRONG_KIND);
    expect(xui_miller_set_columns(columns, nullptr, 1) == XUI_INVALID_ARGUMENT);
    expect(xui_miller_set_columns(columns, path, 33) == XUI_INVALID_ARGUMENT);
    expect(xui_miller_width(columns, NAN) == XUI_INVALID_ARGUMENT);
    expect(xui_miller_width(columns, 119) == XUI_INVALID_ARGUMENT);
    auto bad = path[0]; bad.source = foreign;
    expect(xui_miller_set_columns(columns, &bad, 1) == XUI_INVALID_ARGUMENT);
    bad = path[0]; bad.selected_version = 8;
    expect(xui_miller_set_columns(columns, &bad, 1) == XUI_INVALID_ARGUMENT);
    ok(xui_miller_state(columns, &count, &active, &width)); expect(count == 2 && active == 1);
    xui_handle first{}, again{}; ok(xui_feature_child(columns, 0, &first)); ok(xui_feature_child(columns, 0, &again));
    expect(first == again);
    xui_event child_event{};
    ok(xui_subscribe(first, event, &child_event));
    xui_miller_event selected{};
    ok(xui_miller_subscribe(columns, miller_event, &selected));
    ok(xui_feature_action(first, XUI_A_SELECT, 42, 7));
    expect(selected.kind == XUI_SELECTION && selected.column == 0 && selected.id == 42 && selected.version == 7);
    expect(child_event.kind == XUI_SELECTION);
    expect(data.items < 100 && data.queries < 200);
    std::thread worker([&] {
        expect(xui_miller_active(columns, 0) == XUI_WRONG_THREAD);
        expect(xui_miller_scroll(columns, 0) == XUI_WRONG_THREAD);
        expect(xui_miller_scroll_state(columns, &offset, &maximum) == XUI_WRONG_THREAD);
    }); worker.join();
    ok(xui_source_release(source)); ok(xui_source_release(foreign));
    ok(xui_miller_set_columns(columns, nullptr, 0));
    xui_handle retained{}; ok(xui_feature_child(columns, 31, &retained)); expect(retained == reserved);
    ok(xui_window_destroy(window)); ok(xui_window_destroy(other));
    expect(data.refs == 1);
}
void collection_navigation_contracts() {
    xui_window_options options{sizeof(options), XUI_ABI_VERSION, text("Collection navigation"), 600, 400};
    xui_handle window{}; ok(xui_window_create(&options, &window));
    Source data; data.count = 20;
    xui_source_options source_options{sizeof(source_options), XUI_FEATURE_VERSION, data.count, &data, query, retain, release};
    xui_handle source{}; ok(xui_source_create(window, &source_options, &source));
    for (const auto kind : {XUI_ITEMS_VIEW, XUI_TREE_VIEW}) {
        const auto collection = create(window, kind);
        ok(xui_source_attach(collection, source));
        const auto focus = GetFocus();
        const auto navigate = [&](uint64_t direction, uint64_t modifiers = 0) {
            ok(xui_feature_action(collection, XUI_A_GRID_NAVIGATE, direction, modifiers));
            expect(GetFocus() == focus);
            return read_value(collection, XUI_F_SELECTION_STATE).first;
        };
        const auto contains = [&](uint64_t key) {
            uint32_t selected{}; ok(xui_collection_contains(collection, key, 7, &selected)); return selected != 0;
        };
        expect(navigate(4) == 1 && contains(1));
        expect(navigate(1, 1) == 2 && contains(1) && !contains(2));
        expect(navigate(1, 2) == 3 && contains(1) && contains(2) && contains(3));
        expect(navigate(5, 3) == 20 && contains(1) && contains(20));
        expect(navigate(0) == 19 && contains(19) && !contains(20));
        expect(navigate(2) < 19);
        expect(navigate(5) == 20 && navigate(4, 1) == 1 && contains(20) && !contains(1));
        expect(navigate(1, 3) == 2 && contains(20) && !contains(1) && contains(2));
        expect(xui_feature_action(collection, XUI_A_GRID_NAVIGATE, 6, 0) == XUI_INVALID_ARGUMENT);
        expect(xui_feature_action(collection, XUI_A_GRID_NAVIGATE, 0, 4) == XUI_INVALID_ARGUMENT);
        expect(read_value(collection, XUI_F_SELECTION_STATE).first == 2);
    }
    ok(xui_source_release(source)); ok(xui_window_destroy(window)); expect(data.refs == 1);
}
void tree_details_contracts() {
    xui_window_options options{sizeof(options), XUI_ABI_VERSION, text("Tree detail cells"), 600, 400};
    xui_handle window{}, root{}; ok(xui_window_create(&options, &window)); ok(xui_stack_create(window, 1, &root));
    const auto tree = create(window, XUI_TREE_VIEW);
    ok(xui_stack_add(root, tree, 1)); ok(xui_window_content(window, root));
    xui_column columns[]{
        {sizeof(xui_column), 0, text("Name"), 280},
        {sizeof(xui_column), 0, text("Date modified"), 160},
        {sizeof(xui_column), 0, text("Type"), 125},
        {sizeof(xui_column), 1, text("Size"), 100}
    };
    ok(xui_grid_columns(tree, columns, 4));
    auto bad = columns[0]; bad.flags = 2;
    expect(xui_grid_columns(tree, &bad, 1) == XUI_INVALID_ARGUMENT);
    bad = columns[0]; bad.width = 47;
    expect(xui_grid_columns(tree, &bad, 1) == XUI_INVALID_ARGUMENT);
    expect(xui_grid_columns(tree, columns, 65) == XUI_INVALID_ARGUMENT);
    expect(xui_grid_columns(tree, nullptr, 1) == XUI_INVALID_ARGUMENT);
    expect(xui_grid_columns(create(window, XUI_ITEMS_VIEW), columns, 4) == XUI_WRONG_KIND);
    ok(xui_grid_columns(tree, nullptr, 0)); ok(xui_grid_columns(tree, columns, 4));
    struct Cells { uint64_t first; unsigned refs{1}, seen{}, reads{}; } roots{1}, children{100};
    const auto make_source = [&](Cells& cells) {
        xui_source_options source_options{sizeof(source_options), XUI_FEATURE_VERSION, 3, &cells,
            [](void* context, uint32_t op, uint64_t first, uint64_t second, xui_source_row* row) -> xui_status {
                auto& cells = *static_cast<Cells*>(context);
                if (op == 0) { row->id = cells.first + first; row->version = 1; }
                if (op == 1) {
                    ++cells.reads; if (second < 32) cells.seen |= 1u << second;
                    const auto value = std::to_string(cells.first + first) + " column " + std::to_string(second);
                    row->primary_length = static_cast<uint32_t>(value.size());
                    std::memcpy(row->primary, value.data(), value.size());
                }
                if (op == 2) row->index = second == 1 && first >= cells.first && first - cells.first < 3 ? first - cells.first : UINT64_MAX;
                if (op == 3) row->index = first == 1;
                return XUI_OK;
            },
            [](void* context) { ++static_cast<Cells*>(context)->refs; },
            [](void* context) { --static_cast<Cells*>(context)->refs; }};
        xui_handle source{}; ok(xui_source_create(window, &source_options, &source)); return source;
    };
    const auto source = make_source(roots), child_source = make_source(children);
    ok(xui_source_attach(tree, source));
    xui_event request{}; ok(xui_subscribe(tree, event, &request));
    ok(xui_tree_expand(tree, 1, 1, 1)); expect(request.kind == XUI_REQUEST);
    ok(xui_tree_complete(tree, request.value, child_source, text("")));
    std::thread close([&] {
        Sleep(300);
        ok(xui_window_post(window, [](void* context, uint32_t execute) -> xui_status {
            return execute ? xui_window_close(*static_cast<xui_handle*>(context)) : XUI_OK;
        }, &window));
    });
    const auto status = xui_window_run(window); close.join(); ok(status);
    expect(roots.seen == 15 && children.seen == 15 && roots.reads < 500 && children.reads < 500);
    ok(xui_source_release(source)); ok(xui_source_release(child_source)); ok(xui_window_destroy(window));
    expect(roots.refs == 1 && children.refs == 1);
}
xui_status XUI_CALL posted(void* c, uint32_t execute) {
    auto& counts = *static_cast<std::pair<unsigned, unsigned>*>(c);
    if (execute) ++counts.first; else ++counts.second;
    return XUI_OK;
}
void retained_navigation_style_bridges() {
    xui_window_options options{sizeof(options), XUI_ABI_VERSION, text("Navigation bridges"), 500, 400};
    xui_handle window{};
    ok(xui_window_create_features(&options, 1, &window));
    const auto navigation = create(window, XUI_NAVIGATION_VIEW);
    const auto breadcrumb = create(window, XUI_BREADCRUMB);
    const auto bar = create(window, XUI_COMMAND_BAR);
    struct Child { xui_handle parent; uint32_t index, target; };
    const Child children[]{
        {window, 0, XUI_STYLE_TARGET_TAB_STRIP}, {window, 1, XUI_STYLE_TARGET_BUTTON},
        {window, 2, XUI_STYLE_TARGET_TAB_STRIP}, {window, 3, XUI_STYLE_TARGET_TITLE_BAR},
        {window, 4, XUI_STYLE_TARGET_LABEL}, {window, 5, XUI_STYLE_TARGET_BUTTON},
        {window, 6, XUI_STYLE_TARGET_BUTTON}, {window, 7, XUI_STYLE_TARGET_BUTTON},
        {navigation, 0, XUI_STYLE_TARGET_TEXT_INPUT}, {navigation, 1, XUI_STYLE_TARGET_BUTTON},
        {navigation, 2, XUI_STYLE_TARGET_NAVIGATION_LIST}, {navigation, 3, XUI_STYLE_TARGET_NAVIGATION_LIST},
        {navigation, 4, XUI_STYLE_TARGET_NAVIGATION_LIST}, {navigation, 5, XUI_STYLE_TARGET_LABEL},
        {navigation, 6, XUI_STYLE_TARGET_LABEL}, {breadcrumb, 0, XUI_STYLE_TARGET_BUTTON},
        {bar, 0, XUI_STYLE_TARGET_BUTTON}
    };
    xui_style_property property{sizeof(property), XUI_CONTROL_STYLE_VERSION, XUI_STYLE_BACKGROUND,
        XUI_STYLE_COLOR, XUI_STYLE_ROOT, 0, 0, {0x123456, 0x654321}};
    for (const auto& entry : children) {
        xui_handle child{}, again{}, style{};
        ok(xui_feature_child(entry.parent, entry.index, &child));
        ok(xui_feature_child(entry.parent, entry.index, &again)); expect(child == again);
        xui_control_style_options definition{sizeof(definition), XUI_CONTROL_STYLE_VERSION, entry.target, 0, &property, 1};
        ok(xui_control_style_create(window, &definition, &style));
        ok(xui_control_set_style(child, style));
        ok(xui_control_set_style_values(child, XUI_STYLE_ROOT, &property, 1));
        ok(xui_control_set_style(child, 0));
        xui_style_property values[8]{}; uint32_t count{};
        ok(xui_control_get_style_values(again, XUI_STYLE_ROOT, 1, values, 8, &count));
        expect(count == 1 && values[0].color.light == 0x123456);
        ok(xui_control_set_style_values(child, XUI_STYLE_ROOT, nullptr, 0));
        ok(xui_control_style_release(style));
    }
    xui_choice segments[]{
        {sizeof(xui_choice), 0, 10, 7, text("Root")},
        {sizeof(xui_choice), 0, 20, 9, text("Leaf")}
    };
    ok(xui_choices(breadcrumb, segments, 2, 0, 0));
    xui_handle segment{}, again{};
    ok(xui_breadcrumb_segment_button(breadcrumb, 20, 9, &segment));
    ok(xui_breadcrumb_segment_button(breadcrumb, 20, 9, &again)); expect(segment == again);
    ok(xui_control_set_style_values(segment, XUI_STYLE_ROOT, &property, 1));
    xui_event navigated{}; unsigned segment_clicks{};
    ok(xui_subscribe(breadcrumb, event, &navigated));
    ok(xui_subscribe(segment, count_event, &segment_clicks));
    ok(xui_subscribe(segment, count_event, &segment_clicks));
    ok(xui_invoke(segment)); expect(navigated.kind == XUI_SELECTION && navigated.value == 20 && segment_clicks == 1);
    std::swap(segments[0], segments[1]); segments[0].text = text("Renamed leaf");
    ok(xui_choices(breadcrumb, segments, 2, 0, 0));
    ok(xui_invoke(segment)); expect(navigated.value == 20 && segment_clicks == 2);
    ok(xui_breadcrumb_segment_button(breadcrumb, 20, 9, &again)); expect(again == segment);
    xui_style_property segment_values[8]{}; uint32_t segment_count{};
    ok(xui_control_get_style_values(segment, XUI_STYLE_ROOT, 1, segment_values, 8, &segment_count));
    expect(segment_count == 1 && segment_values[0].color.light == 0x123456);
    ok(xui_subscribe(segment, nullptr, nullptr));
    navigated = {}; ok(xui_invoke(segment)); expect(navigated.value == 20 && segment_clicks == 2);
    again = 99;
    expect(xui_breadcrumb_segment_button(breadcrumb, 20, 8, &again) == XUI_INVALID_ARGUMENT && again == 0);
    expect(xui_breadcrumb_segment_button(bar, 20, 9, &again) == XUI_WRONG_KIND && again == 0);
    expect(xui_breadcrumb_segment_button(breadcrumb, 20, 9, nullptr) == XUI_INVALID_ARGUMENT);
    ok(xui_subscribe(segment, count_event, &segment_clicks));
    segments[0].version = 10;
    ok(xui_choices(breadcrumb, segments, 2, 0, 0));
    navigated = {};
    expect(xui_invoke(segment) == XUI_INVALID_ARGUMENT && navigated.kind == 0 && segment_clicks == 2);
    ok(xui_subscribe(segment, count_event, &segment_clicks));
    expect(xui_invoke(segment) == XUI_INVALID_ARGUMENT && navigated.kind == 0 && segment_clicks == 2);
    expect(xui_breadcrumb_segment_button(breadcrumb, 20, 9, &again) == XUI_INVALID_ARGUMENT && again == 0);
    ok(xui_breadcrumb_segment_button(breadcrumb, 20, 10, &again)); expect(again != segment);
    xui_handle minimize{}; unsigned caption_clicks{};
    ok(xui_feature_child(window, 5, &minimize));
    ok(xui_subscribe(minimize, count_event, &caption_clicks));
    ok(xui_subscribe(minimize, count_event, &caption_clicks));
    ok(xui_invoke(minimize)); expect(caption_clicks == 1);
    ok(xui_subscribe(minimize, nullptr, nullptr));
    ok(xui_invoke(minimize)); expect(caption_clicks == 1);

    xui_command_record command{sizeof(command), 0, 42, 0, text("Command"), {}, {}, 6, 0};
    ok(xui_commands_set(bar, &command, 1));
    xui_handle button{}; ok(xui_command_bar_button(bar, 42, &button));
    ok(xui_control_set_style_values(button, XUI_STYLE_ROOT, &property, 1));
    unsigned actions{}, clicks{};
    ok(xui_subscribe(bar, count_event, &actions));
    for (const uint32_t flags : {6u, 0u, 4u, 6u}) {
        command.flags = flags; command.label = text("Refreshed");
        ok(xui_commands_set(bar, &command, 1));
        ok(xui_command_bar_button(bar, 42, &again)); expect(again == button);
        ok(xui_subscribe(button, count_event, &clicks));
        ok(xui_subscribe(button, count_event, &clicks));
        const auto before_actions = actions, before_clicks = clicks;
        ok(xui_invoke(button)); expect(actions == before_actions + 1 && clicks == before_clicks + 1);
        auto checked = value(); ok(xui_feature_get(button, XUI_F_BUTTON_CHECKED, &checked));
        expect(checked.first == ((flags & 2) != 0));
        ok(xui_subscribe(button, nullptr, nullptr));
        ok(xui_invoke(button)); expect(actions == before_actions + 2 && clicks == before_clicks + 1);
        xui_style_property values[8]{}; uint32_t count{};
        ok(xui_control_get_style_values(button, XUI_STYLE_ROOT, 1, values, 8, &count));
        expect(count == 1 && values[0].color.light == 0x123456);
    }
    // Refresh while subscribed: no second lookup or subscription may be required.
    ok(xui_subscribe(button, count_event, &clicks));
    for (const uint32_t flags : {0u, 4u, 6u}) {
        command.flags = flags; ok(xui_commands_set(bar, &command, 1));
        const auto before_actions = actions, before_clicks = clicks;
        ok(xui_invoke(button)); expect(actions == before_actions + 1 && clicks == before_clicks + 1);
    }
    ok(xui_window_visual_style_set(window, XUI_STYLE_WINUI));
    ok(xui_command_bar_button(bar, 42, &again)); expect(again == button);
    expect(xui_command_bar_button(bar, 99, &again) == XUI_INVALID_ARGUMENT && again == 0);
    expect(xui_command_bar_button(breadcrumb, 42, &again) == XUI_WRONG_KIND && again == 0);
    expect(xui_command_bar_button(bar, 42, nullptr) == XUI_INVALID_ARGUMENT);
    std::thread wrong_thread([&] {
        xui_handle result = 99;
        expect(xui_command_bar_button(bar, 42, &result) == XUI_WRONG_THREAD && result == 0);
    });
    wrong_thread.join();
    const auto retired_actions = actions, retired_clicks = clicks;
    ok(xui_commands_set(bar, nullptr, 0));
    expect(xui_invoke(button) == XUI_INVALID_ARGUMENT && actions == retired_actions && clicks == retired_clicks);
    command.flags = 1;
    ok(xui_commands_set(bar, &command, 1));
    xui_handle replacement{}; unsigned replacement_clicks{};
    ok(xui_command_bar_button(bar, 42, &replacement)); expect(replacement != button);
    ok(xui_subscribe(button, count_event, &clicks));
    ok(xui_subscribe(replacement, count_event, &replacement_clicks));
    expect(xui_invoke(button) == XUI_INVALID_ARGUMENT && xui_invoke(replacement) == XUI_INVALID_ARGUMENT);
    expect(actions == retired_actions && clicks == retired_clicks && replacement_clicks == 0);
    command.flags = 0; ok(xui_commands_set(bar, &command, 1));
    expect(xui_invoke(button) == XUI_INVALID_ARGUMENT);
    ok(xui_invoke(replacement));
    expect(actions == retired_actions + 1 && clicks == retired_clicks && replacement_clicks == 1);
    ok(xui_window_destroy(window));
    expect(xui_command_bar_button(bar, 42, &again) == XUI_INVALID_HANDLE && again == 0);
    expect(xui_invoke(button) == XUI_INVALID_HANDLE);
}
void retained_facade_style_contracts() {
    xui_window_options options{sizeof(options), XUI_ABI_VERSION, text("Retained facade styles"), 400, 300};
    xui_handle window{}, body{};
    ok(xui_window_create(&options, &window));
    ok(xui_stack_create(window, 0, &body));
    const auto dialog = create(window, XUI_CONTENT_DIALOG, body);
    const auto commands = create(window, XUI_COMMAND_SURFACE);
    xui_feature_options menu_options{sizeof(menu_options), XUI_FEATURE_VERSION, text("Order"), 0, 0, 1};
    xui_handle flyout{};
    ok(xui_feature_create(window, XUI_COMMAND_SURFACE, &menu_options, &flyout));
    ok(xui_popup_placement(flyout, 1));
    expect(xui_popup_placement(flyout, 7) == XUI_INVALID_ARGUMENT);
    for (const auto index : {0u, 1u, 3u}) {
        xui_handle absent{};
        expect(xui_feature_child(flyout, index, &absent) == XUI_WRONG_KIND && !absent);
    }
    xui_handle menu_status{};
    ok(xui_feature_child(flyout, 2, &menu_status));
    expect(read_value(menu_status, XUI_F_VISIBLE).first == 0);
    xui_command_record order_commands[]{
        {sizeof(xui_command_record), 0, 1, 0, text("Folders, then files"), {}, {}, 6, XUI_BUTTON_ICON_FOLDERS_FIRST},
        {sizeof(xui_command_record), 0, 2, 0, text("Files, then folders"), {}, {}, 4, XUI_BUTTON_ICON_FILES_FIRST},
        {sizeof(xui_command_record), 0, 3, 0, text("Mixed"), {}, {}, 4, XUI_BUTTON_ICON_MIXED}};
    ok(xui_commands_set(flyout, order_commands, 3));
    ok(xui_command_invoke(flyout, 3, 0));
    order_commands[2].icon = XUI_BUTTON_ICON_MIXED + 1;
    expect(xui_commands_set(flyout, order_commands, 3) == XUI_INVALID_ARGUMENT);
    menu_options.mode = 2;
    xui_handle invalid_flyout{};
    expect(xui_feature_create(window, XUI_COMMAND_SURFACE, &menu_options, &invalid_flyout) == XUI_INVALID_ARGUMENT && !invalid_flyout);
    const auto location = create(window, XUI_LOCATION_PICKER);
    const auto items = create(window, XUI_ITEMS_VIEW);
    const auto view = create(window, XUI_VIEW_PICKER, items);
    const auto pane = create(window, XUI_NAVIGATION_PANE);
    const auto combo = create(window, XUI_COMBO_BOX);
    xui_feature_options editable_options{sizeof(editable_options), XUI_FEATURE_VERSION, text("Editable")};
    editable_options.mode = 1;
    xui_handle editable_combo{};
    ok(xui_feature_create(window, XUI_COMBO_BOX, &editable_options, &editable_combo));
    const auto number = create(window, XUI_NUMERIC_INPUT);
    const auto status = create(window, XUI_INLINE_STATUS);
    const auto picker = create(window, XUI_COLOR_PICKER);
    struct Child { xui_handle parent; uint32_t index, target; };
    const Child children[] {
        {dialog, 0, XUI_STYLE_TARGET_BUTTON}, {dialog, 1, XUI_STYLE_TARGET_BUTTON},
        {dialog, 2, XUI_STYLE_TARGET_LABEL}, {dialog, 3, XUI_STYLE_TARGET_INLINE_STATUS},
        {dialog, 4, XUI_STYLE_TARGET_STACK}, {dialog, 5, XUI_STYLE_TARGET_STACK},
        {commands, 0, XUI_STYLE_TARGET_TEXT_INPUT}, {commands, 1, XUI_STYLE_TARGET_LABEL},
        {commands, 2, XUI_STYLE_TARGET_LABEL}, {commands, 3, XUI_STYLE_TARGET_BUTTON},
        {commands, 4, XUI_STYLE_TARGET_STACK}, {commands, 5, XUI_STYLE_TARGET_STACK},
        {commands, 6, XUI_STYLE_TARGET_COMMAND_MENU},
        {location, 0, XUI_STYLE_TARGET_TEXT_INPUT}, {location, 1, XUI_STYLE_TARGET_NAVIGATION_PANE},
        {location, 2, XUI_STYLE_TARGET_STACK}, {location, 3, XUI_STYLE_TARGET_LABEL},
        {location, 4, XUI_STYLE_TARGET_COMMAND_BAR},
        {view, 0, XUI_STYLE_TARGET_RADIO_GROUP}, {view, 1, XUI_STYLE_TARGET_RANGE_INPUT},
        {view, 2, XUI_STYLE_TARGET_STACK}, {pane, 0, XUI_STYLE_TARGET_ITEMS_VIEW},
        {pane, 1, XUI_STYLE_TARGET_LABEL}, {pane, 2, XUI_STYLE_TARGET_STACK},
        {pane, 3, XUI_STYLE_TARGET_EXPANDER}, {pane, 4, XUI_STYLE_TARGET_PROGRESS},
        {editable_combo, 0, XUI_STYLE_TARGET_TEXT_INPUT}, {combo, 1, XUI_STYLE_TARGET_POPUP},
        {combo, 2, XUI_STYLE_TARGET_CHOICE_LIST},
        {number, 0, XUI_STYLE_TARGET_TEXT_INPUT}, {number, 1, XUI_STYLE_TARGET_BUTTON},
        {number, 2, XUI_STYLE_TARGET_BUTTON}, {status, 0, XUI_STYLE_TARGET_BUTTON},
        {status, 1, XUI_STYLE_TARGET_BUTTON},
        {picker, 0, XUI_STYLE_TARGET_NUMERIC_INPUT}, {picker, 1, XUI_STYLE_TARGET_NUMERIC_INPUT},
        {picker, 2, XUI_STYLE_TARGET_NUMERIC_INPUT}, {picker, 3, XUI_STYLE_TARGET_NUMERIC_INPUT},
        {picker, 4, XUI_STYLE_TARGET_BUTTON}, {picker, 8, XUI_STYLE_TARGET_BUTTON}
    };
    xui_style_property property{sizeof(property), XUI_CONTROL_STYLE_VERSION, XUI_STYLE_BACKGROUND,
        XUI_STYLE_COLOR, XUI_STYLE_ROOT, 0, 0, {0x123456, 0x654321}};
    xui_control_style_options definition{sizeof(definition), XUI_CONTROL_STYLE_VERSION, 0, 0, &property, 1};
    for (const auto& entry : children) {
        xui_handle child{}, again{}, style{};
        ok(xui_feature_child(entry.parent, entry.index, &child));
        ok(xui_feature_child(entry.parent, entry.index, &again));
        expect(child == again);
        definition.target = entry.target;
        ok(xui_control_style_create(window, &definition, &style));
        ok(xui_control_set_style(child, style));
        auto local = property; local.color = {0, 0};
        ok(xui_control_set_style_values(child, XUI_STYLE_ROOT, &local, 1));
        ok(xui_control_set_style(child, 0));
        xui_style_property result[8]{}; uint32_t count{};
        ok(xui_control_get_style_values(again, XUI_STYLE_ROOT, 1, result, 8, &count));
        expect(count == 1 && result[0].property == XUI_STYLE_BACKGROUND && result[0].color.light == 0);
        ok(xui_control_set_style_values(child, XUI_STYLE_ROOT, nullptr, 0));
        ok(xui_control_style_release(style));
    }
    xui_handle absent = 99;
    ok(xui_feature_child(combo, 0, &absent)); expect(absent == 0);
    expect(xui_feature_child(picker, 9, &absent) == XUI_INVALID_ARGUMENT && absent == 0);
    expect(xui_feature_child(picker, UINT32_MAX, &absent) == XUI_INVALID_ARGUMENT && absent == 0);
    xui_handle red{}, increase{}, swatch{};
    ok(xui_feature_child(picker, 0, &red));
    ok(xui_feature_child(red, 2, &increase));
    ok(xui_feature_child(picker, 6, &swatch));
    unsigned changes{}, clicks{};
    ok(xui_subscribe(red, count_event, &changes));
    ok(xui_subscribe(red, count_event, &changes));
    ok(xui_subscribe(increase, count_event, &clicks));
    ok(xui_subscribe(increase, count_event, &clicks));
    ok(xui_feature_action(red, XUI_A_CHANGE_VALUE, std::bit_cast<uint64_t>(21.0), 0));
    auto color = value(); ok(xui_feature_get(picker, XUI_F_COLOR, &color));
    expect((color.first & 255) == 21 && changes == 1);
    ok(xui_invoke(increase));
    color = value(); ok(xui_feature_get(picker, XUI_F_COLOR, &color));
    expect((color.first & 255) == 22 && changes == 2 && clicks == 1);
    ok(xui_subscribe(swatch, count_event, &clicks));
    ok(xui_subscribe(swatch, count_event, &clicks));
    ok(xui_invoke(swatch));
    color = value(); ok(xui_feature_get(picker, XUI_F_COLOR, &color));
    expect(color.first == (220ull | 45ull << 8 | 45ull << 16 | 255ull << 24) && clicks == 2);
    definition.target = XUI_STYLE_TARGET_POPUP;
    xui_handle popup_style{};
    for (const auto state : {XUI_STYLE_STATE_INVALID, XUI_STYLE_STATE_LOADING, XUI_STYLE_STATE_ERROR,
        XUI_STYLE_STATE_SELECTED, XUI_STYLE_STATE_OVERFLOWED}) {
        property.state = state;
        expect(xui_control_style_create(window, &definition, &popup_style) == XUI_INVALID_ARGUMENT);
    }
    property.state = 0;
    ok(xui_control_style_create(window, &definition, &popup_style));
    for (const auto facade : {dialog, commands, location, view}) {
        ok(xui_control_set_style(facade, popup_style));
        ok(xui_control_set_style(facade, 0));
        xui_handle missing = 99;
        expect(xui_feature_child(facade, UINT32_MAX, &missing) == XUI_INVALID_ARGUMENT && missing == 0);
    }
    ok(xui_control_style_release(popup_style));
    for (const auto target : {XUI_STYLE_TARGET_CONTENT_DIALOG, XUI_STYLE_TARGET_COMMAND_SURFACE,
        XUI_STYLE_TARGET_LOCATION_PICKER, XUI_STYLE_TARGET_VIEW_PICKER}) {
        definition.target = target;
        xui_handle unsupported{};
        expect(xui_control_style_create(window, &definition, &unsupported) == XUI_INVALID_ARGUMENT);
    }
    xui_handle menu{};
    ok(xui_feature_child(commands, 6, &menu));
    xui_handle missing = 99;
    expect(xui_feature_child(menu, 0, &missing) == XUI_WRONG_KIND && missing == 0);
    xui_status wrong_thread{};
    xui_handle wrong_thread_child = 99;
    std::thread worker([&] { wrong_thread = xui_feature_child(dialog, 2, &wrong_thread_child); });
    worker.join(); expect(wrong_thread == XUI_WRONG_THREAD && wrong_thread_child == 0);
    xui_handle retained_title{};
    ok(xui_feature_child(dialog, 2, &retained_title));
    ok(xui_window_close(window));
    ok(xui_feature_child(dialog, 2, &body));
    expect(body == retained_title);
    expect(xui_window_post(window, [](void*, uint32_t) -> xui_status { return XUI_OK; }, nullptr) == XUI_CLOSED);
    ok(xui_window_destroy(window));
    body = 99;
    expect(xui_feature_child(dialog, 2, &body) == XUI_INVALID_HANDLE && body == 0);
}
void control_style_contracts() {
    static_assert(sizeof(xui_style_property) == 80);
    static_assert(sizeof(xui_control_style_options) == 40);
    xui_window_options options{sizeof(options), XUI_ABI_VERSION, text("Generic style ABI"), 400, 300};
    xui_handle window{}, other{}, toggle{}, button{}, style{};
    ok(xui_window_create(&options, &window)); ok(xui_window_create(&options, &other));
    ok(xui_create(window, XUI_TOGGLE, text("Toggle"), 0, &toggle));
    ok(xui_create(window, XUI_BUTTON, text("Button"), 0, &button));
    xui_style_property records[] {
        {sizeof(xui_style_property), XUI_CONTROL_STYLE_VERSION, XUI_STYLE_FOREGROUND, XUI_STYLE_COLOR,
            XUI_STYLE_ROOT, 0, 0, {0x123456, 0x654321}},
        {sizeof(xui_style_property), XUI_CONTROL_STYLE_VERSION, XUI_STYLE_SIZE, XUI_STYLE_NUMBER, XUI_STYLE_INDICATOR},
        {sizeof(xui_style_property), XUI_CONTROL_STYLE_VERSION, XUI_STYLE_BACKGROUND, XUI_STYLE_COLOR,
            XUI_STYLE_INDICATOR, 0, XUI_STYLE_CHECKED, {0x445566, 0x665544}}
    };
    records[1].number = 80;
    xui_control_style_options definition{sizeof(definition), XUI_CONTROL_STYLE_VERSION, XUI_STYLE_TARGET_TOGGLE,
        0, records, 3};
    ok(xui_control_style_create(window, &definition, &style));
    ok(xui_control_set_style(toggle, style));
    expect(xui_control_set_style(button, style) == XUI_INVALID_ARGUMENT);
    uint32_t count{};
    xui_style_property output[8]{};
    ok(xui_control_get_style_values(toggle, XUI_STYLE_LABEL, 1, output, 8, &count));
    expect(count == 1 && output[0].property == XUI_STYLE_FOREGROUND &&
        output[0].color.light == 0x123456 && output[0].color.dark == 0x654321);
    ok(xui_invoke(toggle));
    ok(xui_control_get_style_values(toggle, XUI_STYLE_INDICATOR, 1, output, 8, &count));
    expect(count == 2 && output[0].color.light == 0x445566 && output[1].number == 80);
    const auto unchanged = output[0];
    expect(xui_control_get_style_values(toggle, XUI_STYLE_INDICATOR, 1, output, 1, &count) == XUI_BUFFER_TOO_SMALL);
    expect(count == 2 && std::memcmp(&output[0], &unchanged, sizeof(unchanged)) == 0);
    ok(xui_control_get_style_values(toggle, XUI_STYLE_INDICATOR, 1, nullptr, 0, &count)); expect(count == 2);
    expect(xui_control_get_style_values(toggle, 99, 1, output, 8, &count) == XUI_INVALID_ARGUMENT);
    auto local = records[0]; local.color = {0, 0};
    ok(xui_control_set_style_values(toggle, XUI_STYLE_ROOT, &local, 1));
    ok(xui_control_set_style(toggle, 0));
    ok(xui_control_get_style_values(toggle, XUI_STYLE_LABEL, 1, output, 8, &count));
    expect(count == 1 && output[0].color.light == 0);
    ok(xui_control_set_style(toggle, style));
    for (int mutation = 0; mutation < 11; ++mutation) {
        auto bad = records[0];
        switch (mutation) {
            case 0: bad.size--; break;
            case 1: bad.version++; break;
            case 2: bad.property = 128; break;
            case 3: bad.value_type = XUI_STYLE_NUMBER; break;
            case 4: bad.part = 99; break;
            case 5: bad.reserved = 1; break;
            case 6: bad.state = 1ull << 40; break;
            case 7: bad.color.dark = 0x1000000; break;
            case 8: bad.number = 1; break;
            case 9: bad.text = text("unused"); break;
            case 10: bad.insets.top = 1; break;
        }
        auto invalid = definition; invalid.properties = &bad; invalid.property_count = 1;
        xui_handle failed = 123;
        expect(xui_control_style_create(window, &invalid, &failed) ==
            (mutation == 1 ? XUI_VERSION_MISMATCH : XUI_INVALID_ARGUMENT));
        expect(failed == 0);
    }
    auto duplicate = std::vector<xui_style_property>{records[0], records[0]};
    auto invalid = definition; invalid.properties = duplicate.data(); invalid.property_count = 2;
    xui_handle failed{};
    expect(xui_control_style_create(window, &invalid, &failed) == XUI_INVALID_ARGUMENT);
    invalid = definition; invalid.target = 99; invalid.property_count = 0;
    expect(xui_control_style_create(window, &invalid, &failed) == XUI_INVALID_ARGUMENT);
    invalid = definition; invalid.property_count = 2049;
    expect(xui_control_style_create(window, &invalid, &failed) == XUI_INVALID_ARGUMENT);
    invalid = definition; invalid.properties = nullptr;
    expect(xui_control_style_create(window, &invalid, &failed) == XUI_INVALID_ARGUMENT);
    auto bad_local = local; bad_local.state = XUI_STYLE_CHECKED;
    expect(xui_control_set_style_values(toggle, XUI_STYLE_ROOT, &bad_local, 1) == XUI_INVALID_ARGUMENT);
    bad_local = local; bad_local.part = XUI_STYLE_MARK;
    expect(xui_control_set_style_values(toggle, XUI_STYLE_ROOT, &bad_local, 1) == XUI_INVALID_ARGUMENT);
    ok(xui_control_get_style_values(toggle, XUI_STYLE_ROOT, 0, output, 8, &count));
    expect(count == 1 && output[0].color.light == 0);
    xui_handle foreign{};
    ok(xui_control_style_create(other, &definition, &foreign));
    expect(xui_control_set_style(toggle, foreign) == XUI_INVALID_ARGUMENT);
    invalid = definition; invalid.based_on = foreign;
    expect(xui_control_style_create(window, &invalid, &failed) == XUI_INVALID_ARGUMENT);
    expect(xui_control_style_release(button) == XUI_WRONG_KIND);
    std::thread worker([&] { expect(xui_control_set_style(toggle, style) == XUI_WRONG_THREAD); }); worker.join();
    ok(xui_control_style_release(style));
    expect(xui_control_style_release(style) == XUI_INVALID_HANDLE);
    xui_handle retained{};
    ok(xui_control_style_reacquire(window, style, &retained)); expect(retained && retained != style);
    ok(xui_control_style_release(retained));
    uint32_t applied{};
    ok(xui_control_try_set_style(toggle, style, &applied)); expect(applied == 1);
    ok(xui_control_set_style(toggle, 0));
    ok(xui_control_style_reacquire(window, style, &retained)); expect(!retained);
    ok(xui_control_try_set_style(toggle, style, &applied)); expect(!applied);
    ok(xui_control_set_style_values(toggle, XUI_STYLE_ROOT, nullptr, 0));
    ok(xui_control_get_style_values(toggle, XUI_STYLE_ROOT, 1, output, 8, &count)); expect(!count);
    ok(xui_window_destroy(window)); ok(xui_window_destroy(other));
    expect(xui_control_style_release(foreign) == XUI_INVALID_HANDLE);
}
void tooltip_style_contracts() {
    xui_window_options options{sizeof(options), XUI_ABI_VERSION, text("Tooltip style ABI"), 300, 200};
    xui_handle window{}, other{}, button{}, style{}, foreign{};
    ok(xui_window_create(&options, &window)); ok(xui_window_create(&options, &other));
    ok(xui_create(window, XUI_BUTTON, text("Button"), 0, &button));
    xui_style_property records[]{
        {sizeof(xui_style_property), XUI_CONTROL_STYLE_VERSION, XUI_STYLE_BACKGROUND, XUI_STYLE_COLOR,
            XUI_STYLE_ROOT, 0, 0, {1, 2}},
        {sizeof(xui_style_property), XUI_CONTROL_STYLE_VERSION, XUI_STYLE_FOREGROUND, XUI_STYLE_COLOR,
            XUI_STYLE_ROOT, 0, 0, {3, 4}},
        {sizeof(xui_style_property), XUI_CONTROL_STYLE_VERSION, XUI_STYLE_FOREGROUND, XUI_STYLE_COLOR,
            XUI_STYLE_ROOT, 0, XUI_STYLE_STATE_OPEN, {5, 6}}
    };
    xui_control_style_options definition{sizeof(definition), XUI_CONTROL_STYLE_VERSION, XUI_STYLE_TARGET_TOOLTIP,
        0, records, 3};
    ok(xui_control_style_create(window, &definition, &style));
    ok(xui_control_style_create(other, &definition, &foreign));
    ok(xui_window_set_tooltip_style(window, style));
    expect(xui_window_set_tooltip_style(button, style) == XUI_WRONG_KIND);
    expect(xui_control_set_style(window, style) == XUI_WRONG_KIND);
    expect(xui_control_set_style(button, style) == XUI_INVALID_ARGUMENT);
    expect(xui_window_set_tooltip_style(window, foreign) == XUI_INVALID_ARGUMENT);
    uint32_t count{};
    xui_style_property output[8]{};
    ok(xui_window_get_tooltip_style_values(window, XUI_STYLE_ROOT, 1, output, 8, &count));
    expect(count == 2 && output[1].color.light == 3); // Applying a style does not open the tooltip.
    const auto saved = output[0];
    expect(xui_window_get_tooltip_style_values(window, XUI_STYLE_ROOT, 1, output, 1, &count) == XUI_BUFFER_TOO_SMALL);
    expect(count == 2 && std::memcmp(&saved, &output[0], sizeof(saved)) == 0);
    auto local = records[1]; local.color = {7, 8};
    ok(xui_window_set_tooltip_style_values(window, XUI_STYLE_ROOT, &local, 1));
    auto invalid = local; invalid.state = XUI_STYLE_STATE_OPEN;
    expect(xui_window_set_tooltip_style_values(window, XUI_STYLE_ROOT, &invalid, 1) == XUI_INVALID_ARGUMENT);
    invalid = local; invalid.part = XUI_STYLE_TEXT_PART;
    expect(xui_window_set_tooltip_style_values(window, XUI_STYLE_ROOT, &invalid, 1) == XUI_INVALID_ARGUMENT);
    expect(xui_window_get_tooltip_style_values(window, XUI_STYLE_INDICATOR, 1, output, 8, &count) == XUI_INVALID_ARGUMENT);
    xui_style_property font{sizeof(font), XUI_CONTROL_STYLE_VERSION, XUI_STYLE_FONT_FAMILY, XUI_STYLE_TEXT,
        XUI_STYLE_TEXT_PART};
    font.text = text("Segoe UI");
    ok(xui_window_set_tooltip_style_values(window, XUI_STYLE_TEXT_PART, &font, 1));
    ok(xui_window_get_tooltip_style_values(window, XUI_STYLE_TEXT_PART, 0, output, 8, &count));
    expect(count == 1 && output[0].text.length == 8 && std::memcmp(output[0].text.data, "Segoe UI", 8) == 0);
    std::thread worker([&] {
        expect(xui_window_set_tooltip_style(window, 0) == XUI_WRONG_THREAD);
        expect(xui_window_get_tooltip_style_values(window, XUI_STYLE_ROOT, 1, nullptr, 0, &count) == XUI_WRONG_THREAD);
    }); worker.join();
    ok(xui_control_style_release(style));
    uint32_t applied{};
    ok(xui_window_try_set_tooltip_style(window, style, &applied)); expect(applied == 1);
    ok(xui_window_set_tooltip_style(window, 0));
    ok(xui_window_get_tooltip_style_values(window, XUI_STYLE_ROOT, 1, output, 8, &count));
    expect(count == 1 && output[0].color.light == 7);
    ok(xui_window_try_set_tooltip_style(window, style, &applied)); expect(applied == 0);
    ok(xui_window_set_tooltip_style_values(window, XUI_STYLE_ROOT, nullptr, 0));
    ok(xui_window_set_tooltip_style_values(window, XUI_STYLE_TEXT_PART, nullptr, 0));
    ok(xui_window_get_tooltip_style_values(window, XUI_STYLE_ROOT, 1, nullptr, 0, &count)); expect(count == 0);
    ok(xui_window_destroy(window)); ok(xui_window_destroy(other));
    expect(xui_window_set_tooltip_style(window, 0) == XUI_INVALID_HANDLE);
    expect(xui_control_style_release(foreign) == XUI_INVALID_HANDLE);
}
void explorer_contracts() {
    static_assert(sizeof(xui_button_style_values) == 80);
    static_assert(sizeof(xui_button_style_rule) == 88);
    static_assert(sizeof(xui_button_style_options) == 112);
    static_assert(sizeof(xui_navigation_entry) == 56);
    static_assert(sizeof(xui_item_visual) == 24);
    static_assert(sizeof(xui_source_options) == 48);
    static_assert(sizeof(xui_key_event) == 24);
    static_assert(sizeof(xui_navigation_event) == 32);
    xui_window_options options{sizeof(options), XUI_ABI_VERSION, text("Explorer primitives"), 600, 400};
    xui_handle window{}; ok(xui_window_create_features(&options, 1, &window));
    {
        xui_handle button{}, style{}, derived{}, foreign{}, other_window{};
        ok(xui_create(window, XUI_BUTTON, text("Styled"), 0, &button));
        auto empty = [] { return xui_button_style_values{sizeof(xui_button_style_values), XUI_BUTTON_STYLE_VERSION}; };
        xui_button_style_options definition{sizeof(definition), XUI_BUTTON_STYLE_VERSION, empty()};
        definition.values.mask = XUI_BUTTON_STYLE_BACKGROUND | XUI_BUTTON_STYLE_PADDING;
        definition.values.background = {0x123456, 0x654321};
        definition.values.padding = {1, 2, 3, 4};
        ok(xui_button_style_create(window, &definition, &style));
        auto inherited = definition;
        inherited.values = empty(); inherited.values.mask = XUI_BUTTON_STYLE_CORNER_RADIUS;
        inherited.values.corner_radius = 4; inherited.based_on = style;
        ok(xui_button_style_create(window, &inherited, &derived));
        auto local = empty(); local.mask = XUI_BUTTON_STYLE_CORNER_RADIUS; local.corner_radius = 7;
        ok(xui_button_set_style_values(button, &local));
        ok(xui_button_set_style(button, derived));
        auto output = empty(); ok(xui_button_get_style_values(button, 0, &output));
        expect(output.mask == local.mask && output.corner_radius == 7);
        ok(xui_button_get_style_values(button, 1, &output));
        expect(output.mask == 49 && output.background.light == 0x123456 && output.corner_radius == 7);
        auto invalid = local; invalid.corner_radius = NAN;
        expect(xui_button_set_style_values(button, &invalid) == XUI_INVALID_ARGUMENT);
        invalid = local; invalid.size = 0;
        expect(xui_button_set_style_values(button, &invalid) == XUI_INVALID_ARGUMENT);
        invalid = local; invalid.version = 0;
        expect(xui_button_set_style_values(button, &invalid) == XUI_VERSION_MISMATCH);
        invalid = local; invalid.background.light = 1;
        expect(xui_button_set_style_values(button, &invalid) == XUI_INVALID_ARGUMENT);
        invalid = local; invalid.mask |= 1; invalid.background.dark = 0xff123456;
        expect(xui_button_set_style_values(button, &invalid) == XUI_INVALID_ARGUMENT);
        invalid = local; invalid.mask |= 8; invalid.border_thickness.left = -1;
        expect(xui_button_set_style_values(button, &invalid) == XUI_INVALID_ARGUMENT);
        invalid = local; invalid.reserved = 1;
        expect(xui_button_set_style_values(button, &invalid) == XUI_INVALID_ARGUMENT);
        invalid = local; invalid.mask = 64;
        expect(xui_button_set_style_values(button, &invalid) == XUI_INVALID_ARGUMENT);
        expect(xui_button_set_style_values(button, nullptr) == XUI_INVALID_ARGUMENT);
        expect(xui_button_set_style_values(window, &local) == XUI_WRONG_KIND);
        expect(xui_button_get_style_values(button, 2, &output) == XUI_INVALID_ARGUMENT);
        expect(xui_button_get_style_values(button, 0, nullptr) == XUI_INVALID_ARGUMENT);
        xui_handle failed = 123;
        auto bad_definition = definition; bad_definition.size = 0;
        expect(xui_button_style_create(window, &bad_definition, &failed) == XUI_INVALID_ARGUMENT && failed == 0);
        bad_definition = definition; bad_definition.based_on = button;
        expect(xui_button_style_create(window, &bad_definition, &failed) == XUI_WRONG_KIND && failed == 0);
        bad_definition = definition; bad_definition.rule_count = 1;
        expect(xui_button_style_create(window, &bad_definition, &failed) == XUI_INVALID_ARGUMENT);
        xui_button_style_rule rule{sizeof(rule), 5, empty()};
        bad_definition.rules = &rule;
        expect(xui_button_style_create(window, &bad_definition, &failed) == XUI_INVALID_ARGUMENT);
        rule.state = XUI_BUTTON_STYLE_DISABLED; rule.size = 0;
        expect(xui_button_style_create(window, &bad_definition, &failed) == XUI_INVALID_ARGUMENT);
        bad_definition = definition; bad_definition.rule_count = 257;
        expect(xui_button_style_create(window, &bad_definition, &failed) == XUI_INVALID_ARGUMENT);
        xui_handle deepest{};
        auto layer = definition;
        layer.values = empty();
        for (unsigned depth = 0; depth < 16; ++depth) {
            layer.based_on = deepest;
            xui_handle next{};
            ok(xui_button_style_create(window, &layer, &next));
            if (deepest) ok(xui_button_style_release(deepest));
            deepest = next;
        }
        layer.based_on = deepest;
        expect(xui_button_style_create(window, &layer, &failed) == XUI_INVALID_ARGUMENT && !failed);
        ok(xui_button_style_release(deepest));
        rule = {sizeof(rule), XUI_BUTTON_STYLE_DISABLED, empty()};
        rule.values.mask = XUI_BUTTON_STYLE_FOREGROUND;
        rule.values.foreground = {0xffffff, 0};
        bad_definition = definition; bad_definition.rules = &rule; bad_definition.rule_count = 1;
        xui_handle state_style{};
        ok(xui_button_style_create(window, &bad_definition, &state_style));
        ok(xui_button_set_style(button, state_style));
        xui_property disabled{sizeof(xui_property), XUI_ENABLED, button};
        ok(xui_update(window, &disabled, 1));
        ok(xui_button_get_style_values(button, 1, &output));
        expect((output.mask & 2) && output.foreground.light == 0xffffff);
        disabled.integer = 1; ok(xui_update(window, &disabled, 1));
        ok(xui_button_set_style(button, derived)); ok(xui_button_style_release(state_style));
        ok(xui_window_create_features(&options, 0, &other_window));
        ok(xui_button_style_create(other_window, &definition, &foreign));
        expect(xui_button_set_style(button, foreign) == XUI_INVALID_ARGUMENT);
        bad_definition = definition; bad_definition.based_on = foreign;
        expect(xui_button_style_create(window, &bad_definition, &failed) == XUI_INVALID_ARGUMENT);
        expect(xui_button_set_style(button, window) == XUI_WRONG_KIND);
        expect(xui_button_style_release(button) == XUI_WRONG_KIND);
        std::thread worker([&] {
            expect(xui_button_set_style(button, 0) == XUI_WRONG_THREAD);
            expect(xui_button_style_release(style) == XUI_WRONG_THREAD);
        }); worker.join();
        ok(xui_button_get_style_values(button, 0, &output)); expect(output.corner_radius == 7 && output.mask == 32);
        ok(xui_button_style_release(style)); ok(xui_button_style_release(derived));
        expect(xui_button_style_release(style) == XUI_INVALID_HANDLE);
        expect(xui_button_set_style(button, style) == XUI_INVALID_HANDLE);
        xui_handle reused{}, peer{};
        uint32_t applied{99};
        ok(xui_create(window, XUI_BUTTON, text("Shared identity"), 0, &peer));
        ok(xui_button_try_set_style(peer, derived, &applied)); expect(applied == 1);
        ok(xui_button_set_style(button, 0));
        ok(xui_button_try_set_style(button, derived, &applied)); expect(applied == 1);
        ok(xui_button_style_reacquire(window, derived, &reused)); expect(reused != 0 && reused != derived);
        ok(xui_button_set_style(peer, 0));
        ok(xui_button_set_style(button, 0));
        ok(xui_button_set_style(button, reused));
        ok(xui_button_style_release(reused));
        ok(xui_button_try_set_style(peer, derived, &applied)); expect(applied == 1);
        ok(xui_button_set_style(peer, 0));
        ok(xui_button_style_reacquire(other_window, derived, &reused)); expect(reused == 0);
        ok(xui_button_try_set_style(button, foreign, &applied)); expect(applied == 0);
        ok(xui_button_try_set_style(button, 0, &applied)); expect(applied == 0);
        ok(xui_button_try_set_style(button, UINT64_MAX, &applied)); expect(applied == 0);
        expect(xui_button_try_set_style(button, derived, nullptr) == XUI_INVALID_ARGUMENT);
        expect(xui_button_style_reacquire(window, derived, nullptr) == XUI_INVALID_ARGUMENT);
        expect(xui_button_try_set_style(window, derived, &applied) == XUI_WRONG_KIND && applied == 0);
        std::thread identity_worker([&] {
            uint32_t result{99}; xui_handle retained{99};
            expect(xui_button_try_set_style(button, derived, &result) == XUI_WRONG_THREAD && result == 0);
            expect(xui_button_style_reacquire(window, derived, &retained) == XUI_WRONG_THREAD && retained == 0);
        }); identity_worker.join();
        bad_definition = definition; bad_definition.based_on = style;
        expect(xui_button_style_create(window, &bad_definition, &failed) == XUI_INVALID_HANDLE);
        ok(xui_button_get_style_values(button, 1, &output));
        expect(output.mask == 49 && output.background.dark == 0x654321 && output.corner_radius == 7);
        ok(xui_button_set_style(button, 0));
        ok(xui_button_try_set_style(button, derived, &applied)); expect(applied == 0);
        ok(xui_button_style_reacquire(window, derived, &reused)); expect(reused == 0);
        ok(xui_button_get_style_values(button, 1, &output)); expect(output.mask == 32 && output.corner_radius == 7);
        local = empty(); ok(xui_button_set_style_values(button, &local));
        ok(xui_button_get_style_values(button, 1, &output)); expect(output.mask == 0);
        ok(xui_button_set_style(button, 0));
        xui_handle permanent{};
        ok(xui_button_style_create(window, &definition, &permanent));
        ok(xui_button_set_style(peer, permanent));
        ok(xui_button_style_release(permanent));
        for (unsigned i = 0; i < 65537; ++i) {
            xui_handle transient{};
            ok(xui_button_style_create(window, &definition, &transient));
            ok(xui_button_set_style(button, transient));
            ok(xui_button_style_release(transient));
            ok(xui_button_set_style(button, 0));
        }
        ok(xui_button_try_set_style(button, permanent, &applied)); expect(applied == 1);
        ok(xui_button_set_style(peer, 0));
        ok(xui_button_set_style(button, 0));
        ok(xui_button_style_reacquire(window, permanent, &reused)); expect(reused == 0);
        ok(xui_window_destroy(other_window));
        expect(xui_button_style_release(foreign) == XUI_INVALID_HANDLE);
    }
    uint32_t style{};
    ok(xui_window_visual_style_get(window, &style)); expect(style == XUI_STYLE_CLASSIC);
    ok(xui_window_visual_style_set(window, XUI_STYLE_WINUI));
    ok(xui_window_visual_style_get(window, &style)); expect(style == XUI_STYLE_WINUI);
    expect(xui_window_visual_style_set(window, 2) == XUI_INVALID_ARGUMENT);
    ok(xui_window_visual_style_get(window, &style)); expect(style == XUI_STYLE_WINUI);
    expect(xui_window_visual_style_get(window, nullptr) == XUI_INVALID_ARGUMENT);
    std::thread style_worker([&] {
        uint32_t worker_style{};
        expect(xui_window_visual_style_set(window, XUI_STYLE_CLASSIC) == XUI_WRONG_THREAD);
        expect(xui_window_visual_style_get(window, &worker_style) == XUI_WRONG_THREAD);
    });
    style_worker.join();
    ok(xui_window_visual_style_set(window, XUI_STYLE_CLASSIC));
    ok(xui_window_visual_style_get(window, &style)); expect(style == XUI_STYLE_CLASSIC);
    ok(xui_window_title(window, text("Updated title")));
    xui_handle tabs{}, leading{}, second{}, again{};
    ok(xui_feature_child(window, 0, &tabs)); ok(xui_feature_child(window, 1, &leading));
    ok(xui_feature_child(window, 2, &second)); ok(xui_feature_child(window, 0, &again));
    expect(tabs == again && tabs != second);
    xui_choice tab_items[]{{sizeof(xui_choice), 0, 71, 0, text("Folder")},
        {sizeof(xui_choice), 0, 72, 0, text("Other")}};
    xui_item_visual tab_visuals[]{{sizeof(xui_item_visual), 15, text("C:\\")},
        {sizeof(xui_item_visual), 0, text("")}};
    ok(xui_tab_items_visual(tabs, tab_items, tab_visuals, 2, 72, 1));
    ok(xui_tab_items_visual(tabs, tab_items, nullptr, 2, 71, 1));
    expect(xui_tab_items_visual(leading, tab_items, tab_visuals, 2, 71, 1) == XUI_WRONG_KIND);
    expect(xui_tab_items_visual(tabs, nullptr, tab_visuals, 2, 71, 1) == XUI_INVALID_ARGUMENT);
    expect(xui_tab_items_visual(tabs, tab_items, tab_visuals, 4097, 71, 1) == XUI_INVALID_ARGUMENT);
    expect(xui_tab_items_visual(tabs, tab_items, tab_visuals, 2, 99, 1) == XUI_INVALID_ARGUMENT);
    expect(xui_tab_items_visual(tabs, tab_items, tab_visuals, 2, 71, 0) == XUI_INVALID_ARGUMENT);
    tab_visuals[0].size = 0;
    expect(xui_tab_items_visual(tabs, tab_items, tab_visuals, 2, 71, 1) == XUI_VERSION_MISMATCH);
    tab_visuals[0].size = sizeof(xui_item_visual); tab_visuals[0].icon = 999;
    expect(xui_tab_items_visual(tabs, tab_items, tab_visuals, 2, 71, 1) == XUI_INVALID_ARGUMENT);
    tab_visuals[0].icon = 15;
    tab_items[1].id = 71;
    expect(xui_tab_items_visual(tabs, tab_items, tab_visuals, 2, 71, 1) == XUI_INVALID_ARGUMENT);
    tab_items[1].id = 72;
    ok(xui_choices(tabs, tab_items, 2, 71, 1));
    ok(xui_tab_items_visual(tabs, nullptr, nullptr, 0, 0, 0));
    xui_handle new_tab{}, second_new_tab{};
    ok(xui_feature_child(tabs, 0, &new_tab));
    ok(xui_feature_child(tabs, 0, &again));
    ok(xui_feature_child(second, 0, &second_new_tab));
    expect(new_tab == again && new_tab != second_new_tab);
    expect(xui_feature_child(tabs, 1, &again) == XUI_INVALID_ARGUMENT);
    xui_button_style_values icon_style{};
    icon_style.size = sizeof(icon_style);
    icon_style.version = XUI_BUTTON_STYLE_VERSION;
    icon_style.mask = XUI_BUTTON_STYLE_BORDER_THICKNESS;
    ok(xui_button_set_style_values(new_tab, &icon_style));
    uint32_t new_button = 9;
    ok(xui_tab_get_new_button(tabs, &new_button)); expect(new_button == 0);
    ok(xui_tab_set_new_button(tabs, 1));
    ok(xui_tab_get_new_button(tabs, &new_button)); expect(new_button == 1);
    expect(xui_tab_set_new_button(tabs, 2) == XUI_INVALID_ARGUMENT);
    ok(xui_tab_get_new_button(tabs, &new_button)); expect(new_button == 1);
    expect(xui_tab_get_new_button(tabs, nullptr) == XUI_INVALID_ARGUMENT);
    expect(xui_tab_set_new_button(leading, 1) == XUI_WRONG_KIND);
    std::thread tab_worker([&] {
        uint32_t worker_visible{};
        expect(xui_tab_items_visual(tabs, tab_items, tab_visuals, 2, 71, 1) == XUI_WRONG_THREAD);
        expect(xui_tab_set_new_button(tabs, 0) == XUI_WRONG_THREAD);
        expect(xui_tab_get_new_button(tabs, &worker_visible) == XUI_WRONG_THREAD);
    });
    tab_worker.join();
    ok(xui_tab_set_new_button(tabs, 0));
    static_assert(sizeof(xui_tab_colors) == 40);
    xui_tab_colors tab_colors{sizeof(xui_tab_colors), XUI_TAB_COLORS_VERSION, 127,
        0x123456, 0, 0xffffff, 0x234567, 0xeeeeee, 0x345678, 0x456789};
    ok(xui_tab_set_colors(tabs, &tab_colors));
    xui_tab_colors read_colors{sizeof(xui_tab_colors), XUI_TAB_COLORS_VERSION};
    ok(xui_tab_get_colors(tabs, &read_colors));
    expect(std::memcmp(&tab_colors, &read_colors, sizeof(tab_colors)) == 0);
    auto invalid_colors = tab_colors; invalid_colors.border = 0xff123456;
    expect(xui_tab_set_colors(tabs, &invalid_colors) == XUI_INVALID_ARGUMENT);
    invalid_colors = tab_colors; invalid_colors.mask = 128;
    expect(xui_tab_set_colors(tabs, &invalid_colors) == XUI_INVALID_ARGUMENT);
    invalid_colors = tab_colors; invalid_colors.mask = 0;
    expect(xui_tab_set_colors(tabs, &invalid_colors) == XUI_INVALID_ARGUMENT);
    invalid_colors = tab_colors; invalid_colors.version = 0;
    expect(xui_tab_set_colors(tabs, &invalid_colors) == XUI_VERSION_MISMATCH);
    invalid_colors = tab_colors; invalid_colors.size = 0;
    expect(xui_tab_set_colors(tabs, &invalid_colors) == XUI_INVALID_ARGUMENT);
    expect(xui_tab_set_colors(tabs, nullptr) == XUI_INVALID_ARGUMENT);
    expect(xui_tab_get_colors(tabs, nullptr) == XUI_INVALID_ARGUMENT);
    expect(xui_tab_set_colors(leading, &tab_colors) == XUI_WRONG_KIND);
    ok(xui_tab_get_colors(tabs, &read_colors));
    expect(std::memcmp(&tab_colors, &read_colors, sizeof(tab_colors)) == 0);
    tab_colors = {sizeof(xui_tab_colors), XUI_TAB_COLORS_VERSION};
    ok(xui_tab_set_colors(tabs, &tab_colors)); ok(xui_tab_get_colors(tabs, &read_colors));
    expect(read_colors.mask == 0);
    auto v = value(); ok(xui_feature_get(second, XUI_F_VISIBLE, &v)); expect(!v.first);
    v = value(); v.first = 1; ok(xui_feature_set(second, XUI_F_VISIBLE, &v));
    expect(xui_feature_child(window, 8, &again) == XUI_INVALID_ARGUMENT);
    auto navigation = create(window, XUI_NAVIGATION_VIEW);
    ok(xui_window_navigation_handler(window, nullptr, nullptr));
    expect(xui_window_navigation_handler(navigation, nullptr, nullptr) == XUI_WRONG_KIND);
    auto first_pane = create(window, XUI_GRID), second_pane = create(window, XUI_GRID);
    expect(xui_window_visual_style_set(first_pane, XUI_STYLE_WINUI) == XUI_WRONG_KIND);
    expect(xui_window_visual_style_get(first_pane, &style) == XUI_WRONG_KIND);
    ok(xui_window_titlebar_layout(window, first_pane, second_pane, 0));
    expect(xui_window_titlebar_layout(window, first_pane, first_pane, 0) == XUI_INVALID_ARGUMENT);
    expect(xui_window_titlebar_layout(window, first_pane, second_pane, 2) == XUI_INVALID_ARGUMENT);
    expect(xui_window_titlebar_layout(window, window, 0, 0) == XUI_WRONG_KIND);
    xui_handle plain{};
    ok(xui_window_create_features(&options, 0, &plain));
    auto foreign_pane = create(plain, XUI_GRID);
    expect(xui_window_titlebar_layout(window, foreign_pane, second_pane, 0) == XUI_INVALID_ARGUMENT);
    expect(xui_window_titlebar_layout(plain, foreign_pane, 0, 0) == XUI_WRONG_KIND);
    ok(xui_window_destroy(plain));
    ok(xui_navigation_header(navigation, 0));
    ok(xui_navigation_header(navigation, 1));
    expect(xui_navigation_header(navigation, 2) == XUI_INVALID_ARGUMENT);
    expect(xui_navigation_header(first_pane, 0) == XUI_WRONG_KIND);
    auto centered_popup = create(window, XUI_POPUP, create(window, XUI_GRID));
    ok(xui_popup_placement(centered_popup, 4));
    ok(xui_popup_placement(centered_popup, 5));
    ok(xui_popup_placement(centered_popup, 6));
    expect(xui_popup_placement(centered_popup, 7) == XUI_INVALID_ARGUMENT);
    expect(xui_popup_placement(first_pane, 4) == XUI_WRONG_KIND);
    ok(xui_popup_window_background(centered_popup, 1));
    ok(xui_popup_window_background(centered_popup, 0));
    expect(xui_popup_window_background(centered_popup, 2) == XUI_INVALID_ARGUMENT);
    expect(xui_popup_window_background(first_pane, 1) == XUI_WRONG_KIND);
    auto shortcut_items = create(window, XUI_ITEMS_VIEW);
    expect(read_value(shortcut_items, XUI_F_SINGLE_CLICK_ACTIVATION).first == 0);
    auto activation = value(); activation.first = 1;
    ok(xui_feature_set(shortcut_items, XUI_F_SINGLE_CLICK_ACTIVATION, &activation));
    expect(read_value(shortcut_items, XUI_F_SINGLE_CLICK_ACTIVATION).first == 1);
    activation.first = 2;
    expect(xui_feature_set(shortcut_items, XUI_F_SINGLE_CLICK_ACTIVATION, &activation) == XUI_INVALID_ARGUMENT);
    expect(read_value(shortcut_items, XUI_F_SINGLE_CLICK_ACTIVATION).first == 1);
    activation.first = 0;
    ok(xui_feature_set(shortcut_items, XUI_F_SINGLE_CLICK_ACTIVATION, &activation));
    expect(xui_feature_set(first_pane, XUI_F_SINGLE_CLICK_ACTIVATION, &activation) == XUI_WRONG_KIND);
    ok(xui_items_trailing_shortcut_badges(shortcut_items, 1));
    ok(xui_items_trailing_shortcut_badges(shortcut_items, 0));
    expect(xui_items_trailing_shortcut_badges(shortcut_items, 2) == XUI_INVALID_ARGUMENT);
    expect(xui_items_trailing_shortcut_badges(first_pane, 1) == XUI_WRONG_KIND);
    xui_handle filter_input{};
    ok(xui_feature_child(navigation, 0, &filter_input));
    ok(xui_text_input_caption(filter_input, 0));
    ok(xui_text_input_placeholder(filter_input, text("Search commands")));
    expect(xui_text_input_caption(filter_input, 2) == XUI_INVALID_ARGUMENT);
    expect(xui_text_input_placeholder(first_pane, text("")) == XUI_WRONG_KIND);
    float x{}, y{}, width{}, height{};
    ok(xui_element_bounds(first_pane, &x, &y, &width, &height));
    expect(width == 0 && height == 0);
    expect(xui_element_bounds(first_pane, nullptr, &y, &width, &height) == XUI_INVALID_ARGUMENT);
    expect(xui_element_bounds(window, &x, &y, &width, &height) == XUI_WRONG_KIND);
    xui_navigation_entry entries[] {
        {sizeof(xui_navigation_entry), 2, 1, 0, text("Group"), text("")},
        {sizeof(xui_navigation_entry), 0, 2, 1, text("Home"), text("folder")}
    };
    ok(xui_navigation_items(navigation, entries, 2));
    ok(xui_navigation_hover_delay(navigation, 1000));
    expect(xui_navigation_hover_delay(navigation, 99) == XUI_INVALID_ARGUMENT);
    expect(xui_navigation_hover_delay(navigation, 60001) == XUI_INVALID_ARGUMENT);
    expect(xui_navigation_hover_delay(window, 1000) == XUI_WRONG_KIND);
    uint32_t hover_applied = 1;
    ok(xui_navigation_hover_help(navigation, 2, text("Stale help"), &hover_applied));
    expect(hover_applied == 0);
    expect(xui_navigation_hover_help(navigation, 2, text("Help"), nullptr) == XUI_INVALID_ARGUMENT);
    expect(xui_navigation_hover_help(window, 2, text("Help"), &hover_applied) == XUI_WRONG_KIND);
    xui_item_visual visuals[] {{sizeof(xui_item_visual), 18, text("")}, {sizeof(xui_item_visual), 15, text("folder")}};
    ok(xui_navigation_items_visual(navigation, entries, visuals, 2));
    visuals[1].size = 0;
    expect(xui_navigation_items_visual(navigation, entries, visuals, 2) == XUI_VERSION_MISMATCH);
    visuals[1].size = sizeof(xui_item_visual);
    static_assert(XUI_BUTTON_ICON_DRIVE == 21 && XUI_BUTTON_ICON_OPEN == 22);
    static_assert(XUI_BUTTON_ICON_SAVE == 23 && XUI_BUTTON_ICON_CHEVRON_DOWN == 28 && XUI_BUTTON_ICON_CHEVRON_RIGHT == 29);
    for (uint32_t icon = 19; icon <= XUI_BUTTON_ICON_MIXED; ++icon) {
        visuals[1].icon = icon;
        ok(xui_navigation_items_visual(navigation, entries, visuals, 2));
        tab_visuals[0].icon = icon;
        ok(xui_tab_items_visual(tabs, tab_items, tab_visuals, 2, 71, 1));
        auto button_icon = value(); button_icon.first = icon;
        ok(xui_feature_set(leading, XUI_F_BUTTON_ICON, &button_icon));
        button_icon = value();
        ok(xui_feature_get(leading, XUI_F_BUTTON_ICON, &button_icon)); expect(button_icon.first == icon);
    }
    auto invalid_icon = value(); invalid_icon.first = XUI_BUTTON_ICON_MIXED + 1;
    expect(xui_feature_set(leading, XUI_F_BUTTON_ICON, &invalid_icon) == XUI_INVALID_ARGUMENT);
    invalid_icon.first = UINT64_MAX;
    expect(xui_feature_set(leading, XUI_F_BUTTON_ICON, &invalid_icon) == XUI_INVALID_ARGUMENT);
    auto retained_icon = value();
    ok(xui_feature_get(leading, XUI_F_BUTTON_ICON, &retained_icon)); expect(retained_icon.first == XUI_BUTTON_ICON_MIXED);
    tab_visuals[0].icon = XUI_BUTTON_ICON_MIXED + 1;
    expect(xui_tab_items_visual(tabs, tab_items, tab_visuals, 2, 71, 1) == XUI_INVALID_ARGUMENT);
    visuals[1].icon = XUI_BUTTON_ICON_MIXED + 1;
    expect(xui_navigation_items_visual(navigation, entries, visuals, 2) == XUI_INVALID_ARGUMENT);
    visuals[1].icon = 15;
    const std::string oversized(32768, 'x');
    visuals[1].image_path = {oversized.data(), static_cast<uint32_t>(oversized.size()), 0};
    expect(xui_navigation_items_visual(navigation, entries, visuals, 2) == XUI_INVALID_ARGUMENT);
    visuals[1].image_path = {"\xc0\xaf", 2, 0};
    expect(xui_navigation_items_visual(navigation, entries, visuals, 2) == XUI_INVALID_ARGUMENT);
    ok(xui_navigation_items(navigation, entries, 2));
    entries[1].parent = 99;
    expect(xui_navigation_items(navigation, entries, 2) == XUI_INVALID_ARGUMENT);
    xui_event selection{}; ok(xui_subscribe(navigation, event, &selection));
    ok(xui_feature_action(navigation, XUI_A_SELECT, 2, 0));
    expect(selection.kind == XUI_SELECTION && selection.value == 2);
    v = value(); ok(xui_feature_set(navigation, XUI_F_EXPANDED, &v));
    ok(xui_feature_get(navigation, XUI_F_EXPANDED, &v)); expect(!v.first);
    ok(xui_feature_child(navigation, 0, &again));
    v = value(); ok(xui_feature_get(again, XUI_F_FOCUSED, &v)); expect(!v.first);
    std::pair<unsigned, unsigned> counts{};
    std::thread worker([&] { ok(xui_window_post(window, posted, &counts)); }); worker.join();
    ok(xui_window_close(window)); expect(counts.first == 0 && counts.second == 1);
    expect(xui_window_post(window, posted, &counts) == XUI_CLOSED);
    ok(xui_window_destroy(window));
    expect(xui_window_visual_style_set(window, XUI_STYLE_WINUI) == XUI_INVALID_HANDLE);
    expect(xui_window_visual_style_get(window, &style) == XUI_INVALID_HANDLE);
    expect(xui_window_post(window, posted, &counts) == XUI_INVALID_HANDLE);
    expect(counts.first == 0 && counts.second == 1);
}
}
void initial_activation_contracts() {
    xui_window_options options{sizeof(options), XUI_ABI_VERSION, text("XUI no-activate contract"), 320, 240};
    xui_handle window{}, root{}, input{};
    ok(xui_window_create(&options, &window));
    ok(xui_stack_create(window, 1, &root));
    ok(xui_create(window, XUI_TEXT_INPUT, text("Editor"), 0, &input));
    ok(xui_stack_add(root, input, 1));
    ok(xui_window_content(window, root));
    ok(xui_window_show_activated(window, 1));
    ok(xui_window_show_activated(window, 0));
    expect(xui_window_show_activated(window, 2) == XUI_INVALID_ARGUMENT);
    expect(xui_window_show_activated(input, 0) == XUI_WRONG_KIND);
    std::thread worker([&] { expect(xui_window_show_activated(window, 0) == XUI_WRONG_THREAD); });
    worker.join();
    ok(xui_window_post(window, [](void* context, uint32_t execute) -> xui_status {
        if (!execute) return XUI_OK;
        const auto window = *static_cast<xui_handle*>(context);
        const auto hwnd = FindWindowW(L"Xui.Window.1", L"XUI no-activate contract");
        expect(hwnd && IsWindowVisible(hwnd));
        expect(GetForegroundWindow() != hwnd);
        expect(!IsChild(hwnd, GetFocus()));
        expect(xui_window_show_activated(window, 1) == XUI_BUSY);
        return xui_window_close(window);
    }, &window));
    ok(xui_window_run(window));
    expect(xui_window_show_activated(window, 1) == XUI_CLOSED);
    ok(xui_window_destroy(window));
    expect(xui_window_show_activated(window, 1) == XUI_INVALID_HANDLE);
}
void split_first_visibility_contracts() {
    xui_window_options options{sizeof(options), XUI_ABI_VERSION, text("Split first visibility"), 400, 300};
    xui_handle window{}, first{}, second{};
    ok(xui_window_create(&options, &window));
    ok(xui_stack_create(window, 1, &first));
    ok(xui_stack_create(window, 1, &second));
    const auto split = create(window, XUI_SPLIT_VIEW, first, second);
    uint32_t axis{};
    float minimum{};
    ok(xui_split_get_layout(split, &axis, &minimum)); expect(axis == 0 && minimum == 300);
    ok(xui_split_set_layout(split, 1, 48));
    ok(xui_split_get_layout(split, &axis, &minimum)); expect(axis == 1 && minimum == 48);
    expect(xui_split_set_layout(split, 2, 48) == XUI_INVALID_ARGUMENT);
    expect(xui_split_set_layout(split, 0, 0) == XUI_INVALID_ARGUMENT);
    expect(xui_split_set_layout(split, 0, std::numeric_limits<float>::quiet_NaN()) == XUI_INVALID_ARGUMENT);
    expect(xui_split_get_layout(split, nullptr, &minimum) == XUI_INVALID_ARGUMENT);
    expect(xui_split_get_layout(split, &axis, nullptr) == XUI_INVALID_ARGUMENT);
    expect(xui_split_set_layout(first, 0, 48) == XUI_WRONG_KIND);
    ok(xui_split_get_layout(split, &axis, &minimum)); expect(axis == 1 && minimum == 48);
    uint32_t visible{};
    ok(xui_split_get_first_visible(split, &visible)); expect(visible == 1);
    auto ratio = value(); ratio.a = .4;
    ok(xui_feature_set(split, XUI_F_SPLIT_RATIO, &ratio));
    ok(xui_split_set_first_visible(split, 0));
    ok(xui_split_get_first_visible(split, &visible)); expect(visible == 0);
    expect(xui_split_set_first_visible(split, 2) == XUI_INVALID_ARGUMENT);
    expect(xui_split_set_first_visible(split, UINT32_MAX) == XUI_INVALID_ARGUMENT);
    ok(xui_split_get_first_visible(split, &visible)); expect(visible == 0);
    expect(xui_split_get_first_visible(split, nullptr) == XUI_INVALID_ARGUMENT);
    expect(xui_split_set_first_visible(first, 0) == XUI_WRONG_KIND);
    expect(xui_split_get_first_visible(first, &visible) == XUI_WRONG_KIND);
    std::thread worker([&] {
        uint32_t result{};
        expect(xui_split_set_first_visible(split, 1) == XUI_WRONG_THREAD);
        expect(xui_split_get_first_visible(split, &result) == XUI_WRONG_THREAD);
        float extent{};
        expect(xui_split_set_layout(split, 0, 48) == XUI_WRONG_THREAD);
        expect(xui_split_get_layout(split, &result, &extent) == XUI_WRONG_THREAD);
    });
    worker.join();
    ok(xui_split_get_first_visible(split, &visible)); expect(visible == 0);
    ok(xui_split_set_first_visible(split, 1));
    ok(xui_split_get_first_visible(split, &visible)); expect(visible == 1);
    ratio = value(); ok(xui_feature_get(split, XUI_F_SPLIT_RATIO, &ratio));
    expect(std::abs(ratio.a - .4) < .0001);
    auto secondary = value(); ok(xui_feature_get(split, XUI_F_SECOND_VISIBLE, &secondary));
    expect(secondary.first == 1);
    ok(xui_window_destroy(window));
    expect(xui_split_get_first_visible(split, &visible) == XUI_INVALID_HANDLE);
    expect(xui_split_set_first_visible(split, 1) == XUI_INVALID_HANDLE);
    expect(xui_split_set_layout(split, 0, 48) == XUI_INVALID_HANDLE);
    expect(xui_split_get_layout(split, &axis, &minimum) == XUI_INVALID_HANDLE);
}
void toggle_control_contracts() {
    static_assert(XUI_RETAINED_ELEMENT == 47 && XUI_TOGGLE_SWITCH == 48 && XUI_TOGGLE_BUTTON == 49 && XUI_PROGRESS_RING == 50);
    static_assert(XUI_F_BUTTON_ICON == 45 && XUI_F_CHECKED == 46 && XUI_F_PROGRESS_CAPACITY == 47);
    xui_window_options options{sizeof(options), XUI_ABI_VERSION, text("Toggle controls"), 400, 300};
    xui_handle window{}; ok(xui_window_create(&options, &window));
    const auto toggle = create(window, XUI_TOGGLE_SWITCH);
    const auto button = create(window, XUI_TOGGLE_BUTTON);
    const auto ring = create(window, XUI_PROGRESS_RING);
    const auto progress = create(window, XUI_PROGRESS);
    std::vector<xui_event> events;
    auto receive = +[](void* context, const xui_event* e) -> xui_status {
        static_cast<std::vector<xui_event>*>(context)->push_back(*e); return XUI_OK;
    };
    for (auto handle : {toggle, button}) {
        const auto property = handle == toggle ? XUI_F_CHECKED : XUI_F_BUTTON_CHECKED;
        expect(read_value(handle, property).first == 0);
        ok(xui_subscribe(handle, receive, &events));
        auto v = value(); v.first = 1; ok(xui_feature_set(handle, property, &v)); expect(events.empty());
        ok(xui_invoke(handle));
        expect(!events.empty() && events.front().kind == XUI_CHANGE && events.front().value == 0 && events.front().source == handle);
        expect(events.size() == 1);
        expect(read_value(handle, property).first == 0);
        events.clear();
        v.first = 2; expect(xui_feature_set(handle, property, &v) == XUI_INVALID_ARGUMENT);
        expect(read_value(handle, property).first == 0);
        ok(xui_subscribe(handle, nullptr, nullptr));
        ok(xui_invoke(handle)); expect(events.empty());
        expect(read_value(handle, property).first == 1);
    }
    expect(read_value(ring, XUI_F_PROGRESS_STATE).first == 1);
    expect(read_value(progress, XUI_F_PROGRESS_STATE).first == 0);
    auto v = value(); v.a = 100; v.b = 200; v.c = 1; v.d = 10;
    ok(xui_feature_set(ring, XUI_F_RANGE, &v));
    v = value(); v.a = 150; ok(xui_feature_set(ring, XUI_F_VALUE, &v));
    expect(read_value(ring, XUI_F_VALUE).a == 150);
    for (uint64_t state = 0; state <= 4; ++state) {
        v = value(); v.first = state; ok(xui_feature_set(ring, XUI_F_PROGRESS_STATE, &v));
        expect(read_value(ring, XUI_F_PROGRESS_STATE).first == state);
    }
    v = value(); v.a = 25; v.b = 80; v.text = text("items");
    ok(xui_feature_set(ring, XUI_F_PROGRESS_CAPACITY, &v));
    expect(read_value(ring, XUI_F_PROGRESS_STATE).first == 0);
    const auto range = read_value(ring, XUI_F_RANGE); expect(range.a == 0 && range.b == 80);
    expect(read_value(ring, XUI_F_VALUE).a == 25);
    v = value(); v.a = 81; v.b = 80; expect(xui_feature_set(ring, XUI_F_PROGRESS_CAPACITY, &v) == XUI_INVALID_ARGUMENT);
    expect(read_value(ring, XUI_F_VALUE).a == 25);
    v = value(); expect(xui_feature_get(ring, XUI_F_CHECKED, &v) == XUI_WRONG_KIND);
    expect(xui_invoke(ring) == XUI_WRONG_KIND);
    for (auto handle : {toggle, button, ring}) {
        xui_style_property property{sizeof(property), XUI_CONTROL_STYLE_VERSION, XUI_STYLE_BACKGROUND,
            XUI_STYLE_COLOR, XUI_STYLE_ROOT, 0, 0, {0x123456, 0x234567}};
        ok(xui_control_set_style_values(handle, XUI_STYLE_ROOT, &property, 1));
    }
    xui_button_style_values legacy{}; legacy.size = sizeof(legacy); legacy.version = XUI_BUTTON_STYLE_VERSION;
    ok(xui_button_set_style_values(button, &legacy));
    ok(xui_button_get_style_values(button, 0, &legacy));
    ok(xui_window_destroy(window));
    expect(xui_invoke(toggle) == XUI_INVALID_HANDLE);
}
void parity_control_contracts() {
    static_assert(XUI_CHECK_BOX == 51 && XUI_HYPERLINK_BUTTON == 52 && XUI_SELECTOR_BAR == 53 && XUI_INFO_BADGE == 54 && XUI_MENU_BAR == 55);
    xui_window_options options{sizeof(options), XUI_ABI_VERSION, text("Parity controls"), 400, 300};
    xui_handle window{}; ok(xui_window_create(&options, &window));
    const auto check = create(window, XUI_CHECK_BOX), link = create(window, XUI_HYPERLINK_BUTTON);
    const auto selector = create(window, XUI_SELECTOR_BAR), badge = create(window, XUI_INFO_BADGE), menu = create(window, XUI_MENU_BAR);
    std::vector<xui_event> events;
    auto receive = +[](void* context, const xui_event* e) -> xui_status {
        static_cast<std::vector<xui_event>*>(context)->push_back(*e); return XUI_OK;
    };
    expect(read_value(check, XUI_F_CHECK_STATE).first == 0 && read_value(check, XUI_F_THREE_STATE).first == 0);
    ok(xui_subscribe(check, receive, &events));
    auto v = value(); v.first = 1; ok(xui_feature_set(check, XUI_F_THREE_STATE, &v));
    v.first = 2; ok(xui_feature_set(check, XUI_F_CHECK_STATE, &v)); expect(events.empty());
    expect(read_value(check, XUI_F_CHECK_STATE).first == 2);
    v.first = 3; expect(xui_feature_set(check, XUI_F_CHECK_STATE, &v) == XUI_INVALID_ARGUMENT);
    expect(read_value(check, XUI_F_CHECK_STATE).first == 2);
    v.first = 0; ok(xui_feature_set(check, XUI_F_CHECK_STATE, &v));
    ok(xui_invoke(check)); expect(events.size() == 1 && events.back().kind == XUI_CHANGE && events.back().value == 1);
    ok(xui_subscribe(check, nullptr, nullptr)); ok(xui_invoke(check)); expect(events.size() == 1);
    events.clear(); ok(xui_subscribe(link, receive, &events));
    ok(xui_invoke(link)); expect(events.size() == 1 && events.back().kind == XUI_CLICK && events.back().source == link);
    v = value(); v.first = 2; ok(xui_feature_set(link, XUI_F_BUTTON_ICON, &v));
    expect(read_value(link, XUI_F_BUTTON_ICON).first == 2);
    expect(read_value(selector, XUI_F_SELECTED).second == 0);
    xui_choice choices[]{{sizeof(xui_choice), 0, 1, 0, text("First")}, {sizeof(xui_choice), 0, 2, 0, text("Second")},
        {sizeof(xui_choice), 1, 3, 0, text("Disabled")}};
    ok(xui_subscribe(selector, receive, &events)); events.clear();
    ok(xui_choices(selector, choices, 3, 0, 0));
    auto selected = read_value(selector, XUI_F_SELECTED); expect(selected.first == 1 && selected.second == 1 && events.empty());
    v = value(); v.first = 2; ok(xui_feature_set(selector, XUI_F_SELECTED, &v));
    expect(read_value(selector, XUI_F_SELECTED).first == 2 && events.empty());
    v.first = 99; expect(xui_feature_set(selector, XUI_F_SELECTED, &v) == XUI_INVALID_ARGUMENT);
    expect(read_value(selector, XUI_F_SELECTED).first == 2);
    expect(xui_choices(selector, choices, 3, 3, 1) == XUI_INVALID_ARGUMENT);
    expect(read_value(selector, XUI_F_SELECTED).first == 2);
    choices[1].id = 1; expect(xui_choices(selector, choices, 3, 0, 0) == XUI_INVALID_ARGUMENT);
    choices[1].id = 0; expect(xui_choices(selector, choices, 3, 0, 0) == XUI_INVALID_ARGUMENT); choices[1].id = 2;
    ok(xui_choices(selector, choices, 3, 0, 0)); expect(read_value(selector, XUI_F_SELECTED).first == 2);
    ok(xui_feature_action(selector, XUI_A_SELECT, 1, 0));
    expect(events.size() == 1 && events.back().kind == XUI_SELECTION && events.back().value == 1);
    v = value(); expect(xui_feature_set(selector, XUI_F_SELECTED, &v) == XUI_INVALID_ARGUMENT);
    ok(xui_choices(selector, nullptr, 0, 0, 0)); expect(read_value(selector, XUI_F_SELECTED).second == 0);
    expect(read_value(badge, XUI_F_BADGE_KIND).first == 0 && read_value(badge, XUI_F_BADGE_COUNT).first == 0 &&
        read_value(badge, XUI_F_BADGE_ICON).first == 0);
    v = value(); v.first = UINT32_MAX; ok(xui_feature_set(badge, XUI_F_BADGE_COUNT, &v));
    expect(read_value(badge, XUI_F_BADGE_KIND).first == 1 && read_value(badge, XUI_F_BADGE_COUNT).first == UINT32_MAX);
    v.first = uint64_t(UINT32_MAX) + 1; expect(xui_feature_set(badge, XUI_F_BADGE_COUNT, &v) == XUI_INVALID_ARGUMENT);
    v.first = 2; ok(xui_feature_set(badge, XUI_F_BADGE_ICON, &v));
    expect(read_value(badge, XUI_F_BADGE_KIND).first == 2 && read_value(badge, XUI_F_BADGE_ICON).first == 2);
    expect(xui_feature_action(badge, XUI_A_SET_DOT, 1, 0) == XUI_INVALID_ARGUMENT);
    ok(xui_feature_action(badge, XUI_A_SET_DOT, 0, 0)); expect(read_value(badge, XUI_F_BADGE_KIND).first == 0);
    expect(xui_invoke(badge) == XUI_WRONG_KIND);
    xui_command_record commands[]{{sizeof(xui_command_record), 1, 1, 0, text("File"), {}, {}, 0, 0},
        {sizeof(xui_command_record), 0, 2, 1, text("Open"), text("Ctrl+O"), text("Pin"), 0, 0}};
    ok(xui_subscribe(menu, receive, &events)); events.clear();
    ok(xui_commands_set(menu, commands, 2)); expect(events.empty());
    ok(xui_command_invoke(menu, 2, 0)); expect(events.size() == 1 && events.back().kind == XUI_CLICK && events.back().value == 2);
    ok(xui_command_invoke(menu, 2, 1)); expect(events.size() == 2 && events.back().kind == XUI_ACTION);
    commands[0].kind = 0; expect(xui_commands_set(menu, commands, 2) == XUI_INVALID_ARGUMENT); commands[0].kind = 1;
    commands[1].parent = 99; expect(xui_commands_set(menu, commands, 2) == XUI_INVALID_ARGUMENT); commands[1].parent = 1;
    std::vector<xui_command_record> too_many_roots(65, commands[0]);
    for (size_t i = 0; i < too_many_roots.size(); ++i) too_many_roots[i].id = i + 1;
    expect(xui_commands_set(menu, too_many_roots.data(), static_cast<uint32_t>(too_many_roots.size())) == XUI_INVALID_ARGUMENT);
    ok(xui_command_invoke(menu, 2, 0)); expect(events.size() == 3);
    ok(xui_command_bind(menu, 2, 'O', 1));
    ok(xui_subscribe(menu, nullptr, nullptr));
    ok(xui_command_invoke(menu, 2, 0)); expect(events.size() == 3);
    ok(xui_subscribe(menu, receive, &events));
    ok(xui_command_invoke(menu, 2, 1)); expect(events.size() == 4 && events.back().kind == XUI_ACTION);
    ok(xui_commands_set(menu, nullptr, 0)); expect(xui_command_invoke(menu, 2, 0) == XUI_INVALID_ARGUMENT);
    for (auto handle : {check, link, selector, badge, menu}) {
        xui_style_property property{sizeof(property), XUI_CONTROL_STYLE_VERSION, XUI_STYLE_BACKGROUND,
            XUI_STYLE_COLOR, XUI_STYLE_ROOT, 0, 0, {0x123456, 0x234567}};
        ok(xui_control_set_style_values(handle, XUI_STYLE_ROOT, &property, 1));
    }
    v = value(); expect(xui_feature_get(link, XUI_F_CHECK_STATE, &v) == XUI_WRONG_KIND);
    ok(xui_window_destroy(window));
}
int main(int argc, char** argv) {
#ifdef _MSC_VER
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (argc == 2 && std::strcmp(argv[1], "--split-first-visible") == 0) {
        split_first_visibility_contracts();
        std::cout << "Split first visibility contracts: " << assertions << " assertions\n";
        return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "--parity-controls") == 0) {
        std::cout << "Parity control ABI contracts built " << __DATE__ << ' ' << __TIME__ << std::endl;
        parity_control_contracts();
        std::cout << "Parity control contracts: " << assertions << " assertions\n";
        return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "--toggle-controls") == 0) {
        std::cout << "Toggle control ABI contracts built " << __DATE__ << ' ' << __TIME__ << std::endl;
        parity_control_contracts();
        toggle_control_contracts();
        std::cout << "Toggle control contracts: " << assertions << " assertions\n";
        return 0;
    }
    if (argc == 2 && std::strcmp(argv[1], "--activation") == 0) {
        initial_activation_contracts();
        std::cout << "Initial activation contracts: " << assertions << " assertions\n";
        return 0;
    }
    toggle_control_contracts();
    retained_navigation_style_bridges();
    split_first_visibility_contracts();
    retained_facade_style_contracts();
    control_style_contracts();
    tooltip_style_contracts();
    miller_contracts();
    collection_navigation_contracts();
    tree_details_contracts();
    explorer_contracts();
    static_assert(sizeof(xui_feature_options)==48);
    static_assert(sizeof(xui_feature_value)==72);
    static_assert(sizeof(xui_source_row)==2096);
    expect(xui_abi_version()==0x10000);expect(xui_feature_version()==0x10001);
    xui_window_options o{sizeof(o),XUI_ABI_VERSION,text("ABI features"),600,600};
    xui_handle w{};ok(xui_window_create(&o,&w));
    xui_handle handles[45]{};
    for(uint32_t kind=XUI_RANGE_INPUT;kind<=XUI_HISTORY_CHART;++kind){
        xui_handle content{},second{};
        if(kind==XUI_EXPANDER || kind==XUI_POPUP || kind==XUI_CONTENT_DIALOG || kind==XUI_ADAPTIVE_LAYOUT || kind==XUI_SPLIT_VIEW)
            ok(xui_stack_create(w,1,&content));
        if(kind==XUI_ADAPTIVE_LAYOUT || kind==XUI_SPLIT_VIEW)ok(xui_stack_create(w,1,&second));
        if(kind==XUI_VIEW_PICKER)content=handles[XUI_ITEMS_VIEW];
        handles[kind]=create(w,kind,content,second);
    }
    const auto measured = handles[XUI_ADAPTIVE_LAYOUT];
    expect(read_value(measured, XUI_F_CONTENT_SIZED).first == 0);
    auto sizing = value(); sizing.first = 1;
    ok(xui_feature_set(measured, XUI_F_CONTENT_SIZED, &sizing));
    expect(read_value(measured, XUI_F_CONTENT_SIZED).first == 1);
    sizing.first = 2;
    expect(xui_feature_set(measured, XUI_F_CONTENT_SIZED, &sizing) == XUI_INVALID_ARGUMENT);
    expect(read_value(measured, XUI_F_CONTENT_SIZED).first == 1);
    sizing.first = 0;
    expect(xui_feature_set(handles[XUI_ITEMS_VIEW], XUI_F_CONTENT_SIZED, &sizing) == XUI_WRONG_KIND);
    ok(xui_feature_set(measured, XUI_F_CONTENT_SIZED, &sizing));
    expect(read_value(measured, XUI_F_CONTENT_SIZED).first == 0);
    auto range=handles[XUI_RANGE_INPUT];auto v=value();v.a=-10;v.b=10;v.c=0.5;v.d=2;
    auto split_value=value();split_value.a=.4;
    ok(xui_feature_set(handles[XUI_SPLIT_VIEW],XUI_F_SPLIT_RATIO,&split_value));
    split_value=value();ok(xui_feature_get(handles[XUI_SPLIT_VIEW],XUI_F_SPLIT_RATIO,&split_value));
    expect(std::abs(split_value.a-.4)<.0001);
    split_value=value();ok(xui_feature_set(handles[XUI_SPLIT_VIEW],XUI_F_SECOND_VISIBLE,&split_value));
    ok(xui_feature_get(handles[XUI_SPLIT_VIEW],XUI_F_SECOND_VISIBLE,&split_value));expect(!split_value.first);
    ok(xui_feature_set(range,XUI_F_RANGE,&v));v=value();v.a=2.5;ok(xui_feature_set(range,XUI_F_VALUE,&v));
    xui::RangeInput reference;reference.set_range({-10,10,.5,2});reference.set_value(2.5);
    v=value();ok(xui_feature_get(range,XUI_F_VALUE,&v));expect(v.a==reference.value());
    v=value();v.a=NAN;expect(xui_feature_set(range,XUI_F_VALUE,&v)==XUI_INVALID_ARGUMENT);
    v=value();v.second=1;expect(xui_feature_set(range,XUI_F_VALUE,&v)==XUI_INVALID_ARGUMENT);
    v=value();v.text=text("\xf0\x28\x8c\x28");expect(xui_feature_set(range,XUI_F_HELP,&v)==XUI_INVALID_ARGUMENT);
    v=value();v.text=text("A\xf0\x9f\x98\x80Z");ok(xui_feature_set(handles[XUI_MULTILINE_TEXT],XUI_F_DOCUMENT_TEXT,&v));
    v=value();v.first=1;v.second=3;ok(xui_feature_set(handles[XUI_MULTILINE_TEXT],XUI_F_TEXT_SELECTION,&v));
    v.second=2;expect(xui_feature_set(handles[XUI_MULTILINE_TEXT],XUI_F_TEXT_SELECTION,&v)==XUI_INVALID_ARGUMENT);
    v=value();v.text=text("safe");ok(xui_feature_set(handles[XUI_PASSWORD_INPUT],XUI_F_PASSWORD,&v));
    bool received{};ok(xui_password_read(handles[XUI_PASSWORD_INPUT],secret,&received));expect(received);
    uint32_t length{};expect(xui_text_copy(handles[XUI_PASSWORD_INPUT],nullptr,0,&length)==XUI_WRONG_KIND);
    Source source;
    source.mutation_target=range;
    xui_source_options source_options{sizeof(source_options),XUI_FEATURE_VERSION,1000000,&source,query,retain,release};
    xui_handle snapshot{};ok(xui_source_create(w,&source_options,&snapshot));expect(source.refs==2);
    ok(xui_source_attach(handles[XUI_ITEMS_VIEW],snapshot));
    v=value();v.first=3;ok(xui_feature_set(handles[XUI_ITEMS_VIEW],XUI_F_PRESENTATION,&v));
    expect(xui_feature_set(handles[XUI_TREE_VIEW],XUI_F_PRESENTATION,&v)==XUI_INVALID_ARGUMENT);
    v.first=4;expect(xui_feature_set(handles[XUI_ITEMS_VIEW],XUI_F_PRESENTATION,&v)==XUI_INVALID_ARGUMENT);
    v.first=0;ok(xui_feature_set(handles[XUI_ITEMS_VIEW],XUI_F_PRESENTATION,&v));
    ok(xui_feature_action(handles[XUI_ITEMS_VIEW], XUI_A_COLLECTION_STEP, 1, 0));
    v=value();ok(xui_feature_get(handles[XUI_ITEMS_VIEW],XUI_F_SELECTION_STATE,&v));expect(v.a && v.first==2);
    ok(xui_feature_action(handles[XUI_ITEMS_VIEW], XUI_A_COLLECTION_STEP, 1, 0));
    v=value();ok(xui_feature_get(handles[XUI_ITEMS_VIEW],XUI_F_SELECTION_STATE,&v));expect(v.first==3);
    ok(xui_feature_action(handles[XUI_ITEMS_VIEW], XUI_A_COLLECTION_STEP, UINT32_MAX, 0));
    v=value();ok(xui_feature_get(handles[XUI_ITEMS_VIEW],XUI_F_SELECTION_STATE,&v));expect(v.first==2);
    v=value();ok(xui_feature_get(handles[XUI_ITEMS_VIEW],XUI_F_OFFSET,&v));expect(v.a>=0);
    const auto grid = handles[XUI_DATA_GRID];
    ok(xui_source_attach(grid, snapshot));
    ok(xui_feature_action(grid, XUI_A_GRID_NAVIGATE, 4, 0));
    v=value();ok(xui_feature_get(grid,XUI_F_SELECTION_STATE,&v));expect(v.first==1);
    ok(xui_feature_action(grid, XUI_A_GRID_NAVIGATE, 1, 0));
    v=value();ok(xui_feature_get(grid,XUI_F_SELECTION_STATE,&v));expect(v.first==2);
    ok(xui_feature_action(grid, XUI_A_GRID_NAVIGATE, 1, 2));
    uint32_t grid_selected{};
    ok(xui_collection_contains(grid,2,7,&grid_selected));expect(grid_selected==1);
    ok(xui_collection_contains(grid,3,7,&grid_selected));expect(grid_selected==1);
    ok(xui_feature_action(grid, XUI_A_GRID_NAVIGATE, 5, 0));
    v=value();ok(xui_feature_get(grid,XUI_F_SELECTION_STATE,&v));expect(v.first==1000000);
    expect(xui_feature_action(grid,XUI_A_GRID_NAVIGATE,6,0)==XUI_INVALID_ARGUMENT);
    expect(xui_feature_action(grid,XUI_A_GRID_NAVIGATE,0,4)==XUI_INVALID_ARGUMENT);
    expect(xui_feature_action(range,XUI_A_GRID_NAVIGATE,0,0)==XUI_WRONG_KIND);
    ok(xui_feature_action(handles[XUI_ITEMS_VIEW],XUI_A_SELECT_ALL,0,0));expect(source.queries<100);
    v=value();ok(xui_feature_get(handles[XUI_ITEMS_VIEW],XUI_F_SELECTION_STATE,&v));expect(v.b==1);
    uint32_t selected{};ok(xui_collection_contains(handles[XUI_ITEMS_VIEW],999999,7,&selected));expect(selected==1);
    ok(xui_source_attach(handles[XUI_TREE_VIEW],snapshot));
    xui_event request{};ok(xui_subscribe(handles[XUI_TREE_VIEW],event,&request));
    ok(xui_tree_expand(handles[XUI_TREE_VIEW],1,7,1));expect(request.kind==XUI_REQUEST && request.value);
    Source empty_source;empty_source.count=0;source_options.context=&empty_source;
    source_options.count=0;xui_handle empty{};ok(xui_source_create(w,&source_options,&empty));
    ok(xui_tree_complete(handles[XUI_TREE_VIEW],request.value,empty,text("")));
    expect(xui_tree_complete(handles[XUI_TREE_VIEW],request.value,snapshot,text(""))==XUI_INVALID_HANDLE);
    Source replace_source;source_options.context=&replace_source;source_options.count=1000000;
    xui_handle replace_snapshot{};ok(xui_source_create(w,&source_options,&replace_snapshot));
    auto replace_view=create(w,XUI_ITEMS_VIEW);ok(xui_source_attach(replace_view,replace_snapshot));
    ok(xui_source_release(replace_snapshot));expect(replace_source.refs==2);
    ok(xui_source_attach(replace_view,empty));expect(replace_source.refs==1);
    {
        Source visual_source;
        auto visual_options = source_options; visual_options.context = &visual_source;
        xui_handle visual_snapshot{}; ok(xui_source_create_visual(w, &visual_options, visual_query, &visual_snapshot));
        expect(visual_source.refs == 2);
        ok(xui_source_attach(replace_view, visual_snapshot)); ok(xui_source_release(visual_snapshot));
        expect(visual_source.refs == 2 && visual_source.visuals == 0);
        ok(xui_source_attach(replace_view, empty)); expect(visual_source.refs == 1);
    }
    xui_event menu_event{};
    ok(xui_context_menu_bind(handles[XUI_DATA_GRID], event, &menu_event));
    ok(xui_context_menu_bind(handles[XUI_ITEMS_VIEW], event, &menu_event));
    ok(xui_context_menu_bind(handles[XUI_TREE_VIEW], event, &menu_event));
    expect(xui_context_menu_items(handles[XUI_TREE_VIEW], nullptr, 0) == XUI_BUSY);
    expect(xui_context_menu_shell_paths(handles[XUI_TREE_VIEW], nullptr, 0) == XUI_BUSY);
    ok(xui_context_menu_bind(handles[XUI_TREE_VIEW], nullptr, nullptr));
    const auto navigation_menu = create(w, XUI_NAVIGATION_VIEW);
    for (uint32_t section = 2; section <= 4; ++section) {
        xui_handle list{}; ok(xui_feature_child(navigation_menu, section, &list));
        ok(xui_context_menu_bind(list, event, &menu_event));
        expect(xui_context_menu_items(list, nullptr, 0) == XUI_BUSY);
        expect(xui_context_menu_shell_paths(list, nullptr, 0) == XUI_BUSY);
        ok(xui_context_menu_bind(list, nullptr, nullptr));
    }
    ok(xui_context_menu_bind(handles[XUI_TAB_STRIP], event, &menu_event));
    expect(xui_context_menu_items(handles[XUI_TAB_STRIP], nullptr, 0) == XUI_BUSY);
    ok(xui_context_menu_bind(handles[XUI_TAB_STRIP], nullptr, nullptr));
    expect(xui_context_menu_bind(handles[XUI_PROGRESS], event, &menu_event) == XUI_WRONG_KIND);
    expect(xui_context_menu_items(handles[XUI_DATA_GRID], nullptr, 0) == XUI_BUSY);
    expect(xui_context_menu_shell_paths(handles[XUI_DATA_GRID], nullptr, 0) == XUI_BUSY);
    expect(xui_context_menu_shell_paths(handles[XUI_ITEMS_VIEW], nullptr, 0) == XUI_BUSY);
    ok(xui_context_menu_bind(handles[XUI_ITEMS_VIEW], nullptr, nullptr));
    ok(xui_context_menu_bind(handles[XUI_DATA_GRID], nullptr, nullptr));
    auto map=handles[XUI_MAP_VIEW];xui_handle token{},newer{};
    ok(xui_map_request(map,&token));ok(xui_map_request(map,&newer));
    expect(xui_map_complete(map,token,nullptr,0)==XUI_CLOSED);ok(xui_map_complete(map,newer,nullptr,0));
    ok(xui_map_request(map,&token));ok(xui_map_request(map,&newer));
    ok(xui_request_cancel(token));ok(xui_map_complete(map,newer,nullptr,0));
    xui_handle othermap=create(w,XUI_MAP_VIEW);ok(xui_map_request(map,&token));
    expect(xui_map_complete(othermap,token,nullptr,0)==XUI_INVALID_ARGUMENT);ok(xui_request_cancel(token));
    xui_command_record commands[]{{sizeof(xui_command_record),0,1,0,text("Action"),text("Ctrl+K"),text("Pin"),0,0}};
    auto bar=handles[XUI_COMMAND_BAR];ok(xui_commands_set(bar,commands,1));xui_event action{};ok(xui_subscribe(bar,event,&action));
    for (uint32_t icon = 14; icon <= XUI_BUTTON_ICON_MIXED; ++icon) {
        commands[0].icon = icon;
        ok(xui_commands_set(bar, commands, 1));
        xui_handle command_button{}; ok(xui_command_bar_button(bar, 1, &command_button));
        auto command_icon = value();
        ok(xui_feature_get(command_button, XUI_F_BUTTON_ICON, &command_icon));
        expect(command_icon.first == icon);
    }
    commands[0].icon = XUI_BUTTON_ICON_MIXED + 1;
    expect(xui_commands_set(bar, commands, 1) == XUI_INVALID_ARGUMENT);
    commands[0].icon = 0;
    ok(xui_commands_set(bar, commands, 1));
    ok(xui_command_invoke(bar,1,0));expect(action.kind==XUI_CLICK && action.value==1);
    ok(xui_command_invoke(bar,1,1));expect(action.kind==XUI_ACTION && action.value==1);
    ok(xui_command_bind(bar,1,'K',1));
    v=value();ok(xui_feature_get(handles[XUI_WEB_CONTENT],XUI_F_HOST_STATE,&v));expect(v.first==0);
    xui_handle script{};ok(xui_web_evaluate(handles[XUI_WEB_CONTENT],text("1+1"),&script));
    uint32_t failed{};expect(xui_web_result(script,nullptr,0,&length,&failed)==XUI_BUFFER_TOO_SMALL);expect(failed==1);
    ok(xui_request_cancel(script));
    std::thread wrong([&]{auto v=value();expect(xui_feature_get(range,XUI_F_VALUE,&v)==XUI_WRONG_THREAD);});wrong.join();
    expect(source.items<100);expect(source.mutation_blocked);
    ok(xui_map_request(map,&token));
    ok(xui_window_destroy(w));expect(source.refs==1);
    expect(xui_map_complete(map,token,nullptr,0)==XUI_INVALID_HANDLE);
    v=value();expect(xui_feature_get(range,XUI_F_VALUE,&v)==XUI_INVALID_HANDLE);
    std::cout<<"Feature ABI assertions: "<<assertions<<"; source queries: "<<source.queries<<"; rows: "<<source.items<<"\n";
}
