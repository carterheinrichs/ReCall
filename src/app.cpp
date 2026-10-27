#include "app.h"
#include "catalog.h"
#include "clipboard.h"
#include "search.h"
#include "editing.h"
#include <commctrl.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <algorithm>
#include <chrono>

namespace recall {
namespace {
constexpr UINT results_message=WM_APP+1, tray_message=WM_APP+2, changed_message=WM_APP+3, error_message=WM_APP+4;
constexpr UINT hotkey_id=1, paste_timer=2;
constexpr COLORREF background=RGB(27,29,35), foreground=RGB(238,239,245), muted=RGB(156,163,179), accent=RGB(54,65,91);
void fill(HDC dc,RECT r,COLORREF color) { HBRUSH b=CreateSolidBrush(color); FillRect(dc,&r,b); DeleteObject(b); }
void text(HDC dc,const std::wstring& s,RECT r,COLORREF color,HFONT font,UINT flags=DT_LEFT|DT_SINGLELINE|DT_VCENTER|DT_END_ELLIPSIS) {
    auto old=SelectObject(dc,font); SetBkMode(dc,TRANSPARENT); SetTextColor(dc,color);
    DrawTextW(dc,s.c_str(),static_cast<int>(s.size()),&r,flags|DT_NOPREFIX); SelectObject(dc,old);
}
std::wstring single_line(std::wstring s) {
    for(auto& c:s) if(c==L'\n'||c==L'\r'||c==L'\t') c=L' '; return s;
}
}
App* App::instance_{};
App::App(Config config):config_(std::move(config)),store_(std::make_unique<Store>(config_.state,config_.history_limit)) { instance_=this; }
App::~App() {
    if(hook_) UnhookWindowsHookEx(hook_);
    if(registered_) UnregisterHotKey(hwnd_,hotkey_id);
    if(listener_) RemoveClipboardFormatListener(hwnd_);
    { std::lock_guard lock(queue_mutex_); stopping_=true; jobs_.clear(); }
    queue_cv_.notify_all(); if(worker_.joinable()) worker_.join();
    if(hwnd_) { tray(false); if(IsWindow(hwnd_)) DestroyWindow(hwnd_); }
    if(font_) DeleteObject(font_); if(small_font_) DeleteObject(small_font_); if(emoji_font_) DeleteObject(emoji_font_);
    instance_=nullptr;
}
void App::enqueue(std::function<void()> job) {
    { std::lock_guard lock(queue_mutex_); if(stopping_) return; jobs_.push_back(std::move(job)); } queue_cv_.notify_one();
}
void App::report(std::wstring message) {
    { std::lock_guard lock(result_mutex_); error_=std::move(message); } PostMessageW(hwnd_,error_message,0,0);
}
void App::run_worker() {
    ComInit com;
    for(;;) {
        std::function<void()> job;
        { std::unique_lock lock(queue_mutex_); queue_cv_.wait(lock,[&]{return stopping_||!jobs_.empty();});
            if(stopping_) return; job=std::move(jobs_.front()); jobs_.pop_front(); }
        try { job(); } catch(const std::exception& e) { report(wide(e.what())); }
    }
}
void App::refresh() {
    enqueue([this] { apps_=index_apps(); if(emoji_.empty()) emoji_=load_emoji(); PostMessageW(hwnd_,changed_message,0,0); });
}
void App::query() {
    int length=GetWindowTextLengthW(edit_); std::wstring input(static_cast<size_t>(length)+1,L'\0');
    GetWindowTextW(edit_,input.data(),length+1); input.resize(length);
    auto q=route_query(input,config_.clipboard_prefix,config_.emoji_prefix);
    auto generation=++generation_;
    enqueue([this,q=std::move(q),generation] {
        if(generation!=generation_) return;
        Snapshot result; result.generation=generation;
        auto candidates=q.route==Route::Apps?apps_:q.route==Route::Emoji?emoji_:store_->recent();
        // Clipboard stays in recency order; fuzzy matching only filters it.
        if(q.route==Route::Clipboard) {
            for(auto& i:candidates) if(fuzzy_score(q.text,std::wstring_view(i.title).substr(0,8192))>=0) {
                result.items.push_back(std::move(i)); if(result.items.size()==100) break;
            }
        } else result.items=ranked(std::move(candidates),q.text);
        for(auto& i:result.items) if(i.kind==Kind::Image) {
            if(generation!=generation_) return;
            if(auto cached=thumbnail_cache_.find(i.value);cached!=thumbnail_cache_.end()) {
                result.images.emplace(i.value,cached->second); continue;
            }
            Gdiplus::Bitmap source(i.value.c_str()); UINT w=source.GetWidth(),h=source.GetHeight();
            if(source.GetLastStatus()!=Gdiplus::Ok||!w||!h) continue;
            double scale=std::min(1.0,400.0/std::max(w,h));
            auto bitmap=std::make_shared<Gdiplus::Bitmap>(std::max(1,static_cast<int>(w*scale)),std::max(1,static_cast<int>(h*scale)),PixelFormat32bppPARGB);
            Gdiplus::Graphics g(bitmap.get()); g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            g.DrawImage(&source,0,0,static_cast<INT>(bitmap->GetWidth()),static_cast<INT>(bitmap->GetHeight()));
            result.images.emplace(i.value,std::move(bitmap));
        }
        thumbnail_cache_=result.images;
        result.status=(q.route==Route::Apps?L"Applications":q.route==Route::Clipboard?L"Clipboard history":L"Unicode 16 emoji");
        result.status+=L"  ·  "+std::to_wstring(result.items.size())+L" results";
        if(generation!=generation_) return;
        { std::lock_guard lock(result_mutex_); pending_=std::move(result); }
        PostMessageW(hwnd_,results_message,0,0);
    });
}
void App::capture() {
    DWORD seq=GetClipboardSequenceNumber(); clipboard_sequence_=seq;
    if(seq==own_sequence_) return;
    enqueue([this,seq] {
        if(seq!=clipboard_sequence_||seq==own_sequence_) return;
        auto item=capture_clipboard(hwnd_,config_.state/L"clips_media",seq);
        if(item) { store_->insert(*item); PostMessageW(hwnd_,changed_message,0,0); }
    });
}
void App::fonts() {
    if(font_) DeleteObject(font_); if(small_font_) DeleteObject(small_font_); if(emoji_font_) DeleteObject(emoji_font_);
    font_=CreateFontW(-px(17),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    small_font_=CreateFontW(-px(12),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    emoji_font_=CreateFontW(-px(28),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI Emoji");
    if(edit_) SendMessageW(edit_,WM_SETFONT,reinterpret_cast<WPARAM>(font_),TRUE);
}
void App::position() {
    POINT point{}; GetCursorPos(&point); MONITORINFO monitor{sizeof(monitor)};
    HMONITOR handle=MonitorFromPoint(point,MONITOR_DEFAULTTONEAREST); GetMonitorInfoW(handle,&monitor);
    const int w=std::min(px(900),static_cast<int>(monitor.rcWork.right-monitor.rcWork.left)),h=std::min(px(540),static_cast<int>(monitor.rcWork.bottom-monitor.rcWork.top));
    SetWindowPos(hwnd_,HWND_TOPMOST,monitor.rcWork.left+(monitor.rcWork.right-monitor.rcWork.left-w)/2,
        monitor.rcWork.top+(monitor.rcWork.bottom-monitor.rcWork.top-h)/3,w,h,SWP_NOACTIVATE);
    MoveWindow(edit_,px(24),px(21),w-px(48),px(32),TRUE);
}
void App::toggle() {
    if(IsWindowVisible(hwnd_)) { ShowWindow(hwnd_,SW_HIDE); return; }
    last_active_=GetForegroundWindow(); paste_target_=nullptr; KillTimer(hwnd_,paste_timer);
    position(); ShowWindow(hwnd_,SW_SHOW); SetForegroundWindow(hwnd_); SetFocus(edit_);
    SendMessageW(edit_,EM_SETSEL,0,-1); query();
}
void App::tray(bool add) {
    NOTIFYICONDATAW data{sizeof(data)}; data.hWnd=hwnd_; data.uID=1;
    data.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP; data.uCallbackMessage=tray_message;
    data.hIcon=LoadIconW(nullptr,IDI_APPLICATION); wcscpy_s(data.szTip,L"ReCall — quick launcher");
    Shell_NotifyIconW(add?NIM_ADD:NIM_DELETE,&data);
    if(add) { data.uVersion=NOTIFYICON_VERSION_4; Shell_NotifyIconW(NIM_SETVERSION,&data); }
}
void App::menu() {
    HMENU m=CreatePopupMenu(); AppendMenuW(m,MF_STRING,1,L"Open ReCall");
    AppendMenuW(m,MF_STRING,2,L"Edit configuration (restart to apply)");
    AppendMenuW(m,MF_STRING,3,L"Refresh application index"); AppendMenuW(m,MF_SEPARATOR,0,nullptr); AppendMenuW(m,MF_STRING,4,L"Exit");
    POINT p{}; GetCursorPos(&p); SetForegroundWindow(hwnd_);
    int command=TrackPopupMenu(m,TPM_RETURNCMD|TPM_RIGHTBUTTON,p.x,p.y,0,hwnd_,nullptr); DestroyMenu(m);
    PostMessageW(hwnd_,WM_NULL,0,0);
    if(command==1) toggle();
    else if(command==2) ShellExecuteW(hwnd_,L"open",(config_.state/L"config.json").c_str(),nullptr,nullptr,SW_SHOWNORMAL);
    else if(command==3) refresh(); else if(command==4) SendMessageW(hwnd_,WM_CLOSE,0,0);
}
void App::select(int delta) {
    if(shown_.items.empty()) return;
    selected_=std::clamp(selected_+delta,0,static_cast<int>(shown_.items.size())-1);
    RECT r{}; GetClientRect(hwnd_,&r); int rows=std::max(1,static_cast<int>((r.bottom-px(110))/px(58)));
    if(selected_<first_) first_=selected_; if(selected_>=first_+rows) first_=selected_-rows+1;
    InvalidateRect(hwnd_,nullptr,FALSE);
}
void App::execute() {
    if(shown_.generation!=generation_||shown_.items.empty()||selected_>=static_cast<int>(shown_.items.size())) return;
    const auto& item=shown_.items[selected_];
    if(item.kind==Kind::App) {
        SHELLEXECUTEINFOW info{sizeof(info)}; info.fMask=SEE_MASK_FLAG_NO_UI; info.hwnd=hwnd_;
        info.lpVerb=L"open"; info.lpFile=item.value.c_str(); info.nShow=SW_SHOWNORMAL;
        info.lpParameters=item.arguments.empty()?nullptr:item.arguments.c_str();
        info.lpDirectory=item.working_directory.empty()?nullptr:item.working_directory.c_str();
        if(!ShellExecuteExW(&info)) { report(L"Application launch failed ("+std::to_wstring(GetLastError())+L")."); return; }
        ShowWindow(hwnd_,SW_HIDE); return;
    }
    try { own_sequence_=populate_clipboard(hwnd_,item); }
    catch(const std::exception& e) { report(wide(e.what())); return; }
    ShowWindow(hwnd_,SW_HIDE);
    if(IsWindow(last_active_)) {
        if(SetForegroundWindow(last_active_)&&config_.auto_paste) {
            paste_target_=last_active_; SetTimer(hwnd_,paste_timer,20,nullptr);
        }
    }
}
void App::paste() {
    KillTimer(hwnd_,paste_timer);
    if(!paste_target_||GetForegroundWindow()!=paste_target_) { paste_target_=nullptr; return; }
    // Wait for the user's hotkey/Enter modifiers to be released before synthesizing Ctrl+V.
    if((GetAsyncKeyState(VK_CONTROL)|GetAsyncKeyState(VK_MENU)|GetAsyncKeyState(VK_SHIFT)|GetAsyncKeyState(VK_LWIN)|GetAsyncKeyState(VK_RWIN)|GetAsyncKeyState(VK_RETURN))&0x8000) {
        SetTimer(hwnd_,paste_timer,20,nullptr); return;
    }
    INPUT inputs[4]{}; for(auto& i:inputs) i.type=INPUT_KEYBOARD;
    inputs[0].ki.wVk=VK_CONTROL; inputs[1].ki.wVk='V'; inputs[2].ki.wVk='V'; inputs[2].ki.dwFlags=KEYEVENTF_KEYUP;
    inputs[3].ki.wVk=VK_CONTROL; inputs[3].ki.dwFlags=KEYEVENTF_KEYUP;
    if(SendInput(4,inputs,sizeof(INPUT))!=4) report(L"Auto-paste was blocked. The selected item is on the clipboard; paste it manually.");
    paste_target_=nullptr;
}
void App::paint() {
    PAINTSTRUCT ps{}; HDC dc=BeginPaint(hwnd_,&ps); RECT bounds{}; GetClientRect(hwnd_,&bounds);
    HDC buffer=CreateCompatibleDC(dc); HBITMAP surface=CreateCompatibleBitmap(dc,std::max(1L,bounds.right),std::max(1L,bounds.bottom));
    auto old=SelectObject(buffer,surface); fill(buffer,bounds,background);
    int split=bounds.right*3/5; fill(buffer,{px(16),px(70),bounds.right-px(16),px(71)},RGB(60,63,73));
    fill(buffer,{split,px(84),split+1,bounds.bottom-px(48)},RGB(60,63,73));
    int rows=std::max(1,static_cast<int>((bounds.bottom-px(110))/px(58)));
    {
    Gdiplus::Graphics graphics(buffer);
    auto image=[&](const Item& item,RECT target) {
        auto it=shown_.images.find(item.value); if(it==shown_.images.end()) return;
        auto b=it->second; double scale=std::min(static_cast<double>(target.right-target.left)/b->GetWidth(),static_cast<double>(target.bottom-target.top)/b->GetHeight());
        int w=static_cast<int>(b->GetWidth()*scale),h=static_cast<int>(b->GetHeight()*scale);
        graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        graphics.DrawImage(b.get(),target.left+(target.right-target.left-w)/2,target.top+(target.bottom-target.top-h)/2,w,h);
    };
    for(int row=0;row<rows;++row) {
        int idx=first_+row; if(idx>=static_cast<int>(shown_.items.size())) break;
        const auto& item=shown_.items[idx]; int y=px(82)+row*px(58);
        if(idx==selected_) fill(buffer,{px(12),y,split-px(12),y+px(55)},accent);
        int left=px(28);
        if(item.kind==Kind::Image) { image(item,{left,y+px(5),left+px(42),y+px(47)}); left+=px(54); }
        if(item.kind==Kind::Emoji) { text(buffer,item.value,{left,y,left+px(48),y+px(50)},foreground,emoji_font_); left+=px(54); }
        text(buffer,single_line(item.title).substr(0,512),{left,y+px(5),split-px(25),y+px(30)},foreground,font_);
        text(buffer,single_line(item.detail).substr(0,512),{left,y+px(30),split-px(25),y+px(50)},muted,small_font_);
    }
    if(shown_.items.empty()) text(buffer,L"No results",{px(28),px(110),split,px(150)},muted,font_);
    if(!shown_.items.empty()&&selected_<static_cast<int>(shown_.items.size())) {
        const auto& item=shown_.items[selected_]; RECT preview{split+px(24),px(100),bounds.right-px(24),bounds.bottom-px(85)};
        if(item.kind==Kind::Image) image(item,preview);
        else text(buffer,(item.kind==Kind::Emoji?item.value+L"\n\n"+item.detail:item.kind==Kind::Text?item.value:item.title+L"\n\n"+item.detail).substr(0,12000),preview,foreground,
            item.kind==Kind::Emoji?emoji_font_:font_,DT_LEFT|DT_WORDBREAK|DT_EDITCONTROL|DT_WORD_ELLIPSIS);
    }
    }
    text(buffer,shown_.status,{px(24),bounds.bottom-px(40),split,bounds.bottom-px(15)},muted,small_font_);
    text(buffer,L"↑ ↓ navigate   Enter select   Esc close",{split+px(16),bounds.bottom-px(40),bounds.right-px(16),bounds.bottom-px(15)},muted,small_font_);
    BitBlt(dc,0,0,bounds.right,bounds.bottom,buffer,0,0,SRCCOPY);
    SelectObject(buffer,old); DeleteObject(surface); DeleteDC(buffer); EndPaint(hwnd_,&ps);
}
LRESULT CALLBACK App::edit_proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR,DWORD_PTR data) {
    auto* self=reinterpret_cast<App*>(data);
    if(msg==WM_KEYDOWN) {
        switch(wp) {
        case VK_BACK:
            if(GetKeyState(VK_CONTROL)&0x8000) {
                DWORD begin{},end{};
                SendMessageW(hwnd,EM_GETSEL,reinterpret_cast<WPARAM>(&begin),reinterpret_cast<LPARAM>(&end));
                int length=GetWindowTextLengthW(hwnd);
                std::wstring input(static_cast<size_t>(length)+1,L'\0');
                GetWindowTextW(hwnd,input.data(),length+1); input.resize(length);
                auto range=word_erase_range(input,begin,end);
                if(range.first!=range.second) {
                    SendMessageW(hwnd,EM_SETSEL,static_cast<WPARAM>(range.first),static_cast<LPARAM>(range.second));
                    SendMessageW(hwnd,EM_REPLACESEL,TRUE,reinterpret_cast<LPARAM>(L""));
                }
                return 0;
            }
            break;
        case VK_ESCAPE: ShowWindow(self->hwnd_,SW_HIDE); return 0;
        case VK_UP: self->select(-1); return 0; case VK_DOWN: self->select(1); return 0;
        case VK_PRIOR: self->select(-7); return 0; case VK_NEXT: self->select(7); return 0;
        case VK_RETURN: self->execute(); return 0;
        }
    }
    // TranslateMessage produces DEL for Ctrl+Backspace on a standard EDIT control.
    if(msg==WM_CHAR&&(wp==VK_RETURN||wp==VK_ESCAPE||wp==0x7F||(wp==VK_BACK&&(GetKeyState(VK_CONTROL)&0x8000)))) return 0;
    return DefSubclassProc(hwnd,msg,wp,lp);
}
LRESULT CALLBACK App::keyboard_proc(int code,WPARAM wp,LPARAM lp) {
    auto* self=instance_;
    if(code==HC_ACTION&&self) {
        const auto* key=reinterpret_cast<KBDLLHOOKSTRUCT*>(lp);
        if(!(key->flags&LLKHF_INJECTED)&&key->vkCode==self->config_.key) {
            bool down=wp==WM_KEYDOWN||wp==WM_SYSKEYDOWN;
            if(!down&&self->hook_down_) { self->hook_down_=false; return 1; }
            UINT mods=0;
            if((GetAsyncKeyState(VK_LWIN)|GetAsyncKeyState(VK_RWIN))&0x8000) mods|=MOD_WIN;
            if(GetAsyncKeyState(VK_CONTROL)&0x8000) mods|=MOD_CONTROL;
            if(GetAsyncKeyState(VK_MENU)&0x8000) mods|=MOD_ALT;
            if(GetAsyncKeyState(VK_SHIFT)&0x8000) mods|=MOD_SHIFT;
            if(down&&mods==self->config_.modifiers) {
                if(!self->hook_down_) {
                    self->hook_down_=true;
                    // Mark Win as used without emitting a bindable function-key shortcut.
                    // 0xE8 is unassigned; F24 can trigger another app's global hotkey.
                    constexpr WORD menu_mask_key=0xE8;
                    if(mods&MOD_WIN) { INPUT mask[2]{}; for(auto& i:mask) { i.type=INPUT_KEYBOARD; i.ki.wVk=menu_mask_key; }
                        mask[1].ki.dwFlags=KEYEVENTF_KEYUP; SendInput(2,mask,sizeof(INPUT)); }
                    PostMessageW(self->hwnd_,WM_HOTKEY,hotkey_id,0);
                }
                return 1;
            }
        }
    }
    return CallNextHookEx(nullptr,code,wp,lp);
}
LRESULT CALLBACK App::window_proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
    App* self=reinterpret_cast<App*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if(msg==WM_NCCREATE) { self=static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams); self->hwnd_=hwnd; SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self)); }
    return self?self->message(msg,wp,lp):DefWindowProcW(hwnd,msg,wp,lp);
}
LRESULT App::message(UINT msg,WPARAM wp,LPARAM lp) {
    if(msg==taskbar_created_&&taskbar_created_) { tray(true); return 0; }
    switch(msg) {
    case WM_HOTKEY: case WM_APP+9: toggle(); return 0;
    case WM_COMMAND: if(reinterpret_cast<HWND>(lp)==edit_&&HIWORD(wp)==EN_CHANGE) query(); return 0;
    case WM_CLIPBOARDUPDATE: capture(); return 0;
    case changed_message: if(IsWindowVisible(hwnd_)) query(); return 0;
    case results_message: {
        std::lock_guard lock(result_mutex_);
        if(pending_.generation==generation_) { shown_=std::move(pending_); pending_.generation=0; selected_=first_=0; InvalidateRect(hwnd_,nullptr,FALSE); }
        return 0;
    }
    case error_message: {
        std::wstring error; { std::lock_guard lock(result_mutex_); error=error_; }
        shown_.status=L"Error: "+error; InvalidateRect(hwnd_,nullptr,FALSE);
        NOTIFYICONDATAW data{sizeof(data)}; data.hWnd=hwnd_; data.uID=1; data.uFlags=NIF_INFO;
        wcscpy_s(data.szInfoTitle,L"ReCall"); wcsncpy_s(data.szInfo,error.c_str(),_TRUNCATE); data.dwInfoFlags=NIIF_ERROR; Shell_NotifyIconW(NIM_MODIFY,&data); return 0;
    }
    case tray_message:
        if(LOWORD(lp)==WM_CONTEXTMENU||LOWORD(lp)==WM_RBUTTONUP) menu();
        else if(LOWORD(lp)==NIN_SELECT||LOWORD(lp)==NIN_KEYSELECT) toggle(); return 0;
    case WM_ACTIVATE: if(LOWORD(wp)==WA_INACTIVE) ShowWindow(hwnd_,SW_HIDE); return 0;
    case WM_TIMER: if(wp==paste_timer) paste(); return 0;
    case WM_DPICHANGED: dpi_=HIWORD(wp); fonts(); position(); return 0;
    case WM_CTLCOLOREDIT: SetTextColor(reinterpret_cast<HDC>(wp),foreground); SetBkColor(reinterpret_cast<HDC>(wp),background); SetDCBrushColor(reinterpret_cast<HDC>(wp),background); return reinterpret_cast<LRESULT>(GetStockObject(DC_BRUSH));
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: paint(); return 0;
    case WM_MOUSEWHEEL: select(GET_WHEEL_DELTA_WPARAM(wp)>0?-1:1); return 0;
    case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: {
        int x=static_cast<short>(LOWORD(lp)),y=static_cast<short>(HIWORD(lp)); RECT r{}; GetClientRect(hwnd_,&r);
        if(x<r.right*3/5&&y>=px(82)&&y<r.bottom-px(40)) {
            int index=first_+(y-px(82))/px(58);
            if(index<static_cast<int>(shown_.items.size())) { selected_=index; InvalidateRect(hwnd_,nullptr,FALSE); if(msg==WM_LBUTTONDBLCLK) execute(); }
        } SetFocus(edit_); return 0;
    }
    case WM_CLOSE: quitting_=true; ShowWindow(hwnd_,SW_HIDE); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd_,msg,wp,lp);
}
int App::run(HINSTANCE instance) {
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES}; InitCommonControlsEx(&controls);
    WNDCLASSEXW cls{sizeof(cls)}; cls.hInstance=instance; cls.lpfnWndProc=window_proc;
    cls.lpszClassName=L"ReCall.Palette"; cls.hCursor=LoadCursorW(nullptr,IDC_ARROW); cls.style=CS_DBLCLKS;
    if(!RegisterClassExW(&cls)) throw std::runtime_error("Cannot register palette class");
    hwnd_=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST,cls.lpszClassName,L"ReCall",WS_POPUP|WS_CLIPCHILDREN,
        0,0,900,540,nullptr,nullptr,instance,this);
    if(!hwnd_) throw std::runtime_error("Cannot create palette");
    dpi_=static_cast<int>(GetDpiForWindow(hwnd_));
    const int round=2,mica=2; BOOL dark=TRUE;
    DwmSetWindowAttribute(hwnd_,DWMWA_WINDOW_CORNER_PREFERENCE,&round,sizeof(round));
    DwmSetWindowAttribute(hwnd_,DWMWA_SYSTEMBACKDROP_TYPE,&mica,sizeof(mica));
    DwmSetWindowAttribute(hwnd_,DWMWA_USE_IMMERSIVE_DARK_MODE,&dark,sizeof(dark));
    edit_=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL,24,21,852,32,hwnd_,nullptr,instance,nullptr);
    if(!edit_) throw std::runtime_error("Cannot create query control");
    SendMessageW(edit_,EM_SETLIMITTEXT,256,0);
    std::wstring cue=L"Search apps  ·  "+config_.clipboard_prefix+L" clipboard  ·  "+config_.emoji_prefix+L" emoji";
    SendMessageW(edit_,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(cue.c_str()));
    SetWindowSubclass(edit_,edit_proc,1,reinterpret_cast<DWORD_PTR>(this)); fonts(); position();
    taskbar_created_=RegisterWindowMessageW(L"TaskbarCreated"); tray(true);
    registered_=RegisterHotKey(hwnd_,hotkey_id,config_.modifiers|MOD_NOREPEAT,config_.key)!=FALSE;
    if(!registered_&&config_.hook_fallback) hook_=SetWindowsHookExW(WH_KEYBOARD_LL,keyboard_proc,instance,0);
    if(!registered_&&!hook_) report(L"The configured hotkey is unavailable. Open ReCall from the tray and choose another hotkey in config.json.");
    listener_=AddClipboardFormatListener(hwnd_)!=FALSE;
    if(!listener_) report(L"Cannot subscribe to clipboard updates.");
    worker_=std::jthread([this]{run_worker();}); refresh(); ShowWindow(hwnd_,SW_HIDE);
    MSG msg{}; BOOL result;
    while((result=GetMessageW(&msg,nullptr,0,0))>0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    return result==-1?1:static_cast<int>(msg.wParam);
}
}
