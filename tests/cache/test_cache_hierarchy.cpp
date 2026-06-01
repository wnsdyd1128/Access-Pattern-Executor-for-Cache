#include <gtest/gtest.h>
#include "cache/CacheHierarchy.hpp"
#include "cache/CacheConfig.hpp"

namespace {
// L1: 1 set × 1 way (64B)  L2: 16 sets × 1 way (1KB)
HierarchyConfig make_config() {
    HierarchyConfig cfg;
    cfg.num_cores = 1;

    CacheConfig l1;
    l1.name = "L1";  l1.role = "L1";  l1.private_to = 0;
    l1.size_bytes = 64;  l1.line_size = 64;  l1.associativity = 1;
    l1.write_policy = WritePolicy::WriteBack;  l1.write_allocate = true;
    l1.delay_cycles = 4;  l1.next = "L2";
    cfg.caches.push_back(l1);

    CacheConfig l2;
    l2.name = "L2";  l2.role = "LLC";
    l2.size_bytes = 1024;  l2.line_size = 64;  l2.associativity = 1;
    l2.write_policy = WritePolicy::WriteBack;  l2.write_allocate = true;
    l2.delay_cycles = 12;  l2.next = "Memory";
    cfg.caches.push_back(l2);

    cfg.memory.delay_cycles = 120;
    return cfg;
}
}  // namespace

TEST(CacheHierarchy, l1_hit_returns_l1_delay_only) {
    CacheHierarchy h(make_config());
    h.access(0, 0, false);         // cold miss → fill L1 & L2
    auto r = h.access(0, 0, false);   // L1 hit
    EXPECT_EQ(r.delay_cycles, 4u);
    EXPECT_EQ(r.miss_level, 0);
}

TEST(CacheHierarchy, l1_miss_l2_hit_returns_l1_plus_l2_delay) {
    CacheHierarchy h(make_config());
    h.access(0, 0, false);  // fill L1[line 0] & L2[line 0]
    h.access(0, 1, false);  // evict line 0 from L1; L2 keeps line 0
    auto r = h.access(0, 0, false);  // L1 miss, L2 hit
    EXPECT_EQ(r.delay_cycles, 4u + 12u);
    EXPECT_EQ(r.miss_level, 1);
}

TEST(CacheHierarchy, l2_miss_returns_full_memory_delay) {
    CacheHierarchy h(make_config());
    auto r = h.access(0, 0, false);  // cold miss → L1+L2+Mem
    EXPECT_EQ(r.delay_cycles, 4u + 12u + 120u);
    EXPECT_EQ(r.miss_level, 2);
}

TEST(CacheHierarchy, l2_hit_fills_l1) {
    CacheHierarchy h(make_config());
    h.access(0, 0, false);  // fill L1 & L2
    h.access(0, 1, false);  // evict line 0 from L1
    h.access(0, 0, false);  // L1 miss, L2 hit → fills L1
    auto r = h.access(0, 0, false);  // L1 hit
    EXPECT_EQ(r.delay_cycles, 4u);
}

TEST(CacheHierarchy, write_back_does_not_propagate_on_store_hit) {
    CacheHierarchy h(make_config());
    h.access(0, 0, false);         // fill L1
    auto r = h.access(0, 0, true); // store hit at L1 → no L2 write
    EXPECT_EQ(r.delay_cycles, 4u);
    EXPECT_EQ(r.miss_level, 0);
}
