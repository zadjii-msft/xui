#include "xui/titlebar.hpp"
#include <algorithm>
namespace xui {
TitleBar::TitleBar(std::wstring title) : Control(ControlRole::content_view, L"Window title bar", {600, height}),
    tabs_(std::make_shared<TabStrip>(L"Title bar tabs")), title_(std::make_shared<Label>(std::move(title))),
    minimize_(std::make_shared<Button>(L"Minimize")), maximize_(std::make_shared<Button>(L"Maximize")),
    close_(std::make_shared<Button>(L"Close window")) {
    children_ = {title_, tabs_, minimize_, maximize_, close_};
    for (auto& child : children_) adopt(child);
    minimize_->set_automation_id(L"caption-minimize"); maximize_->set_automation_id(L"caption-maximize");
    close_->set_automation_id(L"caption-close"); tabs_->set_automation_id(L"caption-tabs");
    minimize_->set_icon(ButtonIcon::minimize); maximize_->set_icon(ButtonIcon::maximize); close_->set_icon(ButtonIcon::close);
}
TitleBar::~TitleBar() { on_caption({}); }
void TitleBar::set_title(std::wstring title) { title_->set_text(std::move(title)); }
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
    const float title = std::min(180.0f, remaining / 3);
    title_->arrange({b.x + 8, b.y, std::max(0.0f, title - 8), b.height});
    tabs_->arrange({b.x + title, b.y + 3, std::max(0.0f, remaining - title - 36), std::max(0.0f, b.height - 6)});
    minimize_->arrange({b.x + remaining, b.y, caption, button_height});
    maximize_->arrange({b.x + remaining + caption, b.y, caption, button_height});
    close_->arrange({b.x + remaining + caption * 2, b.y, caption, button_height});
}
CaptionHit TitleBar::hit_test(Point point) const {
    const auto inside = [point](Rect b) { return point.x >= b.x && point.y >= b.y && point.x < b.x + b.width && point.y < b.y + b.height; };
    if (!inside(bounds())) return CaptionHit::client;
    if (!enabled()) return CaptionHit::client;
    if (inside(minimize_->bounds())) return minimize_->enabled() ? CaptionHit::minimize : CaptionHit::client;
    if (inside(maximize_->bounds())) return maximize_->enabled() ? CaptionHit::maximize : CaptionHit::client;
    if (inside(close_->bounds())) return close_->enabled() ? CaptionHit::close : CaptionHit::client;
    if (inside(tabs_->bounds()) && tabs_->hit_test(point.x - tabs_->bounds().x)) return CaptionHit::client;
    return CaptionHit::drag;
}
}
