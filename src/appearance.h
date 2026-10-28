#pragma once
#include "common.h"

namespace recall {
struct AppearanceResult {
    BYTE alpha=255;
    bool blur_enabled=false;
};
bool apply_glass_backdrop(HWND window,bool enabled,bool dark);
void apply_window_shape(HWND window,bool glass,int width,int height,int dpi);
AppearanceResult apply_appearance(HWND window,unsigned opacity,bool blur,bool high_contrast=false);
}
