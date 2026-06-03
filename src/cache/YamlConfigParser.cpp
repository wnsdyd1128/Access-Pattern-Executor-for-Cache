#include "cache/YamlConfigParser.hpp"

#include <cctype>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <yaml-cpp/yaml.h>

static WritePolicy parse_write_policy(const std::string & s)
{
  if (s == "write-back") return WritePolicy::WriteBack;
  if (s == "write-through") return WritePolicy::WriteThrough;
  throw std::runtime_error("unknown write_policy: " + s);
}

static Replacement parse_replacement(const std::string & s)
{
  if (s == "LRU") return Replacement::LRU;
  throw std::runtime_error("unknown replacement: " + s);
}

static std::string trim(const std::string & s)
{
  std::size_t first = 0;
  while (first < s.size() &&
         std::isspace(static_cast<unsigned char>(s[first])))
    first += 1;

  std::size_t last = s.size();
  while (last > first &&
         std::isspace(static_cast<unsigned char>(s[last - 1])))
    last -= 1;

  return s.substr(first, last - first);
}

static uint64_t multiplier_for_unit(const std::string & unit)
{
  if (unit == "B") return 1;
  if (unit == "KB") return 1000ull;
  if (unit == "MB") return 1000ull * 1000ull;
  if (unit == "GB") return 1000ull * 1000ull * 1000ull;
  if (unit == "KiB") return 1024ull;
  if (unit == "MiB") return 1024ull * 1024ull;
  if (unit == "GiB") return 1024ull * 1024ull * 1024ull;
  throw std::runtime_error("unknown byte unit: " + unit);
}

static uint64_t parse_byte_quantity(const YAML::Node & node,
                                    const std::string & field)
{
  try
  {
    uint64_t value = node.as<uint64_t>();
    if (value == 0) throw std::runtime_error(field + " must be positive");
    return value;
  }
  catch (const YAML::BadConversion &)
  {
  }

  const std::string text = trim(node.as<std::string>());
  std::size_t pos = 0;
  while (pos < text.size() &&
         std::isdigit(static_cast<unsigned char>(text[pos])))
    pos += 1;

  if (pos == 0) throw std::runtime_error(field + ": missing byte count");

  const uint64_t value = std::stoull(text.substr(0, pos));
  if (value == 0) throw std::runtime_error(field + " must be positive");

  const std::string unit = trim(text.substr(pos));
  if (unit.empty()) return value;

  const uint64_t multiplier = multiplier_for_unit(unit);
  if (value > std::numeric_limits<uint64_t>::max() / multiplier)
    throw std::runtime_error(field + " overflows uint64_t");
  return value * multiplier;
}

static int parse_line_size(const YAML::Node & node)
{
  const uint64_t value = parse_byte_quantity(node, "line_size");
  if (value > static_cast<uint64_t>(std::numeric_limits<int>::max()))
    throw std::runtime_error("line_size overflows int");
  return static_cast<int>(value);
}

static CacheConfig parse_entry(const YAML::Node & n)
{
  if (!n["size_bytes"])
    throw std::runtime_error("missing required field: size_bytes");

  CacheConfig c;
  c.name = n["name"] ? n["name"].as<std::string>() : "";
  c.role = n["role"] ? n["role"].as<std::string>() : "";
  c.private_to = n["private_to"] ? n["private_to"].as<int>() : -1;
  c.size_bytes = parse_byte_quantity(n["size_bytes"], "size_bytes");
  c.line_size = n["line_size"] ? parse_line_size(n["line_size"]) : 64;
  c.associativity = n["associativity"] ? n["associativity"].as<int>() : 8;
  c.replacement = n["replacement"]
                    ? parse_replacement(n["replacement"].as<std::string>())
                    : Replacement::LRU;
  c.write_policy = n["write_policy"]
                     ? parse_write_policy(n["write_policy"].as<std::string>())
                     : WritePolicy::WriteBack;
  c.write_allocate =
    n["write_allocate"] ? n["write_allocate"].as<bool>() : true;
  c.delay_cycles = n["delay_cycles"] ? n["delay_cycles"].as<int>() : 0;
  c.next = n["next"] ? n["next"].as<std::string>() : "";
  return c;
}

// 'like' 상속: base config를 파싱한 뒤 node에 명시된 필드만 덮어씀
static CacheConfig apply_like(const YAML::Node & base, const YAML::Node & node)
{
  CacheConfig c = parse_entry(base);
  if (node["name"]) c.name = node["name"].as<std::string>();
  if (node["role"]) c.role = node["role"].as<std::string>();
  if (node["private_to"]) c.private_to = node["private_to"].as<int>();
  if (node["size_bytes"])
    c.size_bytes = parse_byte_quantity(node["size_bytes"], "size_bytes");
  if (node["line_size"]) c.line_size = parse_line_size(node["line_size"]);
  if (node["associativity"]) c.associativity = node["associativity"].as<int>();
  if (node["replacement"])
    c.replacement = parse_replacement(node["replacement"].as<std::string>());
  if (node["write_policy"])
    c.write_policy = parse_write_policy(node["write_policy"].as<std::string>());
  if (node["write_allocate"])
    c.write_allocate = node["write_allocate"].as<bool>();
  if (node["delay_cycles"]) c.delay_cycles = node["delay_cycles"].as<int>();
  if (node["next"]) c.next = node["next"].as<std::string>();
  return c;
}

HierarchyConfig YamlConfigParser::parse(const std::string & path)
{
  YAML::Node root = YAML::LoadFile(path);
  HierarchyConfig cfg;

  if (!root["caches"]) return cfg;
  const YAML::Node & caches = root["caches"];

  // Build name → sequence index map (avoids storing YAML::Node by value)
  std::unordered_map<std::string, std::size_t> index_map;
  for (std::size_t i = 0; i < caches.size(); ++i)
  {
    if (caches[i]["name"]) index_map[caches[i]["name"].as<std::string>()] = i;
  }

  for (std::size_t i = 0; i < caches.size(); ++i)
  {
    const YAML::Node & node = caches[i];
    if (node["like"])
    {
      const std::string base_name = node["like"].as<std::string>();
      auto it = index_map.find(base_name);
      if (it == index_map.end())
        throw std::runtime_error("like: unknown cache '" + base_name + "'");
      cfg.caches.push_back(apply_like(caches[it->second], node));
    }
    else
    {
      cfg.caches.push_back(parse_entry(node));
    }
  }

  if (root["memory"] && root["memory"]["delay_cycles"])
    cfg.memory.delay_cycles = root["memory"]["delay_cycles"].as<int>();

  return cfg;
}
