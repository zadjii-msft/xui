#include "../src/collection_accessibility.hpp"
#include "../src/grid_accessibility.hpp"
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <thread>

namespace {
class ReleaseWitness {
public:
    explicit ReleaseWitness(std::mutex& state_mutex)
        : state_mutex_(state_mutex), worker_([this] {
            std::unique_lock lock(mutex_);
            if (!changed_.wait_for(lock, std::chrono::seconds(5), [this] { return requested_; })) {
                timed_out_ = completed_ = true;
                changed_.notify_all();
                return;
            }
            lock.unlock();
            const bool available = state_mutex_.try_lock();
            if (available) state_mutex_.unlock();
            lock.lock();
            outside_lock_ = available;
            completed_ = true;
            changed_.notify_all();
        }) {}
    ~ReleaseWitness() { worker_.join(); }
    void released() noexcept {
        std::unique_lock lock(mutex_);
        ++releases_;
        requested_ = true;
        changed_.notify_all();
        if (!changed_.wait_for(lock, std::chrono::seconds(5), [this] { return completed_; }))
            timed_out_ = true;
    }
    bool report(std::string_view name) {
        std::lock_guard lock(mutex_);
        std::cout << "{\"case\":\"" << name << "\",\"source_releases\":" << releases_
                  << ",\"outside_state_lock\":" << (outside_lock_ ? "true" : "false")
                  << ",\"timed_out\":" << (timed_out_ ? "true" : "false") << "}\n";
        return releases_ == 1 && completed_ && !timed_out_ && outside_lock_;
    }
private:
    std::mutex& state_mutex_;
    std::mutex mutex_;
    std::condition_variable changed_;
    bool requested_{}, completed_{}, outside_lock_{}, timed_out_{};
    unsigned releases_{};
    std::thread worker_;
};

class Items final : public xui::ItemsSource {
public:
    explicit Items(ReleaseWitness& witness) : witness_(witness) {}
    ~Items() override { witness_.released(); }
    std::size_t size() const override { return 1; }
    xui::ItemKey key(std::size_t row) const override {
        if (row) throw std::out_of_range("Invalid test row");
        return {13, 37};
    }
    std::optional<std::size_t> find(xui::ItemKey value) const override {
        return value == xui::ItemKey{13, 37} ? std::optional<std::size_t>{0} : std::nullopt;
    }
    xui::ItemContent item(std::size_t row) const override {
        if (row) throw std::out_of_range("Invalid test row");
        return {};
    }
private:
    ReleaseWitness& witness_;
};
class Grid final : public xui::GridSource {
public:
    explicit Grid(ReleaseWitness& witness) : witness_(witness) {}
    ~Grid() override { witness_.released(); }
    std::size_t size() const override { return 1; }
    xui::RowKey key(std::size_t row) const override {
        if (row) throw std::out_of_range("Invalid test row");
        return {13, 37};
    }
    std::optional<std::size_t> find(xui::RowKey value) const override {
        return value == xui::RowKey{13, 37} ? std::optional<std::size_t>{0} : std::nullopt;
    }
    std::wstring text(std::size_t row, std::size_t column) const override {
        if (row || column) throw std::out_of_range("Invalid test cell");
        return L"row";
    }
private:
    ReleaseWitness& witness_;
};

template<class State>
struct Cleanup {
    State& state;
    ~Cleanup() { state.snapshot = {}; }
};

bool collection_case(bool selection_only, bool publication) {
    auto state = std::make_shared<xui::CollectionAccessibility>();
    ReleaseWitness witness(state->mutex);
    Cleanup cleanup{*state};
    {
        const auto source = std::make_shared<Items>(witness);
        if (selection_only) {
            state->snapshot.selection.select_all(source, xui::SelectAllScope::filtered);
            const auto selected = state->snapshot.selection.selected_keys(source);
            if (!selected || selected->size() != 1 || selected->front() != source->key(0))
                throw std::runtime_error("Selection-only retirement must retain a nonempty source selection");
        } else state->snapshot.collection = source;
    }
    if (publication) {
        xui::ItemsView replacement(L"replacement");
        xui::publish_collection(state, nullptr, replacement, nullptr);
    } else xui::disconnect_row_accessibility(state, nullptr);
    return witness.report(publication ?
        (selection_only ? "publish-collection-selection-only" : "publish-collection") :
        (selection_only ? "disconnect-collection-selection-only" : "disconnect-collection"));
}

enum class GridSlot { source, full_source, selection_only };
bool grid_case(GridSlot slot, bool publication) {
    auto state = std::make_shared<xui::GridAccessibility>();
    ReleaseWitness witness(state->mutex);
    Cleanup cleanup{*state};
    {
        const auto source = std::make_shared<Grid>(witness);
        if (slot == GridSlot::source) state->snapshot.grid = source;
        else if (slot == GridSlot::full_source) state->snapshot.full_source = source;
        else {
            state->snapshot.selection.select_all(source, xui::SelectAllScope::filtered);
            const auto selected = state->snapshot.selection.selected_keys(source);
            if (!selected || selected->size() != 1 || selected->front() != source->key(0))
                throw std::runtime_error("Grid selection-only retirement must retain a nonempty source selection");
        }
    }
    if (publication) {
        xui::DataGrid replacement(L"replacement");
        xui::publish_grid(state, nullptr, replacement, nullptr);
    } else xui::disconnect_row_accessibility(state, nullptr);
    const char* name = slot == GridSlot::source ? "grid" :
        slot == GridSlot::full_source ? "grid-full-source" : "grid-selection-only";
    return witness.report(std::string(publication ? "publish-" : "disconnect-") + name);
}
}

int main(int argc, char** argv) {
    const bool disconnect = argc == 1 || (argc == 2 && std::string_view(argv[1]) == "--disconnect-only");
    const bool publication = argc == 1 || (argc == 2 && std::string_view(argv[1]) == "--publication-only");
    if (!disconnect && !publication) {
        std::cerr << "Usage: xui_family_accessibility_retirement_tests [--disconnect-only|--publication-only]\n";
        return 2;
    }
    try {
        bool ok = true;
        unsigned cases{};
        for (const bool publish : {false, true}) {
            if (!(publish ? publication : disconnect)) continue;
            for (const bool selection_only : {false, true}) {
                ok &= collection_case(selection_only, publish);
                ++cases;
            }
            for (const auto slot : {GridSlot::source, GridSlot::full_source, GridSlot::selection_only}) {
                ok &= grid_case(slot, publish);
                ++cases;
            }
        }
        std::cout << "retirement_cases=" << cases << '\n';
        return ok && cases == (disconnect && publication ? 10u : 5u) ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
