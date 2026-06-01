#include "pipeline/Pipeline.hpp"

#include <map>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

#include "analysis/MissClassifier.hpp"
#include "ap/AccessEvent.hpp"
#include "ap/EventBuilder.hpp"
#include "cache/CacheHierarchy.hpp"
#include "memory/AddressMapper.hpp"
#include "memory/MemoryLayout.hpp"

namespace apex
{

namespace
{

/// 객체 카탈로그 항목: shape/elem_size는 byte_address 계산에, total_bytes는
/// 배치에 쓴다.
struct ObjInfo
{
  std::vector<int64_t> shape;
  int64_t elem_size = 4;
  bool is_scalar = false;
};

/// ApNode 트리를 등장 순서대로 walk하며 array/scalar 객체를 수집한다.
void collect_objects(const ApNode & node,
                     std::unordered_map<std::string, ObjInfo> & seen,
                     std::vector<std::string> & order)
{
  switch (node.kind())
  {
    case ApNodeKind::Scalar: {
      const auto & s = static_cast<const ScalarNode &>(node);
      if (seen.emplace(s.name, ObjInfo{{}, 4, true}).second)
        order.push_back(s.name);
      break;
    }
    case ApNodeKind::Array: {
      const auto & a = static_cast<const ArrayNode &>(node);
      if (seen.emplace(a.name, ObjInfo{a.shape, a.elem_size, false}).second)
        order.push_back(a.name);
      break;
    }
    case ApNodeKind::Loop: {
      const auto & l = static_cast<const LoopNode &>(node);
      for (const auto & child : l.body) collect_objects(*child, seen, order);
      break;
    }
    case ApNodeKind::Call:
      // MVP: 함수 간 call 전개 미지원 → callee 객체는 카탈로그에 포함하지 않음
      break;
  }
}

uint64_t total_bytes(const ObjInfo & info)
{
  if (info.is_scalar) return static_cast<uint64_t>(info.elem_size);
  uint64_t n = 1;
  for (int64_t d : info.shape) n *= static_cast<uint64_t>(d);
  return n * static_cast<uint64_t>(info.elem_size);
}

}  // namespace

Pipeline::Pipeline(HierarchyConfig config) : config_(std::move(config)) {}

PipelineResult Pipeline::run(std::vector<std::unique_ptr<ApNode>> nodes)
{
  // L1 line_size: 모든 L1이 동일하다고 가정, 첫 L1 값을 사용.
  int line_size = 64;
  for (const auto & c : config_.caches)
    if (c.role == "L1")
    {
      line_size = c.line_size;
      break;
    }

  // 1. 객체 카탈로그 수집
  std::unordered_map<std::string, ObjInfo> catalog;
  std::vector<std::string> order;
  for (const auto & n : nodes) collect_objects(*n, catalog, order);

  // 2. 메모리 배치 (line_size 정렬 = miss 상한 모델)
  MemoryLayout layout(static_cast<uint64_t>(line_size));
  for (const auto & name : order)
    layout.add_object(name, total_bytes(catalog[name]));

  // 3. 이벤트 스트림 생성 (nodes 소비)
  std::vector<AccessEvent> events = EventBuilder{}.build(std::move(nodes));

  // 4. core별 FA 쉐도우 캐시 (cold/capacity/conflict 분류용)
  std::map<int, MissClassifier> classifiers;
  auto classifier_for = [&](int core) -> MissClassifier & {
    auto it = classifiers.find(core);
    if (it != classifiers.end()) return it->second;
    int lines = 0;
    for (const auto & c : config_.caches)
    {
      if (c.role == "L1" && c.private_to == core)
      {
        lines =
          static_cast<int>(c.size_bytes / static_cast<uint64_t>(c.line_size));
        break;
      }
    }

    return classifiers.emplace(core, MissClassifier{lines}).first->second;
  };

  CacheHierarchy hierarchy(config_);
  PipelineResult result;

  // 5. 이벤트별 시뮬레이션 및 귀속
  for (const auto & e : events)
  {
    const ObjInfo & info = catalog.at(e.object_name);
    uint64_t base = layout.base_of(e.object_name);

    uint64_t byte_addr;
    if (info.is_scalar || info.shape.empty() || e.indices.empty())
      byte_addr = base;
    else
      byte_addr = AddressMapper::byte_address(base, e.indices, info.shape,
                                              static_cast<uint64_t>(e.size));

    uint64_t cache_line =
      AddressMapper::cache_line(byte_addr, static_cast<uint64_t>(line_size));

    bool is_store = (e.op == "store");
    HierarchyAccessResult res =
      hierarchy.access(e.core_id, cache_line, is_store);
    bool l1_miss = (res.miss_level >= 1);

    auto miss_type = classifier_for(e.core_id).classify(cache_line, l1_miss);

    if (!l1_miss) continue;

    result.attribution.record(e.region_path, e.object_name, e.op,
                              res.miss_level);

    if (miss_type)
    {
      switch (*miss_type)
      {
        case MissType::Cold:
          result.stats.cold += 1;
          break;
        case MissType::Capacity:
          result.stats.capacity += 1;
          break;
        case MissType::Conflict:
          result.stats.conflict += 1;
          break;
      }
    }
    if (is_store)
      result.stats.store += 1;
    else
      result.stats.load += 1;
    result.stats.by_object[e.object_name] += 1;
  }

  return result;
}

}  // namespace apex
