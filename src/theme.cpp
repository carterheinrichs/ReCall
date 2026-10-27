#include "theme.h"
#include <roapi.h>
#include <winrt/Windows.UI.ViewManagement.h>

namespace recall {
namespace {
COLORREF blend(COLORREF background,COLORREF foreground,int percent) {
    auto channel=[&](int bg,int fg) { return (bg*(100-percent)+fg*percent)/100; };
    return RGB(channel(GetRValue(background),GetRValue(foreground)),
        channel(GetGValue(background),GetGValue(foreground)),channel(GetBValue(background),GetBValue(foreground)));
}
struct RuntimeInit {
    HRESULT hr=RoInitialize(RO_INIT_SINGLETHREADED);
    ~RuntimeInit() { if(SUCCEEDED(hr)) RoUninitialize(); }
};
}
ThemeColors system_theme() {
    ThemeColors colors;
    colors.background=GetSysColor(COLOR_WINDOW); colors.foreground=GetSysColor(COLOR_WINDOWTEXT);
    colors.selection=GetSysColor(COLOR_HIGHLIGHT); colors.selection_text=GetSysColor(COLOR_HIGHLIGHTTEXT);
    HIGHCONTRASTW contrast{sizeof(contrast)};
    const bool high_contrast=SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(contrast),&contrast,0)&&(contrast.dwFlags&HCF_HIGHCONTRASTON);
    if(high_contrast) {
        colors.muted=colors.foreground; colors.border=colors.foreground;
    } else {
        try {
            RuntimeInit runtime;
            if(FAILED(runtime.hr)) winrt::throw_hresult(runtime.hr);
            using namespace winrt::Windows::UI::ViewManagement;
            UISettings settings;
            auto bg=settings.GetColorValue(UIColorType::Background),fg=settings.GetColorValue(UIColorType::Foreground);
            colors.background=RGB(bg.R,bg.G,bg.B); colors.foreground=RGB(fg.R,fg.G,fg.B);
        } catch(const winrt::hresult_error&) {
            // Windows UISettings may be unavailable in a minimal/sessionless environment.
            DWORD light=1,bytes=sizeof(light);
            RegGetValueW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                L"AppsUseLightTheme",RRF_RT_REG_DWORD,nullptr,&light,&bytes);
            if(!light) { colors.background=RGB(0,0,0); colors.foreground=RGB(255,255,255); }
        }
        colors.muted=blend(colors.background,colors.foreground,65);
        colors.border=blend(colors.background,colors.foreground,20);
    }
    auto brightness=[](COLORREF c) { return GetRValue(c)*299+GetGValue(c)*587+GetBValue(c)*114; };
    colors.dark=brightness(colors.background)<brightness(colors.foreground);
    return colors;
}
}
