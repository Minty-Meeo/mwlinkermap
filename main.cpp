#include <algorithm>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "MWLinkerMap.h"

void tempfunc(const char* name, int choice)
{
  std::cout << name << std::endl;

  std::ifstream infile(name);
  if (!infile.is_open())
  {
    std::cout << "Could not open!" << std::endl;
    return;
  }

  MWLinkerMap linker_map;
  std::size_t line_number;
  MWLinkerMap::Error error;
  switch (choice)
  {
  case 0:
    error = linker_map.Read(infile, line_number);
    break;
  case 1:
    error = linker_map.ReadTLOZTP(infile, line_number);
    break;
  case 2:
    error = linker_map.ReadSMGalaxy(infile, line_number);
    break;
  default:
    std::cout << "bad choice" << std::endl;
    return;
  }

  std::cout << "line: " << line_number + 1 << "   err: " << static_cast<int>(error) << std::endl;
}

int main(const int argc, const char** argv)
{
  if (argc < 2)
  {
    std::cout << "Provide the name" << std::endl;
    return 1;
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

  return 0;
}