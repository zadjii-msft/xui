#pragma once
#include <windows.h>
#include <array>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace native_focus_diagnostics {
// A thread-local CBT observer records focus/activation requests, including requests
// inside synchronous native calls. It never changes their result or captures input.
class Trace {
public:
    explicit Trace(const char* name) : name_(name), exceptions_(std::uncaught_exceptions()) {
        if (current_) throw std::logic_error("Only one native focus trace can observe a thread");
        current_ = this;
        hook_ = SetWindowsHookExW(WH_CBT, observe, nullptr, GetCurrentThreadId());
        if (!hook_) {
            current_ = nullptr;
            throw std::runtime_error("Cannot install thread-local native focus diagnostics");
        }
        sample();
    }
    ~Trace() {
        UnhookWindowsHookEx(hook_);
        current_ = nullptr;
        sample();
        std::cerr << "Focus trace " << name_ << ": process=" << GetCurrentProcessId()
            << " thread=" << GetCurrentThreadId() << " activation_requests=" << activations_
            << " focus_requests=" << focuses_ << " foreground_changes=" << foreground_changes_
            << " dropped=" << dropped_ << '\n';
        if (std::uncaught_exceptions() > exceptions_ || activations_ || foreground_changes_) {
            for (std::size_t i = 0; i < count_; ++i) {
                const auto& e = events_[i];
                std::cerr << "  +" << e.time - started_ << "ms " << e.phase << ' ' << e.boundary
                    << " cbt=" << e.code << " target=" << e.target << '/' << e.target_process
                    << " foreground=" << e.state.foreground << '/' << e.state.foreground_process
                    << " focus=" << e.state.focus << " active=" << e.state.active << '\n';
            }
        }
    }
    Trace(const Trace&) = delete;
    Trace& operator=(const Trace&) = delete;

    void verify_passive() {
        sample();
        if (dropped_) throw std::runtime_error("Native focus diagnostics exceeded their event limit");
        if (activations_ || focuses_)
            throw std::runtime_error("A passive native fixture requested activation or focus");
        if (foreground_changes_)
            throw std::runtime_error("The foreground changed during a passive native fixture");
    }

    template<class F> void during(const char* phase, F&& action) {
        phase_ = phase;
        boundary_ = "before";
        sample();
        boundary_ = "inside";
        std::forward<F>(action)();
        boundary_ = "after";
        sample();
    }
private:
    struct State {
        HWND foreground{}, focus{}, active{};
        DWORD foreground_process{};
        bool operator==(const State&) const = default;
    };
    struct Event {
        ULONGLONG time{};
        const char* phase{};
        const char* boundary{};
        int code{};
        HWND target{};
        DWORD target_process{};
        State state{};
    };
    static State state() noexcept {
        State value{GetForegroundWindow(), GetFocus(), GetActiveWindow()};
        if (value.foreground) GetWindowThreadProcessId(value.foreground, &value.foreground_process);
        return value;
    }
    void record(int code, HWND target, State value) noexcept {
        if (count_ == events_.size()) { ++dropped_; return; }
        DWORD process{};
        if (target) GetWindowThreadProcessId(target, &process);
        events_[count_++] = {GetTickCount64(), phase_, boundary_, code, target, process, value};
    }
    void sample() noexcept {
        const auto value = state();
        if (count_ && value.foreground != last_.foreground) ++foreground_changes_;
        if (!count_ || value != last_) record(-1, nullptr, value);
        last_ = value;
    }
    static LRESULT CALLBACK observe(int code, WPARAM wparam, LPARAM lparam) noexcept {
        auto* self = current_;
        if (self && (code == HCBT_ACTIVATE || code == HCBT_SETFOCUS)) {
            if (code == HCBT_ACTIVATE) ++self->activations_;
            else ++self->focuses_;
            self->record(code, reinterpret_cast<HWND>(wparam), state());
        }
        return CallNextHookEx(nullptr, code, wparam, lparam);
    }
    static inline thread_local Trace* current_{};
    const char* name_;
    int exceptions_{};
    HHOOK hook_{};
    ULONGLONG started_{GetTickCount64()};
    const char* phase_{"fixture"};
    const char* boundary_{"outside"};
    State last_{};
    std::array<Event, 512> events_{};
    std::size_t count_{}, dropped_{}, activations_{}, focuses_{}, foreground_changes_{};
};
}
