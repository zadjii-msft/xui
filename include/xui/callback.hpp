#pragma once

#include <algorithm>
#include <functional>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace xui::detail {

class CallbackRegistration {
public:
    bool active{true};
    bool scoped{true};
    virtual ~CallbackRegistration() = default;
    virtual void retire() noexcept = 0;
};

class CallbackScope {
    std::vector<std::weak_ptr<CallbackRegistration>> registrations_;
    bool active_{true};
public:
    CallbackScope() = default;
    CallbackScope(const CallbackScope&) = delete;
    CallbackScope& operator=(const CallbackScope&) = delete;
    ~CallbackScope() { retire(); }
    bool active() const noexcept { return active_; }
    void add(const std::shared_ptr<CallbackRegistration>& registration) {
        if (!active_) throw std::logic_error("The callback scope is retired");
        std::erase_if(registrations_, [](const auto& entry) { return entry.expired(); });
        registrations_.push_back(registration);
    }
    void retire() noexcept {
        active_ = false;
        auto registrations = std::move(registrations_);
        for (const auto& weak : registrations)
            if (auto registration = weak.lock()) { registration->active = false; registration->scoped = false; }
        for (const auto& weak : registrations)
            if (auto registration = weak.lock()) registration->retire();
    }
};

template<class Signature> class Callback;
template<class... Args> class Callback<void(Args...)> {
    class Observer {
        struct Record {
            std::size_t references{1};
            std::function<void(Args...)> action;
            explicit Record(std::function<void(Args...)> value) : action(std::move(value)) {}
        };
        Record* value_{};
    public:
        Observer() = default;
        explicit Observer(std::function<void(Args...)> value) : value_(new Record(std::move(value))) {}
        Observer(const Observer& other) noexcept : value_(other.value_) { if (value_) ++value_->references; }
        Observer(Observer&& other) noexcept : value_(std::exchange(other.value_, nullptr)) {}
        Observer& operator=(Observer other) noexcept { std::swap(value_, other.value_); return *this; }
        ~Observer() { if (value_ && --value_->references == 0) delete value_; }
        explicit operator bool() const noexcept { return value_ != nullptr; }
        const std::function<void(Args...)>& operator*() const noexcept { return value_->action; }
    };
    struct Composition final : CallbackRegistration {
        std::function<void(Args...)> action;
        std::function<bool(Args...)> observer_current;
        const CallbackScope* scope{};
        Composition(std::function<void(Args...)> value, std::function<bool(Args...)> current, const CallbackScope& owner)
            : action(std::move(value)), observer_current(std::move(current)), scope(&owner) {}
        void retire() noexcept override { active = false; action = {}; observer_current = {}; }
    };
    // Model callbacks and their snapshots stay on the owning UI thread.
    Observer observer_;
    std::shared_ptr<Composition> composition_;
public:
    Callback() = default;
    Callback(std::function<void(Args...)> observer) { *this = std::move(observer); }
    Callback& operator=(std::function<void(Args...)> observer) {
        auto next = observer ? Observer(std::move(observer)) : Observer{};
        observer_ = std::move(next);
        return *this;
    }
    const std::function<void(Args...)>& observer() const noexcept {
        static const std::function<void(Args...)> empty;
        return observer_ ? *observer_ : empty;
    }
    explicit operator bool() const noexcept {
        return observer_ || (composition_ && composition_->active && composition_->action);
    }
    void compose(CallbackScope& scope, std::function<void(Args...)> action, std::function<bool(Args...)> current = {}) {
        if (!action) {
            if (composition_) composition_->retire();
            if (composition_.use_count() > 1) composition_.reset();
            return;
        }
        if (!scope.active()) throw std::logic_error("The callback scope is retired");
        if (composition_ && composition_.use_count() == 1 && composition_->scoped && composition_->scope == &scope) {
            composition_->action = std::move(action);
            composition_->observer_current = std::move(current);
            composition_->active = true;
            return;
        }
        auto next = std::make_shared<Composition>(std::move(action), std::move(current), scope);
        scope.add(next);
        if (composition_) composition_->retire();
        composition_ = std::move(next);
    }
    void operator()(Args... args) const & {
        auto snapshot = *this;
        std::move(snapshot)(args...);
    }
    void operator()(Args... args) && {
        // Consume an already captured event without taking a second ownership snapshot.
        const auto observer = std::move(observer_);
        const auto registration = std::move(composition_);
        const auto composition = registration && registration->active ? registration->action : std::function<void(Args...)>{};
        const auto current = registration && registration->active ? registration->observer_current : std::function<bool(Args...)>{};
        if (composition) composition(args...);
        if (observer && (!current || current(args...))) (*observer)(args...);
    }
};

struct CompositionAccess {
    template<class T> static void selection(T& control) {
        auto callback = control.change_;
        if (callback) std::move(callback)();
    }
    template<class T, class F> static void click(T& control, CallbackScope& scope, F&& action) {
        control.click_.compose(scope, std::forward<F>(action));
    }
    template<class T, class F, class G> static void click(T& control, CallbackScope& scope, F&& action, G&& current) {
        control.click_.compose(scope, std::forward<F>(action), std::forward<G>(current));
    }
    template<class T, class F> static void toggle(T& control, CallbackScope& scope, F&& action) {
        control.toggle_.compose(scope, std::forward<F>(action));
    }
    template<class T, class F, class G> static void toggle(T& control, CallbackScope& scope, F&& action, G&& current) {
        control.toggle_.compose(scope, std::forward<F>(action), std::forward<G>(current));
    }
    template<class T, class F> static void change(T& control, CallbackScope& scope, F&& action) {
        control.change_.compose(scope, std::forward<F>(action));
    }
    template<class T, class F> static void focus(T& control, CallbackScope& scope, F&& action) {
        control.focus_.compose(scope, std::forward<F>(action));
    }
    template<class T, class F> static void submit(T& control, CallbackScope& scope, F&& action) {
        control.submit_.compose(scope, std::forward<F>(action));
    }
    template<class T, class F> static void activate(T& control, CallbackScope& scope, F&& action) {
        control.activate_.compose(scope, std::forward<F>(action));
    }
    template<class T, class F, class G> static void activate(T& control, CallbackScope& scope, F&& action, G&& current) {
        control.activate_.compose(scope, std::forward<F>(action), std::forward<G>(current));
    }
    template<class T, class F> static void accept(T& control, CallbackScope& scope, F&& action) {
        control.accept_.compose(scope, std::forward<F>(action));
    }
    template<class T, class F> static void action(T& control, CallbackScope& scope, F&& action) {
        control.action_.compose(scope, std::forward<F>(action));
    }
};

}
