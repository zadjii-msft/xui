#include "async.hpp"
#include "list_peer.hpp"
#include <deque>

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
}
