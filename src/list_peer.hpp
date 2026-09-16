#pragma once
#include "xui/file_list.hpp"
#include "xui/accessibility.hpp"
#include "drawing.hpp"
#include "images.hpp"

namespace xui {
// The reusable native adapter owns only list presentation, input, and accessibility.
class ListPeer {
public:
    ListPeer(std::shared_ptr<FileList> control, std::function<void()> failure, std::function<void(HWND)> focus);
    ~ListPeer();
    void attach(HWND parent, int id);
    void set_theme(UINT dpi, const Palette& palette) { dpi_ = dpi; palette_ = palette; }
    void update(UINT dpi, const Palette& palette);
    void publish(bool structure = false);
    void paint(Drawing& drawing);
    HWND window() const { return window_; }
    std::size_t rows() const { return rows_; }
    bool sync_thumbnails(bool shown, Rect clip, const std::shared_ptr<TaskWake>& wake,
        std::vector<std::uint64_t>& retained, std::size_t& remaining);
    void detach_thumbnails();
    std::size_t thumbnail_count() const { return thumbnails_.size(); }
    std::size_t thumbnail_ready() const {
        return std::count_if(thumbnails_.begin(), thumbnails_.end(), [](const auto& slot) { return !!slot->pixels; });
    }
private:
    struct Thumbnail {
        ItemId id{};
        std::wstring path;
        std::wstring error;
        std::shared_ptr<ImageRequest> request;
        std::shared_ptr<const ImagePixels> pixels;
        bool reported{};
        ~Thumbnail() { if (request) request->cancel(); }
    };
    std::vector<std::unique_ptr<Thumbnail>> thumbnails_;
    std::weak_ptr<const FileSnapshot> thumbnail_source_;
    std::uint64_t thumbnail_revision_{};
    UINT thumbnail_pixels_{};
    static LRESULT CALLBACK procedure(HWND, UINT, WPARAM, LPARAM) noexcept;
    LRESULT message(HWND, UINT, WPARAM, LPARAM);
    void invalidate();
    void viewport();
    void changed();
    bool select_at(int x, int y);
    void pointer_down(LPARAM);
    void pointer_move(LPARAM);
    float width() const;
    bool in_scrollbar(LPARAM) const;
    std::shared_ptr<FileList> list_;
    std::function<void()> failure_;
    std::function<void(HWND)> focus_;
    HWND window_{};
    UINT dpi_{96};
    Palette palette_{};
    ScrollThumb thumb_{};
    std::optional<std::size_t> hovered_;
    std::optional<Point> pointer_;
    bool hover_scrollbar_{}, dragging_{}, tracking_{};
    float drag_offset_{};
    int wheel_delta_{};
    std::shared_ptr<AccessibilityState> accessibility_{std::make_shared<AccessibilityState>()};
    IRawElementProviderSimple* provider_{};
    const FilteredView* published_{};
    std::shared_ptr<const std::wstring> name_, automation_id_, help_text_;
    std::size_t rows_{};
};

// Releases potentially large snapshots on a background cleanup thread.
void dispose_later(std::shared_ptr<const void> value);
}
