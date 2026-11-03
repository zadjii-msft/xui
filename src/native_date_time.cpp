#include "native_date_time.hpp"
#include "native_field_support.hpp"

namespace xui {
namespace {
SYSTEMTIME native_date(DateTimeValue value) {
    SYSTEMTIME result{};
    result.wYear = static_cast<WORD>(value.year); result.wMonth = static_cast<WORD>(value.month);
    result.wDay = static_cast<WORD>(value.day); result.wHour = static_cast<WORD>(value.hour);
    result.wMinute = static_cast<WORD>(value.minute); result.wSecond = static_cast<WORD>(value.second);
    return result;
}
DateTimeValue date_value(const SYSTEMTIME& value) {
    return {value.wYear, value.wMonth, value.wDay, value.wHour, value.wMinute, value.wSecond};
}
}

NativeDateTimeBridge::NativeDateTimeBridge(std::shared_ptr<DateTimePicker> model) : model_(std::move(model)) {
    if (!model_) throw std::invalid_argument("A native date/time field requires a DateTimePicker");
}
NativeDateTimeBridge::~NativeDateTimeBridge() {
    lifetime_.reset();
    if (window_ && IsWindow(window_)) DestroyWindow(window_);
    if (font_) DeleteObject(font_);
}
void NativeDateTimeBridge::attach(HWND parent, int id) {
    if (window_) throw std::logic_error("Native date/time field already attached");
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_DATE_CLASSES};
    win32_require(InitCommonControlsEx(&controls) != FALSE, "Initialize date controls");
    DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP;
    const auto presentation = model_->presentation();
    const auto* cls = presentation == DateTimePresentation::calendar ? MONTHCAL_CLASSW : DATETIMEPICK_CLASSW;
    if (presentation != DateTimePresentation::calendar)
        style |= presentation == DateTimePresentation::time ? DTS_TIMEFORMAT : DTS_SHORTDATEFORMAT;
    window_ = CreateWindowExW(0, cls, L"", style, 0, 0, 1, 1, parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    win32_require(window_ != nullptr, "Create native date/time control");
    win32_require(SetWindowSubclass(window_, subclass, 1, reinterpret_cast<DWORD_PTR>(this)) != FALSE,
        "Attach date/time adapter");
}
void NativeDateTimeBridge::update(UINT dpi, const Palette&) {
    if (!window_) return;
    if (!composing_) update_native_field_font(window_, font_, dpi_, dpi);
    struct Setting {
        bool& value;
        explicit Setting(bool& value) : value(value) { value = true; }
        ~Setting() { value = false; }
    } setting(setting_);
    styled_font_.update(window_, model_->effective_control_style_values(StylePart::text),
        dpi, font_, L"Segoe UI", 14.0f);
    if (revision_ == model_->revision()) return;
    revision_ = model_->revision();
    SYSTEMTIME range[]{native_date(model_->minimum()), native_date(model_->maximum())};
    auto value = native_date(model_->value());
    const bool calendar = model_->presentation() == DateTimePresentation::calendar;
    SendMessageW(window_, calendar ? MCM_SETRANGE : DTM_SETRANGE, GDTR_MIN | GDTR_MAX, reinterpret_cast<LPARAM>(range));
    SendMessageW(window_, calendar ? MCM_SETCURSEL : DTM_SETSYSTEMTIME,
        calendar ? 0 : GDT_VALID, reinterpret_cast<LPARAM>(&value));
}
LRESULT NativeDateTimeBridge::notify(const NMHDR& notification) {
    if (setting_) return 0;
    SYSTEMTIME value{};
    bool changed{};
    if (notification.code == DTN_DATETIMECHANGE) {
        const auto& change = reinterpret_cast<const NMDATETIMECHANGE&>(notification);
        value = change.st; changed = change.dwFlags == GDT_VALID;
    } else if (notification.code == MCN_SELCHANGE) {
        value = reinterpret_cast<const NMSELCHANGE&>(notification).stSelStart;
        changed = true;
    }
    if (!changed) return 0;
    const auto model = model_;
    auto next = date_value(value);
    const auto before = model->value();
    if (model->presentation() == DateTimePresentation::time) {
        next.year = before.year; next.month = before.month; next.day = before.day;
    } else {
        next.hour = before.hour; next.minute = before.minute; next.second = before.second;
    }
    if (next < model->minimum() || next > model->maximum()) {
        revision_ = 0;
        model->invalidate(Invalidation::paint);
    } else {
        const std::weak_ptr<bool> lifetime = lifetime_;
        model->change_value(next);
        if (!lifetime.expired()) revision_ = model->revision();
    }
    return 0;
}
LRESULT CALLBACK NativeDateTimeBridge::subclass(HWND hwnd, UINT message, WPARAM wp, LPARAM lp,
    UINT_PTR id, DWORD_PTR data) noexcept {
    auto& self = *reinterpret_cast<NativeDateTimeBridge*>(data);
    try {
        if (message == WM_IME_STARTCOMPOSITION) self.composing_ = true;
        if (message == WM_IME_ENDCOMPOSITION) {
            const auto model = self.model_;
            const std::weak_ptr<bool> lifetime = self.lifetime_;
            const auto result = DefSubclassProc(hwnd, message, wp, lp);
            if (lifetime.expired()) return result;
            self.composing_ = false;
            model->invalidate(Invalidation::layout);
            return result;
        }
        if (message == WM_DROPFILES) return 0;
        if (message == WM_PRINTCLIENT) {
            RECT bounds{};
            GetClientRect(hwnd, &bounds);
            FillRect(reinterpret_cast<HDC>(wp), &bounds, GetSysColorBrush(COLOR_WINDOW));
        }
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
