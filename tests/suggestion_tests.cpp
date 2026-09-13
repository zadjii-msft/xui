#include "xui/application.hpp"
#include "xui/suggestions.hpp"
#include "../src/suggestion_worker.hpp"
#include "../src/folder_suggestions.hpp"
#include "../src/drawing.hpp"
#include <windows.h>
#include <commctrl.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <thread>
#include "environment_fixture.hpp"
#include "../demo/explorer_state.hpp"

namespace {
using namespace std::chrono_literals;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void wait(F&& predicate, const char* message) {
    const auto end = std::chrono::steady_clock::now() + 8s;
    while (!predicate()) {
        require(std::chrono::steady_clock::now() < end, message);
        std::this_thread::sleep_for(2ms);
    }
}
double elapsed(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}
struct Gate final : xui::SuggestionSource {
    std::mutex mutex;
    std::condition_variable cv;
    bool entered{}, released{}, finished{};
    std::atomic<int> calls{};
    xui::SuggestionResult suggest(const xui::SuggestionRequest& request, const std::function<bool()>&) override {
        ++calls;
        if (request.text == L"blocked") {
            std::unique_lock lock(mutex);
            entered = true; cv.notify_all();
            cv.wait(lock, [&] { return released; });
            finished = true; cv.notify_all();
        }
        return {{request.context + request.text}, {}};
    }
    void await() {
        std::unique_lock lock(mutex);
        require(cv.wait_for(lock, 8s, [&] { return entered; }), "Slow provider started");
    }
    void release() {
        std::lock_guard lock(mutex);
        released = true; cv.notify_all();
    }
    ~Gate() { release(); }
};
struct ReleaseGate {
    std::shared_ptr<Gate> gate;
    ~ReleaseGate() { gate->release(); }
};
void worker_tests() {
    using namespace xui;
    auto worker = detail::SuggestionWorker::shared();
    auto delivery = std::make_shared<detail::SuggestionDelivery>();
    auto source = std::make_shared<Gate>();
    ReleaseGate release{source};
    worker->request(source, {L"blocked", L"old:"}, delivery);
    source->await();
    for (int i = 0; i < 100000; ++i)
        worker->request(source, {std::to_wstring(i), L"new:"}, delivery);
    source->release();
    std::optional<SuggestionResult> result;
    wait([&] { result = delivery->take(); return bool(result); }, "Latest result delivered");
    require(result->items == std::vector<std::wstring>{L"new:99999"}, "Old and replaced requests cannot publish");
    require(source->calls == 2, "100000 requests retain only one pending request");
    std::cout << "bounded_requests=100000 provider_calls=" << source->calls << " pending_peak=1 active_peak=1\n";
    auto revoked = std::make_shared<detail::SuggestionDelivery>();
    auto slow = std::make_shared<Gate>();
    ReleaseGate release_slow{slow};
    worker->request(slow, {L"blocked"}, revoked);
    slow->await();
    revoked->revoke();
    slow->release();
    {
        std::unique_lock lock(slow->mutex);
        require(slow->cv.wait_for(lock, 8s, [&] { return slow->finished; }), "Revoked provider finishes");
    }
    worker->request(source, {L"barrier"}, delivery);
    wait([&] { return bool(delivery->take()); }, "Queue drained");
    require(!revoked->take(), "Revoked endpoint cannot receive a stale result");
}
void folders(const std::filesystem::path& fixture) {
    const auto source = xui::folder_suggestions();
    const auto query = [&](std::wstring text, std::wstring base = L"", bool explicit_request = false) {
        return source->suggest({std::move(text), base.empty() ? fixture.wstring() : std::move(base), explicit_request},
            [] { return false; });
    };
    for (auto name : {L"alpha", L"alpine", L"alpha.dot", L".hidden", L"日本-🙂", L"with spaces"})
        std::filesystem::create_directory(fixture / name);
    std::filesystem::create_directory(fixture / L"alpha" / L"child");
    { std::ofstream file(fixture / L"app.exe"); file << "not a folder"; }
    require(query(L"al").items.size() == 3, "Relative prefix returns directories with dots");
    require(query(L"AL").items == query(L"al").items, "Case insensitive prefix and order");
    require(query(L".h").items == std::vector<std::wstring>{(fixture / L".hidden").wstring()}, "Dot folder prefix");
    require(query(L"日本").items == std::vector<std::wstring>{(fixture / L"日本-🙂").wstring()}, "Unicode folder");
    require(query(L"with ").items.size() == 1, "Spaces are literal");
    require(query(L"app").items.empty(), "Files and executables are excluded");
    require(query(L"alpha\\").items == std::vector<std::wstring>{(fixture / L"alpha" / L"child").wstring()}, "Trailing separator expands folder");
    require(query(L"alpha/").items == query(L"alpha\\").items, "Forward slash accepted");
    require(query(L"..\\al", (fixture / L"alpha").wstring()).items == query(L"al").items, "Parent relative path");
    require(query(L".").items.size() == 6, "Dot expands the current folder");
    require(query(L"..", (fixture / L"alpha").wstring()).items.size() == 6, "Dot dot expands parent");
    require(query((fixture / L"al").wstring()).items == query(L"al").items, "Absolute path has same candidates");
    const auto extended = query(L"\\\\?\\" + (fixture / L"al").wstring());
    require(extended.items.size() == 3 && extended.items[0].starts_with(L"\\\\?\\"), "Extended absolute paths preserve their prefix");
    require(query(L"\"" + (fixture / L"al").wstring() + L"\"").items == query(L"al").items, "Quoted path matches navigation");
    EnvironmentFixture environment(fixture.c_str()), relative(L"alpha"), missing(nullptr);
    require(query(environment.reference() + L"\\al").items == query(L"al").items, "Environment prefix matches absolute candidates");
    const auto expanded = query(L"\"" + environment.reference() + L"\\日本\"");
    require(expanded.items == query(L"日本").items, "Quoted Unicode environment suggestions");
    require(xui::explorer::resolve_location(expanded.items.front(), fixture).wstring() == expanded.items.front(),
        "Suggestion display path and address resolve to the same folder");
    require(query(relative.reference() + L"\\").items == query(L"alpha\\").items,
        "Relative environment suggestions use the tab context");
    require(query(missing.reference()).items.empty() && query(missing.reference()).status.starts_with(L"Unknown environment"),
        "Missing environment reference reports a nonfatal error");
    std::size_t opens{};
    auto forbidden = xui::detail::folder_suggestions_with([&](const std::wstring&, DWORD&) -> std::unique_ptr<xui::detail::FolderCursor> {
        ++opens; return {};
    });
    EnvironmentFixture long_value(std::wstring(32760, L'a').c_str());
    for (const auto& invalid : {std::wstring(L"%"), std::wstring(L"%unfinished"), missing.reference(), long_value.reference() + L"1234567"}) {
        const auto result = forbidden->suggest({invalid, fixture.wstring()}, [] { return false; });
        if (opens) std::wcerr << L"Unexpected enumeration for " << invalid << L": " << result.status << L'\n';
        require(!result.status.empty(), "Incomplete, unknown and oversized references show status");
    }
    require(opens == 0, "Invalid environment references never enumerate");
    const auto literal = fixture / L"100% complete";
    require(std::filesystem::create_directory(literal), "Create literal percent folder");
    require(query(L"100%").items == std::vector<std::wstring>{literal.wstring()},
        "Unpaired percent in a filename remains a literal suggestion prefix");
    require(std::filesystem::remove(literal), "Remove owned literal percent folder");
    require(query(L"").items.empty(), "Empty typing does not enumerate");
    require(query(L"", L"", true).items.size() == 6, "Explicit Down expands base");
    require(query(L"C:").items == std::vector<std::wstring>{L"C:\\"}, "Drive designator root completion");
    require(!query(L"C:relative").status.empty(), "Drive relative path rejected consistently");
    require(!query(L"\\\\server").status.empty(), "UNC server does not enumerate shares");
    require(!query(L"missing\\x").status.empty(), "Inaccessible folder has nonfatal status");
    require(!query(L"a*").status.empty(), "Wildcards do not bypass literal prefix");
    require(!query(std::wstring(32768, L'a')).status.empty(), "Input bound enforced");
    require(!query(std::wstring(L"a\0b", 3)).status.empty(), "Embedded NUL is rejected");
    require(source->suggest({L"al", fixture.wstring()}, [] { return true; }).items.empty(), "Cancellation before enumeration");
    const auto drives = source->suggest({L"", L"", true}, [] { return false; });
    require(!drives.items.empty() && drives.items.size() <= 26, "Explicit drive listing is bounded");
    for (int i = 0; i < 300; ++i)
        std::filesystem::create_directory(fixture / (L"many-" + std::to_wstring(i)));
    const auto start = std::chrono::steady_clock::now();
    const auto many = query(L"many-");
    require(many.items.size() == 64 && !many.status.empty(), "Large real directory has bounded results and truncation status");
    int checks{};
    const auto cancelled = source->suggest({L"many-", fixture.wstring()}, [&] { return ++checks == 3; });
    require(cancelled.items.empty(), "In-flight enumeration discards partial cancelled result");
    std::size_t retained_bytes = many.items.capacity() * sizeof(std::wstring);
    for (const auto& value : many.items) retained_bytes += (value.capacity() + 1) * sizeof(wchar_t);
    std::cout << "real_fixture_folders=307 retained_max=" << many.items.size() << " result_capacity_bytes=" << retained_bytes <<
        " enumeration_ms=" << elapsed(start) << '\n';
    struct Synthetic final : xui::detail::FolderCursor {
        std::size_t& calls;
        bool directories;
        int& closed;
        Synthetic(std::size_t& calls, bool directories, int& closed) : calls(calls), directories(directories), closed(closed) {}
        ~Synthetic() override { ++closed; }
        bool next(xui::detail::FolderEntry& entry, DWORD& error) override {
            if (calls == 100000) { error = ERROR_NO_MORE_FILES; return false; }
            ++calls;
            entry = {L"synthetic-folder", directories};
            return true;
        }
    };
    for (const bool directories : {true, false}) {
        std::size_t calls{};
        int closed{};
        const auto synthetic = xui::detail::folder_suggestions_with([&](const std::wstring& pattern, DWORD&) {
            require(pattern.ends_with(L"syn*"), "Enumerator gets narrowed filesystem prefix");
            return std::make_unique<Synthetic>(calls, directories, closed);
        });
        const auto now = std::chrono::steady_clock::now();
        const auto bounded = synthetic->suggest({L"syn", fixture.wstring()}, [] { return false; });
        require(calls == (directories ? 64 : 4096) && closed == 1 && !bounded.status.empty(),
            "100000-entry cursor respects retained and scanned budgets and closes immediately");
        std::cout << "synthetic_entries=100000 directories=" << directories << " scanned=" << calls <<
            " retained=" << bounded.items.size() << " scan_ms=" << elapsed(now) << '\n';
    }
}
HWND owned(const wchar_t* cls) {
    struct Search { const wchar_t* cls; HWND value{}; } search{cls};
    EnumWindows([](HWND hwnd, LPARAM data) -> BOOL {
        auto& search = *reinterpret_cast<Search*>(data);
        DWORD pid{}; GetWindowThreadProcessId(hwnd, &pid);
        wchar_t cls[80]{}; GetClassNameW(hwnd, cls, 80);
        if (pid == GetCurrentProcessId() && std::wstring_view(cls) == search.cls && IsWindowVisible(hwnd)) {
            search.value = hwnd; return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.value;
}
void native_tests(const std::filesystem::path& fixture) {
    using namespace xui;
    Window window({L"XUI suggestion tests", {620, 280}});
    auto root = std::make_shared<Stack>(Axis::vertical);
    auto input = std::make_shared<TextInput>(L"Address");
    auto plain = std::make_shared<TextInput>(L"Filter");
    input->set_maximum_length(32767);
    input->set_suggestions(folder_suggestions());
    input->set_suggestion_context(fixture.wstring());
    root->add(input); root->add(plain); window.set_content(root);
    std::shared_ptr<std::packaged_task<void()>> command;
    std::mutex mutex;
    int submits{}, escapes{}, enters{};
    HWND edit{};
    window.on_key([&](const KeyEvent& e) {
        if (e.key == Key::f12) {
            std::shared_ptr<std::packaged_task<void()>> action;
            { std::lock_guard lock(mutex); action = command; }
            (*action)(); return true;
        }
        if (e.key == Key::escape) { ++escapes; return true; }
        if (e.key == Key::enter) ++enters;
        return false;
    });
    input->on_submit([&] { ++submits; });
    auto slow = std::make_shared<Gate>();
    ReleaseGate release{slow};
    std::exception_ptr failure;
    std::jthread driver([&] {
        HWND host{};
        try {
            wait([&] { host = owned(L"Xui.Window.1"); return host != nullptr; }, "Find suggestion test host");
            const auto ui = [&](auto action) {
                auto task = std::make_shared<std::packaged_task<void()>>(action);
                auto result = task->get_future();
                { std::lock_guard lock(mutex); command = task; }
                PostMessageW(host, WM_KEYDOWN, VK_F12, 0);
                require(result.wait_for(8s) == std::future_status::ready, "UI command responsive");
                result.get();
            };
            ui([&] {
                require(window.focus(*input), "Focus actual EDIT");
                edit = GetFocus();
                require(edit != nullptr, "Native EDIT owns focus");
            });
            const auto type = [&](const wchar_t* value) {
                ui([&] {
                    SendMessageW(edit, EM_SETSEL, 0, -1);
                    SendMessageW(edit, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(value));
                });
            };
            const auto results = [&] {
                HWND popup{};
                wait([&] {
                    popup = owned(L"Xui.Suggestions.1");
                    const auto list = popup ? FindWindowExW(popup, nullptr, L"LISTBOX", nullptr) : nullptr;
                    return list && SendMessageW(list, LB_GETCOUNT, 0, 0) > 0;
                }, "Suggestions contain results");
                return popup;
            };
            auto start = std::chrono::steady_clock::now();
            type(L"al");
            const auto popup = results();
            std::cout << "cold_popup_ms=" << elapsed(start) << '\n';
            ui([&] {
                require(GetFocus() == edit, "Popup never takes EDIT focus");
                require(submits == 0, "Typing does not navigate");
                require(Drawing::live_targets() == 1, "Popup creates no D2D target");
                RECT a{}, p{}; GetWindowRect(edit, &a); GetWindowRect(popup, &p);
                require(p.left == a.left - MulDiv(12, GetDpiForWindow(edit), 96) &&
                    p.top == a.bottom + MulDiv(10, GetDpiForWindow(edit), 96), "Popup aligns with the full native input field");
            });
            PostMessageW(edit, WM_KEYDOWN, VK_DOWN, 0);
            PostMessageW(edit, WM_KEYDOWN, VK_TAB, 0);
            ui([&] {
                require(input->text() == (fixture / L"alpha").wstring(), "Tab accepts selected folder");
                require(GetFocus() == edit && submits == 0, "Tab acceptance stays in EDIT without navigation");
                require(!IsWindowVisible(popup), "Acceptance closes popup");
                require(SendMessageW(edit, EM_CANUNDO, 0, 0) != 0, "Suggestion replacement uses native undo");
            });
            start = std::chrono::steady_clock::now();
            type(L"al"); results();
            std::cout << "warm_popup_ms=" << elapsed(start) << '\n';
            PostMessageW(edit, WM_KEYDOWN, VK_DOWN, 0);
            PostMessageW(edit, WM_KEYDOWN, VK_RETURN, 0);
            ui([&] { require(submits == 1 && enters == 0, "Chosen Enter submits once before global key handler"); });
            type(L"quick");
            PostMessageW(edit, WM_KEYDOWN, VK_RETURN, 0);
            ui([&] { require(submits == 2 && enters == 1, "Enter during debounce submits typed text, not stale selection"); });
            type(L"al"); results();
            PostMessageW(edit, WM_KEYDOWN, VK_ESCAPE, 0);
            ui([&] { require(input->text() == L"al" && escapes == 0 && !IsWindowVisible(popup), "First Escape preserves typed text and wins shortcut"); });
            PostMessageW(edit, WM_KEYDOWN, VK_ESCAPE, 0);
            ui([&] { require(escapes == 1, "Second Escape reaches application"); });
            type(L"al"); results();
            ui([&] {
                const auto list = FindWindowExW(popup, nullptr, L"LISTBOX", nullptr);
                SendMessageW(list, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(20, 10));
                SendMessageW(list, WM_LBUTTONUP, 0, MAKELPARAM(20, 10));
                require(submits == 3 && GetFocus() == edit, "Mouse choice submits once without stealing focus");
            });
            type(L"al"); results();
            ui([&] { window.focus(*plain); });
            ui([&] { require(!IsWindowVisible(popup), "Focus loss hides suggestions"); window.focus(*input); });
            type(L"al"); results();
            ui([&] { input->set_text(L"al"); });
            ui([&] { require(!IsWindowVisible(popup), "Programmatic same-text update cancels suggestions"); });
            type(L"al"); results();
            ui([&] { SendMessageW(edit, WM_IME_STARTCOMPOSITION, 0, 0); });
            ui([&] { require(!IsWindowVisible(popup), "IME start hides suggestions"); });
            PostMessageW(edit, WM_KEYDOWN, VK_RETURN, 0);
            ui([&] {
                require(submits == 3 && enters == 1, "Composition Enter bypasses submission and global shortcuts");
                SendMessageW(edit, WM_IME_ENDCOMPOSITION, 0, 0);
            });
            results();
            ui([&] { SendMessageW(edit, EM_SETREADONLY, TRUE, 0); });
            PostMessageW(edit, WM_KEYDOWN, VK_DOWN, 0);
            ui([&] { require(!IsWindowVisible(popup), "Read-only input does not suggest"); SendMessageW(edit, EM_SETREADONLY, FALSE, 0); });
            type(L"al"); results();
            PostMessageW(edit, WM_KEYDOWN, VK_TAB, 0);
            ui([&] { require(GetFocus() != edit && !IsWindowVisible(popup), "Tab without selection follows normal focus traversal"); window.focus(*input); });
            ui([&] { input->set_text(L""); });
            PostMessageW(edit, WM_KEYDOWN, VK_DOWN, 0);
            results();
            ui([&] { require(input->text().empty(), "Explicit Down opens folders without changing text"); });
            for (int i = 0; i < 65; ++i) PostMessageW(edit, WM_KEYDOWN, VK_DOWN, 0);
            ui([&] {
                const auto list = FindWindowExW(popup, nullptr, L"LISTBOX", nullptr);
                const auto count = SendMessageW(list, LB_GETCOUNT, 0, 0);
                require(count == 64 && SendMessageW(list, LB_GETCURSEL, 0, 0) == count - 1 &&
                    SendMessageW(list, LB_GETTOPINDEX, 0, 0) > 0, "Native list scrolls selection within the bounded results");
            });
            for (const auto theme : {ThemeMode::light, ThemeMode::high_contrast, ThemeMode::dark}) {
                ui([&] { window.set_theme(theme); });
                ui([&] {
                    require(GetFocus() == edit && IsWindowVisible(popup), "Theme changes preserve native focus and popup");
                    const auto list = FindWindowExW(popup, nullptr, L"LISTBOX", nullptr);
                    HDC dc = GetDC(list);
                    const auto brush = SendMessageW(popup, WM_CTLCOLORLISTBOX, reinterpret_cast<WPARAM>(dc), reinterpret_cast<LPARAM>(list));
                    require(brush != 0, "Native list uses application or system contrast palette");
                    if (theme == ThemeMode::high_contrast)
                        require(GetTextColor(dc) == GetSysColor(COLOR_WINDOWTEXT), "High contrast uses system text color");
                    ReleaseDC(list, dc);
                });
            }
            ui([&] {
                RECT rect{}; GetWindowRect(host, &rect);
                SendMessageW(host, WM_DPICHANGED, MAKELPARAM(GetDpiForWindow(host), GetDpiForWindow(host)),
                    reinterpret_cast<LPARAM>(&rect));
                require(!IsWindowVisible(popup), "DPI transition dismisses stale popup geometry");
            });
            PostMessageW(edit, WM_KEYDOWN, VK_DOWN, 0); results();
            ui([&] {
                RECT rect{}; GetWindowRect(host, &rect);
                SetWindowPos(host, nullptr, rect.left + 2, rect.top + 2, 0, 0, SWP_NOZORDER | SWP_NOSIZE | SWP_NOACTIVATE);
                require(!IsWindowVisible(popup), "Window movement dismisses popup");
            });
            PostMessageW(edit, WM_KEYDOWN, VK_DOWN, 0); results();
            ui([&] { input->set_enabled(false); });
            ui([&] { require(!IsWindowVisible(popup), "Disabling input hides suggestions"); input->set_enabled(true); window.focus(*input); });
            {
                auto refresh_gate = std::make_shared<Gate>();
                ReleaseGate release_refresh{refresh_gate};
                ui([&] { input->set_suggestions(refresh_gate); input->set_suggestion_context(L"refresh:"); });
                type(L"seed");
                const auto refresh_popup = results();
                const auto list = FindWindowExW(refresh_popup, nullptr, L"LISTBOX", nullptr);
                RECT original{};
                ui([&] {
                    GetWindowRect(refresh_popup, &original);
                    SendMessageW(list, LB_SETCURSEL, 0, 0);
                });
                type(L"blocked");
                const auto retained = [&] {
                    require(IsWindowVisible(refresh_popup) && IsWindowVisible(list),
                        "Typing keeps the existing popup and list visible");
                    require(SendMessageW(list, LB_GETCOUNT, 0, 0) == 1,
                        "Pending refresh retains the previous rows");
                    wchar_t text[128]{};
                    SendMessageW(list, LB_GETTEXT, 0, reinterpret_cast<LPARAM>(text));
                    require(std::wstring_view(text) == L"refresh:seed", "Pending refresh does not replace rows with loading text");
                    RECT current{}; GetWindowRect(refresh_popup, &current);
                    require(EqualRect(&original, &current), "Pending refresh preserves popup geometry");
                    require(SendMessageW(list, LB_GETCURSEL, 0, 0) == LB_ERR,
                        "Typing clears the previous query selection");
                    require(GetFocus() == edit, "Refreshing keeps native EDIT focus");
                };
                ui(retained); // Includes the debounce interval, before the worker starts.
                refresh_gate->await();
                ui(retained);
                PostMessageW(edit, WM_KEYDOWN, VK_DOWN, 0);
                ui([&] {
                    const auto before = submits;
                    SendMessageW(list, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(20, 10));
                    SendMessageW(list, WM_LBUTTONUP, 0, MAKELPARAM(20, 10));
                    require(submits == before && input->text() == L"blocked",
                        "Pending rows cannot accept a stale keyboard or mouse choice");
                    RedrawWindow(refresh_popup, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
                    retained();
                });
                type(L"latest");
                ui(retained);
                refresh_gate->release();
                wait([&] {
                    wchar_t text[128]{};
                    SendMessageW(list, LB_GETTEXT, 0, reinterpret_cast<LPARAM>(text));
                    return std::wstring_view(text) == L"refresh:latest";
                }, "Latest result replaces the retained rows in place");
                ui([&] {
                    require(owned(L"Xui.Suggestions.1") == refresh_popup && IsWindowVisible(list),
                        "Completion reuses the same visible popup and list");
                    require(SendMessageW(list, LB_GETCURSEL, 0, 0) == LB_ERR,
                        "Replacement rows do not inherit an old selection");
                });
                type(L"");
                ui([&] { require(!IsWindowVisible(refresh_popup), "Empty input still dismisses suggestions"); });
            }
            ui([&] { input->set_suggestions(slow); input->set_suggestion_context(L"old:"); });
            type(L"blocked");
            slow->await();
            start = std::chrono::steady_clock::now();
            type(L"latest");
            std::cout << "typing_while_provider_blocked_ms=" << elapsed(start) << '\n';
            require(elapsed(start) < 1000, "Slow filesystem must not block typing");
            ui([&] {
                double maximum{}, total{};
                for (int i = 0; i < 100; ++i) {
                    const auto message_start = std::chrono::steady_clock::now();
                    SendMessageW(edit, WM_CHAR, L'a' + i % 26, 1);
                    const auto time = elapsed(message_start);
                    total += time; maximum = std::max(maximum, time);
                }
                std::cout << "blocked_provider_WM_CHAR_count=100 mean_ms=" << total / 100 << " max_ms=" << maximum << '\n';
                require(maximum < 100, "Real native typing messages do not wait for the provider");
            });
            ui([&] { input->set_suggestion_context(L"new:"); input->set_text(L"new text"); });
            slow->release();
            ui([&] { require(!IsWindowVisible(popup), "Context change hides pending popup"); });
            ui([&] { RedrawWindow(host, nullptr, nullptr, RDW_UPDATENOW | RDW_ALLCHILDREN); });
            const auto paints = SendMessageW(host, WM_APP + 60, 0, 0);
            std::this_thread::sleep_for(250ms);
            require(SendMessageW(host, WM_APP + 60, 0, 0) == paints, "Closed suggestions have zero idle rendering");
            auto close_gate = std::make_shared<Gate>();
            ReleaseGate release_close{close_gate};
            ui([&] { input->set_suggestions(close_gate); });
            type(L"blocked");
            close_gate->await();
            start = std::chrono::steady_clock::now();
            PostMessageW(host, WM_CLOSE, 0, 0);
            wait([&] { return !IsWindow(host); }, "Close does not join blocked provider");
            std::cout << "close_while_provider_blocked_ms=" << elapsed(start) << '\n';
            require(elapsed(start) < 1000, "Closing revokes delivery without waiting for filesystem");
        } catch (...) { failure = std::current_exception(); }
        if (host) PostMessageW(host, WM_CLOSE, 0, 0);
    });
    const auto result = Application::run(window);
    driver.join();
    if (failure) std::rethrow_exception(failure);
    require(result == 0, "Suggestion test window succeeds");
}
}
int main() {
    auto fixture = std::filesystem::current_path() / (L"suggestions-fixture-" + std::to_wstring(GetCurrentProcessId()));
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code error; std::filesystem::remove_all(path, error); } } cleanup{fixture};
    try {
        require(std::filesystem::create_directory(fixture), "Create private owned fixture");
        folders(fixture);
        worker_tests();
        native_tests(fixture);
        std::cout << "Suggestion tests passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
