#pragma once
#include "xui/data_grid.hpp"
#include <chrono>
#include <mutex>
#include <stop_token>

namespace task_manager {
using Clock = std::chrono::steady_clock;
struct Process {
    xui::RowKey key;
    std::wstring name;
    std::uint32_t pid{}, threads{};
    std::optional<std::uint64_t> ticks, working_set, private_commit;
    std::optional<double> io_bytes, cpu, io_rate;
    Clock::time_point time;
    unsigned access_error{}, memory_error{}, io_error{};
    bool identity_known{};
};
struct Snapshot {
    std::vector<Process> processes;
    std::optional<double> cpu;
    std::uint64_t ram_total{}, ram_available{}, commit_used{}, commit_limit{};
    std::uint64_t threads{};
    unsigned logical_processors{}, processor_groups{};
    bool memory_available{}, commit_available{};
    std::wstring error, action_result, selected_path, selected_architecture;
    std::optional<xui::RowKey> details_key;
    double duration_ms{};
    std::uint64_t action_sequence{};
    Clock::time_point time;
};
std::optional<double> cpu_delta(std::uint64_t before, std::uint64_t after, double elapsed_seconds, unsigned processors);
std::optional<double> system_cpu_delta(std::uint64_t idle_before, std::uint64_t kernel_before, std::uint64_t user_before,
    std::uint64_t idle_after, std::uint64_t kernel_after, std::uint64_t user_after);
std::optional<double> rate_delta(double before, double after, double elapsed_seconds);
std::optional<std::uint64_t> checked_sum(std::uint64_t first, std::uint64_t second);
std::wstring percent(std::optional<double> value);
std::wstring mib(std::optional<std::uint64_t> value);
std::wstring gib(std::uint64_t value);
std::wstring windows_error(unsigned code);
bool can_end(const Process& process, unsigned current_pid);
std::wstring end_process(xui::RowKey expected, bool confirmed);

class ProcessView final : public xui::GridSource {
public:
    ProcessView(std::shared_ptr<const Snapshot> snapshot, std::wstring query, std::size_t sort, bool descending);
    std::size_t size() const override { return indices_.size(); }
    xui::RowKey key(std::size_t row) const override;
    std::optional<std::size_t> find(xui::RowKey key) const override;
    std::wstring text(std::size_t row, std::size_t column) const override;
    const Process& process(std::size_t row) const { return snapshot_->processes.at(indices_.at(row)); }
private:
    std::shared_ptr<const Snapshot> snapshot_;
    std::vector<std::size_t> indices_;
};
class Sampler final {
public:
    std::shared_ptr<const Snapshot> sample(std::stop_token stop, bool reset);
    void select(std::optional<xui::RowKey> key);
    void request_end(xui::RowKey key);
private:
    std::shared_ptr<const Snapshot> previous_;
    std::optional<std::array<std::uint64_t, 3>> system_before_;
    std::uint64_t generation_{};
    std::wstring action_result_;
    std::uint64_t action_sequence_{};
    std::mutex mutex_;
    std::optional<xui::RowKey> selected_, terminate_;
};
}
