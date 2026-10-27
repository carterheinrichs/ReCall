#include "previews.h"
#include <algorithm>
#include <map>

namespace recall {
namespace {
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
PreviewLoader::PreviewLoader(Callback callback):callback_(std::move(callback)),thread_([this]{run();}) {}
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
                    bitmap=load_preview(path);
                    if(!bitmap) continue;
                    cache.emplace(path,Cached{bitmap,++tick});
                    if(cache.size()>32) {
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
