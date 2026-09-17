#include "native_file_dialog.hpp"
#include "platform.hpp"
#include <shlobj.h>

namespace xui {
NativeFileDialog::NativeFileDialog(bool save, const FileDialogOptions& options) {
    initialize(save, options);
}
void NativeFileDialog::initialize(bool save, const FileDialogOptions& options) {
    if (dialog_) throw std::logic_error("Native file dialog is already initialized");
    options.validate();
    hr_require(CoCreateInstance(save ? CLSID_FileSaveDialog : CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&dialog_)), "Create native file dialog");
    FILEOPENDIALOGOPTIONS flags{};
    hr_require(dialog_->GetOptions(&flags), "Read native file dialog options");
    flags |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR | FOS_DONTADDTORECENT;
    flags |= save ? FOS_OVERWRITEPROMPT | FOS_NOTESTFILECREATE : FOS_FILEMUSTEXIST;
    hr_require(dialog_->SetOptions(flags), "Set native file dialog options");
    if (!options.title.empty()) hr_require(dialog_->SetTitle(options.title.c_str()), "Set native file dialog title");
    if (!options.default_extension.empty())
        hr_require(dialog_->SetDefaultExtension(options.default_extension.c_str()), "Set native file dialog extension");
    if (!options.suggested_name.empty())
        hr_require(dialog_->SetFileName(options.suggested_name.c_str()), "Set native file dialog filename");
    if (!options.filters.empty()) {
        std::vector<COMDLG_FILTERSPEC> filters;
        filters.reserve(options.filters.size());
        for (const auto& filter : options.filters) filters.push_back({filter.name.c_str(), filter.pattern.c_str()});
        hr_require(dialog_->SetFileTypes(static_cast<UINT>(filters.size()), filters.data()), "Set native file dialog filters");
        hr_require(dialog_->SetFileTypeIndex(1), "Set native file dialog filter index");
    }
    if (!options.initial_directory.empty()) {
        Microsoft::WRL::ComPtr<IShellItem> folder;
        hr_require(SHCreateItemFromParsingName(options.initial_directory.c_str(), nullptr, IID_PPV_ARGS(&folder)),
            "Resolve native file dialog directory");
        SFGAOF attributes{};
        hr_require(folder->GetAttributes(SFGAO_FOLDER | SFGAO_FILESYSTEM, &attributes), "Read native file dialog directory attributes");
        if ((attributes & (SFGAO_FOLDER | SFGAO_FILESYSTEM)) != (SFGAO_FOLDER | SFGAO_FILESYSTEM))
            throw std::invalid_argument("File dialog initial directory must name a filesystem folder");
        hr_require(dialog_->SetFolder(folder.Get()), "Set native file dialog directory");
    }
}
std::optional<std::wstring> NativeFileDialog::show(HWND owner) {
    if (!dialog_) throw std::logic_error("Native file dialog is not initialized");
    if (!owner || !IsWindow(owner) || !IsWindowVisible(owner) || !IsWindowEnabled(owner) ||
        GetWindowThreadProcessId(owner, nullptr) != GetCurrentThreadId())
        throw std::logic_error("File dialog requires a live visible enabled owner on its UI thread");
    if (showing_ || active_) throw std::logic_error("A native file dialog is already open on this thread");
    if (cancelled_) return {};
    active_ = this;
    struct Active {
        NativeFileDialog& dialog;
        ~Active() {
            if (dialog.cancel_timer_) KillTimer(nullptr, dialog.cancel_timer_);
            dialog.cancel_timer_ = 0;
            dialog.showing_ = false;
            active_ = nullptr;
        }
    } active{*this};
    // Show can pump messages before the Shell is ready to honor Close. Keep a
    // cancellation request until the modal loop returns, without destroying its owner.
    cancel_timer_ = SetTimer(nullptr, 0, 50, [](HWND, UINT, UINT_PTR timer, DWORD) noexcept {
        auto* dialog = active_;
        if (!dialog || dialog->cancel_timer_ != timer || !dialog->showing_ || !dialog->cancelled_) return;
        const auto status = dialog->dialog_->Close(HRESULT_FROM_WIN32(ERROR_CANCELLED));
        if (FAILED(status)) {
            dialog->cancel_error_ = status;
            OutputDebugStringW(L"XUI: Native file dialog cancellation failed.\n");
        }
    });
    win32_require(cancel_timer_ != 0, "Schedule native file dialog cancellation");
    showing_ = true;
    const auto status = dialog_->Show(owner);
    showing_ = false;
    hr_require(cancel_error_, "Cancel native file dialog");
    if (status == HRESULT_FROM_WIN32(ERROR_CANCELLED)) return {};
    hr_require(status, "Show native file dialog");
    if (cancelled_) return {};
    Microsoft::WRL::ComPtr<IShellItem> result;
    hr_require(dialog_->GetResult(&result), "Read native file dialog result");
    struct Path {
        PWSTR value{};
        ~Path() { CoTaskMemFree(value); }
    } path;
    hr_require(result->GetDisplayName(SIGDN_FILESYSPATH, &path.value), "Read native file dialog filesystem path");
    if (!path.value || !*path.value) throw std::runtime_error("Native file dialog returned an empty filesystem path");
    std::wstring value(path.value);
    if (value.size() > 32767) throw std::length_error("Native file dialog path exceeds 32767 UTF-16 units");
    win32_require(WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
        nullptr, 0, nullptr, nullptr) != 0, "Validate native file dialog path Unicode");
    if (cancelled_) return {};
    return value;
}
void NativeFileDialog::cancel() noexcept {
    cancelled_ = true;
}
}
