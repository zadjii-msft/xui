#pragma once

#include "xui/controls.hpp"
#include <array>
#include <cstdint>
#include <stdexcept>

namespace xui {

enum class Symbol {
    none, menu, home, folder, library, settings, search, back, forward, up, down,
    refresh, split, theme, add, minimize, maximize, restore, close, more,
    chevron_down, chevron_up, chevron_right, check, remove, clear, document,
    filter, information, success, warning, error, caption_close, indeterminate, breadcrumb_separator,
    history, bookmark, drive, open, save, save_as, undo, redo, count
};

// Microsoft Learn's Segoe Fluent Icons registry. Never use PUA glyphs in body text.
inline constexpr auto symbol_codepoints = std::to_array<uint32_t>({
    0, 0xe700, 0xe80f, 0xe8b7, 0xe8f1, 0xe713, 0xe721, 0xe72b, 0xe72a, 0xe74a, 0xe74b,
    0xe72c, 0xe89a, 0xe706, 0xe710, 0xe921, 0xe922, 0xe923, 0xe711, 0xe712,
    0xe70d, 0xe70e, 0xe76c, 0xe73e, 0xe738, 0xe894, 0xe8a5,
    0xe71c, 0xe946, 0xe930, 0xe7ba, 0xea39, 0xe8bb, 0xe9ae, 0xe974,
    0xe81c, 0xe8a4, 0xeda2, 0xe8a7, 0xe74e, 0xe792, 0xe7a7, 0xe7a6
});
static_assert(symbol_codepoints.size() == static_cast<std::size_t>(Symbol::count));

constexpr Symbol button_symbol(ButtonIcon icon) {
    switch (icon) {
    case ButtonIcon::none: return Symbol::none;
    case ButtonIcon::back: return Symbol::back;
    case ButtonIcon::forward: return Symbol::forward;
    case ButtonIcon::up: return Symbol::up;
    case ButtonIcon::refresh: return Symbol::refresh;
    case ButtonIcon::split: return Symbol::split;
    case ButtonIcon::theme: return Symbol::theme;
    case ButtonIcon::add: return Symbol::add;
    case ButtonIcon::minimize: return Symbol::minimize;
    case ButtonIcon::maximize: return Symbol::maximize;
    case ButtonIcon::restore: return Symbol::restore;
    case ButtonIcon::close: return Symbol::close;
    case ButtonIcon::more: return Symbol::more;
    case ButtonIcon::menu: return Symbol::menu;
    case ButtonIcon::home: return Symbol::home;
    case ButtonIcon::folder: return Symbol::folder;
    case ButtonIcon::settings: return Symbol::settings;
    case ButtonIcon::search: return Symbol::search;
    case ButtonIcon::library: return Symbol::library;
    case ButtonIcon::history: return Symbol::history;
    case ButtonIcon::bookmark: return Symbol::bookmark;
    case ButtonIcon::drive: return Symbol::drive;
    case ButtonIcon::save: return Symbol::save;
    case ButtonIcon::save_as: return Symbol::save_as;
    case ButtonIcon::undo: return Symbol::undo;
    case ButtonIcon::redo: return Symbol::redo;
    case ButtonIcon::chevron_up: return Symbol::chevron_up;
    case ButtonIcon::chevron_down: return Symbol::chevron_down;
    case ButtonIcon::open: return Symbol::open;
    }
    throw std::invalid_argument("Invalid button icon");
}

}
