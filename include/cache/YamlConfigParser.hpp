#pragma once
#include <string>
#include "cache/CacheConfig.hpp"

/**
 * @brief cache.yaml을 파싱해 HierarchyConfig를 반환한다.
 *
 * @note like: 필드로 다른 캐시 항목의 설정을 상속할 수 있다.
 * @throws std::runtime_error size_bytes 등 필수 필드 누락 시
 */
class YamlConfigParser {
public:
    static HierarchyConfig parse(const std::string& path);
};
