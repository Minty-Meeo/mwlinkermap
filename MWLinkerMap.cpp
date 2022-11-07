#include <algorithm>
#include <fstream>
#include <iterator>
#include <list>
#include <memory>
#include <queue>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include <cstring>
#include <iostream>

#include "MWLinkerMap.h"

#define WIN32_FILENAME "[^/\\\?%\\*:|\"<>,;= ]+"
#define HEX "[0-9a-fA-F]+"
#define DEC "\\d+"
#define SYM_NAME "[\\w@$\\.<>,]+"

// TODO C++23: std::basic_string 'contains' method
template <class CharT, class Traits>
constexpr static bool BasicStringContains(const std::basic_string<CharT, Traits>& self,
                                          const CharT* s)
{
  return (self.find(s) != std::basic_string<CharT, Traits>::npos);
}
template <class CharT, class Traits>
constexpr static bool BasicStringContains(const std::basic_string<CharT, Traits>& self,
                                          const std::basic_string_view<CharT, Traits> sv)
{
  return BasicStringContains(self, sv.data());
}

MWLinkerMap::Error MWLinkerMap::ReadStream(std::istream& stream, std::size_t& line_number)
{
  std::vector<std::string> lines;
  std::string line;
  while (!stream.eof())
  {
    std::getline(stream, line);
    lines.push_back(std::move(line));
  }
  return ReadLines(lines, line_number);
}

MWLinkerMap::Error MWLinkerMap::ReadLines(std::vector<std::string>& lines, std::size_t& line_number)
{
  for (line_number = 0; line_number < lines.size(); ++line_number)
  {
    std::string& line = lines[line_number];
    if (line.length() == 0)  // Blank line (TODO: tolerate whitespace lines?)
      continue;
    if (BasicStringContains(line, "Link map of "))
    {
      auto part = std::make_unique<LinkMap>();
      MWLinkerMap::Error err = part->ReadLines(lines, line_number);
      if (err != MWLinkerMap::Error::None)
        return err;
      m_list.push_back(std::move(part));
      continue;
    }
    if (BasicStringContains(line, " section layout"))
    {
      auto part = std::make_unique<SectionLayout>();
      MWLinkerMap::Error err = part->ReadLines(lines, line_number);
      if (err != MWLinkerMap::Error::None)
        return err;
      m_list.push_back(std::move(part));
      continue;
    }
    // This will be reached if something strange has been found
    return MWLinkerMap::Error::GarbageFound;
  }
  // This will be reached when all lines are read
  return MWLinkerMap::Error::None;
}

MWLinkerMap::Error MWLinkerMap::LinkMap::ReadLines(std::vector<std::string>& lines,
                                                   std::size_t& line_number)
{
  std::smatch match;
  {
    static const std::regex re("Link map of (" SYM_NAME ")$");
    if (!std::regex_search(lines[line_number], match, re))
      return MWLinkerMap::Error::RegexFail;
    ++line_number;
  }

  // TODO: Handle potential EOF

  for (; line_number < lines.size(); ++line_number)
  {
    std::string& line = lines[line_number];
    if (line.length() == 0) [[unlikely]]  // Blank line (TODO: tolerate whitespace lines?)
    {
      ++line_number;
      return MWLinkerMap::Error::None;
    }

    if (BasicStringContains(line, "found as"))
    {
      static const std::regex re("(\\d+)\\] (.+) found as linker generated symbol$");
      if (!std::regex_search(line, match, re))
        return MWLinkerMap::Error::RegexFail;
      this->push_back(std::move(std::make_unique<NodeLinkerGenerated>(std::move(match.str(2)))));
    }
    else
    {
      static const std::regex re("(\\d+)\\] (.+) \\((.+),(.+)\\) found in (.+) (.+)?$");
      if (!std::regex_search(line, match, re))
        return MWLinkerMap::Error::RegexFail;
      this->push_back(std::move(std::make_unique<NodeNormal>(
          std::move(match.str(2)), std::move(match.str(3)), std::move(match.str(4)),
          std::move(match.str(5)), std::move(match.str(6)))));
    }
  }
  return MWLinkerMap::Error::None;
}

MWLinkerMap::Error MWLinkerMap::SectionLayout::ReadLines(std::vector<std::string>& lines,
                                                         std::size_t& line_number)
{
  std::smatch match;
  static const std::regex re("(" SYM_NAME ") section layout$");
  if (!std::regex_search(lines[line_number], match, re))
    return MWLinkerMap::Error::RegexFail;
  ++line_number;

  if (!(line_number + 2 < lines.size()))
    return MWLinkerMap::Error::SectionLayoutBadHeader;

  bool pre_2_7;  // TODO: was this the version it changed?

  if (lines[line_number] == "  Starting        Virtual")
  {
    ++line_number;
    if (lines[line_number] == "  address  Size   address")
    {
      ++line_number;
      if (lines[line_number] == "  -----------------------")
        pre_2_7 = true;
      else
        return MWLinkerMap::Error::SectionLayoutBadHeader;
    }
    else
    {
      return MWLinkerMap::Error::SectionLayoutBadHeader;
    }
  }
  else if (lines[line_number] == "  Starting        Virtual  File")
  {
    ++line_number;
    if (lines[line_number] == "  address  Size   address  offset")
    {
      ++line_number;
      if (lines[line_number] == "  ---------------------------------")
        pre_2_7 = false;
      else
        return MWLinkerMap::Error::SectionLayoutBadHeader;
    }
    else
    {
      return MWLinkerMap::Error::SectionLayoutBadHeader;
    }
  }
  else
  {
    return MWLinkerMap::Error::SectionLayoutBadHeader;
  }
  ++line_number;

  if (pre_2_7)
    return ReadLines3Column(lines, line_number);
  else
    return ReadLines4Column(lines, line_number);
}

MWLinkerMap::Error MWLinkerMap::SectionLayout::ReadLines3Column(std::vector<std::string>& lines,
                                                                std::size_t& line_number)
{
  std::smatch match;
  for (; line_number < lines.size(); ++line_number)
  {
    std::string& line = lines[line_number];
    if (line.length() == 0) [[unlikely]]  // Blank line (TODO: tolerate whitespace lines?)
    {
      ++line_number;
      return MWLinkerMap::Error::None;
    }

    if (BasicStringContains(line, "UNUSED  "))
    {
      static const std::regex re("UNUSED   (" HEX ") \\.\\.\\.\\.\\.\\.\\.\\. (" SYM_NAME ") "
                                 "(" WIN32_FILENAME ") (" WIN32_FILENAME ")?$");
      if (!std::regex_search(line, match, re))
        return MWLinkerMap::Error::RegexFail;

      this->push_back(std::move(std::make_unique<NodeUnused>(
          std::stol(match.str(1), nullptr, 16), std::move(match.str(2)), std::move(match.str(3)),
          std::move(match.str(4)))));
    }
    else if (BasicStringContains(line, "(entry of "))
    {
      static const std::regex re("(" HEX ") (" HEX ") (" HEX ") (" SYM_NAME
                                 ") \\(entry of (" SYM_NAME ")\\) \t(" WIN32_FILENAME
                                 ") (" WIN32_FILENAME ")$");
      if (!std::regex_search(line, match, re))
        return MWLinkerMap::Error::RegexFail;

      this->push_back(std::move(std::make_unique<NodeEntry>(
          std::stol(match.str(1), nullptr, 16), std::stol(match.str(2), nullptr, 16),
          std::stol(match.str(3), nullptr, 16), 0, std::move(match.str(4)), std::move(match.str(5)),
          std::move(match.str(6)), std::move(match.str(7)))));
    }
    else
    {
      static const std::regex re("(" HEX ") (" HEX ") (" HEX ")  ?(" DEC ") (" SYM_NAME ") \t"
                                 "(" WIN32_FILENAME ") (" WIN32_FILENAME ")?$");
      if (!std::regex_search(line, match, re))
        return MWLinkerMap::Error::RegexFail;

      this->push_back(std::move(std::make_unique<NodeNormal>(
          std::stol(match.str(1), nullptr, 16), std::stol(match.str(2), nullptr, 16),
          std::stol(match.str(3), nullptr, 16), 0, std::stol(match.str(4), nullptr, 10),
          std::move(match.str(5)), std::move(match.str(6)), std::move(match.str(7)))));
    }
  }
  return MWLinkerMap::Error::None;
}

MWLinkerMap::Error MWLinkerMap::SectionLayout::ReadLines4Column(std::vector<std::string>& lines,
                                                                std::size_t& line_number)
{
  return MWLinkerMap::Error::Unimplemented;
}
