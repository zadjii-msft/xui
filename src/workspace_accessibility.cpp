#include "workspace_accessibility.hpp"
#include <UIAutomation.h>
#include <algorithm>
#include <cmath>
#include <wrl/client.h>

namespace xui {
namespace {
template<class F> HRESULT protect(F&& f) noexcept {
    try { return f(); } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; } catch (...) { return E_FAIL; }
}
class WorkspaceProvider final : public IRawElementProviderSimple, public IRawElementProviderFragment,
    public IRawElementProviderFragmentRoot, public ISelectionProvider, public ISelectionItemProvider,
    public IRangeValueProvider {
public:
    explicit WorkspaceProvider(std::shared_ptr<ControlAccessibility> state) : state_(std::move(state)) {}
    WorkspaceProvider(WorkspaceProvider* root, std::uint64_t id) : state_(root->state_), root_(root), id_(id) { root_->AddRef(); }
    ~WorkspaceProvider() { if (root_) root_->Release(); }
    WorkspaceProvider* child(std::uint64_t id) { return new WorkspaceProvider(root_ ? root_ : this, id); }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IRawElementProviderSimple)) *value = static_cast<IRawElementProviderSimple*>(this);
        else if (iid == __uuidof(IRawElementProviderFragment)) *value = static_cast<IRawElementProviderFragment*>(this);
        else if (!root_ && iid == __uuidof(IRawElementProviderFragmentRoot)) *value = static_cast<IRawElementProviderFragmentRoot*>(this);
        else if (!root_ && iid == __uuidof(ISelectionProvider)) *value = static_cast<ISelectionProvider*>(this);
        else if (root_ && iid == __uuidof(ISelectionItemProvider)) *value = static_cast<ISelectionItemProvider*>(this);
        else if (!root_ && iid == __uuidof(IRangeValueProvider)) *value = static_cast<IRangeValueProvider*>(this);
        else return E_NOINTERFACE;
        AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { const auto refs = --refs_; if (!refs) delete this; return refs; }
    HRESULT snapshot(ControlSnapshot& value) const {
        std::lock_guard lock(state_->mutex);
        if (!state_->snapshot.window) return UIA_E_ELEMENTNOTAVAILABLE;
        value = state_->snapshot;
        if (root_ && std::none_of(value.tabs.begin(), value.tabs.end(), [&](const auto& t) { return t.id == id_; }))
            return UIA_E_ELEMENTNOTAVAILABLE;
        return S_OK;
    }
    template<class F> HRESULT with(F&& f) const {
        return protect([&]() -> HRESULT {
            ControlSnapshot s;
            const auto result = snapshot(s);
            return FAILED(result) ? result : f(s);
        });
    }
    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* value) override {
        if (!value) return E_POINTER;
        *value = static_cast<ProviderOptions>(ProviderOptions_ServerSideProvider |
            ProviderOptions_ProviderOwnsSetFocus | ProviderOptions_UseComThreading);
        return with([](const auto&) { return S_OK; });
    }
    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID id, IUnknown** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return with([&](const auto& s) {
            if (root_ && id == UIA_SelectionItemPatternId) *value = static_cast<ISelectionItemProvider*>(this);
            if (!root_ && s.role == ControlRole::tab_strip && id == UIA_SelectionPatternId) *value = static_cast<ISelectionProvider*>(this);
            if (!root_ && s.role == ControlRole::split_view && id == UIA_RangeValuePatternId) *value = static_cast<IRangeValueProvider*>(this);
            if (*value) AddRef();
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID id, VARIANT* value) override {
        if (!value) return E_POINTER;
        VariantInit(value);
        return with([&](const auto& s) -> HRESULT {
            if (id == UIA_NamePropertyId || id == UIA_AutomationIdPropertyId || id == UIA_FrameworkIdPropertyId) {
                auto name = s.name;
                if (root_) for (const auto& t : s.tabs) if (t.id == id_) name = t.title;
                const auto text = id == UIA_NamePropertyId ? name : id == UIA_FrameworkIdPropertyId ? L"XUI" :
                    s.automation_id + (root_ ? L"-tab-" + std::to_wstring(id_) : L"");
                value->vt = VT_BSTR;
                value->bstrVal = SysAllocStringLen(text.data(), static_cast<UINT>(text.size()));
                return value->bstrVal ? S_OK : E_OUTOFMEMORY;
            }
            if (id == UIA_ControlTypePropertyId) {
                value->vt = VT_I4;
                value->lVal = root_ ? UIA_TabItemControlTypeId : s.role == ControlRole::tab_strip ? UIA_TabControlTypeId : UIA_ThumbControlTypeId;
            } else if (id == UIA_IsControlElementPropertyId || id == UIA_IsContentElementPropertyId ||
                id == UIA_IsKeyboardFocusablePropertyId || id == UIA_IsEnabledPropertyId ||
                id == UIA_IsOffscreenPropertyId || id == UIA_HasKeyboardFocusPropertyId) {
                value->vt = VT_BOOL;
                UiaRect rect{};
                bounds(s, rect);
                const bool result = id == UIA_HasKeyboardFocusPropertyId ? s.focused && (!root_ || s.selected_tab == id_) :
                    id == UIA_IsOffscreenPropertyId ? rect.width <= 0 || rect.height <= 0 :
                    id == UIA_IsEnabledPropertyId || id == UIA_IsKeyboardFocusablePropertyId ? s.enabled : true;
                value->boolVal = result ? VARIANT_TRUE : VARIANT_FALSE;
            }
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return with([&](const auto& s) { return root_ ? S_OK : UiaHostProviderFromHwnd(s.window, value); });
    }
    HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection direction, IRawElementProviderFragment** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return with([&](const auto& s) {
            if (root_ && direction == NavigateDirection_Parent) { *value = root_; root_->AddRef(); }
            else if (!root_ && !s.tabs.empty() &&
                (direction == NavigateDirection_FirstChild || direction == NavigateDirection_LastChild))
                *value = child(direction == NavigateDirection_FirstChild ? s.tabs.front().id : s.tabs.back().id);
            else if (root_ && (direction == NavigateDirection_NextSibling || direction == NavigateDirection_PreviousSibling)) {
                for (std::size_t i = 0; i < s.tabs.size(); ++i) if (s.tabs[i].id == id_) {
                    if (direction == NavigateDirection_NextSibling && i + 1 < s.tabs.size()) *value = child(s.tabs[i + 1].id);
                    if (direction == NavigateDirection_PreviousSibling && i) *value = child(s.tabs[i - 1].id);
                }
            }
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return with([&](const auto& s) -> HRESULT {
            if (!root_) return S_OK;
            int ids[]{UiaAppendRuntimeId, static_cast<int>(s.id >> 32), static_cast<int>(s.id),
                static_cast<int>(id_ >> 32), static_cast<int>(id_)};
            *value = SafeArrayCreateVector(VT_I4, 0, 5);
            if (!*value) return E_OUTOFMEMORY;
            for (LONG i = 0; i < 5; ++i) SafeArrayPutElement(*value, &i, &ids[i]);
            return S_OK;
        });
    }
    void bounds(const ControlSnapshot& s, UiaRect& value) const {
        RECT rect{};
        value = {};
        if (!IsWindowVisible(s.window) || !GetWindowRect(s.window, &rect)) return;
        if (s.role == ControlRole::split_view) {
            const auto scale = GetDpiForWindow(s.window) / 96.0f;
            rect.left += static_cast<LONG>(std::lround(s.split_left * scale));
            rect.right = rect.left + static_cast<LONG>(std::lround(s.split_width * scale));
        }
        if (root_) {
            for (std::size_t i = 0; i < s.tabs.size(); ++i) if (s.tabs[i].id == id_ && s.tab_edges.size() > 2 * i + 1) {
                const auto scale = GetDpiForWindow(s.window) / 96.0f;
                rect.right = rect.left + static_cast<LONG>(std::lround(s.tab_edges[2 * i + 1] * scale));
                rect.left += static_cast<LONG>(std::lround(s.tab_edges[2 * i] * scale));
            }
        }
        for (auto parent = GetParent(s.window); parent; parent = GetParent(parent)) {
            RECT clip{};
            GetClientRect(parent, &clip);
            MapWindowPoints(parent, nullptr, reinterpret_cast<POINT*>(&clip), 2);
            if (!IntersectRect(&rect, &rect, &clip)) return;
        }
        value = {static_cast<double>(rect.left), static_cast<double>(rect.top),
            static_cast<double>(std::max(0L, rect.right - rect.left)), static_cast<double>(std::max(0L, rect.bottom - rect.top))};
    }
    HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* value) override {
        if (!value) return E_POINTER;
        *value = {};
        return with([&](const auto& s) { bounds(s, *value); return S_OK; });
    }
    HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return with([](const auto&) { return S_OK; });
    }
    static HRESULT send(const ControlSnapshot& s, LPARAM operation) {
        if (!s.enabled) return UIA_E_ELEMENTNOTENABLED;
        DWORD_PTR result{};
        if (!SendMessageTimeoutW(s.window, control_action_message, static_cast<WPARAM>(s.id), operation,
            SMTO_ABORTIFHUNG | SMTO_ERRORONEXIT, 1000, &result)) return UIA_E_ELEMENTNOTAVAILABLE;
        return static_cast<HRESULT>(result);
    }
    HRESULT STDMETHODCALLTYPE SetFocus() override {
        return with([&](const auto& s) {
            const auto selected = root_ ? send(s, static_cast<LPARAM>(id_) + 100) : S_OK;
            return FAILED(selected) ? selected : send(s, 1);
        });
    }
    HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return with([&](const auto&) { *value = root_ ? root_ : this; (*value)->AddRef(); return S_OK; });
    }
    HRESULT STDMETHODCALLTYPE ElementProviderFromPoint(double x, double y, IRawElementProviderFragment** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return with([&](const auto& s) {
            UiaRect rect{}; bounds(s, rect);
            if (x < rect.left || x >= rect.left + rect.width || y < rect.top || y >= rect.top + rect.height) return S_OK;
            for (auto& tab : s.tabs) {
                auto* item = child(tab.id);
                UiaRect b{}; item->bounds(s, b);
                if (x >= b.left && x < b.left + b.width) { *value = item; return S_OK; }
                item->Release();
            }
            *value = this; AddRef(); return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE GetFocus(IRawElementProviderFragment** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return with([&](const auto& s) {
            if (s.focused) {
                if (s.selected_tab) *value = child(*s.selected_tab);
                else { *value = this; AddRef(); }
            }
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE GetSelection(SAFEARRAY** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return with([&](const auto& s) -> HRESULT {
            *value = SafeArrayCreateVector(VT_UNKNOWN, 0, s.selected_tab ? 1 : 0);
            if (!*value) return E_OUTOFMEMORY;
            if (s.selected_tab) {
                auto* item = child(*s.selected_tab);
                LONG index{};
                auto result = SafeArrayPutElement(*value, &index, static_cast<IRawElementProviderSimple*>(item));
                item->Release();
                if (FAILED(result)) { SafeArrayDestroy(*value); *value = nullptr; return result; }
            }
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE get_CanSelectMultiple(BOOL* value) override { return boolean(value, false); }
    HRESULT STDMETHODCALLTYPE get_IsSelectionRequired(BOOL* value) override { return boolean(value, true); }
    HRESULT boolean(BOOL* value, bool result) {
        if (!value) return E_POINTER;
        *value = FALSE;
        return with([&](const auto&) { *value = result; return S_OK; });
    }
    HRESULT STDMETHODCALLTYPE Select() override { return with([&](const auto& s) { return root_ ? send(s, static_cast<LPARAM>(id_) + 100) : UIA_E_INVALIDOPERATION; }); }
    HRESULT STDMETHODCALLTYPE AddToSelection() override { return Select(); }
    HRESULT STDMETHODCALLTYPE RemoveFromSelection() override { return with([](const auto&) { return UIA_E_INVALIDOPERATION; }); }
    HRESULT STDMETHODCALLTYPE get_IsSelected(BOOL* value) override {
        if (!value) return E_POINTER;
        *value = FALSE;
        return with([&](const auto& s) { *value = s.selected_tab == id_; return S_OK; });
    }
    HRESULT STDMETHODCALLTYPE get_SelectionContainer(IRawElementProviderSimple** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return with([&](const auto&) { if (root_) { *value = root_; root_->AddRef(); } return S_OK; });
    }
    HRESULT STDMETHODCALLTYPE SetValue(double value) override {
        if (!std::isfinite(value) || value < 10 || value > 90) return E_INVALIDARG;
        return with([&](const auto& s) { return send(s, 1000 + static_cast<LPARAM>(std::lround(value * 100))); });
    }
    HRESULT STDMETHODCALLTYPE get_Value(double* value) override { return number(value, -1); }
    HRESULT STDMETHODCALLTYPE get_IsReadOnly(BOOL* value) override { return boolean(value, false); }
    HRESULT STDMETHODCALLTYPE get_Maximum(double* value) override { return number(value, 90); }
    HRESULT STDMETHODCALLTYPE get_Minimum(double* value) override { return number(value, 10); }
    HRESULT STDMETHODCALLTYPE get_LargeChange(double* value) override { return number(value, 10); }
    HRESULT STDMETHODCALLTYPE get_SmallChange(double* value) override { return number(value, 2.5); }
    HRESULT number(double* value, double result) {
        if (!value) return E_POINTER;
        *value = 0;
        return with([&](const auto& s) { *value = result < 0 ? s.split_ratio * 100 : result; return S_OK; });
    }
private:
    std::atomic<ULONG> refs_{1};
    std::shared_ptr<ControlAccessibility> state_;
    WorkspaceProvider* root_{};
    std::uint64_t id_{};
};
}
IRawElementProviderSimple* create_workspace_provider(std::shared_ptr<ControlAccessibility> state) {
    return new WorkspaceProvider(std::move(state));
}
void raise_workspace_changes(IRawElementProviderSimple* provider, const ControlSnapshot& before, const ControlSnapshot& after) {
    if (!provider || !before.window || !UiaClientsAreListening()) return;
    auto* root = static_cast<WorkspaceProvider*>(provider);
    if (before.focused != after.focused) {
        VARIANT old_value{}, new_value{};
        old_value.vt = new_value.vt = VT_BOOL;
        old_value.boolVal = before.focused ? VARIANT_TRUE : VARIANT_FALSE;
        new_value.boolVal = after.focused ? VARIANT_TRUE : VARIANT_FALSE;
        UiaRaiseAutomationPropertyChangedEvent(provider, UIA_HasKeyboardFocusPropertyId, old_value, new_value);
    }
    if (before.tabs != after.tabs) UiaRaiseStructureChangedEvent(provider, StructureChangeType_ChildrenInvalidated, nullptr, 0);
    if (before.selected_tab != after.selected_tab && after.selected_tab) {
        const auto changed = [&](std::uint64_t id, bool selected) {
            auto* tab = root->child(id);
            VARIANT old_value{}, new_value{};
            old_value.vt = new_value.vt = VT_BOOL;
            old_value.boolVal = selected ? VARIANT_FALSE : VARIANT_TRUE;
            new_value.boolVal = selected ? VARIANT_TRUE : VARIANT_FALSE;
            UiaRaiseAutomationPropertyChangedEvent(tab, UIA_SelectionItemIsSelectedPropertyId, old_value, new_value);
            tab->Release();
        };
        if (before.selected_tab && std::any_of(after.tabs.begin(), after.tabs.end(),
            [&](const auto& tab) { return tab.id == before.selected_tab; })) changed(*before.selected_tab, false);
        changed(*after.selected_tab, true);
        auto* tab = root->child(*after.selected_tab);
        UiaRaiseAutomationEvent(tab, UIA_SelectionItem_ElementSelectedEventId);
        tab->Release();
    }
    if (after.focused && (!before.focused || before.selected_tab != after.selected_tab)) {
        if (after.selected_tab) {
            auto* tab = root->child(*after.selected_tab);
            UiaRaiseAutomationEvent(tab, UIA_AutomationFocusChangedEventId);
            tab->Release();
        } else UiaRaiseAutomationEvent(provider, UIA_AutomationFocusChangedEventId);
    }
    if (before.split_ratio != after.split_ratio) {
        VARIANT old_value{}, new_value{};
        old_value.vt = new_value.vt = VT_R8;
        old_value.dblVal = before.split_ratio * 100;
        new_value.dblVal = after.split_ratio * 100;
        UiaRaiseAutomationPropertyChangedEvent(provider, UIA_RangeValueValuePropertyId, old_value, new_value);
    }
}
}
