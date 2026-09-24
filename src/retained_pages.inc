RetainedPages::RetainedPages(std::wstring name) : Control(ControlRole::content_view, std::move(name), {640, 360}) {
    set_auto_size(true);
}
void RetainedPages::validate_entries(const std::vector<RetainedPageEntry>& entries, std::uint64_t selected) {
    if (entries.size() > 4096) throw std::invalid_argument("Retained pages support at most 4096 entries");
    std::set<std::uint64_t> ids;
    bool found = !selected;
    for (const auto& entry : entries) {
        if (!entry.id || entry.id > INT64_MAX - 100 || !ids.insert(entry.id).second ||
            entry.title.empty() || entry.title.size() > 1024 || entry.title.find(L'\0') != std::wstring::npos ||
            std::all_of(entry.title.begin(), entry.title.end(), [](wchar_t c) { return std::iswspace(c) != 0; }))
            throw std::invalid_argument("Invalid retained page identity or title");
        if (entry.id == selected) found = entry.enabled;
    }
    if (!found) throw std::invalid_argument("The selected page must exist and be enabled");
}
const std::shared_ptr<Element>& RetainedPages::page_root(std::size_t index) const {
    return static_cast<const ContentView&>(*children_.at(index)).content();
}
std::size_t RetainedPages::index_of(const Element& child) const {
    for (std::size_t i = 0; i < children_.size(); ++i) if (page_root(i).get() == &child) return i;
    throw std::invalid_argument("The root is not a child of these retained pages");
}
void RetainedPages::validate_complete() const {
    validate_order(entries_);
}
void RetainedPages::validate_order(const std::vector<RetainedPageEntry>& entries) const {
    if (entries.size() != ids_.size()) throw std::invalid_argument("Page entries require one constructed root each");
    for (std::size_t i = 0; i < ids_.size(); ++i)
        if (entries[i].id != ids_[i]) throw std::invalid_argument("Page entry and root order must match");
}
void RetainedPages::set_entries(std::vector<RetainedPageEntry> entries, std::uint64_t selected, bool complete) {
    validate_entries(entries, selected);
    if (complete) validate_order(entries);
    if (entries_ == entries && selected_ == selected) return;
    entries_ = std::move(entries); selected_ = selected;
    sync_visibility();
    invalidate(Invalidation::layout);
}
void RetainedPages::sync_visibility() {
    for (std::size_t i = 0; i < children_.size(); ++i) {
        const auto entry = std::find_if(entries_.begin(), entries_.end(), [&](const auto& entry) { return entry.id == ids_[i]; });
        auto& wrapper = static_cast<ContentView&>(*children_[i]);
        wrapper.set_visible(ids_[i] == selected_ && entry != entries_.end() && entry->enabled);
        if (entry != entries_.end()) wrapper.set_name(entry->title);
    }
}
void RetainedPages::insert(std::size_t index, std::uint64_t id, std::shared_ptr<Element> child) {
    if (index > children_.size() || children_.size() == 4096 || !id || id > INT64_MAX - 100 ||
        std::find(ids_.begin(), ids_.end(), id) != ids_.end() || !child || child.get() == this)
        throw std::invalid_argument("Invalid retained page insertion");
    children_.reserve(children_.size() + 1); ids_.reserve(ids_.size() + 1);
    auto wrapper = std::make_shared<ContentView>(std::move(child), L"Page");
    wrapper->set_auto_size(true);
    adopt(wrapper);
    children_.insert(children_.begin() + index, std::move(wrapper));
    ids_.insert(ids_.begin() + index, id);
    sync_visibility();
    invalidate(Invalidation::layout);
}
void RetainedPages::remove(const Element& child) {
    const auto index = index_of(child);
    children_.erase(children_.begin() + index); ids_.erase(ids_.begin() + index);
    invalidate(Invalidation::layout);
}
void RetainedPages::move(const Element& child, std::size_t index) {
    const auto before = index_of(child);
    if (index >= children_.size()) throw std::invalid_argument("Retained page move index is out of range");
    if (before < index) {
        std::rotate(children_.begin() + before, children_.begin() + before + 1, children_.begin() + index + 1);
        std::rotate(ids_.begin() + before, ids_.begin() + before + 1, ids_.begin() + index + 1);
    } else if (before > index) {
        std::rotate(children_.begin() + index, children_.begin() + before, children_.begin() + before + 1);
        std::rotate(ids_.begin() + index, ids_.begin() + before, ids_.begin() + before + 1);
    } else return;
    invalidate(Invalidation::layout);
}
Size RetainedPages::measure(Size available) {
    if (!visible()) return {};
    return measure_axes(available, [&](Size offered, bool natural) {
        if (!natural && !auto_size()) return base_measure(offered, false);
        const auto insets = layout_style::insets(effective_control_style_values(StylePart::root));
        Size desired{};
        for (std::size_t i = 0; i < children_.size(); ++i)
            if (ids_[i] == selected_ && static_cast<const Control&>(*children_[i]).visible())
                desired = children_[i]->measure(layout_style::inner(offered, insets));
        return constrain_measure(layout_style::outer(desired, insets), offered, natural);
    });
}
void RetainedPages::arrange(Rect rectangle) {
    if (!visible()) rectangle.width = rectangle.height = 0;
    Element::arrange(rectangle);
    const auto content = layout_style::content(*this, bounds());
    for (const auto& child : children_) {
        const bool active = visible() && static_cast<const Control&>(*child).visible();
        child->arrange(active ? content : Rect{content.x, content.y, 0, 0});
    }
}
