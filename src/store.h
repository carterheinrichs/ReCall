#pragma once
#include "common.h"
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
    std::vector<Item> recent();
};
}
