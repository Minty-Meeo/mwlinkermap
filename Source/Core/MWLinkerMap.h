#pragma once

#include <algorithm>
#include <cstddef>
#include <istream>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

// #define DOLPHIN

#ifdef DOLPHIN  // Dolphin Emulator
#include "Common/CommonTypes.h"
#else  // mwlinkermap-temp
using u32 = std::uint32_t;
#endif

namespace MWLinker
{
enum class Version
{
  Unknown,
  // Codewarrior for GCN 1.0   (May 21 2000 19:00:24)
  version_2_3_3_build_126,
  // CodeWarrior for GCN 1.1   (Feb  7 2001 12:15:53)
  version_2_3_3_build_137,
  // CodeWarrior for GCN 1.2.5 (Jun 12 2001 11:53:24)
  version_2_4_1_build_47,
  // CodeWarrior for GCN 1.3.2 (May  7 2002 23:43:34)
  version_2_4_2_build_81,
  // CodeWarrior for GCN 2.0   (Sep 16 2002 15:15:26)
  version_2_4_7_build_92,
  // CodeWarrior for GCN 2.5   (Nov  7 2002 12:45:57)
  version_2_4_7_build_102,
  // CodeWarrior for GCN 2.6   (Jul 14 2003 14:20:31)
  version_2_4_7_build_107,
  // CodeWarrior for GCN 2.7   (Aug 13 2004 10:40:59)
  version_3_0_4,
  // CodeWarrior for GCN 3.0a3 (Dec 13 2005 17:41:17)
  version_4_1_build_51213,
  // CodeWarrior for GCN 3.0   (Mar 20 2006 23:19:16)
  version_4_2_build_60320,
  // CodeWarrior for Wii 1.0   (Aug 26 2008 02:33:56)
  version_4_2_build_142,
  // CodeWarrior for Wii 1.1   (Apr  2 2009 15:05:36)
  version_4_3_build_151,
  // CodeWarrior for Wii 1.3   (Apr 23 2010 11:39:30)
  version_4_3_build_172,
  // CodeWarrior for Wii 1.7   (Sep  5 2011 13:02:03)
  version_4_3_build_213,
};

enum class Type
{
  // STT_NOTYPE
  notype = 0,
  // STT_OBJECT
  object = 1,
  // STT_FUNC
  func = 2,
  // STT_SECTION
  section = 3,
  // STT_FILE
  file = 4,
  // Default for an unknown ST_TYPE
  unknown = -1,
};

enum class Bind
{
  // STB_LOCAL
  local = 0,
  // STB_GLOBAL
  global = 1,
  // STB_WEAK
  weak = 2,
  // Proprietary binding
  multidef = 13,
  // Proprietary binding
  overload = 14,
  // Default for an unknown ST_BIND
  unknown = -1,
};

struct Map
{
  enum class Error
  {
    None,
    Fail,
    Unimplemented,
    GarbageFound,
    Warning,

    EntryPointNameMissing,
    SMGalaxyYouHadOneJob,

    SymbolClosureHierarchySkip,
    SymbolClosureInvalidHierarchy,
    SymbolClosureInvalidSymbolType,
    SymbolClosureInvalidSymbolBind,
    SymbolClosureUnrefDupsHierarchyMismatch,
    SymbolClosureUnrefDupsNameMismatch,
    SymbolClosureUnrefDupsEmpty,

    // TODO: remove
    EPPC_PatterMatchingIndistinctObjectName,

    EPPC_PatternMatchingMergingFirstNameMismatch,
    EPPC_PatternMatchingMergingSecondNameMismatch,
    EPPC_PatternMatchingMergingSizeMismatch,
    EPPC_PatternMatchingMergingInterchangeMissingEpilogue,
    EPPC_PatternMatchingFoldingNewBranchFunctionNameMismatch,

    SectionLayoutBadPrologue,
    SectionLayoutOrphanedEntry,
    SectionLayoutSpecialNotFill,

    MemoryMapBadPrologue,
  };

  enum class Warning
  {
    // Either your linker map violates the One Definition Rule, or you have found multiple
    // compilation units with identical names! Either way, the previous definition of this
    // symbol in its section layout's lookup will be discarded.
    // https://en.cppreference.com/w/cpp/language/definition#One_Definition_Rule
    SectionLayoutOneDefinitionRuleViolated,
    // A less critical variant of the ODR violation. Before CW for GCN 2.7, compiling with the '-sym
    // on' flag
    // TODO: finish explaining
    // This is for symbols with the same name as the
    // More than one symbol named things like ".text" or ".bss" has been found. This is like a
    // finer-toothed version of the SectionLayoutOneDefinitionRuleViolated warning that can help to
    // identify same-name compilation units.
    SectionLayoutMultipleSectionTypeSymbols,
  };

  struct UnitDebugInfo;
  using DebugInfo = std::map<std::string, std::map<std::string, UnitDebugInfo>>;

  struct PortionBase
  {
    virtual ~PortionBase() = default;

    void SetMinVersion(const Version version) noexcept
    {
      min_version = std::max(min_version, version);
    }
    virtual void Print(std::ostream&) const = 0;
    virtual bool IsEmpty() const noexcept = 0;

    Version min_version = Version::Unknown;
  };

  struct SymbolClosure final : PortionBase
  {
    // CodeWarrior for GCN 1.1
    //  - Added UNREFERENCED DUPLICATE info.
    // CodeWarrior for GCN 2.7
    //  - Symbol closure became optional with '-[no]listclosure', off by default.
    //  - Changed behavior of the source name when linking static libs
    //  - Added _ctors$99 and _dtors$99, among other things.

    struct NodeBase
    {
      NodeBase() = default;  // Necessary for root node
      NodeBase(NodeBase* parent_) : parent(parent_) {}
      virtual ~NodeBase() = default;

      virtual void Print(std::ostream&, int) const;  // Necessary for root and fake _dtor$99 node
      static void PrintPrefix(std::ostream&, int);
      static constexpr std::string_view ToName(Type) noexcept;
      static constexpr std::string_view ToName(Bind) noexcept;
      // virtual void Export(DebugInfo&) const noexcept;

      NodeBase* parent;
      std::list<std::unique_ptr<NodeBase>> children;
    };

    struct NodeReal final : NodeBase
    {
      struct UnreferencedDuplicate
      {
        UnreferencedDuplicate(Type type_, Bind bind_, std::string module_name_,
                              std::string source_name_)
            : type(type_), bind(bind_), module_name(std::move(module_name_)),
              source_name(std::move(source_name_))
        {
        }

        void Print(std::ostream&, int) const;

        Type type;
        Bind bind;
        std::string module_name;
        std::string source_name;
      };

      NodeReal(NodeBase* parent_, std::string name_, Type type_, Bind bind_,
               std::string module_name_, std::string source_name_,
               std::list<UnreferencedDuplicate> unref_dups_)
          : NodeBase(parent_), name(std::move(name_)), type(type_), bind(bind_),
            module_name(std::move(module_name_)), source_name(std::move(source_name_)),
            unref_dups(std::move(unref_dups_))
      {
      }
      virtual ~NodeReal() override = default;

      virtual void Print(std::ostream&, int) const override;
      // virtual void Export(DebugInfo&) const noexcept override;

      std::string name;
      Type type;
      Bind bind;
      // Static library or object name
      std::string module_name;
      // When linking a static library, this is either:
      // A) The name of the STT_FILE symbol from the relevant object in the static library.
      // B) The name of the relevant object in the static library (as early as CW for GCN 2.7).
      std::string source_name;
      std::list<UnreferencedDuplicate> unref_dups;
    };

    struct NodeLinkerGenerated final : NodeBase
    {
      NodeLinkerGenerated(NodeBase* parent_, std::string name_)
          : NodeBase(parent_), name(std::move(name_))
      {
      }
      virtual ~NodeLinkerGenerated() override = default;

      virtual void Print(std::ostream&, int) const override;
      // virtual void Export(DebugInfo&) const noexcept override;

      std::string name;
    };

    using NodeLookup = std::multimap<std::string, const NodeReal&>;
    using ModuleLookup = std::map<std::string, NodeLookup>;

    SymbolClosure() = default;
    virtual ~SymbolClosure() override = default;

    Error Scan(const char*&, const char*, std::size_t&,
               std::list<std::pair<std::size_t, std::string>>&);
    virtual void Print(std::ostream&) const override;
    void Export(DebugInfo&) const noexcept;
    virtual bool IsEmpty() const noexcept override { return root.children.empty(); }

    NodeBase root;
    ModuleLookup lookup;

    struct Warn
    {
      static inline bool do_warn_odr_violation = true;
      static inline bool do_warn_sym_on_flag_detected = true;

    private:
      static void OneDefinitionRuleViolation(std::size_t, std::string_view, std::string_view);
      static void SymOnFlagDetected(std::size_t, std::string_view);

      friend SymbolClosure;
    };
  };

  struct EPPC_PatternMatching final : PortionBase
  {
    // CodeWarrior for Wii 1.0
    //  - Added EPPC_PatternMatching

    struct MergingUnit
    {
      MergingUnit(std::string_view first_name_, std::string_view second_name_, u32 size_,
                  bool will_be_replaced_, bool was_interchanged_)
          : first_name(first_name_), second_name(second_name_), size(size_),
            will_be_replaced(will_be_replaced_), was_interchanged(was_interchanged_)
      {
      }

      void Print(std::ostream&) const;

      std::string first_name;
      std::string second_name;
      u32 size;
      // If the conditions are right (e.g. the function is more than just a BLR instruction), then
      // one function is replaced with a branch to the other function, saving space at the cost of a
      // tiny amount of overhead. This is by far the more common code merging technique.
      bool will_be_replaced;
      // Rarely, a function can be marked for removal when a duplicate of it is elsewhere in the
      // binary. All references to it are then redirected to the duplicate. Even rarer than that,
      // sometimes the linker can change its mind and replace it with a branch instead.
      bool was_interchanged;
    };

    struct FoldingUnit
    {
      struct Unit
      {
        Unit(std::string_view first_name_, std::string_view second_name_, u32 size_,
             bool new_branch_function_)
            : first_name(first_name_), second_name(second_name_), size(size_),
              new_branch_function(new_branch_function_)
        {
        }

        void Print(std::ostream&) const;

        std::string first_name;
        std::string second_name;
        u32 size;
        bool new_branch_function;
      };

      using UnitLookup = std::multimap<std::string_view, const Unit&>;
      using ModuleLookup = std::map<std::string_view, UnitLookup>;

      FoldingUnit(std::string_view object_name_) : object_name(object_name_) {}

      void Print(std::ostream&) const;

      std::string object_name;
      std::list<Unit> units;
    };

    using MergingUnitLookup = std::multimap<std::string_view, const MergingUnit&>;

    EPPC_PatternMatching() { SetMinVersion(Version::version_4_2_build_142); }
    virtual ~EPPC_PatternMatching() override = default;

    Error Scan(const char*&, const char*, std::size_t&);
    virtual void Print(std::ostream&) const override;
    virtual bool IsEmpty() const noexcept override
    {
      return merging_units.empty() || folding_units.empty();
    }

    std::list<MergingUnit> merging_units;
    std::list<FoldingUnit> folding_units;
    MergingUnitLookup merging_lookup;
    FoldingUnit::ModuleLookup folding_lookup;

    struct Warn
    {
      static inline bool do_warn_merging_odr_violation = true;
      static inline bool do_warn_folding_repeat_object = true;
      static inline bool do_warn_folding_odr_violation = true;

    private:
      static void MergingOneDefinitionRuleViolation(std::size_t, std::string_view);
      static void FoldingRepeatObject(std::size_t, std::string_view);
      static void FoldingOneDefinitionRuleViolation(std::size_t, std::string_view,
                                                    std::string_view);

      friend EPPC_PatternMatching;
    };
  };

  struct LinkerOpts final : PortionBase
  {
    // CodeWarrior for Wii 1.0
    //  - Added LinkerOpts

    struct Unit
    {
      enum class Kind
      {
        NotNear,
        NotComputed,
        Optimized,
        DisassembleError,
      };

      Unit(std::string module_name_, std::string name_)
          : unit_kind(Kind::DisassembleError), module_name(std::move(module_name_)),
            name(std::move(name_))
      {
      }
      Unit(const Kind unit_kind_, std::string module_name_, std::string name_,
           std::string reference_name_)
          : unit_kind(unit_kind_), module_name(std::move(module_name_)), name(std::move(name_)),
            reference_name(std::move(reference_name_))
      {
      }

      void Print(std::ostream&) const;

      const Kind unit_kind;
      std::string module_name;
      std::string name;
      std::string reference_name;
    };

    LinkerOpts() { SetMinVersion(Version::version_4_2_build_142); }
    virtual ~LinkerOpts() override = default;

    Error Scan(const char*&, const char*, std::size_t&);
    virtual void Print(std::ostream&) const override;
    virtual bool IsEmpty() const noexcept override { return units.empty(); }

    std::list<Unit> units;
  };

  struct BranchIslands final : PortionBase
  {
    // CodeWarror for GCN 3.0a3 (at the earliest)
    //  - Added Branch Islands.

    struct Unit
    {
      Unit(std::string first_name_, std::string second_name_, bool is_safe_)
          : first_name(std::move(first_name_)), second_name(std::move(second_name_)),
            is_safe(is_safe_)
      {
      }

      void Print(std::ostream&) const;

      std::string first_name;
      std::string second_name;
      bool is_safe;
    };

    BranchIslands() { SetMinVersion(Version::version_4_1_build_51213); }
    virtual ~BranchIslands() override = default;

    Error Scan(const char*&, const char*, std::size_t&);
    virtual void Print(std::ostream&) const override;
    virtual bool IsEmpty() const noexcept override { return units.empty(); }

    std::list<Unit> units;
  };

  struct MixedModeIslands final : PortionBase
  {
    // CodeWarror for GCN 3.0a3 (at the earliest)
    //  - Added Mixed Mode Islands.

    struct Unit
    {
      Unit(std::string first_name_, std::string second_name_, bool is_safe_)
          : first_name(std::move(first_name_)), second_name(std::move(second_name_)),
            is_safe(is_safe_)
      {
      }

      void Print(std::ostream&) const;

      std::string first_name;
      std::string second_name;
      bool is_safe;
    };

    MixedModeIslands() { SetMinVersion(Version::version_4_1_build_51213); }
    virtual ~MixedModeIslands() override = default;

    Error Scan(const char*&, const char*, std::size_t&);
    virtual void Print(std::ostream&) const override;
    virtual bool IsEmpty() const noexcept override { return units.empty(); }

    std::list<Unit> units;
  };

  struct LinktimeSizeDecreasingOptimizations final : PortionBase
  {
    LinktimeSizeDecreasingOptimizations() = default;
    virtual ~LinktimeSizeDecreasingOptimizations() override = default;

    Error Scan(const char*&, const char*, std::size_t&);
    virtual void Print(std::ostream&) const override;
    virtual bool IsEmpty() const noexcept override { return true; }
  };

  struct LinktimeSizeIncreasingOptimizations final : PortionBase
  {
    LinktimeSizeIncreasingOptimizations() = default;
    virtual ~LinktimeSizeIncreasingOptimizations() override = default;

    Error Scan(const char*&, const char*, std::size_t&);
    virtual void Print(std::ostream&) const override;
    virtual bool IsEmpty() const noexcept override { return true; }
  };

  struct SectionLayout final : PortionBase
  {
    // CodeWarrior for GCN 2.7
    //  - Changed to four column info, added *fill* symbols.
    //  - Changed the behavior of the source name when linking static libs

    enum class Kind
    {
      Normal,
      BSS,
      Ctors,
      Dtors,
      ExTab,
      ExTabIndex,
    };

    struct Unit;

    using UnitLookup = std::multimap<std::string_view, const Unit&>;
    using ModuleLookup = std::map<std::string_view, UnitLookup>;

    struct Unit
    {
      enum class Kind
      {
        Normal,
        Unused,
        Entry,
        Special,
      };

      enum class Trait
      {
        // Nothing special
        None,
        // Named after the section they are native to. Multiple can appear in a single compilation
        // unit with the '-sym on' option. The size of a section symbol is the total of all symbols,
        // both used and unused, that one is meant to encompass.
        Section,
        // BSS .comm symbols. Printed first.
        Common,
        // BSS .lcomm symbols. Printed later.
        LCommon,
        // Native to the extabindex section.
        ExTabIndex,
        // *fill*
        Fill1,
        // **fill**
        Fill2,
      };

      // UNUSED symbols
      Unit(u32 size_, std::string_view name_, std::string_view module_name_,
           std::string_view source_name_, SectionLayout& section_layout,
           std::string_view& curr_module_name, std::string_view& curr_source_name,
           UnitLookup*& curr_unit_lookup, bool& is_in_lcomm, bool& is_after_eti_init_info,
           bool& is_multi_stt_section, std::size_t line_number)
          : unit_kind(Kind::Unused), size(size_), name(name_), module_name(module_name_),
            source_name(source_name_),
            unit_trait(DeduceUsualSubtext(section_layout, curr_module_name, curr_source_name,
                                          curr_unit_lookup, is_in_lcomm, is_after_eti_init_info,
                                          is_multi_stt_section, line_number))
      {
      }
      // 3-column normal symbols
      Unit(u32 starting_address_, u32 size_, u32 virtual_address_, int alignment_,
           std::string_view name_, std::string_view module_name_, std::string_view source_name_,
           SectionLayout& section_layout, std::string_view& curr_module_name,
           std::string_view& curr_source_name, UnitLookup*& curr_unit_lookup, bool& is_in_lcomm,
           bool& is_after_eti_init_info, bool& is_multi_stt_section, std::size_t line_number)
          : unit_kind(Kind::Normal), starting_address(starting_address_), size(size_),
            virtual_address(virtual_address_), alignment(alignment_), name(name_),
            module_name(module_name_), source_name(source_name_),
            unit_trait(DeduceUsualSubtext(section_layout, curr_module_name, curr_source_name,
                                          curr_unit_lookup, is_in_lcomm, is_after_eti_init_info,
                                          is_multi_stt_section, line_number))
      {
      }
      // 4-column normal symbols
      Unit(u32 starting_address_, u32 size_, u32 virtual_address_, u32 file_offset_, int alignment_,
           std::string_view name_, std::string_view module_name_, std::string_view source_name_,
           SectionLayout& section_layout, std::string_view& curr_module_name,
           std::string_view& curr_source_name, UnitLookup*& curr_unit_lookup, bool& is_in_lcomm,
           bool& is_after_eti_init_info, bool& is_multi_stt_section, std::size_t line_number)
          : unit_kind(Kind::Normal), starting_address(starting_address_), size(size_),
            virtual_address(virtual_address_), file_offset(file_offset_), alignment(alignment_),
            name(name_), module_name(module_name_), source_name(source_name_),
            unit_trait(DeduceUsualSubtext(section_layout, curr_module_name, curr_source_name,
                                          curr_unit_lookup, is_in_lcomm, is_after_eti_init_info,
                                          is_multi_stt_section, line_number))
      {
      }
      // 3-column entry symbols
      Unit(u32 starting_address_, u32 size_, u32 virtual_address_, std::string_view name_,
           const Unit* entry_parent_, std::string_view module_name_, std::string_view source_name_,
           Trait unit_trait_)
          : unit_kind(Kind::Entry), starting_address(starting_address_), size(size_),
            virtual_address(virtual_address_), name(name_), entry_parent(entry_parent_),
            module_name(module_name_), source_name(source_name_), unit_trait(unit_trait_)
      {
      }
      // 4-column entry symbols
      Unit(u32 starting_address_, u32 size_, u32 virtual_address_, u32 file_offset_,
           std::string_view name_, const Unit* entry_parent_, std::string_view module_name_,
           std::string_view source_name_, Trait unit_trait_)
          : unit_kind(Kind::Entry), starting_address(starting_address_), size(size_),
            virtual_address(virtual_address_), file_offset(file_offset_), name(name_),
            entry_parent(entry_parent_), module_name(module_name_), source_name(source_name_),
            unit_trait(unit_trait_)
      {
      }
      // 4-column special symbols
      Unit(u32 starting_address_, u32 size_, u32 virtual_address_, u32 file_offset_, int alignment_,
           Trait unit_trait_)
          : unit_kind(Kind::Special), starting_address(starting_address_), size(size_),
            virtual_address(virtual_address_), file_offset(file_offset_), alignment(alignment_),
            unit_trait(unit_trait_)
      {
      }

      void Print3Column(std::ostream&) const;
      void Print4Column(std::ostream&) const;
      Unit::Trait DeduceUsualSubtext(SectionLayout&, std::string_view&, std::string_view&,
                                     UnitLookup*&, bool&, bool&, bool&, std::size_t);
      static std::string_view ToSpecialName(Trait);
      void Export(DebugInfo&) const noexcept;

      const Kind unit_kind;
      u32 starting_address;
      u32 size;
      u32 virtual_address;
      u32 file_offset;
      int alignment;
      std::string name;
      // Doubly-linked relationship between entry symbols and their host.
      const Unit* entry_parent;
      // Doubly-linked relationship between entry symbols and their host.
      std::list<const Unit*> entry_children;
      // Static library or object name
      std::string module_name;
      // When linking a static library, this is either:
      // A) The name of the STT_FILE symbol from the relevant object in the static library.
      // B) The name of the relevant object in the static library (as early as CW for GCN 2.7).
      std::string source_name;
      const Trait unit_trait;
    };

    SectionLayout(Kind section_kind_, std::string name_)
        : section_kind(section_kind_), name(std::move(name_))
    {
    }
    virtual ~SectionLayout() override = default;

    Error Scan3Column(const char*&, const char*, std::size_t&);
    Error Scan4Column(const char*&, const char*, std::size_t&);
    Error ScanTLOZTP(const char*&, const char*, std::size_t&);
    virtual void Print(std::ostream&) const override;
    void Export(DebugInfo&) const noexcept;
    virtual bool IsEmpty() const noexcept override { return units.empty(); }

    static Kind ToSectionKind(std::string_view);
    Unit::Trait DeduceUsualSubtext(const std::string&, const std::string&, const std::string&,
                                   std::string&, std::string&, UnitLookup*&, bool&, bool&, bool&,
                                   std::size_t);

    const Kind section_kind;
    std::string name;
    std::list<Unit> units;
    ModuleLookup lookup;

    struct Warn
    {
      static inline bool do_warn_repeat_compilation_unit = true;
      static inline bool do_warn_odr_violation = true;
      static inline bool do_warn_sym_on_flag_detected = true;
      static inline bool do_warn_comm_after_lcomm = true;

    private:
      static void RepeatCompilationUnit(std::size_t, std::string_view, std::string_view);
      static void OneDefinitionRuleViolation(std::size_t, std::string_view, std::string_view,
                                             std::string_view);
      static void SymOnFlagDetected(std::size_t, std::string_view, std::string_view);
      static void CommAfterLComm(std::size_t);

      friend SectionLayout;
    };
  };

  // CodeWarrior for GCN 2.7
  //  - Changed size column for debug sections from "%06x" to "%08x".
  // CodeWarrior for Wii 1.0
  //  - Expanded Memory Map variants, slightly tweaked existing printfs.
  struct MemoryMap final : PortionBase
  {
    struct UnitNormal
    {
      UnitNormal(std::string name_, u32 starting_address_, u32 size_, u32 file_offset_)
          : name(std::move(name_)), starting_address(starting_address_), size(size_),
            file_offset(file_offset_)
      {
      }
      UnitNormal(std::string name_, u32 starting_address_, u32 size_, u32 file_offset_,
                 int s_record_line_)
          : name(std::move(name_)), starting_address(starting_address_), size(size_),
            file_offset(file_offset_), s_record_line(s_record_line_)
      {
      }
      UnitNormal(std::string name_, u32 starting_address_, u32 size_, u32 file_offset_,
                 u32 rom_address_, u32 ram_buffer_address_)
          : name(std::move(name_)), starting_address(starting_address_), size(size_),
            file_offset(file_offset_), rom_address(rom_address_),
            ram_buffer_address(ram_buffer_address_)
      {
      }
      UnitNormal(std::string name_, u32 starting_address_, u32 size_, u32 file_offset_,
                 u32 rom_address_, u32 ram_buffer_address_, int s_record_line_)
          : name(std::move(name_)), starting_address(starting_address_), size(size_),
            file_offset(file_offset_), rom_address(rom_address_),
            ram_buffer_address(ram_buffer_address_), s_record_line(s_record_line_)
      {
      }
      UnitNormal(std::string name_, u32 starting_address_, u32 size_, u32 file_offset_,
                 u32 bin_file_offset_, std::string bin_file_name_)
          : name(std::move(name_)), starting_address(starting_address_), size(size_),
            file_offset(file_offset_), bin_file_offset(bin_file_offset_),
            bin_file_name(std::move(bin_file_name_))
      {
      }
      UnitNormal(std::string name_, u32 starting_address_, u32 size_, u32 file_offset_,
                 int s_record_line_, u32 bin_file_offset_, std::string bin_file_name_)
          : name(std::move(name_)), starting_address(starting_address_), size(size_),
            file_offset(file_offset_), s_record_line(s_record_line_),
            bin_file_offset(bin_file_offset_), bin_file_name(std::move(bin_file_name_))
      {
      }
      UnitNormal(std::string name_, u32 starting_address_, u32 size_, u32 file_offset_,
                 u32 rom_address_, u32 ram_buffer_address_, u32 bin_file_offset_,
                 std::string bin_file_name_)
          : name(std::move(name_)), starting_address(starting_address_), size(size_),
            file_offset(file_offset_), rom_address(rom_address_),
            ram_buffer_address(ram_buffer_address_), bin_file_offset(bin_file_offset_),
            bin_file_name(std::move(bin_file_name_))
      {
      }
      UnitNormal(std::string name_, u32 starting_address_, u32 size_, u32 file_offset_,
                 u32 rom_address_, u32 ram_buffer_address_, int s_record_line_,
                 u32 bin_file_offset_, std::string bin_file_name_)
          : name(std::move(name_)), starting_address(starting_address_), size(size_),
            file_offset(file_offset_), rom_address(rom_address_),
            ram_buffer_address(ram_buffer_address_), s_record_line(s_record_line_),
            bin_file_offset(bin_file_offset_), bin_file_name(std::move(bin_file_name_))
      {
      }

      void PrintSimple_old(std::ostream&) const;
      void PrintRomRam_old(std::ostream&) const;
      void PrintSimple(std::ostream&) const;
      void PrintRomRam(std::ostream&) const;
      void PrintSRecord(std::ostream&) const;
      void PrintBinFile(std::ostream&) const;
      void PrintRomRamSRecord(std::ostream&) const;
      void PrintRomRamBinFile(std::ostream&) const;
      void PrintSRecordBinFile(std::ostream&) const;
      void PrintRomRamSRecordBinFile(std::ostream&) const;

      std::string name;
      u32 starting_address;
      u32 size;
      u32 file_offset;
      u32 rom_address;
      u32 ram_buffer_address;
      int s_record_line;
      u32 bin_file_offset;
      std::string bin_file_name;
    };

    // TODO: There is an opportunity for detecting the min version from the normal and debug section
    // names, but I couldn't be bothered to look into it.
    struct UnitDebug
    {
      UnitDebug(std::string name_, u32 size_, u32 file_offset_)
          : name(std::move(name_)), size(size_), file_offset(file_offset_)
      {
      }

      void Print_older(std::ostream&) const;
      void Print_old(std::ostream&) const;
      void Print(std::ostream&) const;

      std::string name;
      u32 size;
      u32 file_offset;
    };

    MemoryMap(bool has_rom_ram_)  // ctor for old memory map
        : has_rom_ram(has_rom_ram_), has_s_record(false), has_bin_file(false)
    {
    }
    MemoryMap(bool has_rom_ram_, bool has_s_record_, bool has_bin_file_)
        : has_rom_ram(has_rom_ram_), has_s_record(has_s_record_), has_bin_file(has_bin_file_)
    {
      SetMinVersion(Version::version_4_2_build_142);
    }
    virtual ~MemoryMap() override = default;

    Error ScanSimple_old(const char*&, const char*, std::size_t&);
    Error ScanRomRam_old(const char*&, const char*, std::size_t&);
    Error ScanDebug_old(const char*&, const char*, std::size_t&);
    Error ScanSimple(const char*&, const char*, std::size_t&);
    Error ScanRomRam(const char*&, const char*, std::size_t&);
    Error ScanSRecord(const char*&, const char*, std::size_t&);
    Error ScanBinFile(const char*&, const char*, std::size_t&);
    Error ScanRomRamSRecord(const char*&, const char*, std::size_t&);
    Error ScanRomRamBinFile(const char*&, const char*, std::size_t&);
    Error ScanSRecordBinFile(const char*&, const char*, std::size_t&);
    Error ScanRomRamSRecordBinFile(const char*&, const char*, std::size_t&);
    Error ScanDebug(const char*&, const char*, std::size_t&);
    virtual void Print(std::ostream&) const override;
    void PrintSimple_old(std::ostream&) const;
    void PrintRomRam_old(std::ostream&) const;
    void PrintDebug_old(std::ostream&) const;
    void PrintSimple(std::ostream&) const;
    void PrintRomRam(std::ostream&) const;
    void PrintSRecord(std::ostream&) const;
    void PrintBinFile(std::ostream&) const;
    void PrintRomRamSRecord(std::ostream&) const;
    void PrintRomRamBinFile(std::ostream&) const;
    void PrintSRecordBinFile(std::ostream&) const;
    void PrintRomRamSRecordBinFile(std::ostream&) const;
    void PrintDebug(std::ostream&) const;
    virtual bool IsEmpty() const noexcept override
    {
      return normal_units.empty() || debug_units.empty();
    }

    std::list<UnitNormal> normal_units;
    std::list<UnitDebug> debug_units;
    bool has_rom_ram;   // Enabled by '-romaddr addr' and '-rambuffer addr' options
    bool has_s_record;  // Enabled by '-srec [filename]' option
    bool has_bin_file;  // Enabled by '-genbinary keyword' option
  };

  struct LinkerGeneratedSymbols final : PortionBase
  {
    struct Unit
    {
      Unit(std::string name_, u32 value_) : name(std::move(name_)), value(value_) {}

      void Print(std::ostream&) const;

      std::string name;
      u32 value;
    };

    LinkerGeneratedSymbols() = default;
    virtual ~LinkerGeneratedSymbols() override = default;

    Error Scan(const char*&, const char*, std::size_t&);
    virtual void Print(std::ostream&) const override;
    void Export(DebugInfo&) const noexcept;
    virtual bool IsEmpty() const noexcept override { return units.empty(); }

    std::list<Unit> units;
  };

  Error Scan(std::string_view, std::size_t&);
  Error Scan(const char*, const char*, std::size_t&);
  Error ScanTLOZTP(std::string_view, std::size_t&);
  Error ScanTLOZTP(const char*, const char*, std::size_t&);
  Error ScanSMGalaxy(std::string_view, std::size_t&);
  Error ScanSMGalaxy(const char*, const char*, std::size_t&);

  Error ScanPrologue_SectionLayout(const char*&, const char* const, std::size_t&, std::string);
  Error ScanPrologue_MemoryMap(const char*&, const char*, std::size_t&);
  Error ScanForGarbage(const char*, const char*);

  void Print(std::ostream&) const;
  void Export(DebugInfo&) const noexcept;
  Version GetMinVersion() const noexcept;

  std::string entry_point_name;
  std::unique_ptr<SymbolClosure> normal_symbol_closure;
  std::unique_ptr<EPPC_PatternMatching> eppc_pattern_matching;
  std::unique_ptr<SymbolClosure> dwarf_symbol_closure;
  std::list<std::pair<std::size_t, std::string>> unresolved_symbols;
  std::unique_ptr<LinkerOpts> linker_opts;
  std::unique_ptr<MixedModeIslands> mixed_mode_islands;
  std::unique_ptr<BranchIslands> branch_islands;
  std::unique_ptr<LinktimeSizeDecreasingOptimizations> linktime_size_decreasing_optimizations;
  std::unique_ptr<LinktimeSizeIncreasingOptimizations> linktime_size_increasing_optimizations;
  std::list<std::unique_ptr<SectionLayout>> section_layouts;
  std::unique_ptr<MemoryMap> memory_map;
  std::unique_ptr<LinkerGeneratedSymbols> linker_generated_symbols;

  struct UnitDebugInfo
  {
    const SymbolClosure::NodeReal* symbol_closure_unit = nullptr;
    const EPPC_PatternMatching::MergingUnit* eppc_pattern_matching_merging_unit = nullptr;
    const EPPC_PatternMatching::FoldingUnit::Unit* eppc_pattern_matching_folding_unit_unit =
        nullptr;
    const SectionLayout::Unit* section_layout_unit = nullptr;
    const LinkerGeneratedSymbols::Unit* linker_generated_symbol_unit = nullptr;
  };
  DebugInfo debug_info;
};
}  // namespace MWLinker
