#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>

namespace apex {

struct RegionCounter {
    uint64_t l1_miss_exclusive = 0;
    uint64_t l1_miss_inclusive = 0;
};

/**
 * @brief L1 miss를 Region / object / op 단위로 누적한다.
 *
 * region_path는 슬래시(/) 구분 계층 경로 (예: "func/loop_i/loop_j").
 * 마지막 세그먼트에 exclusive +1, 모든 세그먼트에 inclusive +1.
 * record()는 L1 miss 1회를 나타낸다 — 호출자는 miss_level >= 1 일 때만 호출해야 한다.
 *
 * @pre miss_level >= 1
 */
class Attribution {
public:
    void record(const std::string& region_path,
                const std::string& object_name,
                const std::string& op,
                int miss_level);

    uint64_t region_l1_miss_exclusive(const std::string& region_id) const;
    uint64_t region_l1_miss_inclusive(const std::string& region_id) const;
    uint64_t object_miss_count(const std::string& name) const;
    uint64_t load_miss_count()  const { return load_misses_; }
    uint64_t store_miss_count() const { return store_misses_; }

private:
    std::unordered_map<std::string, RegionCounter> regions_;
    std::unordered_map<std::string, uint64_t>      objects_;
    uint64_t load_misses_  = 0;
    uint64_t store_misses_ = 0;
};

}  // namespace apex
