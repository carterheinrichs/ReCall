#pragma once
#include "common.h"
#include <algorithm>

namespace recall {
inline size_t previous_codepoint(std::wstring_view text,size_t end) {
    if(!end) return 0;
    size_t start=end-1;
    if(start&&text[start]>=0xDC00&&text[start]<=0xDFFF&&text[start-1]>=0xD800&&text[start-1]<=0xDBFF) --start;
    return start;
}
inline int word_class(std::wstring_view point) {
    WORD type[2]{},marks[2]{};
    GetStringTypeW(CT_CTYPE1,point.data(),static_cast<int>(point.size()),type);
    GetStringTypeW(CT_CTYPE3,point.data(),static_cast<int>(point.size()),marks);
    if(type[0]&C1_SPACE) return 0;
    if(point[0]==L'_'||(type[0]&(C1_ALPHA|C1_DIGIT))||(marks[0]&(C3_NONSPACING|C3_DIACRITIC))) return 1;
    return 2;
}
inline std::pair<size_t,size_t> word_erase_range(std::wstring_view text,size_t begin,size_t end) {
    begin=std::min(begin,text.size()); end=std::min(end,text.size());
    if(begin>end) std::swap(begin,end);
    if(begin!=end||!begin) return {begin,end};
    size_t pos=begin;
    auto preceding_class=[&](size_t at) {
        size_t prev=previous_codepoint(text,at); return word_class(text.substr(prev,at-prev));
    };
    while(pos&&preceding_class(pos)==0) pos=previous_codepoint(text,pos);
    if(pos) {
        const int kind=preceding_class(pos);
        do { pos=previous_codepoint(text,pos); } while(pos&&preceding_class(pos)==kind);
    }
    return {pos,end};
}
}
