#pragma once
#include <windows.h>
#include <shlobj.h>
#include <filesystem>
#include <string>
#include <string_view>
#include <stdexcept>
#include <vector>
#include <utility>

namespace recall {
namespace fs = std::filesystem;
inline std::wstring wide(std::string_view s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (!n) throw std::runtime_error("Invalid UTF-8");
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}
inline std::string utf8(std::wstring_view s) {
    if (s.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    if (!n) throw std::runtime_error("Invalid UTF-16");
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()), out.data(), n, nullptr, nullptr);
    return out;
}
inline fs::path known_folder(REFKNOWNFOLDERID id) {
    PWSTR p{};
    if (FAILED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &p))) return {};
    fs::path result(p); CoTaskMemFree(p); return result;
}
struct ComInit {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ~ComInit() { if (SUCCEEDED(hr)) CoUninitialize(); }
};
template<class T> struct ComPtr {
    T* p{};
    ~ComPtr() { if (p) p->Release(); }
    T** put() { return &p; }
    T* operator->() const { return p; }
};
enum class Kind { App, Text, Image, Emoji };
struct Item {
    Kind kind = Kind::App;
    std::wstring title, detail, value, arguments, working_directory;
    long long id{};
};
}
