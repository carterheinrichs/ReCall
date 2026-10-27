#include "search.h"
#include "json.h"
#include "editing.h"
#include <iostream>
#include <random>
#include <cwctype>
#include <limits>

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
    hits=ranked({a,b},L"sa",1); check(hits.size()==1&&hits[0].id==2,"top-k preserves input order on score ties");
    check(ranked({a,b},L"sa",0).empty(),"zero ranking limit");
    check(ranked({a,b},L"sa",100,[]{return true;}).empty(),"ranking cancels superseded work");
    Item fire,engine,fuzzy,heart;
    for(auto* item:{&fire,&engine,&fuzzy,&heart}) item->kind=Kind::Emoji;
    fire.title=L"fire"; fire.value=L"🔥"; fire.detail=L"fire Travel & Places sky & weather";
    engine.title=L"fire_engine"; engine.detail=L"fire engine Travel & Places transport-ground";
    fuzzy.title=L"first_reward"; fuzzy.detail=L"first reward";
    heart.title=L"heart_on_fire"; heart.detail=L"heart on fire Smileys & Emotion";
    auto emoji_hits=ranked({fuzzy,engine,heart,fire},L"fire");
    check(emoji_hits.size()==4&&emoji_hits[0].value==L"🔥"&&emoji_hits.back().title==fuzzy.title,
        "exact emoji name outranks literal related names and fuzzy matches");
    check(ranked({engine,fire},L" FIRE ")[0].value==L"🔥","emoji names ignore case and surrounding whitespace");
    check(ranked({engine,fire},L"fire engine")[0].title==engine.title,"spaces match shortcode underscores");
    check(ranked({engine,fire},L"🔥")[0].value==L"🔥","emoji glyph is searchable");
    check(ranked({fire},L"weather").size()==1,"emoji category tags remain searchable");
    int polls=0;
    check(fuzzy_score(std::wstring(64,L'a'),std::wstring(8192,L'a'),[&]{return ++polls>5;})==-1&&polls>5,"scoring cancels within a large candidate");
    // Exhaustive alignment is independent of the optimized dynamic-programming scorer.
    auto exhaustive=[](std::wstring_view query,std::wstring_view candidate) {
        if(query.empty()) return 0;
        int best=std::numeric_limits<int>::min();
        std::function<void(size_t,size_t,int,int)> visit=[&](size_t q,size_t start,int previous,int score) {
            if(q==query.size()) { best=std::max(best,score); return; }
            for(size_t i=start;i<candidate.size();++i) if(towlower(query[q])==towlower(candidate[i])) {
                bool boundary=i==0||!iswalnum(candidate[i-1])||(iswlower(candidate[i-1])&&iswupper(candidate[i]));
                int bonus=10+(boundary?18:0)+(query[q]==candidate[i]?1:0);
                int next=q==0?bonus-static_cast<int>(std::min(i,size_t{20})):
                    score+bonus+(static_cast<int>(i)==previous+1?14:previous-static_cast<int>(i));
                visit(q+1,i+1,static_cast<int>(i),next);
            }
        };
        visit(0,0,-1,0); return best==std::numeric_limits<int>::min()?-1:std::max(0,best);
    };
    std::mt19937 random(42); const std::wstring alphabet=L"aAbB_ 日本";
    for(int trial=0;trial<1000;++trial) {
        std::wstring candidate,query;
        auto n=random()%10,m=random()%5;
        for(unsigned i=0;i<n;++i) candidate+=alphabet[random()%alphabet.size()];
        for(unsigned i=0;i<m;++i) query+=alphabet[random()%alphabet.size()];
        const auto expected=exhaustive(query,candidate);
        check(fuzzy_score(query,candidate)==expected,"optimized score agrees with exhaustive alignments");
        check(fuzzy_matches(query,candidate)==(expected>=0),"linear filtering agrees with scored matching");
    }
    std::cout << (failures?"Core checks failed":"Core checks passed") << '\n'; return failures?1:0;
}
