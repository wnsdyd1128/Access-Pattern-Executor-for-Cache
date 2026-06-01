#pragma once
#include <cstdint>
#include <list>

struct SetAccessResult {
    bool     hit          = false;
    bool     has_eviction = false;
    uint64_t evicted_tag  = 0;
    bool     evicted_dirty = false;
};

/**
 * @brief N-way LRU 캐시 셋.
 *
 * front = MRU, back = LRU.
 * 미스 시 자동으로 새 태그를 채우고, 용량 초과 시 LRU를 퇴출한다.
 */
class CacheSet {
public:
    explicit CacheSet(int num_ways);
    SetAccessResult access(uint64_t tag, bool is_store);

private:
    struct Entry { uint64_t tag; bool dirty; };
    int              num_ways_;
    std::list<Entry> entries_;  // front = MRU, back = LRU
};
