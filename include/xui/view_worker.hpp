#pragma once

#include "xui/file_list_model.hpp"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace xui {
// One worker, one pending request, one result. Refresh cancels source work.
// Query changes cancel only filtering, so typing cannot restart directory I/O.
class ViewWorker {
public:
    using Loader = std::function<SourceResult(const CancelCheck&)>;
    // The notification runs under the mailbox lock. It must not call back into this worker.
    ViewWorker(Loader loader, std::function<void()> ready);
    ~ViewWorker();
    void start();
    std::uint64_t request(std::wstring query, bool refresh = false);
    std::optional<ViewResult> take_result();
    void retire(std::shared_ptr<const FilteredView> view);
    bool busy() const { return busy_.load(); }
    void request_stop();
    void join();
    std::jthread::native_handle_type native_handle() { return thread_.native_handle(); }
    bool joinable() const { return thread_.joinable(); }
private:
    void run(std::stop_token stop);
    Loader loader_;
    std::function<void()> ready_;
    std::mutex mutex_;
    std::condition_variable_any changed_;
    std::atomic<std::uint64_t> generation_{}, source_generation_{};
    std::atomic<bool> busy_{};
    std::wstring query_;
    std::optional<ViewResult> result_;
    std::vector<std::shared_ptr<const FilteredView>> retired_;
    bool stopped_{};
    std::jthread thread_;
};

} // namespace xui
