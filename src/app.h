#pragma once
#include "config.h"
#include "store.h"
#include <gdiplus.h>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

namespace recall {
class App {
    struct Snapshot {
        uint64_t generation{};
        std::vector<Item> items;
        std::map<std::wstring,std::shared_ptr<Gdiplus::Bitmap>> images;
        std::wstring status;
    };
    Config config_;
    std::unique_ptr<Store> store_;
    HWND hwnd_{}, edit_{}, last_active_{}, paste_target_{};
    HFONT font_{}, small_font_{}, emoji_font_{};
    HHOOK hook_{};
    bool registered_{}, hook_down_{}, listener_{}, quitting_{};
    UINT taskbar_created_{};
    int dpi_=96, selected_=0, first_=0;
    std::vector<Item> apps_,emoji_;
    std::map<std::wstring,std::shared_ptr<Gdiplus::Bitmap>> thumbnail_cache_;
    Snapshot shown_;
    std::mutex queue_mutex_, result_mutex_;
    std::condition_variable queue_cv_;
    std::deque<std::function<void()>> jobs_;
    bool stopping_{};
    std::jthread worker_;
    Snapshot pending_;
    std::atomic<uint64_t> generation_{0};
    std::atomic<DWORD> clipboard_sequence_{0}, own_sequence_{0};
    std::wstring error_;
    void enqueue(std::function<void()> job);
    void run_worker();
    void query();
    void capture();
    void paint();
    void select(int delta);
    void execute();
    void paste();
    void position();
    void fonts();
    void tray(bool add);
    void menu();
    void report(std::wstring message);
    void refresh();
    int px(int value) const { return MulDiv(value,dpi_,96); }
    LRESULT message(UINT,WPARAM,LPARAM);
    static App* instance_;
    static LRESULT CALLBACK window_proc(HWND,UINT,WPARAM,LPARAM);
    static LRESULT CALLBACK edit_proc(HWND,UINT,WPARAM,LPARAM,UINT_PTR,DWORD_PTR);
    static LRESULT CALLBACK keyboard_proc(int,WPARAM,LPARAM);
public:
    explicit App(Config config);
    ~App();
    int run(HINSTANCE instance);
    void toggle();
};
}
