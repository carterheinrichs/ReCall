#include "clipboard.h"
#include <gdiplus.h>
#include <iostream>
#include <limits>

using namespace recall;
int main() {
    int failures=0;
    auto check=[&](bool ok,const char* message) { if(!ok) { std::cerr<<"FAIL: "<<message<<'\n'; ++failures; } };
    check(clipboard_image_size_allowed(8192,4096),"128 MiB decoded image accepted");
    check(!clipboard_image_size_allowed(8192,8192),"small monochrome source cannot bypass decoded-size limit");
    check(!clipboard_image_size_allowed(0,1)&&!clipboard_image_size_allowed(1,0),"empty image rejected");
    check(!clipboard_image_size_allowed(std::numeric_limits<uint64_t>::max(),2),"dimension check cannot overflow");
    ULONG_PTR token{}; Gdiplus::GdiplusStartupInput startup;
    if(Gdiplus::GdiplusStartup(&token,&startup,nullptr)!=Gdiplus::Ok) return 1;
    GUID guid{}; CoCreateGuid(&guid); wchar_t name[40]{}; StringFromGUID2(guid,name,40);
    const auto directory=fs::temp_directory_path()/(L"ReCall-clipboard-test-"+std::wstring(name));
    fs::create_directories(directory);
    try {
        Item text; text.kind=Kind::Text; text.value=L"Full text 😀 "+std::wstring(20000,L'x');
        auto original=prepare_clipboard(text);
        auto prepared=std::move(original);
        check(!original.memory&&prepared.memory&&prepared.format==CF_UNICODETEXT,"prepared payload transfers ownership");
        auto data=static_cast<const wchar_t*>(GlobalLock(prepared.memory));
        check(data&&std::wstring_view(data)==text.value,"preparation preserves complete Unicode text");
        if(data) GlobalUnlock(prepared.memory);
        const CLSID png{0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};
        Item image; image.kind=Kind::Image; image.value=(directory/L"small.png").wstring();
        {
            Gdiplus::Bitmap bitmap(2,3,PixelFormat32bppARGB); bitmap.SetPixel(0,0,Gdiplus::Color(255,10,20,30));
            check(bitmap.Save(image.value.c_str(),&png,nullptr)==Gdiplus::Ok,"fixture image saved");
        }
        auto pixels=prepare_clipboard(image);
        auto header=static_cast<const BITMAPINFOHEADER*>(GlobalLock(pixels.memory));
        check(header&&pixels.format==CF_DIB&&header->biWidth==2&&header->biHeight==-3&&header->biBitCount==32,"image prepared as top-down 32-bit DIB");
        if(header) {
            auto first=reinterpret_cast<const BYTE*>(header)+sizeof(*header);
            check(first[0]==30&&first[1]==20&&first[2]==10,"decoded image pixel colors preserved");
            GlobalUnlock(pixels.memory);
        }
        image.value=(directory/L"oversized.png").wstring();
        {
            Gdiplus::Bitmap bitmap(8192,8192,PixelFormat1bppIndexed);
            check(bitmap.Save(image.value.c_str(),&png,nullptr)==Gdiplus::Ok,"oversized monochrome fixture saved");
        }
        bool rejected=false;
        try { auto invalid=prepare_clipboard(image); } catch(const std::runtime_error&) { rejected=true; }
        check(rejected,"oversized image rejected before decoding to a clipboard DIB");
        image.value=(directory/L"missing.png").wstring(); rejected=false;
        try { auto invalid=prepare_clipboard(image); } catch(const std::runtime_error&) { rejected=true; }
        check(rejected,"missing image fails without publishing a payload");
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; ++failures; }
    fs::remove_all(directory); Gdiplus::GdiplusShutdown(token);
    std::cout<<(failures?"Clipboard checks failed":"Clipboard checks passed")<<'\n';
    return failures?1:0;
}
