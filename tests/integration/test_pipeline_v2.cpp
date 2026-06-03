#include <gtest/gtest.h>

#include <string>

#include "ap/ApLoader.hpp"
#include "cache/CacheConfig.hpp"
#include "pipeline/Pipeline.hpp"

// LAT v2 end-to-end: ApLoader.load_program_string → Pipeline.run(ApProgram).
// 손계산 가능한 커널로 동작을 고정한다 (TLD).

using namespace apex;

namespace
{
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
  l1.next = "L2";

  CacheConfig l2;
  l2.name = "L2";
  l2.role = "LLC";
  l2.private_to = -1;
  l2.size_bytes = 1u << 20;
  l2.line_size = line_size;
  l2.associativity = 8;
  l2.write_policy = WritePolicy::WriteBack;
  l2.next = "Memory";

  cfg.caches = {l1, l2};
  cfg.memory.delay_cycles = 30;
  return cfg;
}

PipelineResult run(const char* json, HierarchyConfig cfg)
{
  ApProgram p = ApLoader{}.load_program_string(json);
  return Pipeline(std::move(cfg)).run(p);
}

uint64_t total_misses(const MissStats& s)
{
  return s.cold + s.capacity + s.conflict;
}

// 1D 순차: A[64], line 32(=8 ints/line) → 8 라인 → 8 cold miss.
const char* kSeq1d = R"({
  "schema_version":2,
  "metadata":{"objects":{"global::A":{"kind":"array","shape":[64],"elem_type":"i32","elem_size":4}},"structs":{}},
  "functions":[{"function":"f","params":[],"annotations":["yard.analyze"],"body":[
    {"type":"Loop","var":"i","start":0,"bound":64,"depth":1,"body":[
      {"type":"Array","object":"global::A","access_path":[{"kind":"index","value":"i"}],"op":"load"}
    ]}
  ]}]
})";

// 2D local 커널: A[i][j] store + B[i][j] load, 64x64, line 64(=16 ints/line).
// 행당 64원소=4라인 → 4 miss × 64행 = 256/배열.
const char* kLocal2d = R"({
  "schema_version":2,
  "metadata":{"objects":{
    "fn::A":{"kind":"array","shape":[64,64],"elem_type":"i32","elem_size":4},
    "fn::B":{"kind":"array","shape":[64,64],"elem_type":"i32","elem_size":4}},"structs":{}},
  "functions":[{"function":"f","params":[],"annotations":["yard.analyze"],"body":[
    {"type":"Loop","var":"i","start":0,"bound":64,"depth":1,"body":[
      {"type":"Loop","var":"j","start":0,"bound":64,"depth":2,"body":[
        {"type":"Array","object":"fn::B","access_path":[{"kind":"index","value":"i"},{"kind":"index","value":"j"}],"op":"load"},
        {"type":"Array","object":"fn::A","access_path":[{"kind":"index","value":"i"},{"kind":"index","value":"j"}],"op":"store"}
      ]}
    ]}
  ]}]
})";

// stride = line_size: A[i][0], shape[32,8] elem4 → byte 32*i = 라인 i. 매 접근 새 라인.
const char* kStride = R"({
  "schema_version":2,
  "metadata":{"objects":{"global::A":{"kind":"array","shape":[32,8],"elem_type":"i32","elem_size":4}},"structs":{}},
  "functions":[{"function":"f","params":[],"annotations":["yard.analyze"],"body":[
    {"type":"Loop","var":"i","start":0,"bound":32,"depth":1,"body":[
      {"type":"Array","object":"global::A","access_path":[{"kind":"index","value":"i"},{"kind":"index","value":"0"}],"op":"load"}
    ]}
  ]}]
})";

// direct-mapped(assoc=1, line32, 128B=4 set). A[0]=line0(set0), B[0]=line4(set0).
// A는 128B라 base0(line0..3), B는 base128(line4). 같은 set 교번 → conflict.
const char* kConflict = R"({
  "schema_version":2,
  "metadata":{"objects":{
    "global::A":{"kind":"array","shape":[32],"elem_type":"i32","elem_size":4},
    "global::B":{"kind":"array","shape":[8],"elem_type":"i32","elem_size":4}},"structs":{}},
  "functions":[{"function":"f","params":[],"annotations":["yard.analyze"],"body":[
    {"type":"Loop","var":"k","start":0,"bound":3,"depth":1,"body":[
      {"type":"Array","object":"global::A","access_path":[{"kind":"index","value":"0"}],"op":"load"},
      {"type":"Array","object":"global::B","access_path":[{"kind":"index","value":"0"}],"op":"load"}
    ]}
  ]}]
})";

// A[8][8] elem4, line32(=8 ints/line) → 한 행=1 라인. L1=128B(4 라인)로 배열(8 라인)보다 작음.
const char* kRow = R"({
  "schema_version":2,
  "metadata":{"objects":{"global::A":{"kind":"array","shape":[8,8],"elem_type":"i32","elem_size":4}},"structs":{}},
  "functions":[{"function":"f","params":[],"annotations":["yard.analyze"],"body":[
    {"type":"Loop","var":"i","start":0,"bound":8,"depth":1,"body":[
      {"type":"Loop","var":"j","start":0,"bound":8,"depth":2,"body":[
        {"type":"Array","object":"global::A","access_path":[{"kind":"index","value":"i"},{"kind":"index","value":"j"}],"op":"load"}
      ]}
    ]}
  ]}]
})";

const char* kCol = R"({
  "schema_version":2,
  "metadata":{"objects":{"global::A":{"kind":"array","shape":[8,8],"elem_type":"i32","elem_size":4}},"structs":{}},
  "functions":[{"function":"f","params":[],"annotations":["yard.analyze"],"body":[
    {"type":"Loop","var":"j","start":0,"bound":8,"depth":1,"body":[
      {"type":"Loop","var":"i","start":0,"bound":8,"depth":2,"body":[
        {"type":"Array","object":"global::A","access_path":[{"kind":"index","value":"i"},{"kind":"index","value":"j"}],"op":"load"}
      ]}
    ]}
  ]}]
})";
}  // namespace

TEST(PipelineV2, sequential_1d_one_miss_per_line)
{
  auto r = run(kSeq1d, make_config(32768, 32, 8));
  EXPECT_EQ(r.stats.cold, 8u);
  EXPECT_EQ(total_misses(r.stats), 8u);
}

TEST(PipelineV2, local_2d_kernel_cold_miss_count)
{
  auto r = run(kLocal2d, make_config(1u << 20, 64, 8));
  EXPECT_EQ(total_misses(r.stats), 512u);  // 256 + 256
}

TEST(PipelineV2, local_2d_kernel_load_store_split)
{
  auto r = run(kLocal2d, make_config(1u << 20, 64, 8));
  EXPECT_EQ(r.stats.load, 256u);   // B
  EXPECT_EQ(r.stats.store, 256u);  // A
}

TEST(PipelineV2, per_object_miss_attribution)
{
  auto r = run(kLocal2d, make_config(1u << 20, 64, 8));
  EXPECT_EQ(r.stats.by_object["fn::A"], 256u);
  EXPECT_EQ(r.stats.by_object["fn::B"], 256u);
}

TEST(PipelineV2, stride_equal_to_line_misses_every_access)
{
  auto r = run(kStride, make_config(32768, 32, 8));
  EXPECT_EQ(r.stats.cold, 32u);            // 32회 접근 모두 새 라인
  EXPECT_EQ(total_misses(r.stats), 32u);
}

TEST(PipelineV2, alternating_same_set_access_is_conflict_miss)
{
  auto r = run(kConflict, make_config(128, 32, 1));  // direct-mapped, 4 set
  EXPECT_EQ(r.stats.cold, 2u);      // 최초 A, B
  EXPECT_EQ(r.stats.conflict, 4u);  // 이후 A,B,A,B
  EXPECT_EQ(r.stats.capacity, 0u);
}

TEST(PipelineV2, column_traversal_misses_more_than_row_traversal)
{
  auto row = run(kRow, make_config(128, 32, 4));
  auto col = run(kCol, make_config(128, 32, 4));
  EXPECT_EQ(total_misses(row.stats), 8u);  // 행당 1 라인
  EXPECT_GT(total_misses(col.stats), total_misses(row.stats));
}
