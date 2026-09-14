#pragma once
#include "xui/controls.hpp"
namespace xui {
enum class CaptionAction { minimize, maximize_restore, close };
enum class CaptionHit { client, drag, minimize, maximize, close };
// Caption Buttons retain their actions and accessibility, but use Windows caption styling.
class TitleBar final : public Control {
public:
    explicit TitleBar(std::wstring title);
    ~TitleBar() override;
    const std::shared_ptr<TabStrip>& tabs() const { return tabs_; }
    const std::shared_ptr<Button>& minimize() const { return minimize_; }
    const std::shared_ptr<Button>& maximize() const { return maximize_; }
    const std::shared_ptr<Button>& close() const { return close_; }
    void set_title(std::wstring title);
    void set_maximized(bool maximized);
    void on_caption(std::function<void(CaptionAction)> callback);
    CaptionHit hit_test(Point client) const;
    std::span<const std::shared_ptr<Element>> retained_children() const override { return children_; }
    void arrange(Rect bounds) override;
    static constexpr float height = 44;
    static constexpr float caption_width = 46;
    static constexpr float caption_height = 32;
private:
    std::shared_ptr<TabStrip> tabs_;
    std::shared_ptr<Label> title_;
    std::shared_ptr<Button> minimize_, maximize_, close_;
    std::vector<std::shared_ptr<Element>> children_;
};
}
