#pragma once
#include "common.h"

namespace recall {
struct ThemeColors {
    COLORREF background=RGB(255,255,255), foreground=RGB(0,0,0), muted=RGB(96,96,96);
    COLORREF selection=RGB(0,120,215), selection_text=RGB(255,255,255), border=RGB(204,204,204);
    bool dark=false;
    bool high_contrast=false;
};
ThemeColors system_theme();
}
