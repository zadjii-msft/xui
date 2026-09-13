#include "xui/shell_commands.hpp"
#include "platform.hpp"
#include <shellapi.h>
#include <shlobj.h>
#include <commctrl.h>
#include <wrl/client.h>
#include <atomic>

namespace xui {
namespace {
using Microsoft::WRL::ComPtr;
class NativeShellProvider final : public ShellCommandProvider {
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
    void thread() const {
        if (GetCurrentThreadId() != thread_) throw std::logic_error("Shell commands belong to their STA thread");
        if (!owner_ || !IsWindow(owner_)) throw std::logic_error("Shell command owner is closed");
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
    std::vector<ShellCommandInfo> read(HMENU menu, std::stop_token stop, unsigned depth, std::size_t& count) {
        if (depth > 8) throw std::length_error("Shell menu nesting exceeds eight");
        std::vector<ShellCommandInfo> result;
        const int total = GetMenuItemCount(menu);
        for (int i = 0; i < total; ++i) {
            if (stop.stop_requested()) return {};
            if (++count > 4096) throw std::length_error("Shell menu exceeds 4096 items");
            wchar_t label[1025]{};
            MENUITEMINFOW item{sizeof(item)}; item.fMask = MIIM_STRING | MIIM_ID | MIIM_STATE | MIIM_FTYPE | MIIM_SUBMENU | MIIM_BITMAP;
            item.dwTypeData = label; item.cch = 1024;
            win32_require(GetMenuItemInfoW(menu, i, TRUE, &item) != 0, "Read Shell command");
            ShellCommandInfo entry;
            entry.key = {item.hSubMenu || (item.fType & MFT_SEPARATOR) ? next_synthetic_++ : item.wID, epoch_};
            entry.label = label; entry.enabled = !(item.fState & (MFS_DISABLED | MFS_GRAYED));
            if (const auto tab = entry.label.find(L'\t'); tab != std::wstring::npos) {
                entry.shortcut_hints.push_back(entry.label.substr(tab + 1)); entry.label.resize(tab);
            }
            entry.has_native_icon = item.hbmpItem != nullptr;
            entry.checked = (item.fState & MFS_CHECKED) != 0; entry.separator = (item.fType & MFT_SEPARATOR) != 0;
            entry.native_only = item.hSubMenu || (item.fType & MFT_OWNERDRAW) || item.hbmpItem;
            if (!item.hSubMenu && !entry.separator && item.wID >= 1 && item.wID <= 0x7fff) {
                verbs_.insert(item.wID);
                wchar_t verb[1025]{};
                if (SUCCEEDED(context_->GetCommandString(item.wID - 1, GCS_VERBW, nullptr, reinterpret_cast<LPSTR>(verb), 1024)))
                    entry.verb = verb;
            }
            if (item.hSubMenu) entry.children = read(item.hSubMenu, stop, depth + 1, count);
            result.push_back(std::move(entry));
        }
        return result;
    }
public:
    NativeShellProvider(HWND owner, const std::vector<std::wstring>& paths) : owner_(owner) {
        if (!IsWindow(owner) || GetWindowThreadProcessId(owner, nullptr) != thread_) throw std::invalid_argument("Shell owner must belong to this thread");
        APTTYPE apartment{}; APTTYPEQUALIFIER qualifier{};
        hr_require(CoGetApartmentType(&apartment, &qualifier), "Read Shell COM apartment");
        if (apartment != APTTYPE_STA && apartment != APTTYPE_MAINSTA) throw std::logic_error("Shell commands require an STA");
        if (paths.empty() || paths.size() > 256) throw std::invalid_argument("Shell selection must contain 1 to 256 paths");
        struct Pidls {
            std::vector<PIDLIST_ABSOLUTE> values;
            ~Pidls() { for (auto value : values) CoTaskMemFree(value); }
        } pidls;
        for (const auto& path : paths) {
            if (path.empty() || path.size() > 32767) throw std::invalid_argument("Invalid Shell path");
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
        context_.As(&context2_); context_.As(&context3_);
        menu_ = CreatePopupMenu(); win32_require(menu_ != nullptr, "Create Shell menu");
        try {
            hr_require(context_->QueryContextMenu(menu_, 0, 1, 0x7fff, CMF_NORMAL), "Discover Shell commands");
            win32_require(SetWindowSubclass(owner_, forward, reinterpret_cast<UINT_PTR>(this), reinterpret_cast<DWORD_PTR>(this)) != 0,
                "Observe Shell command owner");
        }
        catch (...) { DestroyMenu(menu_); menu_ = nullptr; throw; }
        static std::atomic<std::uint64_t> next{1}; epoch_ = next.fetch_add(1);
    }
    ~NativeShellProvider() override {
        if (owner_) RemoveWindowSubclass(owner_, forward, reinterpret_cast<UINT_PTR>(this));
        if (menu_) DestroyMenu(menu_);
    }
    std::vector<ShellCommandInfo> discover(std::stop_token cancellation) override {
        thread(); verbs_.clear(); next_synthetic_ = 0x10000;
        std::size_t count{}; return read(menu_, cancellation, 0, count);
    }
    void invoke(ItemKey key) override {
        thread();
        if (key.version != epoch_ || key.id > 0x7fff || !verbs_.contains(static_cast<UINT>(key.id)))
            throw std::invalid_argument("Stale Shell command");
        CMINVOKECOMMANDINFOEX info{sizeof(info)};
        info.fMask = CMIC_MASK_UNICODE | CMIC_MASK_PTINVOKE; info.hwnd = owner_;
        info.lpVerb = MAKEINTRESOURCEA(key.id - 1); info.lpVerbW = MAKEINTRESOURCEW(key.id - 1);
        info.nShow = SW_SHOWNORMAL; GetCursorPos(&info.ptInvoke);
        hr_require(context_->InvokeCommand(reinterpret_cast<CMINVOKECOMMANDINFO*>(&info)), "Invoke Shell command");
    }
    std::optional<ItemKey> track(Point point) {
        thread();
        if (tracking_) throw std::logic_error("Shell menu is already open");
        tracking_ = true;
        struct Scope { NativeShellProvider* self; ~Scope() { self->tracking_ = false; } } scope{this};
        const auto chosen = static_cast<UINT>(TrackPopupMenuEx(menu_, TPM_RETURNCMD | TPM_RIGHTBUTTON, static_cast<int>(point.x), static_cast<int>(point.y), owner_, nullptr));
        if (!chosen || !IsWindow(owner_)) return {};
        verbs_.insert(chosen);
        const ItemKey key{chosen, epoch_}; invoke(key); return key;
    }
};
}
std::shared_ptr<ShellCommandProvider> shell_command_provider(void* owner, const std::vector<std::wstring>& paths) {
    return std::make_shared<NativeShellProvider>(static_cast<HWND>(owner), paths);
}
std::optional<ItemKey> track_shell_commands(void* owner, const std::vector<std::wstring>& paths, Point point) {
    auto provider = std::make_shared<NativeShellProvider>(static_cast<HWND>(owner), paths);
    return provider->track(point);
}
}
