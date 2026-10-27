#pragma once
#include "common.h"
#include <optional>
namespace recall {
constexpr bool clipboard_image_size_allowed(uint64_t width,uint64_t height) {
    return width&&height&&width<=32*1024*1024&&height<=(32*1024*1024)/width;
}
struct PreparedClipboard {
    UINT format=CF_UNICODETEXT;
    HGLOBAL memory{};
    PreparedClipboard()=default;
    ~PreparedClipboard() { if(memory) GlobalFree(memory); }
    PreparedClipboard(const PreparedClipboard&)=delete;
    PreparedClipboard& operator=(const PreparedClipboard&)=delete;
    PreparedClipboard(PreparedClipboard&& other) noexcept:format(other.format),memory(std::exchange(other.memory,nullptr)) {}
};
PreparedClipboard prepare_clipboard(const Item& item);
// A single nonblocking clipboard-open attempt. Ownership transfers only on success.
std::optional<DWORD> publish_clipboard(HWND owner,PreparedClipboard& prepared);
std::optional<Item> capture_clipboard(HWND owner,const fs::path& media,DWORD sequence);
DWORD populate_clipboard(HWND owner,const Item& item);
}
