#include "control_accessibility.hpp"
#include <UIAutomation.h>
#include <wrl/client.h>

namespace xui {
namespace {
ControlSnapshot read(const std::shared_ptr<ControlAccessibility>& state) {
    std::lock_guard lock(state->mutex);
    return state->snapshot;
}
template<class F> HRESULT guarded(F&& f) noexcept {
    try { return f(); }
    catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
    catch (...) { return E_FAIL; }
}
class Provider final : public IRawElementProviderSimple, public IRawElementProviderFragment,
    public IRawElementProviderFragmentRoot, public IInvokeProvider, public IToggleProvider {
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
        *value = static_cast<ProviderOptions>(ProviderOptions_ServerSideProvider |
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
            if (!root_) return S_OK;
            if (id == UIA_InvokePatternId && snapshot.role == ControlRole::button)
                *value = static_cast<IInvokeProvider*>(this);
            if (id == UIA_TogglePatternId && snapshot.role == ControlRole::toggle)
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
            if (id == UIA_NamePropertyId || id == UIA_AutomationIdPropertyId ||
                id == UIA_FrameworkIdPropertyId) {
                const auto text = id == UIA_NamePropertyId ? (root_ ? snapshot.name : L"") :
                    id == UIA_FrameworkIdPropertyId ? L"XUI" : root_ ?
                        (snapshot.automation_id.empty() ? std::to_wstring(snapshot.id) : snapshot.automation_id) : L"";
                value->vt = VT_BSTR;
                value->bstrVal = SysAllocStringLen(text.data(), static_cast<UINT>(text.size()));
                return value->bstrVal ? S_OK : E_OUTOFMEMORY;
            }
            if (id == UIA_ControlTypePropertyId) {
                value->vt = VT_I4;
                value->lVal = !root_ ? UIA_PaneControlTypeId : snapshot.role == ControlRole::button ? UIA_ButtonControlTypeId :
                    snapshot.role == ControlRole::toggle ? UIA_CheckBoxControlTypeId : UIA_TextControlTypeId;
            } else if (id == UIA_IsEnabledPropertyId || id == UIA_HasKeyboardFocusPropertyId ||
                id == UIA_IsKeyboardFocusablePropertyId || id == UIA_IsControlElementPropertyId ||
                id == UIA_IsContentElementPropertyId) {
                value->vt = VT_BOOL;
                const bool result = id == UIA_IsEnabledPropertyId ? snapshot.enabled : root_ && (
                    id == UIA_HasKeyboardFocusPropertyId ? snapshot.focused :
                    id == UIA_IsKeyboardFocusablePropertyId ? snapshot.enabled && snapshot.role != ControlRole::label : true);
                value->boolVal = result ? VARIANT_TRUE : VARIANT_FALSE;
            }
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE Invoke() override { return action(ControlRole::button); }
    HRESULT STDMETHODCALLTYPE Toggle() override { return action(ControlRole::toggle); }
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
            RECT rect{};
            if (IsWindowVisible(snapshot.window) && GetWindowRect(snapshot.window, &rect))
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
            if (snapshot.role == ControlRole::label) return UIA_E_INVALIDOPERATION;
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
            return guarded([&]() -> HRESULT { *value = child(); return S_OK; });
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetFocus(IRawElementProviderFragment** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return guarded([&]() -> HRESULT {
            const auto snapshot = read(state_);
            if (!snapshot.window) return UIA_E_ELEMENTNOTAVAILABLE;
            if (snapshot.focused) *value = child();
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE get_ToggleState(ToggleState* value) override {
        if (!value) return E_POINTER;
        *value = ToggleState_Off;
        return guarded([&]() -> HRESULT {
            const auto snapshot = read(state_);
            if (!snapshot.window) return UIA_E_ELEMENTNOTAVAILABLE;
            if (snapshot.role != ControlRole::toggle) return UIA_E_INVALIDOPERATION;
            *value = snapshot.checked ? ToggleState_On : ToggleState_Off;
            return S_OK;
        });
    }
private:
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
}
IRawElementProviderSimple* create_control_provider(std::shared_ptr<ControlAccessibility> state) {
    return new Provider(std::move(state));
}
void publish_control(const std::shared_ptr<ControlAccessibility>& state,
    IRawElementProviderSimple* provider, const Control& control, HWND window) {
    ControlSnapshot next{window, control.id(), control.role(), control.name(), control.automation_id(), control.enabled(),
        control.focused(), control.role() == ControlRole::toggle && static_cast<const Toggle&>(control).checked()};
    ControlSnapshot previous;
    {
        std::lock_guard lock(state->mutex);
        previous = std::move(state->snapshot);
        state->snapshot = std::move(next);
    }
    if (!provider || !previous.window || !UiaClientsAreListening()) return;
    Microsoft::WRL::ComPtr<IRawElementProviderSimple> element;
    element.Attach(static_cast<Provider*>(provider)->child());
    provider = element.Get();
    boolean_event(provider, UIA_IsEnabledPropertyId, previous.enabled, control.enabled());
    boolean_event(provider, UIA_HasKeyboardFocusPropertyId, previous.focused, control.focused());
    if (!previous.focused && control.focused()) UiaRaiseAutomationEvent(provider, UIA_AutomationFocusChangedEventId);
    if (previous.name != control.name()) {
        VARIANT old_value{}, new_value{};
        old_value.vt = new_value.vt = VT_BSTR;
        old_value.bstrVal = SysAllocString(previous.name.c_str());
        new_value.bstrVal = SysAllocString(control.name().c_str());
        if (old_value.bstrVal && new_value.bstrVal)
            UiaRaiseAutomationPropertyChangedEvent(provider, UIA_NamePropertyId, old_value, new_value);
        VariantClear(&old_value);
        VariantClear(&new_value);
    }
    if (control.role() == ControlRole::toggle && previous.checked != static_cast<const Toggle&>(control).checked()) {
        VARIANT old_value{}, new_value{};
        old_value.vt = new_value.vt = VT_I4;
        old_value.lVal = previous.checked ? ToggleState_On : ToggleState_Off;
        new_value.lVal = static_cast<const Toggle&>(control).checked() ? ToggleState_On : ToggleState_Off;
        UiaRaiseAutomationPropertyChangedEvent(provider, UIA_ToggleToggleStatePropertyId, old_value, new_value);
    }
}
void disconnect_control(const std::shared_ptr<ControlAccessibility>& state, IRawElementProviderSimple* provider) {
    {
        std::lock_guard lock(state->mutex);
        state->snapshot.window = nullptr;
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
