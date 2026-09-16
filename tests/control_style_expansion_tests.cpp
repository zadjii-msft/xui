#include "xui/control_styling.hpp"
#include "xui/controls.hpp"
#include "xui/xui.h"
#include "../src/abi_callbacks.hpp"
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>

namespace allocation_probe {
thread_local bool enabled{};
thread_local std::size_t calls{}, bytes{};
}
void* operator new(std::size_t size) {
    if (auto* value = std::malloc(size ? size : 1)) {
        if (allocation_probe::enabled) { ++allocation_probe::calls; allocation_probe::bytes += size; }
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
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F action) {
    try { action(); } catch (const std::invalid_argument&) { return; }
    throw std::runtime_error("Expected invalid_argument");
}
struct Probe {
    Probe() { allocation_probe::calls = allocation_probe::bytes = 0; allocation_probe::enabled = true; }
    ~Probe() { allocation_probe::enabled = false; }
};
class StyledElement final : public Element {
public:
    explicit StyledElement(StyleTarget target) : target_(target) {}
    void state(StyleStateMask state) { state_ = state; invalidate_control_style_state(); }
protected:
    std::optional<StyleTarget> control_style_target() const override { return target_; }
    StyleStateMask control_style_state_bits() const override { return state_ | Element::control_style_state_bits(); }
private:
    StyleTarget target_;
    StyleStateMask state_{};
};
void identifiers_and_schema() {
    static_assert(static_cast<unsigned>(StyleTarget::toggle) == XUI_STYLE_TARGET_TOGGLE);
    static_assert(static_cast<unsigned>(StyleTarget::web_content) == XUI_STYLE_TARGET_WEB_CONTENT);
    static_assert(static_cast<unsigned>(StylePart::second_pane) == XUI_STYLE_SECOND_PANE);
    static_assert(style_states::selected == XUI_STYLE_STATE_SELECTED);
    static_assert(style_states::determinate == XUI_STYLE_STATE_DETERMINATE);
    static_assert(style_property(StyleProperty::wrapping) == XUI_STYLE_WRAPPING);
    std::size_t targets{}, parts{};
    for (unsigned id = 0; id <= static_cast<unsigned>(StyleTarget::web_content); ++id) {
        const auto target = static_cast<StyleTarget>(id);
        const StyleTargetSchema* schema{};
        try { schema = &control_style_schema(target); }
        catch (const std::invalid_argument&) { continue; }
        ++targets;
        require(schema->parts.size() <= 64, "Schema exceeds the bounded part capacity");
        auto empty = ControlStyle::create(target, {}, {});
        for (const auto& part : schema->parts) {
            ++parts;
            validate_part_values(target, part.part, {});
            require(!empty->resolve(part.part, 0), "An empty definition has no fabricated values");
            rejects([&] { empty->resolve(part.part, 1ull << 63); });
        }
        rejects([&] { empty->resolve(static_cast<StylePart>(0xffffffff), 0); });
    }
    require(targets > 3, "Family schemas were not linked");
    std::cout << "Implemented schema targets=" << targets << " parts=" << parts << '\n';
}
void element_lifecycle() {
    StyledElement layout(StyleTarget::stack);
    PartStyleValues base; base.background = ThemeColor{0x123456, 0x654321}; base.spacing = 7;
    PartStyleValues local; local.padding = Insets{1, 2, 3, 4};
    const auto content = style_content_bounds({10, 20, 30, 40}, &local);
    require(content.x == 11 && content.y == 22 && content.width == 26 && content.height == 34,
        "Shared content geometry preserves asymmetric insets");
    const auto defaults = style_content_bounds({10, 20, 30, 40}, nullptr, {1, 2, 3, 4}, {2, 3, 4, 5});
    require(defaults.x == 13 && defaults.y == 25 && defaults.width == 20 && defaults.height == 26,
        "Absent style values preserve default padding and border");
    local.border_thickness = Insets{};
    const auto cleared_border = style_content_bounds({10, 20, 30, 40}, &local, {}, {9, 9, 9, 9});
    require(cleared_border.x == content.x && cleared_border.width == content.width,
        "Explicit zero border overrides the default border");
    auto style = ControlStyle::create(StyleTarget::stack, {{StylePart::root, base}}, {});
    layout.set_control_style_values(StylePart::root, local);
    layout.set_control_style(style);
    require(layout.effective_control_style_values(StylePart::root)->spacing == 7, "Element resolves layout metrics");
    layout.set_control_style(nullptr);
    require(layout.control_style_values(StylePart::root).padding->left == 1, "Element preserves locals on style removal");
    layout.set_control_style_values(StylePart::root, {});
    require(!layout.has_control_styling(), "Clearing both layers releases the single Element attachment");
    layout.state(style_states::hovered | style_states::pressed);
    layout.set_control_style(style);
    require(layout.effective_control_style_values(StylePart::root)->background == base.background,
        "Unexposed owner interactions do not fail an unrelated layout schema");
}
void transient_states_and_fonts() {
    StyledElement owner(StyleTarget::data_grid);
    auto family = make_style_font_family("Segoe UI");
    PartStyleValues root; root.foreground = ThemeColor{0x102030};
    PartStyleValues root_hover; root_hover.foreground = ThemeColor{0x405060};
    PartStyleValues root_focus; root_focus.foreground = ThemeColor{0x708090};
    PartStyleValues root_disabled; root_disabled.foreground = ThemeColor{0xa0b0c0};
    PartStyleValues row; row.background = ThemeColor{0x123456};
    PartStyleValues hover; hover.background = ThemeColor{0xabcdef};
    PartStyleValues disabled; disabled.background = ThemeColor{0x777777};
    PartStyleValues cell; cell.font_family = family; cell.font_size = 18;
    auto style = ControlStyle::create(StyleTarget::data_grid,
        {{StylePart::root, root}, {StylePart::row, row}, {StylePart::cell, cell}},
        {{StylePart::row, style_states::hovered, hover}, {StylePart::row, style_states::disabled, disabled},
         {StylePart::root, style_states::hovered, root_hover}, {StylePart::root, style_states::focused, root_focus},
         {StylePart::root, style_states::disabled, root_disabled}});
    owner.set_control_style(style);
    owner.state(style_states::hovered | style_states::focused | style_states::pressed);
    auto normal = owner.resolve_control_style_part(StylePart::row, 0);
    require(normal.background == row.background, "Owner interactions do not become row interactions");
    require(normal.foreground == root_hover.foreground, "Authored owner root hover inherits without activating row hover");
    require(owner.effective_control_style_values(StylePart::root)->foreground == root_hover.foreground,
        "Ordinary root painting still uses the owner state");
    owner.state(style_states::focused);
    require(owner.resolve_control_style_part(StylePart::row, 0).foreground == root_focus.foreground,
        "Authored owner root focus inherits without activating row focus");
    require(owner.resolve_control_style_part(StylePart::row, style_states::hovered).foreground == root_focus.foreground,
        "Item hover does not replace the actual owner state for inherited root rules");
    require(owner.resolve_control_style_part(StylePart::row, style_states::hovered).background == hover.background,
        "The actual row state selects the row hover rule");
    owner.state(style_states::disabled);
    require(owner.resolve_control_style_part(StylePart::row, style_states::hovered).background == disabled.background,
        "Owner disabled context dominates transient pointer state");
    require(owner.resolve_control_style_part(StylePart::row, style_states::hovered).foreground == root_disabled.foreground,
        "Inherited root rules preserve disabled context");
    PartStyleValues local; local.foreground = ThemeColor{0};
    owner.set_control_style_values(StylePart::row, local);
    require(owner.resolve_control_style_part(StylePart::row, 0).foreground == local.foreground,
        "Transient local foreground overrides inheritance");
    require(owner.resolve_control_style_part(StylePart::cell, 0).font_family == family, "Font ownership is shared");
    {
        Probe probe;
        for (unsigned i = 0; i < 4096; ++i) {
            owner.state(i & 1 ? style_states::disabled : style_states::hovered);
            auto values = owner.resolve_control_style_part(StylePart::cell, i & 1 ? style_states::selected : 0);
            require(values.font_family == family, "Transient resolution keeps immutable font ownership");
        }
        require(allocation_probe::calls == 0 && allocation_probe::bytes == 0,
            "Warmed state and transient font resolution allocate no memory");
    }
    rejects([&] { owner.resolve_control_style_part(StylePart::cell, 1ull << 63); });
    rejects([] { make_style_font_family(""); });
    rejects([] { make_style_font_family(std::string_view("A\0B", 3)); });
    rejects([] { make_style_font_family("\xc0\xaf"); });
    rejects([] { make_style_font_family("\xed\xa0\x80"); });
    require(make_style_font_family("A\xf0\x9f\x8e\xa8")->name.size() >= 2, "Valid Unicode survives immutable font construction");
}
void retained_child_projection() {
    StyledElement child(StyleTarget::label);
    PartStyleValues projected; projected.foreground = ThemeColor{1}; projected.font_size = 18;
    PartStyleValues authored; authored.foreground = ThemeColor{2}; authored.font_size = 22;
    PartStyleValues local; local.foreground = ThemeColor{3};
    child.set_control_style_projection(StylePart::root, projected);
    child.set_control_style(ControlStyle::create(StyleTarget::label, {{StylePart::root, authored}}, {}));
    child.set_control_style_values(StylePart::root, local);
    require(child.effective_control_style_values(StylePart::root)->foreground == local.foreground,
        "Parent projection cannot replace explicit child locals");
    require(child.effective_control_style_values(StylePart::root)->font_size == authored.font_size,
        "Child style overrides parent projection defaults");
    require(child.own_control_style_values(StylePart::root).font_size == authored.font_size,
        "Legacy adapters can resolve the child's own layer separately");
    {
        Probe probe;
        for (unsigned i = 0; i < 1024; ++i) {
            projected.foreground = ThemeColor{i & 1 ? 4u : 5u};
            child.set_control_style_projection(StylePart::root, projected);
        }
        require(allocation_probe::calls == 0, "Warmed retained-child projection changes allocate no memory");
    }
    PartStyleValues invalid; invalid.size = 99;
    rejects([&] { child.set_control_style_projection(StylePart::root, invalid); });
    require(child.control_style_projection_values(StylePart::root).font_size == 18,
        "A failed projection preserves the existing parent layer");
    child.set_control_style_projection(StylePart::root, {});
    require(child.control_style_values(StylePart::root).foreground == local.foreground &&
        child.effective_control_style_values(StylePart::root)->font_size == authored.font_size,
        "Removing a parent layer preserves child styles and locals");
    child.set_control_style(nullptr);
    child.set_control_style_values(StylePart::root, {});
    require(!child.has_control_styling(), "All cleared layers release the shared attachment");
}
void typography_inheritance() {
    StyledElement label(StyleTarget::label);
    PartStyleValues projected;
    projected.font_family = make_style_font_family("Segoe UI");
    projected.font_size = 18.0f;
    projected.font_weight = 400;
    projected.font_style = StyleFontStyle::italic;
    projected.horizontal_alignment = StyleAlignment::end;
    projected.vertical_alignment = StyleAlignment::center;
    projected.maximum_lines = 2;
    projected.wrapping = true;
    label.set_control_style_projection(StylePart::root, projected);
    PartStyleValues local; local.font_weight = 700;
    label.set_control_style_values(StylePart::label, local);
    const auto* caption = label.effective_control_style_values(StylePart::caption);
    require(caption && caption->font_family == projected.font_family && caption->font_size == 18 &&
        caption->font_weight == 700 && caption->font_style == StyleFontStyle::italic,
        "Schema font inheritance follows multiple parts and preserves explicit destination fields");
    require(caption->horizontal_alignment == StyleAlignment::end &&
        caption->vertical_alignment == StyleAlignment::center && caption->maximum_lines == 2 && caption->wrapping == true,
        "Text presentation follows the same multi-part inheritance chain");
    auto own = label.own_control_style_values(StylePart::caption);
    require(!own.font_family && !own.font_size && own.font_weight == 700 &&
        !own.horizontal_alignment && !own.vertical_alignment && !own.maximum_lines && !own.wrapping,
        "Own-value inheritance excludes projected font defaults");
    PartStyleValues base; base.font_size = 20.0f;
    PartStyleValues disabled; disabled.font_size = 24.0f;
    disabled.horizontal_alignment = StyleAlignment::center;
    disabled.maximum_lines = 4;
    disabled.wrapping = false;
    label.set_control_style(ControlStyle::create(StyleTarget::label, {{StylePart::root, base}},
        {{StylePart::root, style_states::disabled, disabled}}));
    {
        Probe probe;
        for (unsigned i = 0; i < 1024; ++i) {
            const auto state = i & 1 ? style_states::disabled : 0;
            label.state(state);
            const auto cached = label.effective_control_style_values(StylePart::caption);
            const auto transient = label.resolve_control_style_part(StylePart::caption, 0);
            require(cached && part_style_values_equal(*cached, transient) &&
                transient.font_size == (state ? 24 : 20) && transient.font_weight == 700 &&
                transient.horizontal_alignment == (state ? StyleAlignment::center : StyleAlignment::end) &&
                transient.maximum_lines == (state ? 4u : 2u) && transient.wrapping == !bool(state),
                "Cached and transient font inheritance share root state without replacing part locals");
        }
        require(allocation_probe::calls == 0, "Warmed inherited-font state changes allocate no memory");
    }
    label.set_control_style(nullptr);
    require(label.effective_control_style_values(StylePart::caption)->font_size == 18,
        "Removing an own style reveals inherited parent typography");
    PartStyleValues caption_local;
    caption_local.horizontal_alignment = StyleAlignment::start;
    caption_local.maximum_lines = 0;
    caption_local.wrapping = false;
    label.set_control_style_values(StylePart::caption, caption_local);
    const auto overridden = label.resolve_control_style_part(StylePart::caption, 0);
    require(overridden.horizontal_alignment == StyleAlignment::start && overridden.maximum_lines == 0 &&
        overridden.wrapping == false && overridden.font_size == 18 &&
        part_style_values_equal(overridden, *label.effective_control_style_values(StylePart::caption)),
        "Explicit zero, false, and start values override inherited text presentation");
    label.set_control_style_values(StylePart::caption, {});
    require(label.resolve_control_style_part(StylePart::caption, 0).maximum_lines == 2,
        "Clearing text presentation locals restores inherited defaults");
    label.set_control_style_projection(StylePart::root, {});
    require(!label.effective_control_style_values(StylePart::caption)->font_family &&
        label.effective_control_style_values(StylePart::caption)->font_weight == 700 &&
        !label.effective_control_style_values(StylePart::caption)->horizontal_alignment &&
        !label.effective_control_style_values(StylePart::caption)->wrapping,
        "Clearing inherited projection removes fonts without replacing part locals");
}
void positive_row_height() {
    StyledElement grid(StyleTarget::data_grid);
    PartStyleValues valid; valid.row_height = 24.0f;
    grid.set_control_style_values(StylePart::root, valid);
    for (const auto height : {0.0f, -0.0f, -1.0f, std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(), 32769.0f}) {
        PartStyleValues invalid; invalid.row_height = height;
        rejects([&] { ControlStyle::create(StyleTarget::data_grid, {{StylePart::root, invalid}}, {}); });
        rejects([&] { grid.set_control_style_values(StylePart::root, invalid); });
        require(grid.effective_control_style_values(StylePart::root)->row_height == 24,
            "Rejected row heights preserve geometry before layout or division");
    }
    for (const auto height : {0.5f, 32768.0f}) {
        valid.row_height = height;
        validate_part_values(StyleTarget::data_grid, StylePart::root, valid);
    }
}
void native_font_limits() {
    for (const auto target : {StyleTarget::text_input, StyleTarget::multiline_text, StyleTarget::rich_text,
        StyleTarget::password_input, StyleTarget::date_time_picker}) {
        StyledElement field(target);
        PartStyleValues valid;
        valid.font_family = make_style_font_family(std::string(31, 'A'));
        valid.font_size = 512.0f;
        valid.font_style = StyleFontStyle::italic;
        field.set_control_style_values(StylePart::text, valid);
        auto invalid = valid;
        invalid.font_size = 513.0f;
        rejects([&] { ControlStyle::create(target, {{StylePart::text, invalid}}, {}); });
        rejects([&] { field.set_control_style_values(StylePart::text, invalid); });
        require(field.effective_control_style_values(StylePart::text)->font_size == 512,
            "Font limits reject a mutation before native font creation");
        invalid = valid;
        invalid.font_family = make_style_font_family(std::string(32, 'A'));
        rejects([&] { validate_part_values(target, StylePart::text, invalid); });
        invalid = valid;
        invalid.font_style = StyleFontStyle::oblique;
        rejects([&] { validate_part_values(target, StylePart::text, invalid); });
        std::string supplementary;
        for (unsigned i = 0; i < 15; ++i) supplementary += "\xf0\x9f\x8e\xa8";
        valid.font_family = make_style_font_family(supplementary + "A");
        validate_part_values(target, StylePart::text, valid);
        valid.font_family = make_style_font_family(supplementary + "\xf0\x9f\x8e\xa8");
        rejects([&] { validate_part_values(target, StylePart::text, valid); });
    }
}
void alignment_limits() {
    StyledElement label(StyleTarget::label);
    PartStyleValues valid; valid.vertical_alignment = StyleAlignment::center;
    label.set_control_style_values(StylePart::root, valid);
    auto invalid = valid; invalid.vertical_alignment = StyleAlignment::stretch;
    rejects([&] { ControlStyle::create(StyleTarget::label, {{StylePart::root, invalid}}, {}); });
    rejects([&] { label.set_control_style_values(StylePart::root, invalid); });
    require(label.effective_control_style_values(StylePart::root)->vertical_alignment == StyleAlignment::center,
        "Unsupported text alignment preserves existing values");
    for (const auto& part : control_style_schema(StyleTarget::stack).parts) {
        if (part.allowed & style_property(StyleProperty::vertical_alignment)) {
            validate_part_values(StyleTarget::stack, part.part, invalid);
            return;
        }
    }
    throw std::runtime_error("Stack must expose its supported vertical alignment");
}
void default_storage() {
    std::size_t element_bytes{}, element_calls{};
    { Probe probe; Element element; element_bytes = allocation_probe::bytes; element_calls = allocation_probe::calls; }
    { Probe probe; Label label(L"");
        require(allocation_probe::calls == element_calls && allocation_probe::bytes == element_bytes,
            "An unstyled Control adds no style allocation to Element construction"); }
    std::cout << "Default Element allocation baseline=" << element_bytes << " bytes/" << element_calls
        << " calls; sizeof Element=" << sizeof(Element) << " Control=" << sizeof(Control)
        << " PartStyleValues=" << sizeof(PartStyleValues) << '\n';
}
void abi_callback_storage() {
    namespace callbacks = xui::detail::callbacks;
    {
        Probe probe;
        for (unsigned i = 0; i < 1024; ++i) {
            callbacks::Storage storage;
            storage.capture(callbacks::Action{});
            storage.capture(callbacks::Input{});
            storage.capture(callbacks::Range{});
            storage.capture(callbacks::Number{});
            storage.capture(callbacks::Selection{});
            storage.capture(callbacks::Dismiss{});
            require(!storage.get<callbacks::Action>() && !storage.get<callbacks::Input>(),
                "Empty native callbacks need no preservation object");
        }
        require(allocation_probe::calls == 0 && allocation_probe::bytes == 0,
            "Ordinary ABI nodes and empty native callbacks allocate no preservation storage");
    }
    unsigned calls{};
    callbacks::Storage storage;
    {
        Probe probe;
        storage.capture(callbacks::Action{[&] { ++calls; }});
        require(allocation_probe::calls == 1 && allocation_probe::bytes == sizeof(callbacks::Action),
            "An original action allocates only its own callback record");
    }
    (*storage.get<callbacks::Action>())();
    {
        Probe probe;
        storage.capture(callbacks::Input{[&](const std::wstring&) { ++calls; }, [&] { ++calls; }});
        require(allocation_probe::calls == 1 && allocation_probe::bytes == sizeof(callbacks::Input),
            "Input preservation allocates only change and submit callbacks");
    }
    require(!storage.get<callbacks::Action>(), "Callback storage retains only the actual native kind");
    storage.get<callbacks::Input>()->change(L"text");
    storage.get<callbacks::Input>()->submit();
    {
        Probe probe;
        storage.capture(callbacks::Range{[&](double) { ++calls; }, [&](double) { ++calls; }, [&](double) { ++calls; }});
        require(allocation_probe::calls == 1 && allocation_probe::bytes == sizeof(callbacks::Range),
            "Range preservation allocates only its three native callbacks");
    }
    storage.get<callbacks::Range>()->change(1);
    storage.get<callbacks::Range>()->preview(2);
    storage.get<callbacks::Range>()->cancel(3);
    storage.capture(callbacks::Number{[&](double) { ++calls; }});
    (*storage.get<callbacks::Number>())(4);
    storage.capture(callbacks::Selection{[&](uint64_t) { ++calls; }});
    (*storage.get<callbacks::Selection>())(5);
    storage.capture(callbacks::Dismiss{[&](PopupDismissReason) { ++calls; }});
    (*storage.get<callbacks::Dismiss>())(static_cast<PopupDismissReason>(0));
    require(calls == 9, "Typed optional storage preserves each original callback exactly once");
    std::weak_ptr<unsigned> lifetime;
    {
        auto owner = std::make_shared<unsigned>();
        lifetime = owner;
        callbacks::Storage owned;
        owned.capture(callbacks::Action{[owner] { ++*owner; }});
        owner.reset();
        require(!lifetime.expired(), "Callback storage retains its native callback owner");
    }
    require(lifetime.expired(), "Callback storage releases its native callback owner");
    std::cout << "ABI callback storage=" << sizeof(callbacks::Storage)
        << " bytes; empty captures=0 allocations\n";
}
}
int main() {
    try {
        identifiers_and_schema(); element_lifecycle(); transient_states_and_fonts(); retained_child_projection();
        typography_inheritance(); positive_row_height(); native_font_limits(); alignment_limits(); default_storage(); abi_callback_storage();
        std::cout << "PASS common control style expansion\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
