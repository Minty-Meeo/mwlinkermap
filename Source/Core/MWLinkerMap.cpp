#include "MWLinkerMap.h"

#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <istream>
#include <map>
#include <ostream>
#include <ranges>
#include <regex>
#include <sstream>
#include <string>
#include <string_view>
#include <typeinfo>
#include <utility>

#include "Future/CppLibPrint.h"

#ifdef DOLPHIN  // Dolphin Emulator
#include "Common/Assert.h"
#include "Common/CommonTypes.h"
#else  // mwlinkermap-temp
#include <cassert>
#define ASSERT assert
#define DEBUG_ASSERT assert
using u32 = std::uint32_t;
#endif

#define xstoul(__s) std::stoul(__s, nullptr, 16)

// https://lists.isocpp.org/std-proposals/att-0008/Dxxxx_string_view_support_for_regex.pdf
// This version assumes the submatch is valid.  Don't give me an invalid submatch!
constexpr static std::string_view ToStringView(const std::csub_match& submatch)
{
  return std::string_view{submatch.first, submatch.second};
}

// Metrowerks linker maps should be considered binary files containing text with CRLF line endings.
// To account for outside factors, though, this program can support both CRLF and LF line endings.

namespace MWLinker
{
void Map::EPPC_PatternMatching::Warn::MergingOneDefinitionRuleViolation(
    const std::size_t line_number, const std::string_view symbol_name)
{
  // Could be a false positive, as code merging has no information about where the symbol came from.
  if (!do_warn_merging_odr_violation)
    return;
  std::println(std::cerr, "Line {:d}] \"{:s}\" seen again", line_number, symbol_name);
}

void Map::EPPC_PatternMatching::Warn::FoldingRepeatObject(const std::size_t line_number,
                                                          const std::string_view object_name)
{
  // This warning is pretty much the only one guaranteed to not produce false positives.
  if (!do_warn_folding_repeat_object)
    std::println(std::cerr, "Line {:d}] Detected repeat-name object \"{:s}\"", line_number,
                 object_name);
}

void Map::EPPC_PatternMatching::Warn::FoldingOneDefinitionRuleViolation(
    const std::size_t line_number, const std::string_view symbol_name,
    const std::string_view object_name)
{
  // For legal linker maps, this should only ever happen in repeat-name objects.
  if (!do_warn_folding_odr_violation)
    return;
  std::println(std::cerr, "Line {:d}] \"{:s}\" seen again in \"{:s}\"", line_number, symbol_name,
               object_name);
}

void Map::SymbolClosure::Warn::OneDefinitionRuleViolation(
    const std::size_t line_number, const std::string_view symbol_name,
    const std::string_view compilation_unit_name)
{
  // For legal linker maps, this should only ever happen in repeat-name compilation units.
  if (!do_warn_odr_violation)
    return;
  std::println(std::cerr, "Line {:d}] \"{:s}\" seen again in \"{:s}\"", line_number, symbol_name,
               compilation_unit_name);
}

void Map::SymbolClosure::Warn::SymOnFlagDetected(const std::size_t line_number,
                                                 const std::string_view compilation_unit_name)
{
  // Multiple STT_SECTION symbols were seen in an uninterrupted compilation unit.  This could be
  // a false positive, and in turn would be a false negative for a RepeatCompilationUnit warning.
  if (!do_warn_sym_on_flag_detected)
    return;
  std::println(std::cerr, "Line {:d}] Detected '-sym on' flag in \"{:s}\" (.text)", line_number,
               compilation_unit_name);
}

void Map::SectionLayout::Warn::RepeatCompilationUnit(const std::size_t line_number,
                                                     const std::string_view compilation_unit_name,
                                                     const std::string_view section_name)
{
  if (!do_warn_repeat_compilation_unit)
    return;
  std::println(std::cerr, "Line {:d}] Detected repeat-name compilation unit \"{:s}\" ({:s})",
               line_number, compilation_unit_name, section_name);
}

void Map::SectionLayout::Warn::OneDefinitionRuleViolation(
    const std::size_t line_number, const std::string_view symbol_name,
    const std::string_view compilation_unit_name, const std::string_view section_name)
{
  // For legal linker maps, this should only ever happen in repeat-name compilation units.
  if (!do_warn_odr_violation)
    return;
  std::println(std::cerr, "Line {:d}] \"{:s}\" seen again in \"{:s}\" ({:s})", line_number,
               symbol_name, compilation_unit_name, section_name);
}

void Map::SectionLayout::Warn::SymOnFlagDetected(const std::size_t line_number,
                                                 const std::string_view compilation_unit_name,
                                                 const std::string_view section_name)
{
  // Multiple STT_SECTION symbols were seen in an uninterrupted compilation unit.  This could be
  // a false positive, and in turn would be a false negative for a RepeatCompilationUnit warning.
  if (!do_warn_sym_on_flag_detected)
    return;
  std::println(std::cerr, "Line {:d}] Detected '-sym on' flag in \"{:s}\" ({:s})", line_number,
               compilation_unit_name, section_name);
}

void Map::SectionLayout::Warn::CommAfterLComm(const std::size_t line_number)
{
  // Shouldn't this be impossible?
  if (!do_warn_comm_after_lcomm)
    return;
  std::println(std::cerr, "Line {:d}] .comm symbols found after .lcomm symbols.", line_number);
}

// clang-format off
static const std::regex re_entry_point_name{
//  "Link map of %s\r\n"
    "Link map of (.*)\r?\n"};
static const std::regex re_unresolved_symbol{
//  ">>> SYMBOL NOT FOUND: %s\r\n"
    ">>> SYMBOL NOT FOUND: (.*)\r?\n"};
static const std::regex re_mixed_mode_islands_header{
//  "\r\nMixed Mode Islands\r\n"
    "\r?\nMixed Mode Islands\r?\n"};
static const std::regex re_branch_islands_header{
//  "\r\nBranch Islands\r\n"
    "\r?\nBranch Islands\r?\n"};
static const std::regex re_linktime_size_decreasing_optimizations_header{
//  "\r\nLinktime size-decreasing optimizations\r\n"
    "\r?\nLinktime size-decreasing optimizations\r?\n"};
static const std::regex re_linktime_size_increasing_optimizations_header{
//  "\r\nLinktime size-increasing optimizations\r\n"
    "\r?\nLinktime size-increasing optimizations\r?\n"};
static const std::regex re_section_layout_header{
//  "\r\n\r\n%s section layout\r\n"
    "\r?\n\r?\n(.*) section layout\r?\n"};
static const std::regex re_section_layout_header_modified_a{
    "\r?\n(.*) section layout\r?\n"};
static const std::regex re_section_layout_header_modified_b{
    "(.*) section layout\r?\n"};
static const std::regex re_memory_map_header{
//  "\r\n\r\nMemory map:\r\n"
    "\r?\n\r?\nMemory map:\r?\n"};
static const std::regex re_linker_generated_symbols_header{
//  "\r\n\r\nLinker generated symbols:\r\n"
    "\r?\n\r?\nLinker generated symbols:\r?\n"};
// clang-format on

// Other linker map prints are known to exist, but have never been seen. These include:
// ">>> EXCLUDED SYMBOL %s (%s,%s) found in %s %s\r\n"
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

static const std::map<std::string_view, Map::SectionLayout::Kind> map_section_layout_kind{
    {".bss", Map::SectionLayout::Kind::BSS},
    {".sbss", Map::SectionLayout::Kind::BSS},
    {".sbss2", Map::SectionLayout::Kind::BSS},
    {".ctors", Map::SectionLayout::Kind::Ctors},
    {".dtors", Map::SectionLayout::Kind::Dtors},
    {"extab", Map::SectionLayout::Kind::ExTab},
    {"extabindex", Map::SectionLayout::Kind::ExTabIndex},
};

Map::SectionLayout::Kind Map::SectionLayout::ToSectionKind(const std::string_view section_name)
{
  if (map_section_layout_kind.contains(section_name))
    return map_section_layout_kind.at(section_name);
  else
    return Map::SectionLayout::Kind::Normal;
}

Map::Error Map::Scan(const std::string_view string_view, std::size_t& line_number)
{
  return this->Scan(string_view.data(), string_view.data() + string_view.size(), line_number);
}

Map::Error Map::Scan(const char* head, const char* const tail, std::size_t& line_number)
{
  if (head == nullptr || tail == nullptr || head > tail)
    return Error::Fail;

  std::cmatch match;
  line_number = 1u;

  // Linker maps from Animal Crossing (foresta.map and static.map) and Doubutsu no Mori e+
  // (foresta.map, forestd.map, foresti.map, foresto.map, and static.map) appear to have been
  // modified to strip out the Link Map portion and UNUSED symbols, though the way it was done
  // also removed one of the Section Layout header's preceding newlines.
  if (std::regex_search(head, tail, match, re_section_layout_header_modified_a,
                        std::regex_constants::match_continuous))
  {
    line_number += 2u;
    head += match.length();
    const auto error = this->ScanPrologue_SectionLayout(head, tail, line_number, match.str(1));
    if (error != Error::None)
      return error;
    goto NINTENDO_EAD_TRIMMED_LINKER_MAPS_GOTO_HERE;
  }
  // Linker maps from Doubutsu no Mori + (foresta.map2 and static.map2) are modified similarly
  // to their counterparts in Doubutsu no Mori e+, though now with no preceding newlines. The
  // unmodified linker maps were also left on the disc, so maybe just use those instead?
  // Similarly modified linker maps:
  //   The Legend of Zelda - Ocarina of Time & Master Quest
  //   The Legend of Zelda - The Wind Waker (framework.map)
  if (std::regex_search(head, tail, match, re_section_layout_header_modified_b,
                        std::regex_constants::match_continuous))
  {
    line_number += 1u;
    head += match.length();
    const auto error = this->ScanPrologue_SectionLayout(head, tail, line_number, match.str(1));
    if (error != Error::None)
      return error;
    goto NINTENDO_EAD_TRIMMED_LINKER_MAPS_GOTO_HERE;
  }
  if (std::regex_search(head, tail, match, re_entry_point_name,
                        std::regex_constants::match_continuous))
  {
    line_number += 1u;
    head += match.length();
    this->entry_point_name = match.str(1);
  }
  else
  {
    // If this is not present, the file must not be a Metrowerks linker map.
    return Error::EntryPointNameMissing;
  }
  {
    auto portion = std::make_unique<SymbolClosure>();
    const auto error = portion->Scan(head, tail, line_number, this->unresolved_symbols);
    if (error != Error::None)
      return error;
    if (!portion->IsEmpty())
      this->normal_symbol_closure = std::move(portion);
  }
  {
    auto portion = std::make_unique<EPPC_PatternMatching>();
    const auto error = portion->Scan(head, tail, line_number);
    if (error != Error::None)
      return error;
    if (!portion->IsEmpty())
      this->eppc_pattern_matching = std::move(portion);
  }
  // With '-listdwarf' and DWARF debugging information enabled, a second symbol closure
  // containing info about the .dwarf and .debug sections will appear. Note that, without an
  // EPPC_PatternMatching in the middle, this will blend into the prior symbol closure in the
  // eyes of this scan function.
  {
    auto portion = std::make_unique<SymbolClosure>();
    const auto error = portion->Scan(head, tail, line_number, this->unresolved_symbols);
    if (error != Error::None)
      return error;
    if (!portion->IsEmpty())
    {
      portion->SetMinVersion(Version::version_3_0_4);
      this->dwarf_symbol_closure = std::move(portion);
    }
  }
  // Unresolved symbol post-prints probably belong here (I have not confirmed if they preceed
  // LinkerOpts), but the Symbol Closure scanning code that just happened handles them well enough.
  {
    auto portion = std::make_unique<LinkerOpts>();
    const auto error = portion->Scan(head, tail, line_number);
    if (error != Error::None)
      return error;
    if (!portion->IsEmpty())
      this->linker_opts = std::move(portion);
  }
  if (std::regex_search(head, tail, match, re_mixed_mode_islands_header,
                        std::regex_constants::match_continuous))
  {
    line_number += 2u;
    head += match.length();
    auto portion = std::make_unique<MixedModeIslands>();
    const auto error = portion->Scan(head, tail, line_number);
    if (error != Error::None)
      return error;
    this->mixed_mode_islands = std::move(portion);
  }
  if (std::regex_search(head, tail, match, re_branch_islands_header,
                        std::regex_constants::match_continuous))
  {
    line_number += 2u;
    head += match.length();
    auto portion = std::make_unique<BranchIslands>();
    const auto error = portion->Scan(head, tail, line_number);
    if (error != Error::None)
      return error;
    this->branch_islands = std::move(portion);
  }
  if (std::regex_search(head, tail, match, re_linktime_size_decreasing_optimizations_header,
                        std::regex_constants::match_continuous))
  {
    line_number += 2u;
    head += match.length();
    auto portion = std::make_unique<LinktimeSizeDecreasingOptimizations>();
    const auto error = portion->Scan(head, tail, line_number);
    if (error != Error::None)
      return error;
    this->linktime_size_decreasing_optimizations = std::move(portion);
  }
  if (std::regex_search(head, tail, match, re_linktime_size_increasing_optimizations_header,
                        std::regex_constants::match_continuous))
  {
    line_number += 2u;
    head += match.length();
    auto portion = std::make_unique<LinktimeSizeIncreasingOptimizations>();
    const auto error = portion->Scan(head, tail, line_number);
    if (error != Error::None)
      return error;
    this->linktime_size_increasing_optimizations = std::move(portion);
  }
NINTENDO_EAD_TRIMMED_LINKER_MAPS_GOTO_HERE:
  while (std::regex_search(head, tail, match, re_section_layout_header,
                           std::regex_constants::match_continuous))
  {
    line_number += 3u;
    head += match.length();
    const auto error = this->ScanPrologue_SectionLayout(head, tail, line_number, match.str(1));
    if (error != Error::None)
      return error;
  }
  if (std::regex_search(head, tail, match, re_memory_map_header,
                        std::regex_constants::match_continuous))
  {
    line_number += 3u;
    head += match.length();
    const auto error = this->ScanPrologue_MemoryMap(head, tail, line_number);
    if (error != Error::None)
      return error;
  }
  if (std::regex_search(head, tail, match, re_linker_generated_symbols_header,
                        std::regex_constants::match_continuous))
  {
    line_number += 3u;
    head += match.length();
    auto portion = std::make_unique<LinkerGeneratedSymbols>();
    const auto error = portion->Scan(head, tail, line_number);
    if (error != Error::None)
      return error;
    this->linker_generated_symbols = std::move(portion);
  }
  return this->ScanForGarbage(head, tail);
}

Map::Error Map::ScanTLOZTP(const std::string_view string_view, std::size_t& line_number)
{
  return this->ScanTLOZTP(string_view.data(), string_view.data() + string_view.size(), line_number);
}

Map::Error Map::ScanTLOZTP(const char* head, const char* const tail, std::size_t& line_number)
{
  if (head == nullptr || tail == nullptr || head > tail)
    return Error::Fail;

  std::cmatch match;
  line_number = 1u;

  this->entry_point_name = "__start";
  // The Legend of Zelda: Twilight Princess features CodeWarrior for GCN 2.7 linker maps that have
  // been post-processed to appear similar to older linker maps. Nintendo EAD probably did this to
  // procrastinate updating the JUTException library. These linker maps contain prologue-free,
  // three-column section layout portions, and nothing else. Also, not that it matters to this
  // scan function, the line endings of the linker maps left on disc were Unix style (LF).
  while (std::regex_search(head, tail, match, re_section_layout_header_modified_b,
                           std::regex_constants::match_continuous))
  {
    std::string section_name = match.str(1);
    line_number += 1u;
    head += match.length();
    const SectionLayout::Kind section_kind = SectionLayout::ToSectionKind(section_name);
    auto portion = std::make_unique<SectionLayout>(section_kind, std::move(section_name));
    portion->SetMinVersion(Version::version_3_0_4);
    const auto error = portion->ScanTLOZTP(head, tail, line_number);
    if (error != Error::None)
      return error;
    this->section_layouts.push_back(std::move(portion));
  }
  return this->ScanForGarbage(head, tail);
}

Map::Error Map::ScanSMGalaxy(const std::string_view string_view, std::size_t& line_number)
{
  return this->ScanSMGalaxy(string_view.data(), string_view.data() + string_view.size(),
                            line_number);
}

Map::Error Map::ScanSMGalaxy(const char* head, const char* const tail, std::size_t& line_number)
{
  if (head == nullptr || tail == nullptr || head > tail)
    return Error::Fail;

  std::cmatch match;
  line_number = 1;

  // We only see this header once, as every symbol is mashed into an imaginary ".text" section.
  if (std::regex_search(head, tail, match, re_section_layout_header_modified_a,
                        std::regex_constants::match_continuous))
  {
    line_number += 2u;
    head += match.length();
    // TODO: detect and split Section Layout subtext by observing the Starting Address
    auto portion = std::make_unique<SectionLayout>(SectionLayout::Kind::Normal, match.str(1));
    portion->SetMinVersion(Version::version_3_0_4);
    const auto error = portion->Scan4Column(head, tail, line_number);
    if (error != Error::None)
      return error;
    this->section_layouts.push_back(std::move(portion));
  }
  else
  {
    return Error::SMGalaxyYouHadOneJob;
  }
  // It seems like a mistake, but for a few examples, a tiny bit of simple-style,
  // headerless, CodeWarrior for Wii 1.0 (at minimum) Memory Map can be found.
  {
    auto portion = std::make_unique<MemoryMap>(false, false, false);
    const auto error = portion->ScanSimple(head, tail, line_number);
    if (error != Error::None)
      return error;
    if (!portion->IsEmpty())
      this->memory_map = std::move(portion);
  }
  return this->ScanForGarbage(head, tail);
}

void Map::Print(std::ostream& stream) const
{
  // "Link map of %s\r\n"
  std::print(stream, "Link map of {:s}\r\n", entry_point_name);
  if (normal_symbol_closure)
    normal_symbol_closure->Print(stream);
  if (eppc_pattern_matching)
    eppc_pattern_matching->Print(stream);
  if (dwarf_symbol_closure)
    dwarf_symbol_closure->Print(stream);
  // TODO: move this into symbol closure printing now that line numbers are stored.
  for (const auto& [line_number, name] : unresolved_symbols)
    // ">>> SYMBOL NOT FOUND: %s\r\n"
    std::print(stream, ">>> SYMBOL NOT FOUND: {:s}\r\n", name);
  if (linker_opts)
    linker_opts->Print(stream);
  if (mixed_mode_islands)
    mixed_mode_islands->Print(stream);
  if (branch_islands)
    branch_islands->Print(stream);
  if (linktime_size_decreasing_optimizations)
    linktime_size_decreasing_optimizations->Print(stream);
  if (linktime_size_increasing_optimizations)
    linktime_size_increasing_optimizations->Print(stream);
  for (const auto& section_layout : section_layouts)
    section_layout->Print(stream);
  if (memory_map)
    memory_map->Print(stream);
  if (linker_generated_symbols)
    linker_generated_symbols->Print(stream);
}

// void Map::Export(Report& report) const noexcept
// {
//   if (normal_symbol_closure)
//     normal_symbol_closure->Export(report);
//   if (dwarf_symbol_closure)
//     dwarf_symbol_closure->Export(report);
//   if (eppc_pattern_matching)
//     eppc_pattern_matching->Export(report);
//   for (const auto& section_layout : section_layouts)
//     section_layout->Export(report);
//   if (linker_generated_symbols)
//     linker_generated_symbols->Export(report);
// }

Version Map::GetMinVersion() const noexcept
{
  Version min_version = std::max({
      normal_symbol_closure ? normal_symbol_closure->min_version : Version::Unknown,
      eppc_pattern_matching ? eppc_pattern_matching->min_version : Version::Unknown,
      dwarf_symbol_closure ? dwarf_symbol_closure->min_version : Version::Unknown,
      linker_opts ? linker_opts->min_version : Version::Unknown,
      mixed_mode_islands ? mixed_mode_islands->min_version : Version::Unknown,
      branch_islands ? branch_islands->min_version : Version::Unknown,
      linktime_size_decreasing_optimizations ? linktime_size_decreasing_optimizations->min_version :
                                               Version::Unknown,
      linktime_size_increasing_optimizations ? linktime_size_increasing_optimizations->min_version :
                                               Version::Unknown,
      memory_map ? memory_map->min_version : Version::Unknown,
      linker_generated_symbols ? linker_generated_symbols->min_version : Version::Unknown,
  });
  for (const auto& section_layout : section_layouts)
    min_version = std::max(section_layout->min_version, min_version);
  return min_version;
}

// clang-format off
static const std::regex re_section_layout_3column_prologue_1{
//  "  Starting        Virtual\r\n"
    "  Starting        Virtual\r?\n"};
static const std::regex re_section_layout_3column_prologue_2{
//  "  address  Size   address\r\n"
    "  address  Size   address\r?\n"};
static const std::regex re_section_layout_3column_prologue_3{
//  "  -----------------------\r\n"
    "  -----------------------\r?\n"};
static const std::regex re_section_layout_4column_prologue_1{
//  "  Starting        Virtual  File\r\n"
    "  Starting        Virtual  File\r?\n"};
static const std::regex re_section_layout_4column_prologue_2{
//  "  address  Size   address  offset\r\n"
    "  address  Size   address  offset\r?\n"};
static const std::regex re_section_layout_4column_prologue_3{
//  "  ---------------------------------\r\n"
    "  ---------------------------------\r?\n"};
// clang-format on

Map::Error Map::ScanPrologue_SectionLayout(const char*& head, const char* const tail,
                                           std::size_t& line_number, std::string name)
{
  std::cmatch match;
  const SectionLayout::Kind section_kind = SectionLayout::ToSectionKind(name);

  if (std::regex_search(head, tail, match, re_section_layout_3column_prologue_1,
                        std::regex_constants::match_continuous))
  {
    line_number += 1u;
    head += match.length();
    if (std::regex_search(head, tail, match, re_section_layout_3column_prologue_2,
                          std::regex_constants::match_continuous))
    {
      line_number += 1u;
      head += match.length();
      if (std::regex_search(head, tail, match, re_section_layout_3column_prologue_3,
                            std::regex_constants::match_continuous))
      {
        line_number += 1u;
        head += match.length();
        auto portion = std::make_unique<SectionLayout>(section_kind, std::move(name));
        const auto error = portion->Scan3Column(head, tail, line_number);
        if (error != Error::None)
          return error;
        this->section_layouts.push_back(std::move(portion));
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
  else if (std::regex_search(head, tail, match, re_section_layout_4column_prologue_1,
                             std::regex_constants::match_continuous))
  {
    line_number += 1u;
    head += match.length();
    if (std::regex_search(head, tail, match, re_section_layout_4column_prologue_2,
                          std::regex_constants::match_continuous))
    {
      line_number += 1u;
      head += match.length();
      if (std::regex_search(head, tail, match, re_section_layout_4column_prologue_3,
                            std::regex_constants::match_continuous))
      {
        line_number += 1u;
        head += match.length();
        auto portion = std::make_unique<SectionLayout>(section_kind, std::move(name));
        portion->SetMinVersion(Version::version_3_0_4);
        const auto error = portion->Scan4Column(head, tail, line_number);
        if (error != Error::None)
          return error;
        this->section_layouts.push_back(std::move(portion));
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
  return Error::None;
}

// clang-format off
static const std::regex re_memory_map_simple_prologue_1_old{
//  "                   Starting Size     File\r\n"
    "                   Starting Size     File\r?\n"};
static const std::regex re_memory_map_simple_prologue_2_old{
//  "                   address           Offset\r\n"
    "                   address           Offset\r?\n"};
static const std::regex re_memory_map_romram_prologue_1_old{
//  "                   Starting Size     File     ROM      RAM Buffer\r\n"
    "                   Starting Size     File     ROM      RAM Buffer\r?\n"};
static const std::regex re_memory_map_romram_prologue_2_old{
//  "                   address           Offset   Address  Address\r\n"
    "                   address           Offset   Address  Address\r?\n"};
static const std::regex re_memory_map_simple_prologue_1{
//  "                       Starting Size     File\r\n"
    "                       Starting Size     File\r?\n"};
static const std::regex re_memory_map_simple_prologue_2{
//  "                       address           Offset\r\n"
    "                       address           Offset\r?\n"};
static const std::regex re_memory_map_romram_prologue_1{
//  "                       Starting Size     File     ROM      RAM Buffer\r\n"
    "                       Starting Size     File     ROM      RAM Buffer\r?\n"};
static const std::regex re_memory_map_romram_prologue_2{
//  "                       address           Offset   Address  Address\r\n"
    "                       address           Offset   Address  Address\r?\n"};
static const std::regex re_memory_map_srecord_prologue_1{
//  "                       Starting Size     File       S-Record\r\n"
    "                       Starting Size     File       S-Record\r?\n"};
static const std::regex re_memory_map_srecord_prologue_2{
//  "                       address           Offset     Line\r\n"
    "                       address           Offset     Line\r?\n"};
static const std::regex re_memory_map_binfile_prologue_1{
//  "                       Starting Size     File     Bin File Bin File\r\n"
    "                       Starting Size     File     Bin File Bin File\r?\n"};
static const std::regex re_memory_map_binfile_prologue_2{
//  "                       address           Offset   Offset   Name\r\n"
    "                       address           Offset   Offset   Name\r?\n"};
static const std::regex re_memory_map_romram_srecord_prologue_1{
//  "                       Starting Size     File     ROM      RAM Buffer  S-Record\r\n"
    "                       Starting Size     File     ROM      RAM Buffer  S-Record\r?\n"};
static const std::regex re_memory_map_romram_srecord_prologue_2{
//  "                       address           Offset   Address  Address     Line\r\n"
    "                       address           Offset   Address  Address     Line\r?\n"};
static const std::regex re_memory_map_romram_binfile_prologue_1{
//  "                       Starting Size     File     ROM      RAM Buffer Bin File Bin File\r\n"
    "                       Starting Size     File     ROM      RAM Buffer Bin File Bin File\r?\n"};
static const std::regex re_memory_map_romram_binfile_prologue_2{
//  "                       address           Offset   Address  Address    Offset   Name\r\n"
    "                       address           Offset   Address  Address    Offset   Name\r?\n"};
static const std::regex re_memory_map_srecord_binfile_prologue_1{
//  "                       Starting Size     File        S-Record Bin File Bin File\r\n"
    "                       Starting Size     File        S-Record Bin File Bin File\r?\n"};
static const std::regex re_memory_map_srecord_binfile_prologue_2{
//  "                       address           Offset      Line     Offset   Name\r\n"
    "                       address           Offset      Line     Offset   Name\r?\n"};
static const std::regex re_memory_map_romram_srecord_binfile_prologue_1{
//  "                       Starting Size     File     ROM      RAM Buffer    S-Record Bin File Bin File\r\n"
    "                       Starting Size     File     ROM      RAM Buffer    S-Record Bin File Bin File\r?\n"};
static const std::regex re_memory_map_romram_srecord_binfile_prologue_2{
//  "                       address           Offset   Address  Address       Line     Offset   Name\r\n"
    "                       address           Offset   Address  Address       Line     Offset   Name\r?\n"};
// clang-format on

Map::Error Map::ScanPrologue_MemoryMap(const char*& head, const char* const tail,
                                       std::size_t& line_number)
{
  std::cmatch match;

  if (std::regex_search(head, tail, match, re_memory_map_simple_prologue_1_old,
                        std::regex_constants::match_continuous))
  {
    line_number += 1u;
    head += match.length();
    if (std::regex_search(head, tail, match, re_memory_map_simple_prologue_2_old,
                          std::regex_constants::match_continuous))
    {
      line_number += 1u;
      head += match.length();
      auto portion = std::make_unique<MemoryMap>(false);
      const auto error = portion->ScanSimple_old(head, tail, line_number);
      if (error != Error::None)
        return error;
      this->memory_map = std::move(portion);
    }
    else
    {
      return Error::MemoryMapBadPrologue;
    }
  }
  else if (std::regex_search(head, tail, match, re_memory_map_romram_prologue_1_old,
                             std::regex_constants::match_continuous))
  {
    line_number += 1u;
    head += match.length();
    if (std::regex_search(head, tail, match, re_memory_map_romram_prologue_2_old,
                          std::regex_constants::match_continuous))
    {
      line_number += 1u;
      head += match.length();
      auto portion = std::make_unique<MemoryMap>(true);
      const auto error = portion->ScanRomRam_old(head, tail, line_number);
      if (error != Error::None)
        return error;
      this->memory_map = std::move(portion);
    }
    else
    {
      return Error::MemoryMapBadPrologue;
    }
  }
  else if (std::regex_search(head, tail, match, re_memory_map_simple_prologue_1,
                             std::regex_constants::match_continuous))
  {
    line_number += 1u;
    head += match.length();
    if (std::regex_search(head, tail, match, re_memory_map_simple_prologue_2,
                          std::regex_constants::match_continuous))
    {
      line_number += 1u;
      head += match.length();
      auto portion = std::make_unique<MemoryMap>(false, false, false);
      const auto error = portion->ScanSimple(head, tail, line_number);
      if (error != Error::None)
        return error;
      this->memory_map = std::move(portion);
    }
    else
    {
      return Error::MemoryMapBadPrologue;
    }
  }
  else if (std::regex_search(head, tail, match, re_memory_map_romram_prologue_1,
                             std::regex_constants::match_continuous))
  {
    line_number += 1u;
    head += match.length();
    if (std::regex_search(head, tail, match, re_memory_map_romram_prologue_2,
                          std::regex_constants::match_continuous))
    {
      line_number += 1u;
      head += match.length();
      auto portion = std::make_unique<MemoryMap>(true, false, false);
      const auto error = portion->ScanRomRam(head, tail, line_number);
      if (error != Error::None)
        return error;
      this->memory_map = std::move(portion);
    }
    else
    {
      return Error::MemoryMapBadPrologue;
    }
  }
  else if (std::regex_search(head, tail, match, re_memory_map_srecord_prologue_1,
                             std::regex_constants::match_continuous))
  {
    line_number += 1u;
    head += match.length();
    if (std::regex_search(head, tail, match, re_memory_map_srecord_prologue_2,
                          std::regex_constants::match_continuous))
    {
      line_number += 1u;
      head += match.length();
      auto portion = std::make_unique<MemoryMap>(false, true, false);
      const auto error = portion->ScanSRecord(head, tail, line_number);
      if (error != Error::None)
        return error;
      this->memory_map = std::move(portion);
    }
    else
    {
      return Error::MemoryMapBadPrologue;
    }
  }
  else if (std::regex_search(head, tail, match, re_memory_map_binfile_prologue_1,
                             std::regex_constants::match_continuous))
  {
    line_number += 1u;
    head += match.length();
    if (std::regex_search(head, tail, match, re_memory_map_binfile_prologue_2,
                          std::regex_constants::match_continuous))
    {
      line_number += 1u;
      head += match.length();
      auto portion = std::make_unique<MemoryMap>(false, false, true);
      const auto error = portion->ScanBinFile(head, tail, line_number);
      if (error != Error::None)
        return error;
      this->memory_map = std::move(portion);
    }
    else
    {
      return Error::MemoryMapBadPrologue;
    }
  }
  else if (std::regex_search(head, tail, match, re_memory_map_romram_srecord_prologue_1,
                             std::regex_constants::match_continuous))
  {
    line_number += 1u;
    head += match.length();
    if (std::regex_search(head, tail, match, re_memory_map_romram_srecord_prologue_2,
                          std::regex_constants::match_continuous))
    {
      line_number += 1u;
      head += match.length();
      auto portion = std::make_unique<MemoryMap>(true, true, false);
      const auto error = portion->ScanRomRamSRecord(head, tail, line_number);
      if (error != Error::None)
        return error;
      this->memory_map = std::move(portion);
    }
    else
    {
      return Error::MemoryMapBadPrologue;
    }
  }
  else if (std::regex_search(head, tail, match, re_memory_map_romram_binfile_prologue_1,
                             std::regex_constants::match_continuous))
  {
    line_number += 1u;
    head += match.length();
    if (std::regex_search(head, tail, match, re_memory_map_romram_binfile_prologue_2,
                          std::regex_constants::match_continuous))
    {
      line_number += 1u;
      head += match.length();
      auto portion = std::make_unique<MemoryMap>(true, false, true);
      const auto error = portion->ScanRomRamBinFile(head, tail, line_number);
      if (error != Error::None)
        return error;
      this->memory_map = std::move(portion);
    }
    else
    {
      return Error::MemoryMapBadPrologue;
    }
  }
  else if (std::regex_search(head, tail, match, re_memory_map_srecord_binfile_prologue_1,
                             std::regex_constants::match_continuous))
  {
    line_number += 1u;
    head += match.length();
    if (std::regex_search(head, tail, match, re_memory_map_srecord_binfile_prologue_2,
                          std::regex_constants::match_continuous))
    {
      line_number += 1u;
      head += match.length();
      auto portion = std::make_unique<MemoryMap>(false, true, true);
      const auto error = portion->ScanSRecordBinFile(head, tail, line_number);
      if (error != Error::None)
        return error;
      this->memory_map = std::move(portion);
    }
    else
    {
      return Error::MemoryMapBadPrologue;
    }
  }
  else if (std::regex_search(head, tail, match, re_memory_map_romram_srecord_binfile_prologue_1,
                             std::regex_constants::match_continuous))
  {
    line_number += 1u;
    head += match.length();
    if (std::regex_search(head, tail, match, re_memory_map_romram_srecord_binfile_prologue_2,
                          std::regex_constants::match_continuous))
    {
      line_number += 1u;
      head += match.length();
      auto portion = std::make_unique<MemoryMap>(true, true, true);
      const auto error = portion->ScanRomRamSRecordBinFile(head, tail, line_number);
      if (error != Error::None)
        return error;
      this->memory_map = std::move(portion);
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

Map::Error Map::ScanForGarbage(const char* const head, const char* const tail)
{
  if (head < tail)
  {
    // Gamecube ISO Tool (http://www.wiibackupmanager.co.uk/gcit.html) has a bug that appends null
    // byte padding to the next multiple of 32 bytes at the end of any file it extracts. During my
    // research, I ran into a lot of linker maps afflicted by this bug, enough to justify a special
    // case for garbage consisting of only null bytes.
    if (std::any_of(head, tail, [](const char c) { return c != '\0'; }))
      return Error::GarbageFound;
  }
  return Error::None;
}

// clang-format off
static const std::regex re_symbol_closure_node_normal{
//  "%i] " and "%s (%s,%s) found in %s %s\r\n"
    "   *(\\d+)\\] (.*) \\((.*),(.*)\\) found in (.*) (.*)\r?\n"};
static const std::regex re_symbol_closure_node_normal_unref_dup_header{
//  "%i] " and ">>> UNREFERENCED DUPLICATE %s\r\n"
    "   *(\\d+)\\] >>> UNREFERENCED DUPLICATE (.*)\r?\n"};
static const std::regex re_symbol_closure_node_normal_unref_dups{
//  "%i] " and ">>> (%s,%s) found in %s %s\r\n"
    "   *(\\d+)\\] >>> \\((.*),(.*)\\) found in (.*) (.*)\r?\n"};
static const std::regex re_symbol_closure_node_linker_generated{
//  "%i] " and "%s found as linker generated symbol\r\n"
    "   *(\\d+)\\] (.*) found as linker generated symbol\r?\n"};
// clang-format on

static const std::map<std::string_view, Type> map_symbol_closure_st_type{
    {"notype", Type::notype},   {"object", Type::object}, {"func", Type::func},
    {"section", Type::section}, {"file", Type::file},     {"unknown", Type::unknown},
};
static const std::map<std::string_view, Bind> map_symbol_closure_st_bind{
    {"local", Bind::local},       {"global", Bind::global},     {"weak", Bind::weak},
    {"multidef", Bind::multidef}, {"overload", Bind::overload}, {"unknown", Bind::unknown},
};

Map::Error Map::SymbolClosure::Scan(  //
    const char*& head, const char* const tail, std::size_t& line_number,
    std::list<std::pair<std::size_t, std::string>>& unresolved_symbols)
{
  std::cmatch match;

  NodeBase* curr_node = &this->root;
  int curr_hierarchy_level = 0;
  bool after_dtors_99 = false;

  while (true)
  {
    if (std::regex_search(head, tail, match, re_symbol_closure_node_normal,
                          std::regex_constants::match_continuous))
    {
      const int next_hierarchy_level = std::stoi(match.str(1));
      if (next_hierarchy_level <= 0)
        return Error::SymbolClosureInvalidHierarchy;
      if (curr_hierarchy_level + 1 < next_hierarchy_level)
        return Error::SymbolClosureHierarchySkip;
      if (after_dtors_99 && next_hierarchy_level != 3)
        return Error::SymbolClosureAfterDtors99Irregularity;
      const std::string_view type = ToStringView(match[3]), bind = ToStringView(match[4]);
      if (!map_symbol_closure_st_type.contains(type))
        return Error::SymbolClosureInvalidSymbolType;
      if (!map_symbol_closure_st_bind.contains(bind))
        return Error::SymbolClosureInvalidSymbolBind;
      std::string symbol_name = match.str(2), module_name = match.str(5),
                  source_name = match.str(6);
      const std::string& compilation_unit_name = source_name.empty() ? module_name : source_name;
      NodeLookup& curr_node_lookup = this->lookup[compilation_unit_name];
      if (curr_node_lookup.contains(symbol_name))
      {
        if (after_dtors_99 && symbol_name == ".text")
          Warn::SymOnFlagDetected(line_number, compilation_unit_name);
        else
          Warn::OneDefinitionRuleViolation(line_number, symbol_name, compilation_unit_name);
      }

      line_number += 1u;
      head += match.length();

      for (int i = curr_hierarchy_level + 1; i > next_hierarchy_level; --i)
        curr_node = curr_node->parent;
      curr_hierarchy_level = next_hierarchy_level;

      std::list<NodeReal::UnreferencedDuplicate> unref_dups;

      if (std::regex_search(head, tail, match, re_symbol_closure_node_normal_unref_dup_header,
                            std::regex_constants::match_continuous))
      {
        if (std::stoi(match.str(1)) != curr_hierarchy_level)
          return Error::SymbolClosureUnrefDupsHierarchyMismatch;
        if (match.str(2) != symbol_name)
          return Error::SymbolClosureUnrefDupsNameMismatch;
        line_number += 1u;
        head += match.length();
        while (std::regex_search(head, tail, match, re_symbol_closure_node_normal_unref_dups,
                                 std::regex_constants::match_continuous))
        {
          if (std::stoi(match.str(1)) != curr_hierarchy_level)
            return Error::SymbolClosureUnrefDupsHierarchyMismatch;
          const std::string_view unref_dup_type = ToStringView(match[2]),
                                 unref_dup_bind = ToStringView(match[3]);
          if (!map_symbol_closure_st_type.contains(unref_dup_type))
            return Error::SymbolClosureInvalidSymbolType;
          if (!map_symbol_closure_st_bind.contains(unref_dup_bind))
            return Error::SymbolClosureInvalidSymbolType;
          line_number += 1u;
          head += match.length();
          unref_dups.emplace_back(map_symbol_closure_st_type.at(unref_dup_type),
                                  map_symbol_closure_st_bind.at(unref_dup_bind), match.str(4),
                                  match.str(5));
        }
        if (unref_dups.empty())
          return Error::SymbolClosureUnrefDupsEmpty;
        this->SetMinVersion(Version::version_2_3_3_build_137);
      }

      const bool is_dtors_99 = (!after_dtors_99 && symbol_name == "_dtors$99" &&
                                module_name == "Linker Generated Symbol File");

      NodeReal* next_node =
          new NodeReal(curr_node, symbol_name, map_symbol_closure_st_type.at(type),
                       map_symbol_closure_st_bind.at(bind), std::move(module_name),
                       std::move(source_name), std::move(unref_dups));
      curr_node = curr_node->children.emplace_back(next_node).get();
      curr_node_lookup.emplace(std::move(symbol_name), *next_node);

      // Though I do not understand it, the following is a normal occurrence for _dtors$99:
      // "  1] _dtors$99 (object,global) found in Linker Generated Symbol File "
      // "    3] .text (section,local) found in xyz.cpp lib.a"
      if (is_dtors_99)
      {
        // Create a dummy node for hierarchy level 2.
        curr_node = curr_node->children.emplace_back(new NodeBase(curr_node)).get();
        ++curr_hierarchy_level;
        this->SetMinVersion(Version::version_3_0_4);
        after_dtors_99 = true;
      }
      continue;
    }
    if (std::regex_search(head, tail, match, re_symbol_closure_node_linker_generated,
                          std::regex_constants::match_continuous))
    {
      const int next_hierarchy_level = std::stoi(match.str(1));
      if (next_hierarchy_level <= 0)
        return Error::SymbolClosureInvalidHierarchy;
      if (curr_hierarchy_level + 1 < next_hierarchy_level)
        return Error::SymbolClosureHierarchySkip;

      line_number += 1u;
      head += match.length();

      for (int i = curr_hierarchy_level + 1; i > next_hierarchy_level; --i)
        curr_node = curr_node->parent;
      curr_hierarchy_level = next_hierarchy_level;

      curr_node =
          curr_node->children.emplace_back(new NodeLinkerGenerated(curr_node, match.str(2))).get();
      continue;
    }
    // Up until CodeWarrior for GCN 3.0a3 (at the earliest), unresolved symbols were printed as the
    // symbol closure was being walked and printed itself. This gives a good idea of what function
    // was looking for that symbol, but because no hierarchy level is given, it is impossible to be
    // certain without analyzing code. After that version, all unresolved symbols from the symbol
    // closure(s) and EPPC_PatternMatching would (I think) be printed after the DWARF symbol
    // closure. The way it works out, this same scanning code handles that as well. If symbol
    // closures are disabled, this scan function will still parse the unresolved symbol prints.
    // There are also a few linker maps I've found where it appears the unresolved symbols are
    // pre-printed before the first symbol closure. Wouldn't you know it, this scanning code also
    // handles that. The line number is stored so the Map::Print method can accurately reproduce any
    // of the aeformentioned arrangements, though if you find another use for it, good for you.
    if (std::regex_search(head, tail, match, re_unresolved_symbol,
                          std::regex_constants::match_continuous))
    {
      unresolved_symbols.emplace_back(line_number, match.str(1));
      line_number += 1u;
      head += match.length();
      continue;
    }
    break;
  }
  return Error::None;
}

void Map::SymbolClosure::Print(std::ostream& stream) const
{
  this->root.Print(stream, 0);
}

void Map::SymbolClosure::NodeBase::PrintPrefix(std::ostream& stream, const int hierarchy_level)
{
  if (hierarchy_level >= 0)
    for (int i = 0; i <= hierarchy_level; ++i)
      stream.put(' ');
  // "%i] "
  std::print(stream, "{:d}] ", hierarchy_level);
}

constexpr std::string_view Map::SymbolClosure::NodeBase::ToName(const Type st_type) noexcept
{
  switch (st_type)
  {
  case Type::notype:
    return "notype";
  case Type::object:
    return "object";
  case Type::func:
    return "func";
  case Type::section:
    return "section";
  case Type::file:
    return "file";
  default:
    return "unknown";
  }
}

constexpr std::string_view Map::SymbolClosure::NodeBase::ToName(const Bind st_bind) noexcept
{
  switch (st_bind)
  {
  case Bind::local:
    return "local";
  case Bind::global:
    return "global";
  case Bind::weak:
    return "weak";
  default:
    return "unknown";
  case Bind::multidef:
    return "multidef";
  case Bind::overload:
    return "overload";
  }
}

void Map::SymbolClosure::NodeBase::Print(std::ostream& stream, const int hierarchy_level) const
{
  for (const auto& node : this->children)
    node->Print(stream, hierarchy_level + 1);
}

void Map::SymbolClosure::NodeReal::Print(std::ostream& stream, const int hierarchy_level) const
{
  PrintPrefix(stream, hierarchy_level);
  // "%s (%s,%s) found in %s %s\r\n"
  std::print(stream, "{:s} ({:s},{:s}) found in {:s} {:s}\r\n", name, ToName(type), ToName(bind),
             module_name, source_name);
  if (!this->unref_dups.empty())
  {
    PrintPrefix(stream, hierarchy_level);
    // ">>> UNREFERENCED DUPLICATE %s\r\n"
    std::print(stream, ">>> UNREFERENCED DUPLICATE {:s}\r\n", name);
    for (const auto& unref_dup : unref_dups)
      unref_dup.Print(stream, hierarchy_level);
  }
  NodeBase::Print(stream, hierarchy_level);
}

void Map::SymbolClosure::NodeLinkerGenerated::Print(std::ostream& stream,
                                                    const int hierarchy_level) const
{
  PrintPrefix(stream, hierarchy_level);
  // "%s found as linker generated symbol\r\n"
  std::print(stream, "{:s} found as linker generated symbol\r\n", name);
  // Linker generated symbols should have no children but we'll check anyway.
  NodeBase::Print(stream, hierarchy_level);
}

void Map::SymbolClosure::NodeReal::UnreferencedDuplicate::Print(std::ostream& stream,
                                                                const int hierarchy_level) const
{
  PrintPrefix(stream, hierarchy_level);
  // ">>> (%s,%s) found in %s %s\r\n"
  std::print(stream, ">>> ({:s},{:s}) found in {:s} {:s}\r\n", ToName(type), ToName(bind),
             module_name, source_name);
}

// void Map::SymbolClosure::Export(Report& report) const noexcept
// {
//   this->root.Export(report);
// }

// void Map::SymbolClosure::NodeBase::Export(Report& report) const noexcept
// {
//   for (const auto& node : this->children)
//     node->Export(report);
// }

// void Map::SymbolClosure::NodeReal::Export(Report& report) const noexcept
// {
//   auto& debug_info = report[source_name.empty() ? module_name : source_name][name];
//   debug_info.symbol_closure_unit = this;

//   for (const auto& node : this->children)
//     node->Export(report);
// }

// clang-format off
static const std::regex re_code_merging_is_duplicated{
//  "--> duplicated code: symbol %s is duplicated by %s, size = %d \r\n\r\n"
    "--> duplicated code: symbol (.*) is duplicated by (.*), size = (\\d+) \r?\n\r?\n"};
static const std::regex re_code_merging_will_be_replaced{
//  "--> the function %s will be replaced by a branch to %s\r\n\r\n\r\n"
    "--> the function (.*) will be replaced by a branch to (.*)\r?\n\r?\n\r?\n"};
static const std::regex re_code_merging_was_interchanged{
//  "--> the function %s was interchanged with %s, size=%d \r\n"
    "--> the function (.*) was interchanged with (.*), size=(\\d+) \r?\n"};
static const std::regex re_code_folding_header{
//  "\r\n\r\n\r\nCode folded in file: %s \r\n"
    "\r?\n\r?\n\r?\nCode folded in file: (.*) \r?\n"};
static const std::regex re_code_folding_is_duplicated{
//  "--> %s is duplicated by %s, size = %d \r\n\r\n"
    "--> (.*) is duplicated by (.*), size = (\\d+) \r?\n\r?\n"};
static const std::regex re_code_folding_is_duplicated_new_branch{
//  "--> %s is duplicated by %s, size = %d, new branch function %s \r\n\r\n"
    "--> (.*) is duplicated by (.*), size = (\\d+), new branch function (.*) \r?\n\r?\n"};
// clang-format on

Map::Error Map::EPPC_PatternMatching::Scan(const char*& head, const char* const tail,
                                           std::size_t& line_number)
{
  std::cmatch match;

  while (true)
  {
    bool will_be_replaced = false, was_interchanged = false;
    // EPPC_PatternMatching looks for functions that are duplicates of one another and prints what
    // it has changed in real-time to the linker map.
    if (std::regex_search(head, tail, match, re_code_merging_is_duplicated,
                          std::regex_constants::match_continuous))
    {
      std::string first_name = match.str(1), second_name = match.str(2);
      const u32 size = static_cast<u32>(std::stoul(match.str(3)));
      line_number += 2u;
      head += match.length();
      if (std::regex_search(head, tail, match, re_code_merging_will_be_replaced,
                            std::regex_constants::match_continuous))
      {
        if (match.str(1) != first_name)
          return Error::EPPC_PatternMatchingMergingFirstNameMismatch;
        if (match.str(2) != second_name)
          return Error::EPPC_PatternMatchingMergingSecondNameMismatch;
        line_number += 3u;
        head += match.length();
        will_be_replaced = true;
      }
      if (merging_lookup.contains(first_name))
        Warn::MergingOneDefinitionRuleViolation(line_number - 5u, first_name);
      const MergingUnit& unit = this->merging_units.emplace_back(
          first_name, std::move(second_name), size, will_be_replaced, was_interchanged);
      merging_lookup.emplace(std::move(first_name), unit);
      continue;
    }
    if (std::regex_search(head, tail, match, re_code_merging_was_interchanged,
                          std::regex_constants::match_continuous))
    {
      std::string first_name = match.str(1), second_name = match.str(2);
      const u32 size = static_cast<u32>(std::stoul(match.str(3)));
      was_interchanged = true;
      line_number += 1u;
      head += match.length();
      if (std::regex_search(head, tail, match, re_code_merging_will_be_replaced,
                            std::regex_constants::match_continuous))
      {
        if (match.str(1) != first_name)
          return Error::EPPC_PatternMatchingMergingFirstNameMismatch;
        if (match.str(2) != second_name)
          return Error::EPPC_PatternMatchingMergingSecondNameMismatch;

        line_number += 3u;
        head += match.length();
        will_be_replaced = true;
      }
      if (std::regex_search(head, tail, match, re_code_merging_is_duplicated,
                            std::regex_constants::match_continuous))
      {
        if (match.str(1) != first_name)
          return Error::EPPC_PatternMatchingMergingFirstNameMismatch;
        if (match.str(2) != second_name)
          return Error::EPPC_PatternMatchingMergingSecondNameMismatch;
        if (std::stoul(match.str(3)) != size)
          return Error::EPPC_PatternMatchingMergingSizeMismatch;
        line_number += 2u;
        head += match.length();
      }
      else
      {
        return Error::EPPC_PatternMatchingMergingInterchangeMissingEpilogue;
      }
      if (merging_lookup.contains(first_name))
        Warn::MergingOneDefinitionRuleViolation(line_number - 5u, first_name);
      const MergingUnit& unit = this->merging_units.emplace_back(
          first_name, std::move(second_name), size, will_be_replaced, was_interchanged);
      merging_lookup.emplace(std::move(first_name), unit);
      continue;
    }
    break;
  }
  // After analysis concludes, a redundant summary of changes per file is printed.
  while (std::regex_search(head, tail, match, re_code_folding_header,
                           std::regex_constants::match_continuous))
  {
    std::string object_name = match.str(1);
    if (this->folding_lookup.contains(object_name))
      Warn::FoldingRepeatObject(line_number + 3u, object_name);
    FoldingUnit::UnitLookup& curr_unit_lookup = this->folding_lookup[object_name];
    line_number += 4u;
    head += match.length();
    std::list<FoldingUnit::Unit> units;
    while (true)
    {
      if (std::regex_search(head, tail, match, re_code_folding_is_duplicated,
                            std::regex_constants::match_continuous))
      {
        std::string first_name = match.str(1);
        if (curr_unit_lookup.contains(first_name))
          Warn::FoldingOneDefinitionRuleViolation(line_number, first_name, object_name);
        line_number += 2u;
        head += match.length();
        const FoldingUnit::Unit& unit =
            units.emplace_back(first_name, match.str(2), std::stoul(match.str(3)), false);
        curr_unit_lookup.emplace(std::move(first_name), unit);
        continue;
      }
      if (std::regex_search(head, tail, match, re_code_folding_is_duplicated_new_branch,
                            std::regex_constants::match_continuous))
      {
        std::string first_name = match.str(1);
        if (first_name != match.str(4))  // It is my assumption that they will always match.
          return Error::EPPC_PatternMatchingFoldingNewBranchFunctionNameMismatch;
        if (curr_unit_lookup.contains(first_name))
          Warn::FoldingOneDefinitionRuleViolation(line_number, first_name, object_name);
        line_number += 2u;
        head += match.length();
        const FoldingUnit::Unit& unit =
            units.emplace_back(first_name, match.str(2), std::stoul(match.str(3)), true);
        curr_unit_lookup.emplace(std::move(first_name), unit);
        continue;
      }
      break;
    }
    this->folding_units.emplace_back(std::move(object_name), std::move(units));
  }
  return Error::None;
}

void Map::EPPC_PatternMatching::Print(std::ostream& stream) const
{
  for (const auto& unit : merging_units)
    unit.Print(stream);
  for (const auto& unit : folding_units)
    unit.Print(stream);
}

void Map::EPPC_PatternMatching::MergingUnit::Print(std::ostream& stream) const
{
  if (was_interchanged)
  {
    // "--> the function %s was interchanged with %s, size=%d \r\n"
    std::print(stream, "--> the function {:s} was interchanged with {:s}, size={:d} \r\n",
               first_name, second_name, size);
    if (will_be_replaced)
    {
      // "--> the function %s will be replaced by a branch to %s\r\n\r\n\r\n"
      std::print(stream, "--> the function {:s} will be replaced by a branch to {:s}\r\n\r\n\r\n",
                 first_name, second_name);
    }
    // "--> duplicated code: symbol %s is duplicated by %s, size = %d \r\n\r\n"
    std::print(stream,
               "--> duplicated code: symbol {:s} is duplicated by {:s}, size = {:d} \r\n\r\n",
               first_name, second_name, size);
  }
  else
  {
    // "--> duplicated code: symbol %s is duplicated by %s, size = %d \r\n\r\n"
    std::print(stream,
               "--> duplicated code: symbol {:s} is duplicated by {:s}, size = {:d} \r\n\r\n",
               first_name, second_name, size);
    if (will_be_replaced)
    {
      // "--> the function %s will be replaced by a branch to %s\r\n\r\n\r\n"
      std::print(stream, "--> the function {:s} will be replaced by a branch to {:s}\r\n\r\n\r\n",
                 first_name, second_name);
    }
  }
}

void Map::EPPC_PatternMatching::FoldingUnit::Print(std::ostream& stream) const
{
  // "\r\n\r\n\r\nCode folded in file: %s \r\n"
  std::print(stream, "\r\n\r\n\r\nCode folded in file: {:s} \r\n", object_name);
  for (const auto& unit : units)
    unit.Print(stream);
}

void Map::EPPC_PatternMatching::FoldingUnit::Unit::Print(std::ostream& stream) const
{
  if (new_branch_function)
    // "--> %s is duplicated by %s, size = %d, new branch function %s \r\n\r\n"
    std::print(stream,
               "--> {:s} is duplicated by {:s}, size = {:d}, new branch function {:s} \r\n\r\n",
               first_name, second_name, size, first_name);
  else
    // "--> %s is duplicated by %s, size = %d \r\n\r\n"
    std::print(stream, "--> {:s} is duplicated by {:s}, size = {:d} \r\n\r\n", first_name,
               second_name, size);
}

// void Map::EPPC_PatternMatching::Export(Report& report) const noexcept
// {
//   auto GenerateLookup = [this]() {
//     std::map<std::string, const MergingUnit&> lookup;
//     for (const auto& merging_unit : merging_units)
//       // TODO: make sure there are no repeats while in the Scan method.
//       lookup.insert({merging_unit.first_name, merging_unit});
//     return lookup;
//   };
//   auto ObjName = [&report](std::string s) -> std::string {
//     std::size_t dir_sep_off = s.rfind('\\');
//     if (dir_sep_off != std::string::npos && ++dir_sep_off < s.length())
//     {
//       std::string s2 = s.substr(dir_sep_off);
//       if (report.contains(s2))
//         return s2;
//     }
//     return s;
//   };
//   const auto lookup = GenerateLookup();  // O(n) becomes O(log n)

//   for (const auto& folding_unit : folding_units)
//   {
//     auto& subreport = report[ObjName(folding_unit.object_name)];
//     for (const auto& unit : folding_unit.units)
//     {
//       auto& debug_info = subreport[unit.first_name];
//       // TODO: You had better hope this doesn't throw
//       debug_info.eppc_pattern_matching_merging_unit = &lookup.at(unit.first_name);
//       debug_info.eppc_pattern_matching_folding_unit_unit = &unit;
//     }
//   }
// }

// clang-format off
static const std::regex re_linker_opts_unit_not_near{
//  "  %s/ %s()/ %s - address not in near addressing range \r\n"
    "  (.*)/ (.*)\\(\\)/ (.*) - address not in near addressing range \r?\n"};
static const std::regex re_linker_opts_unit_address_not_computed{
//  "  %s/ %s()/ %s - final address not yet computed \r\n"
    "  (.*)/ (.*)\\(\\)/ (.*) - final address not yet computed \r?\n"};
static const std::regex re_linker_opts_unit_optimized{
//  "! %s/ %s()/ %s - optimized addressing \r\n"
    "! (.*)/ (.*)\\(\\)/ (.*) - optimized addressing \r?\n"};
static const std::regex re_linker_opts_unit_disassemble_error{
//  "  %s/ %s() - error disassembling function \r\n"
    "  (.*)/ (.*)\\(\\) - error disassembling function \r?\n"};
// clang-format on

Map::Error Map::LinkerOpts::Scan(const char*& head, const char* const tail,
                                 std::size_t& line_number)

{
  std::cmatch match;

  while (true)
  {
    if (std::regex_search(head, tail, match, re_linker_opts_unit_not_near,
                          std::regex_constants::match_continuous))
    {
      line_number += 1u;
      head += match.length();
      this->units.emplace_back(Unit::Kind::NotNear, match.str(1), match.str(2), match.str(3));
      continue;
    }
    if (std::regex_search(head, tail, match, re_linker_opts_unit_disassemble_error,
                          std::regex_constants::match_continuous))
    {
      line_number += 1u;
      head += match.length();
      this->units.emplace_back(match.str(1), match.str(2));
      continue;
    }
    if (std::regex_search(head, tail, match, re_linker_opts_unit_address_not_computed,
                          std::regex_constants::match_continuous))
    {
      line_number += 1u;
      head += match.length();
      this->units.emplace_back(Unit::Kind::NotComputed, match.str(1), match.str(2), match.str(3));
      continue;
    }
    // I have not seen a single linker map with this
    if (std::regex_search(head, tail, match, re_linker_opts_unit_optimized,
                          std::regex_constants::match_continuous))
    {
      line_number += 1u;
      head += match.length();
      this->units.emplace_back(Unit::Kind::Optimized, match.str(1), match.str(2), match.str(3));
      continue;
    }
    break;
  }
  return Error::None;
}

void Map::LinkerOpts::Print(std::ostream& stream) const
{
  for (const auto& unit : units)
    unit.Print(stream);
}

void Map::LinkerOpts::Unit::Print(std::ostream& stream) const
{
  switch (this->unit_kind)
  {
  case Kind::NotNear:
    // "  %s/ %s()/ %s - address not in near addressing range \r\n"
    std::print(stream, "  {:s}/ {:s}()/ {:s} - address not in near addressing range \r\n",
               module_name, name, reference_name);
    return;
  case Kind::NotComputed:
    // "  %s/ %s()/ %s - final address not yet computed \r\n"
    std::print(stream, "  {:s}/ {:s}()/ {:s} - final address not yet computed \r\n", module_name,
               name, reference_name);
    return;
  case Kind::Optimized:
    // "! %s/ %s()/ %s - optimized addressing \r\n"
    std::print(stream, "! {:s}/ {:s}()/ {:s} - optimized addressing \r\n", module_name, name,
               reference_name);
    return;
  case Kind::DisassembleError:
    // "  %s/ %s() - error disassembling function \r\n"
    std::print(stream, "  {:s}/ {:s}() - error disassembling function \r\n", module_name, name);
    return;
  }
}

// clang-format off
static const std::regex re_mixed_mode_islands_created{
//  "  mixed mode island %s created for %s\r\n"
    "  mixed mode island (.*) created for (.*)\r?\n"};
static const std::regex re_mixed_mode_islands_created_safe{
//  "  safe mixed mode island %s created for %s\r\n"
    "  safe mixed mode island (.*) created for (.*)\r?\n"};
// clang-format on

Map::Error Map::MixedModeIslands::Scan(const char*& head, const char* const tail,
                                       std::size_t& line_number)
{
  std::cmatch match;

  // TODO: I have literally never seen Mixed Mode Islands.
  // Similar to Branch Islands, this is conjecture.
  while (true)
  {
    if (std::regex_search(head, tail, match, re_mixed_mode_islands_created,
                          std::regex_constants::match_continuous))
    {
      line_number += 1u;
      head += match.length();
      this->units.emplace_back(match.str(1), match.str(2), false);
      continue;
    }
    if (std::regex_search(head, tail, match, re_mixed_mode_islands_created_safe,
                          std::regex_constants::match_continuous))
    {
      line_number += 1u;
      head += match.length();
      this->units.emplace_back(match.str(1), match.str(2), true);
      continue;
    }
    break;
  }
  return Error::None;
}

void Map::MixedModeIslands::Print(std::ostream& stream) const
{
  std::print(stream, "\r\nMixed Mode Islands\r\n");
  for (const auto& unit : units)
    unit.Print(stream);
}
void Map::MixedModeIslands::Unit::Print(std::ostream& stream) const
{
  if (is_safe)
    // "  safe mixed mode island %s created for %s\r\n"
    std::print(stream, "  safe mixed mode island {:s} created for {:s}\r\n", first_name,
               second_name);
  else
    // "  mixed mode island %s created for %s\r\n"
    std::print(stream, "  mixed mode island {:s} created for {:s}\r\n", first_name, second_name);
}

// clang-format off
static const std::regex re_branch_islands_created{
//  "  branch island %s created for %s\r\n"
    "  branch island (.*) created for (.*)\r?\n"};
static const std::regex re_branch_islands_created_safe{
//  "  safe branch island %s created for %s\r\n"
    "  safe branch island (.*) created for (.*)\r?\n"};
// clang-format on

Map::Error Map::BranchIslands::Scan(const char*& head, const char* const tail,
                                    std::size_t& line_number)
{
  std::cmatch match;

  // TODO: I have only ever seen Branch Islands from Skylanders Swap Force, and on top of that, it
  // was an empty portion. From datamining MWLDEPPC, I can only assume it goes something like this.
  while (true)
  {
    if (std::regex_search(head, tail, match, re_branch_islands_created,
                          std::regex_constants::match_continuous))
    {
      line_number += 1u;
      head += match.length();
      this->units.emplace_back(match.str(1), match.str(2), false);
      continue;
    }
    if (std::regex_search(head, tail, match, re_branch_islands_created_safe,
                          std::regex_constants::match_continuous))
    {
      line_number += 1u;
      head += match.length();
      this->units.emplace_back(match.str(1), match.str(2), true);
      continue;
    }
    break;
  }
  return Error::None;
}

void Map::BranchIslands::Print(std::ostream& stream) const
{
  std::print(stream, "\r\nBranch Islands\r\n");
  for (const auto& unit : units)
    unit.Print(stream);
}
void Map::BranchIslands::Unit::Print(std::ostream& stream) const
{
  if (is_safe)
    //  "  safe branch island %s created for %s\r\n"
    std::print(stream, "  safe branch island {:s} created for {:s}\r\n", first_name, second_name);
  else
    //  "  branch island %s created for %s\r\n"
    std::print(stream, "  branch island {:s} created for {:s}\r\n", first_name, second_name);
}

Map::Error Map::LinktimeSizeDecreasingOptimizations::Scan(const char*&, const char* const,
                                                          std::size_t&)
{
  // TODO?  I am not convinced this portion is capable of containing anything.
  return Error::None;
}

void Map::LinktimeSizeDecreasingOptimizations::Print(std::ostream& stream) const
{
  std::print(stream, "\r\nLinktime size-decreasing optimizations\r\n");
}

Map::Error Map::LinktimeSizeIncreasingOptimizations::Scan(const char*&, const char* const,
                                                          std::size_t&)
{
  // TODO?  I am not convinced this portion is capable of containing anything.
  return Error::None;
}

void Map::LinktimeSizeIncreasingOptimizations::Print(std::ostream& stream) const
{
  std::print(stream, "\r\nLinktime size-increasing optimizations\r\n");
}

Map::SectionLayout::Unit::Trait Map::SectionLayout::DeduceUsualSubtext(
    const std::string& symbol_name, const std::string& module_name, const std::string& source_name,
    std::string& curr_module_name, std::string& curr_source_name, UnitLookup*& curr_unit_lookup,
    bool& is_in_lcomm, bool& is_after_eti_init_info, bool& is_multi_stt_section,
    const std::size_t line_number)
{
  const bool is_symbol_stt_section = (symbol_name == this->name);

  // Detect a change in compilation unit
  if (curr_module_name != module_name || curr_source_name != source_name)
  {
    curr_module_name = module_name;
    curr_source_name = source_name;
    is_multi_stt_section = false;
    const std::string& compilation_unit_name = source_name.empty() ? module_name : source_name;
    const bool is_repeat_compilation_unit_detected = this->lookup.contains(compilation_unit_name);
    curr_unit_lookup = &this->lookup[compilation_unit_name];

    if (is_symbol_stt_section)
    {
      if (is_repeat_compilation_unit_detected)
      {
        // TODO: At some point, a BSS section's second lap for printing .lcomm symbols was given
        // STT_SECTION symbols, making them indistinguishable from a repeat-name compilation unit
        // without further heuristics.  In other words, false positives ahoy.
        // TODO: What version?
        Warn::RepeatCompilationUnit(line_number, compilation_unit_name, this->name);
      }
      if (is_in_lcomm)
      {
        // Shouldn't this be impossible?
        Warn::CommAfterLComm(line_number);
        is_in_lcomm = false;
      }
      return Unit::Trait::Section;
    }
    else
    {
      if (this->section_kind == Kind::BSS)
      {
        // TODO: There is currently no clean way to detect repeat-name compilation units during
        // a BSS section's second lap for printing .lcomm symbols.
        is_in_lcomm = true;
        return Unit::Trait::LCommon;
      }
      if (this->section_kind == Kind::ExTabIndex)
      {
        if (symbol_name == "_eti_init_info" &&
            compilation_unit_name == "Linker Generated Symbol File")
        {
          is_after_eti_init_info = true;
        }
        // TODO: There is currently no clean way to detect repeat-name compilation units during
        // an extabindex section's second lap for printing UNUSED symbols after _eti_init_info.
        else if (is_repeat_compilation_unit_detected && !is_after_eti_init_info)
        {
          Warn::RepeatCompilationUnit(line_number, compilation_unit_name, this->name);
        }
        return Unit::Trait::ExTabIndex;
      }
    }
    return Unit::Trait::None;
  }
  if (is_symbol_stt_section)
  {
    if (this->section_kind == Kind::Ctors || this->section_kind == Kind::Dtors)
    {
      const std::string& compilation_unit_name = source_name.empty() ? module_name : source_name;
      Warn::RepeatCompilationUnit(line_number, compilation_unit_name, this->name);
    }
    else if (!is_multi_stt_section)
    {
      // Either this compilation unit was compiled with '-sym on', or two repeat-name compilation
      // units are adjacent to one another.
      const std::string& compilation_unit_name = source_name.empty() ? module_name : source_name;
      Warn::SymOnFlagDetected(line_number, compilation_unit_name, this->name);
      is_multi_stt_section = true;
    }
    return Unit::Trait::Section;
  }
  else
  {
    const std::string& compilation_unit_name = source_name.empty() ? module_name : source_name;
    if (curr_unit_lookup->contains(symbol_name))
    {
      // This can be a strong hint that there are two or more repeat-name compilation units in your
      // linker map, assuming it's not messed up in any way.  Note that this does not detect symbols
      // with identical names across section layouts.
      Warn::OneDefinitionRuleViolation(line_number, symbol_name, compilation_unit_name, this->name);
    }
  }
  if (is_in_lcomm)
    return Unit::Trait::LCommon;
  return Unit::Trait::None;
}

// clang-format off
static const std::regex re_section_layout_3column_unit_normal{
//  "  %08x %06x %08x %2i %s \t%s %s\r\n"
    "  ([0-9a-f]{8}) ([0-9a-f]{6}) ([0-9a-f]{8})  ?(\\d+) (.*) \t(.*) (.*)\r?\n"};
static const std::regex re_section_layout_3column_unit_unused{
//  "  UNUSED   %06x ........ %s %s %s\r\n"
    "  UNUSED   ([0-9a-f]{6}) \\.{8} (.*) (.*) (.*)\r?\n"};
static const std::regex re_section_layout_3column_unit_entry{
//  "  %08lx %06lx %08lx %s (entry of %s) \t%s %s\r\n"
    "  ([0-9a-f]{8}) ([0-9a-f]{6}) ([0-9a-f]{8}) (.*) \\(entry of (.*)\\) \t(.*) (.*)\r?\n"};
// clang-format on

Map::Error Map::SectionLayout::Scan3Column(const char*& head, const char* const tail,
                                           std::size_t& line_number)
{
  std::cmatch match;
  std::string curr_module_name, curr_source_name;
  UnitLookup* curr_unit_lookup = nullptr;
  bool is_in_lcomm = false, is_multi_stt_section = false, is_after_eti_init_info = false;

  while (true)
  {
    if (std::regex_search(head, tail, match, re_section_layout_3column_unit_normal,
                          std::regex_constants::match_continuous))
    {
      std::string symbol_name = match.str(5), module_name = match.str(6),
                  source_name = match.str(7);
      const Unit::Trait unit_trait = DeduceUsualSubtext(
          symbol_name, module_name, source_name, curr_module_name, curr_source_name,
          curr_unit_lookup, is_in_lcomm, is_multi_stt_section, is_after_eti_init_info, line_number);
      if (curr_unit_lookup == nullptr) [[unlikely]]
        return Error::Fail;
      line_number += 1u;
      head += match.length();
      const Unit& unit = this->units.emplace_back(
          unit_trait, xstoul(match.str(1)), xstoul(match.str(2)), xstoul(match.str(3)),
          std::stoi(match.str(4)), symbol_name, std::move(module_name), std::move(source_name));
      curr_unit_lookup->emplace(std::move(symbol_name), unit);
      continue;
    }
    if (std::regex_search(head, tail, match, re_section_layout_3column_unit_unused,
                          std::regex_constants::match_continuous))
    {
      std::string symbol_name = match.str(2), module_name = match.str(3),
                  source_name = match.str(4);
      const Unit::Trait unit_trait = DeduceUsualSubtext(
          symbol_name, module_name, source_name, curr_module_name, curr_source_name,
          curr_unit_lookup, is_in_lcomm, is_multi_stt_section, is_after_eti_init_info, line_number);
      if (curr_unit_lookup == nullptr) [[unlikely]]
        return Error::Fail;
      line_number += 1u;
      head += match.length();
      const Unit& unit = this->units.emplace_back(unit_trait, xstoul(match.str(1)), symbol_name,
                                                  std::move(module_name), std::move(source_name));
      curr_unit_lookup->emplace(std::move(symbol_name), unit);
      continue;
    }
    if (std::regex_search(head, tail, match, re_section_layout_3column_unit_entry,
                          std::regex_constants::match_continuous))
    {
      std::string symbol_name = match.str(4), entry_parent_name = match.str(5),
                  module_name = match.str(6), source_name = match.str(7);
      for (auto& parent_unit : std::ranges::reverse_view{this->units})
      {
        if (source_name != parent_unit.source_name || module_name != parent_unit.module_name)
          return Error::SectionLayoutOrphanedEntry;
        if (entry_parent_name != parent_unit.name)
          continue;
        // Should never be the STT_SECTION symbol. Also, this can never belong to a new compilation
        // unit (a new curr_unit_lookup) since that would inherently be an orphaned entry symbol.
        if (curr_unit_lookup->contains(symbol_name))
        {
          const std::string& compilation_unit_name =
              source_name.empty() ? module_name : source_name;
          Warn::OneDefinitionRuleViolation(line_number, symbol_name, compilation_unit_name,
                                           this->name);
        }
        if (curr_unit_lookup == nullptr)
          return Error::Fail;
        line_number += 1u;
        head += match.length();
        const Unit& unit = this->units.emplace_back(
            Unit::Trait::None, xstoul(match.str(1)), xstoul(match.str(2)), xstoul(match.str(3)),
            symbol_name, &parent_unit, std::move(module_name), std::move(source_name));
        curr_unit_lookup->emplace(std::move(symbol_name), unit);
        parent_unit.entry_children.push_back(&unit);
        goto ENTRY_PARENT_FOUND;  // I wish C++ had for-else clauses.
      }
      return Error::SectionLayoutOrphanedEntry;
    ENTRY_PARENT_FOUND:
      continue;
    }
    break;
  }
  return Error::None;
}

// clang-format off
static const std::regex re_section_layout_4column_unit_normal{
//  "  %08x %06x %08x %08x %2i %s \t%s %s\r\n"
    "  ([0-9a-f]{8}) ([0-9a-f]{6}) ([0-9a-f]{8}) ([0-9a-f]{8})  ?(\\d+) (.*) \t(.*) (.*)\r?\n"};
static const std::regex re_section_layout_4column_unit_unused{
//  "  UNUSED   %06x ........ ........    %s %s %s\r\n"
    "  UNUSED   ([0-9a-f]{6}) \\.{8} \\.{8}    (.*) (.*) (.*)\r?\n"};
static const std::regex re_section_layout_4column_unit_entry{
//  "  %08lx %06lx %08lx %08lx    %s (entry of %s) \t%s %s\r\n"
    "  ([0-9a-f]{8}) ([0-9a-f]{6}) ([0-9a-f]{8}) ([0-9a-f]{8})    (.*) \\(entry of (.*)\\) \t(.*) (.*)\r?\n"};
static const std::regex re_section_layout_4column_unit_special{
//  "  %08x %06x %08x %08x %2i %s\r\n"
    "  ([0-9a-f]{8}) ([0-9a-f]{6}) ([0-9a-f]{8}) ([0-9a-f]{8})  ?(\\d+) (.*)\r?\n"};
// clang-format on

Map::Error Map::SectionLayout::Scan4Column(const char*& head, const char* const tail,
                                           std::size_t& line_number)
{
  std::cmatch match;
  std::string curr_module_name, curr_source_name;
  UnitLookup* curr_unit_lookup = nullptr;
  bool is_in_lcomm = false, is_multi_stt_section = false, is_after_eti_init_info = false;

  while (true)
  {
    if (std::regex_search(head, tail, match, re_section_layout_4column_unit_normal,
                          std::regex_constants::match_continuous))
    {
      std::string symbol_name = match.str(6), module_name = match.str(7),
                  source_name = match.str(8);
      const Unit::Trait unit_trait = DeduceUsualSubtext(
          symbol_name, module_name, source_name, curr_module_name, curr_source_name,
          curr_unit_lookup, is_in_lcomm, is_multi_stt_section, is_after_eti_init_info, line_number);
      line_number += 1u;
      head += match.length();
      const Unit& unit = this->units.emplace_back(
          unit_trait, xstoul(match.str(1)), xstoul(match.str(2)), xstoul(match.str(3)),
          xstoul(match.str(4)), std::stoi(match.str(5)), symbol_name, std::move(module_name),
          std::move(source_name));
      curr_unit_lookup->emplace(std::move(symbol_name), unit);
      continue;
    }
    if (std::regex_search(head, tail, match, re_section_layout_4column_unit_unused,
                          std::regex_constants::match_continuous))
    {
      std::string symbol_name = match.str(2), module_name = match.str(3),
                  source_name = match.str(4);
      const Unit::Trait unit_trait = DeduceUsualSubtext(
          symbol_name, module_name, source_name, curr_module_name, curr_source_name,
          curr_unit_lookup, is_in_lcomm, is_multi_stt_section, is_after_eti_init_info, line_number);
      line_number += 1u;
      head += match.length();
      const Unit& unit = this->units.emplace_back(unit_trait, xstoul(match.str(1)), symbol_name,
                                                  std::move(module_name), std::move(source_name));
      curr_unit_lookup->emplace(std::move(symbol_name), unit);
      continue;
    }
    if (std::regex_search(head, tail, match, re_section_layout_4column_unit_entry,
                          std::regex_constants::match_continuous))
    {
      std::string symbol_name = match.str(5), entry_parent_name = match.str(6),
                  module_name = match.str(7), source_name = match.str(8);
      for (auto& parent_unit : std::ranges::reverse_view{this->units})
      {
        if (source_name != parent_unit.source_name || module_name != parent_unit.module_name)
          return Error::SectionLayoutOrphanedEntry;
        if (entry_parent_name != parent_unit.name)
          continue;
        // Should never be the STT_SECTION symbol. Also, this can never belong to a new compilation
        // unit (a new curr_unit_lookup) since that would inherently be an orphaned entry symbol.
        if (curr_unit_lookup->contains(symbol_name))
        {
          const std::string& compilation_unit_name =
              source_name.empty() ? module_name : source_name;
          Warn::OneDefinitionRuleViolation(line_number, symbol_name, compilation_unit_name,
                                           this->name);
        }
        line_number += 1u;
        head += match.length();
        const Unit& unit =
            this->units.emplace_back(Unit::Trait::None, xstoul(match.str(1)), xstoul(match.str(2)),
                                     xstoul(match.str(3)), xstoul(match.str(4)), symbol_name,
                                     &parent_unit, std::move(module_name), std::move(source_name));
        curr_unit_lookup->emplace(std::move(symbol_name), unit);
        parent_unit.entry_children.push_back(&unit);
        goto ENTRY_PARENT_FOUND;  // I wish C++ had for-else clauses.
      }
      return Error::SectionLayoutOrphanedEntry;
    ENTRY_PARENT_FOUND:
      continue;
    }
    if (std::regex_search(head, tail, match, re_section_layout_4column_unit_special,
                          std::regex_constants::match_continuous))
    {
      // Special symbols don't belong to any compilation unit, so they don't go in any lookup.
      std::string_view special_name = ToStringView(match[6]);
      if (special_name == "*fill*")
      {
        line_number += 1u;
        head += match.length();
        this->units.emplace_back(Unit::Trait::Fill1, xstoul(match.str(1)), xstoul(match.str(2)),
                                 xstoul(match.str(3)), xstoul(match.str(4)),
                                 std::stoi(match.str(5)));
        continue;
      }
      if (special_name == "**fill**")
      {
        line_number += 1u;
        head += match.length();
        this->units.emplace_back(Unit::Trait::Fill2, xstoul(match.str(1)), xstoul(match.str(2)),
                                 xstoul(match.str(3)), xstoul(match.str(4)),
                                 std::stoi(match.str(5)));
        continue;
      }
      return Error::SectionLayoutSpecialNotFill;
    }
    break;
  }
  return Error::None;
}

// clang-format off
static const std::regex re_section_layout_tloztp_unit_entry{
    "  ([0-9a-f]{8}) ([0-9a-f]{6}) ([0-9a-f]{8})    (.*) \\(entry of (.*)\\) \t(.*) (.*)\r?\n"};
static const std::regex re_section_layout_tloztp_unit_special{
    "  ([0-9a-f]{8}) ([0-9a-f]{6}) ([0-9a-f]{8})  ?(\\d+) (.*)\r?\n"};
// clang-format on

Map::Error Map::SectionLayout::ScanTLOZTP(const char*& head, const char* const tail,
                                          std::size_t& line_number)
{
  std::cmatch match;
  std::string curr_module_name, curr_source_name;
  UnitLookup* curr_unit_lookup = nullptr;
  bool is_in_lcomm = false, is_multi_stt_section = false, is_after_eti_init_info = false;

  while (true)
  {
    if (std::regex_search(head, tail, match, re_section_layout_3column_unit_normal,
                          std::regex_constants::match_continuous))
    {
      std::string symbol_name = match.str(5), module_name = match.str(6),
                  source_name = match.str(7);
      const Unit::Trait unit_trait = DeduceUsualSubtext(
          symbol_name, module_name, source_name, curr_module_name, curr_source_name,
          curr_unit_lookup, is_in_lcomm, is_multi_stt_section, is_after_eti_init_info, line_number);
      line_number += 1u;
      head += match.length();
      const Unit& unit = this->units.emplace_back(
          unit_trait, xstoul(match.str(1)), xstoul(match.str(2)), xstoul(match.str(3)), 0,
          std::stoi(match.str(4)), symbol_name, std::move(module_name), std::move(source_name));
      curr_unit_lookup->emplace(std::move(symbol_name), unit);
      continue;
    }
    if (std::regex_search(head, tail, match, re_section_layout_tloztp_unit_entry,
                          std::regex_constants::match_continuous))
    {
      std::string symbol_name = match.str(4), entry_parent_name = match.str(5),
                  module_name = match.str(6), source_name = match.str(7);
      for (auto& parent_unit : std::ranges::reverse_view{this->units})
      {
        if (source_name != parent_unit.source_name || module_name != parent_unit.module_name)
          return Error::SectionLayoutOrphanedEntry;
        if (entry_parent_name != parent_unit.name)
          continue;
        // Should never be the STT_SECTION symbol. Also, this can never belong to a new compilation
        // unit (a new curr_unit_lookup) since that would inherently be an orphaned entry symbol.
        if (curr_unit_lookup->contains(symbol_name))
        {
          const std::string& compilation_unit_name =
              source_name.empty() ? module_name : source_name;
          Warn::OneDefinitionRuleViolation(line_number, symbol_name, compilation_unit_name,
                                           this->name);
        }
        line_number += 1u;
        head += match.length();
        const Unit& unit = this->units.emplace_back(
            Unit::Trait::None, xstoul(match.str(1)), xstoul(match.str(2)), xstoul(match.str(3)), 0,
            symbol_name, &parent_unit, std::move(module_name), std::move(source_name));
        curr_unit_lookup->emplace(std::move(symbol_name), unit);
        parent_unit.entry_children.push_back(&unit);
        goto ENTRY_PARENT_FOUND;  // I wish C++ had for-else clauses.
      }
      return Error::SectionLayoutOrphanedEntry;
    ENTRY_PARENT_FOUND:
      continue;
    }
    if (std::regex_search(head, tail, match, re_section_layout_tloztp_unit_special,
                          std::regex_constants::match_continuous))
    {
      // Special symbols don't belong to any compilation unit, so they don't go in any lookup.
      std::string_view special_name = ToStringView(match[5]);
      if (special_name == "*fill*")
      {
        line_number += 1u;
        head += match.length();
        this->units.emplace_back(Unit::Trait::Fill1, xstoul(match.str(1)), xstoul(match.str(2)),
                                 xstoul(match.str(3)), 0, std::stoi(match.str(4)));
        continue;
      }
      if (special_name == "**fill**")
      {
        line_number += 1u;
        head += match.length();
        this->units.emplace_back(Unit::Trait::Fill2, xstoul(match.str(1)), xstoul(match.str(2)),
                                 xstoul(match.str(3)), 0, std::stoi(match.str(4)));
        continue;
      }
      return Error::SectionLayoutSpecialNotFill;
    }
    break;
  }
  return Error::None;
}

void Map::SectionLayout::Print(std::ostream& stream) const
{
  // "\r\n\r\n%s section layout\r\n"
  std::print(stream, "\r\n\r\n{:s} section layout\r\n", name);
  if (min_version < Version::version_3_0_4)
  {
    std::print(stream, "  Starting        Virtual\r\n");
    std::print(stream, "  address  Size   address\r\n");
    std::print(stream, "  -----------------------\r\n");
    for (const auto& unit : this->units)
      unit.Print3Column(stream);
  }
  else
  {
    std::print(stream, "  Starting        Virtual  File\r\n");
    std::print(stream, "  address  Size   address  offset\r\n");
    std::print(stream, "  ---------------------------------\r\n");
    for (const auto& unit : this->units)
      unit.Print4Column(stream);
  }
}

void Map::SectionLayout::Unit::Print3Column(std::ostream& stream) const
{
  switch (this->unit_kind)
  {
  case Kind::Normal:
    // "  %08x %06x %08x %2i %s \t%s %s\r\n"
    std::print(stream, "  {:08x} {:06x} {:08x} {:2d} {:s} \t{:s} {:s}\r\n", starting_address, size,
               virtual_address, alignment, name, module_name, source_name);
    return;
  case Kind::Unused:
    // "  UNUSED   %06x ........ %s %s %s\r\n"
    std::print(stream, "  UNUSED   {:06x} ........ {:s} {:s} {:s}\r\n", size, name, module_name,
               source_name);
    return;
  case Kind::Entry:
    // "  %08lx %06lx %08lx %s (entry of %s) \t%s %s\r\n"
    std::print(stream, "  {:08x} {:06x} {:08x} {:s} (entry of {:s}) \t{:s} {:s}\r\n",
               starting_address, size, virtual_address, name, entry_parent->name, module_name,
               source_name);
    return;
  case Kind::Special:
    ASSERT(false);
    return;
  }
}

void Map::SectionLayout::Unit::Print4Column(std::ostream& stream) const
{
  switch (this->unit_kind)
  {
  case Kind::Normal:
    // "  %08x %06x %08x %08x %2i %s \t%s %s\r\n"
    std::print(stream, "  {:08x} {:06x} {:08x} {:08x} {:2d} {:s} \t{:s} {:s}\r\n", starting_address,
               size, virtual_address, file_offset, alignment, name, module_name, source_name);
    return;
  case Kind::Unused:
    // "  UNUSED   %06x ........ ........    %s %s %s\r\n"
    std::print(stream, "  UNUSED   {:06x} ........ ........    {:s} {:s} {:s}\r\n", size, name,
               module_name, source_name);
    return;
  case Kind::Entry:
    // "  %08lx %06lx %08lx %08lx    %s (entry of %s) \t%s %s\r\n"
    std::print(stream, "  {:08x} {:06x} {:08x} {:08x}    {:s} (entry of {:s}) \t{:s} {:s}\r\n",
               starting_address, size, virtual_address, file_offset, name, entry_parent->name,
               module_name, source_name);
    return;
  case Kind::Special:
    // "  %08x %06x %08x %08x %2i %s\r\n"
    std::print(stream, "  {:08x} {:06x} {:08x} {:08x} {:2d} {:s}\r\n", starting_address, size,
               virtual_address, file_offset, alignment, ToSpecialName(unit_trait));
    return;
  }
}

std::string_view Map::SectionLayout::Unit::ToSpecialName(const Trait unit_trait)
{
  static constexpr std::string_view fill1 = "*fill*";
  static constexpr std::string_view fill2 = "**fill**";

  if (unit_trait == Unit::Trait::Fill1)
    return fill1;
  if (unit_trait == Unit::Trait::Fill2)
    return fill2;
  ASSERT(false);
  return fill1;
}

// void Map::SectionLayout::Export(Report& report) const noexcept
// {
//   for (const auto& unit : this->units)
//     unit.Export(report);
// }

// void Map::SectionLayout::Unit::Export(Report& report) const noexcept
// {
//   auto& debug_info = report[source_name.empty() ? module_name : source_name][name];
//   debug_info.section_layout_unit = this;
// }

// clang-format off
static const std::regex re_memory_map_unit_normal_simple_old{
//  "  %15s  %08x %08x %08x\r\n"
    "   {0,15}(.*)  ([0-9a-f]{8}) ([0-9a-f]{8}) ([0-9a-f]{8})\r?\n"};
// clang-format on

Map::Error Map::MemoryMap::ScanSimple_old(const char*& head, const char* const tail,
                                          std::size_t& line_number)
{
  std::cmatch match;

  while (std::regex_search(head, tail, match, re_memory_map_unit_normal_simple_old,
                           std::regex_constants::match_continuous))
  {
    line_number += 1u;
    head += match.length();
    this->normal_units.emplace_back(match.str(1), xstoul(match.str(2)), xstoul(match.str(3)),
                                    xstoul(match.str(4)));
  }
  return ScanDebug_old(head, tail, line_number);
}

// clang-format off
static const std::regex re_memory_map_unit_normal_romram_old{
//  "  %15s  %08x %08x %08x %08x %08x\r\n"
    "   {0,15}(.*)  ([0-9a-f]{8}) ([0-9a-f]{8}) ([0-9a-f]{8}) ([0-9a-f]{8}) ([0-9a-f]{8})\r?\n"};
// clang-format on

Map::Error Map::MemoryMap::ScanRomRam_old(const char*& head, const char* const tail,
                                          std::size_t& line_number)
{
  std::cmatch match;

  while (std::regex_search(head, tail, match, re_memory_map_unit_normal_romram_old,
                           std::regex_constants::match_continuous))
  {
    line_number += 1u;
    head += match.length();
    this->normal_units.emplace_back(match.str(1), xstoul(match.str(2)), xstoul(match.str(3)),
                                    xstoul(match.str(4)), xstoul(match.str(5)),
                                    xstoul(match.str(6)));
  }
  return ScanDebug_old(head, tail, line_number);
}

// clang-format off
static const std::regex re_memory_map_unit_debug_old{
//  "  %15s           %06x %08x\r\n" <-- Sometimes the size can overflow six digits
//  "  %15s           %08x %08x\r\n" <-- Starting with CodeWarrior for GCN 2.7
    "   {0,15}(.*)           ([0-9a-f]{6,8}) ([0-9a-f]{8})\r?\n"};
// clang-format on

Map::Error Map::MemoryMap::ScanDebug_old(const char*& head, const char* const tail,
                                         std::size_t& line_number)
{
  std::cmatch match;

  while (std::regex_search(head, tail, match, re_memory_map_unit_debug_old,
                           std::regex_constants::match_continuous))
  {
    line_number += 1u;
    head += match.length();
    std::string size = match.str(2);
    if (size.length() == 8 && size.front() == '0')  // Make sure it's not just an overflowed value
      this->SetMinVersion(Version::version_3_0_4);
    this->debug_units.emplace_back(match.str(1), xstoul(size), xstoul(match.str(3)));
  }
  return Error::None;
}

// clang-format off
static const std::regex re_memory_map_unit_normal_simple{
//  "  %20s %08x %08x %08x\r\n"
    "   {0,20}(.*) ([0-9a-f]{8}) ([0-9a-f]{8}) ([0-9a-f]{8})\r?\n"};
// clang-format on

Map::Error Map::MemoryMap::ScanSimple(const char*& head, const char* const tail,
                                      std::size_t& line_number)
{
  std::cmatch match;

  while (std::regex_search(head, tail, match, re_memory_map_unit_normal_simple,
                           std::regex_constants::match_continuous))
  {
    line_number += 1u;
    head += match.length();
    this->normal_units.emplace_back(match.str(1), xstoul(match.str(2)), xstoul(match.str(3)),
                                    xstoul(match.str(4)));
  }
  return ScanDebug(head, tail, line_number);
}

// clang-format off
static const std::regex re_memory_map_unit_normal_romram{
//  "  %20s %08x %08x %08x %08x %08x\r\n"
    "   {0,20}(.*) ([0-9a-f]{8}) ([0-9a-f]{8}) ([0-9a-f]{8}) ([0-9a-f]{8}) ([0-9a-f]{8})\r?\n"};
// clang-format on

Map::Error Map::MemoryMap::ScanRomRam(const char*& head, const char* const tail,
                                      std::size_t& line_number)
{
  std::cmatch match;

  while (std::regex_search(head, tail, match, re_memory_map_unit_normal_romram,
                           std::regex_constants::match_continuous))
  {
    line_number += 1u;
    head += match.length();
    this->normal_units.emplace_back(match.str(1), xstoul(match.str(2)), xstoul(match.str(3)),
                                    xstoul(match.str(4)), xstoul(match.str(5)),
                                    xstoul(match.str(6)));
  }
  return ScanDebug(head, tail, line_number);
}

// clang-format off
static const std::regex re_memory_map_unit_normal_srecord{
//  "  %20s %08x %08x %08x %10i\r\n"
    "   {0,20}(.*) ([0-9a-f]{8}) ([0-9a-f]{8}) ([0-9a-f]{8})  {0,9}(\\d+)\r?\n"};
// clang-format on

Map::Error Map::MemoryMap::ScanSRecord(const char*& head, const char* const tail,
                                       std::size_t& line_number)
{
  std::cmatch match;

  while (std::regex_search(head, tail, match, re_memory_map_unit_normal_srecord,
                           std::regex_constants::match_continuous))
  {
    line_number += 1u;
    head += match.length();
    this->normal_units.emplace_back(match.str(1), xstoul(match.str(2)), xstoul(match.str(3)),
                                    xstoul(match.str(4)), std::stoi(match.str(5)));
  }
  return ScanDebug(head, tail, line_number);
}

// clang-format off
static const std::regex re_memory_map_unit_normal_binfile{
//  "  %20s %08x %08x %08x %08x %s\r\n"
    "   {0,20}(.*) ([0-9a-f]{8}) ([0-9a-f]{8}) ([0-9a-f]{8}) ([0-9a-f]{8}) (.*)\r?\n"};
// clang-format on

Map::Error Map::MemoryMap::ScanBinFile(const char*& head, const char* const tail,
                                       std::size_t& line_number)
{
  std::cmatch match;

  while (std::regex_search(head, tail, match, re_memory_map_unit_normal_binfile,
                           std::regex_constants::match_continuous))
  {
    line_number += 1u;
    head += match.length();
    this->normal_units.emplace_back(match.str(1), xstoul(match.str(2)), xstoul(match.str(3)),
                                    xstoul(match.str(4)), xstoul(match.str(5)), match.str(6));
  }
  return ScanDebug(head, tail, line_number);
}

// clang-format off
static const std::regex re_memory_map_unit_normal_romram_srecord{
//  "  %20s %08x %08x %08x %08x %08x %10i\r\n"
    "   {0,20}(.*) ([0-9a-f]{8}) ([0-9a-f]{8}) ([0-9a-f]{8}) ([0-9a-f]{8}) ([0-9a-f]{8})  {0,9}(\\d+)\r?\n"};
// clang-format on

Map::Error Map::MemoryMap::ScanRomRamSRecord(const char*& head, const char* const tail,
                                             std::size_t& line_number)
{
  std::cmatch match;

  while (std::regex_search(head, tail, match, re_memory_map_unit_normal_romram_srecord,
                           std::regex_constants::match_continuous))
  {
    line_number += 1u;
    head += match.length();
    this->normal_units.emplace_back(match.str(1), xstoul(match.str(2)), xstoul(match.str(3)),
                                    xstoul(match.str(4)), xstoul(match.str(5)),
                                    xstoul(match.str(6)), std::stoi(match.str(7)));
  }
  return ScanDebug(head, tail, line_number);
}

// clang-format off
static const std::regex re_memory_map_unit_normal_romram_binfile{
//  "  %20s %08x %08x %08x %08x %08x   %08x %s\r\n"
    "   {0,20}(.*) ([0-9a-f]{8}) ([0-9a-f]{8}) ([0-9a-f]{8}) ([0-9a-f]{8}) ([0-9a-f]{8})   ([0-9a-f]{8}) (.*)\r?\n"};
// clang-format on

Map::Error Map::MemoryMap::ScanRomRamBinFile(const char*& head, const char* const tail,
                                             std::size_t& line_number)
{
  std::cmatch match;

  while (std::regex_search(head, tail, match, re_memory_map_unit_normal_romram_binfile,
                           std::regex_constants::match_continuous))
  {
    line_number += 1u;
    head += match.length();
    this->normal_units.emplace_back(match.str(1), xstoul(match.str(2)), xstoul(match.str(3)),
                                    xstoul(match.str(4)), xstoul(match.str(5)),
                                    xstoul(match.str(6)), xstoul(match.str(7)), match.str(8));
  }
  return ScanDebug(head, tail, line_number);
}

// clang-format off
static const std::regex re_memory_map_unit_normal_srecord_binfile{
//  "  %20s %08x %08x %08x  %10i %08x %s\r\n"
    "   {0,20}(.*) ([0-9a-f]{8}) ([0-9a-f]{8}) ([0-9a-f]{8})   {0,9}(\\d+) ([0-9a-f]{8}) (.*)\r?\n"};
// clang-format on

Map::Error Map::MemoryMap::ScanSRecordBinFile(const char*& head, const char* const tail,
                                              std::size_t& line_number)
{
  std::cmatch match;

  while (std::regex_search(head, tail, match, re_memory_map_unit_normal_srecord_binfile,
                           std::regex_constants::match_continuous))
  {
    line_number += 1u;
    head += match.length();
    this->normal_units.emplace_back(match.str(1), xstoul(match.str(2)), xstoul(match.str(3)),
                                    xstoul(match.str(4)), std::stoi(match.str(5)),
                                    xstoul(match.str(6)), match.str(7));
  }
  return ScanDebug(head, tail, line_number);
}

// clang-format off
static const std::regex re_memory_map_unit_normal_romram_srecord_binfile{
//  "  %20s %08x %08x %08x %08x %08x    %10i %08x %s\r\n"
    "   {0,20}(.*) ([0-9a-f]{8}) ([0-9a-f]{8}) ([0-9a-f]{8}) ([0-9a-f]{8}) ([0-9a-f]{8})     {0,9}(\\d+) ([0-9a-f]{8}) (.*)\r?\n"};
// clang-format on

Map::Error Map::MemoryMap::ScanRomRamSRecordBinFile(const char*& head, const char* const tail,
                                                    std::size_t& line_number)
{
  std::cmatch match;

  while (std::regex_search(head, tail, match, re_memory_map_unit_normal_romram_srecord_binfile,
                           std::regex_constants::match_continuous))
  {
    line_number += 1u;
    head += match.length();
    this->normal_units.emplace_back(match.str(1), xstoul(match.str(2)), xstoul(match.str(3)),
                                    xstoul(match.str(4)), xstoul(match.str(5)),
                                    xstoul(match.str(6)), std::stoi(match.str(7)),
                                    xstoul(match.str(8)), match.str(9));
  }
  return ScanDebug(head, tail, line_number);
}

// clang-format off
static const std::regex re_memory_map_unit_debug{
//  "  %20s          %08x %08x\r\n"
    "   {0,20}(.*)          ([0-9a-f]{8}) ([0-9a-f]{8})\r?\n"};
// clang-format on

Map::Error Map::MemoryMap::ScanDebug(const char*& head, const char* const tail,
                                     std::size_t& line_number)
{
  std::cmatch match;

  while (std::regex_search(head, tail, match, re_memory_map_unit_debug,
                           std::regex_constants::match_continuous))
  {
    line_number += 1u;
    head += match.length();
    this->debug_units.emplace_back(match.str(1), xstoul(match.str(2)), xstoul(match.str(3)));
  }
  return Error::None;
}

void Map::MemoryMap::Print(std::ostream& stream) const
{
  std::print(stream, "\r\n\r\nMemory map:\r\n");
  if (min_version < Version::version_4_2_build_142)
  {
    if (has_rom_ram)
      PrintRomRam_old(stream);
    else
      PrintSimple_old(stream);
    PrintDebug_old(stream);
  }
  else
  {
    if (has_rom_ram)
      if (has_s_record)
        if (has_bin_file)
          PrintRomRamSRecordBinFile(stream);
        else
          PrintRomRamSRecord(stream);
      else if (has_bin_file)
        PrintRomRamBinFile(stream);
      else
        PrintRomRam(stream);
    else if (has_s_record)
      if (has_bin_file)
        PrintSRecordBinFile(stream);
      else
        PrintSRecord(stream);
    else if (has_bin_file)
      PrintBinFile(stream);
    else
      PrintSimple(stream);
    PrintDebug(stream);
  }
}

void Map::MemoryMap::PrintSimple_old(std::ostream& stream) const
{
  // clang-format off
  std::print(stream, "                   Starting Size     File\r\n");
  std::print(stream, "                   address           Offset\r\n");
  // clang-format on
  for (const auto& unit : normal_units)
    unit.PrintSimple_old(stream);
}
void Map::MemoryMap::UnitNormal::PrintSimple_old(std::ostream& stream) const
{
  // "  %15s  %08x %08x %08x\r\n"
  std::print(stream, "  {:>15s}  {:08x} {:08x} {:08x}\r\n", name, starting_address, size,
             file_offset);
}

void Map::MemoryMap::PrintRomRam_old(std::ostream& stream) const
{
  // clang-format off
  std::print(stream, "                   Starting Size     File     ROM      RAM Buffer\r\n");
  std::print(stream, "                   address           Offset   Address  Address\r\n");
  // clang-format on
  for (const auto& unit : normal_units)
    unit.PrintRomRam_old(stream);
}
void Map::MemoryMap::UnitNormal::PrintRomRam_old(std::ostream& stream) const
{
  // "  %15s  %08x %08x %08x %08x %08x\r\n"
  std::print(stream, "  {:>15s}  {:08x} {:08x} {:08x} {:08x} {:08x}\r\n", name, starting_address,
             size, file_offset, rom_address, ram_buffer_address);
}

void Map::MemoryMap::PrintDebug_old(std::ostream& stream) const
{
  if (min_version < Version::version_3_0_4)
    for (const auto& unit : debug_units)
      unit.Print_older(stream);
  else
    for (const auto& unit : debug_units)
      unit.Print_old(stream);
}
void Map::MemoryMap::UnitDebug::Print_older(std::ostream& stream) const
{
  // "  %15s           %06x %08x\r\n"
  std::print(stream, "  {:>15s}           {:06x} {:08x}\r\n", name, size, file_offset);
}
void Map::MemoryMap::UnitDebug::Print_old(std::ostream& stream) const
{
  // "  %15s           %08x %08x\r\n"
  std::print(stream, "  {:>15s}           {:08x} {:08x}\r\n", name, size, file_offset);
}

void Map::MemoryMap::PrintSimple(std::ostream& stream) const
{
  // clang-format off
  std::print(stream, "                       Starting Size     File\r\n");
  std::print(stream, "                       address           Offset\r\n");
  // clang-format on
  for (const auto& unit : normal_units)
    unit.PrintSimple(stream);
}
void Map::MemoryMap::UnitNormal::PrintSimple(std::ostream& stream) const
{
  // "  %20s %08x %08x %08x\r\n"
  std::print(stream, "  {:>20s} {:08x} {:08x} {:08x}\r\n", name, starting_address, size,
             file_offset);
}

void Map::MemoryMap::PrintRomRam(std::ostream& stream) const
{
  // clang-format off
  std::print(stream, "                       Starting Size     File     ROM      RAM Buffer\r\n");
  std::print(stream, "                       address           Offset   Address  Address\r\n");
  // clang-format on
  for (const auto& unit : normal_units)
    unit.PrintRomRam(stream);
}
void Map::MemoryMap::UnitNormal::PrintRomRam(std::ostream& stream) const
{
  // "  %20s %08x %08x %08x %08x %08x\r\n"
  std::print(stream, "  {:>20s} {:08x} {:08x} {:08x} {:08x} {:08x}\r\n", name, starting_address,
             size, file_offset, rom_address, ram_buffer_address);
}

void Map::MemoryMap::PrintSRecord(std::ostream& stream) const
{
  // clang-format off
  std::print(stream, "                       Starting Size     File       S-Record\r\n");
  std::print(stream, "                       address           Offset     Line\r\n");
  // clang-format on
  for (const auto& unit : normal_units)
    unit.PrintSRecord(stream);
}
void Map::MemoryMap::UnitNormal::PrintSRecord(std::ostream& stream) const
{
  // "  %20s %08x %08x %08x %10i\r\n"
  std::print(stream, "  {:>20s} {:08x} {:08x} {:08x} {:10d}\r\n", name, starting_address, size,
             file_offset, s_record_line);
}

void Map::MemoryMap::PrintBinFile(std::ostream& stream) const
{
  // clang-format off
  std::print(stream, "                       Starting Size     File     Bin File Bin File\r\n");
  std::print(stream, "                       address           Offset   Offset   Name\r\n");
  // clang-format on
  for (const auto& unit : normal_units)
    unit.PrintBinFile(stream);
}
void Map::MemoryMap::UnitNormal::PrintBinFile(std::ostream& stream) const
{
  // "  %20s %08x %08x %08x %08x %s\r\n"
  std::print(stream, "  {:>20s} {:08x} {:08x} {:08x} {:08x} {:s}\r\n", name, starting_address, size,
             file_offset, bin_file_offset, bin_file_name);
}

void Map::MemoryMap::PrintRomRamSRecord(std::ostream& stream) const
{
  // clang-format off
  std::print(stream, "                       Starting Size     File     ROM      RAM Buffer  S-Record\r\n");
  std::print(stream, "                       address           Offset   Address  Address     Line\r\n");
  // clang-format on
  for (const auto& unit : normal_units)
    unit.PrintRomRamSRecord(stream);
}
void Map::MemoryMap::UnitNormal::PrintRomRamSRecord(std::ostream& stream) const
{
  // "  %20s %08x %08x %08x %08x %08x %10i\r\n"
  std::print(stream, "  {:>20s} {:08x} {:08x} {:08x} {:08x} {:08x} {:10d}\r\n", name,
             starting_address, size, file_offset, rom_address, ram_buffer_address, s_record_line);
}

void Map::MemoryMap::PrintRomRamBinFile(std::ostream& stream) const
{
  // clang-format off
  std::print(stream, "                       Starting Size     File     ROM      RAM Buffer Bin File Bin File\r\n");
  std::print(stream, "                       address           Offset   Address  Address    Offset   Name\r\n");
  // clang-format on
  for (const auto& unit : normal_units)
    unit.PrintRomRamBinFile(stream);
}
void Map::MemoryMap::UnitNormal::PrintRomRamBinFile(std::ostream& stream) const
{
  // "  %20s %08x %08x %08x %08x %08x   %08x %s\r\n"
  std::print(stream, "  {:>20s} {:08x} {:08x} {:08x} {:08x} {:08x}   {:08x} {:s}\r\n", name,
             starting_address, size, file_offset, rom_address, ram_buffer_address, bin_file_offset,
             bin_file_name);
}

void Map::MemoryMap::PrintSRecordBinFile(std::ostream& stream) const
{
  // clang-format off
  std::print(stream, "                       Starting Size     File        S-Record Bin File Bin File\r\n");
  std::print(stream, "                       address           Offset      Line     Offset   Name\r\n");
  // clang-format on
  for (const auto& unit : normal_units)
    unit.PrintSRecordBinFile(stream);
}
void Map::MemoryMap::UnitNormal::PrintSRecordBinFile(std::ostream& stream) const
{
  // "  %20s %08x %08x %08x  %10i %08x %s\r\n"
  std::print(stream, "  {:>20s} {:08x} {:08x} {:08x}  {:10d} {:08x} {:s}\r\n", name,
             starting_address, size, file_offset, s_record_line, bin_file_offset, bin_file_name);
}

void Map::MemoryMap::PrintRomRamSRecordBinFile(std::ostream& stream) const
{
  // clang-format off
  std::print(stream, "                       Starting Size     File     ROM      RAM Buffer    S-Record Bin File Bin File\r\n");
  std::print(stream, "                       address           Offset   Address  Address       Line     Offset   Name\r\n");
  // clang-format on
  for (const auto& unit : normal_units)
    unit.PrintRomRamSRecordBinFile(stream);
}
void Map::MemoryMap::UnitNormal::PrintRomRamSRecordBinFile(std::ostream& stream) const
{
  // "  %20s %08x %08x %08x %08x %08x    %10i %08x %s\r\n"
  std::print(stream, "  {:>20s} {:08x} {:08x} {:08x} {:08x} {:08x}    {:10d} {:08x} {:s}\r\n", name,
             starting_address, size, file_offset, rom_address, ram_buffer_address, s_record_line,
             bin_file_offset, bin_file_name);
}

void Map::MemoryMap::PrintDebug(std::ostream& stream) const
{
  for (const auto& unit : debug_units)
    unit.Print(stream);
}
void Map::MemoryMap::UnitDebug::Print(std::ostream& stream) const
{
  // "  %20s          %08x %08x\r\n"
  std::print(stream, "  {:>20s}          {:08x} {:08x}\r\n", name, size, file_offset);
}

// clang-format off
static const std::regex re_linker_generated_symbols_unit{
//  "%25s %08x\r\n"
    " {0,25}(.*) ([0-9a-f]{8})\r?\n"};
// clang-format on

Map::Error Map::LinkerGeneratedSymbols::Scan(const char*& head, const char* const tail,
                                             std::size_t& line_number)
{
  std::cmatch match;

  while (std::regex_search(head, tail, match, re_linker_generated_symbols_unit,
                           std::regex_constants::match_continuous))
  {
    line_number += 1u;
    head += match.length();
    this->units.emplace_back(match.str(1), xstoul(match.str(2)));
  }
  return Error::None;
}

void Map::LinkerGeneratedSymbols::Print(std::ostream& stream) const
{
  std::print(stream, "\r\n\r\nLinker generated symbols:\r\n");
  for (const auto& unit : units)
    unit.Print(stream);
}

void Map::LinkerGeneratedSymbols::Unit::Print(std::ostream& stream) const
{
  // "%25s %08x\r\n"
  std::print(stream, "{:>25s} {:08x}\r\n", name, value);
}

// void Map::LinkerGeneratedSymbols::Export(Report& report) const noexcept
// {
//   auto& subreport = report[""];
//   for (const auto& unit : units)
//     subreport[unit.name].linker_generated_symbol_unit = &unit;
// }
}  // namespace MWLinker

namespace Common
{
};  // namespace Common
