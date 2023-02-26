#include <cstddef>
#include <istream>
#include <regex>
#include <sstream>
#include <string>
#include <utility>

#include "MWLinkerMap2.h"

#define DECLARE_DEBUG_STRING_VIEW std::string_view _debug_string_view(head, tail)
#define UPDATE_DEBUG_STRING_VIEW _debug_string_view = {head, tail}
#define xstoul(__s) std::stoul(__s, nullptr, 16)

auto MWLinkerMap2::Read(std::istream& stream, std::size_t& line_number) -> Error
{
  std::stringstream sstream;
  sstream << stream.rdbuf();
  std::string string = std::move(sstream).str();
  return this->Read(string.begin(), string.end(), line_number);
}

static const std::regex re_mixed_mode_islands_header{
    "\r\nMixed Mode Islands\r\n"};  /////////////////////////////////
//  "" TODO
static const std::regex re_branch_islands_header{
    "\r\nBranch Islands\r\n"};  /////////////////////////////////////
//  "" TODO
static const std::regex re_section_layout_header{
    "\r\n\r\n(.+) section layout\r\n"};  ////////////////////////////
//  "\r\n\r\n%s section layout\r\n"  ////////////////////////////////
static const std::regex re_memory_map_header{
    "\r\n\r\nMemory map:\r\n"};  ////////////////////////////////////
//  "\r\n\r\nMemory map:\r\n"  //////////////////////////////////////
static const std::regex re_linker_generated_symbols_header{
    "\r\n\r\nLinker generated symbols:\r\n"};  //////////////////////
//  "\r\n\r\nLinker generated symbols:\r\n"  ////////////////////////

auto MWLinkerMap2::Read(std::string::const_iterator head, const std::string::const_iterator tail,
                        std::size_t& line_number) -> Error
{
  std::smatch match;
  DECLARE_DEBUG_STRING_VIEW;
  line_number = 0;

  {
    auto piece = std::make_unique<LinkMap>();
    const auto error = piece->Read(head, tail, line_number);
    UPDATE_DEBUG_STRING_VIEW;
    if (error != Error::None)
      return error;
    this->m_pieces.push_back(std::move(piece));
  }

  // TODO: Branch Islands

  // TODO: Mixed Mode Islands

  while (head < tail)
  {
    if (!std::regex_search(head, tail, match, re_section_layout_header,
                           std::regex_constants::match_continuous))
      break;
    line_number += 3, head += match.length(), UPDATE_DEBUG_STRING_VIEW;

    auto piece = std::make_unique<SectionLayout>(match.str(1));
    const auto error = piece->Read(head, tail, line_number);
    UPDATE_DEBUG_STRING_VIEW;
    if (error != Error::None)
      return error;
    this->m_pieces.push_back(std::move(piece));
  }
  if (std::regex_search(head, tail, match, re_memory_map_header,
                        std::regex_constants::match_continuous))
  {
    line_number += 3, head += match.length(), UPDATE_DEBUG_STRING_VIEW;

    auto piece = std::make_unique<MemoryMap>();
    const auto error = piece->Read(head, tail, line_number);
    UPDATE_DEBUG_STRING_VIEW;
    if (error != Error::None)
      return error;
    this->m_pieces.push_back(std::move(piece));
  }
  if (std::regex_search(head, tail, match, re_linker_generated_symbols_header,
                        std::regex_constants::match_continuous))
  {
    line_number += 3, head += match.length(), UPDATE_DEBUG_STRING_VIEW;

    auto piece = std::make_unique<LinkerGeneratedSymbols>();
    const auto error = piece->Read(head, tail, line_number);
    UPDATE_DEBUG_STRING_VIEW;
    if (error != Error::None)
      return error;
    this->m_pieces.push_back(std::move(piece));
  }
  if (head < tail)
  {
    // Gamecube ISO Tool is a tool that can extract and rebuild *.GCM images. This tool has a bug
    // that appends null byte padding to the next multiple of 32 bytes at the end of any file it
    // extracts. During my research, I ran into a lot of linker maps afflicted by this bug, enough
    // to justify a special case for it. http://www.wiibackupmanager.co.uk/gcit.html
    if (std::any_of(head, tail, [](const char c) { return c != '\0'; }))
      return Error::GarbageFound;
  }

  return Error::None;
}

static const std::regex re_link_map_header{
    "Link map of (.+)\r\n"};  ///////////////////////////////////////
//  "Link map of %s\r\n"  ///////////////////////////////////////////
static const std::regex re_link_map_node_normal{
    " +(\\d+)\\] (.+) \\((.+),(.+)\\) found in (.+) (.+)?\r\n"};  //
//  "%s (%s,%s) found in %s %s\r\n"  ///////////////////////////////
static const std::regex re_link_map_unit_normal_unref_dup_header{
    " +(\\d+)\\] >>> UNREFERENCED DUPLICATE (.+)\r\n"};  ///////////
//  ">>> UNREFERENCED DUPLICATE %s\r\n"  ///////////////////////////
static const std::regex re_link_map_unit_normal_unref_dups{
    " +(\\d+)\\] >>> \\((.+),(.+)\\) found in (.+) (.+)?\r\n"};  ///
//  ">>> (%s,%s) found in %s %s\r\n"  //////////////////////////////
static const std::regex re_link_map_node_linker_generated{
    " +(\\d+)\\] (.+) found as linker generated symbol\r\n"};  /////
//  "%s found as linker generated symbol\r\n"  /////////////////////

auto MWLinkerMap2::LinkMap::Read(std::string::const_iterator& head,
                                 const std::string::const_iterator tail, std::size_t& line_number)
    -> Error
{
  std::smatch match;
  DECLARE_DEBUG_STRING_VIEW;

  if (!std::regex_search(head, tail, match, re_link_map_header,
                         std::regex_constants::match_continuous))
    return Error::Fail;
  line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;

  this->entry_point_name = match.str(1);

  int prev_level = 0;
  std::map<int, NodeBase*> hierarchy_history = {std::make_pair(prev_level, &this->root)};

  while (head < tail)
  {
    if (std::regex_search(head, tail, match, re_link_map_node_normal,
                          std::regex_constants::match_continuous))
    {
      int curr_level = std::stol(match.str(1));
      if (curr_level - 1 > prev_level)
        return Error::LinkMapLayerSkip;
      line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;

      auto node = std::make_unique<NodeNormal>(match.str(2), match.str(3), match.str(4),
                                               match.str(5), match.str(6));

      if (std::regex_search(head, tail, match, re_link_map_unit_normal_unref_dup_header,
                            std::regex_constants::match_continuous))
      {
        if (std::stol(match.str(1)) != curr_level)
          return Error::LinkMapUnrefDupsLevelMismatch;
        if (match.str(2) != node->name)
          return Error::LinkMapUnrefDupsNameMismatch;
        line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;

        while (head < tail)
        {
          if (!std::regex_search(head, tail, match, re_link_map_unit_normal_unref_dups,
                                 std::regex_constants::match_continuous))
            break;

          if (std::stol(match.str(1)) != curr_level)
            return Error::LinkMapUnrefDupsLevelMismatch;
          line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;

          node->unref_dups.emplace_back(match.str(2), match.str(3), match.str(4), match.str(5));
        }
        if (node->unref_dups.size() == 0)
          return Error::LinkMapUnrefDupsEmpty;
        this->SetMinVersion(LDVersion::version_2_3_3_build_137);
      }

      NodeBase* parent = hierarchy_history[curr_level - 1];
      hierarchy_history[curr_level] = node.get();
      node->parent = parent;
      parent->children.push_back(std::move(node));
      prev_level = curr_level;
      continue;
    }
    if (std::regex_search(head, tail, match, re_link_map_node_linker_generated,
                          std::regex_constants::match_continuous))
    {
      int curr_level = std::stol(match.str(1));
      if (curr_level - 1 > prev_level)
        return Error::LinkMapLayerSkip;
      line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;

      auto node = std::make_unique<NodeLinkerGenerated>(match.str(2));

      LinkMap::NodeBase* parent = hierarchy_history[curr_level - 1];
      hierarchy_history[curr_level] = node.get();
      node->parent = parent;
      parent->children.push_back(std::move(node));
      prev_level = curr_level;
      continue;
    }
    break;
  }

  return Error::None;
}

static const std::regex re_section_layout_3_column_prologue_a{
    "  Starting        Virtual\r\n"};  ///////////////////////
static const std::regex re_section_layout_3_column_prologue_b{
    "  address  Size   address\r\n"};  ///////////////////////
static const std::regex re_section_layout_3_column_prologue_c{
    "  -----------------------\r\n"};  ///////////////////////
static const std::regex re_section_layout_4_column_prologue_a{
    "  Starting        Virtual  File\r\n"};  /////////////////
static const std::regex re_section_layout_4_column_prologue_b{
    "  address  Size   address  offset\r\n"};  ///////////////
static const std::regex re_section_layout_4_column_prologue_c{
    "  ---------------------------------\r\n"};  /////////////

auto MWLinkerMap2::SectionLayout::Read(std::string::const_iterator& head,
                                       const std::string::const_iterator tail,
                                       std::size_t& line_number) -> Error
{
  std::smatch match;
  DECLARE_DEBUG_STRING_VIEW;

  if (std::regex_search(head, tail, match, re_section_layout_3_column_prologue_a,
                        std::regex_constants::match_continuous))
  {
    line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
    if (std::regex_search(head, tail, match, re_section_layout_3_column_prologue_b,
                          std::regex_constants::match_continuous))
    {
      line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
      if (std::regex_search(head, tail, match, re_section_layout_3_column_prologue_c,
                            std::regex_constants::match_continuous))
      {
        line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
        return this->Read3Column(head, tail, line_number);
      }
      else
      {
        return Error::SectionLayoutBadHeader;
      }
    }
    else
    {
      return Error::SectionLayoutBadHeader;
    }
  }
  else if (std::regex_search(head, tail, match, re_section_layout_4_column_prologue_a,
                             std::regex_constants::match_continuous))
  {
    line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
    if (std::regex_search(head, tail, match, re_section_layout_4_column_prologue_b,
                          std::regex_constants::match_continuous))
    {
      line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
      if (std::regex_search(head, tail, match, re_section_layout_4_column_prologue_c,
                            std::regex_constants::match_continuous))
      {
        line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
        this->SetMinVersion(LDVersion::version_3_0_4);
        return this->Read4Column(head, tail, line_number);
      }
      else
      {
        return Error::SectionLayoutBadHeader;
      }
    }
    else
    {
      return Error::SectionLayoutBadHeader;
    }
  }
  else
  {
    return Error::SectionLayoutBadHeader;
  }
}

// "  %08x %06x %08x %2i %s \t%s %s\r\n"
static const std::regex re_section_layout_3_column_unit_normal{
    "  ([0-9a-f]{8}) ([0-9a-f]{6}) ([0-9a-f]{8})  ?(\\d+) (.+) \t(.+) (.+)?\r\n"};
// "  UNUSED   %06x ........ %s %s %s\r\n"
static const std::regex re_section_layout_3_column_unit_unused{
    "  UNUSED   ([0-9a-f]{6}) \\.{8} (.+) (.+) (.+)?\r\n"};
// "  %08lx %06lx %08lx %s (entry of %s) \t%s %s\r\n"
static const std::regex re_section_layout_3_column_unit_entry{
    "  ([0-9a-f]{8}) ([0-9a-f]{6}) ([0-9a-f]{8}) (.+) \\(entry of (.+)\\) \t(.+) (.+)?\r\n"};

auto MWLinkerMap2::SectionLayout::Read3Column(std::string::const_iterator& head,
                                              const std::string::const_iterator tail,
                                              std::size_t& line_number) -> Error
{
  std::smatch match;
  DECLARE_DEBUG_STRING_VIEW;

  while (head < tail)
  {
    if (std::regex_search(head, tail, match, re_section_layout_3_column_unit_normal,
                          std::regex_constants::match_continuous))
    {
      this->m_units.push_back(std::make_unique<UnitNormal>(  //
          xstoul(match.str(1)), xstoul(match.str(2)), xstoul(match.str(3)), 0,
          std::stoul(match.str(4)), match.str(5), match.str(6), match.str(7)));
      line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
      continue;
    }
    if (std::regex_search(head, tail, match, re_section_layout_3_column_unit_unused,
                          std::regex_constants::match_continuous))
    {
      this->m_units.push_back(std::make_unique<UnitUnused>(  //
          xstoul(match.str(1)), match.str(2), match.str(3), match.str(4)));
      line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
      this->SetMinVersion(LDVersion::version_2_3_3_build_137);
      continue;
    }
    if (std::regex_search(head, tail, match, re_section_layout_3_column_unit_entry,
                          std::regex_constants::match_continuous))
    {
      this->m_units.push_back(std::make_unique<UnitEntry>(  //
          xstoul(match.str(1)), xstoul(match.str(2)), xstoul(match.str(3)), 0, match.str(4),
          match.str(5), match.str(6), match.str(7)));
      line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
      continue;
    }
    break;
  }
  return Error::None;
}

static const std::regex re_section_layout_4_column_unit_normal{
    "  ([0-9a-f]{8}) ([0-9a-f]{6}) ([0-9a-f]{8}) ([0-9a-f]{8})  ?(\\d+) (.+) \t(.+) (.+)?\r\n"};

static const std::regex re_section_layout_4_column_unit_unused{
    "  UNUSED   ([0-9a-f]{6}) \\.{8} \\.{8}    (.+) (.+) (.+)?\r\n"};

static const std::regex re_section_layout_4_column_unit_entry{
    "  ([0-9a-f]{8}) ([0-9a-f]{6}) ([0-9a-f]{8}) ([0-9a-f]{8})    (.+) \\(entry of (.+)\\) \t(.+) "
    "(.+)?\r\n"};

static const std::regex re_section_layout_4_column_unit_fill{
    "  ([0-9a-f]{8}) ([0-9a-f]{6}) ([0-9a-f]{8}) ([0-9a-f]{8})  ?(\\d+) \\*fill\\*\r\n"};

auto MWLinkerMap2::SectionLayout::Read4Column(std::string::const_iterator& head,
                                              const std::string::const_iterator tail,
                                              std::size_t& line_number) -> Error
{
  std::smatch match;
  DECLARE_DEBUG_STRING_VIEW;

  while (head < tail)
  {
    if (std::regex_search(head, tail, match, re_section_layout_4_column_unit_normal,
                          std::regex_constants::match_continuous))
    {
      this->m_units.push_back(std::make_unique<UnitNormal>(  //
          xstoul(match.str(1)), xstoul(match.str(2)), xstoul(match.str(3)), xstoul(match.str(4)),
          std::stoul(match.str(5)), match.str(6), match.str(7), match.str(8)));
      line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
      continue;
    }
    if (std::regex_search(head, tail, match, re_section_layout_4_column_unit_unused,
                          std::regex_constants::match_continuous))
    {
      this->m_units.push_back(std::make_unique<UnitUnused>(  //
          xstoul(match.str(1)), match.str(2), match.str(3), match.str(4)));
      line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
      continue;
    }
    if (std::regex_search(head, tail, match, re_section_layout_4_column_unit_fill,
                          std::regex_constants::match_continuous))
    {
      this->m_units.push_back(std::make_unique<UnitFill>(  //
          xstoul(match.str(1)), xstoul(match.str(2)), xstoul(match.str(3)), xstoul(match.str(4)),
          std::stoul(match.str(5))));
      line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
      continue;
    }
    if (std::regex_search(head, tail, match, re_section_layout_4_column_unit_entry,
                          std::regex_constants::match_continuous))
    {
      this->m_units.push_back(std::make_unique<UnitEntry>(  //
          xstoul(match.str(1)), xstoul(match.str(2)), xstoul(match.str(3)), xstoul(match.str(4)),
          match.str(5), match.str(6), match.str(7), match.str(8)));
      line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
      continue;
    }
    break;
  }
  return Error::None;
}

static const std::regex re_memory_map_2_column_prologue_a{
    "                   Starting Size     File\r\n"};  ////////////////////////
//  "                   Starting Size     File\r\n"  //////////////////////////
static const std::regex re_memory_map_2_column_prologue_b{
    "                   address           Offset\r\n"};
//  "                   address           Offset\r\n"
static const std::regex re_memory_map_4_column_prologue_a{
    "                   Starting Size     File     ROM      RAM Buffer\r\n"};
//  "                   Starting Size     File     ROM      RAM Buffer\r\n"  //
static const std::regex re_memory_map_4_column_prologue_b{
    "                   address           Offset   Address  Address\r\n"};  ///
//  "                   address           Offset   Address  Address\r\n"  /////

auto MWLinkerMap2::MemoryMap::Read(std::string::const_iterator& head,
                                   const std::string::const_iterator tail, std::size_t& line_number)
    -> Error
{
  std::smatch match;
  DECLARE_DEBUG_STRING_VIEW;

  if (std::regex_search(head, tail, match, re_memory_map_2_column_prologue_a,
                        std::regex_constants::match_continuous))
  {
    line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
    if (std::regex_search(head, tail, match, re_memory_map_2_column_prologue_b,
                          std::regex_constants::match_continuous))
    {
      line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
      this->m_extra_info = false;
      return Read3Column(head, tail, line_number);
    }
    else
    {
      return Error::MemoryMapBadPrologue;
    }
  }
  else if (std::regex_search(head, tail, match, re_memory_map_4_column_prologue_a,
                             std::regex_constants::match_continuous))
  {
    line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
    if (std::regex_search(head, tail, match, re_memory_map_4_column_prologue_b,
                          std::regex_constants::match_continuous))
    {
      line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
      this->m_extra_info = true;
      return Read5Column(head, tail, line_number);
    }
    else
    {
      return Error::MemoryMapBadPrologue;
    }
  }
  else
  {
    return Error::MemoryMapBadPrologue;
  }
}

static const std::regex re_memory_map_unit_allocated{
    "   *(.+)  ([0-9a-f]{8}) ([0-9a-f]{8}) ([0-9a-f]{8})\r\n"};  //////////////
//  "  %15s  %08x %08x %08x\r\n"  /////////////////////////////////////////////
static const std::regex re_memory_map_unit_info{
    "   *(.+)           ([0-9a-f]{6,8}) ([0-9a-f]{8})\r\n"};  /////////////////
//  "  %15s           %06x %08x\r\n"  /////////////////////////////////////////

auto MWLinkerMap2::MemoryMap::Read3Column(std::string::const_iterator& head,
                                          const std::string::const_iterator tail,
                                          std::size_t& line_number) -> Error
{
  std::smatch match;
  DECLARE_DEBUG_STRING_VIEW;

  while (head < tail)
  {
    if (std::regex_search(head, tail, match, re_memory_map_unit_allocated,
                          std::regex_constants::match_continuous))
    {
      line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
      this->m_units.push_back(std::make_unique<UnitAllocated>(  //
          match.str(1), xstoul(match.str(3)), xstoul(match.str(4)), xstoul(match.str(2)), 0, 0));
      continue;
    }
    if (std::regex_search(head, tail, match, re_memory_map_unit_info,
                          std::regex_constants::match_continuous))
    {
      line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
      this->m_units.push_back(std::make_unique<UnitInfo>(  //
          match.str(1), xstoul(match.str(2)), xstoul(match.str(3))));
      continue;
    }
    break;
  }
  return Error::None;
}

auto MWLinkerMap2::MemoryMap::Read5Column(std::string::const_iterator& head,
                                          const std::string::const_iterator tail,
                                          std::size_t& line_number) -> Error
{
  return Error::Unimplemented;
}

static const std::regex re_linker_generated_symbols_unit{
    " *(.+) ([0-9a-f]{8})\r\n"};  /////////////////////////////////////////////
//  "%25s %08x\r\n"  //////////////////////////////////////////////////////////

auto MWLinkerMap2::LinkerGeneratedSymbols::Read(std::string::const_iterator& head,
                                                const std::string::const_iterator tail,
                                                std::size_t& line_number) -> Error
{
  std::smatch match;
  DECLARE_DEBUG_STRING_VIEW;

  while (head < tail)
  {
    if (std::regex_search(head, tail, match, re_linker_generated_symbols_unit,
                          std::regex_constants::match_continuous))
    {
      line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
      this->m_units.push_back(std::make_unique<Unit>(  //
          match.str(1), xstoul(match.str(2))));
      continue;
    }
    break;
  }
  return Error::None;
}