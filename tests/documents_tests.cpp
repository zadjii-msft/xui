#include "xui/documents.hpp"
#include <iostream>
#include <stdexcept>
using namespace xui;
namespace {
void require(bool value, const char* text) { if (!value) throw std::runtime_error(text); }
template<class F> void rejects(F action) { bool rejected{}; try { action(); } catch (const std::exception&) { rejected = true; } require(rejected, "Invalid argument rejected"); }
}
int main() {
    try {
        MultilineText text;
        int changes{}; text.on_change([&](const std::wstring&) { ++changes; text.commit_text(L"reentrant"); });
        text.set_text(L"A\r\U0001f642"); require(changes == 0, "Property setters are silent");
        text.set_selection({2, 4}); require(text.selection() == TextSelection{2, 4}, "UTF-16 selection");
        rejects([&] { text.set_selection({2, 3}); });
        rejects([&] { text.set_text(std::wstring(1, 0xd800)); });
        rejects([&] { text.set_text(std::wstring(L"a\0b", 3)); });
        rejects([&] { text.set_maximum_length(1); });
        rejects([&] { text.set_maximum_length(DocumentText::document_limit + 1); });
        text.commit_text(L"Committed"); require(changes == 1 && text.text() == L"Committed", "Exactly once and no nested commit");
        text.set_read_only(true); text.commit_text(L"Blocked"); require(text.text() == L"Committed", "Read-only commit blocked");
        require(!text.command(TextCommand::undo), "Detached native command rejected");
        RichText rich; rich.set_runs({{L"Bold", true}, {L" Link", false, false, true, L"https://example.com"}});
        require(rich.text() == L"Bold Link" && rich.runs().size() == 2, "Styled document retains runs");
        int links{}; rich.on_link([&](const auto&) { ++links; }); rich.activate_link(5); require(links == 1, "Explicit link action");
        rejects([&] { rich.set_runs({{L"Unsafe", false, false, false, L"file:///test"}}); });
        rejects([&] { rich.set_runs({{L"Unsafe", false, false, false, L"https://"}}); });
        rejects([&] { rich.set_runs({{L"Unsafe", false, false, false, L"https://example.com/\rtest"}}); });
        rejects([&] { rich.set_runs(std::vector<TextRun>(4097)); });
        require(rich.text() == L"Bold Link", "Rejected rich document is atomic");
        rich.commit_text(L"New Bold Link"); rich.activate_link(9); require(links == 2, "Native edits move surviving application link offsets");
        PasswordInput password; int secrets{};
        password.on_change([&] { ++secrets; password.commit_password(L"reentrant"); });
        password.set_password(L"Fixture secret"); require(secrets == 0, "Password setter is silent");
        rejects([&] { password.set_revealed(true); });
        password.set_reveal_policy(PasswordRevealPolicy::explicit_request); password.set_revealed(true);
        password.set_reveal_policy(PasswordRevealPolicy::never); require(!password.revealed(), "Policy removes reveal");
        password.commit_password(L"New fixture"); require(secrets == 1 && password.length() == 11, "Password notification has no value");
        password.with_password([&](auto value) { require(value == L"New fixture", "Explicit secret boundary"); });
        DateTimePicker date; int dates{}; date.on_change([&](auto) { ++dates; });
        date.set_value({2024, 2, 29}); date.set_range({2024, 1, 1}, {2024, 12, 31});
        rejects([&] { date.set_value({2023, 2, 29}); });
        rejects([&] { date.set_value({2025, 1, 1}); });
        rejects([&] { date.set_range({2024, 3, 1}, {2024, 12, 31}); });
        require(dates == 0, "Date properties are silent"); date.change_value({2024, 3, 1}); require(dates == 1, "Date action fires once");
        InlineStatus status(L"Saved"); int dismissals{}; status.on_dismiss([&] { ++dismissals; });
        status.set_dismissible(true); status.dismiss(); status.dismiss(); require(dismissals == 1 && !status.visible(), "Dismiss once");
        status.show(); require(status.visible(), "Status can reopen");
        ColorPicker color; int colors{}; color.on_change([&](auto) { ++colors; });
        color.set_value({255, 10, 20, 40}); require(colors == 0 && color.channels()[3]->value() == 40, "RGBA property sync");
        color.change_value({30, 40, 50, 60}); require(colors == 1, "RGBA action sync");
        rejects([&] { color.set_swatches(std::vector<RgbaColor>(17)); });
        for (int i = 0; i < 100; ++i) color.set_swatches({{0, 0, 0}});
        require(color.retained_children().size() == 9, "Swatches retain bounded children");
        auto dialog = std::make_shared<ContentDialog>(L"Dialog", std::make_shared<MultilineText>());
        dialog->popup()->opened(); int closed{};
        dialog->on_validate([] { return L"Invalid value"; }); dialog->bind_close([&](auto) { ++closed; });
        dialog->accept(); require(closed == 0 && dialog->validation()->visible(), "Validation stays visible");
        dialog->on_validate({}); dialog->accept(); require(closed == 1, "Valid default action");
        std::cout << "Seven document models: bounds, invalid UTF-16, property silence, reentrancy, selection, explicit links, password policy, validation passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
