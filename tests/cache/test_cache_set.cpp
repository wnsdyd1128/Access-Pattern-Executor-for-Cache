#include <gtest/gtest.h>
#include "cache/CacheSet.hpp"

TEST(CacheSet, empty_set_misses_on_any_tag) {
    CacheSet set(2);
    auto r = set.access(42, false);
    EXPECT_FALSE(r.hit);
}

TEST(CacheSet, inserted_tag_hits_on_next_access) {
    CacheSet set(2);
    set.access(42, false);  // miss → fill
    auto r = set.access(42, false);
    EXPECT_TRUE(r.hit);
}

TEST(CacheSet, lru_evicts_least_recently_used) {
    CacheSet set(2);
    set.access(10, false);  // fill 10 → [10]
    set.access(20, false);  // fill 20 → [20, 10], LRU=10
    set.access(10, false);  // hit  10 → [10, 20], LRU=20
    auto r = set.access(30, false);  // miss → evict LRU=20
    EXPECT_FALSE(r.hit);
    EXPECT_TRUE(r.has_eviction);
    EXPECT_EQ(r.evicted_tag, 20u);
}

TEST(CacheSet, evicted_tag_returned_on_fill) {
    CacheSet set(1);            // 1-way: always evicts on miss
    set.access(10, false);      // fill tag=10
    auto r = set.access(20, false);  // evict tag=10
    EXPECT_TRUE(r.has_eviction);
    EXPECT_EQ(r.evicted_tag, 10u);
}

TEST(CacheSet, dirty_bit_set_on_store) {
    CacheSet set(1);
    set.access(10, true);        // store miss → fill dirty
    auto r = set.access(20, false);  // evict tag=10
    EXPECT_TRUE(r.evicted_dirty);
}
