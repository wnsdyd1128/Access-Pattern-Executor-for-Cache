#include "analysis/Attribution.hpp"
#include <sstream>
#include <vector>

namespace apex {

static std::vector<std::string> split_path(const std::string& path) {
    std::vector<std::string> segments;
    std::stringstream ss(path);
    std::string token;
    while (std::getline(ss, token, '/')) {
        if (!token.empty()) segments.push_back(token);
    }
    return segments;
}

void Attribution::record(const std::string& region_path,
                         const std::string& object_name,
                         const std::string& op,
                         int /*miss_level*/) {
    auto segments = split_path(region_path);

    for (std::size_t i = 0; i < segments.size(); ++i) {
        regions_[segments[i]].l1_miss_inclusive += 1;
    }
    if (!segments.empty()) {
        regions_[segments.back()].l1_miss_exclusive += 1;
    }

    objects_[object_name] += 1;

    if (op == "store") {
        store_misses_ += 1;
    } else {
        load_misses_ += 1;
    }
}

uint64_t Attribution::region_l1_miss_exclusive(const std::string& region_id) const {
    auto it = regions_.find(region_id);
    return it != regions_.end() ? it->second.l1_miss_exclusive : 0u;
}

uint64_t Attribution::region_l1_miss_inclusive(const std::string& region_id) const {
    auto it = regions_.find(region_id);
    return it != regions_.end() ? it->second.l1_miss_inclusive : 0u;
}

uint64_t Attribution::object_miss_count(const std::string& name) const {
    auto it = objects_.find(name);
    return it != objects_.end() ? it->second : 0u;
}

}  // namespace apex
