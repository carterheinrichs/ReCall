#pragma once
#include "common.h"
#include <d2d1.h>
#include <dwrite.h>

namespace recall {
// DirectWrite/Direct2D preserves color glyphs; GDI DrawText only draws outlines.
class EmojiRenderer {
    ComPtr<ID2D1Factory> factory_;
    ComPtr<IDWriteFactory> write_;
    ComPtr<ID2D1DCRenderTarget> target_;
public:
    bool draw(HDC dc,const std::wstring& glyph,RECT bounds,float size,COLORREF fallback);
};
}
