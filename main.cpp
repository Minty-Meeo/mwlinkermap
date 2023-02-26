#include <algorithm>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "MWLinkerMap.h"
#include "MWLinkerMap2.h"

void tempfunc(const char* name)
{
  std::cout << name << std::endl;

  std::ifstream infile(name);
  if (!infile.is_open())
  {
    std::cout << "Could not open!" << std::endl;
    return;
  }

  MWLinkerMap2 linker_map;
  std::size_t line_number;
  MWLinkerMap2::Error err;

  err = linker_map.Read(infile, line_number);

  std::cout << "line: " << line_number + 1 << "   err: " << static_cast<int>(err) << std::endl;
}

int main(const int argc, const char** argv)
{
  if (argc < 2)
  {
    std::cout << "Provide the name" << std::endl;
    return 1;
  }

  for (int i = 1; i < argc; ++i)
    tempfunc(argv[i]);

  return 0;
}