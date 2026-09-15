#include "xui/xui.h"
#include "xui/application.hpp"
#include "xui/image.hpp"
#include "xui/navigation.hpp"
#include "xui/documents.hpp"
#include "xui/map_view.hpp"
#include "xui/runtime_hosts.hpp"
#include "xui/data_grid.hpp"
#include "xui/titlebar.hpp"
#include "xui/styling.hpp"
#include <bit>
#include <atomic>
#include <variant>
#include <windows.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <unordered_map>

namespace {
struct Failure { xui_status status; const char* message; };
void require(bool condition, xui_status status, const char* message) {
    if (!condition) throw Failure{status, message};
}
thread_local xui_status last_status{};
thread_local std::array<char, 1024> last_message{};
void error(xui_status status, const char* message) noexcept {
    last_status = status;
    size_t used{};
    auto* p = reinterpret_cast<const unsigned char*>(message);
    while (*p && used + 1 < last_message.size()) {
        const size_t length = *p < 0x80 ? 1 : *p >= 0xc2 && *p <= 0xdf ? 2 :
            *p >= 0xe0 && *p <= 0xef ? 3 : *p >= 0xf0 && *p <= 0xf4 ? 4 : 0;
        uint32_t scalar = length == 1 ? *p : length == 2 ? *p & 0x1f : length == 3 ? *p & 0xf : *p & 7;
        bool valid = length != 0;
        for (size_t i = 1; valid && i < length; ++i) {
            valid = p[i] >= 0x80 && p[i] <= 0xbf;
            if (valid) scalar = (scalar << 6) | (p[i] & 0x3f);
        }
        valid = valid && !(scalar >= 0xd800 && scalar <= 0xdfff) && scalar <= 0x10ffff &&
            (length < 3 || scalar >= (length == 3 ? 0x800u : 0x10000u));
        if (!valid) { last_message[used++] = '?'; ++p; continue; }
        if (used + length >= last_message.size()) break;
        std::memcpy(last_message.data() + used, p, length); used += length; p += length;
    }
    last_message[used] = 0;
}
template<class F> xui_status boundary(F&& body) noexcept {
    try { body(); error(XUI_OK, ""); return XUI_OK; }
    catch (const Failure& f) { error(f.status, f.message); return f.status; }
    catch (const std::bad_alloc&) { error(XUI_OUT_OF_MEMORY, "Native allocation failed."); return XUI_OUT_OF_MEMORY; }
    catch (const std::exception& f) { error(XUI_NATIVE_ERROR, f.what()); return XUI_NATIVE_ERROR; }
    catch (...) { error(XUI_NATIVE_ERROR, "Unknown native exception."); return XUI_NATIVE_ERROR; }
}
std::wstring decode(xui_string s) {
    require(!s.reserved && s.length <= XUI_MAX_STRING_BYTES && (s.data || !s.length),
        XUI_INVALID_ARGUMENT, "Invalid UTF-8 span or length.");
    if (!s.length) return {};
    require(std::memchr(s.data, 0, s.length) == nullptr, XUI_INVALID_ARGUMENT, "Embedded NUL is not supported.");
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data, static_cast<int>(s.length), nullptr, 0);
    require(count > 0, XUI_INVALID_ARGUMENT, "Malformed UTF-8.");
    std::wstring result(static_cast<size_t>(count), 0);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data, static_cast<int>(s.length), result.data(), count);
    return result;
}
std::string encode(std::wstring_view text) {
    if (text.empty()) return {};
    // EDIT can publish the high surrogate before the second WM_CHAR arrives.
    const int count = WideCharToMultiByte(CP_UTF8, 0, text.data(),
        static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    require(count > 0, XUI_NATIVE_ERROR, "Native text conversion failed.");
    std::string result(static_cast<size_t>(count), 0);
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
        result.data(), count, nullptr, nullptr);
    return result;
}
struct CachedButtonStyle {
    std::weak_ptr<const xui::ButtonStyle> value;
    uint32_t depth;
};
using ButtonStyleCache = std::unordered_map<xui_handle, CachedButtonStyle>;
struct State {
    DWORD thread{GetCurrentThreadId()};
    std::unique_ptr<xui::Window> window;
    std::vector<xui_handle> handles;
    bool running{}, used{}, closed{};
    unsigned callbacks{};
    unsigned source_callbacks{};
    unsigned secret_callbacks{};
    xui_status callback_failure{};
    std::unique_ptr<ButtonStyleCache> button_styles;
};
struct Node {
    xui_handle handle{};
    uint32_t kind{};
    std::shared_ptr<State> owner;
    std::shared_ptr<xui::Element> element;
    std::shared_ptr<void> resource;
    std::vector<xui_handle> children;
    std::shared_ptr<const xui::CommandSet> commands;
    xui::CommandBindings bindings;
    uint64_t web_generation{};
    std::function<void()> prior_action;
    std::function<void(const std::wstring&)> prior_change;
    unsigned dispatching{};
    xui_callback callback{};
    void* context{};
    bool attached{};
    xui_handle button_style_identity{};
    xui_key_handler key_handler{};
    void* key_context{};
    xui_navigation_handler navigation_handler{};
    void* navigation_context{};
    xui_callback menu_callback{};
    void* menu_context{};
    bool menu_requesting{};
    uint64_t menu_revision{};
    std::vector<xui::MenuItem> menu_items;
    std::vector<std::wstring> menu_shell_paths;
    std::function<bool()> menu_current;
    xui::ShellMenuPresentation menu_presentation{};
};
std::mutex registry_mutex;
std::unordered_map<xui_handle, std::shared_ptr<Node>> registry;
xui_handle next_handle{1};
std::shared_ptr<Node> get(xui_handle handle, uint32_t kind = 0) {
    std::shared_ptr<Node> node;
    {
        std::lock_guard lock(registry_mutex);
        const auto found = registry.find(handle);
        require(found != registry.end(), XUI_INVALID_HANDLE, "Invalid or stale handle.");
        node = found->second;
    }
    require(node->owner->thread == GetCurrentThreadId(), XUI_WRONG_THREAD, "Use the creating UI thread.");
    require(!kind || node->kind == kind, XUI_WRONG_KIND, "Wrong handle kind.");
    return node;
}
void same(const std::shared_ptr<Node>& a, const std::shared_ptr<Node>& b) {
    require(a->owner == b->owner, XUI_INVALID_ARGUMENT, "Handles belong to different windows.");
}
void editable(const std::shared_ptr<State>& state) {
    require(!state->closed, XUI_CLOSED, "The window is closed.");
    require(!state->source_callbacks, XUI_BUSY, "Immutable source callbacks cannot mutate their window.");
}
void topology(const std::shared_ptr<State>& state) {
    editable(state);
    require(!state->used, XUI_BUSY, "Build the control tree before run.");
}
xui_handle insert(const std::shared_ptr<State>& owner, uint32_t kind, std::shared_ptr<xui::Element> element = {}) {
    require(owner->handles.size() < 65536, XUI_INVALID_ARGUMENT, "A window supports at most 65536 handles.");
    auto node = std::make_shared<Node>();
    node->kind = kind; node->owner = owner; node->element = std::move(element);
    std::lock_guard lock(registry_mutex);
    require(next_handle != UINT64_MAX, XUI_NATIVE_ERROR, "Handle generation exhausted.");
    node->handle = next_handle++;
    owner->handles.push_back(node->handle);
    try { registry.emplace(node->handle, node); }
    catch (...) { owner->handles.pop_back(); throw; }
    return node->handle;
}
template<class T> T& as(const std::shared_ptr<Node>& n) { return *static_cast<T*>(n->element.get()); }
xui::Control& control(const std::shared_ptr<Node>& n) {
    auto* c = dynamic_cast<xui::Control*>(n->element.get());
    require(c != nullptr, XUI_WRONG_KIND, "Expected a control.");
    return *c;
}
void wire_feature(const std::shared_ptr<Node>& n);
bool feature_key(const std::shared_ptr<Node>& n, const xui::KeyEvent& e);
void callback_result(const std::shared_ptr<State>& state) {
    require(!state->callback_failure, XUI_CALLBACK_FAILED, "A foreign callback failed. The window was closed.");
}
xui_handle event_target(const std::shared_ptr<State>& owner, const xui::Control* target) {
    if (!target) return 0;
    std::lock_guard lock(registry_mutex);
    for (auto h : owner->handles) {
        const auto child = registry.find(h);
        if (child != registry.end() && child->second->element.get() == target) return h;
    }
    return 0;
}
void dispatch(const std::weak_ptr<Node>& weak, uint32_t kind, uint64_t value = 0) noexcept {
    auto n = weak.lock();
    if (!n || !n->callback || n->owner->closed || n->owner->callback_failure) return;
    auto s = n->owner;
    if (n->dispatching) { s->callback_failure = XUI_BUSY; s->window->close(); return; }
    ++s->callbacks; ++n->dispatching;
    const xui_event event{sizeof(xui_event), kind, n->handle, value};
    xui_status result{};
    try { result = n->callback(n->context, &event); }
    catch (...) { result = XUI_CALLBACK_FAILED; }
    --s->callbacks; --n->dispatching;
    if (result) { s->callback_failure = result; s->window->close(); }
}
void wire(const std::shared_ptr<Node>& n) {
    std::weak_ptr<Node> weak = n;
    if (auto* c = dynamic_cast<xui::Control*>(n->element.get()))
        c->on_focus([weak] { dispatch(weak, XUI_FOCUS_ENTERED); });
    switch (n->kind) {
    case XUI_WINDOW:
        n->owner->window->on_key([weak](const xui::KeyEvent& e) {
            if (auto node = weak.lock(); node && node->key_handler && !node->owner->callback_failure) {
                const auto target = event_target(node->owner, e.target);
                const xui_key_event event{sizeof(xui_key_event), static_cast<uint32_t>(e.key),
                    uint32_t(e.control) | uint32_t(e.shift) << 1 | uint32_t(e.alt) << 2, 0, target};
                uint32_t handled{};
                ++node->owner->callbacks;
                xui_status status{};
                try { status = node->key_handler(node->key_context, &event, &handled); }
                catch (...) { status = XUI_CALLBACK_FAILED; }
                --node->owner->callbacks;
                if (status || handled > 1) {
                    node->owner->callback_failure = status ? status : XUI_INVALID_ARGUMENT;
                    node->owner->window->close(); return true;
                }
                if (handled) return true;
            }
            if (auto node = weak.lock(); node && feature_key(node, e)) return true;
            dispatch(weak, XUI_KEY, static_cast<uint64_t>(e.key) |
                (static_cast<uint64_t>(e.control) << 32) | (static_cast<uint64_t>(e.shift) << 33) |
                (static_cast<uint64_t>(e.alt) << 34));
            return false;
        }); break;
    case XUI_BUTTON: as<xui::Button>(n).on_click([weak] {
        if (auto node = weak.lock(); node && node->prior_action) node->prior_action();
        dispatch(weak, XUI_CLICK);
    }); break;
    case XUI_TOGGLE: as<xui::Toggle>(n).on_change([weak](bool value) { dispatch(weak, XUI_CHANGE, value); }); break;
    case XUI_TEXT_INPUT:
        as<xui::TextInput>(n).on_change([weak](const std::wstring& text) {
            if (auto node = weak.lock(); node && node->prior_change) node->prior_change(text);
            dispatch(weak, XUI_CHANGE);
        });
        as<xui::TextInput>(n).on_submit([weak] { dispatch(weak, XUI_SUBMIT); }); break;
    case XUI_FILE_LIST:
        as<xui::FileList>(n).on_selection_change([weak] { dispatch(weak, XUI_SELECTION); });
        as<xui::FileList>(n).on_view_change([weak] { dispatch(weak, XUI_VIEW); }); break;
    default: wire_feature(n); break;
    }
}
struct Prepared {
    xui_property value;
    std::shared_ptr<Node> node;
    std::wstring text;
};
}

uint32_t XUI_CALL xui_abi_version() noexcept { return XUI_ABI_VERSION; }
xui_status XUI_CALL xui_error_copy(char* buffer, uint32_t capacity, uint32_t* required, xui_status* status) noexcept {
    if (!required || !status || (!buffer && capacity)) return XUI_INVALID_ARGUMENT;
    *required = static_cast<uint32_t>(std::strlen(last_message.data()));
    *status = last_status;
    if (capacity < *required) return XUI_BUFFER_TOO_SMALL;
    if (*required) std::memcpy(buffer, last_message.data(), *required);
    return XUI_OK;
}
xui_status XUI_CALL xui_window_create(const xui_window_options* options, xui_handle* result) noexcept {
    return boundary([&] {
        require(result != nullptr, XUI_INVALID_ARGUMENT, "Missing output handle.");
        *result = 0;
        require(options && options->size == sizeof(*options), XUI_VERSION_MISMATCH, "Window options size mismatch.");
        require(options->version == XUI_ABI_VERSION, XUI_VERSION_MISMATCH, "XUI ABI version mismatch.");
        require(!options->reserved && options->theme <= 2 && std::isfinite(options->width) &&
            std::isfinite(options->height) && options->width > 0 && options->height > 0 &&
            options->width <= 32768 && options->height <= 32768, XUI_INVALID_ARGUMENT, "Invalid window options.");
        auto state = std::make_shared<State>();
        state->window = std::make_unique<xui::Window>(xui::WindowOptions{
            decode(options->title), {options->width, options->height}, static_cast<xui::ThemeMode>(options->theme)});
        *result = insert(state, XUI_WINDOW);
    });
}
xui_status XUI_CALL xui_window_destroy(xui_handle window) noexcept {
    return boundary([&] {
        auto n = get(window, XUI_WINDOW); auto s = n->owner;
        require(!s->running && !s->callbacks, XUI_BUSY, "Close and return from run before destroy.");
        std::vector<std::shared_ptr<Node>> removed;
        removed.reserve(s->handles.size());
        {
            std::lock_guard lock(registry_mutex);
            for (auto h : s->handles) {
                auto it = registry.find(h);
                if (it == registry.end()) continue;
                it->second->callback = nullptr; it->second->context = nullptr;
                removed.push_back(std::move(it->second)); registry.erase(it);
            }
        }
        s->window.reset();
    });
}
xui_status XUI_CALL xui_window_run(xui_handle window) noexcept {
    return boundary([&] {
        auto n = get(window, XUI_WINDOW); auto s = n->owner;
        topology(s); s->used = true; s->running = true;
        struct Reset { State& s; ~Reset() { s.running = false; s.closed = true; } } reset{*s};
        const int result = xui::Application::run(*s->window);
        callback_result(s);
        if (result) throw std::runtime_error(encode(s->window->error()));
    });
}
xui_status XUI_CALL xui_window_close(xui_handle window) noexcept {
    return boundary([&] { auto n = get(window, XUI_WINDOW); n->owner->window->close(); });
}
xui_status XUI_CALL xui_window_callback_error(xui_handle window, xui_status* status) noexcept {
    return boundary([&] {
        require(status != nullptr, XUI_INVALID_ARGUMENT, "Missing callback status.");
        *status = get(window, XUI_WINDOW)->owner->callback_failure;
    });
}
xui_status XUI_CALL xui_stack_create(xui_handle window, uint32_t axis, xui_handle* result) noexcept {
    return boundary([&] {
        require(result != nullptr, XUI_INVALID_ARGUMENT, "Missing output handle."); *result = 0;
        auto n = get(window, XUI_WINDOW); topology(n->owner);
        require(axis <= 1, XUI_INVALID_ARGUMENT, "Invalid stack axis.");
        *result = insert(n->owner, XUI_STACK, std::make_shared<xui::Stack>(static_cast<xui::Axis>(axis)));
    });
}
xui_status XUI_CALL xui_create(xui_handle window, uint32_t kind, xui_string name, xui_handle content, xui_handle* result) noexcept {
    return boundary([&] {
        require(result != nullptr, XUI_INVALID_ARGUMENT, "Missing output handle."); *result = 0;
        auto n = get(window, XUI_WINDOW); topology(n->owner);
        require(kind == XUI_SCROLL_VIEW || !content, XUI_INVALID_ARGUMENT, "Content is only valid for ScrollView.");
        auto text = decode(name);
        std::shared_ptr<xui::Element> element;
        std::shared_ptr<Node> child;
        switch (kind) {
        case XUI_LABEL: element = std::make_shared<xui::Label>(std::move(text)); break;
        case XUI_BUTTON: element = std::make_shared<xui::Button>(std::move(text)); break;
        case XUI_TOGGLE: element = std::make_shared<xui::Toggle>(std::move(text)); break;
        case XUI_TEXT_INPUT: element = std::make_shared<xui::TextInput>(std::move(text)); break;
        case XUI_FILE_LIST: element = std::make_shared<xui::FileList>(std::move(text)); break;
        case XUI_IMAGE: element = std::make_shared<xui::Image>(std::move(text)); break;
        case XUI_SCROLL_VIEW:
            child = get(content); same(n, child);
            require(child->element && !child->attached, XUI_INVALID_ARGUMENT, "Content already has a parent.");
            element = std::make_shared<xui::ScrollView>(child->element, std::move(text)); break;
        default: throw Failure{XUI_WRONG_KIND, "Invalid control kind."};
        }
        *result = insert(n->owner, kind, std::move(element));
        if (child) child->attached = true;
    });
}
xui_status XUI_CALL xui_stack_add(xui_handle stack, xui_handle child, float flex) noexcept {
    return boundary([&] {
        auto n = get(stack, XUI_STACK); auto c = get(child); same(n, c); topology(n->owner);
        require(c->element && !c->attached && std::isfinite(flex) && flex >= 0,
            XUI_INVALID_ARGUMENT, "Invalid child or flex.");
        as<xui::Stack>(n).add(c->element, flex); c->attached = true;
    });
}
xui_status XUI_CALL xui_window_content(xui_handle window, xui_handle stack) noexcept {
    return boundary([&] {
        auto n = get(window, XUI_WINDOW); auto c = get(stack, XUI_STACK); same(n, c); topology(n->owner);
        require(!c->attached, XUI_INVALID_ARGUMENT, "Content already has a parent.");
        n->owner->window->set_content(std::static_pointer_cast<xui::Stack>(c->element)); c->attached = true;
    });
}
xui_status XUI_CALL xui_update(xui_handle window, const xui_property* properties, uint32_t count) noexcept {
    return boundary([&] {
        auto w = get(window, XUI_WINDOW); editable(w->owner);
        require(count <= XUI_MAX_BATCH && (properties || !count), XUI_INVALID_ARGUMENT, "Invalid property span.");
        std::vector<Prepared> prepared; prepared.reserve(count);
        for (uint32_t i = 0; i < count; ++i) {
            const auto p = properties[i];
            require(p.size == sizeof(p), XUI_VERSION_MISMATCH, "Property size mismatch.");
            require(!p.text.reserved, XUI_INVALID_ARGUMENT, "Reserved property fields must be zero.");
            auto n = get(p.target); same(w, n);
            Prepared next{p, n, {}};
            switch (p.property) {
            case XUI_TEXT:
                control(n);
                require(n->kind != XUI_PASSWORD_INPUT && n->kind != XUI_MULTILINE_TEXT && n->kind != XUI_RICH_TEXT,
                    XUI_WRONG_KIND, "Use the document or password API.");
                next.text = decode(p.text); break;
            case XUI_NAME: case XUI_AUTOMATION_ID: control(n); next.text = decode(p.text); break;
            case XUI_ENABLED: control(n); [[fallthrough]];
            case XUI_AUTO_SIZE:
                require(n->element && p.integer <= 1, XUI_INVALID_ARGUMENT, "Expected a boolean element property."); break;
            case XUI_CHECKED:
                require(n->kind == XUI_TOGGLE, XUI_WRONG_KIND, "Checked requires Toggle.");
                require(p.integer <= 1, XUI_INVALID_ARGUMENT, "Expected a boolean."); break;
            case XUI_FIXED_SIZE: case XUI_PREFERRED_SIZE: case XUI_MIN_SIZE: case XUI_MAX_SIZE:
                require(n->element && std::isfinite(p.a) && std::isfinite(p.b) && p.a >= 0 && p.b >= 0,
                    XUI_INVALID_ARGUMENT, "Invalid element size."); break;
            case XUI_PADDING:
                require(std::isfinite(p.b) && std::isfinite(p.c) && std::isfinite(p.d) &&
                    p.b >= 0 && p.c >= 0 && p.d >= 0, XUI_INVALID_ARGUMENT, "Invalid padding."); [[fallthrough]];
            case XUI_SPACING:
                require(n->kind == XUI_STACK, XUI_WRONG_KIND, "Expected Stack.");
                require(std::isfinite(p.a) && p.a >= 0, XUI_INVALID_ARGUMENT, "Invalid spacing or padding."); break;
            case XUI_SCROLL_OFFSET:
                require(n->kind == XUI_SCROLL_VIEW || n->kind == XUI_FILE_LIST, XUI_WRONG_KIND, "Expected scrollable control.");
                require(std::isfinite(p.a) && p.a >= 0, XUI_INVALID_ARGUMENT, "Invalid offset."); break;
            case XUI_THEME:
                require(n->kind == XUI_WINDOW, XUI_WRONG_KIND, "Theme requires Window.");
                require(p.integer <= 2, XUI_INVALID_ARGUMENT, "Invalid theme."); break;
            default: throw Failure{XUI_INVALID_ARGUMENT, "Unknown property."};
            }
            prepared.push_back(std::move(next));
        }
        for (auto& entry : prepared) {
            auto& p = entry.value; auto& n = entry.node;
            switch (p.property) {
            case XUI_TEXT:
                if (n->kind == XUI_TEXT_INPUT) as<xui::TextInput>(n).set_text(std::move(entry.text));
                else control(n).set_name(std::move(entry.text)); break;
            case XUI_NAME: control(n).set_name(std::move(entry.text)); break;
            case XUI_AUTOMATION_ID: control(n).set_automation_id(std::move(entry.text)); break;
            case XUI_ENABLED: control(n).set_enabled(p.integer != 0); break;
            case XUI_CHECKED: as<xui::Toggle>(n).set_checked(p.integer != 0); break;
            case XUI_AUTO_SIZE: n->element->set_auto_size(p.integer != 0); break;
            case XUI_FIXED_SIZE: n->element->set_fixed_size({p.a, p.b}); break;
            case XUI_PREFERRED_SIZE: n->element->set_preferred_size({p.a, p.b}); break;
            case XUI_MIN_SIZE: n->element->set_minimum_size({p.a, p.b}); break;
            case XUI_MAX_SIZE: n->element->set_maximum_size({p.a, p.b}); break;
            case XUI_SPACING: as<xui::Stack>(n).set_spacing(p.a); break;
            case XUI_PADDING: as<xui::Stack>(n).set_padding({p.a, p.b, p.c, p.d}); break;
            case XUI_SCROLL_OFFSET:
                if (n->kind == XUI_SCROLL_VIEW) as<xui::ScrollView>(n).set_offset(p.a);
                else as<xui::FileList>(n).scroll_to(p.a); break;
            case XUI_THEME: n->owner->window->set_theme(static_cast<xui::ThemeMode>(p.integer)); break;
            }
        }
    });
}
xui_status XUI_CALL xui_subscribe(xui_handle target, xui_callback callback, void* context) noexcept {
    return boundary([&] {
        auto n = get(target);
        if (callback) { editable(n->owner); wire(n); }
        n->callback = callback; n->context = callback ? context : nullptr;
    });
}
xui_status XUI_CALL xui_text_copy(xui_handle target, char* buffer, uint32_t capacity, uint32_t* required) noexcept {
    return boundary([&] {
        require(required && (buffer || !capacity), XUI_INVALID_ARGUMENT, "Invalid output span.");
        auto n = get(target); auto& c = control(n);
        require(n->kind != XUI_PASSWORD_INPUT, XUI_WRONG_KIND, "Use the scoped password receiver.");
        const auto text = encode(n->kind == XUI_TEXT_INPUT ? as<xui::TextInput>(n).text() :
            n->kind == XUI_MULTILINE_TEXT || n->kind == XUI_RICH_TEXT ? as<xui::DocumentText>(n).text() : c.name());
        *required = static_cast<uint32_t>(text.size());
        require(capacity >= *required, XUI_BUFFER_TOO_SMALL, "The output buffer is too small.");
        if (*required) std::memcpy(buffer, text.data(), *required);
    });
}
xui_status XUI_CALL xui_focus(xui_handle target, uint32_t select_all) noexcept {
    return boundary([&] {
        auto n = get(target); editable(n->owner);
        require(select_all <= 1, XUI_INVALID_ARGUMENT, "Expected a boolean.");
        require(n->owner->window->focus(control(n), select_all != 0), XUI_INVALID_ARGUMENT, "Focus was rejected.");
    });
}
xui_status XUI_CALL xui_invoke(xui_handle target) noexcept {
    return boundary([&] {
        auto n = get(target); editable(n->owner);
        require(n->kind == XUI_BUTTON || n->kind == XUI_TOGGLE, XUI_WRONG_KIND, "Invoke requires Button or Toggle.");
        require(control(n).invoke(), XUI_INVALID_ARGUMENT, "The control is disabled.");
        callback_result(n->owner);
    });
}
xui_status XUI_CALL xui_image_source(xui_handle image, xui_string path, uint32_t width, uint32_t height) noexcept {
    return boundary([&] {
        auto n = get(image, XUI_IMAGE); editable(n->owner); auto text = decode(path);
        require(width > 0 && height > 0 && width <= 1024 && height <= 1024,
            XUI_INVALID_ARGUMENT, "Invalid image dimensions.");
        if (text.empty()) as<xui::Image>(n).unload();
        else as<xui::Image>(n).set_source(std::move(text), {width, height});
    });
}
xui_status XUI_CALL xui_image_state(xui_handle image, uint32_t* state) noexcept {
    return boundary([&] {
        require(state != nullptr, XUI_INVALID_ARGUMENT, "Missing image state.");
        *state = static_cast<uint32_t>(as<xui::Image>(get(image, XUI_IMAGE)).status());
    });
}
xui_status XUI_CALL xui_list_items(xui_handle list, const xui_file_item* items, uint32_t count) noexcept {
    return boundary([&] {
        auto n = get(list, XUI_FILE_LIST); editable(n->owner);
        require(count <= 1000000 && (items || !count), XUI_INVALID_ARGUMENT, "Invalid item span.");
        auto rows = std::make_shared<std::vector<xui::FileItem>>(); rows->reserve(count);
        for (uint32_t i = 0; i < count; ++i) {
            const auto& item = items[i];
            require(item.size == sizeof(item), XUI_VERSION_MISMATCH, "File item size mismatch.");
            require(item.directory <= 1, XUI_INVALID_ARGUMENT, "Invalid directory flag.");
            rows->push_back({item.id, decode(item.name), decode(item.path), item.directory != 0});
        }
        as<xui::FileList>(n).set_items(std::move(rows)); callback_result(n->owner);
    });
}
xui_status XUI_CALL xui_list_filter(xui_handle list, xui_string query) noexcept {
    return boundary([&] {
        auto n = get(list, XUI_FILE_LIST); editable(n->owner);
        as<xui::FileList>(n).set_filter(decode(query)); callback_result(n->owner);
    });
}
xui_status XUI_CALL xui_list_select(xui_handle list, uint32_t index) noexcept {
    return boundary([&] {
        auto n = get(list, XUI_FILE_LIST); editable(n->owner);
        auto& value = as<xui::FileList>(n);
        if (index == UINT32_MAX) value.clear_selection();
        else {
            require(index < value.model().visible_indices().size(), XUI_INVALID_ARGUMENT, "Selection index is out of range.");
            value.select(index);
        }
        callback_result(n->owner);
    });
}
xui_status XUI_CALL xui_list_state(xui_handle list, uint32_t* count, uint64_t* id, uint32_t* has_selection) noexcept {
    return boundary([&] {
        require(count && id && has_selection, XUI_INVALID_ARGUMENT, "Missing list outputs.");
        auto n = get(list, XUI_FILE_LIST); const auto& model = as<xui::FileList>(n).model();
        *count = static_cast<uint32_t>(model.visible_indices().size());
        *has_selection = model.selected_id().has_value(); *id = model.selected_id().value_or(0);
    });
}

#include "c_api_features.inc"
#include "c_api_layout.inc"
#include "c_api_text.inc"

namespace {
constexpr uint32_t button_style_kind = 102;
struct ButtonStyleResource {
    std::shared_ptr<const xui::ButtonStyle> value;
    uint32_t depth;
    xui_handle identity{};
};
void prune_button_style(const std::shared_ptr<State>& owner, xui_handle identity) {
    if (!owner->button_styles) return;
    auto& cache = *owner->button_styles;
    const auto found = cache.find(identity);
    if (found != cache.end() && found->second.value.expired()) cache.erase(found);
    if (cache.empty()) owner->button_styles.reset();
}
void apply_button_style(const std::shared_ptr<Node>& n,
    std::shared_ptr<const xui::ButtonStyle> definition, xui_handle identity) {
    const auto previous = n->button_style_identity;
    as<xui::Button>(n).set_style(std::move(definition));
    n->button_style_identity = identity;
    prune_button_style(n->owner, previous);
}
void style_record(const xui_button_style_values* v) {
    require(v && v->size == sizeof(*v), XUI_INVALID_ARGUMENT, "Button style values size mismatch.");
    require(v->version == XUI_BUTTON_STYLE_VERSION, XUI_VERSION_MISMATCH, "Button style version mismatch.");
}
xui::ButtonStyleValues read_style_values(const xui_button_style_values* v) {
    style_record(v);
    require(!(v->mask & ~63u) && !v->reserved && !v->reserved_end,
        XUI_INVALID_ARGUMENT, "Invalid Button style mask or reserved fields.");
    xui::ButtonStyleValues result;
    auto color = [&](uint32_t bit, xui_theme_color value, auto& target) {
        require(value.light <= 0xffffff && value.dark <= 0xffffff &&
            ((v->mask & bit) || (!value.light && !value.dark)),
            XUI_INVALID_ARGUMENT, "Invalid or unused Button style color.");
        if (v->mask & bit) target = xui::ThemeColor{value.light, value.dark};
    };
    auto dimension = [&](float value, bool present) {
        require(std::isfinite(value) && value >= 0 && value <= 32768 && (present || value == 0),
            XUI_INVALID_ARGUMENT, "Invalid or unused Button style dimension.");
    };
    auto insets = [&](uint32_t bit, xui_style_insets value, auto& target) {
        const bool present = (v->mask & bit) != 0;
        dimension(value.left, present); dimension(value.top, present);
        dimension(value.right, present); dimension(value.bottom, present);
        if (present) target = xui::Insets{value.left, value.top, value.right, value.bottom};
    };
    color(1, v->background, result.background);
    color(2, v->foreground, result.foreground);
    color(4, v->border_brush, result.border_brush);
    insets(8, v->border_thickness, result.border_thickness);
    insets(16, v->padding, result.padding);
    dimension(v->corner_radius, (v->mask & 32) != 0);
    if (v->mask & 32) result.corner_radius = v->corner_radius;
    return result;
}
xui_button_style_values write_style_values(const xui::ButtonStyleValues& v) {
    xui_button_style_values result{sizeof(result), XUI_BUTTON_STYLE_VERSION};
    auto color = [&](uint32_t bit, const auto& value, auto& target) {
        if (value) { result.mask |= bit; target = {value->light, value->dark}; }
    };
    auto insets = [&](uint32_t bit, const auto& value, auto& target) {
        if (value) { result.mask |= bit; target = {value->left, value->top, value->right, value->bottom}; }
    };
    color(1, v.background, result.background); color(2, v.foreground, result.foreground);
    color(4, v.border_brush, result.border_brush);
    insets(8, v.border_thickness, result.border_thickness); insets(16, v.padding, result.padding);
    if (v.corner_radius) { result.mask |= 32; result.corner_radius = *v.corner_radius; }
    return result;
}
}
xui_status XUI_CALL xui_button_style_create(xui_handle window,
    const xui_button_style_options* options, xui_handle* result) noexcept {
    return boundary([&] {
        require(result, XUI_INVALID_ARGUMENT, "Missing style output handle."); *result = 0;
        auto n = get(window, XUI_WINDOW); editable(n->owner);
        require(options && options->size == sizeof(*options), XUI_INVALID_ARGUMENT, "Button style options size mismatch.");
        require(options->version == XUI_BUTTON_STYLE_VERSION, XUI_VERSION_MISMATCH, "Button style version mismatch.");
        require(!options->reserved && options->rule_count <= 256 && (options->rules || !options->rule_count),
            XUI_INVALID_ARGUMENT, "Invalid Button style rules.");
        auto values = read_style_values(&options->values);
        std::vector<xui::ButtonStyleRule> rules; rules.reserve(options->rule_count);
        for (uint32_t i = 0; i < options->rule_count; ++i) {
            const auto& rule = options->rules[i];
            require(rule.size == sizeof(rule) && rule.state <= XUI_BUTTON_STYLE_DISABLED,
                XUI_INVALID_ARGUMENT, "Invalid Button style rule size or state.");
            rules.push_back({static_cast<xui::ButtonStyleState>(rule.state), read_style_values(&rule.values)});
        }
        std::shared_ptr<const xui::ButtonStyle> base;
        uint32_t depth = 1;
        if (options->based_on) {
            auto b = get(options->based_on, button_style_kind); same(n, b);
            auto definition = resource<ButtonStyleResource>(b, button_style_kind);
            base = definition->value; depth = definition->depth + 1;
            require(depth <= 16, XUI_INVALID_ARGUMENT, "Button style inheritance exceeds 16 layers.");
        }
        auto definition = std::make_shared<ButtonStyleResource>(
            ButtonStyleResource{xui::ButtonStyle::create(std::move(values), std::move(rules), std::move(base)), depth});
        const auto handle = insert(n->owner, button_style_kind);
        try {
            if (!n->owner->button_styles) n->owner->button_styles = std::make_unique<ButtonStyleCache>();
            require(n->owner->button_styles->size() < 65536,
                XUI_INVALID_ARGUMENT, "A window supports at most 65536 cached Button styles.");
            n->owner->button_styles->emplace(handle, CachedButtonStyle{definition->value, depth});
        } catch (...) {
            revoke(get(handle));
            prune_button_style(n->owner, handle);
            throw;
        }
        definition->identity = handle;
        get(handle)->resource = std::move(definition);
        *result = handle;
    });
}
xui_status XUI_CALL xui_button_style_release(xui_handle style) noexcept {
    return boundary([&] {
        auto n = get(style, button_style_kind);
        const auto owner = n->owner;
        const auto identity = resource<ButtonStyleResource>(n, button_style_kind)->identity;
        revoke(n); n.reset();
        prune_button_style(owner, identity);
    });
}
xui_status XUI_CALL xui_button_style_reacquire(xui_handle window,
    xui_handle identity, xui_handle* result) noexcept {
    return boundary([&] {
        require(result, XUI_INVALID_ARGUMENT, "Missing style output handle."); *result = 0;
        auto n = get(window, XUI_WINDOW); editable(n->owner);
        if (!n->owner->button_styles) return;
        const auto found = n->owner->button_styles->find(identity);
        if (found == n->owner->button_styles->end()) return;
        if (auto value = found->second.value.lock()) {
            auto definition = std::make_shared<ButtonStyleResource>(
                ButtonStyleResource{std::move(value), found->second.depth, identity});
            const auto handle = insert(n->owner, button_style_kind);
            get(handle)->resource = std::move(definition);
            *result = handle;
        } else prune_button_style(n->owner, identity);
    });
}
xui_status XUI_CALL xui_button_try_set_style(xui_handle button,
    xui_handle identity, uint32_t* applied) noexcept {
    return boundary([&] {
        require(applied, XUI_INVALID_ARGUMENT, "Missing style application result."); *applied = 0;
        auto n = get(button, XUI_BUTTON); editable(n->owner);
        if (!n->owner->button_styles) return;
        const auto found = n->owner->button_styles->find(identity);
        if (found == n->owner->button_styles->end()) return;
        if (auto value = found->second.value.lock()) {
            apply_button_style(n, std::move(value), identity);
            *applied = 1;
        } else prune_button_style(n->owner, identity);
    });
}
xui_status XUI_CALL xui_button_set_style(xui_handle button, xui_handle style) noexcept {
    return boundary([&] {
        auto n = get(button, XUI_BUTTON); editable(n->owner);
        std::shared_ptr<const xui::ButtonStyle> definition;
        xui_handle identity{};
        if (style) {
            auto s = get(style, button_style_kind); same(n, s);
            const auto resource_value = resource<ButtonStyleResource>(s, button_style_kind);
            definition = resource_value->value;
            identity = resource_value->identity;
        }
        apply_button_style(n, std::move(definition), identity);
    });
}
xui_status XUI_CALL xui_button_set_style_values(xui_handle button, const xui_button_style_values* values) noexcept {
    return boundary([&] {
        auto n = get(button, XUI_BUTTON); editable(n->owner);
        auto prepared = read_style_values(values);
        as<xui::Button>(n).set_style_values(std::move(prepared));
    });
}
xui_status XUI_CALL xui_button_get_style_values(xui_handle button, uint32_t effective, xui_button_style_values* values) noexcept {
    return boundary([&] {
        auto n = get(button, XUI_BUTTON); style_record(values);
        require(effective <= 1, XUI_INVALID_ARGUMENT, "Invalid Button style value selector.");
        const auto& button_value = as<xui::Button>(n);
        const auto* selected = effective ? button_value.effective_style_values() : &button_value.style_values();
        *values = write_style_values(selected ? *selected : xui::ButtonStyleValues{});
    });
}
