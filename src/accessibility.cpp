#include "xui/accessibility.hpp"

#include <UIAutomation.h>
#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <limits>
#include <new>
#include <system_error>

namespace xui {
namespace {

template<class F>
HRESULT guarded(F&& function) noexcept {
    try {
        return function();
    } catch (const std::bad_alloc&) {
        return E_OUTOFMEMORY;
    } catch (const std::system_error&) {
        return E_FAIL;
    }
}

struct View {
    HWND window{};
    AccessibleSnapshot snapshot;

    size_t count() const noexcept {
        return snapshot.view ? snapshot.view->indices().size() : 0;
    }

    const FileItem* item(size_t index) const noexcept {
        if (index >= count()) return nullptr;
        return &(*snapshot.view->source()->items())[snapshot.view->indices()[index]];
    }

    std::optional<size_t> find(ItemId id) const noexcept {
        return snapshot.view ? snapshot.view->find(id) : std::nullopt;
    }

    double height() const noexcept {
        return std::max(0.0, static_cast<double>(snapshot.screen_bounds.bottom) -
            snapshot.screen_bounds.top - snapshot.row_top_inset_pixels - snapshot.row_bottom_inset_pixels);
    }

    double row_height() const noexcept {
        const double height = snapshot.row_height_pixels;
        return std::isfinite(height) && height > 0 ? height : 0;
    }

    double extent() const noexcept {
        return std::max(0.0, static_cast<double>(count()) * row_height() - height());
    }

    double offset() const noexcept {
        const double offset = snapshot.offset_pixels;
        return std::isfinite(offset) ? std::clamp(offset, 0.0, extent()) : 0;
    }

    bool scrollable() const noexcept {
        return height() > 0 && row_height() > 0 && extent() > 0;
    }

    UiaRect bounds(std::optional<size_t> index = {}) const noexcept {
        const RECT& rect = snapshot.screen_bounds;
        double width = static_cast<double>(rect.right) - rect.left;
        if (index) width = std::max(0.0, width - snapshot.row_right_inset_pixels - snapshot.row_left_inset_pixels);
        if (!window || !IsWindowVisible(window) || width <= 0 || (index && height() <= 0))
            return {};
        double top = rect.top;
        double bottom = rect.bottom;
        if (index) {
            if (row_height() <= 0) return {};
            top += snapshot.row_top_inset_pixels; bottom -= snapshot.row_bottom_inset_pixels;
            const double row_top = rect.top + snapshot.row_top_inset_pixels + static_cast<double>(*index) * row_height() - offset();
            top = std::max(top, row_top);
            bottom = std::min(bottom, row_top + row_height());
        }
        double left = rect.left + (index ? snapshot.row_left_inset_pixels : 0), right = left + width;
        // Keep the list's own viewport for row geometry and scroll percentages.
        // Ancestor clipping changes only the exposed bounds, not virtual row indices.
        for (auto parent = GetParent(window); parent; parent = GetParent(parent)) {
            RECT clip{};
            GetClientRect(parent, &clip);
            MapWindowPoints(parent, nullptr, reinterpret_cast<POINT*>(&clip), 2);
            left = std::max(left, static_cast<double>(clip.left));
            right = std::min(right, static_cast<double>(clip.right));
            top = std::max(top, static_cast<double>(clip.top));
            bottom = std::min(bottom, static_cast<double>(clip.bottom));
        }
        if (bottom <= top || right <= left) return {};
        return {left, top, right - left, bottom - top};
    }
};

View read_view(const std::shared_ptr<AccessibilityState>& state) {
    std::lock_guard lock(state->mutex);
    return { state->window, state->snapshot };
}

HRESULT string_value(const wchar_t* text, size_t length, VARIANT* result) noexcept {
    if (length > std::numeric_limits<UINT>::max()) return E_OUTOFMEMORY;
    BSTR string = SysAllocStringLen(text, static_cast<UINT>(length));
    if (!string) return E_OUTOFMEMORY;
    result->vt = VT_BSTR;
    result->bstrVal = string;
    return S_OK;
}

void bool_value(bool value, VARIANT* result) noexcept {
    result->vt = VT_BOOL;
    result->boolVal = value ? VARIANT_TRUE : VARIANT_FALSE;
}

HRESULT action(const View& view, AccessibilityAction operation, LPARAM argument) noexcept {
    if (!view.window) return UIA_E_ELEMENTNOTAVAILABLE;
    DWORD_PTR result{};
    SetLastError(ERROR_SUCCESS);
    // The payload is a value, never a pointer: a timed-out message cannot outlive
    // caller-owned storage. Do not use SMTO_BLOCK; UIA can reenter during events.
    if (!SendMessageTimeoutW(view.window, accessibility_action_message,
            (static_cast<WPARAM>(view.snapshot.control_id) << 8) | static_cast<WPARAM>(operation),
            argument, SMTO_ABORTIFHUNG | SMTO_ERRORONEXIT,
            1000, &result)) {
        const DWORD error = GetLastError();
        return HRESULT_FROM_WIN32(error ? error : ERROR_TIMEOUT);
    }
    if (static_cast<LRESULT>(result) == -1) return UIA_E_INVALIDOPERATION;
    if (static_cast<LRESULT>(result) == -2) return UIA_E_ELEMENTNOTENABLED;
    return result ? S_OK : UIA_E_ELEMENTNOTAVAILABLE;
}

class Provider final : public IRawElementProviderSimple,
                       public IRawElementProviderFragment,
                       public IRawElementProviderFragmentRoot,
                       public ISelectionProvider,
                       public IScrollProvider,
                       public ISelectionItemProvider,
                       public IScrollItemProvider {
public:
    Provider(std::shared_ptr<AccessibilityState> state, HWND window) noexcept
        : state_(std::move(state)), window_(window) {}

    Provider(Provider* root, ItemId id) noexcept
        : state_(root->state_), window_(root->window_), root_(root), id_(id) {
        root_->AddRef();
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IRawElementProviderSimple))
            *result = static_cast<IRawElementProviderSimple*>(this);
        else if (iid == __uuidof(IRawElementProviderFragment))
            *result = static_cast<IRawElementProviderFragment*>(this);
        else if (!root_ && iid == __uuidof(IRawElementProviderFragmentRoot))
            *result = static_cast<IRawElementProviderFragmentRoot*>(this);
        else if (!root_ && iid == __uuidof(ISelectionProvider))
            *result = static_cast<ISelectionProvider*>(this);
        else if (!root_ && iid == __uuidof(IScrollProvider))
            *result = static_cast<IScrollProvider*>(this);
        else if (root_ && iid == __uuidof(ISelectionItemProvider))
            *result = static_cast<ISelectionItemProvider*>(this);
        else if (root_ && iid == __uuidof(IScrollItemProvider))
            *result = static_cast<IScrollItemProvider*>(this);
        else
            return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return references_.fetch_add(1, std::memory_order_relaxed) + 1;
    }

    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG remaining = references_.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (!remaining) delete this;
        return remaining;
    }

    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* result) override {
        if (!result) return E_POINTER;
        *result = static_cast<ProviderOptions>(
            ProviderOptions_ServerSideProvider | ProviderOptions_ProviderOwnsSetFocus | ProviderOptions_UseComThreading);
        return available();
    }

    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID pattern, IUnknown** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        return guarded([&]() -> HRESULT {
            View view;
            const HRESULT status = current(view);
            if (FAILED(status)) return status;
            if (!root_ && pattern == UIA_SelectionPatternId)
                *result = static_cast<ISelectionProvider*>(this);
            else if (!root_ && pattern == UIA_ScrollPatternId)
                *result = static_cast<IScrollProvider*>(this);
            else if (root_ && pattern == UIA_SelectionItemPatternId)
                *result = static_cast<ISelectionItemProvider*>(this);
            else if (root_ && pattern == UIA_ScrollItemPatternId)
                *result = static_cast<IScrollItemProvider*>(this);
            if (*result) AddRef();
            return S_OK;
        });
    }

    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID property, VARIANT* result) override {
        if (!result) return E_POINTER;
        VariantInit(result);
        return guarded([&]() -> HRESULT {
            View view;
            std::optional<size_t> index;
            const HRESULT status = current(view, &index);
            if (FAILED(status)) return status;
            switch (property) {
            case UIA_ControlTypePropertyId:
                result->vt = VT_I4;
                result->lVal = root_ ? UIA_ListItemControlTypeId : UIA_ListControlTypeId;
                break;
            case UIA_NamePropertyId:
                if (index) {
                    const auto& name = view.item(*index)->name;
                    return string_value(name.data(), name.size(), result);
                }
                return view.snapshot.name ? string_value(view.snapshot.name->data(), view.snapshot.name->size(), result) :
                    string_value(L"Files", 5, result);
            case UIA_AutomationIdPropertyId:
                if (!root_) return view.snapshot.automation_id ?
                    string_value(view.snapshot.automation_id->data(), view.snapshot.automation_id->size(), result) :
                    string_value(L"XuiFileList", 11, result);
                break;
            case UIA_HelpTextPropertyId:
                if (!root_ && view.snapshot.help_text)
                    return string_value(view.snapshot.help_text->data(), view.snapshot.help_text->size(), result);
                break;
            case UIA_ItemTypePropertyId:
                if (index) {
                    const bool folder = view.item(*index)->directory;
                    return string_value(folder ? L"Folder" : L"File", folder ? 6 : 4, result);
                }
                break;
            case UIA_IsControlElementPropertyId:
            case UIA_IsContentElementPropertyId:
            case UIA_IsKeyboardFocusablePropertyId:
                bool_value(true, result);
                break;
            case UIA_IsEnabledPropertyId:
                bool_value(IsWindowEnabled(view.window) != FALSE, result);
                break;
            case UIA_HasKeyboardFocusPropertyId:
                bool_value(view.snapshot.focused && (root_
                    ? view.snapshot.focused_item == id_
                    : !view.snapshot.focused_item || !view.find(*view.snapshot.focused_item)), result);
                break;
            case UIA_IsOffscreenPropertyId: {
                const auto rect = view.bounds(index);
                bool_value(rect.width <= 0 || rect.height <= 0, result);
                break;
            }
            case UIA_SelectionItemIsSelectedPropertyId:
                if (root_) bool_value(view.snapshot.selected == id_, result);
                break;
            case UIA_IsSelectionPatternAvailablePropertyId:
            case UIA_IsScrollPatternAvailablePropertyId:
                bool_value(!root_, result);
                break;
            case UIA_IsSelectionItemPatternAvailablePropertyId:
            case UIA_IsScrollItemPatternAvailablePropertyId:
                bool_value(root_ != nullptr, result);
                break;
            }
            return S_OK;
        });
    }

    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        return guarded([&]() -> HRESULT {
            View view;
            const HRESULT status = current(view);
            if (FAILED(status)) return status;
            return root_ ? S_OK : UiaHostProviderFromHwnd(view.window, result);
        });
    }

    HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection direction,
            IRawElementProviderFragment** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        return guarded([&]() -> HRESULT {
            View view;
            std::optional<size_t> index;
            const HRESULT status = current(view, &index);
            if (FAILED(status)) return status;
            if (root_) {
                if (direction == NavigateDirection_Parent) {
                    *result = static_cast<IRawElementProviderFragment*>(root_);
                    root_->AddRef();
                } else if (direction == NavigateDirection_NextSibling && *index + 1 < view.count()) {
                    return child(view, *index + 1, result);
                } else if (direction == NavigateDirection_PreviousSibling && *index > 0) {
                    return child(view, *index - 1, result);
                }
            } else if (view.count()) {
                if (direction == NavigateDirection_FirstChild) return child(view, 0, result);
                if (direction == NavigateDirection_LastChild) return child(view, view.count() - 1, result);
            }
            return direction >= NavigateDirection_Parent && direction <= NavigateDirection_LastChild
                ? S_OK : E_INVALIDARG;
        });
    }

    HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        return guarded([&]() -> HRESULT {
            View view;
            const HRESULT status = current(view);
            if (FAILED(status)) return status;
            const auto window = static_cast<std::uint64_t>(reinterpret_cast<UINT_PTR>(window_));
            int identity[] = { UiaAppendRuntimeId,
                std::bit_cast<int>(static_cast<std::uint32_t>(window)),
                std::bit_cast<int>(static_cast<std::uint32_t>(window >> 32)),
                root_ ? 1 : 0,
                std::bit_cast<int>(static_cast<std::uint32_t>(id_)),
                std::bit_cast<int>(static_cast<std::uint32_t>(id_ >> 32)) };
            SAFEARRAY* array = SafeArrayCreateVector(VT_I4, 0, 6);
            if (!array) return E_OUTOFMEMORY;
            for (LONG index = 0; index < 6; ++index) {
                const HRESULT put = SafeArrayPutElement(array, &index, &identity[index]);
                if (FAILED(put)) {
                    SafeArrayDestroy(array);
                    return put;
                }
            }
            *result = array;
            return S_OK;
        });
    }

    HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* result) override {
        if (!result) return E_POINTER;
        *result = {};
        return guarded([&]() -> HRESULT {
            View view;
            std::optional<size_t> index;
            const HRESULT status = current(view, &index);
            if (FAILED(status)) return status;
            *result = view.bounds(index);
            return S_OK;
        });
    }

    HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        return available();
    }

    HRESULT STDMETHODCALLTYPE SetFocus() override {
        return perform(root_ ? AccessibilityAction::focus_item : AccessibilityAction::focus_list);
    }

    HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        const HRESULT status = available();
        if (FAILED(status)) return status;
        Provider* root = root_ ? root_ : this;
        *result = static_cast<IRawElementProviderFragmentRoot*>(root);
        root->AddRef();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE ElementProviderFromPoint(double x, double y,
            IRawElementProviderFragment** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        if (!std::isfinite(x) || !std::isfinite(y)) return E_INVALIDARG;
        return guarded([&]() -> HRESULT {
            View view;
            const HRESULT status = current(view);
            if (FAILED(status)) return status;
            const auto bounds = view.bounds();
            if (x < bounds.left || x >= bounds.left + bounds.width ||
                y < bounds.top || y >= bounds.top + bounds.height) return S_OK;
            if (view.row_height() > 0 &&
                x >= view.snapshot.screen_bounds.left + view.snapshot.row_left_inset_pixels &&
                x < view.snapshot.screen_bounds.right - view.snapshot.row_right_inset_pixels &&
                y >= view.snapshot.screen_bounds.top + view.snapshot.row_top_inset_pixels &&
                y < view.snapshot.screen_bounds.bottom - view.snapshot.row_bottom_inset_pixels) {
                const double index = std::floor((y - view.snapshot.screen_bounds.top - view.snapshot.row_top_inset_pixels + view.offset()) / view.row_height());
                if (index >= 0 && index < static_cast<double>(view.count()))
                    return child(view, static_cast<size_t>(index), result);
            }
            *result = static_cast<IRawElementProviderFragment*>(this);
            AddRef();
            return S_OK;
        });
    }

    HRESULT STDMETHODCALLTYPE GetFocus(IRawElementProviderFragment** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        return guarded([&]() -> HRESULT {
            View view;
            const HRESULT status = current(view);
            if (FAILED(status)) return status;
            if (!view.snapshot.focused) return S_OK;
            if (view.snapshot.focused_item) {
                const auto index = view.find(*view.snapshot.focused_item);
                if (index) return child(view, *index, result);
            }
            *result = static_cast<IRawElementProviderFragment*>(this);
            AddRef();
            return S_OK;
        });
    }

    HRESULT STDMETHODCALLTYPE GetSelection(SAFEARRAY** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        return guarded([&]() -> HRESULT {
            View view;
            const HRESULT status = current(view);
            if (FAILED(status)) return status;
            const auto selected = view.snapshot.selected ? view.find(*view.snapshot.selected) : std::nullopt;
            SAFEARRAY* array = SafeArrayCreateVector(VT_UNKNOWN, 0, selected ? 1 : 0);
            if (!array) return E_OUTOFMEMORY;
            if (selected) {
                IRawElementProviderFragment* provider{};
                HRESULT item_status = child(view, *selected, &provider);
                if (SUCCEEDED(item_status)) {
                    LONG index = 0;
                    item_status = SafeArrayPutElement(array, &index, provider);
                    provider->Release();
                }
                if (FAILED(item_status)) {
                    SafeArrayDestroy(array);
                    return item_status;
                }
            }
            *result = array;
            return S_OK;
        });
    }

    HRESULT STDMETHODCALLTYPE get_CanSelectMultiple(BOOL* result) override {
        if (!result) return E_POINTER;
        *result = FALSE;
        return available();
    }

    HRESULT STDMETHODCALLTYPE get_IsSelectionRequired(BOOL* result) override {
        if (!result) return E_POINTER;
        *result = FALSE;
        return available();
    }

    HRESULT STDMETHODCALLTYPE Select() override { return perform(AccessibilityAction::select); }

    HRESULT STDMETHODCALLTYPE AddToSelection() override {
        return guarded([&]() -> HRESULT {
            View view;
            const HRESULT status = current(view);
            if (FAILED(status)) return status;
            if (!IsWindowEnabled(view.window)) return UIA_E_ELEMENTNOTENABLED;
            if (view.snapshot.selected && view.snapshot.selected != id_ &&
                view.find(*view.snapshot.selected)) return UIA_E_INVALIDOPERATION;
            return action(view, AccessibilityAction::add_selection, item_argument());
        });
    }

    HRESULT STDMETHODCALLTYPE RemoveFromSelection() override {
        return guarded([&]() -> HRESULT {
            View view;
            const HRESULT status = current(view);
            if (FAILED(status)) return status;
            if (!IsWindowEnabled(view.window)) return UIA_E_ELEMENTNOTENABLED;
            return action(view, AccessibilityAction::remove_selection, item_argument());
        });
    }

    HRESULT STDMETHODCALLTYPE get_IsSelected(BOOL* result) override {
        if (!result) return E_POINTER;
        *result = FALSE;
        return guarded([&]() -> HRESULT {
            View view;
            const HRESULT status = current(view);
            if (FAILED(status)) return status;
            *result = view.snapshot.selected == id_;
            return S_OK;
        });
    }

    HRESULT STDMETHODCALLTYPE get_SelectionContainer(IRawElementProviderSimple** result) override {
        if (!result) return E_POINTER;
        *result = nullptr;
        const HRESULT status = available();
        if (FAILED(status)) return status;
        if (!root_) return UIA_E_NOTSUPPORTED;
        *result = static_cast<IRawElementProviderSimple*>(root_);
        root_->AddRef();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE ScrollIntoView() override { return perform(AccessibilityAction::reveal); }

    HRESULT STDMETHODCALLTYPE Scroll(ScrollAmount horizontal, ScrollAmount vertical) override {
        return guarded([&]() -> HRESULT {
            View view;
            const HRESULT status = current(view);
            if (FAILED(status)) return status;
            if (horizontal < ScrollAmount_LargeDecrement || horizontal > ScrollAmount_SmallIncrement ||
                vertical < ScrollAmount_LargeDecrement || vertical > ScrollAmount_SmallIncrement)
                return E_INVALIDARG;
            if (horizontal != ScrollAmount_NoAmount) return UIA_E_INVALIDOPERATION;
            if (vertical == ScrollAmount_NoAmount) return S_OK;
            if (!view.scrollable()) return UIA_E_INVALIDOPERATION;
            double delta{};
            switch (vertical) {
            case ScrollAmount_LargeDecrement: delta = -view.height(); break;
            case ScrollAmount_SmallDecrement: delta = -view.row_height(); break;
            case ScrollAmount_LargeIncrement: delta = view.height(); break;
            case ScrollAmount_SmallIncrement: delta = view.row_height(); break;
            default: return E_INVALIDARG;
            }
            const double offset = std::clamp(view.offset() + delta, 0.0, view.extent());
            return scroll_to(view, offset * 100 / view.extent());
        });
    }

    HRESULT STDMETHODCALLTYPE SetScrollPercent(double horizontal, double vertical) override {
        return guarded([&]() -> HRESULT {
            View view;
            const HRESULT status = current(view);
            if (FAILED(status)) return status;
            const auto valid = [](double percent) {
                return std::isfinite(percent) &&
                    (percent == UIA_ScrollPatternNoScroll || (percent >= 0 && percent <= 100));
            };
            if (!valid(horizontal) || !valid(vertical)) return E_INVALIDARG;
            if (horizontal != UIA_ScrollPatternNoScroll) return UIA_E_INVALIDOPERATION;
            if (vertical == UIA_ScrollPatternNoScroll) return S_OK;
            if (!view.scrollable()) return UIA_E_INVALIDOPERATION;
            return scroll_to(view, vertical);
        });
    }

    HRESULT STDMETHODCALLTYPE get_HorizontalScrollPercent(double* result) override {
        if (!result) return E_POINTER;
        *result = UIA_ScrollPatternNoScroll;
        return available();
    }

    HRESULT STDMETHODCALLTYPE get_VerticalScrollPercent(double* result) override {
        if (!result) return E_POINTER;
        *result = UIA_ScrollPatternNoScroll;
        return guarded([&]() -> HRESULT {
            View view;
            const HRESULT status = current(view);
            if (FAILED(status)) return status;
            if (view.scrollable()) *result = view.offset() * 100 / view.extent();
            return S_OK;
        });
    }

    HRESULT STDMETHODCALLTYPE get_HorizontalViewSize(double* result) override {
        if (!result) return E_POINTER;
        *result = 100;
        return available();
    }

    HRESULT STDMETHODCALLTYPE get_VerticalViewSize(double* result) override {
        if (!result) return E_POINTER;
        *result = 100;
        return guarded([&]() -> HRESULT {
            View view;
            const HRESULT status = current(view);
            if (FAILED(status)) return status;
            const double total = static_cast<double>(view.count()) * view.row_height();
            if (total > 0) *result = std::clamp(view.height() * 100 / total, 0.0, 100.0);
            return S_OK;
        });
    }

    HRESULT STDMETHODCALLTYPE get_HorizontallyScrollable(BOOL* result) override {
        if (!result) return E_POINTER;
        *result = FALSE;
        return available();
    }

    HRESULT STDMETHODCALLTYPE get_VerticallyScrollable(BOOL* result) override {
        if (!result) return E_POINTER;
        *result = FALSE;
        return guarded([&]() -> HRESULT {
            View view;
            const HRESULT status = current(view);
            if (FAILED(status)) return status;
            *result = view.scrollable();
            return S_OK;
        });
    }

    HRESULT item_at(const View& view, size_t index, IRawElementProviderSimple** result) noexcept {
        *result = nullptr;
        const auto* item = view.item(index);
        if (!item || view.window != window_) return UIA_E_ELEMENTNOTAVAILABLE;
        auto* provider = new (std::nothrow) Provider(root_ ? root_ : this, item->id);
        if (!provider) return E_OUTOFMEMORY;
        *result = static_cast<IRawElementProviderSimple*>(provider);
        return S_OK;
    }

private:
    ~Provider() {
        if (root_) root_->Release();
    }

    HRESULT current(View& view, std::optional<size_t>* index = nullptr) const {
        view = read_view(state_);
        if (!view.window || view.window != window_) return UIA_E_ELEMENTNOTAVAILABLE;
        if (root_) {
            const auto found = view.find(id_);
            if (!found) return UIA_E_ELEMENTNOTAVAILABLE;
            if (index) *index = found;
        }
        return S_OK;
    }

    HRESULT available() const noexcept {
        return guarded([&]() -> HRESULT {
            View view;
            return current(view);
        });
    }

    HRESULT child(const View& view, size_t index, IRawElementProviderFragment** result) noexcept {
        IRawElementProviderSimple* provider{};
        const HRESULT status = item_at(view, index, &provider);
        if (FAILED(status)) return status;
        const HRESULT query = provider->QueryInterface(IID_PPV_ARGS(result));
        provider->Release();
        return query;
    }

    LPARAM item_argument() const noexcept {
        static_assert(sizeof(LPARAM) == sizeof(ItemId), "UIA item actions require a 64-bit host");
        return std::bit_cast<LPARAM>(id_);
    }

    HRESULT perform(AccessibilityAction operation) noexcept {
        return guarded([&]() -> HRESULT {
            View view;
            const HRESULT status = current(view);
            if (FAILED(status)) return status;
            if (!IsWindowEnabled(view.window)) return UIA_E_ELEMENTNOTENABLED;
            return action(view, operation, root_ ? item_argument() : 0);
        });
    }

    static HRESULT scroll_to(const View& view, double percent) noexcept {
        if (!IsWindowEnabled(view.window)) return UIA_E_ELEMENTNOTENABLED;
        return action(view, AccessibilityAction::scroll_percent,
            static_cast<LPARAM>(std::llround(std::clamp(percent, 0.0, 100.0) * 100)));
    }

    std::atomic<ULONG> references_{1};
    std::shared_ptr<AccessibilityState> state_;
    HWND window_{};
    Provider* root_{};
    ItemId id_{};
};

}

IRawElementProviderSimple* create_list_provider(std::shared_ptr<AccessibilityState> state) {
    if (!state) return nullptr;
    const auto view = read_view(state);
    if (!view.window) return nullptr;
    return new (std::nothrow) Provider(std::move(state), view.window);
}

void raise_list_selection(IRawElementProviderSimple* provider,
        std::shared_ptr<AccessibilityState> state, size_t index) {
    if (!provider || !state || !UiaClientsAreListening()) return;
    const auto view = read_view(state);
    if (!view.window) return;
    const auto* item = view.item(index);
    if (!item || view.snapshot.selected != item->id) return;
    IRawElementProviderSimple* selected{};
    if (SUCCEEDED(static_cast<Provider*>(provider)->item_at(view, index, &selected))) {
        UiaRaiseAutomationEvent(selected, UIA_SelectionItem_ElementSelectedEventId);
        selected->Release();
    }
}

void raise_list_structure(IRawElementProviderSimple* provider) {
    if (!provider || !UiaClientsAreListening()) return;
    UiaRaiseStructureChangedEvent(provider, StructureChangeType_ChildrenInvalidated, nullptr, 0);
}

void raise_list_selection_removed(IRawElementProviderSimple* provider,
        std::shared_ptr<AccessibilityState> state, size_t index) {
    if (!provider || !state || !UiaClientsAreListening()) return;
    const auto view = read_view(state);
    IRawElementProviderSimple* removed{};
    if (SUCCEEDED(static_cast<Provider*>(provider)->item_at(view, index, &removed))) {
        UiaRaiseAutomationEvent(removed, UIA_SelectionItem_ElementRemovedFromSelectionEventId);
        removed->Release();
    }
}

void raise_list_focus(IRawElementProviderSimple* provider,
        std::shared_ptr<AccessibilityState> state, std::optional<size_t> index) {
    if (!provider || !state || !UiaClientsAreListening()) return;
    const auto view = read_view(state);
    if (!view.window || !view.snapshot.focused) return;
    if (index) {
        const auto* item = view.item(*index);
        if (!item || view.snapshot.focused_item != item->id) return;
        IRawElementProviderSimple* focused{};
        if (SUCCEEDED(static_cast<Provider*>(provider)->item_at(view, *index, &focused))) {
            UiaRaiseAutomationEvent(focused, UIA_AutomationFocusChangedEventId);
            focused->Release();
        }
    } else {
        UiaRaiseAutomationEvent(provider, UIA_AutomationFocusChangedEventId);
    }
}

void raise_list_properties(IRawElementProviderSimple* provider,
        const std::shared_ptr<AccessibilityState>& state, const AccessibleSnapshot& previous) {
    if (!provider || !previous.control_id || !UiaClientsAreListening()) return;
    const auto next = read_view(state);
    if (!next.window) return;
    const View before{next.window, previous};
    const auto number = [&](PROPERTYID property, double old_number, double new_number) {
        if (old_number == new_number) return;
        VARIANT old_value{}, new_value{};
        old_value.vt = new_value.vt = VT_R8;
        old_value.dblVal = old_number;
        new_value.dblVal = new_number;
        UiaRaiseAutomationPropertyChangedEvent(provider, property, old_value, new_value);
    };
    const auto boolean = [](IRawElementProviderSimple* element, PROPERTYID property, bool old_bool, bool new_bool) {
        if (old_bool == new_bool) return;
        VARIANT old_value{}, new_value{};
        bool_value(old_bool, &old_value);
        bool_value(new_bool, &new_value);
        UiaRaiseAutomationPropertyChangedEvent(element, property, old_value, new_value);
    };
    const auto percent = [](const View& view) {
        return view.scrollable() ? view.offset() * 100 / view.extent() : UIA_ScrollPatternNoScroll;
    };
    const auto size = [](const View& view) {
        const auto total = view.count() * view.row_height();
        return total > 0 ? std::clamp(view.height() * 100 / total, 0.0, 100.0) : 100.0;
    };
    number(UIA_ScrollVerticalScrollPercentPropertyId, percent(before), percent(next));
    number(UIA_ScrollVerticalViewSizePropertyId, size(before), size(next));
    if (previous.row_height_pixels != next.snapshot.row_height_pixels ||
        previous.row_right_inset_pixels != next.snapshot.row_right_inset_pixels ||
        previous.row_left_inset_pixels != next.snapshot.row_left_inset_pixels ||
        previous.row_top_inset_pixels != next.snapshot.row_top_inset_pixels ||
        previous.row_bottom_inset_pixels != next.snapshot.row_bottom_inset_pixels)
        UiaRaiseAutomationEvent(provider, UIA_LayoutInvalidatedEventId);
    boolean(provider, UIA_ScrollVerticallyScrollablePropertyId, before.scrollable(), next.scrollable());
    boolean(provider, UIA_IsEnabledPropertyId, previous.enabled, next.snapshot.enabled);
    boolean(provider, UIA_HasKeyboardFocusPropertyId, previous.focused && !previous.focused_item,
        next.snapshot.focused && !next.snapshot.focused_item);
    if (previous.name && next.snapshot.name && *previous.name != *next.snapshot.name) {
        VARIANT old_value{}, new_value{};
        if (SUCCEEDED(string_value(previous.name->data(), previous.name->size(), &old_value)) &&
            SUCCEEDED(string_value(next.snapshot.name->data(), next.snapshot.name->size(), &new_value)))
            UiaRaiseAutomationPropertyChangedEvent(provider, UIA_NamePropertyId, old_value, new_value);
        VariantClear(&old_value);
        VariantClear(&new_value);
    }
    const auto item_boolean = [&](std::optional<ItemId> id, PROPERTYID property, bool old_value, bool new_value) {
        if (!id || old_value == new_value) return;
        const auto index = next.find(*id);
        if (!index) return;
        IRawElementProviderSimple* item{};
        if (SUCCEEDED(static_cast<Provider*>(provider)->item_at(next, *index, &item))) {
            boolean(item, property, old_value, new_value);
            item->Release();
        }
    };
    if (previous.selected != next.snapshot.selected) {
        item_boolean(previous.selected, UIA_SelectionItemIsSelectedPropertyId, true, false);
        item_boolean(next.snapshot.selected, UIA_SelectionItemIsSelectedPropertyId, false, true);
    }
    const auto old_focus = previous.focused ? previous.focused_item : std::nullopt;
    const auto new_focus = next.snapshot.focused ? next.snapshot.focused_item : std::nullopt;
    if (old_focus != new_focus) {
        item_boolean(old_focus, UIA_HasKeyboardFocusPropertyId, true, false);
        item_boolean(new_focus, UIA_HasKeyboardFocusPropertyId, false, true);
    }
}

}
