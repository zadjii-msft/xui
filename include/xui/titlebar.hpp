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
    const std::shared_ptr<TabStrip>& secondary_tabs() const { return secondary_tabs_; }
    const std::shared_ptr<Button>& leading() const { return leading_; }
    const std::shared_ptr<Button>& minimize() const { return minimize_; }
    const std::shared_ptr<Button>& maximize() const { return maximize_; }
    const std::shared_ptr<Button>& close() const { return close_; }
    const std::shared_ptr<Label>& title() const { return title_; }
    bool active() const { return active_; }
    bool maximized() const { return maximized_; }
    void set_active(bool active);
    void set_title(std::wstring title);
    void set_title_visible(bool visible);
    bool title_visible() const { return title_->visible(); }
    void set_tab_panes(const std::shared_ptr<Element>& first, const std::shared_ptr<Element>& second = {});
    bool has_tab_panes() const { return !first_pane_.expired(); }
    void set_maximized(bool maximized);
    void on_caption(std::function<void(CaptionAction)> callback);
    void set_button_invoked_handler(std::function<void(const Button&)> handler);
    CaptionHit hit_test(Point client) const;
    std::span<const std::shared_ptr<Element>> retained_children() const override { return children_; }
    void arrange(Rect bounds) override;
    static constexpr float height = 44;
    static constexpr float caption_width = 46;
    static constexpr float caption_height = 32;
private:
    void bind_caption_button(const std::shared_ptr<Button>& button, std::function<void()> callback);
    std::shared_ptr<std::function<void(const Button&)>> button_invoked_;
    std::optional<StyleTarget> control_style_target() const override { return StyleTarget::title_bar; }
    StyleStateMask control_style_state_bits() const override;
    bool active_{true}, maximized_{};
    std::shared_ptr<TabStrip> tabs_;
    std::shared_ptr<TabStrip> secondary_tabs_;
    std::shared_ptr<Button> leading_;
    std::shared_ptr<Label> title_;
    std::shared_ptr<Button> minimize_, maximize_, close_;
    std::weak_ptr<Element> first_pane_, second_pane_;
    std::vector<std::shared_ptr<Element>> children_;
};
}
