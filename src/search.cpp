#include "search.h"
#include <algorithm>
#include <cwctype>
#include <limits>

namespace recall {
bool fuzzy_matches(std::wstring_view query,std::wstring_view text,const Cancelled& cancelled) {
    if(query.empty()) return true;
    if(query.size()>text.size()) return false;
    size_t q=0;
    auto wanted=towlower(query[q]);
    for(size_t i=0;i<text.size();++i) {
        if((i%1024)==0&&cancelled&&cancelled()) return false;
        if(wanted==towlower(text[i])) {
            if(++q==query.size()) return true;
            wanted=towlower(query[q]);
        }
    }
    return false;
}
int fuzzy_score(std::wstring_view query, std::wstring_view text,const Cancelled& cancelled) {
    if(query.empty()) return 0;
    if(!fuzzy_matches(query,text,cancelled)) return -1;
    // Dynamic programming retains the best alignment rather than greedily consuming letters.
    constexpr int bad=std::numeric_limits<int>::min()/4;
    // Each search worker reuses its scratch space across candidates and queries.
    thread_local std::vector<int> prev,next,bonuses;
    thread_local std::wstring folded;
    prev.assign(text.size(),bad); next.resize(text.size());
    folded.resize(text.size()); bonuses.resize(text.size());
    for(size_t i=0;i<text.size();++i) {
        if((i%1024)==0&&cancelled&&cancelled()) return -1;
        folded[i]=static_cast<wchar_t>(towlower(text[i]));
        bool boundary=i==0||!iswalnum(text[i-1])||(iswlower(text[i-1])&&iswupper(text[i]));
        bonuses[i]=10+(boundary?18:0);
    }
    for(size_t q=0;q<query.size();++q) {
        if(cancelled&&cancelled()) return -1;
        const auto wanted=towlower(query[q]);
        std::fill(next.begin(),next.end(),bad); int best=bad;
        for(size_t i=0;i<text.size();++i) {
            if(i>0&&prev[i-1]!=bad) best=std::max(best,prev[i-1]+static_cast<int>(i-1));
            if(wanted!=folded[i]) continue;
            const int bonus=bonuses[i]+(query[q]==text[i]?1:0);
            if(q==0) next[i]=bonus-static_cast<int>(std::min(i,size_t{20}));
            else {
                if(best!=bad) next[i]=best-static_cast<int>(i)+bonus;
                if(i>0&&prev[i-1]!=bad) next[i]=std::max(next[i],prev[i-1]+bonus+14);
            }
        }
        prev.swap(next);
    }
    int result=*std::max_element(prev.begin(),prev.end());
    return result==bad?-1:std::max(0,result);
}
Query route_query(std::wstring_view input,std::wstring_view clipboard,std::wstring_view emoji) {
    Route r=Route::Apps;
    if(input.starts_with(clipboard)) { r=Route::Clipboard; input.remove_prefix(clipboard.size()); }
    else if(input.starts_with(emoji)) { r=Route::Emoji; input.remove_prefix(emoji.size()); }
    while(!input.empty()&&iswspace(input.front())) input.remove_prefix(1);
    return {r,std::wstring(input)};
}
std::vector<Item> ranked(const std::vector<Item>& items,std::wstring_view query,size_t limit,const Cancelled& cancelled) {
    struct Hit { int score; size_t index; };
    std::vector<Hit> hits;
    if(!limit) return {};
    hits.reserve(items.size());
    for(size_t i=0;i<items.size();++i) {
        if(cancelled&&cancelled()) return {};
        // Long clipboard entries remain searchable without quadratic work over megabytes of data.
        auto title=std::wstring_view(items[i].title).substr(0,8192);
        int s=fuzzy_score(query,title,cancelled);
        if(items[i].kind==Kind::Emoji) s=std::max(s,fuzzy_score(query,items[i].detail,cancelled));
        if(s>=0) hits.push_back({s,i});
    }
    const auto count=std::min(limit,hits.size());
    std::partial_sort(hits.begin(),hits.begin()+count,hits.end(),[](auto a,auto b){
        return a.score!=b.score?a.score>b.score:a.index<b.index;
    });
    std::vector<Item> out;
    out.reserve(count);
    for(size_t i=0;i<count;++i) out.push_back(items[hits[i].index]);
    return out;
}
}
