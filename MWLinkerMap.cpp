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
template <class CharT, class Traits>
constexpr static bool BasicStringContains(const std::basic_string<CharT, Traits>& self,
                                          const CharT c)
{
  return (self.find(c) != std::basic_string<CharT, Traits>::npos);
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
  for (line_number = 0; line_number < lines.size();)
  {
    std::string& line = lines[line_number];
    if (line.empty())
    {
      ++line_number;
      continue;
    }
    if (line.starts_with("Link map of "))
    {
      auto part = std::make_unique<LinkMap>();
      if (const auto err = part->ReadLines(lines, line_number); err != MWLinkerMap::Error::None)
        return err;
      this->m_parts.push_back(std::move(part));
      continue;
    }
    if (line.ends_with(" section layout"))
    {
      auto part = std::make_unique<SectionLayout>();
      if (const auto err = part->ReadLines(lines, line_number); err != MWLinkerMap::Error::None)
        return err;
      this->m_parts.push_back(std::move(part));
      continue;
    }
    if (line == "Memory map:")
    {
      auto part = std::make_unique<MemoryMap>();
      if (const auto err = part->ReadLines(lines, line_number); err != MWLinkerMap::Error::None)
        return err;
      this->m_parts.push_back(std::move(part));
      continue;
    }
    if (line == "Linker generated symbols:")
    {
      auto part = std::make_unique<LinkerGeneratedSymbols>();
      if (const auto err = part->ReadLines(lines, line_number); err != MWLinkerMap::Error::None)
        return err;
      this->m_parts.push_back(std::move(part));
      continue;
    }
    if (BasicStringContains(line, '\0'))
    {
      // Some linker maps have padding to the next multiple of 32 bytes for... some reason.
      // We shall assume this is always the end of the file if such a thing is found.
      // TODO: Why??
      this->m_null_padding = true;
      break;
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
    static const std::regex re("^Link map of (.+)$");
    if (!std::regex_search(lines[line_number], match, re))
      return MWLinkerMap::Error::RegexFail;
    ++line_number;

    this->entry_point_name = match.str(1);
  }

  int prev_level = 0;
  std::map<int, MWLinkerMap::LinkMap::NodeBase*> hierarchy_history = {
      std::make_pair(prev_level, &this->root)};

  while (line_number < lines.size())
  {
    std::string& line = lines[line_number];
    if (line.empty() || BasicStringContains(line, '\0')) [[unlikely]]
      break;

    if (BasicStringContains(line, "found as"))
    {
      // " ", "%i] ", "%s found as linker generated symbol\r\n"
      static const std::regex re("^ +(\\d+)\\] (.+) found as linker generated symbol$");
      if (!std::regex_search(line, match, re))
        return MWLinkerMap::Error::RegexFail;

      int curr_level = std::stol(match.str(1));
      if (curr_level - 1 > prev_level)
        return MWLinkerMap::Error::LinkMapLayerSkip;
      ++line_number;

      auto node = std::make_unique<NodeLinkerGenerated>(match.str(2));

      MWLinkerMap::LinkMap::NodeBase* parent = hierarchy_history[curr_level - 1];
      hierarchy_history[curr_level] = node.get();
      node->parent = parent;
      parent->children.push_back(std::move(node));
      prev_level = curr_level;
    }
    else if (line.starts_with(">>>"))
    {
      // ">>> SYMBOL NOT FOUND: %s"
      static const std::regex re("^>>> SYMBOL NOT FOUND: (.+)$");
      if (!std::regex_search(line, match, re))
        return MWLinkerMap::Error::RegexFail;
      ++line_number;

      auto node = std::make_unique<NodeNotFound>(match.str(1));

      MWLinkerMap::LinkMap::NodeBase* parent = hierarchy_history[prev_level];
      // It shouldn't be possible to have children, so it is not recorded in the hierarchy history.
      node->parent = parent;
      parent->children.push_back(std::move(node));
    }
    else
    {
      static const std::regex re("^ +(\\d+)\\] (.+) \\((.+),(.+)\\) found in (.+) (.+)?$");
      if (!std::regex_search(line, match, re))
        return MWLinkerMap::Error::RegexFail;

      int curr_level = std::stol(match.str(1));
      if (curr_level - 1 > prev_level)
        return MWLinkerMap::Error::LinkMapLayerSkip;
      ++line_number;

      auto node = std::make_unique<NodeNormal>(match.str(2), match.str(3), match.str(4),
                                               match.str(5), match.str(6));

      if (line_number < lines.size() && BasicStringContains(lines[line_number], "] >>>"))
      {
        if (const auto err = node->ReadLinesUnrefDups(lines, line_number, curr_level);
            err != MWLinkerMap::Error::None)
          return err;
      }

      MWLinkerMap::LinkMap::NodeBase* parent = hierarchy_history[curr_level - 1];
      hierarchy_history[curr_level] = node.get();
      node->parent = parent;
      parent->children.push_back(std::move(node));
      prev_level = curr_level;
    }
  }

  if (this->root.children.size() > 0)
    if (this->root.children.front()->name != this->entry_point_name)
      return MWLinkerMap::Error::LinkMapEntryPointNameMismatch;

  return MWLinkerMap::Error::None;
}

MWLinkerMap::Error
MWLinkerMap::LinkMap::NodeNormal::ReadLinesUnrefDups(std::vector<std::string>& lines,
                                                     std::size_t& line_number, const int curr_level)
{
  std::smatch match;
  {
    static const std::regex re("^ +(\\d+)\\] >>> UNREFERENCED DUPLICATE (.+)$");
    if (!std::regex_search(lines[line_number], match, re))
      return MWLinkerMap::Error::RegexFail;
    if (std::stol(match.str(1)) != curr_level)
      return MWLinkerMap::Error::LinkMapUnrefDupsLevelMismatch;
    if (match.str(2) != this->name)
      return MWLinkerMap::Error::LinkMapUnrefDupsBadHeader;
    ++line_number;
  }

  if (line_number >= lines.size() || lines[line_number].empty() ||
      BasicStringContains(lines[line_number], '\0')) [[unlikely]]
    return MWLinkerMap::Error::LinkMapUnrefDupsEmpty;

  while (line_number < lines.size())
  {
    std::string& line = lines[line_number];
    if (line.empty() || BasicStringContains(line, '\0')) [[unlikely]]
      break;
    if (!BasicStringContains(line, "] >>>"))  // End of UNREFERENCED DUPLICATEs
      break;

    static const std::regex re("^ +(\\d+)\\] >>> \\((.+),(.+)\\) found in (.+) (.+)?$");
    if (!std::regex_search(line, match, re))
      return MWLinkerMap::Error::RegexFail;
    if (std::stol(match.str(1)) != curr_level)
      return MWLinkerMap::Error::LinkMapUnrefDupsLevelMismatch;
    ++line_number;

    this->unref_dups.emplace_back(match.str(2), match.str(3), match.str(4), match.str(5));
  }
  return MWLinkerMap::Error::None;
}

MWLinkerMap::Error MWLinkerMap::SectionLayout::ReadLines(std::vector<std::string>& lines,
                                                         std::size_t& line_number)
{
  std::smatch match;
  static const std::regex re("^(.+) section layout$");
  if (!std::regex_search(lines[line_number], match, re))
    return MWLinkerMap::Error::RegexFail;
  ++line_number;

  if (!(line_number + 2 < lines.size()))
    return MWLinkerMap::Error::SectionLayoutBadHeader;

  if (lines[line_number] == "  Starting        Virtual")
  {
    ++line_number;
    if (lines[line_number] == "  address  Size   address")
    {
      ++line_number;
      if (lines[line_number] == "  -----------------------")
        this->m_pre_2_7 = true;
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
        this->m_pre_2_7 = false;
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

  if (this->m_pre_2_7)
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
    if (line.empty() || BasicStringContains(line, '\0')) [[unlikely]]
      return MWLinkerMap::Error::None;

    if (BasicStringContains(line, "UNUSED  "))
    {
      static const std::regex re("^  UNUSED   ([0-9a-f]{6}) \\.{8} (.+) (.+) (.+)?$");
      if (!std::regex_search(line, match, re))
        return MWLinkerMap::Error::RegexFail;

      this->m_units.push_back(std::make_unique<UnitUnused>(  //
          xstoul(match.str(1)), match.str(2), match.str(3), match.str(4)));
    }
    else if (BasicStringContains(line, "(entry of "))
    {
      static const std::regex re(
          "^  ([0-9a-f]{8}) ([0-9a-f]{6}) ([0-9a-f]{8}) (.+) \\(entry of (.+)\\) \t(.+) (.+)?$");
      if (!std::regex_search(line, match, re))
        return MWLinkerMap::Error::RegexFail;

      this->m_units.push_back(std::make_unique<UnitEntry>(  //
          xstoul(match.str(1)), xstoul(match.str(2)), xstoul(match.str(3)), 0, match.str(4),
          match.str(5), match.str(6), match.str(7)));
    }
    else
    {
      static const std::regex re(
          "^  ([0-9a-f]{8}) ([0-9a-f]{6}) ([0-9a-f]{8})  ?(\\d+) (.+) \t(.+) (.+)?$");
      if (!std::regex_search(line, match, re))
        return MWLinkerMap::Error::RegexFail;

      this->m_units.push_back(std::make_unique<UnitNormal>(  //
          xstoul(match.str(1)), xstoul(match.str(2)), xstoul(match.str(3)), 0,
          std::stoul(match.str(4)), match.str(5), match.str(6), match.str(7)));
    }
  }
  return MWLinkerMap::Error::None;
}

MWLinkerMap::Error MWLinkerMap::SectionLayout::ReadLines4Column(std::vector<std::string>& lines,
                                                                std::size_t& line_number)
{
  std::smatch match;
  for (; line_number < lines.size(); ++line_number)
  {
    std::string& line = lines[line_number];
    if (line.empty()) [[unlikely]]
      return MWLinkerMap::Error::None;

    if (line.starts_with("  UNUSED"))
    {
      static const std::regex re("^  UNUSED   ([0-9a-f]{6}) \\.{8} \\.{8}    (.+) (.+) (.+)?$");
      if (!std::regex_search(line, match, re))
        return MWLinkerMap::Error::RegexFail;

      this->m_units.push_back(std::make_unique<UnitUnused>(  //
          xstoul(match.str(1)), match.str(2), match.str(3), match.str(4)));
    }
    else if (BasicStringContains(line, "(entry of "))
    {
      static const std::regex re("^  ([0-9a-f]{8}) ([0-9a-f]{6}) ([0-9a-f]{8}) ([0-9a-f]{8})    "
                                 "(.+) \\(entry of (.+)\\) \t(.+) (.+)?$");
      if (!std::regex_search(line, match, re))
        return MWLinkerMap::Error::RegexFail;

      this->m_units.push_back(std::make_unique<UnitEntry>(
          xstoul(match.str(1)), xstoul(match.str(2)), xstoul(match.str(3)), xstoul(match.str(4)),
          match.str(5), match.str(6), match.str(7), match.str(8)));
    }
    else if (line.ends_with("*fill*"))
    {
      static const std::regex re(
          "^  ([0-9a-f]{8}) ([0-9a-f]{6}) ([0-9a-f]{8}) ([0-9a-f]{8})  ?(\\d+) \\*fill\\*$");
      if (!std::regex_search(line, match, re))
        return MWLinkerMap::Error::RegexFail;

      this->m_units.push_back(std::make_unique<UnitFill>(  //
          xstoul(match.str(1)), xstoul(match.str(2)), xstoul(match.str(3)), xstoul(match.str(4)),
          std::stoul(match.str(5))));
    }
    else
    {
      static const std::regex re(
          "^  ([0-9a-f]{8}) ([0-9a-f]{6}) ([0-9a-f]{8}) ([0-9a-f]{8})  ?(\\d+) (.+) \t(.+) (.+)?$");
      if (!std::regex_search(line, match, re))
        return MWLinkerMap::Error::RegexFail;

      this->m_units.push_back(std::make_unique<UnitNormal>(
          xstoul(match.str(1)), xstoul(match.str(2)), xstoul(match.str(3)), xstoul(match.str(4)),
          std::stoul(match.str(5)), match.str(6), match.str(7), match.str(8)));
    }
  }
  return MWLinkerMap::Error::None;
}

MWLinkerMap::Error MWLinkerMap::MemoryMap::ReadLines(std::vector<std::string>& lines,
                                                     std::size_t& line_number)
{
  ++line_number;  // Already know this line says "Memory map:"

  if (!(line_number + 1 < lines.size()))
    return MWLinkerMap::Error::MemoryMapBadHeader;

  if (lines[line_number] == "                   Starting Size     File")
  {
    ++line_number;
    if (lines[line_number] == "                   address           Offset")
      this->m_extra_info = false;
    else
      return MWLinkerMap::Error::MemoryMapBadHeader;
  }
  else if (lines[line_number] ==
           "                   Starting Size     File     ROM      RAM Buffer")
  {
    ++line_number;
    if (lines[line_number] == "                   address           Offset   Address  Address")
      this->m_extra_info = true;
    else
      return MWLinkerMap::Error::MemoryMapBadHeader;
  }
  else
  {
    return MWLinkerMap::Error::MemoryMapBadHeader;
  }
  ++line_number;

  if (this->m_extra_info)
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
    if (line.empty() || BasicStringContains(line, '\0')) [[unlikely]]
      return MWLinkerMap::Error::None;

    if (line.substr(19, 8) == "        ")
    {
      // "  %15s           %06x %08x"
      static const std::regex re("^   *(.+)           ([0-9a-f]{6,8}) ([0-9a-f]{8})$");
      if (!std::regex_search(line, match, re))
        return MWLinkerMap::Error::RegexFail;

      this->m_units.push_back(std::make_unique<UnitInfo>(  //
          match.str(1), xstoul(match.str(2)), xstoul(match.str(3))));
    }
    else
    {
      // "  %15s  %08x %08x %08x"
      static const std::regex re("^   *(.+)  ([0-9a-f]{8}) ([0-9a-f]{8}) ([0-9a-f]{8})$");
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

MWLinkerMap::Error MWLinkerMap::LinkerGeneratedSymbols::ReadLines(std::vector<std::string>& lines,
                                                                  std::size_t& line_number)
{
  ++line_number;  // We already know this line says "Linker generated symbols:"

  std::smatch match;
  for (; line_number < lines.size(); ++line_number)
  {
    std::string& line = lines[line_number];
    if (line.empty() || BasicStringContains(line, '\0')) [[unlikely]]
      return MWLinkerMap::Error::None;

    // "%25s %08x"
    static const std::regex re("^ *(.+) ([0-9a-f]{8})$");
    if (!std::regex_search(line, match, re))
      return MWLinkerMap::Error::RegexFail;

    this->m_units.push_back(std::make_unique<Unit>(  //
        match.str(1), xstoul(match.str(2))));
  }
  return MWLinkerMap::Error::None;
}
