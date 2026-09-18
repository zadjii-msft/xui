#pragma once

#include "xui/controls.hpp"
#include "xui/animation.hpp"

namespace xui {

enum class RevealLayout { fixed, expand };
enum class RevealDirection { bottom, top, left, right };

// The child stays full size inside a fixed or expanding clip.
class Reveal final : public Control, public Animation {
public:
    using Clock = std::chrono::steady_clock;
    explicit Reveal(std::shared_ptr<Element> content, std::wstring name = L"Reveal");
    const std::shared_ptr<Element>& content() const { return child_[0]; }
    std::span<const std::shared_ptr<Element>> retained_children() const override { return child_; }
    void set_open(bool value);
    bool open() const { return open_; }
    void set_duration(unsigned milliseconds);
    unsigned duration() const { return duration_; }
    void set_layout(RevealLayout value);
    RevealLayout layout() const { return layout_; }
    void set_direction(RevealDirection value);
    RevealDirection direction() const { return direction_; }
    bool vertical() const { return direction_ == RevealDirection::bottom || direction_ == RevealDirection::top; }
    bool opening_from_zero() const { return layout_ == RevealLayout::expand && open_ && animating_; }
    float progress() const { return progress_; }
    bool animating() const override { return animating_; }
    bool allows_empty_clip() const override { return opening_from_zero(); }
    Size measure(Size available) override;
    void arrange(Rect bounds) override;

    // Backend clock delivery. No worker, timer, or callback belongs to the model.
    void advance(Clock::time_point now) override;
    void settle() override;
private:
    std::shared_ptr<Element> child_[1];
    unsigned duration_{};
    bool open_{}, animating_{};
    float progress_{}, start_{};
    Clock::time_point started_{};
    RevealLayout layout_{RevealLayout::fixed};
    RevealDirection direction_{RevealDirection::bottom};
    Size expanded_size(Size available);
};

}
