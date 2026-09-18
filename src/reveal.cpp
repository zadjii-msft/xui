#include "xui/reveal.hpp"
#include <algorithm>
#include <cmath>

namespace xui {

Reveal::Reveal(std::shared_ptr<Element> content, std::wstring name)
    : Control(ControlRole::content_view, std::move(name), {}), child_{std::move(content)} {
    adopt(child_[0]);
}

void Reveal::set_duration(unsigned milliseconds) {
    if (milliseconds > 10000) throw std::invalid_argument("Reveal duration must be between 0 and 10000 milliseconds");
    if (duration_ == milliseconds) return;
    duration_ = milliseconds;
    if (animating_) settle();
}

void Reveal::set_layout(RevealLayout value) {
    if (value != RevealLayout::fixed && value != RevealLayout::expand)
        throw std::invalid_argument("Invalid reveal layout");
    if (layout_ == value) return;
    settle();
    layout_ = value;
    invalidate(Invalidation::layout);
}

void Reveal::set_direction(RevealDirection value) {
    if (value < RevealDirection::bottom || value > RevealDirection::right)
        throw std::invalid_argument("Invalid reveal direction");
    if (direction_ == value) return;
    settle();
    direction_ = value;
    invalidate(Invalidation::layout);
}

void Reveal::set_open(bool value) {
    if (open_ == value) return;
    const auto now = Clock::now();
    advance(now);
    open_ = value;
    start_ = progress_;
    started_ = now;
    animating_ = duration_ && progress_ != (open_ ? 1.0f : 0.0f);
    if (!animating_) progress_ = open_ ? 1.0f : 0.0f;
    invalidate(Invalidation::layout);
}

void Reveal::advance(Clock::time_point now) {
    if (!animating_) return;
    const float elapsed = std::chrono::duration<float, std::milli>(now - started_).count();
    const float t = std::clamp(elapsed / duration_, 0.0f, 1.0f);
    const float remaining = 1 - t;
    const float target = open_ ? 1.0f : 0.0f;
    const float interpolated = t == 1 ? target : start_ + (target - start_) * (1 - remaining * remaining * remaining);
    const float next = open_ ? std::max(progress_, interpolated) : std::min(progress_, interpolated);
    if (t == 1) animating_ = false;
    if (next == progress_ && t != 1) return;
    progress_ = next;
    invalidate(layout_ == RevealLayout::expand || (!animating_ && !open_) ?
        Invalidation::layout : Invalidation::placement);
}

void Reveal::settle() {
    const float target = open_ ? 1.0f : 0.0f;
    if (!animating_ && progress_ == target) return;
    animating_ = false;
    progress_ = target;
    invalidate(Invalidation::layout);
}

Size Reveal::expanded_size(Size available) {
    // The animation axis must not use the current slot as its natural-size constraint.
    // Otherwise repeated measurement shrinks the child again on every frame.
    if (vertical()) available.height = (std::numeric_limits<float>::max)();
    else available.width = (std::numeric_limits<float>::max)();
    const auto desired = content()->measure(available);
    const auto extent = vertical() ? desired.height : desired.width;
    if (!std::isfinite(extent) || extent >= (std::numeric_limits<float>::max)())
        throw std::invalid_argument("Expanding reveal content requires a bounded natural size on the animation axis");
    return desired;
}

Size Reveal::measure(Size available) {
    if (!open_ && !animating_ && progress_ == 0) return {};
    if (layout_ == RevealLayout::expand) {
        auto desired = expanded_size(available);
        if (vertical()) desired.height *= progress_;
        else desired.width *= progress_;
        return constrain(desired, available);
    }
    return constrain(content()->measure(available), available);
}

void Reveal::arrange(Rect rectangle) {
    if (!open_ && !animating_ && progress_ == 0) {
        if (vertical()) rectangle.height = 0;
        else rectangle.width = 0;
    }
    Element::arrange(rectangle);
    rectangle = bounds();
    if (layout_ == RevealLayout::expand) {
        const auto full = expanded_size({rectangle.width, rectangle.height});
        if (vertical()) {
            if (direction_ == RevealDirection::top) rectangle.y += rectangle.height - full.height;
            rectangle.height = full.height;
        } else {
            if (direction_ == RevealDirection::left) rectangle.x += rectangle.width - full.width;
            rectangle.width = full.width;
        }
    } else {
        switch (direction_) {
        case RevealDirection::bottom: rectangle.y += rectangle.height * (1 - progress_); break;
        case RevealDirection::top: rectangle.y -= rectangle.height * (1 - progress_); break;
        case RevealDirection::left: rectangle.x -= rectangle.width * (1 - progress_); break;
        case RevealDirection::right: rectangle.x += rectangle.width * (1 - progress_); break;
        }
    }
    content()->arrange(rectangle);
}

}
