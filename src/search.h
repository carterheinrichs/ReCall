#pragma once
#include "common.h"
#include <functional>
#include <map>
namespace recall {
using Cancelled = std::function<bool()>;
bool fuzzy_matches(std::wstring_view query,std::wstring_view candidate,const Cancelled& cancelled={});
int fuzzy_score(std::wstring_view query, std::wstring_view candidate,const Cancelled& cancelled={});
enum class Route { Apps, Clipboard, Emoji };
struct Query { Route route; std::wstring text; };
Query route_query(std::wstring_view input, std::wstring_view clipboard, std::wstring_view emoji);
struct AppUsage { long long launches{}, last_used{}; };
using AppUsageMap = std::map<std::wstring,AppUsage>;
std::vector<Item> ranked(const std::vector<Item>& items, std::wstring_view query, size_t limit=100,const Cancelled& cancelled={},const AppUsageMap& usage={},long long now=0);
}
