#pragma once
#include "common.h"
#include <span>

namespace recall {
// Premultiplied BGRA material, kept separate from opaque GDI content.
void draw_glass_material(DWORD* pixels,int width,int height,int dpi,bool dark,
    bool transparent,int selection_y,int split);
void prepare_glass_content(DWORD* pixels,std::span<const DWORD> material);
void finish_glass_content(DWORD* pixels,std::span<const DWORD> material,
    int width,int height,std::span<const RECT> opaque_images);
}
