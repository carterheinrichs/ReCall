#include "catalog.h"
#include "json.h"
#include <shobjidl.h>
#include <algorithm>
#include <cwctype>
#include <set>

namespace recall {
namespace {
std::wstring lower(std::wstring s) { for(auto& c:s) c=static_cast<wchar_t>(towlower(c)); return s; }
struct RegKey { HKEY h{}; ~RegKey() { if(h) RegCloseKey(h); } };
std::wstring reg_string(HKEY key,const wchar_t* name) {
    DWORD bytes=0;
    if(RegGetValueW(key,nullptr,name,RRF_RT_REG_SZ|RRF_RT_REG_EXPAND_SZ,nullptr,nullptr,&bytes)!=ERROR_SUCCESS||bytes>65536) return {};
    std::wstring s(bytes/sizeof(wchar_t),L'\0');
    if(RegGetValueW(key,nullptr,name,RRF_RT_REG_SZ|RRF_RT_REG_EXPAND_SZ,nullptr,s.data(),&bytes)!=ERROR_SUCCESS) return {};
    while(!s.empty()&&s.back()==0) s.pop_back(); return s;
}
}
std::vector<Item> index_apps() {
    std::vector<Item> out; std::set<std::wstring> seen;
    auto add=[&](Item i) {
        auto key=lower(i.title)+L"\n"+lower(i.value);
        if(seen.insert(key).second) out.push_back(std::move(i));
    };
    const KNOWNFOLDERID folders[]={FOLDERID_Desktop,FOLDERID_PublicDesktop,FOLDERID_Programs,FOLDERID_CommonPrograms};
    for(const auto& folder:folders) {
        auto root=known_folder(folder); if(root.empty()) continue;
        std::error_code ec;
        fs::recursive_directory_iterator it(root,fs::directory_options::skip_permission_denied,ec),end;
        while(it!=end) {
            const auto path=it->path(); auto ext=lower(path.extension().wstring());
            if(ext==L".lnk") {
                ComPtr<IShellLinkW> link;
                if(SUCCEEDED(CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(link.put())))) {
                    ComPtr<IPersistFile> file;
                    if(SUCCEEDED(link->QueryInterface(IID_PPV_ARGS(file.put())))&&SUCCEEDED(file->Load(path.c_str(),STGM_READ))) {
                        wchar_t target[32768]{},args[32768]{},cwd[32768]{}; WIN32_FIND_DATAW data{};
                        link->GetPath(target,32768,&data,SLGP_RAWPATH);
                        link->GetArguments(args,32768); link->GetWorkingDirectory(cwd,32768);
                        // Launch the original shell link to preserve advertised and packaged-app targets.
                        Item i; i.title=path.stem().wstring(); i.value=path.wstring();
                        i.detail=*target?target:L"Windows application"; add(std::move(i));
                    }
                }
            } else if(ext==L".exe"||ext==L".appref-ms") {
                Item i; i.title=path.stem().wstring(); i.value=path.wstring(); i.detail=path.wstring(); add(std::move(i));
            }
            it.increment(ec); if(ec) ec.clear();
        }
    }
    for(HKEY hive:{HKEY_CURRENT_USER,HKEY_LOCAL_MACHINE}) for(REGSAM view:{KEY_WOW64_64KEY,KEY_WOW64_32KEY}) {
        RegKey root;
        if(RegOpenKeyExW(hive,L"Software\\Microsoft\\Windows\\CurrentVersion\\App Paths",0,KEY_READ|view,&root.h)!=ERROR_SUCCESS) continue;
        for(DWORD idx=0;;++idx) {
            wchar_t name[256]{}; DWORD n=256; auto rc=RegEnumKeyExW(root.h,idx,name,&n,nullptr,nullptr,nullptr,nullptr);
            if(rc==ERROR_NO_MORE_ITEMS) break; if(rc!=ERROR_SUCCESS) continue;
            RegKey sub; if(RegOpenKeyExW(root.h,name,0,KEY_READ|view,&sub.h)!=ERROR_SUCCESS) continue;
            auto path=reg_string(sub.h,nullptr);
            if(path.size()>1&&path.front()==L'"'&&path.back()==L'"') path=path.substr(1,path.size()-2);
            std::error_code ec; if(!fs::is_regular_file(path,ec)) continue;
            Item i; i.title=fs::path(name).stem().wstring(); i.value=path; i.detail=path;
            i.working_directory=fs::path(path).parent_path().wstring(); add(std::move(i));
        }
    }
    std::sort(out.begin(),out.end(),[](const Item& a,const Item& b){return lower(a.title)<lower(b.title);});
    return out;
}
std::vector<Item> load_emoji() {
    auto resource=FindResourceW(nullptr,MAKEINTRESOURCEW(101),RT_RCDATA);
    if(!resource) throw std::runtime_error("Emoji resource missing");
    auto loaded=LoadResource(nullptr,resource);
    auto data=static_cast<const char*>(LockResource(loaded));
    if(!data) throw std::runtime_error("Cannot load emoji dictionary");
    auto json=Json::parse({data,SizeofResource(nullptr,resource)});
    std::vector<Item> items;
    for(auto& e:json.array()) {
        Item i; i.kind=Kind::Emoji; i.value=wide(e.get("glyph")->string());
        i.title=wide(e.get("shortcode")->string()); i.detail=wide(e.get("tags")->string()); items.push_back(std::move(i));
    }
    return items;
}
}
