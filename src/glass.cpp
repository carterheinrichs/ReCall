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
    GraphicsPath shell; rounded(shell,RectF(px(1),px(1),width-px(2),height-px(2)),px(28));
    Pen edge(&rim,std::max(1.f,px(1))); g.DrawPath(&edge,&shell);

    auto panel=[&](RectF bounds,float radius,Color fill) {
        GraphicsPath path; rounded(path,bounds,radius);
        SolidBrush brush(fill); g.FillPath(&brush,&path);
        LinearGradientBrush border(PointF(0,bounds.Y),PointF(0,bounds.GetBottom()),
            Color(80,181,181,181),Color(40,80,80,80));
        Pen pen(&border,std::max(.7f,px(1))); g.DrawPath(&pen,&path);
    };
    // Search field, selected row, and bottom tray are separate thin layers.
    panel(RectF(px(12),px(11),width-px(24),px(52)),px(22),
        dark?Color(31,0,0,0):Color(60,255,255,255));
    if(selection_y>=0&&selection_y<height-px(80))
        panel(RectF(px(12),static_cast<float>(selection_y),split-px(24),px(55)),px(20),
            dark?Color(48,90,90,90):Color(46,255,255,255));
    panel(RectF(px(12),height-px(50),width-px(24),px(40)),px(20),
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
}
