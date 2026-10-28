#pragma once
#include "common.h"
namespace recall {
enum class ColorMode { System, Light, Dark };
enum class ThemeStyle { Default, LiquidGlass, ClassicWindows };
struct Config {
    UINT modifiers = MOD_WIN;
    UINT key = VK_SPACE;
    bool hook_fallback = true;
    bool auto_paste = true;
    unsigned history_limit = 1000;
    unsigned opacity = 80;
    bool blur = true;
    ColorMode color_mode = ColorMode::System;
    ThemeStyle theme = ThemeStyle::Default;
    void save_appearance() const;
    std::wstring clipboard_prefix = L">", emoji_prefix = L":";
    fs::path state;
    static Config load();
    static Config parse(std::string_view text);
};
}
