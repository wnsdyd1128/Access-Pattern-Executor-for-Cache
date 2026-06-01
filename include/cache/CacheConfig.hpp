#pragma once
#include <cstdint>
#include <string>
#include <vector>

enum class WritePolicy  { WriteBack, WriteThrough };
enum class Replacement  { LRU };

struct CacheConfig {
    std::string name;
    std::string role;              // "L1" or "LLC"
    int         private_to    = -1;    // core id; -1 = shared
    uint64_t    size_bytes    = 0;
    int         line_size     = 64;
    int         associativity = 8;
    Replacement  replacement   = Replacement::LRU;
    WritePolicy  write_policy  = WritePolicy::WriteBack;
    bool        write_allocate = true;
    int         delay_cycles  = 0;
    std::string next;              // name of next level
};

struct MemoryConfig {
    std::string name         = "Memory";
    int         delay_cycles = 120;
};

struct HierarchyConfig {
    int                      num_cores = 1;
    std::vector<CacheConfig> caches;
    MemoryConfig             memory;
};
