#pragma once
#include "common.h"
#include <functional>
struct sqlite3;
namespace recall {
class Store {
    sqlite3* db_{};
    fs::path media_;
    unsigned limit_;
    void exec(const char* sql);
public:
    Store(const fs::path& state,unsigned limit);
    ~Store();
    Store(const Store&)=delete;
    Store& operator=(const Store&)=delete;
    void insert(const Item& item);
    std::vector<Item> recent(size_t limit=0);
    std::vector<Item> search(std::wstring_view query,size_t limit=100,const std::function<bool()>& cancelled={});
};
}
