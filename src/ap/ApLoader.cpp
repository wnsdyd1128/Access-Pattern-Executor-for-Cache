#include "ap/ApLoader.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <yaml-cpp/yaml.h>

namespace apex
{

using json = nlohmann::json;

// ── public ────────────────────────────────────────────────────

ApLoader & ApLoader::with_shapes_yaml(const std::string & path)
{
  shapes_yaml_path_ = path;
  return *this;
}

std::vector<std::unique_ptr<ApNode>>
ApLoader::load_json_string(const std::string & src) const
{
  const ShapeMap shapes = load_shapes_yaml();
  const json j = json::parse(src);
  std::vector<std::unique_ptr<ApNode>> result;
  for (const auto & item : j) result.push_back(parse_node(&item, shapes));
  return result;
}

std::vector<std::unique_ptr<ApNode>>
ApLoader::load_json_file(const std::string & path) const
{
  std::ifstream f(path);
  if (!f) throw std::runtime_error("ApLoader: cannot open " + path);
  std::ostringstream ss;
  ss << f.rdbuf();
  return load_json_string(ss.str());
}

// ── private ───────────────────────────────────────────────────

ApLoader::ShapeMap ApLoader::load_shapes_yaml() const
{
  ShapeMap map;
  if (shapes_yaml_path_.empty()) return map;

  const YAML::Node root = YAML::LoadFile(shapes_yaml_path_);
  const YAML::Node shapes = root["shapes"];
  if (!shapes) return map;

  for (const auto & kv : shapes)
  {
    const std::string name = kv.first.as<std::string>();
    std::vector<int64_t> dims;
    for (const auto & d : kv.second) dims.push_back(d.as<int64_t>());
    map[name] = std::move(dims);
  }
  return map;
}

std::unique_ptr<ApNode> ApLoader::parse_node(const void * raw,
                                             const ShapeMap & shapes) const
{
  const json & j = *static_cast<const json *>(raw);
  const std::string type = j.at("type").get<std::string>();

  if (type == "Scalar")
  {
    auto n = std::make_unique<ScalarNode>();
    n->name = j.at("name").get<std::string>();
    n->op = j.value("op", "");
    return n;
  }

  if (type == "Array")
  {
    auto n = std::make_unique<ArrayNode>();
    n->name = j.at("name").get<std::string>();
    n->op = j.value("op", "");
    n->elem_size = j.value("elem_size", int64_t{4});
    for (const auto & idx : j.at("indices"))
      n->indices.push_back(idx.get<std::string>());

    if (j.contains("shape") && !j["shape"].is_null())
    {
      for (const auto & d : j["shape"]) n->shape.push_back(d.get<int64_t>());
    }
    else
    {
      auto it = shapes.find(n->name);
      if (it == shapes.end())
        throw std::runtime_error("ApLoader: no shape for array '" + n->name +
                                 "' — add it to shapes.yaml");
      n->shape = it->second;
    }
    return n;
  }

  if (type == "Call")
  {
    auto n = std::make_unique<CallNode>();
    n->callee = j.at("callee").get<std::string>();
    for (const auto & a : j.at("args")) n->args.push_back(a.get<std::string>());
    return n;
  }

  if (type == "Loop")
  {
    auto n = std::make_unique<LoopNode>();
    n->var = j.at("var").get<std::string>();
    n->start = j.at("start").get<int64_t>();
    n->bound = j.at("bound").get<int64_t>();
    n->depth = j.value("depth", int64_t{1});
    for (const auto & child : j.at("body"))
      n->body.push_back(parse_node(&child, shapes));
    return n;
  }

  throw std::runtime_error("ApLoader: unknown node type '" + type + "'");
}

// ── LAT v2 파싱 ───────────────────────────────────────────────

namespace
{

std::vector<int64_t> parse_dims(const json & arr)
{
  std::vector<int64_t> v;
  for (const auto & d : arr) v.push_back(d.get<int64_t>());
  return v;
}

// elem_type이 구조체 이름이면 struct_type으로 채운다(원소가 구조체임을 표시).
StructLayout parse_struct_layout(const json & st,
                                 const std::set<std::string> & struct_names)
{
  StructLayout out;
  out.size = st.value("size", int64_t{0});
  out.align = st.value("align", int64_t{0});
  for (const auto & f : st.at("fields"))
  {
    FieldLayout fl;
    fl.offset = f.value("offset", int64_t{0});
    fl.elem_size = f.value("elem_size", int64_t{0});
    if (f.contains("shape")) fl.shape = parse_dims(f["shape"]);
    const std::string et = f.value("elem_type", std::string{});
    if (struct_names.count(et)) fl.struct_type = et;
    out.fields.push_back(std::move(fl));
  }
  return out;
}

ObjectLayout parse_object_layout(const json & o,
                                 const std::set<std::string> & struct_names)
{
  ObjectLayout out;
  out.elem_size = o.value("elem_size", int64_t{0});
  if (o.contains("shape")) out.shape = parse_dims(o["shape"]);
  const std::string et = o.value("elem_type", std::string{});
  if (struct_names.count(et)) out.struct_type = et;
  return out;
}

std::unique_ptr<ApNode> parse_node_v2(const json & j)
{
  const std::string type = j.at("type").get<std::string>();

  if (type == "Loop")
  {
    auto n = std::make_unique<LoopNode>();
    n->var = j.at("var").get<std::string>();
    n->start = j.at("start").get<int64_t>();
    n->bound = j.at("bound").get<int64_t>();
    n->depth = j.value("depth", int64_t{1});
    for (const auto & c : j.at("body")) n->body.push_back(parse_node_v2(c));
    return n;
  }

  if (type == "Array")
  {
    auto n = std::make_unique<ArrayNode>();
    n->object = j.value("object", std::string{});
    n->op = j.value("op", std::string{});
    if (j.contains("access_path"))
      for (const auto & s : j["access_path"])
      {
        RawAccessStep step;
        const std::string kind = s.at("kind").get<std::string>();
        if (kind == "field")
        {
          step.kind = RawAccessStep::Kind::Field;
          step.field_index = s.value("index", int64_t{0});
        }
        else
        {
          step.kind = RawAccessStep::Kind::Index;
          step.index_expr = s.at("value").get<std::string>();
        }
        n->access_path.push_back(std::move(step));
      }
    return n;
  }

  if (type == "Scalar")
  {
    auto n = std::make_unique<ScalarNode>();
    n->object =
      j.contains("object") ? j["object"].get<std::string>() : j.value("name", std::string{});
    n->op = j.value("op", std::string{});
    return n;
  }

  if (type == "Call")
  {
    auto n = std::make_unique<CallNode>();
    n->callee = j.at("callee").get<std::string>();
    for (const auto & a : j.value("args", json::array()))
      n->args.push_back(a.get<std::string>());
    for (const auto & a : j.value("arg_objects", json::array()))
      n->arg_objects.push_back(a.get<std::string>());
    return n;
  }

  throw std::runtime_error("ApLoader: unknown v2 node type '" + type + "'");
}

}  // namespace

ApProgram ApLoader::load_program_string(const std::string & src) const
{
  const json root = json::parse(src);
  ApProgram prog;

  const json & meta = root.at("metadata");

  std::set<std::string> struct_names;
  if (meta.contains("structs"))
    for (auto it = meta["structs"].begin(); it != meta["structs"].end(); ++it)
      struct_names.insert(it.key());

  if (meta.contains("structs"))
    for (auto it = meta["structs"].begin(); it != meta["structs"].end(); ++it)
      prog.structs[it.key()] = parse_struct_layout(it.value(), struct_names);

  if (meta.contains("objects"))
    for (auto it = meta["objects"].begin(); it != meta["objects"].end(); ++it)
      prog.objects[it.key()] = parse_object_layout(it.value(), struct_names);

  for (const auto & fn : root.at("functions"))
  {
    const std::string name = fn.at("function").get<std::string>();
    std::vector<std::unique_ptr<ApNode>> body;
    for (const auto & node : fn.at("body")) body.push_back(parse_node_v2(node));
    prog.functions[name] = std::move(body);

    if (fn.contains("annotations"))
      for (const auto & a : fn["annotations"])
        if (a.get<std::string>() == "yard.analyze")
        {
          prog.roots.push_back(name);
          break;
        }
  }
  return prog;
}

ApProgram ApLoader::load_program_file(const std::string & path) const
{
  std::ifstream f(path);
  if (!f) throw std::runtime_error("ApLoader: cannot open " + path);
  std::ostringstream ss;
  ss << f.rdbuf();
  return load_program_string(ss.str());
}

}  // namespace apex
