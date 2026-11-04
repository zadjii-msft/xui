#include "xui/application.hpp"
#include "native_editor_test_support.hpp"
#include <ole2.h>
#include <UIAutomation.h>
#include <commctrl.h>
#include <wrl/client.h>
#include <chrono>
#include <future>
#include <iostream>
#include <thread>
#include <utility>
#include <cstdlib>
#include <new>

namespace { thread_local std::uint64_t ordinary_allocations{}; }
void* operator new(std::size_t size) {
    ++ordinary_allocations;
    if (auto* pointer = std::malloc(size ? size : 1)) return pointer;
    throw std::bad_alloc{};
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { std::free(pointer); }

namespace {
using namespace xui;
constexpr UINT update = WM_APP + 12, metrics = WM_APP + 60;
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
HWND named(HWND parent, const wchar_t* name) {
    struct Search { const wchar_t* name; HWND result{}; } search{name};
    EnumChildWindows(parent, [](HWND child, LPARAM data) -> BOOL {
        auto& search = *reinterpret_cast<Search*>(data);
        wchar_t text[256]{};
        GetWindowTextW(child, text, 256);
        if (std::wstring_view(text) == search.name) { search.result = child; return FALSE; }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    require(search.result != nullptr, "The retained native control exists");
    return search.result;
}
void flush(HWND window) {
    for (unsigned pass = 0; pass != 16; ++pass) {
        SendMessageW(window, update, 0, 0);
        if (!SendMessageW(window, metrics, 49, 0)) return;
    }
    throw std::runtime_error("Dirty updates failed to settle in16 passes");
}
struct Work {
    LRESULT topology{}, native{}, accessibility{}, placement{}, passes{}, peers{}, layouts{};
};
Work work(HWND window) {
    return {SendMessageW(window, metrics, 44, 0), SendMessageW(window, metrics, 45, 0),
        SendMessageW(window, metrics, 46, 0), SendMessageW(window, metrics, 47, 0),
        SendMessageW(window, metrics, 48, 0), SendMessageW(window, metrics, 14, 0),
        SendMessageW(window, metrics, 2, 0)};
}
struct NativeCalls {
    unsigned properties{}, text{}, selections{}, fonts{};
    std::function<void()> during_sync;
    static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wp, LPARAM lp,
        UINT_PTR id, DWORD_PTR data) {
        auto& calls = *reinterpret_cast<NativeCalls*>(data);
        if (message == EM_SETLIMITTEXT) {
            ++calls.properties;
            if (auto callback = std::exchange(calls.during_sync, {})) callback();
        }
        if (message == WM_SETTEXT || message == EM_REPLACESEL) ++calls.text;
        if (message == EM_SETSEL) ++calls.selections;
        if (message == WM_SETFONT) ++calls.fonts;
        if (message == WM_NCDESTROY) RemoveWindowSubclass(window, procedure, id);
        return DefSubclassProc(window, message, wp, lp);
    }
    void attach(HWND window) {
        require(SetWindowSubclass(window, procedure, 83, reinterpret_cast<DWORD_PTR>(this)) != 0,
            "Attach native property observation");
    }
};
struct ReentrantLayout final : Stack {
    std::function<void()> during_measure;
    ReentrantLayout() : Stack(Axis::vertical) {}
    Size measure(Size available) override {
        if (auto callback = std::exchange(during_measure, {})) callback();
        return Stack::measure(available);
    }
};
std::wstring native_text(HWND window) {
    std::wstring value(static_cast<std::size_t>(GetWindowTextLengthW(window)) + 1, L'\0');
    value.resize(GetWindowTextW(window, value.data(), static_cast<int>(value.size())));
    return value;
}
void run(bool measured) {
    WindowOptions options{L"XUI targeted dirty work", {720, 600}, ThemeMode::light};
    options.show_activated = false;
    Window window(options);
    auto root = std::make_shared<ReentrantLayout>();
    auto input = std::make_shared<TextInput>(L"Target input");
    auto unrelated = std::make_shared<TextInput>(L"Unrelated input");
    input->set_caption_visible(false); unrelated->set_caption_visible(false);
    input->set_text(L"Native selection and undo remain owned");
    unrelated->set_text(L"Untouched native state");
    root->add(input); root->add(unrelated);
    auto label = std::make_shared<Label>(L"Target label");
    label->set_fixed_size({400, 24});
    root->add(label);
    auto replacement = std::make_shared<ContentHost>(std::make_shared<Button>(L"Old content"));
    root->add(replacement);
    auto pages = std::make_shared<PageView>();
    pages->add_page(std::make_shared<Button>(L"First page"));
    pages->add_page(std::make_shared<Button>(L"Second page"));
    root->add(pages);
    for (unsigned index = 0; index != 128; ++index) {
        auto sibling = std::make_shared<Label>(L"Unchanged sibling");
        sibling->set_fixed_size({400, 2});
        root->add(sibling);
    }
    window.set_content(root);
    std::exception_ptr failure;
    std::jthread worker([&] {
        const auto ui = [&](std::function<void()> callback) {
            auto done = std::make_shared<std::promise<void>>();
            auto result = done->get_future();
            require(window.post([done, callback = std::move(callback)] {
                try { callback(); done->set_value(); } catch (...) { done->set_exception(std::current_exception()); }
            }), "Post dirty-work test to the owning UI thread");
            require(result.wait_for(std::chrono::seconds(90)) == std::future_status::ready,
                "Bounded dirty-work test completes");
            result.get();
        };
        try {
            require(SUCCEEDED(OleInitialize(nullptr)), "Initialize the owned UIA client");
            struct Com { ~Com() { OleUninitialize(); } } com;
            HWND hwnd{}, target{}, outside{};
            NativeCalls target_calls, outside_calls;
            ui([&] {
                hwnd = FindWindowW(L"Xui.Window.1", options.title.c_str());
                require(hwnd != nullptr, "Create the owned dirty-work window");
                flush(hwnd);
                target = GetDlgItem(hwnd, 100);
                outside = GetDlgItem(hwnd, 101);
                require(target && outside && native_text(target) == input->text() &&
                    native_text(outside) == unrelated->text(), "Observe real EDIT HWNDs, not caption siblings");
                target_calls.attach(target); outside_calls.attach(outside);
                SendMessageW(outside, EM_SETSEL, 3, 9);
                outside_calls = {};
            });
            const unsigned iterations = measured ? 2048 : 16;
            const auto phase = [&](const char* name, auto change, bool paint_only, bool metadata_only) {
                ui([&] {
                    const auto before = work(hwnd);
                    const auto calls_before = outside_calls;
                    const auto gdi = test::gui_resources(GR_GDIOBJECTS), user = test::gui_resources(GR_USEROBJECTS);
                    FILETIME creation{}, exit{}, kernel_before{}, user_before{}, kernel_after{}, user_after{};
                    require(GetThreadTimes(GetCurrentThread(), &creation, &exit, &kernel_before, &user_before) != 0,
                        "Read owning thread CPU");
                    const auto started = std::chrono::steady_clock::now();
                    const auto allocations_before = ordinary_allocations;
                    for (unsigned index = 0; index != iterations; ++index) {
                        change(index);
                        flush(hwnd);
                    }
                    const auto wall = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
                    const auto allocations = ordinary_allocations - allocations_before;
                    require(GetThreadTimes(GetCurrentThread(), &creation, &exit, &kernel_after, &user_after) != 0,
                        "Read owning thread CPU after work");
                    const auto ticks = [](FILETIME value) {
                        return (std::uint64_t(value.dwHighDateTime) << 32) | value.dwLowDateTime;
                    };
                    const auto after = work(hwnd);
#ifndef XUI_DIRTY_WORK_BASELINE
                    require(after.topology == before.topology && after.placement == before.placement &&
                        after.layouts == before.layouts, "Local property work never recollects topology or lays out the window");
                    require(outside_calls.properties == calls_before.properties &&
                        outside_calls.text == calls_before.text && outside_calls.selections == calls_before.selections &&
                        outside_calls.fonts == calls_before.fonts, "Unrelated native controls receive no property writes");
                    if (paint_only)
                        require(after.native == before.native && after.accessibility == before.accessibility,
                            "Paint-only work never republishes native or accessibility state");
                    else if (metadata_only)
                        require(after.native == before.native && after.accessibility - before.accessibility == iterations,
                            "Accessible metadata publishes exactly its originating peer");
                    else
                        require(after.native - before.native == iterations &&
                            after.accessibility - before.accessibility == iterations,
                            "Native state synchronizes exactly its originating peer");
#endif
                    require(after.peers == before.peers && test::gui_resources(GR_GDIOBJECTS) <= gdi &&
                        test::gui_resources(GR_USEROBJECTS) <= user, "Local updates preserve bounded native peer/GDI/USER counts");
                    DWORD first{}, last{};
                    SendMessageW(outside, EM_GETSEL, reinterpret_cast<WPARAM>(&first), reinterpret_cast<LPARAM>(&last));
                    require(first == 3 && last == 9 && native_text(outside) == L"Untouched native state",
                        "Unrelated native text, selection and identity stay unchanged");
                    require(GetDlgItem(hwnd, 100) == target && GetDlgItem(hwnd, 101) == outside,
                        "Property synchronization retains exact HWND identities");
                    require(native_text(target) == input->text(),
                        "Local native synchronization delivers the actual target model value to EDIT");
                    std::cout << "{\"phase\":\"" << name << "\",\"iterations\":" << iterations
                        << ",\"measured\":" << (measured ? "true" : "false") << ",\"wall_ms\":" << (measured ? wall : 0)
                        << ",\"owner_kernel_before100ns\":" << ticks(kernel_before)
                        << ",\"owner_kernel_after100ns\":" << ticks(kernel_after)
                        << ",\"owner_user_before100ns\":" << ticks(user_before)
                        << ",\"owner_user_after100ns\":" << ticks(user_after)
                        << ",\"topology_passes\":" << after.topology - before.topology
                        << ",\"native_syncs\":" << after.native - before.native
                        << ",\"accessibility_syncs\":" << after.accessibility - before.accessibility
                        << ",\"placement_passes\":" << after.placement - before.placement
                        << ",\"unrelated_native_properties\":" << outside_calls.properties - calls_before.properties
                        << ",\"cpp_allocations\":" << allocations
                        << ",\"peers\":" << after.peers << ",\"gdi_growth\":"
                        << int(test::gui_resources(GR_GDIOBJECTS)) - int(gdi) << ",\"user_growth\":"
                        << int(test::gui_resources(GR_USEROBJECTS)) - int(user) << "}\n";
                });
            };
            phase("paint-only", [&](unsigned index) { label->set_tone(index % 2 ? TextTone::accent : TextTone::secondary); }, true, false);
            phase("local-native", [&](unsigned index) { input->set_text(index % 2 ? L"Target value A" : L"Target value B"); }, false, false);
            phase("local-accessibility", [&](unsigned index) { input->set_help_text(index % 2 ? L"Help A" : L"Help B"); }, false, true);
            {
                Microsoft::WRL::ComPtr<IUIAutomation> automation;
                require(SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                    IID_PPV_ARGS(&automation))), "Create the real OS UIA client");
                Microsoft::WRL::ComPtr<IUIAutomationElement> element;
                require(SUCCEEDED(automation->ElementFromHandle(target, &element)), "Query the retained real EDIT provider");
                BSTR help{};
                require(SUCCEEDED(element->get_CurrentHelpText(&help)), "Read the actual current UIA HelpText");
                const bool matches = help && std::wstring_view(help) == L"Help A";
                SysFreeString(help);
                require(matches, "Targeted accessibility-only updates reach the real OS provider");
            }
            ui([&] {
#ifndef XUI_DIRTY_WORK_BASELINE
                target_calls.during_sync = [&] { label->set_text(L"Queued during native synchronization"); };
                const auto before = work(hwnd);
                input->set_text(L"Trigger reentrant native synchronization");
                SendMessageW(hwnd, update, 0, 0);
                require(SendMessageW(hwnd, metrics, 49, 0) != 0, "New synchronization dirtiness remains queued after the captured pass");
                require(work(hwnd).native - before.native == 1, "The captured pass excludes a newly dirty sibling");
                flush(hwnd);
                require(work(hwnd).native - before.native == 2, "The next pass delivers the newly dirty sibling exactly once");
                root->during_measure = [&] { label->set_text(L"Queued during measurement"); };
                root->set_spacing(1);
                SendMessageW(hwnd, update, 0, 0);
                require(SendMessageW(hwnd, metrics, 49, 0) != 0, "Measurement dirtiness is not cleared by the layout pass");
                flush(hwnd);
                require(label->name() == L"Queued during measurement", "The measurement's newer state is retained");
#endif
                const auto old = replacement->content();
                auto next = std::make_shared<Button>(L"Replacement child");
                window.replace_content(*replacement, next);
                flush(hwnd);
                require(named(hwnd, L"Replacement child") != nullptr, "Topology updates materialize replacement content");
                const auto before_stale = work(hwnd);
                old->invalidate(Invalidation::paint);
                flush(hwnd);
#ifndef XUI_DIRTY_WORK_BASELINE
                require(work(hwnd).passes == before_stale.passes, "A retired element cannot enqueue stale host work");
#endif
                pages->select(1);
                flush(hwnd);
                require(named(hwnd, L"Second page") != nullptr, "Selected lazy pages request explicit reconciliation");
                window.set_theme(ThemeMode::dark);
                flush(hwnd);
                require(work(hwnd).peers >= before_stale.peers, "Global theme and lazy retention preserve peer ownership");
                input->set_text(L"Pending at closure");
                window.close();
            });
        } catch (...) {
            failure = std::current_exception();
            window.post([&] { window.close(); });
        }
    });
    const auto result = Application::run(window);
    worker.join();
    if (failure) std::rethrow_exception(failure);
    require(result == 0, "Dirty-work native loop closes successfully");
    std::cout << "Targeted dirty work, reentrant capture, replacement, lazy pages and closure passed\n";
}
}
int main(int argc, char** argv) {
    bool measured{};
    if (argc == 2 && std::string_view(argv[1]) == "--measure") measured = true;
    else if (argc != 1) { std::cerr << "Usage: xui_dirty_work_window_tests [--measure]\n"; return 2; }
    try { run(measured); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
