#include "collections_fixture.hpp"
#include "xui/application.hpp"
#include "../src/drawing.hpp"
#include <commctrl.h>
#include <psapi.h>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <new>
#include <string_view>

namespace {
std::atomic<std::size_t> allocations{};
}
void* operator new(std::size_t size) {
    allocations.fetch_add(1, std::memory_order_relaxed);
    if (auto* value = std::malloc(size ? size : 1)) return value;
    throw std::bad_alloc{};
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* value) noexcept { std::free(value); }
void operator delete[](void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }
void operator delete[](void* value, std::size_t) noexcept { std::free(value); }

namespace {
using namespace xui;
using collections_test::require;
constexpr UINT update = WM_APP + 12, metrics = WM_APP + 60;
std::uint64_t ticks(FILETIME value) {
    return (std::uint64_t(value.dwHighDateTime) << 32) | value.dwLowDateTime;
}
struct ThreadCpu {
    std::uint64_t kernel100ns{}, user100ns{};
};
ThreadCpu thread_cpu() {
    FILETIME created{}, exited{}, kernel{}, user{};
    require(GetThreadTimes(GetCurrentThread(), &created, &exited, &kernel, &user) != FALSE,
        "Read owning UI thread CPU");
    return {ticks(kernel), ticks(user)};
}
struct Measurement {
    ThreadCpu before, after;
    double wall_ms{};
    std::size_t allocations{};
};
template<class F> Measurement measure(unsigned count, F action) {
    const auto before_allocations = allocations.load();
    const auto before_cpu = thread_cpu();
    const auto before = std::chrono::steady_clock::now();
    for (unsigned index = 0; index < count; ++index) action(index);
    const auto wall = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - before).count();
    const auto after_cpu = thread_cpu();
    require(after_cpu.kernel100ns >= before_cpu.kernel100ns &&
        after_cpu.user100ns >= before_cpu.user100ns, "Owning thread CPU counters are monotonic");
    const auto allocated = allocations.load() - before_allocations;
    return {before_cpu, after_cpu, wall, allocated};
}
struct Work {
    std::uint64_t item_reads{}, item_keys{}, item_lookups{}, grid_reads{}, grid_keys{}, grid_lookups{};
    std::uint64_t item_callbacks{}, grid_callbacks{}, updates{}, checksum{};
};
void report(const char* phase, unsigned count, const Measurement& timing, const Work& work,
    double item_offset, double grid_offset, double legacy_offset,
    ItemKey selected_item, RowKey selected_grid, ItemId selected_file, unsigned source_transitions, LRESULT paints) {
    const auto cpu = (timing.after.kernel100ns - timing.before.kernel100ns +
        timing.after.user100ns - timing.before.user100ns) / 10000.0;
    std::cout << "{\"phase\":\"" << phase << "\",\"iterations\":" << count
        << ",\"wall_ms\":" << timing.wall_ms << ",\"ui_thread_cpu_ms\":" << cpu
        << ",\"kernel_before100ns\":" << timing.before.kernel100ns
        << ",\"kernel_after100ns\":" << timing.after.kernel100ns
        << ",\"user_before100ns\":" << timing.before.user100ns
        << ",\"user_after100ns\":" << timing.after.user100ns
        << ",\"cpp_allocations\":" << timing.allocations
        << ",\"item_reads\":" << work.item_reads << ",\"item_keys\":" << work.item_keys
        << ",\"item_lookups\":" << work.item_lookups << ",\"grid_reads\":" << work.grid_reads
        << ",\"grid_keys\":" << work.grid_keys << ",\"grid_lookups\":" << work.grid_lookups
        << ",\"item_callbacks\":" << work.item_callbacks << ",\"grid_callbacks\":" << work.grid_callbacks
        << ",\"updates\":" << work.updates << ",\"checksum\":" << work.checksum
        << ",\"item_offset\":" << item_offset << ",\"grid_offset\":" << grid_offset
        << ",\"legacy_offset\":" << legacy_offset << ",\"selected_item\":" << selected_item.id
        << ",\"selected_grid\":" << selected_grid.id << ",\"selected_file\":" << selected_file
        << ",\"source_transitions\":" << source_transitions << ",\"paints\":" << paints << "}\n";
}
std::uint64_t mix(std::uint64_t checksum, std::uint64_t value) {
    return ((checksum * 16777619) ^ value) & 0x7fffffffffffffffULL;
}
class CountedRows final : public GridSource {
public:
    explicit CountedRows(bool reverse) : rows_(100000, reverse) {}
    mutable std::atomic<std::uint64_t> reads{}, keys{}, lookups{};
    std::size_t size() const override { return rows_.size(); }
    RowKey key(std::size_t row) const override { ++keys; return rows_.key(row); }
    std::optional<std::size_t> find(RowKey key) const override { ++lookups; return rows_.find(key); }
    std::wstring text(std::size_t row, std::size_t column) const override {
        ++reads; return rows_.text(row, column);
    }
private:
    collections_test::Rows rows_;
};
std::size_t descendants(HWND root) {
    std::size_t count{};
    require(EnumChildWindows(root, [](HWND, LPARAM data) -> BOOL {
        ++*reinterpret_cast<std::size_t*>(data); return TRUE;
    }, reinterpret_cast<LPARAM>(&count)) != FALSE, "Enumerate owned descendants");
    return count;
}
HWND named(HWND root, const wchar_t* name) {
    struct Search { const wchar_t* name; HWND found{}; } search{name};
    EnumChildWindows(root, [](HWND child, LPARAM data) -> BOOL {
        auto& value = *reinterpret_cast<Search*>(data);
        wchar_t name[128]{};
        GetWindowTextW(child, name, 128);
        if (std::wstring_view(name) == value.name) { value.found = child; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    require(search.found != nullptr, "Find the owned native peer");
    return search.found;
}
struct EditorWrites {
    unsigned text{}, font{}, selection{};
    bool empty() const { return !text && !font && !selection; }
};
LRESULT CALLBACK editor_messages(HWND hwnd, UINT message, WPARAM wp, LPARAM lp,
    UINT_PTR, DWORD_PTR data) {
    auto& writes = *reinterpret_cast<EditorWrites*>(data);
    if (message == WM_SETTEXT) ++writes.text;
    if (message == WM_SETFONT) ++writes.font;
    if (message == EM_SETSEL) ++writes.selection;
    return DefSubclassProc(hwnd, message, wp, lp);
}
struct EditorObserver {
    HWND window;
    EditorWrites writes;
    explicit EditorObserver(HWND value) : window(value) {
        require(SetWindowSubclass(window, editor_messages, 901,
            reinterpret_cast<DWORD_PTR>(&writes)) != FALSE, "Observe unchanged native editor");
    }
    ~EditorObserver() {
        if (window && IsWindow(window) && !RemoveWindowSubclass(window, editor_messages, 901))
            OutputDebugStringW(L"XUI probe: failed to remove native editor observer during unwinding.\n");
    }
    void detach() {
        require(RemoveWindowSubclass(window, editor_messages, 901) != FALSE, "Remove native editor observer");
        window = nullptr;
    }
};
class FailingRows final : public GridSource {
public:
    HWND owner{};
    mutable bool failed{};
    std::size_t size() const override {
        if (owner && FindWindowExW(owner, nullptr, L"Xui.Control.1", L"Broken grid")) {
            failed = true;
            throw std::runtime_error("Controlled row-family native materialization failure");
        }
        return 10;
    }
    RowKey key(std::size_t index) const override { return {index + 1, 1}; }
    std::optional<std::size_t> find(RowKey key) const override {
        return key.version == 1 && key.id && key.id <= size() ?
            std::optional<std::size_t>{key.id - 1} : std::nullopt;
    }
    std::wstring text(std::size_t, std::size_t) const override { return L"Value"; }
};
void native_failure_contract() {
    WindowOptions options;
    options.title = L"XUI row-family rollback";
    options.show_activated = false;
    Window window(options);
    auto host = std::make_shared<ContentHost>(std::make_shared<Label>(L"Original content"));
    window.set_content(host);
    auto source = std::make_shared<FailingRows>();
    HWND native_owner{};
    bool rejected{};
    window.post([&] {
        native_owner = source->owner = window.native_window();
        auto replacement = std::make_shared<Stack>(Axis::vertical);
        auto items = std::make_shared<ItemsView>(L"Attached collection before failure");
        items->set_items(std::make_shared<collections_test::Items>());
        auto grid = std::make_shared<DataGrid>(L"Broken grid");
        grid->set_columns({{L"Value", 160}});
        grid->set_source(source);
        replacement->add(items, 1); replacement->add(grid, 1);
        try { window.replace_content(*host, replacement); }
        catch (const std::runtime_error&) { rejected = true; }
    });
    const auto result = Application::run(window);
    require(result == 1 && rejected && source->failed &&
        window.error().find(L"Controlled row-family native materialization failure") != std::wstring::npos,
        "Partial row-family native failure is explicit, not a successful empty replacement");
    require(native_owner && !IsWindow(native_owner) && Drawing::live_targets() == 0,
        "Failure retires already attached collection/grid children and the whole native owner");
}
void probe() {
    WindowOptions options;
    options.title = L"XUI collection host probe";
    options.size = {1080, 720};
    options.show_activated = false;
    Window window(options);
    window.set_presentation("Segoe UI", 14, false, false);
    auto items = std::make_shared<ItemsView>(L"Probe collection");
    const auto first = std::make_shared<collections_test::Items>(1000000);
    const auto second = std::make_shared<collections_test::Items>(1000000);
    items->set_items(first);
    auto grid = std::make_shared<DataGrid>(L"Probe grid");
    grid->set_columns({{L"Name", 200}, {L"Value", 120}});
    const auto rows = std::make_shared<CountedRows>(false);
    const auto other_rows = std::make_shared<CountedRows>(true);
    grid->set_source(rows);
    auto files = std::make_shared<FileList>(L"Probe legacy list");
    auto file_items = std::make_shared<std::vector<FileItem>>();
    file_items->reserve(10000);
    for (unsigned index = 0; index < 10000; ++index)
        file_items->push_back({index + 1, L"File " + std::to_wstring(index), L"", false});
    const auto snapshot = FileSnapshot::build(file_items);
    const auto view = FilteredView::build(snapshot, {});
    files->set_view(view);
    auto editor = std::make_shared<TextInput>(L"Probe native editor");
    editor->set_text(L"Selection and native text must survive scrolling");
    auto panes = std::make_shared<Stack>(Axis::horizontal);
    panes->add(items, 1); panes->add(grid, 1); panes->add(files, 1);
    auto root = std::make_shared<Stack>(Axis::vertical);
    root->add(editor); root->add(panes, 1);
    window.set_content(root);
    bool completed{};
    window.post([&] {
        const auto hwnd = window.native_window();
        std::uint64_t updates{}, item_callbacks{}, grid_callbacks{};
        items->on_selection([&] { ++item_callbacks; });
        grid->on_select([&] { ++grid_callbacks; });
        struct Callbacks {
            ItemsView& items;
            DataGrid& grid;
            ~Callbacks() { items.on_selection({}); grid.on_select({}); }
        } callbacks{*items, *grid};
        const auto flush = [&] {
            MSG queued{};
            unsigned pending{};
            while (PeekMessageW(&queued, hwnd, update, update, PM_REMOVE))
                require(++pending <= 1, "Control updates stay coalesced to one queued message");
            SendMessageW(hwnd, update, 0, 0);
            ++updates;
        };
        flush(); UpdateWindow(hwnd);
        const auto item_hwnd = named(hwnd, L"Probe collection");
        const auto grid_hwnd = named(hwnd, L"Probe grid");
        const auto file_hwnd = named(hwnd, L"Probe legacy list");
        const auto editor_hwnd = named(hwnd, editor->text().c_str());
        SendMessageW(item_hwnd, WM_MOUSELEAVE, 0, 0);
        flush(); UpdateWindow(hwnd);
        require(!items->hovered(), "Measured collection starts without native pointer hover");
        const auto native_count = descendants(hwnd);
        const auto peer_count = SendMessageW(hwnd, metrics, 14, 0);
        const auto target_count = SendMessageW(hwnd, metrics, 11, 0);
        require(peer_count == 4 && target_count == 1 && native_count == 5,
            "One peer per control, one root target, no per-row HWNDs");
        EditorObserver editor_observer(editor_hwnd);
        const auto layouts = SendMessageW(hwnd, metrics, 2, 0);
        const auto work = [&] {
            return Work{first->reads + second->reads, first->keys + second->keys,
                first->lookups + second->lookups, rows->reads + other_rows->reads,
                rows->keys + other_rows->keys, rows->lookups + other_rows->lookups,
                item_callbacks, grid_callbacks, updates};
        };
        Work before;
        std::uint64_t checksum{};
        const auto begin = [&] {
            require(!items->hovered(), "Every measured phase starts without collection hover");
            before = work(); checksum = 1;
        };
        const auto finish = [&](const char* phase, unsigned count, const Measurement& timing,
            unsigned source_transitions = 0, LRESULT paints = 0) {
            require(window.state() == WindowState::open && IsWindow(hwnd),
                "Every measured phase retains a live native owner");
            auto after = work();
            Work delta{after.item_reads - before.item_reads, after.item_keys - before.item_keys,
                after.item_lookups - before.item_lookups, after.grid_reads - before.grid_reads,
                after.grid_keys - before.grid_keys, after.grid_lookups - before.grid_lookups,
                after.item_callbacks - before.item_callbacks, after.grid_callbacks - before.grid_callbacks,
                after.updates - before.updates, checksum};
            require(delta.updates == count && delta.item_reads >= count && delta.grid_keys >= count && checksum,
                "Every iteration performs native synchronization and bounded visible source work");
            report(phase, count, timing, delta, items->offset(), grid->offset(), files->offset(),
                items->selection().focused().value_or(ItemKey{}), grid->selected().value_or(RowKey{}),
                files->model().selected_id().value_or(0), source_transitions, paints);
        };
        begin();
        auto timing = measure(32768, [&](unsigned index) {
            items->set_offset((index * 997u % 100000u) * 24.0);
            grid->set_offset((index * 577u % 20000u) * 24.0, 0);
            files->scroll_to(float(index * 179u % 9000u) * files->row_height());
            flush();
            checksum = mix(mix(mix(checksum, std::uint64_t(items->offset())),
                std::uint64_t(grid->offset())), std::uint64_t(files->offset()));
        });
        require(items->offset() == (32767u * 997u % 100000u) * 24.0 &&
            grid->offset() == (32767u * 577u % 20000u) * 24.0 &&
            files->offset() == float(32767u * 179u % 9000u) * files->row_height(),
            "Scroll phase reaches the exact three expected offsets");
        require(editor_observer.writes.empty(), "Scroll-only sync does not rewrite editor text, font or selection");
        require(SendMessageW(hwnd, metrics, 2, 0) == layouts, "Scroll-only sync does not arrange the window");
        require(!item_callbacks && !grid_callbacks, "Scrolling does not emit selection callbacks");
        finish("scroll-sync", 32768, timing);
        begin();
        timing = measure(8192, [&](unsigned index) {
            items->select(items->source()->key(index * 13u % 1000000u));
            grid->select(grid->source()->key(index * 17u % 100000u));
            files->select(index * 19u % 10000u);
            flush();
            checksum = mix(mix(mix(checksum, items->selection().focused().value_or(ItemKey{}).id),
                grid->selected().value_or(RowKey{}).id), files->model().selected_id().value_or(0));
        });
        const ItemKey expected_item{8191u * 13u % 1000000u + 1, 1};
        const RowKey expected_grid{8191u * 17u % 100000u + 1, 1};
        const auto expected_file = (*files->model().items())[files->model().visible_indices()[8191u * 19u % 10000u]].id;
        require(items->selection().focused() == expected_item && grid->selected() == expected_grid &&
            files->model().selected_id() == expected_file && item_callbacks == 8192 && grid_callbacks == 8192,
            "Selection phase delivers every callback and the exact three final stable identities");
        finish("selection-publication", 8192, timing);
        begin();
        unsigned transitions{};
        timing = measure(8192, [&](unsigned index) {
            transitions += items->source() != (index % 2 ? first : second);
            transitions += grid->source() != (index % 2 ? rows : other_rows);
            items->set_items(index % 2 ? first : second);
            grid->set_source(index % 2 ? rows : other_rows);
            flush();
            require(items->source() == (index % 2 ? first : second) &&
                grid->source() == (index % 2 ? rows : other_rows), "Source identity changes are actually accepted");
            checksum = mix(checksum, index % 2 ? 1 : 2);
        });
        require(transitions == 16384 && items->source() == first && grid->source() == rows &&
            items->selection().focused() == expected_item && grid->selected() == expected_grid &&
            item_callbacks == 8192 && grid_callbacks == 8192,
            "Source phase publishes both alternating identities without losing selection or adding callbacks");
        finish("source-publication", 8192, timing, transitions);
        begin();
        const auto painted_before = SendMessageW(hwnd, metrics, 0, 0);
        timing = measure(256, [&](unsigned index) {
            items->set_offset(index * 41.0);
            grid->set_offset(index * 37.0, 0);
            flush(); UpdateWindow(hwnd);
            checksum = mix(mix(checksum, std::uint64_t(items->offset())), std::uint64_t(grid->offset()));
        });
        const auto painted = SendMessageW(hwnd, metrics, 0, 0) - painted_before;
        require(painted == 256 && items->offset() == 255 * 41.0 && grid->offset() == 255 * 37.0,
            "Paint phase presents every requested frame at its expected final offsets");
        require(!items->hovered() && work().item_keys - before.item_keys == 19422,
            "Paint retains nonhovered collection and canonical source-key work");
        finish("paint-visible-rows", 256, timing, 0, painted);
        require(named(hwnd, L"Probe collection") == item_hwnd && named(hwnd, L"Probe grid") == grid_hwnd &&
            named(hwnd, L"Probe legacy list") == file_hwnd, "Native identities survive all operations");
        require(descendants(hwnd) == native_count && SendMessageW(hwnd, metrics, 14, 0) == peer_count &&
            SendMessageW(hwnd, metrics, 11, 0) == target_count, "No row-dependent native or renderer growth");
        require(SendMessageW(hwnd, metrics, 33, 0) == 0, "No animation timer for explicit scroll work");
        require(items->selection().storage_size() == 1 && grid->selection().storage_size() == 1,
            "Selections remain bounded and nonempty");
        editor_observer.detach();
        PROCESS_MEMORY_COUNTERS_EX memory{sizeof(memory)};
        DWORD handles{};
        require(GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),
            sizeof(memory)) != FALSE && GetProcessHandleCount(GetCurrentProcess(), &handles) != FALSE,
            "Read retained process resources");
        std::cout << "{\"phase\":\"retained\",\"items\":1000000,\"grid_rows\":100000,\"legacy_rows\":10000"
            << ",\"peers\":" << peer_count << ",\"child_hwnds\":" << native_count
            << ",\"targets\":" << target_count << ",\"selection_terms\":2,\"editor_scroll_writes\":0"
            << ",\"handles\":" << handles << ",\"gdi\":" << GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS)
            << ",\"user\":" << GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS)
            << ",\"private_bytes\":" << memory.PrivateUsage
            << ",\"item_reads\":" << first->reads + second->reads << "}\n";
        completed = true;
        items->on_selection({});
        grid->on_select({});
        window.close();
    });
    const auto result = Application::run(window);
    if (result) std::wcerr << window.error() << '\n';
    require(result == 0 && completed, "Real host probe completes every phase");
    require(Drawing::live_targets() == 0, "Root renderer retires after close");
}
}
int main(int argc, char**) {
    if (argc != 1) { std::cerr << "This probe accepts no selectors\n"; return 2; }
    try { std::cout << std::setprecision(17); native_failure_contract(); probe(); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
