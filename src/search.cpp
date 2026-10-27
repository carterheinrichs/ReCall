#include "search.h"
#include <algorithm>
#include <cwctype>
#include <limits>

namespace recall {
int fuzzy_score(std::wstring_view query, std::wstring_view text) {
    if(query.empty()) return 0;
    if(query.size()>text.size()) return -1;
    // Dynamic programming retains the best alignment rather than greedily consuming letters.
    constexpr int bad=std::numeric_limits<int>::min()/4;
    std::vector<int> prev(text.size(),bad), next(text.size(),bad);
    for(size_t q=0;q<query.size();++q) {
        std::fill(next.begin(),next.end(),bad); int best=bad;
        for(size_t i=0;i<text.size();++i) {
            if(i>0&&prev[i-1]!=bad) best=std::max(best,prev[i-1]+static_cast<int>(i-1));
            if(towlower(query[q])!=towlower(text[i])) continue;
            bool boundary=i==0||!iswalnum(text[i-1])||(iswlower(text[i-1])&&iswupper(text[i]));
            const int bonus=10+(boundary?18:0)+(query[q]==text[i]?1:0);
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
std::vector<Item> ranked(std::vector<Item> items,std::wstring_view query,size_t limit) {
    struct Hit { int score; size_t index; };
    std::vector<Hit> hits;
    for(size_t i=0;i<items.size();++i) {
        // Long clipboard entries remain searchable without quadratic work over megabytes of data.
        auto title=std::wstring_view(items[i].title).substr(0,8192);
        int s=fuzzy_score(query,title);
        if(items[i].kind==Kind::Emoji) s=std::max(s,fuzzy_score(query,items[i].detail));
        if(s>=0) hits.push_back({s,i});
    }
    std::stable_sort(hits.begin(),hits.end(),[](auto a,auto b){return a.score>b.score;});
    std::vector<Item> out;
    for(size_t i=0;i<std::min(limit,hits.size());++i) out.push_back(std::move(items[hits[i].index]));
    return out;
}
}
