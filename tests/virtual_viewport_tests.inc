namespace virtual_viewport_tests {
using namespace xui;
unsigned assertions{};
void check(bool condition, const std::source_location where = std::source_location::current()) {
    if (!condition) throw std::runtime_error("Virtual viewport assertion at line " + std::to_string(where.line()));
    ++assertions;
}
template<class F> void rejects(F&& action) {
    bool rejected{};
    try { action(); } catch (const std::exception&) { rejected = true; }
    check(rejected);
}
void run() {
    VirtualViewport view(10000, 128, 1);
    unsigned notifications{};
    view.on_request([&] { ++notifications; });
    auto initial = view.request();
    check(initial.epoch > 0 && initial.committed == VirtualViewportRect{} &&
        initial.requested.extent == 1280000 && initial.requested_source_version == 1);
    check(view.try_begin(initial.epoch) == VirtualViewportResult::blocked);
    view.observe({400.4f, 250.9f}, 1.25f, false, 0);
    auto first = view.request();
    check(first.requested.width == 400 && first.requested.height == 250.4f && notifications == 1);
    check(view.try_begin(initial.epoch) == VirtualViewportResult::superseded);
    view.observe({400.4f, 250.9f}, 1.25f, true, 42);
    check(view.request().blocked && notifications == 2);
    check(view.try_begin(view.request().epoch) == VirtualViewportResult::blocked);
    view.cancel(view.request().epoch);
    view.observe({400.4f, 250.9f}, 1.25f, true, 42);
    check(notifications == 2 && view.request().committed == VirtualViewportRect{});
    view.request_offset(1000);
    check(notifications == 3 && view.request().requested.offset == 1000);
    view.cancel(view.request().epoch);
    view.observe({400.4f, 250.9f}, 1.25f, true, 43);
    check(notifications == 4 && view.request().requested.offset == 1000);
    view.cancel(view.request().epoch);
    view.observe({400.4f, 250.9f}, 1.25f, false, 43);
    auto accepted = view.request();
    check(notifications == 5 && !accepted.blocked);
    check(view.try_begin(accepted.epoch) == VirtualViewportResult::ready && view.updating());
    view.request_offset(5000);
    view.set_extent(10001, 2);
    view.observe({200, 100}, 1.25f, false, 43);
    check(notifications == 5 && view.request().committed == VirtualViewportRect{});
    check(view.commit(accepted.epoch) == VirtualViewportResult::ready);
    check(view.committed_epoch() == accepted.epoch);
    auto next = view.request();
    check(notifications == 6 && next.committed == accepted.requested && next.committed_source_version == 1);
    check(next.requested.offset == 5000 && next.requested.width == 200 && next.requested.height == 100 &&
        next.requested.extent == 1280128 && next.requested_source_version == 2);
    check(view.try_begin(next.epoch) == VirtualViewportResult::ready);
    check(view.commit(next.epoch) == VirtualViewportResult::ready && notifications == 6);
    check(view.committed_epoch() == next.epoch);
    const auto stable = view.request();
    for (const auto value : {-1.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
        rejects([&] { view.request_offset(value); });
    rejects([&] { view.set_extent(UINT32_MAX, 3); });
    rejects([&] { view.set_extent(10001, 1); });
    rejects([&] { view.set_extent(10000, 2); });
    rejects([&] { view.set_extent(10001, 0); });
    rejects([&] { view.observe({200, 100}, 16, false, 43); });
    check(view.request().epoch == stable.epoch && view.request().requested == stable.requested);
    view.set_extent(10001, 2); check(view.request().epoch == stable.epoch);
    view.set_extent(10001, 3); check(view.request().epoch > stable.epoch && notifications == 7);
    view.cancel(view.request().epoch); view.request_offset(5000);
    check(notifications == 8);
    view.observe({200, 100}, 8, false, 43);
    check(notifications == 9);
    view.request_offset(std::numeric_limits<float>::max());
    check(view.request().requested.offset == view.requested_maximum_offset());
    const auto active = view.request();
    check(view.try_begin(active.epoch) == VirtualViewportResult::ready);
    view.observe({200, 100}, 8, true, 43);
    rejects([&] { view.commit(active.epoch); });
    check(view.request().committed == stable.committed && view.updating());
    view.close();
    check(view.closed() && !view.updating());
    rejects([&] { view.request(); });
    rejects([&] { view.request_offset(0); });
    view.close();
    rejects([] { VirtualViewport bad(1, 0, 1); });
    rejects([] { VirtualViewport bad(1, 128, 0); });
    rejects([] { VirtualViewport bad(1, 40000, 1); });
    rejects([] { VirtualViewport bad(16000000, 1, 1); });
    {
        auto content = std::make_shared<Stack>(Axis::vertical);
        ScrollView scroll(content);
        const auto state = scroll.begin_virtual_viewport(10000, 128, 1);
        state->observe({200, 100}, 1, false, 0);
        const auto request = state->request();
        check(state->try_begin(request.epoch) == VirtualViewportResult::ready);
        check(state->commit(request.epoch) == VirtualViewportResult::ready);
        scroll.arrange({0, 0, 200, 100});
        scroll.scroll_by(-48);
        check(state->request().requested.offset == 0 && state->request().epoch == request.epoch);
        state->request_offset(1000);
        scroll.reveal({0, 256, 100, 20});
        check(state->request().requested.offset == 176 && state->request().committed.offset == 0);
        scroll.scroll_by(-1000);
        check(state->request().requested.offset == 0);
        rejects([&] { state->request_offset(-1); });
    }
    {
        VirtualViewport same(10000, 128, 3);
        unsigned delivered{};
        same.on_request([&] { ++delivered; });
        same.observe({400, 250}, 1.25f, false, 0);
        auto request = same.request();
        check(same.try_begin(request.epoch) == VirtualViewportResult::ready);
        check(same.commit(request.epoch) == VirtualViewportResult::ready);
        same.request_offset(0);
        check(same.request().epoch > request.epoch && delivered == 2);
        request = same.request();
        check(same.try_begin(request.epoch) == VirtualViewportResult::ready);
        same.request_offset(0);
        check(same.request().epoch > request.epoch && delivered == 2 && same.updating());
        check(same.commit(request.epoch) == VirtualViewportResult::ready && delivered == 3);
        const auto queued = same.request();
        same.cancel(queued.epoch);
        same.request_offset(0);
        check(same.request().epoch > queued.epoch && delivered == 4);
        same.close();
        rejects([&] { same.request_offset(0); });
    }
    std::cout << assertions << " virtual viewport state assertions passed\n";
}
}
