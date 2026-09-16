#pragma once

#include "xui/controls.hpp"
#include "xui/theme.hpp"
#include "xui/file_list.hpp"
#include "xui/foundation.hpp"
#include "xui/file_transfer.hpp"
#include "xui/miller_columns.hpp"
#include <stop_token>

namespace xui {
class CommandSurface;
class TitleBar;
class LocationPicker;
class ContentDialog;

struct WindowOptions {
    std::wstring title = L"XUI";
    // Initial client size in device-independent pixels.
    Size size{600, 520};
    ThemeMode theme = ThemeMode::dark;
    // Optional minimum outer window size in DIPs. Zero uses the system minimum.
    Size minimum_size{};
    bool custom_titlebar{};
    // Experimental solid-surface skin. Does not change control behavior or density.
    VisualStyle visual_style = VisualStyle::classic;
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
struct KeyEvent {
    Key key;
    bool control{}, shift{};
    Control* target{};
    bool alt{};
    // A text-producing key outside a native editor. The keyboard layout performs translation.
    bool text_input{};
};
enum class NavigationDirection { back, forward };
struct NavigationEvent {
    NavigationDirection direction;
    Control* target{};
    // Mouse position in client DIPs. Keyboard application commands have no position.
    std::optional<Point> position;
};

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
    // Calling UI thread only, before or during run. The title remains available after run.
    void set_title(std::wstring title);
    const std::wstring& title() const;
    // Uses the shared asynchronous Shell thumbnail/icon service. Empty clears the icon.
    // Replacing the source or closing cancels delivery. Failures use the UI-thread callback.
    void set_icon_source(std::wstring path);
    void on_icon_error(std::function<void(const std::wstring&)> callback);
    const std::shared_ptr<TitleBar>& titlebar() const;
    void set_theme(ThemeMode theme);
    ThemeMode theme() const;
    void set_visual_style(VisualStyle style);
    VisualStyle visual_style() const;
    void set_tooltip_style(std::shared_ptr<const ControlStyle> style);
    std::shared_ptr<const ControlStyle> tooltip_style() const;
    void set_tooltip_style_values(StylePart part, PartStyleValues values);
    const PartStyleValues& tooltip_style_values(StylePart part) const;
    const PartStyleValues* effective_tooltip_style_values(StylePart part) const;
    bool focus(Control& control, bool select_all = false);
    void show_popup(std::shared_ptr<Popup> popup, Control& anchor, Control* initial_focus = nullptr);
    void show_dialog(std::shared_ptr<ContentDialog> dialog, Control& anchor, Control* initial_focus = nullptr);
    void dismiss_popup(Popup& popup, PopupDismissReason reason = PopupDismissReason::cancel);
    void show_commands(std::shared_ptr<CommandSurface> surface, Control& anchor);
    void show_location_picker(std::shared_ptr<LocationPicker> picker, Control& anchor);
    // Explicit native fallback for third-party Shell extensions. No verbs run during discovery.
    void show_shell_commands(Control& anchor, const std::vector<std::wstring>& paths);
    void on_key(std::function<bool(const KeyEvent&)> callback);
    // Runs on the UI thread. Returns false after close. Close discards queued work.
    // Worker threads can post while the Window lives. Posted exceptions close the Window.
    bool post(std::function<void()> callback);
    // Return true to consume browser navigation. This does not change keyboard focus.
    void on_navigation(std::function<bool(const NavigationEvent&)> callback);
    std::shared_ptr<ViewTask> create_view_task(ViewWorker::Loader loader, std::function<void(ViewResult)> receive);
    std::shared_ptr<SampleTask> create_sample_task(SampleTask::Loader loader, SampleTask::Receiver receive, unsigned milliseconds = 1000);
    bool confirm(const std::wstring& title, const std::wstring& message);
    void copy_text(const std::wstring& text);
    void set_clipboard_text(const std::wstring& text);
    void set_file_clipboard(const std::vector<std::wstring>& paths, FileTransferEffect effect);
    std::optional<FileClipboardContent> get_file_clipboard();
    bool transfer_files(const std::vector<std::wstring>& paths, const std::wstring& destination, FileTransferEffect effect);
    std::optional<bool> paste_files(const std::wstring& destination);
    void close();
    const std::wstring& error() const;
private:
    friend class Application;
    struct Impl;
    std::shared_ptr<Impl> impl_;
};

class Application final {
public:
    // Runs once per Window. The caller must not initialize COM as MTA.
    static int run(Window& window);
};

}
