#include "app.h"

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int) {
    recall::ComInit com;
    HANDLE mutex=CreateMutexW(nullptr,FALSE,L"Local\\ReCall.Desktop.Instance");
    if(!mutex) return 1;
    if(GetLastError()==ERROR_ALREADY_EXISTS) {
        if(HWND existing=FindWindowW(L"ReCall.Palette",nullptr)) PostMessageW(existing,WM_APP+9,0,0);
        CloseHandle(mutex); return 0;
    }
    ULONG_PTR token{}; Gdiplus::GdiplusStartupInput startup;
    if(Gdiplus::GdiplusStartup(&token,&startup,nullptr)!=Gdiplus::Ok) { CloseHandle(mutex); return 1; }
    int result=1;
    try { recall::App app(recall::Config::load()); result=app.run(instance); }
    catch(const std::exception& e) { MessageBoxW(nullptr,recall::wide(e.what()).c_str(),L"ReCall — startup error",MB_OK|MB_ICONERROR); }
    Gdiplus::GdiplusShutdown(token); CloseHandle(mutex); return result;
}
