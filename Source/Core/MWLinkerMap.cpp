#include "MWLinkerMap.h"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <map>
#include <ostream>
#include <ranges>
#include <regex>
#include <string>
#include <string_view>
#include <utility>

#include "PrintUtil.h"
#include "RegexUtil.h"

// Metrowerks linker maps should be considered binary files containing text with CRLF line endings.
// To account for outside factors, though, this program can support both CRLF and LF line endings.

// This program uses std::regex in ECMAScript mode, which never matches '\r' or '\n' with the '.'
// metacharacter. If you want to try adapting this program to a different regular expression flavor,
// make sure it still follows that rule.

namespace MWLinker
{
bool Map::SymbolClosure::Warn::do_warn_odr_violation = true;

void Map::SymbolClosure::Warn::OneDefinitionRuleViolation(
    const std::size_t line_number, const std::string_view symbol_name,
    const std::string_view compilation_unit_name)
{
  // For legal linker maps, this should only ever happen in repeat-name compilation units.
  if (!do_warn_odr_violation)
    return;
  mijo::println(std::cerr, "Line {:d}] \"{:s}\" seen again in \"{:s}\"", line_number, symbol_name,
                compilation_unit_name);
}

bool Map::SymbolClosure::Warn::do_warn_sym_on_flag_detected = true;

void Map::SymbolClosure::Warn::SymOnFlagDetected(const std::size_t line_number,
                                                 const std::string_view compilation_unit_name)
{
  // Multiple STT_SECTION symbols were seen in an uninterrupted compilation unit.  This could be
  // a false positive, and in turn would be a false negative for a RepeatCompilationUnit warning.
  if (!do_warn_sym_on_flag_detected)
    return;
  mijo::println(std::cerr, "Line {:d}] Detected '-sym on' flag in \"{:s}\" (.text)", line_number,
                compilation_unit_name);
}

bool Map::EPPC_PatternMatching::Warn::do_warn_merging_odr_violation = true;

void Map::EPPC_PatternMatching::Warn::MergingOneDefinitionRuleViolation(
    const std::size_t line_number, const std::string_view symbol_name)
{
  // Could be a false positive, as code merging has no information about where the symbol came from.
  if (!do_warn_merging_odr_violation)
    return;
  mijo::println(std::cerr, "Line {:d}] \"{:s}\" seen again", line_number, symbol_name);
}

bool Map::EPPC_PatternMatching::Warn::do_warn_folding_repeat_object = true;

void Map::EPPC_PatternMatching::Warn::FoldingRepeatObject(const std::size_t line_number,
                                                          const std::string_view object_name)
{
  // This warning is pretty much the only one guaranteed to not produce false positives.
  if (!do_warn_folding_repeat_object)
    mijo::println(std::cerr, "Line {:d}] Detected repeat-name object \"{:s}\"", line_number,
                  object_name);
}

bool Map::EPPC_PatternMatching::Warn::do_warn_folding_odr_violation = true;

void Map::EPPC_PatternMatching::Warn::FoldingOneDefinitionRuleViolation(
    const std::size_t line_number, const std::string_view symbol_name,
    const std::string_view object_name)
{
  // For legal linker maps, this should only ever happen in repeat-name objects.
  if (!do_warn_folding_odr_violation)
    return;
  mijo::println(std::cerr, "Line {:d}] \"{:s}\" seen again in \"{:s}\"", line_number, symbol_name,
                object_name);
}

bool Map::SectionLayout::Warn::do_warn_repeat_compilation_unit = true;

void Map::SectionLayout::Warn::RepeatCompilationUnit(const std::size_t line_number,
                                                     const std::string_view compilation_unit_name,
                                                     const std::string_view section_name)
{
  if (!do_warn_repeat_compilation_unit)
    return;
  mijo::println(std::cerr, "Line {:d}] Detected repeat-name compilation unit \"{:s}\" ({:s})",
                line_number, compilation_unit_name, section_name);
}

bool Map::SectionLayout::Warn::do_warn_odr_violation = true;

void Map::SectionLayout::Warn::OneDefinitionRuleViolation(
    const std::size_t line_number, const std::string_view symbol_name,
    const std::string_view compilation_unit_name, const std::string_view section_name)
{
  // For legal linker maps, this should only ever happen in repeat-name compilation units.
  if (!do_warn_odr_violation)
    return;
  mijo::println(std::cerr, "Line {:d}] \"{:s}\" seen again in \"{:s}\" ({:s})", line_number,
                symbol_name, compilation_unit_name, section_name);
}

bool Map::SectionLayout::Warn::do_warn_sym_on_flag_detected = true;

void Map::SectionLayout::Warn::SymOnFlagDetected(const std::size_t line_number,
                                                 const std::string_view compilation_unit_name,
                                                 const std::string_view section_name)
{
  // Multiple STT_SECTION symbols were seen in an uninterrupted compilation unit.  This could be
  // a false positive, and in turn would be a false negative for a RepeatCompilationUnit warning.
  if (!do_warn_sym_on_flag_detected)
    return;
  mijo::println(std::cerr, "Line {:d}] Detected '-sym on' flag in \"{:s}\" ({:s})", line_number,
                compilation_unit_name, section_name);
}

bool Map::SectionLayout::Warn::do_warn_common_on_flag_detected = true;

void Map::SectionLayout::Warn::CommonOnFlagDetected(const std::size_t line_number,
                                                    const std::string_view compilation_unit_name,
                                                    const std::string_view section_name)
{
  if (!do_warn_common_on_flag_detected)
    return;
  mijo::println(std::cerr, "Line {:d}] Detected '-common on' flag in \"{:s}\" ({:s})", line_number,
                compilation_unit_name, section_name);
}

bool Map::SectionLayout::Warn::do_warn_lcomm_after_comm = true;

void Map::SectionLayout::Warn::LCommAfterComm(const std::size_t line_number)
{
  // Shouldn't this be impossible?
  if (!do_warn_lcomm_after_comm)
    return;
  mijo::println(std::cerr, "Line {:d}] .lcomm symbols found after .comm symbols.", line_number);
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

// This is far from a comprehensive listing.  There's also all of these which may or may not appear
// in a linker map:
// ".PPC.EMB.sdata0" ".PPC.EMB.sbss0" ".PPC.EMB.seginfo" ".PPC.EMB.apuinfo"
// ".gnu.version" ".gnu.version_r" ".gnu.warning" ".gnu.version_d"
// ".BINARY" ".rela" ".dynsym" ".rel" ".symtab" ".interp" ".dynstr" ".hash" ".dynamic" ".plt" ".got"
// ".note.ABI-tag" ".line" ".shstrtab" ".strtab" ".comment" ".stab"
static const std::unordered_map<std::string_view, Map::SectionLayout::Kind> map_section_layout_kind{
    {".init", Map::SectionLayout::Kind::Code},
    {".text", Map::SectionLayout::Kind::Code},
    {".fini", Map::SectionLayout::Kind::Code},

    {".init_vle", Map::SectionLayout::Kind::VLECode},
    {".text_vle", Map::SectionLayout::Kind::VLECode},

    {".compress.init", Map::SectionLayout::Kind::ZCode},
    {".compress.text", Map::SectionLayout::Kind::ZCode},
    {".compress.fini", Map::SectionLayout::Kind::ZCode},

    {".data", Map::SectionLayout::Kind::Data},
    {".rodata", Map::SectionLayout::Kind::Data},
    {".sdata", Map::SectionLayout::Kind::Data},
    {".sdata2", Map::SectionLayout::Kind::Data},

    {".bss", Map::SectionLayout::Kind::BSS},
    {".sbss", Map::SectionLayout::Kind::BSS},
    {".sbss2", Map::SectionLayout::Kind::BSS},

    {".ctors", Map::SectionLayout::Kind::Ctors},
    {".dtors", Map::SectionLayout::Kind::Dtors},
    {"extab", Map::SectionLayout::Kind::ExTab},
    {"extabindex", Map::SectionLayout::Kind::ExTabIndex},

    {".debug", Map::SectionLayout::Kind::Debug},
    {".debug_sfnames", Map::SectionLayout::Kind::Debug},
    {".debug_scrinfo", Map::SectionLayout::Kind::Debug},
    {".debug_abbrev", Map::SectionLayout::Kind::Debug},
    {".debug_info", Map::SectionLayout::Kind::Debug},
    {".debug_arranges", Map::SectionLayout::Kind::Debug},
    {".debug_frame", Map::SectionLayout::Kind::Debug},
    {".debug_line", Map::SectionLayout::Kind::Debug},
    {".debug_loc", Map::SectionLayout::Kind::Debug},
    {".debug_macinfo", Map::SectionLayout::Kind::Debug},
    {".debug_pubnames", Map::SectionLayout::Kind::Debug},
};

Map::SectionLayout::Kind Map::SectionLayout::ToSectionKind(const std::string_view section_name)
{
  if (map_section_layout_kind.contains(section_name))
    return map_section_layout_kind.at(section_name);
  return Map::SectionLayout::Kind::Unknown;
}

Map::Error Map::Scan(const std::string_view string_view, std::size_t& line_number)
{
  return this->Scan(string_view.begin(), string_view.end(), line_number);
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
    head = match[0].second;
    const auto error =
        this->ScanPrologue_SectionLayout(head, tail, line_number, mijo::to_string_view(match[1]));
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
    head = match[0].second;
    const auto error =
        this->ScanPrologue_SectionLayout(head, tail, line_number, mijo::to_string_view(match[1]));
    if (error != Error::None)
      return error;
    goto NINTENDO_EAD_TRIMMED_LINKER_MAPS_GOTO_HERE;
  }
  if (std::regex_search(head, tail, match, re_entry_point_name,
                        std::regex_constants::match_continuous))
  {
    line_number += 1u;
    head = match[0].second;
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
      portion->SetVersionRange(Version::version_3_0_4, Version::Latest);
    this->dwarf_symbol_closure = std::move(portion);
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
    head = match[0].second;
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
    head = match[0].second;
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
    head = match[0].second;
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
    head = match[0].second;
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
    head = match[0].second;
    const auto error =
        this->ScanPrologue_SectionLayout(head, tail, line_number, mijo::to_string_view(match[1]));
    if (error != Error::None)
      return error;
  }
  if (std::regex_search(head, tail, match, re_memory_map_header,
                        std::regex_constants::match_continuous))
  {
    line_number += 3u;
    head = match[0].second;
    const auto error = this->ScanPrologue_MemoryMap(head, tail, line_number);
    if (error != Error::None)
      return error;
  }
  if (std::regex_search(head, tail, match, re_linker_generated_symbols_header,
                        std::regex_constants::match_continuous))
  {
    line_number += 3u;
    head = match[0].second;
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
  return this->ScanTLOZTP(string_view.begin(), string_view.end(), line_number);
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
    const std::string_view section_name = mijo::to_string_view(match[1]);
    line_number += 1u;
    head = match[0].second;
    auto portion =
        std::make_unique<SectionLayout>(SectionLayout::ToSectionKind(section_name), section_name);
    portion->SetVersionRange(Version::version_3_0_4, Version::version_3_0_4);
    const auto error = portion->ScanTLOZTP(head, tail, line_number);
    if (error != Error::None)
      return error;
    this->section_layouts.push_back(std::move(portion));
  }
  return this->ScanForGarbage(head, tail);
}

Map::Error Map::ScanSMGalaxy(const std::string_view string_view, std::size_t& line_number)
{
  return this->ScanSMGalaxy(string_view.begin(), string_view.end(), line_number);
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
    head = match[0].second;
    // TODO: detect and split Section Layout subtext by observing the Starting Address
    auto portion =
        std::make_unique<SectionLayout>(SectionLayout::Kind::Code, mijo::to_string_view(match[1]));
    portion->SetVersionRange(Version::version_3_0_4, Version::Latest);
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

void Map::Print(std::ostream& stream, std::size_t& line_number) const
{
  auto unresolved_head = this->unresolved_symbols.cbegin(),
       unresolved_tail = this->unresolved_symbols.cend();
  // "Link map of %s\r\n"
  mijo::print(stream, "Link map of {:s}\r\n", entry_point_name);
  line_number = 2;
  if (normal_symbol_closure)
    normal_symbol_closure->Print(stream, unresolved_head, unresolved_tail, line_number);
  if (eppc_pattern_matching)
    eppc_pattern_matching->Print(stream, line_number);
  if (dwarf_symbol_closure)
    dwarf_symbol_closure->Print(stream, unresolved_head, unresolved_tail, line_number);
  if (linker_opts)
    linker_opts->Print(stream, line_number);
  if (mixed_mode_islands)
    mixed_mode_islands->Print(stream, line_number);
  if (branch_islands)
    branch_islands->Print(stream, line_number);
  if (linktime_size_decreasing_optimizations)
    linktime_size_decreasing_optimizations->Print(stream, line_number);
  if (linktime_size_increasing_optimizations)
    linktime_size_increasing_optimizations->Print(stream, line_number);
  for (const auto& section_layout : section_layouts)
    section_layout->Print(stream, line_number);
  if (memory_map)
    memory_map->Print(stream, line_number);
  if (linker_generated_symbols)
    linker_generated_symbols->Print(stream, line_number);
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
                                           std::size_t& line_number, const std::string_view name)
{
  std::cmatch match;

  if (std::regex_search(head, tail, match, re_section_layout_3column_prologue_1,
                        std::regex_constants::match_continuous))
  {
    line_number += 1u;
    head = match[0].second;
    if (std::regex_search(head, tail, match, re_section_layout_3column_prologue_2,
                          std::regex_constants::match_continuous))
    {
      line_number += 1u;
      head = match[0].second;
      if (std::regex_search(head, tail, match, re_section_layout_3column_prologue_3,
                            std::regex_constants::match_continuous))
      {
        line_number += 1u;
        head = match[0].second;
        auto portion = std::make_unique<SectionLayout>(SectionLayout::ToSectionKind(name), name);
        portion->SetVersionRange(Version::Unknown, Version::version_2_4_7_build_107);
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
    head = match[0].second;
    if (std::regex_search(head, tail, match, re_section_layout_4column_prologue_2,
                          std::regex_constants::match_continuous))
    {
      line_number += 1u;
      head = match[0].second;
      if (std::regex_search(head, tail, match, re_section_layout_4column_prologue_3,
                            std::regex_constants::match_continuous))
      {
        line_number += 1u;
        head = match[0].second;
        auto portion = std::make_unique<SectionLayout>(SectionLayout::ToSectionKind(name), name);
        portion->SetVersionRange(Version::version_3_0_4, Version::Latest);
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
    head = match[0].second;
    if (std::regex_search(head, tail, match, re_memory_map_simple_prologue_2_old,
                          std::regex_constants::match_continuous))
    {
      line_number += 1u;
      head = match[0].second;
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
    head = match[0].second;
    if (std::regex_search(head, tail, match, re_memory_map_romram_prologue_2_old,
                          std::regex_constants::match_continuous))
    {
      line_number += 1u;
      head = match[0].second;
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
    head = match[0].second;
    if (std::regex_search(head, tail, match, re_memory_map_simple_prologue_2,
                          std::regex_constants::match_continuous))
    {
      line_number += 1u;
      head = match[0].second;
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
    head = match[0].second;
    if (std::regex_search(head, tail, match, re_memory_map_romram_prologue_2,
                          std::regex_constants::match_continuous))
    {
      line_number += 1u;
      head = match[0].second;
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
    head = match[0].second;
    if (std::regex_search(head, tail, match, re_memory_map_srecord_prologue_2,
                          std::regex_constants::match_continuous))
    {
      line_number += 1u;
      head = match[0].second;
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
    head = match[0].second;
    if (std::regex_search(head, tail, match, re_memory_map_binfile_prologue_2,
                          std::regex_constants::match_continuous))
    {
      line_number += 1u;
      head = match[0].second;
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
    head = match[0].second;
    if (std::regex_search(head, tail, match, re_memory_map_romram_srecord_prologue_2,
                          std::regex_constants::match_continuous))
    {
      line_number += 1u;
      head = match[0].second;
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
    head = match[0].second;
    if (std::regex_search(head, tail, match, re_memory_map_romram_binfile_prologue_2,
                          std::regex_constants::match_continuous))
    {
      line_number += 1u;
      head = match[0].second;
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
    head = match[0].second;
    if (std::regex_search(head, tail, match, re_memory_map_srecord_binfile_prologue_2,
                          std::regex_constants::match_continuous))
    {
      line_number += 1u;
      head = match[0].second;
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
    head = match[0].second;
    if (std::regex_search(head, tail, match, re_memory_map_romram_srecord_binfile_prologue_2,
                          std::regex_constants::match_continuous))
    {
      line_number += 1u;
      head = match[0].second;
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

// clang-format off
static const std::regex re_excluded_symbol{
//  ">>> EXCLUDED SYMBOL %s (%s,%s) found in %s %s\r\n"
    ">>> EXCLUDED SYMBOL (.*) \\((.*),(.*)\\) found in (.*) (.*)\r\n"};
static const std::regex re_wasnt_passed_section{
//  ">>> %s wasn't passed a section\r\n"
    ">>> (.*) wasn't passed a section\r\n"};
static const std::regex re_dynamic_symbol_referenced{
//  ">>> DYNAMIC SYMBOL: %s referenced\r\n"
    ">>> DYNAMIC SYMBOL: (.*) referenced\r\n"};
static const std::regex re_module_symbol_name_too_large{
//  ">>> MODULE SYMBOL NAME TOO LARGE: %s\r\n"
    ">>> MODULE SYMBOL NAME TOO LARGE: (.*)\r\n"};
static const std::regex re_nonmodule_symbol_name_too_large{
//  ">>> NONMODULE SYMBOL NAME TOO LARGE: %s\r\n"
    ">>> NONMODULE SYMBOL NAME TOO LARGE: (.*)\r\n"};
static const std::regex re_ComputeSizeETI_section_header_size_failure{
//  "<<< Failure in ComputeSizeETI: section->Header->sh_size was %x, rel_size should be %x\r\n"
    "<<< Failure in ComputeSizeETI: section->Header->sh_size was ([0-9a-f]+), rel_size should be ([0-9a-f]+)\r\n"};
static const std::regex re_ComputeSizeETI_st_size_failure{
//  "<<< Failure in ComputeSizeETI: st_size was %x, st_size should be %x\r\n"
    "<<< Failure in ComputeSizeETI: st_size was ([0-9a-f]+), st_size should be ([0-9a-f]+)\r\n"};
static const std::regex re_PreCalculateETI_section_header_size_failure{
//  "<<< Failure in PreCalculateETI: section->Header->sh_size was %x, rel_size should be %x\r\n"
    "<<< Failure in PreCalculateETI: section->Header->sh_size was ([0-9a-f]+), rel_size should be ([0-9a-f]+)\r\n"};
static const std::regex re_PreCalculateETI_st_size_failure{
//  "<<< Failure in PreCalculateETI: st_size was %x, st_size should be %x\r\n"
    "<<< Failure in PreCalculateETI: st_size was ([0-9a-f]+), st_size should be ([0-9a-f]+)\r\n"};
static const std::regex re_GetFilePos_calc_offset_failure{
//  "<<< Failure in %s: GetFilePos is %x, sect->calc_offset is %x\r\n"
    "<<< Failure in (.*): GetFilePos is ([0-9a-f]+), sect->calc_offset is ([0-9a-f]+)\r\n"};
static const std::regex re_GetFilePos_bin_offset_failure{
//  "<<< Failure in %s: GetFilePos is %x, sect->bin_offset is %x\r\n"
    "<<< Failure in (.*): GetFilePos is ([0-9a-f]+), sect->bin_offset is ([0-9a-f]+)\r\n"};
// clang-format on

Map::Error Map::ScanForGarbage(const char* const head, const char* const tail)
{
  if (head < tail)
  {
    std::cmatch match;

    // These linker map prints are known to exist, but I have never seen them.
    if (std::regex_search(head, tail, match, re_excluded_symbol,
                          std::regex_constants::match_continuous))
      return Error::Unimplemented;
    if (std::regex_search(head, tail, match, re_wasnt_passed_section,
                          std::regex_constants::match_continuous))
      return Error::Unimplemented;
    if (std::regex_search(head, tail, match, re_dynamic_symbol_referenced,
                          std::regex_constants::match_continuous))
      return Error::Unimplemented;
    if (std::regex_search(head, tail, match, re_module_symbol_name_too_large,
                          std::regex_constants::match_continuous))
      return Error::Unimplemented;
    if (std::regex_search(head, tail, match, re_nonmodule_symbol_name_too_large,
                          std::regex_constants::match_continuous))
      return Error::Unimplemented;
    if (std::regex_search(head, tail, match, re_ComputeSizeETI_section_header_size_failure,
                          std::regex_constants::match_continuous))
      return Error::Unimplemented;
    if (std::regex_search(head, tail, match, re_ComputeSizeETI_st_size_failure,
                          std::regex_constants::match_continuous))
      return Error::Unimplemented;
    if (std::regex_search(head, tail, match, re_PreCalculateETI_section_header_size_failure,
                          std::regex_constants::match_continuous))
      return Error::Unimplemented;
    if (std::regex_search(head, tail, match, re_PreCalculateETI_st_size_failure,
                          std::regex_constants::match_continuous))
      return Error::Unimplemented;
    if (std::regex_search(head, tail, match, re_GetFilePos_calc_offset_failure,
                          std::regex_constants::match_continuous))
      return Error::Unimplemented;
    if (std::regex_search(head, tail, match, re_GetFilePos_bin_offset_failure,
                          std::regex_constants::match_continuous))
      return Error::Unimplemented;

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

static const std::unordered_map<std::string_view, Type> map_symbol_closure_st_type{
    {"notype", Type::notype},   {"object", Type::object}, {"func", Type::func},
    {"section", Type::section}, {"file", Type::file},     {"unknown", Type::unknown},
};
static const std::unordered_map<std::string_view, Bind> map_symbol_closure_st_bind{
    {"local", Bind::local},       {"global", Bind::global},     {"weak", Bind::weak},
    {"multidef", Bind::multidef}, {"overload", Bind::overload}, {"unknown", Bind::unknown},
};

Map::Error Map::SymbolClosure::Scan(  //
    const char*& head, const char* const tail, std::size_t& line_number,
#if defined(__clang__)         // Why am I getting a -Wshadow warning about
#pragma clang diagnostic push  // a member from an entirely different class
#pragma clang diagnostic ignored "-Wshadow"
#endif
    UnresolvedSymbols& unresolved_symbols)
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
{
  std::cmatch match;

  NodeBase* curr_node = &this->root;
  int curr_hierarchy_level = 0;

  while (true)
  {
    if (std::regex_search(head, tail, match, re_symbol_closure_node_normal,
                          std::regex_constants::match_continuous))
    {
      const int next_hierarchy_level = mijo::smto<int>(match[1]);
      if (next_hierarchy_level <= 0)
        return Error::SymbolClosureInvalidHierarchy;
      if (curr_hierarchy_level + 1 < next_hierarchy_level)
        return Error::SymbolClosureHierarchySkip;
      const std::string_view type = mijo::to_string_view(match[3]),
                             bind = mijo::to_string_view(match[4]);
      if (!map_symbol_closure_st_type.contains(type))
        return Error::SymbolClosureInvalidSymbolType;
      if (!map_symbol_closure_st_bind.contains(bind))
        return Error::SymbolClosureInvalidSymbolBind;
      const std::string_view symbol_name = mijo::to_string_view(match[2]),
                             module_name = mijo::to_string_view(match[5]),
                             source_name = mijo::to_string_view(match[6]);

      for (int i = curr_hierarchy_level + 1; i > next_hierarchy_level; --i)
        curr_node = curr_node->parent;
      curr_hierarchy_level = next_hierarchy_level;

      const std::size_t line_number_backup = line_number;  // unfortunate
      line_number += 1u;
      head = match[0].second;

      std::list<NodeReal::UnreferencedDuplicate> unref_dups;

      if (std::regex_search(head, tail, match, re_symbol_closure_node_normal_unref_dup_header,
                            std::regex_constants::match_continuous))
      {
        if (mijo::smto<int>(match[1]) != curr_hierarchy_level)
          return Error::SymbolClosureUnrefDupsHierarchyMismatch;
        if (mijo::to_string_view(match[2]) != symbol_name)
          return Error::SymbolClosureUnrefDupsNameMismatch;
        line_number += 1u;
        head = match[0].second;
        while (std::regex_search(head, tail, match, re_symbol_closure_node_normal_unref_dups,
                                 std::regex_constants::match_continuous))
        {
          if (mijo::smto<int>(match[1]) != curr_hierarchy_level)
            return Error::SymbolClosureUnrefDupsHierarchyMismatch;
          const std::string_view unref_dup_type = mijo::to_string_view(match[2]),
                                 unref_dup_bind = mijo::to_string_view(match[3]);
          if (!map_symbol_closure_st_type.contains(unref_dup_type))
            return Error::SymbolClosureInvalidSymbolType;
          if (!map_symbol_closure_st_bind.contains(unref_dup_bind))
            return Error::SymbolClosureInvalidSymbolBind;
          unref_dups.emplace_back(map_symbol_closure_st_type.at(unref_dup_type),
                                  map_symbol_closure_st_bind.at(unref_dup_bind),
                                  mijo::to_string_view(match[4]), mijo::to_string_view(match[5]));
          line_number += 1u;
          head = match[0].second;
        }
        if (unref_dups.empty())
          return Error::SymbolClosureUnrefDupsEmpty;
        this->SetVersionRange(Version::version_2_3_3_build_137, Version::Latest);
      }

      NodeReal* next_node = new NodeReal(  // Non-owning pointer.
          curr_node, symbol_name, map_symbol_closure_st_type.at(type),
          map_symbol_closure_st_bind.at(bind), module_name, source_name, std::move(unref_dups));
      curr_node = curr_node->children.emplace_back(next_node).get();

      const std::string_view& compilation_unit_name =
          next_node->source_name.empty() ? next_node->module_name : next_node->source_name;
      NodeLookup& curr_node_lookup = this->lookup[compilation_unit_name];
      if (curr_node_lookup.contains(symbol_name))
      {
        // TODO: restore sym on detection (it was flawed)
        Warn::OneDefinitionRuleViolation(line_number_backup, symbol_name, compilation_unit_name);
      }
      curr_node_lookup.emplace(next_node->name, *next_node);

      // Though I do not understand it, the following is a normal occurrence for _dtors$99:
      // "  1] _dtors$99 (object,global) found in Linker Generated Symbol File "
      // "    3] .text (section,local) found in xyz.cpp lib.a"
      if (symbol_name == "_dtors$99" && module_name == "Linker Generated Symbol File")
      {
        // Create a dummy node for hierarchy level 2.
        curr_node = curr_node->children.emplace_back(new NodeBase(curr_node)).get();
        ++curr_hierarchy_level;
        this->SetVersionRange(Version::version_3_0_4, Version::Latest);
      }
      continue;
    }
    if (std::regex_search(head, tail, match, re_symbol_closure_node_linker_generated,
                          std::regex_constants::match_continuous))
    {
      const int next_hierarchy_level = mijo::smto<int>(match[1]);
      if (next_hierarchy_level <= 0)
        return Error::SymbolClosureInvalidHierarchy;
      if (curr_hierarchy_level + 1 < next_hierarchy_level)
        return Error::SymbolClosureHierarchySkip;

      for (int i = curr_hierarchy_level + 1; i > next_hierarchy_level; --i)
        curr_node = curr_node->parent;
      curr_hierarchy_level = next_hierarchy_level;

      // clang-format off
      curr_node = curr_node->children.emplace_back(new NodeLinkerGenerated(curr_node, mijo::to_string_view(match[2]))).get();
      // clang-format on

      line_number += 1u;
      head = match[0].second;
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
      head = match[0].second;
      continue;
    }
    break;
  }
  return Error::None;
}

void Map::SymbolClosure::Print(std::ostream& stream,
                               UnresolvedSymbols::const_iterator& unresolved_head,
                               const UnresolvedSymbols::const_iterator unresolved_tail,
                               std::size_t& line_number) const
{
  this->root.Print(stream, 0, unresolved_head, unresolved_tail, line_number);
}

void Map::SymbolClosure::NodeBase::PrintPrefix(std::ostream& stream, const int hierarchy_level)
{
  if (hierarchy_level >= 0)
    for (int i = 0; i <= hierarchy_level; ++i)
      stream.put(' ');
  // "%i] "
  mijo::print(stream, "{:d}] ", hierarchy_level);
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

void Map::SymbolClosure::NodeBase::Print(std::ostream& stream, const int hierarchy_level,
                                         UnresolvedSymbols::const_iterator& unresolved_head,
                                         const UnresolvedSymbols::const_iterator unresolved_tail,
                                         std::size_t& line_number) const
{
  while (unresolved_head != unresolved_tail && unresolved_head->first == line_number)
  {
    // ">>> SYMBOL NOT FOUND: %s\r\n"
    mijo::print(stream, ">>> SYMBOL NOT FOUND: {:s}\r\n", (unresolved_head++)->second);
    line_number += 1;
  }
  for (const auto& node : this->children)
    node->Print(stream, hierarchy_level + 1, unresolved_head, unresolved_tail, line_number);
}

void Map::SymbolClosure::NodeReal::Print(std::ostream& stream, const int hierarchy_level,
                                         UnresolvedSymbols::const_iterator& unresolved_head,
                                         const UnresolvedSymbols::const_iterator unresolved_tail,
                                         std::size_t& line_number) const
{
  PrintPrefix(stream, hierarchy_level);
  // "%s (%s,%s) found in %s %s\r\n"
  mijo::print(stream, "{:s} ({:s},{:s}) found in {:s} {:s}\r\n", name, ToName(type), ToName(bind),
              module_name, source_name);
  line_number += 1u;
  if (!this->unref_dups.empty())
  {
    PrintPrefix(stream, hierarchy_level);
    // ">>> UNREFERENCED DUPLICATE %s\r\n"
    mijo::print(stream, ">>> UNREFERENCED DUPLICATE {:s}\r\n", name);
    line_number += 1u;
    for (const auto& unref_dup : unref_dups)
      unref_dup.Print(stream, hierarchy_level, line_number);
  }
  NodeBase::Print(stream, hierarchy_level, unresolved_head, unresolved_tail, line_number);
}

void Map::SymbolClosure::NodeLinkerGenerated::Print(
    std::ostream& stream, const int hierarchy_level,
    UnresolvedSymbols::const_iterator& unresolved_head,
    const UnresolvedSymbols::const_iterator unresolved_tail, std::size_t& line_number) const
{
  PrintPrefix(stream, hierarchy_level);
  // "%s found as linker generated symbol\r\n"
  mijo::print(stream, "{:s} found as linker generated symbol\r\n", name);
  line_number += 1u;
  NodeBase::Print(stream, hierarchy_level, unresolved_head, unresolved_tail, line_number);
}

void Map::SymbolClosure::NodeReal::UnreferencedDuplicate::Print(  //
    std::ostream& stream, const int hierarchy_level, std::size_t& line_number) const
{
  PrintPrefix(stream, hierarchy_level);
  // ">>> (%s,%s) found in %s %s\r\n"
  mijo::print(stream, ">>> ({:s},{:s}) found in {:s} {:s}\r\n", ToName(type), ToName(bind),
              module_name, source_name);
  line_number += 1u;
}

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
      const std::string_view first_name = mijo::to_string_view(match[1]),
                             second_name = mijo::to_string_view(match[2]);
      const Elf32_Word size = mijo::smto<Elf32_Word>(match[3]);
      line_number += 2u;
      head = match[0].second;
      if (std::regex_search(head, tail, match, re_code_merging_will_be_replaced,
                            std::regex_constants::match_continuous))
      {
        if (mijo::to_string_view(match[1]) != first_name)
          return Error::EPPC_PatternMatchingMergingFirstNameMismatch;
        if (mijo::to_string_view(match[2]) != second_name)
          return Error::EPPC_PatternMatchingMergingSecondNameMismatch;
        will_be_replaced = true;
        line_number += 3u;
        head = match[0].second;
      }
      const MergingUnit& unit = this->merging_units.emplace_back(
          first_name, second_name, size, will_be_replaced, was_interchanged);
      if (merging_lookup.contains(first_name))
        Warn::MergingOneDefinitionRuleViolation(line_number - 5u, first_name);
      merging_lookup.emplace(unit.first_name, unit);
      continue;
    }
    if (std::regex_search(head, tail, match, re_code_merging_was_interchanged,
                          std::regex_constants::match_continuous))
    {
      const std::string_view first_name = mijo::to_string_view(match[1]),
                             second_name = mijo::to_string_view(match[2]);
      const Elf32_Word size = mijo::smto<Elf32_Word>(match[3]);
      was_interchanged = true;
      line_number += 1u;
      head = match[0].second;
      if (std::regex_search(head, tail, match, re_code_merging_will_be_replaced,
                            std::regex_constants::match_continuous))
      {
        if (mijo::to_string_view(match[1]) != first_name)
          return Error::EPPC_PatternMatchingMergingFirstNameMismatch;
        if (mijo::to_string_view(match[2]) != second_name)
          return Error::EPPC_PatternMatchingMergingSecondNameMismatch;
        will_be_replaced = true;
        line_number += 3u;
        head = match[0].second;
      }
      if (std::regex_search(head, tail, match, re_code_merging_is_duplicated,
                            std::regex_constants::match_continuous))
      {
        if (mijo::to_string_view(match[1]) != first_name)
          return Error::EPPC_PatternMatchingMergingFirstNameMismatch;
        if (mijo::to_string_view(match[2]) != second_name)
          return Error::EPPC_PatternMatchingMergingSecondNameMismatch;
        if (mijo::smto<Elf32_Word>(match[3]) != size)
          return Error::EPPC_PatternMatchingMergingSizeMismatch;
        line_number += 2u;
        head = match[0].second;
      }
      else
      {
        return Error::EPPC_PatternMatchingMergingInterchangeMissingEpilogue;
      }
      const MergingUnit& unit = this->merging_units.emplace_back(
          first_name, second_name, size, will_be_replaced, was_interchanged);
      if (merging_lookup.contains(first_name))
        Warn::MergingOneDefinitionRuleViolation(line_number - 5u, first_name);
      merging_lookup.emplace(unit.first_name, unit);
      continue;
    }
    break;
  }
  // After analysis concludes, a redundant summary of changes per file is printed.
  while (std::regex_search(head, tail, match, re_code_folding_header,
                           std::regex_constants::match_continuous))
  {
    const std::string_view object_name = mijo::to_string_view(match[1]);
    if (this->folding_lookup.contains(object_name))
      Warn::FoldingRepeatObject(line_number + 3u, object_name);
    FoldingUnit& folding_unit = this->folding_units.emplace_back(object_name);

    FoldingUnit::UnitLookup& curr_unit_lookup = this->folding_lookup[folding_unit.object_name];
    line_number += 4u;
    head = match[0].second;
    while (true)
    {
      if (std::regex_search(head, tail, match, re_code_folding_is_duplicated,
                            std::regex_constants::match_continuous))
      {
        const std::string_view first_name = mijo::to_string_view(match[1]);
        if (curr_unit_lookup.contains(first_name))
          Warn::FoldingOneDefinitionRuleViolation(line_number, first_name, object_name);
        const FoldingUnit::Unit& unit = folding_unit.units.emplace_back(
            first_name, mijo::to_string_view(match[2]), mijo::smto<Elf32_Word>(match[3]), false);
        curr_unit_lookup.emplace(unit.first_name, unit);
        line_number += 2u;
        head = match[0].second;
        continue;
      }
      if (std::regex_search(head, tail, match, re_code_folding_is_duplicated_new_branch,
                            std::regex_constants::match_continuous))
      {
        const std::string_view first_name = mijo::to_string_view(match[1]);
        // It is my assumption that these will always match.
        if (first_name != mijo::to_string_view(match[4]))
          return Error::EPPC_PatternMatchingFoldingNewBranchFunctionNameMismatch;
        if (curr_unit_lookup.contains(first_name))
          Warn::FoldingOneDefinitionRuleViolation(line_number, first_name, object_name);
        const FoldingUnit::Unit& unit = folding_unit.units.emplace_back(
            first_name, mijo::to_string_view(match[2]), mijo::smto<Elf32_Word>(match[3]), true);
        curr_unit_lookup.emplace(unit.first_name, unit);
        line_number += 2u;
        head = match[0].second;
        continue;
      }
      break;
    }
  }
  return Error::None;
}

void Map::EPPC_PatternMatching::Print(std::ostream& stream, std::size_t& line_number) const
{
  for (const auto& unit : merging_units)
    unit.Print(stream, line_number);
  for (const auto& unit : folding_units)
    unit.Print(stream, line_number);
}

void Map::EPPC_PatternMatching::MergingUnit::Print(std::ostream& stream,
                                                   std::size_t& line_number) const
{
  if (was_interchanged)
  {
    // "--> the function %s was interchanged with %s, size=%d \r\n"
    mijo::print(stream, "--> the function {:s} was interchanged with {:s}, size={:d} \r\n",
                first_name, second_name, size);
    line_number += 1u;
    if (will_be_replaced)
    {
      // "--> the function %s will be replaced by a branch to %s\r\n\r\n\r\n"
      mijo::print(stream, "--> the function {:s} will be replaced by a branch to {:s}\r\n\r\n\r\n",
                  first_name, second_name);
      line_number += 3u;
    }
    // "--> duplicated code: symbol %s is duplicated by %s, size = %d \r\n\r\n"
    mijo::print(stream,
                "--> duplicated code: symbol {:s} is duplicated by {:s}, size = {:d} \r\n\r\n",
                first_name, second_name, size);
    line_number += 2u;
  }
  else
  {
    // "--> duplicated code: symbol %s is duplicated by %s, size = %d \r\n\r\n"
    mijo::print(stream,
                "--> duplicated code: symbol {:s} is duplicated by {:s}, size = {:d} \r\n\r\n",
                first_name, second_name, size);
    line_number += 2u;
    if (will_be_replaced)
    {
      // "--> the function %s will be replaced by a branch to %s\r\n\r\n\r\n"
      mijo::print(stream, "--> the function {:s} will be replaced by a branch to {:s}\r\n\r\n\r\n",
                  first_name, second_name);
      line_number += 3u;
    }
  }
}

void Map::EPPC_PatternMatching::FoldingUnit::Print(std::ostream& stream,
                                                   std::size_t& line_number) const
{
  // "\r\n\r\n\r\nCode folded in file: %s \r\n"
  mijo::print(stream, "\r\n\r\n\r\nCode folded in file: {:s} \r\n", object_name);
  line_number += 4u;
  for (const auto& unit : units)
    unit.Print(stream, line_number);
}

void Map::EPPC_PatternMatching::FoldingUnit::Unit::Print(std::ostream& stream,
                                                         std::size_t& line_number) const
{
  if (new_branch_function)
  {
    // "--> %s is duplicated by %s, size = %d, new branch function %s \r\n\r\n"
    mijo::print(stream,
                "--> {:s} is duplicated by {:s}, size = {:d}, new branch function {:s} \r\n\r\n",
                first_name, second_name, size, first_name);
    line_number += 2u;
  }
  else
  {
    // "--> %s is duplicated by %s, size = %d \r\n\r\n"
    mijo::print(stream, "--> {:s} is duplicated by {:s}, size = {:d} \r\n\r\n", first_name,
                second_name, size);
    line_number += 2u;
  }
}

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
      this->units.emplace_back(Unit::Kind::NotNear, mijo::to_string_view(match[1]),
                               mijo::to_string_view(match[2]), mijo::to_string_view(match[3]));
      line_number += 1u;
      head = match[0].second;
      continue;
    }
    if (std::regex_search(head, tail, match, re_linker_opts_unit_disassemble_error,
                          std::regex_constants::match_continuous))
    {
      this->units.emplace_back(mijo::to_string_view(match[1]), mijo::to_string_view(match[2]));
      line_number += 1u;
      head = match[0].second;
      continue;
    }
    if (std::regex_search(head, tail, match, re_linker_opts_unit_address_not_computed,
                          std::regex_constants::match_continuous))
    {
      this->units.emplace_back(Unit::Kind::NotComputed, mijo::to_string_view(match[1]),
                               mijo::to_string_view(match[2]), mijo::to_string_view(match[3]));
      line_number += 1u;
      head = match[0].second;
      continue;
    }
    // I have not seen a single linker map with this
    if (std::regex_search(head, tail, match, re_linker_opts_unit_optimized,
                          std::regex_constants::match_continuous))
    {
      this->units.emplace_back(Unit::Kind::Optimized, mijo::to_string_view(match[1]),
                               mijo::to_string_view(match[2]), mijo::to_string_view(match[3]));
      line_number += 1u;
      head = match[0].second;
      continue;
    }
    break;
  }
  return Error::None;
}

void Map::LinkerOpts::Print(std::ostream& stream, std::size_t& line_number) const
{
  for (const auto& unit : units)
    unit.Print(stream, line_number);
}

void Map::LinkerOpts::Unit::Print(std::ostream& stream, std::size_t& line_number) const
{
  switch (this->unit_kind)
  {
  case Kind::NotNear:
    // "  %s/ %s()/ %s - address not in near addressing range \r\n"
    mijo::print(stream, "  {:s}/ {:s}()/ {:s} - address not in near addressing range \r\n",
                module_name, name, reference_name);
    line_number += 1u;
    return;
  case Kind::NotComputed:
    // "  %s/ %s()/ %s - final address not yet computed \r\n"
    mijo::print(stream, "  {:s}/ {:s}()/ {:s} - final address not yet computed \r\n", module_name,
                name, reference_name);
    line_number += 1u;
    return;
  case Kind::Optimized:
    // "! %s/ %s()/ %s - optimized addressing \r\n"
    mijo::print(stream, "! {:s}/ {:s}()/ {:s} - optimized addressing \r\n", module_name, name,
                reference_name);
    line_number += 1u;
    return;
  case Kind::DisassembleError:
    // "  %s/ %s() - error disassembling function \r\n"
    mijo::print(stream, "  {:s}/ {:s}() - error disassembling function \r\n", module_name, name);
    line_number += 1u;
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
      this->units.emplace_back(mijo::to_string_view(match[1]), mijo::to_string_view(match[2]),
                               false);
      line_number += 1u;
      head = match[0].second;
      continue;
    }
    if (std::regex_search(head, tail, match, re_mixed_mode_islands_created_safe,
                          std::regex_constants::match_continuous))
    {
      this->units.emplace_back(mijo::to_string_view(match[1]), mijo::to_string_view(match[2]),
                               true);
      line_number += 1u;
      head = match[0].second;
      continue;
    }
    break;
  }
  return Error::None;
}

void Map::MixedModeIslands::Print(std::ostream& stream, std::size_t& line_number) const
{
  mijo::print(stream, "\r\nMixed Mode Islands\r\n");
  line_number += 2u;
  for (const auto& unit : units)
    unit.Print(stream, line_number);
}
void Map::MixedModeIslands::Unit::Print(std::ostream& stream, std::size_t& line_number) const
{
  if (is_safe)
  {
    // "  safe mixed mode island %s created for %s\r\n"
    mijo::print(stream, "  safe mixed mode island {:s} created for {:s}\r\n", first_name,
                second_name);
    line_number += 1u;
  }
  else
  {
    // "  mixed mode island %s created for %s\r\n"
    mijo::print(stream, "  mixed mode island {:s} created for {:s}\r\n", first_name, second_name);
    line_number += 1u;
  }
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
      this->units.emplace_back(mijo::to_string_view(match[1]), mijo::to_string_view(match[2]),
                               false);
      line_number += 1u;
      head = match[0].second;
      continue;
    }
    if (std::regex_search(head, tail, match, re_branch_islands_created_safe,
                          std::regex_constants::match_continuous))
    {
      this->units.emplace_back(mijo::to_string_view(match[1]), mijo::to_string_view(match[2]),
                               true);
      line_number += 1u;
      head = match[0].second;
      continue;
    }
    break;
  }
  return Error::None;
}

void Map::BranchIslands::Print(std::ostream& stream, std::size_t& line_number) const
{
  mijo::print(stream, "\r\nBranch Islands\r\n");
  line_number += 2u;
  for (const auto& unit : units)
    unit.Print(stream, line_number);
}
void Map::BranchIslands::Unit::Print(std::ostream& stream, std::size_t& line_number) const
{
  if (is_safe)
  {
    //  "  safe branch island %s created for %s\r\n"
    mijo::print(stream, "  safe branch island {:s} created for {:s}\r\n", first_name, second_name);
    line_number += 1u;
  }
  else
  {
    //  "  branch island %s created for %s\r\n"
    mijo::print(stream, "  branch island {:s} created for {:s}\r\n", first_name, second_name);
    line_number += 1u;
  }
}

Map::Error Map::LinktimeSizeDecreasingOptimizations::Scan(const char*&, const char* const,
                                                          std::size_t&)
{
  // TODO?  I am not convinced this portion is capable of containing anything.
  return Error::None;
}

void Map::LinktimeSizeDecreasingOptimizations::Print(std::ostream& stream,
                                                     std::size_t& line_number) const
{
  mijo::print(stream, "\r\nLinktime size-decreasing optimizations\r\n");
  line_number += 2u;
}

Map::Error Map::LinktimeSizeIncreasingOptimizations::Scan(const char*&, const char* const,
                                                          std::size_t&)
{
  // TODO?  I am not convinced this portion is capable of containing anything.
  return Error::None;
}

void Map::LinktimeSizeIncreasingOptimizations::Print(std::ostream& stream,
                                                     std::size_t& line_number) const
{
  mijo::print(stream, "\r\nLinktime size-increasing optimizations\r\n");
  line_number += 2u;
}

Map::SectionLayout::Unit::Trait Map::SectionLayout::Unit::DeduceUsualSubtext(  //
    ScanningContext& scanning_context)
{
  auto& [section_layout, line_number, is_second_lap, is_after_eti_init_info, is_multi_stt_section,
         curr_unit_lookup, curr_module_name, curr_source_name] = scanning_context;

  const bool is_symbol_stt_section = (this->name == section_layout.name);

  // Detect a change in compilation unit
  if (curr_module_name != this->module_name || curr_source_name != this->source_name)
  {
    curr_module_name = this->module_name;
    curr_source_name = this->source_name;
    is_multi_stt_section = false;
    const std::string_view& compilation_unit_name =
        this->source_name.empty() ? this->module_name : this->source_name;
    const bool is_repeat_compilation_unit_detected =
        section_layout.lookup.contains(compilation_unit_name);
    curr_unit_lookup = &section_layout.lookup[compilation_unit_name];

    if (is_symbol_stt_section)
    {
      if (is_repeat_compilation_unit_detected)
      {
        // TODO: At some point, a BSS section's second lap for printing .comm symbols was given
        // STT_SECTION symbols, making them indistinguishable from a repeat-name compilation unit
        // without further heuristics.  In other words, false positives ahoy.
        // TODO: What version?
        Map::SectionLayout::Warn::RepeatCompilationUnit(line_number, compilation_unit_name,
                                                        section_layout.name);
      }
      if (is_second_lap)
      {
        // Shouldn't this be impossible?
        Map::SectionLayout::Warn::LCommAfterComm(line_number);
        is_second_lap = false;
      }
      return Unit::Trait::Section;
    }
    if (section_layout.section_kind == Map::SectionLayout::Kind::BSS)
    {
      Map::SectionLayout::Warn::CommonOnFlagDetected(line_number, compilation_unit_name,
                                                     section_layout.name);
      // TODO: There is currently no clean way to detect repeat-name compilation units during
      // a BSS section's second lap for printing .lcomm symbols.
      is_second_lap = true;
      return Unit::Trait::Common;
    }
    if (section_layout.section_kind == Map::SectionLayout::Kind::ExTab)
    {
      if (is_repeat_compilation_unit_detected)
      {
        Map::SectionLayout::Warn::RepeatCompilationUnit(line_number, compilation_unit_name,
                                                        section_layout.name);
      }
      return Unit::Trait::ExTab;
    }
    if (section_layout.section_kind == Map::SectionLayout::Kind::ExTabIndex)
    {
      if (this->name == "_eti_init_info" && compilation_unit_name == "Linker Generated Symbol File")
      {
        is_after_eti_init_info = true;
      }
      // TODO: There is currently no clean way to detect repeat-name compilation units during
      // an extabindex section's second lap for printing UNUSED symbols after _eti_init_info.
      else if (is_repeat_compilation_unit_detected && !is_after_eti_init_info)
      {
        Map::SectionLayout::Warn::RepeatCompilationUnit(line_number, compilation_unit_name,
                                                        section_layout.name);
      }
      return Unit::Trait::ExTabIndex;
    }
    return Unit::Trait::None;
  }
  if (is_symbol_stt_section)
  {
    if (section_layout.section_kind == Map::SectionLayout::Kind::Ctors ||
        section_layout.section_kind == Map::SectionLayout::Kind::Dtors)
    {
      const std::string_view& compilation_unit_name =
          this->source_name.empty() ? this->module_name : this->source_name;
      Warn::RepeatCompilationUnit(line_number, compilation_unit_name, section_layout.name);
    }
    else if (!is_multi_stt_section)
    {
      // Either this compilation unit was compiled with '-sym on', or two repeat-name compilation
      // units are adjacent to one another.
      const std::string_view& compilation_unit_name =
          this->source_name.empty() ? this->module_name : this->source_name;
      Warn::SymOnFlagDetected(line_number, compilation_unit_name, section_layout.name);
      is_multi_stt_section = true;
    }
    return Unit::Trait::Section;
  }

  if (curr_unit_lookup->contains(this->name))
  {
    const std::string_view& compilation_unit_name =
        this->source_name.empty() ? this->module_name : this->source_name;
    // This can be a strong hint that there are two or more repeat-name compilation units in your
    // linker map, assuming it's not messed up in any way.  Note that this does not detect symbols
    // with identical names across section layouts.
    Warn::OneDefinitionRuleViolation(line_number, this->name, compilation_unit_name,
                                     section_layout.name);
  }

  if (is_second_lap)
    return Unit::Trait::Common;
  if (section_layout.section_kind == Map::SectionLayout::Kind::BSS)
    return Unit::Trait::LCommon;
  if (section_layout.section_kind == Map::SectionLayout::Kind::Code)
    return Unit::Trait::Function;
  if (section_layout.section_kind == Map::SectionLayout::Kind::Data)
    return Unit::Trait::Object;
  if (section_layout.section_kind == Map::SectionLayout::Kind::ExTab)
    return Unit::Trait::ExTab;
  if (section_layout.section_kind == Map::SectionLayout::Kind::ExTabIndex)
    return Unit::Trait::ExTabIndex;
  return Unit::Trait::None;
}

Map::SectionLayout::Unit::Trait Map::SectionLayout::Unit::DeduceEntrySubtext(  //
    ScanningContext& scanning_context)
{
  auto& [section_layout, line_number, is_second_lap, is_after_eti_init_info, is_multi_stt_section,
         curr_unit_lookup, curr_module_name, curr_source_name] = scanning_context;

  // Should never be the STT_SECTION symbol. Also, this can never belong to a new compilation
  // unit (a new curr_unit_lookup) since that would inherently be an orphaned entry symbol.
  if (scanning_context.m_curr_unit_lookup->contains(this->name))
  {
    const std::string_view& compilation_unit_name = source_name.empty() ? module_name : source_name;
    Warn::OneDefinitionRuleViolation(line_number, this->name, compilation_unit_name,
                                     section_layout.name);
  }
  return Trait::NoType;
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
  ScanningContext scanning_context{*this, line_number, false, false, false, nullptr, {}, {}};

  while (true)
  {
    if (std::regex_search(head, tail, match, re_section_layout_3column_unit_normal,
                          std::regex_constants::match_continuous))
    {
      const Unit& unit = this->units.emplace_back(
          mijo::xsmto<std::uint32_t>(match[1]), mijo::xsmto<Elf32_Word>(match[2]),
          mijo::xsmto<Elf32_Addr>(match[3]), mijo::smto<int>(match[4]),
          mijo::to_string_view(match[5]), mijo::to_string_view(match[6]),
          mijo::to_string_view(match[7]), scanning_context);
      scanning_context.m_curr_unit_lookup->emplace(unit.name, unit);
      line_number += 1u;
      head = match[0].second;
      continue;
    }
    if (std::regex_search(head, tail, match, re_section_layout_3column_unit_unused,
                          std::regex_constants::match_continuous))
    {
      const Unit& unit = this->units.emplace_back(
          mijo::xsmto<Elf32_Word>(match[1]), mijo::to_string_view(match[2]),
          mijo::to_string_view(match[3]), mijo::to_string_view(match[4]), scanning_context);
      scanning_context.m_curr_unit_lookup->emplace(unit.name, unit);
      line_number += 1u;
      head = match[0].second;
      continue;
    }
    if (std::regex_search(head, tail, match, re_section_layout_3column_unit_entry,
                          std::regex_constants::match_continuous))
    {
      const std::string_view symbol_name = mijo::to_string_view(match[4]),
                             entry_parent_name = mijo::to_string_view(match[5]),
                             module_name = mijo::to_string_view(match[6]),
                             source_name = mijo::to_string_view(match[7]);
      // TODO: I want to use std::ranges::reverse_view, but it Clang doesn't support it yet.
      for (auto parent_unit = this->units.rbegin(); parent_unit != this->units.rend();
           ++parent_unit)
      {
        if (source_name != parent_unit->source_name || module_name != parent_unit->module_name)
          return Error::SectionLayoutOrphanedEntry;
        if (entry_parent_name != parent_unit->name)
          continue;
        const Unit& unit = this->units.emplace_back(
            mijo::xsmto<std::uint32_t>(match[1]), mijo::xsmto<Elf32_Word>(match[2]),
            mijo::xsmto<Elf32_Addr>(match[3]), symbol_name, &parent_unit.operator*(), module_name,
            source_name, scanning_context);
        scanning_context.m_curr_unit_lookup->emplace(unit.name, unit);
        parent_unit->entry_children.push_back(&unit);
        line_number += 1u;
        head = match[0].second;
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
  ScanningContext scanning_context{*this, line_number, false, false, false, nullptr, {}, {}};

  while (true)
  {
    if (std::regex_search(head, tail, match, re_section_layout_4column_unit_normal,
                          std::regex_constants::match_continuous))
    {
      const Unit& unit = this->units.emplace_back(
          mijo::xsmto<std::uint32_t>(match[1]), mijo::xsmto<Elf32_Word>(match[2]),
          mijo::xsmto<Elf32_Addr>(match[3]), mijo::xsmto<std::uint32_t>(match[4]),
          mijo::smto<int>(match[5]), mijo::to_string_view(match[6]), mijo::to_string_view(match[7]),
          mijo::to_string_view(match[8]), scanning_context);
      scanning_context.m_curr_unit_lookup->emplace(unit.name, unit);
      line_number += 1u;
      head = match[0].second;
      continue;
    }
    if (std::regex_search(head, tail, match, re_section_layout_4column_unit_unused,
                          std::regex_constants::match_continuous))
    {
      const Unit& unit = this->units.emplace_back(
          mijo::xsmto<Elf32_Word>(match[1]), mijo::to_string_view(match[2]),
          mijo::to_string_view(match[3]), mijo::to_string_view(match[4]), scanning_context);
      scanning_context.m_curr_unit_lookup->emplace(unit.name, unit);
      line_number += 1u;
      head = match[0].second;
      continue;
    }
    if (std::regex_search(head, tail, match, re_section_layout_4column_unit_entry,
                          std::regex_constants::match_continuous))
    {
      const std::string_view symbol_name = mijo::to_string_view(match[5]),
                             entry_parent_name = mijo::to_string_view(match[6]),
                             module_name = mijo::to_string_view(match[7]),
                             source_name = mijo::to_string_view(match[8]);
      // TODO: I want to use std::ranges::reverse_view, but it Clang doesn't support it yet.
      for (auto parent_unit = this->units.rbegin(); parent_unit != this->units.rend();
           ++parent_unit)
      {
        if (source_name != parent_unit->source_name || module_name != parent_unit->module_name)
          return Error::SectionLayoutOrphanedEntry;
        if (entry_parent_name != parent_unit->name)
          continue;
        const Unit& unit = this->units.emplace_back(
            mijo::xsmto<std::uint32_t>(match[1]), mijo::xsmto<Elf32_Word>(match[2]),
            mijo::xsmto<Elf32_Addr>(match[3]), mijo::xsmto<std::uint32_t>(match[4]), symbol_name,
            &parent_unit.operator*(), module_name, source_name, scanning_context);
        scanning_context.m_curr_unit_lookup->emplace(unit.name, unit);
        parent_unit->entry_children.push_back(&unit);
        line_number += 1u;
        head = match[0].second;
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
      const std::string_view special_name = mijo::to_string_view(match[6]);
      if (special_name == "*fill*")
      {
        this->units.emplace_back(
            mijo::xsmto<std::uint32_t>(match[1]), mijo::xsmto<Elf32_Word>(match[2]),
            mijo::xsmto<Elf32_Addr>(match[3]), mijo::xsmto<std::uint32_t>(match[4]),
            mijo::smto<int>(match[5]), Unit::Trait::Fill1);
        line_number += 1u;
        head = match[0].second;
        continue;
      }
      if (special_name == "**fill**")
      {
        this->units.emplace_back(
            mijo::xsmto<std::uint32_t>(match[1]), mijo::xsmto<Elf32_Word>(match[2]),
            mijo::xsmto<Elf32_Addr>(match[3]), mijo::xsmto<std::uint32_t>(match[4]),
            mijo::smto<int>(match[5]), Unit::Trait::Fill2);
        line_number += 1u;
        head = match[0].second;
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
  ScanningContext scanning_context{*this, line_number, false, false, false, nullptr, {}, {}};

  while (true)
  {
    if (std::regex_search(head, tail, match, re_section_layout_3column_unit_normal,
                          std::regex_constants::match_continuous))
    {
      const Unit& unit = this->units.emplace_back(
          mijo::xsmto<std::uint32_t>(match[1]), mijo::xsmto<Elf32_Word>(match[2]),
          mijo::xsmto<Elf32_Addr>(match[3]), std::uint32_t{0}, mijo::smto<int>(match[4]),
          mijo::to_string_view(match[5]), mijo::to_string_view(match[6]),
          mijo::to_string_view(match[7]), scanning_context);
      scanning_context.m_curr_unit_lookup->emplace(unit.name, unit);
      line_number += 1u;
      head = match[0].second;
      continue;
    }
    if (std::regex_search(head, tail, match, re_section_layout_tloztp_unit_entry,
                          std::regex_constants::match_continuous))
    {
      std::string_view symbol_name = mijo::to_string_view(match[4]),
                       entry_parent_name = mijo::to_string_view(match[5]),
                       module_name = mijo::to_string_view(match[6]),
                       source_name = mijo::to_string_view(match[7]);
      // TODO: I want to use std::ranges::reverse_view, but it Clang doesn't support it yet.
      for (auto parent_unit = this->units.rbegin(); parent_unit != this->units.rend();
           ++parent_unit)
      {
        if (source_name != parent_unit->source_name || module_name != parent_unit->module_name)
          return Error::SectionLayoutOrphanedEntry;
        if (entry_parent_name != parent_unit->name)
          continue;
        const Unit& unit = this->units.emplace_back(
            mijo::xsmto<std::uint32_t>(match[1]), mijo::xsmto<Elf32_Word>(match[2]),
            mijo::xsmto<Elf32_Addr>(match[3]), std::uint32_t{0}, symbol_name,
            &parent_unit.operator*(), module_name, source_name, scanning_context);
        scanning_context.m_curr_unit_lookup->emplace(unit.name, unit);
        parent_unit->entry_children.push_back(&unit);
        line_number += 1u;
        head = match[0].second;
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
      std::string_view special_name = mijo::to_string_view(match[5]);
      if (special_name == "*fill*")
      {
        this->units.emplace_back(  //
            mijo::xsmto<std::uint32_t>(match[1]), mijo::xsmto<Elf32_Word>(match[2]),
            mijo::xsmto<Elf32_Addr>(match[3]), std::uint32_t{0}, mijo::smto<int>(match[4]),
            Unit::Trait::Fill1);
        line_number += 1u;
        head = match[0].second;
        continue;
      }
      if (special_name == "**fill**")
      {
        this->units.emplace_back(  //
            mijo::xsmto<std::uint32_t>(match[1]), mijo::xsmto<Elf32_Word>(match[2]),
            mijo::xsmto<Elf32_Addr>(match[3]), std::uint32_t{0}, mijo::smto<int>(match[4]),
            Unit::Trait::Fill2);
        line_number += 1u;
        head = match[0].second;
        continue;
      }
      return Error::SectionLayoutSpecialNotFill;
    }
    break;
  }
  return Error::None;
}

void Map::SectionLayout::Print(std::ostream& stream, std::size_t& line_number) const
{
  // "\r\n\r\n%s section layout\r\n"
  mijo::print(stream, "\r\n\r\n{:s} section layout\r\n", name);
  if (GetMinVersion() < Version::version_3_0_4)
  {
    mijo::print(stream, "  Starting        Virtual\r\n"
                        "  address  Size   address\r\n"
                        "  -----------------------\r\n");
    line_number += 6u;
    for (const auto& unit : this->units)
      unit.Print3Column(stream, line_number);
  }
  else
  {
    mijo::print(stream, "  Starting        Virtual  File\r\n"
                        "  address  Size   address  offset\r\n"
                        "  ---------------------------------\r\n");
    line_number += 6u;
    for (const auto& unit : this->units)
      unit.Print4Column(stream, line_number);
  }
}

void Map::SectionLayout::Unit::Print3Column(std::ostream& stream, std::size_t& line_number) const
{
  switch (this->unit_kind)
  {
  case Kind::Normal:
    // "  %08x %06x %08x %2i %s \t%s %s\r\n"
    mijo::print(stream, "  {:08x} {:06x} {:08x} {:2d} {:s} \t{:s} {:s}\r\n", starting_address, size,
                virtual_address, alignment, name, module_name, source_name);
    line_number += 1u;
    return;
  case Kind::Unused:
    // "  UNUSED   %06x ........ %s %s %s\r\n"
    mijo::print(stream, "  UNUSED   {:06x} ........ {:s} {:s} {:s}\r\n", size, name, module_name,
                source_name);
    line_number += 1u;
    return;
  case Kind::Entry:
    // "  %08lx %06lx %08lx %s (entry of %s) \t%s %s\r\n"
    mijo::print(stream, "  {:08x} {:06x} {:08x} {:s} (entry of {:s}) \t{:s} {:s}\r\n",
                starting_address, size, virtual_address, name, entry_parent->name, module_name,
                source_name);
    line_number += 1u;
    return;
  case Kind::Special:
    assert(false);
    return;
  }
}

void Map::SectionLayout::Unit::Print4Column(std::ostream& stream, std::size_t& line_number) const
{
  switch (this->unit_kind)
  {
  case Kind::Normal:
    // "  %08x %06x %08x %08x %2i %s \t%s %s\r\n"
    mijo::print(stream, "  {:08x} {:06x} {:08x} {:08x} {:2d} {:s} \t{:s} {:s}\r\n",
                starting_address, size, virtual_address, file_offset, alignment, name, module_name,
                source_name);
    line_number += 1u;
    return;
  case Kind::Unused:
    // "  UNUSED   %06x ........ ........    %s %s %s\r\n"
    mijo::print(stream, "  UNUSED   {:06x} ........ ........    {:s} {:s} {:s}\r\n", size, name,
                module_name, source_name);
    line_number += 1u;
    return;
  case Kind::Entry:
    // "  %08lx %06lx %08lx %08lx    %s (entry of %s) \t%s %s\r\n"
    mijo::print(stream, "  {:08x} {:06x} {:08x} {:08x}    {:s} (entry of {:s}) \t{:s} {:s}\r\n",
                starting_address, size, virtual_address, file_offset, name, entry_parent->name,
                module_name, source_name);
    line_number += 1u;
    return;
  case Kind::Special:
    // "  %08x %06x %08x %08x %2i %s\r\n"
    mijo::print(stream, "  {:08x} {:06x} {:08x} {:08x} {:2d} {:s}\r\n", starting_address, size,
                virtual_address, file_offset, alignment, ToSpecialName(unit_trait));
    line_number += 1u;
    return;
  }
}

constexpr std::string_view Map::SectionLayout::Unit::ToSpecialName(const Trait unit_trait)
{
  if (unit_trait == Unit::Trait::Fill1)
    return "*fill*";
  if (unit_trait == Unit::Trait::Fill2)
    return "**fill**";
  assert(false);
  return "";
}

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
    this->normal_units.emplace_back(
        mijo::to_string_view(match[1]), mijo::xsmto<Elf32_Addr>(match[2]),
        mijo::xsmto<Elf32_Word>(match[3]), mijo::xsmto<std::uint32_t>(match[4]));
    line_number += 1u;
    head = match[0].second;
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
    this->normal_units.emplace_back(
        mijo::to_string_view(match[1]), mijo::xsmto<Elf32_Addr>(match[2]),
        mijo::xsmto<Elf32_Word>(match[3]), mijo::xsmto<std::uint32_t>(match[4]),
        mijo::xsmto<std::uint32_t>(match[5]), mijo::xsmto<std::uint32_t>(match[6]));
    line_number += 1u;
    head = match[0].second;
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
    const std::csub_match& size = match[2];
    if (size.length() == 8 && *size.first == '0')  // Make sure it's not just an overflowed value
      this->SetVersionRange(Version::version_3_0_4, Version::Latest);
    this->debug_units.emplace_back(mijo::to_string_view(match[1]), mijo::xsmto<Elf32_Word>(size),
                                   mijo::xsmto<std::uint32_t>(match[3]));
    line_number += 1u;
    head = match[0].second;
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
    this->normal_units.emplace_back(
        mijo::to_string_view(match[1]), mijo::xsmto<Elf32_Addr>(match[2]),
        mijo::xsmto<Elf32_Word>(match[3]), mijo::xsmto<std::uint32_t>(match[4]));
    line_number += 1u;
    head = match[0].second;
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
    this->normal_units.emplace_back(
        mijo::to_string_view(match[1]), mijo::xsmto<Elf32_Addr>(match[2]),
        mijo::xsmto<Elf32_Word>(match[3]), mijo::xsmto<std::uint32_t>(match[4]),
        mijo::xsmto<std::uint32_t>(match[5]), mijo::xsmto<std::uint32_t>(match[6]));
    line_number += 1u;
    head = match[0].second;
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
    this->normal_units.emplace_back(
        mijo::to_string_view(match[1]), mijo::xsmto<Elf32_Addr>(match[2]),
        mijo::xsmto<Elf32_Word>(match[3]), mijo::xsmto<std::uint32_t>(match[4]),
        mijo::smto<int>(match[5]));
    line_number += 1u;
    head = match[0].second;
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
    this->normal_units.emplace_back(
        mijo::to_string_view(match[1]), mijo::xsmto<Elf32_Addr>(match[2]),
        mijo::xsmto<Elf32_Word>(match[3]), mijo::xsmto<std::uint32_t>(match[4]),
        mijo::xsmto<std::uint32_t>(match[5]), mijo::to_string_view(match[6]));
    line_number += 1u;
    head = match[0].second;
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
    this->normal_units.emplace_back(
        mijo::to_string_view(match[1]), mijo::xsmto<Elf32_Addr>(match[2]),
        mijo::xsmto<Elf32_Word>(match[3]), mijo::xsmto<std::uint32_t>(match[4]),
        mijo::xsmto<std::uint32_t>(match[5]), mijo::xsmto<std::uint32_t>(match[6]),
        mijo::smto<int>(match[7]));
    line_number += 1u;
    head = match[0].second;
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
    this->normal_units.emplace_back(
        mijo::to_string_view(match[1]), mijo::xsmto<Elf32_Addr>(match[2]),
        mijo::xsmto<Elf32_Word>(match[3]), mijo::xsmto<std::uint32_t>(match[4]),
        mijo::xsmto<std::uint32_t>(match[5]), mijo::xsmto<std::uint32_t>(match[6]),
        mijo::xsmto<std::uint32_t>(match[7]), mijo::to_string_view(match[8]));
    line_number += 1u;
    head = match[0].second;
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
    this->normal_units.emplace_back(
        mijo::to_string_view(match[1]), mijo::xsmto<Elf32_Addr>(match[2]),
        mijo::xsmto<Elf32_Word>(match[3]), mijo::xsmto<std::uint32_t>(match[4]),
        mijo::smto<int>(match[5]), mijo::xsmto<std::uint32_t>(match[6]),
        mijo::to_string_view(match[7]));
    line_number += 1u;
    head = match[0].second;
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
    this->normal_units.emplace_back(
        mijo::to_string_view(match[1]), mijo::xsmto<Elf32_Addr>(match[2]),
        mijo::xsmto<Elf32_Word>(match[3]), mijo::xsmto<std::uint32_t>(match[4]),
        mijo::xsmto<std::uint32_t>(match[5]), mijo::xsmto<std::uint32_t>(match[6]),
        mijo::smto<int>(match[7]), mijo::xsmto<std::uint32_t>(match[8]),
        mijo::to_string_view(match[9]));
    line_number += 1u;
    head = match[0].second;
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
    this->debug_units.emplace_back(mijo::to_string_view(match[1]),
                                   mijo::xsmto<Elf32_Word>(match[2]),
                                   mijo::xsmto<std::uint32_t>(match[3]));
    line_number += 1u;
    head = match[0].second;
  }
  return Error::None;
}

void Map::MemoryMap::Print(std::ostream& stream, std::size_t& line_number) const
{
  mijo::print(stream, "\r\n\r\nMemory map:\r\n");
  line_number += 3u;
  if (GetMinVersion() < Version::version_4_2_build_142)
  {
    if (has_rom_ram)
      PrintRomRam_old(stream, line_number);
    else
      PrintSimple_old(stream, line_number);
    PrintDebug_old(stream, line_number);
  }
  else
  {
    if (has_rom_ram)
      if (has_s_record)
        if (has_bin_file)
          PrintRomRamSRecordBinFile(stream, line_number);
        else
          PrintRomRamSRecord(stream, line_number);
      else if (has_bin_file)
        PrintRomRamBinFile(stream, line_number);
      else
        PrintRomRam(stream, line_number);
    else if (has_s_record)
      if (has_bin_file)
        PrintSRecordBinFile(stream, line_number);
      else
        PrintSRecord(stream, line_number);
    else if (has_bin_file)
      PrintBinFile(stream, line_number);
    else
      PrintSimple(stream, line_number);
    PrintDebug(stream, line_number);
  }
}

void Map::MemoryMap::PrintSimple_old(std::ostream& stream, std::size_t& line_number) const
{
  // clang-format off
  mijo::print(stream, "                   Starting Size     File\r\n"
                      "                   address           Offset\r\n");
  // clang-format on
  line_number += 2u;
  for (const auto& unit : normal_units)
    unit.PrintSimple_old(stream, line_number);
}
void Map::MemoryMap::UnitNormal::PrintSimple_old(std::ostream& stream,
                                                 std::size_t& line_number) const
{
  // "  %15s  %08x %08x %08x\r\n"
  mijo::print(stream, "  {:>15s}  {:08x} {:08x} {:08x}\r\n", name, starting_address, size,
              file_offset);
  line_number += 1u;
}

void Map::MemoryMap::PrintRomRam_old(std::ostream& stream, std::size_t& line_number) const
{
  // clang-format off
  mijo::print(stream, "                   Starting Size     File     ROM      RAM Buffer\r\n"
                      "                   address           Offset   Address  Address\r\n");
  // clang-format on
  line_number += 2u;
  for (const auto& unit : normal_units)
    unit.PrintRomRam_old(stream, line_number);
}
void Map::MemoryMap::UnitNormal::PrintRomRam_old(std::ostream& stream,
                                                 std::size_t& line_number) const
{
  // "  %15s  %08x %08x %08x %08x %08x\r\n"
  mijo::print(stream, "  {:>15s}  {:08x} {:08x} {:08x} {:08x} {:08x}\r\n", name, starting_address,
              size, file_offset, rom_address, ram_buffer_address);
  line_number += 1u;
}

void Map::MemoryMap::PrintDebug_old(std::ostream& stream, std::size_t& line_number) const
{
  if (GetMinVersion() < Version::version_3_0_4)
    for (const auto& unit : debug_units)
      unit.Print_older(stream, line_number);
  else
    for (const auto& unit : debug_units)
      unit.Print_old(stream, line_number);
}
void Map::MemoryMap::UnitDebug::Print_older(std::ostream& stream, std::size_t& line_number) const
{
  // "  %15s           %06x %08x\r\n"
  mijo::print(stream, "  {:>15s}           {:06x} {:08x}\r\n", name, size, file_offset);
  line_number += 1u;
}
void Map::MemoryMap::UnitDebug::Print_old(std::ostream& stream, std::size_t& line_number) const
{
  // "  %15s           %08x %08x\r\n"
  mijo::print(stream, "  {:>15s}           {:08x} {:08x}\r\n", name, size, file_offset);
  line_number += 1u;
}

void Map::MemoryMap::PrintSimple(std::ostream& stream, std::size_t& line_number) const
{
  // clang-format off
  mijo::print(stream, "                       Starting Size     File\r\n"
                      "                       address           Offset\r\n");
  // clang-format on
  line_number += 2u;
  for (const auto& unit : normal_units)
    unit.PrintSimple(stream, line_number);
}
void Map::MemoryMap::UnitNormal::PrintSimple(std::ostream& stream, std::size_t& line_number) const
{
  // "  %20s %08x %08x %08x\r\n"
  mijo::print(stream, "  {:>20s} {:08x} {:08x} {:08x}\r\n", name, starting_address, size,
              file_offset);
  line_number += 1u;
}

void Map::MemoryMap::PrintRomRam(std::ostream& stream, std::size_t& line_number) const
{
  // clang-format off
  mijo::print(stream, "                       Starting Size     File     ROM      RAM Buffer\r\n"
                      "                       address           Offset   Address  Address\r\n");
  // clang-format on
  line_number += 2u;
  for (const auto& unit : normal_units)
    unit.PrintRomRam(stream, line_number);
}
void Map::MemoryMap::UnitNormal::PrintRomRam(std::ostream& stream, std::size_t& line_number) const
{
  // "  %20s %08x %08x %08x %08x %08x\r\n"
  mijo::print(stream, "  {:>20s} {:08x} {:08x} {:08x} {:08x} {:08x}\r\n", name, starting_address,
              size, file_offset, rom_address, ram_buffer_address);
  line_number += 1u;
}

void Map::MemoryMap::PrintSRecord(std::ostream& stream, std::size_t& line_number) const
{
  // clang-format off
  mijo::print(stream, "                       Starting Size     File       S-Record\r\n"
                      "                       address           Offset     Line\r\n");
  // clang-format on
  line_number += 2u;
  for (const auto& unit : normal_units)
    unit.PrintSRecord(stream, line_number);
}
void Map::MemoryMap::UnitNormal::PrintSRecord(std::ostream& stream, std::size_t& line_number) const
{
  // "  %20s %08x %08x %08x %10i\r\n"
  mijo::print(stream, "  {:>20s} {:08x} {:08x} {:08x} {:10d}\r\n", name, starting_address, size,
              file_offset, s_record_line);
  line_number += 1u;
}

void Map::MemoryMap::PrintBinFile(std::ostream& stream, std::size_t& line_number) const
{
  // clang-format off
  mijo::print(stream, "                       Starting Size     File     Bin File Bin File\r\n"
                      "                       address           Offset   Offset   Name\r\n");
  // clang-format on
  line_number += 2u;
  for (const auto& unit : normal_units)
    unit.PrintBinFile(stream, line_number);
}
void Map::MemoryMap::UnitNormal::PrintBinFile(std::ostream& stream, std::size_t& line_number) const
{
  // "  %20s %08x %08x %08x %08x %s\r\n"
  mijo::print(stream, "  {:>20s} {:08x} {:08x} {:08x} {:08x} {:s}\r\n", name, starting_address,
              size, file_offset, bin_file_offset, bin_file_name);
  line_number += 1u;
}

void Map::MemoryMap::PrintRomRamSRecord(std::ostream& stream, std::size_t& line_number) const
{
  // clang-format off
  mijo::print(stream, "                       Starting Size     File     ROM      RAM Buffer  S-Record\r\n"
                      "                       address           Offset   Address  Address     Line\r\n");
  // clang-format on
  line_number += 2u;
  for (const auto& unit : normal_units)
    unit.PrintRomRamSRecord(stream, line_number);
}
void Map::MemoryMap::UnitNormal::PrintRomRamSRecord(std::ostream& stream,
                                                    std::size_t& line_number) const
{
  // "  %20s %08x %08x %08x %08x %08x %10i\r\n"
  mijo::print(stream, "  {:>20s} {:08x} {:08x} {:08x} {:08x} {:08x} {:10d}\r\n", name,
              starting_address, size, file_offset, rom_address, ram_buffer_address, s_record_line);
  line_number += 1u;
}

void Map::MemoryMap::PrintRomRamBinFile(std::ostream& stream, std::size_t& line_number) const
{
  // clang-format off
  mijo::print(stream, "                       Starting Size     File     ROM      RAM Buffer Bin File Bin File\r\n"
                      "                       address           Offset   Address  Address    Offset   Name\r\n");
  // clang-format on
  line_number += 2u;
  for (const auto& unit : normal_units)
    unit.PrintRomRamBinFile(stream, line_number);
}
void Map::MemoryMap::UnitNormal::PrintRomRamBinFile(std::ostream& stream,
                                                    std::size_t& line_number) const
{
  // "  %20s %08x %08x %08x %08x %08x   %08x %s\r\n"
  mijo::print(stream, "  {:>20s} {:08x} {:08x} {:08x} {:08x} {:08x}   {:08x} {:s}\r\n", name,
              starting_address, size, file_offset, rom_address, ram_buffer_address, bin_file_offset,
              bin_file_name);
  line_number += 1u;
}

void Map::MemoryMap::PrintSRecordBinFile(std::ostream& stream, std::size_t& line_number) const
{
  // clang-format off
  mijo::print(stream, "                       Starting Size     File        S-Record Bin File Bin File\r\n"
                      "                       address           Offset      Line     Offset   Name\r\n");
  // clang-format on
  line_number += 2u;
  for (const auto& unit : normal_units)
    unit.PrintSRecordBinFile(stream, line_number);
}
void Map::MemoryMap::UnitNormal::PrintSRecordBinFile(std::ostream& stream,
                                                     std::size_t& line_number) const
{
  // "  %20s %08x %08x %08x  %10i %08x %s\r\n"
  mijo::print(stream, "  {:>20s} {:08x} {:08x} {:08x}  {:10d} {:08x} {:s}\r\n", name,
              starting_address, size, file_offset, s_record_line, bin_file_offset, bin_file_name);
  line_number += 1u;
}

void Map::MemoryMap::PrintRomRamSRecordBinFile(std::ostream& stream, std::size_t& line_number) const
{
  // clang-format off
  mijo::print(stream, "                       Starting Size     File     ROM      RAM Buffer    S-Record Bin File Bin File\r\n"
                      "                       address           Offset   Address  Address       Line     Offset   Name\r\n");
  // clang-format on
  line_number += 2u;
  for (const auto& unit : normal_units)
    unit.PrintRomRamSRecordBinFile(stream, line_number);
}
void Map::MemoryMap::UnitNormal::PrintRomRamSRecordBinFile(std::ostream& stream,
                                                           std::size_t& line_number) const
{
  // "  %20s %08x %08x %08x %08x %08x    %10i %08x %s\r\n"
  mijo::print(stream, "  {:>20s} {:08x} {:08x} {:08x} {:08x} {:08x}    {:10d} {:08x} {:s}\r\n",
              name, starting_address, size, file_offset, rom_address, ram_buffer_address,
              s_record_line, bin_file_offset, bin_file_name);
  line_number += 1u;
}

void Map::MemoryMap::PrintDebug(std::ostream& stream, std::size_t& line_number) const
{
  for (const auto& unit : debug_units)
    unit.Print(stream, line_number);
}
void Map::MemoryMap::UnitDebug::Print(std::ostream& stream, std::size_t& line_number) const
{
  // "  %20s          %08x %08x\r\n"
  mijo::print(stream, "  {:>20s}          {:08x} {:08x}\r\n", name, size, file_offset);
  line_number += 1u;
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
    this->units.emplace_back(mijo::to_string_view(match[1]), mijo::xsmto<Elf32_Addr>(match[2]));
    line_number += 1u;
    head = match[0].second;
  }
  return Error::None;
}

void Map::LinkerGeneratedSymbols::Print(std::ostream& stream, std::size_t& line_number) const
{
  mijo::print(stream, "\r\n\r\nLinker generated symbols:\r\n");
  line_number += 3u;
  for (const auto& unit : units)
    unit.Print(stream, line_number);
}

void Map::LinkerGeneratedSymbols::Unit::Print(std::ostream& stream, std::size_t& line_number) const
{
  // "%25s %08x\r\n"
  mijo::print(stream, "{:>25s} {:08x}\r\n", name, value);
  line_number += 1u;
}
}  // namespace MWLinker
