#include "appearance.h"
#include <dwmapi.h>

namespace recall {
namespace {
// The Win32 accent API is resolved dynamically. Composition effects can be
// unavailable/disabled; constant-alpha transparency is independent of this API.
struct AccentPolicy { int state; DWORD flags,color,animation; };
struct CompositionData { int attribute; void* data; SIZE_T size; };
using SetCompositionAttribute=BOOL(WINAPI*)(HWND,CompositionData*);
bool set_blur(HWND window,bool enabled) {
    auto user32=GetModuleHandleW(L"user32.dll");
    auto set=user32?reinterpret_cast<SetCompositionAttribute>(GetProcAddress(user32,"SetWindowCompositionAttribute")):nullptr;
    if(!set) return false;
    constexpr int accent_policy_attribute=19,accent_disabled=0,accent_blur_behind=3;
    AccentPolicy policy{enabled?accent_blur_behind:accent_disabled,0,0,0};
    CompositionData data{accent_policy_attribute,&policy,sizeof(policy)};
    return set(window,&data)!=FALSE&&enabled;
}
}
bool apply_glass_backdrop(HWND window,bool enabled,bool dark) {
    DWORD transparency=1,size=sizeof(transparency);
    RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"EnableTransparency",RRF_RT_REG_DWORD,nullptr,&transparency,&size);
    BOOL composition=FALSE; DwmIsCompositionEnabled(&composition);
    enabled=enabled&&transparency&&composition;
    BOOL use_dark=dark;
    DwmSetWindowAttribute(window,DWMWA_USE_IMMERSIVE_DARK_MODE,&use_dark,sizeof(use_dark));
    const int type=enabled?DWMSBT_TRANSIENTWINDOW:DWMSBT_NONE;
    const bool supported=SUCCEEDED(DwmSetWindowAttribute(window,DWMWA_SYSTEMBACKDROP_TYPE,&type,sizeof(type)));
    MARGINS margins=enabled&&supported?MARGINS{-1,-1,-1,-1}:MARGINS{};
    const bool extended=SUCCEEDED(DwmExtendFrameIntoClientArea(window,&margins));
    return enabled&&supported&&extended;
}
AppearanceResult apply_appearance(HWND window,unsigned opacity,bool blur,bool high_contrast) {
    if(opacity<1||opacity>100) throw std::runtime_error("Window opacity must be from 1 to 100 percent");
    if(high_contrast) { opacity=100; blur=false; }
    AppearanceResult result;
    result.alpha=static_cast<BYTE>(MulDiv(static_cast<int>(opacity),255,100));
    const auto previous=GetWindowLongPtrW(window,GWL_EXSTYLE);
    const auto style=opacity<100?previous|WS_EX_LAYERED:previous&~static_cast<LONG_PTR>(WS_EX_LAYERED);
    if(style!=previous) {
        SetLastError(ERROR_SUCCESS);
        if(!SetWindowLongPtrW(window,GWL_EXSTYLE,style)&&GetLastError()!=ERROR_SUCCESS)
            throw std::runtime_error("Cannot set window opacity style");
        SetWindowPos(window,nullptr,0,0,0,0,SWP_FRAMECHANGED|SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
    }
    DWORD transparency=1,size=sizeof(transparency);
    RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"EnableTransparency",RRF_RT_REG_DWORD,nullptr,&transparency,&size);
    BOOL composition=FALSE; DwmIsCompositionEnabled(&composition);
    result.blur_enabled=set_blur(window,blur&&transparency&&composition);
    if(opacity<100&&!SetLayeredWindowAttributes(window,0,result.alpha,LWA_ALPHA))
        throw std::runtime_error("Cannot set window opacity");
    return result;
}
}
