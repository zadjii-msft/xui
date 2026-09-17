#pragma once
#include "xui/file_dialog.hpp"
#include <windows.h>
#include <shobjidl.h>
#include <wrl/client.h>
#include <optional>

namespace xui {
class NativeFileDialog final {
public:
    NativeFileDialog() = default;
    NativeFileDialog(bool save, const FileDialogOptions& options);
    void initialize(bool save, const FileDialogOptions& options);
    std::optional<std::wstring> show(HWND owner);
    void cancel() noexcept;
private:
    Microsoft::WRL::ComPtr<IFileDialog> dialog_;
    bool showing_{}, cancelled_{};
    HRESULT cancel_error_{S_OK};
    UINT_PTR cancel_timer_{};
    inline static thread_local NativeFileDialog* active_{};
};
}
