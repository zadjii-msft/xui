#include "xui/application.hpp"
#include <windows.h>
#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <ole2.h>
#include <UIAutomation.h>
#include <wrl/client.h>

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
    list->set_items(std::make_shared<const std::vector<xui::FileItem>>(
        std::vector<xui::FileItem>{{811, L"One", L"one", false}, {922, L"Two", L"two", false}}));
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
        window_lifecycle();
        public_list_delivery();
        cancellation_without_join();
        public_list_provider_lifetime();
        std::cout << "Window lifecycle, public list, asynchronous delivery and disposal tests passed\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
