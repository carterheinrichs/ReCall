#include "backdrop.h"
#include "appearance.h"
#include <dwmapi.h>
#include <iostream>

using namespace recall;
namespace {
HWND current_backdrop() {
    HWND result{};
    EnumThreadWindows(GetCurrentThreadId(),[](HWND window,LPARAM context)->BOOL {
        wchar_t name[128]{}; GetClassNameW(window,name,128);
        if(wcscmp(name,L"ReCall.GlassBackdrop")==0) *reinterpret_cast<HWND*>(context)=window;
        return TRUE;
    },reinterpret_cast<LPARAM>(&result));
    return result;
}
}
int main() {
    ComInit com;
    int failures=0;
    auto check=[&](bool ok,const char* message) { if(!ok) { std::cerr << "FAIL: " << message << '\n'; ++failures; } };
    HWND palette=CreateWindowExW(WS_EX_TOOLWINDOW,L"STATIC",L"ReCall backdrop test",WS_POPUP,
        0,0,400,300,nullptr,nullptr,nullptr,nullptr);
    try {
        if(!palette) throw std::runtime_error("Cannot create test palette");
        apply_appearance(palette,60,false);
        HWND helper{};
        {
            GlassBackdrop backdrop;
            check(!backdrop.configure(false)&&!current_backdrop(),"disabled blur creates no helper window");
            const bool enabled=backdrop.configure(true);
            BOOL composition=FALSE; DwmIsCompositionEnabled(&composition);
            check(enabled==(composition!=FALSE),"explicit blur is independent of the Windows transparency preference");
            if(enabled) {
                helper=current_backdrop();
                check(helper&&helper!=palette,"blur has its own compositor surface");
                const auto style=GetWindowLongPtrW(helper,GWL_EXSTYLE);
                check((style&WS_EX_NOREDIRECTIONBITMAP)&&!(style&WS_EX_LAYERED),"blur does not share the layered GDI redirection path");
                check((style&WS_EX_NOACTIVATE)&&(style&WS_EX_TRANSPARENT),"blur cannot take keyboard focus or mouse clicks");
                HRGN region=CreateRectRgn(0,0,0,0);
                check(GetWindowRgn(helper,region)==ERROR,"blur is clipped by compositor geometry rather than a native window region");
                DeleteObject(region);
                backdrop.sync(palette,144,false);
                check(!IsWindowVisible(helper)&&!IsWindowVisible(palette),"hidden palette never exposes its backdrop");
                BYTE alpha{}; DWORD flags{}; COLORREF key{};
                check(GetLayeredWindowAttributes(palette,&key,&alpha,&flags)&&alpha==153,"creating blur preserves the palette's 60 percent opacity");
                backdrop.configure(false);
                check(!IsWindowVisible(helper),"turning blur off hides its surface");
                backdrop.configure(true);
                check(current_backdrop()==helper,"toggling blur reuses the surface without creating ghost windows");
            }
        }
        check(!helper||!IsWindow(helper),"destroying the backdrop releases its native window");
    } catch(const std::exception& error) { std::cerr << error.what() << '\n'; ++failures; }
    if(palette) DestroyWindow(palette);
    return failures?1:0;
}
