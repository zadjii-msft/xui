#include "xui/xui.h"
#include "xui/foundation.hpp"
#include <windows.h>
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>
#include <bit>
namespace {
unsigned assertions{};
void expect(bool condition) { if (!condition) std::abort(); ++assertions; }
void ok(xui_status status) { if (status) { char error[1024]{}; uint32_t size{}; xui_status code{}; xui_error_copy(error,1024,&size,&code); std::cerr.write(error,size); std::cerr << '\n'; } expect(status == 0); }
xui_string text(const char* v) { return {v, static_cast<uint32_t>(std::strlen(v)), 0}; }
xui_feature_value value() { xui_feature_value v{}; v.size=sizeof(v); v.version=XUI_FEATURE_VERSION; return v; }
xui_handle create(xui_handle w,uint32_t kind,xui_handle content=0,xui_handle second=0) {
    xui_feature_options o{sizeof(o),XUI_FEATURE_VERSION,text("Feature"),content,second};
    xui_handle h{};ok(xui_feature_create(w,kind,&o,&h));return h;
}
struct Source {unsigned refs{1}, queries{}, items{}; bool fail{}; uint64_t count{1000000}; xui_handle mutation_target{}; bool mutation_blocked{};};
void XUI_CALL retain(void* c) {++static_cast<Source*>(c)->refs;}
void XUI_CALL release(void* c) {--static_cast<Source*>(c)->refs;}
xui_status XUI_CALL query(void* c,uint32_t op,uint64_t first,uint64_t second,xui_source_row* row) {
    auto& source=*static_cast<Source*>(c);++source.queries;
    if(source.mutation_target) { auto v=value(); v.a=9; source.mutation_blocked=xui_feature_set(source.mutation_target,XUI_F_VALUE,&v)==XUI_BUSY; }
    if(source.fail) return 83;
    if(op==0){row->id=first+1;row->version=7;}
    if(op==1){++source.items;row->primary_length=3;std::memcpy(row->primary,"row",3);}
    if(op==2) row->index=first && first<=source.count && second==7 ? first-1 : UINT64_MAX;
    if(op==3) row->index=first==1;
    return 0;
}
xui_status XUI_CALL secret(void* c,const char* bytes,uint32_t size) {
    *static_cast<bool*>(c)=size==4 && std::memcmp(bytes,"safe",4)==0;return 0;
}
xui_status XUI_CALL event(void* c,const xui_event* e) { *static_cast<xui_event*>(c)=*e; return 0; }
}
int main() {
    static_assert(sizeof(xui_feature_options)==48);
    static_assert(sizeof(xui_feature_value)==72);
    static_assert(sizeof(xui_source_row)==2096);
    expect(xui_abi_version()==0x10000);expect(xui_feature_version()==0x10001);
    xui_window_options o{sizeof(o),XUI_ABI_VERSION,text("ABI features"),600,600};
    xui_handle w{};ok(xui_window_create(&o,&w));
    xui_handle handles[45]{};
    for(uint32_t kind=XUI_RANGE_INPUT;kind<=XUI_HISTORY_CHART;++kind){
        xui_handle content{},second{};
        if(kind==XUI_EXPANDER || kind==XUI_POPUP || kind==XUI_CONTENT_DIALOG || kind==XUI_ADAPTIVE_LAYOUT || kind==XUI_SPLIT_VIEW)
            ok(xui_stack_create(w,1,&content));
        if(kind==XUI_ADAPTIVE_LAYOUT || kind==XUI_SPLIT_VIEW)ok(xui_stack_create(w,1,&second));
        if(kind==XUI_VIEW_PICKER)content=handles[XUI_ITEMS_VIEW];
        handles[kind]=create(w,kind,content,second);
    }
    auto range=handles[XUI_RANGE_INPUT];auto v=value();v.a=-10;v.b=10;v.c=0.5;v.d=2;
    ok(xui_feature_set(range,XUI_F_RANGE,&v));v=value();v.a=2.5;ok(xui_feature_set(range,XUI_F_VALUE,&v));
    xui::RangeInput reference;reference.set_range({-10,10,.5,2});reference.set_value(2.5);
    v=value();ok(xui_feature_get(range,XUI_F_VALUE,&v));expect(v.a==reference.value());
    v=value();v.a=NAN;expect(xui_feature_set(range,XUI_F_VALUE,&v)==XUI_INVALID_ARGUMENT);
    v=value();v.second=1;expect(xui_feature_set(range,XUI_F_VALUE,&v)==XUI_INVALID_ARGUMENT);
    v=value();v.text=text("\xf0\x28\x8c\x28");expect(xui_feature_set(range,XUI_F_HELP,&v)==XUI_INVALID_ARGUMENT);
    v=value();v.text=text("A\xf0\x9f\x98\x80Z");ok(xui_feature_set(handles[XUI_MULTILINE_TEXT],XUI_F_DOCUMENT_TEXT,&v));
    v=value();v.first=1;v.second=3;ok(xui_feature_set(handles[XUI_MULTILINE_TEXT],XUI_F_TEXT_SELECTION,&v));
    v.second=2;expect(xui_feature_set(handles[XUI_MULTILINE_TEXT],XUI_F_TEXT_SELECTION,&v)==XUI_INVALID_ARGUMENT);
    v=value();v.text=text("safe");ok(xui_feature_set(handles[XUI_PASSWORD_INPUT],XUI_F_PASSWORD,&v));
    bool received{};ok(xui_password_read(handles[XUI_PASSWORD_INPUT],secret,&received));expect(received);
    uint32_t length{};expect(xui_text_copy(handles[XUI_PASSWORD_INPUT],nullptr,0,&length)==XUI_WRONG_KIND);
    Source source;
    source.mutation_target=range;
    xui_source_options source_options{sizeof(source_options),XUI_FEATURE_VERSION,1000000,&source,query,retain,release};
    xui_handle snapshot{};ok(xui_source_create(w,&source_options,&snapshot));expect(source.refs==2);
    ok(xui_source_attach(handles[XUI_ITEMS_VIEW],snapshot));
    ok(xui_feature_action(handles[XUI_ITEMS_VIEW],XUI_A_SELECT_ALL,0,0));expect(source.queries<100);
    v=value();ok(xui_feature_get(handles[XUI_ITEMS_VIEW],XUI_F_SELECTION_STATE,&v));expect(v.b==1);
    uint32_t selected{};ok(xui_collection_contains(handles[XUI_ITEMS_VIEW],999999,7,&selected));expect(selected==1);
    ok(xui_source_attach(handles[XUI_TREE_VIEW],snapshot));
    xui_event request{};ok(xui_subscribe(handles[XUI_TREE_VIEW],event,&request));
    ok(xui_tree_expand(handles[XUI_TREE_VIEW],1,7,1));expect(request.kind==XUI_REQUEST && request.value);
    Source empty_source;empty_source.count=0;source_options.context=&empty_source;
    source_options.count=0;xui_handle empty{};ok(xui_source_create(w,&source_options,&empty));
    ok(xui_tree_complete(handles[XUI_TREE_VIEW],request.value,empty,text("")));
    expect(xui_tree_complete(handles[XUI_TREE_VIEW],request.value,snapshot,text(""))==XUI_INVALID_HANDLE);
    Source replace_source;source_options.context=&replace_source;source_options.count=1000000;
    xui_handle replace_snapshot{};ok(xui_source_create(w,&source_options,&replace_snapshot));
    auto replace_view=create(w,XUI_ITEMS_VIEW);ok(xui_source_attach(replace_view,replace_snapshot));
    ok(xui_source_release(replace_snapshot));expect(replace_source.refs==2);
    ok(xui_source_attach(replace_view,empty));expect(replace_source.refs==1);
    auto map=handles[XUI_MAP_VIEW];xui_handle token{},newer{};
    ok(xui_map_request(map,&token));ok(xui_map_request(map,&newer));
    expect(xui_map_complete(map,token,nullptr,0)==XUI_CLOSED);ok(xui_map_complete(map,newer,nullptr,0));
    ok(xui_map_request(map,&token));ok(xui_map_request(map,&newer));
    ok(xui_request_cancel(token));ok(xui_map_complete(map,newer,nullptr,0));
    xui_handle othermap=create(w,XUI_MAP_VIEW);ok(xui_map_request(map,&token));
    expect(xui_map_complete(othermap,token,nullptr,0)==XUI_INVALID_ARGUMENT);ok(xui_request_cancel(token));
    xui_command_record commands[]{{sizeof(xui_command_record),0,1,0,text("Action"),text("Ctrl+K"),text("Pin"),0,0}};
    auto bar=handles[XUI_COMMAND_BAR];ok(xui_commands_set(bar,commands,1));xui_event action{};ok(xui_subscribe(bar,event,&action));
    ok(xui_command_invoke(bar,1,0));expect(action.kind==XUI_CLICK && action.value==1);
    ok(xui_command_invoke(bar,1,1));expect(action.kind==XUI_ACTION && action.value==1);
    ok(xui_command_bind(bar,1,'K',1));
    v=value();ok(xui_feature_get(handles[XUI_WEB_CONTENT],XUI_F_HOST_STATE,&v));expect(v.first==0);
    xui_handle script{};ok(xui_web_evaluate(handles[XUI_WEB_CONTENT],text("1+1"),&script));
    uint32_t failed{};expect(xui_web_result(script,nullptr,0,&length,&failed)==XUI_BUFFER_TOO_SMALL);expect(failed==1);
    ok(xui_request_cancel(script));
    std::thread wrong([&]{auto v=value();expect(xui_feature_get(range,XUI_F_VALUE,&v)==XUI_WRONG_THREAD);});wrong.join();
    expect(source.items<100);expect(source.mutation_blocked);
    ok(xui_map_request(map,&token));
    ok(xui_window_destroy(w));expect(source.refs==1);
    expect(xui_map_complete(map,token,nullptr,0)==XUI_INVALID_HANDLE);
    v=value();expect(xui_feature_get(range,XUI_F_VALUE,&v)==XUI_INVALID_HANDLE);
    std::cout<<"Feature ABI assertions: "<<assertions<<"; source queries: "<<source.queries<<"; rows: "<<source.items<<"\n";
}
