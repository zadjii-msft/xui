#include "xui/application.hpp"
#include "xui/image.hpp"
#include <windows.h>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    using namespace xui;
    Window window({L"XUI Control Gallery", {680, 740}});
    auto content = std::make_shared<Stack>(Axis::vertical);
    content->set_padding({28, 24, 28, 24});
    content->set_spacing(12);
    auto title = std::make_shared<Label>(L"Make yourself at home");
    title->set_heading(true);
    auto hint = std::make_shared<Label>(L"Edit your name, then save your greeting.");
    hint->set_tone(TextTone::secondary);
    auto name = std::make_shared<TextInput>(L"Your name");
    auto greeting = std::make_shared<Label>(L"Your greeting will appear here.");
    auto save = std::make_shared<Button>(L"Save greeting");
    auto permission = std::make_shared<Toggle>(L"Allow greeting updates");
    auto light = std::make_shared<Toggle>(L"Use light theme");
    auto status = std::make_shared<Label>(L"Enter a name to enable Save.");
    status->set_caption(true);
    status->set_tone(TextTone::secondary);
    greeting->set_tone(TextTone::accent);
    name->set_placeholder(L"How should we greet you?");
    permission->set_checked(true);
    save->set_enabled(false);
    const auto update = [&] {
        save->set_enabled(permission->checked() && !name->text().empty());
        status->set_text(!permission->checked() ? L"Updates are paused." :
            name->text().empty() ? L"Enter a name to enable Save." : L"Ready to save your greeting.");
    };
    name->on_change([update](const auto&) { update(); });
    permission->on_change([update](bool) { update(); });
    save->on_click([&] {
        greeting->set_text(L"Hello, " + name->text() + L"!");
        status->set_text(L"Greeting saved for this session.");
    });
    light->on_change([&window](bool checked) { window.set_theme(checked ? ThemeMode::light : ThemeMode::dark); });
    window.on_key([&window, light](const KeyEvent& event) {
        if (event.key != Key::f6) return false;
        const auto next = window.theme() == ThemeMode::dark ? ThemeMode::light :
            window.theme() == ThemeMode::light ? ThemeMode::high_contrast : ThemeMode::dark;
        window.set_theme(next);
        light->set_checked(next == ThemeMode::light);
        return true;
    });
    auto actions = std::make_shared<Stack>(Axis::horizontal);
    actions->set_spacing(8);
    actions->add(save);
    for (const auto& control : std::initializer_list<std::shared_ptr<Element>>{
        title, hint, name, actions, permission, light, greeting, status}) content->add(control);

    auto details = std::make_shared<Stack>(Axis::vertical);
    details->set_padding({16, 16, 16, 16});
    details->set_spacing(12);
    details->set_surface(true);
    auto section = std::make_shared<Label>(L"Workspace preferences");
    section->set_heading(true);
    details->add(section);
    auto explanation = std::make_shared<Label>(L"Scroll to review your workspace. Tab reveals each field.");
    explanation->set_tone(TextTone::secondary);
    details->add(explanation);
    auto sizes = std::make_shared<Stack>(Axis::horizontal);
    sizes->set_spacing(16);
    sizes->add(std::make_shared<Label>(L"Short"));
    sizes->add(std::make_shared<Label>(L"A longer label uses its measured text width"));
    details->add(sizes);
    auto workspace = std::make_shared<TextInput>(L"Workspace name");
    workspace->set_text(L"Personal workspace");
    details->add(workspace);
    auto restore = std::make_shared<Toggle>(L"Restore this workspace when the app opens");
    restore->set_checked(true);
    details->add(restore);
    auto preview = std::make_shared<Toggle>(L"Show previews in the file browser");
    details->add(preview);
    auto unavailable = std::make_shared<Toggle>(L"Shared workspaces (not connected)");
    unavailable->set_enabled(false);
    details->add(unavailable);
    auto description = std::make_shared<TextInput>(L"Workspace description");
    description->set_placeholder(L"Add a short description");
    details->add(description);
    auto long_label = std::make_shared<Label>(L"Long labels keep their full accessible text, but show an ellipsis when the viewport is too narrow.");
    long_label->set_caption(true);
    long_label->set_tone(TextTone::secondary);
    details->add(long_label);
    auto footer = std::make_shared<Stack>(Axis::horizontal);
    footer->set_spacing(8);
    auto apply = std::make_shared<Button>(L"Apply preferences");
    auto reset = std::make_shared<Button>(L"Reset");
    footer->add(apply);
    footer->add(reset);
    details->add(footer);
    auto result = std::make_shared<Label>(L"Preferences stay in this session.");
    result->set_tone(TextTone::secondary);
    details->add(result);
    auto image_path = std::make_shared<TextInput>(L"Preview image path");
    image_path->set_placeholder(L"Enter a PNG, JPEG, BMP, GIF, or TIFF path");
    details->add(image_path);
    auto image = std::make_shared<Image>(L"Workspace image preview");
    image->set_preferred_size({320, 144});
    details->add(image);
    auto image_actions = std::make_shared<Stack>(Axis::horizontal);
    image_actions->set_spacing(8);
    auto load_image = std::make_shared<Button>(L"Load image");
    auto unload_image = std::make_shared<Button>(L"Unload image");
    load_image->on_click([image, image_path] {
        image->set_source(image_path->text(), {320, 144});
        image->reload();
    });
    unload_image->on_click([image] { image->unload(); });
    image_actions->add(load_image);
    image_actions->add(unload_image);
    details->add(image_actions);
    apply->on_click([result] { result->set_text(L"Workspace preferences applied."); });
    reset->on_click([workspace, description, restore, preview, result] {
        workspace->set_text(L"Personal workspace");
        description->set_text(L"");
        restore->set_checked(true);
        preview->set_checked(false);
        result->set_text(L"Preferences reset.");
    });
    auto scroll = std::make_shared<ScrollView>(details, L"Workspace preferences");
    scroll->set_automation_id(L"workspace-scroll");
    content->add(scroll, 1);
    window.set_content(content);
    return Application::run(window);
}
