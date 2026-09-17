#include "xui/xui.h"
#include "file_dialog_test_probe.hpp"
#include <filesystem>
#include <iostream>
#include <source_location>
#include <string>
#include <thread>
namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void status(xui_status actual, xui_status expected, const std::source_location location = std::source_location::current()) {
    if (actual != expected) throw std::runtime_error("Unexpected file dialog ABI status " + std::to_string(actual) +
        " at line " + std::to_string(location.line()));
}
xui_string text(std::string_view value) { return {value.data(), static_cast<uint32_t>(value.size()), 0}; }
std::string utf8(const std::filesystem::path& path) {
    const auto bytes = path.u8string();
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}
struct Result {
    unsigned calls{};
    uint32_t accepted = 77;
    std::string path = "unchanged";
    xui_status result{};
    static xui_status XUI_CALL receive(void* context, uint32_t accepted, xui_string path) noexcept {
        auto& self = *static_cast<Result*>(context);
        try {
            require(accepted <= 1 && !path.reserved && (path.data || !path.length), "Valid borrowed ABI result");
            ++self.calls;
            self.accepted = accepted;
            self.path.assign(path.data ? path.data : "", path.length);
            return self.result;
        } catch (...) { return XUI_CALLBACK_FAILED; }
    }
};
struct Post {
    std::function<void()> action;
    std::exception_ptr error;
    static xui_status XUI_CALL call(void* context, uint32_t run) noexcept {
        auto& self = *static_cast<Post*>(context);
        if (!run) return XUI_OK;
        try { self.action(); return XUI_OK; }
        catch (...) { self.error = std::current_exception(); return XUI_CALLBACK_FAILED; }
    }
};
void run(bool receiver_failure) {
    xui_handle window{}, root{}, host{};
    xui_window_options creation{sizeof(creation), XUI_ABI_VERSION, text("File dialog ABI"), 500, 300};
    status(xui_window_create(&creation, &window), XUI_OK);
    status(xui_stack_create(window, 1, &root), XUI_OK);
    status(xui_content_host_create(window, &host), XUI_OK);
    status(xui_stack_add(root, host, 0), XUI_OK);
    status(xui_window_content(window, root), XUI_OK);
    xui_file_dialog_filter filter{text("Components"), text("*.xui")};
    const auto directory = utf8(std::filesystem::temp_directory_path());
    const auto leaf = utf8(L"xui-\u65e5\U0001f600-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
        std::to_wstring(GetTickCount64()));
    xui_file_dialog_options options{sizeof(options), XUI_FILE_DIALOG_VERSION, text("Owned ABI dialog"),
        &filter, 1, 0, text("xui"), text(leaf), text(directory)};
    Result result;
    const auto open = [&] { return xui_window_open_file_dialog(window, &options, Result::receive, &result); };
    if (!receiver_failure) {
        status(open(), XUI_BUSY);
        auto invalid = options; invalid.version = 0;
        status(xui_window_open_file_dialog(window, &invalid, Result::receive, &result), XUI_VERSION_MISMATCH);
        status(xui_window_open_file_dialog(window, nullptr, Result::receive, &result), XUI_VERSION_MISMATCH);
        status(xui_window_open_file_dialog(window, &options, nullptr, &result), XUI_INVALID_ARGUMENT);
        invalid = options; invalid.reserved = 1;
        status(xui_window_open_file_dialog(window, &invalid, Result::receive, &result), XUI_INVALID_ARGUMENT);
        invalid = options; invalid.filters = nullptr;
        status(xui_window_open_file_dialog(window, &invalid, Result::receive, &result), XUI_INVALID_ARGUMENT);
        filter.pattern = text("not a filter"); status(open(), XUI_INVALID_ARGUMENT); filter.pattern = text("*.xui");
        invalid = options; invalid.title = {"\xed\xa0\x80", 3, 0};
        status(xui_window_open_file_dialog(window, &invalid, Result::receive, &result), XUI_INVALID_ARGUMENT);
        invalid.title = {"a\0b", 3, 0};
        status(xui_window_open_file_dialog(window, &invalid, Result::receive, &result), XUI_INVALID_ARGUMENT);
        xui_status threaded{};
        std::thread worker([&] { threaded = open(); }); worker.join();
        status(threaded, XUI_WRONG_THREAD);
        xui_handle scope{};
        status(xui_content_begin(host, &scope), XUI_OK);
        status(open(), XUI_BUSY);
        status(xui_content_release(scope), XUI_OK);
        require(result.calls == 0 && result.accepted == 77 && result.path == "unchanged",
            "ABI rejection never delivers or mutates outputs");
    }
    bool ran{};
    Post post{[&] {
        const auto hwnd = FindWindowW(L"Xui.Window.1", L"File dialog ABI");
        DWORD process{}; GetWindowThreadProcessId(hwnd, &process);
        require(hwnd && process == GetCurrentProcessId(), "Owned ABI HWND found only in fixture");
        if (!receiver_failure) {
            xui_handle scope{}, candidate{}, previous{};
            status(xui_content_begin(host, &scope), XUI_OK);
            status(xui_stack_create(window, 1, &candidate), XUI_OK);
            status(xui_content_commit(scope, candidate), XUI_OK);
            status(xui_content_context(window, scope, &previous), XUI_OK);
            status(open(), XUI_BUSY);
            status(xui_content_context(window, previous, &previous), XUI_OK);
            const auto missing = directory + "\\xui-absent-" + std::to_string(GetTickCount64());
            auto invalid = options; invalid.initial_directory = text(missing);
            status(xui_window_open_file_dialog(window, &invalid, Result::receive, &result), XUI_NATIVE_ERROR);
            require(result.calls == 0, "Native failure is not cancellation");
        }
        {
            file_dialog_tests::ModalProbe probe(hwnd, [&](HWND dialog) {
                status(open(), XUI_BUSY);
                status(xui_window_destroy(window), XUI_BUSY);
                PostMessageW(dialog, WM_COMMAND, IDCANCEL, 0);
            });
            result.result = receiver_failure ? XUI_INVALID_ARGUMENT : XUI_OK;
            status(open(), receiver_failure ? XUI_CALLBACK_FAILED : XUI_OK);
            probe.check();
            require(result.calls == 1 && result.accepted == 0 && result.path.empty(), "One cancellation result");
        }
        if (!receiver_failure) {
            const auto destination = directory + leaf + ".xui";
            const std::filesystem::path destination_path(std::u8string(destination.begin(), destination.end()));
            require(!std::filesystem::exists(destination_path), "Save starts absent");
            file_dialog_tests::ModalProbe probe(hwnd, [](HWND dialog) { PostMessageW(dialog, WM_COMMAND, IDOK, 0); });
            status(xui_window_save_file_dialog(window, &options, Result::receive, &result), XUI_OK);
            probe.check();
            require(result.calls == 2 && result.accepted == 1 && result.path == destination, "Exact Unicode ABI save result");
            require(!std::filesystem::exists(destination_path), "ABI save selection writes no file");
            status(xui_window_close(window), XUI_OK);
        }
        ran = true;
    }};
    status(xui_window_post(window, Post::call, &post), XUI_OK);
    const auto run_result = xui_window_run(window);
    if (post.error) std::rethrow_exception(post.error);
    status(run_result, receiver_failure ? XUI_CALLBACK_FAILED : XUI_OK);
    require(ran, "ABI modal fixture completed");
    status(open(), XUI_CLOSED);
    status(xui_window_destroy(window), XUI_OK);
    status(open(), XUI_INVALID_HANDLE);
}
}
int main() {
    try { run(false); run(true); std::cout << "File dialog ABI ownership, results, errors, scopes and callback failure passed\n"; }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
