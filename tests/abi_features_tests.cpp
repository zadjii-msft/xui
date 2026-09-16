#include "xui/xui.h"
#include "xui/xui_layout.h"
#include "xui/foundation.hpp"
#include <windows.h>
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>
#include <bit>
#include <source_location>
namespace {
unsigned assertions{};
void expect(bool condition, const std::source_location where = std::source_location::current()) {
    if (!condition) { std::cerr << "Feature assertion failed at line " << where.line() << '\n'; std::abort(); }
    ++assertions;
}
void ok(xui_status status) { if (status) { char error[1024]{}; uint32_t size{}; xui_status code{}; xui_error_copy(error,1024,&size,&code); std::cerr.write(error,size); std::cerr << '\n'; } expect(status == 0); }
xui_string text(const char* v) { return {v, static_cast<uint32_t>(std::strlen(v)), 0}; }
xui_feature_value value() { xui_feature_value v{}; v.size=sizeof(v); v.version=XUI_FEATURE_VERSION; return v; }
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
xui_status XUI_CALL posted(void* c, uint32_t execute) {
    auto& counts = *static_cast<std::pair<unsigned, unsigned>*>(c);
    if (execute) ++counts.first; else ++counts.second;
    return XUI_OK;
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
    expect(xui_feature_child(window, 3, &again) == XUI_INVALID_ARGUMENT);
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
    expect(xui_popup_placement(centered_popup, 5) == XUI_INVALID_ARGUMENT);
    expect(xui_popup_placement(first_pane, 4) == XUI_WRONG_KIND);
    ok(xui_popup_window_background(centered_popup, 1));
    ok(xui_popup_window_background(centered_popup, 0));
    expect(xui_popup_window_background(centered_popup, 2) == XUI_INVALID_ARGUMENT);
    expect(xui_popup_window_background(first_pane, 1) == XUI_WRONG_KIND);
    auto shortcut_items = create(window, XUI_ITEMS_VIEW);
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
    xui_item_visual visuals[] {{sizeof(xui_item_visual), 18, text("")}, {sizeof(xui_item_visual), 15, text("folder")}};
    ok(xui_navigation_items_visual(navigation, entries, visuals, 2));
    visuals[1].size = 0;
    expect(xui_navigation_items_visual(navigation, entries, visuals, 2) == XUI_VERSION_MISMATCH);
    visuals[1].size = sizeof(xui_item_visual);
    for (uint32_t icon = 19; icon <= 21; ++icon) {
        visuals[1].icon = icon;
        ok(xui_navigation_items_visual(navigation, entries, visuals, 2));
        auto button_icon = value(); button_icon.first = icon;
        ok(xui_feature_set(leading, XUI_F_BUTTON_ICON, &button_icon));
        button_icon = value();
        ok(xui_feature_get(leading, XUI_F_BUTTON_ICON, &button_icon)); expect(button_icon.first == icon);
    }
    visuals[1].icon = 22;
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
int main() {
    control_style_contracts();
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
    expect(xui_context_menu_bind(handles[XUI_ITEMS_VIEW], event, &menu_event) == XUI_WRONG_KIND);
    expect(xui_context_menu_items(handles[XUI_DATA_GRID], nullptr, 0) == XUI_BUSY);
    expect(xui_context_menu_shell_paths(handles[XUI_DATA_GRID], nullptr, 0) == XUI_BUSY);
    expect(xui_context_menu_shell_paths(handles[XUI_ITEMS_VIEW], nullptr, 0) == XUI_WRONG_KIND);
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
