#include "../src/native_field_accessibility.hpp"
#include "../src/text_input_host.hpp"
#include "../src/document_host.hpp"
#include "../src/password_host.hpp"
#include "../src/date_time_host.hpp"
#include "native_editor_test_support.hpp"
#include "xui/foundation.hpp"
#include <UIAutomation.h>
#include <wrl/client.h>
#include <iostream>
#include <type_traits>

namespace {
using namespace xui;
using Microsoft::WRL::ComPtr;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct DetachedServices final : PeerHostServices, NativeFieldServices {
    unsigned retired{}, paints{};
    HWND create_input_surface(HostPeer&, HWND, int) override { std::terminate(); }
    LRESULT dispatch_input(HostPeer&, HWND, UINT, WPARAM, LPARAM) noexcept override { std::terminate(); }
    bool input_enabled(const HostPeer&) const override { std::terminate(); }
    bool input_visible(const HostPeer&) const override { std::terminate(); }
    bool owner_open() const override { std::terminate(); }
    bool focus_peer(HostPeer&, bool) override { std::terminate(); }
    void reveal_peer(HostPeer&) override { std::terminate(); }
    void update_peer_state() override { std::terminate(); }
    void invalidate_peer_paint() override { ++paints; }
    bool wheel_animation_allowed() const override { std::terminate(); }
    void offer_row_tooltip(HostPeer&) override { std::terminate(); }
    bool file_drag_eligible(const Control&, HWND) const override { std::terminate(); }
    void deliver_file_drop(std::function<void()>) noexcept override { std::terminate(); }
    std::shared_ptr<PeerHostServices> acquire_owner_lease() override { std::terminate(); }
    void forget_peer(HostPeer&) noexcept override { ++retired; }
    void peer_failure() noexcept override { std::terminate(); }
    const wchar_t* native_font_family() const override { std::terminate(); }
    HFONT native_caption_font() const override { std::terminate(); }
    std::shared_ptr<NativeFieldServices> acquire_native_lease() override { std::terminate(); }
    NativeFieldEmbedding native_embedding(const NativeFieldHostPeer&) const override { std::terminate(); }
    LRESULT dispatch_native_field(NativeFieldHostPeer&, HWND, UINT, WPARAM, LPARAM) noexcept override { std::terminate(); }
    void collect_text_clear(TextInputHostPeer&) override { std::terminate(); }
    void activate_text_clear(std::uint64_t) override { std::terminate(); }
    void invalidate_native_field(Invalidation) override { std::terminate(); }
};
struct ParentPeer final : HostPeer {
    ParentPeer(PeerHostServices& host, std::shared_ptr<Control> model)
        : HostPeer(host, HostFamily::unextracted, std::move(model)) {}
    ~ParentPeer() override { retire_native(); }
    void attach(HWND, int) override { std::terminate(); }
    void sync(UINT, const Palette&) override { std::terminate(); }
    void publish_accessibility() override { std::terminate(); }
    void disconnect_accessibility() override { std::terminate(); }
    void paint(const HostPaintContext&) override { std::terminate(); }
    std::optional<LRESULT> input(UINT, WPARAM, LPARAM, const HostInputContext&) override { std::terminate(); }
};
void clear_policy() {
    DetachedServices services;
    {
        auto model = std::make_shared<TextInput>(L"Policy");
        TextInputHostPeer input(services, services, model);
        require(!input.can_clear(), "Classic input rejects clear without any host phase");
        model->set_visual_style(VisualStyle::winui);
        require(input.can_clear(), "Ordinary WinUI input needs no embedding, font, focus or enabled query");
        model->set_search_style(true);
        require(!input.can_clear(), "Search adornment rejects clear before any host query");
        model->set_search_style(false);
        model->set_shortcut_hint(L"Ctrl+F");
        model->arrange({0, 0, 400, 60});
        require(!input.can_clear(), "Visible shortcut rejects clear before any host query");
        model->set_shortcut_hint(L"");
        auto number = std::make_shared<NumericInput>();
        ParentPeer parent(services, number);
        input.parent = &parent;
        require(!input.can_clear(), "Numeric enclosing behavior retains its own adornment policy");
        parent.control = std::make_shared<ComboBox>();
        require(!input.can_clear(), "Combo enclosing behavior retains its own adornment policy");
        input.parent = nullptr;
        require(input.can_clear(), "Removing the embedding restores ordinary clear eligibility");
    }
    require(services.retired == 2 && !services.paints, "Both unattached concrete peers retire exactly once without scheduling paint");
    for (unsigned kind = 0; kind != 4; ++kind) {
        bool rejected{};
        try {
            if (kind == 0) TextInputHostPeer invalid(services, services, {});
            if (kind == 1) DocumentTextHostPeer invalid(services, services, {});
            if (kind == 2) PasswordHostPeer invalid(services, services, {});
            if (kind == 3) DateTimeHostPeer invalid(services, services, {});
        } catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "Every concrete editor owner rejects a missing model before native attachment");
    }
}
void clear_cancellation() {
    DetachedServices services;
    {
        auto button = std::make_shared<Button>(L"Clear cancellation fixture");
        button->set_enabled(false);
        unsigned invalidations{};
        button->set_invalidator([&](Invalidation) { ++invalidations; });
        TextClearHostPeer peer(services, button, {});
        peer.window = reinterpret_cast<HWND>(1);
        Palette palette{};
        const HostInputContext context{96, palette};
        for (unsigned index = 0; index != 64; ++index) {
            const auto result = peer.input(WM_CANCELMODE, 0, 0, context);
            require(result && *result == 0 && !services.paints && !invalidations,
                "Repeated already-disabled clear cancellation never manufactures another host update");
        }
        const auto captured = peer.input(WM_CAPTURECHANGED, 0, 0, context);
        require(captured && *captured == 0 && services.paints == 1 && !invalidations,
            "Capture loss preserves its distinct exactly-once paint scheduling");
        peer.input(WM_CANCELMODE, 0, 0, context);
        require(services.paints == 1 && !invalidations, "Later cancellation does not repeat capture-loss scheduling");
        button->set_invalidator({});
    }
    require(services.retired == 1, "Unattached clear owner retires exactly once");
}
void resource_results() {
    require(xui::test::gui_resource_count(GR_GDIOBJECTS, 0, ERROR_SUCCESS) == 0 &&
        xui::test::gui_resource_count(GR_USEROBJECTS, 3, ERROR_ACCESS_DENIED) == 3,
        "Zero with no error is valid, while successful nonzero counts do not consume an undefined last error");
    bool rejected{};
    try { xui::test::gui_resource_count(GR_GDIOBJECTS, 0, ERROR_ACCESS_DENIED); }
    catch (const std::runtime_error& error) {
        rejected = std::string_view(error.what()).find("Win32 error=5") != std::string_view::npos;
    }
    require(rejected, "Failed zero GUI-resource reads are explicit errors, never successful zero growth");
}
void native_provider() {
    static_assert(std::is_same_v<decltype(NativeFieldAccessibility::snapshot), HostAccessibilityIdentity>);
    auto state = std::make_shared<NativeFieldAccessibility>();
    auto password = std::make_shared<PasswordInput>(L"Account secret");
    password->set_password(L"must not enter the accessibility snapshot");
    password->set_automation_id(L"account-password");
    password->set_help_text(L"Enter the account secret");
    const std::weak_ptr<PasswordInput> model = password;
    publish_native_field(state, *password, reinterpret_cast<HWND>(1));
    ComPtr<IRawElementProviderSimple> provider;
    provider.Attach(create_native_field_provider(state));
    password.reset();
    require(model.expired(), "Native provider state never retains a live Control");
    for (const auto property : {UIA_NamePropertyId, UIA_AutomationIdPropertyId, UIA_HelpTextPropertyId}) {
        VARIANT value{};
        require(provider->GetPropertyValue(property, &value) == S_OK && value.vt == VT_BSTR && value.bstrVal,
            "Independent native metadata survives model release");
        const auto expected = property == UIA_NamePropertyId ? L"Account secret" :
            property == UIA_AutomationIdPropertyId ? L"account-password" : L"Enter the account secret";
        const bool matches = std::wstring_view(value.bstrVal) == expected;
        VariantClear(&value);
        require(matches, "Native metadata matches its exact property");
    }
    VARIANT value{};
    require(provider->GetPropertyValue(UIA_ValueValuePropertyId, &value) == E_ACCESSDENIED && value.vt == VT_EMPTY,
        "Password value is explicitly denied, never substituted or published");
    require(provider->GetPropertyValue(UIA_IsPasswordPropertyId, &value) == S_OK &&
        value.vt == VT_BOOL && value.boolVal == VARIANT_TRUE, "Password identity remains explicit");
    VariantClear(&value);
    for (const auto pattern : {UIA_ValuePatternId, UIA_TextPatternId, UIA_TextPattern2Id}) {
        ComPtr<IUnknown> native;
        require(provider->GetPatternProvider(pattern, &native) == S_OK && !native,
            "Override does not impersonate or replace Windows native text/value providers");
    }
    ComPtr<IRawElementProviderFragment> fragment;
    require(provider.As(&fragment) == S_OK, "Native override has a fragment identity");
    ComPtr<IUnknown> root_identity, fragment_identity;
    require(provider.As(&root_identity) == S_OK && fragment.As(&fragment_identity) == S_OK &&
        root_identity.Get() == fragment_identity.Get(), "Native override interfaces share one COM identity");
    disconnect_native_field(state, provider.Get());
    require(!state->snapshot.window && state->snapshot.name.empty() && state->snapshot.help_text.empty(),
        "Disconnect retires all metadata and native identity");
    state.reset();
    require(provider->GetPropertyValue(UIA_NamePropertyId, &value) == UIA_E_ELEMENTNOTAVAILABLE &&
        value.vt == VT_EMPTY, "Retained native provider is independently disconnected");
    require(fragment->SetFocus() == UIA_E_ELEMENTNOTAVAILABLE, "Disconnected native focus cannot target an old HWND");
    require(provider->GetPropertyValue(UIA_NamePropertyId, nullptr) == E_POINTER, "Native getter rejects null outputs");
}
void clear_provider() {
    auto state = std::make_shared<NativeFieldAccessibility>();
    state->snapshot.window = reinterpret_cast<HWND>(1);
    state->snapshot.id = 0x0102030405060708ull;
    state->snapshot.name = L"Clear account metadata whose lifetime exceeds small-string storage";
    state->snapshot.automation_id = L"xui-text-clear-account-independent-long-automation-identity";
    state->snapshot.help_text = L"Clear action help metadata whose lifetime also exceeds small-string storage";
    state->snapshot.role = ControlRole::button;
    ComPtr<IRawElementProviderSimple> root;
    root.Attach(create_text_clear_provider(state));
    ComPtr<IInvokeProvider> invoke;
    require(root.As(&invoke) == E_NOINTERFACE && !invoke, "Clear HWND root is not its semantic action child");
    ComPtr<IRawElementProviderFragment> fragment, child;
    require(root.As(&fragment) == S_OK && fragment->Navigate(NavigateDirection_FirstChild, &child) == S_OK && child,
        "Clear root exposes one semantic child");
    require(child.As(&invoke) == S_OK, "Clear semantic child exposes Invoke");
    ComPtr<IRawElementProviderSimple> simple;
    require(child.As(&simple) == S_OK, "Clear semantic child exposes independent metadata");
    for (const auto property : {UIA_NamePropertyId, UIA_AutomationIdPropertyId, UIA_HelpTextPropertyId, UIA_FrameworkIdPropertyId}) {
        const std::wstring_view expected = property == UIA_NamePropertyId ? std::wstring_view{state->snapshot.name} :
            property == UIA_AutomationIdPropertyId ? std::wstring_view{state->snapshot.automation_id} :
            property == UIA_HelpTextPropertyId ? std::wstring_view{state->snapshot.help_text} : std::wstring_view{L"XUI"};
        VARIANT value{};
        require(simple->GetPropertyValue(property, &value) == S_OK && value.vt == VT_BSTR && value.bstrVal,
            "Clear semantic metadata has its exact independent BSTR shape");
        const bool matches = std::wstring_view(value.bstrVal, SysStringLen(value.bstrVal)) == expected;
        VariantClear(&value);
        require(matches, "Long clear Name/AutomationId/HelpText and FrameworkId survive their query snapshot lifetime");
        require(root->GetPropertyValue(property, &value) == S_OK && value.vt == VT_BSTR && value.bstrVal,
            "Clear native root metadata preserves its BSTR contract");
        const auto root_expected = property == UIA_NamePropertyId || property == UIA_AutomationIdPropertyId ?
            std::wstring_view{} : expected;
        const bool root_matches = std::wstring_view(value.bstrVal, SysStringLen(value.bstrVal)) == root_expected;
        VariantClear(&value);
        require(root_matches, "Clear Pane keeps empty semantic name/id and independent help/framework metadata");
    }
    SAFEARRAY* runtime{};
    require(child->GetRuntimeId(&runtime) == S_OK && runtime && runtime->rgsabound[0].cElements == 3,
        "Clear semantic identity has exactly the existing three runtime components");
    LONG at = 1;
    int high{}, low{};
    auto read = SafeArrayGetElement(runtime, &at, &high);
    ++at; const auto read_low = SafeArrayGetElement(runtime, &at, &low);
    SafeArrayDestroy(runtime);
    require(read == S_OK && read_low == S_OK && high == 0x01020304 && low == 0x05060708,
        "Clear runtime identity preserves both halves of the control id");
    require(invoke->Invoke() == UIA_E_ELEMENTNOTENABLED, "Disabled clear action never dispatches to an HWND");
    disconnect_native_field(state, root.Get());
    root.Reset(); fragment.Reset(); state.reset();
    require(invoke->Invoke() == UIA_E_ELEMENTNOTAVAILABLE, "Retained clear action cannot invoke after retirement");
    VARIANT value{};
    require(simple->GetPropertyValue(UIA_NamePropertyId, &value) == UIA_E_ELEMENTNOTAVAILABLE,
        "Clear child retains only independent disconnected state");
}
}
int main(int argc, char**) {
    if (argc != 1) { std::cerr << "This native provider fixture accepts no selectors\n"; return 2; }
    try {
        clear_policy(); clear_cancellation(); resource_results(); native_provider(); clear_provider();
        std::cout << "Native metadata, password denial, pattern non-impersonation, COM/runtime identity, clear policy and retirement passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
