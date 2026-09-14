#include "native_document.hpp"
#include "platform.hpp"
#include "drawing.hpp"
#include "window_host.hpp"
#include <commctrl.h>
#include <richedit.h>
#include <richole.h>
#include <stdexcept>

namespace xui {
namespace {
SYSTEMTIME native_date(DateTimeValue value) {
    SYSTEMTIME result{}; result.wYear = static_cast<WORD>(value.year); result.wMonth = static_cast<WORD>(value.month);
    result.wDay = static_cast<WORD>(value.day); result.wHour = static_cast<WORD>(value.hour);
    result.wMinute = static_cast<WORD>(value.minute); result.wSecond = static_cast<WORD>(value.second); return result;
}
DateTimeValue date_value(const SYSTEMTIME& value) {
    return {value.wYear, value.wMonth, value.wDay, value.wHour, value.wMinute, value.wSecond};
}
bool complete_utf16(std::wstring_view text) {
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto value = static_cast<unsigned>(text[i]);
        if (value >= 0xd800 && value <= 0xdbff) {
            if (++i == text.size() || text[i] < 0xdc00 || text[i] > 0xdfff) return false;
        } else if (value >= 0xdc00 && value <= 0xdfff) return false;
    }
    return true;
}
// RichEdit can otherwise deserialize OLE objects from the clipboard. This bridge
// accepts only plain Unicode paste and application-authored run formatting.
class NoObjects final : public IRichEditOleCallback {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        static const IID callback_id{0x00020d03, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
        if (iid != IID_IUnknown && iid != callback_id) return E_NOINTERFACE;
        *value = this; AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { auto count = --refs_; if (!count) delete this; return count; }
    HRESULT STDMETHODCALLTYPE GetNewStorage(LPSTORAGE*) override { return E_ACCESSDENIED; }
    HRESULT STDMETHODCALLTYPE GetInPlaceContext(LPOLEINPLACEFRAME*, LPOLEINPLACEUIWINDOW*, LPOLEINPLACEFRAMEINFO) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE ShowContainerUI(BOOL) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE QueryInsertObject(LPCLSID, LPSTORAGE, LONG) override { return E_ACCESSDENIED; }
    HRESULT STDMETHODCALLTYPE DeleteObject(LPOLEOBJECT) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE QueryAcceptData(LPDATAOBJECT, CLIPFORMAT* format, DWORD, BOOL, HGLOBAL) override {
        if (!format) return E_POINTER;
        if (*format && *format != CF_UNICODETEXT) return E_ACCESSDENIED;
        *format = CF_UNICODETEXT; return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ContextSensitiveHelp(BOOL) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetClipboardData(CHARRANGE*, DWORD, LPDATAOBJECT*) override { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE GetDragDropEffect(BOOL, DWORD, LPDWORD effect) override { if (effect) *effect = DROPEFFECT_NONE; return S_OK; }
    HRESULT STDMETHODCALLTYPE GetContextMenu(WORD, LPOLEOBJECT, CHARRANGE*, HMENU* menu) override { if (menu) *menu = nullptr; return S_OK; }
private:
    ULONG refs_{1};
};
}
NativeDocumentBridge::NativeDocumentBridge(std::shared_ptr<Control> model) : model_(std::move(model)) {}
NativeDocumentBridge::~NativeDocumentBridge() {
    if (auto document = std::dynamic_pointer_cast<DocumentText>(model_)) document->bind_commands({});
    if (window_ && IsWindow(window_)) DestroyWindow(window_);
    if (font_) DeleteObject(font_);
}
void NativeDocumentBridge::attach(HWND parent, int id) {
    DWORD style = WS_CHILD | WS_VISIBLE | WS_TABSTOP;
    const wchar_t* cls{};
    if (auto document = std::dynamic_pointer_cast<DocumentText>(model_)) {
        // The OS owns this module for the process lifetime. No per-document module reference.
        static HMODULE rich = LoadLibraryExW(L"Msftedit.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        win32_require(rich != nullptr, "Load Windows RichEdit");
        cls = MSFTEDIT_CLASS; style |= ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL;
    } else if (std::dynamic_pointer_cast<PasswordInput>(model_)) {
        cls = L"EDIT"; style |= ES_PASSWORD | ES_AUTOHSCROLL;
    } else if (auto date = std::dynamic_pointer_cast<DateTimePicker>(model_)) {
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_DATE_CLASSES};
        win32_require(InitCommonControlsEx(&controls) != FALSE, "Initialize date controls");
        cls = date->presentation() == DateTimePresentation::calendar ? MONTHCAL_CLASSW : DATETIMEPICK_CLASSW;
        if (date->presentation() != DateTimePresentation::calendar)
            style |= date->presentation() == DateTimePresentation::time ? DTS_TIMEFORMAT : DTS_SHORTDATEFORMAT;
    } else throw std::invalid_argument("Unsupported native document control");
    window_ = CreateWindowExW(0, cls, L"", style, 0, 0, 1, 1, parent,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    win32_require(window_ != nullptr, "Create native document control");
    win32_require(SetWindowSubclass(window_, subclass, 1, reinterpret_cast<DWORD_PTR>(this)) != FALSE, "Attach document adapter");
    if (auto document = std::dynamic_pointer_cast<DocumentText>(model_)) {
        SendMessageW(window_, EM_SETTEXTMODE, document->rich() ? TM_RICHTEXT : TM_PLAINTEXT, 0);
        SendMessageW(window_, EM_SETUNDOLIMIT, 16, 0);
        SendMessageW(window_, EM_SETEVENTMASK, 0, ENM_CHANGE | ENM_SELCHANGE | ENM_LINK);
        auto callback = new NoObjects;
        const auto restricted = SendMessageW(window_, EM_SETOLECALLBACK, 0, reinterpret_cast<LPARAM>(callback));
        callback->Release();
        win32_require(restricted != 0, "Restrict native rich document objects");
        SendMessageW(window_, EM_AUTOURLDETECT, FALSE, 0);
        document->bind_commands([this](TextCommand value) { return command(value); });
    }
}
std::wstring NativeDocumentBridge::text() const {
    if (std::dynamic_pointer_cast<DocumentText>(model_)) {
        GETTEXTLENGTHEX length_request{GTL_PRECISE | GTL_NUMCHARS, 1200};
        const auto length = SendMessageW(window_, EM_GETTEXTLENGTHEX, reinterpret_cast<WPARAM>(&length_request), 0);
        if (length < 0 || static_cast<std::size_t>(length) > DocumentText::document_limit) throw std::length_error("Native document exceeds its limit");
        std::wstring value(static_cast<std::size_t>(length) + 1, L'\0');
        GETTEXTEX request{static_cast<DWORD>(value.size() * sizeof(wchar_t)), GT_DEFAULT, 1200, nullptr, nullptr};
        value.resize(static_cast<std::size_t>(SendMessageW(window_, EM_GETTEXTEX, reinterpret_cast<WPARAM>(&request), reinterpret_cast<LPARAM>(value.data()))));
        return value;
    }
    const auto length = GetWindowTextLengthW(window_);
    if (length < 0 || static_cast<std::size_t>(length) > DocumentText::document_limit)
        throw std::length_error("Native document exceeds its limit");
    std::wstring value(static_cast<std::size_t>(length) + 1, L'\0');
    value.resize(GetWindowTextW(window_, value.data(), length + 1)); return value;
}
void NativeDocumentBridge::update(UINT dpi, const Palette& palette) {
    if (!window_) return;
    palette_ = palette;
    if (dpi_ != dpi) {
        if (font_ && std::dynamic_pointer_cast<DocumentText>(model_)) {
            CHARFORMAT2W format{sizeof(format)};
            format.dwMask = CFM_SIZE;
            format.yHeight = MulDiv(210, static_cast<int>(dpi), static_cast<int>(GetDpiForWindow(window_)));
            SendMessageW(window_, EM_SETCHARFORMAT, SCF_DEFAULT, reinterpret_cast<LPARAM>(&format));
            SendMessageW(window_, EM_SETCHARFORMAT, SCF_ALL, reinterpret_cast<LPARAM>(&format));
        } else {
        auto font = CreateFontW(-MulDiv(14, dpi, 96), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        win32_require(font != nullptr, "Create native document font");
        SendMessageW(window_, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        if (font_) DeleteObject(font_); font_ = font;
        }
        dpi_ = dpi;
    }
    const auto text_color = platform::native_color(palette.text), background = platform::native_color(palette.field);
    const bool recolor = !colors_set_ || text_color_ != text_color || background_ != background;
    text_color_ = text_color; background_ = background; colors_set_ = true;
    struct Setting { bool& value; Setting(bool& v) : value(v) { value = true; } ~Setting() { value = false; } } setting(setting_);
    if (auto document = std::dynamic_pointer_cast<DocumentText>(model_)) {
        if (accessible_name_ != document->name()) {
            accessible_name_ = document->name();
            SendMessageW(window_, EM_SETUIANAME, 0, reinterpret_cast<LPARAM>(accessible_name_.c_str()));
        }
        if (maximum_ != document->maximum_length()) {
            maximum_ = document->maximum_length(); SendMessageW(window_, EM_EXLIMITTEXT, 0, maximum_);
        }
        if (readonly_ != document->read_only()) {
            readonly_ = document->read_only(); SendMessageW(window_, EM_SETREADONLY, readonly_, 0);
        }
        if (monospace_ != document->monospace()) {
            monospace_ = document->monospace();
            CHARFORMAT2W format{sizeof(format)};
            format.dwMask = CFM_FACE;
            wcscpy_s(format.szFaceName, monospace_ ? L"Consolas" : L"Segoe UI");
            SendMessageW(window_, EM_SETCHARFORMAT, SCF_DEFAULT, reinterpret_cast<LPARAM>(&format));
            SendMessageW(window_, EM_SETCHARFORMAT, SCF_ALL, reinterpret_cast<LPARAM>(&format));
        }
        if (recolor) {
            SendMessageW(window_, EM_SETBKGNDCOLOR, 0, background_);
            CHARRANGE previous{}; SendMessageW(window_, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&previous));
            CHARFORMAT2W format{sizeof(format)}; format.dwMask = CFM_COLOR; format.crTextColor = text_color_;
            SendMessageW(window_, EM_SETCHARFORMAT, SCF_ALL, reinterpret_cast<LPARAM>(&format));
            SendMessageW(window_, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&previous));
        }
        if (!composing_ && revision_ != document->revision()) {
            revision_ = document->revision();
            struct Source { std::wstring_view text; std::size_t offset{}; } source{document->text()};
            EDITSTREAM stream{};
            stream.dwCookie = reinterpret_cast<DWORD_PTR>(&source);
            stream.pfnCallback = [](DWORD_PTR cookie, LPBYTE buffer, LONG size, LONG* copied) -> DWORD {
                auto& source = *reinterpret_cast<Source*>(cookie);
                const auto bytes = std::min<std::size_t>(static_cast<std::size_t>(size), source.text.size() * sizeof(wchar_t) - source.offset);
                memcpy(buffer, reinterpret_cast<const BYTE*>(source.text.data()) + source.offset, bytes);
                source.offset += bytes; *copied = static_cast<LONG>(bytes); return 0;
            };
            SendMessageW(window_, EM_STREAMIN, SF_TEXT | SF_UNICODE, reinterpret_cast<LPARAM>(&stream));
            win32_require(stream.dwError == 0, "Set bounded Unicode document");
            CHARFORMAT2W format{sizeof(format)};
            format.dwMask = CFM_BOLD | CFM_ITALIC | CFM_UNDERLINE | CFM_LINK | CFM_COLOR;
            format.crTextColor = text_color_;
            SendMessageW(window_, EM_SETCHARFORMAT, SCF_ALL, reinterpret_cast<LPARAM>(&format));
            LONG position{};
            for (const auto& run : document->runs()) {
                CHARRANGE range{position, position + static_cast<LONG>(run.text.size())}; position = range.cpMax;
                SendMessageW(window_, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&range));
                format.dwEffects = (run.bold ? CFE_BOLD : 0) | (run.italic ? CFE_ITALIC : 0) |
                    (run.underline || !run.link.empty() ? CFE_UNDERLINE : 0) | (!run.link.empty() ? CFE_LINK : 0);
                SendMessageW(window_, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&format));
            }
            SendMessageW(window_, EM_EMPTYUNDOBUFFER, 0, 0);
            selection_revision_ = ~document->selection_revision();
        }
        if (!composing_ && selection_revision_ != document->selection_revision()) {
            selection_revision_ = document->selection_revision();
            CHARRANGE range{static_cast<LONG>(document->selection().start), static_cast<LONG>(document->selection().end)};
            SendMessageW(window_, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&range));
        }
    } else if (auto password = std::dynamic_pointer_cast<PasswordInput>(model_)) {
        if (maximum_ != password->maximum_length()) {
            maximum_ = password->maximum_length(); SendMessageW(window_, EM_SETLIMITTEXT, maximum_, 0);
        }
        if (!composing_ && revision_ != password->revision()) {
            revision_ = password->revision();
            password->with_password([&](std::wstring_view value) { SetWindowTextW(window_, value.data()); });
        }
        // The editor always stays masked. Explicit reveal is a separate retained
        // preview, so native Value/Text providers never gain access to plaintext.
    } else if (auto date = std::dynamic_pointer_cast<DateTimePicker>(model_); date && revision_ != date->revision()) {
        revision_ = date->revision(); SYSTEMTIME range[]{native_date(date->minimum()), native_date(date->maximum())};
        auto value = native_date(date->value()); const bool calendar = date->presentation() == DateTimePresentation::calendar;
        SendMessageW(window_, calendar ? MCM_SETRANGE : DTM_SETRANGE, GDTR_MIN | GDTR_MAX, reinterpret_cast<LPARAM>(range));
        SendMessageW(window_, calendar ? MCM_SETCURSEL : DTM_SETSYSTEMTIME, calendar ? 0 : GDT_VALID, reinterpret_cast<LPARAM>(&value));
    }
}
void NativeDocumentBridge::changed() {
    if (setting_ || composing_) return;
    if (auto document = std::dynamic_pointer_cast<DocumentText>(model_)) {
        auto value = text();
        // WM_CHAR can deliver a surrogate pair in two notifications. Publish only
        // complete text, without treating the first half as an application error.
        if (complete_utf16(value)) document->commit_text(std::move(value));
    }
    else if (auto password = std::dynamic_pointer_cast<PasswordInput>(model_)) {
        auto value = text();
        if (!complete_utf16(value)) {
            volatile wchar_t* data = value.data();
            for (std::size_t i = 0; i < value.size(); ++i) data[i] = 0;
            return;
        }
        password->commit_password(std::move(value)); revision_ = password->revision();
    }
}
LRESULT NativeDocumentBridge::notify(const NMHDR& notification) {
    if (setting_) return 0;
    if (auto document = std::dynamic_pointer_cast<DocumentText>(model_)) {
        if (notification.code == EN_SELCHANGE) {
            const auto& selected = reinterpret_cast<const SELCHANGE&>(notification);
            document->commit_selection({static_cast<std::size_t>(std::max(0L, selected.chrg.cpMin)),
                static_cast<std::size_t>(std::max(0L, selected.chrg.cpMax))});
        } else if (notification.code == EN_LINK) {
            const auto& link = reinterpret_cast<const ENLINK&>(notification);
            if (link.msg == WM_LBUTTONUP) document->activate_link(static_cast<std::size_t>(link.chrg.cpMin));
        }
    } else if (auto date = std::dynamic_pointer_cast<DateTimePicker>(model_)) {
        SYSTEMTIME value{}; bool changed{};
        if (notification.code == DTN_DATETIMECHANGE) {
            const auto& change = reinterpret_cast<const NMDATETIMECHANGE&>(notification);
            value = change.st; changed = change.dwFlags == GDT_VALID;
        } else if (notification.code == MCN_SELCHANGE) {
            value = reinterpret_cast<const NMSELCHANGE&>(notification).stSelStart; changed = true;
        }
        if (changed) {
            auto next = date_value(value); const auto before = date->value();
            if (date->presentation() == DateTimePresentation::time) {
                next.year = before.year; next.month = before.month; next.day = before.day;
            } else { next.hour = before.hour; next.minute = before.minute; next.second = before.second; }
            if (next < date->minimum() || next > date->maximum()) {
                revision_ = 0; model_->invalidate(Invalidation::paint);
            } else {
                date->change_value(next); revision_ = date->revision();
            }
        }
    }
    return 0;
}
bool NativeDocumentBridge::command(TextCommand value) {
    auto document = std::dynamic_pointer_cast<DocumentText>(model_);
    if (!document || !window_ || !IsWindowVisible(window_) || !IsWindowEnabled(window_) || composing_) return false;
    update(dpi_, palette_);
    if (document->read_only() && (value == TextCommand::undo || value == TextCommand::redo || value == TextCommand::cut || value == TextCommand::paste))
        return false;
    switch (value) {
    case TextCommand::undo: return SendMessageW(window_, EM_UNDO, 0, 0) != 0;
    case TextCommand::redo: return SendMessageW(window_, EM_REDO, 0, 0) != 0;
    case TextCommand::copy: SendMessageW(window_, WM_COPY, 0, 0); return true;
    case TextCommand::cut: SendMessageW(window_, WM_CUT, 0, 0); return true;
    case TextCommand::paste: SendMessageW(window_, WM_PASTE, 0, 0); return true;
    case TextCommand::select_all: SendMessageW(window_, EM_SETSEL, 0, -1); return true;
    }
    return false;
}
LRESULT CALLBACK NativeDocumentBridge::subclass(HWND hwnd, UINT message, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR data) noexcept {
    auto& self = *reinterpret_cast<NativeDocumentBridge*>(data);
    try {
        if (message == WM_IME_STARTCOMPOSITION) self.composing_ = true;
        if (message == WM_IME_ENDCOMPOSITION) {
            const auto result = DefSubclassProc(hwnd, message, wp, lp); self.composing_ = false; self.changed(); return result;
        }
        if (message == WM_DROPFILES) return 0;
        if (message == WM_PRINTCLIENT && std::dynamic_pointer_cast<DateTimePicker>(self.model_)) {
            // Common controls do not paint unused calendar margins for WM_PRINTCLIENT.
            // Initialize their native background before the shared bitmap is composed.
            RECT bounds{}; GetClientRect(hwnd, &bounds);
            FillRect(reinterpret_cast<HDC>(wp), &bounds, GetSysColorBrush(COLOR_WINDOW));
        }
        if (auto password = std::dynamic_pointer_cast<PasswordInput>(self.model_)) {
            if (message == WM_COPY || message == WM_CUT || message == WM_CONTEXTMENU) return 0;
            if (message == WM_KILLFOCUS || message == WM_CANCELMODE || (message == WM_SHOWWINDOW && !wp)) password->set_revealed(false);
        } else if (auto document = std::dynamic_pointer_cast<DocumentText>(self.model_)) {
            if (message == WM_PASTE) {
                if (document->read_only()) return 0;
                return SendMessageW(hwnd, EM_PASTESPECIAL, CF_UNICODETEXT, 0);
            }
            if (message == WM_KEYDOWN && wp == VK_RETURN && GetKeyState(VK_CONTROL) < 0) {
                document->activate_link(document->selection().start); return 0;
            }
        }
        if (message == WM_NCDESTROY) {
            RemoveWindowSubclass(hwnd, subclass, id); self.window_ = nullptr;
            if (auto document = std::dynamic_pointer_cast<DocumentText>(self.model_)) document->bind_commands({});
        }
        return DefSubclassProc(hwnd, message, wp, lp);
    } catch (...) { if (self.failure_) { auto callback = self.failure_; callback(); } return 0; }
}
}
