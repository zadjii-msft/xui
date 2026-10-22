#include "grid_host.hpp"
#include "context_menu.hpp"
#include "file_transfer.hpp"
#include "wheel.hpp"
#include <windowsx.h>

namespace xui {

GridHostPeer::GridHostPeer(PeerHostServices& owner, std::shared_ptr<DataGrid> value)
    : HostPeer(owner, HostFamily::grid, value), grid_(*value) {}

void GridHostPeer::publish_accessibility() {
    publish_grid(accessibility_, provider, grid_, window);
}
void GridHostPeer::disconnect_accessibility() {
    disconnect_row_accessibility(accessibility_, provider);
}
void GridHostPeer::sync(UINT, const Palette&) { publish_accessibility(); }
void GridHostPeer::detach_owner() {
    wheel_.active = false;
    if (row_images_) row_images_->clear();
}
void GridHostPeer::wheel_scroll(double delta, bool precise) {
    const auto value = wheel_.scroll(grid_.offset(), grid_.maximum_offset(),
        grid_.source(), delta, precise, host.wheel_animation_allowed());
    grid_.set_offset(value, grid_.horizontal_offset());
    if (wheel_.active) host.invalidate_peer_paint();
}
bool GridHostPeer::advance_wheel(Animation::Clock::time_point now, bool settle, bool tick) {
    if (const auto value = wheel_.advance(grid_.offset(), grid_.source().get(), now, settle, tick)) {
        grid_.set_offset(*value, grid_.horizontal_offset());
        wheel_.last = grid_.offset();
    }
    return wheel_.active;
}
std::size_t GridHostPeer::metric(PeerMetric metric) const {
    return metric == PeerMetric::retained_images && row_images_ ? row_images_->count() : 0;
}

GridHostPeer::~GridHostPeer() {
    host.forget_peer(*this);
    if (file_target && window) RevokeDragDrop(window);
    file_target.Reset();
    detach_owner();
    retire_native();
    disconnect_accessibility();
}
void GridHostPeer::attach(HWND parent_window, int id) {
    window = host.create_input_surface(*this, parent_window, id);
    publish_grid(accessibility_, nullptr, grid_, window);
    provider = create_grid_provider(accessibility_);
    const std::weak_ptr<PeerHostServices> weak = host.acquire_owner_lease();
    const std::weak_ptr<DataGrid> weak_grid = std::dynamic_pointer_cast<DataGrid>(control);
    file_target = files::drop_target(window, std::dynamic_pointer_cast<DataGrid>(control), [weak, weak_grid, hwnd = window] {
        const auto owner = weak.lock();
        const auto grid = weak_grid.lock();
        return owner && grid && owner->file_drag_eligible(*grid, hwnd);
    }, [weak](std::function<void()> callback) {
        if (const auto owner = weak.lock()) owner->deliver_file_drop(std::move(callback));
    });
}
void GridHostPeer::cancel_input() { grid_drag = 0; grid_.cancel(); }

void GridHostPeer::paint(const HostPaintContext& context) {
    auto& canvas = context.canvas;
    const auto& palette = context.palette;
    auto& control = *this->control;
    const auto bounds = paint_bounds;
    const bool fluent = palette.style == VisualStyle::winui;
    const auto foundation_text = context.enabled ? palette.text : palette.disabled;
    auto* grid = &grid_;
    if (grid->has_control_styling()) {
        const auto g = grid->geometry();
        const auto view = g.viewport();
        const auto grid_root_values = grid->part_style(StylePart::root);
        const auto ink = [&](const PartStyleValues& values, D2D1_COLOR_F fallback) {
            return Drawing::style_foreground(values, palette, fallback);
        };
        const auto inset = [](Rect box, const PartStyleValues& values, Insets fallback) {
            const auto p = values.padding.value_or(fallback), b = values.border_thickness.value_or(Insets{});
            const auto left = p.left + b.left, top = p.top + b.top;
            return Rect{box.x + left, box.y + top, std::max(0.0f, box.width - left - p.right - b.right),
                std::max(0.0f, box.height - top - p.bottom - b.bottom)};
        };
        const auto text = [&](std::wstring_view value, Rect box, PartStyleValues values, D2D1_COLOR_F fallback, bool numeric) {
            if (numeric && !values.horizontal_alignment) values.horizontal_alignment = StyleAlignment::end;
            canvas.styled_text(value, box, ink(values, fallback), values, TextStyle::caption);
        };
        const auto check = [&](Rect box, const PartStyleValues& values, bool checked, bool mixed) {
            if (values.empty() || palette.high_contrast) {
                canvas.check_indicator(box, palette, checked, context.enabled, mixed);
                return;
            }
            const auto fallback = checked || mixed ? palette.accent : palette.field;
            canvas.styled_surface(box, palette, values, fallback, palette.border, 2, {1, 1, 1, 1});
            if (checked || mixed) canvas.text(mixed ? L"\u2212" : L"\u2713", inset(box, values, {}),
                ink(values, checked || mixed ? palette.selection_text : foundation_text));
        };
        canvas.styled_surface({0, 0, bounds.width, bounds.height}, palette, grid_root_values,
            fluent ? palette.surface : palette.background, palette.border, fluent ? 4.0f : 0.0f,
            fluent ? Insets{1, 1, 1, 1} : Insets{});
        const auto [first, end] = grid->visible_rows();
        const auto& source = grid->source();
        canvas.push_clip(view);
        for (auto row = first; row < end; ++row) {
            const auto state = grid->row_style_states(row, context.enabled, grid_drag != 0);
            const bool selected = (state & style_states::selected) != 0, hovered = (state & style_states::hovered) != 0;
            const auto values = grid->row_style(StylePart::row, row, context.enabled, grid_drag != 0);
            const auto row_box = g.row(row);
            auto face = row_box;
            if (fluent) face = {face.x + 2, face.y + 2, std::max(0.0f, face.width - 4), std::max(0.0f, face.height - 4)};
            canvas.styled_surface(face, palette, values,
                selected ? palette.selection : hovered ? palette.hover : !fluent && row % 2 ? palette.surface : D2D1::ColorF(0, 0.0f),
                palette.border, fluent ? 4.0f : 0.0f, {});
            if (fluent && selected) canvas.rounded({view.x + 2, row_box.y + 9, 3, std::max(0.0f, row_box.height - 18)}, palette.accent, 1.5f);
            for (std::size_t c = 0; c < grid->columns().size(); ++c) {
                const auto source_column = grid->source_column(c);
                const auto cell = grid->cell_bounds(row, source_column);
                if (cell.x + cell.width <= view.x || cell.x >= view.x + view.width) continue;
                const auto& column = grid->columns()[c];
                canvas.push_clip(cell);
                auto content = inset(cell, values, {12, 0, 12, 0});
                const auto line = grid->row_style(StylePart::grid_line, row, context.enabled, grid_drag != 0);
                if (!line.empty()) {
                    content.width = std::max(0.0f, content.width - line.thickness.value_or(1));
                    content.height = std::max(0.0f, content.height - line.thickness.value_or(1));
                }
                const auto cell_values = grid->row_style(StylePart::cell, row, context.enabled, grid_drag != 0);
                const auto fallback = !context.enabled ? palette.disabled : selected ? palette.selection_text : palette.text;
                if (column.checkable) {
                    check({cell.x + 10, cell.y + (cell.height - 16) / 2, 16, 16},
                        grid->row_style(StylePart::indicator, row, context.enabled, grid_drag != 0), selected, false);
                    content.x += 24; content.width = std::max(0.0f, content.width - 24);
                }
                if (source_column == 0 && row_images_) {
                    const auto item_key = source->key(row);
                    const auto visual = row_images_->visual(item_key);
                    if (visual.icon != ButtonIcon::none || !visual.image_path.empty()) {
                        canvas.item_visual(visual, row_images_->pixels(item_key),
                            {content.x, content.y + (content.height - 24) / 2, 24, 24}, ink(cell_values, fallback),
                            false, row_images_->loading(item_key));
                        content.x += 32; content.width = std::max(0.0f, content.width - 32);
                    }
                }
                text(source->text(row, source_column), content, cell_values, fallback, column.numeric);
                if (!line.empty() && line.thickness.value_or(1) > 0) {
                    const auto thickness = line.thickness.value_or(1);
                    const auto right = cell.x + cell.width - thickness / 2, bottom = cell.y + cell.height - thickness / 2;
                    canvas.line(right, cell.y, right, cell.y + cell.height, ink(line, palette.border), thickness);
                    canvas.line(cell.x, bottom, cell.x + cell.width, bottom, ink(line, palette.border), thickness);
                }
                canvas.pop_clip();
            }
            if (state & style_states::focused) canvas.focus_ring({row_box.x + 1, row_box.y + 1,
                std::max(0.0f, row_box.width - 2), std::max(0.0f, row_box.height - 2)}, palette);
            else if (hovered && palette.high_contrast) canvas.outline(face, palette.text);
        }
        if (!source || !source->size()) canvas.text(L"No matching rows",
            {view.x + 16, view.y + 14, std::max(0.0f, view.width - 32), 40}, palette.secondary);
        canvas.pop_clip();
        canvas.push_clip(g.header());
        canvas.fill(g.header(), palette.surface);
        for (std::size_t c = 0; c < grid->columns().size(); ++c) {
            const auto column = grid->source_column(c);
            const auto box = g.column(grid->columns(), c);
            const bool dragging = grid_drag >= 3 && grid_column == c;
            const auto state = grid->header_style_states(column, context.enabled, dragging);
            const auto values = grid->header_style(StylePart::header, column, context.enabled, dragging);
            canvas.push_clip(box);
            canvas.styled_surface(box, palette, values, palette.surface, palette.border, 0, {});
            auto content = inset(box, values, {12, 0, 12, 0});
            const auto line = grid->header_style(StylePart::grid_line, column, context.enabled, dragging);
            const auto thickness = line.thickness.value_or(1);
            content.width = std::max(0.0f, content.width - thickness);
            content.height = std::max(0.0f, content.height - thickness);
            const auto& definition = grid->columns()[c];
            if (definition.checkable) { content.x += 24; content.width = std::max(0.0f, content.width - 24); }
            if (definition.filterable) content.width = std::max(0.0f, content.width - 20);
            if (state & style_states::sorted) {
                const auto sort = grid->header_style(StylePart::sort_icon, column, context.enabled, dragging);
                canvas.symbol(grid->descending() ? Symbol::down : Symbol::up,
                    {content.x + std::max(0.0f, content.width - 20), content.y, 20, content.height}, ink(sort, foundation_text), 12);
                content.width = std::max(0.0f, content.width - 20);
            }
            text(definition.name, content, values, context.enabled ? palette.secondary : palette.disabled, definition.numeric);
            if (definition.checkable) check({box.x + 10, box.y + (box.height - 16) / 2, 16, 16},
                grid->header_style(StylePart::indicator, column, context.enabled, dragging),
                (state & style_states::checked) != 0, (state & style_states::mixed) != 0);
            if (definition.filterable) {
                const auto filter = grid->header_style(StylePart::filter_icon, column, context.enabled, dragging);
                canvas.symbol(Symbol::filter, grid->header_part_bounds(column, GridHeaderPart::filter),
                    ink(filter, state & style_states::filtered ? palette.accent : palette.secondary), 12);
            }
            if (thickness > 0) {
                const auto right = box.x + box.width - thickness / 2, bottom = box.y + box.height - thickness / 2;
                canvas.line(right, box.y + 8, right, box.y + std::max(8.0f, box.height - 8), ink(line, palette.border), thickness);
                canvas.line(box.x, bottom, box.x + box.width, bottom, ink(line, palette.border), thickness);
            }
            if (state & style_states::focused) canvas.focus_ring(grid->header_part_bounds(column, grid->header_part()), palette);
            canvas.pop_clip();
        }
        if (grid_drag == 5) {
            float marker = g.left - static_cast<float>(grid->horizontal_offset());
            for (std::size_t c = 0; c < std::min(grid_drop, grid->columns().size()); ++c) marker += grid->columns()[c].width;
            const auto values = grid->part_style(StylePart::reorder_marker, style_states::dragging);
            const auto thickness = values.thickness.value_or(3);
            if (thickness > 0) canvas.fill({marker - thickness / 2, g.top + 2, thickness, std::max(0.0f, g.header_height - 4)}, ink(values, palette.accent));
        }
        canvas.pop_clip();
        for (bool vertical : {true, false}) {
            const bool dragging = grid_drag == (vertical ? 1 : 2);
            const auto state = dragging ? style_states::dragging : 0;
            const auto track = grid->part_style(StylePart::scrollbar_track, state);
            canvas.styled_surface(vertical ? g.vertical_track() : g.horizontal_track(), palette, track,
                ink(track, D2D1::ColorF(0, 0.0f)), palette.border, 0, {});
            const auto thumb = vertical ? grid->vertical_thumb() : grid->horizontal_thumb();
            if (thumb.width <= 0 || thumb.height <= 0) continue;
            const auto values = grid->part_style(StylePart::scrollbar_thumb, state);
            if (values.empty()) canvas.scrollbar_thumb(thumb, palette, dragging, context.enabled);
            else canvas.styled_surface(thumb, palette, values, ink(values, context.enabled ? palette.secondary : palette.disabled), palette.border, 3, {});
        }
        return;
    }
    canvas.fill({0, 0, bounds.width, bounds.height}, fluent ? palette.surface : palette.background);
    canvas.push_clip({0, 0, grid->viewport_width(), bounds.height});
    const auto [first, end] = grid->visible_rows();
    const auto& source = grid->source();
    const auto hovered = context.enabled && !grid_drag ? grid->hovered_row() : std::optional<std::size_t>{};
    canvas.push_clip({0, DataGrid::header_height, grid->viewport_width(), grid->viewport_height()});
    for (auto row = first; row < end; ++row) {
        const float y = DataGrid::header_height + static_cast<float>(row * double(DataGrid::row_height) - grid->offset());
        const bool selected = grid->selection().contains(source->key(row));
        if (fluent) {
            if (selected) {
                canvas.rounded({2, y + 2, std::max(0.0f, grid->viewport_width() - 4), DataGrid::row_height - 4}, palette.selection, 4);
                canvas.rounded({2, y + 9, 3, DataGrid::row_height - 18}, palette.accent, 1.5f);
            } else if (hovered == row)
                canvas.rounded({2, y + 2, std::max(0.0f, grid->viewport_width() - 4), DataGrid::row_height - 4}, palette.hover, 4);
        } else if (selected || hovered == row || row % 2) canvas.fill({0, y, grid->viewport_width(), DataGrid::row_height},
            selected ? palette.selection : hovered == row ? palette.hover : palette.surface);
        float x = -static_cast<float>(grid->horizontal_offset());
        for (std::size_t c = 0; c < grid->columns().size(); ++c) {
            const auto& column = grid->columns()[c];
            if (x + column.width > 0 && x < grid->viewport_width()) {
                const auto ink = !context.enabled ? palette.disabled : selected ? palette.selection_text : palette.text;
                if (column.checkable) {
                    if (fluent) canvas.check_indicator({x + 10, y + 8, 16, 16}, palette, selected, context.enabled);
                    else {
                        canvas.rounded({x + 10, y + 8, 16, 16}, ink, 2, true);
                        if (selected) canvas.text(L"\u2713", {x + 10, y, 20, DataGrid::row_height}, ink);
                    }
                }
                float left = column.checkable ? 36.0f : 12.0f;
                canvas.push_clip({x, y, column.width, DataGrid::row_height});
                if (grid->source_column(c) == 0 && row_images_) {
                    const auto row_key = source->key(row);
                    const auto visual = row_images_->visual(row_key);
                    if (visual.icon != ButtonIcon::none || !visual.image_path.empty()) {
                        canvas.item_visual(visual, row_images_->pixels(row_key), {x + left, y + 4, 24, 24}, ink,
                            false, row_images_->loading(row_key));
                        left += 32;
                    }
                }
                canvas.cell_text(source->text(row, grid->source_column(c)), {x + left, y,
                    std::max(0.0f, column.width - left - 12), DataGrid::row_height}, ink, column.numeric);
                canvas.pop_clip();
            }
            x += column.width;
        }
        if (grid->selected() == source->key(row) && control.focused() && !grid->header_focus()) {
            const Rect face{1, y + 1, std::max(0.0f, grid->viewport_width() - 2), DataGrid::row_height - 2};
            if (fluent) canvas.focus_ring(face, palette);
            else canvas.outline(face, palette.accent);
        } else if (!selected && hovered == row && palette.high_contrast)
            canvas.outline({1, y + 1, std::max(0.0f, grid->viewport_width() - 2), DataGrid::row_height - 2}, palette.text);
    }
    if (!source || !source->size()) canvas.text(L"No matching rows", {16, 52, grid->viewport_width() - 32, 40}, palette.secondary);
    canvas.pop_clip();
    canvas.fill({0, 0, grid->viewport_width(), DataGrid::header_height}, palette.surface);
    float x = -static_cast<float>(grid->horizontal_offset());
    for (std::size_t c = 0; c < grid->columns().size(); ++c) {
        const auto& column = grid->columns()[c];
        const bool sorted = grid->source_column(c) == grid->sort_column();
        const float sort_space = fluent && sorted ? 20.0f : 0.0f;
        const float text_right = x + column.width - (column.filterable ? 32 : 12);
        canvas.cell_text(column.name + (!fluent && sorted ? (grid->descending() ? L" \u2193" : L" \u2191") : L""),
            {x + (column.checkable ? 36 : 12), 0, std::max(0.0f, text_right - x - (column.checkable ? 36 : 12) - sort_space),
            DataGrid::header_height}, palette.secondary, column.numeric);
        if (fluent && sorted)
            canvas.symbol(grid->descending() ? Symbol::down : Symbol::up,
                {text_right - 20, 0, 20, DataGrid::header_height}, foundation_text, 12);
        if (column.checkable) {
            const auto state = grid->check_state();
            if (fluent) canvas.check_indicator({x + 10, 11, 16, 16}, palette, state == SelectionState::all,
                context.enabled, state == SelectionState::mixed);
            else {
                canvas.rounded({x + 10, 11, 16, 16}, foundation_text, 2, true);
                if (state != SelectionState::none) canvas.text(state == SelectionState::all ? L"\u2713" : L"\u2212", {x + 10, 0, 20, DataGrid::header_height}, foundation_text);
            }
        }
        if (column.filterable) {
            const auto ink = grid->filters()[grid->source_column(c)].empty() ? palette.secondary : palette.accent;
            const Rect filter{x + column.width - 28, 0, 24, DataGrid::header_height};
            if (fluent) canvas.symbol(Symbol::filter, filter, ink, 12);
            else canvas.text(L"\u25bd", filter, ink);
        }
        canvas.line(x + column.width, 8, x + column.width, DataGrid::header_height - 8, palette.border);
        if (control.focused() && grid->header_focus() && grid->focused_column() == c) {
            const auto b = grid->header_part_bounds(grid->source_column(c), grid->header_part());
            const Rect face{b.x + 1, 1, std::max(0.0f, b.width - 2), DataGrid::header_height - 2};
            if (fluent) canvas.focus_ring(face, palette);
            else canvas.outline(face, palette.accent);
        }
        x += column.width;
    }
    if (grid_drag == 5) {
        float marker = -static_cast<float>(grid->horizontal_offset());
        for (std::size_t c = 0; c < std::min(grid_drop, grid->columns().size()); ++c) marker += grid->columns()[c].width;
        marker = std::clamp(marker, 2.0f, std::max(2.0f, grid->viewport_width() - 2));
        canvas.fill({marker - 1, 2, 3, DataGrid::header_height - 4}, palette.accent);
    }
    canvas.pop_clip();
    canvas.line(0, DataGrid::header_height, grid->viewport_width(), DataGrid::header_height, palette.border);
    const auto vertical = grid->vertical_thumb(), horizontal = grid->horizontal_thumb();
    if (fluent) {
        if (vertical.height) canvas.scrollbar_thumb(vertical, palette, grid_drag == 1, context.enabled);
        if (horizontal.width) canvas.scrollbar_thumb(horizontal, palette, grid_drag == 2, context.enabled);
        canvas.rounded({0.5f, 0.5f, std::max(0.0f, bounds.width - 1), std::max(0.0f, bounds.height - 1)}, palette.border, 4, true);
    } else {
        if (vertical.height) canvas.rounded(vertical, palette.secondary, 3);
        if (horizontal.width) canvas.rounded(horizontal, palette.secondary, 3);
    }
    return;
}

bool GridHostPeer::image_work(RowImageWork& work) {
    if (!row_images_) row_images_ = std::make_unique<RowImages>();
    auto* grid = &grid_;
    auto rect = work.clip;
    rect.x -= control->bounds().x; rect.y -= control->bounds().y;
    std::vector<RowVisual> rows;
    std::shared_ptr<const CollectionIndex> source;
    if (work.shown) {
        source = grid->source();
        const auto column = grid->display_column(0);
        float x = grid->geometry().left - static_cast<float>(grid->horizontal_offset());
        if (column) for (size_t i = 0; i < *column; ++i) x += grid->columns()[i].width;
        if (column && x < rect.x + rect.width && x + grid->columns()[*column].width > rect.x) {
            const auto [begin, end] = grid->visible_rows();
            for (auto row = begin; row < end && rows.size() < RowImages::maximum_rows; ++row) {
                const auto box = grid->row_bounds(row);
                if (box.y + box.height <= rect.y || box.y >= rect.y + rect.height) continue;
                rows.push_back({grid->source()->key(row), grid->source()->visual(row, 0)});
            }
        }
    }
    work.clip = rect;
    work.source = std::move(source); work.rows = std::move(rows);
    return true;
}
bool GridHostPeer::sync_images(RowImageWork work, UINT dpi, const std::shared_ptr<TaskWake>& wake,
    std::vector<std::uint64_t>& retained, std::size_t&) {
    return row_images_->sync(std::move(work.source), std::move(work.rows), dpi, wake, retained);
}

HRESULT GridHostPeer::accessibility_action(WPARAM wparam, LPARAM lparam) {
    if (wparam != control->id()) return UIA_E_ELEMENTNOTAVAILABLE;
    GridAction action;
    if (!take_row_action(accessibility_, control->id(), window, static_cast<std::uint64_t>(lparam), action))
        return UIA_E_ELEMENTNOTAVAILABLE;
    if (!host.input_enabled(*this) || !host.input_visible(*this)) return UIA_E_ELEMENTNOTENABLED;
    auto* grid = &grid_;
    if (action.kind == GridAction::filter) { grid->filter(action.column, action.text); host.update_peer_state(); return S_OK; }
    if (action.kind == GridAction::filter_open) { grid->open_filter(action.column); host.update_peer_state(); return S_OK; }
    if (action.kind == GridAction::check) { grid->toggle_check(action.key); host.update_peer_state(); return S_OK; }
    if (action.kind == GridAction::add || action.kind == GridAction::remove) {
        if (!action.key || !grid->source() || !grid->source()->find(*action.key)) return UIA_E_ELEMENTNOTAVAILABLE;
        if (grid->selection().contains(*action.key) != (action.kind == GridAction::add)) grid->toggle_check(action.key);
        host.update_peer_state(); return S_OK;
    }
    if (action.kind == GridAction::scroll) grid->set_offset(action.y, action.x);
    else if (action.kind == GridAction::reveal) {
        if (!action.key || !grid->reveal(*action.key)) return UIA_E_ELEMENTNOTAVAILABLE;
    } else if (action.kind == GridAction::clear) {
        if (!action.key || !grid->source() || !grid->source()->find(*action.key)) return UIA_E_ELEMENTNOTAVAILABLE;
        if (grid->selection().contains(*action.key)) grid->toggle_check(action.key);
    }
    else if (action.kind == GridAction::sort) {
        if (action.column >= grid->columns().size()) return UIA_E_ELEMENTNOTAVAILABLE;
        grid->sort(action.column);
    } else if (action.kind == GridAction::header_focus) {
        const auto column = grid->display_column(action.column);
        if (!column) return UIA_E_ELEMENTNOTAVAILABLE;
        if (!host.focus_peer(*this, true)) return UIA_E_INVALIDOPERATION;
        grid->focus_header(true);
        grid->step_header(static_cast<int>(*column) - static_cast<int>(grid->focused_column()));
        grid->set_header_part(static_cast<GridHeaderPart>(static_cast<int>(action.x)));
    } else if (action.kind == GridAction::focus && !action.key) {
        if (!host.focus_peer(*this, true)) return UIA_E_INVALIDOPERATION;
    } else {
        if (!action.key || !grid->select(*action.key, action.kind == GridAction::focus ?
            SelectionGesture::focus_only : SelectionGesture::replace)) return UIA_E_ELEMENTNOTAVAILABLE;
        if (action.kind == GridAction::focus && !host.focus_peer(*this, true)) return UIA_E_INVALIDOPERATION;
        if (action.kind == GridAction::invoke) grid->activate_selected();
    }
    host.update_peer_state(); return S_OK;
}

std::optional<LRESULT> GridHostPeer::input(UINT message, WPARAM wparam, LPARAM lparam,
    const HostInputContext& context) {
    auto& control = *this->control;
    auto* grid = &grid_;
    const auto hwnd = window;
    const auto dpi = context.dpi;
    switch (message) {
    case WM_SETCURSOR:
        if (host.input_enabled(*this) && LOWORD(lparam) == HTCLIENT) {
            POINT point{}; GetCursorPos(&point); ScreenToClient(hwnd, &point);
            const float x = point.x * 96.0f / dpi, y = point.y * 96.0f / dpi;
            const auto cursor = grid_drag == 3 || (y >= grid->geometry().top && y < grid->geometry().header_bottom() && grid->resize_boundary(x)) ?
                IDC_SIZEWE : grid_drag == 5 ? IDC_SIZEALL : IDC_ARROW;
            SetCursor(LoadCursorW(nullptr, cursor)); return TRUE;
        }
        return {};
    case WM_MOUSEWHEEL: {
        if (!host.input_enabled(*this)) return 0;
        const int delta = GET_WHEEL_DELTA_WPARAM(wparam);
        const double steps = static_cast<double>(delta) / WHEEL_DELTA;
        if (GET_KEYSTATE_WPARAM(wparam) & MK_SHIFT)
            grid->set_offset(grid->offset(), grid->horizontal_offset() - steps * 96);
        else wheel_scroll(-vertical_wheel_dips(wparam, grid->viewport_height()),
            std::abs(delta) < WHEEL_DELTA);
        return 0;
    }
    case WM_MOUSEHWHEEL:
        grid->set_offset(grid->offset(), grid->horizontal_offset() + GET_WHEEL_DELTA_WPARAM(wparam) * 0.8);
        return 0;
    case WM_CONTEXTMENU: {
        grid->hover_pointer({});
        SetFocus(hwnd);
        std::optional<Point> position;
        if (lparam != -1) {
            POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)}; ScreenToClient(hwnd, &point);
            position = Point{point.x * 96.0f / dpi, point.y * 96.0f / dpi};
        }
        grid->prepare_context_menu(position);
        show_control_menu(control, hwnd, lparam, context.palette, dpi);
        return 0;
    }
    case WM_KILLFOCUS:
        grid_drag = 0;
        return {};
    case WM_MOUSEMOVE: {
        if (grid_drag == 6) {
            const Point point{GET_X_LPARAM(lparam) * 96.0f / dpi, GET_Y_LPARAM(lparam) * 96.0f / dpi};
            if (!(wparam & MK_LBUTTON) || !host.input_enabled(*this) || !host.input_visible(*this)) {
                grid_drag = 0; grid->end_file_press(false);
                if (GetCapture() == hwnd) ReleaseCapture();
                return 0;
            }
            const Size threshold{GetSystemMetricsForDpi(SM_CXDRAG, dpi) * 96.0f / dpi,
                GetSystemMetricsForDpi(SM_CYDRAG, dpi) * 96.0f / dpi};
            if (!grid->file_drag_threshold(point, threshold)) return 0;
            grid_drag = 0; grid->end_file_press(false);
            if (GetCapture() == hwnd) ReleaseCapture();
            const auto paths = grid->file_drag_paths();
            if (!paths.empty() && host.owner_open() && IsWindow(hwnd)) {
                const auto owner = host.acquire_owner_lease();
                const auto retained_grid = std::dynamic_pointer_cast<DataGrid>(this->control);
                const auto effect = files::drag(hwnd, paths, [owner, retained_grid, hwnd] {
                    return !owner->file_drag_eligible(*retained_grid, hwnd);
                });
                if (owner->owner_open() && IsWindow(hwnd)) retained_grid->file_drag_completed(effect);
            }
            return 0;
        }
        const bool hovering = host.input_enabled(*this) && !grid_drag && !GetCapture() &&
            !(wparam & (MK_LBUTTON | MK_RBUTTON | MK_MBUTTON));
        grid->hover_pointer(hovering ? std::optional{Point{GET_X_LPARAM(lparam) * 96.0f / dpi,
            GET_Y_LPARAM(lparam) * 96.0f / dpi}} : std::nullopt);
        if (grid_drag) {
            auto& grid = grid_;
            const float x = GET_X_LPARAM(lparam) * 96.0f / dpi, y = GET_Y_LPARAM(lparam) * 96.0f / dpi;
            if (grid_drag == 3) grid.resize_column(grid_column, drag_offset + x - drag_y);
            else if (grid_drag >= 4) {
                if (grid_drag == 4 && (std::abs(x - drag_y) >= 6 || std::abs(y - drag_offset) >= 6))
                    grid_drag = 5;
                if (grid_drag == 5) {
                    const auto view = grid.geometry().viewport();
                    if (x < view.x + 24 || x > view.x + view.width - 24)
                        grid.set_offset(grid.offset(), grid.horizontal_offset() + (x < view.x + 24 ? -32 : 32));
                    float edge = view.x - static_cast<float>(grid.horizontal_offset());
                    grid_drop = 0;
                    for (const auto& column : grid.columns()) {
                        if (x < edge + column.width / 2) break;
                        edge += column.width; ++grid_drop;
                    }
                    SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
                    host.invalidate_peer_paint();
                }
            }
            else if (grid_drag == 1) {
                const float track = grid.viewport_height() - grid.vertical_thumb().height;
                if (track > 0) grid.set_offset(drag_offset + (y - drag_y) * grid.maximum_offset() / track, grid.horizontal_offset());
            } else {
                const float track = grid.viewport_width() - grid.horizontal_thumb().width;
                if (track > 0) grid.set_offset(grid.offset(), drag_offset + (x - drag_y) * grid.maximum_horizontal() / track);
            }
            return 0;
        }

        control.pointer_move(row_pointer_inside(hwnd, lparam));
        track_pointer_leave();
        return 0;
    }
    case WM_MOUSELEAVE:
        grid->hover_pointer({});
        tracking = false;
        control.pointer_move(false);
        return 0;
    case WM_LBUTTONDOWN: {
        wheel_.active = false;
        grid->hover_pointer({});
        SetFocus(hwnd);
        const float x = GET_X_LPARAM(lparam) * 96.0f / dpi, y = GET_Y_LPARAM(lparam) * 96.0f / dpi;
        const auto g = grid->geometry();
        const auto vertical_track = g.vertical_track(), horizontal_track = g.horizontal_track();
        const auto contains = [x, y](Rect box) { return x >= box.x && x < box.x + box.width && y >= box.y && y < box.y + box.height; };
        if (contains(vertical_track)) {
            const auto thumb = grid->vertical_thumb();
            if (thumb.height && y >= thumb.y && y < thumb.y + thumb.height) {
                grid_drag = 1; drag_y = y; drag_offset = static_cast<float>(grid->offset()); SetCapture(hwnd);
            } else grid->set_offset(grid->offset() + (y < thumb.y ? -grid->viewport_height() : grid->viewport_height()), grid->horizontal_offset());
        } else if (contains(horizontal_track)) {
            const auto thumb = grid->horizontal_thumb();
            if (thumb.width && x >= thumb.x && x < thumb.x + thumb.width) {
                grid_drag = 2; drag_y = x; drag_offset = static_cast<float>(grid->horizontal_offset()); SetCapture(hwnd);
            } else grid->set_offset(grid->offset(), grid->horizontal_offset() + (x < thumb.x ? -grid->viewport_width() : grid->viewport_width()));
        } else if (contains(g.header())) {
            if (const auto c = grid->resize_boundary(x)) {
                grid_drag = 3; grid_column = *c; drag_y = x; drag_offset = grid->columns()[*c].width;
                SetCapture(hwnd); return 0;
            }
            if (const auto column = grid->column_at(x)) {
                grid->focus_header(true); grid->step_header(static_cast<int>(*column) - static_cast<int>(grid->focused_column()));
                const auto part = grid->header_part_at(x); grid->set_header_part(part);
                if (part == GridHeaderPart::check) { grid->toggle_check(); return 0; }
                if (part == GridHeaderPart::filter) { grid->open_filter(grid->source_column(*column)); return 0; }
                grid_drag = 4; grid_column = *column; grid_drop = *column;
                drag_y = x; drag_offset = y; SetCapture(hwnd);
            }
        } else if (const auto row = contains(g.viewport()) ? grid->row_at(y) : std::nullopt) {
            const auto column = grid->column_at(x);
            if (column && grid->columns()[*column].checkable && grid->header_part_at(x) == GridHeaderPart::check)
                grid->toggle_check(grid->source()->key(*row));
            else {
                const auto selection_gesture = (wparam & MK_SHIFT) ?
                    ((wparam & MK_CONTROL) ? SelectionGesture::add_range : SelectionGesture::extend) :
                    (wparam & MK_CONTROL) ? SelectionGesture::toggle : SelectionGesture::replace;
                if (grid->file_drag_enabled()) {
                    const auto source = grid->source();
                    const auto pressed = source->key(*row);
                    if (grid->begin_file_press({x, y}, selection_gesture)) {
                        grid_single_click = selection_gesture == SelectionGesture::replace;
                        grid_press_source = source;
                        grid_press_key = pressed;
                        grid_drag = 6; SetCapture(hwnd);
                    }
                } else {
                    const auto source = grid->source();
                    const auto pressed = source->key(*row);
                    grid->select(pressed, selection_gesture, false);
                    if (grid->single_click_activation() && !(wparam & (MK_CONTROL | MK_SHIFT)) &&
                        host.owner_open() && host.input_visible(*this) && host.input_enabled(*this) && grid->source() == source &&
                        grid->selected() == pressed)
                        grid->activate_selected();
                }
            }
        }
        else grid->clear_selection();
        return 0;
    }
    case WM_LBUTTONUP: {
        if (grid_drag == 6) {
            grid_drag = 0;
            auto& grid = static_cast<DataGrid&>(control);
            const auto source = grid_press_source.lock();
            const auto pressed = std::exchange(grid_press_key, {});
            grid_press_source.reset();
            const bool current = source && source == grid.source() && pressed &&
                grid.pending_file_press() == pressed && grid.selected() == pressed;
            grid.end_file_press(current && host.input_enabled(*this) && row_pointer_inside(hwnd, lparam));
            if (GetCapture() == hwnd) ReleaseCapture();
            if (current && control.single_click_activation() && grid_single_click &&
                !(wparam & (MK_CONTROL | MK_SHIFT)) && host.owner_open() && host.input_visible(*this) && host.input_enabled(*this) && row_pointer_inside(hwnd, lparam) &&
                grid.source() == source && grid.selected() == pressed)
                grid.activate_selected();
            return 0;
        }
        if (std::exchange(suppress_popup_click, false)) return 0;
        if (grid_drag >= 4 && host.input_enabled(*this)) {
            const float x = GET_X_LPARAM(lparam) * 96.0f / dpi, y = GET_Y_LPARAM(lparam) * 96.0f / dpi;
            const auto header = grid->geometry().header();
            if (y >= header.y && y < header.y + header.height && x >= header.x && x < header.x + header.width) {
                if (grid_drag == 5) {
                    const auto to = grid_drop - (grid_drop > grid_column ? 1 : 0);
                    grid->reorder_column(grid_column, to);
                    grid->step_header(0);
                } else if (grid_column < grid->columns().size() && grid->column_at(x) == grid_column)
                    grid->sort(grid->source_column(grid_column));
            }
            host.invalidate_peer_paint();
        }
        grid_drag = 0;
        control.pointer_up(row_pointer_inside(hwnd, lparam));
        if (GetCapture() == hwnd) ReleaseCapture();
        return 0;
    }
    case WM_LBUTTONDBLCLK:
        if (host.input_enabled(*this)) {
            if (grid->single_click_activation()) return 0;
            if (auto row = grid->row_at(GET_Y_LPARAM(lparam) * 96.0f / dpi)) {
                grid->select(grid->source()->key(*row), false); grid->activate_selected();
            }
            return 0;
        }
        return 0;
    case WM_CANCELMODE:
    case WM_CAPTURECHANGED:
        cancel_input();
        if (GetCapture() == hwnd) ReleaseCapture();
        if (message == WM_CAPTURECHANGED) host.invalidate_peer_paint();
        return 0;
    case WM_KEYDOWN:
        if (host.input_enabled(*this)) {
            if (wparam == 'A' && GetKeyState(VK_CONTROL) < 0) { grid->select_all(); return 0; }
            if (wparam == VK_ESCAPE && grid_drag) {
                if (grid_drag == 3 && grid_column < grid->columns().size()) grid->set_column_width(grid_column, drag_offset);
                grid_drag = 0;
                if (GetCapture() == hwnd) ReleaseCapture();
                host.invalidate_peer_paint(); return 0;
            }
            if (grid_drag) return 0;
            if (wparam == VK_F6) { grid->focus_header(!grid->header_focus()); return 0; }
            if (grid->header_focus()) {
                if (wparam == VK_F4) {
                    if (!grid->columns().empty()) for (int step = 1; step <= 3; ++step) {
                        const auto next = static_cast<GridHeaderPart>((static_cast<int>(grid->header_part()) + step) % 3);
                        if (grid->header_part_bounds(grid->source_column(grid->focused_column()), next).width) {
                            grid->set_header_part(next); break;
                        }
                    }
                    return 0;
                }
                if (wparam == VK_LEFT || wparam == VK_RIGHT) {
                    const int delta = wparam == VK_LEFT ? -1 : 1;
                    if (GetKeyState(VK_CONTROL) < 0 && !grid->columns().empty()) {
                        const auto column = grid->focused_column();
                        if (GetKeyState(VK_SHIFT) < 0) {
                            const auto to = static_cast<std::size_t>(std::clamp(static_cast<int>(column) + delta, 0, static_cast<int>(grid->columns().size() - 1)));
                            grid->reorder_column(column, to);
                        } else grid->resize_column(column, grid->columns()[column].width + delta * 16);
                        grid->step_header(0);
                    } else grid->step_header(delta);
                    return 0;
                }
                if (wparam == VK_RETURN || wparam == VK_SPACE) {
                    if (!(lparam & (1LL << 30)) && !grid->columns().empty()) {
                        if (grid->header_part() == GridHeaderPart::check) grid->toggle_check();
                        else if (grid->header_part() == GridHeaderPart::filter) grid->open_filter(grid->source_column(grid->focused_column()));
                        else grid->sort(grid->source_column(grid->focused_column()));
                    }
                    return 0;
                }
                if (wparam == VK_DOWN) { grid->focus_header(false); grid->step(0); return 0; }
            } else {
                if (wparam == VK_UP || wparam == VK_DOWN) { grid->step(wparam == VK_UP ? -1 : 1, row_keyboard_gesture()); return 0; }
                if (wparam == VK_PRIOR || wparam == VK_NEXT) { grid->step((wparam == VK_PRIOR ? -1 : 1) * grid->page_rows(), row_keyboard_gesture()); return 0; }
                if (wparam == VK_HOME || wparam == VK_END) { grid->edge(wparam == VK_END, row_keyboard_gesture()); return 0; }
                if (wparam == VK_SPACE) { if (!(lparam & (1LL << 30)) && grid->selected()) grid->toggle_check(grid->selected()); return 0; }
                if (wparam == VK_RETURN) { if (!(lparam & (1LL << 30))) grid->activate_selected(); return 0; }
                if (wparam == VK_LEFT || wparam == VK_RIGHT) { grid->set_offset(grid->offset(), grid->horizontal_offset() + (wparam == VK_LEFT ? -80 : 80)); return 0; }
            }
        }
        return {};
    case grid_action_message: return accessibility_action(wparam, lparam);
    case WM_NCDESTROY:
        if (file_target) { RevokeDragDrop(hwnd); file_target.Reset(); }
        return {};
    default: return {};
    }
}
}
