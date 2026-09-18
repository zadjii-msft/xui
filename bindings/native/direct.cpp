#include "xui/application.hpp"
#include "xui/image.hpp"
#include "../../demo/branding.hpp"
#include <windows.h>
#include <chrono>
#include <iostream>

int wmain(int argc, wchar_t** argv) {
    try {
        xui::Window window({L"XUI bindings", {600, 720}});
        xui::demo::set_application_icon(window);
        auto root = std::make_shared<xui::Stack>(xui::Axis::vertical);
        root->set_padding({20, 20, 20, 20}); root->set_spacing(10);
        auto label = std::make_shared<xui::Label>(L"Ready — 日本語 😀 — a long Unicode label with native retained layout");
        label->set_automation_id(L"status");
        auto input = std::make_shared<xui::TextInput>(L"Workspace name");
        input->set_automation_id(L"input"); input->set_text(L"Alpha 😀");
        auto button = std::make_shared<xui::Button>(L"Apply"); button->set_automation_id(L"apply");
        auto toggle = std::make_shared<xui::Toggle>(L"Enable previews"); toggle->set_automation_id(L"toggle");
        auto form = std::make_shared<xui::Stack>(xui::Axis::vertical); form->set_spacing(8);
        for (int i = 0; i < 8; ++i) form->add(std::make_shared<xui::Label>(L"Preference " + std::to_wstring(i) + L" — Unicode 日本語"));
        auto scroll = std::make_shared<xui::ScrollView>(form, L"Preferences");
        scroll->set_automation_id(L"scroll"); scroll->set_fixed_size({560, 120});
        auto image = std::make_shared<xui::Image>(L"Preview");
        image->set_automation_id(L"image"); image->set_fixed_size({192, 96});
        auto list = std::make_shared<xui::FileList>(L"Files"); list->set_automation_id(L"files");
        auto rows = std::make_shared<std::vector<xui::FileItem>>();
        for (int i = 0; i < 60; ++i) {
            wchar_t name[32]; swprintf_s(name, L"Entry-%03d.txt", i);
            rows->push_back({static_cast<uint64_t>(i + 1), name, {}, false});
        }
        list->set_items(rows);
        root->add(label); root->add(input); root->add(button); root->add(toggle);
        root->add(scroll); root->add(image); root->add(list, 1); window.set_content(root);
        bool callback_fail{};
        button->on_click([&] {
            if (callback_fail) throw std::runtime_error("GUI callback sentinel");
            label->set_text(L"Applied");
        });
        toggle->on_change([&](bool value) { label->set_text(value ? L"Enabled" : L"Disabled"); });
        input->on_change([&](const std::wstring&) { label->set_text(L"Edited"); });
        input->on_submit([&] { label->set_text(L"Submitted"); });
        window.on_key([&](const xui::KeyEvent& e) {
            if (e.key == xui::Key::f6) label->set_text(L"Keyboard");
            if (e.key == xui::Key::f7) label->set_text(image->status() == xui::ImageStatus::ready ? L"Image ready" : L"Image pending");
            if (e.key == xui::Key::f8 && argc > 1) { image->unload(); image->set_source(argv[1], {193, 145}); }
            if (e.key == xui::Key::f12) window.close();
            return false;
        });
        bool throughput{};
        for (int i = 1; i < argc; ++i) {
            if (std::wstring_view(argv[i]) == L"--throughput") throughput = true;
            else if (std::wstring_view(argv[i]) == L"--callback-fail") callback_fail = true;
            else image->set_source(argv[i]);
        }
        if (throughput) {
            std::vector<std::wstring> values;
            for (int i = 0; i < 64; ++i) values.push_back(L"Update " + std::to_wstring(i));
            auto start = std::chrono::steady_clock::now();
            for (int j = 0; j < 1000; ++j) for (const auto& value : values) label->set_text(value);
            std::cout << "{\"mutations\":64000,\"single_ms\":"
                << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count()
                << ",\"batch_ms\":null}\n";
            return 0;
        }
        return xui::Application::run(window);
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
