#include "xui/reveal.hpp"
#include <algorithm>
#include <cmath>

namespace xui {

Reveal::Reveal(std::shared_ptr<Element> content, std::wstring name)
    : Control(ControlRole::content_view, std::move(name), {}), child_{std::move(content)} {
    adopt(child_[0]);
}

void Reveal::set_duration(unsigned milliseconds) {
    if (portable_) throw std::logic_error("Use atomic portable Reveal state");
    if (milliseconds > 10000) throw std::invalid_argument("Reveal duration must be between 0 and 10000 milliseconds");
    if (duration_ == milliseconds) return;
    duration_ = milliseconds;
    if (animating_) settle();
}

void Reveal::set_layout(RevealLayout value) {
    if (portable_) throw std::logic_error("Use atomic portable Reveal state");
    if (value != RevealLayout::fixed && value != RevealLayout::expand)
        throw std::invalid_argument("Invalid reveal layout");
    if (layout_ == value) return;
    settle();
    layout_ = value;
    invalidate(Invalidation::layout);
}

void Reveal::set_direction(RevealDirection value) {
    if (portable_) throw std::logic_error("Use atomic portable Reveal state");
    if (value < RevealDirection::bottom || value > RevealDirection::right)
        throw std::invalid_argument("Invalid reveal direction");
    if (direction_ == value) return;
    settle();
    direction_ = value;
    invalidate(Invalidation::layout);
}

void Reveal::set_open(bool value) {
    if (portable_) throw std::logic_error("Use atomic portable Reveal state");
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
        if (portable_) return {std::min(desired.width, available.width), std::min(desired.height, available.height)};
        return constrain(desired, available);
    }
    return constrain(content()->measure(available), available);
}

void Reveal::arrange(Rect rectangle) {
    if (portable_) {
        const auto full = expanded_size({rectangle.width, rectangle.height});
        auto child = rectangle;
        if (vertical()) {
            rectangle.height = std::min(rectangle.height, full.height * progress_);
            child.height = full.height;
        } else {
            rectangle.width = std::min(rectangle.width, full.width * progress_);
            child.width = full.width;
        }
        Element::arrange(rectangle);
        content()->arrange(child);
        return;
    }
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

void Reveal::validate_portable_content(const std::shared_ptr<Element>& content) {
    if (!content) throw std::invalid_argument("A portable Reveal requires content");
    if (const auto stack = std::dynamic_pointer_cast<Stack>(content)) {
        for (std::size_t i = 0; i < stack->child_count(); ++i) validate_portable_content(stack->child_at(i));
        return;
    }
    const auto control = std::dynamic_pointer_cast<Control>(content);
    if (!control) throw std::invalid_argument("Unsupported portable Reveal content");
    switch (control->role()) {
    case ControlRole::document_text:
    case ControlRole::file_list:
    case ControlRole::media_playback:
    case ControlRole::web_content:
    case ControlRole::swap_chain_panel:
        throw std::invalid_argument("This native accessibility provider family is unsupported inside portable Reveal");
    default: break;
    }
    if (const auto scroll = std::dynamic_pointer_cast<ScrollView>(content)) {
        if (scroll->virtual_viewport() && !scroll->virtual_viewport()->closed())
            throw std::invalid_argument("Leased virtual viewports are unsupported inside portable Reveal");
        validate_portable_content(scroll->content());
    }
    if (const auto view = std::dynamic_pointer_cast<ContentView>(content)) validate_portable_content(view->content());
    if (const auto split = std::dynamic_pointer_cast<SplitView>(content)) {
        validate_portable_content(split->first()); validate_portable_content(split->second());
    }
    for (const auto& child : control->retained_children()) validate_portable_content(child);
}
void Reveal::validate_portable_state(bool, unsigned duration, RevealDirection direction, bool initial) const {
    if (duration > 400 || (direction != RevealDirection::bottom && direction != RevealDirection::right))
        throw std::invalid_argument("Invalid portable Reveal motion");
    if (portable_cancelled_) throw std::logic_error("The portable Reveal is retired");
    if (initial == portable_) throw std::invalid_argument("Initialize portable Reveal exactly once");
    if (preferred_size_explicit() || width_constraints() || height_constraints())
        throw std::invalid_argument("Size the child rather than the portable Reveal");
    validate_portable_content(content());
}
void Reveal::apply_portable_state(bool open, unsigned duration, RevealDirection direction, bool initial) {
    validate_portable_state(open, duration, direction, initial);
    if (initial) {
        portable_ = true; layout_ = RevealLayout::expand;
        open_ = open; progress_ = start_ = open ? 1.0f : 0.0f; animating_ = false;
        duration_ = duration; direction_ = direction;
        invalidate(Invalidation::layout);
        return;
    }
    if (open_ == open && duration_ == duration && direction_ == direction) return;
    const bool changed_motion = duration_ != duration || direction_ != direction;
    if (changed_motion) {
        progress_ = open_ ? 1.0f : 0.0f;
        animating_ = false;
    } else advance(Clock::now());
    duration_ = duration; direction_ = direction;
    if (open_ != open) {
        open_ = open; start_ = progress_; started_ = Clock::now();
        animating_ = duration_ && progress_ != (open_ ? 1.0f : 0.0f);
        if (!animating_) progress_ = open_ ? 1.0f : 0.0f;
    }
    invalidate(Invalidation::layout);
}
void Reveal::cancel_portable() {
    if (!portable_ || portable_cancelled_) return;
    settle();
    portable_cancelled_ = true;
}

}
