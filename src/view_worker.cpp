#include "xui/view_worker.hpp"
#include <stdexcept>
#include <utility>

namespace xui {
ViewWorker::ViewWorker(Loader loader, std::function<void()> ready)
    : loader_(std::move(loader)), ready_(std::move(ready)) {}
ViewWorker::~ViewWorker() { request_stop(); join(); }
void ViewWorker::start() {
    std::lock_guard lock(mutex_);
    if (thread_.joinable() || stopped_) throw std::logic_error("View worker can start only once");
    thread_ = std::jthread([this](std::stop_token stop) { run(stop); });
}

std::uint64_t ViewWorker::request(std::wstring query, bool refresh) {
    std::lock_guard lock(mutex_);
    if (stopped_) throw std::logic_error("View worker is stopped");
    query_ = std::move(query);
    if (refresh || source_generation_ == 0) ++source_generation_;
    const auto generation = ++generation_;
    busy_ = true;
    changed_.notify_one();
    return generation;
}

std::optional<ViewResult> ViewWorker::take_result() {
    std::lock_guard lock(mutex_);
    if (result_ && result_->generation != generation_) return {};
    return std::exchange(result_, std::nullopt);
}

void ViewWorker::retire(std::shared_ptr<const FilteredView> view) {
    std::lock_guard lock(mutex_);
    retired_.push_back(std::move(view));
    changed_.notify_one();
}

void ViewWorker::request_stop() {
    std::lock_guard lock(mutex_);
    stopped_ = true;
    thread_.request_stop();
    changed_.notify_all();
}
void ViewWorker::join() {
    if (thread_.joinable()) thread_.join();
    busy_ = false;
}

void ViewWorker::run(std::stop_token stop) {
    std::uint64_t processed{}, loaded{};
    SourceResult source;
    for (;;) {
        if (stop.stop_requested()) return;
        std::uint64_t generation, source_generation;
        std::wstring query;
        std::vector<std::shared_ptr<const FilteredView>> garbage;
        {
            std::unique_lock lock(mutex_);
            if (!changed_.wait(lock, stop, [&] { return generation_ != processed || !retired_.empty(); }))
                return;
            garbage.swap(retired_);
            generation = generation_;
            source_generation = source_generation_;
            query = query_;
            busy_ = generation != processed;
        }
        garbage.clear(); // Destruction of replaced collections stays off the UI thread.
        if (generation == processed) continue;
        const CancelCheck cancelled = [&] { return stop.stop_requested() || generation_ != generation; };
        ViewResult result{generation, {}, {}};
        try {
            if (loaded != source_generation) {
                const CancelCheck source_cancelled = [&] {
                    return stop.stop_requested() || source_generation_ != source_generation;
                };
                auto replacement = loader_(source_cancelled);
                if (source_cancelled()) continue;
                if (!replacement.source && replacement.error.empty())
                    throw std::runtime_error("Source loader returned no snapshot");
                source = std::move(replacement);
                loaded = source_generation;
            }
            if (cancelled()) continue;
            if (source.source) result.view = FilteredView::build(source.source, std::move(query), cancelled);
            result.error = source.error;
        } catch (const std::exception& error) {
            const std::string text(error.what());
            result.error = L"View update failed: " + std::wstring(text.begin(), text.end());
        }
        std::optional<ViewResult> discarded;
        {
            std::lock_guard lock(mutex_);
            processed = generation;
            if (cancelled()) continue;
            busy_ = false;
            discarded = std::move(result_);
            result_ = std::move(result);
            if (ready_) ready_();
        }
        // An unread result can own the last reference to a large old source.
        // Release it outside the request mutex, so typing cannot block on destruction.
    }
}
}
