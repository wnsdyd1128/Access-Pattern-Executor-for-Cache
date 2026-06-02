#include <gtest/gtest.h>

#include "ap/ApLoader.hpp"
#include "ap/ApNode.hpp"
#include "ap/ApProgram.hpp"

// LAT v2 파싱: root{schema_version,metadata,functions} →
//   ApProgram{functions(name→body), roots(yard.analyze), objects, structs}.
// Array 노드는 object id + access_path(미평가)로 파싱된다.
// 각 테스트는 하나의 동작만 검증한다.

using namespace apex;

namespace
{
const ArrayNode* first_array(const std::vector<std::unique_ptr<ApNode>>& body)
{
  for (const auto& n : body)
  {
    if (n->kind() == ApNodeKind::Array)
      return static_cast<const ArrayNode*>(n.get());
    if (n->kind() == ApNodeKind::Loop)
    {
      const auto* l = static_cast<const LoopNode*>(n.get());
      if (const ArrayNode* a = first_array(l->body)) return a;
    }
  }
  return nullptr;
}

const char* kArrayJson = R"({
  "schema_version": 2,
  "metadata": {
    "objects": {
      "global::A": {"id":"global::A","name":"A","scope":"global",
        "storage":"global","kind":"array","shape":[100],
        "elem_type":"i32","elem_size":4}
    },
    "structs": {}
  },
  "functions": [
    {"function":"f","params":[],"annotations":["yard.analyze"],
     "body":[
       {"type":"Loop","var":"i","start":0,"bound":100,"depth":1,
        "body":[
          {"type":"Array","name":"A[i]","object":"global::A","indices":["i"],
           "access_path":[{"kind":"index","value":"i"}],"op":"store"}
        ]}
     ]}
  ]
})";

const char* kStructJson = R"({
  "schema_version": 2,
  "metadata": {
    "objects": {
      "function:g::param:o": {"id":"function:g::param:o","name":"o",
        "scope":"function:g","storage":"param","kind":"pointer",
        "elem_type":"Outer","elem_size":72}
    },
    "structs": {
      "Outer": {"name":"Outer","size":72,"align":8,"fields":[
        {"name":"tag","index":0,"offset":0,"size":4,"kind":"scalar","elem_type":"i32","elem_size":4},
        {"name":"items","index":1,"offset":8,"size":64,"kind":"array","shape":[4],"elem_type":"S","elem_size":16}
      ]},
      "S": {"name":"S","size":16,"align":8,"fields":[
        {"name":"x","index":0,"offset":0,"size":4,"kind":"scalar","elem_type":"i32","elem_size":4},
        {"name":"y","index":1,"offset":8,"size":8,"kind":"scalar","elem_type":"double","elem_size":8}
      ]}
    }
  },
  "functions": [
    {"function":"g","params":["o"],"annotations":["yard.analyze"],
     "body":[
       {"type":"Array","name":"o.items[0].x","object":"function:g::param:o",
        "indices":[],"access_path":[
          {"kind":"field","index":1,"name":"items"},
          {"kind":"index","value":"0"},
          {"kind":"field","index":0,"name":"x"}],"op":"store"}
     ]}
  ]
})";

const char* kArray2dJson = R"({
  "schema_version": 2,
  "metadata": {
    "objects": {
      "global::M": {"id":"global::M","name":"M","scope":"global",
        "storage":"global","kind":"array","shape":[8,8],
        "elem_type":"i32","elem_size":4}
    },
    "structs": {}
  },
  "functions": [
    {"function":"f2","params":[],"annotations":["yard.analyze"],
     "body":[
       {"type":"Loop","var":"i","start":0,"bound":8,"depth":1,"body":[
         {"type":"Loop","var":"j","start":0,"bound":8,"depth":2,"body":[
           {"type":"Array","name":"M[i][j]","object":"global::M","indices":["i","j"],
            "access_path":[{"kind":"index","value":"i"},{"kind":"index","value":"j"}],
            "op":"load"}
         ]}
       ]}
     ]}
  ]
})";

ApProgram loadArray() { return ApLoader{}.load_program_string(kArrayJson); }
ApProgram loadArray2d() { return ApLoader{}.load_program_string(kArray2dJson); }
ApProgram loadStruct() { return ApLoader{}.load_program_string(kStructJson); }
}  // namespace

// ── 객체 metadata ─────────────────────────────────────────────

TEST(ApLoaderV2, object_shape_parsed)
{
  EXPECT_EQ(loadArray().objects.at("global::A").shape,
            (std::vector<int64_t>{100}));
}

TEST(ApLoaderV2, object_elem_size_parsed)
{
  EXPECT_EQ(loadArray().objects.at("global::A").elem_size, 4);
}

TEST(ApLoaderV2, non_struct_object_has_empty_struct_type)
{
  EXPECT_TRUE(loadArray().objects.at("global::A").struct_type.empty());
}

// ── 함수/root ─────────────────────────────────────────────────

TEST(ApLoaderV2, roots_list_only_yard_analyze_functions)
{
  ApProgram p = loadArray();
  ASSERT_EQ(p.roots.size(), 1u);
  EXPECT_EQ(p.roots[0], "f");
}

TEST(ApLoaderV2, functions_map_contains_parsed_function)
{
  EXPECT_EQ(loadArray().functions.count("f"), 1u);
}

// ── Array 노드 ────────────────────────────────────────────────

TEST(ApLoaderV2, array_node_carries_object_id)
{
  ApProgram p = loadArray();
  const ArrayNode* a = first_array(p.functions.at("f"));
  ASSERT_NE(a, nullptr);
  EXPECT_EQ(a->object, "global::A");
}

TEST(ApLoaderV2, array_node_access_path_index_step)
{
  ApProgram p = loadArray();
  const ArrayNode* a = first_array(p.functions.at("f"));
  ASSERT_NE(a, nullptr);
  ASSERT_EQ(a->access_path.size(), 1u);
  EXPECT_EQ(a->access_path[0].kind, RawAccessStep::Kind::Index);
  EXPECT_EQ(a->access_path[0].index_expr, "i");
}

// ── 2차원 배열 ────────────────────────────────────────────────

TEST(ApLoaderV2, object_2d_shape_parsed)
{
  EXPECT_EQ(loadArray2d().objects.at("global::M").shape,
            (std::vector<int64_t>{8, 8}));
}

TEST(ApLoaderV2, array_2d_access_path_has_two_index_steps)
{
  ApProgram p = loadArray2d();
  const ArrayNode* a = first_array(p.functions.at("f2"));
  ASSERT_NE(a, nullptr);
  ASSERT_EQ(a->access_path.size(), 2u);
  EXPECT_EQ(a->access_path[0].index_expr, "i");
  EXPECT_EQ(a->access_path[1].index_expr, "j");
}

// ── 구조체 metadata ───────────────────────────────────────────

TEST(ApLoaderV2, struct_field_offset_parsed)
{
  EXPECT_EQ(loadStruct().structs.at("Outer").fields[1].offset, 8);
}

TEST(ApLoaderV2, struct_array_field_shape_parsed)
{
  EXPECT_EQ(loadStruct().structs.at("Outer").fields[1].shape,
            (std::vector<int64_t>{4}));
}

TEST(ApLoaderV2, struct_field_element_struct_type_resolved)
{
  EXPECT_EQ(loadStruct().structs.at("Outer").fields[1].struct_type, "S");
}

TEST(ApLoaderV2, pointer_object_struct_type_resolved)
{
  EXPECT_EQ(loadStruct().objects.at("function:g::param:o").struct_type, "Outer");
}

// ── 구조체 access_path ────────────────────────────────────────

TEST(ApLoaderV2, struct_access_path_has_field_index_field_sequence)
{
  ApProgram p = loadStruct();
  const ArrayNode* a = first_array(p.functions.at("g"));
  ASSERT_NE(a, nullptr);
  ASSERT_EQ(a->access_path.size(), 3u);
  EXPECT_EQ(a->access_path[0].kind, RawAccessStep::Kind::Field);
  EXPECT_EQ(a->access_path[1].kind, RawAccessStep::Kind::Index);
  EXPECT_EQ(a->access_path[2].kind, RawAccessStep::Kind::Field);
}

TEST(ApLoaderV2, struct_access_path_field_index_value)
{
  ApProgram p = loadStruct();
  const ArrayNode* a = first_array(p.functions.at("g"));
  ASSERT_NE(a, nullptr);
  EXPECT_EQ(a->access_path[0].field_index, 1);  // items
  EXPECT_EQ(a->access_path[2].field_index, 0);  // x
}
