#include "grid_accessibility.hpp"
#include <UIAutomation.h>
#include <algorithm>
#include <cmath>
#include <climits>

namespace xui {
namespace {
class GridProvider final : public IRawElementProviderSimple, public IRawElementProviderFragment,
    public IRawElementProviderFragmentRoot, public IGridProvider, public ITableProvider,
    public IGridItemProvider, public ITableItemProvider, public ISelectionProvider,
    public ISelectionItemProvider, public IInvokeProvider, public IScrollProvider, public IScrollItemProvider,
    public IToggleProvider, public IValueProvider {
    enum Kind { root, header, row, cell, header_filter, header_check };
    std::atomic<ULONG> refs_{1};
    std::shared_ptr<ControlAccessibility> state_;
    GridProvider* root_{};
    Kind kind_{root};
    RowKey key_{};
    std::size_t column_{};
public:
    explicit GridProvider(std::shared_ptr<ControlAccessibility> state) : state_(std::move(state)) {}
    GridProvider(GridProvider* owner, Kind kind, RowKey key, std::size_t column) :
        state_(owner->state_), root_(owner->root_ ? owner->root_ : owner), kind_(kind), key_(key), column_(column) { root_->AddRef(); }
    ~GridProvider() { if (root_) root_->Release(); }
    GridProvider* make(Kind kind, RowKey key = {}, std::size_t column = 0) { return new GridProvider(this, kind, key, column); }
    std::size_t ordinal(const ControlSnapshot& s) const {
        return static_cast<std::size_t>(std::find(s.column_order.begin(), s.column_order.end(), column_) - s.column_order.begin());
    }
    void changes(const ControlSnapshot& before, const ControlSnapshot& after) {
        const auto active = [](const ControlSnapshot& s) {
            return s.grid && s.selected_row && s.grid->find(*s.selected_row) ? s.selected_row : std::nullopt;
        };
        const auto previous = active(before), current = active(after);
        if (before.grid != after.grid) UiaRaiseAutomationEvent(this, UIA_LayoutInvalidatedEventId);
        if (before.columns != after.columns || before.column_order != after.column_order) {
            UiaRaiseAutomationEvent(this, UIA_LayoutInvalidatedEventId);
            UiaRaiseStructureChangedEvent(this, StructureChangeType_ChildrenInvalidated, nullptr, 0);
        }
        if ((before.grid ? before.grid->size() : 0) != (after.grid ? after.grid->size() : 0))
            UiaRaiseStructureChangedEvent(this, StructureChangeType_ChildrenInvalidated, nullptr, 0);
        if (previous != current || !(before.selection == after.selection)) {
            UiaRaiseAutomationEvent(this, UIA_Selection_InvalidatedEventId);
            if (current) {
                auto* item = make(row, *current);
                UiaRaiseAutomationEvent(item, UIA_SelectionItem_ElementSelectedEventId);
                item->Release();
            }
            if (!(before.selection == after.selection)) for (std::size_t c = 0; c < after.columns.size(); ++c) if (after.columns[c].checkable) {
                auto* item = make(header_check, {}, after.column_order[c]);
                VARIANT old_value{}, new_value{}; old_value.vt = new_value.vt = VT_I4;
                const auto old_state = before.selection.state(before.grid, before.full_source), new_state = after.selection.state(after.grid, after.full_source);
                old_value.lVal = old_state == SelectionState::all ? ToggleState_On : old_state == SelectionState::mixed ? ToggleState_Indeterminate : ToggleState_Off;
                new_value.lVal = new_state == SelectionState::all ? ToggleState_On : new_state == SelectionState::mixed ? ToggleState_Indeterminate : ToggleState_Off;
                if (old_value.lVal != new_value.lVal) UiaRaiseAutomationPropertyChangedEvent(item, UIA_ToggleToggleStatePropertyId, old_value, new_value);
                item->Release();
            }
        }
        if (after.focused && (!before.focused || before.header_focus != after.header_focus ||
            (after.header_focus ? before.header_column != after.header_column : previous != current))) {
            auto* item = after.header_focus ? make(header, {}, after.header_column) : current ? make(row, *current) : nullptr;
            UiaRaiseAutomationEvent(item ? item : this, UIA_AutomationFocusChangedEventId);
            if (item) item->Release();
        }
        if (before.sort_column != after.sort_column || before.descending != after.descending) {
            for (auto column : {before.sort_column, after.sort_column}) {
                if (column >= before.columns.size() || column >= after.columns.size()) continue;
                auto* item = make(header, {}, column);
                const auto old_name = item->name(before), new_name = item->name(after);
                VARIANT old_value{}, new_value{};
                old_value.vt = new_value.vt = VT_BSTR;
                old_value.bstrVal = SysAllocString(old_name.c_str()); new_value.bstrVal = SysAllocString(new_name.c_str());
                if (old_value.bstrVal && new_value.bstrVal && old_name != new_name)
                    UiaRaiseAutomationPropertyChangedEvent(item, UIA_NamePropertyId, old_value, new_value);
                VariantClear(&old_value); VariantClear(&new_value); item->Release();
                if (before.sort_column == after.sort_column) break;
            }
        }
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        if (id == __uuidof(IUnknown) || id == __uuidof(IRawElementProviderSimple)) *value = static_cast<IRawElementProviderSimple*>(this);
        else if (id == __uuidof(IRawElementProviderFragment)) *value = static_cast<IRawElementProviderFragment*>(this);
        else if (kind_ == root && id == __uuidof(IRawElementProviderFragmentRoot)) *value = static_cast<IRawElementProviderFragmentRoot*>(this);
        else if (kind_ == root && id == __uuidof(IGridProvider)) *value = static_cast<IGridProvider*>(this);
        else if (kind_ == root && id == __uuidof(ITableProvider)) *value = static_cast<ITableProvider*>(this);
        else if (kind_ == root && id == __uuidof(ISelectionProvider)) *value = static_cast<ISelectionProvider*>(this);
        else if (kind_ == root && id == __uuidof(IScrollProvider)) *value = static_cast<IScrollProvider*>(this);
        else if (kind_ == cell && id == __uuidof(IGridItemProvider)) *value = static_cast<IGridItemProvider*>(this);
        else if (kind_ == cell && id == __uuidof(ITableItemProvider)) *value = static_cast<ITableItemProvider*>(this);
        else if ((kind_ == row || kind_ == cell) && id == __uuidof(ISelectionItemProvider)) *value = static_cast<ISelectionItemProvider*>(this);
        else if ((kind_ == row || kind_ == cell) && id == __uuidof(IScrollItemProvider)) *value = static_cast<IScrollItemProvider*>(this);
        else if ((kind_ == row || kind_ == header) && id == __uuidof(IInvokeProvider)) *value = static_cast<IInvokeProvider*>(this);
        else if (kind_ == header_filter && id == __uuidof(IInvokeProvider)) *value = static_cast<IInvokeProvider*>(this);
        else if (kind_ == header_filter && id == __uuidof(IValueProvider)) *value = static_cast<IValueProvider*>(this);
        else if ((kind_ == header_check || kind_ == cell) && id == __uuidof(IToggleProvider)) *value = static_cast<IToggleProvider*>(this);
        else return E_NOINTERFACE;
        AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { const auto n = --refs_; if (!n) delete this; return n; }
    template<class F> HRESULT with(F&& fn) const noexcept {
        try {
            ControlSnapshot s;
            { std::lock_guard lock(state_->mutex); s = state_->snapshot; }
            if (!s.window) return UIA_E_ELEMENTNOTAVAILABLE;
            if ((kind_ == row || kind_ == cell) && (!s.grid || !s.grid->find(key_))) return UIA_E_ELEMENTNOTAVAILABLE;
            if ((kind_ == header || kind_ == cell || kind_ == header_filter || kind_ == header_check) && ordinal(s) >= s.columns.size()) return UIA_E_ELEMENTNOTAVAILABLE;
            if (kind_ == header_filter && !s.columns[ordinal(s)].filterable) return UIA_E_ELEMENTNOTAVAILABLE;
            if (kind_ == header_check && !s.columns[ordinal(s)].checkable) return UIA_E_ELEMENTNOTAVAILABLE;
            return fn(s);
        } catch (const std::bad_alloc&) { return E_OUTOFMEMORY; } catch (...) { return E_FAIL; }
    }
    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* value) override {
        if (!value) return E_POINTER;
        *value = static_cast<ProviderOptions>(ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading | ProviderOptions_ProviderOwnsSetFocus);
        return with([](const auto&) { return S_OK; });
    }
    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID id, IUnknown** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return with([&](const auto& s) {
            if (kind_ == root) {
                if (id == UIA_GridPatternId) *value = static_cast<IGridProvider*>(this);
                if (id == UIA_TablePatternId) *value = static_cast<ITableProvider*>(this);
                if (id == UIA_SelectionPatternId) *value = static_cast<ISelectionProvider*>(this);
                if (id == UIA_ScrollPatternId) *value = static_cast<IScrollProvider*>(this);
            }
            if (kind_ == cell) {
                if (id == UIA_GridItemPatternId) *value = static_cast<IGridItemProvider*>(this);
                if (id == UIA_TableItemPatternId) *value = static_cast<ITableItemProvider*>(this);
            }
            if (kind_ == row || kind_ == cell) {
                if (id == UIA_SelectionItemPatternId) *value = static_cast<ISelectionItemProvider*>(this);
                if (id == UIA_ScrollItemPatternId) *value = static_cast<IScrollItemProvider*>(this);
            }
            if ((kind_ == row || kind_ == header) && id == UIA_InvokePatternId) *value = static_cast<IInvokeProvider*>(this);
            if (kind_ == header_filter && id == UIA_InvokePatternId) *value = static_cast<IInvokeProvider*>(this);
            if (kind_ == header_filter && id == UIA_ValuePatternId) *value = static_cast<IValueProvider*>(this);
            if ((kind_ == header_check || (kind_ == cell && s.columns[ordinal(s)].checkable)) && id == UIA_TogglePatternId)
                *value = static_cast<IToggleProvider*>(this);
            if (*value) AddRef();
            return S_OK;
        });
    }
    std::wstring name(const ControlSnapshot& s) const {
        if (kind_ == root) return s.name;
        if (kind_ == header_filter) return s.columns[ordinal(s)].name + L" filter";
        if (kind_ == header_check) return L"Select all " + s.columns[ordinal(s)].name;
        if (kind_ == header) return s.columns[ordinal(s)].name +
            (s.sort_column == column_ ? (s.descending ? L", sorted descending" : L", sorted ascending") : L", sort");
        const auto index = *s.grid->find(key_);
        if (kind_ == cell) return s.grid->text(index, column_);
        std::wstring value;
        for (std::size_t c = 0; c < s.columns.size(); ++c) {
            if (c) value += L", ";
            value += s.columns[c].name + L": " + s.grid->text(index, s.column_order[c]);
        }
        return value;
    }
    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID id, VARIANT* value) override {
        if (!value) return E_POINTER;
        VariantInit(value);
        return with([&](const auto& s) -> HRESULT {
            if (id == UIA_NamePropertyId || id == UIA_AutomationIdPropertyId || id == UIA_FrameworkIdPropertyId ||
                id == UIA_HelpTextPropertyId) {
                auto text = id == UIA_NamePropertyId ? name(s) : id == UIA_FrameworkIdPropertyId ? L"XUI" :
                    id == UIA_HelpTextPropertyId ? (s.help_text.empty() ? L"F6 switches between headers and rows. Left/Right selects a header. Ctrl+Left/Right resizes it. Ctrl+Shift+Left/Right moves it. Enter sorts a header or opens row details. Drag headers to move columns. Drag a header boundary to resize. Escape cancels a drag." : s.help_text) :
                    kind_ == root ? s.automation_id : s.automation_id + L"-" + std::to_wstring(kind_) + L"-" +
                    std::to_wstring(key_.id) + L"-" + std::to_wstring(key_.version) + L"-" + std::to_wstring(column_);
                value->vt = VT_BSTR; value->bstrVal = SysAllocStringLen(text.data(), static_cast<UINT>(text.size()));
                return value->bstrVal ? S_OK : E_OUTOFMEMORY;
            }
            if (id == UIA_ControlTypePropertyId) {
                value->vt = VT_I4; value->lVal = kind_ == root ? UIA_DataGridControlTypeId :
                    kind_ == header ? UIA_HeaderItemControlTypeId : kind_ == header_filter ? UIA_EditControlTypeId :
                    kind_ == header_check || (kind_ == cell && s.columns[ordinal(s)].checkable) ? UIA_CheckBoxControlTypeId :
                    kind_ == row ? UIA_DataItemControlTypeId : UIA_TextControlTypeId;
            } else if (id == UIA_IsControlElementPropertyId || id == UIA_IsContentElementPropertyId ||
                id == UIA_IsKeyboardFocusablePropertyId || id == UIA_IsEnabledPropertyId ||
                id == UIA_HasKeyboardFocusPropertyId || id == UIA_IsOffscreenPropertyId) {
                UiaRect b{}; bounds(s, b);
                const bool result = id == UIA_IsOffscreenPropertyId ? b.width <= 0 || b.height <= 0 :
                    id == UIA_IsEnabledPropertyId ? s.enabled : id == UIA_IsKeyboardFocusablePropertyId ? s.enabled && kind_ != cell :
                    id == UIA_HasKeyboardFocusPropertyId ? s.focused && (kind_ == root ||
                        (kind_ == header || kind_ == header_filter || kind_ == header_check ? s.header_focus && s.header_column == column_ &&
                            s.header_part == (kind_ == header_filter ? GridHeaderPart::filter : kind_ == header_check ? GridHeaderPart::check : GridHeaderPart::sort) :
                            !s.header_focus && kind_ == row && s.selected_row == key_)) : true;
                value->vt = VT_BOOL; value->boolVal = result ? VARIANT_TRUE : VARIANT_FALSE;
            }
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return with([&](const auto& s) { return kind_ == root ? UiaHostProviderFromHwnd(s.window, value) : S_OK; });
    }
    HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection direction, IRawElementProviderFragment** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return with([&](const auto& s) {
            const auto count = s.grid ? s.grid->size() : 0;
            const auto first = static_cast<std::size_t>(std::min(double(count), s.grid_y / s.grid_geometry.row_height));
            const auto end = first + static_cast<std::size_t>(std::min(double(count - first), std::ceil(s.grid_height / s.grid_geometry.row_height) + 1));
            if (direction == NavigateDirection_Parent && kind_ != root) {
                if (kind_ == cell) *value = make(row, key_);
                else if (kind_ == header_filter || kind_ == header_check) *value = make(header, {}, column_);
                else { *value = root_; root_->AddRef(); }
            } else if (kind_ == root) {
                if (direction == NavigateDirection_FirstChild && !s.columns.empty()) *value = make(header, {}, s.column_order.front());
                if (direction == NavigateDirection_LastChild) {
                    if (end > first) *value = make(row, s.grid->key(end - 1));
                    else if (!s.columns.empty()) *value = make(header, {}, s.column_order.back());
                }
            } else if (kind_ == header && (direction == NavigateDirection_FirstChild || direction == NavigateDirection_LastChild)) {
                const auto& column = s.columns[ordinal(s)];
                if (direction == NavigateDirection_FirstChild) {
                    if (column.checkable) *value = make(header_check, {}, column_);
                    else if (column.filterable) *value = make(header_filter, {}, column_);
                } else {
                    if (column.filterable) *value = make(header_filter, {}, column_);
                    else if (column.checkable) *value = make(header_check, {}, column_);
                }
            } else if (kind_ == row && (direction == NavigateDirection_FirstChild || direction == NavigateDirection_LastChild)) {
                if (!s.columns.empty()) *value = make(cell, key_, direction == NavigateDirection_FirstChild ? s.column_order.front() : s.column_order.back());
            } else if (direction == NavigateDirection_NextSibling || direction == NavigateDirection_PreviousSibling) {
                const bool next = direction == NavigateDirection_NextSibling;
                if (kind_ == header_check || kind_ == header_filter) {
                    if (next && kind_ == header_check && s.columns[ordinal(s)].filterable) *value = make(header_filter, {}, column_);
                    if (!next && kind_ == header_filter && s.columns[ordinal(s)].checkable) *value = make(header_check, {}, column_);
                    return S_OK;
                }
                if (kind_ == header || kind_ == cell || kind_ == header_filter || kind_ == header_check) {
                    const auto column = ordinal(s);
                    if (next && column + 1 < s.columns.size()) *value = make(kind_, key_, s.column_order[column + 1]);
                    else if (!next && column) *value = make(kind_, key_, s.column_order[column - 1]);
                    else if (next && kind_ == header && end > first) *value = make(row, s.grid->key(first));
                } else if (kind_ == row) {
                    const auto index = *s.grid->find(key_);
                    if (next && index + 1 < end) *value = make(row, s.grid->key(index + 1));
                    else if (!next && index > first) *value = make(row, s.grid->key(index - 1));
                    else if (!next && !s.columns.empty()) *value = make(header, {}, s.column_order.back());
                }
            }
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return with([&](const auto& s) -> HRESULT {
            if (kind_ == root) return S_OK;
            int ids[]{UiaAppendRuntimeId, static_cast<int>(s.id), static_cast<int>(s.id >> 32), kind_,
                static_cast<int>(key_.id), static_cast<int>(key_.id >> 32),
                static_cast<int>(key_.version), static_cast<int>(key_.version >> 32), static_cast<int>(column_)};
            *value = SafeArrayCreateVector(VT_I4, 0, 9);
            if (!*value) return E_OUTOFMEMORY;
            for (LONG i = 0; i < 9; ++i) SafeArrayPutElement(*value, &i, &ids[i]);
            return S_OK;
        });
    }
    void bounds(const ControlSnapshot& s, UiaRect& value) const {
        value = {};
        RECT window{};
        if (!IsWindowVisible(s.window) || !GetWindowRect(s.window, &window)) return;
        const double scale = GetDpiForWindow(s.window) / 96.0;
        const auto& g = s.grid_geometry;
        Rect box = g.frame();
        const bool body = kind_ == row || kind_ == cell;
        if (body) box = g.row(*s.grid->find(key_));
        if (kind_ == header || kind_ == header_filter || kind_ == header_check || kind_ == cell) {
            const auto column = g.column(s.columns, ordinal(s));
            box.x = column.x; box.width = column.width;
            if (!body) box = kind_ == header ? column : g.header_part(s.columns, ordinal(s),
                kind_ == header_filter ? GridHeaderPart::filter : GridHeaderPart::check);
        }
        const auto viewport_clip = body ? g.viewport() : kind_ == root ? g.frame() : g.header();
        double left = std::max(box.x, viewport_clip.x), right = std::min(box.x + box.width, viewport_clip.x + viewport_clip.width);
        double top = std::max(box.y, viewport_clip.y), bottom = std::min(box.y + box.height, viewport_clip.y + viewport_clip.height);
        if (right <= left || bottom <= top) return;
        RECT rect{window.left + static_cast<LONG>(std::lround(left * scale)), window.top + static_cast<LONG>(std::lround(top * scale)),
            window.left + static_cast<LONG>(std::lround(right * scale)), window.top + static_cast<LONG>(std::lround(bottom * scale))};
        for (auto parent = GetParent(s.window); parent; parent = GetParent(parent)) {
            RECT clip{}; GetClientRect(parent, &clip); MapWindowPoints(parent, nullptr, reinterpret_cast<POINT*>(&clip), 2);
            if (!IntersectRect(&rect, &rect, &clip)) return;
        }
        value = {double(rect.left), double(rect.top), double(rect.right - rect.left), double(rect.bottom - rect.top)};
    }
    HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* value) override {
        if (!value) return E_POINTER;
        *value = {}; return with([&](const auto& s) { bounds(s, *value); return S_OK; });
    }
    HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** value) override {
        if (!value) return E_POINTER;
        *value = nullptr; return with([](const auto&) { return S_OK; });
    }
    HRESULT send(const ControlSnapshot& s, GridAction action) {
        if (!s.enabled) return UIA_E_ELEMENTNOTENABLED;
        std::uint64_t token;
        { std::lock_guard lock(state_->mutex);
          if (state_->snapshot.window != s.window || state_->grid_actions.size() >= 64) return UIA_E_ELEMENTNOTAVAILABLE;
          token = ++state_->next_grid_action; state_->grid_actions.emplace(token, std::move(action)); }
        DWORD_PTR result{};
        const auto ok = SendMessageTimeoutW(s.window, grid_action_message, static_cast<WPARAM>(s.id),
            static_cast<LPARAM>(token), SMTO_ABORTIFHUNG | SMTO_ERRORONEXIT, 1000, &result);
        { std::lock_guard lock(state_->mutex); state_->grid_actions.erase(token); }
        return ok ? static_cast<HRESULT>(result) : UIA_E_ELEMENTNOTAVAILABLE;
    }
    HRESULT STDMETHODCALLTYPE SetFocus() override {
        return with([&](const auto& s) { return send(s, {kind_ == header || kind_ == header_filter || kind_ == header_check ? GridAction::header_focus : GridAction::focus,
            kind_ == row || kind_ == cell ? std::optional{key_} : std::nullopt, column_,
            static_cast<double>(kind_ == header_filter ? GridHeaderPart::filter : kind_ == header_check ? GridHeaderPart::check : GridHeaderPart::sort)}); });
    }
    HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot** value) override {
        if (!value) return E_POINTER;
        *value = nullptr; return with([&](const auto&) { *value = root_ ? root_ : this; (*value)->AddRef(); return S_OK; });
    }
    HRESULT STDMETHODCALLTYPE ElementProviderFromPoint(double x, double y, IRawElementProviderFragment** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return with([&](const auto& s) {
            UiaRect rect{}; bounds(s, rect);
            if (x < rect.left || x >= rect.left + rect.width || y < rect.top || y >= rect.top + rect.height) return S_OK;
            RECT window{}; GetWindowRect(s.window, &window);
            const double scale = GetDpiForWindow(s.window) / 96.0;
            x = (x - window.left) / scale; y = (y - window.top) / scale;
            const auto& g = s.grid_geometry;
            const auto view = g.viewport();
            if (x < view.x || x >= view.x + view.width || y < g.top || y >= view.y + view.height) {
                *value = this; AddRef(); return S_OK;
            }
            const double local_x = x;
            x += s.grid_x - g.left;
            std::size_t c{};
            while (c < s.columns.size() && x >= s.columns[c].width) x -= s.columns[c++].width;
            if (c < s.columns.size()) {
                if (y < g.header_bottom()) {
                    const auto check = g.header_part(s.columns, c, GridHeaderPart::check);
                    const auto filter = g.header_part(s.columns, c, GridHeaderPart::filter);
                    *value = make(check.width && local_x >= check.x && local_x < check.x + check.width ? header_check :
                        filter.width && local_x >= filter.x && local_x < filter.x + filter.width ? header_filter : header, {}, s.column_order[c]);
                }
                else {
                    const auto index = (y - g.header_bottom() + s.grid_y) / g.row_height;
                    if (s.grid && index >= 0 && index < double(s.grid->size())) *value = make(cell, s.grid->key(static_cast<std::size_t>(index)), s.column_order[c]);
                }
            }
            if (!*value) { *value = this; AddRef(); }
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE GetFocus(IRawElementProviderFragment** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return with([&](const auto& s) {
            if (s.focused) {
                if (s.header_focus && s.header_column < s.columns.size()) *value = make(
                    s.header_part == GridHeaderPart::filter ? header_filter : s.header_part == GridHeaderPart::check ? header_check : header, {}, s.header_column);
                else if (s.grid && s.selected_row && s.grid->find(*s.selected_row)) *value = make(row, *s.selected_row);
                else { *value = this; AddRef(); }
            }
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE GetItem(int r, int c, IRawElementProviderSimple** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        return with([&](const auto& s) -> HRESULT {
            if (r < 0 || c < 0 || !s.grid || static_cast<std::size_t>(r) >= s.grid->size() || static_cast<std::size_t>(c) >= s.columns.size()) return E_INVALIDARG;
            *value = make(cell, s.grid->key(r), s.column_order[c]); return S_OK;
        });
    }
    HRESULT integer(int* value, int property) {
        if (!value) return E_POINTER;
        *value = 0;
        return with([&](const auto& s) {
            *value = property == 0 ? static_cast<int>(std::min(s.grid ? s.grid->size() : 0, static_cast<std::size_t>(INT_MAX))) :
                property == 1 ? static_cast<int>(s.columns.size()) : property == 2 ? static_cast<int>(*s.grid->find(key_)) :
                property == 3 ? static_cast<int>(ordinal(s)) : 1;
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE get_RowCount(int* value) override { return integer(value, 0); }
    HRESULT STDMETHODCALLTYPE get_ColumnCount(int* value) override { return integer(value, 1); }
    HRESULT STDMETHODCALLTYPE get_Row(int* value) override { return integer(value, 2); }
    HRESULT STDMETHODCALLTYPE get_Column(int* value) override { return integer(value, 3); }
    HRESULT STDMETHODCALLTYPE get_RowSpan(int* value) override { return integer(value, 4); }
    HRESULT STDMETHODCALLTYPE get_ColumnSpan(int* value) override { return integer(value, 4); }
    HRESULT STDMETHODCALLTYPE get_ContainingGrid(IRawElementProviderSimple** value) override { return container(value); }
    HRESULT STDMETHODCALLTYPE get_SelectionContainer(IRawElementProviderSimple** value) override { return container(value); }
    HRESULT container(IRawElementProviderSimple** value) {
        if (!value) return E_POINTER;
        *value = nullptr; return with([&](const auto&) { *value = root_ ? root_ : this; (*value)->AddRef(); return S_OK; });
    }
    HRESULT array(SAFEARRAY** value, int property) {
        if (!value) return E_POINTER;
        *value = nullptr;
        return with([&](const auto& s) -> HRESULT {
            const auto selected = property == 3 ? s.selection.selected_keys(s.grid) : std::optional{std::vector<ItemKey>{}};
            if (!selected) return UIA_E_INVALIDOPERATION;
            const ULONG count = property == 0 ? 0 : property == 1 ? static_cast<ULONG>(s.columns.size()) : property == 2 ? 1 : static_cast<ULONG>(selected->size());
            *value = SafeArrayCreateVector(VT_UNKNOWN, 0, count);
            if (!*value) return E_OUTOFMEMORY;
            for (LONG i = 0; i < static_cast<LONG>(count); ++i) {
                auto* item = property == 3 ? make(row, (*selected)[i]) : make(header, {}, property == 2 ? column_ : s.column_order[i]);
                const auto result = SafeArrayPutElement(*value, &i, static_cast<IRawElementProviderSimple*>(item));
                item->Release();
                if (FAILED(result)) { SafeArrayDestroy(*value); *value = nullptr; return result; }
            }
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE GetRowHeaders(SAFEARRAY** value) override { return array(value, 0); }
    HRESULT STDMETHODCALLTYPE GetColumnHeaders(SAFEARRAY** value) override { return array(value, 1); }
    HRESULT STDMETHODCALLTYPE GetRowHeaderItems(SAFEARRAY** value) override { return array(value, 0); }
    HRESULT STDMETHODCALLTYPE GetColumnHeaderItems(SAFEARRAY** value) override { return array(value, 2); }
    HRESULT STDMETHODCALLTYPE GetSelection(SAFEARRAY** value) override { return array(value, 3); }
    HRESULT STDMETHODCALLTYPE get_RowOrColumnMajor(RowOrColumnMajor* value) override {
        if (!value) return E_POINTER;
        *value = RowOrColumnMajor_RowMajor; return with([](const auto&) { return S_OK; });
    }
    HRESULT boolean(BOOL* value, int property) {
        if (!value) return E_POINTER;
        *value = FALSE; return with([&](const auto& s) {
            *value = property == 0 ? TRUE : property == 2 ? s.selection.contains(key_) : property == 3 ? maximum(s, true) > 0 :
                property == 4 ? maximum(s, false) > 0 : FALSE;
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE get_CanSelectMultiple(BOOL* value) override { return boolean(value, 0); }
    HRESULT STDMETHODCALLTYPE get_IsSelectionRequired(BOOL* value) override { return boolean(value, 1); }
    HRESULT STDMETHODCALLTYPE get_IsSelected(BOOL* value) override { return boolean(value, 2); }
    HRESULT STDMETHODCALLTYPE get_HorizontallyScrollable(BOOL* value) override { return boolean(value, 3); }
    HRESULT STDMETHODCALLTYPE get_VerticallyScrollable(BOOL* value) override { return boolean(value, 4); }
    HRESULT STDMETHODCALLTYPE Select() override { return with([&](const auto& s) { return send(s, {GridAction::select, key_}); }); }
    HRESULT STDMETHODCALLTYPE AddToSelection() override { return with([&](const auto& s) { return send(s, {GridAction::add, key_}); }); }
    HRESULT STDMETHODCALLTYPE RemoveFromSelection() override { return with([&](const auto& s) { return send(s, {GridAction::clear, key_}); }); }
    HRESULT STDMETHODCALLTYPE ScrollIntoView() override { return with([&](const auto& s) { return send(s, {GridAction::reveal, key_}); }); }
    HRESULT STDMETHODCALLTYPE Invoke() override {
        return with([&](const auto& s) { return send(s, {kind_ == header_filter ? GridAction::filter_open : kind_ == header ? GridAction::sort : GridAction::invoke,
            kind_ == header || kind_ == header_filter ? std::nullopt : std::optional{key_}, column_}); });
    }
    HRESULT STDMETHODCALLTYPE Toggle() override {
        return with([&](const auto& s) -> HRESULT {
            if (!s.columns[ordinal(s)].checkable) return UIA_E_INVALIDOPERATION;
            return send(s, {GridAction::check, kind_ == cell ? std::optional{key_} : std::nullopt, column_});
        });
    }
    HRESULT STDMETHODCALLTYPE get_ToggleState(ToggleState* value) override {
        if (!value) return E_POINTER; *value = ToggleState_Off;
        return with([&](const auto& s) {
            const auto state = kind_ == cell ? (s.selection.contains(key_) ? SelectionState::all : SelectionState::none) : s.selection.state(s.grid, s.full_source);
            *value = state == SelectionState::all ? ToggleState_On : state == SelectionState::mixed ? ToggleState_Indeterminate : ToggleState_Off;
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE SetValue(LPCWSTR value) override {
        if (!value || wcsnlen(value, 4097) > 4096) return E_INVALIDARG;
        return with([&](const auto& s) { GridAction action{GridAction::filter, {}, column_}; action.text = value; return send(s, std::move(action)); });
    }
    HRESULT STDMETHODCALLTYPE get_Value(BSTR* value) override {
        if (!value) return E_POINTER; *value = nullptr;
        return with([&](const auto& s) -> HRESULT { *value = SysAllocString(s.grid_filters[column_].c_str()); return *value ? S_OK : E_OUTOFMEMORY; });
    }
    HRESULT STDMETHODCALLTYPE get_IsReadOnly(BOOL* value) override { if (!value) return E_POINTER; *value = FALSE; return S_OK; }
    static double maximum(const ControlSnapshot& s, bool horizontal) {
        double extent{};
        if (horizontal) { for (const auto& c : s.columns) extent += c.width; }
        else if (s.grid) extent = static_cast<double>(s.grid->size()) * s.grid_geometry.row_height;
        return std::max(0.0, extent - (horizontal ? s.grid_width : s.grid_height));
    }
    HRESULT number(double* value, bool horizontal, bool percent) {
        if (!value) return E_POINTER;
        *value = 0;
        return with([&](const auto& s) {
            const auto max = maximum(s, horizontal), size = horizontal ? s.grid_width : s.grid_height;
            *value = percent ? (max > 0 ? (horizontal ? s.grid_x : s.grid_y) * 100 / max : UIA_ScrollPatternNoScroll) :
                max > 0 ? size * 100 / (max + size) : 100;
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE get_HorizontalScrollPercent(double* value) override { return number(value, true, true); }
    HRESULT STDMETHODCALLTYPE get_VerticalScrollPercent(double* value) override { return number(value, false, true); }
    HRESULT STDMETHODCALLTYPE get_HorizontalViewSize(double* value) override { return number(value, true, false); }
    HRESULT STDMETHODCALLTYPE get_VerticalViewSize(double* value) override { return number(value, false, false); }
    HRESULT STDMETHODCALLTYPE SetScrollPercent(double horizontal, double vertical) override {
        return with([&](const auto& s) -> HRESULT {
            for (auto v : {horizontal, vertical}) if (v != UIA_ScrollPatternNoScroll && (!std::isfinite(v) || v < 0 || v > 100)) return E_INVALIDARG;
            return send(s, {GridAction::scroll, {}, 0, horizontal == UIA_ScrollPatternNoScroll ? s.grid_x : maximum(s, true) * horizontal / 100,
                vertical == UIA_ScrollPatternNoScroll ? s.grid_y : maximum(s, false) * vertical / 100});
        });
    }
    HRESULT STDMETHODCALLTYPE Scroll(ScrollAmount horizontal, ScrollAmount vertical) override {
        return with([&](const auto& s) -> HRESULT {
            if (horizontal < ScrollAmount_LargeDecrement || horizontal > ScrollAmount_SmallIncrement ||
                vertical < ScrollAmount_LargeDecrement || vertical > ScrollAmount_SmallIncrement) return E_INVALIDARG;
            const auto delta = [](ScrollAmount amount, double viewport, double step) {
                switch (amount) {
                case ScrollAmount_LargeDecrement: return -viewport;
                case ScrollAmount_SmallDecrement: return -step;
                case ScrollAmount_LargeIncrement: return viewport;
                case ScrollAmount_SmallIncrement: return step;
                default: return 0.0;
                }
            };
            return send(s, {GridAction::scroll, {}, 0, s.grid_x + delta(horizontal, s.grid_width, 32),
                s.grid_y + delta(vertical, s.grid_height, s.grid_geometry.row_height)});
        });
    }
};
}
IRawElementProviderSimple* create_grid_provider(std::shared_ptr<ControlAccessibility> state) {
    return new GridProvider(std::move(state));
}
void raise_grid_changes(IRawElementProviderSimple* provider, const ControlSnapshot& before, const ControlSnapshot& after) {
    static_cast<GridProvider*>(provider)->changes(before, after);
}
}
