#include <gtest/gtest.h>
#include "analysis/Attribution.hpp"

// Region path 규칙: 슬래시(/) 구분, 마지막 세그먼트 = exclusive 귀속 대상.
// record()는 L1 miss 1회를 귀속한다 (호출자가 miss_level >= 1 일 때만 호출).

TEST(Attribution, l1_miss_increments_region_l1_miss_count) {
    apex::Attribution attr;
    attr.record("loop_i", "A", "load", 1);
    EXPECT_EQ(attr.region_l1_miss_exclusive("loop_i"), 1u);
}

TEST(Attribution, inclusive_count_includes_child_region) {
    apex::Attribution attr;
    attr.record("outer/inner", "A", "load", 1);
    EXPECT_EQ(attr.region_l1_miss_inclusive("outer"), 1u);
}

TEST(Attribution, exclusive_count_excludes_child_region) {
    apex::Attribution attr;
    attr.record("outer/inner", "A", "load", 1);
    EXPECT_EQ(attr.region_l1_miss_exclusive("outer"), 0u);
}

TEST(Attribution, per_object_miss_count_correct) {
    apex::Attribution attr;
    attr.record("loop_i", "A", "load", 1);
    attr.record("loop_i", "A", "load", 1);
    attr.record("loop_i", "A", "load", 1);
    EXPECT_EQ(attr.object_miss_count("A"), 3u);
}

TEST(Attribution, load_and_store_counters_are_independent) {
    apex::Attribution attr;
    attr.record("loop_i", "A", "load",  1);
    attr.record("loop_i", "A", "load",  1);
    attr.record("loop_i", "B", "store", 1);
    EXPECT_EQ(attr.load_miss_count(),  2u);
    EXPECT_EQ(attr.store_miss_count(), 1u);
}
