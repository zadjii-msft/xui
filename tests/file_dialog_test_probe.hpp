#pragma once
#include <windows.h>
#include <functional>
#include <stdexcept>
#include <utility>

namespace file_dialog_tests {
struct ModalProbe {
    HWND owner{};
    UINT_PTR timer{};
    ULONGLONG deadline = GetTickCount64() + 30000;
    bool observed{};
    std::exception_ptr error;
    std::function<void(HWND)> action;
    static inline ModalProbe* active{};
    static void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
    static void CALLBACK tick(HWND, UINT, UINT_PTR id, DWORD) noexcept {
        if (!active || active->timer != id) return;
        auto& self = *active;
        HWND dialog{};
        std::pair search{self.owner, &dialog};
        EnumThreadWindows(GetCurrentThreadId(), [](HWND window, LPARAM data) -> BOOL {
            auto& pair = *reinterpret_cast<std::pair<HWND, HWND*>*>(data);
            wchar_t name[32]{};
            GetClassNameW(window, name, 32);
            if (GetWindow(window, GW_OWNER) == pair.first && wcscmp(name, L"#32770") == 0 && IsWindowVisible(window)) {
                *pair.second = window;
                return FALSE;
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&search));
        try {
            if (dialog && !self.observed) {
                require(!IsWindowEnabled(self.owner), "Native modal dialog disables its actual owner");
                self.observed = true;
                self.action(dialog);
            }
            if (GetTickCount64() >= self.deadline) throw std::runtime_error("Owned native dialog timed out");
        } catch (...) {
            self.error = std::current_exception();
            if (dialog) PostMessageW(dialog, WM_COMMAND, IDCANCEL, 0);
        }
    }
    ModalProbe(HWND owner, std::function<void(HWND)> action) : owner(owner), action(std::move(action)) {
        require(!active, "Only one modal probe");
        timer = SetTimer(nullptr, 0, 25, tick);
        require(timer != 0, "Create bounded native dialog probe");
        active = this;
    }
    ~ModalProbe() { KillTimer(nullptr, timer); active = nullptr; }
    void check(bool owner_alive = true) {
        if (error) std::rethrow_exception(error);
        require(observed, "Native dialog appeared");
        require(owner_alive ? IsWindowEnabled(owner) != FALSE : IsWindow(owner) == FALSE,
            "Native dialog restored or closed its owner");
    }
};
}
