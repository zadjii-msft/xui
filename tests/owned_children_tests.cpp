#include "xui/menu_bar.hpp"
#include "xui/miller_columns.hpp"
#include "xui/titlebar.hpp"
#include "xui/map_view.hpp"
#include "xui/reveal.hpp"
#include "xui/adaptive_layout.hpp"
#include "xui/documents.hpp"
#include "xui/runtime_hosts.hpp"
#include "xui/navigation.hpp"
#include "owned_children_workload.hpp"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <new>
#include <ranges>

namespace {
std::atomic<std::size_t> allocations{};
}
void* operator new(std::size_t size) {
    allocations.fetch_add(1, std::memory_order_relaxed);
    if (auto* memory = std::malloc(size ? size : 1)) return memory;
    throw std::bad_alloc{};
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

namespace {
using namespace xui;
static_assert(std::ranges::forward_range<OwnedChildren>);
static_assert(std::is_same_v<std::ranges::range_reference_t<OwnedChildren>, const std::shared_ptr<Element>&>);
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F action) {
    try { action(); } catch (const std::invalid_argument&) { return; }
    throw std::runtime_error("Invalid ownership was accepted");
}
void expect(const Element& element, std::span<const std::shared_ptr<Element>> expected) {
    const auto view = element.owned_children();
    require(view.size() == expected.size() && view.empty() == (expected.size() == 0), "Direct child count");
    auto next = expected.begin();
    std::size_t index{};
    for (const auto& child : view) {
        require(child && child == *next++ && &child == &view.at(index), "Borrowed identity and slot order");
        require(&child == &element.owned_children()[index++], "Repeated views borrow the same shared_ptr");
        const auto owners = child.use_count();
        const auto borrowed = element.owned_children();
        require(borrowed.size() == view.size() && child.use_count() == owners, "Views do not acquire ownership");
    }
    if (!view.empty()) require(&view.front() == &view[0] && &view.back() == &view[view.size() - 1], "Endpoints");
    bool bounds_checked{};
    try { view.at(view.size()); } catch (const std::out_of_range&) { bounds_checked = true; }
    require(bounds_checked, "Checked access rejects empty and past-the-end indices");
    const auto before = allocations.load();
    for (int repetition = 0; repetition < 1000; ++repetition) {
        std::size_t visited{};
        for (const auto& child : element.owned_children())
            require(&child == &view[visited++], "Stable borrowed storage");
        require(visited == expected.size(), "Repeated exact count");
    }
    require(allocations.load() == before, "Repeated direct enumeration allocates nothing");
}
void expect(const Element& element, std::initializer_list<std::shared_ptr<Element>> expected) {
    expect(element, std::span{expected.begin(), expected.size()});
}
class RecordOwner final : public Element {
public:
    explicit RecordOwner(std::shared_ptr<Element> child) : records_{{std::move(child), 3}} { adopt(records_[0].element); }
    OwnedChildren owned_children() const override {
        return OwnedChildren::members<&Record::element>(std::span<const Record>{records_});
    }
private:
    struct Record { std::shared_ptr<Element> element; int metadata; };
    std::array<Record, 1> records_;
};
void layouts() {
    Element leaf;
    Label label(L"Leaf"); Button button(L"Leaf"); TextInput input(L"Leaf"); RadioGroup choices;
    Progress progress; ItemsView items; CommandMenu menu;
    const Element* leaves[]{&leaf, &label, &button, &input, &choices, &progress, &items, &menu};
    for (const auto* value : leaves) expect(*value, {});
    auto custom_child = std::make_shared<Element>();
    RecordOwner custom(custom_child);
    expect(custom, {custom_child});
    Stack stack(Axis::vertical);
    auto first = std::make_shared<Label>(L"First"), second = std::make_shared<Label>(L"Second");
    stack.add(first, 2); stack.add(second);
    expect(stack, {first, second});
    require(&stack.owned_children()[0] == &stack.child_at(0), "Stack projects Child records without copying");
    Grid grid;
    auto a = std::make_shared<Button>(L"A"), b = std::make_shared<Button>(L"B");
    grid.add(a, 0, 0); grid.add(b, 0, 0);
    expect(grid, {a, b});
    Wrap wrap;
    auto wrapped = std::make_shared<Label>(L"Wrapped");
    wrap.add(wrapped); expect(wrap, {wrapped});
    auto navigation = std::make_shared<Label>(L"Navigation"), content = std::make_shared<Label>(L"Content");
    AdaptiveLayout adaptive(navigation, content);
    adaptive.set_navigation_open(false);
    expect(adaptive, {content, navigation});
    ContentHost empty;
    expect(empty, {});
    auto hosted = std::make_shared<Label>(L"Hosted");
    ContentHost host(hosted); expect(host, {hosted});
    PageView pages;
    auto active = std::make_shared<Label>(L"Active"), inactive = std::make_shared<Label>(L"Inactive");
    pages.add(active); pages.add(inactive);
    expect(pages, {active, inactive});
    pages.select(1); pages.arrange({0, 0, 800, 600});
    expect(pages, {active, inactive});
    auto scrolled = std::make_shared<Label>(L"Scroll");
    ScrollView scroll(scrolled); expect(scroll, {scrolled});
    require(&scroll.owned_children().front() == &scroll.content(), "Scroll borrows its direct slot");
    auto clipped = std::make_shared<Label>(L"Clip");
    ContentView view(clipped); expect(view, {clipped});
    require(&view.owned_children().front() == &view.content(), "ContentView borrows its direct slot");
    auto left = std::make_shared<Label>(L"Left"), right = std::make_shared<Label>(L"Right");
    SplitView split(left, right);
    expect(split, {split.first(), split.second()});
    expect(*split.first(), {left}); expect(*split.second(), {right});
    split.set_secondary_visible(false); split.arrange({0, 0, 100, 100});
    expect(split, {split.first(), split.second()});
    auto deferred = std::make_shared<Label>(L"Deferred");
    Reveal reveal(deferred); expect(reveal, {deferred});
    require(!reveal.open(), "Enumeration does not open Reveal");
    auto body = std::make_shared<Label>(L"Body");
    Expander expander(L"Expander", body); expander.set_expanded(false);
    expect(expander, {body});
    require(!expander.expanded(), "Enumeration does not expand content");
    rejects([&] { stack.add({}); });
    rejects([&] { stack.add(first); });
    auto parent = std::make_shared<Stack>(Axis::vertical), child = std::make_shared<Stack>(Axis::vertical);
    parent->add(child);
    rejects([&] { child->add(parent); });
}
void composites() {
    auto popup_body = std::make_shared<Label>(L"Popup");
    Popup popup(popup_body); expect(popup, {popup_body});
    ComboBox combo;
    expect(combo, {});
    ComboBox editable(L"Editable", true);
    expect(editable, {editable.editor()});
    expect(*editable.popup(), {editable.choices()});
    require(!editable.popup()->is_open(), "Combo popup remains a separate unopened root");
    NumericInput number;
    expect(number, {number.editor(), number.decrease_button(), number.increase_button()});
    SplitButton split_button(L"Split", L"More");
    expect(split_button, {split_button.primary(), split_button.secondary()});
    TabStrip tabs;
    expect(tabs, {tabs.new_tab_button()});
    TitleBar title(L"Title");
    expect(title, {title.title(), title.leading(), title.tabs(), title.secondary_tabs(),
        title.minimize(), title.maximize(), title.close()});
    NavigationView navigation;
    expect(navigation, {navigation.toggle_button(), navigation.title(), navigation.search(), navigation.header_items(),
        navigation.items(), navigation.footer_items(), navigation.empty_message()});
    NavigationPane pane;
    expect(pane, {pane.content()});
    expect(*pane.content(), {pane.group(), pane.progress(), pane.status()});
    Breadcrumb breadcrumb;
    expect(breadcrumb, {breadcrumb.overflow_button()});
    breadcrumb.set_segments({{{1, 1}, L"One"}, {{2, 1}, L"Two"}});
    expect(breadcrumb, {breadcrumb.overflow_button(), breadcrumb.segment_button({1, 1}), breadcrumb.segment_button({2, 1})});
    CommandBar bar;
    auto commands = std::make_shared<CommandSet>(std::vector<CommandRecord>{{1, 0, L"One"}, {2, 0, L"Two"}});
    bar.set_commands(commands);
    expect(bar, {bar.command_button(1), bar.command_button(2), bar.overflow_button()});
    MenuBar menu;
    expect(menu, {});
    CommandRecord file{1, 0, L"File"}; file.kind = CommandKind::submenu;
    CommandRecord edit{2, 0, L"Edit"}; edit.kind = CommandKind::submenu;
    menu.set_commands(std::make_shared<CommandSet>(std::vector<CommandRecord>{file, edit}));
    expect(menu, {menu.heading(1), menu.heading(2)});
    MillerColumns columns;
    std::vector<std::shared_ptr<Element>> column_children;
    require(columns.owned_children().size() == 2 * MillerColumns::maximum_columns, "Dormant Miller slots remain owned");
    for (std::size_t i = 0; i < MillerColumns::maximum_columns; ++i) {
        require(dynamic_cast<Label*>(columns.owned_children()[i * 2].get()) &&
            columns.owned_children()[i * 2 + 1] == columns.column_list(i), "Miller header/list order");
        column_children.push_back(columns.owned_children()[i * 2]);
        column_children.push_back(columns.column_list(i));
    }
    expect(columns, column_children);
    columns.set_columns({});
    expect(columns, column_children);
    InlineStatus status;
    expect(status, {status.action_button(), status.dismiss_button()});
    ColorPicker color;
    std::vector<std::shared_ptr<Element>> color_children;
    for (const auto& channel : color.channels()) color_children.push_back(channel);
    for (std::size_t i = 0; i < color.swatches().size(); ++i) color_children.push_back(color.swatch_button(i));
    expect(color, color_children);
    color.set_swatches({{0, 0, 0}, {255, 255, 255}});
    require(color.owned_children().size() == 9, "Four channels precede all retained swatch slots, including hidden ones");
    const wchar_t* channel_names[]{L"Red", L"Green", L"Blue", L"Alpha"};
    for (std::size_t i = 0; i < 4; ++i)
        require(color.owned_children()[i] == color.channels()[i] && color.channels()[i]->name() == channel_names[i], "Color channel order");
    require(color.owned_children()[4] == color.swatch_button(0) && color.owned_children()[5] == color.swatch_button(1), "Swatch order");
    for (std::size_t i = 4; i < color_children.size(); ++i)
        require(dynamic_cast<const Button&>(*color_children[i]).visible() == (i < 6), "Dormant swatches stay owned but hidden");
    expect(color, color_children);
    VectorCanvas canvas; MapView map; MediaPlayback media; WebContent web;
    expect(canvas, {canvas.accessible_items()}); expect(map, {map.accessible_items()});
    expect(media, {media.status()}); expect(web, {web.status()});
}
void policies_and_dialog() {
    auto active = std::make_shared<Button>(L"Active"), hidden = std::make_shared<Button>(L"Hidden");
    auto pages = std::make_shared<PageView>(); pages->add(active); pages->add(hidden);
    pages->arrange({0, 0, 800, 600});
    auto expander = std::make_shared<Expander>(L"Closed", pages); expander->set_expanded(false);
    auto split = std::make_shared<SplitView>(expander, std::make_shared<TextInput>(L"Second"));
    split->set_layout(Axis::vertical, 100);
    split->arrange({0, 0, 800, 600});
    split->set_secondary_visible(false); split->arrange({0, 0, 800, 600});
    auto content = std::make_shared<ContentView>(split);
    ContentDialog dialog(L"Nested dialog", content);
    std::vector<const Element*> identities;
    const auto capture = [&](const auto& self, const Element& element) -> void {
        identities.push_back(&element);
        for (const auto& child : element.owned_children()) self(self, *child);
    };
    capture(capture, *dialog.popup());
    for (const auto style : {VisualStyle::winui, VisualStyle::classic}) {
        dialog.set_visual_style(style);
        const auto verify = [&](const auto& self, const Element& element) -> void {
            if (const auto* control = dynamic_cast<const Control*>(&element))
                require(control->visual_style() == style, "Dialog typography/presentation propagates through every slot and hidden page");
            for (const auto& child : element.owned_children()) self(self, *child);
        };
        verify(verify, *dialog.popup());
        std::vector<const Element*> after;
        after.swap(identities); capture(capture, *dialog.popup());
        require(identities == after, "Typography retains every composition identity");
    }
    require(!dialog.popup()->is_open() && !expander->expanded(), "Model traversal cannot realize or open lazy content");
}
void lifetime() {
    std::weak_ptr<Element> weak;
    OwnedChildren borrowed;
    {
        Stack owner(Axis::vertical);
        auto child = std::make_shared<Label>(L"Borrowed");
        weak = child; owner.add(child); child.reset();
        borrowed = owner.owned_children();
        require(weak.use_count() == 1, "Borrowed view adds no reference");
    }
    require(weak.expired(), "A surviving view does not retain a discarded tree");
    Breadcrumb breadcrumb;
    breadcrumb.set_segments({{{1, 1}, L"Retired"}});
    weak = breadcrumb.segment_button({1, 1});
    borrowed = breadcrumb.owned_children();
    breadcrumb.set_segments({});
    require(weak.expired(), "Replacement does not retain retired composite children");
}
void benchmark() {
    auto root = owned_workload::make_tree();
    std::size_t expected_visits{};
    const auto expected_sum = owned_workload::walk(*root, expected_visits);
    require(expected_visits == owned_workload::nodes, "Representative workload cardinality");
    constexpr int iterations = owned_workload::iterations;
    std::size_t visits{};
    std::uint64_t sum{};
    const auto before = allocations.load();
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < iterations; ++i) sum += owned_workload::walk(*root, visits);
    const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    const auto allocated = allocations.load() - before;
    require(allocated == 0 && visits == expected_visits * iterations && sum == expected_sum * iterations,
        "Repeated nested ownership walks have exact work and zero allocations");
    std::cout << "owned_walk nodes=" << expected_visits << " iterations=" << iterations << " visits=" << visits
        << " allocations=" << allocated << " elapsed_ms=" << elapsed << " sizeof_element=" << sizeof(Element)
        << " sizeof_control=" << sizeof(Control) << " sizeof_split=" << sizeof(SplitView) << '\n';
}
}
int main(int argc, char** argv) {
    const bool benchmark_only = argc == 2 && std::string_view(argv[1]) == "--benchmark";
    if (argc != 1 && !benchmark_only) {
        std::cerr << "Usage: xui_owned_children_tests [--benchmark]\n";
        return 2;
    }
    try {
        if (!benchmark_only) { layouts(); composites(); policies_and_dialog(); lifetime(); }
        benchmark();
        std::cout << (benchmark_only ? "Owned-child benchmark exact work and allocation checks passed\n" :
            "Owned-child identities, order, policy, lifetime and allocation checks passed\n");
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
