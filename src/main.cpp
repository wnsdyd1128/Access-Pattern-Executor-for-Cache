#include <fstream>
#include <iostream>
#include <filesystem>
#include <string>

#include "analysis/Diagnostics.hpp"
#include "ap/ApLoader.hpp"
#include "cache/YamlConfigParser.hpp"
#include "pipeline/Pipeline.hpp"
#include "report/CsvWriter.hpp"
#include "report/JsonWriter.hpp"
#include "report/MarkdownWriter.hpp"

namespace
{

void print_usage()
{
  std::cerr << "Usage: apex-cache run <input.ap.json> --cache <cache.yaml> "
               "[--output <dir>]\n";
}

/// --flag value 형태 인자에서 flag의 값을 찾는다. 없으면 빈 문자열.
std::string find_opt(int argc, char * argv[], const std::string & flag)
{
  for (int i = 0; i + 1 < argc; ++i)
    if (flag == argv[i]) return argv[i + 1];
  return {};
}

std::string output_basename(const std::string & input)
{
  std::string base = std::filesystem::path(input).stem().string();
  const std::string suffix = "_g_ape";
  if (base.size() >= suffix.size() &&
      base.compare(base.size() - suffix.size(), suffix.size(), suffix) == 0)
  {
    base.erase(base.size() - suffix.size());
    base += "_ape";
  }
  return base;
}

int run_command(int argc, char * argv[])
{
  // argv[2] = input.ap.json
  if (argc < 3)
  {
    print_usage();
    return 2;
  }
  const std::string input = argv[2];
  const std::string cache_yaml = find_opt(argc, argv, "--cache");
  std::string output = find_opt(argc, argv, "--output");
  if (output.empty()) output = "results";

  if (cache_yaml.empty())
  {
    std::cerr << "error: --cache is required\n";
    print_usage();
    return 2;
  }

  try
  {
    std::filesystem::create_directories(output);

    apex::ApProgram program = apex::ApLoader{}.load_program_file(input);

    HierarchyConfig config = YamlConfigParser::parse(cache_yaml);
    apex::Pipeline pipeline(config);
    apex::PipelineResult result = pipeline.run(program);

    const std::filesystem::path output_dir(output);
    const std::string base = output_basename(input);

    std::ofstream summary_csv(output_dir / (base + ".csv"));
    if (!summary_csv) throw std::runtime_error("cannot write summary.csv");
    apex::CsvWriter::write_summary(summary_csv, result.stats,
                                   result.cache_stats);

    std::ofstream object_csv(output_dir / (base + "_objects.csv"));
    if (!object_csv)
      throw std::runtime_error("cannot write object_breakdown.csv");
    apex::CsvWriter::write_object_breakdown(object_csv, result.cache_stats);

    std::ofstream summary_json(output_dir / (base + ".json"));
    if (!summary_json) throw std::runtime_error("cannot write summary.json");
    apex::JsonWriter::write_summary(summary_json, result.stats,
                                    result.cache_stats);

    auto hints = apex::Diagnostics{}.generate(result.stats);
    std::ofstream diag_md(output_dir / (base + "_diagnostics.md"));
    if (!diag_md) throw std::runtime_error("cannot write diagnostics.md");
    apex::MarkdownWriter::write_diagnostics(diag_md, hints);

    std::cerr << "wrote results to " << output << "/\n";
    return 0;
  }
  catch (const std::exception & ex)
  {
    std::cerr << "error: " << ex.what() << "\n";
    return 1;
  }
}

}  // namespace

int main(int argc, char * argv[])
{
  if (argc < 2)
  {
    print_usage();
    return 2;
  }
  const std::string cmd = argv[1];
  if (cmd == "run") return run_command(argc, argv);

  std::cerr << "unknown command: " << cmd << "\n";
  print_usage();
  return 2;
}
