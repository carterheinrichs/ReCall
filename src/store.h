#pragma once
#include "common.h"
#include "search.h"
#include <functional>
#include <optional>
struct sqlite3;
namespace recall {
class Store {
    sqlite3* db_{};
    fs::path media_;
    unsigned limit_;
    size_t count_{}; // This worker-owned connection is the history's only writer.
    void exec(const char* sql);
public:
    Store(const fs::path& state,unsigned limit);
    ~Store();
    Store(const Store&)=delete;
    Store& operator=(const Store&)=delete;
    void insert(const Item& item);
    void record_app_launch(std::wstring_view key);
    AppUsageMap app_usage();
    std::optional<Item> load(long long id);
    // Lists contain bounded display text; load(id) retrieves the complete paste payload.
    std::vector<Item> recent(size_t limit=0);
    std::vector<Item> search(std::wstring_view query,size_t limit=100,const std::function<bool()>& cancelled={});
};
}
