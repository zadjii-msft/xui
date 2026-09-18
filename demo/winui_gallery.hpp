#pragma once

#include "xui/application.hpp"
#include "xui/documents.hpp"
#include "xui/navigation.hpp"
#include "parity_samples.hpp"
#include "gallery_links.hpp"

namespace winui_gallery {

class Notes final : public xui::ItemsSource {
public:
    explicit Notes(bool favorites = false) : favorites_(favorites) {}
    std::size_t size() const override { return favorites_ ? 40 : 120; }
    xui::ItemKey key(std::size_t index) const override { return {1 + index * (favorites_ ? 3 : 1), 1}; }
    std::optional<std::size_t> find(xui::ItemKey value) const override {
        const std::uint64_t stride = favorites_ ? 3 : 1;
        if (value.version != 1 || value.id < 1 || (value.id - 1) % stride) return {};
        const auto index = (value.id - 1) / stride;
        return index < size() ? std::optional<std::size_t>{static_cast<std::size_t>(index)} : std::nullopt;
    }
    xui::ItemContent item(std::size_t index) const override {
        const auto id = key(index).id;
        return {L"Design note " + std::to_wstring(id),
            (id - 1) % 3 == 0 ? L"Favorite \u00b7 Sample collection" : L"Sample collection",
            xui::ButtonIcon::library};
    }
private:
    bool favorites_;
};

class Gallery final {
public:
    explicit Gallery(xui::Window& window) : window_(window) {
        using namespace xui;
        if (window_.theme() != ThemeMode::high_contrast) regular_theme_ = window_.theme();
        events_ = std::make_shared<Label>(L"Ready. Try the controls.");
        events_->set_automation_id(L"winui-events");
        events_->set_tone(TextTone::secondary);
        events_->set_caption(true);

        auto root = panel();
        root->set_padding({16, 12, 16, 12});
        auto header = panel(Axis::horizontal);
        auto heading = panel();
        heading->set_spacing(2);
        auto title = label(heading, L"WinUI experiment", L"winui-title");
        title->set_heading(true);
        label(heading, L"Compare button appearances, native input, and collection layouts without replacing control state.",
            L"winui-subtitle", TextTone::secondary)->set_caption(true);
        label(heading, L"For control guidance and C++, C#, Rust, and .xui examples, run: xui_gallery.exe --winui-catalog", L"winui-catalog-hint",
            TextTone::secondary)->set_caption(true);
        header->add(heading, 1);
        root->add(header);
        auto links = panel(Axis::horizontal);
        button(links, L"Style documentation", L"winui-docs", [this] {
            report(gallery::open_documentation(L"docs/specs/winui-style.md"));
        });
        button(links, L"Copy documentation link", L"winui-copy-docs", [this] {
            try {
                window_.copy_text(gallery::documentation_url(L"docs/specs/winui-style.md"));
                report(L"Documentation link copied.");
            } catch (const std::exception&) { report(L"Clipboard is unavailable."); }
        });
        root->add(links);

        auto appearance = panel(Axis::horizontal);
        style_ = button(appearance, L"", L"winui-style", [this] {
            window_.set_visual_style(window_.visual_style() == VisualStyle::winui ? VisualStyle::classic : VisualStyle::winui);
            update_appearance();
            report(window_.visual_style() == VisualStyle::winui ? L"WinUI style applied." : L"Classic style applied.");
        });
        style_->set_help_text(L"Switch the whole window between Classic and WinUI.");
        theme_ = button(appearance, L"", L"winui-theme", [this] {
            regular_theme_ = regular_theme_ == ThemeMode::dark ? ThemeMode::light : ThemeMode::dark;
            window_.set_theme(regular_theme_);
            update_appearance();
            report(regular_theme_ == ThemeMode::light ? L"Light theme applied." : L"Dark theme applied.");
        });
        theme_->set_help_text(L"Switch between dark and light. This also leaves high contrast.");
        contrast_ = button(appearance, L"", L"winui-high-contrast", {});
        contrast_->set_behavior(ButtonBehavior::toggle);
        contrast_->on_toggle([this](bool) {
            if (window_.theme() == ThemeMode::high_contrast) window_.set_theme(regular_theme_);
            else {
                regular_theme_ = window_.theme();
                window_.set_theme(ThemeMode::high_contrast);
            }
            update_appearance();
            report(window_.theme() == ThemeMode::high_contrast ? L"High contrast applied." : L"Previous theme restored.");
        });
        update_appearance();
        root->add(appearance);

        auto body = panel(Axis::horizontal);
        auto nav = std::make_shared<NavigationView>(L"Explore");
        nav->set_automation_id(L"winui-navigation");
        nav->items()->set_automation_id(L"winui-navigation-items");
        nav->toggle_button()->set_automation_id(L"winui-navigation-toggle");
        nav->set_search_visible(false);
        nav->set_pane_widths(180, 64);
        nav->set_items({
            {{1, 1}, {}, L"Overview", ButtonIcon::home},
            {{2, 1}, {}, L"Collection", ButtonIcon::library},
            {{3, 1}, {}, L"Controls", ButtonIcon::settings}
        });
        body->add(nav);
        pages_ = std::make_shared<PageView>();
        pages_->add_page(overview());
        pages_->add_page(collection());
        pages_->add_page(specimens());
        body->add(pages_, 1);
        root->add(body, 1);
        root->add(events_);
        nav->on_select([this](ItemKey key) {
            pages_->select(key.id == 3 ? 2 : key.id == 2 ? 1 : 0);
            report(key.id == 3 ? L"Control specimens opened." : key.id == 2 ? L"Collection opened." : L"Overview opened.");
        });
        nav->select({1, 1});
        report(L"Ready. Try the controls.");
        window_.set_content(root);
    }

private:
    using Panel = std::shared_ptr<xui::Stack>;
    xui::Window& window_;
    xui::ThemeMode regular_theme_{xui::ThemeMode::dark};
    std::shared_ptr<xui::Label> events_;
    std::shared_ptr<xui::Button> style_, theme_, contrast_;
    std::shared_ptr<xui::TextInput> input_;
    std::shared_ptr<xui::Toggle> editable_;
    std::shared_ptr<xui::PageView> pages_;
    std::shared_ptr<xui::CommandSurface> commands_;
    std::shared_ptr<const Notes> notes_{std::make_shared<Notes>()};
    std::shared_ptr<const Notes> favorites_{std::make_shared<Notes>(true)};

    static Panel panel(xui::Axis axis = xui::Axis::vertical) {
        auto result = std::make_shared<xui::Stack>(axis);
        result->set_spacing(8);
        return result;
    }
    static Panel card() {
        auto result = panel();
        result->set_padding({12, 10, 12, 10});
        result->set_surface(true);
        return result;
    }
    static std::shared_ptr<xui::Label> label(const Panel& parent, std::wstring text, std::wstring id,
        xui::TextTone tone = xui::TextTone::normal) {
        auto result = std::make_shared<xui::Label>(std::move(text));
        result->set_automation_id(std::move(id));
        result->set_tone(tone);
        result->set_wrapping(true);
        parent->add(result);
        return result;
    }
    static std::shared_ptr<xui::Button> button(const Panel& parent, std::wstring text, std::wstring id,
        std::function<void()> action) {
        auto result = std::make_shared<xui::Button>(std::move(text));
        result->set_automation_id(std::move(id));
        result->on_click(std::move(action));
        parent->add(result);
        return result;
    }
    void report(std::wstring text) { events_->set_text(std::move(text)); }
    void update_appearance() {
        using namespace xui;
        style_->set_name(window_.visual_style() == VisualStyle::winui ? L"Style: WinUI" : L"Style: Classic");
        const bool high_contrast = window_.theme() == ThemeMode::high_contrast;
        theme_->set_name(high_contrast ? L"Theme: High contrast" :
            window_.theme() == ThemeMode::light ? L"Theme: Light" : L"Theme: Dark");
        contrast_->set_name(high_contrast ? L"High contrast: On" : L"High contrast: Off");
        contrast_->set_checked(high_contrast);
    }
    std::shared_ptr<xui::ItemsView> items(std::wstring id, float height) {
        using namespace xui;
        auto result = std::make_shared<ItemsView>(L"Design notes");
        result->set_automation_id(std::move(id));
        result->set_preferred_size({560, height});
        result->set_items(notes_, notes_);
        result->set_help_text(L"Scroll for more notes. Use arrow keys to select and Enter to open.");
        result->on_selection([this, view = result.get()] {
            const auto selected = view->selection().focused();
            if (selected) report(L"Selected note " + std::to_wstring(selected->id) + L".");
        });
        result->on_activate([this](ItemKey key) { report(L"Opened sample note " + std::to_wstring(key.id) + L"."); });
        return result;
    }
    void show_commands(xui::Control& anchor) {
        using namespace xui;
        if (!commands_) {
            commands_ = std::make_shared<CommandSurface>(L"Note actions", false);
            commands_->popup()->set_automation_id(L"winui-command-popup");
            commands_->menu()->set_automation_id(L"winui-command-menu");
        }
        std::vector<CommandRecord> records{
            {1, 0, L"Preview note", [this] { preview(); }},
            {2, 0, L"Allow edits", [this] {
                editable_->set_checked(!editable_->checked());
                apply_editable(editable_->checked());
            }, true, editable_->checked()},
            {3, 0, L"Export unavailable", {}, false},
            {4, 0, L"Reset note", [this] { reset(); }}
        };
        commands_->set_commands(std::make_shared<CommandSet>(std::move(records)));
        window_.show_commands(commands_, anchor);
    }
    void preview() { report(input_->text().empty() ? L"Enter a note name first." : L"Preview: " + input_->text()); }
    void reset() {
        input_->set_text(L"First design");
        report(L"Note name reset.");
    }
    void apply_editable(bool value) {
        input_->set_enabled(value);
        report(value ? L"Editing enabled." : L"Editing disabled.");
    }
    std::shared_ptr<xui::ScrollView> overview() {
        using namespace xui;
        auto page = panel();
        label(page, L"Overview", L"winui-overview-title")->set_heading(true);
        label(page, L"Button::set_appearance selects standard, accent, or subtle emphasis. "
            L"TextInput::on_change reports edits. on_submit handles Enter. Save changes only this sample.",
            L"winui-overview-guide", TextTone::secondary);
        auto controls = card();
        label(controls, L"Buttons and input", L"winui-controls-title", TextTone::secondary)->set_caption(true);
        auto actions = panel(Axis::horizontal);
        button(actions, L"Preview", L"winui-standard", [this] { preview(); });
        auto save = button(actions, L"Save note", L"winui-accent", [this] {
            report(input_->text().empty() ? L"Enter a note name first." : L"Saved sample: " + input_->text());
        });
        save->set_appearance(ButtonAppearance::accent);
        auto reset_button = button(actions, L"Reset", L"winui-subtle", [this] { reset(); });
        reset_button->set_appearance(ButtonAppearance::subtle);
        auto more = button(actions, L"More actions", L"winui-more", {});
        more->set_behavior(ButtonBehavior::dropdown);
        more->set_minimum_size({140, 0});
        more->on_click([this, anchor = more.get()] { show_commands(*anchor); });
        controls->add(actions);

        auto states = panel(Axis::horizontal);
        auto pinned = button(states, L"Pinned", L"winui-checked", {});
        pinned->set_behavior(ButtonBehavior::toggle);
        pinned->set_checked(true);
        pinned->on_toggle([this, target = pinned.get()](bool checked) {
            target->set_name(checked ? L"Pinned" : L"Pin");
            report(checked ? L"Note pinned." : L"Note unpinned.");
        });
        button(states, L"Unavailable", L"winui-disabled", {})->set_enabled(false);
        editable_ = std::make_shared<Toggle>(L"Allow edits");
        editable_->set_automation_id(L"winui-toggle");
        editable_->set_checked(true);
        editable_->on_change([this](bool value) { apply_editable(value); });
        states->add(editable_);
        controls->add(states);

        input_ = std::make_shared<TextInput>(L"Note name");
        input_->set_automation_id(L"winui-input");
        input_->set_placeholder(L"Name this note");
        input_->set_maximum_length(80);
        input_->set_text(L"First design");
        input_->on_change([this](const auto&) { report(L"Note name changed."); });
        input_->on_submit([this] { preview(); });
        controls->add(input_);
        page->add(controls);

        auto recent = card();
        auto tabs = std::make_shared<TabStrip>(L"Note views");
        tabs->set_automation_id(L"winui-tabs");
        tabs->set_tabs({{1, L"All notes (120)"}, {2, L"Favorites (40)"}}, 1);
        recent->add(tabs);
        auto view = items(L"winui-items", 140);
        tabs->on_select([this, view](std::uint64_t id) {
            view->set_items(id == 2 ? favorites_ : notes_, notes_);
            view->set_offset(0);
            report(id == 2 ? L"Showing 40 favorites." : L"Showing all 120 notes.");
        });
        recent->add(view);
        page->add(recent);
        auto scroll = std::make_shared<ScrollView>(page, L"Overview");
        scroll->set_automation_id(L"winui-overview-scroll");
        return scroll;
    }
    std::shared_ptr<xui::ScrollView> collection() {
        using namespace xui;
        auto page = panel();
        label(page, L"Collection", L"winui-collection-title")->set_heading(true);
        label(page, L"ItemsView::set_presentation switches between list and tile layouts. "
            L"Both layouts use the same source and selection. Activation reports a note ID. It does not open a file.",
            L"winui-collection-guide", TextTone::secondary);
        auto content = card();
        label(content, L"120 sample notes \u00b7 Scroll, select, and open", L"winui-collection-subtitle",
            TextTone::secondary)->set_caption(true);
        auto views = panel(Axis::horizontal);
        auto view = items(L"winui-collection-items", 330);
        button(views, L"List", L"winui-list", [this, view] {
            view->set_presentation(ItemsPresentation::list);
            report(L"List view applied.");
        });
        button(views, L"Tiles", L"winui-tiles", [this, view] {
            view->set_presentation(ItemsPresentation::tiles);
            report(L"Tile view applied.");
        });
        button(views, L"Select all", L"winui-select-all", [this, view] {
            view->select_all();
            report(L"All 120 notes selected.");
        });
        content->add(views);
        content->add(view);
        page->add(content);
        auto scroll = std::make_shared<ScrollView>(page, L"Collection");
        scroll->set_automation_id(L"winui-collection-scroll");
        return scroll;
    }
    std::shared_ptr<xui::ScrollView> specimens() {
        using namespace xui;
        auto page = panel();
        label(page, L"Control specimens", L"winui-specimens-title")->set_subtitle(true);
        label(page, L"Edit a value. Then switch styles. Window::set_visual_style changes appearance without rebuilding controls or clearing their values.", L"winui-specimens-hint",
            TextTone::secondary)->set_caption(true);

        auto actions = card();
        label(actions, L"Buttons", L"winui-specimens-buttons-title")->set_subtitle(true);
        auto row = panel(Axis::horizontal);
        button(row, L"Standard", L"winui-specimens-standard", [this] { report(L"Standard button invoked."); });
        button(row, L"Accent", L"winui-specimens-accent", [this] {
            report(L"Accent button invoked.");
        })->set_appearance(ButtonAppearance::accent);
        button(row, L"Subtle", L"winui-specimens-subtle", [this] {
            report(L"Subtle button invoked.");
        })->set_appearance(ButtonAppearance::subtle);
        actions->add(row);

        label(actions, L"Keyboard focus shapes", L"winui-specimens-focus-title", TextTone::secondary)->set_caption(true);
        auto focus_shapes = panel(Axis::horizontal);
        for (const auto& [name, radius] : {std::pair{L"Square", 0.0f}, std::pair{L"Rounded", 12.0f},
            std::pair{L"Pill", 1000.0f}}) {
            auto specimen = button(focus_shapes, name, L"winui-specimens-focus-" + std::wstring(name),
                [this, name] { report(std::wstring(name) + L" button invoked."); });
            ButtonStyleValues shape;
            shape.corner_radius = radius;
            specimen->set_style_values(shape);
        }
        actions->add(focus_shapes);

        label(actions, L"Command icons", L"winui-specimens-icons-title", TextTone::secondary)->set_caption(true);
        auto icons = panel(Axis::horizontal);
        const std::pair<const wchar_t*, ButtonIcon> commands[]{
            {L"Back", ButtonIcon::back}, {L"Forward", ButtonIcon::forward}, {L"Up", ButtonIcon::up},
            {L"Refresh", ButtonIcon::refresh}, {L"Add", ButtonIcon::add}, {L"Folder", ButtonIcon::folder},
            {L"Library", ButtonIcon::library}, {L"Settings", ButtonIcon::settings}, {L"Search", ButtonIcon::search},
            {L"More", ButtonIcon::more}
        };
        for (const auto& [name, icon] : commands) {
            auto command = button(icons, name, L"winui-specimens-icon-" + std::wstring(name),
                [this, name] { report(std::wstring(name) + L" invoked."); });
            command->set_icon(icon);
            command->set_help_text(name);
            command->set_fixed_size({32, 32});
            command->set_appearance(ButtonAppearance::subtle);
        }
        actions->add(icons);

        auto dialog_content = panel();
        label(dialog_content, L"The dialog keeps its text between visits.", L"winui-specimens-dialog-hint",
            TextTone::secondary)->set_caption(true);
        auto dialog_input = std::make_shared<TextInput>(L"Dialog note");
        dialog_input->set_automation_id(L"winui-specimens-dialog-input");
        dialog_input->set_text(L"Keep this note");
        dialog_content->add(dialog_input);
        auto dialog = std::make_shared<ContentDialog>(L"Control specimen", dialog_content);
        dialog->popup()->set_automation_id(L"winui-specimens-dialog");
        dialog->primary()->set_automation_id(L"winui-specimens-dialog-primary");
        dialog->cancel_button()->set_automation_id(L"winui-specimens-dialog-cancel");
        dialog->on_result([this](DialogResult result) {
            report(result == DialogResult::primary ? L"Dialog accepted." : L"Dialog canceled.");
        });
        auto dialog_row = panel(Axis::horizontal);
        auto open = button(dialog_row, L"Open dialog", L"winui-specimens-open-dialog", {});
        open->on_click([this, dialog, dialog_input, anchor = open.get()] {
            window_.show_dialog(dialog, *anchor, dialog_input.get());
        });
        actions->add(dialog_row);
        page->add(actions);

        auto fields = card();
        label(fields, L"Text and choices", L"winui-specimens-fields-title")->set_subtitle(true);
        auto field_column = panel();
        field_column->set_maximum_size({320, (std::numeric_limits<float>::max)()});
        auto captioned = std::make_shared<TextInput>(L"Project name");
        captioned->set_automation_id(L"winui-specimens-input");
        captioned->set_text(L"Reference project");
        captioned->on_change([this](const auto&) { report(L"Project name changed."); });
        field_column->add(captioned);
        auto plain = std::make_shared<TextInput>(L"Filter projects");
        plain->set_automation_id(L"winui-specimens-input-plain");
        plain->set_caption_visible(false);
        plain->set_placeholder(L"Filter projects");
        plain->on_change([this](const auto&) { report(L"Project filter changed."); });
        field_column->add(plain);

        label(field_column, L"Quantity (inline spin)", L"winui-specimens-number-label", TextTone::secondary)->set_caption(true);
        auto number = std::make_shared<NumericInput>(L"Quantity");
        number->set_automation_id(L"winui-specimens-number");
        number->editor()->set_automation_id(L"winui-specimens-number-editor");
        number->set_range({0, 100, 1, 10});
        number->set_value(4);
        number->on_change([this](double) { report(L"Quantity changed."); });
        field_column->add(number);

        label(field_column, L"Project type", L"winui-specimens-combo-label", TextTone::secondary)->set_caption(true);
        const std::vector<ChoiceItem> choices{{1, L"Personal"}, {2, L"Shared"}, {3, L"Unavailable", false}};
        auto choice = std::make_shared<ComboBox>(L"Project type");
        choice->set_automation_id(L"winui-specimens-combo");
        choice->popup()->set_automation_id(L"winui-specimens-combo-popup");
        choice->choices()->set_automation_id(L"winui-specimens-combo-choices");
        choice->set_items(choices, 1);
        choice->on_change([this](std::uint64_t) { report(L"Project type changed."); });
        field_column->add(choice);
        label(field_column, L"Editable project type", L"winui-specimens-combo-editable-label",
            TextTone::secondary)->set_caption(true);
        auto editable_choice = std::make_shared<ComboBox>(L"Editable project type", true);
        editable_choice->set_automation_id(L"winui-specimens-combo-editable");
        editable_choice->editor()->set_automation_id(L"winui-specimens-combo-editor");
        editable_choice->popup()->set_automation_id(L"winui-specimens-combo-editable-popup");
        editable_choice->choices()->set_automation_id(L"winui-specimens-combo-editable-choices");
        editable_choice->set_items(choices, 1);
        editable_choice->on_change([this](std::uint64_t) { report(L"Editable type selected."); });
        editable_choice->on_edit([this](const auto&) { report(L"Editable type text changed."); });
        field_column->add(editable_choice);
        fields->add(field_column);
        page->add(fields);

        auto selection = card();
        label(selection, L"Selection and details", L"winui-specimens-selection-title")->set_subtitle(true);
        auto radio = std::make_shared<RadioGroup>(L"Sharing");
        radio->set_automation_id(L"winui-specimens-radio");
        radio->set_maximum_size({320, (std::numeric_limits<float>::max)()});
        radio->set_items({{1, L"Only me"}, {2, L"My team"}, {3, L"Unavailable", false}}, 1);
        radio->on_change([this](std::uint64_t) { report(L"Sharing choice changed."); });
        label(selection, L"Radio focus: default / rounded", L"winui-specimens-radio-focus-title");
        auto radio_focus = panel(Axis::horizontal);
        radio_focus->set_spacing(16);
        radio_focus->add(radio);
        auto rounded_radio = std::make_shared<RadioGroup>(L"Rounded sharing");
        rounded_radio->set_automation_id(L"winui-specimens-radio-rounded");
        rounded_radio->set_maximum_size({320, (std::numeric_limits<float>::max)()});
        rounded_radio->set_items(radio->items(), 1);
        PartStyleValues radio_shape;
        radio_shape.corner_radius = 16.0f;
        rounded_radio->set_control_style(ControlStyle::create(StyleTarget::radio_group, {},
            {{StylePart::item, style_states::focused, radio_shape}}));
        rounded_radio->on_change([this](std::uint64_t) { report(L"Rounded sharing choice changed."); });
        radio_focus->add(rounded_radio);
        selection->add(radio_focus);
        auto check_row = panel(Axis::horizontal);
        auto check = std::make_shared<Toggle>(L"Send notifications");
        check->set_automation_id(L"winui-specimens-toggle");
        check->set_checked(true);
        check->on_change([this](bool value) { report(value ? L"Notifications enabled." : L"Notifications disabled."); });
        check_row->add(check);
        selection->add(check_row);
        auto details = panel();
        details->set_padding({12, 8, 12, 8});
        auto detail_check = std::make_shared<Toggle>(L"Include a summary");
        detail_check->set_automation_id(L"winui-specimens-expander-toggle");
        detail_check->on_change([this](bool value) { report(value ? L"Summary included." : L"Summary excluded."); });
        details->add(detail_check);
        auto expander = std::make_shared<Expander>(L"Notification details", details);
        expander->set_automation_id(L"winui-specimens-expander");
        expander->set_maximum_size({320, (std::numeric_limits<float>::max)()});
        expander->on_change([this](bool value) { report(value ? L"Details expanded." : L"Details collapsed."); });
        selection->add(expander);
        auto rounded_expander = std::make_shared<Expander>(L"Rounded details", std::make_shared<Label>(L"Additional settings."));
        rounded_expander->set_automation_id(L"winui-specimens-expander-rounded");
        rounded_expander->set_maximum_size({320, (std::numeric_limits<float>::max)()});
        rounded_expander->set_expanded(false);
        PartStyleValues expander_shape;
        expander_shape.corner_radius = 16.0f;
        rounded_expander->set_control_style_values(StylePart::header, expander_shape);
        rounded_expander->on_change([this](bool value) {
            report(value ? L"Rounded details expanded." : L"Rounded details collapsed.");
        });
        selection->add(rounded_expander);
        page->add(selection);

        auto disabled = card();
        label(disabled, L"Disabled controls", L"winui-specimens-disabled-title")->set_subtitle(true);
        auto disabled_buttons = panel(Axis::horizontal);
        button(disabled_buttons, L"Standard", L"winui-specimens-disabled-standard", {})->set_enabled(false);
        auto disabled_accent = button(disabled_buttons, L"Accent", L"winui-specimens-disabled-accent", {});
        disabled_accent->set_appearance(ButtonAppearance::accent);
        disabled_accent->set_enabled(false);
        auto disabled_subtle = button(disabled_buttons, L"Subtle", L"winui-specimens-disabled-subtle", {});
        disabled_subtle->set_appearance(ButtonAppearance::subtle);
        disabled_subtle->set_enabled(false);
        disabled->add(disabled_buttons);
        auto disabled_fields = panel();
        disabled_fields->set_maximum_size({320, (std::numeric_limits<float>::max)()});
        auto disabled_input = std::make_shared<TextInput>(L"Unavailable text");
        disabled_input->set_automation_id(L"winui-specimens-disabled-input");
        disabled_input->set_caption_visible(false);
        disabled_input->set_text(L"Read access unavailable");
        disabled_input->set_enabled(false);
        disabled_fields->add(disabled_input);
        auto disabled_number = std::make_shared<NumericInput>(L"Unavailable quantity");
        disabled_number->set_automation_id(L"winui-specimens-disabled-number");
        disabled_number->set_value(4);
        disabled_number->set_enabled(false);
        disabled_fields->add(disabled_number);
        auto disabled_combo = std::make_shared<ComboBox>(L"Unavailable type");
        disabled_combo->set_automation_id(L"winui-specimens-disabled-combo");
        disabled_combo->set_items(choices, 1);
        disabled_combo->set_enabled(false);
        disabled_fields->add(disabled_combo);
        auto disabled_toggle = std::make_shared<Toggle>(L"Unavailable notifications");
        disabled_toggle->set_automation_id(L"winui-specimens-disabled-toggle");
        disabled_toggle->set_checked(true);
        disabled_toggle->set_enabled(false);
        disabled_fields->add(disabled_toggle);
        disabled->add(disabled_fields);
        page->add(disabled);

        auto indicators = card();
        label(indicators, L"Toggles and progress", L"winui-specimens-indicators-title")->set_subtitle(true);
        auto immediate = std::make_shared<ToggleSwitch>(L"Live preview");
        immediate->set_automation_id(L"winui-specimens-switch");
        immediate->set_checked(true);
        immediate->on_change([this](bool value) { report(value ? L"Live preview on." : L"Live preview off."); });
        indicators->add(immediate);
        auto switch_states = panel(Axis::horizontal);
        switch_states->set_spacing(16);
        for (const auto name : {L"Off", L"Rounded", L"Disabled off", L"Disabled on"}) {
            auto specimen = std::make_shared<ToggleSwitch>(name);
            specimen->set_automation_id(std::wstring{L"winui-specimens-switch-"} + name);
            specimen->set_checked(std::wstring_view{name} == L"Disabled on" || std::wstring_view{name} == L"Rounded");
            specimen->set_enabled(std::wstring_view{name} == L"Off" || std::wstring_view{name} == L"Rounded");
            if (std::wstring_view{name} == L"Rounded") {
                PartStyleValues focus;
                focus.corner_radius = 16.0f;
                specimen->set_control_style_values(StylePart::root, focus);
            }
            specimen->on_change([this](bool value) { report(value ? L"Switch specimen on." : L"Switch specimen off."); });
            switch_states->add(specimen);
        }
        indicators->add(switch_states);
        auto pin = std::make_shared<ToggleButton>(L"Pin preview");
        pin->set_automation_id(L"winui-specimens-toggle-button");
        pin->on_toggle([this](bool value) { report(value ? L"Preview pinned." : L"Preview unpinned."); });
        indicators->add(pin);
        auto progress_row = panel(Axis::horizontal);
        auto ring = std::make_shared<ProgressRing>(L"Preview task");
        ring->set_automation_id(L"winui-specimens-progress-ring");
        ring->set_fixed_size({48, 48});
        progress_row->add(ring);
        auto progress = std::make_shared<Progress>(L"Preview progress");
        progress->set_automation_id(L"winui-specimens-progress");
        progress->set_state(ProgressState::indeterminate);
        progress_row->add(progress, 1);
        indicators->add(progress_row);
        auto busy = std::make_shared<ToggleSwitch>(L"Indeterminate task");
        busy->set_checked(true);
        busy->on_change([this, ring, progress](bool value) {
            const auto state = value ? ProgressState::indeterminate : ProgressState::determinate;
            ring->set_state(state); ring->set_value(60);
            progress->set_state(state); progress->set_value(60);
            report(value ? L"Task progress unknown." : L"Task progress 60 percent.");
        });
        indicators->add(busy);
        page->add(indicators);

        auto parity = card();
        label(parity, L"Choices, links, and status", L"winui-specimens-parity-title")->set_subtitle(true);
        auto mixed = std::make_shared<CheckBox>(L"Include attachments");
        mixed->set_automation_id(L"winui-specimens-checkbox");
        mixed->set_three_state(true); mixed->set_state(CheckState::indeterminate);
        mixed->on_change([this](CheckState value) {
            report(value == CheckState::indeterminate ? L"Attachments have mixed values." :
                value == CheckState::checked ? L"All attachments selected." : L"No attachments selected.");
        });
        parity->add(mixed);
        auto checkbox_states = panel(Axis::horizontal);
        checkbox_states->set_spacing(16);
        for (const auto name : {L"Unchecked", L"Checked", L"Rounded"}) {
            auto checkbox = std::make_shared<CheckBox>(name);
            checkbox->set_automation_id(std::wstring{L"winui-specimens-checkbox-"} + name);
            checkbox->set_state(std::wstring_view{name} == L"Unchecked" ? CheckState::unchecked : CheckState::checked);
            if (std::wstring_view{name} == L"Rounded") {
                PartStyleValues shape;
                shape.corner_radius = 16.0f;
                checkbox->set_control_style_values(StylePart::root, shape);
            }
            checkbox->on_change([this, name](CheckState value) {
                report(std::wstring{name} + (value == CheckState::checked ? L" selected." : L" cleared."));
            });
            checkbox_states->add(checkbox);
        }
        parity->add(checkbox_states);
        auto links = panel(Axis::horizontal);
        auto link = std::make_shared<HyperlinkButton>(L"Learn about these controls");
        link->set_automation_id(L"winui-specimens-hyperlink");
        link->on_click([this] { report(L"Help requested. No browser was opened."); });
        links->add(link);
        auto rounded_link = std::make_shared<HyperlinkButton>(L"Rounded help");
        rounded_link->set_automation_id(L"winui-specimens-hyperlink-rounded");
        PartStyleValues link_shape;
        link_shape.corner_radius = 16.0f;
        rounded_link->set_control_style_values(StylePart::root, link_shape);
        rounded_link->on_click([this] { report(L"Rounded help requested. No browser was opened."); });
        links->add(rounded_link);
        auto disabled_link = std::make_shared<HyperlinkButton>(L"Unavailable help");
        disabled_link->set_automation_id(L"winui-specimens-hyperlink-disabled");
        disabled_link->set_enabled(false);
        links->add(disabled_link);
        parity->add(links);
        auto selector = std::make_shared<SelectorBar>(L"Sample filter");
        selector->set_automation_id(L"winui-specimens-selector");
        selector->set_items({{1, L"All"}, {2, L"Active"}, {3, L"Completed"}}, 1);
        selector->on_change([this](std::uint64_t id) { report(L"Sample filter " + std::to_wstring(id)); });
        parity->add(selector);
        auto badge_row = panel(Axis::horizontal);
        label(badge_row, L"Unread samples", L"winui-specimens-badge-label");
        auto badge = std::make_shared<InfoBadge>(L"Unread samples");
        badge->set_automation_id(L"winui-specimens-info-badge"); badge->set_count(7);
        badge_row->add(badge);
        parity->add(badge_row);
        auto menu = std::make_shared<MenuBar>(L"Sample menu");
        menu->set_automation_id(L"winui-specimens-menu-bar");
        menu->set_commands(gallery::menu_bar_commands([this](std::wstring text) { report(std::move(text)); }));
        parity->add(menu);
        page->add(parity);

        auto scroll = std::make_shared<ScrollView>(page, L"Control specimens");
        scroll->set_automation_id(L"winui-specimens-scroll");
        return scroll;
    }
};

inline std::unique_ptr<Gallery> compose(xui::Window& window) {
    return std::make_unique<Gallery>(window);
}

}
