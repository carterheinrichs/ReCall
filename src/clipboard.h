#pragma once
#include "common.h"
#include <optional>
namespace recall {
std::optional<Item> capture_clipboard(HWND owner,const fs::path& media,DWORD sequence);
DWORD populate_clipboard(HWND owner,const Item& item);
}
