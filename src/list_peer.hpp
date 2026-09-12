#pragma once
#include "xui/file_list.hpp"
#include "xui/accessibility.hpp"
#include "drawing.hpp"

namespace xui {
// The reusable native adapter owns only list presentation, input, and accessibility.
class ListPeer {
public:
    ListPeer(std::shared_ptr<FileList> control, std::function<void()> failure, std::function<void(HWND)> focus);
    ~ListPeer();
    void attach(HWND parent, int id);
    void update(UINT dpi, const Palette& palette);
    void publish(bool structure = false);
    void paint(Drawing& drawing);
    HWND window() const { return window_; }
    std::size_t rows() const { return rows_; }
private:
    static LRESULT CALLBACK procedure(HWND, UINT, WPARAM, LPARAM) noexcept;
    LRESULT message(HWND, UINT, WPARAM, LPARAM);
    void invalidate();
    void viewport();
    void changed();
    void select_at(int y);
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
    bool hover_scrollbar_{}, dragging_{}, tracking_{};
    float drag_offset_{};
    int wheel_delta_{};
    std::shared_ptr<AccessibilityState> accessibility_{std::make_shared<AccessibilityState>()};
    IRawElementProviderSimple* provider_{};
    const FilteredView* published_{};
    std::shared_ptr<const std::wstring> name_, automation_id_;
    std::size_t rows_{};
};

void show_control_menu(Control& control, HWND window, LPARAM position);
// Releases potentially large snapshots on a background cleanup thread.
void dispose_later(std::shared_ptr<const void> value);
}
