#pragma once
#include <string>
#include <string_view>
#include <stdexcept>
#include <utility>

namespace xui {
class PathInputError : public std::invalid_argument {
public:
    explicit PathInputError(std::wstring message)
        : std::invalid_argument("Invalid folder path"), message_(std::move(message)) {}
    const std::wstring& message() const { return message_; }
private:
    std::wstring message_;
};
struct PathInput {
    std::wstring text;
    std::wstring error;
};
// Windows environment references use %name%. Values are expanded once, without shell execution.
// The input and result, including their terminators, cannot exceed 32767 characters.
PathInput expand_path_input(std::wstring_view input);
}
