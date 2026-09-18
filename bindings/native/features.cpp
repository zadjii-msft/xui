#include "xui/xui.h"
#include "../../demo/branding.hpp"
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
namespace {
void check(xui_status status) { if(status) throw std::runtime_error("Feature ABI call failed: "+std::to_string(status)); }
xui_string text(const char* s) { return {s,static_cast<uint32_t>(std::strlen(s)),0}; }
xui_feature_value value() {xui_feature_value v{};v.size=sizeof(v);v.version=XUI_FEATURE_VERSION;return v;}
xui_handle create(xui_handle w,uint32_t kind,const char* name,xui_handle content=0) {
    xui_feature_options o{sizeof(o),XUI_FEATURE_VERSION,text(name),content};xui_handle h{};
    check(xui_feature_create(w,kind,&o,&h));return h;
}
struct App {xui_handle window{},range{},button{},dialog{};bool fail{};unsigned rows{};};
xui_status XUI_CALL key(void* context,const xui_event* event) {
    auto& app=*static_cast<App*>(context);
    switch(event->value&0xffff) {
    case 0x75:return xui_popup_show(app.dialog,app.button);
    case 0x77:return xui_feature_action(app.range,XUI_A_CHANGE_VALUE,0x4039000000000000ull,0);
    case 0x7b:return xui_window_close(app.window);
    default:return 0;
    }
}
xui_status XUI_CALL changed(void* context,const xui_event*) {return static_cast<App*>(context)->fail ? 123 : 0;}
xui_status XUI_CALL open(void* context,const xui_event*) {auto& app=*static_cast<App*>(context);return xui_popup_show(app.dialog,app.button);}
void XUI_CALL reference(void*) {}
xui_status XUI_CALL row(void* context,uint32_t op,uint64_t first,uint64_t second,xui_source_row* row) {
    if(op==0){row->id=first+1;row->version=1;}
    if(op==1){++static_cast<App*>(context)->rows;row->primary_length=14;std::memcpy(row->primary,"Virtual record",14);}
    if(op==2)row->index=first && first<=1000000 && second==1 ? first-1 : UINT64_MAX;
    return 0;
}
}
int main(int argc,char** argv) {
    App app;for(int i=1;i<argc;++i)if(std::strcmp(argv[i],"--callback-fail")==0)app.fail=true;
    try {
        xui_window_options options{sizeof(options),XUI_ABI_VERSION,text("XUI feature bindings"),700,800};
        check(xui_window_create(&options,&app.window));
        const auto icon = xui::demo::utf8(xui::demo::application_icon_source());
        check(xui_window_on_icon_error(app.window, [](void*, const char* error, uint32_t length) -> xui_status {
            std::cerr.write(error, length);
            return XUI_CALLBACK_FAILED;
        }, nullptr));
        check(xui_window_set_icon_source(app.window, text(icon.c_str())));
        xui_handle root{},heading{},button{};check(xui_stack_create(app.window,1,&root));
        check(xui_create(app.window,XUI_LABEL,text("F6: dialog. Escape: cancel. F8: range. F12: close."),0,&heading));
        auto combo=create(app.window,XUI_COMBO_BOX,"Choices");
        xui_choice choices[]{{sizeof(xui_choice),0,1,0,text("First")},{sizeof(xui_choice),0,2,0,text("Second")}};
        check(xui_choices(combo,choices,2,1,1));
        app.range=create(app.window,XUI_RANGE_INPUT,"Range");auto v=value();v.a=20;
        check(xui_feature_set(app.range,XUI_F_VALUE,&v));
        check(xui_create(app.window,XUI_BUTTON,text("Edit document"),0,&button));app.button=button;
        auto map=create(app.window,XUI_MAP_VIEW,"Offline map");v=value();v.a=47.6;v.b=-122.3;v.c=4;
        check(xui_feature_set(map,XUI_F_MAP_VIEW,&v));
        xui_map_marker marker{sizeof(marker),0,1,47.6,-122.3,text("Seattle")};check(xui_map_markers(map,&marker,1));
        auto items=create(app.window,XUI_ITEMS_VIEW,"One million rows");
        xui_source_options source_options{sizeof(source_options),XUI_FEATURE_VERSION,1000000,&app,row,reference,reference};
        xui_handle source{};check(xui_source_create(app.window,&source_options,&source));check(xui_source_attach(items,source));check(xui_source_release(source));
        xui_handle form{};check(xui_stack_create(app.window,1,&form));
        auto doc=create(app.window,XUI_MULTILINE_TEXT,"Notes");v=value();v.text=text("Authored text");
        check(xui_feature_set(doc,XUI_F_DOCUMENT_TEXT,&v));
        auto color=create(app.window,XUI_COLOR_PICKER,"Accent");v=value();v.first=0xffdc641e;check(xui_feature_set(color,XUI_F_COLOR,&v));
        check(xui_stack_add(form,doc,0));check(xui_stack_add(form,color,0));
        app.dialog=create(app.window,XUI_CONTENT_DIALOG,"Document and color",form);
        check(xui_subscribe(app.window,key,&app));check(xui_subscribe(app.range,changed,&app));check(xui_subscribe(button,open,&app));
        for(auto child:{heading,combo,app.range,button,map})check(xui_stack_add(root,child,0));
        check(xui_stack_add(root,items,1));check(xui_window_content(app.window,root));
        auto status=xui_window_run(app.window);check(xui_window_destroy(app.window));app.window=0;check(status);
        if(!app.rows || app.rows>=4096)throw std::runtime_error("Virtual source exceeded visible-query budget.");
        std::cout<<"Million-row source fetched "<<app.rows<<" visible rows.\n";return 0;
    } catch(const std::exception& e){if(app.window)xui_window_destroy(app.window);std::cerr<<e.what()<<'\n';return 1;}
}
