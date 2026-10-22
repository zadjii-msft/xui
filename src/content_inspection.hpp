#pragma once
#include "xui/application.hpp"
#include "xui/adaptive_layout.hpp"
#include "xui/commands.hpp"
#include "xui/documents.hpp"
#include "xui/navigation.hpp"
#include "xui/runtime_hosts.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <set>
#include <unordered_map>

namespace xui::inspection {

inline bool contains(Rect bounds, Point point) {
    return bounds.width > 0 && bounds.height > 0 &&
        point.x >= bounds.x && point.y >= bounds.y &&
        point.x < bounds.x + bounds.width && point.y < bounds.y + bounds.height;
}
inline Rect intersect(Rect first, Rect second) {
    const auto right = std::min(first.x + first.width, second.x + second.width);
    const auto bottom = std::min(first.y + first.height, second.y + second.height);
    const auto x = std::max(first.x, second.x), y = std::max(first.y, second.y);
    return {x, y, std::max(0.0f, right - x), std::max(0.0f, bottom - y)};
}

struct Outline {
    std::array<Rect, 4> segments{};
    bool visible() const {
        return std::any_of(segments.begin(), segments.end(), [](Rect rect) { return rect.width > 0 && rect.height > 0; });
    }
    bool overlaps(Rect bounds) const {
        return std::any_of(segments.begin(), segments.end(), [&](Rect rect) {
            const auto overlap = intersect(rect, bounds);
            return overlap.width > 0 && overlap.height > 0;
        });
    }
};
inline Outline outline(Rect original, Rect clip, float scale) {
    const auto snap = [scale](Rect rect) {
        const float x = std::ceil(rect.x * scale), y = std::ceil(rect.y * scale);
        return Rect{x, y, std::max(0.0f, std::floor((rect.x + rect.width) * scale) - x),
            std::max(0.0f, std::floor((rect.y + rect.height) * scale) - y)};
    };
    const auto bounds = snap(original), viewport = snap(clip);
    Outline result;
    constexpr float stroke = 2;
    if (bounds.width < 2 * stroke || bounds.height < 2 * stroke) return result;
    // Form the original perimeter first. Clipping must never introduce a new edge.
    result.segments = {{
        {bounds.x, bounds.y, bounds.width, stroke},
        {bounds.x, bounds.y + bounds.height - stroke, bounds.width, stroke},
        {bounds.x, bounds.y + stroke, stroke, bounds.height - 2 * stroke},
        {bounds.x + bounds.width - stroke, bounds.y + stroke, stroke, bounds.height - 2 * stroke}}};
    for (auto& segment : result.segments) {
        segment = intersect(segment, viewport);
        segment = {segment.x / scale, segment.y / scale, segment.width / scale, segment.height / scale};
    }
    return result;
}

struct Child {
    const std::shared_ptr<Element>& element;
    Rect clip;
};

class Children {
public:
    Children(const Element& element, Rect clip, bool active) : owned_(element.owned_children()), clip_(clip), count_(owned_.size()) {
        if (!active) return;
        if (const auto* pages = dynamic_cast<const PageView*>(&element)) {
            first_ = pages->selected();
            count_ = owned_.empty() ? 0 : 1;
        } else if (const auto* scroll = dynamic_cast<const ScrollView*>(&element)) {
            clip_ = intersect(clip, scroll->viewport());
        } else if (const auto* content = dynamic_cast<const ContentView*>(&element)) {
            clip_ = intersect(clip, content->content_bounds());
        } else if (const auto* split = dynamic_cast<const SplitView*>(&element)) {
            split_ = split;
            if (!split->expanded()) count_ = 1;
        } else if (const auto* control = dynamic_cast<const Control*>(&element)) {
            auto viewport = control->bounds();
            if (const auto* expander = dynamic_cast<const Expander*>(control)) {
                if (!expander->expanded()) count_ = 0;
                viewport = expander->content_bounds();
            }
            clip_ = intersect(clip, viewport);
        }
    }
    std::size_t size() const { return count_; }
    Child operator[](std::size_t index) const {
        const auto slot = first_ + index;
        return {owned_[slot], split_ ? intersect(clip_, slot == 0 ? split_->first_pane_area() : split_->second_pane_area()) : clip_};
    }
    struct Iterator {
        const Children* children;
        std::size_t index;
        Child operator*() const { return (*children)[index]; }
        Iterator& operator++() { ++index; return *this; }
        bool operator==(const Iterator&) const = default;
    };
    Iterator begin() const { return {this, 0}; }
    Iterator end() const { return {this, count_}; }
private:
    OwnedChildren owned_;
    Rect clip_;
    std::size_t first_{}, count_{};
    const SplitView* split_{};
};

// Registration uses ownership; pointer inspection adds selected-page and clip policy.
inline Children children(const std::shared_ptr<Element>& element, Rect clip, bool active) {
    return {*element, clip, active};
}

inline void enumerate(const std::shared_ptr<Element>& element,
    std::unordered_map<std::uint64_t, std::weak_ptr<Element>>& result) {
    if (!element) return;
    if (!result.emplace(element->id(), element).second)
        throw std::invalid_argument("Inspection content contains duplicate children or a cycle");
    for (const auto& child : element->owned_children()) enumerate(child, result);
}

struct Registration {
    std::vector<ContentInspectionTarget> targets;
    std::unordered_map<std::uint64_t, std::uint32_t> keys;
    std::function<void(std::uint32_t)> picked;

    static Registration prepare(const std::shared_ptr<Element>& root,
        std::vector<ContentInspectionTarget> targets, std::function<void(std::uint32_t)> picked) {
        if (targets.empty() != !picked)
            throw std::invalid_argument("Inspection targets and callback must be supplied together");
        Registration result;
        std::unordered_map<std::uint64_t, std::weak_ptr<Element>> members;
        enumerate(root, members);
        std::vector<bool> used(targets.size());
        for (const auto& target : targets) {
            if (target.key >= targets.size() || used[target.key])
                throw std::invalid_argument("Inspection keys must be unique and dense, starting at zero");
            used[target.key] = true;
            const auto element = target.element.lock();
            if (!element || !members.contains(element->id()))
                throw std::invalid_argument("Inspection targets must be live members of the candidate content");
            if (!result.keys.emplace(element->id(), target.key).second)
                throw std::invalid_argument("An inspection element cannot have multiple keys");
        }
        result.targets = std::move(targets);
        result.picked = std::move(picked);
        return result;
    }
    std::optional<std::uint32_t> key(const Element& element, std::optional<std::uint32_t> ancestor) const {
        const auto found = keys.find(element.id());
        return found == keys.end() ? ancestor : std::optional(found->second);
    }
};

inline void supported_node(const std::shared_ptr<Element>& element, const ContentHost* host = nullptr) {
    if (!element) return;
    if (const auto stack = std::dynamic_pointer_cast<Stack>(element)) {
        const auto& type = typeid(*stack);
        if (type != typeid(Stack) && type != typeid(Grid) && type != typeid(Wrap) &&
            type != typeid(PageView) && type != typeid(ContentHost))
            throw std::invalid_argument("Pointer picking does not support this layout or adaptive overlay");
        if (const auto nested = dynamic_cast<ContentHost*>(stack.get()); nested && nested != host)
            throw std::invalid_argument("Pointer picking does not support nested ContentHosts");
    } else {
        const auto control = std::dynamic_pointer_cast<Control>(element);
        if (!control) throw std::invalid_argument("Pointer picking requires standard retained controls");
        switch (control->role()) {
        case ControlRole::file_list:
        case ControlRole::date_time:
        case ControlRole::popup:
        case ControlRole::media_playback:
        case ControlRole::web_content:
        case ControlRole::swap_chain_panel:
        case ControlRole::vector_canvas:
        case ControlRole::map_view:
            throw std::invalid_argument("Pointer picking does not support this native or popup surface");
        default: break;
        }
        if (const auto input = std::dynamic_pointer_cast<TextInput>(control); input && input->suggestions())
            throw std::invalid_argument("Pointer picking does not support native suggestion surfaces");
    }
}
inline void supported(const std::shared_ptr<Element>& element, const ContentHost* host = nullptr) {
    if (!element) return;
    supported_node(element, host);
    for (const auto& child : element->owned_children()) supported(child, host);
}

struct HostPath {
    std::shared_ptr<ContentHost> host;
    Rect clip;
    std::vector<std::shared_ptr<Element>> ancestors;
};
inline std::optional<Rect> element_clip(const std::shared_ptr<Element>& element,
    const Element& target, Rect clip) {
    if (!element) return {};
    if (const auto control = std::dynamic_pointer_cast<Control>(element); control && !control->visible()) return {};
    if (element.get() == &target) return clip;
    for (const auto& child : children(element, clip, true))
        if (auto result = element_clip(child.element, target, child.clip)) return result;
    return {};
}
inline std::optional<HostPath> host_path(const std::shared_ptr<Element>& element,
    const ContentHost& host, Rect clip, bool active, std::vector<std::shared_ptr<Element>> ancestors = {}) {
    if (!element) return {};
    if (active) if (const auto control = std::dynamic_pointer_cast<Control>(element); control && !control->visible()) return {};
    if (element.get() == &host)
        return HostPath{std::static_pointer_cast<ContentHost>(element), clip, std::move(ancestors)};
    ancestors.push_back(element);
    for (const auto& child : children(element, clip, active))
        if (auto path = host_path(child.element, host, child.clip, active, ancestors)) return path;
    return {};
}

struct Hit {
    bool matched{};
    std::optional<std::uint32_t> key;
};

// A native target constrains traversal to its actual retained ancestry. Only gaps
// without a child HWND use reverse retained surface-paint order, never peer order.
inline Hit hit(const std::shared_ptr<Element>& element, Point point, Rect clip,
    const Registration& registration, const Element* native_target,
    std::optional<std::uint32_t> ancestor = {}) {
    if (!element || !contains(clip, point)) return {};
    if (const auto control = std::dynamic_pointer_cast<Control>(element); control && !control->visible()) return {};
    const auto nearest = registration.key(*element, ancestor);
    const bool matched = !native_target || native_target == element.get();
    const auto required = matched ? nullptr : native_target;
    auto descendants = children(element, clip, true);
    for (auto i = descendants.size(); i > 0; --i) {
        const auto child = descendants[i - 1];
        const auto result = hit(child.element, point, child.clip, registration, required, nearest);
        if (result.matched) return result;
    }
    return matched && contains(element->bounds(), point) ? Hit{true, nearest} : Hit{};
}

}
