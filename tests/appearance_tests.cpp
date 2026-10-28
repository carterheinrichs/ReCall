#include "appearance.h"
#include "config.h"
#include "theme.h"
#include <iostream>

using namespace recall;
int main() {
    int failures=0;
    auto check=[&](bool ok,const char* message) { if(!ok) { std::cerr << "FAIL: " << message << '\n'; ++failures; } };
    try {
        auto defaults=Config::parse("{}");
        check(defaults.opacity==80&&defaults.blur,"existing configuration gets 80 percent opacity and blur by default");
        check(defaults.theme==ThemeStyle::Default&&defaults.color_mode==ColorMode::System,"default look follows Windows");
        auto baseline=system_theme();
        auto unchanged=resolve_theme(baseline,ColorMode::System,ThemeStyle::Default);
        check(unchanged.background==baseline.background&&unchanged.selection==baseline.selection&&unchanged.border==baseline.border,"default palette unchanged");
        baseline.high_contrast=false;
        for(auto style:{ThemeStyle::Default,ThemeStyle::LiquidGlass,ThemeStyle::Windows95,ThemeStyle::Windows7}) {
            auto light=resolve_theme(baseline,ColorMode::Light,style),dark=resolve_theme(baseline,ColorMode::Dark,style);
            check(!light.dark&&dark.dark&&light.background!=dark.background,"each theme supports explicit light and dark");
            baseline.high_contrast=true;
            auto contrast=resolve_theme(baseline,ColorMode::Dark,style);
            check(contrast.background==baseline.background&&contrast.selection==baseline.selection,"contrast colors override theme");
            baseline.high_contrast=false;
        }
        check(Config::parse(R"({"appearance":{"theme":"liquid_glass","mode":"dark"}})").theme==ThemeStyle::LiquidGlass,"glass setting parsed");
        auto classic=Config::parse(R"({"appearance":{"theme":"classic_windows","mode":"light"}})");
        check(classic.theme==ThemeStyle::Windows95&&classic.color_mode==ColorMode::Light,"classic and mode settings parsed");
        check(Config::parse(R"({"appearance":{"theme":"windows_95"}})").theme==ThemeStyle::Windows95,"Windows 95 setting parsed");
        check(Config::parse(R"({"appearance":{"theme":"windows_7"}})").theme==ThemeStyle::Windows7,"Windows 7 setting parsed");
        auto win95=resolve_theme(baseline,ColorMode::Light,ThemeStyle::Windows95);
        auto win7=resolve_theme(baseline,ColorMode::Light,ThemeStyle::Windows7);
        check(win95.background!=win7.background&&win95.selection!=win7.selection,"95 and 7 have distinct palettes");
        auto custom=Config::parse(R"({"appearance":{"opacity":95,"blur":false}})");
        check(custom.opacity==95&&!custom.blur,"custom appearance settings parsed");
        check(Config::parse(R"({"appearance":{"opacity":100}})").blur,"partial settings retain defaults");
        for(auto invalid:{R"({"appearance":{"opacity":0}})",R"({"appearance":{"opacity":101}})",
            R"({"appearance":{"opacity":79.5}})",R"({"appearance":{"opacity":"80"}})",
            R"({"appearance":{"mode":"sepia"}})",R"({"appearance":{"theme":"unknown"}})",
            R"({"appearance":{"mode":true}})",R"({"appearance":{"theme":null}})",R"({"appearance":{"blur":1}})",R"({"appearance":{"blur":"true"}})"}) {
            bool rejected=false; try { Config::parse(invalid); } catch(const std::exception&) { rejected=true; }
            check(rejected,"invalid appearance setting rejected");
        }
        HWND window=CreateWindowExW(WS_EX_TOOLWINDOW,L"STATIC",L"ReCall appearance test",WS_POPUP,0,0,400,300,nullptr,nullptr,nullptr,nullptr);
        if(!window) throw std::runtime_error("Cannot create test window");
        try {
            apply_glass_backdrop(window,true,false);
            check(!(GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_LAYERED),"glass backdrop does not fade foreground with layered alpha");
            check(!apply_glass_backdrop(window,false,false),"glass backdrop can be reset for default theme");
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
