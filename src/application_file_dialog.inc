std::optional<std::wstring> Window::show_open_file_dialog(const FileDialogOptions& options) {
    return show_file_dialog(false, options);
}
std::optional<std::wstring> Window::show_save_file_dialog(const FileDialogOptions& options) {
    return show_file_dialog(true, options);
}
std::optional<std::wstring> Window::show_file_dialog(bool save, const FileDialogOptions& options) {
    const auto impl = impl_;
    if (GetCurrentThreadId() != impl->owner_thread)
        throw std::logic_error("Show file dialogs on the window UI thread");
    const auto snapshot = options;
    snapshot.validate();
    if (!impl->ready || impl->closing || !impl->window || !IsWindowVisible(impl->window) || !IsWindowEnabled(impl->window))
        throw std::logic_error("File dialog requires a running visible enabled window");
    if (impl->native_file_dialog || impl->replacing || impl->syncing || !impl->popups.empty())
        throw std::logic_error("Close the current popup or dialog before opening a file dialog");
    for (const auto& peer : impl->peers)
        if ((peer->edit && peer->edit->composing()) || (peer->document && peer->document->composing()))
            throw std::logic_error("Finish native text composition before opening a file dialog");
    Impl::InputScope input(*impl);
    const auto owner = impl->window;
    const auto focus = GetFocus();
    auto dialog = std::make_shared<NativeFileDialog>();
    impl->native_file_dialog = dialog;
    const auto finish = [&] {
        impl->native_file_dialog.reset();
        if (std::exchange(impl->file_dialog_teardown_pending, false)) impl->teardown();
        else if (impl->closing) impl->destroy();
        else if (GetActiveWindow() == owner && IsWindow(focus) && IsChild(owner, focus) &&
            IsWindowEnabled(focus) && IsWindowVisible(focus)) {
            SetFocus(focus);
            if (!impl->closing && impl->window == owner)
                win32_require(GetFocus() == focus, "Restore focus after native file dialog");
        }
    };
    std::optional<std::wstring> selected;
    try {
        dialog->initialize(save, snapshot);
        selected = dialog->show(owner);
    }
    catch (...) { finish(); throw; }
    finish();
    if (impl->failed) throw std::runtime_error("File dialog owner failed during modal dispatch");
    return selected;
}
