#include "control_accessibility.hpp"
#include <UIAutomation.h>
#include <algorithm>
#include <cmath>

namespace xui {
namespace {
class CollectionProvider final : public IRawElementProviderSimple, public IRawElementProviderFragment,
    public IRawElementProviderFragmentRoot, public ISelectionProvider, public ISelectionItemProvider,
    public IScrollProvider, public IScrollItemProvider, public IExpandCollapseProvider, public IInvokeProvider,
    public IVirtualizedItemProvider, public IItemContainerProvider, public IToggleProvider {
    std::atomic<ULONG> refs_{1};
    std::shared_ptr<ControlAccessibility> state_;
    CollectionProvider* root_{};
    std::optional<ItemKey> key_;
    bool action_{};
public:
    explicit CollectionProvider(std::shared_ptr<ControlAccessibility> state) : state_(std::move(state)) {}
    CollectionProvider(CollectionProvider* root, ItemKey key, bool action = false) :
        state_(root->state_), root_(root->root_ ? root->root_ : root), key_(key), action_(action) { root_->AddRef(); }
    ~CollectionProvider() { if (root_) root_->Release(); }
    CollectionProvider* make(ItemKey key, bool action = false) { return new CollectionProvider(this, key, action); }
    template<class F> HRESULT with(F&& callback) const noexcept {
        try {
            ControlSnapshot s; { std::lock_guard lock(state_->mutex); s = state_->snapshot; }
            if (!s.window || (key_ && (!s.collection || !s.collection->find(*key_)))) return UIA_E_ELEMENTNOTAVAILABLE;
            return callback(s);
        } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; } catch (...) { return E_FAIL; }
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** value) override {
        if (!value) return E_POINTER; *value = nullptr;
        if (id == __uuidof(IUnknown) || id == __uuidof(IRawElementProviderSimple)) *value = static_cast<IRawElementProviderSimple*>(this);
        else if (id == __uuidof(IRawElementProviderFragment)) *value = static_cast<IRawElementProviderFragment*>(this);
        else if (!key_ && id == __uuidof(IRawElementProviderFragmentRoot)) *value = static_cast<IRawElementProviderFragmentRoot*>(this);
        else if (!key_ && id == __uuidof(ISelectionProvider)) *value = static_cast<ISelectionProvider*>(this);
        else if (!key_ && id == __uuidof(IScrollProvider)) *value = static_cast<IScrollProvider*>(this);
        else if (!key_ && id == __uuidof(IItemContainerProvider)) *value = static_cast<IItemContainerProvider*>(this);
        else if (key_ && !action_ && id == __uuidof(ISelectionItemProvider)) *value = static_cast<ISelectionItemProvider*>(this);
        else if (key_ && id == __uuidof(IScrollItemProvider)) *value = static_cast<IScrollItemProvider*>(this);
        else if (key_ && id == __uuidof(IVirtualizedItemProvider)) *value = static_cast<IVirtualizedItemProvider*>(this);
        else if (key_ && !action_ && id == __uuidof(IExpandCollapseProvider)) *value = static_cast<IExpandCollapseProvider*>(this);
        else if (key_ && id == __uuidof(IInvokeProvider)) *value = static_cast<IInvokeProvider*>(this);
        else if (key_ && !action_ && id == __uuidof(IToggleProvider)) *value = static_cast<IToggleProvider*>(this);
        else return E_NOINTERFACE;
        AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { const auto n = --refs_; if (!n) delete this; return n; }
    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* value) override {
        if (!value) return E_POINTER;
        *value = static_cast<ProviderOptions>(ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading | ProviderOptions_ProviderOwnsSetFocus);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID id, IUnknown** value) override {
        if (!value) return E_POINTER; *value = nullptr;
        return with([&](const auto& s) {
            if (!key_) {
                if (id == UIA_SelectionPatternId) *value = static_cast<ISelectionProvider*>(this);
                if (id == UIA_ScrollPatternId) *value = static_cast<IScrollProvider*>(this);
                if (id == UIA_ItemContainerPatternId) *value = static_cast<IItemContainerProvider*>(this);
            } else {
                const auto item = s.collection->item(*s.collection->find(*key_));
                if (id == UIA_SelectionItemPatternId && !action_ && !item.separator && !s.collection->hierarchy(*s.collection->find(*key_)).group)
                    *value = static_cast<ISelectionItemProvider*>(this);
                if (id == UIA_ExpandCollapsePatternId && !action_ && s.collection->hierarchy(*s.collection->find(*key_)).expandable)
                    *value = static_cast<IExpandCollapseProvider*>(this);
                if (id == UIA_ScrollItemPatternId) *value = static_cast<IScrollItemProvider*>(this);
                if (id == UIA_VirtualizedItemPatternId) *value = static_cast<IVirtualizedItemProvider*>(this);
                if (id == UIA_InvokePatternId && !item.separator) *value = static_cast<IInvokeProvider*>(this);
                if (id == UIA_TogglePatternId && !action_ && !item.separator && item.checked)
                    *value = static_cast<IToggleProvider*>(this);
            }
            if (*value) AddRef(); return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID id, VARIANT* value) override {
        if (!value) return E_POINTER; VariantInit(value);
        return with([&](const auto& s) -> HRESULT {
            const auto index = key_ ? s.collection->find(*key_) : std::nullopt;
            const auto item = index ? s.collection->item(*index) : ItemContent{};
            const auto info = index ? s.collection->hierarchy(*index) : ItemHierarchy{};
            if (id == UIA_NamePropertyId || id == UIA_AutomationIdPropertyId || id == UIA_FrameworkIdPropertyId ||
                id == UIA_HelpTextPropertyId || id == UIA_AcceleratorKeyPropertyId) {
                std::wstring text = id == UIA_FrameworkIdPropertyId ? L"XUI" :
                    id == UIA_AcceleratorKeyPropertyId ? (action_ ? L"F2" : L"") :
                    id == UIA_NamePropertyId ? (!key_ ? s.name : action_ ? item.action : item.primary) :
                    id == UIA_HelpTextPropertyId ? (!key_ ? s.help_text : item.secondary) :
                    !key_ ? s.automation_id : std::to_wstring(key_->id) + L":" + std::to_wstring(key_->version) + (action_ ? L":action" : L"");
                value->vt = VT_BSTR; value->bstrVal = SysAllocString(text.c_str()); return value->bstrVal ? S_OK : E_OUTOFMEMORY;
            }
            if (id == UIA_ControlTypePropertyId || id == UIA_LevelPropertyId || id == UIA_PositionInSetPropertyId || id == UIA_SizeOfSetPropertyId) {
                value->vt = VT_I4;
                value->lVal = id == UIA_LevelPropertyId ? static_cast<LONG>(info.depth + 1) :
                    id == UIA_PositionInSetPropertyId ? static_cast<LONG>(info.position ? info.position : index.value_or(0) + 1) :
                    id == UIA_SizeOfSetPropertyId ? static_cast<LONG>(info.count ? info.count : s.collection ? s.collection->size() : 0) :
                    action_ ? UIA_ButtonControlTypeId : !key_ ? (s.role == ControlRole::command_menu ? UIA_MenuControlTypeId :
                    s.role == ControlRole::tree_view ? UIA_TreeControlTypeId : UIA_ListControlTypeId) :
                    item.separator ? UIA_SeparatorControlTypeId : s.role == ControlRole::command_menu ? UIA_MenuItemControlTypeId :
                    s.role == ControlRole::tree_view ? UIA_TreeItemControlTypeId : info.group ? UIA_GroupControlTypeId : UIA_ListItemControlTypeId;
            } else if (id == UIA_IsControlElementPropertyId || id == UIA_IsContentElementPropertyId || id == UIA_IsKeyboardFocusablePropertyId ||
                id == UIA_IsEnabledPropertyId || id == UIA_HasKeyboardFocusPropertyId || id == UIA_IsOffscreenPropertyId) {
                UiaRect b{}; bounds(s, b);
                const bool result = id == UIA_IsOffscreenPropertyId ? b.width <= 0 || b.height <= 0 :
                    id == UIA_IsEnabledPropertyId ? s.enabled && (!key_ || item.enabled) :
                    id == UIA_IsKeyboardFocusablePropertyId ? s.enabled && !action_ && (!key_ || (item.enabled && !item.separator)) :
                    id == UIA_HasKeyboardFocusPropertyId ? s.focused && !action_ && (!key_ || s.selection.focused() == key_) : true;
                value->vt = VT_BOOL; value->boolVal = result ? VARIANT_TRUE : VARIANT_FALSE;
            }
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** value) override {
        if (!value) return E_POINTER; *value = nullptr;
        return with([&](const auto& s) { return key_ ? S_OK : UiaHostProviderFromHwnd(s.window, value); });
    }
    HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection direction, IRawElementProviderFragment** value) override {
        if (!value) return E_POINTER; *value = nullptr;
        return with([&](const auto& s) {
            if (!s.collection) return S_OK;
            const auto index = key_ ? s.collection->find(*key_) : std::nullopt;
            if (action_) {
                if (direction == NavigateDirection_Parent) *value = make(*key_);
                if (direction == NavigateDirection_NextSibling && s.role == ControlRole::tree_view)
                    if (const auto child = s.collection->navigate(index, CollectionNavigation::first_child)) *value = make(s.collection->key(*child));
                return S_OK;
            }
            if (key_ && (direction == NavigateDirection_FirstChild || direction == NavigateDirection_LastChild) &&
                !s.collection->item(*index).action.empty() && (direction == NavigateDirection_FirstChild ||
                    !s.collection->navigate(index, CollectionNavigation::last_child))) {
                *value = make(*key_, true); return S_OK;
            }
            if (key_ && direction == NavigateDirection_Parent) {
                const auto parent = s.collection->hierarchy(*index).parent;
                if (s.role == ControlRole::tree_view && parent) *value = make(*parent);
                else { *value = root_; root_->AddRef(); }
                return S_OK;
            }
            const bool child = direction == NavigateDirection_FirstChild || direction == NavigateDirection_LastChild;
            if (!key_ && !child) return S_OK;
            const auto parent = s.role != ControlRole::tree_view ? std::optional<ItemKey>{} :
                child ? key_ : s.collection->hierarchy(*index).parent;
            if (key_ && child && s.role != ControlRole::tree_view) return S_OK;
            std::set<std::size_t> realized;
            const auto first = std::min(s.collection->size(), static_cast<std::size_t>(s.collection_offset / s.collection_item_height) * s.collection_columns);
            const auto end = std::min(s.collection->size(), first +
                (static_cast<std::size_t>(std::ceil(s.collection_height / s.collection_item_height)) + 1) * s.collection_columns);
            for (auto i = first; i < end; ++i) {
                realized.insert(i);
                if (s.role == ControlRole::tree_view) {
                    auto ancestor = s.collection->hierarchy(i).parent;
                    for (std::size_t depth = 0; ancestor && depth < TreeView::maximum_depth; ++depth) {
                        const auto row = s.collection->find(*ancestor); if (!row) break;
                        realized.insert(*row); ancestor = s.collection->hierarchy(*row).parent;
                    }
                }
            }
            std::optional<std::size_t> target;
            for (auto i : realized) {
                if (s.role == ControlRole::tree_view && s.collection->hierarchy(i).parent != parent) continue;
                if (direction == NavigateDirection_FirstChild || (direction == NavigateDirection_NextSibling && i > *index)) { target = i; break; }
                if (direction == NavigateDirection_LastChild || (direction == NavigateDirection_PreviousSibling && i < *index)) target = i;
            }
            if (target) *value = make(s.collection->key(*target));
            else if (direction == NavigateDirection_PreviousSibling && parent) {
                const auto parent_row = s.collection->find(*parent);
                if (parent_row && !s.collection->item(*parent_row).action.empty()) *value = make(*parent, true);
            }
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** value) override {
        if (!value) return E_POINTER; *value = nullptr;
        return with([&](const auto& s) -> HRESULT {
            if (!key_) return S_OK;
            int ids[]{UiaAppendRuntimeId, static_cast<int>(s.id), static_cast<int>(s.id >> 32), static_cast<int>(key_->id),
                static_cast<int>(key_->id >> 32), static_cast<int>(key_->version), static_cast<int>(key_->version >> 32), action_};
            *value = SafeArrayCreateVector(VT_I4, 0, 8); if (!*value) return E_OUTOFMEMORY;
            for (LONG i = 0; i < 8; ++i) SafeArrayPutElement(*value, &i, &ids[i]); return S_OK;
        });
    }
    void bounds(const ControlSnapshot& s, UiaRect& value) const {
        value = {}; RECT window{};
        if (!IsWindowVisible(s.window) || !GetWindowRect(s.window, &window)) return;
        const auto scale = GetDpiForWindow(s.window) / 96.0;
        double x{}, y{}, width = s.collection_width, height = s.collection_height;
        if (key_) {
            const auto row = *s.collection->find(*key_);
            width = std::max(0.0, width - VirtualCollection::bar_width) / s.collection_columns;
            x = (row % s.collection_columns) * width; y = (row / s.collection_columns) * s.collection_item_height - s.collection_offset;
            height = s.collection_item_height;
            if (action_) { if (width < 160) return; x += width - 74; width = 74; }
        }
        const auto left = std::max(0.0, x), right = std::min(s.collection_width, x + width);
        const auto top = std::max(0.0, y), bottom = std::min(s.collection_height, y + height);
        if (right <= left || bottom <= top) return;
        value = {window.left + left * scale, window.top + top * scale, (right - left) * scale, (bottom - top) * scale};
    }
    HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* value) override {
        if (!value) return E_POINTER; *value = {}; return with([&](const auto& s) { bounds(s, *value); return S_OK; });
    }
    HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** value) override { if (!value) return E_POINTER; *value = nullptr; return S_OK; }
    HRESULT send(const ControlSnapshot& s, GridAction action) {
        if (!s.enabled || (key_ && !s.collection->item(*s.collection->find(*key_)).enabled)) return UIA_E_ELEMENTNOTENABLED;
        std::uint64_t token{};
        { std::lock_guard lock(state_->mutex);
            if (state_->snapshot.window != s.window || state_->grid_actions.size() >= 32) return UIA_E_ELEMENTNOTAVAILABLE;
            token = ++state_->next_grid_action; state_->grid_actions.emplace(token, std::move(action)); }
        DWORD_PTR result{};
        const auto ok = SendMessageTimeoutW(s.window, grid_action_message, static_cast<WPARAM>(s.id), static_cast<LPARAM>(token),
            SMTO_ABORTIFHUNG | SMTO_ERRORONEXIT, 1000, &result);
        { std::lock_guard lock(state_->mutex); state_->grid_actions.erase(token); }
        return ok ? static_cast<HRESULT>(result) : UIA_E_ELEMENTNOTAVAILABLE;
    }
    HRESULT action(GridAction::Kind kind) { return with([&](const auto& s) { return send(s, {kind, key_}); }); }
    HRESULT STDMETHODCALLTYPE SetFocus() override { return action_ ? UIA_E_INVALIDOPERATION : action(GridAction::focus); }
    HRESULT STDMETHODCALLTYPE Select() override { return action(GridAction::select); }
    HRESULT STDMETHODCALLTYPE AddToSelection() override { return action(GridAction::add); }
    HRESULT STDMETHODCALLTYPE RemoveFromSelection() override { return action(GridAction::remove); }
    HRESULT STDMETHODCALLTYPE ScrollIntoView() override { return action(GridAction::reveal); }
    HRESULT STDMETHODCALLTYPE Realize() override { return ScrollIntoView(); }
    HRESULT STDMETHODCALLTYPE Expand() override { return action(GridAction::expand); }
    HRESULT STDMETHODCALLTYPE Collapse() override { return action(GridAction::collapse); }
    HRESULT STDMETHODCALLTYPE Invoke() override { return action(action_ ? GridAction::inline_action : GridAction::invoke); }
    HRESULT STDMETHODCALLTYPE get_ExpandCollapseState(ExpandCollapseState* value) override {
        if (!value) return E_POINTER; *value = ExpandCollapseState_LeafNode;
        return with([&](const auto& s) {
            if (key_) { const auto info = s.collection->hierarchy(*s.collection->find(*key_));
                *value = !info.expandable ? ExpandCollapseState_LeafNode : info.expanded ? ExpandCollapseState_Expanded : ExpandCollapseState_Collapsed; }
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot** value) override {
        if (!value) return E_POINTER; *value = root_ ? root_ : this; (*value)->AddRef(); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_SelectionContainer(IRawElementProviderSimple** value) override {
        if (!value) return E_POINTER; *value = root_ ? root_ : this; (*value)->AddRef(); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetSelection(SAFEARRAY** value) override {
        if (!value) return E_POINTER; *value = nullptr;
        return with([&](const auto& s) -> HRESULT {
            const auto selected = s.selection.selected_keys(s.collection);
            if (!selected) return UIA_E_INVALIDOPERATION;
            *value = SafeArrayCreateVector(VT_UNKNOWN, 0, static_cast<ULONG>(selected->size()));
            if (!*value) return E_OUTOFMEMORY;
            for (LONG i = 0; i < static_cast<LONG>(selected->size()); ++i) {
                auto* item = make((*selected)[i]); const auto result = SafeArrayPutElement(*value, &i, static_cast<IRawElementProviderSimple*>(item)); item->Release();
                if (FAILED(result)) { SafeArrayDestroy(*value); *value = nullptr; return result; }
            }
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE get_CanSelectMultiple(BOOL* value) override {
        if (!value) return E_POINTER; return with([&](const auto& s) { *value = s.role != ControlRole::command_menu && !s.single_selection; return S_OK; });
    }
    HRESULT STDMETHODCALLTYPE Toggle() override { return Invoke(); }
    HRESULT STDMETHODCALLTYPE get_ToggleState(ToggleState* value) override {
        if (!value) return E_POINTER; *value = ToggleState_Off;
        return with([&](const auto& s) -> HRESULT {
            if (!key_ || action_) return UIA_E_INVALIDOPERATION;
            const auto checked = s.collection->item(*s.collection->find(*key_)).checked;
            if (!checked) return UIA_E_INVALIDOPERATION;
            *value = *checked ? ToggleState_On : ToggleState_Off; return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE get_IsSelectionRequired(BOOL* value) override { if (!value) return E_POINTER; *value = FALSE; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_IsSelected(BOOL* value) override {
        if (!value) return E_POINTER; *value = FALSE; return with([&](const auto& s) { *value = key_ && s.selection.contains(*key_); return S_OK; });
    }
    HRESULT STDMETHODCALLTYPE ElementProviderFromPoint(double x, double y, IRawElementProviderFragment** value) override {
        if (!value) return E_POINTER; *value = nullptr;
        return with([&](const auto& s) {
            RECT window{}; GetWindowRect(s.window, &window); const auto scale = GetDpiForWindow(s.window) / 96.0;
            x = (x - window.left) / scale; y = (y - window.top) / scale;
            if (x < 0 || y < 0 || x >= s.collection_width || y >= s.collection_height) return S_OK;
            const auto width = std::max(0.0, s.collection_width - VirtualCollection::bar_width) / s.collection_columns;
            if (s.collection && width > 0 && x < width * s.collection_columns) {
                const auto row = static_cast<std::size_t>((y + s.collection_offset) / s.collection_item_height) * s.collection_columns + static_cast<std::size_t>(x / width);
                if (row < s.collection->size()) {
                    const auto item = s.collection->item(row);
                    *value = make(s.collection->key(row), width >= 160 && !item.action.empty() && std::fmod(x, width) >= width - 74);
                }
            }
            if (!*value) { *value = this; AddRef(); } return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE GetFocus(IRawElementProviderFragment** value) override {
        if (!value) return E_POINTER; *value = nullptr;
        return with([&](const auto& s) {
            if (s.focused) {
                if (s.selection.focused() && s.collection && s.collection->find(*s.selection.focused())) *value = make(*s.selection.focused());
                else { *value = this; AddRef(); }
            }
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE FindItemByProperty(IRawElementProviderSimple* start, PROPERTYID property, VARIANT match, IRawElementProviderSimple** value) override {
        if (!value) return E_POINTER; *value = nullptr;
        return with([&](const auto& s) -> HRESULT {
            if (!s.collection) return S_OK;
            if (property == UIA_AutomationIdPropertyId && match.vt == VT_BSTR && match.bstrVal) {
                std::wstring text(match.bstrVal, SysStringLen(match.bstrVal)); std::size_t used{};
                ItemKey key{std::stoull(text, &used), 0};
                if (used < text.size() && text[used] == L':') key.version = std::stoull(text.substr(used + 1));
                if (s.collection->find(key)) *value = make(key); return S_OK;
            }
            if (property != 0) return UIA_E_NOTSUPPORTED;
            std::size_t index{};
            if (start) {
                VARIANT id{}; const auto hr = start->GetPropertyValue(UIA_AutomationIdPropertyId, &id);
                if (FAILED(hr) || id.vt != VT_BSTR) { VariantClear(&id); return E_INVALIDARG; }
                std::wstring text(id.bstrVal, SysStringLen(id.bstrVal)); VariantClear(&id); std::size_t used{};
                ItemKey key{std::stoull(text, &used), 0}; if (used < text.size()) key.version = std::stoull(text.substr(used + 1));
                const auto found = s.collection->find(key); if (!found) return E_INVALIDARG; index = *found + 1;
            }
            if (index < s.collection->size()) *value = make(s.collection->key(index)); return S_OK;
        });
    }
    static double maximum(const ControlSnapshot& s) {
        return std::max(0.0, (s.collection ? std::ceil(double(s.collection->size()) / s.collection_columns) * s.collection_item_height : 0) - s.collection_height);
    }
    HRESULT STDMETHODCALLTYPE get_HorizontallyScrollable(BOOL* value) override { if (!value) return E_POINTER; *value = FALSE; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_VerticallyScrollable(BOOL* value) override {
        if (!value) return E_POINTER; return with([&](const auto& s) { *value = maximum(s) > 0; return S_OK; });
    }
    HRESULT STDMETHODCALLTYPE get_HorizontalScrollPercent(double* value) override { if (!value) return E_POINTER; *value = UIA_ScrollPatternNoScroll; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_HorizontalViewSize(double* value) override { if (!value) return E_POINTER; *value = 100; return S_OK; }
    HRESULT STDMETHODCALLTYPE get_VerticalScrollPercent(double* value) override {
        if (!value) return E_POINTER; return with([&](const auto& s) { *value = maximum(s) ? s.collection_offset * 100 / maximum(s) : UIA_ScrollPatternNoScroll; return S_OK; });
    }
    HRESULT STDMETHODCALLTYPE get_VerticalViewSize(double* value) override {
        if (!value) return E_POINTER; return with([&](const auto& s) { *value = maximum(s) ? s.collection_height * 100 / (maximum(s) + s.collection_height) : 100; return S_OK; });
    }
    HRESULT STDMETHODCALLTYPE SetScrollPercent(double horizontal, double vertical) override {
        return with([&](const auto& s) -> HRESULT {
            if (horizontal != UIA_ScrollPatternNoScroll || !std::isfinite(vertical) || (vertical != UIA_ScrollPatternNoScroll && (vertical < 0 || vertical > 100))) return E_INVALIDARG;
            return send(s, {GridAction::scroll, {}, 0, 0, vertical == UIA_ScrollPatternNoScroll ? s.collection_offset : maximum(s) * vertical / 100});
        });
    }
    HRESULT STDMETHODCALLTYPE Scroll(ScrollAmount horizontal, ScrollAmount vertical) override {
        return with([&](const auto& s) -> HRESULT {
            if (horizontal != ScrollAmount_NoAmount || vertical < ScrollAmount_LargeDecrement || vertical > ScrollAmount_SmallIncrement) return E_INVALIDARG;
            const double delta = vertical == ScrollAmount_LargeDecrement ? -s.collection_height : vertical == ScrollAmount_LargeIncrement ? s.collection_height :
                vertical == ScrollAmount_SmallDecrement ? -s.collection_item_height : vertical == ScrollAmount_SmallIncrement ? s.collection_item_height : 0;
            return send(s, {GridAction::scroll, {}, 0, 0, s.collection_offset + delta});
        });
    }
};
}
IRawElementProviderSimple* create_collection_provider(std::shared_ptr<ControlAccessibility> state) { return new CollectionProvider(std::move(state)); }
void raise_collection_changes(IRawElementProviderSimple* provider, const ControlSnapshot& before, const ControlSnapshot& after) {
    if (before.collection != after.collection) UiaRaiseStructureChangedEvent(provider, StructureChangeType_ChildrenInvalidated, nullptr, 0);
    if (!(before.selection == after.selection)) UiaRaiseAutomationEvent(provider, UIA_Selection_InvalidatedEventId);
    if (after.focused && (!before.focused || before.selection.focused() != after.selection.focused()))
        UiaRaiseAutomationEvent(provider, UIA_AutomationFocusChangedEventId);
}
}
