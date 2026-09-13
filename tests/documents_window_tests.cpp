#include "xui/application.hpp"
#include "xui/documents.hpp"
#include "../src/drawing.hpp"
#include "suggestion_capture.hpp"
#include "owned_window_capture.hpp"
#include <UIAutomation.h>
#include <richedit.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <psapi.h>
#include <wrl/client.h>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

namespace xui {
struct DrawingTestAccess {
    static void observe(void (*callback)(HWND)) { Drawing::present_observer_ = callback; }
    static void lose() { Drawing::end_result_override_ = D2DERR_RECREATE_TARGET; }
};
}
namespace {
using namespace xui;
using Microsoft::WRL::ComPtr;
void require(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
void success(HRESULT value, const char* text) { if (FAILED(value)) throw std::runtime_error(std::string(text) + ": " + std::to_string(value)); }
void flush(HWND hwnd) { SendMessageW(hwnd, WM_APP + 12, 0, 0); InvalidateRect(hwnd, nullptr, FALSE); UpdateWindow(hwnd); }
template<class F> void eventually(F callback, const char* message) {
    for (int i = 0; i < 400; ++i) { if (callback()) return; Sleep(20); } throw std::runtime_error(message);
}
HWND native(HWND root, const wchar_t* cls, int ordinal = 0) {
    struct Find { const wchar_t* cls; int index; HWND result{}; } find{cls, ordinal};
    EnumChildWindows(root, [](HWND hwnd, LPARAM data) -> BOOL {
        auto& find = *reinterpret_cast<Find*>(data); wchar_t cls[128]{}; GetClassNameW(hwnd, cls, 128);
        if (_wcsicmp(cls, find.cls) == 0 && find.index-- == 0) { find.result = hwnd; return FALSE; } return TRUE;
    }, reinterpret_cast<LPARAM>(&find));
    require(find.result != nullptr, "Native bridge exists"); return find.result;
}
std::wstring read(HWND hwnd) {
    std::wstring value(GetWindowTextLengthW(hwnd) + 1, L'\0'); value.resize(GetWindowTextW(hwnd, value.data(), static_cast<int>(value.size()))); return value;
}
ComPtr<IUIAutomationElement> find(IUIAutomation* automation, IUIAutomationElement* root, const wchar_t* id) {
    VARIANT value{}; value.vt = VT_BSTR; value.bstrVal = SysAllocString(id);
    ComPtr<IUIAutomationCondition> condition; auto hr = automation->CreatePropertyCondition(UIA_AutomationIdPropertyId, value, &condition);
    VariantClear(&value); success(hr, "Create UIA ID condition");
    ComPtr<IUIAutomationElement> result; success(root->FindFirst(TreeScope_Subtree, condition.Get(), &result), "Find UIA document");
    if (!result && (std::wstring_view(id) == L"document" || std::wstring_view(id) == L"rich-document")) {
        value.vt = VT_BSTR;
        value.bstrVal = SysAllocString(std::wstring_view(id) == L"document" ? L"Plain document" : L"Styled fixture");
        success(automation->CreatePropertyCondition(UIA_NamePropertyId, value, &condition), "Native RichEdit name");
        VariantClear(&value);
        success(root->FindFirst(TreeScope_Subtree, condition.Get(), &result), "Find native RichEdit provider");
    }
    require(result != nullptr, "Document has a UIA element"); return result;
}
template<class T> ComPtr<T> pattern(IUIAutomationElement* element, PATTERNID id) {
    ComPtr<T> result; success(element->GetCurrentPatternAs(id, IID_PPV_ARGS(&result)), "Native UIA pattern");
    require(result != nullptr, "Native UIA pattern must not be null"); return result;
}
class LiveEvents final : public IUIAutomationEventHandler {
public:
    std::atomic<unsigned> count{};
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        if (iid != IID_IUnknown && iid != __uuidof(IUIAutomationEventHandler)) return E_NOINTERFACE;
        *value = this; AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { auto remaining = --refs_; if (!remaining) delete this; return remaining; }
    HRESULT STDMETHODCALLTYPE HandleAutomationEvent(IUIAutomationElement*, EVENTID event) override {
        if (event == UIA_LiveRegionChangedEventId) ++count;
        return S_OK;
    }
private:
    std::atomic<ULONG> refs_{1};
};
void password_contract(IUIAutomationElement* password) {
    BOOL secure{}; success(password->get_CurrentIsPassword(&secure), "Password property"); require(secure, "Password always exposes IsPassword");
    ComPtr<IUIAutomationValuePattern> value;
    if (SUCCEEDED(password->GetCurrentPatternAs(UIA_ValuePatternId, IID_PPV_ARGS(&value))) && value) {
        BSTR text{}; const auto result = value->get_CurrentValue(&text);
        const bool leaked = text && std::wstring_view(text).find(L"fixture") != std::wstring_view::npos;
        std::cout << "Password Value read hr=" << result << " length=" << (text ? SysStringLen(text) : 0) << " contains-fixture=" << leaked << '\n';
        const bool empty = !text || SysStringLen(text) == 0;
        SysFreeString(text); require(!leaked && (FAILED(result) || empty), "Native password Value read is unavailable or empty");
    }
    ComPtr<IUIAutomationTextPattern> document;
    if (SUCCEEDED(password->GetCurrentPatternAs(UIA_TextPatternId, IID_PPV_ARGS(&document))) && document) {
        ComPtr<IUIAutomationTextRange> range;
        if (SUCCEEDED(document->get_DocumentRange(&range)) && range) {
            BSTR text{}; const auto result = range->GetText(-1, &text);
            const bool leaked = text && std::wstring_view(text).find(L"fixture") != std::wstring_view::npos;
            SysFreeString(text); require(!leaked, "Password Text pattern never discloses plaintext"); (void)result;
        }
    }
}
int uia(HWND hwnd) {
    success(CoInitializeEx(nullptr, COINIT_MULTITHREADED), "Client COM");
    struct Com { ~Com() { CoUninitialize(); } } com;
    ComPtr<IUIAutomation> automation; success(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&automation)), "UIA client");
    ComPtr<IUIAutomationElement> root; success(automation->ElementFromHandle(hwnd, &root), "Owned root");
    std::cout << "UIA owned root\n" << std::flush;
    auto document = find(automation.Get(), root.Get(), L"document");
    auto text = pattern<IUIAutomationTextPattern>(document.Get(), UIA_TextPatternId);
    std::cout << "UIA native Text pattern\n" << std::flush;
    ComPtr<IUIAutomationTextRange> range; success(text->get_DocumentRange(&range), "Native document range");
    BSTR contents{}; success(range->GetText(1024, &contents), "Read bounded native document");
    require(contents && std::wstring_view(contents).find(L"fixture") != std::wstring_view::npos, "Native document text is real"); SysFreeString(contents);
    success(document->SetFocus(), "Marshal native document focus");
    auto rich = find(automation.Get(), root.Get(), L"rich-document");
    pattern<IUIAutomationTextPattern>(rich.Get(), UIA_TextPatternId);
    auto password = find(automation.Get(), root.Get(), L"password"); password_contract(password.Get());
    std::cout << "UIA masked password\n" << std::flush;
    PostMessageW(hwnd, WM_KEYDOWN, VK_F8, 0); Sleep(100); password_contract(password.Get());
    std::cout << "UIA revealed password\n" << std::flush;
    auto status = find(automation.Get(), root.Get(), L"status");
    CONTROLTYPEID type{}; success(status->get_CurrentControlType(&type), "Status role"); require(type == UIA_StatusBarControlTypeId, "Status is not a label alias");
    VARIANT setting{}; success(status->GetCurrentPropertyValue(UIA_LiveSettingPropertyId, &setting), "Live announcement semantics");
    require(setting.vt == VT_I4 && setting.lVal == Assertive, "Error status is assertive"); VariantClear(&setting);
    ComPtr<LiveEvents> announcements; announcements.Attach(new LiveEvents);
    success(automation->AddAutomationEventHandler(UIA_LiveRegionChangedEventId, status.Get(), TreeScope_Element, nullptr, announcements.Get()), "Subscribe to actual status announcements");
    PostMessageW(hwnd, WM_KEYDOWN, VK_F5, 0);
    eventually([&] { return announcements->count == 1; }, "One semantic status change raises one live event");
    PostMessageW(hwnd, WM_KEYDOWN, VK_F4, 0);
    eventually([&] { return announcements->count == 2; }, "Showing a dismissed message raises a new live event");
    std::cout << "UIA live events: one message change, one re-show; duplicate setter silent\n";
    success(automation->RemoveAutomationEventHandler(UIA_LiveRegionChangedEventId, status.Get(), announcements.Get()), "Release the owned announcement subscription");
    auto color = find(automation.Get(), root.Get(), L"color"); success(color->get_CurrentControlType(&type), "Color role");
    require(type == UIA_GroupControlTypeId, "Color has a group and real channel children");
    auto date = find(automation.Get(), root.Get(), L"date");
    success(date->get_CurrentControlType(&type), "Native date role"); require(type != UIA_CustomControlTypeId, "Date uses an OS provider");
    PostMessageW(hwnd, WM_KEYDOWN, VK_F6, 0);
    ComPtr<IUIAutomationElement> dialog;
    eventually([&] {
        try { dialog = find(automation.Get(), root.Get(), L"modal-dialog"); return dialog != nullptr; } catch (...) { return false; }
    }, "Native modal dialog appears to external UIA");
    auto modal = pattern<IUIAutomationWindowPattern>(dialog.Get(), UIA_WindowPatternId);
    BOOL is_modal{}; success(modal->get_CurrentIsModal(&is_modal), "Dialog Window pattern"); require(is_modal, "Window pattern reports modal isolation");
    BOOL owner_enabled{}; success(document->get_CurrentIsEnabled(&owner_enabled), "Native owner enabled state"); require(!owner_enabled, "Native RichEdit provider respects modal owner disable");
    document->SetFocus();
    BOOL owner_focus{}; success(document->get_CurrentHasKeyboardFocus(&owner_focus), "Read actual native owner focus");
    require(!owner_focus, "Native RichEdit UIA cannot focus the disabled owner");
    success(modal->Close(), "Bounded UI-thread modal close");
    eventually([&] { BOOL active{}; return SUCCEEDED(document->get_CurrentIsEnabled(&active)) && active; }, "Owner UIA state recovers");
    PostMessageW(hwnd, WM_KEYDOWN, VK_F7, 0);
    eventually([&] { BOOL off{}; return FAILED(document->get_CurrentIsOffscreen(&off)) || off; }, "Providers become unavailable after close");
    return 0;
}
struct Frame {
    int width{}, height{};
    std::vector<DWORD> pixels;
    explicit Frame(HWND hwnd) {
        if (GetForegroundWindow() != hwnd) {
            auto image = owned_window_capture::capture(hwnd);
            width = image.width; height = image.height; pixels = std::move(image.data);
            return;
        }
        require(GetForegroundWindow() == hwnd, "Owned root must be foreground before screen capture");
        RECT rect{}; GetClientRect(hwnd, &rect); POINT origin{}; ClientToScreen(hwnd, &origin);
        width = rect.right; height = rect.bottom; pixels.resize(static_cast<std::size_t>(width) * height);
        HDC screen = GetDC(nullptr), dc = CreateCompatibleDC(screen);
        BITMAPINFO info{}; info.bmiHeader = {sizeof(BITMAPINFOHEADER), width, -height, 1, 32, BI_RGB};
        void* data{}; const auto bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &data, nullptr, 0);
        require(bitmap && dc, "Create owned client frame"); const auto old = SelectObject(dc, bitmap);
        const auto copied = BitBlt(dc, 0, 0, width, height, screen, origin.x, origin.y, SRCCOPY | CAPTUREBLT);
        GdiFlush(); memcpy(pixels.data(), data, pixels.size() * sizeof(DWORD));
        SelectObject(dc, old); DeleteObject(bitmap); DeleteDC(dc); ReleaseDC(nullptr, screen);
        require(copied && GetForegroundWindow() == hwnd, "Capture only the confirmed foreground-owned rectangle");
    }
    void save(const std::filesystem::path& path) {
        std::ofstream file(path, std::ios::binary);
        BITMAPFILEHEADER header{0x4d42, static_cast<DWORD>(sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + pixels.size() * 4),
            0, 0, sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER)};
        BITMAPINFOHEADER info{sizeof(BITMAPINFOHEADER), width, -height, 1, 32, BI_RGB};
        file.write(reinterpret_cast<const char*>(&header), sizeof(header)); file.write(reinterpret_cast<const char*>(&info), sizeof(info));
        file.write(reinterpret_cast<const char*>(pixels.data()), pixels.size() * 4);
    }
    std::size_t magenta(RECT region) const {
        std::size_t count{};
        for (int y = std::max(0L, region.top); y < std::min<LONG>(height, region.bottom); ++y)
            for (int x = std::max(0L, region.left); x < std::min<LONG>(width, region.right); ++x) {
                const auto pixel = pixels[y * width + x];
                if (((pixel >> 16) & 255) > 190 && (pixel & 255) > 190 && ((pixel >> 8) & 255) < 80) ++count;
            }
        return count;
    }
};
HWND colored_edit{}, colored_caption{};
RECT covered{};
int observed{};
std::filesystem::path capture_directory{L"documents-captures"};
LRESULT CALLBACK unique_ink(HWND hwnd, UINT message, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    const auto result = DefSubclassProc(hwnd, message, wp, lp);
    if ((message == WM_CTLCOLOREDIT || message == WM_CTLCOLORSTATIC) &&
        (reinterpret_cast<HWND>(lp) == colored_edit || reinterpret_cast<HWND>(lp) == colored_caption))
        SetTextColor(reinterpret_cast<HDC>(wp), RGB(251, 0, 247));
    return result;
}
void observe(HWND hwnd) {
    DwmFlush(); Frame frame(hwnd);
    require(frame.magenta(covered) == 0, "Root EndDraw already covers native EDIT and STATIC ink");
    if (!observed) frame.save(capture_directory / L"popup-root-enddraw.bmp");
    ++observed;
    std::cout << "Root EndDraw observation " << observed << ": underlying native pixels=0\n";
}
void occlusion(Window& window, HWND hwnd, const std::shared_ptr<Button>& anchor, const std::shared_ptr<TextInput>& under) {
    SetForegroundWindow(hwnd); SetActiveWindow(hwnd); DwmFlush();
    std::cout << "Owned compositor capture=" << (GetForegroundWindow() == hwnd ? "foreground client BitBlt" : "Windows Graphics Capture HWND interop") << '\n';
    colored_edit = native(hwnd, L"EDIT", 0); colored_caption = native(hwnd, L"STATIC", 0);
    SetWindowSubclass(hwnd, unique_ink, 71, 0);
    flush(hwnd); RedrawWindow(colored_edit, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
    RedrawWindow(colored_caption, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW); DwmFlush();
    GetWindowRect(colored_edit, &covered); MapWindowPoints(nullptr, hwnd, reinterpret_cast<POINT*>(&covered), 2);
    const auto edit_region = covered;
    RECT caption{}; GetWindowRect(colored_caption, &caption); MapWindowPoints(nullptr, hwnd, reinterpret_cast<POINT*>(&caption), 2);
    UnionRect(&covered, &covered, &caption);
    Frame before(hwnd); before.save(capture_directory / L"native-magenta-before.bmp");
    const auto edit_pixels = before.magenta(edit_region), caption_pixels = before.magenta(caption);
    require(edit_pixels > 20 && caption_pixels > 20, "Live native fixture contains uniquely colored EDIT and STATIC pixels");
    std::cout << "Live native baseline: EDIT pixels=" << edit_pixels << ", STATIC pixels=" << caption_pixels << '\n';
    auto content = std::make_shared<Stack>(Axis::vertical); content->add(std::make_shared<Label>(L"Popup covers the native pixels"));
    auto popup = std::make_shared<Popup>(content); popup->set_preferred_size({650, 180});
    window.show_popup(popup, *anchor); flush(hwnd);
    const auto scale = GetDpiForWindow(hwnd) / 96.0f;
    const auto b = popup->bounds();
    const RECT panel{static_cast<LONG>((b.x + 3) * scale), static_cast<LONG>((b.y + 3) * scale),
        static_cast<LONG>((b.x + b.width - 3) * scale), static_cast<LONG>((b.y + b.height - 3) * scale)};
    require(IntersectRect(&covered, &covered, &panel), "Popup covers the native fixture");
    DrawingTestAccess::observe(observe);
    for (int i = 0; i < 4; ++i) flush(hwnd);
    DrawingTestAccess::observe(nullptr);
    require(observed >= 4, "Four intermediate root presentations contain no native overlap");
    Frame after(hwnd); after.save(capture_directory / L"popup-live-screen.bmp");
    require(after.magenta(covered) == 0, "Live compositor masks native text under popup");
    suggestion_capture::bitmap(hwnd, nullptr, capture_directory / L"popup-printwindow.bmp");
    window.dismiss_popup(*popup); flush(hwnd); window.focus(*under, true);
    SendMessageW(colored_edit, WM_CHAR, L'K', 0); require(under->text() == L"K", "Underlying native selection and caret remain editable");
    RemoveWindowSubclass(hwnd, unique_ink, 71); colored_edit = colored_caption = nullptr;
}
void run_case(ThemeMode theme, UINT dpi, const std::wstring& executable, bool capture) {
    Window window({L"XUI document contracts", {820, 960}, theme});
    auto root = std::make_shared<Stack>(Axis::vertical); root->set_padding({8, 8, 8, 8}); root->set_spacing(4);
    auto anchor = std::make_shared<Button>(L"Open modal fixture"); root->add(anchor);
    auto under = std::make_shared<TextInput>(L"Unique native caption"); under->set_text(L"Unique native EDIT fixture XXXXX"); root->add(under);
    auto document = std::make_shared<MultilineText>(L"Plain document"); document->set_text(L"Plain fixture\rUnicode \U0001f642");
    document->set_automation_id(L"document"); document->set_preferred_size({400, 90}); root->add(document);
    auto rich = std::make_shared<RichText>(L"Styled fixture"); rich->set_automation_id(L"rich-document");
    rich->set_runs({{L"Bold fixture", true}, {L" Link", false, true, true, L"https://example.com"}});
    rich->set_preferred_size({400, 70}); root->add(rich);
    auto password = std::make_shared<PasswordInput>(L"Password fixture"); password->set_automation_id(L"password");
    password->set_password(L"safe-fixture-secret"); password->set_reveal_policy(PasswordRevealPolicy::explicit_request); root->add(password);
    auto date = std::make_shared<DateTimePicker>(L"Date fixture"); date->set_automation_id(L"date");
    auto time = std::make_shared<DateTimePicker>(L"Time fixture", DateTimePresentation::time);
    auto calendar = std::make_shared<DateTimePicker>(L"Calendar fixture", DateTimePresentation::calendar);
    auto date_row = std::make_shared<Stack>(Axis::horizontal); date_row->set_spacing(8);
    date_row->add(date, 1); date_row->add(time, 1); date_row->add(calendar, 1); root->add(date_row);
    auto status = std::make_shared<InlineStatus>(L"Invalid fixture"); status->set_message(L"Invalid fixture", StatusSeverity::error);
    status->set_automation_id(L"status"); status->set_dismissible(true); root->add(status);
    auto color = std::make_shared<ColorPicker>(); color->set_automation_id(L"color"); root->add(color);
    window.set_content(root);
    int changes{}, secrets{}, dates{}, times{}, days{}, owner_actions{}, results{};
    document->on_change([&](const auto&) { ++changes; }); password->on_change([&] { ++secrets; });
    date->on_change([&](auto) { ++dates; }); anchor->on_click([&] { ++owner_actions; });
    time->on_change([&](auto) { ++times; }); calendar->on_change([&](auto) { ++days; });
    auto content = std::make_shared<Stack>(Axis::vertical);
    auto modal_editor = std::make_shared<MultilineText>(L"Dialog document"); modal_editor->set_text(L"Modal fixture");
    content->add(modal_editor);
    auto dialog = std::make_shared<ContentDialog>(L"Modal fixture dialog", content);
    dialog->popup()->set_automation_id(L"modal-dialog");
    dialog->on_result([&](auto) { ++results; });
    std::atomic<int> stage{}; std::string driver_error;
    window.on_key([&](const KeyEvent& key) {
        const auto hwnd = FindWindowW(L"Xui.Window.1", L"XUI document contracts");
        if (key.key == Key::f8) { password->set_revealed(true); flush(hwnd); return true; }
        if (key.key == Key::f5) {
            status->set_message(L"Announced fixture", StatusSeverity::error); flush(hwnd);
            status->set_message(L"Announced fixture", StatusSeverity::error); flush(hwnd); return true;
        }
        if (key.key == Key::f4) { status->set_visible(false); flush(hwnd); status->show(); flush(hwnd); return true; }
        if (key.key == Key::f6) { window.show_dialog(dialog, *anchor, modal_editor.get()); flush(hwnd); return true; }
        if (key.key == Key::f7) { stage = 3; window.close(); return true; }
        if (key.key != Key::f12) return false;
        RECT outer{}; GetWindowRect(hwnd, &outer); const auto current = GetDpiForWindow(hwnd);
        outer.right = outer.left + MulDiv(outer.right - outer.left, dpi, current);
        outer.bottom = outer.top + MulDiv(outer.bottom - outer.top, dpi, current);
        SendMessageW(hwnd, WM_DPICHANGED, MAKEWPARAM(dpi, dpi), reinterpret_cast<LPARAM>(&outer)); flush(hwnd);
        const auto edit = native(hwnd, MSFTEDIT_CLASS, 0), rich_hwnd = native(hwnd, MSFTEDIT_CLASS, 1);
        require(read(edit).find(L"fixture") != std::wstring::npos, "Native multiline owns actual text");
        window.focus(*document); SendMessageW(edit, EM_SETSEL, 0, 5); SendMessageW(edit, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"Changed"));
        require(changes == 1 && document->text().starts_with(L"Changed"), "Native committed change fires once");
        require(document->command(TextCommand::undo) && changes == 2, "Native undo");
        require(document->command(TextCommand::redo) && changes == 3, "Native redo");
        document->set_read_only(true); flush(hwnd);
        SendMessageW(edit, WM_CHAR, L'Z', 0); require(changes == 3, "Read-only native key is blocked");
        document->set_read_only(false); flush(hwnd);
        document->set_selection({0, 7}); flush(hwnd); CHARRANGE selection{};
        SendMessageW(edit, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&selection));
        require(selection.cpMin == 0 && selection.cpMax == 7, "Retained selection reaches native RichEdit");
        CHARFORMAT2W style{sizeof(style)}; SendMessageW(rich_hwnd, EM_SETSEL, 0, 4);
        SendMessageW(rich_hwnd, EM_GETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&style));
        require((style.dwEffects & CFE_BOLD) != 0, "RichEdit renders actual bold runs");
        const auto styled_runs = rich->runs();
        const std::wstring untrusted = L"{\\rtf1\\object\\objdata untrusted literal}";
        rich->set_runs({{untrusted}}); flush(hwnd);
        require(read(rich_hwnd) == untrusted, "Untrusted RTF syntax remains literal Unicode, not a document importer");
        rich->set_runs(styled_runs); flush(hwnd);
        SendMessageW(edit, WM_IME_STARTCOMPOSITION, 0, 0);
        SetWindowTextW(edit, L"Composition fixture"); require(changes == 3, "Synthetic composition defers application notification");
        SendMessageW(edit, WM_IME_ENDCOMPOSITION, 0, 0); require(changes == 4, "Synthetic composition end commits once");
        SendMessageW(edit, EM_SETSEL, static_cast<WPARAM>(-1), -1);
        SendMessageW(edit, WM_CHAR, 0xd83d, 0); SendMessageW(edit, WM_CHAR, 0xde42, 0);
        require(document->text().ends_with(L"\U0001f642") && changes == 5, "Native UTF-16 surrogate input commits one complete scalar");
        const auto secret = native(hwnd, L"EDIT", 1);
        require((GetWindowLongPtrW(secret, GWL_STYLE) & ES_PASSWORD) != 0, "Real ES_PASSWORD style");
        window.copy_text(L"owned clipboard sentinel"); SendMessageW(secret, EM_SETSEL, 0, -1);
        SendMessageW(secret, WM_COPY, 0, 0);
        eventually([&] { return OpenClipboard(hwnd) != FALSE; }, "Open owned clipboard fixture");
        const auto clipboard = static_cast<const wchar_t*>(GlobalLock(GetClipboardData(CF_UNICODETEXT)));
        require(clipboard && std::wstring_view(clipboard) == L"owned clipboard sentinel", "Password does not copy plaintext");
        GlobalUnlock(GetClipboardData(CF_UNICODETEXT)); CloseClipboard();
        require(secrets == 0, "Password property is silent");
        SendMessageW(secret, EM_SETSEL, static_cast<WPARAM>(-1), -1);
        SendMessageW(secret, WM_CHAR, 0xd83d, 0); SendMessageW(secret, WM_CHAR, 0xde42, 0);
        require(secrets == 1, "Native password surrogate input commits once without exposing a partial secret");
        password->with_password([](std::wstring_view value) { require(value.ends_with(L"\U0001f642"), "Native password preserves non-BMP Unicode"); });
        const auto date_hwnd = native(hwnd, DATETIMEPICK_CLASSW); SYSTEMTIME value{2026, 2, 0, 3, 0, 0, 0, 0};
        SendMessageW(date_hwnd, DTM_SETSYSTEMTIME, GDT_VALID, reinterpret_cast<LPARAM>(&value));
        window.focus(*date);
        SendMessageW(date_hwnd, WM_KEYDOWN, VK_UP, 0); SendMessageW(date_hwnd, WM_KEYUP, VK_UP, 0);
        std::cout << "Native date events=" << dates << " date=" << date->value().year << '-' << date->value().month << '-' << date->value().day << '\n';
        require(dates == 1, "Native date keyboard edit reaches retained model once");
        const auto time_hwnd = native(hwnd, DATETIMEPICK_CLASSW, 1);
        window.focus(*time); SendMessageW(time_hwnd, WM_KEYDOWN, VK_UP, 0); SendMessageW(time_hwnd, WM_KEYUP, VK_UP, 0);
        require(times == 1, "Native time keyboard edit reaches retained model once");
        const auto calendar_hwnd = native(hwnd, MONTHCAL_CLASSW);
        window.focus(*calendar); SendMessageW(calendar_hwnd, WM_KEYDOWN, VK_RIGHT, 0); SendMessageW(calendar_hwnd, WM_KEYUP, VK_RIGHT, 0);
        require(days == 1 && calendar->value().day == 2, "Native calendar keyboard selection reaches retained model");
        const auto underlying_edit = native(hwnd, L"EDIT", 0);
        window.focus(*anchor); window.show_dialog(dialog, *anchor, modal_editor.get()); flush(hwnd);
        std::cout << "Modal open=" << dialog->popup()->is_open() << " owner-enabled=" << IsWindowEnabled(underlying_edit) << " results=" << results << '\n';
        require(!IsWindowEnabled(underlying_edit), "Modal dialog disables native owner controls");
        require(!window.focus(*under), "Modal dialog refuses owner focus");
        const auto owner = GetDlgItem(hwnd, 100);
        SendMessageW(owner, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(10, 10)); SendMessageW(owner, WM_LBUTTONUP, 0, MAKELPARAM(10, 10));
        require(owner_actions == 0 && dialog->popup()->is_open(), "Owner pointer input cannot light-dismiss or activate");
        for (int i = 0; i < 8; ++i) {
            SendMessageW(hwnd, WM_NEXTDLGCTL, 0, FALSE);
            require(GetFocus() != owner && !under->focused(), "Modal focus traversal never reaches owner");
        }
        dialog->on_validate([] { return L"Keep the invalid text visible"; }); dialog->accept(); flush(hwnd);
        require(dialog->popup()->is_open() && dialog->validation()->visible() && results == 0, "Invalid dialog remains open");
        dialog->on_validate({}); dialog->accept(); flush(hwnd);
        require(results == 1 && !dialog->popup()->is_open() && anchor->focused(), "Default dialog action closes and restores focus");
        require(IsWindowEnabled(native(hwnd, L"EDIT", 0)), "Owner controls recover after modal close");
        const auto resources = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
        for (int i = 0; i < 12; ++i) { window.show_dialog(dialog, *anchor, modal_editor.get()); dialog->cancel(); flush(hwnd); }
        require(GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS) == resources, "Repeated native dialogs have stable peer resources");
        DrawingTestAccess::lose(); flush(hwnd); flush(hwnd); require(Drawing::live_targets() == 1, "Native documents recreate only the root target");
        suggestion_capture::bitmap(hwnd, nullptr, capture_directory / (L"documents-" + std::to_wstring(static_cast<int>(theme)) + L"-" + std::to_wstring(dpi) + L".bmp"));
        if (capture) occlusion(window, hwnd, anchor, under);
        window.focus(*anchor); flush(hwnd);
        stage = 1; return true;
    });
    std::jthread driver([&] {
        HWND hwnd{};
        try {
            eventually([&] { hwnd = FindWindowW(L"Xui.Window.1", L"XUI document contracts"); return hwnd != nullptr; }, "Owned window starts");
            Sleep(100); PostMessageW(hwnd, WM_KEYDOWN, VK_F12, 0);
            eventually([&] { return stage == 1 || !IsWindow(hwnd); }, "Native checks finish"); require(IsWindow(hwnd), "Native checks pass");
            Sleep(100);
            const auto idle_paints = SendMessageW(hwnd, WM_APP + 60, 0, 0);
            Sleep(300);
            require(SendMessageW(hwnd, WM_APP + 60, 0, 0) == idle_paints, "Native document families have zero idle root paints");
            if (capture) {
                std::wstring command = L"\"" + executable + L"\" --uia " + std::to_wstring(reinterpret_cast<std::uintptr_t>(hwnd));
                STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION process{};
                require(CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process), "Start external UIA probe");
                const auto wait = WaitForSingleObject(process.hProcess, 30000); DWORD code{}; GetExitCodeProcess(process.hProcess, &code);
                if (wait == WAIT_TIMEOUT) TerminateProcess(process.hProcess, 1);
                CloseHandle(process.hThread); CloseHandle(process.hProcess);
                std::cout << "UIA child exit=" << code << " wait=" << wait << '\n';
                require(wait == WAIT_OBJECT_0 && code == 0, "External document UIA passes");
            } else PostMessageW(hwnd, WM_KEYDOWN, VK_F7, 0);
        } catch (const std::exception& error) {
            driver_error = error.what(); if (hwnd && IsWindow(hwnd)) PostMessageW(hwnd, WM_CLOSE, 0, 0);
        }
    });
    const auto result = Application::run(window); driver.join(); DrawingTestAccess::observe(nullptr);
    if (result) std::wcerr << window.error() << '\n';
    if (!driver_error.empty()) throw std::runtime_error(driver_error);
    require(result == 0 && stage == 3, "Document window case passes"); require(Drawing::live_targets() == 0, "Root target releases after close");
}
void lifetime_case(int kind) {
    auto window = std::make_unique<Window>(WindowOptions{L"XUI document lifetime", {540, 420}});
    auto root = std::make_shared<Stack>(Axis::vertical);
    auto anchor = std::make_shared<Button>(L"Dialog anchor"); root->add(anchor);
    auto text = std::make_shared<MultilineText>(); text->set_text(L"Before"); root->add(text);
    auto password = std::make_shared<PasswordInput>(); root->add(password);
    auto dialog = std::make_shared<ContentDialog>(L"Lifetime dialog", std::make_shared<TextInput>(L"Title"));
    window->set_content(root);
    int callbacks{}, results{}; bool reentered{};
    text->on_change([&](const auto&) { ++callbacks; if (kind == 0) window.reset(); else window->close(); });
    password->on_change([&] { ++callbacks; window->close(); throw std::runtime_error("Expected native password callback failure"); });
    dialog->on_result([&](DialogResult) { ++results; });
    window->on_key([&](const KeyEvent& key) {
        if (key.key != Key::f12) return false;
        const auto hwnd = FindWindowW(L"Xui.Window.1", L"XUI document lifetime");
        if (kind <= 1) {
            const auto editor = native(hwnd, MSFTEDIT_CLASS);
            SendMessageW(editor, EM_SETSEL, 0, -1);
            SendMessageW(editor, EM_REPLACESEL, TRUE, reinterpret_cast<LPARAM>(L"Changed"));
        } else if (kind == 2) {
            SendMessageW(native(hwnd, L"EDIT"), WM_CHAR, L'Z', 0);
        } else {
            window->show_dialog(dialog, *anchor);
            const auto generation = dialog->popup()->generation();
            dialog->on_validate([&] {
                window->dismiss_popup(*dialog->popup());
                window->show_dialog(dialog, *anchor);
                return L"";
            });
            dialog->accept();
            reentered = dialog->popup()->is_open() && dialog->popup()->generation() != generation && results == 1;
            dialog->on_validate({});
            dialog->on_result([&](DialogResult result) {
                ++results; require(result == DialogResult::primary, "Reopened dialog commits only the current generation");
                window->close(); throw std::runtime_error("Expected closed dialog callback failure");
            });
            dialog->accept();
        }
        return true;
    });
    std::jthread driver([&] {
        HWND hwnd{};
        for (int i = 0; i < 400 && !hwnd; ++i) { hwnd = FindWindowW(L"Xui.Window.1", L"XUI document lifetime"); Sleep(10); }
        if (hwnd) PostMessageW(hwnd, WM_KEYDOWN, VK_F12, 0);
    });
    const auto result = Application::run(*window); driver.join();
    require(kind >= 2 ? result == 1 : result == 0, "Native callback closure and exception result");
    require(kind == 3 ? reentered && results == 2 : callbacks == 1, "Native callbacks run once across reentry and close");
    require(!text->command(TextCommand::undo), "Closed native command adapter is revoked");
    require(Drawing::live_targets() == 0, "Callback closure releases the root target");
}
}
int wmain(int argc, wchar_t** argv) {
    try {
        if (argc == 3 && std::wstring_view(argv[1]) == L"--uia") return uia(reinterpret_cast<HWND>(_wcstoui64(argv[2], nullptr, 10)));
        std::filesystem::create_directories(capture_directory);
        wchar_t exe[32768]{}; GetModuleFileNameW(nullptr, exe, 32768);
        for (auto theme : {ThemeMode::dark, ThemeMode::light, ThemeMode::high_contrast})
            for (UINT dpi : {96u, 144u, 192u}) run_case(theme, dpi, exe, theme == ThemeMode::dark && dpi == 96);
        for (int kind = 0; kind < 4; ++kind) lifetime_case(kind);
        std::cout << "Seven native document families, external UIA, 9 theme/DPI cases, native undo/selection, modal isolation, live native occlusion and root EndDraw passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
