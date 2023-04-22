#include <algorithm>
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
  std::size_t line_number;
  MWLinker::Map::Error error;
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

  while (temp.back() == '\0')
    temp.pop_back();
  linker_map.Print(sstream);
  std::string temp2 = std::move(sstream).str();

  const bool matches = (temp == temp2);
  const MWLinker::Version min_version = linker_map.GetMinVersion();
  std::println(std::cout, "line: {:d}   err: {:d}   matches: {:s}   min_version: {:d}", line_number,
               static_cast<int>(error), matches, static_cast<int>(min_version));
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
