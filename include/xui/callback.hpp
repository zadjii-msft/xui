#pragma once

#include <functional>
#include <memory>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace xui::detail {

class CallbackScope;
class CallbackRegistration {
    friend class CallbackScope;
    CallbackScope* scope_{};
    CallbackRegistration* previous_{};
    CallbackRegistration* next_{};
protected:
    void detach() noexcept;
public:
    bool active{true};
    virtual ~CallbackRegistration();
    virtual void retire() noexcept = 0;
    const CallbackScope* scope() const noexcept { return scope_; }
};

class CallbackScope {
    friend class CallbackRegistration;
    CallbackRegistration* first_{};
    bool active_{true};
    void remove(CallbackRegistration& registration) noexcept {
        if (registration.previous_) registration.previous_->next_ = registration.next_;
        else first_ = registration.next_;
        if (registration.next_) registration.next_->previous_ = registration.previous_;
        registration.scope_ = nullptr;
        registration.previous_ = registration.next_ = nullptr;
    }
public:
    CallbackScope() = default;
    CallbackScope(const CallbackScope&) = delete;
    CallbackScope& operator=(const CallbackScope&) = delete;
    ~CallbackScope() { retire(); }
    bool active() const noexcept { return active_; }
    void add(CallbackRegistration& registration) {
        if (!active_) throw std::logic_error("The callback scope is retired");
        registration.scope_ = this;
        registration.next_ = first_;
        if (first_) first_->previous_ = &registration;
        first_ = &registration;
    }
    void retire() noexcept {
        active_ = false;
        for (auto* item = first_; item; item = item->next_) item->active = false;
        while (first_) {
            auto* item = first_;
            remove(*item);
            item->retire();
        }
    }
};

inline void CallbackRegistration::detach() noexcept {
    if (scope_) scope_->remove(*this);
}
inline CallbackRegistration::~CallbackRegistration() { detach(); }

template<class Signature> class Callback;
template<class... Args> class Callback<void(Args...)> {
    class Observer {
        struct Record {
            std::size_t references{1};
            std::function<void(Args...)> action;
            explicit Record(std::function<void(Args...)> value) : action(std::move(value)) {}
        };
        struct Capture {
            Record* value;
            explicit Capture(Record* record) noexcept : value(record) {}
            Capture(const Capture& other) noexcept : value(other.value) { ++value->references; }
            Capture(Capture&& other) noexcept : value(std::exchange(other.value, nullptr)) {}
            ~Capture() { if (value && --value->references == 0) delete value; }
            void operator()(Args... args) const { value->action(args...); }
        };
        std::function<void(Args...)> value_;
    public:
        Observer() = default;
        template<class F> requires (!std::is_same_v<std::decay_t<F>, Observer>)
        explicit Observer(F&& value) {
            using Value = std::decay_t<F>;
            if constexpr (sizeof(Value) <= 3 * sizeof(void*) && alignof(Value) <= alignof(void*) &&
                std::is_nothrow_copy_constructible_v<Value> &&
                std::is_nothrow_move_constructible_v<Value>) {
                value_ = std::forward<F>(value);
            } else {
                std::function<void(Args...)> action(std::forward<F>(value));
                if (action) value_ = Capture(new Record(std::move(action)));
            }
        }
        Observer(const Observer&) = default;
        Observer(Observer&&) noexcept = default;
        Observer& operator=(Observer other) noexcept { value_.swap(other.value_); return *this; }
        explicit operator bool() const noexcept { return bool(value_); }
        const std::function<void(Args...)>& operator*() const noexcept { return value_; }
    };
    struct Composition final : CallbackRegistration {
        std::size_t references{1};
        unsigned dispatches{};
        std::function<void(Args...)> action;
        std::function<bool(Args...)> observer_current;
        Composition(std::function<void(Args...)> value, std::function<bool(Args...)> current)
            : action(std::move(value)), observer_current(std::move(current)) {}
        ~Composition() override { detach(); }
        void clear() noexcept {
            auto old_action = std::move(action);
            auto old_current = std::move(observer_current);
        }
        void retire() noexcept override {
            active = false;
            if (references == 1 && !dispatches) {
                ++references;
                clear();
                if (--references == 0) delete this;
            }
        }
    };
    class CompositionRef {
        Composition* value_{};
    public:
        CompositionRef() = default;
        explicit CompositionRef(Composition* value) : value_(value) {}
        CompositionRef(const CompositionRef& other) noexcept : value_(other.value_) { if (value_) ++value_->references; }
        CompositionRef(CompositionRef&& other) noexcept : value_(std::exchange(other.value_, nullptr)) {}
        CompositionRef& operator=(CompositionRef other) noexcept { std::swap(value_, other.value_); return *this; }
        ~CompositionRef() {
            if (!value_) return;
            if (--value_->references == 0) delete value_;
            else if (value_->references == 1 && !value_->active && !value_->dispatches) {
                ++value_->references;
                value_->clear();
                if (--value_->references == 0) delete value_;
            }
        }
        explicit operator bool() const noexcept { return value_ != nullptr; }
        Composition* operator->() const noexcept { return value_; }
        Composition& operator*() const noexcept { return *value_; }
    };
    // Model callbacks and their snapshots stay on the owning UI thread.
    Observer observer_;
    CompositionRef composition_;
public:
    Callback() = default;
    Callback(std::function<void(Args...)> observer) { *this = std::move(observer); }
    Callback& operator=(std::function<void(Args...)> observer) {
        auto next = observer ? Observer(std::move(observer)) : Observer{};
        observer_ = std::move(next);
        return *this;
    }
    template<class F> requires (!std::is_same_v<std::decay_t<F>, Callback> &&
        !std::is_same_v<std::decay_t<F>, std::function<void(Args...)>> &&
        std::is_constructible_v<std::function<void(Args...)>, F>)
    Callback& operator=(F&& observer) {
        observer_ = Observer(std::forward<F>(observer));
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
            if (composition_ && composition_->references > 1) composition_ = {};
            return;
        }
        if (!scope.active()) throw std::logic_error("The callback scope is retired");
        if (composition_ && composition_->references == 1 && composition_->scope() == &scope) {
            const auto registration = composition_;
            auto old_action = std::move(registration->action);
            auto old_current = std::move(registration->observer_current);
            composition_->action = std::move(action);
            composition_->observer_current = std::move(current);
            composition_->active = true;
            return;
        }
        CompositionRef next(new Composition(std::move(action), std::move(current)));
        scope.add(*next);
        if (composition_) composition_->retire();
        composition_ = std::move(next);
    }
    void operator()(Args... args) const & {
        if (!composition_) {
            const auto observer = observer_;
            if (observer) (*observer)(args...);
            return;
        }
        auto snapshot = *this;
        std::move(snapshot)(args...);
    }
    void operator()(Args... args) && {
        // Consume an already captured event without taking a second ownership snapshot.
        const auto observer = std::move(observer_);
        const auto registration = std::move(composition_);
        if (!registration || !registration->active) {
            if (observer) (*observer)(args...);
            return;
        }
        // Keep the registration immutable through action and current-identity checking.
        ++registration->dispatches;
        struct Finish {
            Composition& owner;
            ~Finish() { if (--owner.dispatches == 0 && !owner.active) owner.clear(); }
        } finish{*registration};
        if (registration->action) registration->action(args...);
        if (observer && (!registration->observer_current ||
            (registration->active && registration->observer_current(args...)))) (*observer)(args...);
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
