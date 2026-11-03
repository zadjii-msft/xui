#include "native_field_accessibility.hpp"
#include <UIAutomation.h>
#include <wrl/client.h>

namespace xui {
namespace {
template<class F> HRESULT guarded(F&& action) noexcept {
    try { return action(); }
    catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
    catch (...) { return E_FAIL; }
}
struct FieldQuery {
    HWND window{};
    std::uint64_t id{};
    ControlRole role{};
    bool enabled{}, focused{};
    std::wstring text;
};
FieldQuery read_query(const std::shared_ptr<NativeFieldAccessibility>& state, PROPERTYID property = 0) {
    std::lock_guard lock(state->mutex);
    const auto& snapshot = state->snapshot;
    FieldQuery result{snapshot.window, snapshot.id, snapshot.role, snapshot.enabled, snapshot.focused};
    if (snapshot.window) {
        if (property == UIA_NamePropertyId) result.text = snapshot.name;
        else if (property == UIA_AutomationIdPropertyId) result.text = snapshot.automation_id;
        else if (property == UIA_HelpTextPropertyId) result.text = snapshot.help_text;
    }
    return result;
}
HRESULT send_action(const FieldQuery& snapshot, LPARAM operation) {
    if (!snapshot.window) return UIA_E_ELEMENTNOTAVAILABLE;
    DWORD_PTR result{};
    if (!SendMessageTimeoutW(snapshot.window, control_action_message, static_cast<WPARAM>(snapshot.id),
        operation, SMTO_ABORTIFHUNG | SMTO_ERRORONEXIT, 1000, &result)) return UIA_E_ELEMENTNOTAVAILABLE;
    return static_cast<HRESULT>(result);
}
class NativeFieldProvider : public IRawElementProviderSimple, public IRawElementProviderFragment,
    public IRawElementProviderFragmentRoot {
public:
    explicit NativeFieldProvider(std::shared_ptr<NativeFieldAccessibility> state) : state_(std::move(state)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        if (iid == IID_IUnknown || iid == __uuidof(IRawElementProviderSimple))
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
    ULONG STDMETHODCALLTYPE Release() override {
        const auto refs = --refs_;
        if (!refs) delete this;
        return refs;
    }
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
        return guarded([&]() -> HRESULT {
            const auto snapshot = read_query(state_, id);
            const auto hwnd = snapshot.window;
            if (!hwnd) return UIA_E_ELEMENTNOTAVAILABLE;
            if (id == UIA_HasKeyboardFocusPropertyId) {
                GUITHREADINFO info{sizeof(info)};
                value->vt = VT_BOOL;
                value->boolVal = GetGUIThreadInfo(GetWindowThreadProcessId(hwnd, nullptr), &info) &&
                    (info.hwndFocus == hwnd || IsChild(hwnd, info.hwndFocus)) ? VARIANT_TRUE : VARIANT_FALSE;
                return S_OK;
            }
            if (snapshot.role == ControlRole::password_input) {
                if (id == UIA_IsPasswordPropertyId) {
                    value->vt = VT_BOOL; value->boolVal = VARIANT_TRUE; return S_OK;
                }
                if (id == UIA_ValueValuePropertyId) return E_ACCESSDENIED;
            }
            if (id == UIA_NamePropertyId || id == UIA_AutomationIdPropertyId || id == UIA_HelpTextPropertyId) {
                const auto& text = snapshot.text;
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
        });
    }
    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        HWND window{};
        { std::lock_guard lock(state_->mutex); window = state_->snapshot.window; }
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
        HWND window{};
        { std::lock_guard lock(state_->mutex); window = state_->snapshot.window; }
        if (!window) return UIA_E_ELEMENTNOTAVAILABLE;
        const auto rect = clipped_bounds(window);
        *value = {static_cast<double>(rect.left), static_cast<double>(rect.top),
            static_cast<double>(rect.right - rect.left), static_cast<double>(rect.bottom - rect.top)};
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return available();
    }
    HRESULT STDMETHODCALLTYPE SetFocus() override {
        FieldQuery snapshot;
        {
            std::lock_guard lock(state_->mutex);
            snapshot.window = state_->snapshot.window; snapshot.id = state_->snapshot.id;
        }
        return send_action(snapshot, 1);
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
        HWND window{};
        { std::lock_guard lock(state_->mutex); window = state_->snapshot.window; }
        if (!window) return UIA_E_ELEMENTNOTAVAILABLE;
        GUITHREADINFO info{sizeof(info)};
        if (GetGUIThreadInfo(GetWindowThreadProcessId(window, nullptr), &info) && info.hwndFocus == window) {
            *value = this; AddRef();
        }
        return S_OK;
    }
protected:
    virtual ~NativeFieldProvider() = default;
    HRESULT available() const {
        std::lock_guard lock(state_->mutex);
        return state_->snapshot.window ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
    }
    std::shared_ptr<NativeFieldAccessibility> state_;
private:
    std::atomic<ULONG> refs_{1};
};

class TextClearProvider final : public NativeFieldProvider, public IInvokeProvider, public IScrollItemProvider {
public:
    explicit TextClearProvider(std::shared_ptr<NativeFieldAccessibility> state) : NativeFieldProvider(std::move(state)) {}
    explicit TextClearProvider(TextClearProvider* root) : NativeFieldProvider(root->state_), root_(root) { root_->AddRef(); }
    ~TextClearProvider() override { if (root_) root_->Release(); }
    TextClearProvider* child() { return new TextClearProvider(root_ ? root_ : this); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        if (root_ && iid == __uuidof(IInvokeProvider)) *value = static_cast<IInvokeProvider*>(this);
        else if (iid == __uuidof(IScrollItemProvider)) *value = static_cast<IScrollItemProvider*>(this);
        else if (root_ && iid == __uuidof(IRawElementProviderFragmentRoot)) return E_NOINTERFACE;
        else return NativeFieldProvider::QueryInterface(iid, value);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return NativeFieldProvider::AddRef(); }
    ULONG STDMETHODCALLTYPE Release() override { return NativeFieldProvider::Release(); }
    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* value) override {
        if (!value) return E_POINTER;
        *value = static_cast<ProviderOptions>(ProviderOptions_ServerSideProvider |
            ProviderOptions_ProviderOwnsSetFocus | ProviderOptions_UseComThreading);
        return available();
    }
    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return root_ ? available() : NativeFieldProvider::get_HostRawElementProvider(value);
    }
    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID id, IUnknown** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        const auto result = available();
        if (FAILED(result)) return result;
        if (root_ && id == UIA_InvokePatternId) *value = static_cast<IInvokeProvider*>(this);
        else if (root_ && id == UIA_ScrollItemPatternId) *value = static_cast<IScrollItemProvider*>(this);
        if (*value) AddRef();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID id, VARIANT* value) override {
        if (!value) return E_POINTER;
        VariantInit(value);
        return guarded([&]() -> HRESULT {
            const auto snapshot = read_query(state_, !root_ &&
                (id == UIA_NamePropertyId || id == UIA_AutomationIdPropertyId) ? 0 : id);
            if (!snapshot.window) return UIA_E_ELEMENTNOTAVAILABLE;
            if (id == UIA_NamePropertyId || id == UIA_AutomationIdPropertyId ||
                id == UIA_FrameworkIdPropertyId || id == UIA_HelpTextPropertyId) {
                std::wstring fallback;
                std::wstring_view text = id == UIA_FrameworkIdPropertyId ?
                    std::wstring_view{L"XUI"} : std::wstring_view{snapshot.text};
                if (root_ && id == UIA_AutomationIdPropertyId && text.empty()) {
                    fallback = std::to_wstring(snapshot.id); text = fallback;
                }
                value->vt = VT_BSTR;
                value->bstrVal = SysAllocStringLen(text.data(), static_cast<UINT>(text.size()));
                return value->bstrVal ? S_OK : E_OUTOFMEMORY;
            }
            if (id == UIA_ControlTypePropertyId) {
                value->vt = VT_I4;
                value->lVal = root_ ? UIA_ButtonControlTypeId : UIA_PaneControlTypeId;
            } else if (id == UIA_IsEnabledPropertyId || id == UIA_HasKeyboardFocusPropertyId ||
                id == UIA_IsKeyboardFocusablePropertyId || id == UIA_IsControlElementPropertyId ||
                id == UIA_IsContentElementPropertyId || id == UIA_IsOffscreenPropertyId) {
                value->vt = VT_BOOL;
                const auto rect = clipped_bounds(snapshot.window);
                const bool result = id == UIA_IsOffscreenPropertyId ? IsRectEmpty(&rect) != FALSE :
                    id == UIA_IsEnabledPropertyId ? snapshot.enabled : root_ && (
                    id == UIA_HasKeyboardFocusPropertyId ? snapshot.focused :
                    id == UIA_IsKeyboardFocusablePropertyId ? snapshot.enabled : true);
                value->boolVal = result ? VARIANT_TRUE : VARIANT_FALSE;
            }
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE Invoke() override { return action(0); }
    HRESULT STDMETHODCALLTYPE ScrollIntoView() override {
        return guarded([&] { return send_action(read_query(state_), 6); });
    }
    HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection direction, IRawElementProviderFragment** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return guarded([&]() -> HRESULT {
            if (FAILED(available())) return UIA_E_ELEMENTNOTAVAILABLE;
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
            const auto snapshot = read_query(state_);
            if (!snapshot.window) return UIA_E_ELEMENTNOTAVAILABLE;
            if (!root_) return S_OK;
            int ids[]{UiaAppendRuntimeId, static_cast<int>(snapshot.id >> 32), static_cast<int>(snapshot.id)};
            *value = SafeArrayCreateVector(VT_I4, 0, 3);
            if (!*value) return E_OUTOFMEMORY;
            for (LONG i = 0; i < 3; ++i) {
                const auto result = SafeArrayPutElement(*value, &i, &ids[i]);
                if (FAILED(result)) { SafeArrayDestroy(*value); *value = nullptr; return result; }
            }
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE SetFocus() override { return action(1); }
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
        UiaRect rect{};
        const auto result = get_BoundingRectangle(&rect);
        if (FAILED(result)) return result;
        return guarded([&]() -> HRESULT {
            if (x >= rect.left && x < rect.left + rect.width && y >= rect.top && y < rect.top + rect.height)
                *value = child();
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE GetFocus(IRawElementProviderFragment** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return guarded([&]() -> HRESULT {
            const auto snapshot = read_query(state_);
            if (!snapshot.window) return UIA_E_ELEMENTNOTAVAILABLE;
            if (snapshot.focused) *value = child();
            return S_OK;
        });
    }
private:
    HRESULT action(LPARAM operation) {
        return guarded([&]() -> HRESULT {
            const auto snapshot = read_query(state_);
            if (!snapshot.window) return UIA_E_ELEMENTNOTAVAILABLE;
            if (!snapshot.enabled) return UIA_E_ELEMENTNOTENABLED;
            return send_action(snapshot, operation);
        });
    }
    TextClearProvider* root_{};
};
void boolean_event(IRawElementProviderSimple* provider, PROPERTYID property, bool before, bool after) {
    if (before == after) return;
    VARIANT old_value{}, new_value{};
    old_value.vt = new_value.vt = VT_BOOL;
    old_value.boolVal = before ? VARIANT_TRUE : VARIANT_FALSE;
    new_value.boolVal = after ? VARIANT_TRUE : VARIANT_FALSE;
    UiaRaiseAutomationPropertyChangedEvent(provider, property, old_value, new_value);
}
}

IRawElementProviderSimple* create_native_field_provider(std::shared_ptr<NativeFieldAccessibility> state) {
    return new NativeFieldProvider(std::move(state));
}
IRawElementProviderSimple* create_text_clear_provider(std::shared_ptr<NativeFieldAccessibility> state) {
    return new TextClearProvider(std::move(state));
}
void publish_native_field(const std::shared_ptr<NativeFieldAccessibility>& state, const Control& control, HWND window) {
    HostAccessibilityIdentity previous;
    replace_host_snapshot(state, accessibility_identity(control, window), previous);
}
void publish_text_clear(const std::shared_ptr<NativeFieldAccessibility>& state,
    IRawElementProviderSimple* provider, const Control& control, HWND window) {
    auto next = accessibility_identity(control, window);
    const bool enabled = next.enabled, focused = next.focused;
    HostAccessibilityIdentity previous;
    std::wstring next_name;
    bool name_changed{};
    {
        std::lock_guard lock(state->mutex);
        if (state->snapshot == next) return;
        name_changed = state->snapshot.name != next.name;
        if (name_changed && provider) next_name = next.name;
        previous = std::move(state->snapshot);
        state->snapshot = std::move(next);
    }
    if (!provider || !UiaClientsAreListening()) return;
    Microsoft::WRL::ComPtr<IRawElementProviderSimple> element;
    element.Attach(static_cast<TextClearProvider*>(provider)->child());
    boolean_event(element.Get(), UIA_IsEnabledPropertyId, previous.enabled, enabled);
    boolean_event(element.Get(), UIA_HasKeyboardFocusPropertyId, previous.focused, focused);
    if (!previous.focused && focused) UiaRaiseAutomationEvent(element.Get(), UIA_AutomationFocusChangedEventId);
    if (name_changed) {
        VARIANT old_value{}, new_value{};
        old_value.vt = new_value.vt = VT_BSTR;
        old_value.bstrVal = SysAllocString(previous.name.c_str());
        new_value.bstrVal = SysAllocString(next_name.c_str());
        if (old_value.bstrVal && new_value.bstrVal)
            UiaRaiseAutomationPropertyChangedEvent(element.Get(), UIA_NamePropertyId, old_value, new_value);
        VariantClear(&old_value);
        VariantClear(&new_value);
    }
}
void disconnect_native_field(const std::shared_ptr<NativeFieldAccessibility>& state, IRawElementProviderSimple* provider) {
    HostAccessibilityIdentity retired;
    {
        std::lock_guard lock(state->mutex);
        retired = std::move(state->snapshot);
        state->snapshot = {};
    }
    if (provider) UiaDisconnectProvider(provider);
}
void raise_text_clear_invoked(IRawElementProviderSimple* provider) {
    if (!provider || !UiaClientsAreListening()) return;
    Microsoft::WRL::ComPtr<IRawElementProviderSimple> element;
    element.Attach(static_cast<TextClearProvider*>(provider)->child());
    UiaRaiseAutomationEvent(element.Get(), UIA_Invoke_InvokedEventId);
}

}
