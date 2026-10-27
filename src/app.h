#pragma once
#include "config.h"
#include "store.h"
#include "theme.h"
#include "previews.h"
#include "clipboard.h"
#include "emoji_renderer.h"
#include <gdiplus.h>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <thread>

namespace recall {
class App {
    struct Snapshot {
        uint64_t generation{};
        std::shared_ptr<const std::vector<Item>> items=std::make_shared<const std::vector<Item>>();
        const std::vector<Item>& rows() const { return *items; }
        std::map<std::wstring,std::shared_ptr<Gdiplus::Bitmap>> images;
        std::wstring status;
    };
    Config config_;
    ThemeColors colors_;
    std::unique_ptr<Store> store_;
    HWND hwnd_{}, edit_{}, last_active_{}, paste_target_{};
    HFONT font_{}, small_font_{}, emoji_font_{};
    EmojiRenderer emoji_renderer_;
    HDC paint_dc_{};
    HBITMAP paint_surface_{};
    HGDIOBJ paint_original_{};
    SIZE paint_size_{};
    HHOOK hook_{};
    bool registered_{}, hook_down_{}, listener_{}, quitting_{};
    UINT taskbar_created_{};
    int dpi_=96, selected_=0, first_=0;
    std::shared_ptr<const std::vector<Item>> apps_=std::make_shared<const std::vector<Item>>(),emoji_=apps_;
    std::unique_ptr<PreviewLoader> previews_,icons_;
    std::set<std::wstring> requested_previews_;
    struct PreviewUpdate { uint64_t generation; std::wstring path; PreviewLoader::Bitmap bitmap; };
    std::vector<PreviewUpdate> pending_previews_;
    std::shared_ptr<const std::vector<Item>> clipboard_history_,pending_history_;
    Snapshot shown_;
    std::mutex queue_mutex_, result_mutex_;
    std::condition_variable queue_cv_;
    std::condition_variable index_cv_;
    std::deque<std::function<void()>> jobs_;
    bool stopping_{};
    bool index_requested_{};
    std::jthread worker_,index_worker_;
    struct ClipboardReady {
        uint64_t generation{};
        HWND target{};
        std::shared_ptr<PreparedClipboard> payload;
        std::wstring error;
    };
    std::optional<ClipboardReady> pending_clipboard_,ready_clipboard_;
    uint64_t preparing_generation_{};
    ULONGLONG clipboard_deadline_{};
    Snapshot pending_;
    std::atomic<uint64_t> generation_{0};
    std::atomic<DWORD> clipboard_sequence_{0}, own_sequence_{0};
    std::wstring error_;
    void enqueue(std::function<void()> job,bool priority=false);
    void run_worker();
    void run_indexer(std::stop_token stop);
    void query();
    void show_results(Snapshot result);
    void request_previews();
    void refresh_history();
    void capture();
    void paint();
    void select(int delta);
    void execute();
    void finish_clipboard();
    void paste();
    void position();
    void fonts();
    void apply_theme();
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
