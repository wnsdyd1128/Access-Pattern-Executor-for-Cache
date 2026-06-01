#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "cache/CacheLevel.hpp"
#include "cache/CacheConfig.hpp"

struct HierarchyAccessResult {
    uint64_t delay_cycles;
    int      miss_level;   // 0 = L1 hit, 1 = L2 hit, 2 = memory
};

/**
 * @brief private L1(s) → shared L2 → Memory 체인 시뮬레이터.
 *
 * L1 미스 시 자동으로 L1에 채우고, L2 미스 시 L2에도 채운다.
 * write-back 정책: L1 스토어 히트는 L2에 전파하지 않는다.
 *
 * @pre config.caches의 role="L1" 항목은 private_to >= 0이어야 한다.
 */
class CacheHierarchy {
public:
    explicit CacheHierarchy(const HierarchyConfig& config);
    HierarchyAccessResult access(int core_id, uint64_t cache_line, bool is_store);

private:
    struct LevelEntry {
        CacheLevel  level;
        int         delay_cycles = 0;
        WritePolicy write_policy = WritePolicy::WriteBack;
    };

    std::vector<LevelEntry> l1s_;      // indexed by core_id
    LevelEntry              l2_;
    int                     mem_delay_ = 120;
};
