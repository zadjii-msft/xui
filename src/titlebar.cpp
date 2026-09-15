#include "xui/titlebar.hpp"
#include <algorithm>
#include <stdexcept>
namespace xui {
TitleBar::TitleBar(std::wstring title) : Control(ControlRole::content_view, L"Window title bar", {600, height}),
    tabs_(std::make_shared<TabStrip>(L"Title bar tabs")), title_(std::make_shared<Label>(std::move(title))),
    minimize_(std::make_shared<Button>(L"Minimize")), maximize_(std::make_shared<Button>(L"Maximize")),
    close_(std::make_shared<Button>(L"Close window")) {
    secondary_tabs_ = std::make_shared<TabStrip>(L"Secondary title bar tabs");
    leading_ = std::make_shared<Button>(L"Toggle navigation");
    leading_->set_icon(ButtonIcon::menu);
    leading_->set_visible(false);
    secondary_tabs_->set_visible(false);
    children_ = {title_, leading_, tabs_, secondary_tabs_, minimize_, maximize_, close_};
    for (auto& child : children_) adopt(child);
    minimize_->set_automation_id(L"caption-minimize"); maximize_->set_automation_id(L"caption-maximize");
    close_->set_automation_id(L"caption-close"); tabs_->set_automation_id(L"caption-tabs");
    minimize_->set_icon(ButtonIcon::minimize); maximize_->set_icon(ButtonIcon::maximize); close_->set_icon(ButtonIcon::close);
}
TitleBar::~TitleBar() { on_caption({}); }
void TitleBar::set_title(std::wstring title) { title_->set_text(std::move(title)); }
void TitleBar::set_title_visible(bool visible) { title_->set_visible(visible); invalidate(Invalidation::layout); }
void TitleBar::set_tab_panes(const std::shared_ptr<Element>& first, const std::shared_ptr<Element>& second) {
    if (!first || first == second || first.get() == this || second.get() == this)
        throw std::invalid_argument("Title bar tabs require distinct content panes");
    first_pane_ = first; second_pane_ = second; invalidate(Invalidation::layout);
}
void TitleBar::set_maximized(bool value) {
    maximize_->set_name(value ? L"Restore" : L"Maximize"); maximize_->set_icon(value ? ButtonIcon::restore : ButtonIcon::maximize);
}
void TitleBar::on_caption(std::function<void(CaptionAction)> callback) {
    minimize_->on_click(callback ? std::function<void()>{[callback] { callback(CaptionAction::minimize); }} : std::function<void()>{});
    maximize_->on_click(callback ? std::function<void()>{[callback] { callback(CaptionAction::maximize_restore); }} : std::function<void()>{});
    close_->on_click(callback ? std::function<void()>{[callback] { callback(CaptionAction::close); }} : std::function<void()>{});
}
void TitleBar::arrange(Rect b) {
    Element::arrange(b);
    b = bounds();
    const float caption = std::min(caption_width, b.width / 3), remaining = std::max(0.0f, b.width - caption * 3);
    const float button_height = std::min(caption_height, b.height);
    const float leading = leading_->visible() ? std::min(44.0f, remaining) : 0;
    const float title = title_->visible() ? std::min(180.0f, (remaining - leading) / 3) : 0;
    leading_->arrange({b.x, b.y + 3, leading, std::max(0.0f, b.height - 6)});
    title_->arrange({b.x + leading + 8, b.y, std::max(0.0f, title - 8), b.height});
    const float tab_width = std::max(0.0f, remaining - leading - title - 36);
    const float first_width = secondary_tabs_->visible() ? tab_width / 2 : tab_width;
    tabs_->arrange({b.x + leading + title, b.y + 3, first_width, std::max(0.0f, b.height - 3)});
    secondary_tabs_->arrange({b.x + leading + title + first_width, b.y + 3,
        secondary_tabs_->visible() ? tab_width - first_width : 0, std::max(0.0f, b.height - 3)});
    minimize_->arrange({b.x + remaining, b.y, caption, button_height});
    maximize_->arrange({b.x + remaining + caption, b.y, caption, button_height});
    close_->arrange({b.x + remaining + caption * 2, b.y, caption, button_height});
    if (const auto first = first_pane_.lock()) {
        const float content_right = b.x + remaining;
        const auto align = [&](TabStrip& tabs, const std::shared_ptr<Element>& pane) {
            if (!pane || !tabs.visible() || pane->bounds().width <= 0 || pane->bounds().height <= 0) { tabs.arrange({}); return; }
            const auto p = pane->bounds();
            const float left = std::clamp(p.x, b.x + leading, content_right);
            const float right = std::clamp(p.x + p.width, left, content_right);
            tabs.arrange({left, b.y + 3, right - left, std::max(0.0f, b.height - 3)});
        };
        align(*tabs_, first);
        align(*secondary_tabs_, second_pane_.lock());
        const float title_left = b.x + leading;
        const float title_width = title_->visible() ? std::max(0.0f, std::min(180.0f, first->bounds().x - title_left)) : 0;
        title_->arrange({title_left, b.y, title_width, title_->visible() ? b.height : 0});
    }
}
CaptionHit TitleBar::hit_test(Point point) const {
    const auto inside = [point](Rect b) { return point.x >= b.x && point.y >= b.y && point.x < b.x + b.width && point.y < b.y + b.height; };
    if (!inside(bounds())) return CaptionHit::client;
    if (!enabled()) return CaptionHit::client;
    if (inside(minimize_->bounds())) return minimize_->enabled() ? CaptionHit::minimize : CaptionHit::client;
    if (inside(maximize_->bounds())) return maximize_->enabled() ? CaptionHit::maximize : CaptionHit::client;
    if (inside(close_->bounds())) return close_->enabled() ? CaptionHit::close : CaptionHit::client;
    if (leading_->visible() && inside(leading_->bounds())) return CaptionHit::client;
    const auto tab_client = [&](const TabStrip& tabs) {
        auto button = tabs.new_tab_button_bounds();
        button.x += tabs.bounds().x; button.y += tabs.bounds().y;
        return tabs.visible() && inside(tabs.bounds()) &&
            (tabs.hit_test(point.x - tabs.bounds().x) || inside(button));
    };
    if (tab_client(*secondary_tabs_) || tab_client(*tabs_)) return CaptionHit::client;
    return CaptionHit::drag;
}
}
