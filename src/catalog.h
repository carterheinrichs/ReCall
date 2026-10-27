#pragma once
#include "common.h"
#include <functional>
namespace recall {
std::vector<Item> index_apps(const std::function<bool()>& cancelled={});
std::vector<Item> load_emoji();
}
