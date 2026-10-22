#include "native_password.hpp"
#include "native_field_support.hpp"
#include "window_host.hpp"

namespace xui {

NativePasswordBridge::NativePasswordBridge(std::shared_ptr<PasswordInput> model) : model_(std::move(model)) {
    if (!model_) throw std::invalid_argument("A native password requires a PasswordInput");
}
NativePasswordBridge::~NativePasswordBridge() {
    lifetime_.reset();
    if (window_ && IsWindow(window_)) DestroyWindow(window_);
    if (font_) DeleteObject(font_);
}
void NativePasswordBridge::attach(HWND parent, int id) {
    if (window_) throw std::logic_error("Native password already attached");
    window_ = CreateWindowExW(0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_PASSWORD | ES_AUTOHSCROLL,
        0, 0, 1, 1, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    win32_require(window_ != nullptr, "Create native password control");
    win32_require(SetWindowSubclass(window_, subclass, 1, reinterpret_cast<DWORD_PTR>(this)) != FALSE,
        "Attach password adapter");
}
std::wstring NativePasswordBridge::text() const {
    const auto length = GetWindowTextLengthW(window_);
    if (length < 0 || static_cast<std::size_t>(length) > DocumentText::document_limit)
        throw std::length_error("Native password exceeds its limit");
    std::wstring value(static_cast<std::size_t>(length) + 1, L'\0');
    value.resize(GetWindowTextW(window_, value.data(), length + 1));
    return value;
}
void NativePasswordBridge::update(UINT dpi, const Palette& palette) {
    if (!window_) return;
    const auto* text_style = model_->effective_control_style_values(StylePart::text);
    const auto* root_style = model_->effective_control_style_values(StylePart::root);
    if (!composing_) update_native_field_font(window_, font_, dpi_, dpi);
    const auto color = [&](const std::optional<ThemeColor>& value, D2D1_COLOR_F fallback) {
        return value && !palette.high_contrast ? D2D1::ColorF(value->resolve(palette.mode)) : fallback;
    };
    const auto ink = IsWindowEnabled(window_) ? palette.text : palette.disabled;
    const auto text_color = platform::native_color(text_style && IsWindowEnabled(window_) ?
        color(text_style->foreground, ink) : ink);
    const auto background = platform::native_color(root_style ? color(root_style->background, palette.field) : palette.field);
    const bool recolor = !colors_set_ || text_color_ != text_color || background_ != background;
    text_color_ = text_color; background_ = background; colors_set_ = true;
    struct Setting {
        bool& value;
        explicit Setting(bool& value) : value(value) { value = true; }
        ~Setting() { value = false; }
    } setting(setting_);
    if (recolor) win32_require(InvalidateRect(window_, nullptr, FALSE) != FALSE, "Refresh native password colors");
    if (!composing_) styled_font_.update(window_, text_style, dpi, font_, L"Segoe UI", 14.0f);
    if (maximum_ != model_->maximum_length()) {
        maximum_ = model_->maximum_length();
        SendMessageW(window_, EM_SETLIMITTEXT, maximum_, 0);
    }
    if (!composing_ && revision_ != model_->revision()) {
        revision_ = model_->revision();
        model_->with_password([&](std::wstring_view value) { SetWindowTextW(window_, value.data()); });
    }
}
void NativePasswordBridge::changed() {
    if (setting_ || composing_) return;
    auto value = text();
    if (!complete_native_utf16(value)) {
        volatile wchar_t* data = value.data();
        for (std::size_t i = 0; i < value.size(); ++i) data[i] = 0;
        return;
    }
    const auto model = model_;
    const std::weak_ptr<bool> lifetime = lifetime_;
    model->commit_password(std::move(value));
    if (!lifetime.expired()) revision_ = model->revision();
}
LRESULT CALLBACK NativePasswordBridge::subclass(HWND hwnd, UINT message, WPARAM wp, LPARAM lp,
    UINT_PTR id, DWORD_PTR data) noexcept {
    auto& self = *reinterpret_cast<NativePasswordBridge*>(data);
    try {
        if (message == WM_IME_STARTCOMPOSITION) self.composing_ = true;
        if (message == WM_IME_ENDCOMPOSITION) {
            const auto model = self.model_;
            const std::weak_ptr<bool> lifetime = self.lifetime_;
            const auto failure = self.failure_;
            try {
                const auto result = DefSubclassProc(hwnd, message, wp, lp);
                if (lifetime.expired()) return result;
                self.composing_ = false;
                self.changed();
                model->invalidate(Invalidation::layout);
                return result;
            } catch (...) {
                if (failure) failure();
                return 0;
            }
        }
        if (message == WM_DROPFILES || message == WM_COPY || message == WM_CUT || message == WM_CONTEXTMENU)
            return 0;
        if (message == WM_KILLFOCUS || message == WM_CANCELMODE || (message == WM_SHOWWINDOW && !wp))
            self.model_->set_revealed(false);
        if (message == WM_NCDESTROY) {
            RemoveWindowSubclass(hwnd, subclass, id);
            self.window_ = nullptr;
        }
        return DefSubclassProc(hwnd, message, wp, lp);
    } catch (...) {
        if (self.failure_) { auto callback = self.failure_; callback(); }
        return 0;
    }
}

}
