#include "appearance.h"
#include "config.h"
#include "theme.h"
#include "glass.h"
#include <gdiplus.h>
#include <dwmapi.h>
#include <iostream>
#include <algorithm>

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
        for(auto style:{ThemeStyle::Default,ThemeStyle::LiquidGlass}) {
            auto light=resolve_theme(baseline,ColorMode::Light,style),dark=resolve_theme(baseline,ColorMode::Dark,style);
            check(!light.dark&&dark.dark&&light.background!=dark.background,"each theme supports explicit light and dark");
            baseline.high_contrast=true;
            auto contrast=resolve_theme(baseline,ColorMode::Dark,style);
            check(contrast.background==baseline.background&&contrast.selection==baseline.selection,"contrast colors override theme");
            baseline.high_contrast=false;
        }
        check(Config::parse(R"({"appearance":{"theme":"liquid_glass","mode":"dark"}})").theme==ThemeStyle::LiquidGlass,"glass setting parsed");
        for(auto removed:{"classic_windows","windows_95","windows_7"}) {
            bool rejected=false;
            try { Config::parse(std::string("{\"appearance\":{\"theme\":\"")+removed+"\"}}"); }
            catch(const std::exception&) { rejected=true; }
            check(rejected,"removed Windows themes rejected with supported values in error");
        }
        ULONG_PTR token{}; Gdiplus::GdiplusStartupInput input;
        if(Gdiplus::GdiplusStartup(&token,&input,nullptr)!=Gdiplus::Ok) throw std::runtime_error("GDI+ startup failed");
        {
            constexpr int width=300,height=220;
            std::vector<DWORD> pixels(width*height);
            draw_glass_material(pixels.data(),width,height,96,true,true,82,180);
            auto material=pixels;
            check((material[75*width+30]>>24)==26,"reference dark glass uses ten percent tint rather than dense acrylic");
            check((material[100*width+30]>>24)>26&&(material[100*width+30]>>24)<100,"selected panel remains translucent");
            prepare_glass_content(pixels.data(),material);
            pixels[75*width+30]=0x00ffffff; // GDI text clears alpha.
            RECT image{210,100,220,110};
            finish_glass_content(pixels.data(),material,width,height,std::span<const RECT>(&image,1));
            check(pixels[75*width+30]==0xffffffff,"foreground text regains opaque alpha");
            check(pixels[76*width+30]==material[76*width+30],"unpainted material retains original alpha");
            check((pixels[100*width+210]>>24)==255,"image matching tint is still opaque");
            draw_glass_material(pixels.data(),width,height,96,false,false,-1,180);
            check((pixels[75*width+30]>>24)==255,"unavailable blur uses solid material fallback");
            draw_glass_material(pixels.data(),width,height,96,true,true,-1,180);
            auto before_text=pixels;
            auto dc=CreateCompatibleDC(nullptr);
            auto font=CreateFontW(-24,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Segoe UI");
            draw_glass_text(pixels.data(),width,height,dc,L"Glass",{45,65,150,115},RGB(255,255,255),font,DT_LEFT|DT_SINGLELINE);
            bool text_drawn=false,premultiplied=true;
            for(size_t i=0;i<pixels.size();++i) {
                if(pixels[i]!=before_text[i]) text_drawn=true;
                auto pixel=pixels[i],a=pixel>>24;
                if((pixel&255)>a||((pixel>>8)&255)>a||((pixel>>16)&255)>a) premultiplied=false;
            }
            check(text_drawn&&premultiplied,"glass text renders coverage without opaque dark halos");
            check(pixels[75*width+30]==before_text[75*width+30],"drawing text preserves surrounding translucent material");
            DeleteObject(font); DeleteDC(dc);
        }
        Gdiplus::GdiplusShutdown(token);
        for(int dpi:{96,120,144,192}) {
            const int width=MulDiv(900,dpi,96);
            const auto field=glass_search_bounds(width,dpi);
            for(int height:{MulDiv(17,dpi,96),MulDiv(21,dpi,96)}) {
                const auto edit=glass_search_edit_bounds(width,dpi,height);
                check(field.left+field.right==width&&edit.left+edit.right==width,"search field and input share the horizontal center at every DPI");
                check(abs((edit.top-field.top)-(field.bottom-edit.bottom))<=1,"search input has equal top and bottom padding for the measured font");
                check(edit.bottom-edit.top==height,"search input uses the font line height rather than a taller fixed control");
            }
        }
        for(bool dark:{false,true}) {
            std::vector<DWORD> pixels(81,dark?0x1a000000u:0x4c4c4c4cu);
            const DWORD glyph=255;
            blend_glass_text_mask(pixels.data(),9,9,&glyph,1,1,{4,4},dark?RGB(250,250,250):RGB(24,24,24),96);
            const auto ink=pixels[4*9+4],outline=pixels[4*9+3];
            // Composite white text over a white window, and dark text over a
            // black window. Both need a visible opposing local edge.
            const auto ink_red=(ink>>16)&255,edge_red=(outline>>16)&255;
            check(dark?(ink_red>235&&edge_red+255-(outline>>24)<80):(ink_red<40&&edge_red>200),"text retains a high-contrast edge over the opposite background");
            check(pixels[0]==(dark?0x1a000000u:0x4c4c4c4cu),"contrast treatment leaves surrounding glass untouched");
        }
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
            apply_window_shape(window,true,400,300,96);
            HRGN shape=CreateRectRgn(0,0,0,0);
            check(GetWindowRgn(window,shape)!=ERROR,"glass has an actual window clipping region");
            check(!PtInRegion(shape,0,0)&&!PtInRegion(shape,399,0)&&!PtInRegion(shape,0,299)&&!PtInRegion(shape,399,299),"all four native corners are excluded");
            check(PtInRegion(shape,200,0)&&PtInRegion(shape,200,150),"clipping preserves the straight edges and content");
            check(PtInRegion(shape,32,8),"96 DPI curve follows the 48 DIP material radius");
            BOOL frame=TRUE;
            if(SUCCEEDED(DwmGetWindowAttribute(window,DWMWA_NCRENDERING_ENABLED,&frame,sizeof(frame))))
                check(!frame,"glass does not retain a native frame behind its outline");
            apply_window_shape(window,true,400,300,192);
            GetWindowRgn(window,shape);
            check(!PtInRegion(shape,32,8),"window clipping radius follows DPI changes");
            apply_window_shape(window,false,400,300,96);
            check(GetWindowRgn(window,shape)==ERROR,"default theme restores the native window outline");
            DeleteObject(shape);
            auto result=apply_appearance(window,80,false);
            BYTE alpha{}; DWORD flags{}; COLORREF key{};
            check(GetLayeredWindowAttributes(window,&key,&alpha,&flags)&&alpha==204&&(flags&LWA_ALPHA),"80 percent native window alpha applied");
            check(result.alpha==204&&!result.blur_enabled,"opacity and blur independently controlled");
            check((GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_TOOLWINDOW)!=0,"existing window styles preserved");
            // The same native alpha must work for glass, even without blur.
            for(bool blur:{false,true}) {
                for(unsigned opacity:{1u,20u,60u,80u,99u}) {
                    apply_glass_backdrop(window,false,false);
                    apply_appearance(window,opacity,false);
                    apply_glass_backdrop(window,blur,false);
                    apply_window_shape(window,true,400,300,96);
                    check(GetLayeredWindowAttributes(window,&key,&alpha,&flags)&&alpha==MulDiv(opacity,255,100),"glass opacity reaches the native window independently of blur");
                }
            }
            apply_window_shape(window,false,400,300,96);
            apply_appearance(window,100,false);
            check(!(GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_LAYERED),"100 percent restores non-layered window");
            apply_appearance(window,80,true);
            check(GetLayeredWindowAttributes(window,&key,&alpha,&flags)&&alpha==204,"blur request retains configured alpha");
            result=apply_appearance(window,80,true,true);
            check(result.alpha==255&&!result.blur_enabled&&!(GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_LAYERED),"contrast mode stays opaque without blur");
            // Exercise the real per-pixel presenter after a constant-alpha
            // window: Windows requires the layered-style state to be reset.
            auto dc=CreateCompatibleDC(nullptr);
            BITMAPINFO info{}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth=400; info.bmiHeader.biHeight=-300;
            info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32;
            void* bits=nullptr;
            auto bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);
            if(!bitmap) { DeleteDC(dc); throw std::runtime_error("Cannot create presentation test surface"); }
            auto old=SelectObject(dc,bitmap);
            auto surface=static_cast<DWORD*>(bits);
            std::fill_n(surface,400*300,0x1a000000u);
            surface[200*400+200]=0xffffffff;
            apply_appearance(window,60,false);
            prepare_glass_window(window);
            apply_window_shape(window,true,400,300,96);
            for(unsigned opacity:{20u,60u,100u}) {
                check(present_glass_window(window,dc,{400,300},opacity)==MulDiv(opacity,255,100),"per-pixel presentation preserves configured window opacity");
                check(surface[0]==0x1a000000&&surface[200*400+200]==0xffffffff,"presenting retains thin material and opaque foreground alpha");
            }
            apply_window_shape(window,false,400,300,96);
            apply_appearance(window,80,false);
            check(GetLayeredWindowAttributes(window,&key,&alpha,&flags)&&alpha==204,"returning from glass restores default constant-alpha rendering");
            SelectObject(dc,old); DeleteObject(bitmap); DeleteDC(dc);
            check(!IsWindowVisible(window),"appearance changes do not show hidden window");
        } catch(...) { DestroyWindow(window); throw; }
        DestroyWindow(window);
    } catch(const std::exception& e) { std::cerr << e.what() << '\n'; ++failures; }
    std::cout << (failures?"Appearance checks failed":"Appearance checks passed") << '\n'; return failures?1:0;
}
