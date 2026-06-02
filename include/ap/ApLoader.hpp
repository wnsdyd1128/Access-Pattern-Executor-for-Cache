#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "ap/ApNode.hpp"
#include "ap/ApProgram.hpp"

namespace apex
{

/**
 * @brief AP JSON 문자열 또는 파일을 ApNode 트리 목록으로 파싱한다.
 *
 * shape가 없는 ArrayNode는 shapes_yaml에 등록된 값으로 보완한다.
 * shapes_yaml에도 없으면 예외를 던진다.
 */
class ApLoader
{
public:
  /**
   * @brief shapes.yaml 경로를 설정한다.
   * @param path shapes.yaml 파일 경로
   * @return *this (method chaining)
   */
  ApLoader & with_shapes_yaml(const std::string & path);

  /**
   * @brief AP JSON 문자열을 파싱해 ApNode 목록을 반환한다.
   * @param json 최상위가 배열인 JSON 문자열
   * @throws std::runtime_error shape 정보가 없는 배열이 존재할 때
   */
  std::vector<std::unique_ptr<ApNode>>
  load_json_string(const std::string & json) const;

  /**
   * @brief AP JSON 파일을 파싱해 ApNode 목록을 반환한다.
   * @throws std::runtime_error 파일을 열 수 없거나 shape 정보가 없을 때
   */
  std::vector<std::unique_ptr<ApNode>>
  load_json_file(const std::string & path) const;

  /**
   * @brief LAT v2 JSON 문자열을 파싱해 ApProgram을 반환한다.
   * @param json {schema_version,metadata,functions} 객체 JSON
   * @throws std::runtime_error JSON 형식이 LAT v2가 아닐 때
   */
  ApProgram load_program_string(const std::string & json) const;

  /** @brief LAT v2 JSON 파일을 파싱해 ApProgram을 반환한다. */
  ApProgram load_program_file(const std::string & path) const;

private:
  using ShapeMap = std::unordered_map<std::string, std::vector<int64_t>>;

  std::string shapes_yaml_path_;

  ShapeMap load_shapes_yaml() const;

  std::vector<std::unique_ptr<ApNode>>
  parse_toplevel(const std::string & json) const;
  std::unique_ptr<ApNode> parse_node(const void * j,
                                     const ShapeMap & shapes) const;
};

}  // namespace apex
