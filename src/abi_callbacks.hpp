#pragma once
#include "xui/foundation.hpp"
#include <functional>
#include <memory>
#include <string>
#include <variant>

namespace xui::detail::callbacks {
using Action = std::function<void()>;
using Number = std::function<void(double)>;
using Selection = std::function<void(uint64_t)>;
using Dismiss = std::function<void(PopupDismissReason)>;
struct Input {
    std::function<void(const std::wstring&)> change;
    Action submit;
    explicit operator bool() const noexcept { return change || submit; }
};
struct Range {
    Number change, preview, cancel;
    explicit operator bool() const noexcept { return change || preview || cancel; }
};
class Storage {
public:
    template<class T> void capture(T callbacks) {
        if (callbacks) value_ = std::make_unique<T>(std::move(callbacks));
    }
    template<class T> const T* get() const noexcept {
        const auto* value = std::get_if<std::unique_ptr<T>>(&value_);
        return value ? value->get() : nullptr;
    }
private:
    std::variant<std::unique_ptr<Action>, std::unique_ptr<Input>, std::unique_ptr<Range>,
        std::unique_ptr<Number>, std::unique_ptr<Selection>, std::unique_ptr<Dismiss>> value_;
};
static_assert(sizeof(Storage) <= 2 * sizeof(void*));
}
