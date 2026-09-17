#include "async.hpp"
#include "list_peer.hpp"
#include <deque>
#include <chrono>

namespace xui {
namespace {
class DisposalQueue {
    std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<std::shared_ptr<const void>> queue_;
    bool stopping_{};
    std::thread thread_{[this] {
        for (;;) {
            std::shared_ptr<const void> value;
            {
                std::unique_lock lock(mutex_);
                changed_.wait(lock, [&] { return stopping_ || !queue_.empty(); });
                if (queue_.empty()) return;
                value = std::move(queue_.front());
                queue_.pop_front();
            }
            value.reset();
        }
    }};
public:
    ~DisposalQueue() {
        { std::lock_guard lock(mutex_); stopping_ = true; }
        changed_.notify_one();
        thread_.join();
    }
    void add(std::shared_ptr<const void> value) {
        { std::lock_guard lock(mutex_); queue_.push_back(std::move(value)); }
        changed_.notify_one();
    }
};
}
void dispose_later(std::shared_ptr<const void> value) {
    if (!value) return;
    static DisposalQueue queue;
    queue.add(std::move(value));
}
ViewTask::ViewTask(std::shared_ptr<Impl> impl) : impl_(std::move(impl)) {}
ViewTask::~ViewTask() { cancel(); }
std::uint64_t ViewTask::request(std::wstring query, bool refresh) {
    if (!impl_->worker || impl_->cancelled) return 0;
    return impl_->generation = impl_->worker->request(std::move(query), refresh);
}
bool ViewTask::busy() const { return impl_->worker && impl_->worker->busy(); }
std::uint64_t ViewTask::generation() const { return impl_->generation; }
std::uint64_t ViewTask::applied_generation() const { return impl_->applied; }
void ViewTask::cancel() { impl_->cancel(); }
void ViewTask::Impl::cancel() {
    if (cancelled) return;
    cancelled = true;
    receive = {};
    if (worker) {
        worker->request_stop();
        if (worker->joinable()) CancelSynchronousIo(worker->native_handle());
        dispose_later(std::move(worker));
    }
}
void ViewTask::Impl::deliver() {
    if (cancelled || !worker) return;
    auto result = worker->take_result();
    if (!result) return;
    if (result->generation != generation) {
        dispose_later(std::move(result->view));
        return;
    }
    applied = result->generation;
    struct Retire {
        std::shared_ptr<const FilteredView> view;
        ~Retire() { dispose_later(std::move(view)); }
    } retire{result->view};
    auto callback = receive;
    if (callback) callback(std::move(*result));
    result->view.reset();
}

struct SampleTask::Impl::Worker {
    std::mutex mutex;
    std::condition_variable_any changed;
    Payload result;
    std::wstring error;
    std::uint64_t generation{}, result_generation{};
    unsigned interval{1000};
    bool paused{}, suspended{}, requested{true}, reset{true}, ready{};
    Loader loader;
    std::shared_ptr<TaskWake> wake;
    std::jthread thread;
    void start() {
        thread = std::jthread([this](std::stop_token stop) {
            std::unique_lock lock(mutex);
            while (!stop.stop_requested()) {
                changed.wait(lock, stop, [&] { return requested && !suspended; });
                if (stop.stop_requested()) break;
                requested = false;
                const auto version = generation;
                const bool baseline = std::exchange(reset, false);
                lock.unlock();
                Payload value;
                std::wstring failure;
                try { value = loader(stop, baseline); }
                catch (const std::exception& e) { failure = exception_message(e); }
                catch (...) { failure = L"The background sample failed."; }
                lock.lock();
                if (version == generation && !stop.stop_requested()) {
                    result = std::move(value);
                    // Keep an undelivered failure even when a newer successful sample replaces the payload.
                    if (!failure.empty() || !ready) error = std::move(failure);
                    result_generation = version; ready = true;
                    wake->signal();
                }
                if (requested) continue;
                if (!paused && !suspended) {
                    changed.wait_for(lock, stop, std::chrono::milliseconds(interval),
                        [&] { return requested || paused || suspended; });
                    if (!paused && !suspended) requested = true;
                }
            }
        });
    }
    void stop() {
        thread.request_stop(); changed.notify_all();
        if (thread.joinable()) CancelSynchronousIo(thread.native_handle());
    }
};
SampleTask::SampleTask(std::shared_ptr<Impl> value) : impl_(std::move(value)) {}
SampleTask::~SampleTask() { cancel(); }
void SampleTask::Impl::start(Loader loader, std::shared_ptr<TaskWake> wake, unsigned interval) {
    worker = std::make_shared<Worker>();
    worker->loader = std::move(loader); worker->wake = std::move(wake);
    worker->interval = std::clamp(interval, 100u, 60000u); worker->start();
}
void SampleTask::pause(bool value) {
    if (impl_->cancelled) return;
    auto& w = *impl_->worker;
    { std::lock_guard lock(w.mutex);
      if (w.paused == value) return;
      w.paused = value; ++w.generation; w.reset = true; w.ready = false; w.result.reset();
      w.requested = !value; }
    w.changed.notify_all();
}
void SampleTask::set_interval(unsigned value) {
    if (impl_->cancelled) return;
    auto& w = *impl_->worker;
    { std::lock_guard lock(w.mutex); w.interval = std::clamp(value, 100u, 60000u); }
}
void SampleTask::refresh() {
    if (impl_->cancelled) return;
    auto& w = *impl_->worker;
    { std::lock_guard lock(w.mutex); ++w.generation; w.requested = true; }
    w.changed.notify_all();
}
void SampleTask::cancel() { impl_->cancel(); }
void SampleTask::Impl::cancel() {
    if (cancelled) return;
    cancelled = true; receive = {};
    if (worker) { worker->stop(); dispose_later(std::move(worker)); }
}
void SampleTask::Impl::suspend(bool value) {
    if (cancelled) return;
    auto& w = *worker;
    { std::lock_guard lock(w.mutex);
      if (w.suspended == value) return;
      w.suspended = value; ++w.generation; w.reset = true; w.ready = false; w.result.reset();
      w.requested = !value && !w.paused; }
    w.changed.notify_all();
}
void SampleTask::Impl::deliver() {
    if (cancelled) return;
    Payload value;
    std::wstring failure;
    { std::lock_guard lock(worker->mutex);
      if (!worker->ready || worker->result_generation != worker->generation) return;
      worker->ready = false; value = std::move(worker->result); failure = std::move(worker->error); }
    auto callback = receive;
    if (callback) { ++delivered; callback(std::move(value), failure); }
}
}
