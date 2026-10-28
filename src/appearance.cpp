#include "appearance.h"
#include <dwmapi.h>

namespace recall {
namespace {
// The Win32 accent API is resolved dynamically. Composition effects can be
// unavailable/disabled; constant-alpha transparency is independent of this API.
struct AccentPolicy { int state; DWORD flags,color,animation; };
struct CompositionData { int attribute; void* data; SIZE_T size; };
using SetCompositionAttribute=BOOL(WINAPI*)(HWND,CompositionData*);
bool set_blur(HWND window,bool enabled,bool strong=false,bool dark=false) {
    auto user32=GetModuleHandleW(L"user32.dll");
    auto set=user32?reinterpret_cast<SetCompositionAttribute>(GetProcAddress(user32,"SetWindowCompositionAttribute")):nullptr;
    if(!set) return false;
    constexpr int accent_policy_attribute=19,accent_disabled=0,accent_blur_behind=3,accent_acrylic_blur=4;
    AccentPolicy policy{enabled?accent_blur_behind:accent_disabled,0,0,0};
    CompositionData data{accent_policy_attribute,&policy,sizeof(policy)};
    if(enabled&&strong) {
        // Acrylic provides the stronger diffusion missing from basic blur.
        // Its tint requires nonzero alpha; keep it minimal so our existing
        // glass palette and independent window opacity still control the look.
        policy.state=accent_acrylic_blur;
        policy.color=dark?0x01000000u:0x01ffffffu; // ABGR
        if(set(window,&data)) return true;
        // Composition effects are optional on some Windows/session setups.
        policy.state=accent_blur_behind; policy.color=0;
    }
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
    // Use acrylic diffusion with minimal native tint for a stronger blur.
    // Keep the glass tint in our renderer and opacity on the window itself.
    const int type=DWMSBT_NONE;
    DwmSetWindowAttribute(window,DWMWA_SYSTEMBACKDROP_TYPE,&type,sizeof(type));
    const bool blurred=set_blur(window,enabled,true,dark);
    // Keep the native frame out of the client area. Extending it behind our
    // larger rounded outline leaves the system's smaller corners exposed.
    MARGINS margins{};
    const bool extended=SUCCEEDED(DwmExtendFrameIntoClientArea(window,&margins));
    if(!extended&&blurred) set_blur(window,false);
    return blurred&&extended;
}
void apply_window_shape(HWND window,bool glass,int width,int height,int dpi) {
    const int corners=glass?DWMWCP_DONOTROUND:DWMWCP_ROUND;
    const DWMNCRENDERINGPOLICY frame=glass?DWMNCRP_DISABLED:DWMNCRP_USEWINDOWSTYLE;
    const COLORREF border=glass?DWMWA_COLOR_NONE:DWMWA_COLOR_DEFAULT;
    DwmSetWindowAttribute(window,DWMWA_WINDOW_CORNER_PREFERENCE,&corners,sizeof(corners));
    DwmSetWindowAttribute(window,DWMWA_NCRENDERING_POLICY,&frame,sizeof(frame));
    DwmSetWindowAttribute(window,DWMWA_BORDER_COLOR,&border,sizeof(border));
    if(glass) {
        // Match the painted 48-DIP shell; the region owns the complete window,
        // including its frame, and must not extend a pixel beyond its bounds.
        const int diameter=MulDiv(96,dpi,96);
        auto region=CreateRoundRectRgn(0,0,width,height,diameter,diameter);
        if(!region) throw std::runtime_error("Cannot create glass window shape");
        if(!SetWindowRgn(window,region,TRUE)) {
            DeleteObject(region); throw std::runtime_error("Cannot apply glass window shape");
        }
    } else if(!SetWindowRgn(window,nullptr,TRUE)) {
        throw std::runtime_error("Cannot restore window shape");
    }
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
void prepare_glass_window(HWND window) {
    const auto style=GetWindowLongPtrW(window,GWL_EXSTYLE);
    // SetLayeredWindowAttributes redirects GDI and discards material alpha.
    // Clear its state before switching to a supplied premultiplied surface.
    SetWindowLongPtrW(window,GWL_EXSTYLE,style&~static_cast<LONG_PTR>(WS_EX_LAYERED));
    SetWindowLongPtrW(window,GWL_EXSTYLE,style|WS_EX_LAYERED);
}
BYTE present_glass_window(HWND window,HDC source,SIZE size,unsigned opacity) {
    if(opacity<1||opacity>100) throw std::runtime_error("Window opacity must be from 1 to 100 percent");
    BLENDFUNCTION blend{AC_SRC_OVER,0,static_cast<BYTE>(MulDiv(static_cast<int>(opacity),255,100)),AC_SRC_ALPHA};
    POINT origin{};
    GdiFlush();
    if(!UpdateLayeredWindow(window,nullptr,nullptr,&size,source,&origin,0,&blend,ULW_ALPHA))
        throw std::runtime_error("Cannot present translucent glass window: "+std::to_string(GetLastError()));
    return blend.SourceConstantAlpha;
}
}
