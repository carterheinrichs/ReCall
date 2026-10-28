#pragma once
#include "common.h"
#include <map>
#include <variant>
#include <charconv>

namespace recall {
// Strict UTF-8 JSON reader shared by configuration and the embedded emoji data.
struct Json {
    using Object = std::map<std::string, Json, std::less<>>;
    using Array = std::vector<Json>;
    std::variant<std::nullptr_t, bool, double, std::string, Object, Array> value;
    const Object& object() const { return std::get<Object>(value); }
    const Array& array() const { return std::get<Array>(value); }
    const std::string& string() const { return std::get<std::string>(value); }
    const Json* get(std::string_view key) const {
        auto i = object().find(key); return i == object().end() ? nullptr : &i->second;
    }
    std::string dump() const {
        auto quote=[](const std::string& text) {
            std::string result="\"";
            constexpr char hex[]="0123456789abcdef";
            for(unsigned char c:text) {
                if(c=='"'||c=='\\') { result+='\\'; result+=static_cast<char>(c); }
                else if(c<32) { result+="\\u00"; result+=hex[c>>4]; result+=hex[c&15]; }
                else result+=static_cast<char>(c);
            }
            return result+'"';
        };
        if(std::holds_alternative<std::nullptr_t>(value)) return "null";
        if(auto v=std::get_if<bool>(&value)) return *v?"true":"false";
        if(auto v=std::get_if<double>(&value)) {
            char buffer[64]; auto result=std::to_chars(buffer,buffer+sizeof(buffer),*v);
            if(result.ec!=std::errc{}) throw std::runtime_error("Cannot serialize JSON number");
            return std::string(buffer,result.ptr);
        }
        if(auto v=std::get_if<std::string>(&value)) return quote(*v);
        std::string result;
        if(auto v=std::get_if<Object>(&value)) {
            result="{";
            for(const auto& [key,item]:*v) { if(result.size()>1) result+=","; result+="\n"+quote(key)+": "+item.dump(); }
            return result+"\n}";
        }
        result="[";
        for(const auto& item:array()) { if(result.size()>1) result+=","; result+=item.dump(); }
        return result+"]";
    }
    static Json parse(std::string_view input) {
        struct Parser {
            std::string_view s; size_t pos{}; unsigned depth{};
            [[noreturn]] void fail() { throw std::runtime_error("Invalid JSON at byte " + std::to_string(pos)); }
            void ws() { while (pos < s.size() && (s[pos]==' ' || s[pos]=='\n' || s[pos]=='\r' || s[pos]=='\t')) ++pos; }
            bool take(char c) { ws(); if (pos < s.size() && s[pos]==c) { ++pos; return true; } return false; }
            unsigned hex() {
                unsigned v=0;
                for (int i=0;i<4;++i) { if (pos==s.size()) fail(); char c=s[pos++]; v <<= 4;
                    if (c>='0'&&c<='9') v+=c-'0'; else if(c>='a'&&c<='f') v+=c-'a'+10;
                    else if(c>='A'&&c<='F') v+=c-'A'+10; else fail(); }
                return v;
            }
            std::string str() {
                if (!take('"')) fail(); std::string out;
                while(pos<s.size()) { char c=s[pos++]; if(c=='"') { wide(out); return out; }
                    if(static_cast<unsigned char>(c)<32) fail();
                    if(c!='\\') { out+=c; continue; }
                    if(pos==s.size()) fail(); c=s[pos++];
                    switch(c) {
                    case '"': case '\\': case '/': out+=c; break;
                    case 'b': out+='\b'; break; case 'f': out+='\f'; break;
                    case 'n': out+='\n'; break; case 'r': out+='\r'; break; case 't': out+='\t'; break;
                    case 'u': { unsigned cp=hex(); std::wstring w;
                        if(cp>=0xD800&&cp<=0xDBFF) { w+=static_cast<wchar_t>(cp);
                            if(pos+2>s.size()||s.substr(pos,2)!="\\u") fail(); pos+=2; cp=hex();
                            if(cp<0xDC00||cp>0xDFFF) fail();
                        } else if(cp>=0xDC00&&cp<=0xDFFF) fail();
                        w+=static_cast<wchar_t>(cp); out+=utf8(w); break; }
                    default: fail(); }
                } fail();
            }
            Json read() {
                if(++depth>64) fail(); ws(); if(pos==s.size()) fail(); Json result;
                if(s[pos]=='"') result.value=str();
                else if(take('{')) { Object o;
                    if(!take('}')) { do { auto k=str(); if(!take(':')) fail();
                        if(!o.emplace(std::move(k),read()).second) fail(); } while(take(',')); if(!take('}')) fail(); }
                    result.value=std::move(o);
                } else if(take('[')) { Array a; if(!take(']')) { do { a.push_back(read()); } while(take(',')); if(!take(']')) fail(); } result.value=std::move(a);
                } else if(s.substr(pos,4)=="true") { pos+=4; result.value=true;
                } else if(s.substr(pos,5)=="false") { pos+=5; result.value=false;
                } else if(s.substr(pos,4)=="null") { pos+=4; result.value=nullptr;
                } else {
                    const size_t start=pos;
                    if(pos<s.size()&&s[pos]=='-') ++pos;
                    if(pos==s.size()) fail();
                    if(s[pos]=='0') ++pos; else { if(s[pos]<'1'||s[pos]>'9') fail(); while(pos<s.size()&&s[pos]>='0'&&s[pos]<='9') ++pos; }
                    if(pos<s.size()&&s[pos]=='.') { ++pos; size_t p=pos; while(pos<s.size()&&s[pos]>='0'&&s[pos]<='9') ++pos; if(p==pos) fail(); }
                    if(pos<s.size()&&(s[pos]=='e'||s[pos]=='E')) { ++pos; if(pos<s.size()&&(s[pos]=='+'||s[pos]=='-')) ++pos;
                        size_t p=pos; while(pos<s.size()&&s[pos]>='0'&&s[pos]<='9') ++pos; if(p==pos) fail(); }
                    double number{}; auto r=std::from_chars(s.data()+start,s.data()+pos,number);
                    if(r.ec!=std::errc{}||r.ptr!=s.data()+pos) fail(); result.value=number;
                }
                --depth; return result;
            }
        } p{input};
        Json result=p.read(); p.ws(); if(p.pos!=input.size()) p.fail(); return result;
    }
};
}
