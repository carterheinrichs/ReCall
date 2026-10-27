#include "search.h"
#include "json.h"
#include "editing.h"
#include <iostream>

using namespace recall;
int main() {
    int failures=0;
    auto check=[&](bool ok,const char* what) { if(!ok) { std::cerr << "FAIL: " << what << '\n'; ++failures; } };
    auto erase=[](std::wstring s,size_t begin,size_t end) {
        auto range=word_erase_range(s,begin,end); s.erase(range.first,range.second-range.first); return s;
    };
    check(erase(L"hello world",11,11)==L"hello ","Ctrl+Backspace deletes previous word");
    check(erase(L"hello world  ",13,13)==L"hello ","Ctrl+Backspace skips trailing whitespace");
    check(erase(L"hello world",8,8)==L"hello rld","Ctrl+Backspace in middle of word");
    check(erase(L"hello world",6,11)==L"hello ","Ctrl+Backspace deletes selection");
    check(erase(L"hello-world",11,11)==L"hello-","punctuation is a boundary");
    check(erase(L"> 日本語",5,5)==L"> ","Unicode word deletion preserves prefix");
    check(erase(L"hello 😀",8,8)==L"hello ","emoji surrogate pair remains intact");
    check(erase(L"cafe\u0301",5,5).empty(),"combining marks stay with word");
    check(erase(L"hello",0,0)==L"hello","Ctrl+Backspace at start");
    check(erase(L"",0,0).empty(),"Ctrl+Backspace on empty edit");
    check(fuzzy_score(L"vsc",L"VisualStudioCode")>=0,"CamelCase subsequence");
    check(fuzzy_score(L"abc",L"acb")==-1,"out-of-order letters rejected");
    check(fuzzy_score(L"",L"")==0,"empty query");
    check(fuzzy_score(L"x",L"")==-1,"empty candidate");
    check(fuzzy_score(L"code",L"Code")>fuzzy_score(L"code",L"cold older device"),"proximity bonus");
    check(fuzzy_score(L"fb",L"FooBar")>fuzzy_score(L"fb",L"foobaz"),"word boundary bonus");
    check(fuzzy_score(L"ab",L"a___ab")>fuzzy_score(L"ab",L"a________b"),"best non-greedy alignment");
    check(route_query(L">  hello",L">",L":").text==L"hello","prefix whitespace");
    check(route_query(L":smile",L">",L":").route==Route::Emoji,"emoji routing");
    check(route_query(L"code",L">",L":").route==Route::Apps,"default routing");
    check(route_query(L"clip: value",L"clip:",L"emoji:").route==Route::Clipboard,"custom multi-character prefixes");
    auto j=Json::parse(R"({"s":"\uD83D\uDE00","array":[true,null,3.5]})");
    check(wide(j.get("s")->string())==L"\xD83D\xDE00","surrogate pair decoding");
    check(j.get("array")->array().size()==3,"JSON collections");
    for(auto invalid:{R"({"a":1,"a":2})",R"([1,])",R"("\uD800")",R"(01)",R"(true false)",R"({"x":+1})"}) {
        bool threw=false; try { Json::parse(invalid); } catch(...) { threw=true; } check(threw,"malformed JSON rejected");
    }
    Item a,b; a.title=L"same"; b.title=L"same"; a.id=2; b.id=1;
    auto hits=ranked({a,b},L"sa"); check(hits.size()==2&&hits[0].id==2,"stable ranking");
    std::cout << (failures?"Core checks failed":"Core checks passed") << '\n'; return failures?1:0;
}
