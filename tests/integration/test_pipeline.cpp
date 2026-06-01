#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "ap/ApNode.hpp"
#include "cache/CacheConfig.hpp"
#include "pipeline/Pipeline.hpp"

// 손계산 가능한 소규모 커널로 파이프라인 동작을 고정하는 회귀 테스트 (TLD).
// 객체 base는 line_size 정렬되므로 첫 객체는 line 0에서 시작한다.

namespace
{

using NodeVec = std::vector<std::unique_ptr<apex::ApNode>>;

template <class... Ts>
NodeVec nodes(Ts &&... ts)
{
  NodeVec v;
  (v.push_back(std::move(ts)), ...);
  return v;
}

std::unique_ptr<apex::ApNode> array_node(std::string name, std::string op,
                                         std::vector<std::string> idx,
                                         std::vector<int64_t> shape,
                                         int64_t elem = 4)
{
  auto n = std::make_unique<apex::ArrayNode>();
  n->name = std::move(name);
  n->op = std::move(op);
  n->indices = std::move(idx);
  n->shape = std::move(shape);
  n->elem_size = elem;
  return n;
}

std::unique_ptr<apex::ApNode> loop_node(std::string var, int64_t lo, int64_t hi,
                                        NodeVec body)
{
  auto n = std::make_unique<apex::LoopNode>();
  n->var = std::move(var);
  n->start = lo;
  n->bound = hi;
  n->body = std::move(body);
  return n;
}

/// 단일 L1 + 큰 L2 + Memory 계층. L2는 L1 miss 분류에 영향을 주지 않게 충분히 크다.
HierarchyConfig make_config(uint64_t l1_bytes, int line_size, int assoc)
{
  HierarchyConfig cfg;
  cfg.num_cores = 1;

  CacheConfig l1;
  l1.name = "L1D0";
  l1.role = "L1";
  l1.private_to = 0;
  l1.size_bytes = l1_bytes;
  l1.line_size = line_size;
  l1.associativity = assoc;
  l1.write_policy = WritePolicy::WriteBack;
  l1.write_allocate = true;
  l1.delay_cycles = 1;
  l1.next = "L2";

  CacheConfig l2;
  l2.name = "L2";
  l2.role = "LLC";
  l2.private_to = -1;
  l2.size_bytes = 1u << 20;
  l2.line_size = line_size;
  l2.associativity = 8;
  l2.write_policy = WritePolicy::WriteBack;
  l2.write_allocate = true;
  l2.delay_cycles = 5;
  l2.next = "Memory";

  cfg.caches = {l1, l2};
  cfg.memory.delay_cycles = 30;
  return cfg;
}

uint64_t total_misses(const apex::MissStats & s)
{
  return s.cold + s.capacity + s.conflict;
}

}  // namespace

// line_size=32 → 라인당 int 8개. 64개 순차 스캔 → 8 라인 → 8 cold miss, 나머지 hit.
TEST(Pipeline, sequential_scan_one_miss_per_line)
{
  auto tree = nodes(loop_node("i", 0, 64, nodes(array_node("A", "load", {"i"},
                                                           {64}))));
  auto r = apex::Pipeline(make_config(32768, 32, 8)).run(std::move(tree));

  EXPECT_EQ(r.stats.cold, 8u);
  EXPECT_EQ(r.stats.capacity + r.stats.conflict, 0u);
  EXPECT_EQ(r.stats.load, 8u);
  EXPECT_EQ(r.stats.by_object["A"], 8u);
}

// A[i][0] → byte (i*8)*4 = 32*i = 라인 i. stride가 정확히 line_size → 모든 접근이 miss.
TEST(Pipeline, stride_equal_to_line_misses_every_access)
{
  auto tree = nodes(loop_node("i", 0, 32,
                              nodes(array_node("A", "load", {"i", "z"},
                                               {32, 8}))));
  auto r = apex::Pipeline(make_config(32768, 32, 8)).run(std::move(tree));

  EXPECT_EQ(r.stats.cold, 32u);  // 32회 접근 모두 새 라인
  EXPECT_EQ(r.stats.load, 32u);
}

// direct-mapped(assoc=1, 4 sets). A[0]=line0(set0), B[0]=line4(set0) → 같은 set 교번.
// 총 용량(4라인)은 충분하므로 FA는 hit → set 충돌은 conflict miss로 분류된다.
TEST(Pipeline, alternating_same_set_access_is_conflict_miss)
{
  auto body = nodes(array_node("A", "load", {"z"}, {32}),   // 128B = line 0..3
                    array_node("B", "load", {"z"}, {1}));   // base 128 = line 4
  auto tree = nodes(loop_node("k", 0, 3, std::move(body)));
  auto r = apex::Pipeline(make_config(128, 32, 1)).run(std::move(tree));

  EXPECT_EQ(r.stats.cold, 2u);      // 최초 A, B
  EXPECT_EQ(r.stats.conflict, 4u);  // 이후 A,B,A,B
  EXPECT_EQ(r.stats.capacity, 0u);
}

// A[8][8], L1=128B(4라인)으로 배열(8라인)보다 작음.
// row 순회는 라인당 8회 연속 접근 → 8 miss. col 순회는 라인 재사용이 evict로 깨짐 → 훨씬 많음.
TEST(Pipeline, column_traversal_misses_more_than_row_traversal)
{
  auto row_tree = nodes(loop_node(
    "i", 0, 8, nodes(loop_node("j", 0, 8,
                               nodes(array_node("A", "load", {"i", "j"},
                                                {8, 8}))))));
  auto col_tree = nodes(loop_node(
    "j", 0, 8, nodes(loop_node("i", 0, 8,
                               nodes(array_node("A", "load", {"i", "j"},
                                                {8, 8}))))));

  auto row = apex::Pipeline(make_config(128, 32, 4)).run(std::move(row_tree));
  auto col = apex::Pipeline(make_config(128, 32, 4)).run(std::move(col_tree));

  EXPECT_EQ(total_misses(row.stats), 8u);
  EXPECT_GT(total_misses(col.stats), total_misses(row.stats));
}

// A는 load, B는 store. 각각 4개 라인에 stride 접근 → load miss 4, store miss 4 독립 집계.
TEST(Pipeline, load_and_store_misses_attributed_separately)
{
  auto body = nodes(array_node("A", "load", {"i", "z"}, {4, 8}),
                    array_node("B", "store", {"i", "z"}, {4, 8}));
  auto tree = nodes(loop_node("i", 0, 4, std::move(body)));
  auto r = apex::Pipeline(make_config(32768, 32, 8)).run(std::move(tree));

  EXPECT_EQ(r.stats.load, 4u);
  EXPECT_EQ(r.stats.store, 4u);
  EXPECT_EQ(r.attribution.load_miss_count(), 4u);
  EXPECT_EQ(r.attribution.store_miss_count(), 4u);
}
