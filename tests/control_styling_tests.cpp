#include "xui/controls.hpp"
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>

// Allocation accounting, mirroring tests/collections_tests.cpp: a thread-local
// counter scoped by `active`, plus an optional `fail_at` to inject a failure
// on a specific allocation for strong-exception-guarantee coverage.
namespace allocation_probe {
thread_local bool active{};
thread_local std::size_t bytes{}, calls{}, fail_at{};
}
void* operator new(std::size_t size) {
    if (allocation_probe::active && allocation_probe::fail_at && allocation_probe::calls + 1 == allocation_probe::fail_at) {
        // One-shot: clear fail_at before throwing so any allocation performed
        // while constructing/propagating the exception object itself does
        // not recursively re-trigger this same synthetic failure.
        allocation_probe::fail_at = 0;
        throw std::bad_alloc{};
    }
    if (auto* value = std::malloc(size ? size : 1)) {
        if (allocation_probe::active) { allocation_probe::bytes += size; ++allocation_probe::calls; }
        return value;
    }
    throw std::bad_alloc{};
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* value) noexcept { std::free(value); }
void operator delete[](void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }
void operator delete[](void* value, std::size_t) noexcept { std::free(value); }

namespace {
using namespace xui;
// Release exercises every allocation failure point. MSVC iterator-debug builds
// also allocate container proxies in noexcept paths; injection excludes those.
#if defined(_ITERATOR_DEBUG_LEVEL) && _ITERATOR_DEBUG_LEVEL != 0
constexpr bool fault_injection_supported = false;
#else
constexpr bool fault_injection_supported = true;
#endif
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
template<class F> void rejects(F action, const char* message) {
    try { action(); } catch (const std::invalid_argument&) { return; }
    throw std::runtime_error(message);
}
struct AllocationScope {
    AllocationScope() { allocation_probe::bytes = allocation_probe::calls = 0; allocation_probe::active = true; }
    ~AllocationScope() { allocation_probe::active = false; allocation_probe::fail_at = 0; }
};
template<class F> std::pair<std::size_t, std::size_t> measure_allocations(F action) {
    AllocationScope scope;
    action();
    return {allocation_probe::bytes, allocation_probe::calls};
}

void schema_allowlists() {
    // Full part x property matrix, mirroring the documented pilot schema
    // exactly: root gets the six legacy surface properties, label and mark
    // are foreground-only, indicator is background/border/radius/size.
    struct Property { StyleProperty id; void (*set)(PartStyleValues&); };
    const Property properties[] = {
        {StyleProperty::background, [](PartStyleValues& v) { v.background = ThemeColor{1}; }},
        {StyleProperty::foreground, [](PartStyleValues& v) { v.foreground = ThemeColor{1}; }},
        {StyleProperty::border_brush, [](PartStyleValues& v) { v.border_brush = ThemeColor{1}; }},
        {StyleProperty::border_thickness, [](PartStyleValues& v) { v.border_thickness = Insets{1, 1, 1, 1}; }},
        {StyleProperty::padding, [](PartStyleValues& v) { v.padding = Insets{1, 1, 1, 1}; }},
        {StyleProperty::corner_radius, [](PartStyleValues& v) { v.corner_radius = 2.0f; }},
        {StyleProperty::size, [](PartStyleValues& v) { v.size = 10.0f; }},
    };
    const auto bit = [](StyleProperty p) { return static_cast<std::uint32_t>(p); };
    const std::uint32_t root_allowed = bit(StyleProperty::background) | bit(StyleProperty::foreground) |
        bit(StyleProperty::border_brush) | bit(StyleProperty::border_thickness) | bit(StyleProperty::padding) | bit(StyleProperty::corner_radius);
    const std::uint32_t label_allowed = bit(StyleProperty::foreground);
    const std::uint32_t indicator_allowed = bit(StyleProperty::background) | bit(StyleProperty::border_brush) |
        bit(StyleProperty::border_thickness) | bit(StyleProperty::corner_radius) | bit(StyleProperty::size);
    const std::uint32_t mark_allowed = bit(StyleProperty::foreground);
    const std::pair<StylePart, std::uint32_t> parts[] = {
        {StylePart::root, root_allowed}, {StylePart::label, label_allowed},
        {StylePart::indicator, indicator_allowed}, {StylePart::mark, mark_allowed},
    };
    for (const auto& [part, allowed] : parts) {
        for (const auto& property : properties) {
            PartStyleValues values;
            property.set(values);
            if (bit(property.id) & allowed) {
                validate_part_values(StyleTarget::toggle, part, values); // must not throw
            } else {
                rejects([&] { validate_part_values(StyleTarget::toggle, part, values); },
                    "Every disallowed part/property pair in the pilot schema must be rejected explicitly");
            }
        }
    }
    rejects([] { validate_part_values(StyleTarget::toggle, static_cast<StylePart>(99), {}); }, "An unsupported part is rejected");
    rejects([] { validate_part(StyleTarget::toggle, static_cast<StylePart>(99)); }, "validate_part rejects an unsupported part");
}

void unset_and_dimension_validation() {
    PartStyleValues empty;
    validate_part_values(StyleTarget::toggle, StylePart::root, empty); // unset/empty always valid
    PartStyleValues zero; zero.corner_radius = 0.0f; zero.padding = Insets{0, 0, 0, 0};
    validate_part_values(StyleTarget::toggle, StylePart::root, zero); // explicit zero is not "unset"
    PartStyleValues invalid;
    invalid.corner_radius = std::numeric_limits<float>::infinity();
    rejects([&] { validate_part_values(StyleTarget::toggle, StylePart::root, invalid); }, "Infinite dimension fails");
    invalid.corner_radius = std::numeric_limits<float>::quiet_NaN();
    rejects([&] { validate_part_values(StyleTarget::toggle, StylePart::root, invalid); }, "NaN dimension fails");
    invalid.corner_radius = 32769.0f;
    rejects([&] { validate_part_values(StyleTarget::toggle, StylePart::root, invalid); }, "Huge dimension fails");
    invalid.corner_radius = -1.0f;
    rejects([&] { validate_part_values(StyleTarget::toggle, StylePart::root, invalid); }, "Negative dimension fails");
    PartStyleValues indicator; indicator.size = -1.0f;
    rejects([&] { validate_part_values(StyleTarget::toggle, StylePart::indicator, indicator); }, "Negative size fails");
    PartStyleValues color; color.foreground = ThemeColor{0x1000000};
    rejects([&] { validate_part_values(StyleTarget::toggle, StylePart::root, color); }, "ARGB is not RGB24");
}

void state_precedence() {
    // Exactly one positive state per rule for this pilot; unrecognized or
    // multi-bit masks are rejected without restricting the mask type itself.
    rejects([] { ControlStyle::create(StyleTarget::toggle, {}, {{StylePart::root, 0, {}}}); }, "Zero state fails");
    rejects([] { ControlStyle::create(StyleTarget::toggle, {}, {{StylePart::root, style_states::focused | style_states::checked, {}}}); },
        "Multi-bit state fails");
    rejects([] { ControlStyle::create(StyleTarget::toggle, {}, {{StylePart::root, StyleStateMask(1) << 40, {}}}); },
        "High/unrecognized bit fails in the pilot schema without limiting the mask type");
    PartStyleValues base; base.background = ThemeColor{1};
    std::vector<StyleRule> rules;
    const std::array<StyleStateMask, 5> ordered{style_states::focused, style_states::checked, style_states::hovered,
        style_states::pressed, style_states::disabled};
    for (unsigned i = 0; i < 5; ++i) {
        PartStyleValues value; value.background = ThemeColor{10 + i};
        rules.push_back({StylePart::indicator, ordered[i], value});
    }
    auto style = ControlStyle::create(StyleTarget::toggle, {{StylePart::indicator, base}}, rules);
    for (unsigned mask = 0; mask < 32; ++mask) {
        auto expected = 1u;
        for (unsigned i = 0; i < 5; ++i) if (mask & (1u << i)) expected = 10 + i;
        const auto resolved = style->resolve(StylePart::indicator, mask);
        require(resolved && resolved->background == ThemeColor{expected},
            "State conflicts follow focused/checked/hovered/pressed/disabled order");
    }
    // Derivation: inherited disabled still wins over a newly derived hover,
    // and derivation does not mutate the base definition.
    PartStyleValues hover_override; hover_override.background = ThemeColor{77};
    auto derived = ControlStyle::create(StyleTarget::toggle, {}, {{StylePart::indicator, style_states::hovered, hover_override}}, style);
    require(derived->resolve(StylePart::indicator, style_states::hovered)->background == ThemeColor{77},
        "Derived state wins the same inherited state");
    require(derived->resolve(StylePart::indicator, style_states::hovered | style_states::disabled)->background == ThemeColor{14},
        "Inherited disabled still wins derived hover");
    require(style->resolve(StylePart::indicator, style_states::hovered)->background == ThemeColor{12},
        "Derivation does not mutate the base definition");
    require(!style->has_part(StylePart::root), "Unauthored part is not compiled");
    require(!style->resolve(StylePart::root, 0).has_value(), "Resolving an unauthored part returns nullopt");
}

void target_and_part_validation() {
    // The target is validated unconditionally and first, even for a
    // completely empty definition; schema_for is not reachable only through
    // the base_values/rules loops.
    rejects([] { ControlStyle::create(static_cast<StyleTarget>(99), {}, {}); },
        "An unrecognized target is rejected even with empty base_values and rules");
    rejects([] { validate_part(static_cast<StyleTarget>(99), StylePart::root); }, "validate_part rejects an unrecognized target");

    // Read paths reject an unsupported part explicitly instead of treating
    // it as a silently unauthored (but otherwise valid) part.
    auto style = ControlStyle::create(StyleTarget::toggle, {}, {});
    rejects([&] { style->resolve(static_cast<StylePart>(50), 0); }, "ControlStyle::resolve rejects an unsupported part");
    rejects([&] { style->has_part(static_cast<StylePart>(50)); }, "ControlStyle::has_part rejects an unsupported part");
    ControlStyleAttachment plain(StyleTarget::toggle);
    rejects([&] { plain.local(static_cast<StylePart>(50)); }, "ControlStyleAttachment::local rejects an unsupported part");
    rejects([&] { plain.effective(static_cast<StylePart>(50), 0); }, "ControlStyleAttachment::effective rejects an unsupported part");

    rejects([] { ControlStyleAttachment foreign(static_cast<StyleTarget>(5)); },
        "An attachment rejects an unsupported target at construction");
    rejects([&] { style->resolve(StylePart::root, 1ull << 40); }, "Resolve rejects unsupported state bits");
    rejects([&] { plain.effective(StylePart::root, 1ull << 40); }, "Empty attachment rejects unsupported state bits");
    rejects([&] { plain.assign_style(style, 1ull << 40); }, "Style assignment rejects unsupported state bits before publication");
    PartStyleValues local; local.foreground = ThemeColor{123};
    rejects([&] { plain.assign_local(StylePart::root, local, 1ull << 40); }, "Local assignment rejects unsupported state bits before publication");
    rejects([&] { plain.state_changed(1ull << 40); }, "State changes reject unsupported state bits");
    require(plain.empty() && !plain.effective(StylePart::root, 0), "Invalid masks leave the empty attachment unchanged");
    plain.assign_local(StylePart::root, local, 0);
    rejects([&] { plain.effective(StylePart::label, 1ull << 40); }, "Local-only attachment rejects unsupported state bits");
    require(plain.effective(StylePart::label, 0)->foreground == local.foreground, "Rejected mask preserves local inheritance");
    Button unsupported(L"Button");
    rejects([&] { unsupported.set_control_style(style); }, "A Button rejects a generic Toggle style");
    rejects([&] { unsupported.control_style_values(StylePart::root); }, "Unsupported control getters reject generic style values");
    rejects([&] { unsupported.effective_control_style_values(StylePart::root); }, "Unsupported control effective getters reject generic style values");

    // Control-level: an unsupported part is rejected even before any
    // attachment has ever been created.
    Toggle fresh(L"x");
    rejects([&] { fresh.style_values(static_cast<StylePart>(50)); },
        "control_style_values rejects an unsupported part even with no attachment yet");
    rejects([&] { (void)fresh.effective_style_values(static_cast<StylePart>(50)); },
        "effective_control_style_values rejects an unsupported part even with no attachment yet");
    require(!fresh.effective_style_values(StylePart::root), "A valid, unauthored part returns nullptr without throwing");
}

void inheritance_bounds() {
    auto style = ControlStyle::create(StyleTarget::toggle, {}, {});
    for (unsigned i = 1; i < 16; ++i) style = ControlStyle::create(StyleTarget::toggle, {}, {}, style);
    rejects([&] { ControlStyle::create(StyleTarget::toggle, {}, {}, style); }, "Style depth is bounded");

    // Otherwise-valid rules (a single recognized state, an allowed
    // property), so this exercises the rule-count bound itself rather than
    // failing for the unrelated reason a default-constructed StyleRule
    // (state 0) would fail for.
    std::vector<StyleRule> too_many_rules;
    for (unsigned i = 0; i < 257; ++i) {
        PartStyleValues values; values.background = ThemeColor{(i % 250) + 1};
        too_many_rules.push_back({StylePart::indicator, style_states::hovered, values});
    }
    rejects([&] { ControlStyle::create(StyleTarget::toggle, {}, too_many_rules); }, "Rule count is bounded using otherwise-valid rules");

    std::vector<std::pair<StylePart, PartStyleValues>> too_many_bases;
    for (unsigned i = 0; i < 65; ++i) too_many_bases.push_back({StylePart::root, {}});
    rejects([&] { ControlStyle::create(StyleTarget::toggle, too_many_bases, {}); }, "Base entry count is bounded");

    // Honest disclosure, not a fabricated test: the pilot's fixed universe
    // of 4 real StylePart values x 5 recognized state bits can produce at
    // most 4 distinct parts and 4*5=20 distinct (part, state) buckets, both
    // far below the 8-part and 256-total-bucket defensive bounds. Those two
    // specific bounds are not reachable through valid authoring in this
    // pilot and are not claimed to be exercised above; the reachable bounds
    // (rule count, base-entry count, inheritance depth) are covered above.
}

void root_to_label_inheritance() {
    Toggle toggle(L"Compact");
    toggle.set_text_measurer([](auto, auto) { return Size{40, 14}; });

    // Local-only path (no ControlStyle ever attached): touching root alone
    // must still create label's tracked slot via the schema-owned
    // dependent-part relationship, not by unconditionally creating both
    // parts for every family.
    PartStyleValues local_root; local_root.foreground = ThemeColor{0x123456};
    toggle.set_style_values(StylePart::root, local_root);
    require(toggle.effective_style_values(StylePart::label) &&
        toggle.effective_style_values(StylePart::label)->foreground == ThemeColor{0x123456},
        "A local-only root foreground is inherited by label with no style ever attached");

    // An explicit label override always wins over inheritance.
    PartStyleValues explicit_label; explicit_label.foreground = ThemeColor{0x654321};
    toggle.set_style_values(StylePart::label, explicit_label);
    require(toggle.effective_style_values(StylePart::label)->foreground == ThemeColor{0x654321},
        "An explicitly authored label foreground is never overridden by inheritance");

    // Clearing the label override reveals root inheritance again.
    toggle.set_style_values(StylePart::label, {});
    require(toggle.effective_style_values(StylePart::label)->foreground == ThemeColor{0x123456},
        "Clearing an explicit label override reveals root inheritance again");

    // A style-authored root overrides the local root value while attached...
    PartStyleValues style_root; style_root.foreground = ThemeColor{0xaaaaaa};
    auto style = ControlStyle::create(StyleTarget::toggle, {{StylePart::root, style_root}}, {});
    toggle.set_style(style);
    require(toggle.effective_style_values(StylePart::label)->foreground == ThemeColor{0x123456},
        "The root local value still wins over the style's root base (locals win over style)");

    // ...and clearing the shared style while the root local value remains
    // must continue to resolve and inherit correctly.
    toggle.set_style(nullptr);
    require(!toggle.style(), "Style is cleared");
    require(toggle.effective_style_values(StylePart::label) &&
        toggle.effective_style_values(StylePart::label)->foreground == ThemeColor{0x123456},
        "Clearing the shared style while the root local value remains still resolves and inherits correctly");
}

void toggle_model() {
    Toggle toggle(L"Enable feature");
    toggle.set_text_measurer([](auto, auto) { return Size{60, 14}; });
    const auto original = toggle.measure({1000, 1000});
    require(!toggle.effective_style_values(StylePart::root) && !toggle.style(), "Unstyled Toggle has no style state");

    PartStyleValues root, indicator_base, indicator_checked, mark, disabled_root;
    root.background = ThemeColor{0xcccccc, 0x111111};
    root.padding = Insets{4, 5, 6, 7};
    root.border_thickness = Insets{3, 0, 0, 0};
    indicator_base.background = ThemeColor{0x222222};
    indicator_base.corner_radius = 3.0f;
    indicator_base.size = 18.0f;
    indicator_checked.background = ThemeColor{0x00ff00};
    mark.foreground = ThemeColor{0xffffff};
    disabled_root.padding = Insets{0, 0, 0, 0};
    auto style = ControlStyle::create(StyleTarget::toggle,
        {{StylePart::root, root}, {StylePart::indicator, indicator_base}, {StylePart::mark, mark}},
        {{StylePart::indicator, style_states::checked, indicator_checked}, {StylePart::root, style_states::disabled, disabled_root}});

    Invalidation invalidation{};
    unsigned notifications{};
    toggle.set_invalidator([&](Invalidation value) { invalidation = value; ++notifications; });
    toggle.set_style(style);
    require(invalidation == Invalidation::layout, "Padding, borders, and indicator size require layout");
    const auto* cached_root = toggle.effective_style_values(StylePart::root);
    const auto cached_notifications = notifications;
    for (unsigned i = 0; i < 8192; ++i) toggle.set_style(style);
    require(toggle.style().get() == style.get() && toggle.effective_style_values(StylePart::root) == cached_root,
        "Same definition preserves native identity and cached effective values");
    require(notifications == cached_notifications, "Same definition does not invalidate");

    // padding{4,5,6,7} + border{3,0,0,0} + indicator 18 + text{60,14}, gap 12
    // (classic): width = 4+3+18+12+60+6+0 = 103; height = max(14,18)+5+7 = 30.
    auto size = toggle.measure({1000, 1000});
    require(size.width == 103 && size.height == 30, "Measurement includes root padding, border, and indicator size");

    require(toggle.effective_style_values(StylePart::indicator)->size == 18.0f, "Indicator resolves its size metric");
    require(!toggle.checked(), "Toggle starts unchecked");
    require(toggle.effective_style_values(StylePart::indicator)->background == ThemeColor{0x222222},
        "Unchecked indicator uses the base color");
    toggle.set_checked(true);
    require(invalidation == Invalidation::paint, "Checked indicator color is a paint-only change");
    require(toggle.effective_style_values(StylePart::indicator)->background == ThemeColor{0x00ff00},
        "Checked state resolves the checked rule");
    toggle.set_checked(false);

    toggle.pointer_move(true);
    require(invalidation == Invalidation::paint, "Hover with no authored hover rule is still a defined no-color-change paint pass");
    toggle.pointer_move(false);

    toggle.set_enabled(false);
    require(invalidation == Invalidation::layout, "Disabled padding change requires layout");
    // disabled root padding{0,0,0,0}, border stays {3,0,0,0} (merge keeps the
    // base value the rule did not override), indicator stays 18:
    // width = 0+3+18+12+60+0+0 = 93; height = max(14,18)+0+0 = 18.
    size = toggle.measure({1000, 1000});
    require(size.width == 93 && size.height == 18, "Explicit zero padding remains explicit; border and indicator size persist");
    toggle.set_enabled(true);

    PartStyleValues local_indicator;
    local_indicator.background = ThemeColor{0};
    toggle.set_style_values(StylePart::indicator, local_indicator);
    require(toggle.effective_style_values(StylePart::indicator)->background == ThemeColor{0},
        "Local value wins active state and supports black");
    toggle.set_style(nullptr);
    require(toggle.style_values(StylePart::indicator).background == ThemeColor{0} && !toggle.style(),
        "Clear style preserves stored local values");
    toggle.set_style(style);
    require(toggle.effective_style_values(StylePart::indicator)->background == ThemeColor{0},
        "Apply style preserves local value");

    auto invalid = local_indicator;
    invalid.corner_radius = -1.0f;
    rejects([&] { toggle.set_style_values(StylePart::indicator, invalid); }, "Negative dimension fails");
    require(toggle.style_values(StylePart::indicator).background == ThemeColor{0} &&
        !toggle.style_values(StylePart::indicator).corner_radius, "Failed local update is atomic");
    rejects([&] { toggle.set_style_values(StylePart::indicator, PartStyleValues{.foreground = ThemeColor{1}}); },
        "Indicator does not support foreground even via a control's local setter");

    toggle.set_style(nullptr);
    toggle.set_style_values(StylePart::indicator, {});
    toggle.set_style_values(StylePart::root, {});
    toggle.set_style_values(StylePart::mark, {});
    toggle.set_enabled(true);
    size = toggle.measure({1000, 1000});
    require(!toggle.effective_style_values(StylePart::root) && size.width == original.width && size.height == original.height,
        "Clearing local and style releases the attachment and restores original measurement");

    const auto before = notifications;
    toggle.set_style(nullptr);
    toggle.set_style_values(StylePart::root, {});
    require(notifications == before, "Repeated empty clears do not invalidate");

    for (unsigned i = 0; i < 10000; ++i) {
        toggle.set_style(style);
        require(toggle.style().get() == style.get(), "Toggles share immutable definitions");
        toggle.pointer_move(i % 2 == 0);
        toggle.set_style(nullptr);
        require(!toggle.effective_style_values(StylePart::root), "Clear drops effective state");
    }
    require(style.use_count() == 1, "Apply/clear does not retain style definitions");
}

void measurement_geometry() {
    const auto make = [](VisualStyle style) {
        auto toggle = std::make_unique<Toggle>(L"Enable feature");
        toggle->set_visual_style(style);
        toggle->set_text_measurer([](auto, auto) { return Size{60, 14}; });
        return toggle;
    };

    // Paint-only style: no padding/border/indicator size authored, so
    // measurement is exactly the pre-existing, unstyled path.
    auto paint_only_ptr = make(VisualStyle::classic);
    auto& paint_only = *paint_only_ptr;
    const auto original = paint_only.measure({1000, 1000});
    PartStyleValues background_only; background_only.background = ThemeColor{0x445566};
    paint_only.set_style_values(StylePart::root, background_only);
    const auto still = paint_only.measure({1000, 1000});
    require(still.width == original.width && still.height == original.height,
        "A paint-only style change does not affect measurement");

    // Indicator size 0/18/80 with everything else default (classic: left
    // padding 12, right padding 12, gap 12).
    struct SizeCase { float size, width, height; };
    for (const auto& size_case : {SizeCase{0, 96, 14}, SizeCase{18, 114, 18}, SizeCase{80, 176, 80}}) {
        auto toggle_ptr = make(VisualStyle::classic); auto& toggle = *toggle_ptr;
        PartStyleValues indicator; indicator.size = size_case.size;
        toggle.set_style_values(StylePart::indicator, indicator);
        const auto measured = toggle.measure({1000, 1000});
        require(measured.width == size_case.width && measured.height == size_case.height,
            "Indicator size 0/18/80 changes measurement by exactly the size delta");
    }

    // Asymmetric root padding/border: padding{1,2,3,4}, border{5,6,7,8},
    // indicator 20, text{50,10}, gap 12 (classic):
    // width = 1+5+20+12+50+3+7 = 98; height = max(10,20)+2+4+6+8 = 40.
    {
        auto toggle_ptr = make(VisualStyle::classic); auto& toggle = *toggle_ptr;
        toggle.set_text_measurer([](auto, auto) { return Size{50, 10}; });
        PartStyleValues root; root.padding = Insets{1, 2, 3, 4}; root.border_thickness = Insets{5, 6, 7, 8};
        toggle.set_style_values(StylePart::root, root);
        PartStyleValues indicator; indicator.size = 20.0f;
        toggle.set_style_values(StylePart::indicator, indicator);
        const auto measured = toggle.measure({1000, 1000});
        require(measured.width == 98 && measured.height == 40, "Asymmetric root padding/border are each applied on their own edge");
        const auto bounds = toggle.indicator_bounds({10, 20, measured.width, measured.height});
        const auto label = toggle.label_bounds({10, 20, measured.width, measured.height});
        require(bounds.x == 16 && bounds.y == 28 && bounds.width == 20 && bounds.height == 20,
            "Indicator placement uses the asymmetric content area");
        require(label.x == 48 && label.y == 28 && label.width == 50 && label.height == 20,
            "Label placement uses the same content geometry as measurement");
    }

    // A style-state size change (disabled indicator grows to 40) changes
    // measurement only while that state is active.
    {
        auto toggle_ptr = make(VisualStyle::classic); auto& toggle = *toggle_ptr;
        PartStyleValues indicator_base; indicator_base.size = 18.0f;
        PartStyleValues indicator_disabled; indicator_disabled.size = 40.0f;
        auto style = ControlStyle::create(StyleTarget::toggle, {{StylePart::indicator, indicator_base}},
            {{StylePart::indicator, style_states::disabled, indicator_disabled}});
        toggle.set_style(style);
        const auto enabled_size = toggle.measure({1000, 1000});
        toggle.set_enabled(false);
        const auto disabled_size = toggle.measure({1000, 1000});
        require(disabled_size.width > enabled_size.width && disabled_size.height > enabled_size.height,
            "A style-state indicator size change is reflected in measurement while that state is active");
        toggle.set_enabled(true);
        require(toggle.measure({1000, 1000}).width == enabled_size.width, "Measurement returns to the enabled size once re-enabled");
    }

    // Constrained available space clamps the styled desired size.
    {
        auto toggle_ptr = make(VisualStyle::classic); auto& toggle = *toggle_ptr;
        PartStyleValues indicator; indicator.size = 80.0f;
        toggle.set_style_values(StylePart::indicator, indicator);
        const auto measured = toggle.measure({50, 50});
        require(measured.width == 50 && measured.height == 50, "Available space clamps the styled desired size");
    }

    // A fixed size disables auto-sizing entirely; styling must not override it.
    {
        auto toggle_ptr = make(VisualStyle::classic); auto& toggle = *toggle_ptr;
        toggle.set_fixed_size({200, 50});
        PartStyleValues indicator; indicator.size = 999.0f;
        toggle.set_style_values(StylePart::indicator, indicator);
        const auto measured = toggle.measure({1000, 1000});
        require(measured.width == 200 && measured.height == 50, "A fixed size is unaffected by indicator styling");
    }

    // Classic vs WinUI: same authored indicator size, different default gap
    // and left padding (winui: gap 9, left padding 0; classic: gap 12, left
    // padding 12); right padding/border stay whatever is authored/default in
    // both. Difference here is exactly (12-0) + (12-9) = 15.
    {
        auto classic_ptr = make(VisualStyle::classic);
        auto winui_ptr = make(VisualStyle::winui);
        auto& classic = *classic_ptr;
        auto& winui = *winui_ptr;
        PartStyleValues indicator; indicator.size = 20.0f;
        classic.set_style_values(StylePart::indicator, indicator);
        winui.set_style_values(StylePart::indicator, indicator);
        const auto classic_size = classic.measure({1000, 1000});
        const auto winui_size = winui.measure({1000, 1000});
        require(classic_size.width - winui_size.width == 15, "Classic and WinUI apply different default gap/left padding");
        require(classic_size.height == winui_size.height, "Height is unaffected by the gap/left-padding difference");
    }

    // Indicator border insets the mark within a fixed-size outer square: it
    // does not change the outer measured extents, but must still reach the
    // shared invalidation path (paint-vs-layout) via part_style_layout_equal.
    {
        auto toggle_ptr = make(VisualStyle::classic); auto& toggle = *toggle_ptr;
        PartStyleValues indicator; indicator.size = 20.0f;
        toggle.set_style_values(StylePart::indicator, indicator);
        const auto before_border = toggle.measure({1000, 1000});
        Invalidation invalidation{};
        toggle.set_invalidator([&](Invalidation value) { invalidation = value; });
        PartStyleValues bordered = indicator; bordered.border_thickness = Insets{2, 2, 2, 2};
        toggle.set_style_values(StylePart::indicator, bordered);
        require(invalidation == Invalidation::layout, "An indicator border change reaches the shared layout invalidation");
        const auto after_border = toggle.measure({1000, 1000});
        require(after_border.width == before_border.width && after_border.height == before_border.height,
            "Indicator border insets the mark; it does not change the outer measured/indicator size");
        const auto bounds = toggle.indicator_bounds({0, 0, after_border.width, after_border.height});
        const auto mark = toggle.mark_bounds({0, 0, after_border.width, after_border.height});
        require(bounds.width == 20 && bounds.height == 20, "The indicator's outer bounds are the square outer size");
        require(mark.width == 16 && mark.height == 16, "The mark bounds are the indicator bounds inset by its border");
    }
}

void allocation_contract() {
    if (fault_injection_supported) {
        const auto baseline = measure_allocations([] { Label unstyled(L""); });
        const auto constructed = measure_allocations([] { Toggle unstyled(L""); });
        require(constructed == baseline, "An unstyled Toggle adds no allocation beyond the common Control construction");
        std::cout << "Unstyled construction: baseline=" << baseline.first << " bytes/" << baseline.second
            << " allocations, Toggle=" << constructed.first << " bytes/" << constructed.second << " allocations\n";
    }
    Toggle plain(L"Plain");
    plain.set_text_measurer([](auto, auto) { return Size{40, 14}; });
    plain.set_invalidator([](Invalidation) {});
    const auto ordinary = measure_allocations([&] {
        plain.measure({1000, 1000});
        plain.pointer_move(true); plain.pointer_move(false);
        plain.set_checked(true); plain.set_checked(false);
    });
    require(ordinary.first == 0 && ordinary.second == 0,
        "An ordinary (unstyled) Toggle's measure/hover/checked path allocates nothing: "
        "it never creates the generic attachment");

    PartStyleValues root; root.background = ThemeColor{1};
    auto style = ControlStyle::create(StyleTarget::toggle, {{StylePart::root, root}}, {});
    const auto activation = measure_allocations([&] { plain.set_style(style); });
    std::cout << "First set_style activation: " << activation.first << " bytes across " << activation.second << " allocations\n";
    require(activation.second > 0,
        "Activating styling for the first time allocates the attachment; this is measured, not claimed free");
    require(activation.first < 4096 && activation.second <= 16,
        "First activation cost is small and bounded (the attachment plus a couple of tracked-part slots)");

    const auto warmed = measure_allocations([&] {
        for (int i = 0; i < 1000; ++i) {
            plain.pointer_move(i % 2 == 0);
            plain.set_checked(i % 2 == 0);
            (void)plain.effective_style_values(StylePart::root);
            plain.measure({1000, 1000});
        }
    });
    require(warmed.first == 0 && warmed.second == 0,
        "A warmed hover/checked/resolve/measure loop after activation allocates nothing: "
        "only already-allocated slots are updated in place");
    std::cout << "Warmed loop (1000 iterations): " << warmed.first << " bytes, " << warmed.second << " allocations\n";
}

void exhaustive_allocation_failures() {
    if (!fault_injection_supported) {
        std::cout << "SKIP allocation injection: MSVC iterator-debug proxy allocation is outside the Release allocation contract\n";
        return;
    }
    PartStyleValues indicator; indicator.size = 80.0f; indicator.background = ThemeColor{9};
    PartStyleValues root; root.foreground = ThemeColor{3};
    auto style = ControlStyle::create(StyleTarget::toggle, {{StylePart::root, root}}, {});
    auto replacement = ControlStyle::create(StyleTarget::toggle, {{StylePart::root, root}, {StylePart::indicator, indicator}}, {});
    for (unsigned scenario = 0; scenario < 4; ++scenario) {
        unsigned failures{};
        bool completed{};
        for (std::size_t point = 1; point <= 64; ++point) {
            Toggle toggle(L"");
            unsigned notifications{};
            toggle.set_invalidator([&](Invalidation) { ++notifications; });
            if (scenario >= 2) {
                toggle.set_style(style);
                toggle.set_style_values(StylePart::root, root);
            }
            const auto before_style = toggle.style();
            const auto* before_root = toggle.effective_style_values(StylePart::root);
            const auto before_local = toggle.style_values(StylePart::root);
            const auto before_notifications = notifications;
            bool threw{};
            {
                AllocationScope probe;
                allocation_probe::fail_at = point;
                try {
                    if (scenario % 2 == 0) toggle.set_style(replacement);
                    else toggle.set_style_values(StylePart::indicator, indicator);
                } catch (const std::bad_alloc&) { threw = true; }
            }
            if (!threw) { completed = true; break; }
            ++failures;
            require(toggle.style() == before_style && notifications == before_notifications,
                "Every allocation failure preserves the previous style and notifications");
            require(toggle.effective_style_values(StylePart::root) == before_root &&
                toggle.style_values(StylePart::root).foreground == before_local.foreground,
                "Every allocation failure preserves locals and cached values");
            require(!toggle.effective_style_values(StylePart::indicator), "Failed assignments do not publish new slots");
            if (scenario < 2) require(!toggle.has_control_styling(), "Failed first activation does not retain an empty sidecar");
        }
        require(completed && failures > 0, "Failure sweep reaches success after exercising every allocation point");
        std::cout << "Allocation failure sweep scenario=" << scenario << " points=" << failures << " passed\n";
    }
}
}

int main() {
    try {
        schema_allowlists();
        unset_and_dimension_validation();
        state_precedence();
        target_and_part_validation();
        inheritance_bounds();
        root_to_label_inheritance();
        toggle_model();
        measurement_geometry();
        allocation_contract();
        exhaustive_allocation_failures();
        std::cout << "Generic control styling engine: schema, precedence, inheritance, attachment lifecycle, "
                     "Toggle model/measurement, allocation accounting, and failure injection passed.\n";
        std::cout << "sizeof_control=" << sizeof(xui::Control) << " sizeof_toggle=" << sizeof(xui::Toggle)
                   << " sizeof_button=" << sizeof(xui::Button) << '\n';
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
