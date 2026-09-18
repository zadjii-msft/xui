#pragma once
#include <algorithm>
#include <chrono>

namespace xui {

// Retained controls share the window clock and its motion/lifetime policy.
class Animation {
public:
    using Clock = std::chrono::steady_clock;
    virtual ~Animation() = default;
    virtual bool animating() const = 0;
    virtual void advance(Clock::time_point now) = 0;
    virtual void settle() = 0;
    virtual bool allows_empty_clip() const { return false; }
};

class ScalarTransition {
public:
    using Clock = Animation::Clock;
    explicit ScalarTransition(float value) : value_(value), start_(value), target_(value) {}
    float value() const { return value_; }
    bool animating() const { return active_; }
    bool retarget(float target, unsigned milliseconds, Clock::time_point now) {
        if (target_ == target) return false;
        advance(now);
        start_ = value_;
        target_ = target;
        duration_ = milliseconds;
        started_ = now;
        active_ = duration_ && value_ != target_;
        if (!active_) value_ = target_;
        return true;
    }
    bool advance(Clock::time_point now) {
        if (!active_) return false;
        const float elapsed = std::chrono::duration<float, std::milli>(now - started_).count();
        const float t = std::clamp(elapsed / duration_, 0.0f, 1.0f);
        if (t == 1) return settle();
        const float remaining = 1 - t;
        const float next = start_ + (target_ - start_) * (1 - remaining * remaining * remaining);
        const float value = target_ > start_ ? std::max(value_, next) : std::min(value_, next);
        if (value == value_) return false;
        value_ = value;
        return true;
    }
    bool settle() {
        if (!active_ && value_ == target_) return false;
        active_ = false;
        value_ = target_;
        return true;
    }
private:
    float value_, start_, target_;
    unsigned duration_{};
    bool active_{};
    Clock::time_point started_{};
};

}
