#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "ap/AccessLayout.hpp"
#include "ap/ApNode.hpp"

namespace apex
{

/**
 * @brief LAT v2 한 모듈의 파싱 결과.
 *
 * 함수 본문(이름→노드 목록), 분석 root(yard.analyze) 이름,
 * 그리고 주소 해석에 쓰는 object/struct 배치 metadata를 담는다.
 */
struct ApProgram
{
  std::map<std::string, std::vector<std::unique_ptr<ApNode>>> functions;
  std::vector<std::string> roots;  ///< yard.analyze 함수 이름
  std::map<std::string, ObjectLayout> objects;
  std::map<std::string, StructLayout> structs;
};

}  // namespace apex
