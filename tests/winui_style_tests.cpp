#include "xui/application.hpp"
#include "xui/documents.hpp"
#include "xui/navigation.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace xui;
namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
double luminance(uint32_t color) {
    const auto channel = [](uint32_t value) {
        const double c = value / 255.0;
        return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * channel((color >> 16) & 255) +
        0.7152 * channel((color >> 8) & 255) + 0.0722 * channel(color & 255);
}
double contrast(uint32_t a, uint32_t b) {
    const auto first = luminance(a), second = luminance(b);
    return (std::max(first, second) + 0.05) / (std::min(first, second) + 0.05);
}
void tokens() {
    require(winui_card_brushes(ThemeMode::light).fill == 0xb3ffffff &&
        winui_card_brushes(ThemeMode::light).secondary_fill == 0x80f6f6f6 &&
        winui_card_brushes(ThemeMode::dark).fill == 0x0dffffff &&
        winui_card_brushes(ThemeMode::dark).secondary_fill == 0x08ffffff,
        "WinUI card header and content fills retain distinct alpha resources");
    require(winui_focus_strokes(ThemeMode::light).outer == 0xe4000000 &&
        winui_focus_strokes(ThemeMode::light).inner == 0xb3ffffff &&
        winui_focus_strokes(ThemeMode::dark).outer == 0xffffffff &&
        winui_focus_strokes(ThemeMode::dark).inner == 0xb3000000,
        "WinUI focus brushes preserve their separate primary and translucent secondary resources");
    require(WindowOptions{}.visual_style == VisualStyle::classic, "Existing windows keep the classic style");
    require(theme_colors(ThemeMode::dark).background == 0x15181b &&
        theme_colors(ThemeMode::light).accent == 0x00718d, "Classic palette remains unchanged");
    require(style_metrics(VisualStyle::classic).control_radius == 6 &&
        style_metrics(VisualStyle::classic).progress_thickness == 6, "Classic geometry remains unchanged");
    require(style_metrics(VisualStyle::winui).control_radius == 4 &&
        style_metrics(VisualStyle::winui).surface_radius == 8 &&
        style_metrics(VisualStyle::winui).progress_thickness == 4, "WinUI distinguishes control and surface geometry");
    for (const auto mode : {ThemeMode::light, ThemeMode::dark}) {
        const auto colors = theme_colors(mode, VisualStyle::winui);
        require(colors.background != colors.surface, "Window and content surfaces remain distinct");
        require(contrast(colors.text, colors.surface) >= 4.5 &&
            contrast(colors.secondary, colors.surface) >= 4.5, "Content text meets normal-text contrast");
        require(contrast(colors.selection_text, colors.selection) >= 4.5, "Selected rows retain readable text");
        const auto status = winui_status_colors(mode);
        for (const auto fill : {status.information_fill, status.success_fill, status.warning_fill, status.error_fill})
            require(contrast(colors.text, fill) >= 4.5, "Status messages meet normal-text contrast");
        require(contrast(status.success, status.success_fill) >= 3 &&
            contrast(status.warning, status.warning_fill) >= 3 &&
            contrast(colors.error, status.error_fill) >= 3 &&
            contrast(colors.accent, status.information_fill) >= 3, "Status indicators remain visible on their surfaces");
        for (const auto appearance : {ButtonAppearance::standard, ButtonAppearance::accent, ButtonAppearance::subtle})
            for (const bool hover : {false, true})
                for (const bool pressed : {false, true})
                    for (const bool checked : {false, true}) {
                        const auto visual = winui_button_visual(mode, appearance, true, hover, pressed, checked);
                        if (!pressed || (appearance != ButtonAppearance::accent && !checked))
                            require(contrast(visual.text, visual.fill_visible ? visual.fill : colors.surface) >= 4.5,
                                "Normal and hover button states meet normal-text contrast");
                        else
                            require(winui_button_brushes(mode, appearance, true, hover, pressed, checked).text ==
                                (mode == ThemeMode::light ? 0xb3ffffff : 0x80000000),
                                "Pressed accent text uses WinUI's deliberately translucent secondary foreground");
                        if (!visual.fill_visible)
                            require(contrast(visual.text, colors.background) >= 4.5, "Subtle button text works on window surfaces");
                        const auto disabled = winui_button_visual(mode, appearance, false, hover, pressed, checked);
                        const auto idle_disabled = winui_button_visual(mode, appearance, false, false, false, checked);
                        require(disabled.fill == idle_disabled.fill && disabled.text == idle_disabled.text &&
                            disabled.stroke == idle_disabled.stroke, "Disabled state overrides hover and press without discarding checked state");
                    }
        const auto accent = winui_button_visual(mode, ButtonAppearance::accent, true, false, false, false);
        const auto pressed = winui_button_visual(mode, ButtonAppearance::accent, true, true, true, false);
        require(accent.fill != pressed.fill, "Accent button has a distinct pressed state");
        const auto subtle = winui_button_visual(mode, ButtonAppearance::subtle, true, false, false, false);
        require(!subtle.fill_visible && !subtle.stroke_visible, "Idle subtle buttons have no frame");
        const bool light = mode == ThemeMode::light;
        for (const bool enabled : {false, true})
            for (const bool hover : {false, true})
                for (const bool pressed : {false, true}) {
                    const auto slider = winui_slider_brushes(mode, enabled, hover, pressed);
                    require(slider.track == (enabled ? (light ? 0x72000000u : 0x8bffffffu) : (light ? 0x51000000u : 0x3fffffffu)) &&
                        slider.thumb == (light ? 0xffffffffu : 0xff454545u),
                        "Slider uses the strong translucent track and solid thumb resources");
                    const auto accent = light ? 0x005fb8u : 0x60cdffu;
                    require(slider.value == (!enabled ? (light ? 0x37000000u : 0x28ffffffu) :
                        (pressed ? 0xcc000000u : hover ? 0xe6000000u : 0xff000000u) | accent),
                        "Slider value and inner thumb retain native interaction alpha");
                }
        const auto normal = winui_button_brushes(mode, ButtonAppearance::standard, true, false, false, false);
        const auto down = winui_button_brushes(mode, ButtonAppearance::standard, true, true, true, false);
        require(normal.fill == (light ? 0xb3ffffff : 0x0fffffff) &&
            normal.stroke == (light ? 0x0f000000 : 0x12ffffff) &&
            normal.elevation == (light ? 0x29000000 : 0x18ffffff) && normal.elevated &&
            down.fill == (light ? 0x4df9f9f9 : 0x08ffffff) && !down.elevated,
            "Standard buttons retain the exact translucent fill and elevation stops");
        const auto subtle_hover = winui_button_brushes(mode, ButtonAppearance::subtle, true, true, false, false);
        const auto accent_disabled = winui_button_brushes(mode, ButtonAppearance::accent, false, true, true, false);
        require(subtle_hover.fill == (light ? 0x09000000 : 0x0fffffff) && subtle_hover.stroke == subtle_hover.fill &&
            accent_disabled.fill == (light ? 0x37000000 : 0x28ffffff) && !accent_disabled.stroke &&
            accent_disabled.text == (light ? 0xffffffff : 0x87ffffff),
            "Subtle hover is translucent; disabled accent buttons retain their distinct brush family");
        for (const auto appearance : {ButtonAppearance::standard, ButtonAppearance::accent, ButtonAppearance::subtle}) {
            const auto checked_disabled = winui_button_brushes(mode, appearance, false, true, true, true);
            require(checked_disabled.fill == (light ? 0x37000000u : 0x28ffffffu) &&
                checked_disabled.text == (light ? 0xffffffffu : 0x87ffffffu) &&
                checked_disabled.stroke == 0 && checked_disabled.accent && !checked_disabled.elevated,
                "Checked disabled buttons retain the native accent-disabled fill, text, and transparent border");
        }
        for (const bool marked : {false, true}) {
            const auto idle = winui_indicator_brushes(mode, marked, true, false, false);
            const auto hover = winui_indicator_brushes(mode, marked, true, true, false);
            const auto press = winui_indicator_brushes(mode, marked, true, true, true);
            const auto disabled = winui_indicator_brushes(mode, marked, false, true, true);
            require(idle.fill != hover.fill && hover.fill != press.fill,
                "Check and radio indicators have distinct fill feedback for each interaction state");
            require(disabled.fill == (marked ? (light ? 0x37000000 : 0x28ffffff) : 0x00ffffff) &&
                disabled.stroke == (light ? 0x37000000 : 0x28ffffff),
                "Disabled indicator brushes override hover and press");
            require(idle.text == hover.text && hover.text == press.text,
                "Checkbox and radio labels do not dim during a press");
            if (!marked) require(press.stroke == disabled.stroke,
                "An unchecked press weakens the outline instead of adding an accent");
        }
        require(winui_indicator_brushes(mode, true, false, false, false, true).mark ==
            (light ? 0xffffffff : 0xff000000) &&
            winui_indicator_brushes(mode, true, false, false, false).mark == (light ? 0xffffffff : 0x87ffffff),
            "Disabled radio and checkbox marks use different template foregrounds");
        require(winui_radio_dot(false, true, true) == 14 && winui_radio_dot(true, false, false) == 12 &&
            winui_radio_dot(true, true, false) == 14 && winui_radio_dot(true, true, true) == 10,
            "Radio center diameter follows disabled, normal, hover and pressed template states");
    }
}
void button_appearance() {
    Button button(L"Save");
    const auto identity = button.id();
    int paints{}, layouts{}, clicks{};
    button.set_invalidator([&](Invalidation kind) { kind == Invalidation::paint ? ++paints : ++layouts; });
    button.on_click([&] { ++clicks; });
    require(button.appearance() == ButtonAppearance::standard, "Buttons default to standard appearance");
    button.set_appearance(ButtonAppearance::accent);
    button.set_appearance(ButtonAppearance::accent);
    require(paints == 1 && layouts == 0 && clicks == 0, "Appearance changes only repaint and no-ops do nothing");
    require(button.id() == identity && button.behavior() == ButtonBehavior::momentary, "Appearance preserves identity and behavior");
    button.set_focused(true);
    button.key_down(ActivationKey::space);
    button.set_appearance(ButtonAppearance::subtle);
    require(button.pressed() && button.key_up(ActivationKey::space) && clicks == 1, "Appearance preserves an active keyboard gesture");
    bool rejected{};
    try { button.set_appearance(static_cast<ButtonAppearance>(99)); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected && button.appearance() == ButtonAppearance::subtle, "Invalid appearance is rejected without mutation");
}
void composite_appearances() {
    NavigationView navigation(L"Explore");
    const auto title = std::dynamic_pointer_cast<Label>(navigation.retained_children()[1]);
    require(title && title->text_style() == TextStyle::heading, "Classic navigation retains its original title typography");
    navigation.set_visual_style(VisualStyle::winui);
    require(title->text_style() == TextStyle::body_strong &&
        navigation.toggle_button()->appearance() == ButtonAppearance::subtle,
        "WinUI navigation uses a 14-DIP semibold pane title and unframed toggle");
    navigation.set_visual_style(VisualStyle::classic);
    require(title->text_style() == TextStyle::heading, "Style reversal restores the same retained navigation title");
    require(winui_input_strokes(ThemeMode::light).outline == 0x0f000000 &&
        winui_input_strokes(ThemeMode::light).elevation == 0x72000000 &&
        winui_input_strokes(ThemeMode::dark).outline == 0x12ffffff &&
        winui_input_strokes(ThemeMode::dark).elevation == 0x8bffffff,
        "Input outlines and bottom elevation preserve the pinned stroke alpha");
    require(winui_input_background(ThemeMode::light, true, false, false) == 0xb3ffffff &&
        winui_input_background(ThemeMode::light, true, false, true) == 0x80f9f9f9 &&
        winui_input_background(ThemeMode::light, true, true, true) == 0xffffffff &&
        winui_input_background(ThemeMode::light, false, true, true) == 0x4df9f9f9,
        "Light input resources preserve alpha and state precedence");
    require(composite_argb_on_rgb(winui_input_background(ThemeMode::dark, true, false, false), 0x272727) == 0x343434 &&
        composite_argb_on_rgb(winui_input_background(ThemeMode::dark, true, false, true), 0x272727) == 0x393939 &&
        composite_argb_on_rgb(winui_input_background(ThemeMode::dark, true, true, true), 0x272727) == 0x212121 &&
        composite_argb_on_rgb(winui_input_background(ThemeMode::dark, false, true, true), 0x272727) == 0x303030,
        "Dark input states composite once over the actual card surface");
    require(composite_argb_on_rgb(0xb3ffffff, 0xf3f3f3) == 0xfbfbfb &&
        composite_argb_on_rgb(0xb3ffffff, 0xffffff) == 0xffffff,
        "The same translucent input brush resolves differently over different parents");
    ContentDialog dialog(L"Confirm", std::make_shared<Label>(L"Continue?"));
    require(dialog.primary()->appearance() == ButtonAppearance::accent &&
        dialog.cancel_button()->appearance() == ButtonAppearance::standard,
        "Dialog primary and secondary actions use distinct WinUI appearances");
    dialog.primary()->set_appearance(ButtonAppearance::standard);
    require(dialog.primary()->appearance() == ButtonAppearance::standard, "Applications can override dialog button appearance");
    InlineStatus status(L"Saved");
    status.set_dismissible(true);
    const auto dismiss = std::dynamic_pointer_cast<Button>(status.retained_children().back());
    require(dismiss && dismiss->appearance() == ButtonAppearance::subtle, "Status dismiss action uses subtle appearance");
}
void presentation_measurement() {
    const Size available{1000, 1000};
    Label body(L"Field header");
    body.set_text_measurer([](std::wstring_view, TextStyle) { return Size{80, 19}; });
    require(body.measure(available).height == 24, "Classic labels retain their original minimum line box");
    body.set_visual_style(VisualStyle::winui);
    require(body.measure(available).height == 19, "WinUI labels use the measured text line instead of a Classic height floor");
    Button button(L"Save");
    button.set_text_measurer([](std::wstring_view, TextStyle) { return Size{28, 19}; });
    require(!button.preferred_size_explicit(), "Constructor sizes are style defaults, not application constraints");
    require(button.measure(available).height == 36, "Classic automatic button remains 36 DIPs high");
    const auto identity = button.id();
    int layouts{}, clicks{};
    button.set_invalidator([&](Invalidation kind) { if (kind == Invalidation::layout) ++layouts; });
    button.on_click([&] { ++clicks; });
    button.set_focused(true);
    button.key_down(ActivationKey::space);
    button.set_visual_style(VisualStyle::winui);
    const auto first = layouts;
    button.set_visual_style(VisualStyle::winui);
    require(first > 0 && first == layouts, "Style changes layout; repeated style is a no-op");
    const auto natural = button.measure(available);
    require(natural.width == 52 && natural.height == 32, "WinUI Button uses 32-DIP height and 24-DIP horizontal chrome");
    const auto content = button.content_bounds({0, 0, natural.width, natural.height});
    require(content.x == 12 && content.y == 6 && content.width == 28 && content.height == 19,
        "WinUI Button content subtracts template padding and its border exactly once");
    for (const bool named : {false, true}) {
        HyperlinkButton link(L"Natural link");
        link.set_visual_style(VisualStyle::winui);
        link.set_text_measurer([](std::wstring_view, TextStyle) { return Size{87, 19}; });
        if (named) {
            PartStyleValues shape;
            shape.corner_radius = 16.0f;
            link.set_control_style_values(StylePart::root, shape);
        }
        const auto size = link.measure(available);
        const auto text = link.content_bounds({0, 0, size.width, size.height});
        require(text.width >= 87 && text.height >= 19,
            "Natural-width hyperlinks retain the complete measured text with default and rounded styles");
        ButtonStyleValues border;
        border.border_thickness = Insets{};
        link.set_style_values(border);
        const auto borderless = link.measure(available);
        const auto borderless_text = link.content_bounds({0, 0, borderless.width, borderless.height});
        require(borderless.width == 109 && borderless_text.width == 87,
            "Explicit border removal retains native padding without an extra hidden border allowance");
    }
    const auto check_natural_label = [&](Toggle& choice) {
        choice.set_visual_style(VisualStyle::winui);
        choice.set_text_measurer([](std::wstring_view, TextStyle) { return Size{87, 19}; });
        for (const bool rounded : {false, true}) {
            if (rounded) {
                PartStyleValues shape;
                shape.corner_radius = 16.0f;
                choice.set_control_style_values(StylePart::root, shape);
            }
            const auto size = choice.measure(available);
            const auto text = choice.label_bounds({0, 0, size.width, size.height});
            require(size.width == 115 && text.x == 28 && text.width == 87 && text.height >= 19,
                "Natural-width WinUI checkboxes retain the full label with default and rounded root styles");
        }
        choice.set_visual_style(VisualStyle::classic);
        auto size = choice.measure(available);
        require(size.width == 141 && choice.label_bounds({0, 0, size.width, size.height}).width == 87,
            "Classic checkbox chrome and trailing padding remain unchanged");
        choice.set_visual_style(VisualStyle::winui);
        PartStyleValues custom;
        custom.padding = Insets{1, 2, 3, 4};
        choice.set_control_style_values(StylePart::root, custom);
        size = choice.measure(available);
        const auto text = choice.label_bounds({0, 0, size.width, size.height});
        require(size.width == 119 && text.x == 29 && text.width == 87,
            "Explicit checkbox padding replaces the WinUI default without clipping its label");
    };
    Toggle binary(L"Natural toggle");
    CheckBox check(L"Natural checkbox");
    check_natural_label(binary);
    check_natural_label(check);
    Button tall_text(L"Large text");
    tall_text.set_visual_style(VisualStyle::winui);
    tall_text.set_text_measurer([](std::wstring_view, TextStyle) { return Size{80, 30}; });
    require(tall_text.measure(available).height == 43, "WinUI button measurement includes asymmetric padding and both borders");
    require(button.id() == identity && button.pressed() && clicks == 0 &&
        button.key_up(ActivationKey::space) && clicks == 1, "Style relayout retains identity and active gesture");
    button.set_behavior(ButtonBehavior::dropdown);
    require(button.measure(available).width == 72, "Dropdown measurement reserves space for its chevron");
    button.set_preferred_size({111, 47});
    button.set_visual_style(VisualStyle::classic);
    button.set_visual_style(VisualStyle::winui);
    require(button.measure(available).width == 111 && button.measure(available).height == 47,
        "Application preferred dimensions survive style changes");
    Button headless(L"Save");
    headless.set_visual_style(VisualStyle::winui);
    require(headless.measure(available).height == 32, "Headless default measurement uses the same WinUI height");
    Button explicitly_default(L"Explicit default");
    explicitly_default.set_preferred_size({240, 40});
    explicitly_default.set_visual_style(VisualStyle::winui);
    require(explicitly_default.measure(available).height == 40,
        "Explicitly setting the constructor's size still counts as an application constraint");
    TextInput explicitly_default_input(L"Explicit");
    explicitly_default_input.set_visual_style(VisualStyle::winui);
    int explicit_layouts{};
    explicitly_default_input.set_invalidator([&](Invalidation kind) { if (kind == Invalidation::layout) ++explicit_layouts; });
    explicitly_default_input.set_preferred_size({320, 68});
    require(explicit_layouts == 1 && explicitly_default_input.measure(available).height == 68,
        "An explicit constructor-equivalent size invalidates the active style's different default geometry");

    TextInput input(L"Name");
    input.set_text(L"Retained");
    require(input.measure(available).height == 68, "Classic captioned input keeps its original size");
    input.set_visual_style(VisualStyle::winui);
    require(input.measure(available).height == 60 && input.caption_extent() == 28,
        "WinUI TextBox has a 20-DIP header, eight-DIP gap, and 32-DIP field");
    input.set_caption_visible(false);
    require(input.measure(available).height == 32 && input.caption_extent() == 0,
        "A captionless WinUI field has no phantom header");
    input.set_fixed_size({200, 44});
    input.set_visual_style(VisualStyle::classic);
    input.set_visual_style(VisualStyle::winui);
    require(input.text() == L"Retained" && input.measure(available).height == 44,
        "Native input model and explicit height survive switching");
    input.set_visible(false);
    require(input.measure(available).height == 0, "Invisible input does not occupy layout space");
    bool rejected{};
    try { input.set_visual_style(static_cast<VisualStyle>(99)); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected && input.visual_style() == VisualStyle::winui, "Invalid style does not mutate the presentation");
}
void wrapping_measurement() {
    Label title(L"A long dialog title");
    title.set_text_measurer([](std::wstring_view, TextStyle) { return Size{300, 27}; });
    int measures{};
    title.set_wrapped_text_measurer([&](std::wstring_view, TextStyle style, float width, std::size_t lines) {
        require(style == TextStyle::subtitle && lines == 2, "Wrapped title retains its typography and line limit");
        ++measures;
        return Size{std::min(width, 300.0f), width < 300 ? 54.0f : 27.0f};
    });
    title.set_subtitle(true);
    title.set_wrapping(true, 2);
    require(title.measure({160, 100}).height == 54, "A narrow title measures two full lines");
    require(title.measure({160, 100}).height == 54 && measures == 1, "Stable wrapped measurement is cached");
    title.set_text(L"An updated title");
    title.measure({160, 100});
    require(measures == 2, "Changed text invalidates the wrapped measurement");
    require(title.measure({400, 100}).height == 27 && measures == 3, "Wider layout reflows the title");
    title.discard_wrapped_text();
    title.measure({400, 100});
    require(measures == 4, "Hidden native text layouts can be rebuilt without stale measurement caches");
    title.set_wrapping(false);
    require(title.measure({160, 100}).height == 27, "Classic single-line layout remains available");
    title.set_wrapped_text_measurer({});
    title.set_wrapping(true, 2);
    require(title.measure({160, 100}).height == 27, "Detached labels do not retain native measurer callbacks");
}
void scroll_presentation() {
    auto content = std::make_shared<Label>(L"Content");
    content->set_preferred_size({240, 600});
    ScrollView scroll(content);
    scroll.set_tab_stop(false);
    require(!scroll.tab_stop() && scroll.focusable(), "A scrolling wrapper can skip Tab without disabling its descendants or programmatic focus");
    scroll.set_overlay_scrollbar(true);
    scroll.arrange({10, 20, 200, 180});
    require(scroll.viewport().width == 200 && content->bounds().width == 200 &&
        scroll.maximum_offset() == 420, "Overlay scrolling retains the full content width");
    scroll.set_offset(160);
    scroll.arrange({10, 20, 200, 180});
    require(content->bounds().y == -140 && scroll.thumb().height > 0, "Overflow content and scrollbar move together");
    scroll.set_passthrough(true);
    scroll.arrange({10, 20, 200, 180});
    require(content->bounds().y == 20 && content->bounds().height == 180 &&
        scroll.maximum_offset() == 0 && scroll.thumb().height == 0, "Passthrough preserves Classic geometry without a scrollbar");
    scroll.set_passthrough(false);
    scroll.arrange({10, 20, 200, 180});
    require(scroll.offset() == 160, "Returning to the scrollable presentation preserves the previous scroll anchor");
}
void slider_geometry() {
    for (const auto axis : {Axis::horizontal, Axis::vertical})
        for (const bool reversed : {false, true})
            for (const double fraction : {0.0, 0.25, 1.0}) {
                const bool vertical = axis == Axis::vertical;
                const Size size = vertical ? Size{40, 224} : Size{224, 40};
                const auto visual = slider_visual(size, axis, reversed, fraction, VisualStyle::winui);
                const auto classic = slider_visual(size, axis, reversed, fraction, VisualStyle::classic);
                require((vertical ? visual.track.y : visual.track.x) == 0 &&
                    (vertical ? visual.track.height : visual.track.width) == 224,
                    "WinUI rail spans the content instead of stopping at thumb centers");
                const Point center{visual.thumb.x + visual.thumb.width / 2, visual.thumb.y + visual.thumb.height / 2};
                const Point classic_center{classic.thumb.x + classic.thumb.width / 2, classic.thumb.y + classic.thumb.height / 2};
                const double physical_fraction = reversed ? 1 - fraction : fraction;
                require(std::abs(visual.pointer_fraction(center, axis) - physical_fraction) < 1e-6 &&
                    std::abs(classic.pointer_fraction(classic_center, axis) - physical_fraction) < 1e-6,
                    "Both styles map each painted thumb center back to its value");
                require((vertical ? center.y : center.x) ==
                    9 + static_cast<float>(vertical ? 1 - physical_fraction : physical_fraction) * 206,
                    "WinUI travel uses the native eighteen-DIP thumb layout box");
                require((vertical ? center.x : center.y) == 16 &&
                    (vertical ? classic_center.x : classic_center.y) == 20,
                    "WinUI uses its leading 32-DIP cross-axis slot while Classic stays centered");
                const auto length = vertical ? visual.filled.height : visual.filled.width;
                const auto start = vertical ? visual.filled.y : visual.filled.x;
                require(length == 206 * fraction, "Filled track length uses the space outside the native thumb layout box");
                require(start == (vertical == reversed ? 0.0f : 224.0f - length),
                    "Filled track starts at the minimum-value end in both orientations");
                require(visual.thumb.width == 22 && classic.thumb.width == 16,
                    "WinUI outer thumb includes the native two-DIP negative border margin");
            }
    for (const auto axis : {Axis::horizontal, Axis::vertical})
        for (const float cross : {0.0f, 16.0f, 32.0f, 100.0f}) {
            const bool vertical = axis == Axis::vertical;
            const Size size = vertical ? Size{cross, 200} : Size{200, cross};
            for (const float thickness : {4.0f, 10.0f})
                for (const auto thumb : {std::optional<float>{}, std::optional<float>{40.0f}}) {
                    const auto visual = slider_visual(size, axis, false, .5, VisualStyle::winui, thickness, thumb);
                    const float center = std::min(cross / 2, std::max(14 + thickness / 2, thumb.value_or(22.0f) / 2));
                    require((vertical ? visual.track.x + visual.track.width / 2 : visual.track.y + visual.track.height / 2) == center &&
                        (vertical ? visual.thumb.x + visual.thumb.width / 2 : visual.thumb.y + visual.thumb.height / 2) == center,
                        "WinUI slot respects narrow content, authored track thickness, and thumb size");
                }
        }
    for (const auto axis : {Axis::horizontal, Axis::vertical})
        for (const float extent : {0.0f, 8.0f, 18.0f, 100.0f})
            for (const auto thumb : {std::optional<float>{}, std::optional<float>{0.0f}, std::optional<float>{40.0f}})
                for (const bool reversed : {false, true})
                    for (const double fraction : {0.0, .4, 1.0}) {
                        const bool vertical = axis == Axis::vertical;
                        const Size size = vertical ? Size{32, extent} : Size{extent, 32};
                        const auto visual = slider_visual(size, axis, reversed, fraction, VisualStyle::winui, 4, thumb);
                        const Point center{visual.thumb.x + visual.thumb.width / 2, visual.thumb.y + visual.thumb.height / 2};
                        const double expected = extent > thumb.value_or(18.0f) ? (reversed ? 1 - fraction : fraction) : 0;
                        require(std::abs(visual.pointer_fraction(center, axis) - expected) < 1e-6,
                            "Short and authored WinUI thumbs retain finite, coherent pointer mapping");
                        require(visual.thumb.width == thumb.value_or(22.0f) &&
                            visual.filled.width >= 0 && visual.filled.height >= 0 &&
                            (vertical ? visual.filled.height : visual.filled.width) <= extent,
                            "Authored thumb size is exact and short tracks never produce inverted fill");
                    }
}
}
int main() {
    try {
        tokens();
        button_appearance();
        composite_appearances();
        presentation_measurement();
        wrapping_measurement();
        scroll_presentation();
        slider_geometry();
        std::cout << "WinUI style tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
