#pragma once

#include <UIAutomation.h>
#include <wrl/client.h>
#include <atomic>
#include <mutex>
#include <new>
#include <string>
#include <vector>

namespace uia_test {
struct Event {
    int id{};
    std::wstring name;
    VARIANT value{};
    Event(int key, std::wstring text, const VARIANT* data = nullptr) : id(key), name(std::move(text)) {
        if (data && FAILED(VariantCopy(&value, data))) throw std::bad_alloc();
    }
    Event(Event&& other) noexcept : id(other.id), name(std::move(other.name)), value(other.value) {
        VariantInit(&other.value);
    }
    ~Event() { VariantClear(&value); }
};
class Events final : public IUIAutomationFocusChangedEventHandler,
    public IUIAutomationPropertyChangedEventHandler, public IUIAutomationEventHandler,
    public IUIAutomationStructureChangedEventHandler {
public:
    explicit Events(DWORD process) : process_(process) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** value) override {
        if (!value) return E_POINTER;
        *value = nullptr;
        if (iid == __uuidof(IUnknown) || iid == __uuidof(IUIAutomationFocusChangedEventHandler))
            *value = static_cast<IUIAutomationFocusChangedEventHandler*>(this);
        else if (iid == __uuidof(IUIAutomationPropertyChangedEventHandler))
            *value = static_cast<IUIAutomationPropertyChangedEventHandler*>(this);
        else if (iid == __uuidof(IUIAutomationEventHandler))
            *value = static_cast<IUIAutomationEventHandler*>(this);
        else if (iid == __uuidof(IUIAutomationStructureChangedEventHandler))
            *value = static_cast<IUIAutomationStructureChangedEventHandler*>(this);
        else return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const auto result = --refs_;
        if (!result) delete this;
        return result;
    }
    HRESULT STDMETHODCALLTYPE HandleFocusChangedEvent(IUIAutomationElement* sender) override {
        return record(sender, UIA_AutomationFocusChangedEventId);
    }
    HRESULT STDMETHODCALLTYPE HandlePropertyChangedEvent(IUIAutomationElement* sender, PROPERTYID id, VARIANT value) override {
        return record(sender, id, &value);
    }
    HRESULT STDMETHODCALLTYPE HandleAutomationEvent(IUIAutomationElement* sender, EVENTID id) override {
        return record(sender, id);
    }
    HRESULT STDMETHODCALLTYPE HandleStructureChangedEvent(IUIAutomationElement* sender, StructureChangeType, SAFEARRAY*) override {
        return record(sender, UIA_StructureChangedEventId);
    }
    size_t count(int id, const wchar_t* name = nullptr) {
        std::lock_guard lock(mutex_);
        size_t count{};
        for (const auto& event : events_)
            if (event.id == id && (!name || event.name == name)) ++count;
        return count;
    }
    bool boolean(int id, const wchar_t* name, bool value) {
        std::lock_guard lock(mutex_);
        for (const auto& event : events_)
            if (event.id == id && event.name == name && event.value.vt == VT_BOOL &&
                (event.value.boolVal != VARIANT_FALSE) == value) return true;
        return false;
    }
private:
    HRESULT record(IUIAutomationElement* sender, int id, const VARIANT* value = nullptr) noexcept {
        try {
            int process{};
            if (!sender || FAILED(sender->get_CachedProcessId(&process)) || static_cast<DWORD>(process) != process_)
                return S_OK;
            BSTR text{};
            if (FAILED(sender->get_CachedName(&text))) return S_OK;
            struct Text { BSTR value; ~Text() { SysFreeString(value); } } cleanup{text};
            std::wstring name(text ? text : L"");
            std::lock_guard lock(mutex_);
            events_.emplace_back(id, std::move(name), value);
            return S_OK;
        } catch (...) { return E_OUTOFMEMORY; }
    }
    std::atomic<ULONG> refs_{1};
    DWORD process_;
    std::mutex mutex_;
    std::vector<Event> events_;
};
struct Subscription {
    Microsoft::WRL::ComPtr<IUIAutomation> automation;
    ~Subscription() { if (automation) automation->RemoveAllEventHandlers(); }
};
}
