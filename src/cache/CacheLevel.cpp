#include "cache/CacheLevel.hpp"

CacheLevel::CacheLevel(int num_sets, int num_ways) : num_sets_(num_sets) {
    sets_.reserve(num_sets);
    for (int i = 0; i < num_sets; ++i) sets_.emplace_back(num_ways);
}

LevelAccessResult CacheLevel::access(uint64_t cache_line, bool is_store) {
    uint64_t si  = set_index_of(cache_line, num_sets_);
    uint64_t tag = tag_of(cache_line, num_sets_);
    auto sr = sets_[si].access(tag, is_store);
    return {sr.hit, sr};
}

uint64_t CacheLevel::set_index_of(uint64_t cache_line, int num_sets) {
    return cache_line % static_cast<uint64_t>(num_sets);
}

uint64_t CacheLevel::tag_of(uint64_t cache_line, int num_sets) {
    return cache_line / static_cast<uint64_t>(num_sets);
}
