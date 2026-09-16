#include "xui/navigation.hpp"
#include "xui/titlebar.hpp"
#include "xui/documents.hpp"
#include <array>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>

namespace navigation_allocations {
thread_local bool active{};
thread_local std::size_t calls{}, bytes{};
}
void* operator new(std::size_t size) {
    if (auto* result = std::malloc(size ? size : 1)) {
        if (navigation_allocations::active) { ++navigation_allocations::calls; navigation_allocations::bytes += size; }
        return result;
    }
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* value) noexcept { std::free(value); }
void operator delete[](void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }
void operator delete[](void* value, std::size_t) noexcept { std::free(value); }

namespace {
using namespace xui;
struct AllocationScope {
    AllocationScope() { navigation_allocations::calls = navigation_allocations::bytes = 0; navigation_allocations::active = true; }
    ~AllocationScope() { navigation_allocations::active = false; }
};
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class F> void rejects(F action, const char* message) {
    try { action(); } catch (const std::invalid_argument&) { return; }
    throw std::runtime_error(message);
}
void unexposed_command_allocations() {
#ifdef _MSC_VER
    CommandBar bar;
    unsigned invoked{};
    std::array<std::shared_ptr<const CommandSet>, 3> snapshots;
    const std::array<std::optional<bool>, 3> states{std::nullopt, true, false};
    for (std::size_t i = 0; i < snapshots.size(); ++i)
        snapshots[i] = std::make_shared<CommandSet>(std::vector<CommandRecord>{
            {42, 0, L"Run", [&] { ++invoked; }, true, states[i]}});
    for (const auto& snapshot : snapshots) {
        bar.set_commands(snapshot);
        require(bar.command_button(42)->invoke(), "Warm up the actual unexposed command dispatch");
    }
    const auto button = bar.command_button(42);
    std::size_t storage_calls{}, storage_bytes{};
    {
        AllocationScope scope;
        std::vector<std::shared_ptr<Element>> children;
        std::vector<CommandId> ids;
        children.push_back(button); ids.push_back(42); children.push_back(bar.overflow_button());
        storage_calls = navigation_allocations::calls; storage_bytes = navigation_allocations::bytes;
    }
    for (unsigned round = 0; round < 4; ++round) for (std::size_t i = 0; i < snapshots.size(); ++i) {
        {
            AllocationScope scope;
            bar.set_commands(snapshots[i]);
        }
        require(navigation_allocations::calls == storage_calls && navigation_allocations::bytes == storage_bytes,
            "Unstyled, unexposed command refresh allocates only its child/id vectors, not callbacks");
        const auto before = invoked;
        bool activated = true;
        {
            AllocationScope scope;
            for (unsigned repeat = 0; repeat < 16; ++repeat) activated = button->invoke() && activated;
        }
        require(navigation_allocations::calls == 0 && navigation_allocations::bytes == 0,
            "Unstyled, unexposed momentary and toggle activation copy callbacks without heap allocation");
        require(activated && invoked == before + 16 && button->checked() == states[i].value_or(false) &&
            button == bar.command_button(42) && !button->has_control_styling() && !bar.has_control_styling(),
            "Allocation checks execute the actual retained CommandBar actions and authoritative checked states");
    }
#endif
}
void retired_keyed_buttons() {
    CommandBar bar;
    unsigned actions{}, notifications{};
    bar.set_commands(std::make_shared<CommandSet>(std::vector<CommandRecord>{
        {42, 0, L"Old", [&] { ++actions; }}}));
    bar.set_button_invoked_handler([&](const Button&) { ++notifications; });
    const auto old = bar.command_button(42);
    const auto saved_action = old->click_callback();
    require(old->invoke() && actions == 1 && notifications == 1, "Observe a live keyed command");
    bar.set_commands(std::make_shared<CommandSet>(std::vector<CommandRecord>{}));
    require(!old->enabled() && !old->invoke() && !old->click_callback(),
        "Removed command handles retain no enabled native action");
    saved_action();
    require(actions == 1 && notifications == 1, "Previously copied command callbacks also reject retired Buttons");
    bar.set_commands(std::make_shared<CommandSet>(std::vector<CommandRecord>{
        {42, 0, L"New", [&] { actions += 10; }, false, true}}));
    const auto replacement = bar.command_button(42);
    require(replacement != old && !replacement->invoke() && !old->invoke() && actions == 1 && notifications == 1,
        "Disabled reintroduction cannot reactivate obsolete command handles or notifications");
    bar.set_commands(std::make_shared<CommandSet>(std::vector<CommandRecord>{
        {42, 0, L"New", [&] { actions += 10; }, true, false}}));
    require(replacement->invoke() && !old->invoke() && actions == 11 && notifications == 2,
        "Only the current command identity is re-enabled");
    bar.set_commands(std::make_shared<CommandSet>(std::vector<CommandRecord>{
        {42, 0, L"Replace", [&] {
            ++actions;
            bar.set_commands(std::make_shared<CommandSet>(std::vector<CommandRecord>{}));
            bar.set_commands(std::make_shared<CommandSet>(std::vector<CommandRecord>{{42, 0, L"Replacement"}}));
        }}}));
    require(replacement->invoke() && bar.command_button(42) != replacement && actions == 12 && notifications == 2,
        "Removing and reintroducing an enabled key during its action cannot notify the retired identity");

    Breadcrumb breadcrumb;
    breadcrumb.set_segments({{{20, 9}, L"Old"}});
    const auto segment = breadcrumb.segment_button({20, 9});
    const auto saved_navigation = segment->click_callback();
    unsigned navigations{}, segment_notifications{};
    breadcrumb.on_navigate([&](ItemKey) { ++navigations; });
    breadcrumb.set_button_invoked_handler([&](const Button&) { ++segment_notifications; });
    breadcrumb.set_segments({{{20, 10}, L"New"}});
    require(!segment->enabled() && !segment->invoke() && !segment->click_callback() &&
        navigations == 0 && segment_notifications == 0, "A replaced Breadcrumb version retires its old handle");
    saved_navigation();
    require(navigations == 0 && segment_notifications == 0, "Previously copied navigation callbacks reject retired versions");
    require(breadcrumb.segment_button({20, 10})->invoke() && navigations == 1 && segment_notifications == 1,
        "The new Breadcrumb version remains actionable");
    const auto current = breadcrumb.segment_button({20, 10});
    breadcrumb.on_navigate([&](ItemKey) { ++navigations; breadcrumb.set_segments({}); });
    require(current->invoke() && !current->enabled() && navigations == 2 && segment_notifications == 1,
        "Retiring a segment during navigation suppresses its subsequent foreign notification");
}
void disabling_during_invocation() {
    for (const auto checked : {std::optional<bool>{}, std::optional<bool>{false}, std::optional<bool>{true}}) {
        CommandBar bar;
        unsigned actions{}, notifications{};
        bar.set_commands(std::make_shared<CommandSet>(std::vector<CommandRecord>{
            {42, 0, L"Run", [&] {
                ++actions;
                bar.set_commands(std::make_shared<CommandSet>(std::vector<CommandRecord>{
                    {42, 0, L"Busy", {}, false, checked}}));
            }, true, checked}}));
        const auto button = bar.command_button(42);
        bar.set_button_invoked_handler([&](const Button& invoked) {
            require(&invoked == button.get(), "A completed invocation notifies the same retained child");
            ++notifications;
        });
        require(button->invoke() && !button->enabled() && bar.command_button(42) == button &&
            actions == 1 && notifications == 1 && !button->invoke(),
            "Disabling a current command during its action preserves exactly one notification");
    }
    Breadcrumb breadcrumb;
    breadcrumb.set_segments({{{20, 9}, L"Current"}});
    const auto segment = breadcrumb.segment_button({20, 9});
    unsigned navigations{}, notifications{};
    breadcrumb.on_navigate([&](ItemKey) {
        ++navigations;
        breadcrumb.set_segments({{{20, 9}, L"Busy"}});
        segment->set_enabled(false);
    });
    breadcrumb.set_button_invoked_handler([&](const Button&) { ++notifications; });
    require(segment->invoke() && breadcrumb.segment_button({20, 9}) == segment &&
        !segment->enabled() && navigations == 1 && notifications == 1 && !segment->invoke(),
        "Disabling a current segment during navigation preserves exactly one notification");

    auto dying_bar = std::make_shared<CommandBar>();
    dying_bar->set_commands(std::make_shared<CommandSet>(std::vector<CommandRecord>{
        {42, 0, L"Exit", [&] { dying_bar.reset(); }}}));
    dying_bar->set_button_invoked_handler([&](const Button&) { ++notifications; });
    const auto retained = dying_bar->command_button(42);
    require(retained->invoke() && !dying_bar && notifications == 1,
        "Destruction during a command invalidates observer ownership before notification");
}
void stable_geometry_rules() {
    PartStyleValues padding, width, size;
    padding.padding = Insets{10, 10, 10, 10};
    width.width = 80;
    size.size = 20;
    for (const auto target : {StyleTarget::breadcrumb, StyleTarget::command_bar})
        rejects([&] { ControlStyle::create(target, {}, {{StylePart::root, style_states::overflowed, padding}}); },
            "Overflow cannot change the geometry that determines overflow");
    rejects([&] { ControlStyle::create(StyleTarget::tab_strip, {}, {{StylePart::tab, style_states::selected, width}}); },
        "Per-tab transient styles cannot change stable tab widths");
    rejects([&] { ControlStyle::create(StyleTarget::tab_strip, {}, {{StylePart::close_action, style_states::hovered, size}}); },
        "Close-hover styles cannot change their own hit target");
    PartStyleValues separator;
    separator.thickness = 20;
    rejects([&] { ControlStyle::create(StyleTarget::command_bar, {}, {{StylePart::separator, style_states::disabled, separator}}); },
        "Command separator state rules cannot change overflow geometry");
    for (const auto target : {StyleTarget::tab_strip, StyleTarget::tooltip}) {
        const auto part = target == StyleTarget::tab_strip ? StylePart::label : StylePart::text;
        PartStyleValues alignment;
        alignment.vertical_alignment = StyleAlignment::stretch;
        rejects([&] { ControlStyle::create(target, {{part, alignment}}, {}); },
            "Navigation text parts reject unsupported vertical stretching before rendering");
        alignment.vertical_alignment = StyleAlignment::center;
        alignment.horizontal_alignment = StyleAlignment::stretch;
        require(bool(ControlStyle::create(target, {{part, alignment}}, {})),
            "Navigation text parts retain paragraph centering and horizontal justification");
    }
}
std::shared_ptr<const ControlStyle> surface(StyleTarget target) {
    PartStyleValues values;
    values.background = ThemeColor{0x123456, 0x654321};
    values.border_brush = ThemeColor{0xabcdef};
    values.border_thickness = Insets{1, 2, 3, 4};
    values.padding = Insets{5, 6, 7, 8};
    values.corner_radius = 9;
    return ControlStyle::create(target, {{StylePart::root, values}}, {});
}
void navigation() {
    NavigationView view;
    view.set_items({{{1, 1}, {}, L"Home"}});
    const auto search = view.search();
    const auto items = view.items();
    view.select({1, 1});
    view.arrange({0, 0, 280, 480});
    const auto before = search->bounds();
    view.set_control_style(surface(StyleTarget::navigation_view));
    view.arrange({0, 0, 280, 480});
    require(search->bounds().x > before.x && search->bounds().width < before.width, "Navigation padding changes child geometry");
    PartStyleValues local;
    local.foreground = ThemeColor{0x112233};
    view.title()->set_control_style_values(StylePart::root, local);
    view.set_visual_style(VisualStyle::winui);
    view.set_expanded(false);
    view.arrange({0, 0, 64, 480});
    view.set_expanded(true);
    view.set_control_style(nullptr);
    require(view.search() == search && view.items() == items && view.selected() == ItemKey{1, 1},
        "Style, theme and compact changes retain navigation children and selected key");
    require(view.title()->control_style_values(StylePart::root).foreground == local.foreground,
        "Parent styles preserve authored title-local values");
    PartStyleValues compact;
    compact.background = ThemeColor{0x778899};
    view.set_control_style(ControlStyle::create(StyleTarget::navigation_view, {},
        {{StylePart::root, style_states::compact, compact}}));
    view.set_expanded(false);
    require(view.effective_control_style_values(StylePart::root)->background == compact.background, "Compact state comes from navigation model");

    NavigationPane pane;
    const auto rows = pane.items();
    pane.on_query([](NavigationQuery) {});
    const auto query = pane.request(L"home");
    pane.set_control_style(surface(StyleTarget::navigation_pane));
    pane.arrange({0, 0, 320, 320});
    pane.set_visual_style(VisualStyle::winui);
    require(!query.cancellation.stop_requested() && pane.items() == rows, "Navigation styling neither cancels queries nor replaces rows");
    require(pane.complete(query, {}, L"Unavailable"), "Styled navigation still accepts current query errors");
    require(pane.status()->text() == L"Unavailable", "Query error remains explicit");
    NavigationView sections;
    NavigationItem header{{1, 1}, {}, L"Header"};
    header.section = NavigationSection::header;
    NavigationItem footer{{2, 1}, {}, L"Footer"};
    footer.section = NavigationSection::footer;
    sections.set_items({header, footer});
    PartStyleValues section;
    section.row_height = 56;
    section.padding = Insets{0, 3, 0, 5};
    section.border_thickness = Insets{0, 1, 0, 2};
    sections.header_items()->set_control_style_values(StylePart::root, section);
    sections.footer_items()->set_control_style_values(StylePart::root, section);
    sections.arrange({0, 0, 280, 800});
    require(sections.header_items()->bounds().height == 67 && sections.footer_items()->bounds().height == 67 &&
        sections.header_items()->content_viewport().height == 56,
        "Navigation section heights include styled row metrics, padding, and borders");
    sections.set_items({});
    sections.arrange({0, 0, 280, 800});
    require(!sections.header_items()->visible() && !sections.footer_items()->visible(),
        "Styled padding does not reveal empty navigation sections");
}
void breadcrumb_and_commands() {
    Breadcrumb breadcrumb;
    breadcrumb.set_segments({{{1, 1}, L"Root"}, {{2, 1}, L"Folder"}, {{3, 1}, L"Current"}});
    const auto button = breadcrumb.segment_button({3, 1});
    const auto overflow = breadcrumb.overflow_button();
    int navigated{};
    breadcrumb.on_navigate([&](ItemKey key) { require(key == ItemKey{3, 1}, "Exact segment key"); ++navigated; });
    ButtonStyleValues local;
    local.foreground = ThemeColor{0x00ff00};
    button->set_style_values(local);
    breadcrumb.set_control_style(surface(StyleTarget::breadcrumb));
    breadcrumb.arrange({0, 0, 180, 40});
    require(breadcrumb.overflowed() && breadcrumb.current() == ItemKey{3, 1}, "Styled breadcrumb keeps overflow and current identities");
    require(breadcrumb.segment_button({3, 1}) == button && breadcrumb.overflow_button() == overflow,
        "Breadcrumb style application retains segment and overflow Buttons");
    require(button->style_values().foreground == local.foreground && button->invoke() && navigated == 1,
        "Segment-local style and action remain independent of parent");
    breadcrumb.arrange({0, 0, 800, 40});
    require(!breadcrumb.overflowed() && breadcrumb.adjacent(*button, -1) == breadcrumb.segment_button({2, 1}).get(),
        "Resizing preserves keyboard segment order");
    int invoked{};
    auto commands = std::make_shared<CommandSet>(std::vector<CommandRecord>{
        {1, 0, L"One", [&] { ++invoked; }}, {2, 0, L"Two"}, {3, 0, L"Three"}});
    CommandBar bar;
    bar.set_commands(commands);
    const auto action = bar.command_button(1);
    bar.set_control_style(surface(StyleTarget::command_bar));
    bar.arrange({0, 0, 240, 40});
    require(bar.overflowed() && bar.overflow_commands()->find(3), "Command overflow retains original IDs");
    require(bar.command_button(1) == action && action->invoke() && invoked == 1, "Styled command retains its Button and callback");
    bar.set_visual_style(VisualStyle::winui);
    bar.arrange({0, 0, 700, 40});
    require(!bar.overflowed() && bar.command_button(1) == action, "Resize and theme retain command Buttons");
    action->set_style_values(local);
    bar.set_commands(std::make_shared<CommandSet>(std::vector<CommandRecord>{
        {1, 0, L"Updated", [&] { invoked += 10; }, true, true}, {2, 0, L"Two"}}));
    require(bar.command_button(1) == action, "Command snapshot refresh retains the Button for its stable ID");
    require(action->style_values().foreground == local.foreground, "Command snapshot refresh preserves Button-local styling");
    require(action->behavior() == ButtonBehavior::toggle && action->checked(),
        "Checked command snapshots update the retained Button behavior and authoritative state");
    require(action->invoke(), "The retained checked command remains invokable");
    require(invoked == 11, "The retained checked command dispatches its current action exactly once");
    require(action->checked(), "Command dispatch preserves the snapshot's authoritative checked state");
    bar.set_commands(std::make_shared<CommandSet>(std::vector<CommandRecord>{
        {1, 0, L"Momentary", [&] { invoked += 100; }}, {2, 0, L"Two"}}));
    require(bar.command_button(1) == action && action->invoke() && invoked == 111 && !action->checked(),
        "A retained command switches back to its current momentary callback");
    bar.set_commands(std::make_shared<CommandSet>(std::vector<CommandRecord>{
        {1, 0, L"Unchecked", [&] { invoked += 1000; }, true, false}, {2, 0, L"Two"}}));
    require(bar.command_button(1) == action && action->invoke() && invoked == 1111 && !action->checked() &&
        action->style_values().foreground == local.foreground,
        "Unchecked command snapshots dispatch once and preserve authoritative state and local styles");
    CommandRecord separator;
    separator.id = 99;
    separator.kind = CommandKind::separator;
    bar.set_commands(std::make_shared<CommandSet>(std::vector<CommandRecord>{
        {1, 0, L"One"}, separator, {2, 0, L"Two"}, {3, 0, L"Three"}}));
    bar.set_control_style(nullptr);
    bar.arrange({0, 0, 340, 40});
    require(!bar.overflowed() && bar.command_button(2)->bounds().x == 112 && bar.separator_bounds(1).width == 0,
        "Unstyled command separators preserve existing command pitch and presentation");
    PartStyleValues separator_values;
    separator_values.background = ThemeColor{0x123456};
    separator_values.thickness = 20;
    bar.set_control_style_values(StylePart::separator, separator_values);
    bar.arrange({0, 0, 400, 40});
    require(!bar.overflowed() && bar.separator_bounds(1).width == 20 && bar.command_button(2)->bounds().x == 128 &&
        bar.separator_bounds(2).width == 0, "Styled command separators reserve only their real model gaps");
    bar.arrange({0, 0, 340, 40});
    require(bar.overflowed() && bar.overflow_commands()->find(2) && bar.separator_bounds(1).width == 0 &&
        bar.command_button(1) == action && action->style_values().foreground == local.foreground,
        "Separator metrics and overflow share geometry without replacing authored command Buttons");
    bar.set_control_style_values(StylePart::separator, {});
    bar.arrange({0, 0, 340, 40});
    require(!bar.overflowed() && bar.command_button(2)->bounds().x == 112,
        "Clearing separator styling restores default command geometry");
    auto dying_breadcrumb = std::make_shared<Breadcrumb>();
    dying_breadcrumb->set_segments({{{8, 1}, L"Exit"}});
    const auto retained_segment = dying_breadcrumb->segment_button({8, 1});
    unsigned notifications_after_destroy{};
    dying_breadcrumb->set_button_invoked_handler([&](const Button&) { ++notifications_after_destroy; });
    dying_breadcrumb->on_navigate([&](ItemKey) { dying_breadcrumb.reset(); });
    require(retained_segment->invoke() && !dying_breadcrumb && notifications_after_destroy == 0,
        "Destroying a Breadcrumb in its native action revokes retained child notifications");
}
void titlebar() {
    TitleBar bar(L"Title");
    const auto minimize = bar.minimize();
    const auto maximize = bar.maximize();
    unsigned caption_actions{}, caption_notifications{};
    bar.set_button_invoked_handler([&](const Button& button) {
        require(&button == minimize.get(), "Caption notifications retain actual Button identity");
        ++caption_notifications;
    });
    bar.on_caption([&](CaptionAction action) {
        require(action == CaptionAction::minimize, "Caption handler keeps native action identity");
        ++caption_actions;
    });
    require(minimize->invoke() && caption_actions == 1 && caption_notifications == 1,
        "Caption subscriptions survive delayed native handler installation");
    bar.on_caption([&](CaptionAction) { caption_actions += 10; });
    require(minimize->invoke() && caption_actions == 11 && caption_notifications == 2,
        "Replacing a native caption handler preserves subscriber delivery");
    bar.set_button_invoked_handler({});
    require(minimize->invoke() && caption_actions == 21 && caption_notifications == 2,
        "Clearing caption notifications preserves native actions");
    bar.set_control_style(surface(StyleTarget::title_bar));
    bar.arrange({0, 0, 800, 44});
    const auto center = [](Rect b) { return Point{b.x + b.width / 2, b.y + b.height / 2}; };
    require(bar.hit_test(center(minimize->bounds())) == CaptionHit::minimize, "Styled titlebar minimize hit matches arranged Button");
    require(bar.hit_test(center(maximize->bounds())) == CaptionHit::maximize, "Styled titlebar keeps native maximize hit region");
    PartStyleValues inactive;
    inactive.background = ThemeColor{0x111111};
    bar.set_control_style(ControlStyle::create(StyleTarget::title_bar, {},
        {{StylePart::root, style_states::inactive, inactive}}));
    bar.set_active(false);
    require(bar.effective_control_style_values(StylePart::root)->background == inactive.background, "Inactive state follows native activation");
    bar.set_maximized(true);
    require(bar.maximized() && bar.maximize() == maximize && maximize->icon() == ButtonIcon::restore,
        "Maximized presentation retains system Button identity");
    PartStyleValues caption, close;
    caption.background = ThemeColor{0x334455};
    close.background = ThemeColor{0xaa1122};
    bar.set_control_style(ControlStyle::create(StyleTarget::title_bar, {{StylePart::caption_button, caption}},
        {{StylePart::caption_close, style_states::hovered, close}}));
    require(bar.resolve_control_style_part(StylePart::caption_button, style_states::inactive).background == caption.background &&
        bar.resolve_control_style_part(StylePart::caption_close, style_states::hovered).background == close.background,
        "Caption Button and close-hover use their dedicated public parts");
}
void tabs() {
    TabStrip tabs;
    tabs.set_tabs({{1, L"First"}, {2, L"Second"}, {3, L"Third"}, {4, L"Last"}}, 1);
    uint64_t closed{};
    tabs.on_close([&](uint64_t id) { closed = id; });
    PartStyleValues root, tab, close;
    root.padding = Insets{7, 5, 9, 6};
    tab.width = 90;
    close.size = 18;
    tabs.set_control_style(ControlStyle::create(StyleTarget::tab_strip,
        {{StylePart::root, root}, {StylePart::tab, tab}, {StylePart::close_action, close}}, {}));
    tabs.arrange({0, 0, 300, 44});
    const auto first = tabs.tab_bounds(0);
    require(first.x == 7 && first.y == 5 && first.width == 90 && first.height == 33,
        "Tab padding and width use shared layout geometry");
    require(!tabs.hit_test(Point{first.x + 1, 1}) && tabs.hit_test(Point{first.x + 1, first.y + 1}) == 0,
        "Tab hit testing excludes authored padding");
    require(tabs.close_bounds(0).width == 18, "Close action size changes its hit rectangle");
    tabs.step(-1);
    require(tabs.selected() == 4 && tabs.tab_bounds(3).width > 0, "Keyboard selection reveals styled tabs");
    tabs.request_close(4);
    require(closed == 4, "Styled close action retains tab identity");
    const auto id = tabs.id();
    tabs.set_visual_style(VisualStyle::winui);
    tabs.set_control_style(nullptr);
    tabs.arrange({0, 0, 500, 44});
    require(tabs.id() == id && tabs.selected() == 4 && tabs.tabs()[3].id == 4,
        "Tab theme, resize, and style removal retain model identities");
}
void facades() {
    CommandSurface commands;
    const auto menu = commands.menu();
    const auto editor = commands.editor();
    commands.on_query([](CommandQuery) {});
    const auto request = commands.request(L"test");
    commands.popup()->set_control_style(surface(StyleTarget::popup));
    PartStyleValues local;
    local.foreground = ThemeColor{0x123456};
    commands.title()->set_control_style_values(StylePart::root, local);
    commands.status()->set_control_style_values(StylePart::root, local);
    require(commands.menu() == menu && commands.editor() == editor && !request.cancellation.stop_requested(),
        "Command facade child styling preserves editor, menu, and query");
    PartStyleValues content_style;
    content_style.padding = Insets{2, 3, 4, 5};
    content_style.spacing = 7;
    const auto content_definition = ControlStyle::create(StyleTarget::stack, {{StylePart::root, content_style}}, {});
    commands.content()->set_control_style(content_definition);
    require(commands.content()->effective_layout_insets().top == 3 && commands.content()->effective_spacing() == 7,
        "Styles override command composition constructor defaults");
    commands.content()->set_control_style(nullptr);
    require(commands.content()->effective_layout_insets().top == 16 && commands.content()->effective_spacing() == 12,
        "Removing styles restores command composition defaults");
    commands.content()->set_padding({9, 9, 9, 9});
    commands.content()->set_spacing(4);
    commands.content()->set_control_style(content_definition);
    require(commands.content()->effective_layout_insets().top == 9 && commands.content()->effective_spacing() == 4 &&
        !request.cancellation.stop_requested(), "Explicit child-local metrics override styles without canceling queries");
    rejects([&] { ControlStyle::create(StyleTarget::command_surface, {}, {}); },
        "Unsupported combined facade targets fail instead of accepting unused styles");
    LocationPicker location;
    const auto pane = location.navigation();
    location.popup()->set_control_style(surface(StyleTarget::popup));
    location.footer()->set_control_style_values(StylePart::root, local);
    require(location.navigation() == pane && location.content(), "Location style accessors expose retained components");
    auto items = std::make_shared<ItemsView>();
    ViewPicker view(items);
    const auto choices = view.choices();
    const auto size = view.size();
    view.popup()->set_control_style(surface(StyleTarget::popup));
    view.popup()->arrange({0, 0, 280, 210});
    require(view.choices() == choices && view.size() == size && view.content(), "View picker retains choices and size editor");
    SplitButton split(L"Run", L"More");
    const auto primary = split.primary();
    ButtonStyleValues button_values;
    button_values.background = ThemeColor{0x123456};
    primary->set_style_values(button_values);
    split.set_control_style(surface(StyleTarget::split_button));
    split.set_spacing(8);
    split.arrange({0, 0, 300, 40});
    require(split.primary() == primary && primary->style_values().background == button_values.background,
        "SplitButton uses existing Button styles without replacing actions");
    require(split.secondary()->bounds().x >= primary->bounds().x + primary->bounds().width + 8,
        "SplitButton spacing reserves a real separator gap");
    auto input = std::make_shared<TextInput>(L"Value");
    ContentDialog dialog(L"Confirm", input);
    const auto primary_action = dialog.primary();
    const auto validation = dialog.validation();
    dialog.popup()->set_control_style(surface(StyleTarget::popup));
    dialog.title()->set_control_style_values(StylePart::root, local);
    dialog.footer()->set_control_style(surface(StyleTarget::stack));
    PartStyleValues body;
    body.padding = Insets{30, 20, 30, 20};
    body.spacing = 16;
    dialog.body()->set_control_style_values(StylePart::root, body);
    int validations{};
    dialog.on_validate([&] { ++validations; return L"Required"; });
    dialog.set_visual_style(VisualStyle::winui);
    dialog.popup()->arrange({0, 0, 460, 360});
    dialog.accept();
    require(validations == 0 && dialog.primary() == primary_action && dialog.validation() == validation,
        "Styling a closed dialog cannot invoke validation or replace actions");
    require(!dialog.popup()->is_open() && !validation->visible(), "Styles cannot open a dialog or expose hidden validation");
    require(input->bounds().x >= 30, "Dialog body metrics reserve space around the original native editor");
    dialog.body()->set_padding({9, 9, 9, 9});
    dialog.body()->set_spacing(7);
    dialog.set_visual_style(VisualStyle::classic);
    dialog.set_visual_style(VisualStyle::winui);
    require(dialog.body()->effective_layout_insets().top == 9 && dialog.body()->effective_spacing() == 7,
        "Dialog presentation defaults do not overwrite explicit body-local metrics");
}
}
int main() {
    try {
        unexposed_command_allocations(); retired_keyed_buttons(); disabling_during_invocation();
        stable_geometry_rules(); navigation(); breadcrumb_and_commands(); titlebar(); tabs(); facades();
        std::cout << "Navigation styling tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
