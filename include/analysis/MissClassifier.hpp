#pragma once
#include <cstdint>
#include <list>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace apex {

enum class MissType { Cold, Capacity, Conflict };

/**
 * @brief L1 miss를 cold / capacity / conflict 로 분류한다.
 *
 * 동일 총 용량의 FA 쉐도우 캐시(LRU)를 실제 캐시와 병렬로 유지한다.
 *   cold     : 처음 보는 cache_line
 *   conflict : 실제 캐시 miss + FA hit  (set 배치 충돌)
 *   capacity : 실제 캐시 miss + FA miss (총 용량 부족)
 *
 * @note classify()는 is_actual_miss 여부와 무관하게 FA 상태를 항상 갱신한다.
 *       이래야 FA LRU 순서가 실제 캐시의 접근 패턴을 정확히 반영한다.
 *
 * @param fa_capacity_lines  FA 쉐도우 캐시의 총 라인 수 (= 실제 캐시 size_bytes / line_size)
 */
class MissClassifier {
public:
    explicit MissClassifier(int fa_capacity_lines);

    /**
     * @brief 접근 1회를 처리하고 miss 유형을 반환한다.
     *
     * @param cache_line      접근한 캐시 라인 번호
     * @param is_actual_miss  실제 set-assoc 캐시에서 miss 발생 여부
     * @return L1 miss 시 MissType, hit 시 std::nullopt
     */
    std::optional<MissType> classify(uint64_t cache_line, bool is_actual_miss);

private:
    int fa_capacity_;

    std::unordered_set<uint64_t>                                ever_seen_;
    std::list<uint64_t>                                         fa_lru_;
    std::unordered_map<uint64_t, std::list<uint64_t>::iterator> fa_map_;

    // FA 쉐도우 캐시 접근: 히트 여부 반환 후 LRU 갱신 및 용량 초과 시 퇴출.
    bool fa_access(uint64_t cache_line);
};

}  // namespace apex
