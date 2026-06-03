#include <gtest/gtest.h>

#include "ap/ApLoader.hpp"
#include "ap/EventBuilder.hpp"

// build_program: ApProgram(roots+metadata)에서 이벤트 생성.
// access_path를 IndexExpr로 평가하고 resolve_offset으로 byte_offset을 채운다.
// 각 테스트는 하나의 동작만 검증한다.

using namespace apex;

namespace
{
const char* kArr1d = R"({
  "schema_version":2,
  "metadata":{"objects":{"global::A":{"kind":"array","shape":[100],"elem_type":"i32","elem_size":4}},"structs":{}},
  "functions":[{"function":"f","params":[],"annotations":["yard.analyze"],"body":[
    {"type":"Loop","var":"i","start":0,"bound":100,"depth":1,"body":[
      {"type":"Array","object":"global::A","access_path":[{"kind":"index","value":"i"}],"op":"store"}
    ]}
  ]}]
})";

const char* kArr2d = R"({
  "schema_version":2,
  "metadata":{"objects":{"global::M":{"kind":"array","shape":[8,8],"elem_type":"i32","elem_size":4}},"structs":{}},
  "functions":[{"function":"f","params":[],"annotations":["yard.analyze"],"body":[
    {"type":"Loop","var":"i","start":0,"bound":8,"depth":1,"body":[
      {"type":"Loop","var":"j","start":0,"bound":8,"depth":2,"body":[
        {"type":"Array","object":"global::M","access_path":[{"kind":"index","value":"i"},{"kind":"index","value":"j"}],"op":"load"}
      ]}
    ]}
  ]}]
})";

const char* kStruct = R"({
  "schema_version":2,
  "metadata":{"objects":{"function:g::param:o":{"kind":"pointer","elem_type":"Outer","elem_size":72}},
    "structs":{
      "Outer":{"size":72,"align":8,"fields":[
        {"offset":0,"kind":"scalar","elem_type":"i32","elem_size":4},
        {"offset":8,"kind":"array","shape":[4],"elem_type":"S","elem_size":16}]},
      "S":{"size":16,"align":8,"fields":[
        {"offset":0,"kind":"scalar","elem_type":"i32","elem_size":4},
        {"offset":8,"kind":"scalar","elem_type":"double","elem_size":8}]}
    }},
  "functions":[{"function":"g","params":["o"],"annotations":["yard.analyze"],"body":[
    {"type":"Array","object":"function:g::param:o","op":"store","access_path":[
      {"kind":"field","index":1},{"kind":"index","value":"2"},{"kind":"field","index":1}]}
  ]}]
})";

// helper(yard.inline)의 Scalar 접근을 main(yard.analyze)이 루프 3회 Call.
const char* kScalarCall = R"({
  "schema_version":2,
  "metadata":{"objects":{"global::x":{"kind":"scalar","elem_type":"i32","elem_size":4}},"structs":{}},
  "functions":[
    {"function":"helper","params":[],"annotations":["yard.inline"],"body":[
      {"type":"Scalar","object":"global::x","op":"load"}
    ]},
    {"function":"main","params":[],"annotations":["yard.analyze"],"body":[
      {"type":"Loop","var":"i","start":0,"bound":3,"depth":1,"body":[
        {"type":"Call","callee":"helper","args":[],"arg_objects":[]}
      ]}
    ]}
  ]
})";

// touch(yard.inline)가 포인터 param x[idx]를 load/store. call_kernel(yard.analyze)이
// for i 루프에서 touch(a, i)를 Call → param x↔global::a, idx↔루프변수 i 바인딩.
const char* kCall = R"({
  "schema_version":2,
  "metadata":{"objects":{
    "function:touch::param:x":{"kind":"pointer","elem_type":"float","elem_size":4},
    "function:touch::param:idx":{"kind":"scalar","elem_type":"i32","elem_size":4},
    "global::a":{"kind":"array","shape":[16],"elem_type":"float","elem_size":4}
  },"structs":{}},
  "functions":[
    {"function":"touch","params":["x","idx"],"annotations":["yard.inline"],"body":[
      {"type":"Array","object":"function:touch::param:x","op":"load","access_path":[{"kind":"index","value":"idx"}]},
      {"type":"Array","object":"function:touch::param:x","op":"store","access_path":[{"kind":"index","value":"idx"}]}
    ]},
    {"function":"call_kernel","params":[],"annotations":["yard.analyze"],"body":[
      {"type":"Loop","var":"i","start":0,"bound":16,"depth":1,"body":[
        {"type":"Call","callee":"touch","args":["a","i"],"arg_objects":["global::a","i"]}
      ]}
    ]}
  ]
})";

// 리터럴 인자: kernel(a, 5)를 루프 없이 1회 Call → idx param이 상수 5로 평가.
const char* kCallLiteral = R"({
  "schema_version":2,
  "metadata":{"objects":{
    "function:touch::param:x":{"kind":"pointer","elem_type":"float","elem_size":4},
    "global::a":{"kind":"array","shape":[16],"elem_type":"float","elem_size":4}
  },"structs":{}},
  "functions":[
    {"function":"touch","params":["x","idx"],"annotations":["yard.inline"],"body":[
      {"type":"Array","object":"function:touch::param:x","op":"load","access_path":[{"kind":"index","value":"idx"}]}
    ]},
    {"function":"call_kernel","params":[],"annotations":["yard.analyze"],"body":[
      {"type":"Call","callee":"touch","args":["a","5"],"arg_objects":["global::a","5"]}
    ]}
  ]
})";

std::vector<AccessEvent> events(const char* json)
{
  ApProgram p = ApLoader{}.load_program_string(json);
  return EventBuilder{}.build_program(p);
}
}  // namespace

TEST(EventBuilderV2, loop_produces_one_event_per_iteration)
{
  EXPECT_EQ(events(kArr1d).size(), 100u);
}

TEST(EventBuilderV2, event_carries_object_id)
{
  EXPECT_EQ(events(kArr1d)[0].object_name, "global::A");
}

TEST(EventBuilderV2, byte_offset_is_index_times_elem)
{
  EXPECT_EQ(events(kArr1d)[3].byte_offset, 12);  // 3 * 4
}

TEST(EventBuilderV2, op_preserved)
{
  EXPECT_EQ(events(kArr1d)[0].op, "store");
}

TEST(EventBuilderV2, nested_loop_produces_product_of_bounds)
{
  EXPECT_EQ(events(kArr2d).size(), 64u);  // 8 * 8
}

TEST(EventBuilderV2, two_d_row_major_byte_offset)
{
  // (i=1, j=2) → 인덱스 i*8+j=10 → (1*8+2)*4 = 40
  EXPECT_EQ(events(kArr2d)[10].byte_offset, 40);
}

TEST(EventBuilderV2, struct_access_byte_offset)
{
  // o.items[2].y → 8 + 2*16 + 8 = 48
  auto ev = events(kStruct);
  ASSERT_EQ(ev.size(), 1u);
  EXPECT_EQ(ev[0].byte_offset, 48);
}

TEST(EventBuilderV2, scalar_node_produces_event_with_object)
{
  // main이 helper의 Scalar 접근을 3회 Call → 3 이벤트, object=global::x
  auto ev = events(kScalarCall);
  ASSERT_EQ(ev.size(), 3u);
  EXPECT_EQ(ev[0].object_name, "global::x");
  EXPECT_EQ(ev[0].op, "load");
}

TEST(EventBuilderV2, call_is_inlined_with_region_path)
{
  // Call 전개 시 region_path에 callee가 포함된다.
  auto ev = events(kScalarCall);
  ASSERT_FALSE(ev.empty());
  EXPECT_NE(ev[0].region_path.find("helper"), std::string::npos);
}

TEST(EventBuilderV2, call_binds_object_to_caller_argument)
{
  // touch(a,i)의 x[idx] 접근 → param 객체가 아닌 호출자 인자 global::a로 귀속.
  auto ev = events(kCall);
  ASSERT_FALSE(ev.empty());
  EXPECT_EQ(ev[0].object_name, "global::a");
}

TEST(EventBuilderV2, call_binds_index_var_to_caller_loop_value)
{
  // for i: touch(a,i) → callee의 idx가 호출자 루프변수 i 값으로 평가(크래시 없음).
  // i=3의 load 이벤트(call당 load/store 2개 → index 6) → 3*4 = 12.
  auto ev = events(kCall);
  ASSERT_EQ(ev.size(), 32u);  // 16 iter * (load+store)
  EXPECT_EQ(ev[6].byte_offset, 12);
}

TEST(EventBuilderV2, call_with_literal_arg_binds_constant)
{
  // kernel(a,5) → idx param이 상수 5로 평가 → 5*4 = 20.
  auto ev = events(kCallLiteral);
  ASSERT_EQ(ev.size(), 1u);
  EXPECT_EQ(ev[0].byte_offset, 20);
}

TEST(EventBuilderV2, call_expansion_leaves_no_param_object)
{
  // 전개된 모든 이벤트는 실 객체로 귀속되어야 한다(param 객체 잔존 금지).
  for (const auto & e : events(kCall))
    EXPECT_EQ(e.object_name.rfind("function:touch::param:", 0), std::string::npos);
}
