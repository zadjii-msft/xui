#pragma once

#include <functional>
#include <map>
#include <stdexcept>
#include <string>

namespace xui {

struct Command {
    std::wstring label;
    std::function<bool()> enabled;
    std::function<void()> execute;
};

class Commands {
public:
    void add(unsigned id, Command command) {
        if (!commands_.emplace(id, std::move(command)).second)
            throw std::invalid_argument("Duplicate command ID");
    }
    const Command& get(unsigned id) const { return commands_.at(id); }
    bool invoke(unsigned id) const {
        const auto& command = get(id);
        if (!command.enabled()) return false;
        command.execute();
        return true;
    }
private:
    std::map<unsigned, Command> commands_;
};

}
