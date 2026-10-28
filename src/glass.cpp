#include "glass.h"
#include <gdiplus.h>
#include <algorithm>

namespace recall {
namespace {
void rounded(Gdiplus::GraphicsPath& path,Gdiplus::RectF r,float radius) {
    float d=std::min({radius*2,r.Width,r.Height});
    path.AddArc(r.X,r.Y,d,d,180,90);
    path.AddArc(r.GetRight()-d,r.Y,d,d,270,90);
    path.AddArc(r.GetRight()-d,r.GetBottom()-d,d,d,0,90);
    path.AddArc(r.X,r.GetBottom()-d,d,d,90,90);
    path.CloseFigure();
}
DWORD opaque_color(DWORD pixel) {
    const unsigned a=pixel>>24;
    if(!a) return 0xff000000;
    auto channel=[&](unsigned shift) { return std::min<DWORD>(255,((pixel>>shift)&255)*255/a); };
    return 0xff000000|(channel(16)<<16)|(channel(8)<<8)|channel(0);
}
}
RECT glass_search_bounds(int width,int dpi) {
    return {MulDiv(24,dpi,96),MulDiv(18,dpi,96),width-MulDiv(24,dpi,96),MulDiv(62,dpi,96)};
}
RECT glass_search_edit_bounds(int width,int dpi,int line_height) {
    auto bounds=glass_search_bounds(width,dpi);
    const int padding=MulDiv(12,dpi,96);
    line_height=std::clamp(line_height,1,static_cast<int>(bounds.bottom-bounds.top));
    const int top=bounds.top+(bounds.bottom-bounds.top-line_height)/2;
    return {bounds.left+padding,top,bounds.right-padding,top+line_height};
}
void draw_glass_material(DWORD* pixels,int width,int height,int dpi,bool dark,
    bool transparent,int selection_y,int split) {
    using namespace Gdiplus;
    Bitmap bitmap(width,height,width*4,PixelFormat32bppPARGB,reinterpret_cast<BYTE*>(pixels));
    Graphics g(&bitmap);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    // LiquidGlass2's GlassDark is #1A000000 (10% black). The light variant
    // uses white tint for legibility. Window opacity is applied separately.
    const BYTE tint=dark?26:76;
    g.Clear(transparent?Color(tint,dark?0:255,dark?0:255,dark?0:255):
        dark?Color(255,20,20,20):Color(255,246,246,246));
    auto px=[&](int value) { return static_cast<float>(MulDiv(value,dpi,96)); };
    // Directional rim: bright at the top/bottom, subdued through the middle.
    // These stops follow the reference's Root border instead of a white wash.
    const REAL stops[]={0,.2f,.3f,.65f,.75f,1};
    const Color colors[]={Color(105,181,181,181),Color(64,181,181,181),
        Color(80,48,48,48),Color(80,16,16,16),Color(80,48,48,48),Color(80,172,172,172)};
    LinearGradientBrush rim(PointF(0,0),PointF(0,static_cast<float>(height)),Color(255,255,255,255),Color(255,0,0,0));
    rim.SetInterpolationColors(colors,stops,6);
    GraphicsPath shell; rounded(shell,RectF(px(1),px(1),width-px(2),height-px(2)),px(48));
    Pen edge(&rim,std::max(1.f,px(1))); g.DrawPath(&edge,&shell);

    auto panel=[&](RectF bounds,float radius,Color fill) {
        GraphicsPath path; rounded(path,bounds,radius);
        SolidBrush brush(fill); g.FillPath(&brush,&path);
        LinearGradientBrush border(PointF(0,bounds.Y),PointF(0,bounds.GetBottom()),
            Color(80,181,181,181),Color(40,80,80,80));
        Pen pen(&border,std::max(.7f,px(1))); g.DrawPath(&pen,&path);
    };
    // Search field, selected row, and bottom tray are separate thin layers.
    const auto search=glass_search_bounds(width,dpi);
    panel(RectF(static_cast<REAL>(search.left),static_cast<REAL>(search.top),
        static_cast<REAL>(search.right-search.left),static_cast<REAL>(search.bottom-search.top)),px(22),
        dark?Color(31,0,0,0):Color(60,255,255,255));
    if(selection_y>=0&&selection_y<height-px(80))
        panel(RectF(px(12),static_cast<float>(selection_y),split-px(24),px(55)),px(20),
            dark?Color(48,90,90,90):Color(46,255,255,255));
    panel(RectF(px(24),height-px(56),width-px(48),px(40)),px(20),
        dark?Color(62,74,74,74):Color(76,255,255,255));
    Pen divider(dark?Color(30,255,255,255):Color(25,0,0,0),std::max(.5f,px(1)));
    g.DrawLine(&divider,static_cast<float>(split),px(86),static_cast<float>(split),height-px(68));
}
void prepare_glass_content(DWORD* pixels,std::span<const DWORD> material) {
    for(size_t i=0;i<material.size();++i) pixels[i]=opaque_color(material[i]);
}
void finish_glass_content(DWORD* pixels,std::span<const DWORD> material,
    int width,int height,std::span<const RECT> opaque_images) {
    for(size_t i=0;i<material.size();++i) {
        const DWORD color=pixels[i]&0x00ffffff;
        pixels[i]=color==(opaque_color(material[i])&0x00ffffff)?material[i]:0xff000000|color;
    }
    // A black/white image can match the tint exactly. It must still be opaque.
    for(const auto& r:opaque_images)
        for(int y=std::max(0L,r.top);y<std::min(static_cast<LONG>(height),r.bottom);++y)
            for(int x=std::max(0L,r.left);x<std::min(static_cast<LONG>(width),r.right);++x)
                pixels[static_cast<size_t>(y)*width+x]|=0xff000000;
}
void blend_glass_text_mask(DWORD* pixels,int width,int height,const DWORD* mask,int w,int h,
    POINT origin,COLORREF color,int dpi) {
    auto coverage=[&](int x,int y)->unsigned {
        if(x<0||y<0||x>=w||y>=h) return 0;
        const auto value=mask[static_cast<size_t>(y)*w+x];
        return std::max({value&255,(value>>8)&255,(value>>16)&255});
    };
    auto composite=[](DWORD& dst,COLORREF ink,unsigned a) {
        auto blend=[&](unsigned channel,unsigned shift) { return (channel*a+((dst>>shift)&255)*(255-a)+127)/255; };
        dst=((a+((dst>>24)*(255-a)+127)/255)<<24)|
            (blend(GetRValue(ink),16)<<16)|(blend(GetGValue(ink),8)<<8)|blend(GetBValue(ink),0);
    };
    const bool light=GetRValue(color)*299+GetGValue(color)*587+GetBValue(color)*114>=128000;
    const auto outline=light?RGB(0,0,0):RGB(255,255,255);
    const int radius=std::max(1,MulDiv(1,dpi,96));
    // A one-DIP opposing outline supplies local contrast on both white and
    // dark windows without flattening or darkening the glass material itself.
    for(int y=-radius;y<h+radius;++y) for(int x=-radius;x<w+radius;++x) {
        const int xx=origin.x+x,yy=origin.y+y;
        if(xx<0||yy<0||xx>=width||yy>=height) continue;
        unsigned a=0;
        for(int dy=-radius;dy<=radius;++dy) for(int dx=-radius;dx<=radius;++dx) {
            const unsigned weight=abs(dx)+abs(dy)<=radius?255:180;
            a=std::max(a,coverage(x+dx,y+dy)*weight/255);
        }
        if(a) composite(pixels[static_cast<size_t>(yy)*width+xx],outline,a*200/255);
    }
    for(int y=0;y<h;++y) for(int x=0;x<w;++x) {
        const int xx=origin.x+x,yy=origin.y+y;
        if(xx<0||yy<0||xx>=width||yy>=height) continue;
        if(const auto a=coverage(x,y)) composite(pixels[static_cast<size_t>(yy)*width+xx],color,a);
    }
}
void draw_glass_text(DWORD* pixels,int width,int height,HDC compatible,const std::wstring& text,
    RECT bounds,COLORREF color,HFONT font,UINT flags,int dpi) {
    const int w=bounds.right-bounds.left,h=bounds.bottom-bounds.top;
    if(text.empty()||w<=0||h<=0) return;
    BITMAPINFO info{}; info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=w; info.bmiHeader.biHeight=-h;
    info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32;
    void* data=nullptr;
    auto bitmap=CreateDIBSection(compatible,&info,DIB_RGB_COLORS,&data,nullptr,0);
    auto dc=bitmap?CreateCompatibleDC(compatible):nullptr;
    if(dc) {
        auto old=SelectObject(dc,bitmap),oldfont=SelectObject(dc,font);
        auto coverage=static_cast<DWORD*>(data);
        std::fill_n(coverage,static_cast<size_t>(w)*h,0);
        SetBkMode(dc,TRANSPARENT); SetTextColor(dc,RGB(255,255,255));
        RECT local{0,0,w,h};
        DrawTextW(dc,text.c_str(),static_cast<int>(text.size()),&local,flags|DT_NOPREFIX);
        GdiFlush();
        blend_glass_text_mask(pixels,width,height,coverage,w,h,{bounds.left,bounds.top},color,dpi);
        SelectObject(dc,oldfont); SelectObject(dc,old); DeleteDC(dc);
    }
    if(bitmap) DeleteObject(bitmap);
}
}
