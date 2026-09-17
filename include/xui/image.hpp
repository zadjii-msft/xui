#pragma once

#include "xui/controls.hpp"

namespace xui {

struct ImageSize {
    std::uint32_t width{192}, height{144};
    bool operator==(const ImageSize&) const = default;
};
enum class ImageStatus { empty, loading, ready, error };
enum class ImageKind { wic, shell };

// Process-wide limits. Pixel and bitmap counters exclude codec/driver allocations.
struct ImageLimits {
    static constexpr std::size_t cpu_bytes = 8 * 1024 * 1024;
    static constexpr std::size_t gpu_bytes = 8 * 1024 * 1024;
    static constexpr std::size_t queue = 64, cache_entries = 128, workers = 2;
    static constexpr std::uint32_t output_dimension = 1024, source_dimension = 16384;
    static constexpr std::uint64_t source_pixels = 16 * 1024 * 1024, file_bytes = 32 * 1024 * 1024;
};
struct ImageStatistics {
    std::size_t cpu_bytes{}, cpu_reserved{}, cpu_peak{}, gpu_bytes{}, gpu_reserved{}, gpu_peak{};
    std::size_t queued{}, active{}, cache_entries{};
    std::uint64_t decoded{}, cache_hits{}, evicted{}, rejected{}, cancelled{}, uploaded{};
    double decode_ms{}, decode_max_ms{}, upload_ms{}, upload_max_ms{};
};
class ImageResources final {
public:
    static ImageStatistics statistics();
    // Removes unpinned decoded entries. No file I/O or decoder join.
    static void clear_unused();
};

// Windows backend. Source changes and all control properties belong to the UI thread.
class Image final : public Control {
public:
    explicit Image(std::wstring name = L"Image");
    void set_source(std::wstring path, ImageSize display_pixels = {});
    void set_shell_source(std::wstring path, ImageSize display_pixels = {});
    void reload();
    void unload();
    const std::wstring& source() const { return source_; }
    ImageKind source_kind() const { return kind_; }
    ImageSize display_pixels() const { return size_; }
    ImageStatus status() const { return status_; }
    const std::wstring& error() const { return error_; }
    std::uint64_t revision() const { return revision_; }
    Rect content_bounds() const;
protected:
    std::optional<StyleTarget> control_style_target() const override { return StyleTarget::image; }
    StyleStateMask control_style_state_bits() const override;
private:
    friend struct ImagePeer;
    void set_source_kind(std::wstring path, ImageSize display_pixels, ImageKind kind);
    void publish(ImageStatus status, std::wstring error = {});
    std::wstring source_, error_;
    ImageSize size_{};
    ImageKind kind_{ImageKind::wic};
    ImageStatus status_{};
    std::uint64_t revision_{1};
};

}
