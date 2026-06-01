#pragma once
#include <cstdint>
#include <vector>
#include "cache/CacheSet.hpp"

struct LevelAccessResult {
    bool          hit;
    SetAccessResult set_result;
};

/**
 * @brief num_sets 개의 CacheSet으로 구성된 캐시 레벨.
 *
 * set_index = cache_line % num_sets
 * tag       = cache_line / num_sets
 * 미스 시 해당 셋에 자동으로 채운다.
 */
class CacheLevel {
public:
    CacheLevel() = default;
    CacheLevel(int num_sets, int num_ways);

    LevelAccessResult access(uint64_t cache_line, bool is_store);

    static uint64_t set_index_of(uint64_t cache_line, int num_sets);
    static uint64_t tag_of(uint64_t cache_line, int num_sets);

private:
    int                  num_sets_ = 0;
    std::vector<CacheSet> sets_;
};
