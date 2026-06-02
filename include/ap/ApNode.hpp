#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace apex
{

/**
 * @brief AP JSON의 노드 종류.
 */
enum class ApNodeKind
{
  Scalar,
  Array,
  Call,
  Loop
};

struct ApNode
{
  virtual ~ApNode() = default;
  virtual ApNodeKind kind() const = 0;
};

/**
 * @brief access_path의 한 스텝(미평가 형태) — LAT v2.
 *
 * index_expr는 affine 식 문자열("i","i-1","5")이며 EventBuilder가
 * 루프 문맥으로 평가한다. field는 구조체 필드 인덱스를 보관한다.
 */
struct RawAccessStep
{
  enum class Kind
  {
    Index,
    Field
  };
  Kind kind;
  std::string index_expr;   ///< Kind::Index일 때 affine 식 문자열
  int64_t field_index = 0;  ///< Kind::Field일 때 구조체 필드 인덱스
};

/**
 * @brief 스칼라 변수 접근 노드.
 *
 * @pre op은 "load" 또는 "store"
 */
struct ScalarNode : ApNode
{
  std::string name;
  std::string op;

  std::string object;  ///< LAT v2: metadata.objects의 object id

  ApNodeKind kind() const override { return ApNodeKind::Scalar; }
};

/**
 * @brief 배열 접근 노드.
 *
 * shape가 비어 있으면 ApLoader가 shapes.yaml에서 보완한다.
 *
 * @pre op은 "load" 또는 "store"
 * @pre indices의 각 원소는 루프 유도 변수 이름
 */
struct ArrayNode : ApNode
{
  std::string name;
  std::vector<std::string> indices;
  std::vector<int64_t> shape;
  int64_t elem_size = 4;
  std::string op;

  std::string object;                      ///< LAT v2: metadata.objects의 object id
  std::vector<RawAccessStep> access_path;  ///< LAT v2

  ApNodeKind kind() const override { return ApNodeKind::Array; }
};

/**
 * @brief 함수 호출 노드.
 */
struct CallNode : ApNode
{
  std::string callee;
  std::vector<std::string> args;
  std::vector<std::string> arg_objects;  ///< LAT v2: 인자별 object id(또는 리터럴)

  ApNodeKind kind() const override { return ApNodeKind::Call; }
};

/**
 * @brief 루프 노드. body에 자식 노드를 소유한다.
 *
 * trip_count = bound - start
 */
struct LoopNode : ApNode
{
  std::string var;
  int64_t start = 0;
  int64_t bound = 0;
  int64_t depth = 1;
  std::vector<std::unique_ptr<ApNode>> body;

  ApNodeKind kind() const override { return ApNodeKind::Loop; }

  int64_t trip_count() const { return bound - start; }
};

}  // namespace apex
