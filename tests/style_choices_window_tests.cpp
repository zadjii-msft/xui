#include "xui/application.hpp"
#include "xui/documents.hpp"
#include "owned_window_capture.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <map>
#include <sstream>
#include <thread>

namespace {
using namespace xui;
constexpr uint32_t light = 0x173959, dark = 0x294b6b, accent = 0x15db73, mark = 0xf23293, disabled = 0x778899;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
PartStyleValues background(uint32_t value) {
    PartStyleValues result; result.background = ThemeColor{value}; return result;
}
PartStyleValues text(uint32_t value) {
    PartStyleValues result; result.foreground = ThemeColor{value}; result.font_size = 18.0f; return result;
}
PartStyleValues surface() {
    auto result = background(light); result.background = ThemeColor{light, dark};
    result.padding = Insets{5, 5, 5, 5}; result.corner_radius = 0.0f; return result;
}
std::size_t count(const owned_window_capture::Pixels& pixels, Rect bounds, float scale, uint32_t color) {
    const int left = std::clamp(static_cast<int>(bounds.x * scale), 0, pixels.width);
    const int top = std::clamp(static_cast<int>(bounds.y * scale), 0, pixels.height);
    const int right = std::clamp(static_cast<int>((bounds.x + bounds.width) * scale), left, pixels.width);
    const int bottom = std::clamp(static_cast<int>((bounds.y + bounds.height) * scale), top, pixels.height);
    std::size_t result{};
    for (int y = top; y < bottom; ++y) for (int x = left; x < right; ++x) {
        const auto value = pixels.data[static_cast<std::size_t>(y) * pixels.width + x];
        bool match = true;
        for (unsigned shift : {0, 8, 16}) match &= std::abs(int((value >> shift) & 255) - int((color >> shift) & 255)) <= 5;
        result += match;
    }
    return result;
}
void require_native_colors(const owned_window_capture::Pixels& pixels, const Control& parent, const TextInput& editor,
    float scale, uint32_t background_color, std::optional<uint32_t> foreground_color, const char* phase) {
    const auto bounds = editor.bounds();
    const auto background_count = count(pixels, bounds, scale, background_color);
    const auto foreground_count = foreground_color ? count(pixels, bounds, scale, *foreground_color) : 0;
    if (background_count > 600 && (!foreground_color || foreground_count > 3)) return;
    const int left = std::clamp(static_cast<int>(bounds.x * scale), 0, pixels.width);
    const int top = std::clamp(static_cast<int>(bounds.y * scale), 0, pixels.height);
    const int right = std::clamp(static_cast<int>((bounds.x + bounds.width) * scale), left, pixels.width);
    const int bottom = std::clamp(static_cast<int>((bounds.y + bounds.height) * scale), top, pixels.height);
    std::map<uint32_t, std::size_t> colors;
    for (int y = top; y < bottom; ++y) for (int x = left; x < right; ++x)
        ++colors[pixels.data[static_cast<std::size_t>(y) * pixels.width + x] & 0xffffff];
    std::vector<std::pair<std::size_t, uint32_t>> frequencies;
    for (const auto& [color, frequency] : colors) frequencies.emplace_back(frequency, color);
    std::sort(frequencies.rbegin(), frequencies.rend());
    std::ostringstream message;
    const auto selection = editor.selection();
    message << phase << ": parent=" << (parent.role() == ControlRole::numeric_input ? "NumericInput" : "ComboBox")
        << " style=" << (parent.visual_style() == VisualStyle::winui ? "winui" : "classic")
        << " theme=light scale=" << scale << " editor=(" << bounds.x << ',' << bounds.y << ','
        << bounds.width << ',' << bounds.height << ") text_length=" << editor.text().size()
        << " focused=" << editor.focused() << " selection=" << selection.start << ':' << selection.end
        << " expected_bg=0x" << std::hex << background_color << std::dec << " bg_count=" << background_count;
    if (foreground_color) message << " expected_fg=0x" << std::hex << *foreground_color << std::dec << " fg_count=" << foreground_count;
    message << " dominant_pixels=";
    for (std::size_t i = 0; i < std::min<std::size_t>(5, frequencies.size()); ++i)
        message << " 0x" << std::hex << frequencies[i].second << std::dec << ':' << frequencies[i].first;
    throw std::runtime_error(message.str());
}
void flush(HWND window) {
    SendMessageW(window, WM_APP + 12, 0, 0);
    InvalidateRect(window, nullptr, FALSE);
    require(RedrawWindow(window, nullptr, nullptr, RDW_UPDATENOW | RDW_ALLCHILDREN) != FALSE,
        "Flush pending owned window and native child paints");
}
}
int main() {
    try {
        Window window({L"XUI choice style owned rendering", {760, 700}, ThemeMode::light});
        auto columns = std::make_shared<Stack>(Axis::horizontal); columns->set_spacing(12);
        columns->set_padding({8, 8, 8, 8});
        auto left = std::make_shared<Stack>(Axis::vertical); left->set_spacing(6);
        auto right = std::make_shared<Stack>(Axis::vertical); right->set_spacing(6);
        columns->add(left, 1); columns->add(right, 1);
        auto owner = std::make_shared<ContentView>(columns);
        auto radio = std::make_shared<RadioGroup>();
        auto choices = std::make_shared<RadioGroup>(L"List", true);
        auto combo = std::make_shared<ComboBox>(L"Selection");
        auto editable = std::make_shared<ComboBox>(L"Editable", true);
        auto numeric = std::make_shared<NumericInput>(L"Quantity");
        auto range = std::make_shared<RangeInput>();
        auto progress = std::make_shared<Progress>();
        auto status = std::make_shared<InlineStatus>(L"Styled warning message");
        auto picker = std::make_shared<ColorPicker>();
        const std::shared_ptr<Control> controls[]{radio, choices, combo, editable, numeric, range, progress, status, picker};
        const StyleTarget targets[]{StyleTarget::radio_group, StyleTarget::choice_list, StyleTarget::combo_box,
            StyleTarget::combo_box, StyleTarget::numeric_input, StyleTarget::range_input, StyleTarget::progress,
            StyleTarget::inline_status, StyleTarget::color_picker};
        for (unsigned i = 0; i < 9; ++i) {
            controls[i]->set_control_style(ControlStyle::create(targets[i], {{StylePart::root, surface()}},
                {{StylePart::root, style_states::disabled, background(disabled)}}));
            controls[i]->set_fixed_size({340, i < 2 ? 102.0f : i == 8 ? 290.0f : i == 7 ? 88.0f : 64.0f});
            (i < 7 ? left : right)->add(controls[i]);
        }
        const std::vector<ChoiceItem> items{{17, L"First choice"}, {29, L"Second choice"}};
        radio->set_items(items); choices->set_items(items); combo->set_items(items); editable->set_items(items);
        radio->set_control_style_values(StylePart::label, text(mark));
        radio->set_control_style_values(StylePart::mark, [] { PartStyleValues value; value.foreground = ThemeColor{accent}; return value; }());
        choices->set_control_style_values(StylePart::selected_marker, background(accent));
        combo->set_control_style_values(StylePart::text, text(mark));
        PartStyleValues arrow; arrow.foreground = ThemeColor{mark}; arrow.size = 42.0f;
        combo->set_control_style_values(StylePart::arrow, arrow);
        PartStyleValues header = text(mark); header.header_height = 22.0f;
        editable->set_control_style_values(StylePart::header, header);
        numeric->set_control_style_values(StylePart::header, header);
        editable->set_fixed_size({340, 76});
        numeric->set_fixed_size({340, 76});
        numeric->set_range({0, 999999, 1, 10});
        numeric->set_value(888888);
        PartStyleValues native_typography; native_typography.font_size = 18.0f; native_typography.font_weight = 700u;
        numeric->editor()->set_control_style_values(StylePart::text, native_typography);
        editable->editor()->set_control_style_values(StylePart::text, native_typography);
        auto track = background(0x596b7d); track.thickness = 10.0f;
        range->set_control_style_values(StylePart::track, track);
        range->set_control_style_values(StylePart::fill, background(accent));
        auto thumb = background(mark); thumb.size = 30.0f;
        range->set_control_style_values(StylePart::thumb, thumb); range->set_value(50);
        progress->set_control_style_values(StylePart::track, track);
        progress->set_control_style_values(StylePart::fill, background(accent));
        progress->set_control_style_values(StylePart::caption, text(mark)); progress->set_value(50);
        auto stripe = background(accent); stripe.size = 7.0f;
        status->set_control_style_values(StylePart::stripe, stripe);
        status->set_control_style_values(StylePart::message, text(mark));
        status->set_message(L"Styled warning message", StatusSeverity::warning);
        PartStyleValues swatch; swatch.border_brush = ThemeColor{accent}; swatch.border_thickness = Insets{3, 3, 3, 3};
        PartStyleValues selected_swatch; selected_swatch.border_brush = ThemeColor{mark};
        picker->set_control_style(ControlStyle::create(StyleTarget::color_picker,
            {{StylePart::root, surface()}, {StylePart::swatch, swatch}},
            {{StylePart::root, style_states::disabled, background(disabled)},
             {StylePart::swatch, style_states::selected, selected_swatch}}));
        picker->set_swatches({{0, 0, 255, 255}, {255, 0, 0, 255}, {0, 255, 0, 96}});
        picker->set_value({0, 0, 255, 255});
        picker->set_control_style_values(StylePart::channel_label, text(mark));
        picker->set_control_style_values(StylePart::checkerboard_light, background(0xf9f5ed));
        picker->set_control_style_values(StylePart::checkerboard_dark, background(0x776655));
        PartStyleValues preview; preview.border_brush = ThemeColor{mark}; preview.border_thickness = Insets{4, 4, 4, 4};
        picker->set_control_style_values(StylePart::preview, preview);
        auto root = std::make_shared<Stack>(Axis::vertical);
        root->add(owner, 1);
        window.set_content(root);
        std::string error;
        std::jthread worker([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            window.post([&] {
                try {
                    const auto hwnd = FindWindowW(L"Xui.Window.1", L"XUI choice style owned rendering");
                    require(hwnd != nullptr, "Owned choice style window exists");
                    const auto input_id = numeric->editor()->id(), button_id = numeric->increase_button()->id();
                    const float scale = GetDpiForWindow(hwnd) / 96.0f;
                    flush(hwnd);
                    auto pixels = owned_window_capture::capture(hwnd);
                    for (const auto& control : controls)
                        require(count(pixels, control->bounds(), scale, light) > 80, "Actual family branch paints authored root padding");
                    for (const auto& control : {std::static_pointer_cast<Control>(radio), std::static_pointer_cast<Control>(combo),
                        std::static_pointer_cast<Control>(numeric), std::static_pointer_cast<Control>(range),
                        std::static_pointer_cast<Control>(progress), std::static_pointer_cast<Control>(status),
                        std::static_pointer_cast<Control>(picker)})
                        require(count(pixels, control->bounds(), scale, mark) > 5, "Actual non-root text, thumb, or border is styled");
                    for (const auto& control : {std::static_pointer_cast<Control>(choices), std::static_pointer_cast<Control>(range),
                        std::static_pointer_cast<Control>(progress), std::static_pointer_cast<Control>(status)})
                        require(count(pixels, control->bounds(), scale, accent) > 10, "Selected marker, filled track, and severity stripe render");
                    require(count(pixels, picker->bounds(), scale, 0x0000ff) > 200, "Actual selected color remains authored blue");
                    require(count(pixels, picker->swatch_button(0)->bounds(), scale, mark) > 20 &&
                        count(pixels, picker->swatch_button(1)->bounds(), scale, accent) > 20,
                        "Only the actual selected swatch receives its selected border style");
                    picker->set_value({0, 0, 255, 0}); flush(hwnd);
                    pixels = owned_window_capture::capture(hwnd);
                    require(count(pixels, picker->bounds(), scale, 0xf9f5ed) > 100 &&
                        count(pixels, picker->bounds(), scale, 0x776655) > 100,
                        "Both authored checkerboard colors remain visible beneath transparent color data");
                    picker->set_value({0, 0, 255, 255});
                    const auto native_colors = [&](const std::shared_ptr<Control>& parent, const std::shared_ptr<TextInput>& editor) {
                        editor->set_selection({0, 0});
                        auto parent_field = background(0x3658aa); parent_field.foreground = ThemeColor{0x19e7f4};
                        parent->set_control_style_values(StylePart::field, parent_field);
                        flush(hwnd); pixels = owned_window_capture::capture(hwnd);
                        require_native_colors(pixels, *parent, *editor, scale, 0x3658aa, 0x19e7f4u,
                            "Parent field colors must reach native EDIT background and glyphs");
                        editor->set_control_style_values(StylePart::root, background(0x664422));
                        editor->set_control_style_values(StylePart::text, text(0xeedc55));
                        flush(hwnd); pixels = owned_window_capture::capture(hwnd);
                        require_native_colors(pixels, *parent, *editor, scale, 0x664422, 0xeedc55u,
                            "Explicit native child colors must override parent defaults");
                        editor->set_control_style_values(StylePart::root, {});
                        editor->set_control_style_values(StylePart::text, {});
                        flush(hwnd); pixels = owned_window_capture::capture(hwnd);
                        require_native_colors(pixels, *parent, *editor, scale, 0x3658aa, {},
                            "Clearing child colors must restore parent projection without changing identity");
                    };
                    native_colors(numeric, numeric->editor());
                    native_colors(editable, editable->editor());
                    owner->set_enabled(false); flush(hwnd);
                    pixels = owned_window_capture::capture(hwnd);
                    require(radio->enabled() && count(pixels, radio->bounds(), scale, disabled) > 80,
                        "Inherited disabled presentation does not rewrite local enabled state");
                    owner->set_enabled(true); window.set_theme(ThemeMode::dark); flush(hwnd);
                    pixels = owned_window_capture::capture(hwnd);
                    require(count(pixels, range->bounds(), scale, dark) > 80, "Theme resources resolve in actual slider rendering");
                    window.set_theme(ThemeMode::high_contrast); flush(hwnd);
                    pixels = owned_window_capture::capture(hwnd);
                    require(count(pixels, range->bounds(), scale, mark) == 0 &&
                        count(pixels, progress->bounds(), scale, accent) == 0, "High contrast suppresses authored presentation colors");
                    require(count(pixels, picker->bounds(), scale, 0x0000ff) > 200, "High contrast preserves actual color data");
                    range->set_orientation(Axis::vertical); range->set_reversed(true); range->set_value(25);
                    flush(hwnd);
                    require(range->slider_geometry().track.width == 10, "Styled vertical track shares model geometry");
                    for (const auto& control : controls) control->set_control_style(nullptr);
                    require(numeric->editor()->id() == input_id && numeric->increase_button()->id() == button_id &&
                        combo->selected() == 17 && picker->value() == RgbaColor{0, 0, 255, 255},
                        "Style replacement preserves child identities and authored values");
                } catch (const std::exception& failure) { error = failure.what(); }
                window.close();
            });
        });
        const auto result = Application::run(window);
        require(result == 0 && error.empty(), error.empty() ? "Choice style window loop succeeds" : error.c_str());
        std::cout << "Actual choice, field, range, progress, status, and color style rendering passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
