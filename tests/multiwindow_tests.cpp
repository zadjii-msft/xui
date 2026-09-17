#include "xui/application.hpp"
#include "xui/documents.hpp"
#include <windows.h>
#include <array>
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
void windows(std::array<int, 3> order, bool fail) {
    xui::Application app;
    std::array<std::shared_ptr<xui::Window>, 3> windows;
    std::array<std::shared_ptr<xui::TextInput>, 3> fields;
    std::array<HWND, 3> handles{};
    std::array<int, 3> keys{};
    int closed{}, delivered{};
    std::atomic<bool> done{};
    std::jthread watchdog([&](std::stop_token stop) {
        for (int i = 0; i < 500 && !stop.stop_requested() && !done; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        if (!stop.stop_requested() && !done) app.post([&] {
            app.shutdown();
            throw std::runtime_error("Multi-window fixture timed out");
        });
    });
    for (int i = 0; i < 3; ++i) {
        auto title = L"XUI independent fixture " + std::to_wstring(i);
        windows[i] = app.create_window({title, {420, 300}});
        auto root = std::make_shared<xui::Stack>(xui::Axis::vertical);
        root->add(std::make_shared<xui::Label>(title));
        fields[i] = std::make_shared<xui::TextInput>(L"Independent field");
        fields[i]->set_text(L"captured target " + std::to_wstring(i));
        root->add(fields[i]);
        auto text = std::make_shared<xui::MultilineText>(L"Preview contents");
        text->set_read_only(true);
        text->set_text(L"Independent selectable native preview");
        root->add(text, 1);
        windows[i]->set_content(root);
        windows[i]->on_key([&, i](const xui::KeyEvent& e) {
            if (e.key != xui::Key::f8) return false;
            require(e.target == fields[i].get(), "Keys retain the target control");
            ++keys[i];
            windows[i]->close();
            return true;
        });
        windows[i]->on_closed([&, i] {
            require(windows[i]->state() == xui::WindowState::closed, "Closed state precedes notification");
            require(!IsWindow(handles[i]), "Closed notification follows native destruction");
            require(!windows[i]->post([] {}), "Closed windows reject posts");
            ++closed;
            require(app.post([&] {
                ++delivered;
                if (closed == 3) return;
                const auto next = order[closed];
                require(windows[next]->state() == xui::WindowState::open && IsWindowVisible(handles[next]),
                    "Other document windows survive opener closure");
                require(windows[next]->focus(*fields[next], true), "Surviving native field accepts focus");
                require(GetAncestor(GetFocus(), GA_ROOT) == handles[next], "Focus belongs to surviving window");
                RECT before{}; GetWindowRect(handles[next], &before);
                require(SetWindowPos(handles[next], nullptr, before.left + 20, before.top + 10, 470, 340,
                    SWP_NOZORDER | SWP_NOACTIVATE), "Surviving window moves and resizes");
                windows[next]->copy_text(fields[next]->text());
                require(PostMessageW(GetFocus(), WM_KEYDOWN, VK_F8, 0), "Send routed key");
            }), "Application accepts deferred cleanup after window closure");
        });
        app.show(*windows[i]);
        handles[i] = FindWindowW(L"Xui.Window.1", title.c_str());
        DWORD process{};
        require(handles[i] && GetWindowThreadProcessId(handles[i], &process) == GetCurrentThreadId()
            && process == GetCurrentProcessId(), "Each native window uses this process and UI thread");
        require(!GetWindow(handles[i], GW_OWNER) && !(GetWindowLongPtrW(handles[i], GWL_STYLE) & WS_CHILD),
            "Independent windows have no native owner or parent");
        require(IsWindowVisible(handles[i]), "Show creates a visible nonblocking window");
        RedrawWindow(handles[i], nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        require(SendMessageW(handles[i], WM_APP + 60, 0, 0) > 0, "The native host paints before Show returns");
    }
    require(app.post([&] {
        bool nested{};
        try { app.run(); } catch (const std::logic_error&) { nested = true; }
        require(nested, "Nested application runs are rejected");
        if (fail) windows[order[0]]->post([] { throw std::runtime_error("Independent callback failure"); });
        else windows[order[0]]->close();
    }), "Application post accepted");
    const auto result = app.run();
    done = true;
    watchdog.request_stop();
    require(result == (fail ? 1 : 0), "Failures remain observable after healthy windows finish");
    if (fail) require(app.error().find(L"Independent callback failure") != std::wstring::npos, "Failure detail survives retirement");
    else require(app.error().empty(), "Successful lifecycle reports no error");
    require(closed == 3 && delivered == 3, "Every closure and final deferred action executes once");
    require(keys[order[0]] == 0 && keys[order[1]] == 1 && keys[order[2]] == 1, "No wrong-window shortcuts");
    require(!app.post([] {}), "Stopped application rejects posts");
}
void shared_root() {
    xui::Application app;
    auto a = app.create_window({L"XUI root owner"});
    auto b = app.create_window({L"XUI foreign root"});
    auto root = std::make_shared<xui::Stack>(xui::Axis::vertical);
    auto label = std::make_shared<xui::Label>(L"Still owned");
    root->add(label);
    a->set_content(root); b->set_content(root);
    app.show(*a);
    bool rejected{};
    try { app.show(*b); } catch (const std::logic_error&) { rejected = true; }
    require(rejected, "A root cannot belong to two live hosts");
    a->post([&] { label->set_text(L"First host remains usable"); a->close(); });
    require(app.run() == 1 && a->error().empty(), "Failed second host does not corrupt the original host");
}
void independent_content_replacement() {
    xui::Application app;
    auto opener = app.create_window({L"XUI replacement opener"});
    auto preview = app.create_window({L"XUI replacement survivor"});
    auto opener_root = std::make_shared<xui::Stack>(xui::Axis::vertical);
    auto opener_field = std::make_shared<xui::TextInput>(L"Opener field");
    opener_root->add(opener_field);
    opener->set_content(opener_root);
    auto initial = std::make_shared<xui::TextInput>(L"Initial preview field");
    auto host = std::make_shared<xui::ContentHost>(initial);
    preview->set_content(host);
    app.show(*opener);
    app.show(*preview);
    bool replaced{};
    require(app.post([&] {
        bool rejected{};
        try { preview->replace_content(*host, opener_root); }
        catch (const std::invalid_argument&) { rejected = true; }
        require(rejected && host->content() == initial,
            "Replacement rejects a root owned by another live window without mutation");
        require(opener->focus(*opener_field, true), "Rejected replacement preserves the original native host");
        opener->close();
        require(app.post([&] {
            auto next = std::make_shared<xui::TextInput>(L"Replacement preview field");
            next->set_text(L"Surviving content");
            preview->replace_content(*host, next);
            require(host->content() == next && preview->focus(*next, true),
                "Surviving application window replaces content and focuses its new native peer");
            require(!preview->focus(*initial), "Retired content no longer has a native peer");
            preview->replace_content(*host, {});
            require(!host->content(), "Surviving application window clears content");
            replaced = true;
            preview->close();
        }), "Application accepts replacement after opener retirement");
    }), "Application accepts independent replacement fixture");
    require(app.run() == 0 && replaced, "Content replacement preserves independent window lifetime");
}
}
int main() {
    try {
        windows({0, 1, 2}, false);
        windows({2, 0, 1}, false);
        windows({1, 2, 0}, true);
        shared_root();
        independent_content_replacement();
        xui::Window legacy({L"XUI legacy after applications"});
        auto root = std::make_shared<xui::Stack>(xui::Axis::vertical);
        root->add(std::make_shared<xui::Label>(L"Legacy"));
        legacy.set_content(root);
        legacy.post([&] { legacy.close(); });
        require(xui::Application::run(legacy) == 0, "Legacy run remains usable after application shutdown");
        std::cout << "Multi-window native lifecycle passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
