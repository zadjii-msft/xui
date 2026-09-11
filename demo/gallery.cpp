#include "xui/application.hpp"
#include <windows.h>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    using namespace xui;
    Window window({L"XUI Control Gallery", {620, 560}});
    auto content = std::make_shared<Stack>(Axis::vertical);
    content->set_padding({28, 24, 28, 24});
    content->set_spacing(12);
    auto title = std::make_shared<Label>(L"Make yourself at home");
    title->set_heading(true);
    title->set_preferred_size({400, 40});
    auto hint = std::make_shared<Label>(L"Edit your name, then save your greeting.");
    auto name = std::make_shared<TextInput>(L"Your name");
    auto greeting = std::make_shared<Label>(L"Your greeting will appear here.");
    auto save = std::make_shared<Button>(L"Save greeting");
    auto permission = std::make_shared<Toggle>(L"Allow greeting updates");
    auto light = std::make_shared<Toggle>(L"Use light theme");
    auto status = std::make_shared<Label>(L"Enter a name to enable Save.");
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
    for (const auto& control : std::initializer_list<std::shared_ptr<Element>>{
        title, hint, name, save, permission, light, greeting, status}) content->add(control);
    window.set_content(content);
    return Application::run(window);
}
