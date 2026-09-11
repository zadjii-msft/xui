#include "xui/core.hpp"
#include "xui/file_list.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>
#endif

namespace {
std::atomic<std::size_t> allocations{};
}
void* operator new(std::size_t size) {
    allocations.fetch_add(1, std::memory_order_relaxed);
    if (auto memory = std::malloc(size ? size : 1)) return memory;
    throw std::bad_alloc{};
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

namespace {
using Clock = std::chrono::steady_clock;
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class F> void await(F predicate, const char* message) {
    const auto limit = Clock::now() + std::chrono::seconds(15);
    while (!predicate()) {
        require(Clock::now() < limit, message);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
double milliseconds(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
std::shared_ptr<const std::vector<xui::FileItem>> synthetic(std::size_t count) {
    auto items = std::make_shared<std::vector<xui::FileItem>>();
    items->reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        auto number = std::to_wstring(i);
        std::wstring name = L"File-" + std::wstring(7 - number.size(), L'0') + number + L".txt";
        items->push_back({static_cast<xui::ItemId>((count - i) * 17 + 1),
            name, L"C:\\synthetic\\" + name, false});
    }
    return items;
}

xui::ViewResult result_of(xui::ViewWorker& worker, std::uint64_t generation) {
    std::optional<xui::ViewResult> result;
    await([&] {
        result = worker.take_result();
        return result.has_value();
    }, "Worker did not publish a result");
    require(result->generation == generation, "Worker published a stale generation");
    return std::move(*result);
}

void replacement_tests() {
    auto source = xui::FileSnapshot::build(synthetic(20));
    auto all = xui::FilteredView::build(source, {});
    xui::FileList list;
    list.set_view(all);
    list.select(7);
    list.focus_item(12);
    const auto selected = list.model().selected_id();
    const auto focused = list.focused_id();
    auto hidden = xui::FilteredView::build(source, L"FILE-0000001");
    list.set_view(hidden);
    require(list.model().selected_id() == selected && list.focused_id() == focused,
        "Filtering must retain independent identities");
    require(!list.model().selected_index() && !list.focused_index(),
        "Hidden identities must not expose a visible index");
    list.set_view(all);
    require(list.model().selected_index() == 7 && list.focused_index() == 12,
        "Clearing the filter must restore both identities");
    auto replacement = std::make_shared<const std::vector<xui::FileItem>>(
        std::vector<xui::FileItem>{{*focused, L"Renamed focus", L"focus", false},
                                  {*selected, L"Renamed selection", L"selection", false}});
    list.set_view(xui::FilteredView::build(xui::FileSnapshot::build(replacement), {}));
    require(list.model().selected_index() == 1 && list.focused_index() == 0,
        "Changed order must resolve IDs, not previous indices");
    auto removed = std::make_shared<const std::vector<xui::FileItem>>(
        std::vector<xui::FileItem>{{*selected, L"Survivor", L"survivor", false}});
    list.set_view(xui::FilteredView::build(xui::FileSnapshot::build(removed), L"none"));
    require(list.model().selected_id() == selected && !list.focused_id(),
        "Replacement must retain a hidden selection and clear a removed focus");
    list.set_view(xui::FilteredView::build(xui::FileSnapshot::build(nullptr), {}));
    require(!list.model().selected_id() && !list.focused_id(), "Empty source must clear identities");
    require(all->indices().size() == 20 && hidden->indices().size() == 1,
        "Previously published views must stay immutable");
    auto duplicates = std::make_shared<const std::vector<xui::FileItem>>(
        std::vector<xui::FileItem>{{1, L"A", L"A", false}, {1, L"B", L"B", false}});
    bool rejected{};
    try { xui::FileSnapshot::build(duplicates); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "Duplicate IDs must fail before publication");
    rejected = false;
    try { list.set_view(nullptr); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "Null view must be rejected");
}

void worker_tests() {
    auto source = xui::FileSnapshot::build(synthetic(100000));
    std::atomic<int> scans{}, cancelled{}, notifications{};
    std::atomic<bool> release{}, fail{};
    xui::ViewWorker worker([&](const xui::CancelCheck& cancel) -> xui::SourceResult {
        ++scans;
        while (!release && !cancel()) std::this_thread::yield();
        if (cancel()) { ++cancelled; return {}; }
        if (fail.exchange(false)) throw std::runtime_error("Injected source error");
        return {source, {}};
    }, [&] { ++notifications; });
    worker.start();
    auto generation = worker.request(L"old", true);
    await([&] { return scans == 1; }, "Initial source work did not start");
    for (int i = 0; i < 200; ++i) generation = worker.request(L"File-" + std::to_wstring(i));
    generation = worker.request(L"FILE-0000042");
    require(scans == 1 && cancelled == 0, "Query edits must not restart source work");
    release = true;
    auto result = result_of(worker, generation);
    require(result.view && result.view->indices() == std::vector<xui::RowIndex>{42},
        "Latest query must apply to the completed source");
    require(result.view->source() == source, "Worker filters must share the source");
    require(scans == 1, "Filtering must not call the source loader");

    // Leave one completed result pending, then supersede it before the consumer reads.
    const auto old_notifications = notifications.load();
    worker.request(L"0000043");
    await([&] { return notifications > old_notifications; }, "Pending result did not finish");
    release = false;
    generation = worker.request(L"stale-source", true);
    await([&] { return scans == 2; }, "Refresh did not start");
    require(!worker.take_result(), "A pending stale result must not cross the worker boundary");
    generation = worker.request(L"0000099", true);
    await([&] { return scans == 3; }, "Refresh did not cancel the previous source");
    require(cancelled == 1, "Only a source refresh must cancel source work");
    for (int i = 0; i < 100; ++i) generation = worker.request(i % 2 ? L".txt" : L"missing");
    generation = worker.request(L"0000099");
    release = true;
    result = result_of(worker, generation);
    require(result.view && result.view->indices() == std::vector<xui::RowIndex>{99},
        "Rapid refresh and query edits must publish the latest pair");

    fail = true;
    generation = worker.request(L"error", true);
    result = result_of(worker, generation);
    require(!result.view && result.error.find(L"Injected source error") != std::wstring::npos,
        "Worker exceptions must produce a visible error result");
    generation = worker.request(L"0000001", true);
    result = result_of(worker, generation);
    require(result.view && result.error.empty(), "Worker must recover after an error");

    const auto before_stop = scans.load();
    release = false;
    worker.request(L"shutdown", true);
    await([&] { return scans > before_stop; }, "Shutdown test needs active source work");
    worker.request_stop();
    worker.join();
    require(cancelled == 2, "Shutdown must cancel active source work");
    bool rejected{};
    try { worker.request(L"after stop"); }
    catch (const std::logic_error&) { rejected = true; }
    require(rejected, "A stopped worker must reject requests");

    // Cancel filtering itself, with no directory loader involved.
    int checks{};
    require(!xui::FilteredView::build(source, L".txt", [&] { return ++checks == 4; }),
        "Filtering must stop at a deterministic cancellation checkpoint");
    require(checks == 4, "Filtering must not continue after cancellation");
    checks = 0;
    require(!xui::FileSnapshot::build(source->items(), [&] { return ++checks == 5; }),
        "Snapshot construction must support cancellation");
    require(checks == 5, "Snapshot construction must stop immediately after cancellation");
    require(!xui::FilteredView::build(source, {}, [] { return true; }),
        "An empty query must still honor cancellation");
    checks = 0;
    const auto final_checkpoint = 3 + (source->items()->size() + 255) / 256;
    require(!xui::FilteredView::build(source, L".txt", [&] {
        return static_cast<std::size_t>(++checks) == final_checkpoint;
    }), "Cancellation at the final checkpoint must reject a complete but stale view");
    auto unnamed = std::make_shared<std::vector<xui::FileItem>>();
    for (xui::ItemId id = 1024; id > 0; --id) unnamed->push_back({id, {}, {}, false});
    checks = 0;
    require(!xui::FileSnapshot::build(unnamed, [&] { return ++checks == 6; }),
        "ID sorting must honor cancellation after the name-building pass");

    for (int i = 0; i < 30; ++i) {
        xui::ViewWorker short_lived([&](const xui::CancelCheck&) { return xui::SourceResult{source, {}}; }, {});
        short_lived.start();
        short_lived.request(L".txt", true);
        short_lived.request(L"none");
        short_lived.request_stop();
        short_lived.join();
    }
}

void source_generation_test() {
    auto old_source = xui::FileSnapshot::build(synthetic(10));
    auto new_source = xui::FileSnapshot::build(
        std::make_shared<const std::vector<xui::FileItem>>(
            std::vector<xui::FileItem>{{98765, L"New source", L"new", false}}));
    std::atomic<int> scans{};
    xui::ViewWorker worker([&](const xui::CancelCheck& cancel) {
        if (++scans == 1) {
            while (!cancel()) std::this_thread::yield();
            // A loader can finish concurrently with cancellation and still return data.
            return xui::SourceResult{old_source, {}};
        }
        return xui::SourceResult{new_source, {}};
    }, {});
    worker.start();
    worker.request(L"File", true);
    await([&] { return scans == 1; }, "Old source did not start");
    const auto latest = worker.request(L"new", true);
    const auto result = result_of(worker, latest);
    require(scans == 2 && result.view && result.view->source() == new_source,
        "A completed cancelled source must never replace the current source");
    require(result.view->indices().size() == 1 && result.view->find(98765) == 0,
        "The current query must apply only to the current source");
}

void retirement_test() {
    std::atomic<bool> deleted{};
    std::thread::id deleted_on;
    auto items = std::shared_ptr<const std::vector<xui::FileItem>>(
        new std::vector<xui::FileItem>{{1, L"Retire", L"retire", false}},
        [&](const auto* value) { delete value; deleted_on = std::this_thread::get_id(); deleted = true; });
    auto view = xui::FilteredView::build(xui::FileSnapshot::build(std::move(items)), {});
    xui::ViewWorker worker([](const xui::CancelCheck&) {
        return xui::SourceResult{xui::FileSnapshot::build(nullptr), {}};
    }, {});
    worker.start();
    worker.retire(std::move(view));
    await([&] { return deleted.load(); }, "Retired source was not released");
    require(deleted_on != std::this_thread::get_id(), "Retired collection must be released off the caller thread");
}

void mailbox_destruction_test() {
    std::atomic<int> scans{}, notifications{};
    std::atomic<bool> deleting{}, release_delete{}, requested{};
    xui::ViewWorker worker([&](const xui::CancelCheck&) {
        if (++scans == 1) {
            auto items = std::shared_ptr<const std::vector<xui::FileItem>>(
                new std::vector<xui::FileItem>{{1, L"Old", L"old", false}},
                [&](const auto* value) {
                    deleting = true;
                    while (!release_delete) std::this_thread::yield();
                    delete value;
                });
            return xui::SourceResult{xui::FileSnapshot::build(std::move(items)), {}};
        }
        return xui::SourceResult{xui::FileSnapshot::build(synthetic(10)), {}};
    }, [&] { ++notifications; });
    worker.start();
    worker.request(L"", true);
    await([&] { return notifications == 1; }, "Unread result did not finish");
    worker.request(L"", true);
    // The pending first result owns the old source. Replacing it calls this deleter.
    bool started{};
    try {
        await([&] { return deleting.load(); }, "Unread source did not retire");
        started = true;
    } catch (...) {
        release_delete = true;
        throw;
    }
    require(started, "Old source destruction must start");
    std::uint64_t latest{};
    std::jthread requester([&] { latest = worker.request(L"0000003"); requested = true; });
    bool nonblocking{};
    try {
        await([&] { return requested.load(); }, "Request mutex was held during source destruction");
        nonblocking = true;
    } catch (...) {
        release_delete = true;
        requester.join();
        throw;
    }
    release_delete = true;
    requester.join();
    require(nonblocking, "A request must not wait for old source destruction");
    auto result = result_of(worker, latest);
    require(result.view && result.view->indices() == std::vector<xui::RowIndex>{3},
        "Worker must process the request after old source destruction");
}

void memory(const char* label) {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    require(GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
        sizeof(counters)) != 0, "Read process memory");
    std::cout << label << " private_bytes=" << counters.PrivateUsage
              << " working_set_bytes=" << counters.WorkingSetSize << '\n';
#else
    std::cout << label << " process memory counters unavailable\n";
#endif
}

void large_case(std::size_t count, bool benchmark) {
    const auto create_start = Clock::now();
    auto items = synthetic(count);
    const double create_ms = milliseconds(create_start);
    const auto build_start = Clock::now();
    auto source = xui::FileSnapshot::build(items);
    const double snapshot_ms = milliseconds(build_start);
    const auto all_start = Clock::now();
    auto all = xui::FilteredView::build(source, {});
    const double all_ms = milliseconds(all_start);
    require(source->items() == items && all->source() == source, "Snapshots must not copy item arrays");
    require(all->indices().size() == count, "Empty query must include all rows");
    require(std::is_sorted(all->indices().begin(), all->indices().end()),
        "Visible indices must stay sorted for logarithmic reverse lookup");
    xui::FileList list;
    list.set_view(all);
    list.set_viewport_height(320);
    std::size_t selection_events{};
    list.on_selection_change([&] { ++selection_events; });
    const auto* const indices = all->indices().data();
    const auto allocations_before = allocations.load();
    const auto lookup_start = Clock::now();
    std::uint64_t checksum{};
    constexpr std::size_t operations = 20000;
    for (std::size_t i = 0; i < operations; ++i) {
        const std::size_t index = (i * 7919) % count;
        list.select(index, false);
        list.focus_item((index + 1) % count);
        const auto selected = list.model().selected_index();
        const auto focused = list.focused_index();
        require(selected == index && focused == (index + 1) % count,
            "Large-list selection and focus lookups must resolve the exact identity");
        require(list.model().selected_item()->id == (*items)[index].id,
            "Large-list ID lookup must resolve the selected item");
        const auto rows = list.visible_rows();
        require(rows.end - rows.begin <= 14, "Viewport work must remain independent of source size");
        checksum += *selected + *focused;
    }
    const double lookup_ms = milliseconds(lookup_start);
    require(allocations.load() == allocations_before, "Select, focus, scroll and lookup must allocate nothing");
    require(selection_events == operations, "Public list selection events retain zero-allocation interactions");
    require(list.model().view() == all && all->indices().data() == indices,
        "Ordinary interactions must not replace or copy the view");
    require(!source->find(0) && !all->find(0), "Missing IDs must return no row");
    auto sparse = xui::FilteredView::build(source, L"FILE-0000042");
    require(sparse->indices() == std::vector<xui::RowIndex>{42} && sparse->find((*items)[42].id) == 0,
        "Sparse result must share the same ID lookup");
    require(!sparse->find((*items)[43].id), "Hidden existing IDs must have no visible index");
    auto empty = xui::FilteredView::build(source, L"not-in-any-name");
    require(empty->indices().empty() && !empty->find((*items)[42].id), "No-match lookup must return no row");
    if (benchmark) {
        std::cout << "rows=" << count << " synthetic_ms=" << create_ms
                  << " snapshot_ms=" << snapshot_ms << " empty_query_ms=" << all_ms
                  << " interaction_20000_ms=" << lookup_ms << " checksum=" << checksum << '\n';
        for (const auto* query : {L".TXT", L"FILE-0000042", L"not-in-any-name"}) {
            std::vector<double> samples;
            std::size_t matched{}, bytes{};
            for (int repeat = 0; repeat < 5; ++repeat) {
                const auto start = Clock::now();
                auto view = xui::FilteredView::build(source, query);
                samples.push_back(milliseconds(start));
                matched = view->indices().size();
                bytes = view->indices().capacity() * sizeof(xui::RowIndex);
            }
            std::sort(samples.begin(), samples.end());
            std::wcout << L"query=" << query << L" matched=" << matched
                       << L" filter_median_ms=" << samples[2] << L" view_capacity_bytes=" << bytes << L'\n';
        }
        std::cout << "snapshot_index_capacity_bytes=" << source->index_bytes()
                  << " all_view_bytes=" << all->indices().capacity() * sizeof(xui::RowIndex) << '\n';
        memory("retained_source_and_views");
    }
}
}

int main(int argc, char** argv) {
    try {
        const bool benchmark = argc > 1 && std::string_view(argv[1]) == "--benchmark";
        if (benchmark) memory("before_workload");
        else {
            replacement_tests();
            worker_tests();
            source_generation_test();
            retirement_test();
            mailbox_destruction_test();
        }
        large_case(100000, benchmark);
        large_case(1000000, benchmark);
        std::cout << "Performance invariants passed: 100k/1m rows, zero-allocation interactions.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
