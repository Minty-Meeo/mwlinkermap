#include <algorithm>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "MWLinkerMap.h"

void tempfunc(const char* name)
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
  MWLinkerMap::Error err;

  std::stringstream stream;
  stream << infile.rdbuf();
  infile.close();
#ifndef _WIN32  // MWLDEPPC generates CRLF line endings, but *nix streams lack a Windows text mode
  std::string temp = std::move(stream).str();
  temp.erase(std::remove(temp.begin(), temp.end(), '\r'), temp.end());
  stream.str(std::move(temp));
#endif
  err = linker_map.ReadStream(stream, line_number);

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