#include <algorithm>
#include <fstream>
#include <functional>
#include <iterator>
#include <list>
#include <map>
#include <memory>
#include <queue>
#include <regex>
#include <sstream>
#include <stack>
#include <string>
#include <string_view>
#include <utility>

#include <cstring>
#include <iostream>

#include "MWLinkerMap.h"

#define WIN32_FILENAME "[^/\\\?%\\*:|\"<>,;= ]+"
#define HEX "[0-9A-Fa-f]+"
#define HEX8 "[0-9A-Fa-f]{8}"
#define HEX6 "[0-9A-Fa-f]{6}"
#define DEC "\\d+"
#define SYM_NAME "[\\w@$\\.<>,]+"

#define xstoul(__str) std::stoul(__str, nullptr, 16)

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
      if (const auto err = part->ReadLines(lines, line_number); err != MWLinkerMap::Error::None)
        return err;
      m_parts.push_back(std::move(part));
      continue;
    }
    if (BasicStringContains(line, " section layout"))
    {
      auto part = std::make_unique<SectionLayout>();
      if (const auto err = part->ReadLines(lines, line_number); err != MWLinkerMap::Error::None)
        return err;
      m_parts.push_back(std::move(part));
      continue;
    }
    if (line == "Memory map:")
    {
      auto part = std::make_unique<MemoryMap>();
      if (const auto err = part->ReadLines(lines, line_number); err != MWLinkerMap::Error::None)
        return err;
      m_parts.push_back(std::move(part));
      continue;
    }
    if (line == "Linker generated symbols:")
    {
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
    static const std::regex re("Link map of (.+)$");
    if (!std::regex_search(lines[line_number], match, re))
      return MWLinkerMap::Error::RegexFail;
    ++line_number;
  }

  std::string entry_point_name = match.str(1);

  // TODO: Handle potential EOF

  std::map<int, MWLinkerMap::LinkMap::NodeBase*> hierarchy_history = {
      std::make_pair(0, &this->root)};
  int prev_level = 0;
  int curr_level = 0;

  for (; line_number < lines.size();)
  {
    std::string& line = lines[line_number];
    if (line.length() == 0) [[unlikely]]  // Blank line (TODO: tolerate whitespace lines?)
      return MWLinkerMap::Error::None;

    if (BasicStringContains(line, "found as"))
    {
      static const std::regex re("(\\d+)\\] (.+) found as linker generated symbol$");
      if (!std::regex_search(line, match, re))
        return MWLinkerMap::Error::RegexFail;

      curr_level = std::stol(match.str(1));
      if (curr_level - 1 > prev_level)
        return MWLinkerMap::Error::LinkMapLayerSkip;
      ++line_number;

      auto node = std::make_unique<NodeLinkerGenerated>(match.str(2));

      MWLinkerMap::LinkMap::NodeBase* parent = hierarchy_history[curr_level - 1];
      hierarchy_history[curr_level] = node.get();
      node->parent = parent;
      parent->children.push_back(std::move(node));
    }
    else
    {
      static const std::regex re("(\\d+)\\] (.+) \\((.+),(.+)\\) found in (.+) (.+)?$");
      if (!std::regex_search(line, match, re))
        return MWLinkerMap::Error::RegexFail;

      curr_level = std::stol(match.str(1));
      if (curr_level - 1 > prev_level)
        return MWLinkerMap::Error::LinkMapLayerSkip;
      ++line_number;

      auto node = std::make_unique<NodeNormal>(match.str(2), match.str(3), match.str(4),
                                               match.str(5), match.str(6));

      if (line_number < lines.size() && BasicStringContains(lines[line_number], ">>>"))
      {
        if (const auto err = node->ReadLinesUnrefDups(lines, line_number, curr_level);
            err != MWLinkerMap::Error::None)
          return err;
      }

      MWLinkerMap::LinkMap::NodeBase* parent = hierarchy_history[curr_level - 1];
      hierarchy_history[curr_level] = node.get();
      node->parent = parent;
      parent->children.push_back(std::move(node));
    }

    prev_level = curr_level;
  }

  if (this->root.children.size() > 0)
    if (this->root.children.front()->name != entry_point_name)
      return MWLinkerMap::Error::LinkMapEntryPointNameMismatch;

  return MWLinkerMap::Error::None;
}

MWLinkerMap::Error
MWLinkerMap::LinkMap::NodeNormal::ReadLinesUnrefDups(std::vector<std::string>& lines,
                                                     std::size_t& line_number, const int curr_level)
{
  std::smatch match;
  {
    static const std::regex re("(\\d+)\\] >>> UNREFERENCED DUPLICATE (.+)$");
    if (!std::regex_search(lines[line_number], match, re))
      return MWLinkerMap::Error::RegexFail;
    if (std::stol(match.str(1)) != curr_level)
      return MWLinkerMap::Error::LinkMapUnrefDupsLevelMismatch;
    if (match.str(2) != this->name)
      return MWLinkerMap::Error::LinkMapUnrefDupsBadHeader;
    ++line_number;
  }

  if (line_number >= lines.size() || lines[line_number].length() == 0) [[unlikely]]
    return MWLinkerMap::Error::LinkMapUnrefDupsEmpty;

  for (; line_number < lines.size(); ++line_number)
  {
    std::string& line = lines[line_number];
    if (line.length() == 0) [[unlikely]]  // Blank line (TODO: tolerate whitespace lines?)
      return MWLinkerMap::Error::None;
    if (!BasicStringContains(line, ">>>"))  // End of UNREFERENCED DUPLICATEs
      return MWLinkerMap::Error::None;

    static const std::regex re("(\\d+)\\] >>> \\((.+),(.+)\\) found in (.+) (.+)?$");
    if (!std::regex_search(line, match, re))
      return MWLinkerMap::Error::RegexFail;
    if (std::stol(match.str(1)) != curr_level)
      return MWLinkerMap::Error::LinkMapUnrefDupsLevelMismatch;

    this->unref_dups.emplace_back(match.str(2), match.str(3), match.str(4), match.str(5));
  }
  return MWLinkerMap::Error::None;
}

MWLinkerMap::Error MWLinkerMap::SectionLayout::ReadLines(std::vector<std::string>& lines,
                                                         std::size_t& line_number)
{
  std::smatch match;
  static const std::regex re("(.+) section layout$");
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
      return MWLinkerMap::Error::None;

    if (BasicStringContains(line, "UNUSED  "))
    {
      static const std::regex re("UNUSED   (" HEX ") \\.\\.\\.\\.\\.\\.\\.\\. (" SYM_NAME ") "
                                 "(" WIN32_FILENAME ") (" WIN32_FILENAME ")?$");
      if (!std::regex_search(line, match, re))
        return MWLinkerMap::Error::RegexFail;

      this->m_units.push_back(std::make_unique<UnitUnused>(  //
          match.str(2), match.str(3), match.str(4), xstoul(match.str(1))));
    }
    else if (BasicStringContains(line, "(entry of "))
    {
      static const std::regex re("(" HEX ") (" HEX ") (" HEX ") (" SYM_NAME
                                 ") \\(entry of (" SYM_NAME ")\\) \t(" WIN32_FILENAME
                                 ") (" WIN32_FILENAME ")$");
      if (!std::regex_search(line, match, re))
        return MWLinkerMap::Error::RegexFail;

      this->m_units.push_back(std::make_unique<UnitEntry>(  //
          match.str(4), match.str(6), match.str(7), xstoul(match.str(2)), xstoul(match.str(1)),
          xstoul(match.str(3)), 0, match.str(5)));
    }
    else
    {
      static const std::regex re("(" HEX ") (" HEX ") (" HEX ")  ?(" DEC ") (" SYM_NAME ") \t"
                                 "(" WIN32_FILENAME ") (" WIN32_FILENAME ")?$");
      if (!std::regex_search(line, match, re))
        return MWLinkerMap::Error::RegexFail;

      this->m_units.push_back(std::make_unique<UnitNormal>(  //
          match.str(5), match.str(6), match.str(7), xstoul(match.str(2)), xstoul(match.str(1)),
          xstoul(match.str(3)), 0, stoul(match.str(4))));
    }
  }
  return MWLinkerMap::Error::None;
}

MWLinkerMap::Error MWLinkerMap::SectionLayout::ReadLines4Column(std::vector<std::string>& lines,
                                                                std::size_t& line_number)
{
  return MWLinkerMap::Error::Unimplemented;
}

MWLinkerMap::Error MWLinkerMap::MemoryMap::ReadLines(std::vector<std::string>& lines,
                                                     std::size_t& line_number)
{
  ++line_number;  // Already know this line says "Memory map:"

  if (!(line_number + 1 < lines.size()))
    return MWLinkerMap::Error::MemoryMapBadHeader;

  bool extra_info;  // TODO: What causes this??

  if (lines[line_number] == "                   Starting Size     File")
  {
    ++line_number;
    if (lines[line_number] == "                   address           Offset")
      extra_info = false;
    else
      return MWLinkerMap::Error::MemoryMapBadHeader;
  }
  else if (lines[line_number] ==
           "                   Starting Size     File     ROM      RAM Buffer")
  {
    ++line_number;
    if (lines[line_number] == "                   address           Offset   Address  Address")
      extra_info = true;
    else
      return MWLinkerMap::Error::MemoryMapBadHeader;
  }
  else
  {
    return MWLinkerMap::Error::MemoryMapBadHeader;
  }
  ++line_number;

  if (extra_info)
    return ReadLines5Column(lines, line_number);
  else
    return ReadLines3Column(lines, line_number);
}

MWLinkerMap::Error MWLinkerMap::MemoryMap::ReadLines3Column(std::vector<std::string>& lines,
                                                            std::size_t& line_number)
{
  std::smatch match;
  for (; line_number < lines.size(); ++line_number)
  {
    std::string& line = lines[line_number];
    if (line.length() == 0) [[unlikely]]  // Blank line (TODO: tolerate whitespace lines?)
      return MWLinkerMap::Error::None;

    if (line.substr(19, 8) == "        ")
    {
      // "  %15s           %06x %08x"
      static const std::regex re("   *(.+)           (" HEX6 ") (" HEX8 ")$");
      if (!std::regex_search(line, match, re))
        return MWLinkerMap::Error::RegexFail;

      this->m_units.push_back(std::make_unique<UnitInfo>(  //
          match.str(1), xstoul(match.str(2)), xstoul(match.str(3))));
    }
    else
    {
      // "  %15s  %08x %08x %08x"
      static const std::regex re("   *(.+)  (" HEX8 ") (" HEX8 ") (" HEX8 ")$");
      if (!std::regex_search(line, match, re))
        return MWLinkerMap::Error::RegexFail;

      this->m_units.push_back(std::make_unique<UnitAllocated>(  //
          match.str(1), xstoul(match.str(3)), xstoul(match.str(4)), xstoul(match.str(2)), 0, 0));
    }
  }
  return MWLinkerMap::Error::None;
}

MWLinkerMap::Error MWLinkerMap::MemoryMap::ReadLines5Column(std::vector<std::string>& lines,
                                                            std::size_t& line_number)
{
  // "  %15s  %08x %08x %08x %08x %08x"
  return MWLinkerMap::Error::Unimplemented;
}
