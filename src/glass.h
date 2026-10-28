#pragma once
#include "common.h"
#include <span>

namespace recall {
RECT glass_search_bounds(int width,int dpi);
RECT glass_search_edit_bounds(int width,int dpi,int line_height);
// Premultiplied BGRA material, kept separate from opaque GDI content.
void draw_glass_material(DWORD* pixels,int width,int height,int dpi,bool dark,
    bool transparent,int selection_y,int split);
void prepare_glass_content(DWORD* pixels,std::span<const DWORD> material);
void finish_glass_content(DWORD* pixels,std::span<const DWORD> material,
    int width,int height,std::span<const RECT> opaque_images);
void draw_glass_text(DWORD* pixels,int width,int height,HDC compatible,const std::wstring& text,
    RECT bounds,COLORREF color,HFONT font,UINT flags,int dpi=96);
void blend_glass_text_mask(DWORD* pixels,int width,int height,const DWORD* mask,int mask_width,
    int mask_height,POINT origin,COLORREF color,int dpi);
}
