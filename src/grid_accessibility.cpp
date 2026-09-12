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
    public ISelectionItemProvider, public IInvokeProvider, public IScrollProvider, public IScrollItemProvider {
    enum Kind { root, header, row, cell };
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
    void changes(const ControlSnapshot& before, const ControlSnapshot& after) {
        const auto active = [](const ControlSnapshot& s) {
            return s.grid && s.selected_row && s.grid->find(*s.selected_row) ? s.selected_row : std::nullopt;
        };
        const auto previous = active(before), current = active(after);
        if (before.grid != after.grid) UiaRaiseAutomationEvent(this, UIA_LayoutInvalidatedEventId);
        if ((before.grid ? before.grid->size() : 0) != (after.grid ? after.grid->size() : 0))
            UiaRaiseStructureChangedEvent(this, StructureChangeType_ChildrenInvalidated, nullptr, 0);
        if (previous != current) {
            UiaRaiseAutomationEvent(this, UIA_Selection_InvalidatedEventId);
            if (current) {
                auto* item = make(row, *current);
                UiaRaiseAutomationEvent(item, UIA_SelectionItem_ElementSelectedEventId);
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
            if ((kind_ == header || kind_ == cell) && column_ >= s.columns.size()) return UIA_E_ELEMENTNOTAVAILABLE;
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
        return with([&](const auto&) {
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
            if (*value) AddRef();
            return S_OK;
        });
    }
    std::wstring name(const ControlSnapshot& s) const {
        if (kind_ == root) return s.name;
        if (kind_ == header) return s.columns[column_].name +
            (s.sort_column == column_ ? (s.descending ? L", sorted descending" : L", sorted ascending") : L", sort");
        const auto index = *s.grid->find(key_);
        if (kind_ == cell) return s.grid->text(index, column_);
        std::wstring value;
        for (std::size_t c = 0; c < s.columns.size(); ++c) {
            if (c) value += L", ";
            value += s.columns[c].name + L": " + s.grid->text(index, c);
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
                    id == UIA_HelpTextPropertyId ? L"F6 switches between column headers and rows. Enter sorts a header or opens row details." :
                    kind_ == root ? s.automation_id : s.automation_id + L"-" + std::to_wstring(kind_) + L"-" +
                    std::to_wstring(key_.id) + L"-" + std::to_wstring(key_.version) + L"-" + std::to_wstring(column_);
                value->vt = VT_BSTR; value->bstrVal = SysAllocStringLen(text.data(), static_cast<UINT>(text.size()));
                return value->bstrVal ? S_OK : E_OUTOFMEMORY;
            }
            if (id == UIA_ControlTypePropertyId) {
                value->vt = VT_I4; value->lVal = kind_ == root ? UIA_DataGridControlTypeId :
                    kind_ == header ? UIA_HeaderItemControlTypeId : kind_ == row ? UIA_DataItemControlTypeId : UIA_TextControlTypeId;
            } else if (id == UIA_IsControlElementPropertyId || id == UIA_IsContentElementPropertyId ||
                id == UIA_IsKeyboardFocusablePropertyId || id == UIA_IsEnabledPropertyId ||
                id == UIA_HasKeyboardFocusPropertyId || id == UIA_IsOffscreenPropertyId) {
                UiaRect b{}; bounds(s, b);
                const bool result = id == UIA_IsOffscreenPropertyId ? b.width <= 0 || b.height <= 0 :
                    id == UIA_IsEnabledPropertyId ? s.enabled : id == UIA_IsKeyboardFocusablePropertyId ? s.enabled && kind_ != cell :
                    id == UIA_HasKeyboardFocusPropertyId ? s.focused && (kind_ == root ||
                        (kind_ == header ? s.header_focus && s.header_column == column_ : !s.header_focus && kind_ == row && s.selected_row == key_)) : true;
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
            if (direction == NavigateDirection_Parent && kind_ != root) {
                if (kind_ == cell) *value = make(row, key_);
                else { *value = root_; root_->AddRef(); }
            } else if (kind_ == root) {
                if (direction == NavigateDirection_FirstChild && !s.columns.empty()) *value = make(header);
                if (direction == NavigateDirection_LastChild) {
                    if (count) *value = make(row, s.grid->key(count - 1));
                    else if (!s.columns.empty()) *value = make(header, {}, s.columns.size() - 1);
                }
            } else if (kind_ == row && (direction == NavigateDirection_FirstChild || direction == NavigateDirection_LastChild)) {
                if (!s.columns.empty()) *value = make(cell, key_, direction == NavigateDirection_FirstChild ? 0 : s.columns.size() - 1);
            } else if (direction == NavigateDirection_NextSibling || direction == NavigateDirection_PreviousSibling) {
                const bool next = direction == NavigateDirection_NextSibling;
                if (kind_ == header || kind_ == cell) {
                    if (next && column_ + 1 < s.columns.size()) *value = make(kind_, key_, column_ + 1);
                    else if (!next && column_) *value = make(kind_, key_, column_ - 1);
                    else if (next && kind_ == header && count) *value = make(row, s.grid->key(0));
                } else if (kind_ == row) {
                    const auto index = *s.grid->find(key_);
                    if (next && index + 1 < count) *value = make(row, s.grid->key(index + 1));
                    else if (!next && index) *value = make(row, s.grid->key(index - 1));
                    else if (!next && !s.columns.empty()) *value = make(header, {}, s.columns.size() - 1);
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
        double x{}, y{}, width = s.grid_width, height = s.grid_height + DataGrid::header_height + DataGrid::bar_width;
        if (kind_ == header || kind_ == cell) {
            x = -s.grid_x;
            for (std::size_t c = 0; c < column_; ++c) x += s.columns[c].width;
            width = s.columns[column_].width;
        }
        if (kind_ == header) height = DataGrid::header_height;
        if (kind_ == row || kind_ == cell) {
            y = DataGrid::header_height + static_cast<double>(*s.grid->find(key_)) * DataGrid::row_height - s.grid_y;
            height = DataGrid::row_height;
        }
        double left = std::max(0.0, x), right = std::min(s.grid_width, x + width);
        double top = std::max(kind_ == row || kind_ == cell ? double(DataGrid::header_height) : 0.0, y);
        double bottom = std::min(kind_ == row || kind_ == cell ? DataGrid::header_height + s.grid_height : height, y + height);
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
        return with([&](const auto& s) { return send(s, {kind_ == header ? GridAction::header_focus : GridAction::focus,
            kind_ == row || kind_ == cell ? std::optional{key_} : std::nullopt, column_}); });
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
            x = (x - window.left) / scale + s.grid_x; y = (y - window.top) / scale;
            std::size_t c{};
            while (c < s.columns.size() && x >= s.columns[c].width) x -= s.columns[c++].width;
            if (c < s.columns.size()) {
                if (y < DataGrid::header_height) *value = make(header, {}, c);
                else {
                    const auto index = static_cast<std::size_t>((y - DataGrid::header_height + s.grid_y) / DataGrid::row_height);
                    if (s.grid && index < s.grid->size()) *value = make(cell, s.grid->key(index), c);
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
                if (s.header_focus && s.header_column < s.columns.size()) *value = make(header, {}, s.header_column);
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
            *value = make(cell, s.grid->key(r), c); return S_OK;
        });
    }
    HRESULT integer(int* value, int property) {
        if (!value) return E_POINTER;
        *value = 0;
        return with([&](const auto& s) {
            *value = property == 0 ? static_cast<int>(std::min(s.grid ? s.grid->size() : 0, static_cast<std::size_t>(INT_MAX))) :
                property == 1 ? static_cast<int>(s.columns.size()) : property == 2 ? static_cast<int>(*s.grid->find(key_)) :
                property == 3 ? static_cast<int>(column_) : 1;
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
            const bool selected = s.grid && s.selected_row && s.grid->find(*s.selected_row);
            const ULONG count = property == 0 ? 0 : property == 1 ? static_cast<ULONG>(s.columns.size()) : property == 2 ? 1 : selected ? 1 : 0;
            *value = SafeArrayCreateVector(VT_UNKNOWN, 0, count);
            if (!*value) return E_OUTOFMEMORY;
            for (LONG i = 0; i < static_cast<LONG>(count); ++i) {
                auto* item = property == 3 ? make(row, *s.selected_row) : make(header, {}, property == 2 ? column_ : i);
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
            *value = property == 2 ? s.selected_row == key_ : property == 3 ? maximum(s, true) > 0 :
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
    HRESULT STDMETHODCALLTYPE AddToSelection() override { return Select(); }
    HRESULT STDMETHODCALLTYPE RemoveFromSelection() override { return with([&](const auto& s) { return send(s, {GridAction::clear, key_}); }); }
    HRESULT STDMETHODCALLTYPE ScrollIntoView() override { return with([&](const auto& s) { return send(s, {GridAction::reveal, key_}); }); }
    HRESULT STDMETHODCALLTYPE Invoke() override {
        return with([&](const auto& s) { return send(s, {kind_ == header ? GridAction::sort : GridAction::invoke,
            kind_ == header ? std::nullopt : std::optional{key_}, column_}); });
    }
    static double maximum(const ControlSnapshot& s, bool horizontal) {
        double extent{};
        if (horizontal) { for (const auto& c : s.columns) extent += c.width; }
        else if (s.grid) extent = static_cast<double>(s.grid->size()) * DataGrid::row_height;
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
            const auto delta = [](ScrollAmount amount, double viewport) {
                switch (amount) {
                case ScrollAmount_LargeDecrement: return -viewport;
                case ScrollAmount_SmallDecrement: return -32.0;
                case ScrollAmount_LargeIncrement: return viewport;
                case ScrollAmount_SmallIncrement: return 32.0;
                default: return 0.0;
                }
            };
            return send(s, {GridAction::scroll, {}, 0, s.grid_x + delta(horizontal, s.grid_width), s.grid_y + delta(vertical, s.grid_height)});
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
