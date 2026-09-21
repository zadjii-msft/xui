#include "xui/shell_commands.hpp"
#include "platform.hpp"
#include "context_menu.hpp"
#include "shell_commands_internal.hpp"
#include <shellapi.h>
#include <shlobj.h>
#include <commctrl.h>
#include <wrl/client.h>
#include <atomic>
#include <mutex>
#include <algorithm>

namespace xui {
std::atomic<ShellMenuTestAccess::Track> ShellMenuTestAccess::track{};
std::atomic<ShellMenuTestAccess::Create> ShellMenuTestAccess::create{};
namespace {
using Microsoft::WRL::ComPtr;
std::shared_ptr<const MenuIcon> copy_menu_icon(HBITMAP bitmap, std::size_t& remaining) {
    // HBMMENU_* values are pseudo-handles, not bitmaps. Never call an extension to paint them.
    const auto value = reinterpret_cast<INT_PTR>(bitmap);
    if (value >= -1 && value <= 12) return {};
    BITMAP source{};
    if (GetObjectW(bitmap, sizeof(source), &source) != sizeof(source)) {
        OutputDebugStringW(L"XUI Shell menu: unreadable icon; omitting icon.\n");
        return {};
    }
    if (source.bmWidth <= 0 || source.bmHeight <= 0 || source.bmWidth > 64 || source.bmHeight > 64) {
        OutputDebugStringW(L"XUI Shell menu: icon exceeds 64x64 limit; omitting icon.\n");
        return {};
    }
    const auto bytes = std::size_t(source.bmWidth) * source.bmHeight * 4;
    if (bytes > remaining) {
        OutputDebugStringW(L"XUI Shell menu: icon budget exceeded; omitting icon.\n");
        return {};
    }
    struct Context {
        HDC dc{CreateCompatibleDC(nullptr)};
        ~Context() { if (dc) DeleteDC(dc); }
    } context;
    win32_require(context.dc != nullptr, "Copy Shell menu icon");
    auto icon = std::make_shared<MenuIcon>();
    icon->width = source.bmWidth; icon->height = source.bmHeight;
    icon->pixels.resize(bytes / 4);
    BITMAPINFO info{};
    info.bmiHeader = {sizeof(BITMAPINFOHEADER), source.bmWidth, -source.bmHeight, 1, 32, BI_RGB};
    if (GetDIBits(context.dc, bitmap, 0, icon->height, icon->pixels.data(), &info, DIB_RGB_COLORS) != source.bmHeight) {
        OutputDebugStringW(L"XUI Shell menu: cannot copy icon pixels; omitting icon.\n");
        return {};
    }
    const bool alpha = source.bmBitsPixel == 32 &&
        std::any_of(icon->pixels.begin(), icon->pixels.end(), [](auto pixel) { return (pixel >> 24) != 0; });
    for (auto& pixel : icon->pixels) {
        if (!alpha) pixel |= 0xff000000;
        else {
            const auto a = pixel >> 24;
            pixel = (a << 24) | (std::min((pixel >> 16) & 255, a) << 16) |
                (std::min((pixel >> 8) & 255, a) << 8) | std::min(pixel & 255, a);
        }
    }
    remaining -= bytes;
    return icon;
}
struct ActiveShellMenu {
    HWND root{};
    explicit ActiveShellMenu(HWND owner) {
        if (!IsWindow(owner) || GetWindowThreadProcessId(owner, nullptr) != GetCurrentThreadId())
            throw std::invalid_argument("Shell owner must belong to this thread");
        root = GetAncestor(owner, GA_ROOT);
        if (GetPropW(root, active_menu_property)) throw std::logic_error("A context menu is already open");
        win32_require(SetPropW(root, active_menu_property, this) != FALSE, "Register active Shell menu");
    }
    ~ActiveShellMenu() { if (IsWindow(root)) RemovePropW(root, active_menu_property); }
};
struct TrackedCommand {
    std::optional<ItemKey> shell;
    std::function<void()> app;
};
class NativeShellProvider final : public ShellCommandProvider {
    static constexpr UINT first_app = 0x8000;
    const DWORD thread_{GetCurrentThreadId()};
    HWND owner_{};
    HMENU menu_{};
    ComPtr<IContextMenu> context_;
    ComPtr<IContextMenu2> context2_;
    ComPtr<IContextMenu3> context3_;
    std::uint64_t epoch_{};
    std::set<UINT> verbs_;
    UINT next_synthetic_{0x10000};
    bool tracking_{};
    std::vector<MenuItem> app_commands_;
    void thread() const {
        if (GetCurrentThreadId() != thread_) throw std::logic_error("Shell commands belong to their STA thread");
        if (!owner_ || !IsWindow(owner_)) throw std::logic_error("Shell command owner is closed");
    }
    void append_apps(const std::vector<MenuItem>& app_commands) {
        if (!app_commands_.empty()) throw std::logic_error("App commands are already merged");
        if (app_commands.size() > 4096) throw std::length_error("App menu exceeds 4096 items");
        for (const auto& item : app_commands)
            if (item.text.size() > 32767 || item.text.find(L'\0') != std::wstring::npos)
                throw std::invalid_argument("Invalid app menu label");
        app_commands_ = app_commands;
        if (!app_commands_.empty() && GetMenuItemCount(menu_) > 0)
            win32_require(AppendMenuW(menu_, MF_SEPARATOR, 0, nullptr) != FALSE, "Separate Shell and app commands");
        for (size_t i = 0; i < app_commands_.size(); ++i) {
            const auto& item = app_commands_[i];
            const UINT flags = item.separator ? MF_SEPARATOR :
                MF_STRING | (item.enabled ? MF_ENABLED : MF_GRAYED) | (item.checked ? MF_CHECKED : MF_UNCHECKED);
            win32_require(AppendMenuW(menu_, flags, first_app + i, item.separator ? nullptr : item.text.c_str()) != FALSE,
                "Append app command to Shell menu");
        }
    }
    void initialize(const std::vector<MenuItem>& app_commands) {
        context_.As(&context2_); context_.As(&context3_);
        menu_ = CreatePopupMenu(); win32_require(menu_ != nullptr, "Create Shell menu");
        try {
            hr_require(context_->QueryContextMenu(menu_, 0, 1, first_app - 1, CMF_NORMAL), "Discover Shell commands");
            append_apps(app_commands);
            win32_require(SetWindowSubclass(owner_, forward, reinterpret_cast<UINT_PTR>(this), reinterpret_cast<DWORD_PTR>(this)) != 0,
                "Observe Shell command owner");
        }
        catch (...) { DestroyMenu(menu_); menu_ = nullptr; throw; }
        static std::atomic<std::uint64_t> next{1}; epoch_ = next.fetch_add(1);
    }
    static std::optional<bool> enabled_command(HMENU menu, UINT id, unsigned depth, size_t& count) {
        if (depth > 8) throw std::length_error("Shell menu nesting exceeds eight");
        const int total = GetMenuItemCount(menu);
        win32_require(total >= 0, "Read Shell menu size");
        for (int i = 0; i < total; ++i) {
            if (++count > 8192) throw std::length_error("Merged menu exceeds 8192 items");
            MENUITEMINFOW item{sizeof(item)}; item.fMask = MIIM_ID | MIIM_STATE | MIIM_FTYPE | MIIM_SUBMENU;
            win32_require(GetMenuItemInfoW(menu, i, TRUE, &item) != FALSE, "Read selected Shell command");
            const bool enabled = !(item.fState & (MFS_DISABLED | MFS_GRAYED));
            if (!item.hSubMenu && item.wID == id) return enabled && !(item.fType & MFT_SEPARATOR);
            if (item.hSubMenu)
                if (const auto found = enabled_command(item.hSubMenu, id, depth + 1, count)) return enabled && *found;
        }
        return {};
    }
    static LRESULT CALLBACK forward(HWND hwnd, UINT message, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR data) noexcept {
        auto* self = reinterpret_cast<NativeShellProvider*>(data);
        if (self->tracking_ && (message == WM_INITMENUPOPUP || message == WM_DRAWITEM || message == WM_MEASUREITEM || message == WM_MENUCHAR)) {
            LRESULT result{};
            if (self->context3_ && SUCCEEDED(self->context3_->HandleMenuMsg2(message, w, l, &result))) return result;
            if (self->context2_ && message != WM_MENUCHAR && SUCCEEDED(self->context2_->HandleMenuMsg(message, w, l))) return 0;
        }
        if (message == WM_NCDESTROY) { RemoveWindowSubclass(hwnd, forward, id); self->owner_ = nullptr; }
        return DefSubclassProc(hwnd, message, w, l);
    }
    std::vector<ShellCommandInfo> read(HMENU menu, std::stop_token stop, unsigned depth, std::size_t& count,
        std::size_t& icon_budget, bool gallery = false) {
        if (depth > 8) throw std::length_error("Shell menu nesting exceeds eight");
        std::vector<ShellCommandInfo> result;
        const int total = GetMenuItemCount(menu);
        for (int i = 0; i < total; ++i) {
            if (stop.stop_requested()) return {};
            if (++count > 4096) throw std::length_error("Shell menu exceeds 4096 items");
            MENUITEMINFOW item{sizeof(item)}; item.fMask = MIIM_STRING | MIIM_ID | MIIM_STATE | MIIM_FTYPE | MIIM_SUBMENU | MIIM_BITMAP;
            win32_require(GetMenuItemInfoW(menu, i, TRUE, &item) != 0, "Read Shell command");
            if (item.cch > 1024) throw std::length_error("Shell command label exceeds 1024 characters");
            wchar_t label[1025]{};
            item.dwTypeData = label; item.cch = 1025;
            win32_require(GetMenuItemInfoW(menu, i, TRUE, &item) != 0, "Read Shell command label");
            ShellCommandInfo entry;
            entry.key = {item.hSubMenu || (item.fType & MFT_SEPARATOR) ? next_synthetic_++ : item.wID, epoch_};
            entry.label = label; entry.enabled = !(item.fState & (MFS_DISABLED | MFS_GRAYED));
            if (const auto tab = entry.label.find(L'\t'); tab != std::wstring::npos) {
                entry.shortcut_hints.push_back(entry.label.substr(tab + 1)); entry.label.resize(tab);
            }
            entry.has_native_icon = item.hbmpItem != nullptr;
            entry.checked = (item.fState & MFS_CHECKED) != 0; entry.separator = (item.fType & MFT_SEPARATOR) != 0;
            entry.native_only = item.hSubMenu || (item.fType & MFT_OWNERDRAW) || entry.label.empty();
            if (!entry.separator && !(item.fType & MFT_OWNERDRAW) && item.hbmpItem)
                entry.native_icon = copy_menu_icon(item.hbmpItem, icon_budget);
            if (!item.hSubMenu && !entry.separator && item.wID >= 1 && item.wID <= 0x7fff) {
                verbs_.insert(item.wID);
                if (!gallery) {
                    wchar_t verb[1025]{};
                    if (SUCCEEDED(context_->GetCommandString(item.wID - 1, GCS_VERBW, nullptr, reinterpret_cast<LPSTR>(verb), 1024)))
                        entry.verb = verb;
                }
            }
            if (item.hSubMenu && !gallery) entry.children = read(item.hSubMenu, stop, depth + 1, count, icon_budget);
            result.push_back(std::move(entry));
        }
        return result;
    }
public:
    HWND owner() const { thread(); return owner_; }
    void merge_apps(const std::vector<MenuItem>& apps) { thread(); append_apps(apps); }
    NativeShellProvider(HWND owner, const std::vector<std::wstring>& paths, const std::vector<MenuItem>& app_commands = {}) : owner_(owner) {
        if (!IsWindow(owner) || GetWindowThreadProcessId(owner, nullptr) != thread_) throw std::invalid_argument("Shell owner must belong to this thread");
        APTTYPE apartment{}; APTTYPEQUALIFIER qualifier{};
        hr_require(CoGetApartmentType(&apartment, &qualifier), "Read Shell COM apartment");
        if (apartment != APTTYPE_STA && apartment != APTTYPE_MAINSTA) throw std::logic_error("Shell commands require an STA");
        if (paths.empty() || paths.size() > 256) throw std::invalid_argument("Shell selection must contain 1 to 256 paths");
        struct Pidls {
            std::vector<PIDLIST_ABSOLUTE> values;
            ~Pidls() { for (auto value : values) CoTaskMemFree(value); }
        } pidls;
        pidls.values.reserve(paths.size());
        for (const auto& path : paths) {
            if (path.empty() || path.size() > 32767 || path.find(L'\0') != std::wstring::npos)
                throw std::invalid_argument("Invalid Shell path");
            PIDLIST_ABSOLUTE value{};
            hr_require(SHParseDisplayName(path.c_str(), nullptr, &value, 0, nullptr), "Parse Shell selection");
            pidls.values.push_back(value);
        }
        ComPtr<IShellFolder> folder; PCUITEMID_CHILD child{};
        hr_require(SHBindToParent(pidls.values[0], IID_PPV_ARGS(&folder), &child), "Bind Shell parent");
        std::vector<PCUITEMID_CHILD> children{child};
        for (std::size_t i = 1; i < pidls.values.size(); ++i) {
            {
                auto first = ILCloneFull(pidls.values[0]), other = ILCloneFull(pidls.values[i]);
                if (!first || !other) { CoTaskMemFree(first); CoTaskMemFree(other); throw std::bad_alloc{}; }
                ILRemoveLastID(first); ILRemoveLastID(other);
                const bool equal = ILIsEqual(first, other) != FALSE; CoTaskMemFree(first); CoTaskMemFree(other);
                if (!equal) throw std::invalid_argument("Shell selection must share one parent");
            }
            children.push_back(ILFindLastID(pidls.values[i]));
        }
        hr_require(folder->GetUIObjectOf(owner_, static_cast<UINT>(children.size()), children.data(), IID_IContextMenu, nullptr,
            reinterpret_cast<void**>(context_.GetAddressOf())), "Get Shell context menu");
        initialize(app_commands);
    }
    NativeShellProvider(HWND owner, IContextMenu* context, const std::vector<MenuItem>& app_commands) : owner_(owner), context_(context) {
        thread();
        if (!context_) throw std::invalid_argument("Shell context is required");
        initialize(app_commands);
    }
    ~NativeShellProvider() override {
        if (owner_) RemoveWindowSubclass(owner_, forward, reinterpret_cast<UINT_PTR>(this));
        if (menu_) DestroyMenu(menu_);
    }
    std::vector<ShellCommandInfo> discover(std::stop_token cancellation) override {
        thread(); verbs_.clear(); next_synthetic_ = 0x10000;
        std::size_t count{}, icon_budget{4 * 1024 * 1024}; return read(menu_, cancellation, 0, count, icon_budget);
    }
    std::vector<ShellCommandInfo> gallery_commands(std::stop_token cancellation) {
        thread(); verbs_.clear(); next_synthetic_ = 0x10000;
        std::size_t count{}, icon_budget{4 * 1024 * 1024}; return read(menu_, cancellation, 0, count, icon_budget, true);
    }
    void invoke(ItemKey key) override {
        thread();
        if (key.version != epoch_ || key.id > 0x7fff || !verbs_.contains(static_cast<UINT>(key.id)))
            throw std::invalid_argument("Stale Shell command");
        std::size_t count{};
        if (!enabled_command(menu_, static_cast<UINT>(key.id), 0, count).value_or(tracking_))
            throw std::logic_error("Shell command is no longer enabled");
        CMINVOKECOMMANDINFOEX info{sizeof(info)};
        info.fMask = CMIC_MASK_UNICODE | CMIC_MASK_PTINVOKE; info.hwnd = owner_;
        info.lpVerb = MAKEINTRESOURCEA(key.id - 1); info.lpVerbW = MAKEINTRESOURCEW(key.id - 1);
        info.nShow = SW_SHOWNORMAL; GetCursorPos(&info.ptInvoke);
        hr_require(context_->InvokeCommand(reinterpret_cast<CMINVOKECOMMANDINFO*>(&info)), "Invoke Shell command");
    }
    TrackedCommand track(Point point, const std::function<bool()>& current) {
        thread();
        if (tracking_) throw std::logic_error("Shell menu is already open");
        tracking_ = true;
        struct Scope { NativeShellProvider* self; ~Scope() { self->tracking_ = false; } } scope{this};
        SetLastError(ERROR_SUCCESS);
        const auto hook = ShellMenuTestAccess::track.load();
        const auto chosen = hook ? hook(menu_, owner_, point) : static_cast<UINT>(TrackPopupMenuEx(menu_,
            TPM_RETURNCMD | TPM_RIGHTBUTTON, static_cast<int>(point.x), static_cast<int>(point.y), owner_, nullptr));
        const DWORD error = GetLastError();
        if (!chosen && error != ERROR_SUCCESS) {
            SetLastError(error); win32_require(false, "Show Shell menu");
        }
        if (!chosen || !IsWindow(owner_) || !IsWindowEnabled(owner_) || (current && !current())) return {};
        size_t count{};
        if (const auto enabled = enabled_command(menu_, chosen, 0, count); enabled && !*enabled) {
            OutputDebugStringW(L"XUI: Selected menu command is disabled.\n"); return {};
        }
        if (chosen >= first_app) {
            const auto index = chosen - first_app;
            if (index >= app_commands_.size()) throw std::invalid_argument("Invalid app menu command ID");
            const auto& item = app_commands_[index];
            if (!item.enabled || item.separator) {
                OutputDebugStringW(L"XUI: Selected app command is disabled or a separator.\n"); return {};
            }
            return {{}, item.action};
        }
        // Extensions can discard dynamic submenu entries when tracking ends.
        verbs_.insert(chosen);
        const ItemKey key{chosen, epoch_}; invoke(key); return {key, {}};
    }
};
template<class Create>
std::optional<ItemKey> track_menu(HWND owner, Point point, const std::function<bool()>& current, Create&& create) {
    if (current && !current()) return {};
    TrackedCommand selected;
    {
        ActiveShellMenu active(owner);
        auto provider = create();
        if (current && !current()) return {};
        selected = provider->track(point, current);
    }
    if (selected.app && IsWindow(owner) && IsWindowEnabled(owner) && (!current || current())) selected.app();
    return selected.shell;
}
}
std::shared_ptr<ShellCommandProvider> shell_command_provider(void* owner, const std::vector<std::wstring>& paths) {
    return std::make_shared<NativeShellProvider>(static_cast<HWND>(owner), paths);
}
std::optional<ItemKey> track_shell_commands(void* owner, const std::vector<std::wstring>& paths, Point point) {
    return track_shell_commands(owner, paths, point, {}, {});
}
std::optional<ItemKey> track_shell_commands(void* owner, const std::vector<std::wstring>& paths, Point point,
    const std::vector<MenuItem>& app_commands, std::function<bool()> current) {
    const auto hwnd = static_cast<HWND>(owner);
    return track_menu(hwnd, point, current, [&] { return std::make_unique<NativeShellProvider>(hwnd, paths, app_commands); });
}
std::optional<ItemKey> track_shell_commands(std::shared_ptr<ShellCommandProvider> provider, Point point,
    const std::vector<MenuItem>& apps, std::function<bool()> current) {
    auto native = std::dynamic_pointer_cast<NativeShellProvider>(provider);
    if (!native) throw std::invalid_argument("The Windows fallback requires a native Shell provider");
    provider.reset();
    if (current && !current()) return {};
    const auto owner = native->owner();
    return track_menu(owner, point, current, [&] {
        native->merge_apps(apps); return std::exchange(native, {});
    });
}
std::optional<ItemKey> ShellMenuTestAccess::run(HWND owner, IContextMenu* context, const std::vector<MenuItem>& items,
    std::function<bool()> current) {
    return track_menu(owner, {}, current, [&] { return std::make_unique<NativeShellProvider>(owner, context, items); });
}
std::shared_ptr<ShellCommandProvider> ShellMenuTestAccess::provider(HWND owner, IContextMenu* context) {
    return std::make_shared<NativeShellProvider>(owner, context, std::vector<MenuItem>{});
}

struct AsyncShellMenu::State {
    HWND parent{};
    std::vector<std::wstring> paths;
    HANDLE wake{CreateEventW(nullptr, FALSE, FALSE, nullptr)};
    std::stop_source stop;
    std::atomic<bool> ready{}, finished{};
    std::vector<ShellCommandInfo> commands;
    std::string discovery_error;
    std::exception_ptr failure;
    std::optional<size_t> app;
    std::mutex mutex;
    HWND owner{};
    bool started{}, prefetch{};
    bool canonical_verbs{};
    enum class Action { none, invoke, windows } action{};
    ItemKey key{};
    Point point{};
    std::vector<MenuItem> apps;
    State() { win32_require(wake != nullptr, "Create Shell request event"); }
    ~State() { CloseHandle(wake); }
    void cancel() {
        stop.request_stop();
        std::lock_guard lock(mutex);
        if (!started) finished.store(true, std::memory_order_release);
        if (owner) PostMessageW(owner, WM_CANCELMODE, 0, 0);
        SetEvent(wake);
    }
    bool current() const {
        if (stop.stop_requested()) return false;
        DWORD_PTR valid{};
        return SendMessageTimeoutW(parent, shell_validation_message(), reinterpret_cast<WPARAM>(this), 0,
            SMTO_ABORTIFHUNG, 1000, &valid) && valid == 1 && !stop.stop_requested();
    }
};
UINT shell_validation_message() {
    static const UINT message = RegisterWindowMessageW(L"Xui.ShellMenu.Validate.1");
    return message;
}
namespace {
// Shell extensions may block inside COM. Keep one running request and one latest
// pending request, not one thread per click. Cancellation never joins the worker.
class ShellWorker {
    std::mutex mutex_;
    std::shared_ptr<AsyncShellMenu::State> active_, pending_;
    HANDLE wake_{CreateEventW(nullptr, FALSE, FALSE, nullptr)};
    bool stopping_{};

    static void pump(HANDLE event) {
        const auto result = MsgWaitForMultipleObjectsEx(1, &event, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
        win32_require(result != WAIT_FAILED, "Wait for Shell request");
        if (result == WAIT_OBJECT_0 + 1) {
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&message); DispatchMessageW(&message);
            }
        }
    }
    static std::shared_ptr<ShellCommandProvider> create(const std::shared_ptr<AsyncShellMenu::State>& state) {
        if (const auto factory = ShellMenuTestAccess::create.load()) return factory(state->owner, state->paths);
        return shell_command_provider(state->owner, state->paths);
    }
    static void run(const std::shared_ptr<AsyncShellMenu::State>& state) {
        if (state->stop.stop_requested()) return;
        const HWND owner = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"STATIC", L"XUI Shell command owner",
            WS_POPUP, 0, 0, 0, 0, state->parent, nullptr, GetModuleHandleW(nullptr), nullptr);
        win32_require(owner != nullptr, "Create Shell STA owner");
        struct Owner {
            std::shared_ptr<AsyncShellMenu::State> state;
            HWND window;
            ~Owner() {
                { std::lock_guard lock(state->mutex); state->owner = nullptr; }
                DestroyWindow(window);
            }
        } lifetime{state, owner};
        { std::lock_guard lock(state->mutex); state->owner = owner; }
        std::shared_ptr<ShellCommandProvider> provider;
        try {
            provider = create(state);
            if (!state->stop.stop_requested()) {
                std::vector<ShellCommandInfo> commands;
                if (auto native = std::dynamic_pointer_cast<NativeShellProvider>(provider); native && !state->canonical_verbs)
                    commands = native->gallery_commands(state->stop.get_token());
                else commands = provider->discover(state->stop.get_token());
                if (!state->prefetch) state->commands = std::move(commands);
            }
        } catch (const std::exception& error) { state->discovery_error = error.what(); }
        state->ready.store(true, std::memory_order_release);
        if (state->prefetch) return;
        for (;;) {
            if (state->stop.stop_requested()) return;
            AsyncShellMenu::State::Action action;
            { std::lock_guard lock(state->mutex); action = state->action; }
            if (action != AsyncShellMenu::State::Action::none) {
                if (action == AsyncShellMenu::State::Action::invoke) {
                    if (!provider) throw std::logic_error("Shell discovery did not produce a command provider");
                    // Validate the copied identity and disabled/native-only state again on its STA.
                    const auto found = std::find_if(state->commands.begin(), state->commands.end(),
                        [&](const auto& item) { return item.key == state->key; });
                    if (found == state->commands.end() || !found->enabled || found->separator ||
                        found->native_only || !found->children.empty()) throw std::logic_error("Shell command is not invocable");
                    auto key = state->key;
                    const auto same_verb = [&](const ShellCommandInfo& item) {
                        return !item.separator && !item.native_only && item.children.empty() &&
                            CompareStringOrdinal(item.verb.c_str(), -1, found->verb.c_str(), -1, TRUE) == CSTR_EQUAL;
                    };
                    if (state->canonical_verbs && !found->verb.empty() &&
                        std::count_if(state->commands.begin(), state->commands.end(), same_verb) == 1) {
                        if (!state->current()) return;
                        auto fresh = create(state);
                        const auto commands = fresh->discover(state->stop.get_token());
                        if (state->stop.stop_requested()) return;
                        if (std::count_if(commands.begin(), commands.end(), same_verb) != 1)
                            throw std::logic_error("The Shell canonical action is no longer uniquely available");
                        const auto resolved = std::find_if(commands.begin(), commands.end(), same_verb);
                        if (!resolved->enabled) throw std::logic_error("The Shell canonical action is no longer enabled");
                        key = resolved->key;
                        provider = std::move(fresh);
                    }
                    if (state->current()) provider->invoke(key);
                } else {
                    if (!provider) provider = create(state);
                    if (state->current())
                        track_shell_commands(std::move(provider), state->point, state->apps,
                            [state] { return state->current(); });
                }
                return;
            }
            pump(state->wake);
        }
    }
    void work() noexcept {
        const HRESULT apartment = OleInitialize(nullptr);
        for (;;) {
            std::shared_ptr<AsyncShellMenu::State> state;
            {
                std::lock_guard lock(mutex_);
                if (stopping_) break;
                state = std::exchange(pending_, {});
                active_ = state;
            }
            if (!state) {
                const auto result = MsgWaitForMultipleObjectsEx(1, &wake_, 10000, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
                if (result == WAIT_TIMEOUT) {
                    std::lock_guard lock(mutex_);
                    if (!pending_) { stopping_ = true; break; }
                } else if (result == WAIT_OBJECT_0 + 1) {
                    MSG message{};
                    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
                        TranslateMessage(&message); DispatchMessageW(&message);
                    }
                }
                continue;
            }
            try { hr_require(apartment, "Initialize Shell worker STA"); run(state); }
            catch (...) { state->failure = std::current_exception(); }
            if (!state->ready.load()) {
                state->discovery_error = "Shell worker could not discover commands";
                state->ready.store(true, std::memory_order_release);
            }
            if (state->prefetch && !state->stop.stop_requested() && !state->discovery_error.empty()) {
                OutputDebugStringA("XUI Shell menu prefetch failed: ");
                OutputDebugStringA(state->discovery_error.c_str());
                OutputDebugStringA("\n");
            }
            state->finished.store(true, std::memory_order_release);
            { std::lock_guard lock(mutex_); active_.reset(); }
        }
        if (SUCCEEDED(apartment)) OleUninitialize();
    }
public:
    ShellWorker() {
        win32_require(wake_ != nullptr, "Create Shell worker event");
    }
    ~ShellWorker() { CloseHandle(wake_); }
    static void start(const std::shared_ptr<ShellWorker>& worker) {
        struct Launch { std::shared_ptr<ShellWorker> worker; HMODULE module; };
        HMODULE module{};
        win32_require(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCWSTR>(&ShellMenuTestAccess::create), &module) != FALSE, "Retain Shell worker module");
        std::unique_ptr<Launch> launch;
        try { launch = std::make_unique<Launch>(Launch{worker, module}); }
        catch (...) { FreeLibrary(module); throw; }
        const auto thread = CreateThread(nullptr, 0, [](void* value) -> DWORD {
            HMODULE module;
            {
                std::unique_ptr<Launch> launch(static_cast<Launch*>(value));
                module = launch->module;
                launch->worker->work();
            }
            // No join from DLL detach, and no return through an unloaded module.
            FreeLibraryAndExitThread(module, 0);
        }, launch.get(), 0, nullptr);
        if (!thread) { FreeLibrary(module); win32_require(false, "Start Shell worker"); }
        launch.release(); CloseHandle(thread);
    }
    bool submit(const std::shared_ptr<AsyncShellMenu::State>& state) {
        std::lock_guard lock(mutex_);
        if (stopping_) return false;
        if (state->prefetch &&
            ((active_ && !active_->prefetch && !active_->finished.load(std::memory_order_acquire)) ||
                (pending_ && !pending_->prefetch))) {
            state->cancel();
            state->ready.store(true, std::memory_order_release);
            state->finished.store(true, std::memory_order_release);
            OutputDebugStringW(L"XUI Shell menu prefetch skipped: an interactive request has priority.\n");
            return true;
        }
        if (!state->prefetch && active_ && active_->prefetch) active_->cancel();
        if (pending_) {
            pending_->discovery_error = "A newer Shell menu request replaced this request";
            pending_->cancel();
            pending_->ready.store(true, std::memory_order_release);
            pending_->finished.store(true, std::memory_order_release);
        }
        pending_ = state;
        SetEvent(wake_);
        return true;
    }
};
}
std::shared_ptr<AsyncShellMenu> AsyncShellMenu::start(HWND owner, std::vector<std::wstring> paths) {
    auto request = prepare(owner, std::move(paths));
    request->begin();
    return request;
}
std::shared_ptr<AsyncShellMenu> AsyncShellMenu::prefetch(HWND owner, std::wstring path) {
    auto request = prepare(owner, {std::move(path)});
    request->state_->prefetch = true;
    request->begin();
    return request;
}
std::shared_ptr<AsyncShellMenu> AsyncShellMenu::prepare(HWND owner, std::vector<std::wstring> paths, bool canonical_verbs) {
    win32_require(shell_validation_message() != 0, "Register Shell selection validation");
    auto state = std::make_shared<State>();
    state->parent = owner; state->paths = std::move(paths); state->canonical_verbs = canonical_verbs;
    return std::shared_ptr<AsyncShellMenu>(new AsyncShellMenu(std::move(state)));
}
void AsyncShellMenu::begin() {
    const auto state = state_;
    {
        std::lock_guard lock(state->mutex);
        if (state->started || state->stop.stop_requested()) return;
        state->started = true;
    }
    static std::mutex mutex;
    static std::weak_ptr<ShellWorker> idle;
    try {
        std::lock_guard lock(mutex);
        auto worker = idle.lock();
        if (!worker || !worker->submit(state)) {
            worker = std::make_shared<ShellWorker>();
            worker->submit(state);
            ShellWorker::start(worker);
            idle = worker;
        }
    }
    catch (...) {
        std::lock_guard lock(state->mutex); state->started = false;
        throw;
    }
}
AsyncShellMenu::~AsyncShellMenu() { cancel(); }
bool AsyncShellMenu::ready() const { return state_->ready.load(std::memory_order_acquire); }
bool AsyncShellMenu::finished() const { return state_->finished.load(std::memory_order_acquire); }
std::vector<ShellCommandInfo> AsyncShellMenu::commands() const {
    if (!ready()) throw std::logic_error("Shell commands are not ready");
    return state_->commands;
}
std::string AsyncShellMenu::discovery_error() const {
    if (!ready()) return {};
    return state_->discovery_error;
}
void AsyncShellMenu::invoke(ItemKey key) {
    std::lock_guard lock(state_->mutex);
    if (state_->stop.stop_requested() || finished() || state_->action != State::Action::none)
        throw std::logic_error("Shell menu request is no longer active");
    state_->key = key; state_->action = State::Action::invoke; SetEvent(state_->wake);
}
void AsyncShellMenu::windows_menu(Point point, const std::vector<MenuItem>& apps) {
    {
        std::lock_guard lock(state_->mutex);
        if (state_->stop.stop_requested() || finished() || state_->action != State::Action::none)
            throw std::logic_error("Shell menu request is no longer active");
        state_->point = point; state_->apps = apps;
        for (size_t i = 0; i < state_->apps.size(); ++i) {
            // Application callbacks stay on the UI thread, including native fallback choices.
            state_->apps[i].action = [state = state_.get(), i] { state->app = i; };
        }
        state_->action = State::Action::windows; SetEvent(state_->wake);
    }
    begin();
}
std::optional<size_t> AsyncShellMenu::result() const {
    if (!finished()) throw std::logic_error("Shell command has not finished");
    if (state_->failure) std::rethrow_exception(state_->failure);
    return state_->app;
}
void AsyncShellMenu::cancel() { state_->cancel(); }
}
