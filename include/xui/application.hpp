#pragma once

#include "xui/controls.hpp"
#include "xui/theme.hpp"
#include "xui/file_list.hpp"
#include <stop_token>

namespace xui {

struct WindowOptions {
    std::wstring title = L"XUI";
    // Initial client size in device-independent pixels.
    Size size{600, 520};
    ThemeMode theme = ThemeMode::dark;
    // Optional minimum outer window size in DIPs. Zero uses the system minimum.
    Size minimum_size{};
};

// Stable virtual-key values. TextInput remains responsible for character input.
enum class Key : std::uint16_t {
    backspace = 0x08, tab = 0x09, enter = 0x0d, escape = 0x1b, space = 0x20,
    page_up = 0x21, page_down, end, home, left, up, right, down,
    insert = 0x2d, delete_key,
    digit0 = 0x30, digit1, digit2, digit3, digit4, digit5, digit6, digit7, digit8, digit9,
    a = 0x41, b, c, d, e, f, g, h, i, j, k, l, m, n, o, p, q, r, s, t, u, v, w, x, y, z,
    f1 = 0x70, f2, f3, f4, f5, f6, f7, f8, f9, f10, f11, f12,
    f13, f14, f15, f16, f17, f18, f19, f20, f21, f22, f23, f24
};
struct KeyEvent { Key key; bool control{}, shift{}; Control* target{}; bool alt{}; };

// A bounded, cancellable snapshot/filter task. Delivery occurs on the Window thread.
// Closing the Window revokes delivery and cancels work without joining the UI thread.
class ViewTask final {
public:
    ~ViewTask();
    std::uint64_t request(std::wstring query, bool refresh = false);
    bool busy() const;
    std::uint64_t generation() const;
    std::uint64_t applied_generation() const;
    void cancel();
private:
    friend class Window;
    struct Impl;
    explicit ViewTask(std::shared_ptr<Impl> impl);
    std::shared_ptr<Impl> impl_;
};

// One bounded worker and one latest-result slot. Payloads must be immutable.
// The loader must not capture controls or a Window. Delivery belongs to the UI thread.
class SampleTask final {
public:
    using Payload = std::shared_ptr<const void>;
    using Loader = std::function<Payload(std::stop_token, bool reset_baseline)>;
    using Receiver = std::function<void(Payload, const std::wstring& error)>;
    ~SampleTask();
    void pause(bool paused);
    void set_interval(unsigned milliseconds);
    void refresh();
    void cancel();
private:
    friend class Window;
    struct Impl;
    explicit SampleTask(std::shared_ptr<Impl> impl);
    std::shared_ptr<Impl> impl_;
};

// One UI thread, one active window. The window retains its content until destruction.
// All properties and callbacks belong to the calling UI thread.
class Window final {
public:
    explicit Window(WindowOptions options = {});
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    void set_content(std::shared_ptr<Stack> content);
    void set_theme(ThemeMode theme);
    ThemeMode theme() const;
    bool focus(Control& control, bool select_all = false);
    void on_key(std::function<bool(const KeyEvent&)> callback);
    std::shared_ptr<ViewTask> create_view_task(ViewWorker::Loader loader, std::function<void(ViewResult)> receive);
    std::shared_ptr<SampleTask> create_sample_task(SampleTask::Loader loader, SampleTask::Receiver receive, unsigned milliseconds = 1000);
    bool confirm(const std::wstring& title, const std::wstring& message);
    void copy_text(const std::wstring& text);
    void close();
    const std::wstring& error() const;
private:
    friend class Application;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

class Application final {
public:
    // Runs once per Window. The caller must not initialize COM as MTA.
    static int run(Window& window);
};

}
