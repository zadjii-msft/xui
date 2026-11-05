#include "xui/foundation.hpp"
#include "xui/documents.hpp"
#include "xui/navigation.hpp"
#include "xui/commands.hpp"
#include "xui/miller_columns.hpp"
#include "collections_fixture.hpp"
#include <iostream>

namespace {
using namespace xui;
using collections_test::require;
void native_composition() {
    auto number = std::make_unique<NumericInput>(L"Scoped number");
    const auto editor = number->editor();
    const auto increase = number->increase_button();
    std::vector<unsigned> order;
    number->on_change([&](double value) { require(value == number->value(), "Parent observers see committed model state"); order.push_back(1); });
    editor->on_change([&](const std::wstring&) { require(number->value() == 12, "Editor observers run after numeric parsing"); order.push_back(2); });
    editor->commit_text(L"12");
    require(order == std::vector<unsigned>{1, 2}, "Numeric behavior precedes one parent and one child observation");
    editor->on_change({});
    editor->commit_text(L"13");
    require(number->value() == 13 && order == std::vector<unsigned>{1, 2, 1},
        "Removing the editor observer preserves numeric composition");
    increase->on_click([&] { require(number->value() == 14, "Button observer sees the stepped value"); order.push_back(3); });
    require(increase->invoke() && number->value() == 14, "Observed step button still performs its step");
    increase->on_click({});
    require(increase->invoke() && number->value() == 15, "Unsubscribed step button still performs its step");
    const auto events = order.size();
    number->set_value(3);
    require(order.size() == events, "Programmatic numeric values do not synthesize action callbacks");
    unsigned orphan_observations{};
    editor->on_change([&](const std::wstring&) { ++orphan_observations; });
    number.reset();
    editor->commit_text(L"16");
    require(orphan_observations == 1, "A retained child keeps its own observer, not its retired parent's behavior");

    auto combo = std::make_unique<ComboBox>(L"Editable", true);
    const auto combo_editor = combo->editor();
    unsigned edits{}, children{};
    combo->on_edit([&](const auto&) { ++edits; });
    combo_editor->on_change([&](const auto&) { require(edits == children + 1, "Combo behavior precedes child observation"); ++children; });
    combo_editor->commit_text(L"first");
    combo_editor->on_change({});
    combo_editor->commit_text(L"second");
    require(edits == 2 && children == 1, "Editable ComboBox behavior survives observer replacement and removal");
    combo.reset();
    combo_editor->commit_text(L"retained after parent");

    ColorPicker color;
    auto red = color.channels()[0];
    unsigned observed{};
    red->on_change([&](double value) { require(color.value().red == value, "Channel composition precedes the observer"); ++observed; });
    red->change_value(42);
    red->on_change({});
    red->change_value(43);
    require(color.value().red == 43 && observed == 1, "Observing a color channel cannot disable its parent behavior");

    NavigationView navigation;
    unsigned clicks{};
    const bool expanded = navigation.expanded();
    navigation.toggle_button()->on_click([&] { ++clicks; });
    navigation.toggle_button()->invoke();
    require(navigation.expanded() != expanded && clicks == 1, "A navigation button preserves composition and observation");
    navigation.toggle_button()->on_click({});
    navigation.toggle_button()->invoke();
    require(navigation.expanded() == expanded, "Removing a navigation observer leaves composition intact");
}
void captured_delivery() {
    detail::CallbackScope scope;
    detail::Callback<void()> signal;
    std::vector<unsigned> events;
    signal.compose(scope, [&] {
        events.push_back(1);
        signal = [&] { events.push_back(3); };
    });
    signal = [&] { events.push_back(2); };
    signal();
    require(events == std::vector<unsigned>{1, 2}, "Observer mutation during composition affects the next event");
    signal();
    require(events == std::vector<unsigned>{1, 2, 1, 3}, "The next event captures the replacement once");

    auto owner = std::make_shared<unsigned>();
    const std::weak_ptr<unsigned> lifetime = owner;
    signal.compose(scope, [&, owner] {
        scope.retire();
        require(!lifetime.expired(), "Retirement retains the callable's foreign/owner captures through its current dispatch");
        ++*owner;
    });
    signal = std::function<void()>{};
    owner.reset();
    signal();
    require(lifetime.expired() && !signal, "Retirement releases captured ownership exactly after the in-flight callback");
    scope.retire();
    bool rejected{};
    try { signal.compose(scope, [] {}); } catch (const std::logic_error&) { rejected = true; }
    require(rejected && !signal, "A failed registration on a retired scope preserves the previous slot");

    unsigned orphan{};
    auto dying = std::make_unique<NumericInput>();
    auto step = dying->increase_button();
    dying->on_change([&](double) { dying.reset(); });
    step->on_click([&] { ++orphan; });
    require(step->invoke() && !dying && orphan == 1, "Parent destruction during composition preserves the captured child observer safely");
    step->invoke();
    require(orphan == 2, "Later retained-child events do not access the retired parent");

    detail::CallbackScope cancellation;
    detail::Callback<void()> first, second;
    unsigned cancelled_calls{};
    first.compose(cancellation, [&] { ++cancelled_calls; });
    auto capture = std::shared_ptr<unsigned>(new unsigned, [&](unsigned* value) {
        first();
        delete value;
    });
    second.compose(cancellation, [capture] {});
    capture.reset();
    cancellation.retire();
    require(cancelled_calls == 0, "All registrations are cancelled before capture destruction can reenter a sibling");

    detail::CallbackScope unlinking;
    auto removed = std::make_unique<detail::Callback<void()>>();
    auto retiring_capture = std::shared_ptr<unsigned>(new unsigned, [&](unsigned* value) {
        unlinking.retire();
        delete value;
    });
    removed->compose(unlinking, [retiring_capture] {});
    retiring_capture.reset();
    removed.reset();
    require(!unlinking.active(), "A registration unlinks before destroying captures that can retire its scope");

    detail::CallbackScope replacement;
    detail::Callback<void()> rebound;
    unsigned replaced{}, captured{};
    rebound = [&] { ++captured; };
    rebound.compose(replacement, [&] { rebound.compose(replacement, [&] { ++replaced; }); });
    rebound();
    rebound();
    require(replaced == 1 && captured == 2, "Rebinding during dispatch preserves the current callable and captured observation");
}
void miller_events() {
    MillerColumns columns;
    const auto source = std::make_shared<collections_test::Items>(4);
    const auto child = columns.column_list(1);
    std::vector<unsigned> order;
    columns.on_selection([&](std::size_t column, ItemKey) { require(column == 1, "Parent selection has the actual column"); order.push_back(1); });
    child->on_selection([&] { order.push_back(2); });
    child->on_focus([&] { require(columns.active_column() == 1, "Focus composition precedes observation"); });
    columns.set_columns({{L"First", source}, {L"Second", source}});
    child->set_focused(true);
    require(child->select(source->key(1)) && order == std::vector<unsigned>{1, 2},
        "Miller selection emits parent then child exactly once without an ABI relay");
    columns.on_selection([&](std::size_t, ItemKey) { columns.set_columns({}); });
    order.clear();
    child->select(source->key(2));
    require(order.empty(), "Path replacement suppresses a stale child selection after the parent event");
}
void immediate_delivery() {
    RadioGroup radio;
    radio.set_items({{1, L"First"}, {2, L"Second"}}, 1);
    unsigned old_calls{}, next_calls{};
    radio.on_change([&](std::uint64_t id) {
        ++old_calls;
        radio.on_change([&](std::uint64_t) { ++next_calls; });
        require(radio.selected() == id, "Immediate observers see committed selection");
    });
    radio.select(2); radio.select(1);
    require(old_calls == 1 && next_calls == 1, "Immediate delivery retains the selected observer during replacement");

    NumericInput number;
    RangeInput range;
    unsigned changes{};
    number.on_change([&](double) { ++changes; });
    range.on_change([&](double) { ++changes; });
    number.change_value(number.value());
    number.commit_text(number.editor()->text());
    range.change_value(range.value());
    require(changes == 0, "Unchanged numeric/range values do not deliver an event");

    detail::CallbackScope scope;
    detail::CompositionAccess::change(number, scope, [&](double) { scope.retire(); throw std::runtime_error("controlled"); });
    bool failed{};
    try { number.change_value(2); }
    catch (const std::runtime_error& error) { failed = std::string_view(error.what()) == "controlled"; }
    require(failed && changes == 0, "Composition failure unwinds the lease without delivering the observer");
    number.change_value(3);
    require(changes == 1, "Retired composition leaves the observer independently usable after unwinding");
}
}
int main() {
    try {
        native_composition();
        captured_delivery();
        miller_events();
        immediate_delivery();
        std::cout << "Composition/observer separation, reentrant mutation, scope retirement and Miller ownership passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
