#include "app.h"
#include "catalog.h"
#include "clipboard.h"
#include "search.h"
#include "editing.h"
#include "version.h"
#include "appearance.h"
#include <commctrl.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <algorithm>
#include <chrono>

namespace recall {
namespace {
constexpr UINT results_message=WM_APP+1, tray_message=WM_APP+2, changed_message=WM_APP+3, error_message=WM_APP+4,
    preview_message=WM_APP+5,history_message=WM_APP+6,clipboard_ready_message=WM_APP+7;
constexpr UINT hotkey_id=1, paste_timer=2,clipboard_timer=3;
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
    ++generation_;
    index_worker_.request_stop(); index_cv_.notify_all();
    queue_cv_.notify_all(); if(worker_.joinable()) worker_.join();
    if(index_worker_.joinable()) index_worker_.join();
    previews_.reset(); icons_.reset();
    if(hwnd_) { tray(false); if(IsWindow(hwnd_)) DestroyWindow(hwnd_); }
    if(font_) DeleteObject(font_); if(small_font_) DeleteObject(small_font_); if(emoji_font_) DeleteObject(emoji_font_);
    if(paint_dc_) { SelectObject(paint_dc_,paint_original_); DeleteObject(paint_surface_); DeleteDC(paint_dc_); }
    instance_=nullptr;
}
void App::enqueue(std::function<void()> job,bool priority) {
    { std::lock_guard lock(queue_mutex_); if(stopping_) return;
        if(priority) jobs_.push_front(std::move(job)); else jobs_.push_back(std::move(job)); }
    queue_cv_.notify_one();
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
    { std::lock_guard lock(queue_mutex_); if(stopping_) return; index_requested_=true; }
    index_cv_.notify_one();
}
void App::run_indexer(std::stop_token stop) {
    ComInit com;
    for(;;) {
        { std::unique_lock lock(queue_mutex_); index_cv_.wait(lock,[&]{return stopping_||index_requested_;});
            if(stopping_) return; index_requested_=false; }
        try {
            auto apps=std::make_shared<const std::vector<Item>>(index_apps([&]{return stop.stop_requested();}));
            if(stop.stop_requested()) return;
            std::shared_ptr<const std::vector<Item>> emoji;
            { std::lock_guard lock(result_mutex_); emoji=emoji_; }
            if(emoji->empty()) emoji=std::make_shared<const std::vector<Item>>(load_emoji());
            { std::lock_guard lock(result_mutex_); apps_=std::move(apps); emoji_=std::move(emoji); }
            PostMessageW(hwnd_,changed_message,0,0);
        } catch(const std::exception& e) { report(wide(e.what())); }
    }
}
void App::refresh_history() {
    auto history=std::make_shared<const std::vector<Item>>(store_->recent(100));
    { std::lock_guard lock(result_mutex_); pending_history_=std::move(history); }
    PostMessageW(hwnd_,history_message,0,0);
}
void App::query() {
    int length=GetWindowTextLengthW(edit_); std::wstring input(static_cast<size_t>(length)+1,L'\0');
    GetWindowTextW(edit_,input.data(),length+1); input.resize(length);
    auto q=route_query(input,config_.clipboard_prefix,config_.emoji_prefix);
    auto generation=++generation_;
    preparing_generation_=0; ready_clipboard_.reset(); KillTimer(hwnd_,clipboard_timer);
    if(previews_) previews_->submit(generation,{});
    if(icons_) icons_->submit(generation,{});
    if(q.route==Route::Clipboard&&q.text.empty()&&clipboard_history_) {
        Snapshot result; result.generation=generation; result.items=clipboard_history_;
        result.status=L"Clipboard history  ·  "+std::to_wstring(result.rows().size())+L" results";
        show_results(std::move(result)); return;
    }
    enqueue([this,q=std::move(q),generation] {
        if(generation!=generation_) return;
        Snapshot result; result.generation=generation;
        // Clipboard stays in recency order; fuzzy matching only filters it.
        if(q.route==Route::Clipboard) result.items=std::make_shared<const std::vector<Item>>(store_->search(q.text,100,[this,generation]{return generation!=generation_;}));
        else {
            std::shared_ptr<const std::vector<Item>> catalog;
            { std::lock_guard lock(result_mutex_); catalog=q.route==Route::Apps?apps_:emoji_; }
            result.items=std::make_shared<const std::vector<Item>>(ranked(*catalog,q.text,100,[this,generation]{return generation!=generation_;},q.route==Route::Apps?store_->app_usage():AppUsageMap{}));
        }
        result.status=(q.route==Route::Apps?L"Applications":q.route==Route::Clipboard?L"Clipboard history":L"Unicode 16 emoji");
        result.status+=L"  ·  "+std::to_wstring(result.rows().size())+L" results";
        if(generation!=generation_) return;
        { std::lock_guard lock(result_mutex_); pending_=std::move(result); }
        PostMessageW(hwnd_,results_message,0,0);
    },true);
}
void App::show_results(Snapshot result) {
    if(result.generation!=generation_) return;
    // Preserve any visible images that are shared by the old and new results.
    result.images=std::move(shown_.images);
    shown_=std::move(result); selected_=first_=0;
    request_previews(); InvalidateRect(hwnd_,nullptr,FALSE);
}
void App::request_previews() {
    if(!previews_) return;
    requested_previews_.clear(); std::vector<std::wstring> paths,icon_paths;
    auto add=[&](int index) {
        if(index<0||index>=static_cast<int>(shown_.rows().size())) return;
        const auto& item=shown_.rows()[index];
        if((item.kind==Kind::Image||item.kind==Kind::App)&&requested_previews_.insert(item.value).second) {
            (item.kind==Kind::App?icon_paths:paths).push_back(item.value);
        }
    };
    add(selected_); // Prioritize the selected side-pane preview.
    RECT bounds{}; GetClientRect(hwnd_,&bounds);
    int rows=std::max(1,static_cast<int>((bounds.bottom-px(110))/px(58)));
    for(int row=0;row<rows;++row) add(first_+row);
    std::erase_if(shown_.images,[&](const auto& entry){return !requested_previews_.contains(entry.first);});
    previews_->submit(shown_.generation,std::move(paths));
    if(icons_) icons_->submit(shown_.generation,std::move(icon_paths));
}
void App::capture() {
    DWORD seq=GetClipboardSequenceNumber(); clipboard_sequence_=seq;
    if(seq==own_sequence_) return;
    enqueue([this,seq] {
        if(seq!=clipboard_sequence_||seq==own_sequence_) return;
        auto item=capture_clipboard(hwnd_,config_.state/L"clips_media",seq);
        if(item) { store_->insert(*item); refresh_history(); }
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
void App::apply_theme() {
    colors_=system_theme();
    BOOL dark=colors_.dark;
    DwmSetWindowAttribute(hwnd_,DWMWA_USE_IMMERSIVE_DARK_MODE,&dark,sizeof(dark));
    const int backdrop=DWMSBT_NONE;
    DwmSetWindowAttribute(hwnd_,DWMWA_SYSTEMBACKDROP_TYPE,&backdrop,sizeof(backdrop));
    try { apply_appearance(hwnd_,config_.opacity,config_.blur,colors_.high_contrast); }
    catch(const std::exception& e) { report(wide(e.what())); }
    RedrawWindow(hwnd_,nullptr,nullptr,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN);
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
    data.hIcon=reinterpret_cast<HICON>(GetClassLongPtrW(hwnd_,GCLP_HICONSM));
    const std::wstring tip=L"ReCall "+std::wstring(RECALL_VERSION_W)+L" — quick launcher";
    wcsncpy_s(data.szTip,tip.c_str(),_TRUNCATE);
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
    if(shown_.rows().empty()) return;
    selected_=std::clamp(selected_+delta,0,static_cast<int>(shown_.rows().size())-1);
    RECT r{}; GetClientRect(hwnd_,&r); int rows=std::max(1,static_cast<int>((r.bottom-px(110))/px(58)));
    if(selected_<first_) first_=selected_; if(selected_>=first_+rows) first_=selected_-rows+1;
    request_previews();
    InvalidateRect(hwnd_,nullptr,FALSE);
}
void App::execute() {
    if(shown_.generation!=generation_||shown_.rows().empty()||selected_>=static_cast<int>(shown_.rows().size())) return;
    const auto& item=shown_.rows()[selected_];
    if(item.kind==Kind::App) {
        SHELLEXECUTEINFOW info{sizeof(info)}; info.fMask=SEE_MASK_FLAG_NO_UI; info.hwnd=hwnd_;
        info.lpVerb=L"open"; info.lpFile=item.value.c_str(); info.nShow=SW_SHOWNORMAL;
        info.lpParameters=item.arguments.empty()?nullptr:item.arguments.c_str();
        info.lpDirectory=item.working_directory.empty()?nullptr:item.working_directory.c_str();
        if(!ShellExecuteExW(&info)) { report(L"Application launch failed ("+std::to_wstring(GetLastError())+L")."); return; }
        enqueue([this,key=item.app_key]{store_->record_app_launch(key);},true);
        ShowWindow(hwnd_,SW_HIDE); return;
    }
    if(preparing_generation_==generation_) return;
    const auto generation=generation_.load(); preparing_generation_=generation;
    const auto target=last_active_;
    shown_.status=L"Preparing clipboard…"; InvalidateRect(hwnd_,nullptr,FALSE);
    enqueue([this,item,generation,target] {
        if(generation!=generation_) return;
        ClipboardReady ready; ready.generation=generation; ready.target=target;
        try {
            auto full=item.id?store_->load(item.id):std::optional<Item>(item);
            if(!full) throw std::runtime_error("This clipboard entry has expired");
            ready.payload=std::make_shared<PreparedClipboard>(prepare_clipboard(*full));
        } catch(const std::exception& e) { ready.error=wide(e.what()); }
        if(generation!=generation_) return;
        { std::lock_guard lock(result_mutex_); pending_clipboard_=std::move(ready); }
        PostMessageW(hwnd_,clipboard_ready_message,0,0);
    },true);
}
void App::finish_clipboard() {
    KillTimer(hwnd_,clipboard_timer);
    if(!ready_clipboard_) return;
    if(ready_clipboard_->generation!=generation_||!IsWindowVisible(hwnd_)||GetForegroundWindow()!=hwnd_) {
        ready_clipboard_.reset(); preparing_generation_=0; return;
    }
    if(!ready_clipboard_->error.empty()) {
        report(ready_clipboard_->error); ready_clipboard_.reset(); preparing_generation_=0; return;
    }
    try {
        auto sequence=publish_clipboard(hwnd_,*ready_clipboard_->payload);
        if(!sequence) {
            if(GetTickCount64()<clipboard_deadline_) { SetTimer(hwnd_,clipboard_timer,10,nullptr); return; }
            throw std::runtime_error("Clipboard is busy");
        }
        own_sequence_=*sequence;
    } catch(const std::exception& e) {
        report(wide(e.what())); ready_clipboard_.reset(); preparing_generation_=0; return;
    }
    const auto target=ready_clipboard_->target;
    ready_clipboard_.reset(); preparing_generation_=0;
    ShowWindow(hwnd_,SW_HIDE);
    if(IsWindow(target)) {
        if(SetForegroundWindow(target)&&config_.auto_paste) {
            paste_target_=target; SetTimer(hwnd_,paste_timer,20,nullptr);
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
    const auto background=colors_.background,foreground=colors_.foreground,muted=colors_.muted;
    PAINTSTRUCT ps{}; HDC dc=BeginPaint(hwnd_,&ps); RECT bounds{}; GetClientRect(hwnd_,&bounds);
    if(!paint_dc_) paint_dc_=CreateCompatibleDC(dc);
    if(paint_dc_&&(!paint_surface_||paint_size_.cx!=bounds.right||paint_size_.cy!=bounds.bottom)) {
        auto surface=CreateCompatibleBitmap(dc,std::max(1L,bounds.right),std::max(1L,bounds.bottom));
        if(surface) {
            auto previous=SelectObject(paint_dc_,surface);
            if(!paint_surface_) paint_original_=previous; else DeleteObject(paint_surface_);
            paint_surface_=surface; paint_size_={bounds.right,bounds.bottom};
        }
    }
    if(!paint_dc_||!paint_surface_||paint_size_.cx!=bounds.right||paint_size_.cy!=bounds.bottom) {
        fill(dc,bounds,background); EndPaint(hwnd_,&ps); return;
    }
    HDC buffer=paint_dc_; fill(buffer,bounds,background);
    int split=bounds.right*3/5; fill(buffer,{px(16),px(70),bounds.right-px(16),px(71)},colors_.border);
    fill(buffer,{split,px(84),split+1,bounds.bottom-px(48)},colors_.border);
    int rows=std::max(1,static_cast<int>((bounds.bottom-px(110))/px(58)));
    {
    Gdiplus::Graphics graphics(buffer);
    auto image=[&](const Item& item,RECT target) {
        auto it=shown_.images.find(item.value); if(it==shown_.images.end()||!it->second) return;
        auto b=it->second; double scale=std::min(static_cast<double>(target.right-target.left)/b->GetWidth(),static_cast<double>(target.bottom-target.top)/b->GetHeight());
        int w=static_cast<int>(b->GetWidth()*scale),h=static_cast<int>(b->GetHeight()*scale);
        graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        graphics.DrawImage(b.get(),target.left+(target.right-target.left-w)/2,target.top+(target.bottom-target.top-h)/2,w,h);
    };
    for(int row=0;row<rows;++row) {
        int idx=first_+row; if(idx>=static_cast<int>(shown_.rows().size())) break;
        const auto& item=shown_.rows()[idx]; int y=px(82)+row*px(58);
        const bool selected=idx==selected_;
        const auto row_text=selected?colors_.selection_text:foreground;
        const auto row_detail=selected?colors_.selection_text:muted;
        if(selected) fill(buffer,{px(12),y,split-px(12),y+px(55)},colors_.selection);
        int left=px(28);
        if(item.kind==Kind::App) { image(item,{left,y+px(11),left+px(32),y+px(43)}); left+=px(44); }
        if(item.kind==Kind::Image) { image(item,{left,y+px(5),left+px(42),y+px(47)}); left+=px(54); }
        if(item.kind==Kind::Emoji) {
            RECT glyph{left,y,left+px(48),y+px(50)};
            if(!emoji_renderer_.draw(buffer,item.value,glyph,static_cast<float>(px(28)),row_text))
                text(buffer,item.value,glyph,row_text,emoji_font_);
            left+=px(54);
        }
        text(buffer,single_line(item.title.substr(0,512)),{left,y+px(5),split-px(25),y+px(30)},row_text,font_);
        text(buffer,single_line(item.detail).substr(0,512),{left,y+px(30),split-px(25),y+px(50)},row_detail,small_font_);
    }
    if(shown_.rows().empty()) text(buffer,L"No results",{px(28),px(110),split,px(150)},muted,font_);
    if(!shown_.rows().empty()&&selected_<static_cast<int>(shown_.rows().size())) {
        const auto& item=shown_.rows()[selected_]; RECT preview{split+px(24),px(100),bounds.right-px(24),bounds.bottom-px(85)};
        if(item.kind==Kind::Image) image(item,preview);
        else if(item.kind==Kind::Emoji) {
            RECT glyph{preview.left,preview.top,preview.right,preview.top+px(72)};
            if(!emoji_renderer_.draw(buffer,item.value,glyph,static_cast<float>(px(48)),foreground))
                text(buffer,item.value,glyph,foreground,emoji_font_);
            preview.top+=px(88);
            text(buffer,item.detail,preview,foreground,font_,DT_LEFT|DT_WORDBREAK|DT_EDITCONTROL|DT_WORD_ELLIPSIS);
        }
        else text(buffer,(item.kind==Kind::Text?item.value.substr(0,12000):item.title+L"\n\n"+item.detail).substr(0,12000),preview,foreground,
            font_,DT_LEFT|DT_WORDBREAK|DT_EDITCONTROL|DT_WORD_ELLIPSIS);
    }
    }
    text(buffer,shown_.status,{px(24),bounds.bottom-px(40),split,bounds.bottom-px(15)},muted,small_font_);
    text(buffer,L"↑ ↓ navigate   Enter select   Esc close",{split+px(16),bounds.bottom-px(40),bounds.right-px(16),bounds.bottom-px(15)},muted,small_font_);
    BitBlt(dc,0,0,bounds.right,bounds.bottom,buffer,0,0,SRCCOPY);
    EndPaint(hwnd_,&ps);
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
        Snapshot result;
        { std::lock_guard lock(result_mutex_); result=std::move(pending_); pending_.generation=0; }
        show_results(std::move(result));
        return 0;
    }
    case history_message: {
        { std::lock_guard lock(result_mutex_); if(pending_history_) clipboard_history_=std::move(pending_history_); }
        if(IsWindowVisible(hwnd_)) {
            int length=GetWindowTextLengthW(edit_); std::wstring input(static_cast<size_t>(length)+1,L'\0');
            GetWindowTextW(edit_,input.data(),length+1); input.resize(length);
            if(route_query(input,config_.clipboard_prefix,config_.emoji_prefix).route==Route::Clipboard) query();
        }
        return 0;
    }
    case preview_message: {
        std::vector<PreviewUpdate> updates;
        { std::lock_guard lock(result_mutex_); updates.swap(pending_previews_); }
        bool changed=false;
        for(auto& update:updates) if(update.generation==generation_&&update.generation==shown_.generation&&requested_previews_.contains(update.path)) {
            shown_.images[update.path]=std::move(update.bitmap); changed=true;
        }
        if(changed) InvalidateRect(hwnd_,nullptr,FALSE);
        return 0;
    }
    case clipboard_ready_message: {
        std::optional<ClipboardReady> ready;
        { std::lock_guard lock(result_mutex_); ready=std::move(pending_clipboard_); pending_clipboard_.reset(); }
        if(ready&&ready->generation==generation_) {
            ready_clipboard_=std::move(ready); clipboard_deadline_=GetTickCount64()+250; finish_clipboard();
        }
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
    case WM_TIMER: if(wp==paste_timer) paste(); else if(wp==clipboard_timer) finish_clipboard(); return 0;
    case WM_DPICHANGED: dpi_=HIWORD(wp); fonts(); position(); request_previews(); return 0;
    case WM_SETTINGCHANGE: case WM_THEMECHANGED: case WM_SYSCOLORCHANGE: apply_theme(); return 0;
    case WM_CTLCOLOREDIT: SetTextColor(reinterpret_cast<HDC>(wp),colors_.foreground); SetBkColor(reinterpret_cast<HDC>(wp),colors_.background); SetDCBrushColor(reinterpret_cast<HDC>(wp),colors_.background); return reinterpret_cast<LRESULT>(GetStockObject(DC_BRUSH));
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: paint(); return 0;
    case WM_MOUSEWHEEL: select(GET_WHEEL_DELTA_WPARAM(wp)>0?-1:1); return 0;
    case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: {
        int x=static_cast<short>(LOWORD(lp)),y=static_cast<short>(HIWORD(lp)); RECT r{}; GetClientRect(hwnd_,&r);
        if(x<r.right*3/5&&y>=px(82)&&y<r.bottom-px(40)) {
            int index=first_+(y-px(82))/px(58);
            if(index<static_cast<int>(shown_.rows().size())) { selected_=index; request_previews(); InvalidateRect(hwnd_,nullptr,FALSE); if(msg==WM_LBUTTONDBLCLK) execute(); }
        } SetFocus(edit_); return 0;
    }
    case WM_CLOSE: quitting_=true; ShowWindow(hwnd_,SW_HIDE); PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd_,msg,wp,lp);
}
int App::run(HINSTANCE instance) {
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_STANDARD_CLASSES}; InitCommonControlsEx(&controls);
    WNDCLASSEXW cls{sizeof(cls)}; cls.hInstance=instance; cls.lpfnWndProc=window_proc;
    cls.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(102));
    cls.hIconSm=static_cast<HICON>(LoadImageW(instance,MAKEINTRESOURCEW(102),IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON),GetSystemMetrics(SM_CYSMICON),LR_SHARED));
    cls.lpszClassName=L"ReCall.Palette"; cls.hCursor=LoadCursorW(nullptr,IDC_ARROW); cls.style=CS_DBLCLKS;
    if(!RegisterClassExW(&cls)) throw std::runtime_error("Cannot register palette class");
    hwnd_=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST,cls.lpszClassName,L"ReCall",WS_POPUP|WS_CLIPCHILDREN,
        0,0,900,540,nullptr,nullptr,instance,this);
    if(!hwnd_) throw std::runtime_error("Cannot create palette");
    dpi_=static_cast<int>(GetDpiForWindow(hwnd_));
    const int round=DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd_,DWMWA_WINDOW_CORNER_PREFERENCE,&round,sizeof(round));
    edit_=CreateWindowExW(0,L"EDIT",L"",WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL,24,21,852,32,hwnd_,nullptr,instance,nullptr);
    if(!edit_) throw std::runtime_error("Cannot create query control");
    SendMessageW(edit_,EM_SETLIMITTEXT,256,0);
    std::wstring cue=L"Search apps  ·  "+config_.clipboard_prefix+L" clipboard  ·  "+config_.emoji_prefix+L" emoji";
    SendMessageW(edit_,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(cue.c_str()));
    SetWindowSubclass(edit_,edit_proc,1,reinterpret_cast<DWORD_PTR>(this)); fonts(); apply_theme(); position();
    taskbar_created_=RegisterWindowMessageW(L"TaskbarCreated"); tray(true);
    registered_=RegisterHotKey(hwnd_,hotkey_id,config_.modifiers|MOD_NOREPEAT,config_.key)!=FALSE;
    if(!registered_&&config_.hook_fallback) hook_=SetWindowsHookExW(WH_KEYBOARD_LL,keyboard_proc,instance,0);
    if(!registered_&&!hook_) report(L"The configured hotkey is unavailable. Open ReCall from the tray and choose another hotkey in config.json.");
    listener_=AddClipboardFormatListener(hwnd_)!=FALSE;
    if(!listener_) report(L"Cannot subscribe to clipboard updates.");
    previews_=std::make_unique<PreviewLoader>([this](uint64_t generation,const std::wstring& path,PreviewLoader::Bitmap bitmap) {
        { std::lock_guard lock(result_mutex_); pending_previews_.push_back({generation,path,std::move(bitmap)}); }
        PostMessageW(hwnd_,preview_message,0,0);
    });
    icons_=std::make_unique<PreviewLoader>([this](uint64_t generation,const std::wstring& path,PreviewLoader::Bitmap bitmap) {
        { std::lock_guard lock(result_mutex_); pending_previews_.push_back({generation,path,std::move(bitmap)}); }
        PostMessageW(hwnd_,preview_message,0,0);
    },PreviewLoader::Mode::AppIcons);
    worker_=std::jthread([this]{run_worker();});
    index_worker_=std::jthread([this](std::stop_token stop){run_indexer(stop);});
    enqueue([this]{refresh_history();}); refresh(); ShowWindow(hwnd_,SW_HIDE);
    MSG msg{}; BOOL result;
    while((result=GetMessageW(&msg,nullptr,0,0))>0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    return result==-1?1:static_cast<int>(msg.wParam);
}
}
