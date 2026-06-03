#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "ap/AccessEvent.hpp"
#include "ap/ApNode.hpp"
#include "ap/ApProgram.hpp"

namespace apex
{

/**
 * @brief LAT v2 ApProgram을 선형 AccessEvent 스트림으로 변환한다.
 *
 * roots(yard.analyze) 함수 본문을 순회·루프 언롤하고, Array 노드의 access_path를
 * IndexExpr로 평가해 resolve_offset으로 byte_offset(객체 base 기준)을 채운다.
 * Call은 callee 본문을 inline 전개하며, callee param을 호출자 인자로 바인딩한다
 * (object id → 실인자 객체, index 변수 → 인자 값).
 *
 * @note SMP core_id 기본값은 0이다.
 */
class EventBuilder
{
public:
  /**
   * @brief ApProgram을 AccessEvent 스트림으로 변환한다.
   * @param program 파싱된 ApProgram (functions/params/roots/objects/structs)
   * @return AccessEvent 목록 (object_name=object id, byte_offset 채움)
   */
  std::vector<AccessEvent> build_program(const ApProgram & program);

private:
  /**
   * @param bindings  callee param 변수 이름 → 바인딩된 정수 값 (index 평가에 합류)
   * @param obj_subst callee param object id → 호출자 인자 object id (접근 객체 치환)
   */
  void visit_v2(const ApNode & node, const ApProgram & program,
                std::vector<AccessEvent> & out,
                std::vector<LoopFrame> & loop_stack,
                const std::string & region_path, uint64_t & seq,
                const std::unordered_map<std::string, int64_t> & bindings,
                const std::unordered_map<std::string, std::string> & obj_subst);
};

}  // namespace apex
