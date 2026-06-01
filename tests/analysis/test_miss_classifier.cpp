#include <gtest/gtest.h>
#include "analysis/MissClassifier.hpp"

// FA 쉐도우 캐시 용량 = fa_capacity_lines (총 라인 수, 실제 캐시와 동일하게 설정하는 것이 원칙).
// classify(cache_line, is_actual_miss):
//   is_actual_miss == false → nullopt (FA만 갱신)
//   is_actual_miss == true  → MissType 반환

TEST(MissClassifier, first_access_is_cold_miss) {
    apex::MissClassifier cls(16);
    auto result = cls.classify(42, true);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, apex::MissType::Cold);
}

TEST(MissClassifier, hit_returns_no_miss_type) {
    apex::MissClassifier cls(16);
    cls.classify(42, true);               // cold miss → FA에 추가
    auto result = cls.classify(42, false); // L1 hit → 분류 없음
    EXPECT_FALSE(result.has_value());
}

TEST(MissClassifier, repeated_access_after_eviction_is_capacity_miss_when_fa_also_misses) {
    // FA 용량 4라인: 1→2→3→4 채운 뒤 5 접근 시 LRU인 라인 1이 FA에서 퇴출된다.
    apex::MissClassifier cls(4);
    cls.classify(1, true);
    cls.classify(2, true);
    cls.classify(3, true);
    cls.classify(4, true);
    cls.classify(5, true);  // 라인 1을 FA에서 LRU 퇴출

    // 라인 1은 ever_seen에 있지만 FA에서도 없음 → capacity miss
    auto result = cls.classify(1, true);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, apex::MissType::Capacity);
}

TEST(MissClassifier, conflict_miss_when_set_assoc_misses_but_fa_hits) {
    // FA 용량이 충분해 라인 1이 FA에서 퇴출되지 않는다.
    apex::MissClassifier cls(8);
    cls.classify(1, true);   // cold miss, FA에 추가

    // 실제 캐시에서 라인 1이 conflict로 퇴출됐으나 FA에는 여전히 존재.
    auto result = cls.classify(1, true);  // not cold, FA hit → conflict
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, apex::MissType::Conflict);
}
