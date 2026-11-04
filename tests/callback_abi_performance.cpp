#include "xui/xui.h"
#include "xui/xui_layout.h"
#include "xui/xui_content.h"
#include <windows.h>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <fstream>
#include <map>
#include <stdexcept>

namespace {
bool measured{};
std::map<std::string,unsigned> counts;
std::uint64_t delivered{};
void require(bool value,const char* message) { if(!value)throw std::runtime_error(message); }
void ok(xui_status status) {
    if(!status)return;
    char text[1024]{};uint32_t count{};xui_status error{};
    xui_error_copy(text,1024,&count,&error);
    throw std::runtime_error(std::string(text,count));
}
xui_string text(const char* value) { return {value,static_cast<uint32_t>(std::strlen(value)),0}; }
xui_handle create(xui_handle owner,uint32_t kind) {
    xui_feature_options options{sizeof(options),XUI_FEATURE_VERSION,text("Study"),0,0};
    xui_handle result{};ok(xui_feature_create(owner,kind,&options,&result));return result;
}
xui_status XUI_CALL event(void*,const xui_event*) {++delivered;return XUI_OK;}
xui_status XUI_CALL miller(void*,const xui_miller_event*) {++delivered;return XUI_OK;}
std::uint64_t ticks(FILETIME value){return(std::uint64_t(value.dwHighDateTime)<<32)|value.dwLowDateTime;}
template<class F> void phase(const char* name,unsigned iterations,F action,std::uint64_t callbacks) {
    iterations=measured?iterations:16;
    if(measured)if(const auto found=counts.find(name);found!=counts.end())iterations=found->second;
    delivered=0;
    FILETIME creation{},exit{},k0{},u0{},k1{},u1{};
    require(GetThreadTimes(GetCurrentThread(),&creation,&exit,&k0,&u0)!=0,"Read ABI CPU");
    const auto start=std::chrono::steady_clock::now();
    for(unsigned i=0;i<iterations;++i)action(i);
    const auto wall=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    require(GetThreadTimes(GetCurrentThread(),&creation,&exit,&k1,&u1)!=0,"Read ABI CPU after work");
    require(delivered==std::uint64_t(iterations)*callbacks,"Exact ABI observer delivery count");
    std::cout<<"{\"phase\":\""<<name<<"\",\"iterations\":"<<iterations<<",\"callbacks\":"<<delivered
        <<",\"measured\":"<<(measured?"true":"false")<<",\"wall_ms\":"<<(measured?wall:0)
        <<",\"kernel_before100ns\":"<<ticks(k0)<<",\"kernel_after100ns\":"<<ticks(k1)
        <<",\"user_before100ns\":"<<ticks(u0)<<",\"user_after100ns\":"<<ticks(u1)<<"}\n";
}
}
int main(int argc,char** argv){
    if(argc==2&&std::string_view(argv[1])=="--measure")measured=true;
    else if(argc!=1){std::cerr<<"Usage: callback_abi_performance [--measure]\n";return 2;}
    try {
        std::cout<<std::setprecision(17);
        if(const auto path=std::getenv("XUI_PERF_COUNTS")){
            std::ifstream input(path);require(bool(input),"Read ABI count protocol");
            std::string name;unsigned count{};
            while(input>>name>>count)require(count>=32&&count%32==0&&counts.emplace(name,count).second,"Exact unique ABI count");
            require(input.eof(),"Complete ABI count protocol");
        }
        xui_window_options options{sizeof(options),XUI_ABI_VERSION,text("ABI study"),600,400};
        xui_handle window{};ok(xui_window_create(&options,&window));
        xui_handle button{};ok(xui_create(window,XUI_BUTTON,text("Action"),0,&button));
        ok(xui_subscribe(button,event,nullptr));
        phase("abi-observed-invoke",1048576,[&](unsigned){ok(xui_invoke(button));},1);
        phase("abi-subscribe-replace",262144,[&](unsigned){ok(xui_subscribe(button,event,nullptr));ok(xui_invoke(button));},1);
        phase("abi-unsubscribe-resubscribe",262144,[&](unsigned){ok(xui_subscribe(button,nullptr,nullptr));ok(xui_subscribe(button,event,nullptr));ok(xui_invoke(button));},1);
        const auto range=create(window,XUI_RANGE_INPUT);
        ok(xui_subscribe(range,event,nullptr));
        phase("abi-range-value",262144,[&](unsigned i){ok(xui_feature_action(range,XUI_A_CHANGE_VALUE,i%2?1:2,0));},1);
        const auto columns=create(window,XUI_MILLER_COLUMNS);
        phase("abi-miller-registration",262144,[&](unsigned){ok(xui_miller_subscribe(columns,miller,nullptr));ok(xui_miller_subscribe(columns,nullptr,nullptr));},0);
        xui_handle host{},root{};ok(xui_content_host_create(window,&host));
        ok(xui_stack_create(window,0,&root));ok(xui_stack_add(root,host,0));ok(xui_window_content(window,root));
        uint32_t before{};ok(xui_content_handle_count(window,&before));
        phase("abi-content-rollback",4096,[&](unsigned){
            xui_handle scope{},child{};ok(xui_content_begin(host,&scope));
            ok(xui_create(window,XUI_BUTTON,text("Candidate"),0,&child));
            ok(xui_subscribe(child,event,nullptr));
            ok(xui_content_release(scope));
        },0);
        phase("abi-content-commit-clear",4096,[&](unsigned){
            xui_handle scope{},child{};ok(xui_content_begin(host,&scope));
            ok(xui_create(window,XUI_BUTTON,text("Candidate"),0,&child));
            ok(xui_subscribe(child,event,nullptr));ok(xui_content_commit(scope,child));
            ok(xui_invoke(child));ok(xui_content_clear(host));
        },1);
        uint32_t after{};ok(xui_content_handle_count(window,&after));require(before==after,"Retained ABI handles return to baseline");
        ok(xui_window_destroy(window));
        phase("abi-window-retirement",4096,[&](unsigned){
            xui_handle owner{},child{};ok(xui_window_create(&options,&owner));
            ok(xui_create(owner,XUI_BUTTON,text("Action"),0,&child));ok(xui_subscribe(child,event,nullptr));
            ok(xui_invoke(child));ok(xui_window_destroy(owner));
        },1);
        std::cout<<"{\"retained_handles_before\":"<<before<<",\"retained_handles_after\":"<<after<<"}\n";
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
