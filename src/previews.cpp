#include "previews.h"
#include <algorithm>
#include <map>
#include <shellapi.h>
#include <shlobj.h>
#include <commoncontrols.h>
#include <wincodec.h>
#include <wrl/client.h>

namespace recall {
namespace {
PreviewLoader::Bitmap load_icon(const std::wstring& path) {
    // Use the original link's icon index to retain custom icons, but request the
    // base image without the shortcut overlay that SHGFI_ICON can add.
    SHFILEINFOW info{};
    if(!SHGetFileInfoW(path.c_str(),0,&info,sizeof(info),SHGFI_SYSICONINDEX)) return {};
    Microsoft::WRL::ComPtr<IImageList> images;
    if(FAILED(SHGetImageList(SHIL_EXTRALARGE,IID_PPV_ARGS(&images)))) return {};
    HICON icon{};
    if(FAILED(images->GetIcon(info.iIcon,ILD_TRANSPARENT,&icon))) return {};
    // GDI+ FromHICON loses alpha for some 32-bit icons. WIC understands both
    // alpha icons and legacy AND masks, including legitimate black pixels.
    Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
    Microsoft::WRL::ComPtr<IWICBitmap> source;
    HRESULT result=CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory));
    if(SUCCEEDED(result)) result=factory->CreateBitmapFromHICON(icon,&source);
    DestroyIcon(icon);
    if(FAILED(result)) return {};
    Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
    if(FAILED(factory->CreateFormatConverter(&converter))||
       FAILED(converter->Initialize(source.Get(),GUID_WICPixelFormat32bppPBGRA,WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom))) return {};
    UINT width{},height{};
    if(FAILED(converter->GetSize(&width,&height))||!width||!height) return {};
    auto bitmap=std::make_shared<Gdiplus::Bitmap>(width,height,PixelFormat32bppPARGB);
    if(!bitmap||bitmap->GetLastStatus()!=Gdiplus::Ok) return {};
    Gdiplus::Rect bounds(0,0,width,height);
    Gdiplus::BitmapData data{};
    if(bitmap->LockBits(&bounds,Gdiplus::ImageLockModeWrite,PixelFormat32bppPARGB,&data)!=Gdiplus::Ok) return {};
    result=converter->CopyPixels(nullptr,data.Stride,data.Stride*height,static_cast<BYTE*>(data.Scan0));
    bitmap->UnlockBits(&data);
    if(FAILED(result)) return {};
    return bitmap;
}
PreviewLoader::Bitmap load_preview(const std::wstring& path) {
    Gdiplus::Bitmap source(path.c_str());
    if(source.GetLastStatus()!=Gdiplus::Ok) return {};
    UINT w=source.GetWidth(),h=source.GetHeight();
    if(!w||!h||static_cast<uint64_t>(w)*h>32*1024*1024) return {};
    const double scale=std::min(1.0,400.0/std::max(w,h));
    auto bitmap=std::make_shared<Gdiplus::Bitmap>(std::max(1,static_cast<int>(w*scale)),std::max(1,static_cast<int>(h*scale)),PixelFormat32bppPARGB);
    if(bitmap->GetLastStatus()!=Gdiplus::Ok) return {};
    Gdiplus::Graphics graphics(bitmap.get());
    graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
    if(graphics.DrawImage(&source,0,0,static_cast<INT>(bitmap->GetWidth()),static_cast<INT>(bitmap->GetHeight()))!=Gdiplus::Ok) return {};
    return bitmap;
}
}
PreviewLoader::PreviewLoader(Callback callback,Mode mode):callback_(std::move(callback)),mode_(mode),thread_([this]{run();}) {}
PreviewLoader::~PreviewLoader() {
    { std::lock_guard lock(mutex_); stopping_=true; ++revision_; }
    cv_.notify_one(); thread_.join();
}
void PreviewLoader::submit(uint64_t generation,std::vector<std::wstring> paths) {
    { std::lock_guard lock(mutex_); pending_={generation,++revision_,std::move(paths)}; ready_=true; }
    cv_.notify_one();
}
void PreviewLoader::run() {
    ComInit com;
    struct Cached { Bitmap bitmap; uint64_t used; };
    std::map<std::wstring,Cached> cache;
    uint64_t tick=0;
    for(;;) {
        Request request;
        { std::unique_lock lock(mutex_); cv_.wait(lock,[&]{return stopping_||ready_;});
            if(stopping_) return; request=std::move(pending_); ready_=false; }
        for(const auto& path:request.paths) {
            if(request.revision!=revision_) break;
            try {
                Bitmap bitmap;
                if(auto it=cache.find(path);it!=cache.end()) { bitmap=it->second.bitmap; it->second.used=++tick; }
                else {
                    bitmap=mode_==Mode::AppIcons?load_icon(path):load_preview(path);
                    if(!bitmap&&mode_==Mode::Images) continue; // Cache icon failures too.
                    cache.emplace(path,Cached{bitmap,++tick});
                    if(cache.size()>(mode_==Mode::AppIcons?256u:32u)) {
                        auto oldest=std::min_element(cache.begin(),cache.end(),[](const auto& a,const auto& b){return a.second.used<b.second.used;});
                        cache.erase(oldest);
                    }
                }
                if(request.revision==revision_) callback_(request.generation,path,std::move(bitmap));
            } catch(const std::exception&) {
                // A missing/corrupt image must not interrupt navigation or other previews.
            }
        }
    }
}
}
