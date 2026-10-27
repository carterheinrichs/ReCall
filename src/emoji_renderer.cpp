#include "emoji_renderer.h"

namespace recall {
bool EmojiRenderer::draw(HDC dc,const std::wstring& glyph,RECT bounds,float size,COLORREF fallback) {
    if(bounds.right<=bounds.left||bounds.bottom<=bounds.top) return false;
    if(!factory_.p&&FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,factory_.put()))) return false;
    if(!write_.p&&FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,__uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(write_.put())))) return false;
    if(!target_.p) {
        auto props=D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,D2D1_ALPHA_MODE_IGNORE),96,96);
        if(FAILED(factory_->CreateDCRenderTarget(&props,target_.put()))) return false;
    }
    ComPtr<IDWriteTextFormat> format;
    if(FAILED(write_->CreateTextFormat(L"Segoe UI Emoji",nullptr,DWRITE_FONT_WEIGHT_NORMAL,
        DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,size,L"",format.put()))) return false;
    format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    format->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    if(FAILED(target_->BindDC(dc,&bounds))) return false;
    ComPtr<ID2D1SolidColorBrush> brush;
    if(FAILED(target_->CreateSolidColorBrush(D2D1::ColorF(GetRValue(fallback)/255.f,
        GetGValue(fallback)/255.f,GetBValue(fallback)/255.f),brush.put()))) return false;
    target_->BeginDraw();
    target_->DrawText(glyph.c_str(),static_cast<UINT32>(glyph.size()),format.p,
        D2D1::RectF(0,0,static_cast<float>(bounds.right-bounds.left),static_cast<float>(bounds.bottom-bounds.top)),
        brush.p,static_cast<D2D1_DRAW_TEXT_OPTIONS>(D2D1_DRAW_TEXT_OPTIONS_ENABLE_COLOR_FONT|D2D1_DRAW_TEXT_OPTIONS_CLIP));
    const auto hr=target_->EndDraw();
    if(hr==D2DERR_RECREATE_TARGET) { target_.p->Release(); target_.p=nullptr; }
    return SUCCEEDED(hr);
}
}
