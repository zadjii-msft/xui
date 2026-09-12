#include "xui/application.hpp"
#include "../src/drawing.hpp"
#include "../src/list_peer.hpp"
#include <windows.h>
#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <ole2.h>
#include <UIAutomation.h>
#include <wrl/client.h>
#include <psapi.h>
#include "resource_probe.hpp"
#include "uia_events.hpp"

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
struct Search { const wchar_t* title; HWND result{}; DWORD process{GetCurrentProcessId()}; };
BOOL CALLBACK find(HWND window, LPARAM data) {
    auto& search = *reinterpret_cast<Search*>(data);
    DWORD process{};
    GetWindowThreadProcessId(window, &process);
    if (process != search.process) return TRUE;
    wchar_t text[100]{};
    GetWindowTextW(window, text, 100);
    if (std::wstring(text) == search.title && IsWindowVisible(window)) {
        search.result = window;
        return FALSE;
    }
    return TRUE;
}
int drive(xui::Window& window, const wchar_t* title) {
    std::atomic<bool> clicked{};
    std::jthread input([&] {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        do {
            Search search{title};
            EnumWindows(find, reinterpret_cast<LPARAM>(&search));
            if (search.result) {
                const auto button = FindWindowExW(search.result, nullptr, L"Xui.Control.1", L"Close");
                if (button) {
                    PostMessageW(button, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(10, 10));
                    PostMessageW(button, WM_LBUTTONUP, 0, MAKELPARAM(10, 10));
                    clicked = true;
                    return;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        } while (std::chrono::steady_clock::now() < deadline);
    });
    const auto result = xui::Application::run(window);
    input.join();
    if (!clicked) std::wcerr << title << L": " << window.error() << L'\n';
    require(clicked, "A new window must run after prior shutdown or startup failure");
    return result;
}
void window_lifecycle() {
    auto retained = std::make_shared<xui::Button>(L"Close");
    auto retained_root = std::make_shared<xui::Stack>(xui::Axis::vertical);
    int later_invalidations{};
    const auto identity = retained->id();
    {
        xui::Window window({L"XUI lifecycle normal"});
        auto root = retained_root;
        auto input = std::make_shared<xui::TextInput>(L"Name");
        root->add(retained);
        root->add(input);
        window.set_content(root);
        retained->on_click([&] {
            input->set_enabled(false);
            require(!window.focus(*input), "Focus rejects a disabled control");
            input->set_enabled(true);
            require(window.focus(*input) && input->focused(), "Application focus reaches native EDIT");
            window.close();
        });
        require(drive(window, L"XUI lifecycle normal") == 0, "Callback closes the window");
        retained->on_click({});
        require(xui::Application::run(window) == 1, "Window cannot run twice");
        root->set_invalidator([&](xui::Invalidation) { ++later_invalidations; });
    }
    retained->set_name(L"Retained after window destruction");
    retained->set_preferred_size({200, 44});
    require(retained->id() == identity && !retained->focused() && !retained->captured(),
        "Retained control is detached after window destruction");
    require(later_invalidations == 2, "A detached window must not clear a later invalidator");
    {
        xui::Window window({L"XUI lifecycle invalid"});
        auto root = std::make_shared<xui::Stack>(xui::Axis::vertical);
        root->add(std::make_shared<xui::Element>());
        window.set_content(root);
        require(xui::Application::run(window) == 1, "Unsupported content fails without a leaked window");
    }
    for (const bool throws : {true, false}) {
        const auto title = throws ? L"XUI lifecycle callback failure" : L"XUI lifecycle recovered";
        xui::Window window({title});
        auto root = std::make_shared<xui::Stack>(xui::Axis::vertical);
        auto button = std::make_shared<xui::Button>(L"Close");
        button->on_click([&] {
            if (throws) throw std::runtime_error("Test callback failure");
            window.close();
        });
        root->add(button);
        window.set_content(root);
        require(drive(window, title) == (throws ? 1 : 0), "Callback failure stops only its own run");
    }
}
void resource_lifecycle(bool diagnostics = false, bool scroll_content = false, bool split_content = false) {
    std::vector<HANDLE> previous_handles;
    DWORD handles_after_warmup{}, gdi_after_warmup{}, user_after_warmup{};
    for (int cycle = 0; cycle < 8; ++cycle) {
        {
            xui::Window window({L"XUI resource lifecycle", split_content ? xui::Size{924, 641} : xui::Size{640, 640}});
            auto root = std::make_shared<xui::Stack>(xui::Axis::vertical);
            for (int i = 0; i < 16; ++i) {
                auto label = std::make_shared<xui::Label>(L"Shared renderer label");
                label->set_preferred_size({0, 12});
                root->add(label);
            }
            auto input = std::make_shared<xui::TextInput>(L"Native input");
            auto button = std::make_shared<xui::Button>(L"Focus");
            auto toggle = std::make_shared<xui::Toggle>(L"Toggle");
            auto list = std::make_shared<xui::FileList>();
            root->add(input);
            root->add(button);
            root->add(toggle);
            root->add(list, 1);
            std::shared_ptr<xui::ScrollView> viewport;
            std::shared_ptr<xui::SplitView> split;
            std::shared_ptr<xui::TabStrip> tabs;
            std::shared_ptr<xui::FileList> second_list;
            if (split_content) {
                auto secondary = std::make_shared<xui::Stack>(xui::Axis::vertical);
                tabs = std::make_shared<xui::TabStrip>(L"Resource tabs");
                tabs->set_tabs({{1, L"One"}, {2, L"Two"}}, 1);
                secondary->add(tabs);
                secondary->add(std::make_shared<xui::TextInput>(L"Secondary input"));
                second_list = std::make_shared<xui::FileList>(L"Secondary list");
                secondary->add(second_list, 1);
                split = std::make_shared<xui::SplitView>(root, secondary, L"Resource divider");
                auto outer = std::make_shared<xui::Stack>(xui::Axis::vertical);
                outer->add(split, 1);
                window.set_content(outer);
            } else if (scroll_content) {
                viewport = std::make_shared<xui::ScrollView>(root, L"Resource viewport");
                auto outer = std::make_shared<xui::Stack>(xui::Axis::vertical);
                outer->add(viewport, 1);
                window.set_content(outer);
            } else window.set_content(root);
            auto task = window.create_view_task(
                [](const xui::CancelCheck &cancel) {
                    auto items = std::make_shared<std::vector<xui::FileItem>>();
                    for (int i = 0; i < 60; ++i)
                        items->push_back(
                            {static_cast<xui::ItemId>(i + 1), L"Entry " + std::to_wstring(i), L"path", false});
                    return xui::SourceResult{xui::FileSnapshot::build(std::move(items), cancel), {}};
                },
                [list, second_list](xui::ViewResult result) {
                    if (result.view) {
                        if (second_list) second_list->set_view(result.view);
                        list->set_view(std::move(result.view));
                    }
                });
            task->request(L"");
            int resize_step{};
            window.on_key([&](const xui::KeyEvent &key) {
                if (key.key == xui::Key::f5) {
                    for (int i = 0; i < 100; ++i)
                        task->request(L"obsolete", true);
                    task->request(L"", true);
                    return true;
                }
                if (key.key == xui::Key::f6) {
                    window.set_theme(window.theme() == xui::ThemeMode::dark ? xui::ThemeMode::light
                                                                            : xui::ThemeMode::dark);
                    window.focus(*button);
                    if (viewport) viewport->scroll_by(32);
                    toggle->set_checked(!toggle->checked());
                    Search search{L"XUI resource lifecycle"};
                    EnumWindows(find, reinterpret_cast<LPARAM>(&search));
                    HWND divider{};
                    if (split) {
                        split->set_ratio(0.3f + resize_step * 0.1f);
                        tabs->step(1);
                        divider = FindWindowExW(search.result, nullptr, L"Xui.Control.1", L"Resource divider");
                        require(divider != nullptr, "SplitView has one native input peer");
                        SetCapture(divider);
                    }
                    SetWindowPos(search.result, nullptr, 0, 0, 650 + resize_step * 11, 660 + resize_step * 13,
                                 SWP_NOMOVE | SWP_NOZORDER);
                    const UINT scale[] = {96, 120, 144, 192};
                    RECT suggested{40, 40, 720, 940};
                    SendMessageW(search.result, WM_DPICHANGED,
                                 MAKEWPARAM(scale[resize_step % 4], scale[resize_step % 4]),
                                 reinterpret_cast<LPARAM>(&suggested));
                    if (split) {
                        require(GetCapture() != divider, "DPI transition releases splitter capture");
                        SendMessageW(search.result, WM_APP + 12, 0, 0);
                        const auto second = FindWindowExW(divider, nullptr, L"Xui.Control.1", L"Right pane");
                        const auto edit = FindWindowExW(second, nullptr, L"EDIT", nullptr);
                        require(edit != nullptr, "SplitView retains a native secondary EDIT");
                        require((IsWindowVisible(edit) != FALSE) == split->expanded(),
                            "DPI layout clips and hides native children in a collapsed pane");
                        if (split->expanded()) {
                            RECT pane_bounds{}, edit_bounds{};
                            GetWindowRect(second, &pane_bounds); GetWindowRect(edit, &edit_bounds);
                            require(second_list->bounds().width > 0 && edit_bounds.left >= pane_bounds.left &&
                                edit_bounds.right <= pane_bounds.right && edit_bounds.top >= pane_bounds.top &&
                                edit_bounds.bottom <= pane_bounds.bottom, "DPI layout contains native EDIT within its pane");
                        }
                    }
                    SendMessageW(search.result, WM_DISPLAYCHANGE, 0, 0);
                    ++resize_step;
                    return true;
                }
                return false;
            });
            std::exception_ptr failure;
            std::jthread driver([&] {
                HWND hwnd{};
                try {
                    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
                    const auto wait = [&](auto predicate) {
                        while (!predicate()) {
                            require(std::chrono::steady_clock::now() < deadline, "Resource lifecycle timed out");
                            std::this_thread::sleep_for(std::chrono::milliseconds(5));
                        }
                    };
                    wait([&] {
                        Search search{L"XUI resource lifecycle"};
                        EnumWindows(find, reinterpret_cast<LPARAM>(&search));
                        hwnd = search.result;
                        return hwnd != nullptr;
                    });
                    const auto metric = [&](int key) { return SendMessageW(hwnd, WM_APP + 60, key, 0); };
                    wait([&] { return metric(9) == 60 && metric(4) == 0; });
                    for (int step = 0; step < 4; ++step) {
                        const auto generation = metric(3);
                        const auto prior_paints = metric(0);
                        PostMessageW(hwnd, WM_KEYDOWN, VK_F5, 0);
                        PostMessageW(hwnd, WM_KEYDOWN, VK_F6, 0);
                        wait([&] { return metric(3) > generation && metric(4) == 0; });
                        wait([&] { return metric(11) == 1 && metric(0) > prior_paints; });
                        std::this_thread::sleep_for(std::chrono::milliseconds(50));
                        require(metric(11) == 1, "Labels, buttons, toggles and lists must share one host target");
                        require(metric(8) == 60, "Repeated cancelled refreshes retain the latest complete view");
                    }
                    auto paints = metric(0);
                    auto quiet_since = std::chrono::steady_clock::now();
                    wait([&] {
                        const auto now = std::chrono::steady_clock::now();
                        const auto current = metric(0);
                        if (current != paints) {
                            paints = current;
                            quiet_since = now;
                        }
                        return now - quiet_since >= std::chrono::milliseconds(200);
                    });
                    if (diagnostics && cycle == 2) {
                        std::cout << "open_window ";
                        resource_probe::heaps();
                    }
                } catch (...) {
                    failure = std::current_exception();
                }
                if (hwnd) PostMessageW(hwnd, WM_CLOSE, 0, 0);
            });
            require(xui::Application::run(window) == 0, "Resource stress window completes");
            require(xui::Drawing::live_targets() == 0, "A retained closed Window must not retain graphics targets");
            driver.join();
            if (failure) std::rethrow_exception(failure);
        }
        std::atomic<bool> drained{};
        xui::dispose_later(std::shared_ptr<const void>(new int, [&](const void *value) {
            delete static_cast<const int *>(value);
            drained = true;
        }));
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!drained) {
            require(std::chrono::steady_clock::now() < deadline, "Resource cleanup must finish off the UI thread");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        require(xui::Drawing::live_targets() == 0, "Window destruction releases every XUI render target");
        Search search{L"XUI resource lifecycle"};
        EnumWindows(find, reinterpret_cast<LPARAM>(&search));
        require(!search.result, "Window destruction releases native hosts");
        DWORD handles{};
        require(GetProcessHandleCount(GetCurrentProcess(), &handles) != 0, "Read lifecycle handle count");
        const auto gdi = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
        const auto user = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
        if (cycle == 2) {
            handles_after_warmup = handles;
            gdi_after_warmup = gdi;
            user_after_warmup = user;
        } else if (cycle > 2) {
            require(gdi <= gdi_after_warmup + 2 && user <= user_after_warmup + 2,
                    "Repeated windows must not retain GDI or USER objects");
            require(handles <= handles_after_warmup + 2, "Repeated windows must not retain process handles");
        }
        std::cout << "resource_cycle=" << cycle << " scroll=" << scroll_content << " split=" << split_content << " targets=" << xui::Drawing::live_targets() << " handles=" << handles
                  << " gdi=" << gdi << " user=" << user << '\n';
        if (diagnostics) {
            resource_probe::heaps();
            previous_handles = resource_probe::handle_difference(previous_handles);
        }
    }
}
void public_list_delivery() {
    const auto ui_thread = GetCurrentThreadId();
    std::atomic<DWORD> disposed_thread{};
    int deliveries{}, selections{};
    {
        xui::Window window({L"XUI public list delivery"});
        auto root = std::make_shared<xui::Stack>(xui::Axis::vertical);
        auto label = std::make_shared<xui::Label>(L"Results");
        auto input = std::make_shared<xui::TextInput>(L"Filter");
        auto button = std::make_shared<xui::Button>(L"Next");
        auto list = std::make_shared<xui::FileList>(L"Public results");
        root->add(label);
        root->add(input);
        root->add(button);
        root->add(list, 1);
        window.set_content(root);
        std::shared_ptr<xui::ViewTask> task;
        list->on_selection_change([&] { ++selections; label->set_text(L"Selected"); });
        button->on_click([list] { list->navigate(xui::Navigation::next); });
        input->on_submit([&] { window.focus(*list); });
        task = window.create_view_task([&](const xui::CancelCheck&) {
            require(GetCurrentThreadId() != ui_thread, "Loader must run off the UI thread");
            auto items = std::shared_ptr<const std::vector<xui::FileItem>>(
                new std::vector<xui::FileItem>{{1, L"Alpha", L"A", false}, {2, L"Beta", L"B", false}},
                [&](const std::vector<xui::FileItem>* value) { disposed_thread = GetCurrentThreadId(); delete value; });
            return xui::SourceResult{xui::FileSnapshot::build(std::move(items)), {}};
        }, [&](xui::ViewResult result) {
            require(GetCurrentThreadId() == ui_thread, "Result delivery must use the Window thread");
            ++deliveries;
            list->set_view(std::move(result.view));
            require(result.generation == task->generation(), "Only the latest generation is delivered");
            if (deliveries == 1) {
                require(list->model().view()->query() == L"alpha", "Initial asynchronous filter is applied");
                require(window.focus(*input), "Window hosts a native input beside the list");
                input->submit();
                require(list->focused() && list->viewport_height() > 0, "Window hosts the public list and its viewport");
                button->invoke();
                require(selections == 1 && label->text() == L"Selected", "List events update a sibling label");
                task->request(L"missing");
                task->request(L"Beta");
            } else {
                require(deliveries == 2 && list->model().view()->query() == L"beta" &&
                    list->model().visible_indices().size() == 1, "Superseded filters cannot reach application callbacks");
                require(!list->model().selected_index() && list->model().selected_id() == 1,
                    "Delivery preserves the hidden selection identity");
                window.close();
                require(task->request(L"late") == 0, "Reentrant close immediately revokes new requests");
                require(!window.focus(*button), "Closing windows reject new focus requests");
            }
        });
        task->request(L"Alpha");
        const auto result = xui::Application::run(window);
        if (result) std::wcerr << L"Public delivery: " << window.error() << L'\n';
        require(result == 0 && deliveries == 2, "Public asynchronous composition completes");
        require(!list->focused() && list->model().selected_id() == 1, "Retained list is detached with its model intact");
        list->on_selection_change({});
        input->on_submit({});
    }
    const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!disposed_thread && std::chrono::steady_clock::now() < limit) std::this_thread::yield();
    require(disposed_thread && disposed_thread != ui_thread, "Final list snapshot disposal stays off the UI thread");
}

void cancellation_without_join() {
    std::atomic<bool> entered{}, cancelled{}, release{}, finished{};
    std::atomic<int> callbacks{};
    bool returned{};
    {
        xui::Window window({L"XUI cancellation without UI join"});
        auto root = std::make_shared<xui::Stack>(xui::Axis::vertical);
        root->add(std::make_shared<xui::Label>(L"Cancellation"));
        window.set_content(root);
        auto blocked = window.create_view_task([&](const xui::CancelCheck& cancel) -> xui::SourceResult {
            entered = true;
            while (!cancel()) std::this_thread::yield();
            cancelled = true;
            while (!release) std::this_thread::yield();
            finished = true;
            return {};
        }, [&](xui::ViewResult) { ++callbacks; });
        blocked->request(L"", true);
        auto closer = window.create_view_task([&](const xui::CancelCheck&) {
            while (!entered) std::this_thread::yield();
            return xui::SourceResult{xui::FileSnapshot::build(nullptr), {}};
        }, [&](xui::ViewResult) { window.close(); });
        closer->request(L"");
        // The watchdog prevents a broken join implementation from hanging the suite.
        std::jthread watchdog([&](std::stop_token stop) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (!stop.stop_requested() && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            if (!stop.stop_requested()) release = true;
        });
        const auto result = xui::Application::run(window);
        returned = !release && !finished;
        release = true;
        watchdog.request_stop();
        require(result == 0, "Close from result delivery succeeds");
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!finished && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
    require(returned && cancelled && finished && callbacks == 0,
        "Window closure cancels and revokes results without joining a blocked loader");
}
int run_public_list_server() {
    xui::Window window({L"XUI public provider lifetime"});
    auto root = std::make_shared<xui::Stack>(xui::Axis::vertical);
    auto list = std::make_shared<xui::FileList>(L"Public result list");
    list->set_automation_id(L"public-result-list");
    auto items = std::make_shared<std::vector<xui::FileItem>>(
        std::vector<xui::FileItem>{{811, L"One", L"one", false}, {922, L"Two", L"two", false}});
    for (int i = 0; i < 58; ++i) items->push_back({static_cast<xui::ItemId>(1000 + i), L"Extra", L"extra", false});
    list->set_items(std::move(items));
    bool filtered{};
    window.on_key([&](const xui::KeyEvent& event) {
        if (event.key == xui::Key::f1) { list->set_name(L"Renamed results"); return true; }
        if (event.key == xui::Key::f2) { list->set_enabled(!list->enabled()); return true; }
        if (event.key == xui::Key::f3) { filtered = !filtered; list->set_filter(filtered ? L"Two" : L""); return true; }
        return false;
    });
    root->add(std::make_shared<xui::Label>(L"Provider lifetime"));
    root->add(std::make_shared<xui::TextInput>(L"Query"));
    root->add(std::make_shared<xui::Button>(L"Action"));
    root->add(list, 1);
    window.set_content(root);
    return xui::Application::run(window);
}
void public_list_provider_lifetime() {
    using Microsoft::WRL::ComPtr;
    struct Process {
        PROCESS_INFORMATION info{};
        ~Process() {
            if (info.hProcess) {
                if (WaitForSingleObject(info.hProcess, 5000) == WAIT_TIMEOUT) TerminateProcess(info.hProcess, 1);
                CloseHandle(info.hProcess);
                CloseHandle(info.hThread);
            }
        }
    } process;
    wchar_t executable[32768]{};
    require(GetModuleFileNameW(nullptr, executable, 32768) != 0, "Locate public provider server");
    std::wstring command = L"\"" + std::wstring(executable) + L"\" --public-list-server";
    STARTUPINFOW startup{sizeof(startup)};
    require(CreateProcessW(executable, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr,
        &startup, &process.info) != 0, "Start isolated public Window provider server");
    std::exception_ptr failure;
    require(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)), "Initialize public UIA client");
    HWND hwnd{};
    try {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!hwnd && std::chrono::steady_clock::now() < deadline) {
            Search search{L"XUI public provider lifetime"};
            search.process = process.info.dwProcessId;
            EnumWindows(find, reinterpret_cast<LPARAM>(&search));
            hwnd = search.result;
            if (!hwnd) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        require(hwnd != nullptr, "Public list window appears");
        ComPtr<IUIAutomation> automation;
        require(SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&automation))), "Create public control UIA client");
        ComPtr<IUIAutomationElement> element, host;
        require(SUCCEEDED(automation->ElementFromHandle(hwnd, &host)), "Read public Window accessibility tree");
        VARIANT id{};
        id.vt = VT_BSTR;
        id.bstrVal = SysAllocString(L"public-result-list");
        ComPtr<IUIAutomationCondition> condition;
        const auto created = automation->CreatePropertyCondition(UIA_AutomationIdPropertyId, id, &condition);
        VariantClear(&id);
        require(SUCCEEDED(created), "Create public identity condition");
        const auto found = host->FindFirst(TreeScope_Descendants, condition.Get(), &element);
        require(SUCCEEDED(found) && element, "Public list appears in the Window tree by its public identity");
        BSTR text{};
        require(SUCCEEDED(element->get_CurrentName(&text)), "Read public list name");
        const bool named = std::wstring(text ? text : L"") == L"Public result list";
        SysFreeString(text);
        require(named, "Public list name is not a browser-specific caption");
        require(SUCCEEDED(element->get_CurrentAutomationId(&text)), "Read public list automation ID");
        const bool identified = std::wstring(text ? text : L"") == L"public-result-list";
        SysFreeString(text);
        require(identified, "Public list uses its own automation identity");
        const auto wait_for = [&](auto predicate, const char* message) {
            const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(4);
            while (!predicate()) {
                require(std::chrono::steady_clock::now() < end, message);
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        };
        uia_test::Subscription subscription{automation};
        ComPtr<uia_test::Events> events;
        events.Attach(new uia_test::Events(process.info.dwProcessId));
        ComPtr<IUIAutomationCacheRequest> cache;
        require(SUCCEEDED(automation->CreateCacheRequest(&cache)) &&
            SUCCEEDED(cache->AddProperty(UIA_ProcessIdPropertyId)) &&
            SUCCEEDED(cache->AddProperty(UIA_NamePropertyId)), "Cache event identities");
        PROPERTYID properties[]{UIA_NamePropertyId, UIA_IsEnabledPropertyId, UIA_HasKeyboardFocusPropertyId,
            UIA_SelectionItemIsSelectedPropertyId, UIA_ScrollVerticalScrollPercentPropertyId,
            UIA_ScrollVerticalViewSizePropertyId, UIA_ScrollVerticallyScrollablePropertyId};
        require(SUCCEEDED(automation->AddPropertyChangedEventHandlerNativeArray(element.Get(), TreeScope_Subtree,
            cache.Get(), events.Get(), properties, static_cast<int>(std::size(properties)))), "Subscribe list property events");
        for (const auto event : {UIA_SelectionItem_ElementSelectedEventId, UIA_SelectionItem_ElementRemovedFromSelectionEventId})
            require(SUCCEEDED(automation->AddAutomationEventHandler(event, element.Get(), TreeScope_Subtree,
                cache.Get(), events.Get())), "Subscribe list selection events");
        require(SUCCEEDED(automation->AddStructureChangedEventHandler(element.Get(), TreeScope_Subtree,
            cache.Get(), events.Get())), "Subscribe list structure events");
        ComPtr<IUIAutomationTreeWalker> walker;
        require(SUCCEEDED(automation->get_ControlViewWalker(&walker)), "Read public list fragments");
        ComPtr<IUIAutomationElement> first, second;
        require(SUCCEEDED(walker->GetFirstChildElement(element.Get(), &first)) && first, "Read first public row");
        require(SUCCEEDED(walker->GetNextSiblingElement(first.Get(), &second)) && second, "Read second public row");
        ComPtr<IUIAutomationSelectionItemPattern> selection;
        require(SUCCEEDED(first->GetCurrentPatternAs(UIA_SelectionItemPatternId, IID_PPV_ARGS(&selection))),
            "Public row exposes selection");
        require(SUCCEEDED(selection->Select()) && SUCCEEDED(second->SetFocus()), "Public list supports independent UIA actions");
        BOOL selected{}, first_focus{}, second_focus{};
        require(SUCCEEDED(selection->get_CurrentIsSelected(&selected)) && selected &&
            SUCCEEDED(first->get_CurrentHasKeyboardFocus(&first_focus)) && !first_focus &&
            SUCCEEDED(second->get_CurrentHasKeyboardFocus(&second_focus)) && second_focus,
            "Keyboard focus is separate from selection on the public control");
        wait_for([&] {
            return events->boolean(UIA_SelectionItemIsSelectedPropertyId, L"One", true) &&
                events->boolean(UIA_HasKeyboardFocusPropertyId, L"Two", true) &&
                events->count(UIA_SelectionItem_ElementSelectedEventId, L"One");
        }, "External client receives independent selection and focus properties");
        require(SUCCEEDED(selection->RemoveFromSelection()), "Optional selection supports removal");
        wait_for([&] {
            return events->boolean(UIA_SelectionItemIsSelectedPropertyId, L"One", false) &&
                events->count(UIA_SelectionItem_ElementRemovedFromSelectionEventId, L"One");
        }, "External client receives explicit selection removal");
        require(SUCCEEDED(selection->Select()), "Restore stable selection before filtering");
        ComPtr<IUIAutomationScrollPattern> scroll;
        require(SUCCEEDED(element->GetCurrentPatternAs(UIA_ScrollPatternId, IID_PPV_ARGS(&scroll))) && scroll,
            "Public list exposes scroll");
        require(SUCCEEDED(scroll->SetScrollPercent(-1, 100)), "Scroll public list");
        wait_for([&] { return events->count(UIA_ScrollVerticalScrollPercentPropertyId); }, "External scroll event arrives");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        const auto scroll_events = events->count(UIA_ScrollVerticalScrollPercentPropertyId);
        for (int repeat = 0; repeat < 8; ++repeat)
            require(SUCCEEDED(scroll->SetScrollPercent(-1, 100)), "Repeated scroll endpoint succeeds");
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        require(events->count(UIA_ScrollVerticalScrollPercentPropertyId) == scroll_events,
            "Unchanged scroll endpoints must not raise redundant property events");
        PostMessageW(hwnd, WM_KEYDOWN, VK_F3, 0);
        wait_for([&] { return events->count(UIA_StructureChangedEventId); }, "Filtering raises structure event");
        wait_for([&] {
            return events->boolean(UIA_ScrollVerticallyScrollablePropertyId, L"Public result list", false) &&
                events->count(UIA_ScrollVerticalViewSizePropertyId, L"Public result list");
        }, "Filtering publishes changed scroll availability and viewport fraction");
        require(selection->Select() == UIA_E_ELEMENTNOTAVAILABLE, "Hidden provider actions reject stale visible positions");
        PostMessageW(hwnd, WM_KEYDOWN, VK_F3, 0);
        wait_for([&] {
            BOOL restored{};
            return SUCCEEDED(selection->get_CurrentIsSelected(&restored)) && restored;
        }, "Filtering restores the hidden stable selection");
        PostMessageW(hwnd, WM_KEYDOWN, VK_F1, 0);
        wait_for([&] { return events->count(UIA_NamePropertyId, L"Renamed results"); }, "List rename raises name event");
        PostMessageW(hwnd, WM_KEYDOWN, VK_F2, 0);
        wait_for([&] { return events->boolean(UIA_IsEnabledPropertyId, L"Renamed results", false); },
            "List disable raises enabled event");
        require(selection->Select() == UIA_E_ELEMENTNOTENABLED, "Disabled list rejects retained selection actions");
        PostMessageW(hwnd, WM_KEYDOWN, VK_F2, 0);
        wait_for([&] { return events->boolean(UIA_IsEnabledPropertyId, L"Renamed results", true); },
            "List enable raises enabled event");
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
        require(WaitForSingleObject(process.info.hProcess, 5000) == WAIT_OBJECT_0, "Public provider server stops");
        DWORD code{};
        require(GetExitCodeProcess(process.info.hProcess, &code) && code == 0, "Public provider window closes normally");
        require(FAILED(selection->Select()) && FAILED(second->SetFocus()),
            "Retained public row providers reject actions after close");
    } catch (...) {
        failure = std::current_exception();
        if (hwnd) PostMessageW(hwnd, WM_CLOSE, 0, 0);
    }
    CoUninitialize();
    if (failure) std::rethrow_exception(failure);
}
}
int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--public-list-server") return run_public_list_server();
        if (argc == 2 && std::string(argv[1]) == "--resources") { resource_lifecycle(true); return 0; }
        if (argc == 2 && std::string(argv[1]) == "--split-resources") { resource_lifecycle(false, false, true); return 0; }
        window_lifecycle();
        public_list_delivery();
        cancellation_without_join();
        resource_lifecycle();
        resource_lifecycle(false, true);
        public_list_provider_lifetime();
        std::cout << "Window lifecycle, public list, asynchronous delivery and disposal tests passed\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
