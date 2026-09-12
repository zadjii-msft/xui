#include "../demo/processes.hpp"
#include "xui/application.hpp"
#include <windows.h>
#include <psapi.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

namespace {
int assertions{};
void require(bool value, const char* message) { ++assertions; if (!value) throw std::runtime_error(message); }
class Synthetic final : public xui::GridSource {
    std::size_t count_;
    bool reverse_;
public:
    mutable std::size_t strings{};
    Synthetic(std::size_t count, bool reverse = false) : count_(count), reverse_(reverse) {}
    std::size_t size() const override { return count_; }
    xui::RowKey key(std::size_t row) const override { return {reverse_ ? count_ - row : row + 1, 42}; }
    std::optional<std::size_t> find(xui::RowKey value) const override {
        if (value.version != 42 || !value.id || value.id > count_) return {};
        return reverse_ ? count_ - value.id : value.id - 1;
    }
    std::wstring text(std::size_t, std::size_t) const override { ++strings; return L"value"; }
};
void pure() {
    using namespace task_manager;
    require(cpu_delta(0, 10000000, 1, 4) == 25.0, "CPU is normalized by elapsed time and active processors");
    require(!cpu_delta(10, 9, 1, 4) && !cpu_delta(0, 10, 0, 4) && !cpu_delta(0, 10, 1, 0), "Invalid CPU counters are unavailable");
    require(!cpu_delta(0, UINT64_MAX, 1, 1), "Large invalid CPU drift is not hidden by clamping");
    require(!cpu_delta(0, 100, std::numeric_limits<double>::quiet_NaN(), 4), "Nonfinite clocks unavailable");
    require(system_cpu_delta(0, 0, 0, 50, 80, 20) == 50, "System kernel includes idle");
    require(!system_cpu_delta(0, 0, 0, 101, 80, 20), "Invalid idle delta rejected");
    require(!checked_sum(UINT64_MAX, 1), "Counter overflow rejected");
    require(!system_cpu_delta(0, 0, 0, 1, UINT64_MAX, 1), "System sum overflow rejected");
    require(rate_delta(10, 30, 2) == 10 && !rate_delta(30, 10, 2), "I/O rates and reset guards");
    auto data = std::make_shared<Snapshot>();
    Process a; a.name = L"Alpha.exe"; a.pid = 20; a.key = {20, 12}; a.cpu = 10; a.working_set = 200; a.identity_known = true;
    Process b; b.name = L"Beta.exe"; b.pid = 3; b.key = {3, 15}; b.cpu = 30; b.working_set = 10;
    Process c; c.name = L"Protected.exe"; c.pid = 4; c.key = {4, 16}; c.access_error = ERROR_ACCESS_DENIED;
    data->processes = {a, b, c};
    ProcessView names(data, L"ALPHA", 0, false), pid(data, L"20", 0, false), memory(data, L"", 3, false), cpu(data, L"", 2, true);
    require(names.size() == 1 && names.key(0) == a.key && pid.size() == 1, "Case-insensitive name and PID search");
    require(cpu.key(0) == b.key && cpu.key(2) == c.key && memory.key(0) == b.key && memory.key(2) == c.key, "Numeric sort and missing values last");
    require(!names.find({20, 13}), "PID reuse does not match old identity");
    require(cpu.text(2, 2) == L"\u2014" && cpu.text(2, 3) == L"\u2014", "Unavailable metrics are not zeros");
    require(can_end(a, 99) && !can_end(a, 20) && !can_end(c, 99), "Unsafe end-task targets disabled");
    xui::HistoryChart chart;
    for (int i = 0; i < 150; ++i) chart.append(double(i % 100));
    require(chart.size() == 60 && chart.at(0) == 90 && chart.at(59) == 49, "Fixed ring keeps newest 60 values in order");
    chart.append({}); require(!chart.at(59), "Unavailable chart data creates gap");
    chart.append(INFINITY); require(!chart.at(59), "Nonfinite chart values create gap");
    chart.append(101); require(!chart.at(59), "Out-of-scale value not clipped into a false reading");
    require(!chart.focusable(), "Chart is accessible text, not an inactive keyboard stop");
    xui::DataGrid grid;
    grid.set_columns({{L"Name", 240}, {L"Number", 120, true}});
    grid.arrange({0, 0, 300, 250});
    for (const std::size_t count : {100000u, 1000000u}) {
        auto source = std::make_shared<Synthetic>(count);
        auto reverse = std::make_shared<Synthetic>(count, true);
        xui::Element before;
        grid.set_source(source);
        require(grid.select({1234, 42}), "Synthetic row selected by stable identity");
        grid.set_offset(grid.maximum_offset(), grid.maximum_horizontal());
        auto [first, last] = grid.visible_rows();
        require(last == count && last - first <= 8, "Million-row viewport bounds are constant");
        require(source->strings == 0, "Source updates, selection and scrolling allocate no row text");
        for (std::size_t i = first; i < last; ++i) for (std::size_t column = 0; column < 2; ++column) source->text(i, column);
        require(source->strings <= 16, "Only visible cells need display strings");
        grid.set_source(reverse);
        require(grid.selected() == xui::RowKey{1234, 42}, "Sort preserves identity independently of display position");
        require(!grid.select({1234, 43}), "Grid rejects stale identities");
        xui::Element after;
        require(after.id() == before.id() + 1, "No retained visual allocated for any synthetic row");
        grid.edge(false); require(grid.selected() == reverse->key(0), "Home selects first view row");
        grid.edge(true); require(grid.selected() == reverse->key(count - 1), "End selects last view row");
    }
    grid.set_offset(INFINITY, NAN); require(grid.offset() == 0 && grid.horizontal_offset() == 0, "Nonfinite offsets cannot poison geometry");
    grid.resize_column(1, 100000); require(grid.columns()[1].width == 1000, "Column resize bounded");
    grid.focus_header(true); grid.step_header(999); require(grid.focused_column() == 1, "Keyboard headers clamp and reveal");
    const auto selected = grid.selected();
    require(grid.reveal({100, 42}) && grid.selected() == selected, "ScrollIntoView does not change row selection");
    bool rejected{};
    try { grid.set_source(std::make_shared<Synthetic>(std::numeric_limits<std::size_t>::max())); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected && grid.source()->size() == 1000000, "Overflowing row count rejected before changing source");
    auto pages = std::make_shared<xui::PageView>();
    auto first = std::make_shared<xui::Label>(L"first"), second = std::make_shared<xui::Label>(L"second");
    pages->add_page(first); pages->add_page(second); pages->arrange({0, 0, 500, 300});
    require(pages->child_at(1)->bounds().width == 0, "Inactive page has no visible host");
    pages->select(1); pages->arrange({0, 0, 500, 300});
    require(pages->child_at(0)->bounds().width == 0 && pages->child_at(1)->bounds().width == 500, "Page switch reuses retained hosts");
}
int fixture() {
    auto* memory = static_cast<unsigned char*>(VirtualAlloc(nullptr, 32 * 1024 * 1024, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!memory) return 1;
    for (std::size_t i = 0; i < 32 * 1024 * 1024; i += 4096) memory[i] = 1;
    const auto end = task_manager::Clock::now() + std::chrono::seconds(90);
    std::uint64_t value = 1;
    while (task_manager::Clock::now() < end) {
        for (int i = 0; i < 10000; ++i) value = value * 1664525 + 1013904223;
        memory[value % (32 * 1024 * 1024)] = static_cast<unsigned char>(value);
    }
    VirtualFree(memory, 0, MEM_RELEASE); return 0;
}
void actual(const wchar_t* executable) {
    using namespace task_manager;
    PROCESS_INFORMATION info{}; STARTUPINFOW startup{sizeof(startup)};
    std::wstring command = L"\"" + std::wstring(executable) + L"\" --fixture";
    require(CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &info), "Launch only owned benign fixture");
    struct Cleanup { PROCESS_INFORMATION& p; ~Cleanup() {
        if (WaitForSingleObject(p.hProcess, 0) == WAIT_TIMEOUT) TerminateProcess(p.hProcess, 1);
        WaitForSingleObject(p.hProcess, 3000); CloseHandle(p.hThread); CloseHandle(p.hProcess);
    } } cleanup{info};
    Sampler sampler;
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    auto first = sampler.sample({}, true);
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    auto second = sampler.sample({}, false);
    const auto find = [&](const auto& snapshot) -> const Process& {
        const auto it = std::find_if(snapshot->processes.begin(), snapshot->processes.end(), [&](const auto& p) { return p.pid == info.dwProcessId; });
        require(it != snapshot->processes.end(), "Fixture exists in real Toolhelp snapshot"); return *it;
    };
    const auto& p1 = find(first); const auto& p2 = find(second);
    require(p1.identity_known && p1.key == p2.key && !p1.cpu, "First sample unavailable; subsequent identity stable");
    require(p2.working_set && *p2.working_set >= 30 * 1024 * 1024, "Allocated fixture pages appear in total working set");
    require(p2.private_commit && *p2.private_commit >= 32 * 1024 * 1024, "Private commit reflects fixture allocation");
    require(p2.cpu && *p2.cpu > 0 && *p2.cpu <= 110.0 / second->logical_processors, "Busy single-thread CPU scales by logical processors");
    require(second->memory_available && second->commit_available && second->ram_available <= second->ram_total, "Real RAM and commit metrics available");
    sampler.select(p2.key);
    auto details = sampler.sample({}, false);
    require(details->details_key == p2.key && !details->selected_path.empty(), "Selected metadata queried on background sampler boundary");
    auto reset = sampler.sample({}, true);
    require(!find(reset).cpu && !reset->cpu, "Resume resets system and per-process delta baselines");
    require(end_process({p2.key.id, p2.key.version + 1}, true).find(L"identity changed") != std::wstring::npos, "Creation-time mismatch rejects fixture termination");
    require(WaitForSingleObject(info.hProcess, 0) == WAIT_TIMEOUT, "Identity mismatch leaves fixture alive");
    require(end_process(p2.key, false) == L"End task cancelled.", "Confirmation cancellation seam");
    require(WaitForSingleObject(info.hProcess, 0) == WAIT_TIMEOUT, "Cancel leaves fixture alive");
    require(end_process(p2.key, true).starts_with(L"Termination requested"), "Confirmed owned fixture termination succeeds");
    require(WaitForSingleObject(info.hProcess, 3000) == WAIT_OBJECT_0, "Owned fixture exits");
    auto exited = sampler.sample({}, false);
    require(std::none_of(exited->processes.begin(), exited->processes.end(), [&](const auto& p) { return p.key == p2.key; }), "Exited identity absent in next snapshot");
    std::cout << "real_processes=" << second->processes.size() << " logical_processors=" << second->logical_processors
        << " fixture_cpu_percent=" << *p2.cpu << " fixture_working_set=" << *p2.working_set
        << " sampler_ms=" << second->duration_ms << '\n';
}
}
int wmain(int argc, wchar_t** argv) {
    if (argc > 1 && std::wstring_view(argv[1]) == L"--fixture") return fixture();
    if (argc > 1 && std::wstring_view(argv[1]) == L"--sample-bench") {
        task_manager::Sampler sampler;
        for (int i = 0; i < 10; ++i) {
            const auto value = sampler.sample({}, i == 0);
            PROCESS_MEMORY_COUNTERS_EX memory{};
            GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory));
            DWORD handles{}; GetProcessHandleCount(GetCurrentProcess(), &handles);
            std::cout << "sample=" << i << " processes=" << value->processes.size() << " duration_ms=" << value->duration_ms
                << " private_commit=" << memory.PrivateUsage << " working_set=" << memory.WorkingSetSize << " handles=" << handles << '\n';
            Sleep(100);
        }
        return 0;
    }
    try { pure(); actual(argv[0]); std::cout << assertions << " Task Manager assertions passed\n"; return 0; }
    catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
