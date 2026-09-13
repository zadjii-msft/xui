#include "processes.hpp"
#include <windows.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <algorithm>
#include <cmath>
#include <cwctype>
#include <limits>
#include <unordered_map>
#include <utility>

namespace task_manager {
namespace {
struct Handle {
    HANDLE value{};
    explicit Handle(HANDLE h) : value(h) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    explicit operator bool() const { return value && value != INVALID_HANDLE_VALUE; }
};
std::uint64_t ticks(FILETIME value) { return (std::uint64_t(value.dwHighDateTime) << 32) | value.dwLowDateTime; }
std::wstring decimal(double value, const wchar_t* suffix) {
    wchar_t buffer[96]{};
    swprintf_s(buffer, L"%.1f%s", value, suffix);
    return buffer;
}
std::wstring lower(std::wstring value) {
    for (auto& c : value) c = static_cast<wchar_t>(std::towlower(c));
    return value;
}
template<class T> int compare(const T& a, const T& b) { return a < b ? -1 : b < a ? 1 : 0; }
template<class T> int metric(const std::optional<T>& a, const std::optional<T>& b, bool descending) {
    // Missing values stay last in both directions.
    if (bool(a) != bool(b)) return a ? -1 : 1;
    if (!a) return 0;
    return compare(*a, *b) * (descending ? -1 : 1);
}
}
std::optional<std::uint64_t> checked_sum(std::uint64_t a, std::uint64_t b) {
    if (b > std::numeric_limits<std::uint64_t>::max() - a) return {};
    return a + b;
}
std::optional<double> cpu_delta(std::uint64_t before, std::uint64_t after, double seconds, unsigned processors) {
    if (after < before || !std::isfinite(seconds) || seconds <= 0 || !processors) return {};
    const double value = (after - before) / (seconds * 10000000.0 * processors) * 100;
    if (!std::isfinite(value) || value > 100.01) return {};
    return std::min(100.0, value); // Only sub-0.01% counter/clock drift is clamped.
}
std::optional<double> system_cpu_delta(std::uint64_t ib, std::uint64_t kb, std::uint64_t ub,
    std::uint64_t ia, std::uint64_t ka, std::uint64_t ua) {
    if (ia < ib || ka < kb || ua < ub) return {};
    const auto total = checked_sum(ka - kb, ua - ub);
    if (!total || !*total || ia - ib > *total) return {};
    // GetSystemTimes includes idle time in kernel time.
    return 100.0 * (*total - (ia - ib)) / *total;
}
std::optional<double> rate_delta(double before, double after, double seconds) {
    if (!std::isfinite(before) || !std::isfinite(after) || after < before || before < 0 ||
        !std::isfinite(seconds) || seconds <= 0) return {};
    const auto value = (after - before) / seconds;
    return std::isfinite(value) ? std::optional{value} : std::nullopt;
}
std::wstring percent(std::optional<double> value) { return value ? decimal(*value, L"%") : L"\u2014"; }
std::wstring mib(std::optional<std::uint64_t> value) { return value ? decimal(*value / 1048576.0, L" MiB") : L"\u2014"; }
std::wstring gib(std::uint64_t value) { return decimal(value / 1073741824.0, L" GiB"); }
std::wstring windows_error(unsigned code) {
    wchar_t* buffer{};
    const auto size = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::wstring text = size ? std::wstring(buffer, size) : L"Windows error";
    if (buffer) LocalFree(buffer);
    while (!text.empty() && (text.back() == L'\r' || text.back() == L'\n' || text.back() == L' ')) text.pop_back();
    return text + L" (" + std::to_wstring(code) + L")";
}
bool can_end(const Process& p, unsigned current) { return p.identity_known && p.pid != 0 && p.pid != 4 && p.pid != current; }
std::wstring end_process(xui::RowKey expected, bool confirmed) {
    if (!confirmed) return L"End task cancelled.";
    if (expected.id == 0 || expected.id == 4 || expected.id == GetCurrentProcessId() ||
        expected.id > MAXDWORD || !expected.version) return L"End task denied for this process.";
    Handle process(OpenProcess(PROCESS_TERMINATE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(expected.id)));
    if (!process) return L"Cannot end task: " + windows_error(GetLastError());
    FILETIME creation{}, exit{}, kernel{}, user{};
    if (!GetProcessTimes(process.value, &creation, &exit, &kernel, &user)) return L"Cannot verify process identity: " + windows_error(GetLastError());
    if (ticks(creation) != expected.version) return L"End task rejected: the process identity changed.";
    BOOL critical{};
    if (!IsProcessCritical(process.value, &critical)) return L"Cannot verify process safety: " + windows_error(GetLastError());
    if (critical) return L"End task denied: this is a critical process.";
    // Keep this same handle from the creation-time check through termination.
    if (!TerminateProcess(process.value, 1)) return L"Cannot end task: " + windows_error(GetLastError());
    return L"Termination requested for PID " + std::to_wstring(expected.id) + L".";
}
ProcessView::ProcessView(std::shared_ptr<const Snapshot> snapshot, std::wstring query, std::size_t sort, bool descending) :
    snapshot_(std::move(snapshot)) {
    if (!snapshot_) return;
    query = lower(std::move(query));
    const bool exact_pid = !query.empty() && std::all_of(query.begin(), query.end(),
        [](wchar_t value) { return value >= L'0' && value <= L'9'; });
    indices_.reserve(snapshot_->processes.size());
    for (std::size_t i = 0; i < snapshot_->processes.size(); ++i) {
        const auto& p = snapshot_->processes[i];
        if (query.empty() || (exact_pid ? std::to_wstring(p.pid) == query :
            lower(p.name).find(query) != std::wstring::npos)) indices_.push_back(i);
    }
    std::sort(indices_.begin(), indices_.end(), [&](auto a, auto b) {
        const auto& left = snapshot_->processes[a]; const auto& right = snapshot_->processes[b];
        int order{};
        switch (sort) {
        case 1: order = compare(left.pid, right.pid) * (descending ? -1 : 1); break;
        case 2: order = metric(left.cpu, right.cpu, descending); break;
        case 3: order = metric(left.working_set, right.working_set, descending); break;
        case 4: order = compare(left.threads, right.threads) * (descending ? -1 : 1); break;
        case 5: order = metric(left.io_rate, right.io_rate, descending); break;
        default: order = CompareStringOrdinal(left.name.c_str(), -1, right.name.c_str(), -1, TRUE) - CSTR_EQUAL;
            if (descending) order = -order;
        }
        return order ? order < 0 : left.key < right.key;
    });
}
xui::RowKey ProcessView::key(std::size_t row) const { return process(row).key; }
std::optional<std::size_t> ProcessView::find(xui::RowKey key) const {
    for (std::size_t i = 0; i < indices_.size(); ++i) if (process(i).key == key) return i;
    return {};
}
std::wstring ProcessView::text(std::size_t row, std::size_t column) const {
    const auto& p = process(row);
    switch (column) {
    case 0: return p.name;
    case 1: return std::to_wstring(p.pid);
    case 2: return percent(p.cpu);
    case 3: return mib(p.working_set);
    case 4: return std::to_wstring(p.threads);
    case 5: return p.io_rate ? decimal(*p.io_rate / 1024, L" KiB/s") : L"\u2014";
    default: return p.access_error ? L"Limited access (" + std::to_wstring(p.access_error) + L")" :
        p.memory_error || p.io_error ? L"Some counters unavailable" : L"Available";
    }
}
void Sampler::select(std::optional<xui::RowKey> key) { std::lock_guard lock(mutex_); selected_ = key; }
void Sampler::request_end(xui::RowKey key) { std::lock_guard lock(mutex_); terminate_ = key; }
std::shared_ptr<const Snapshot> Sampler::sample(std::stop_token stop, bool reset) {
    const auto start = Clock::now();
    if (reset) { previous_.reset(); system_before_.reset(); }
    auto result = std::make_shared<Snapshot>();
    result->time = start;
    result->logical_processors = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    result->processor_groups = GetActiveProcessorGroupCount();
    std::optional<xui::RowKey> selected, terminate;
    { std::lock_guard lock(mutex_); selected = selected_; terminate = std::exchange(terminate_, {}); }
    if (terminate && !stop.stop_requested()) { action_result_ = end_process(*terminate, true); ++action_sequence_; }
    result->action_result = action_result_; result->action_sequence = action_sequence_;
    MEMORYSTATUSEX memory{sizeof(memory)};
    if (GlobalMemoryStatusEx(&memory)) {
        result->memory_available = true; result->ram_total = memory.ullTotalPhys; result->ram_available = memory.ullAvailPhys;
    } else result->error = L"RAM counters: " + windows_error(GetLastError());
    PERFORMANCE_INFORMATION info{sizeof(info)};
    if (GetPerformanceInfo(&info, sizeof(info)) && info.PageSize &&
        info.CommitLimit <= std::numeric_limits<std::uint64_t>::max() / info.PageSize &&
        info.CommitTotal <= std::numeric_limits<std::uint64_t>::max() / info.PageSize) {
        result->commit_available = true; result->commit_used = info.CommitTotal * info.PageSize; result->commit_limit = info.CommitLimit * info.PageSize;
    } else result->error += L" Commit counters unavailable.";
    FILETIME idle{}, kernel{}, user{};
    if (GetSystemTimes(&idle, &kernel, &user)) {
        const std::array<std::uint64_t, 3> now{ticks(idle), ticks(kernel), ticks(user)};
        if (system_before_) result->cpu = system_cpu_delta((*system_before_)[0], (*system_before_)[1], (*system_before_)[2], now[0], now[1], now[2]);
        system_before_ = now;
    } else { result->error += L" CPU counters: " + windows_error(GetLastError()); system_before_.reset(); }
    Handle enumeration(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (!enumeration) throw std::runtime_error("Process enumeration failed (Windows error " + std::to_string(GetLastError()) + ")");
    PROCESSENTRY32W entry{sizeof(entry)};
    if (!Process32FirstW(enumeration.value, &entry))
        throw std::runtime_error("Process enumeration failed (Windows error " + std::to_string(GetLastError()) + ")");
    std::unordered_map<std::uint32_t, const Process*> before;
    if (previous_) for (const auto& p : previous_->processes) before.emplace(p.pid, &p);
    result->processes.reserve(previous_ ? previous_->processes.size() + 16 : 512);
    ++generation_;
    do {
        if (stop.stop_requested()) return {};
        Process p;
        p.pid = entry.th32ProcessID; p.threads = entry.cntThreads; p.name = entry.szExeFile;
        p.key = {p.pid, (std::uint64_t{1} << 63) | generation_};
        Handle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, p.pid));
        if (process) {
            FILETIME creation{}, exit{}, k{}, u{};
            if (GetProcessTimes(process.value, &creation, &exit, &k, &u)) {
                p.key.version = ticks(creation); p.identity_known = true;
                p.ticks = checked_sum(ticks(k), ticks(u)); p.time = Clock::now();
            } else p.access_error = GetLastError();
            PROCESS_MEMORY_COUNTERS_EX counters{};
            if (GetProcessMemoryInfo(process.value, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof(counters))) {
                p.working_set = counters.WorkingSetSize; p.private_commit = counters.PrivateUsage;
            } else p.memory_error = GetLastError();
            IO_COUNTERS io{};
            if (GetProcessIoCounters(process.value, &io)) p.io_bytes = double(io.ReadTransferCount) + double(io.WriteTransferCount) + double(io.OtherTransferCount);
            else p.io_error = GetLastError();
            if (selected == p.key && p.identity_known) {
                result->details_key = p.key;
                wchar_t path[32768]; DWORD size = static_cast<DWORD>(std::size(path));
                if (QueryFullProcessImageNameW(process.value, 0, path, &size)) result->selected_path.assign(path, size);
                else result->selected_path = L"Path unavailable: " + windows_error(GetLastError());
                USHORT process_machine{}, native_machine{};
                if (IsWow64Process2(process.value, &process_machine, &native_machine)) {
                    const auto machine = process_machine ? process_machine : native_machine;
                    result->selected_architecture = machine == IMAGE_FILE_MACHINE_ARM64 ? L"ARM64" :
                        machine == IMAGE_FILE_MACHINE_AMD64 ? L"x64" : machine == IMAGE_FILE_MACHINE_I386 ? L"x86" : L"Other architecture";
                } else result->selected_architecture = L"Architecture unavailable";
            }
        } else p.access_error = GetLastError();
        const auto old = before.find(p.pid);
        if (old != before.end() && old->second->identity_known && p.identity_known && old->second->key == p.key) {
            const auto& last = *old->second;
            const double seconds = std::chrono::duration<double>(p.time - last.time).count();
            if (last.ticks && p.ticks) p.cpu = cpu_delta(*last.ticks, *p.ticks, seconds, result->logical_processors);
            if (last.io_bytes && p.io_bytes && p.ticks && last.ticks) p.io_rate = rate_delta(*last.io_bytes, *p.io_bytes, seconds);
        }
        result->threads += p.threads;
        result->processes.push_back(std::move(p));
    } while (Process32NextW(enumeration.value, &entry));
    const auto enumeration_error = GetLastError();
    if (enumeration_error != ERROR_NO_MORE_FILES) result->error += L"Process enumeration ended early: " + windows_error(enumeration_error);
    result->duration_ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    previous_ = result;
    return result;
}
}
