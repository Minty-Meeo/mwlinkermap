#include <cstddef>
#include <istream>
#include <regex>
#include <sstream>
#include <string>
#include <utility>

#include "MWLinkerMap.h"

#define DECLARE_DEBUG_STRING_VIEW std::string_view _debug_string_view(head, tail)
#define UPDATE_DEBUG_STRING_VIEW _debug_string_view = {head, tail}
#define xstoul(__s) std::stoul(__s, nullptr, 16)

auto MWLinkerMap::Read(std::istream& stream, std::size_t& line_number) -> Error
{
  std::stringstream sstream;
  sstream << stream.rdbuf();
  const std::string string = std::move(sstream).str();
  return this->Read(string, line_number);
}

auto MWLinkerMap::Read(const std::string& string, std::size_t& line_number) -> Error
{
  return this->Read(string.begin(), string.end(), line_number);
}

// clang-format off
static const std::regex re_entry_point_name{
//  "Link map of %s\r\n"
    "Link map of (.+)\r\n"};
static const std::regex re_unresolved_symbol{
//  ">>> SYMBOL NOT FOUND: %s\r\n"
    ">>> SYMBOL NOT FOUND: (.+)\r\n"};
static const std::regex re_excluded_symbol{
//  ">>> EXCLUDED SYMBOL %s (%s,%s) found in %s %s\r\n"
    ">>> EXCLUDED SYMBOL (.+) \\((.+),(.+)\\) found in (.+) (.+)\r\n"};
static const std::regex re_linktime_size_increasing_optimizations_header{
//  "\r\nLinktime size-increasing optimizations\r\n"
    "\r\nLinktime size-increasing optimizations\r\n"};
static const std::regex re_linktime_size_decreasing_optimizations_header{
//  "\r\nLinktime size-decreasing optimizations\r\n"
    "\r\nLinktime size-decreasing optimizations\r\n"};
static const std::regex re_mixed_mode_islands_header{
//  "\r\nMixed Mode Islands\r\n"
    "\r\nMixed Mode Islands\r\n"};
static const std::regex re_branch_islands_header{
//  "\r\nBranch Islands\r\n"
    "\r\nBranch Islands\r\n"};
static const std::regex re_section_layout_header{
//  "\r\n\r\n%s section layout\r\n"
    "\r\n\r\n(.+) section layout\r\n"};
static const std::regex re_section_layout_header_modified_a{
//  "\r\n%s section layout\r\n"
    "\r\n(.+) section layout\r\n"};
static const std::regex re_section_layout_header_modified_b{
//  "%s section layout\r\n"
    "(.+) section layout\r\n"};
static const std::regex re_memory_map_header{
//  "\r\n\r\nMemory map:\r\n"
    "\r\n\r\nMemory map:\r\n"};
static const std::regex re_linker_generated_symbols_header{
//  "\r\n\r\nLinker generated symbols:\r\n"
    "\r\n\r\nLinker generated symbols:\r\n"};
// clang-format on

auto MWLinkerMap::Read(std::string::const_iterator head, const std::string::const_iterator tail,
                       std::size_t& line_number) -> Error
{
  std::smatch match;
  DECLARE_DEBUG_STRING_VIEW;
  line_number = 0;

  // Normally this would always be present; its absence would be an early sign that the file is
  // not a MetroWerks linker map. However, I have decided to support certain modified linker maps
  // that are almost on-spec but are missing this portion, among other things.
  if (std::regex_search(head, tail, match, re_entry_point_name,
                        std::regex_constants::match_continuous))
  {
    line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
    auto portion = std::make_unique<EntryPoint>(match.str(1));
    this->portions.push_back(std::move(portion));  // TODO: emplace_back
  }
  while (head < tail)
  {
    if (std::regex_search(head, tail, match, re_unresolved_symbol,
                          std::regex_constants::match_continuous))
    {
      line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
      this->unresolved_symbols.push_back(match.str(1));
      // TODO: min version if this is pre-printed
      continue;
    }
    break;
  }
  {
    auto portion = std::make_unique<SymbolClosure>();
    const auto error = portion->Read(head, tail, this->unresolved_symbols, line_number);
    UPDATE_DEBUG_STRING_VIEW;
    if (error != Error::None)
      return error;
    // TODO: don't add empty ones
    this->portions.push_back(std::move(portion));
  }
  {
    auto portion = std::make_unique<EPPC_PatternMatching>();
    const auto error = portion->Read(head, tail, line_number);
    UPDATE_DEBUG_STRING_VIEW;
    if (error != Error::None)
      return error;
    // TODO: don't add empty ones
    this->portions.push_back(std::move(portion));
  }
  while (head < tail)
  {
    // TODO: is this where SYMBOL NOT FOUND post-prints really go?  Double check Ghidra.
    if (std::regex_search(head, tail, match, re_unresolved_symbol,
                          std::regex_constants::match_continuous))
    {
      line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
      this->unresolved_symbols.push_back(match.str(1));
      // TODO: min version if this is post-printed
      continue;
    }
    break;
  }
  {
    auto portion = std::make_unique<LinkerOpts>();
    const auto error = portion->Read(head, tail, line_number);
    UPDATE_DEBUG_STRING_VIEW;
    if (error != Error::None)
      return error;
    // TODO: don't add empty ones
    this->portions.push_back(std::move(portion));
  }
  if (std::regex_search(head, tail, match, re_mixed_mode_islands_header,
                        std::regex_constants::match_continuous))
  {
    line_number += 2, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
    // TODO
  }
  if (std::regex_search(head, tail, match, re_branch_islands_header,
                        std::regex_constants::match_continuous))
  {
    line_number += 2, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
    // TODO
  }
  if (std::regex_search(head, tail, match, re_linktime_size_decreasing_optimizations_header,
                        std::regex_constants::match_continuous))
  {
    line_number += 2, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
    // TODO
  }
  if (std::regex_search(head, tail, match, re_linktime_size_increasing_optimizations_header,
                        std::regex_constants::match_continuous))
  {
    line_number += 2, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
    // TODO
  }
  while (head < tail)
  {
    if (std::regex_search(head, tail, match, re_section_layout_header,
                          std::regex_constants::match_continuous))
    {
      line_number += 3, head += match.length(), UPDATE_DEBUG_STRING_VIEW;

      auto portion = std::make_unique<SectionLayout>(match.str(1));
      const auto error = portion->Read(head, tail, line_number);
      UPDATE_DEBUG_STRING_VIEW;
      if (error != Error::None)
        return error;
      this->portions.push_back(std::move(portion));
      continue;
    }
    if (std::regex_search(head, tail, match, re_section_layout_header_modified_a,
                          std::regex_constants::match_continuous))
    {
      line_number += 2, head += match.length(), UPDATE_DEBUG_STRING_VIEW;

      // Linker maps from Animal Crossing (foresta.map and static.map) and Doubutsu no Mori e+
      // (foresta.map, forestd.map, foresti.map, foresto.map, and static.map) appear to have been
      // modified to strip out the Link Map portion and UNUSED symbols, though the way it was done
      // also removed one of the Section Layout header's preceding newlines.
      auto portion = std::make_unique<SectionLayout>(match.str(1));
      const auto error = portion->Read(head, tail, line_number);
      UPDATE_DEBUG_STRING_VIEW;
      if (error != Error::None)
        return error;
      this->portions.push_back(std::move(portion));
      continue;
    }
    if (std::regex_search(head, tail, match, re_section_layout_header_modified_b,
                          std::regex_constants::match_continuous))
    {
      line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;

      // Linker maps from Doubutsu no Mori + (foresta.map2 and static.map2) are modified similarly
      // to their counterparts in Doubutsu no Mori e+, though now with no preceding newlines. The
      // unmodified linker maps were also left on the disc, so maybe just use those instead?
      auto portion = std::make_unique<SectionLayout>(match.str(1));
      const auto error = portion->Read(head, tail, line_number);
      UPDATE_DEBUG_STRING_VIEW;
      if (error != Error::None)
        return error;
      this->portions.push_back(std::move(portion));
      continue;
    }
    break;
  }
  if (std::regex_search(head, tail, match, re_memory_map_header,
                        std::regex_constants::match_continuous))
  {
    line_number += 3, head += match.length(), UPDATE_DEBUG_STRING_VIEW;

    auto portion = std::make_unique<MemoryMap>();
    const auto error = portion->Read(head, tail, line_number);
    UPDATE_DEBUG_STRING_VIEW;
    if (error != Error::None)
      return error;
    this->portions.push_back(std::move(portion));
  }
  if (std::regex_search(head, tail, match, re_linker_generated_symbols_header,
                        std::regex_constants::match_continuous))
  {
    line_number += 3, head += match.length(), UPDATE_DEBUG_STRING_VIEW;

    auto portion = std::make_unique<LinkerGeneratedSymbols>();
    const auto error = portion->Read(head, tail, line_number);
    UPDATE_DEBUG_STRING_VIEW;
    if (error != Error::None)
      return error;
    this->portions.push_back(std::move(portion));
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

// clang-format off
static const std::regex re_symbol_closure_node_prefix{
//  "%i] "
    " +(\\d+)] "};
static const std::regex re2_symbol_closure_node_normal{
//  "%s (%s,%s) found in %s %s\r\n"
    "(.+) \\((.+),(.+)\\) found in (.+) (.*)\r\n"};
static const std::regex re2_symbol_closure_node_normal_unref_dup_header{
    " +(\\d+)] >>> UNREFERENCED DUPLICATE (.+)\r\n"};
static const std::regex re2_symbol_closure_node_normal_unref_dups{
//  ">>> (%s,%s) found in %s %s\r\n"
    " +(\\d+)\\] >>> \\((.+),(.+)\\) found in (.+) (.*)\r\n"};
static const std::regex re2_symbol_closure_node_linker_generated{
//  "%s found as linker generated symbol\r\n"
    " +(\\d+)\\] (.+) found as linker generated symbol\r\n"};
// clang-format on

auto MWLinkerMap::SymbolClosure::Read2(std::string::const_iterator& head,
                                       const std::string::const_iterator tail,
                                       NodeBase* const parent_node, const int curr_level,
                                       std::size_t& line_number) -> Error
{
  std::smatch match;
  DECLARE_DEBUG_STRING_VIEW;

  while (head < tail)
  {
    if (std::regex_search(head, tail, match, re_symbol_closure_node_prefix,
                          std::regex_constants::match_continuous))
    {
      const int next_level = std::stoi(match.str(1));
      if (next_level == curr_level)
      {
        const auto prefix_length = match.length();
        if (std::regex_search(head + prefix_length, tail, match, re2_symbol_closure_node_normal,
                              std::regex_constants::match_continuous))
        {
          line_number += 1, head += prefix_length + match.length(), UPDATE_DEBUG_STRING_VIEW;

          auto node = std::make_unique<NodeNormal>(match.str(2), match.str(3), match.str(4),
                                                   match.str(5), match.str(6));

          if (std::regex_search(head, tail, match, re2_symbol_closure_node_normal_unref_dup_header,
                                std::regex_constants::match_continuous))
          {
            if (std::stoi(match.str(1)) != curr_level)
              return Error::SymbolClosureUnrefDupsLevelMismatch;
            if (match.str(2) != node->name)
              return Error::SymbolClosureUnrefDupsNameMismatch;
            line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;

            while (head < tail)
            {
              if (!std::regex_search(head, tail, match, re2_symbol_closure_node_normal_unref_dups,
                                     std::regex_constants::match_continuous))
                break;

              if (std::stoi(match.str(1)) != curr_level)
                return Error::SymbolClosureUnrefDupsLevelMismatch;
              line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;

              node->unref_dups.emplace_back(match.str(2), match.str(3), match.str(4), match.str(5));
            }
            if (node->unref_dups.size() == 0)
              return Error::SymbolClosureUnrefDupsEmpty;
            this->SetMinVersion(MWLinkerVersion::version_2_3_3_build_137);
          }

          node->parent = parent_node;
          Read2(head, tail, node.get(), next_level + 1, line_number);
          parent_node->children.push_back(std::move(node));
          continue;
        }
        if (std::regex_search(head + prefix_length, tail, match,
                              re2_symbol_closure_node_linker_generated,
                              std::regex_constants::match_continuous))
        {
          /* code */
        }
      }
      else if (next_level > curr_level)
      {
        return Error::SymbolClosureHierarchySkip;
      }
      else
      {
        return Error::None;
      }
    }
  }
}

// clang-format off
static const std::regex re_symbol_closure_node_normal{
//  "%s (%s,%s) found in %s %s\r\n"
    " +(\\d+)\\] (.+) \\((.+),(.+)\\) found in (.+) (.*)\r\n"};
static const std::regex re_symbol_closure_node_normal_unref_dup_header{
//  ">>> UNREFERENCED DUPLICATE %s\r\n"
    " +(\\d+)\\] >>> UNREFERENCED DUPLICATE (.+)\r\n"};
static const std::regex re_symbol_closure_node_normal_unref_dups{
//  ">>> (%s,%s) found in %s %s\r\n"
    " +(\\d+)\\] >>> \\((.+),(.+)\\) found in (.+) (.*)\r\n"};
static const std::regex re_symbol_closure_node_linker_generated{
//  "%s found as linker generated symbol\r\n"
    " +(\\d+)\\] (.+) found as linker generated symbol\r\n"};
// clang-format on

auto MWLinkerMap::SymbolClosure::Read(std::string::const_iterator& head,
                                      const std::string::const_iterator tail,
                                      std::list<std::string>& unresolved_symbols,
                                      std::size_t& line_number) -> Error
{
  std::smatch match;
  DECLARE_DEBUG_STRING_VIEW;

  int prev_level = 0;
  std::map<int, NodeBase*> hierarchy_history = {std::make_pair(prev_level, &this->root)};

  while (head < tail)
  {
    if (std::regex_search(head, tail, match, re_symbol_closure_node_normal,
                          std::regex_constants::match_continuous))
    {
      int curr_level = std::stoi(match.str(1));
      if (curr_level - 1 > prev_level)
        return Error::SymbolClosureHierarchySkip;
      line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;

      auto node = std::make_unique<NodeNormal>(match.str(2), match.str(3), match.str(4),
                                               match.str(5), match.str(6));

      if (std::regex_search(head, tail, match, re_symbol_closure_node_normal_unref_dup_header,
                            std::regex_constants::match_continuous))
      {
        if (std::stoi(match.str(1)) != curr_level)
          return Error::SymbolClosureUnrefDupsLevelMismatch;
        if (match.str(2) != node->name)
          return Error::SymbolClosureUnrefDupsNameMismatch;
        line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;

        while (head < tail)
        {
          if (!std::regex_search(head, tail, match, re_symbol_closure_node_normal_unref_dups,
                                 std::regex_constants::match_continuous))
            break;

          if (std::stoi(match.str(1)) != curr_level)
            return Error::SymbolClosureUnrefDupsLevelMismatch;
          line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;

          node->unref_dups.emplace_back(match.str(2), match.str(3), match.str(4), match.str(5));
        }
        if (node->unref_dups.size() == 0)
          return Error::SymbolClosureUnrefDupsEmpty;
        this->SetMinVersion(MWLinkerVersion::version_2_3_3_build_137);
      }

      if (node->name == "_dtors$99")
      {
        this->SetMinVersion(MWLinkerVersion::version_3_0_4);
        // Though I do not understand it, the following is a normal occurrence:
        // "  1] _dtors$99 (object,global) found in Linker Generated Symbol File "
        // "    3] .text (section,local) found in xyz.cpp lib.a"
        auto fake_node = std::make_unique<NodeBase>();
        hierarchy_history[curr_level + 1] = fake_node.get();
        fake_node->parent = node.get();
        node->children.push_back(std::move(fake_node));
        prev_level = curr_level + 1;
      }
      else
      {
        prev_level = curr_level;
      }
      NodeBase* parent = hierarchy_history[curr_level - 1];
      hierarchy_history[curr_level] = node.get();
      node->parent = parent;
      parent->children.push_back(std::move(node));

      continue;
    }
    if (std::regex_search(head, tail, match, re_symbol_closure_node_linker_generated,
                          std::regex_constants::match_continuous))
    {
      int curr_level = std::stoi(match.str(1));
      if (curr_level - 1 > prev_level)
        return Error::SymbolClosureHierarchySkip;
      line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;

      auto node = std::make_unique<NodeLinkerGenerated>(match.str(2));

      SymbolClosure::NodeBase* parent = hierarchy_history[curr_level - 1];
      hierarchy_history[curr_level] = node.get();
      node->parent = parent;
      parent->children.push_back(std::move(node));
      prev_level = curr_level;
      continue;
    }
    if (std::regex_search(head, tail, match, re_unresolved_symbol,
                          std::regex_constants::match_continuous))
    {
      // Some versions of MWLDEPPC print unresolved symbols as the link tree is being walked and
      // printed itself. This gives a good idea of what function was looking for that symbol, but
      // because no hierarchy tier is given, it is impossible to be certain without analyzing code.
      // TODO: min version if this is mid-printed
      line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
      unresolved_symbols.push_back(match.str(1));
      continue;
    }
    if (std::regex_search(head, tail, match, re_excluded_symbol,
                          std::regex_constants::match_continuous))
    {
      // TODO: wtf is this
      return Error::Unimplemented;
      continue;
    }
    break;
  }
  return Error::None;
}

// clang-format off
static const std::regex re_code_fold_analysis_unit_duplicate{
//  "--> duplicated code: symbol %s is duplicated by %s, size = %d \r\n\r\n"
    "--> duplicated code: symbol (.+) is duplicated by (.+), size = (\\d+) \r\n\r\n"};
static const std::regex re_code_fold_analysis_unit_replace{
//  "--> the function %s will be replaced by a branch to %s\r\n\r\n\r\n"
    "--> the function (.+) will be replaced by a branch to (.+)\r\n\r\n\r\n"};
static const std::regex re_code_fold_analysis_unit_interchange{
//  "--> the function %s was interchanged with %s, size=%d \r\n"
    "--> the function (.+) was interchanged with (.+), size=(\\d+) \r\n"};
static const std::regex re_code_fold_summary_header{
//  "\r\n\r\n\r\nCode folded in file: %s \r\n"
    "\r\n\r\n\r\nCode folded in file: (.+) \r\n"};
// clang-format on

auto MWLinkerMap::EPPC_PatternMatching::Read(std::string::const_iterator& head,
                                             const std::string::const_iterator tail,
                                             std::size_t& line_number) -> Error
{
  std::smatch match;
  DECLARE_DEBUG_STRING_VIEW;

  while (head < tail)
  {
    if (std::regex_search(head, tail, match, re_code_fold_analysis_unit_duplicate,
                          std::regex_constants::match_continuous))
    {
      line_number += 2, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
      continue;
    }
    if (std::regex_search(head, tail, match, re_code_fold_analysis_unit_replace,
                          std::regex_constants::match_continuous))
    {
      line_number += 3, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
      continue;
    }
    if (std::regex_search(head, tail, match, re_code_fold_analysis_unit_interchange,
                          std::regex_constants::match_continuous))
    {
      line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
      continue;
    }
    break;
  }
  while (head < tail)
  {
    if (std::regex_search(head, tail, match, re_code_fold_summary_header,
                          std::regex_constants::match_continuous))
    {
      line_number += 4, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
      const auto error = ReadSummary(head, tail, line_number);
      if (error != Error::None)
        return error;
      continue;
    }
    break;
  }
  return Error::None;
}

// clang-format off
static const std::regex re_code_fold_summary_unit_duplicate{
//  "--> %s is duplicated by %s, size = %d \r\n\r\n"
    "--> (.+) is duplicated by (.+), size = (\\d+) \r\n\r\n"};
static const std::regex re_code_fold_summary_unit_duplicate_new_branch{
//  "--> %s is duplicated by %s, size = %d, new branch function %s \r\n\r\n"
    "--> (.+) is duplicated by (.+), size = (\\d+), new branch function (.+) \r\n\r\n"};
// clang-format on

auto MWLinkerMap::EPPC_PatternMatching::ReadSummary(std::string::const_iterator& head,
                                                    const std::string::const_iterator tail,
                                                    std::size_t& line_number) -> Error
{
  std::smatch match;
  DECLARE_DEBUG_STRING_VIEW;

  while (head < tail)
  {
    if (std::regex_search(head, tail, match, re_code_fold_summary_unit_duplicate,
                          std::regex_constants::match_continuous))
    {
      line_number += 2, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
      continue;
    }
    if (std::regex_search(head, tail, match, re_code_fold_summary_unit_duplicate_new_branch,
                          std::regex_constants::match_continuous))
    {
      line_number += 2, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
      continue;
    }
    break;
  }
  return Error::None;
}

// clang-format off
static const std::regex re_linker_opts_unit_address_range{
//  "  %s/ %s()/ %s - address not in near addressing range \r\n"
    "  (.+)/ (.+)\\(\\)/ (.+) - address not in near addressing range \r\n"};
static const std::regex re_linker_opts_unit_address_not_computed{
//  "  %s/ %s()/ %s - final address not yet computed \r\n"
    "  (.+)/ (.+)\\(\\)/ (.+) - final address not yet computed \r\n"};
static const std::regex re_linker_opts_unit_address_optimize{
//  "! %s/ %s()/ %s - optimized addressing \r\n"
    "! (.+)/ (.+)\\(\\)/ (.+) - optimized addressing \r\n"};
static const std::regex re_linker_opts_unit_disassemble_error{
//  "  %s/ %s() - error disassembling function \r\n"
    "  (.+)/ (.+)\\(\\) - error disassembling function \r\n"};
// clang-format on

auto MWLinkerMap::LinkerOpts::Read(std::string::const_iterator& head,
                                   const std::string::const_iterator tail, std::size_t& line_number)
    -> Error
{
  std::smatch match;
  DECLARE_DEBUG_STRING_VIEW;

  while (head < tail)
  {
    if (std::regex_search(head, tail, match, re_linker_opts_unit_address_range,
                          std::regex_constants::match_continuous))
    {
      line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
      continue;
    }
    if (std::regex_search(head, tail, match, re_linker_opts_unit_address_not_computed,
                          std::regex_constants::match_continuous))
    {
      line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
      continue;
    }
    if (std::regex_search(head, tail, match, re_linker_opts_unit_address_optimize,
                          std::regex_constants::match_continuous))
    {
      line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
      continue;
    }
    if (std::regex_search(head, tail, match, re_linker_opts_unit_disassemble_error,
                          std::regex_constants::match_continuous))
    {
      line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
      continue;
    }
    break;
  }
  return Error::None;
}

// clang-format off
static const std::regex re_section_layout_3_column_prologue_1{
    "  Starting        Virtual\r\n"};
static const std::regex re_section_layout_3_column_prologue_2{
    "  address  Size   address\r\n"};
static const std::regex re_section_layout_3_column_prologue_3{
    "  -----------------------\r\n"};
static const std::regex re_section_layout_4_column_prologue_1{
    "  Starting        Virtual  File\r\n"};
static const std::regex re_section_layout_4_column_prologue_2{
    "  address  Size   address  offset\r\n"};
static const std::regex re_section_layout_4_column_prologue_3{
    "  ---------------------------------\r\n"};
// clang-format on

auto MWLinkerMap::SectionLayout::Read(std::string::const_iterator& head,
                                      const std::string::const_iterator tail,
                                      std::size_t& line_number) -> Error
{
  std::smatch match;
  DECLARE_DEBUG_STRING_VIEW;

  if (std::regex_search(head, tail, match, re_section_layout_3_column_prologue_1,
                        std::regex_constants::match_continuous))
  {
    line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
    if (std::regex_search(head, tail, match, re_section_layout_3_column_prologue_2,
                          std::regex_constants::match_continuous))
    {
      line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
      if (std::regex_search(head, tail, match, re_section_layout_3_column_prologue_3,
                            std::regex_constants::match_continuous))
      {
        line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
        return this->Read3Column(head, tail, line_number);
      }
      else
      {
        return Error::SectionLayoutBadPrologue;
      }
    }
    else
    {
      return Error::SectionLayoutBadPrologue;
    }
  }
  else if (std::regex_search(head, tail, match, re_section_layout_4_column_prologue_1,
                             std::regex_constants::match_continuous))
  {
    line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
    if (std::regex_search(head, tail, match, re_section_layout_4_column_prologue_2,
                          std::regex_constants::match_continuous))
    {
      line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
      if (std::regex_search(head, tail, match, re_section_layout_4_column_prologue_3,
                            std::regex_constants::match_continuous))
      {
        line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
        this->SetMinVersion(MWLinkerVersion::version_3_0_4);
        return this->Read4Column(head, tail, line_number);
      }
      else
      {
        return Error::SectionLayoutBadPrologue;
      }
    }
    else
    {
      return Error::SectionLayoutBadPrologue;
    }
  }
  else
  {
    return Error::SectionLayoutBadPrologue;
  }
}

// clang-format off
static const std::regex re_section_layout_3_column_unit_normal{
//  "  %08x %06x %08x %2i %s \t%s %s\r\n"
    "  ([0-9a-f]{8}) ([0-9a-f]{6}) ([0-9a-f]{8})  ?(\\d+) (.+) \t(.+) (.*)\r\n"};
static const std::regex re_section_layout_3_column_unit_unused{
//  "  UNUSED   %06x ........ %s %s %s\r\n"
    "  UNUSED   ([0-9a-f]{6}) \\.{8} (.+) (.+) (.*)\r\n"};
static const std::regex re_section_layout_3_column_unit_entry{
//  "  %08lx %06lx %08lx %s (entry of %s) \t%s %s\r\n"
    "  ([0-9a-f]{8}) ([0-9a-f]{6}) ([0-9a-f]{8}) (.+) \\(entry of (.+)\\) \t(.+) (.*)\r\n"};
// clang-format on

auto MWLinkerMap::SectionLayout::Read3Column(std::string::const_iterator& head,
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
      this->units.push_back(std::make_unique<UnitNormal>(  //
          xstoul(match.str(1)), xstoul(match.str(2)), xstoul(match.str(3)), 0,
          std::stoul(match.str(4)), match.str(5), match.str(6), match.str(7)));
      line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
      continue;
    }
    if (std::regex_search(head, tail, match, re_section_layout_3_column_unit_unused,
                          std::regex_constants::match_continuous))
    {
      this->units.push_back(std::make_unique<UnitUnused>(  //
          xstoul(match.str(1)), match.str(2), match.str(3), match.str(4)));
      line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
      continue;
    }
    if (std::regex_search(head, tail, match, re_section_layout_3_column_unit_entry,
                          std::regex_constants::match_continuous))
    {
      this->units.push_back(std::make_unique<UnitEntry>(  //
          xstoul(match.str(1)), xstoul(match.str(2)), xstoul(match.str(3)), 0, match.str(4),
          match.str(5), match.str(6), match.str(7)));
      line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
      continue;
    }
    break;
  }
  return Error::None;
}

// clang-format off
static const std::regex re_section_layout_4_column_unit_normal{
//  "  %08x %06x %08x %08x %2i %s \t%s %s\r\n"
    "  ([0-9a-f]{8}) ([0-9a-f]{6}) ([0-9a-f]{8}) ([0-9a-f]{8})  ?(\\d+) (.+) \t(.+) (.*)\r\n"};
static const std::regex re_section_layout_4_column_unit_unused{
//  "  UNUSED   %06x ........ ........    %s %s %s\r\n"
    "  UNUSED   ([0-9a-f]{6}) \\.{8} \\.{8}    (.+) (.+) (.*)\r\n"};
static const std::regex re_section_layout_4_column_unit_entry{
//  "  %08lx %06lx %08lx %08lx    %s (entry of %s) \t%s %s\r\n"
    "  ([0-9a-f]{8}) ([0-9a-f]{6}) ([0-9a-f]{8}) ([0-9a-f]{8})    (.+) \\(entry of (.+)\\) \t(.+) (.*)\r\n"};
static const std::regex re_section_layout_4_column_unit_special{
//  "  %08x %06x %08x %08x %2i %s\r\n"
    "  ([0-9a-f]{8}) ([0-9a-f]{6}) ([0-9a-f]{8}) ([0-9a-f]{8})  ?(\\d+) (.+)\r\n"};
// clang-format on

auto MWLinkerMap::SectionLayout::Read4Column(std::string::const_iterator& head,
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
      this->units.push_back(std::make_unique<UnitNormal>(  //
          xstoul(match.str(1)), xstoul(match.str(2)), xstoul(match.str(3)), xstoul(match.str(4)),
          std::stoul(match.str(5)), match.str(6), match.str(7), match.str(8)));
      line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
      continue;
    }
    if (std::regex_search(head, tail, match, re_section_layout_4_column_unit_unused,
                          std::regex_constants::match_continuous))
    {
      this->units.push_back(std::make_unique<UnitUnused>(  //
          xstoul(match.str(1)), match.str(2), match.str(3), match.str(4)));
      line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
      continue;
    }
    if (std::regex_search(head, tail, match, re_section_layout_4_column_unit_entry,
                          std::regex_constants::match_continuous))
    {
      this->units.push_back(std::make_unique<UnitEntry>(  //
          xstoul(match.str(1)), xstoul(match.str(2)), xstoul(match.str(3)), xstoul(match.str(4)),
          match.str(5), match.str(6), match.str(7), match.str(8)));
      line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
      continue;
    }
    // This regex is so overkill, it risks finding false positives.  TODO: irregularity post-checker
    if (std::regex_search(head, tail, match, re_section_layout_4_column_unit_special,
                          std::regex_constants::match_continuous))
    {
      this->units.push_back(std::make_unique<UnitSpecial>(  //
          xstoul(match.str(1)), xstoul(match.str(2)), xstoul(match.str(3)), xstoul(match.str(4)),
          std::stoul(match.str(5)), match.str(6)));
      line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
      continue;
    }
    break;
  }
  return Error::None;
}

// clang-format off
static const std::regex re_memory_map_3_column_prologue_1a{
//  "                   Starting Size     File\r\n"
    "                   Starting Size     File\r\n"};
static const std::regex re_memory_map_3_column_prologue_2a{
//  "                   address           Offset\r\n"
    "                   address           Offset\r\n"};
static const std::regex re_memory_map_3_column_prologue_1b{
//  "                       Starting Size     File\r\n"
    "                       Starting Size     File\r\n"};
static const std::regex re_memory_map_3_column_prologue_2b{
//  "                       address           Offset\r\n"
    "                       address           Offset\r\n"};
static const std::regex re_memory_map_5_column_prologue_1{
//  "                   Starting Size     File     ROM      RAM Buffer\r\n"
    "                   Starting Size     File     ROM      RAM Buffer\r\n"};
static const std::regex re_memory_map_5_column_prologue_2{
//  "                   address           Offset   Address  Address\r\n"
    "                   address           Offset   Address  Address\r\n"};
// clang-format on

auto MWLinkerMap::MemoryMap::Read(std::string::const_iterator& head,
                                  const std::string::const_iterator tail, std::size_t& line_number)
    -> Error
{
  std::smatch match;
  DECLARE_DEBUG_STRING_VIEW;

  if (std::regex_search(head, tail, match, re_memory_map_3_column_prologue_1a,
                        std::regex_constants::match_continuous))
  {
    line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
    if (std::regex_search(head, tail, match, re_memory_map_3_column_prologue_2a,
                          std::regex_constants::match_continuous))
    {
      line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
      this->extra_info = false;
      return Read3ColumnA(head, tail, line_number);
    }
    else
    {
      return Error::MemoryMapBadPrologue;
    }
  }
  else if (std::regex_search(head, tail, match, re_memory_map_3_column_prologue_1b,
                             std::regex_constants::match_continuous))
  {
    line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
    if (std::regex_search(head, tail, match, re_memory_map_3_column_prologue_2b,
                          std::regex_constants::match_continuous))
    {
      line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
      this->extra_info = false;
      this->SetMinVersion(MWLinkerVersion::version_4_2_build_142);
      return Read3ColumnB(head, tail, line_number);
    }
    else
    {
      return Error::MemoryMapBadPrologue;
    }
  }
  else if (std::regex_search(head, tail, match, re_memory_map_5_column_prologue_1,
                             std::regex_constants::match_continuous))
  {
    line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
    if (std::regex_search(head, tail, match, re_memory_map_5_column_prologue_2,
                          std::regex_constants::match_continuous))
    {
      line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
      this->extra_info = true;
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

// clang-format off
static const std::regex re_memory_map_unit_allocated_a{
//  "  %15s  %08x %08x %08x\r\n"
    "   *(.+)  ([0-9a-f]{8}) ([0-9a-f]{8}) ([0-9a-f]{8})\r\n"};
static const std::regex re_memory_map_unit_info_a{
//  "  %15s           %06x %08x\r\n"
    "   *(.+)           ([0-9a-f]{6,8}) ([0-9a-f]{8})\r\n"};
// clang-format on

auto MWLinkerMap::MemoryMap::Read3ColumnA(std::string::const_iterator& head,
                                          const std::string::const_iterator tail,
                                          std::size_t& line_number) -> Error
{
  std::smatch match;
  DECLARE_DEBUG_STRING_VIEW;

  while (head < tail)
  {
    if (std::regex_search(head, tail, match, re_memory_map_unit_allocated_a,
                          std::regex_constants::match_continuous))
    {
      line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
      this->units.push_back(std::make_unique<UnitAllocated>(  //
          match.str(1), xstoul(match.str(3)), xstoul(match.str(4)), xstoul(match.str(2)), 0, 0));
      continue;
    }
    if (std::regex_search(head, tail, match, re_memory_map_unit_info_a,
                          std::regex_constants::match_continuous))
    {
      line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
      this->units.push_back(std::make_unique<UnitInfo>(  //
          match.str(1), xstoul(match.str(2)), xstoul(match.str(3))));
      continue;
    }
    break;
  }
  return Error::None;
}

// clang-format off
static const std::regex re_memory_map_unit_allocated_b{
//  "  %20s %08x %08x %08x\r\n"
    "   *(.+) ([0-9a-f]{8}) ([0-9a-f]{8}) ([0-9a-f]{8})\r\n"};
static const std::regex re_memory_map_unit_info_b{
//  "  %20s          %08x %08x\r\n"
    "   *(.+)          ([0-9a-f]{8}) ([0-9a-f]{8})\r\n"};
// clang-format on

auto MWLinkerMap::MemoryMap::Read3ColumnB(std::string::const_iterator& head,
                                          const std::string::const_iterator tail,
                                          std::size_t& line_number) -> Error
{
  std::smatch match;
  DECLARE_DEBUG_STRING_VIEW;

  while (head < tail)
  {
    if (std::regex_search(head, tail, match, re_memory_map_unit_allocated_b,
                          std::regex_constants::match_continuous))
    {
      line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
      this->units.push_back(std::make_unique<UnitAllocated>(  //
          match.str(1), xstoul(match.str(3)), xstoul(match.str(4)), xstoul(match.str(2)), 0, 0));
      continue;
    }
    if (std::regex_search(head, tail, match, re_memory_map_unit_info_b,
                          std::regex_constants::match_continuous))
    {
      line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
      this->units.push_back(std::make_unique<UnitInfo>(  //
          match.str(1), xstoul(match.str(2)), xstoul(match.str(3))));
      continue;
    }
    break;
  }
  return Error::None;
}

auto MWLinkerMap::MemoryMap::Read5Column(std::string::const_iterator& head,
                                         const std::string::const_iterator tail,
                                         std::size_t& line_number) -> Error
{
  return Error::Unimplemented;
}

// clang-format off
static const std::regex re_linker_generated_symbols_unit{
//  "%25s %08x\r\n"
    " *(.+) ([0-9a-f]{8})\r\n"};
// clang-format on

auto MWLinkerMap::LinkerGeneratedSymbols::Read(std::string::const_iterator& head,
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
      this->units.push_back(std::make_unique<Unit>(  //
          match.str(1), xstoul(match.str(2))));
      continue;
    }
    break;
  }
  return Error::None;
}