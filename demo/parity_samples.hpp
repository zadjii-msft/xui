#pragma once
#include "xui/commands.hpp"

namespace gallery {
inline std::shared_ptr<const xui::CommandSet> menu_bar_commands(std::function<void(std::wstring)> report) {
    using namespace xui;
    std::vector<CommandRecord> records;
    const auto submenu = [&](CommandId id, CommandId parent, std::wstring label, bool enabled = true) {
        CommandRecord record{id, parent, std::move(label)};
        record.kind = CommandKind::submenu;
        record.enabled = enabled;
        records.push_back(std::move(record));
    };
    submenu(1, 0, L"&File");
    submenu(2, 0, L"&Edit");
    submenu(3, 0, L"&View");
    submenu(4, 0, L"&Publish", false);
    records.push_back({11, 1, L"New document", [report] { report(L"New document requested. No file was created."); }});
    records.push_back({12, 1, L"Save sample", [report] { report(L"Sample saved in memory."); }});
    submenu(13, 1, L"Recent samples");
    records.push_back({131, 13, L"Welcome sample", [report] { report(L"Welcome sample selected."); }});
    records.push_back({21, 2, L"Cut unavailable", {}, false});
    records.push_back({22, 2, L"Copy sample", [report] { report(L"Copy requested. The clipboard was not changed."); }});
    records.push_back({31, 3, L"Show preview", [report] { report(L"Preview requested."); }});
    records.push_back({41, 4, L"Publish sample", {}, false});
    return std::make_shared<const CommandSet>(std::move(records));
}
}
