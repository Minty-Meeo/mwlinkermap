#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "Future/CppLibPrint.h"

#include "MWLinkerMap.h"

static void tempfunc(const char* name, int choice)
{
  std::println(std::cout, "{:s}", name);

  std::ifstream infile(name);
  if (!infile.is_open())
  {
    std::println(std::cerr, "Could not open!");
    return;
  }
  std::stringstream sstream;
  sstream << infile.rdbuf();
  std::string temp = std::move(sstream).str();

  MWLinker::Map linker_map;
  MWLinker::Map::SymbolClosure::Warn::do_warn_odr_violation = false;
  MWLinker::Map::SymbolClosure::Warn::do_warn_sym_on_flag_detected = false;
  MWLinker::Map::EPPC_PatternMatching::Warn::do_warn_folding_odr_violation = false;
  MWLinker::Map::EPPC_PatternMatching::Warn::do_warn_folding_repeat_object = false;
  MWLinker::Map::EPPC_PatternMatching::Warn::do_warn_merging_odr_violation = false;
  MWLinker::Map::SectionLayout::Warn::do_warn_comm_after_lcomm = false;
  MWLinker::Map::SectionLayout::Warn::do_warn_odr_violation = false;
  MWLinker::Map::SectionLayout::Warn::do_warn_repeat_compilation_unit = false;
  MWLinker::Map::SectionLayout::Warn::do_warn_sym_on_flag_detected = false;

  std::size_t line_number;
  MWLinker::Map::Error error;
  const auto time_start = std::chrono::high_resolution_clock::now();
  switch (choice)
  {
  case 0:
    error = linker_map.Scan(temp, line_number);
    break;
  case 1:
    error = linker_map.ScanTLOZTP(temp, line_number);
    break;
  case 2:
    error = linker_map.ScanSMGalaxy(temp, line_number);
    break;
  default:
    std::println(std::cerr, "bad choice");
    return;
  }
  const auto time_end = std::chrono::high_resolution_clock::now();

  while (temp.back() == '\0')
    temp.pop_back();
  linker_map.Print(sstream);
  std::string temp2 = std::move(sstream).str();

  const bool matches = (temp == temp2);
  const MWLinker::Version min_version = linker_map.GetMinVersion();
  std::print(std::cout,
             "line: {:d}   err: {:d}   matches: {:s}   min_version: {:d}   time: ", line_number,
             static_cast<int>(error), matches, static_cast<int>(min_version));
  std::cout << std::chrono::duration_cast<std::chrono::milliseconds>(time_end - time_start)
            << std::endl;
}

int main(const int argc, const char** argv)
{
  if (argc < 2)
  {
    std::println(std::cerr, "Provide the name");
    return EXIT_FAILURE;
  }
  if (argc < 3)
  {
    tempfunc(argv[1], 0);
  }
  else
  {
    if (!std::strcmp(argv[1], "tloztp"))
      tempfunc(argv[2], 1);
    else if (!std::strcmp(argv[1], "smgalaxy"))
      tempfunc(argv[2], 2);
    else
      tempfunc(argv[1], 0);
  }

  return EXIT_SUCCESS;
}
