#pragma once
#include "common.h"
#include <memory>

namespace recall {
// A compositor-owned surface behind the layered GDI palette. Blur must not
// share the palette's constant-alpha redirection or SetWindowRgn clipping.
class GlassBackdrop {
    struct State;
    std::unique_ptr<State> state_;
public:
    GlassBackdrop();
    ~GlassBackdrop();
    bool configure(bool enabled);
    void sync(HWND palette,int dpi,bool visible);
};
}
