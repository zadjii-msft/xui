#include "control_accessibility.hpp"
#include "xui/map_view.hpp"
#include "xui/image.hpp"
#include "xui/foundation.hpp"
#include "xui/documents.hpp"
#include "workspace_accessibility.hpp"
#include "grid_accessibility.hpp"
#include <UIAutomation.h>
#include <wrl/client.h>
#include <cmath>

namespace xui {
namespace {
RECT clipped_bounds(HWND window) {
    RECT rect{};
    if (!IsWindowVisible(window) || !GetWindowRect(window, &rect)) return {};
    RECT region{};
    const auto kind = GetWindowRgnBox(window, &region);
    if (kind == NULLREGION) return {};
    if (kind != ERROR) {
        OffsetRect(&region, rect.left, rect.top);
        if (!IntersectRect(&rect, &rect, &region)) return {};
    }
    for (auto parent = GetParent(window); parent; parent = GetParent(parent)) {
        RECT clip{};
        GetClientRect(parent, &clip);
        MapWindowPoints(parent, nullptr, reinterpret_cast<POINT*>(&clip), 2);
        if (!IntersectRect(&rect, &rect, &clip)) return {};
    }
    return rect;
}
ControlSnapshot read(const std::shared_ptr<ControlAccessibility>& state) {
    std::lock_guard lock(state->mutex);
    return state->snapshot;
}
template<class F> HRESULT guarded(F&& f) noexcept {
    try { return f(); }
    catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
    catch (...) { return E_FAIL; }
}
// Override focus and geometry. Windows still supplies EDIT's value/text patterns,
// accessible name, selection, caret, undo, and IME implementation.
class NativeClipProvider final : public IRawElementProviderSimple, public IRawElementProviderFragment,
    public IRawElementProviderFragmentRoot {
public:
    explicit NativeClipProvider(std::shared_ptr<ControlAccessibility> state) : state_(std::move(state)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IRawElementProviderSimple))
            *value = static_cast<IRawElementProviderSimple*>(this);
        else if (iid == __uuidof(IRawElementProviderFragment))
            *value = static_cast<IRawElementProviderFragment*>(this);
        else if (iid == __uuidof(IRawElementProviderFragmentRoot))
            *value = static_cast<IRawElementProviderFragmentRoot*>(this);
        else return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { const auto refs = --refs_; if (!refs) delete this; return refs; }
    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* value) override {
        if (!value) return E_POINTER;
        *value = static_cast<ProviderOptions>(ProviderOptions_ServerSideProvider | ProviderOptions_OverrideProvider |
            ProviderOptions_ProviderOwnsSetFocus | ProviderOptions_UseComThreading);
        return available();
    }
    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID, IUnknown** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return available();
    }
    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID id, VARIANT* value) override {
        if (!value) return E_POINTER;
        VariantInit(value);
        std::lock_guard lock(state_->mutex);
        const auto hwnd = state_->snapshot.window;
        if (!hwnd) return UIA_E_ELEMENTNOTAVAILABLE;
        if (id == UIA_HasKeyboardFocusPropertyId) {
            GUITHREADINFO info{sizeof(info)};
            value->vt = VT_BOOL;
            value->boolVal = GetGUIThreadInfo(GetWindowThreadProcessId(hwnd, nullptr), &info) &&
                (info.hwndFocus == hwnd || IsChild(hwnd, info.hwndFocus)) ? VARIANT_TRUE : VARIANT_FALSE;
            return S_OK;
        }
        if (state_->snapshot.role == ControlRole::password_input) {
            if (id == UIA_IsPasswordPropertyId) { value->vt = VT_BOOL; value->boolVal = VARIANT_TRUE; return S_OK; }
            if (id == UIA_ValueValuePropertyId) {
                return E_ACCESSDENIED;
            }
        }
        if (id == UIA_NamePropertyId || id == UIA_AutomationIdPropertyId || id == UIA_HelpTextPropertyId) {
            const auto& text = id == UIA_HelpTextPropertyId ? state_->snapshot.help_text :
                id == UIA_NamePropertyId ? state_->snapshot.name : state_->snapshot.automation_id;
            value->vt = VT_BSTR;
            value->bstrVal = SysAllocStringLen(text.data(), static_cast<UINT>(text.size()));
            return value->bstrVal ? S_OK : E_OUTOFMEMORY;
        }
        const auto rect = clipped_bounds(hwnd);
        if (id == UIA_IsOffscreenPropertyId) {
            value->vt = VT_BOOL;
            value->boolVal = IsRectEmpty(&rect) ? VARIANT_TRUE : VARIANT_FALSE;
        } else if (id == UIA_BoundingRectanglePropertyId) {
            value->vt = VT_ARRAY | VT_R8;
            value->parray = SafeArrayCreateVector(VT_R8, 0, 4);
            if (!value->parray) return E_OUTOFMEMORY;
            double dimensions[]{static_cast<double>(rect.left), static_cast<double>(rect.top),
                static_cast<double>(rect.right - rect.left), static_cast<double>(rect.bottom - rect.top)};
            for (LONG i = 0; i < 4; ++i) {
                const auto result = SafeArrayPutElement(value->parray, &i, &dimensions[i]);
                if (FAILED(result)) { VariantClear(value); return result; }
            }
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        HWND window{};
        {
            std::lock_guard lock(state_->mutex);
            window = state_->snapshot.window;
        }
        return window ? UiaHostProviderFromHwnd(window, value) : UIA_E_ELEMENTNOTAVAILABLE;
    }
    HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection, IRawElementProviderFragment** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return available();
    }
    HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return available();
    }
    HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* value) override {
        if (!value) return E_POINTER;
        *value = {};
        std::lock_guard lock(state_->mutex);
        if (!state_->snapshot.window) return UIA_E_ELEMENTNOTAVAILABLE;
        const auto rect = clipped_bounds(state_->snapshot.window);
        *value = {static_cast<double>(rect.left), static_cast<double>(rect.top),
            static_cast<double>(rect.right - rect.left), static_cast<double>(rect.bottom - rect.top)};
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** value) override {
        return GetRuntimeId(value);
    }
    HRESULT STDMETHODCALLTYPE SetFocus() override {
        HWND window{};
        std::uint64_t id{};
        {
            std::lock_guard lock(state_->mutex);
            window = state_->snapshot.window;
            id = state_->snapshot.id;
        }
        if (!window) return UIA_E_ELEMENTNOTAVAILABLE;
        DWORD_PTR result{};
        if (!SendMessageTimeoutW(window, control_action_message, static_cast<WPARAM>(id), 1,
            SMTO_ABORTIFHUNG | SMTO_ERRORONEXIT, 1000, &result)) return UIA_E_ELEMENTNOTAVAILABLE;
        return static_cast<HRESULT>(result);
    }
    HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        const auto result = available();
        if (SUCCEEDED(result)) { *value = this; AddRef(); }
        return result;
    }
    HRESULT STDMETHODCALLTYPE ElementProviderFromPoint(double x, double y, IRawElementProviderFragment** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        UiaRect rect{};
        const auto result = get_BoundingRectangle(&rect);
        if (SUCCEEDED(result) && x >= rect.left && x < rect.left + rect.width &&
            y >= rect.top && y < rect.top + rect.height) { *value = this; AddRef(); }
        return result;
    }
    HRESULT STDMETHODCALLTYPE GetFocus(IRawElementProviderFragment** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        std::lock_guard lock(state_->mutex);
        const auto window = state_->snapshot.window;
        if (!window) return UIA_E_ELEMENTNOTAVAILABLE;
        GUITHREADINFO info{sizeof(info)};
        if (GetGUIThreadInfo(GetWindowThreadProcessId(window, nullptr), &info) && info.hwndFocus == window) {
            *value = this;
            AddRef();
        }
        return S_OK;
    }
private:
    HRESULT available() {
        std::lock_guard lock(state_->mutex);
        return state_->snapshot.window ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
    }
    std::atomic<ULONG> refs_{1};
    std::shared_ptr<ControlAccessibility> state_;
};
class Provider final : public IRawElementProviderSimple, public IRawElementProviderFragment,
    public IRawElementProviderFragmentRoot, public IInvokeProvider, public IToggleProvider,
    public IScrollProvider, public IScrollItemProvider {
public:
    explicit Provider(std::shared_ptr<ControlAccessibility> state) : state_(std::move(state)) {}
    explicit Provider(Provider* root) : state_(root->state_), root_(root) { root_->AddRef(); }
    ~Provider() { if (root_) root_->Release(); }
    Provider* child() { return new Provider(root_ ? root_ : this); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IRawElementProviderSimple))
            *value = static_cast<IRawElementProviderSimple*>(this);
        else if (root_ && iid == __uuidof(IInvokeProvider)) *value = static_cast<IInvokeProvider*>(this);
        else if (root_ && iid == __uuidof(IToggleProvider)) *value = static_cast<IToggleProvider*>(this);
        else if (iid == __uuidof(IScrollProvider)) *value = static_cast<IScrollProvider*>(this);
        else if (iid == __uuidof(IScrollItemProvider)) *value = static_cast<IScrollItemProvider*>(this);
        else if (iid == __uuidof(IRawElementProviderFragment)) *value = static_cast<IRawElementProviderFragment*>(this);
        else if (!root_ && iid == __uuidof(IRawElementProviderFragmentRoot)) *value = static_cast<IRawElementProviderFragmentRoot*>(this);
        else return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const auto refs = --refs_;
        if (!refs) delete this;
        return refs;
    }
    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* value) override {
        if (!value) return E_POINTER;
        *value = static_cast<ProviderOptions>(ProviderOptions_ServerSideProvider | ProviderOptions_ProviderOwnsSetFocus |
            ProviderOptions_UseComThreading);
        std::lock_guard lock(state_->mutex);
        return state_->snapshot.window ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
    }
    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return guarded([&] {
            const auto snapshot = read(state_);
            if (root_) return snapshot.window ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
            return snapshot.window ? UiaHostProviderFromHwnd(snapshot.window, value) : UIA_E_ELEMENTNOTAVAILABLE;
        });
    }
    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID id, IUnknown** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return guarded([&]() -> HRESULT {
            const auto snapshot = read(state_);
            if (!snapshot.window) return UIA_E_ELEMENTNOTAVAILABLE;
            if (id == UIA_ScrollPatternId && snapshot.role == ControlRole::scroll_view)
                *value = static_cast<IScrollProvider*>(this);
            if (id == UIA_ScrollItemPatternId && root_)
                *value = static_cast<IScrollItemProvider*>(this);
            if (!root_) { if (*value) AddRef(); return S_OK; }
            if (id == UIA_InvokePatternId && snapshot.role == ControlRole::button && !snapshot.toggle_action)
                *value = static_cast<IInvokeProvider*>(this);
            if (id == UIA_TogglePatternId && (snapshot.role == ControlRole::toggle || snapshot.toggle_action))
                *value = static_cast<IToggleProvider*>(this);
            if (*value) AddRef();
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID id, VARIANT* value) override {
        if (!value) return E_POINTER;
        VariantInit(value);
        return guarded([&]() -> HRESULT {
            const auto snapshot = read(state_);
            if (!snapshot.window) return UIA_E_ELEMENTNOTAVAILABLE;
            const bool semantic = root_ || snapshot.role == ControlRole::scroll_view || snapshot.role == ControlRole::content_view;
            if (id == UIA_NamePropertyId || id == UIA_AutomationIdPropertyId ||
                id == UIA_FrameworkIdPropertyId || id == UIA_HelpTextPropertyId) {
                const auto text = id == UIA_HelpTextPropertyId ? snapshot.help_text : id == UIA_NamePropertyId ? (semantic ? snapshot.name : L"") :
                    id == UIA_FrameworkIdPropertyId ? L"XUI" : semantic ?
                        (snapshot.automation_id.empty() ? std::to_wstring(snapshot.id) : snapshot.automation_id) : L"";
                value->vt = VT_BSTR;
                value->bstrVal = SysAllocStringLen(text.data(), static_cast<UINT>(text.size()));
                return value->bstrVal ? S_OK : E_OUTOFMEMORY;
            }
            if (id == UIA_ControlTypePropertyId) {
                value->vt = VT_I4;
                value->lVal = !root_ || snapshot.role == ControlRole::scroll_view || snapshot.role == ControlRole::content_view ? UIA_PaneControlTypeId : snapshot.role == ControlRole::button ? UIA_ButtonControlTypeId :
                    snapshot.role == ControlRole::toggle ? UIA_CheckBoxControlTypeId :
                    snapshot.role == ControlRole::image ? UIA_ImageControlTypeId : UIA_TextControlTypeId;
            } else if (id == UIA_IsEnabledPropertyId || id == UIA_HasKeyboardFocusPropertyId ||
                id == UIA_IsKeyboardFocusablePropertyId || id == UIA_IsControlElementPropertyId ||
                id == UIA_IsContentElementPropertyId || id == UIA_IsOffscreenPropertyId) {
                value->vt = VT_BOOL;
                const auto rect = clipped_bounds(snapshot.window);
                const bool result = id == UIA_IsOffscreenPropertyId ? IsRectEmpty(&rect) != FALSE :
                    id == UIA_IsEnabledPropertyId ? snapshot.enabled : semantic && (
                    id == UIA_HasKeyboardFocusPropertyId ? snapshot.focused :
                    id == UIA_IsKeyboardFocusablePropertyId ? snapshot.enabled && snapshot.role != ControlRole::label &&
                        snapshot.role != ControlRole::image && snapshot.role != ControlRole::content_view &&
                        snapshot.role != ControlRole::history_chart : true);
                value->boolVal = result ? VARIANT_TRUE : VARIANT_FALSE;
            }
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE Invoke() override { return action(ControlRole::button); }
    HRESULT STDMETHODCALLTYPE Toggle() override {
        return guarded([&]() -> HRESULT {
            const auto s = read(state_);
            if (!s.window) return UIA_E_ELEMENTNOTAVAILABLE;
            if (!s.enabled) return UIA_E_ELEMENTNOTENABLED;
            return s.role == ControlRole::toggle || s.toggle_action ? send(s, 0) : UIA_E_INVALIDOPERATION;
        });
    }
    HRESULT STDMETHODCALLTYPE ScrollIntoView() override {
        return guarded([&]() -> HRESULT {
            const auto snapshot = read(state_);
            return snapshot.window ? send(snapshot, 6) : UIA_E_ELEMENTNOTAVAILABLE;
        });
    }
    HRESULT STDMETHODCALLTYPE Scroll(ScrollAmount horizontal, ScrollAmount vertical) override {
        return guarded([&]() -> HRESULT {
            const auto snapshot = read(state_);
            if (!snapshot.window) return UIA_E_ELEMENTNOTAVAILABLE;
            if (!snapshot.enabled) return UIA_E_ELEMENTNOTENABLED;
            if (horizontal != ScrollAmount_NoAmount) return UIA_E_INVALIDOPERATION;
            if (vertical == ScrollAmount_NoAmount) return S_OK;
            if (snapshot.scroll_extent <= snapshot.viewport_height) return UIA_E_INVALIDOPERATION;
            switch (vertical) {
            case ScrollAmount_SmallDecrement: return send(snapshot, 2);
            case ScrollAmount_SmallIncrement: return send(snapshot, 3);
            case ScrollAmount_LargeDecrement: return send(snapshot, 4);
            case ScrollAmount_LargeIncrement: return send(snapshot, 5);
            default: return E_INVALIDARG;
            }
        });
    }
    HRESULT STDMETHODCALLTYPE SetScrollPercent(double horizontal, double vertical) override {
        return guarded([&]() -> HRESULT {
            const auto snapshot = read(state_);
            if (!snapshot.window) return UIA_E_ELEMENTNOTAVAILABLE;
            if (!snapshot.enabled) return UIA_E_ELEMENTNOTENABLED;
            if (horizontal != UIA_ScrollPatternNoScroll) return UIA_E_INVALIDOPERATION;
            if (vertical == UIA_ScrollPatternNoScroll) return S_OK;
            if (!std::isfinite(vertical) || vertical < 0 || vertical > 100) return E_INVALIDARG;
            if (snapshot.scroll_extent <= snapshot.viewport_height) return UIA_E_INVALIDOPERATION;
            return send(snapshot, 1000 + static_cast<LPARAM>(std::lround(vertical * 100)));
        });
    }
    HRESULT STDMETHODCALLTYPE get_HorizontalScrollPercent(double* value) override { return number(value, 0); }
    HRESULT STDMETHODCALLTYPE get_VerticalScrollPercent(double* value) override { return number(value, 1); }
    HRESULT STDMETHODCALLTYPE get_HorizontalViewSize(double* value) override { return number(value, 2); }
    HRESULT STDMETHODCALLTYPE get_VerticalViewSize(double* value) override { return number(value, 3); }
    HRESULT STDMETHODCALLTYPE get_HorizontallyScrollable(BOOL* value) override { return scrollable(value, false); }
    HRESULT STDMETHODCALLTYPE get_VerticallyScrollable(BOOL* value) override { return scrollable(value, true); }
    HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection direction, IRawElementProviderFragment** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return guarded([&]() -> HRESULT {
            if (FAILED(available())) return UIA_E_ELEMENTNOTAVAILABLE;
            if (read(state_).role == ControlRole::scroll_view || read(state_).role == ControlRole::content_view) return S_OK;
            if (root_ && direction == NavigateDirection_Parent) { *value = root_; root_->AddRef(); }
            else if (!root_ && (direction == NavigateDirection_FirstChild || direction == NavigateDirection_LastChild))
                *value = child();
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return guarded([&]() -> HRESULT {
            const auto snapshot = read(state_);
            if (!snapshot.window) return UIA_E_ELEMENTNOTAVAILABLE;
            if (!root_) return S_OK;
            const int id[]{UiaAppendRuntimeId, static_cast<int>(snapshot.id >> 32), static_cast<int>(snapshot.id)};
            *value = SafeArrayCreateVector(VT_I4, 0, 3);
            if (!*value) return E_OUTOFMEMORY;
            for (LONG i = 0; i < 3; ++i) {
                const auto result = SafeArrayPutElement(*value, &i, const_cast<int*>(&id[i]));
                if (FAILED(result)) { SafeArrayDestroy(*value); *value = nullptr; return result; }
            }
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* value) override {
        if (!value) return E_POINTER;
        *value = {};
        return guarded([&]() -> HRESULT {
            const auto snapshot = read(state_);
            if (!snapshot.window) return UIA_E_ELEMENTNOTAVAILABLE;
            const auto rect = clipped_bounds(snapshot.window);
            if (!IsRectEmpty(&rect))
                *value = {static_cast<double>(rect.left), static_cast<double>(rect.top),
                    static_cast<double>(rect.right - rect.left), static_cast<double>(rect.bottom - rect.top)};
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return available();
    }
    HRESULT STDMETHODCALLTYPE SetFocus() override {
        return guarded([&]() -> HRESULT {
            const auto snapshot = read(state_);
            if (!snapshot.window) return UIA_E_ELEMENTNOTAVAILABLE;
            if (!snapshot.enabled) return UIA_E_ELEMENTNOTENABLED;
            if (snapshot.role == ControlRole::label || snapshot.role == ControlRole::image || snapshot.role == ControlRole::history_chart) return UIA_E_INVALIDOPERATION;
            return send(snapshot, 1);
        });
    }
    HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        if (FAILED(available())) return UIA_E_ELEMENTNOTAVAILABLE;
        *value = root_ ? root_ : this;
        (*value)->AddRef();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE ElementProviderFromPoint(double x, double y, IRawElementProviderFragment** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        UiaRect bounds{};
        const auto result = get_BoundingRectangle(&bounds);
        if (FAILED(result)) return result;
        if (x >= bounds.left && x < bounds.left + bounds.width && y >= bounds.top && y < bounds.top + bounds.height) {
            return guarded([&]() -> HRESULT {
                if (read(state_).role == ControlRole::scroll_view || read(state_).role == ControlRole::content_view) { *value = this; AddRef(); }
                else *value = child();
                return S_OK;
            });
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetFocus(IRawElementProviderFragment** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return guarded([&]() -> HRESULT {
            const auto snapshot = read(state_);
            if (!snapshot.window) return UIA_E_ELEMENTNOTAVAILABLE;
            if (snapshot.focused) {
                if (snapshot.role == ControlRole::scroll_view) { *value = this; AddRef(); }
                else *value = child();
            }
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE get_ToggleState(ToggleState* value) override {
        if (!value) return E_POINTER;
        *value = ToggleState_Off;
        return guarded([&]() -> HRESULT {
            const auto snapshot = read(state_);
            if (!snapshot.window) return UIA_E_ELEMENTNOTAVAILABLE;
            if (snapshot.role != ControlRole::toggle && !snapshot.toggle_action) return UIA_E_INVALIDOPERATION;
            *value = snapshot.checked ? ToggleState_On : ToggleState_Off;
            return S_OK;
        });
    }
private:
    HRESULT number(double* value, int property) {
        if (!value) return E_POINTER;
        *value = 0;
        return guarded([&]() -> HRESULT {
            const auto snapshot = read(state_);
            if (!snapshot.window) return UIA_E_ELEMENTNOTAVAILABLE;
            const bool scrolls = snapshot.scroll_extent > snapshot.viewport_height;
            *value = property == 0 ? UIA_ScrollPatternNoScroll : property == 2 ? 100.0 :
                property == 3 ? (scrolls ? snapshot.viewport_height * 100 / snapshot.scroll_extent : 100.0) :
                scrolls ? snapshot.scroll_offset * 100 / (snapshot.scroll_extent - snapshot.viewport_height) : UIA_ScrollPatternNoScroll;
            return S_OK;
        });
    }
    HRESULT scrollable(BOOL* value, bool vertical) {
        if (!value) return E_POINTER;
        *value = FALSE;
        std::lock_guard lock(state_->mutex);
        if (!state_->snapshot.window) return UIA_E_ELEMENTNOTAVAILABLE;
        *value = vertical && state_->snapshot.scroll_extent > state_->snapshot.viewport_height;
        return S_OK;
    }
    HRESULT available() {
        std::lock_guard lock(state_->mutex);
        return state_->snapshot.window ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
    }
    HRESULT send(const ControlSnapshot& snapshot, LPARAM operation) {
        DWORD_PTR result{};
        // Value-only payload and stable identity prevent late messages from invoking a reused HWND.
        if (!SendMessageTimeoutW(snapshot.window, control_action_message,
            static_cast<WPARAM>(snapshot.id), operation, SMTO_ABORTIFHUNG | SMTO_ERRORONEXIT, 1000, &result))
            return UIA_E_ELEMENTNOTAVAILABLE;
        return static_cast<HRESULT>(result);
    }
    HRESULT action(ControlRole role) {
        return guarded([&]() -> HRESULT {
            const auto snapshot = read(state_);
            if (!snapshot.window) return UIA_E_ELEMENTNOTAVAILABLE;
            if (!snapshot.enabled) return UIA_E_ELEMENTNOTENABLED;
            if (snapshot.role != role) return UIA_E_INVALIDOPERATION;
            return send(snapshot, 0);
        });
    }
    std::atomic<ULONG> refs_{1};
    std::shared_ptr<ControlAccessibility> state_;
    Provider* root_{};
};
void boolean_event(IRawElementProviderSimple* provider, PROPERTYID property, bool before, bool after) {
    if (before == after) return;
    VARIANT old_value{}, new_value{};
    old_value.vt = new_value.vt = VT_BOOL;
    old_value.boolVal = before ? VARIANT_TRUE : VARIANT_FALSE;
    new_value.boolVal = after ? VARIANT_TRUE : VARIANT_FALSE;
    UiaRaiseAutomationPropertyChangedEvent(provider, property, old_value, new_value);
}
void name_event(IRawElementProviderSimple* provider, const std::wstring& before, const std::wstring& after) {
    if (before == after) return;
    VARIANT old_value{}, new_value{};
    old_value.vt = new_value.vt = VT_BSTR;
    old_value.bstrVal = SysAllocString(before.c_str());
    new_value.bstrVal = SysAllocString(after.c_str());
    if (old_value.bstrVal && new_value.bstrVal)
        UiaRaiseAutomationPropertyChangedEvent(provider, UIA_NamePropertyId, old_value, new_value);
    VariantClear(&old_value);
    VariantClear(&new_value);
}
}
IRawElementProviderSimple* create_control_provider(std::shared_ptr<ControlAccessibility> state) {
    return new Provider(std::move(state));
}
IRawElementProviderSimple* create_native_clip_provider(std::shared_ptr<ControlAccessibility> state) {
    return new NativeClipProvider(std::move(state));
}
void publish_control(const std::shared_ptr<ControlAccessibility>& state,
    IRawElementProviderSimple* provider, const Control& control, HWND window) {
    const bool enabled = control.enabled() && IsWindowEnabled(window);
    ControlSnapshot next{window, control.id(), control.role(), control.name(), control.automation_id(), enabled,
        control.focused(), control.role() == ControlRole::toggle && static_cast<const Toggle&>(control).checked()};
    next.help_text = control.help_text();
    if (const auto button = dynamic_cast<const Button*>(&control)) {
        next.toggle_action = button->behavior() == ButtonBehavior::toggle;
        next.checked = button->checked();
    }
    if (const auto choices = dynamic_cast<const RadioGroup*>(&control)) {
        next.selected_tab = choices->selected(); next.vertical_choices = true;
        for (std::size_t i = 0; i < choices->items().size(); ++i) {
            const auto& item = choices->items()[i];
            next.tabs.push_back({item.id, item.text}); next.choice_enabled.push_back(item.enabled);
            const auto b = choices->item_bounds(i);
            next.tab_edges.push_back(b.y); next.tab_edges.push_back(b.y + b.height);
        }
    }
    if (const auto combo = dynamic_cast<const ComboBox*>(&control)) {
        next.selected_tab = combo->selected(); next.expanded = combo->popup()->is_open();
        for (const auto& item : combo->items()) {
            next.tabs.push_back({item.id, item.text}); next.choice_enabled.push_back(item.enabled);
            // Collapsed items have no screen geometry.
            next.tab_edges.push_back(0); next.tab_edges.push_back(0);
        }
    }
    const auto range_values = [&](const NumericRange& range, double value) {
        next.minimum = range.minimum; next.maximum = range.maximum;
        next.small_step = range.small_step; next.large_step = range.large_step; next.value = value;
    };
    if (const auto range = dynamic_cast<const RangeInput*>(&control)) range_values(range->range(), range->preview_value());
    if (const auto number = dynamic_cast<const NumericInput*>(&control)) {
        range_values(number->range(), number->value()); next.invalid = !number->valid(); next.value_text = number->editor()->text();
        if (next.invalid) next.help_text = L"Enter a finite number within the allowed range.";
    }
    if (const auto progress = dynamic_cast<const Progress*>(&control)) {
        range_values(progress->range(), progress->value()); next.read_only = true;
        next.invalid = progress->state() == ProgressState::indeterminate || progress->state() == ProgressState::unknown;
        next.value_text = progress->value_text();
        next.help_text += progress->state() == ProgressState::indeterminate ? L" In progress." :
            progress->state() == ProgressState::unknown ? L" Unknown." : progress->state() == ProgressState::error ? L" Error." :
            progress->state() == ProgressState::paused ? L" Paused." : L" " + progress->value_text();
    }
    if (const auto expander = dynamic_cast<const Expander*>(&control)) next.expanded = expander->expanded();
    if (const auto popup = dynamic_cast<const Popup*>(&control)) next.dialog_surface = popup->dialog_surface();
    if (const auto status = dynamic_cast<const InlineStatus*>(&control)) {
        const wchar_t* severity[]{L"Information: ", L"Success: ", L"Warning: ", L"Error: "};
        next.name = severity[static_cast<int>(status->severity())] + status->name();
        next.invalid = status->severity() == StatusSeverity::error;
        next.visible = provider && IsWindowVisible(window);
    }
    if (control.role() == ControlRole::image) {
        const auto& image = static_cast<const Image&>(control);
        if (image.status() == ImageStatus::error) next.name += L". " + image.error();
        else if (image.status() == ImageStatus::loading) next.name += L". Loading image.";
        else if (image.status() == ImageStatus::empty) next.name += L". No image.";
    }
    if (control.role() == ControlRole::scroll_view) {
        const auto& scroll = static_cast<const ScrollView&>(control);
        next.scroll_offset = scroll.offset();
        next.scroll_extent = scroll.extent();
        next.viewport_height = scroll.bounds().height;
    }
    if (const auto tabs = dynamic_cast<const TabStrip*>(&control)) {
        next.tabs = tabs->tabs();
        next.selected_tab = tabs->selected();
        for (std::size_t i = 0; i < tabs->tabs().size(); ++i) {
            const auto b = tabs->tab_bounds(i);
            next.tab_edges.push_back(b.x);
            next.tab_edges.push_back(b.x + b.width);
        }
    }
    if (const auto split = dynamic_cast<const SplitView*>(&control)) {
        next.split_ratio = split->ratio();
        next.split_left = split->divider().x - split->bounds().x;
        next.split_width = split->divider().width;
        next.minimum = 10; next.maximum = 90; next.small_step = 2.5; next.large_step = 10; next.value = split->ratio() * 100;
    }
    if (const auto grid = dynamic_cast<const DataGrid*>(&control)) {
        next.grid = grid->source(); next.columns = grid->columns(); next.column_order = grid->column_order(); next.selected_row = grid->selected();
        next.grid_x = grid->horizontal_offset(); next.grid_y = grid->offset();
        next.grid_width = grid->viewport_width(); next.grid_height = grid->viewport_height();
        next.sort_column = grid->sort_column(); next.descending = grid->descending();
        next.header_column = grid->columns().empty() ? 0 : grid->source_column(grid->focused_column()); next.header_focus = grid->header_focus();
        next.selection = grid->selection(); next.grid_filters = grid->filters(); next.header_part = grid->header_part(); next.full_source = grid->full_source();
    }
    if (const auto collection = dynamic_cast<const VirtualCollection*>(&control)) {
        next.single_selection = !collection->multiple_selection();
        next.collection = collection->source(); next.selection = collection->selection();
        next.collection_columns = collection->columns(); next.collection_item_height = collection->item_size().height;
        next.collection_offset = collection->offset(); next.collection_width = collection->bounds().width;
        next.collection_height = collection->bounds().height;
    }
    if (const auto map = dynamic_cast<const MapView*>(&control)) {
        const auto center = map->center();
        next.help_text += L" Latitude " + std::to_wstring(center.latitude) + L", longitude " +
            std::to_wstring(center.longitude) + L", zoom " + std::to_wstring(map->zoom()) + L".";
        if (!map->error().empty()) next.help_text += L" Provider error: " + map->error();
    }
    ControlSnapshot previous;
    {
        std::lock_guard lock(state->mutex);
        if (state->snapshot == next) return;
        previous = std::move(state->snapshot);
        state->snapshot = std::move(next);
    }
    if (!provider || !previous.window || !UiaClientsAreListening()) return;
    if (control.role() == ControlRole::inline_status) {
        const auto current = read(state);
        name_event(provider, previous.name, current.name);
        if ((previous.name != current.name || !previous.visible) && current.visible)
            UiaRaiseAutomationEvent(provider, UIA_LiveRegionChangedEventId);
        return;
    }
    if (control.role() == ControlRole::items_view || control.role() == ControlRole::tree_view || control.role() == ControlRole::command_menu) {
        raise_collection_changes(provider, previous, read(state)); return;
    }
    if (control.role() == ControlRole::data_grid) {
        raise_grid_changes(provider, previous, read(state));
        return;
    }
    if (control.role() == ControlRole::tab_strip || control.role() == ControlRole::split_view ||
        control.role() >= ControlRole::popup) {
        raise_workspace_changes(provider, previous, read(state));
        return;
    }
    if (control.role() == ControlRole::content_view) return;
    if (control.role() == ControlRole::scroll_view) {
        name_event(provider, previous.name, control.role() == ControlRole::image ? read(state).name : control.name());
        const auto& scroll = static_cast<const ScrollView&>(control);
        const double before = previous.scroll_extent > previous.viewport_height ?
            previous.scroll_offset * 100 / (previous.scroll_extent - previous.viewport_height) : UIA_ScrollPatternNoScroll;
        const double after = scroll.maximum_offset() > 0 ? scroll.offset() * 100 / scroll.maximum_offset() : UIA_ScrollPatternNoScroll;
        const auto number_event = [&](PROPERTYID property, double old_number, double new_number) {
            if (old_number == new_number) return;
            VARIANT old_value{}, new_value{};
            old_value.vt = new_value.vt = VT_R8;
            old_value.dblVal = old_number;
            new_value.dblVal = new_number;
            UiaRaiseAutomationPropertyChangedEvent(provider, property, old_value, new_value);
        };
        number_event(UIA_ScrollVerticalScrollPercentPropertyId, before, after);
        number_event(UIA_ScrollVerticalViewSizePropertyId, previous.scroll_extent > previous.viewport_height ?
            previous.viewport_height * 100 / previous.scroll_extent : 100, scroll.extent() > scroll.bounds().height ?
            scroll.bounds().height * 100 / scroll.extent() : 100);
        boolean_event(provider, UIA_ScrollVerticallyScrollablePropertyId,
            previous.scroll_extent > previous.viewport_height, scroll.maximum_offset() > 0);
        boolean_event(provider, UIA_IsEnabledPropertyId, previous.enabled, enabled);
        boolean_event(provider, UIA_HasKeyboardFocusPropertyId, previous.focused, control.focused());
        if (!previous.focused && control.focused()) UiaRaiseAutomationEvent(provider, UIA_AutomationFocusChangedEventId);
        return;
    }
    Microsoft::WRL::ComPtr<IRawElementProviderSimple> element;
    element.Attach(static_cast<Provider*>(provider)->child());
    provider = element.Get();
    boolean_event(provider, UIA_IsEnabledPropertyId, previous.enabled, enabled);
    boolean_event(provider, UIA_HasKeyboardFocusPropertyId, previous.focused, control.focused());
    if (!previous.focused && control.focused()) UiaRaiseAutomationEvent(provider, UIA_AutomationFocusChangedEventId);
    name_event(provider, previous.name, control.name());
    const auto snapshot = read(state);
    if ((control.role() == ControlRole::toggle || snapshot.toggle_action) && previous.checked != snapshot.checked) {
        VARIANT old_value{}, new_value{};
        old_value.vt = new_value.vt = VT_I4;
        old_value.lVal = previous.checked ? ToggleState_On : ToggleState_Off;
        new_value.lVal = snapshot.checked ? ToggleState_On : ToggleState_Off;
        UiaRaiseAutomationPropertyChangedEvent(provider, UIA_ToggleToggleStatePropertyId, old_value, new_value);
    }
}
void disconnect_control(const std::shared_ptr<ControlAccessibility>& state, IRawElementProviderSimple* provider) {
    {
        std::lock_guard lock(state->mutex);
        state->snapshot = {};
        state->grid_actions.clear(); state->foundation_actions.clear();
    }
    if (provider) UiaDisconnectProvider(provider);
}
void raise_control_invoked(IRawElementProviderSimple* provider) {
    if (!provider || !UiaClientsAreListening()) return;
    Microsoft::WRL::ComPtr<IRawElementProviderSimple> element;
    element.Attach(static_cast<Provider*>(provider)->child());
    UiaRaiseAutomationEvent(element.Get(), UIA_Invoke_InvokedEventId);
}
}
