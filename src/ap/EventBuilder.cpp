#include "ap/EventBuilder.hpp"

#include <unordered_map>
#include <variant>

#include "ap/AddressResolver.hpp"
#include "ap/IndexExpr.hpp"

namespace apex
{

std::vector<AccessEvent> EventBuilder::build_program(const ApProgram & program)
{
  std::vector<AccessEvent> out;
  std::vector<LoopFrame> loop_stack;
  uint64_t seq = 0;
  const std::unordered_map<std::string, int64_t> no_bindings;
  const std::unordered_map<std::string, std::string> no_subst;
  for (const std::string & root : program.roots)
  {
    auto it = program.functions.find(root);
    if (it == program.functions.end()) continue;
    for (const auto & n : it->second)
      visit_v2(*n, program, out, loop_stack, root, seq, no_bindings, no_subst);
  }
  return out;
}

void EventBuilder::visit_v2(
  const ApNode & node, const ApProgram & program, std::vector<AccessEvent> & out,
  std::vector<LoopFrame> & loop_stack, const std::string & region_path,
  uint64_t & seq, const std::unordered_map<std::string, int64_t> & bindings,
  const std::unordered_map<std::string, std::string> & obj_subst)
{
  // callee param 치환을 반영해 접근 객체 id를 해석한다.
  auto resolve_object = [&](const std::string & id) {
    auto it = obj_subst.find(id);
    return it != obj_subst.end() ? it->second : id;
  };

  switch (node.kind())
  {
    case ApNodeKind::Scalar: {
      const auto & s = static_cast<const ScalarNode &>(node);
      AccessEvent e;
      e.sequence_id = seq++;
      e.region_path = region_path;
      e.op = s.op;
      e.object_name = resolve_object(s.object);
      e.loop_stack = loop_stack;
      out.push_back(std::move(e));
      break;
    }

    case ApNodeKind::Array: {
      const auto & a = static_cast<const ArrayNode &>(node);

      std::unordered_map<std::string, int64_t> vars;
      for (const auto & f : loop_stack) vars[f.var] = f.iter;
      for (const auto & b : bindings) vars[b.first] = b.second;  // param 바인딩

      std::vector<AccessStep> path;
      path.reserve(a.access_path.size());
      for (const auto & rs : a.access_path)
      {
        if (std::holds_alternative<RawIndexStep>(rs))
          path.push_back(
            IndexStep{eval_index(std::get<RawIndexStep>(rs).expr, vars)});
        else
          path.push_back(std::get<FieldStep>(rs));
      }

      const std::string object = resolve_object(a.object);
      ObjectLayout obj;
      auto it = program.objects.find(object);
      if (it != program.objects.end()) obj = it->second;

      AccessEvent e;
      e.sequence_id = seq++;
      e.region_path = region_path;
      e.op = a.op;
      e.object_name = object;
      e.byte_offset = resolve_offset(obj, path, program.structs);
      e.loop_stack = loop_stack;
      out.push_back(std::move(e));
      break;
    }

    case ApNodeKind::Loop: {
      const auto & l = static_cast<const LoopNode &>(node);
      for (int64_t iter = l.start; iter < l.bound; ++iter)
      {
        loop_stack.push_back({l.var, iter});
        for (const auto & child : l.body)
          visit_v2(*child, program, out, loop_stack, region_path, seq, bindings,
                   obj_subst);
        loop_stack.pop_back();
      }
      break;
    }

    case ApNodeKind::Call: {
      const auto & c = static_cast<const CallNode &>(node);
      auto fit = program.functions.find(c.callee);
      if (fit == program.functions.end()) break;

      // 호출자 스코프 변수(루프 + 현재 바인딩)로 인자 값을 평가한다.
      std::unordered_map<std::string, int64_t> caller_vars;
      for (const auto & f : loop_stack) caller_vars[f.var] = f.iter;
      for (const auto & b : bindings) caller_vars[b.first] = b.second;

      // callee param ↔ 호출자 인자로 object 치환·index 바인딩을 구성한다.
      // arg_objects[k]가 등록된 객체면 치환, 아니면(루프변수·리터럴) 정수로 평가.
      std::unordered_map<std::string, int64_t> callee_bindings;
      std::unordered_map<std::string, std::string> callee_subst;
      auto pit = program.params.find(c.callee);
      if (pit != program.params.end())
      {
        const auto & params = pit->second;
        for (size_t k = 0; k < params.size() && k < c.arg_objects.size(); ++k)
        {
          const std::string resolved = resolve_object(c.arg_objects[k]);
          if (program.objects.count(resolved))
            callee_subst["function:" + c.callee + "::param:" + params[k]] =
              resolved;
          else
            callee_bindings[params[k]] = eval_index(resolved, caller_vars);
        }
      }

      const std::string path =
        region_path.empty() ? c.callee : region_path + "/" + c.callee;
      for (const auto & child : fit->second)
        visit_v2(*child, program, out, loop_stack, path, seq, callee_bindings,
                 callee_subst);
      break;
    }
  }
}

}  // namespace apex
