#include "config.h"
#include "json.h"
#include <appmodel.h>
#include <fstream>
#include <iterator>
#include <cmath>

namespace recall {
Config Config::load() {
    Config c;
    UINT32 n=0;
    if(GetCurrentPackageFamilyName(&n,nullptr)==ERROR_INSUFFICIENT_BUFFER) {
        std::wstring family(n,L'\0');
        if(GetCurrentPackageFamilyName(&n,family.data())!=ERROR_SUCCESS) throw std::runtime_error("Cannot read package identity");
        family.resize(n-1);
        c.state=known_folder(FOLDERID_LocalAppData)/L"Packages"/family/L"LocalState";
    } else c.state=known_folder(FOLDERID_LocalAppData)/L"ReCall";
    fs::create_directories(c.state/L"clips_media");
    const auto path=c.state/L"config.json";
    if(!fs::exists(path)) {
        std::ofstream out(path,std::ios::binary);
        out << "{\n  \"hotkey\": { \"modifiers\": [\"win\"], \"key\": \"SPACE\", \"hook_fallback\": true },\n"
               "  \"prefixes\": { \"clipboard\": \">\", \"emoji\": \":\" },\n  \"auto_paste\": true,\n  \"history_limit\": 1000,\n"
               "  \"appearance\": { \"opacity\": 80, \"blur\": true }\n}\n";
        if(!out) throw std::runtime_error("Cannot create config.json");
    }
    std::ifstream in(path,std::ios::binary);
    if(!in) throw std::runtime_error("Cannot read config.json");
    std::string text((std::istreambuf_iterator<char>(in)),{});
    auto settings=parse(text); settings.state=std::move(c.state); return settings;
}
Config Config::parse(std::string_view text) {
    Config c;
    auto root=Json::parse(text);
    if(auto h=root.get("hotkey")) {
        if(auto m=h->get("modifiers")) { c.modifiers=0; for(auto& v:m->array()) {
            auto s=v.string(); if(s=="win") c.modifiers|=MOD_WIN; else if(s=="ctrl") c.modifiers|=MOD_CONTROL;
            else if(s=="alt") c.modifiers|=MOD_ALT; else if(s=="shift") c.modifiers|=MOD_SHIFT;
            else throw std::runtime_error("Unknown hotkey modifier");
        } }
        if(auto k=h->get("key")) {
            auto s=k->string();
            if(s=="SPACE") c.key=VK_SPACE;
            else if(s.size()==1&&((s[0]>='A'&&s[0]<='Z')||(s[0]>='0'&&s[0]<='9'))) c.key=static_cast<UINT>(s[0]);
            else if(s.size()>=2&&s[0]=='F') { int f=std::stoi(s.substr(1)); if(f<1||f>24||s!="F"+std::to_string(f)) throw std::runtime_error("Invalid function key"); c.key=VK_F1+f-1; }
            else throw std::runtime_error("Hotkey key must be SPACE, A-Z, 0-9, or F1-F24");
        }
        if(auto b=h->get("hook_fallback")) c.hook_fallback=std::get<bool>(b->value);
    }
    if(auto p=root.get("prefixes")) {
        if(auto v=p->get("clipboard")) c.clipboard_prefix=wide(v->string());
        if(auto v=p->get("emoji")) c.emoji_prefix=wide(v->string());
    }
    if(c.clipboard_prefix.empty()||c.emoji_prefix.empty()||c.clipboard_prefix.starts_with(c.emoji_prefix)||c.emoji_prefix.starts_with(c.clipboard_prefix))
        throw std::runtime_error("Prefixes must be nonempty and must not overlap");
    if(auto v=root.get("auto_paste")) c.auto_paste=std::get<bool>(v->value);
    if(auto v=root.get("history_limit")) {
        double x=std::get<double>(v->value);
        if(x<1||x>100000||x!=static_cast<unsigned>(x)) throw std::runtime_error("history_limit must be an integer from 1 to 100000");
        c.history_limit=static_cast<unsigned>(x);
    }
    if(auto appearance=root.get("appearance")) {
        if(auto v=appearance->get("mode")) {
            auto value=v->string();
            if(value=="system") c.color_mode=ColorMode::System;
            else if(value=="light") c.color_mode=ColorMode::Light;
            else if(value=="dark") c.color_mode=ColorMode::Dark;
            else throw std::runtime_error("appearance.mode must be system, light, or dark");
        }
        if(auto v=appearance->get("theme")) {
            auto value=v->string();
            if(value=="default") c.theme=ThemeStyle::Default;
            else if(value=="liquid_glass") c.theme=ThemeStyle::LiquidGlass;
            else if(value=="classic_windows") c.theme=ThemeStyle::ClassicWindows;
            else throw std::runtime_error("appearance.theme must be default, liquid_glass, or classic_windows");
        }
        if(auto v=appearance->get("opacity")) {
            const auto* x=std::get_if<double>(&v->value);
            if(!x||!std::isfinite(*x)||*x<1||*x>100||std::floor(*x)!=*x)
                throw std::runtime_error("appearance.opacity must be an integer from 1 to 100 (percent)");
            c.opacity=static_cast<unsigned>(*x);
        }
        if(auto v=appearance->get("blur")) {
            const auto* enabled=std::get_if<bool>(&v->value);
            if(!enabled) throw std::runtime_error("appearance.blur must be true or false");
            c.blur=*enabled;
        }
    }
    return c;
}
void Config::save_appearance() const {
    auto path=state/L"config.json", temporary=state/L"config.json.tmp";
    std::ifstream in(path,std::ios::binary);
    if(!in) throw std::runtime_error("Cannot read config.json");
    std::string contents((std::istreambuf_iterator<char>(in)),{});
    in.close();
    auto root=Json::parse(contents);
    auto& object=std::get<Json::Object>(root.value);
    auto entry=object.try_emplace("appearance",Json{Json::Object{}}).first;
    auto& appearance=std::get<Json::Object>(entry->second.value);
    appearance["mode"].value=std::string(color_mode==ColorMode::Light?"light":color_mode==ColorMode::Dark?"dark":"system");
    appearance["theme"].value=std::string(theme==ThemeStyle::LiquidGlass?"liquid_glass":theme==ThemeStyle::ClassicWindows?"classic_windows":"default");
    appearance["opacity"].value=static_cast<double>(opacity);
    appearance["blur"].value=blur;
    std::ofstream out(temporary,std::ios::binary|std::ios::trunc);
    out << root.dump() << '\n'; out.close();
    if(!out) { DeleteFileW(temporary.c_str()); throw std::runtime_error("Cannot save appearance settings"); }
    if(!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str()); throw std::runtime_error("Cannot replace config.json");
    }
}

}
