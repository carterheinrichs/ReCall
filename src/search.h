#pragma once
#include "common.h"
namespace recall {
int fuzzy_score(std::wstring_view query, std::wstring_view candidate);
enum class Route { Apps, Clipboard, Emoji };
struct Query { Route route; std::wstring text; };
Query route_query(std::wstring_view input, std::wstring_view clipboard, std::wstring_view emoji);
std::vector<Item> ranked(std::vector<Item> items, std::wstring_view query, size_t limit=100);
}
