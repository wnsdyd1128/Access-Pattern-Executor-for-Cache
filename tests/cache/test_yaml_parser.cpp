#include <gtest/gtest.h>
#include <fstream>
#include "cache/YamlConfigParser.hpp"

static void write_yaml(const std::string& path, const std::string& content) {
    std::ofstream f(path);
    f << content;
}

TEST(YamlConfigParser, parses_single_cache_entry) {
    write_yaml("/tmp/apex_test_single.yaml", R"(
caches:
  - name: L1D0
    role: L1
    private_to: 0
    size_bytes: 32768
    line_size: 64
    associativity: 8
    replacement: LRU
    write_policy: write-back
    write_allocate: true
    delay_cycles: 4
    next: Memory
memory:
  delay_cycles: 120
)");
    auto cfg = YamlConfigParser::parse("/tmp/apex_test_single.yaml");
    ASSERT_EQ(cfg.caches.size(), 1u);
    const auto& c = cfg.caches[0];
    EXPECT_EQ(c.name, "L1D0");
    EXPECT_EQ(c.role, "L1");
    EXPECT_EQ(c.private_to, 0);
    EXPECT_EQ(c.size_bytes, 32768u);
    EXPECT_EQ(c.line_size, 64);
    EXPECT_EQ(c.associativity, 8);
    EXPECT_EQ(c.delay_cycles, 4);
}

TEST(YamlConfigParser, like_field_inherits_parent_config) {
    write_yaml("/tmp/apex_test_like.yaml", R"(
caches:
  - name: L1D0
    role: L1
    private_to: 0
    size_bytes: 32768
    line_size: 64
    associativity: 8
    delay_cycles: 4
    next: L2
  - name: L1D1
    like: L1D0
    private_to: 1
memory:
  delay_cycles: 120
)");
    auto cfg = YamlConfigParser::parse("/tmp/apex_test_like.yaml");
    ASSERT_EQ(cfg.caches.size(), 2u);
    const auto& c = cfg.caches[1];
    EXPECT_EQ(c.name, "L1D1");
    EXPECT_EQ(c.private_to, 1);        // overridden
    EXPECT_EQ(c.size_bytes, 32768u);   // inherited
    EXPECT_EQ(c.associativity, 8);     // inherited
    EXPECT_EQ(c.delay_cycles, 4);      // inherited
}

TEST(YamlConfigParser, parses_memory_level) {
    write_yaml("/tmp/apex_test_mem.yaml", R"(
caches: []
memory:
  delay_cycles: 200
)");
    auto cfg = YamlConfigParser::parse("/tmp/apex_test_mem.yaml");
    EXPECT_EQ(cfg.memory.delay_cycles, 200);
}

TEST(YamlConfigParser, aborts_on_missing_required_field) {
    write_yaml("/tmp/apex_test_bad.yaml", R"(
caches:
  - name: L1D0
    role: L1
    line_size: 64
memory:
  delay_cycles: 120
)");
    EXPECT_THROW(YamlConfigParser::parse("/tmp/apex_test_bad.yaml"), std::runtime_error);
}
