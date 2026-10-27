#include "clipboard.h"
#include <gdiplus.h>
#include <memory>
#include <thread>
#include <chrono>
#include <cstring>
#include <limits>

namespace recall {
namespace {
struct ClipboardLock {
    bool open{};
    explicit ClipboardLock(HWND owner) {
        for(int i=0;i<8&&!open;++i) { open=OpenClipboard(owner)!=FALSE; if(!open) std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
    }
    ~ClipboardLock() { if(open) CloseClipboard(); }
};
struct GlobalLockGuard {
    HGLOBAL handle; void* data;
    explicit GlobalLockGuard(HGLOBAL h):handle(h),data(GlobalLock(h)) {}
    ~GlobalLockGuard() { if(data) GlobalUnlock(handle); }
};
struct BitmapHandle { HBITMAP h{}; ~BitmapHandle() { if(h) DeleteObject(h); } };
std::unique_ptr<Gdiplus::Bitmap> read_dib(HGLOBAL h) {
    const SIZE_T size=GlobalSize(h); GlobalLockGuard lock(h);
    if(!lock.data||size<sizeof(BITMAPINFOHEADER)) return {};
    const auto* hdr=static_cast<BITMAPINFOHEADER*>(lock.data);
    if(hdr->biSize<sizeof(BITMAPINFOHEADER)||hdr->biSize>size||hdr->biWidth<=0||hdr->biHeight==0||hdr->biHeight==LONG_MIN||hdr->biPlanes!=1) return {};
    if(hdr->biCompression!=BI_RGB&&hdr->biCompression!=BI_BITFIELDS) return {};
    if(hdr->biBitCount!=1&&hdr->biBitCount!=4&&hdr->biBitCount!=8&&hdr->biBitCount!=16&&hdr->biBitCount!=24&&hdr->biBitCount!=32) return {};
    size_t offset=hdr->biSize;
    if(hdr->biCompression==BI_BITFIELDS&&hdr->biSize==sizeof(BITMAPINFOHEADER)) offset+=3*sizeof(DWORD);
    size_t colors=hdr->biClrUsed?hdr->biClrUsed:(hdr->biBitCount<=8?(size_t{1}<<hdr->biBitCount):0);
    if(colors>256) return {};
    offset+=colors*sizeof(RGBQUAD);
    const uint64_t stride=((static_cast<uint64_t>(hdr->biWidth)*hdr->biBitCount+31)/32)*4;
    const uint64_t bytes=stride*static_cast<uint64_t>(std::abs(hdr->biHeight));
    if(bytes>128*1024*1024||offset>size||bytes>size-offset) return {};
    HDC dc=GetDC(nullptr);
    BitmapHandle bmp{CreateDIBitmap(dc,hdr,CBM_INIT,static_cast<BYTE*>(lock.data)+offset,static_cast<BITMAPINFO*>(lock.data),DIB_RGB_COLORS)};
    ReleaseDC(nullptr,dc);
    if(!bmp.h) return {};
    std::unique_ptr<Gdiplus::Bitmap> source(Gdiplus::Bitmap::FromHBITMAP(bmp.h,nullptr));
    if(!source||source->GetLastStatus()!=Gdiplus::Ok) return {};
    return std::unique_ptr<Gdiplus::Bitmap>(source->Clone(0,0,source->GetWidth(),source->GetHeight(),PixelFormat32bppARGB));
}
}
std::optional<Item> capture_clipboard(HWND owner,const fs::path& media,DWORD sequence) {
    std::unique_ptr<Gdiplus::Bitmap> bitmap;
    {
        ClipboardLock clipboard(owner);
        if(!clipboard.open||GetClipboardSequenceNumber()!=sequence) return {};
        if(IsClipboardFormatAvailable(CF_DIBV5)) bitmap=read_dib(GetClipboardData(CF_DIBV5));
        if(!bitmap&&IsClipboardFormatAvailable(CF_DIB)) bitmap=read_dib(GetClipboardData(CF_DIB));
        if(!bitmap&&IsClipboardFormatAvailable(CF_BITMAP)) {
            std::unique_ptr<Gdiplus::Bitmap> source(Gdiplus::Bitmap::FromHBITMAP(static_cast<HBITMAP>(GetClipboardData(CF_BITMAP)),nullptr));
            if(source&&source->GetLastStatus()==Gdiplus::Ok&&static_cast<uint64_t>(source->GetWidth())*source->GetHeight()<=32*1024*1024)
                bitmap.reset(source->Clone(0,0,source->GetWidth(),source->GetHeight(),PixelFormat32bppARGB));
        }
        if(!bitmap&&IsClipboardFormatAvailable(CF_UNICODETEXT)) {
            HGLOBAL h=GetClipboardData(CF_UNICODETEXT); const size_t size=GlobalSize(h);
            if(size>16*1024*1024) return {};
            GlobalLockGuard lock(h); if(!lock.data) return {};
            auto data=static_cast<const wchar_t*>(lock.data); size_t n=0;
            while(n<size/sizeof(wchar_t)&&data[n]) ++n;
            if(!n||n==size/sizeof(wchar_t)) return {};
            Item item; item.kind=Kind::Text; item.value.assign(data,n); return item;
        }
    }
    if(!bitmap) return {};
    GUID guid{}; if(FAILED(CoCreateGuid(&guid))) throw std::runtime_error("Cannot create image identifier");
    wchar_t name[40]{}; StringFromGUID2(guid,name,40);
    fs::path path=media/(std::wstring(name)+L".png");
    const CLSID png{0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};
    if(bitmap->Save(path.c_str(),&png,nullptr)!=Gdiplus::Ok) throw std::runtime_error("Cannot save clipboard PNG");
    Item item; item.kind=Kind::Image; item.value=path.wstring(); return item;
}
DWORD populate_clipboard(HWND owner,const Item& item) {
    UINT format=CF_UNICODETEXT; HGLOBAL memory{};
    if(item.kind==Kind::Image) {
        Gdiplus::Bitmap image(item.value.c_str());
        const UINT w=image.GetWidth(),h=image.GetHeight();
        if(image.GetLastStatus()!=Gdiplus::Ok||!w||!h||static_cast<uint64_t>(w)*h>32*1024*1024) throw std::runtime_error("Cannot load clipboard image");
        const size_t bytes=static_cast<size_t>(w)*h*4;
        memory=GlobalAlloc(GMEM_MOVEABLE,sizeof(BITMAPINFOHEADER)+bytes);
        if(!memory) throw std::bad_alloc();
        GlobalLockGuard lock(memory);
        if(!lock.data) { GlobalFree(memory); throw std::bad_alloc(); }
        BITMAPINFOHEADER header{}; header.biSize=sizeof(header); header.biWidth=static_cast<LONG>(w); header.biHeight=-static_cast<LONG>(h);
        header.biPlanes=1; header.biBitCount=32; header.biCompression=BI_RGB; header.biSizeImage=static_cast<DWORD>(bytes);
        std::memcpy(lock.data,&header,sizeof(header));
        Gdiplus::Rect rect(0,0,w,h); Gdiplus::BitmapData data{};
        if(image.LockBits(&rect,Gdiplus::ImageLockModeRead,PixelFormat32bppARGB,&data)!=Gdiplus::Ok) {
            GlobalUnlock(memory); lock.data=nullptr; GlobalFree(memory); throw std::runtime_error("Cannot decode image");
        }
        for(UINT y=0;y<h;++y) std::memcpy(static_cast<BYTE*>(lock.data)+sizeof(header)+static_cast<size_t>(y)*w*4,
            static_cast<BYTE*>(data.Scan0)+static_cast<ptrdiff_t>(y)*data.Stride,static_cast<size_t>(w)*4);
        image.UnlockBits(&data); format=CF_DIB;
    } else {
        memory=GlobalAlloc(GMEM_MOVEABLE,(item.value.size()+1)*sizeof(wchar_t));
        if(!memory) throw std::bad_alloc();
        GlobalLockGuard lock(memory);
        if(!lock.data) { GlobalFree(memory); throw std::bad_alloc(); }
        std::memcpy(lock.data,item.value.c_str(),(item.value.size()+1)*sizeof(wchar_t));
    }
    ClipboardLock clipboard(owner);
    if(!clipboard.open) { GlobalFree(memory); throw std::runtime_error("Clipboard is busy"); }
    if(!EmptyClipboard()||!SetClipboardData(format,memory)) { GlobalFree(memory); throw std::runtime_error("Cannot populate clipboard"); }
    return GetClipboardSequenceNumber();
}
}
