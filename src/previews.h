#pragma once
#include "common.h"
#include <gdiplus.h>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace recall {
// A separate, coalescing worker: slow PNG decoding never holds up search results.
class PreviewLoader {
public:
    using Bitmap = std::shared_ptr<Gdiplus::Bitmap>;
    using Callback = std::function<void(uint64_t,const std::wstring&,Bitmap)>;
    enum class Mode { Images, AppIcons };
    explicit PreviewLoader(Callback callback,Mode mode=Mode::Images);
    ~PreviewLoader();
    void submit(uint64_t generation,std::vector<std::wstring> paths);
private:
    struct Request { uint64_t generation{}, revision{}; std::vector<std::wstring> paths; };
    Callback callback_;
    Mode mode_;
    std::mutex mutex_;
    std::condition_variable cv_;
    Request pending_;
    bool ready_{},stopping_{};
    std::atomic<uint64_t> revision_{0};
    std::jthread thread_;
    void run();
};
}
