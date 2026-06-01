#include "cache/CacheHierarchy.hpp"
#include <stdexcept>

CacheHierarchy::CacheHierarchy(const HierarchyConfig& config) {
    mem_delay_ = config.memory.delay_cycles;

    for (const auto& c : config.caches) {
        int num_sets = static_cast<int>(
            c.size_bytes / (static_cast<uint64_t>(c.line_size) * c.associativity));

        LevelEntry entry;
        entry.level        = CacheLevel(num_sets, c.associativity);
        entry.delay_cycles = c.delay_cycles;
        entry.write_policy = c.write_policy;

        if (c.role == "L1") {
            int core = c.private_to;
            if (core < 0) throw std::runtime_error("L1 cache must have private_to >= 0");
            while (static_cast<int>(l1s_.size()) <= core) l1s_.emplace_back();
            l1s_[core] = std::move(entry);
        } else {
            l2_ = std::move(entry);
        }
    }
}

HierarchyAccessResult CacheHierarchy::access(int core_id, uint64_t cache_line, bool is_store) {
    auto& l1e = l1s_[core_id];

    // L1 lookup — auto-fills on miss
    auto r1 = l1e.level.access(cache_line, is_store);
    if (r1.hit) {
        return {static_cast<uint64_t>(l1e.delay_cycles), 0};
    }

    // L2 lookup — always read (write-back: dirty stays in L1 until eviction)
    auto r2 = l2_.level.access(cache_line, false);
    if (r2.hit) {
        return {static_cast<uint64_t>(l1e.delay_cycles + l2_.delay_cycles), 1};
    }

    return {static_cast<uint64_t>(l1e.delay_cycles + l2_.delay_cycles + mem_delay_), 2};
}
