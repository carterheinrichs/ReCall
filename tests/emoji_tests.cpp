#include "emoji_renderer.h"
#include <iostream>
#include <algorithm>

using namespace recall;
int main() {
    ComInit com;
    HDC dc=CreateCompatibleDC(nullptr);
    BITMAPINFO info{};
    info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=128; info.bmiHeader.biHeight=-128;
    info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32;
    void* pixels{};
    HBITMAP bitmap=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
    if(!dc||!bitmap) return 1;
    auto old=SelectObject(dc,bitmap);
    int failures=0;
    {
        EmojiRenderer renderer;
        for(auto glyph:{L"🔥",L"😀",L"❤️",L"👩‍💻"}) {
            PatBlt(dc,0,0,128,128,WHITENESS);
            GdiFlush();
            const bool drawn=renderer.draw(dc,glyph,{0,0,128,128},64,RGB(0,0,0));
            GdiFlush();
            int colored=0;
            auto data=static_cast<const unsigned char*>(pixels);
            for(int i=0;i<128*128;++i) {
                auto b=data[i*4],g=data[i*4+1],r=data[i*4+2];
                if(std::max({r,g,b})-std::min({r,g,b})>40) ++colored;
            }
            if(!drawn||colored<100) { std::cerr<<"Emoji must render in color\n"; ++failures; }
        }
    }
    SelectObject(dc,old); DeleteObject(bitmap); DeleteDC(dc);
    return failures?1:0;
}
