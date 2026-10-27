#include "previews.h"
#include <algorithm>
#include <chrono>
#include <iostream>

using namespace recall;
int main() {
    int failures=0;
    auto check=[&](bool ok,const char* message) { if(!ok) { std::cerr << "FAIL: " << message << '\n'; ++failures; } };
    ULONG_PTR token{}; Gdiplus::GdiplusStartupInput startup;
    if(Gdiplus::GdiplusStartup(&token,&startup,nullptr)!=Gdiplus::Ok) return 1;
    GUID guid{}; CoCreateGuid(&guid); wchar_t name[40]{}; StringFromGUID2(guid,name,40);
    const auto temp=fs::temp_directory_path()/(L"ReCall-previews-"+std::wstring(name));
    fs::create_directories(temp);
    try {
        const auto first=(temp/L"first.png").wstring(),second=(temp/L"second.png").wstring();
        const CLSID png{0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};
        {
            Gdiplus::Bitmap bitmap(1600,900,PixelFormat32bppARGB);
            { Gdiplus::Graphics g(&bitmap); g.Clear(Gdiplus::Color(255,40,120,210)); }
            check(bitmap.Save(first.c_str(),&png,nullptr)==Gdiplus::Ok,"generated synthetic PNG fixture");
            check(bitmap.Save(second.c_str(),&png,nullptr)==Gdiplus::Ok,"generated second PNG fixture");
        }
        struct Update { uint64_t generation; std::wstring path; PreviewLoader::Bitmap bitmap; std::thread::id thread; };
        std::mutex mutex; std::condition_variable cv; std::vector<Update> updates;
        const auto caller=std::this_thread::get_id();
        auto wait=[&](uint64_t generation,const std::wstring& path) {
            std::unique_lock lock(mutex);
            auto find=[&] {return std::find_if(updates.begin(),updates.end(),[&](const auto& u){return u.generation==generation&&u.path==path;});};
            if(!cv.wait_for(lock,std::chrono::seconds(10),[&]{return find()!=updates.end();})) throw std::runtime_error("Preview callback timed out");
            return *find();
        };
        {
            PreviewLoader loader([&](uint64_t gen,const std::wstring& path,PreviewLoader::Bitmap bitmap) {
                { std::lock_guard lock(mutex); updates.push_back({gen,path,std::move(bitmap),std::this_thread::get_id()}); }
                cv.notify_all();
            });
            loader.submit(1,{first}); auto a=wait(1,first);
            check(a.thread!=caller,"PNG decoded off the caller thread");
            check(a.bitmap->GetWidth()==400&&a.bitmap->GetHeight()==225,"bounded preview preserves aspect ratio");
            fs::remove(first);
            loader.submit(2,{first}); auto cached=wait(2,first);
            check(cached.bitmap==a.bitmap,"preview cache reused after original file is removed");
            loader.submit(3,{(temp/L"missing.png").wstring(),second}); auto b=wait(3,second);
            check(b.bitmap!=nullptr,"missing image does not block subsequent previews");
            fs::remove(second);
            loader.submit(4,{}); // Switching routes must not discard cached thumbnails.
            loader.submit(5,{second,first}); wait(5,first);
            {
                std::lock_guard lock(mutex);
                auto hit=std::find_if(updates.begin(),updates.end(),[](const auto& u){return u.generation==5;});
                check(hit!=updates.end()&&hit->path==second,"selected preview loaded before remaining rows");
            }
            auto reused=wait(5,second);
            check(reused.bitmap==b.bitmap,"cache survives route changes");
        }
    } catch(const std::exception& e) { std::cerr << e.what() << '\n'; ++failures; }
    fs::remove_all(temp); Gdiplus::GdiplusShutdown(token);
    std::cout << (failures?"Preview checks failed":"Preview checks passed") << '\n'; return failures?1:0;
}
