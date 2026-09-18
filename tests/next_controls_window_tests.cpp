#include <windows.h>
#include <ole2.h>
#include <UIAutomation.h>
#include <wrl/client.h>
#include "xui/application.hpp"
#include "xui/menu_bar.hpp"
#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
using namespace xui;
using Microsoft::WRL::ComPtr;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void success(HRESULT value, const char* message) { if (FAILED(value)) throw std::runtime_error(message); }
ComPtr<IUIAutomationElement> find(IUIAutomation* uia, IUIAutomationElement* root, const wchar_t* id) {
    VARIANT value{}; value.vt = VT_BSTR; value.bstrVal = SysAllocString(id);
    ComPtr<IUIAutomationCondition> condition;
    const auto hr = uia->CreatePropertyCondition(UIA_AutomationIdPropertyId, value, &condition);
    VariantClear(&value); success(hr, "Create ID condition");
    ComPtr<IUIAutomationElement> result;
    success(root->FindFirst(TreeScope_Subtree, condition.Get(), &result), "Find new control");
    require(result != nullptr, "New control exists in UIA"); return result;
}
void run(VisualStyle style) {
    WindowOptions options{L"XUI next controls", {680,420}, ThemeMode::light}; options.visual_style = style;
    Window window(options);
    auto root = std::make_shared<Stack>(Axis::vertical); root->set_spacing(8);
    auto check = std::make_shared<CheckBox>(L"Include"); check->set_automation_id(L"check"); check->set_three_state(true);
    auto link = std::make_shared<HyperlinkButton>(L"Documentation"); link->set_automation_id(L"link");
    auto selector = std::make_shared<SelectorBar>(L"Views"); selector->set_automation_id(L"selector");
    selector->set_items({{1,L"All"},{2,L"Unavailable",false},{3,L"Recent"}}, 1);
    auto badge = std::make_shared<InfoBadge>(L"Unread"); badge->set_automation_id(L"badge"); badge->set_count(123);
    auto menu = std::make_shared<MenuBar>(L"Main menu"); menu->set_automation_id(L"menubar");
    CommandRecord heading{10,0,L"&File"}; heading.kind = CommandKind::submenu;
    menu->set_commands(std::make_shared<CommandSet>(std::vector<CommandRecord>{heading,{11,10,L"Open",[]{}}}));
    menu->command_button(10)->set_automation_id(L"menu-heading");
    root->add(menu); root->add(check); root->add(link); root->add(selector); root->add(badge);
    window.set_content(root);
    int changes{}, clicks{};
    check->on_change([&](CheckState) { ++changes; }); link->on_click([&] { ++clicks; });
    std::string error;
    std::jthread worker([&] {
        const auto ui = [&](std::function<void()> callback) {
            auto completed = std::make_shared<std::promise<void>>(); auto done = completed->get_future();
            require(window.post([callback = std::move(callback),completed] {
                try { callback(); completed->set_value(); }
                catch (...) { completed->set_exception(std::current_exception()); }
            }), "Post UI-thread test");
            require(done.wait_for(std::chrono::seconds(15)) == std::future_status::ready, "UI-thread test timeout"); done.get();
        };
        try {
            HWND hwnd{};
            ui([&] {
                hwnd = FindWindowW(L"Xui.Window.1", options.title.c_str()); require(hwnd != nullptr, "Owned host exists");
                SendMessageW(hwnd, WM_APP + 12, 0, 0);
                require(window.focus(*check), "Checkbox receives native focus");
                SendMessageW(GetFocus(), WM_KEYDOWN, VK_SPACE, 0); SendMessageW(GetFocus(), WM_KEYUP, VK_SPACE, 0);
                SendMessageW(GetFocus(), WM_KEYDOWN, VK_SPACE, 0); SendMessageW(GetFocus(), WM_KEYUP, VK_SPACE, 0);
                require(changes == 2 && check->state() == CheckState::indeterminate, "Native checkbox cycles through mixed");
                SendMessageW(GetFocus(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(10,10));
                SendMessageW(GetFocus(), WM_CANCELMODE, 0, 0);
                SendMessageW(GetFocus(), WM_LBUTTONUP, 0, MAKELPARAM(10,10));
                require(changes == 2, "Cancelled native checkbox capture is silent");
                require(window.focus(*link), "Hyperlink receives native focus");
                SendMessageW(GetFocus(), WM_KEYDOWN, VK_RETURN, 0); require(clicks == 1, "Native link invokes callback");
                require(window.focus(*selector), "Selector receives one native keyboard target");
                SendMessageW(GetFocus(), WM_KEYDOWN, VK_RIGHT, 0);
                require(selector->selected() == 3, "Selector Right skips disabled choices");
                SendMessageW(GetFocus(), WM_KEYDOWN, VK_HOME, 0); require(selector->selected() == 1, "Selector Home selects first choice");
                SendMessageW(GetFocus(), WM_KEYDOWN, VK_END, 0); require(selector->selected() == 3, "Selector End selects last choice");
                require(!window.focus(*badge), "InfoBadge cannot take focus");
                SendMessageW(hwnd, WM_APP + 12, 0, 0);
            });
            success(CoInitializeEx(nullptr, COINIT_MULTITHREADED), "Initialize UIA client");
            struct Com { ~Com() { CoUninitialize(); } } com;
            ComPtr<IUIAutomation> uia;
            success(CoCreateInstance(CLSID_CUIAutomation,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&uia)), "Create UIA client");
            ComPtr<IUIAutomationElement> host; success(uia->ElementFromHandle(hwnd,&host), "Read owned UIA root");
            auto check_element = find(uia.Get(),host.Get(),L"check");
            ComPtr<IUIAutomationTogglePattern> toggle;
            success(check_element->GetCurrentPatternAs(UIA_TogglePatternId,IID_PPV_ARGS(&toggle)), "Checkbox exposes Toggle");
            ToggleState state{}; success(toggle->get_CurrentToggleState(&state), "Read checkbox state");
            require(state == ToggleState_Indeterminate, "UIA reports genuine mixed state");
            success(toggle->Toggle(), "UIA checkbox toggle");
            auto link_element = find(uia.Get(),host.Get(),L"link");
            CONTROLTYPEID type{}; success(link_element->get_CurrentControlType(&type), "Read link role");
            require(type == UIA_HyperlinkControlTypeId, "Link has Hyperlink control type");
            ComPtr<IUIAutomationInvokePattern> invoke;
            success(link_element->GetCurrentPatternAs(UIA_InvokePatternId,IID_PPV_ARGS(&invoke)), "Link exposes Invoke");
            success(invoke->Invoke(), "UIA invokes link");
            auto selector_element = find(uia.Get(),host.Get(),L"selector");
            ComPtr<IUIAutomationSelectionPattern> selection;
            success(selector_element->GetCurrentPatternAs(UIA_SelectionPatternId,IID_PPV_ARGS(&selection)), "Selector exposes Selection");
            auto item = find(uia.Get(),host.Get(),L"selector-tab-1");
            ComPtr<IUIAutomationSelectionItemPattern> select;
            success(item->GetCurrentPatternAs(UIA_SelectionItemPatternId,IID_PPV_ARGS(&select)), "Selector children expose SelectionItem");
            success(select->Select(), "UIA selects a horizontal choice");
            ComPtr<IUnknown> no_toggle;
            item->GetCurrentPattern(UIA_TogglePatternId,&no_toggle);
            require(!no_toggle, "Selector items do not expose checkbox semantics");
            auto badge_element = find(uia.Get(),host.Get(),L"badge");
            BSTR name{}; success(badge_element->get_CurrentName(&name), "Read badge status");
            const std::wstring badge_name(name ? name : L""); SysFreeString(name);
            require(badge_name.find(L"123") != std::wstring::npos, "Badge UIA keeps its full count");
            BOOL focusable{}; badge_element->get_CurrentIsKeyboardFocusable(&focusable);
            require(!focusable, "Badge UIA does not promise focus");
            auto menu_element = find(uia.Get(),host.Get(),L"menubar");
            menu_element->get_CurrentControlType(&type); require(type == UIA_MenuBarControlTypeId, "MenuBar has native menu semantics");
            auto menu_heading = find(uia.Get(),host.Get(),L"menu-heading");
            menu_heading->get_CurrentControlType(&type); require(type == UIA_MenuItemControlTypeId, "Heading is an accessible menu item");
            ComPtr<IUIAutomationExpandCollapsePattern> expand;
            success(menu_heading->GetCurrentPatternAs(UIA_ExpandCollapsePatternId,IID_PPV_ARGS(&expand)), "Heading exposes ExpandCollapse");
            success(expand->Expand(), "UIA automatically opens heading menu");
            success(expand->Collapse(), "UIA closes heading menu");
            ui([&] {
                require(clicks == 2 && changes == 3 && selector->selected() == 1, "UIA actions reach native models once");
                window.focus(*link); link->set_enabled(false); SendMessageW(hwnd,WM_APP+12,0,0);
                require(!window.focus(*link) && !link->invoke(), "Disabled link rejects native focus and action");
                window.set_theme(ThemeMode::high_contrast); SendMessageW(hwnd,WM_APP+12,0,0); UpdateWindow(hwnd);
            });
        } catch (const std::exception& failure) { error = failure.what(); }
        window.post([&] { window.close(); });
    });
    const auto result = Application::run(window); worker.join();
    require(result == 0 && error.empty(), error.empty() ? "Native controls complete" : error.c_str());
}
}
int main() {
    try { run(VisualStyle::classic); run(VisualStyle::winui); std::cout << "Next controls native/UIA contracts passed\n"; }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
