#pragma once

// Shared sparse styles. ButtonStyle remains a separate, compatible API.

#include "xui/core.hpp"
#include "xui/styling.hpp"
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace xui {

// Each target supplies a schema to the common resolver.
enum class StyleTarget : uint32_t { toggle };

// Parts are presentation roles, not controls or accessibility peers.
enum class StylePart : uint32_t { root, label, indicator, mark };

// Storage depends on authored rules, not the number of state combinations.
using StyleStateMask = std::uint64_t;
namespace style_states {
inline constexpr StyleStateMask focused = 1ull << 0;
inline constexpr StyleStateMask checked = 1ull << 1;
inline constexpr StyleStateMask hovered = 1ull << 2;
inline constexpr StyleStateMask pressed = 1ull << 3;
inline constexpr StyleStateMask disabled = 1ull << 4;
}

enum class StyleProperty : std::uint32_t {
    background = 1u << 0, foreground = 1u << 1, border_brush = 1u << 2,
    border_thickness = 1u << 3, padding = 1u << 4, corner_radius = 1u << 5, size = 1u << 6
};

// The target/part schema rejects unsupported properties. Zero remains set.
// Toggle's size is the indicator's outer square size in DIPs.
struct PartStyleValues {
    std::optional<ThemeColor> background, foreground, border_brush;
    std::optional<Insets> border_thickness, padding;
    std::optional<float> corner_radius, size;
    bool empty() const;
};

// A rule always carries exactly one positive state bit for this pilot.
struct StyleRule { StylePart part; StyleStateMask state; PartStyleValues values; };

// Invalid target, part, property, color, or dimension throws invalid_argument.
void validate_part(StyleTarget target, StylePart part);

void validate_part_values(StyleTarget target, StylePart part, const PartStyleValues& values);

PartStyleValues merge_part_values(PartStyleValues base, const PartStyleValues& overlay);
bool part_style_layout_equal(const PartStyleValues& first, const PartStyleValues& second);

// Immutable definition with separate ordinary values and inherited state buckets.
class ControlStyle final {
public:
    ControlStyle(const ControlStyle&) = delete;
    ControlStyle& operator=(const ControlStyle&) = delete;
    // Inputs are copied and validated before publication. The base is unchanged.
    static std::shared_ptr<const ControlStyle> create(StyleTarget target,
        std::vector<std::pair<StylePart, PartStyleValues>> base_values,
        std::vector<StyleRule> rules, std::shared_ptr<const ControlStyle> based_on = {});
    StyleTarget target() const { return target_; }
    // Applies schema precedence. Unauthored parts return nullopt.
    std::optional<PartStyleValues> resolve(StylePart part, StyleStateMask mask) const;
    // Throws for a part unsupported by this target (see validate_part).
    bool has_part(StylePart part) const;
    std::vector<StylePart> authored_parts() const;
private:
    ControlStyle() = default;
    struct Bucket { StyleStateMask state; PartStyleValues values; };
    struct CompiledPart { StylePart part; PartStyleValues base; std::vector<Bucket> buckets; };
    StyleTarget target_{};
    unsigned depth_{1};
    std::vector<CompiledPart> parts_;
};

// Optional Control-owned storage. Only assignment can allocate.
// State changes update existing slots. Schema dependents are tracked automatically.
class ControlStyleAttachment final {
public:
    explicit ControlStyleAttachment(StyleTarget target);
    ControlStyleAttachment(const ControlStyleAttachment&) = delete;
    ControlStyleAttachment& operator=(const ControlStyleAttachment&) = delete;
    StyleTarget target() const { return target_; }
    std::shared_ptr<const ControlStyle> style() const { return style_; }
    bool empty() const { return !style_ && parts_.empty(); }
    // True when the owner can release the attachment.
    bool fully_empty() const;
    void set_context_enabled(bool enabled) { context_enabled_ = enabled; }
    bool context_enabled() const { return context_enabled_; }
    // Throws for a part unsupported by this target (see validate_part).
    const PartStyleValues& local(StylePart part) const;
    // No allocation. Untracked parts return nullptr; unsupported parts throw.
    const PartStyleValues* effective(StylePart part, StyleStateMask mask);
    // Same identity is a no-op. Failed assignment preserves all previous values.
    std::optional<Invalidation> assign_style(std::shared_ptr<const ControlStyle> style, StyleStateMask mask);
    // Replaces a part's locals transactionally. Empty values clear the part.
    std::optional<Invalidation> assign_local(StylePart part, PartStyleValues values, StyleStateMask mask);
    // No allocation; compares effective metrics to select layout or paint.
    Invalidation state_changed(StyleStateMask mask);
private:
    struct Slot { StylePart part; PartStyleValues local; PartStyleValues effective; };
    static constexpr std::size_t max_parts = 8;
    Slot* find(StylePart part);
    const Slot* find(StylePart part) const;
    Slot& ensure(StylePart part);
    void recompute(StyleStateMask mask);
    std::vector<StylePart> required_parts(const std::vector<StylePart>& authored) const;
    StyleTarget target_;
    std::shared_ptr<const ControlStyle> style_;
    std::vector<Slot> parts_;
    StyleStateMask mask_{~StyleStateMask(0)};
    bool context_enabled_{true};
};

}
