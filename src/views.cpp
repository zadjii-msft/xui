#include "xui/core.hpp"
#include <algorithm>
#include <cwctype>
#include <limits>
#include <stdexcept>
#include <utility>

namespace xui {
namespace {
struct Cancelled {};
void checkpoint(const CancelCheck& cancel) {
    if (cancel && cancel()) throw Cancelled{};
}
}

std::shared_ptr<const FileSnapshot> FileSnapshot::build(
    std::shared_ptr<const std::vector<FileItem>> items, const CancelCheck& cancel) {
    try {
        checkpoint(cancel);
        auto result = std::make_shared<FileSnapshot>();
        result->items_ = items ? std::move(items) : std::make_shared<const std::vector<FileItem>>();
        const auto count = result->items_->size();
        if (count > std::numeric_limits<RowIndex>::max())
            throw std::length_error("File snapshot exceeds the 32-bit row limit");
        result->by_id_.reserve(count);
        result->name_offsets_.reserve(count + 1);
        std::size_t name_units{};
        for (std::size_t index = 0; index < count; ++index) {
            if ((index & 255) == 0) checkpoint(cancel);
            const auto size = (*result->items_)[index].name.size();
            if (size > result->names_.max_size() - name_units)
                throw std::length_error("File names exceed the snapshot storage limit");
            name_units += size;
        }
        result->names_.reserve(name_units);
        std::size_t characters{};
        for (std::size_t index = 0; index < count; ++index) {
            if ((index & 255) == 0) checkpoint(cancel);
            result->by_id_.push_back(static_cast<RowIndex>(index));
            result->name_offsets_.push_back(result->names_.size());
            for (wchar_t character : (*result->items_)[index].name) {
                if ((characters++ & 4095) == 0) checkpoint(cancel);
                result->names_.push_back(static_cast<wchar_t>(std::towlower(character)));
            }
        }
        result->name_offsets_.push_back(result->names_.size());
        std::size_t comparisons{};
        std::sort(result->by_id_.begin(), result->by_id_.end(), [&](RowIndex a, RowIndex b) {
            if ((comparisons++ & 4095) == 0) checkpoint(cancel);
            return (*result->items_)[a].id < (*result->items_)[b].id;
        });
        for (std::size_t i = 1; i < count; ++i) {
            if ((i & 255) == 0) checkpoint(cancel);
            if ((*result->items_)[result->by_id_[i - 1]].id == (*result->items_)[result->by_id_[i]].id)
                throw std::invalid_argument("File item IDs must be unique");
        }
        checkpoint(cancel);
        return result;
    } catch (const Cancelled&) { return {}; }
}

std::optional<RowIndex> FileSnapshot::find(ItemId id) const noexcept {
    const auto found = std::lower_bound(by_id_.begin(), by_id_.end(), id,
        [&](RowIndex index, ItemId value) { return (*items_)[index].id < value; });
    if (found == by_id_.end() || (*items_)[*found].id != id) return {};
    return *found;
}

std::wstring_view FileSnapshot::folded_name(RowIndex index) const noexcept {
    return {names_.data() + name_offsets_[index], name_offsets_[index + 1] - name_offsets_[index]};
}

std::size_t FileSnapshot::index_bytes() const noexcept {
    return by_id_.capacity() * sizeof(RowIndex) + name_offsets_.capacity() * sizeof(std::size_t)
        + names_.capacity() * sizeof(wchar_t);
}

std::shared_ptr<const FilteredView> FilteredView::build(
    std::shared_ptr<const FileSnapshot> source, std::wstring query, const CancelCheck& cancel) {
    if (!source) throw std::invalid_argument("Filter source must not be null");
    try {
        checkpoint(cancel);
        auto result = std::make_shared<FilteredView>();
        result->source_ = std::move(source);
        for (std::size_t i = 0; i < query.size(); ++i) {
            if ((i & 4095) == 0) checkpoint(cancel);
            query[i] = static_cast<wchar_t>(std::towlower(query[i]));
        }
        result->query_ = std::move(query);
        const auto count = result->source_->items()->size();
        // Sparse results allocate only for matches. An empty query has an exact-sized array.
        if (result->query_.empty()) result->indices_.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            if ((i & 255) == 0) checkpoint(cancel);
            if (result->query_.empty() ||
                result->source_->folded_name(static_cast<RowIndex>(i)).find(result->query_) != std::wstring_view::npos)
                result->indices_.push_back(static_cast<RowIndex>(i));
        }
        checkpoint(cancel);
        return result;
    } catch (const Cancelled&) { return {}; }
}

std::optional<std::size_t> FilteredView::find(ItemId id) const noexcept {
    const auto underlying = source_->find(id);
    if (!underlying) return {};
    const auto found = std::lower_bound(indices_.begin(), indices_.end(), *underlying);
    if (found == indices_.end() || *found != *underlying) return {};
    return static_cast<std::size_t>(found - indices_.begin());
}

ViewWorker::ViewWorker(Loader loader, std::function<void()> ready)
    : loader_(std::move(loader)), ready_(std::move(ready)) {}
ViewWorker::~ViewWorker() { request_stop(); join(); }
void ViewWorker::start() {
    std::lock_guard lock(mutex_);
    if (thread_.joinable() || stopped_) throw std::logic_error("View worker can start only once");
    thread_ = std::jthread([this](std::stop_token stop) { run(stop); });
}

std::uint64_t ViewWorker::request(std::wstring query, bool refresh) {
    std::lock_guard lock(mutex_);
    if (stopped_) throw std::logic_error("View worker is stopped");
    query_ = std::move(query);
    if (refresh || source_generation_ == 0) ++source_generation_;
    const auto generation = ++generation_;
    busy_ = true;
    changed_.notify_one();
    return generation;
}

std::optional<ViewResult> ViewWorker::take_result() {
    std::lock_guard lock(mutex_);
    if (result_ && result_->generation != generation_) return {};
    return std::exchange(result_, std::nullopt);
}

void ViewWorker::retire(std::shared_ptr<const FilteredView> view) {
    std::lock_guard lock(mutex_);
    retired_.push_back(std::move(view));
    changed_.notify_one();
}

void ViewWorker::request_stop() {
    std::lock_guard lock(mutex_);
    stopped_ = true;
    thread_.request_stop();
    changed_.notify_all();
}
void ViewWorker::join() {
    if (thread_.joinable()) thread_.join();
    busy_ = false;
}

void ViewWorker::run(std::stop_token stop) {
    std::uint64_t processed{}, loaded{};
    SourceResult source;
    for (;;) {
        if (stop.stop_requested()) return;
        std::uint64_t generation, source_generation;
        std::wstring query;
        std::vector<std::shared_ptr<const FilteredView>> garbage;
        {
            std::unique_lock lock(mutex_);
            if (!changed_.wait(lock, stop, [&] { return generation_ != processed || !retired_.empty(); }))
                return;
            garbage.swap(retired_);
            generation = generation_;
            source_generation = source_generation_;
            query = query_;
            busy_ = generation != processed;
        }
        garbage.clear(); // Destruction of replaced collections stays off the UI thread.
        if (generation == processed) continue;
        const CancelCheck cancelled = [&] { return stop.stop_requested() || generation_ != generation; };
        ViewResult result{generation, {}, {}};
        try {
            if (loaded != source_generation) {
                const CancelCheck source_cancelled = [&] {
                    return stop.stop_requested() || source_generation_ != source_generation;
                };
                auto replacement = loader_(source_cancelled);
                if (source_cancelled()) continue;
                if (!replacement.source && replacement.error.empty())
                    throw std::runtime_error("Source loader returned no snapshot");
                source = std::move(replacement);
                loaded = source_generation;
            }
            if (cancelled()) continue;
            if (source.source) result.view = FilteredView::build(source.source, std::move(query), cancelled);
            result.error = source.error;
        } catch (const std::exception& error) {
            const std::string text(error.what());
            result.error = L"View update failed: " + std::wstring(text.begin(), text.end());
        }
        std::optional<ViewResult> discarded;
        {
            std::lock_guard lock(mutex_);
            processed = generation;
            if (cancelled()) continue;
            busy_ = false;
            discarded = std::move(result_);
            result_ = std::move(result);
            if (ready_) ready_();
        }
        // An unread result can own the last reference to a large old source.
        // Release it outside the request mutex, so typing cannot block on destruction.
    }
}
}
