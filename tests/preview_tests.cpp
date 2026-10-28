#include "previews.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <fstream>
#include <shobjidl.h>
#include <wrl/client.h>

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
        {
            // A transparent icon containing opaque black and semitransparent red.
            // Comparing its shortcut also catches accidental Shell arrow overlays.
            const auto custom=temp/L"transparent.ico",shortcut=temp/L"custom.lnk";
            {
                std::ofstream file(custom,std::ios::binary);
                const WORD header[]{0,1,1};
                file.write(reinterpret_cast<const char*>(header),sizeof(header));
                const BYTE dimensions[]{32,32,0,0};
                file.write(reinterpret_cast<const char*>(dimensions),sizeof(dimensions));
                const WORD format[]{1,32};
                const DWORD entry[]{40+32*32*4+32*4,22};
                file.write(reinterpret_cast<const char*>(format),sizeof(format));
                file.write(reinterpret_cast<const char*>(entry),sizeof(entry));
                BITMAPINFOHEADER dib{}; dib.biSize=40; dib.biWidth=32; dib.biHeight=64; dib.biPlanes=1; dib.biBitCount=32;
                file.write(reinterpret_cast<const char*>(&dib),sizeof(dib));
                for(int y=0;y<32;++y) for(int x=0;x<32;++x) {
                    DWORD pixel=x>=8&&x<24&&y>=8&&y<24?(x<16?0xff000000:0x80800000):0;
                    file.write(reinterpret_cast<const char*>(&pixel),4);
                }
                for(int y=0;y<32;++y) { DWORD mask=0; file.write(reinterpret_cast<const char*>(&mask),4); }
            }
            ComInit com;
            Microsoft::WRL::ComPtr<IShellLinkW> link;
            check(SUCCEEDED(CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&link))),"created shortcut fixture");
            if(!link) throw std::runtime_error("Shell link unavailable");
            link->SetPath(L"C:\\Windows\\notepad.exe"); link->SetIconLocation(custom.c_str(),0);
            Microsoft::WRL::ComPtr<IPersistFile> persist; link.As(&persist);
            check(persist&&SUCCEEDED(persist->Save(shortcut.c_str(),TRUE)),"saved custom icon shortcut");
            PreviewLoader loader([&](uint64_t gen,const std::wstring& path,PreviewLoader::Bitmap bitmap) {
                { std::lock_guard lock(mutex); updates.push_back({gen,path,std::move(bitmap),std::this_thread::get_id()}); }
                cv.notify_all();
            },PreviewLoader::Mode::AppIcons);
            loader.submit(20,{custom.wstring(),shortcut.wstring()});
            auto direct=wait(20,custom.wstring()).bitmap,linked=wait(20,shortcut.wstring()).bitmap;
            check(direct&&linked,"custom icon and shortcut extracted");
            if(direct&&linked) {
                Gdiplus::Color corner,black,red;
                direct->GetPixel(0,0,&corner);
                direct->GetPixel(direct->GetWidth()*3/8,direct->GetHeight()/2,&black);
                direct->GetPixel(direct->GetWidth()*5/8,direct->GetHeight()/2,&red);
                check(corner.GetA()==0,"icon background remains transparent");
                check(black.GetA()>=250&&black.GetR()<=2,"opaque black artwork preserved");
                check(red.GetA()>100&&red.GetA()<160,"partial alpha preserved");
                bool equal=direct->GetWidth()==linked->GetWidth()&&direct->GetHeight()==linked->GetHeight();
                for(UINT y=0;equal&&y<direct->GetHeight();++y) for(UINT x=0;equal&&x<direct->GetWidth();++x) {
                    Gdiplus::Color a,b; direct->GetPixel(x,y,&a); linked->GetPixel(x,y,&b); equal=a.GetValue()==b.GetValue();
                }
                check(equal,"shortcut retains custom artwork without arrow overlay");
            }
        }
        {
            wchar_t system[MAX_PATH]{}; GetSystemDirectoryW(system,MAX_PATH);
            auto executable=(fs::path(system)/L"notepad.exe").wstring();
            PreviewLoader loader([&](uint64_t gen,const std::wstring& path,PreviewLoader::Bitmap bitmap) {
                { std::lock_guard lock(mutex); updates.push_back({gen,path,std::move(bitmap),std::this_thread::get_id()}); }
                cv.notify_all();
            },PreviewLoader::Mode::AppIcons);
            loader.submit(10,{executable}); auto icon=wait(10,executable);
            check(icon.thread!=caller,"Shell icons loaded off the caller thread");
            check(icon.bitmap&&icon.bitmap->GetWidth()>0,"application icon extracted");
            loader.submit(11,{executable});
            check(wait(11,executable).bitmap==icon.bitmap,"application icon bitmap reused from cache");
        }
    } catch(const std::exception& e) { std::cerr << e.what() << '\n'; ++failures; }
    fs::remove_all(temp); Gdiplus::GdiplusShutdown(token);
    std::cout << (failures?"Preview checks failed":"Preview checks passed") << '\n'; return failures?1:0;
}
