#include "appearance.h"
#include "config.h"
#include <iostream>

using namespace recall;
int main() {
    int failures=0;
    auto check=[&](bool ok,const char* message) { if(!ok) { std::cerr << "FAIL: " << message << '\n'; ++failures; } };
    try {
        auto defaults=Config::parse("{}");
        check(defaults.opacity==80&&defaults.blur,"existing configuration gets 80 percent opacity and blur by default");
        auto custom=Config::parse(R"({"appearance":{"opacity":95,"blur":false}})");
        check(custom.opacity==95&&!custom.blur,"custom appearance settings parsed");
        check(Config::parse(R"({"appearance":{"opacity":100}})").blur,"partial settings retain defaults");
        for(auto invalid:{R"({"appearance":{"opacity":0}})",R"({"appearance":{"opacity":101}})",
            R"({"appearance":{"opacity":79.5}})",R"({"appearance":{"opacity":"80"}})",
            R"({"appearance":{"blur":1}})",R"({"appearance":{"blur":"true"}})"}) {
            bool rejected=false; try { Config::parse(invalid); } catch(const std::exception&) { rejected=true; }
            check(rejected,"invalid appearance setting rejected");
        }
        HWND window=CreateWindowExW(WS_EX_TOOLWINDOW,L"STATIC",L"ReCall appearance test",WS_POPUP,0,0,400,300,nullptr,nullptr,nullptr,nullptr);
        if(!window) throw std::runtime_error("Cannot create test window");
        try {
            auto result=apply_appearance(window,80,false);
            BYTE alpha{}; DWORD flags{}; COLORREF key{};
            check(GetLayeredWindowAttributes(window,&key,&alpha,&flags)&&alpha==204&&(flags&LWA_ALPHA),"80 percent native window alpha applied");
            check(result.alpha==204&&!result.blur_enabled,"opacity and blur independently controlled");
            check((GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_TOOLWINDOW)!=0,"existing window styles preserved");
            apply_appearance(window,100,false);
            check(!(GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_LAYERED),"100 percent restores non-layered window");
            apply_appearance(window,80,true);
            check(GetLayeredWindowAttributes(window,&key,&alpha,&flags)&&alpha==204,"blur request retains configured alpha");
            result=apply_appearance(window,80,true,true);
            check(result.alpha==255&&!result.blur_enabled&&!(GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_LAYERED),"contrast mode stays opaque without blur");
            check(!IsWindowVisible(window),"appearance changes do not show hidden window");
        } catch(...) { DestroyWindow(window); throw; }
        DestroyWindow(window);
    } catch(const std::exception& e) { std::cerr << e.what() << '\n'; ++failures; }
    std::cout << (failures?"Appearance checks failed":"Appearance checks passed") << '\n'; return failures?1:0;
}
