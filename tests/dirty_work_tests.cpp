#include "../src/dirty_work.hpp"
#include "xui/controls.hpp"
#include <iostream>
#include <stdexcept>

namespace {
using namespace xui;
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void origin_and_lifetime() {
    auto root = std::make_shared<Stack>(Axis::vertical);
    auto child = std::make_shared<Button>(L"Origin");
    root->add(child);
    InvalidationEvent received;
    root->set_invalidation_handler([&](InvalidationEvent event) { received = event; });
    child->invalidate(Invalidation::accessibility);
    require(received.origin == child->id() && received.kind == Invalidation::accessibility,
        "Bubbling retains the originating identity and exact work category");
    child->set_automation_id(L"origin-id");
    require(received.origin == child->id() && received.kind == Invalidation::accessibility,
        "Accessible metadata does not broaden into native or structural invalidation");
    child->set_checked(true);
    require(received.origin == child->id() && received.kind == Invalidation::state,
        "Checked state requests both native/accessible state and paint, not topology");
    auto added = std::make_shared<Button>(L"Added");
    root->add(added);
    require(received.origin == root->id() && received.kind == Invalidation::topology,
        "Changing owned children explicitly requests topology work");
    unsigned destroyed_calls{};
    auto transient = std::make_shared<Stack>(Axis::vertical);
    auto survivor = std::make_shared<Button>(L"Survivor");
    transient->add(survivor);
    transient->set_invalidation_handler([&](InvalidationEvent) { ++destroyed_calls; });
    survivor->set_invalidation_handler([&](InvalidationEvent event) {
        require(event.origin == survivor->id(), "Origin is a value, not a retained element pointer");
        transient.reset();
    });
    survivor->invalidate(Invalidation::paint);
    require(!transient && !destroyed_calls, "A destroyed ancestor cannot receive a stale callback");
    survivor->set_invalidation_handler({});
    survivor->invalidate(Invalidation::paint);
    require(!destroyed_calls, "Retained descendants do not revive a detached host");
}
void capture_and_coalescing() {
    DirtyWork pending, active;
    pending.add(7, host_work(Invalidation::paint));
    pending.add(7, host_work(Invalidation::accessibility));
    pending.add(7, host_work(Invalidation::native));
    require(pending.requests().size() == 1 && includes(pending.combined(), HostWork::native) &&
        !includes(pending.combined(), HostWork::topology), "Local categories coalesce without broadening topology");
    active.swap(pending);
    pending.add(7, host_work(Invalidation::topology));
    pending.add(8, host_work(Invalidation::paint));
    require(active.requests().size() == 1 && !includes(active.combined(), HostWork::topology) &&
        pending.requests().size() == 2, "New dirtiness survives independently of the captured pass");
    active.clear();
    active.swap(pending);
    require(includes(active.combined(), HostWork::topology) && includes(active.combined(), HostWork::layout) &&
        includes(active.combined(), HostWork::placement) && includes(active.combined(), HostWork::paint),
        "Topology escalates ordered layout, placement, native, accessibility and paint");
    active.clear();
    pending.add(0, host_work(Invalidation::state));
    active.swap(pending);
    require(active.requests().front().origin == 0 && includes(active.combined(), HostWork::accessibility),
        "Whole-window changes retain an explicit global scope");
    bool rejected{};
    try { (void)host_work(static_cast<Invalidation>(999)); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "Unknown categories are explicit errors, not silent no-work defaults");
}
}
int main() {
    try {
        origin_and_lifetime();
        capture_and_coalescing();
        std::cout << "Invalidation identity, detached lifetime, captured work and category escalation passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
