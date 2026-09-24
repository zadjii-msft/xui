namespace {
template<class F> HRESULT virtual_uia(F&& body) noexcept {
    try { return body(); }
    catch (const std::bad_alloc&) { return E_OUTOFMEMORY; }
    catch (...) { return E_FAIL; }
}
HRESULT virtual_fragment(IRawElementProviderSimple* provider, IRawElementProviderFragment** value) {
    return provider ? provider->QueryInterface(IID_PPV_ARGS(value)) : UIA_E_ELEMENTNOTAVAILABLE;
}
}
std::optional<HRESULT> navigate_fragment(const std::shared_ptr<ControlAccessibility>& state,
    NavigateDirection direction, IRawElementProviderFragment** result) {
    std::shared_ptr<FragmentNavigation> navigation;
    std::uint64_t id{};
    HWND window{};
    {
        std::lock_guard lock(state->mutex);
        navigation = state->fragment_navigation.lock();
        id = state->snapshot.id;
        window = state->snapshot.window;
    }
    if (!navigation) return {};
    const auto status = navigation->navigate(id, window, direction, result);
    return status == S_FALSE ? std::nullopt : std::optional<HRESULT>{status};
}
std::optional<HRESULT> fragment_root(const std::shared_ptr<ControlAccessibility>& state,
    IRawElementProviderFragmentRoot** result) {
    std::shared_ptr<FragmentNavigation> navigation;
    { std::lock_guard lock(state->mutex); navigation = state->fragment_navigation.lock(); }
    return navigation ? std::optional<HRESULT>{navigation->fragment_root(result)} : std::nullopt;
}
class VirtualRowProvider final : public IRawElementProviderSimple, public IRawElementProviderFragment,
    public IRawElementProviderFragmentRoot {
public:
    VirtualRowProvider(std::weak_ptr<VirtualRowsAccessibility> state, std::uint64_t row) : state_(std::move(state)), row_(row) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IRawElementProviderSimple)) *value = static_cast<IRawElementProviderSimple*>(this);
        else if (iid == __uuidof(IRawElementProviderFragment)) *value = static_cast<IRawElementProviderFragment*>(this);
        else if (!row_ && iid == __uuidof(IRawElementProviderFragmentRoot)) *value = static_cast<IRawElementProviderFragmentRoot*>(this);
        else return E_NOINTERFACE;
        AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override { const auto count = --refs_; if (!count) delete this; return count; }
    template<class F> HRESULT with(F&& body) {
        return virtual_uia([&]() -> HRESULT {
            const auto state = state_.lock();
            if (!state) return UIA_E_ELEMENTNOTAVAILABLE;
            std::shared_ptr<const VirtualRowsAccessibility::Snapshot> snapshot;
            { std::lock_guard lock(state->mutex_); snapshot = state->snapshot_; }
            if (!snapshot || !snapshot->window_ || !IsWindow(snapshot->window_)) return UIA_E_ELEMENTNOTAVAILABLE;
            const auto found = std::find_if(snapshot->rows_.begin(), snapshot->rows_.end(), [&](const auto& row) { return row.id == row_; });
            if (row_ && found == snapshot->rows_.end()) return UIA_E_ELEMENTNOTAVAILABLE;
            return body(*snapshot, row_ ? &*found : nullptr);
        });
    }
    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* value) override {
        if (!value) return E_POINTER;
        *value = static_cast<ProviderOptions>(ProviderOptions_ServerSideProvider | ProviderOptions_UseComThreading);
        return with([](auto&, auto*) { return S_OK; });
    }
    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID, IUnknown** value) override {
        if (!value) return E_POINTER; *value = nullptr;
        return with([](auto&, auto*) { return S_OK; });
    }
    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID property, VARIANT* value) override {
        if (!value) return E_POINTER; VariantInit(value);
        return with([&](auto& state, const auto* row) -> HRESULT {
            if (property == UIA_ControlTypePropertyId || property == UIA_PositionInSetPropertyId || property == UIA_SizeOfSetPropertyId) {
                value->vt = VT_I4;
                value->lVal = property == UIA_ControlTypePropertyId ? (row ? UIA_ListItemControlTypeId : UIA_ListControlTypeId) :
                    property == UIA_PositionInSetPropertyId ? (row ? static_cast<LONG>(row->info.index + 1) : 0) :
                    static_cast<LONG>(row ? row->info.count : state.count_);
            } else if (property == UIA_NamePropertyId) {
                const auto name = row ? L"Item " + std::to_wstring(row->info.index + 1) : state.name_;
                value->vt = VT_BSTR; value->bstrVal = SysAllocStringLen(name.data(), static_cast<UINT>(name.size()));
                if (!value->bstrVal) return E_OUTOFMEMORY;
            } else if (property == UIA_IsControlElementPropertyId || property == UIA_IsContentElementPropertyId ||
                property == UIA_IsEnabledPropertyId || property == UIA_IsOffscreenPropertyId || property == UIA_IsKeyboardFocusablePropertyId) {
                value->vt = VT_BOOL;
                const auto root_bounds = clipped_bounds(state.window_);
                const bool offscreen = row ? row->bounds.width <= 0 || row->bounds.height <= 0 : IsRectEmpty(&root_bounds) != FALSE;
                bool enabled = true;
                for (auto window = state.window_; window; window = GetParent(window))
                    if (!IsWindowEnabled(window)) { enabled = false; break; }
                const bool flag = property == UIA_IsOffscreenPropertyId ? offscreen :
                    property == UIA_IsKeyboardFocusablePropertyId ? false :
                    property == UIA_IsEnabledPropertyId ? enabled : true;
                value->boolVal = flag ? VARIANT_TRUE : VARIANT_FALSE;
            }
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** value) override {
        if (!value) return E_POINTER; *value = nullptr;
        return with([&](auto& state, auto*) { return row_ ? S_OK : UiaHostProviderFromHwnd(state.window_, value); });
    }
    HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection direction, IRawElementProviderFragment** value) override {
        if (!value) return E_POINTER; *value = nullptr;
        return with([&](auto& state, const auto* row) -> HRESULT {
            std::uint64_t destination{};
            bool produce{};
            if (row && direction == NavigateDirection_Parent) produce = true;
            else if (!row && !state.rows_.empty() &&
                (direction == NavigateDirection_FirstChild || direction == NavigateDirection_LastChild)) {
                destination = direction == NavigateDirection_FirstChild ? state.rows_.front().id : state.rows_.back().id;
                produce = true;
            } else if (row && (direction == NavigateDirection_NextSibling || direction == NavigateDirection_PreviousSibling)) {
                const auto index = static_cast<std::size_t>(row - state.rows_.data());
                if (direction == NavigateDirection_NextSibling && index + 1 < state.rows_.size()) {
                    destination = state.rows_[index + 1].id; produce = true;
                } else if (direction == NavigateDirection_PreviousSibling && index) {
                    destination = state.rows_[index - 1].id; produce = true;
                }
            } else if (row && !row->children.empty() &&
                (direction == NavigateDirection_FirstChild || direction == NavigateDirection_LastChild)) {
                const auto& child = direction == NavigateDirection_FirstChild ? row->children.front() : row->children.back();
                return virtual_fragment(child.provider.Get(), value);
            }
            if (produce) *value = new VirtualRowProvider(state.weak_from_this(), destination);
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** value) override {
        if (!value) return E_POINTER; *value = nullptr;
        return with([&](auto& state, auto*) -> HRESULT {
            int ids[]{UiaAppendRuntimeId, 0x585652, static_cast<int>(state.identity_ >> 32), static_cast<int>(state.identity_),
                static_cast<int>(row_ >> 32), static_cast<int>(row_)};
            *value = SafeArrayCreateVector(VT_I4, 0, 6);
            if (!*value) return E_OUTOFMEMORY;
            for (LONG i = 0; i < 6; ++i) {
                const auto result = SafeArrayPutElement(*value, &i, &ids[i]);
                if (FAILED(result)) { SafeArrayDestroy(*value); *value = nullptr; return result; }
            }
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* value) override {
        if (!value) return E_POINTER; *value = {};
        return with([&](auto& state, const auto* row) {
            if (row) *value = row->bounds;
            else {
                const auto bounds = clipped_bounds(state.window_);
                *value = {double(bounds.left), double(bounds.top), double(bounds.right - bounds.left), double(bounds.bottom - bounds.top)};
            }
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** value) override {
        if (!value) return E_POINTER; *value = nullptr; return with([](auto&, auto*) { return S_OK; });
    }
    HRESULT STDMETHODCALLTYPE SetFocus() override { return with([](auto&, auto*) { return UIA_E_NOTSUPPORTED; }); }
    HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot** value) override {
        if (!value) return E_POINTER; *value = nullptr;
        return with([&](auto& state, auto*) { *value = new VirtualRowProvider(state.weak_from_this(), 0); return S_OK; });
    }
    HRESULT STDMETHODCALLTYPE ElementProviderFromPoint(double x, double y, IRawElementProviderFragment** value) override {
        if (!value) return E_POINTER; *value = nullptr;
        return with([&](auto& state, auto*) -> HRESULT {
            for (const auto& row : state.rows_) if (row.bounds.width > 0 && row.bounds.height > 0 &&
                x >= row.bounds.left && x < row.bounds.left + row.bounds.width &&
                y >= row.bounds.top && y < row.bounds.top + row.bounds.height) {
                for (const auto& child : row.children) {
                    const auto bounds = clipped_bounds(child.window);
                    if (x >= bounds.left && x < bounds.right && y >= bounds.top && y < bounds.bottom)
                        return virtual_fragment(child.provider.Get(), value);
                }
                *value = new VirtualRowProvider(state.weak_from_this(), row.id); return S_OK;
            }
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE GetFocus(IRawElementProviderFragment** value) override {
        if (!value) return E_POINTER; *value = nullptr;
        return with([&](auto& state, auto*) -> HRESULT {
            GUITHREADINFO info{sizeof(info)};
            if (!GetGUIThreadInfo(GetWindowThreadProcessId(state.window_, nullptr), &info)) return S_OK;
            for (const auto& row : state.rows_) for (const auto& child : row.children)
                if (info.hwndFocus == child.window || IsChild(child.window, info.hwndFocus))
                    return virtual_fragment(child.provider.Get(), value);
            return S_OK;
        });
    }
private:
    std::atomic<ULONG> refs_{1};
    std::weak_ptr<VirtualRowsAccessibility> state_;
    std::uint64_t row_{};
};
VirtualRowsAccessibility::VirtualRowsAccessibility(HWND window, std::uint64_t identity, std::wstring name)
    : window_(window), identity_(identity), name_(std::move(name)) {}
IRawElementProviderSimple* VirtualRowsAccessibility::provider(std::uint64_t row) { return new VirtualRowProvider(weak_from_this(), row); }
bool VirtualRowsAccessibility::publish(std::uint32_t count, std::vector<VirtualRowPresentation> rows) {
    auto next = std::make_shared<Snapshot>();
    next->window_ = window_; next->identity_ = identity_; next->name_ = name_; next->count_ = count;
    next->rows_ = std::move(rows); next->owner_ = weak_from_this();
    std::shared_ptr<const Snapshot> previous;
    { std::lock_guard lock(mutex_); previous = std::exchange(snapshot_, next); }
    bool changed = !previous || previous->count_ != count || previous->rows_.size() != next->rows_.size();
    for (std::size_t i = 0; !changed && i < next->rows_.size(); ++i)
        changed = previous->rows_[i].id != next->rows_[i].id || previous->rows_[i].info.index != next->rows_[i].info.index ||
            previous->rows_[i].info.source_version != next->rows_[i].info.source_version ||
            previous->rows_[i].children.size() != next->rows_[i].children.size();
    if (previous) for (const auto& row : previous->rows_) for (const auto& child : row.children) {
        std::lock_guard child_lock(child.accessibility->mutex);
        if (child.accessibility->fragment_navigation.lock().get() == this) child.accessibility->fragment_navigation.reset();
    }
    for (const auto& row : next->rows_) for (const auto& child : row.children) {
        std::lock_guard child_lock(child.accessibility->mutex);
        child.accessibility->fragment_navigation = shared_from_this();
    }
    return changed;
}
void VirtualRowsAccessibility::close() {
    std::shared_ptr<const Snapshot> previous;
    { std::lock_guard lock(mutex_); window_ = nullptr; previous = std::exchange(snapshot_, {}); }
    if (previous) for (const auto& row : previous->rows_) for (const auto& child : row.children) {
        std::lock_guard child_lock(child.accessibility->mutex);
        if (child.accessibility->fragment_navigation.lock().get() == this) child.accessibility->fragment_navigation.reset();
    }
}
HRESULT VirtualRowsAccessibility::navigate(std::uint64_t control, HWND window, NavigateDirection direction, IRawElementProviderFragment** result) {
    return virtual_uia([&]() -> HRESULT {
        std::shared_ptr<const Snapshot> snapshot;
        { std::lock_guard lock(mutex_); snapshot = snapshot_; }
        if (!snapshot || !snapshot->window_) return UIA_E_ELEMENTNOTAVAILABLE;
        if (direction != NavigateDirection_Parent && direction != NavigateDirection_NextSibling &&
            direction != NavigateDirection_PreviousSibling) return S_FALSE;
        for (const auto& row : snapshot->rows_) for (std::size_t i = 0; i < row.children.size(); ++i)
            if (row.children[i].id == control && row.children[i].window == window) {
            if (direction == NavigateDirection_Parent) { *result = new VirtualRowProvider(weak_from_this(), row.id); return S_OK; }
            if (direction == NavigateDirection_NextSibling && i + 1 < row.children.size())
                return virtual_fragment(row.children[i + 1].provider.Get(), result);
            if (direction == NavigateDirection_PreviousSibling && i)
                return virtual_fragment(row.children[i - 1].provider.Get(), result);
            return S_OK;
        }
        return UIA_E_ELEMENTNOTAVAILABLE;
    });
}
HRESULT VirtualRowsAccessibility::fragment_root(IRawElementProviderFragmentRoot** result) {
    return virtual_uia([&]() -> HRESULT {
        { std::lock_guard lock(mutex_); if (!snapshot_ || !snapshot_->window_) return UIA_E_ELEMENTNOTAVAILABLE; }
        *result = new VirtualRowProvider(weak_from_this(), 0); return S_OK;
    });
}
