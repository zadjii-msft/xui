#include "native_preview_host.hpp"
#include "preview_session.hpp"
#include <commctrl.h>
#include <cmath>

namespace xui {
struct NativePreviewHost::State : std::enable_shared_from_this<State> {
    std::shared_ptr<ShellPreview> model;
    std::shared_ptr<preview::Session> session;
    HWND visual{};
    std::uint64_t generation{};
    bool visible{}, disposed{}, allowed{};
    std::function<bool()> can_activate;
    std::function<void(bool)> leave;
    std::function<void()> dismiss, failed;
    void retire() {
        if (session) { session->revoke(); session.reset(); }
        if (visual) ShowWindow(visual, SW_HIDE);
    }
    void command(PreviewOperation op, bool reverse) {
        if (disposed) return;
        if (op == PreviewOperation::unload) { retire(); return; }
        if (op == PreviewOperation::focus) {
            if (session) { std::lock_guard lock(session->mutex); session->focus = reverse; }
            return;
        }
        retire();
        generation = 0;
    }
    void poll() {
        if (disposed || !session) return;
        std::optional<PreviewStatus> status;
        LONG actions{};
        {
            std::lock_guard lock(session->mutex);
            status = session->result; session->result.reset();
            actions = session->focus_actions; session->focus_actions = 0;
        }
        if (status && status->generation == model->status().generation) {
            if (status->state != PreviewState::accepted) ShowWindow(visual, SW_HIDE);
            model->publish(*status);
        }
        if (disposed || !session) return;
        if ((actions & preview::dismiss) && dismiss) dismiss();
        else if ((actions & (preview::leave_forward | preview::leave_reverse)) && leave)
            leave((actions & preview::leave_reverse) != 0);
    }
    static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR, DWORD_PTR data) {
        auto* self = reinterpret_cast<State*>(data);
        if (message == WM_TIMER && wparam == 1) {
            auto lifetime = self->shared_from_this();
            try { lifetime->poll(); } catch (...) { lifetime->failed(); }
            return 0;
        }
        return DefSubclassProc(window, message, wparam, lparam);
    }
};
NativePreviewHost::NativePreviewHost(std::shared_ptr<ShellPreview> model, HWND parent,
    std::function<bool()> can_activate, std::function<void(bool)> leave, std::function<void()> dismiss,
    std::function<void()> failed) : state_(std::make_shared<State>()) {
    auto& s = *state_; s.model = std::move(model); s.can_activate = std::move(can_activate);
    s.leave = std::move(leave); s.dismiss = std::move(dismiss); s.failed = std::move(failed);
    s.visual = CreateWindowExW(0, L"STATIC", L"Installed preview", WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        0, 0, 1, 1, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!s.visual) throw std::runtime_error("Cannot create native preview surface");
    if (!SetWindowSubclass(s.visual, State::procedure, 1, reinterpret_cast<DWORD_PTR>(&s)) ||
        !SetTimer(s.visual, 1, 30, nullptr)) {
        DestroyWindow(s.visual); s.visual = nullptr;
        throw std::runtime_error("Cannot attach native preview delivery");
    }
    std::weak_ptr<State> weak = state_;
    s.model->bind([weak](PreviewOperation op, bool reverse) { if (auto value = weak.lock()) value->command(op, reverse); });
}
NativePreviewHost::~NativePreviewHost() {
    auto s = std::move(state_);
    s->disposed = true; s->retire(); s->model->bind({});
    if (s->visual) { KillTimer(s->visual, 1); RemoveWindowSubclass(s->visual, State::procedure, 1); DestroyWindow(s->visual); }
}
void NativePreviewHost::sync(bool visible, UINT dpi) {
    auto s = state_;
    if (s->disposed) return;
    s->visible = visible; s->allowed = !s->can_activate || s->can_activate();
    if (!visible || !s->allowed) {
        if (s->session && (s->model->status().state == PreviewState::loading || s->model->status().state == PreviewState::accepted)) {
            s->retire();
            s->model->publish({s->model->status().generation, PreviewState::unsupported, PreviewReason::hidden,
                PreviewPhase::unload, 0, PreviewCleanup::pending});
        }
        s->poll();
        return;
    }
    const auto bounds = s->model->bounds();
    const auto scale = dpi / 96.0;
    RECT rect{0, 0, static_cast<LONG>(std::lround(bounds.width * scale)), static_cast<LONG>(std::lround(bounds.height * scale))};
    if (!SetWindowPos(s->visual, nullptr, 0, 0, rect.right, rect.bottom, SWP_NOZORDER | SWP_NOACTIVATE))
        throw std::runtime_error("Cannot arrange native preview surface");
    if (!s->session && !s->model->source().empty() && s->generation != s->model->status().generation) {
        s->generation = s->model->status().generation;
        RECT external{0, 0, static_cast<LONG>(720 * scale), static_cast<LONG>(480 * scale)};
        s->session = preview::start_session(preview::preview_helper_path(), s->model->source(), external, s->generation);
    }
    s->poll();
}
void NativePreviewHost::cancel_owner() { state_->disposed = true; state_->retire(); }
bool NativePreviewHost::active() const { return state_->session != nullptr; }
bool NativePreviewHost::contains_native(HWND window) const {
    return state_->visual && (window == state_->visual || IsChild(state_->visual, window));
}
}
