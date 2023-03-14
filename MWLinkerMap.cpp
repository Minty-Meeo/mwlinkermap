// TODO: speed test std::vector vs std::list

#include <cstddef>
#include <istream>
#include <regex>
#include <sstream>
#include <string>
#include <utility>

#include <iostream>
#include <typeinfo>

#include "MWLinkerMap.h"

#define DECLARE_DEBUG_STRING_VIEW std::string_view _debug_string_view(head, tail)
#define UPDATE_DEBUG_STRING_VIEW _debug_string_view = {head, tail}
#define xstoul(__s) std::stoul(__s, nullptr, 16)

MWLinkerMap::Error MWLinkerMap::Read(std::istream& stream, std::size_t& line_number)
{
  std::stringstream sstream;
  sstream << stream.rdbuf();
  const std::string string = std::move(sstream).str();
  return this->Read(string, line_number);
}

MWLinkerMap::Error MWLinkerMap::Read(const std::string& string, std::size_t& line_number)
{
  return this->Read(string.c_str(), string.c_str() + string.length(), line_number);
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
static const std::regex re_mixed_mode_islands_header{
//  "\r\nMixed Mode Islands\r\n"
    "\r\nMixed Mode Islands\r\n"};
static const std::regex re_branch_islands_header{
//  "\r\nBranch Islands\r\n"
    "\r\nBranch Islands\r\n"};
static const std::regex re_linktime_size_decreasing_optimizations_header{
//  "\r\nLinktime size-decreasing optimizations\r\n"
    "\r\nLinktime size-decreasing optimizations\r\n"};
static const std::regex re_linktime_size_increasing_optimizations_header{
//  "\r\nLinktime size-increasing optimizations\r\n"
    "\r\nLinktime size-increasing optimizations\r\n"};
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

// Other linker map prints are known to exist, but have never been seen.  These include:

// ">>> %s wasn't passed a section\r\n"
// ">>> DYNAMIC SYMBOL: %s referenced\r\n"
// ">>> MODULE SYMBOL NAME TOO LARGE: %s\r\n"
// ">>> NONMODULE SYMBOL NAME TOO LARGE: %s\r\n"
// "<<< Failure in ComputeSizeETI: section->Header->sh_size was %x, rel_size should be %x\r\n"
// "<<< Failure in ComputeSizeETI: st_size was %x, st_size should be %x\r\n"
// "<<< Failure in PreCalculateETI: section->Header->sh_size was %x, rel_size should be %x\r\n"
// "<<< Failure in PreCalculateETI: st_size was %x, st_size should be %x\r\n"
// "<<< Failure in %s: GetFilePos is %x, sect->calc_offset is %x\r\n"
// "<<< Failure in %s: GetFilePos is %x, sect->bin_offset is %x\r\n"

MWLinkerMap::Error MWLinkerMap::Read(  //
    const char* head, const char* const tail, std::size_t& line_number)
{
  if (head == nullptr || tail == nullptr || head > tail)
    return Error::Fail;

  std::cmatch match;
  DECLARE_DEBUG_STRING_VIEW;
  line_number = 0;

  // 1 Link map of <entry point>

  // ? Unresolved Symbols pre-print(?)

  // 1 Program's Symbol Closure
  // 2 EPPC_PatternMatching
  // 3 DWARF Symbol Closure

  // ? Unresolved Symbols post-print(?)

  // ? LinkerOpts

  // 1 Mixed Mode Islands
  // 2 Branch Islands
  // 3 Linktime size-decreasing optimizations
  // 4 Linktime size-increasing optimizations

  // 1 Section layouts
  // 2 Memory map
  // 3 Linker generated symbols

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
    goto MODIFIED_LINKER_MAPS_SKIP_TO_HERE;
  }
  if (std::regex_search(head, tail, match, re_section_layout_header_modified_b,
                        std::regex_constants::match_continuous))
  {
    line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;

    // Linker maps from Doubutsu no Mori + (foresta.map2 and static.map2) are modified similarly
    // to their counterparts in Doubutsu no Mori e+, though now with no preceding newlines. The
    // unmodified linker maps were also left on the disc, so maybe just use those instead?
    // Similarly modified linker maps:
    //   The Legend of Zelda - Ocarina of Time & Master Quest
    //   The Legend of Zelda - The Wind Waker (framework.map)
    auto portion = std::make_unique<SectionLayout>(match.str(1));
    const auto error = portion->Read(head, tail, line_number);
    UPDATE_DEBUG_STRING_VIEW;
    if (error != Error::None)
      return error;
    this->portions.push_back(std::move(portion));
    goto MODIFIED_LINKER_MAPS_SKIP_TO_HERE;
  }
  if (std::regex_search(head, tail, match, re_entry_point_name,
                        std::regex_constants::match_continuous))
  {
    // Normally this would always be present; its absence would be an early sign that the file is
    // not a MetroWerks linker map. However, I have decided to support certain modified linker maps
    // that are almost on-spec but are missing this portion, among other things.
    line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
    this->portions.push_back(std::make_unique<EntryPoint>(match.str(1)));  // TODO: emplace_back?
  }
  while (std::regex_search(head, tail, match, re_unresolved_symbol,
                           std::regex_constants::match_continuous))
  {
    line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
    this->unresolved_symbols.push_back(match.str(1));
    // TODO: min version if this is pre-printed
  }
  {
    auto portion = std::make_unique<SymbolClosure>();
    const auto error = portion->Read(head, tail, this->unresolved_symbols, line_number);
    UPDATE_DEBUG_STRING_VIEW;
    if (error != Error::None)
      return error;
    if (!portion->IsEmpty())
      this->portions.push_back(std::move(portion));
  }
  {
    auto portion = std::make_unique<EPPC_PatternMatching>();
    const auto error = portion->Read(head, tail, line_number);
    UPDATE_DEBUG_STRING_VIEW;
    if (error != Error::None)
      return error;
    if (!portion->IsEmpty())
      this->portions.push_back(std::move(portion));
  }
  {
    // With '-[no]listdwarf' and DWARF debugging information enabled, a second symbol closure
    // containing info about the .dwarf and .debug sections will appear. Note that, without an
    // EPPC_PatternMatching in the middle, this will blend into the prior symbol closure in the
    // eyes of this read function.
    auto portion = std::make_unique<SymbolClosure>();
    const auto error = portion->Read(head, tail, this->unresolved_symbols, line_number);
    UPDATE_DEBUG_STRING_VIEW;
    if (error != Error::None)
      return error;
    if (!portion->IsEmpty())
    {
      portion->SetMinVersion(MWLinkerVersion::version_3_0_4);
      this->portions.push_back(std::move(portion));
    }
  }
  // TODO: is this where SYMBOL NOT FOUND post-prints really go?  Double check Ghidra.
  // TODO: this might belong to EPPC_PatternMatching...
  while (std::regex_search(head, tail, match, re_unresolved_symbol,
                           std::regex_constants::match_continuous))
  {
    line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
    this->unresolved_symbols.push_back(match.str(1));
    // TODO: min version if this is post-printed
  }
  {
    auto portion = std::make_unique<LinkerOpts>();
    const auto error = portion->Read(head, tail, line_number);
    UPDATE_DEBUG_STRING_VIEW;
    if (error != Error::None)
      return error;
    if (!portion->IsEmpty())
      this->portions.push_back(std::move(portion));
  }
  if (std::regex_search(head, tail, match, re_mixed_mode_islands_header,
                        std::regex_constants::match_continuous))
  {
    line_number += 2, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
    auto portion = std::make_unique<MixedModeIslands>();
    const auto error = portion->Read(head, tail, line_number);
    UPDATE_DEBUG_STRING_VIEW;
    if (error != Error::None)
      return error;
    this->portions.push_back(std::move(portion));
  }
  if (std::regex_search(head, tail, match, re_branch_islands_header,
                        std::regex_constants::match_continuous))
  {
    line_number += 2, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
    auto portion = std::make_unique<BranchIslands>();
    const auto error = portion->Read(head, tail, line_number);
    UPDATE_DEBUG_STRING_VIEW;
    if (error != Error::None)
      return error;
    this->portions.push_back(std::move(portion));
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
MODIFIED_LINKER_MAPS_SKIP_TO_HERE:
  while (std::regex_search(head, tail, match, re_section_layout_header,
                           std::regex_constants::match_continuous))
  {
    line_number += 3, head += match.length(), UPDATE_DEBUG_STRING_VIEW;

    auto portion = std::make_unique<SectionLayout>(match.str(1));
    const auto error = portion->Read(head, tail, line_number);
    UPDATE_DEBUG_STRING_VIEW;
    if (error != Error::None)
      return error;
    this->portions.push_back(std::move(portion));
  }
  if (std::regex_search(head, tail, match, re_memory_map_header,
                        std::regex_constants::match_continuous))
  {
    line_number += 3, head += match.length(), UPDATE_DEBUG_STRING_VIEW;

    const auto error = this->ReadMemoryMapPrologue(head, tail, line_number);
    UPDATE_DEBUG_STRING_VIEW;
    if (error != Error::None)
      return error;
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
static const std::regex re_memory_map_simple_prologue_1_old{
//  "                   Starting Size     File\r\n"
    "                   Starting Size     File\r\n"};
static const std::regex re_memory_map_simple_prologue_2_old{
//  "                   address           Offset\r\n"
    "                   address           Offset\r\n"};
static const std::regex re_memory_map_romram_prologue_1_old{
//  "                   Starting Size     File     ROM      RAM Buffer\r\n"
    "                   Starting Size     File     ROM      RAM Buffer\r\n"};
static const std::regex re_memory_map_romram_prologue_2_old{
//  "                   address           Offset   Address  Address\r\n"
    "                   address           Offset   Address  Address\r\n"};
static const std::regex re_memory_map_simple_prologue_1{
//  "                       Starting Size     File\r\n"
    "                       Starting Size     File\r\n"};
static const std::regex re_memory_map_simple_prologue_2{
//  "                       address           Offset\r\n"
    "                       address           Offset\r\n"};
static const std::regex re_memory_map_simple_srecord_prologue_1{
//  "                       Starting Size     File       S-Record\r\n"
    "                       Starting Size     File       S-Record\r\n"};
static const std::regex re_memory_map_simple_srecord_prologue_2{
//  "                       address           Offset     Line\r\n"
    "                       address           Offset     Line\r\n"};
static const std::regex re_memory_map_romram_prologue_1{
//  "                       Starting Size     File     ROM      RAM Buffer\r\n"
    "                       Starting Size     File     ROM      RAM Buffer\r\n"};
static const std::regex re_memory_map_romram_prologue_2{
//  "                       address           Offset   Address  Address\r\n"
    "                       address           Offset   Address  Address\r\n"};
static const std::regex re_memory_map_romram_srecord_prologue_1{
//  "                       Starting Size     File     ROM      RAM Buffer  S-Record\r\n"
    "                       Starting Size     File     ROM      RAM Buffer  S-Record\r\n"};
static const std::regex re_memory_map_romram_srecord_prologue_2{
//  "                       address           Offset   Address  Address     Line\r\n"
    "                       address           Offset   Address  Address     Line\r\n"};
static const std::regex re_memory_map_binfile_prologue_1{
//  "                       Starting Size     File     Bin File Bin File\r\n"
    "                       Starting Size     File     Bin File Bin File\r\n"};
static const std::regex re_memory_map_binfile_prologue_2{
//  "                       address           Offset   Offset   Name\r\n"
    "                       address           Offset   Offset   Name\r\n"};
static const std::regex re_memory_map_binfile_srecord_prologue_1{
//  "                       Starting Size     File        S-Record Bin File Bin File\r\n"
    "                       Starting Size     File        S-Record Bin File Bin File\r\n"};
static const std::regex re_memory_map_binfile_srecord_prologue_2{
//  "                       address           Offset      Line     Offset   Name\r\n"
    "                       address           Offset      Line     Offset   Name\r\n"};
static const std::regex re_memory_map_romram_binfile_prologue_1{
//  "                       Starting Size     File     ROM      RAM Buffer Bin File Bin File\r\n"
    "                       Starting Size     File     ROM      RAM Buffer Bin File Bin File\r\n"};
static const std::regex re_memory_map_romram_binfile_prologue_2{
//  "                       address           Offset   Address  Address    Offset   Name\r\n"
    "                       address           Offset   Address  Address    Offset   Name\r\n"};
static const std::regex re_memory_map_romram_binfile_srecord_prologue_1{
//  "                       Starting Size     File     ROM      RAM Buffer    S-Record Bin File Bin File\r\n"
    "                       Starting Size     File     ROM      RAM Buffer    S-Record Bin File Bin File\r\n"};
static const std::regex re_memory_map_romram_binfile_srecord_prologue_2{
//  "                       address           Offset   Address  Address       Line     Offset   Name\r\n"
    "                       address           Offset   Address  Address       Line     Offset   Name\r\n"};
// clang-format on

MWLinkerMap::Error MWLinkerMap::ReadMemoryMapPrologue(  //
    const char*& head, const char* const tail, std::size_t& line_number)
{
  std::cmatch match;
  DECLARE_DEBUG_STRING_VIEW;

  if (std::regex_search(head, tail, match, re_memory_map_simple_prologue_1_old,
                        std::regex_constants::match_continuous))
  {
    line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
    if (std::regex_search(head, tail, match, re_memory_map_simple_prologue_2_old,
                          std::regex_constants::match_continuous))
    {
      line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
      auto portion = std::make_unique<MemoryMap>(false);
      const auto error = portion->ReadSimple_old(head, tail, line_number);
      UPDATE_DEBUG_STRING_VIEW;
      if (error != Error::None)
        return error;
      this->portions.push_back(std::move(portion));
    }
    else
    {
      return Error::MemoryMapBadPrologue;
    }
  }
  else if (std::regex_search(head, tail, match, re_memory_map_romram_prologue_1_old,
                             std::regex_constants::match_continuous))
  {
    line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
    if (std::regex_search(head, tail, match, re_memory_map_romram_prologue_2_old,
                          std::regex_constants::match_continuous))
    {
      line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
      auto portion = std::make_unique<MemoryMap>(true);
      const auto error = portion->ReadRomRam_old(head, tail, line_number);
      UPDATE_DEBUG_STRING_VIEW;
      if (error != Error::None)
        return error;
      this->portions.push_back(std::move(portion));
    }
    else
    {
      return Error::MemoryMapBadPrologue;
    }
  }
  else if (std::regex_search(head, tail, match, re_memory_map_simple_prologue_1,
                             std::regex_constants::match_continuous))
  {
    line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
    if (std::regex_search(head, tail, match, re_memory_map_simple_prologue_2,
                          std::regex_constants::match_continuous))
    {
      line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
      auto portion = std::make_unique<MemoryMap>(false, false, false);
      const auto error = portion->ReadSimple(head, tail, line_number);
      UPDATE_DEBUG_STRING_VIEW;
      if (error != Error::None)
        return error;
      this->portions.push_back(std::move(portion));
    }
    else
    {
      return Error::MemoryMapBadPrologue;
    }
  }
  else if (std::regex_search(head, tail, match, re_memory_map_simple_srecord_prologue_1,
                             std::regex_constants::match_continuous))
  {
    line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
    if (std::regex_search(head, tail, match, re_memory_map_simple_srecord_prologue_2,
                          std::regex_constants::match_continuous))
    {
      line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
      auto portion = std::make_unique<MemoryMap>(false, false, true);
      const auto error = portion->ReadSimpleSRecord(head, tail, line_number);
      UPDATE_DEBUG_STRING_VIEW;
      if (error != Error::None)
        return error;
      this->portions.push_back(std::move(portion));
    }
    else
    {
      return Error::MemoryMapBadPrologue;
    }
  }
  else if (std::regex_search(head, tail, match, re_memory_map_romram_prologue_1,
                             std::regex_constants::match_continuous))
  {
    line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
    if (std::regex_search(head, tail, match, re_memory_map_romram_prologue_2,
                          std::regex_constants::match_continuous))
    {
      line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
      auto portion = std::make_unique<MemoryMap>(true, false, false);
      const auto error = portion->ReadRomRam(head, tail, line_number);
      UPDATE_DEBUG_STRING_VIEW;
      if (error != Error::None)
        return error;
      this->portions.push_back(std::move(portion));
    }
    else
    {
      return Error::MemoryMapBadPrologue;
    }
  }
  else if (std::regex_search(head, tail, match, re_memory_map_romram_srecord_prologue_1,
                             std::regex_constants::match_continuous))
  {
    line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
    if (std::regex_search(head, tail, match, re_memory_map_romram_srecord_prologue_2,
                          std::regex_constants::match_continuous))
    {
      line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
      auto portion = std::make_unique<MemoryMap>(true, false, true);
      const auto error = portion->ReadRomRamSRecord(head, tail, line_number);
      UPDATE_DEBUG_STRING_VIEW;
      if (error != Error::None)
        return error;
      this->portions.push_back(std::move(portion));
    }
    else
    {
      return Error::MemoryMapBadPrologue;
    }
  }
  else if (std::regex_search(head, tail, match, re_memory_map_binfile_prologue_1,
                             std::regex_constants::match_continuous))
  {
    line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
    if (std::regex_search(head, tail, match, re_memory_map_binfile_prologue_2,
                          std::regex_constants::match_continuous))
    {
      line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
      auto portion = std::make_unique<MemoryMap>(false, true, false);
      const auto error = portion->ReadBinFile(head, tail, line_number);
      UPDATE_DEBUG_STRING_VIEW;
      if (error != Error::None)
        return error;
      this->portions.push_back(std::move(portion));
    }
    else
    {
      return Error::MemoryMapBadPrologue;
    }
  }
  else if (std::regex_search(head, tail, match, re_memory_map_binfile_srecord_prologue_1,
                             std::regex_constants::match_continuous))
  {
    line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
    if (std::regex_search(head, tail, match, re_memory_map_binfile_srecord_prologue_2,
                          std::regex_constants::match_continuous))
    {
      line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
      auto portion = std::make_unique<MemoryMap>(false, true, true);
      const auto error = portion->ReadBinFileSRecord(head, tail, line_number);
      UPDATE_DEBUG_STRING_VIEW;
      if (error != Error::None)
        return error;
      this->portions.push_back(std::move(portion));
    }
    else
    {
      return Error::MemoryMapBadPrologue;
    }
  }
  else if (std::regex_search(head, tail, match, re_memory_map_romram_binfile_prologue_1,
                             std::regex_constants::match_continuous))
  {
    line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
    if (std::regex_search(head, tail, match, re_memory_map_romram_binfile_prologue_2,
                          std::regex_constants::match_continuous))
    {
      line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
      auto portion = std::make_unique<MemoryMap>(true, true, false);
      const auto error = portion->ReadRomRamBinFile(head, tail, line_number);
      UPDATE_DEBUG_STRING_VIEW;
      if (error != Error::None)
        return error;
      this->portions.push_back(std::move(portion));
    }
    else
    {
      return Error::MemoryMapBadPrologue;
    }
  }
  else if (std::regex_search(head, tail, match, re_memory_map_romram_binfile_srecord_prologue_1,
                             std::regex_constants::match_continuous))
  {
    line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
    if (std::regex_search(head, tail, match, re_memory_map_romram_binfile_srecord_prologue_2,
                          std::regex_constants::match_continuous))
    {
      line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
      auto portion = std::make_unique<MemoryMap>(true, true, true);
      const auto error = portion->ReadRomRamBinFileSRecord(head, tail, line_number);
      UPDATE_DEBUG_STRING_VIEW;
      if (error != Error::None)
        return error;
      this->portions.push_back(std::move(portion));
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
  return Error::None;
}

// clang-format off
static const std::regex re_symbol_closure_node_normal{
//  "%i] " and "%s (%s,%s) found in %s %s\r\n"
    "   *(\\d+)\\] (.+) \\((.+),(.+)\\) found in (.+) (.*)\r\n"};
static const std::regex re_symbol_closure_node_normal_unref_dup_header{
//  "%i] " and ">>> UNREFERENCED DUPLICATE %s\r\n"
    "   *(\\d+)\\] >>> UNREFERENCED DUPLICATE (.+)\r\n"};
static const std::regex re_symbol_closure_node_normal_unref_dups{
//  "%i] " and ">>> (%s,%s) found in %s %s\r\n"
    "   *(\\d+)\\] >>> \\((.+),(.+)\\) found in (.+) (.*)\r\n"};
static const std::regex re_symbol_closure_node_linker_generated{
//  "%i] " and "%s found as linker generated symbol\r\n"
    "   *(\\d+)\\] (.+) found as linker generated symbol\r\n"};
// clang-format on

MWLinkerMap::Error MWLinkerMap::SymbolClosure::Read(  //
    const char*& head, const char* const tail, std::list<std::string>& unresolved_symbols,
    std::size_t& line_number)
{
  std::cmatch match;
  DECLARE_DEBUG_STRING_VIEW;

  NodeBase* curr_node = &this->root;
  unsigned long curr_hierarchy_level = 0;

  while (head < tail)
  {
    if (std::regex_search(head, tail, match, re_symbol_closure_node_normal,
                          std::regex_constants::match_continuous))
    {
      const unsigned long next_hierarchy_level = std::stoul(match.str(1));
      if (curr_hierarchy_level + 1 < next_hierarchy_level)
        return Error::SymbolClosureHierarchySkip;
      line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;

      auto next_node = std::make_unique<NodeNormal>(  //
          match.str(2), match.str(3), match.str(4), match.str(5), match.str(6));

      for (auto i = curr_hierarchy_level + 1; i > next_hierarchy_level; --i)
        curr_node = curr_node->parent;
      curr_hierarchy_level = next_hierarchy_level;

      if (std::regex_search(head, tail, match, re_symbol_closure_node_normal_unref_dup_header,
                            std::regex_constants::match_continuous))
      {
        if (std::stoul(match.str(1)) != curr_hierarchy_level)
          return Error::SymbolClosureUnrefDupsHierarchyMismatch;
        if (match.str(2) != next_node->name)
          return Error::SymbolClosureUnrefDupsNameMismatch;
        line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
        while (head < tail)
        {
          if (std::regex_search(head, tail, match, re_symbol_closure_node_normal_unref_dups,
                                std::regex_constants::match_continuous))
          {
            if (std::stoul(match.str(1)) != curr_hierarchy_level)
              return Error::SymbolClosureUnrefDupsHierarchyMismatch;
            line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
            next_node->unref_dups.emplace_back(  //
                match.str(2), match.str(3), match.str(4), match.str(5));
            continue;
          }
          break;
        }
        if (next_node->unref_dups.empty())
          return Error::SymbolClosureUnrefDupsEmpty;
        this->SetMinVersion(MWLinkerVersion::version_2_3_3_build_137);
      }

      next_node->parent = curr_node;
      if (const bool is_weird = (next_node->name == "_dtors$99" &&  // Redundancy out of paranoia
                                 next_node->module == "Linker Generated Symbol File");
          curr_node->children.push_back((curr_node = next_node.get(), std::move(next_node))),
          is_weird)  // Yo dawg, I herd you like operator comma.
      {
        // Though I do not understand it, the following is a normal occurrence for _dtors$99:
        // "  1] _dtors$99 (object,global) found in Linker Generated Symbol File "
        // "    3] .text (section,local) found in xyz.cpp lib.a"
        auto dummy_node = std::make_unique<NodeBase>();
        dummy_node->parent = curr_node;
        curr_node->children.push_back((curr_node = dummy_node.get(), std::move(dummy_node)));
        ++curr_hierarchy_level;
        this->SetMinVersion(MWLinkerVersion::version_3_0_4);
      }
      continue;
    }
    if (std::regex_search(head, tail, match, re_symbol_closure_node_linker_generated,
                          std::regex_constants::match_continuous))
    {
      const unsigned long next_hierarchy_level = std::stoul(match.str(1));
      if (curr_hierarchy_level + 1 < next_hierarchy_level)
        return Error::SymbolClosureHierarchySkip;
      line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;

      auto next_node = std::make_unique<NodeLinkerGenerated>(match.str(2));

      for (auto i = curr_hierarchy_level + 1; i > next_hierarchy_level; --i)
        curr_node = curr_node->parent;
      curr_hierarchy_level = next_hierarchy_level;

      next_node->parent = curr_node;
      curr_node->children.push_back((curr_node = next_node.get(), std::move(next_node)));
      continue;
    }
    if (std::regex_search(head, tail, match, re_unresolved_symbol,
                          std::regex_constants::match_continuous))
    {
      // Up until CW for GCN 3.0a3 (at the earliest), unresolved symbols were printed as the symbol
      // closure was being walked and printed itself. This gives a good idea of what function was
      // looking for that symbol, but because no hierarchy tier is given, it is impossible to be
      // certain without analyzing code.
      // TODO: min version if this is mid-printed
      do
      {
        line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
        unresolved_symbols.push_back(match.str(1));
      } while (std::regex_search(head, tail, match, re_unresolved_symbol,
                                 std::regex_constants::match_continuous));
      continue;
    }
    break;
  }
  return Error::None;
}

// clang-format off
static const std::regex re_code_merging_is_duplicated{
//  "--> duplicated code: symbol %s is duplicated by %s, size = %d \r\n\r\n"
    "--> duplicated code: symbol (.+) is duplicated by (.+), size = (\\d+) \r\n\r\n"};
static const std::regex re_code_merging_will_be_replaced{
//  "--> the function %s will be replaced by a branch to %s\r\n\r\n\r\n"
    "--> the function (.+) will be replaced by a branch to (.+)\r\n\r\n\r\n"};
static const std::regex re_code_merging_was_interchanged{
//  "--> the function %s was interchanged with %s, size=%d \r\n"
    "--> the function (.+) was interchanged with (.+), size=(\\d+) \r\n"};
static const std::regex re_code_folding_header{
//  "\r\n\r\n\r\nCode folded in file: %s \r\n"
    "\r\n\r\n\r\nCode folded in file: (.+) \r\n"};
static const std::regex re_code_folding_is_duplicated{
//  "--> %s is duplicated by %s, size = %d \r\n\r\n"
    "--> (.+) is duplicated by (.+), size = (\\d+) \r\n\r\n"};
static const std::regex re_code_folding_is_duplicated_new_branch{
//  "--> %s is duplicated by %s, size = %d, new branch function %s \r\n\r\n"
    "--> (.+) is duplicated by (.+), size = (\\d+), new branch function (.+) \r\n\r\n"};
// clang-format on

MWLinkerMap::Error MWLinkerMap::EPPC_PatternMatching::Read(  //
    const char*& head, const char* const tail, std::size_t& line_number)
{
  std::cmatch match;
  DECLARE_DEBUG_STRING_VIEW;

  std::string last;

  while (head < tail)
  {
    bool will_be_replaced = false;
    bool was_interchanged = false;
    if (std::regex_search(head, tail, match, re_code_merging_is_duplicated,
                          std::regex_constants::match_continuous))
    {
      line_number += 2, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
      std::string first_name = match.str(1);
      std::string second_name = match.str(2);
      const std::uint32_t size = std::stoul(match.str(3));
      if (std::regex_search(head, tail, match, re_code_merging_will_be_replaced,
                            std::regex_constants::match_continuous))
      {
        if (match.str(1) != first_name)
          return Error::EPPC_PatternMatchingMergingFirstNameMismatch;
        if (match.str(2) != second_name)
          return Error::EPPC_PatternMatchingMergingSecondNameMismatch;
        line_number += 3, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
        will_be_replaced = true;
      }
      this->merging_units.emplace_back(  //
          std::move(first_name), std::move(second_name), size, will_be_replaced, was_interchanged);
      continue;
    }
    if (std::regex_search(head, tail, match, re_code_merging_was_interchanged,
                          std::regex_constants::match_continuous))
    {
      std::string first_name = match.str(1);
      std::string second_name = match.str(2);
      const std::uint32_t size = std::stoul(match.str(3));
      was_interchanged = true;

      line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
      if (std::regex_search(head, tail, match, re_code_merging_will_be_replaced,
                            std::regex_constants::match_continuous))
      {
        if (match.str(1) != first_name)
          return Error::EPPC_PatternMatchingMergingFirstNameMismatch;
        if (match.str(2) != second_name)
          return Error::EPPC_PatternMatchingMergingSecondNameMismatch;
        line_number += 3, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
        will_be_replaced = true;
      }
      if (std::regex_search(head, tail, match, re_code_merging_is_duplicated))
      {
        if (match.str(1) != first_name)
          return Error::EPPC_PatternMatchingMergingFirstNameMismatch;
        if (match.str(2) != second_name)
          return Error::EPPC_PatternMatchingMergingSecondNameMismatch;
        if (std::stoul(match.str(3)) != size)
          return Error::EPPC_PatternMatchingMergingSizeMismatch;
        line_number += 2, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
      }
      else
      {
        return Error::EPPC_PatternMatchingMergingInterchangeMissingEpilogue;
      }
      this->merging_units.emplace_back(  //
          std::move(first_name), std::move(second_name), size, will_be_replaced, was_interchanged);
      continue;
    }
    break;
  }
  while (head < tail)
  {
    if (std::regex_search(head, tail, match, re_code_folding_header,
                          std::regex_constants::match_continuous))
    {
      auto folding_unit = FoldingUnit(match.str(1));
      line_number += 4, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
      while (head < tail)
      {
        if (std::regex_search(head, tail, match, re_code_folding_is_duplicated,
                              std::regex_constants::match_continuous))
        {
          line_number += 2, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
          folding_unit.units.emplace_back(  //
              match.str(1), match.str(2), std::stoul(match.str(3)), false);
          continue;
        }
        if (std::regex_search(head, tail, match, re_code_folding_is_duplicated_new_branch,
                              std::regex_constants::match_continuous))
        {
          if (match.str(1) != match.str(4))  // It is my assumption that they will always match
            return Error::EPPC_PatternMatchingFoldingNewBranchFunctionNameMismatch;
          line_number += 2, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
          folding_unit.units.emplace_back(  //
              match.str(1), match.str(2), std::stoul(match.str(3)), true);
          continue;
        }
        break;
      }
      this->folding_units.push_back(std::move(folding_unit));
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

MWLinkerMap::Error MWLinkerMap::LinkerOpts::Read(  //
    const char*& head, const char* const tail, std::size_t& line_number)

{
  std::cmatch match;
  DECLARE_DEBUG_STRING_VIEW;

  while (head < tail)
  {
    if (std::regex_search(head, tail, match, re_linker_opts_unit_address_range,
                          std::regex_constants::match_continuous))
    {
      line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
      this->units.push_back(  //
          std::make_unique<UnitNotNear>(match.str(1), match.str(2), match.str(3)));
      continue;
    }
    if (std::regex_search(head, tail, match, re_linker_opts_unit_disassemble_error,
                          std::regex_constants::match_continuous))
    {
      line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
      this->units.push_back(  //
          std::make_unique<UnitDisassembleError>(match.str(1), match.str(2)));
      continue;
    }
    if (std::regex_search(head, tail, match, re_linker_opts_unit_address_not_computed,
                          std::regex_constants::match_continuous))
    {
      line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
      this->units.push_back(  //
          std::make_unique<UnitNotComputed>(match.str(1), match.str(2), match.str(3)));
      continue;
    }
    if (std::regex_search(head, tail, match, re_linker_opts_unit_address_optimize,
                          std::regex_constants::match_continuous))
    {
      // I have not seen a single linker map with this
      line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
      this->units.push_back(  //
          std::make_unique<UnitOptimized>(match.str(1), match.str(2), match.str(3)));
      continue;
    }
    break;
  }
  return Error::None;
}

// clang-format off
static const std::regex re_branch_islands_created{
//  "  branch island %s created for %s\r\n"
    "  branch island (.+) created for (.+)\r\n"};
static const std::regex re_branch_islands_created_safe{
//  "  safe branch island %s created for %s\r\n"
    "  safe branch island (.+) created for (.+)\r\n"};
// clang-format on

MWLinkerMap::Error MWLinkerMap::BranchIslands::Read(  //
    const char*& head, const char* const tail, std::size_t& line_number)
{
  // TODO: I have only ever seen Branch Islands from Skylanders Swap Force, and on top of that, it
  // was an empty portion. From datamining MWLDEPPC, I can only assume it goes something like this.

  std::cmatch match;
  DECLARE_DEBUG_STRING_VIEW;

  while (head < tail)
  {
    if (std::regex_search(head, tail, match, re_branch_islands_created,
                          std::regex_constants::match_continuous))
    {
      line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
      this->units.emplace_back(match.str(1), match.str(2), false);
      continue;
    }
    if (std::regex_search(head, tail, match, re_branch_islands_created_safe,
                          std::regex_constants::match_continuous))
    {
      line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
      this->units.emplace_back(match.str(1), match.str(2), true);
      continue;
    }
    break;
  }
  return Error::None;
}

// clang-format off
static const std::regex re_mixed_mode_islands_created{
//  "  mixed mode island %s created for %s\r\n"
    "  mixed mode island (.+) created for (.+)\r\n"};
static const std::regex re_mixed_mode_islands_created_safe{
//  "  safe mixed mode island %s created for %s\r\n"
    "  safe mixed mode island (.+) created for (.+)\r\n"};
// clang-format on

MWLinkerMap::Error MWLinkerMap::MixedModeIslands::Read(  //
    const char*& head, const char* const tail, std::size_t& line_number)
{
  // TODO: I have literally never seen Mixed Mode Islands.
  // Similar to Branch Islands, this is conjecture.

  std::cmatch match;
  DECLARE_DEBUG_STRING_VIEW;

  while (head < tail)
  {
    if (std::regex_search(head, tail, match, re_mixed_mode_islands_created,
                          std::regex_constants::match_continuous))
    {
      line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
      continue;
    }
    if (std::regex_search(head, tail, match, re_mixed_mode_islands_created_safe,
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

MWLinkerMap::Error MWLinkerMap::SectionLayout::Read(  //
    const char*& head, const char* const tail, std::size_t& line_number)
{
  std::cmatch match;
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

MWLinkerMap::Error MWLinkerMap::SectionLayout::Read3Column(  //
    const char*& head, const char* const tail, std::size_t& line_number)
{
  std::cmatch match;
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

MWLinkerMap::Error MWLinkerMap::SectionLayout::Read4Column(  //
    const char*& head, const char* const tail, std::size_t& line_number)
{
  std::cmatch match;
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
static const std::regex re_memory_map_unit_normal_simple_old{
//  "  %15s  %08x %08x %08x\r\n"
    "   {0,15}(.*)  ([0-9a-f]{8}) ([0-9a-f]{8}) ([0-9a-f]{8})\r\n"};
static const std::regex re_memory_map_unit_debug_old{
//  "  %15s           %06x %08x\r\n" <-- Sometimes the size can overflow six digits
//  "  %15s           %08x %08x\r\n" <-- Starting with CW for GCN 2.7
    "   {0,15}(.*)           ([0-9a-f]{6,8}) ([0-9a-f]{8})\r\n"};
// clang-format on

MWLinkerMap::Error MWLinkerMap::MemoryMap::ReadSimple_old(  //
    const char*& head, const char* const tail, std::size_t& line_number)
{
  std::cmatch match;
  DECLARE_DEBUG_STRING_VIEW;

  while (std::regex_search(head, tail, match, re_memory_map_unit_normal_simple_old,
                           std::regex_constants::match_continuous))
  {
    line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
    this->normal_units.emplace_back(match.str(1), xstoul(match.str(2)), xstoul(match.str(3)),
                                    xstoul(match.str(4)));
  }
  while (std::regex_search(head, tail, match, re_memory_map_unit_debug_old,
                           std::regex_constants::match_continuous))
  {
    line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
    this->debug_units.emplace_back(match.str(1), xstoul(match.str(2)), xstoul(match.str(3)));
  }
  return Error::None;
}

// clang-format off
static const std::regex re_memory_map_unit_normal_romram_old{
//  "  %15s  %08x %08x %08x %08x %08x\r\n"
    "   {0,15}(.*)  ([0-9a-f]{8}) ([0-9a-f]{8}) ([0-9a-f]{8})\r\n"};
// clang-format on

MWLinkerMap::Error MWLinkerMap::MemoryMap::ReadRomRam_old(  //
    const char*& head, const char* const tail, std::size_t& line_number)
{
  std::cmatch match;
  DECLARE_DEBUG_STRING_VIEW;

  while (std::regex_search(head, tail, match, re_memory_map_unit_normal_romram_old,
                           std::regex_constants::match_continuous))
  {
    line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
    this->normal_units.emplace_back(match.str(1), xstoul(match.str(2)), xstoul(match.str(3)),
                                    xstoul(match.str(4)));
  }
  while (std::regex_search(head, tail, match, re_memory_map_unit_debug_old,
                           std::regex_constants::match_continuous))
  {
    line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
    this->debug_units.emplace_back(match.str(1), xstoul(match.str(2)), xstoul(match.str(3)));
  }
  return Error::None;
}

// clang-format off
static const std::regex re_memory_map_unit_normal_simple{
//  "  %20s %08x %08x %08x\r\n"
    "   {0,20}(.*) ([0-9a-f]{8}) ([0-9a-f]{8}) ([0-9a-f]{8})\r\n"};
static const std::regex re_memory_map_unit_debug{
//  "  %20s          %08x %08x\r\n"
    "   {0,20}(.*)          ([0-9a-f]{8}) ([0-9a-f]{8})\r\n"};
// clang-format on

MWLinkerMap::Error MWLinkerMap::MemoryMap::ReadSimple(  //
    const char*& head, const char* const tail, std::size_t& line_number)
{
  std::cmatch match;
  DECLARE_DEBUG_STRING_VIEW;

  while (std::regex_search(head, tail, match, re_memory_map_unit_normal_simple,
                           std::regex_constants::match_continuous))
  {
    line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
    this->normal_units.emplace_back(match.str(1), xstoul(match.str(2)), xstoul(match.str(3)),
                                    xstoul(match.str(4)));
  }
  while (std::regex_search(head, tail, match, re_memory_map_unit_debug,
                           std::regex_constants::match_continuous))
  {
    line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
    this->debug_units.emplace_back(match.str(1), xstoul(match.str(2)), xstoul(match.str(3)));
  }
  return Error::None;
}

// clang-format off
static const std::regex re_memory_map_unit_normal_simple_srecord{
//  "  %20s %08x %08x %08x %10i\r\n"
    "   {0,20}(.*) ([0-9a-f]{8}) ([0-9a-f]{8}) ([0-9a-f]{8})  {0,9}(\\d+)\r\n"};
// clang-format on

MWLinkerMap::Error MWLinkerMap::MemoryMap::ReadSimpleSRecord(  //
    const char*& head, const char* const tail, std::size_t& line_number)
{
  std::cmatch match;
  DECLARE_DEBUG_STRING_VIEW;

  while (std::regex_search(head, tail, match, re_memory_map_unit_normal_simple_srecord,
                           std::regex_constants::match_continuous))
  {
    line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
    this->normal_units.emplace_back(match.str(1), xstoul(match.str(2)), xstoul(match.str(3)),
                                    xstoul(match.str(4)), std::stoi(match.str(5)));
  }
  while (std::regex_search(head, tail, match, re_memory_map_unit_debug,
                           std::regex_constants::match_continuous))
  {
    line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
    this->debug_units.emplace_back(match.str(1), xstoul(match.str(2)), xstoul(match.str(3)));
  }
  return Error::None;
}

// clang-format off
static const std::regex re_memory_map_unit_normal_romram{
//  "  %20s %08x %08x %08x %08x %08x\r\n"
    "   {0,20}(.*) ([0-9a-f]{8}) ([0-9a-f]{8}) ([0-9a-f]{8}) ([0-9a-f]{8}) ([0-9a-f]{8})\r\n"};
// clang-format on

MWLinkerMap::Error MWLinkerMap::MemoryMap::ReadRomRam(  //
    const char*& head, const char* const tail, std::size_t& line_number)
{
  std::cmatch match;
  DECLARE_DEBUG_STRING_VIEW;

  while (std::regex_search(head, tail, match, re_memory_map_unit_normal_romram,
                           std::regex_constants::match_continuous))
  {
    line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
    this->normal_units.emplace_back(  //
        match.str(1), xstoul(match.str(2)), xstoul(match.str(3)), xstoul(match.str(4)),
        xstoul(match.str(5)), xstoul(match.str(6)));
  }
  while (std::regex_search(head, tail, match, re_memory_map_unit_debug,
                           std::regex_constants::match_continuous))
  {
    line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
    this->debug_units.emplace_back(match.str(1), xstoul(match.str(2)), xstoul(match.str(3)));
  }
  return Error::None;
}

// clang-format off
static const std::regex re_memory_map_unit_normal_romram_srecord{
//  "  %20s %08x %08x %08x %08x %08x %10i\r\n"
    "   {0,20}(.*) ([0-9a-f]{8}) ([0-9a-f]{8}) ([0-9a-f]{8}) ([0-9a-f]{8}) ([0-9a-f]{8})  {0,9}(\\d+)\r\n"};
// clang-format on

MWLinkerMap::Error MWLinkerMap::MemoryMap::ReadRomRamSRecord(  //
    const char*& head, const char* const tail, std::size_t& line_number)
{
  std::cmatch match;
  DECLARE_DEBUG_STRING_VIEW;

  while (std::regex_search(head, tail, match, re_memory_map_unit_normal_romram_srecord,
                           std::regex_constants::match_continuous))
  {
    line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
    this->normal_units.emplace_back(  //
        match.str(1), xstoul(match.str(2)), xstoul(match.str(3)), xstoul(match.str(4)),
        xstoul(match.str(5)), xstoul(match.str(6)), std::stoi(match.str(7)));
  }
  while (std::regex_search(head, tail, match, re_memory_map_unit_debug,
                           std::regex_constants::match_continuous))
  {
    line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
    this->debug_units.emplace_back(match.str(1), xstoul(match.str(2)), xstoul(match.str(3)));
  }
  return Error::None;
}

// clang-format off
static const std::regex re_memory_map_unit_normal_binfile{
//  "  %20s %08x %08x %08x %08x %s\r\n"
    "   {0,20}(.*) ([0-9a-f]{8}) ([0-9a-f]{8}) ([0-9a-f]{8}) ([0-9a-f]{8}) (.+)\r\n"};
// clang-format on

MWLinkerMap::Error MWLinkerMap::MemoryMap::ReadBinFile(  //
    const char*& head, const char* const tail, std::size_t& line_number)
{
  std::cmatch match;
  DECLARE_DEBUG_STRING_VIEW;

  while (std::regex_search(head, tail, match, re_memory_map_unit_normal_binfile,
                           std::regex_constants::match_continuous))
  {
    line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
    this->normal_units.emplace_back(match.str(1), xstoul(match.str(2)), xstoul(match.str(3)),
                                    xstoul(match.str(4)), xstoul(match.str(5)), match.str(6));
  }
  while (std::regex_search(head, tail, match, re_memory_map_unit_debug,
                           std::regex_constants::match_continuous))
  {
    line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
    this->debug_units.emplace_back(match.str(1), xstoul(match.str(2)), xstoul(match.str(3)));
  }
  return Error::None;
}

// clang-format off
static const std::regex re_memory_map_unit_normal_binfile_srecord{
//  "  %20s %08x %08x %08x  %10i %08x %s\r\n"
    "   {0,20}(.*) ([0-9a-f]{8}) ([0-9a-f]{8}) ([0-9a-f]{8})   {0,9}(\\d+) ([0-9a-f]{8}) (.+)\r\n"};
// clang-format on

MWLinkerMap::Error MWLinkerMap::MemoryMap::ReadBinFileSRecord(  //
    const char*& head, const char* const tail, std::size_t& line_number)
{
  std::cmatch match;
  DECLARE_DEBUG_STRING_VIEW;

  while (std::regex_search(head, tail, match, re_memory_map_unit_normal_binfile_srecord,
                           std::regex_constants::match_continuous))
  {
    line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
    this->normal_units.emplace_back(  //
        match.str(1), xstoul(match.str(2)), xstoul(match.str(3)), xstoul(match.str(4)),
        std::stoi(match.str(5)), xstoul(match.str(6)), match.str(7));
  }
  while (std::regex_search(head, tail, match, re_memory_map_unit_debug,
                           std::regex_constants::match_continuous))
  {
    line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
    this->debug_units.emplace_back(match.str(1), xstoul(match.str(2)), xstoul(match.str(3)));
  }
  return Error::None;
}

// clang-format off
static const std::regex re_memory_map_unit_normal_romram_binfile{
//  "  %20s %08x %08x %08x %08x %08x   %08x %s\r\n"
    "   {0,20}(.*) ([0-9a-f]{8}) ([0-9a-f]{8}) ([0-9a-f]{8}) ([0-9a-f]{8}) ([0-9a-f]{8})   ([0-9a-f]{8}) (.+)\r\n"};
// clang-format on

MWLinkerMap::Error MWLinkerMap::MemoryMap::ReadRomRamBinFile(  //
    const char*& head, const char* const tail, std::size_t& line_number)
{
  std::cmatch match;
  DECLARE_DEBUG_STRING_VIEW;

  while (std::regex_search(head, tail, match, re_memory_map_unit_normal_romram_binfile,
                           std::regex_constants::match_continuous))
  {
    line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
    this->normal_units.emplace_back(  //
        match.str(1), xstoul(match.str(2)), xstoul(match.str(3)), xstoul(match.str(4)),
        xstoul(match.str(5)), xstoul(match.str(6)), xstoul(match.str(7)), match.str(8));
  }
  while (std::regex_search(head, tail, match, re_memory_map_unit_debug,
                           std::regex_constants::match_continuous))
  {
    line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
    this->debug_units.emplace_back(match.str(1), xstoul(match.str(2)), xstoul(match.str(3)));
  }
  return Error::None;
}

// clang-format off
static const std::regex re_memory_map_unit_normal_romram_binfile_srecord{
//  "  %20s %08x %08x %08x %08x %08x    %10i %08x %s\r\n"
    "   {0,20}(.*) ([0-9a-f]{8}) ([0-9a-f]{8}) ([0-9a-f]{8}) ([0-9a-f]{8}) ([0-9a-f]{8})     {0,9}(\\d+) ([0-9a-f]{8}) (.+)\r\n"};
// clang-format on

MWLinkerMap::Error MWLinkerMap::MemoryMap::ReadRomRamBinFileSRecord(  //
    const char*& head, const char* const tail, std::size_t& line_number)
{
  std::cmatch match;
  DECLARE_DEBUG_STRING_VIEW;

  while (std::regex_search(head, tail, match, re_memory_map_unit_normal_romram_binfile_srecord,
                           std::regex_constants::match_continuous))
  {
    line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
    this->normal_units.emplace_back(  //
        match.str(1), xstoul(match.str(2)), xstoul(match.str(3)), xstoul(match.str(4)),
        xstoul(match.str(5)), xstoul(match.str(6)), std::stoi(match.str(7)), xstoul(match.str(8)),
        match.str(9));
  }
  while (std::regex_search(head, tail, match, re_memory_map_unit_debug,
                           std::regex_constants::match_continuous))
  {
    line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
    this->debug_units.emplace_back(match.str(1), xstoul(match.str(2)), xstoul(match.str(3)));
  }
  return Error::None;
}

// clang-format off
static const std::regex re_linker_generated_symbols_unit{
//  "%25s %08x\r\n"
    " *(.+) ([0-9a-f]{8})\r\n"};
// clang-format on

MWLinkerMap::Error MWLinkerMap::LinkerGeneratedSymbols::Read(  //
    const char*& head, const char* const tail, std::size_t& line_number)
{
  std::cmatch match;
  DECLARE_DEBUG_STRING_VIEW;

  while (head < tail)
  {
    if (std::regex_search(head, tail, match, re_linker_generated_symbols_unit,
                          std::regex_constants::match_continuous))
    {
      line_number += 1, head += match.length(), UPDATE_DEBUG_STRING_VIEW;
      this->units.push_back(std::make_unique<Unit>(match.str(1), xstoul(match.str(2))));
      continue;
    }
    break;
  }
  return Error::None;
}
