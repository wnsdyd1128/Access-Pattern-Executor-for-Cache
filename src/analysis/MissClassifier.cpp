#include "analysis/MissClassifier.hpp"

namespace apex {

MissClassifier::MissClassifier(int fa_capacity_lines)
    : fa_capacity_(fa_capacity_lines) {}

bool MissClassifier::fa_access(uint64_t cache_line) {
    auto it = fa_map_.find(cache_line);
    bool hit = (it != fa_map_.end());

    if (hit) {
        fa_lru_.erase(it->second);
    } else if (static_cast<int>(fa_lru_.size()) >= fa_capacity_) {
        // LRU 퇴출: 리스트 맨 앞(가장 오래된 라인)
        uint64_t evicted = fa_lru_.front();
        fa_lru_.pop_front();
        fa_map_.erase(evicted);
    }

    fa_lru_.push_back(cache_line);
    fa_map_[cache_line] = std::prev(fa_lru_.end());
    return hit;
}

std::optional<MissType> MissClassifier::classify(uint64_t cache_line, bool is_actual_miss) {
    bool is_cold   = (ever_seen_.count(cache_line) == 0);
    bool fa_hit    = fa_access(cache_line);  // FA 상태 갱신 (항상 실행)
    ever_seen_.insert(cache_line);

    if (!is_actual_miss) return std::nullopt;

    if (is_cold)   return MissType::Cold;
    if (fa_hit)    return MissType::Conflict;
    return MissType::Capacity;
}

}  // namespace apex
