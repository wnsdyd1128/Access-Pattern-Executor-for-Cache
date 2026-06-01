#include <gtest/gtest.h>
#include "cache/CacheLevel.hpp"

TEST(CacheLevel, cold_miss_on_first_access) {
    CacheLevel level(8, 4);
    auto r = level.access(17, false);
    EXPECT_FALSE(r.hit);
}

TEST(CacheLevel, hit_after_fill) {
    CacheLevel level(8, 4);
    level.access(17, false);  // miss → fill
    auto r = level.access(17, false);
    EXPECT_TRUE(r.hit);
}

TEST(CacheLevel, set_index_is_cache_line_mod_num_sets) {
    EXPECT_EQ(CacheLevel::set_index_of(17, 8), 1u);  // 17 % 8 = 1
}

TEST(CacheLevel, tag_is_cache_line_div_num_sets) {
    EXPECT_EQ(CacheLevel::tag_of(17, 8), 2u);  // 17 / 8 = 2
}
