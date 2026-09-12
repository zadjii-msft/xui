#include "suggestion_worker.hpp"
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace xui::detail {
void SuggestionDelivery::cancel() {
    std::lock_guard lock(mutex);
    ++generation;
    result.reset();
}
void SuggestionDelivery::revoke() {
    std::lock_guard lock(mutex);
    ++generation;
    window = nullptr;
    result.reset();
}
std::optional<SuggestionResult> SuggestionDelivery::take() {
    std::lock_guard lock(mutex);
    return std::exchange(result, {});
}
std::shared_ptr<SuggestionWorker> SuggestionWorker::shared() {
    static std::mutex mutex;
    static std::weak_ptr<SuggestionWorker> worker;
    std::lock_guard lock(mutex);
    auto value = worker.lock();
    if (!value) { value = std::make_shared<SuggestionWorker>(); worker = value; }
    return value;
}
void SuggestionWorker::request(std::shared_ptr<SuggestionSource> source, SuggestionRequest request,
    std::shared_ptr<SuggestionDelivery> delivery) {
    std::lock_guard lock(mutex_);
    if (pending_) pending_->delivery->cancel();
    delivery->cancel();
    pending_ = Work{std::move(source), std::move(request), delivery, delivery->generation.load()};
    if (running_) return;
    auto context = std::make_unique<std::shared_ptr<SuggestionWorker>>(shared_from_this());
    if (!TrySubmitThreadpoolCallback(run, context.get(), nullptr)) {
        pending_.reset();
        throw std::runtime_error("Cannot start suggestion worker");
    }
    running_ = true;
    context.release();
}
void CALLBACK SuggestionWorker::run(PTP_CALLBACK_INSTANCE instance, void* context) noexcept {
    std::unique_ptr<std::shared_ptr<SuggestionWorker>> owner(
        static_cast<std::shared_ptr<SuggestionWorker>*>(context));
    auto self = *owner;
    CallbackMayRunLong(instance);
    for (;;) {
        std::optional<Work> work;
        {
            std::lock_guard lock(self->mutex_);
            work = std::exchange(self->pending_, {});
            if (!work) { self->running_ = false; return; }
        }
        const auto cancelled = [&] { return work->delivery->generation.load() != work->generation; };
        if (cancelled()) continue;
        try {
            auto result = work->source->suggest(work->request, cancelled);
            if (cancelled()) continue;
            if (result.items.size() > SuggestionRequest::maximum_results)
                result.items.resize(SuggestionRequest::maximum_results);
            std::erase_if(result.items, [](const auto& item) {
                return item.empty() || item.size() > SuggestionRequest::maximum_text ||
                    item.find(L'\0') != item.npos;
            });
            if (result.status.size() > 256) result.status.resize(256);
            std::lock_guard lock(work->delivery->mutex);
            if (cancelled()) continue;
            work->delivery->result = std::move(result);
            if (work->delivery->window)
                PostMessageW(work->delivery->window, suggestions_ready, 0, 0);
        } catch (...) {
            try {
                std::lock_guard lock(work->delivery->mutex);
                if (!cancelled()) {
                    work->delivery->result = SuggestionResult{{}, L"Suggestions are unavailable."};
                    if (work->delivery->window)
                        PostMessageW(work->delivery->window, suggestions_ready, 0, 0);
                }
            } catch (...) {}
        }
    }
}
}
