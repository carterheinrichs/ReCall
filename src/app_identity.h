#pragma once
#include "common.h"
#include <cwctype>
#include <algorithm>
namespace recall {
inline std::wstring app_path(std::wstring path) {
    wchar_t expanded[32768]{};
    auto n=ExpandEnvironmentStringsW(path.c_str(),expanded,32768);
    if(n&&n<=32768) path=expanded;
    path=fs::path(path).lexically_normal().wstring();
    std::replace(path.begin(),path.end(),L'/',L'\\');
    for(auto& c:path) c=static_cast<wchar_t>(towlower(c));
    return path;
}
inline std::wstring app_identity(std::wstring target,std::wstring_view arguments,std::wstring directory,int show=SW_SHOWNORMAL,bool elevated=false) {
    target=app_path(std::move(target));
    if(directory.empty()) directory=fs::path(target).parent_path().wstring();
    directory=app_path(std::move(directory));
    auto part=[](std::wstring_view s){return std::to_wstring(s.size())+L":"+std::wstring(s);};
    return part(target)+part(arguments)+part(directory)+L":"+std::to_wstring(show)+L":"+(elevated?L"1":L"0");
}
inline void add_application(std::vector<Item>& items,Item item) {
    for(auto& existing:items) if(existing.app_key==item.app_key) {
        if(existing.title!=item.title) existing.app_aliases.push_back(item.title);
        return;
    }
    items.push_back(std::move(item));
}
}
