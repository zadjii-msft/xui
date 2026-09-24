namespace {
constexpr std::uint64_t virtual_max_epoch = INT64_MAX;
constexpr double virtual_max_pixels = 0x00ffffff;
}
VirtualViewport::VirtualViewport(std::uint32_t count, float pitch, std::uint64_t source)
    : pitch_(pitch), count_(count), requested_source_(source) {
    if (!std::isfinite(pitch) || pitch <= 0 || !source || source > virtual_max_epoch)
        throw std::invalid_argument("Invalid virtual viewport pitch or source version");
    extent_ = validate_extent(count, 1);
    requested_.extent = extent_;
    static std::atomic<std::uint64_t> next_identity{1};
    auto identity = next_identity.load(std::memory_order_relaxed);
    for (;;) {
        if (identity == UINT64_MAX) throw std::overflow_error("Virtual viewport identity exhausted");
        if (next_identity.compare_exchange_weak(identity, identity + 1, std::memory_order_relaxed)) break;
    }
    identity_ = identity;
}
void VirtualViewport::require_open() const {
    if (closed_) throw std::logic_error("The virtual viewport lease is closed");
}
float VirtualViewport::validate_extent(std::uint32_t count, float scale) const {
    const double value = static_cast<double>(count) * pitch_;
    if (count > INT32_MAX || value * scale > virtual_max_pixels || pitch_ * scale < 1 ||
        pitch_ * scale > 32767)
        throw std::invalid_argument("Virtual rows exceed the supported native geometry range");
    const auto result = static_cast<float>(value);
    if (count > 1 && (std::nextafter(result, std::numeric_limits<float>::infinity()) - result) * 8 > pitch_)
        throw std::invalid_argument("Virtual row boundaries cannot be represented distinctly");
    return result;
}
VirtualViewportRect VirtualViewport::normalized_request(float extent, float offset, Size available, float scale) const {
    if (!std::isfinite(available.width) || !std::isfinite(available.height) || available.width < 0 || available.height < 0 ||
        !std::isfinite(scale) || scale <= 0 || available.width * scale > 32767 || available.height * scale > 32767)
        throw std::invalid_argument("Invalid native virtual viewport allocation");
    const auto width = static_cast<float>(std::floor(static_cast<double>(available.width) * scale) / scale);
    const auto height = static_cast<float>(std::floor(static_cast<double>(available.height) * scale) / scale);
    const auto maximum = std::max(0.0f, extent - height);
    const auto snapped = static_cast<float>(std::round(static_cast<double>(std::min(offset, maximum)) * scale) / scale);
    return {std::clamp(snapped, 0.0f, maximum), width, height, extent};
}
VirtualViewportRequest VirtualViewport::request() const {
    require_open();
    return {epoch_, committed_source_, requested_source_, committed_, requested_, composing_};
}
void VirtualViewport::notify() {
    if (observed_ && !active_ && !cancelled_ && !closed_ && notify_) notify_();
}
void VirtualViewport::publish_request(VirtualViewportRect next, bool force) {
    if (!force && requested_ == next) return;
    if (epoch_ == virtual_max_epoch) throw std::overflow_error("Virtual viewport epoch exhausted");
    requested_ = next;
    ++epoch_;
    cancelled_ = false;
    notify();
}
void VirtualViewport::observe(Size available, float scale, bool composing, std::uint64_t focus) {
    require_open();
    if (!std::isfinite(scale) || scale <= 0) throw std::invalid_argument("Invalid virtual viewport scale");
    const auto extent = validate_extent(count_, scale);
    const auto next = normalized_request(extent, offset_intent_, available, scale);
    const bool changed = !observed_ || composing != composing_ || focus != focus_identity_ || scale != scale_;
    if ((changed || requested_ != next) && epoch_ == virtual_max_epoch)
        throw std::overflow_error("Virtual viewport epoch exhausted");
    available_ = available; scale_ = scale; extent_ = extent; composing_ = composing; focus_identity_ = focus;
    observed_ = true;
    publish_request(next, changed);
}
void VirtualViewport::set_extent(std::uint32_t count, std::uint64_t source) {
    require_open();
    if (!source || source > virtual_max_epoch || source < requested_source_ ||
        (source == requested_source_ && count != count_))
        throw std::invalid_argument("Virtual source versions must increase when the source changes");
    if (source == requested_source_ && count == count_) return;
    const auto extent = validate_extent(count, scale_);
    const auto next = normalized_request(extent, offset_intent_, available_, scale_);
    if (epoch_ == virtual_max_epoch) throw std::overflow_error("Virtual viewport epoch exhausted");
    count_ = count; extent_ = extent; requested_source_ = source;
    publish_request(next, true);
}
void VirtualViewport::request_offset(float offset, bool force) {
    require_open();
    if (!std::isfinite(offset) || offset < 0) throw std::invalid_argument("Invalid virtual scroll offset");
    const auto next = normalized_request(extent_, offset, available_, scale_);
    if ((force || cancelled_ || next != requested_) && epoch_ == virtual_max_epoch)
        throw std::overflow_error("Virtual viewport epoch exhausted");
    offset_intent_ = offset;
    publish_request(next, force || cancelled_);
}
VirtualViewportResult VirtualViewport::try_begin(std::uint64_t epoch) {
    require_open();
    if (!epoch || epoch > virtual_max_epoch) throw std::invalid_argument("Invalid virtual viewport epoch");
    if (epoch != epoch_ || cancelled_) return VirtualViewportResult::superseded;
    if (!observed_ || composing_ || active_) return VirtualViewportResult::blocked;
    active_ = request();
    active_count_ = count_;
    return VirtualViewportResult::ready;
}
VirtualViewportResult VirtualViewport::commit(std::uint64_t epoch) {
    require_open();
    if (!active_ || active_->epoch != epoch)
        throw std::invalid_argument("Commit requires the reserved virtual viewport epoch");
    if (composing_) throw std::runtime_error("Composition began during a reserved virtual viewport update");
    committed_ = active_->requested;
    committed_epoch_ = active_->epoch;
    committed_source_ = active_->requested_source_version;
    committed_count_ = active_count_;
    active_.reset();
    if (epoch_ != epoch) notify();
    return VirtualViewportResult::ready;
}
void VirtualViewport::cancel(std::uint64_t epoch) {
    require_open();
    if (!epoch || epoch > virtual_max_epoch || epoch > epoch_) throw std::invalid_argument("Invalid cancellation epoch");
    if (active_) {
        if (epoch != active_->epoch) return;
        active_.reset();
        cancelled_ = true;
    } else if (epoch == epoch_) cancelled_ = true;
}
void VirtualViewport::close() noexcept {
    closed_ = true;
    active_.reset();
    notify_ = {};
    items_.clear();
}
float VirtualViewport::requested_maximum_offset() const {
    return std::max(0.0f, requested_.extent - requested_.height);
}
float VirtualViewport::layout_extent() const {
    if (closed_) return 0;
    return std::max(committed_.extent, active_ ? active_->requested.extent : 0.0f);
}
float VirtualViewport::layout_width() const {
    return closed_ ? 0 : active_ ? active_->requested.width : committed_.width;
}
const VirtualViewportRequest& VirtualViewport::active_request() const {
    require_open();
    if (!active_) throw std::logic_error("A reserved viewport update is required");
    return *active_;
}
void VirtualViewport::set_item(std::shared_ptr<Stack> root, VirtualItemInfo info) {
    const auto& active = active_request();
    if (!root || typeid(*root) != typeid(Stack) || info.key.empty() || info.key.size() > 4096 ||
        info.key.find(u'\0') != std::u16string::npos || info.index >= info.count ||
        info.count != active_count_ || info.source_version != active.requested_source_version)
        throw std::invalid_argument("Invalid virtual row identity, source, or count");
    for (const auto& [id, item] : items_) {
        if (id == root->id()) {
            if (item.info.key != info.key) throw std::invalid_argument("A retained row cannot change its key identity");
        } else if (!item.root.expired() && item.info.source_version == info.source_version &&
            (item.info.index == info.index || item.info.key == info.key))
            throw std::invalid_argument("Duplicate mounted virtual row index or key");
    }
    items_.insert_or_assign(root->id(), VirtualViewportItem{root, std::move(info)});
}
void VirtualViewport::remove_item(std::uint64_t id) { items_.erase(id); }
std::vector<VirtualViewportItem> VirtualViewport::items() const {
    std::vector<VirtualViewportItem> result;
    result.reserve(items_.size());
    for (const auto& [id, item] : items_) if (!item.root.expired()) result.push_back(item);
    std::stable_sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        return a.info.index < b.info.index;
    });
    return result;
}
