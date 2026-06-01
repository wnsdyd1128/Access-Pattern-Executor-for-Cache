#include "cache/CacheSet.hpp"

CacheSet::CacheSet(int num_ways) : num_ways_(num_ways) {}

SetAccessResult CacheSet::access(uint64_t tag, bool is_store) {
    SetAccessResult result;

    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
        if (it->tag == tag) {
            result.hit = true;
            if (is_store) it->dirty = true;
            entries_.splice(entries_.begin(), entries_, it);  // promote to MRU
            return result;
        }
    }

    // Miss: evict LRU if at capacity
    if (static_cast<int>(entries_.size()) >= num_ways_) {
        result.has_eviction  = true;
        result.evicted_tag   = entries_.back().tag;
        result.evicted_dirty = entries_.back().dirty;
        entries_.pop_back();
    }

    entries_.push_front({tag, is_store});
    return result;
}
