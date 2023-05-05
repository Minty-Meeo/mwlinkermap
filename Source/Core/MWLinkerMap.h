// Copyright 2023 Bradley G. (Minty Meeo)
// SPDX-License-Identifier: MIT

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <list>
#include <memory>
#include <ostream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace MWLinker
{
using Elf32_Word = std::uint32_t;
using Elf32_Addr = std::uint32_t;

enum class Version
{
  // Oldest known version
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
  // Latest known version
  Latest,
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

    EntryPointNameMissing,
    SMGalaxyYouHadOneJob,

    SymbolClosureHierarchySkip,
    SymbolClosureInvalidHierarchy,
    SymbolClosureInvalidSymbolType,
    SymbolClosureInvalidSymbolBind,
    SymbolClosureUnrefDupsHierarchyMismatch,
    SymbolClosureUnrefDupsNameMismatch,
    SymbolClosureUnrefDupsEmpty,

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

  using UnresolvedSymbols = std::list<std::pair<std::size_t, std::string>>;

  struct PortionBase
  {
    friend Map;

    virtual ~PortionBase() = default;
    constexpr Version GetMinVersion() const noexcept { return m_min_version; }
    constexpr Version GetMaxVersion() const noexcept { return m_max_version; }
    constexpr static Version GetMinVersion(PortionBase* portion)
    {
      return portion ? portion->GetMinVersion() : Version::Unknown;
    }
    constexpr static Version GetMaxVersion(PortionBase* portion)
    {
      return portion ? portion->GetMaxVersion() : Version::Latest;
    }
    virtual bool IsEmpty() const noexcept = 0;

  private:
    constexpr void SetVersionRange(const Version min_version, const Version max_version) noexcept
    {
      m_min_version = std::max(m_min_version, min_version);
      m_max_version = std::min(m_max_version, max_version);
    }

    Version m_min_version = Version::Unknown;
    Version m_max_version = Version::Latest;
  };

  struct SymbolClosure final : PortionBase
  {
    friend Map;

    // CodeWarrior for GCN 1.1
    //  - Added UNREFERENCED DUPLICATE info.
    // CodeWarrior for GCN 2.7
    //  - Symbol closure became optional with '-[no]listclosure', off by default.
    //  - Changed behavior of the source name when linking static libs
    //  - Added _ctors$99 and _dtors$99, among other things.

    struct NodeBase
    {
      friend SymbolClosure;

      explicit NodeBase() : m_parent(nullptr) {}  // Necessary for root node
      explicit NodeBase(NodeBase* parent_) : m_parent(parent_) {}
      virtual ~NodeBase() = default;

      const NodeBase* GetParent() { return m_parent; }
      const std::list<std::unique_ptr<NodeBase>>& GetChildren() { return m_children; }
      static constexpr std::string_view ToName(Type) noexcept;
      static constexpr std::string_view ToName(Bind) noexcept;

    private:
      virtual void Print(std::ostream&, int, UnresolvedSymbols::const_iterator&,
                         UnresolvedSymbols::const_iterator, std::size_t&) const;
      static void PrintPrefix(std::ostream&, int);

      NodeBase* m_parent;
      std::list<std::unique_ptr<NodeBase>> m_children;
    };

    struct NodeReal final : NodeBase
    {
      struct UnreferencedDuplicate
      {
        friend NodeReal;

        explicit UnreferencedDuplicate(Type type_, Bind bind_, std::string_view module_name_,
                                       std::string_view source_name_)
            : m_type(type_), m_bind(bind_), m_module_name(module_name_), m_source_name(source_name_)
        {
        }

        const Type m_type;
        const Bind m_bind;
        const std::string m_module_name;
        const std::string m_source_name;

      private:
        void Print(std::ostream&, int, std::size_t&) const;
      };

      explicit NodeReal(NodeBase* parent_, std::string_view name_, Type type_, Bind bind_,
                        std::string_view module_name_, std::string_view source_name_,
                        std::list<UnreferencedDuplicate> unref_dups_)
          : NodeBase(parent_), m_name(name_), m_type(type_), m_bind(bind_),
            m_module_name(module_name_), m_source_name(source_name_),
            m_unref_dups(std::move(unref_dups_))
      {
      }
      virtual ~NodeReal() override = default;

      const std::string m_name;
      const Type m_type;
      const Bind m_bind;
      // Static library or object name
      const std::string m_module_name;
      // When linking a static library, this is either:
      // A) The name of the STT_FILE symbol from the relevant object in the static library.
      // B) The name of the relevant object in the static library (as early as CW for GCN 2.7).
      const std::string m_source_name;
      const std::list<UnreferencedDuplicate> m_unref_dups;

    private:
      virtual void Print(std::ostream&, int, UnresolvedSymbols::const_iterator&,
                         UnresolvedSymbols::const_iterator, std::size_t&) const override;
    };

    struct NodeLinkerGenerated final : NodeBase
    {
      explicit NodeLinkerGenerated(NodeBase* parent_, std::string_view name_)
          : NodeBase(parent_), m_name(name_)
      {
      }
      virtual ~NodeLinkerGenerated() override = default;

      const std::string m_name;

    private:
      virtual void Print(std::ostream&, int, UnresolvedSymbols::const_iterator&,
                         UnresolvedSymbols::const_iterator, std::size_t&) const override;
    };

    using NodeLookup = std::unordered_multimap<std::string_view, const NodeReal&>;
    using ModuleLookup = std::unordered_map<std::string_view, NodeLookup>;

    explicit SymbolClosure() = default;
    virtual ~SymbolClosure() override = default;

    virtual bool IsEmpty() const noexcept override { return m_root.m_children.empty(); }
    const ModuleLookup& GetModuleLookup() { return m_lookup; }

    struct Warn
    {
      friend SymbolClosure;

      static void DisableAll() noexcept
      {
        do_warn_odr_violation = false;
        do_warn_sym_on_flag_detected = false;
      }

      static bool do_warn_odr_violation;
      static bool do_warn_sym_on_flag_detected;

    private:
      static void OneDefinitionRuleViolation(std::size_t, std::string_view, std::string_view);
      static void SymOnFlagDetected(std::size_t, std::string_view);
    };

  private:
    Error Scan(const char*&, const char*, std::size_t&, UnresolvedSymbols&);
    void Print(std::ostream&, UnresolvedSymbols::const_iterator&, UnresolvedSymbols::const_iterator,
               std::size_t&) const;

    NodeBase m_root;
    ModuleLookup m_lookup;
  };

  struct EPPC_PatternMatching final : PortionBase
  {
    friend Map;

    // CodeWarrior for Wii 1.0
    //  - Added EPPC_PatternMatching

    struct MergingUnit
    {
      friend EPPC_PatternMatching;

      explicit MergingUnit(std::string_view first_name_, std::string_view second_name_,
                           Elf32_Word size_, bool will_be_replaced_, bool was_interchanged_)
          : m_first_name(first_name_), m_second_name(second_name_), m_size(size_),
            m_will_be_replaced(will_be_replaced_), m_was_interchanged(was_interchanged_)
      {
      }

      const std::string m_first_name;
      const std::string m_second_name;
      const Elf32_Word m_size;
      // If the conditions are right (e.g. the function is more than just a BLR instruction), then
      // one function is replaced with a branch to the other function, saving space at the cost of a
      // tiny amount of overhead. This is by far the more common code merging technique.
      const bool m_will_be_replaced;
      // Rarely, a function can be marked for removal when a duplicate of it is elsewhere in the
      // binary. All references to it are then redirected to the duplicate. Even rarer than that,
      // sometimes the linker can change its mind and replace it with a branch instead.
      const bool m_was_interchanged;

    private:
      void Print(std::ostream&, std::size_t&) const;
    };

    struct FoldingUnit
    {
      friend EPPC_PatternMatching;

      struct Unit
      {
        friend FoldingUnit;

        explicit Unit(std::string_view first_name_, std::string_view second_name_, Elf32_Word size_,
                      bool new_branch_function_)
            : m_first_name(first_name_), m_second_name(second_name_), m_size(size_),
              m_new_branch_function(new_branch_function_)
        {
        }

        const std::string m_first_name;
        const std::string m_second_name;
        const Elf32_Word m_size;
        const bool m_new_branch_function;

      private:
        void Print(std::ostream&, std::size_t&) const;
      };

      using UnitLookup = std::unordered_multimap<std::string_view, const Unit&>;
      using ModuleLookup = std::unordered_map<std::string_view, UnitLookup>;

      explicit FoldingUnit(std::string_view object_name_) : m_object_name(object_name_) {}

      const std::list<Unit>& GetUnits() { return m_units; }

      const std::string m_object_name;

    private:
      void Print(std::ostream&, std::size_t&) const;

      std::list<Unit> m_units;
    };

    using MergingUnitLookup = std::unordered_multimap<std::string_view, const MergingUnit&>;

    explicit EPPC_PatternMatching()
    {
      SetVersionRange(Version::version_4_2_build_142, Version::Latest);
    }
    virtual ~EPPC_PatternMatching() override = default;

    virtual bool IsEmpty() const noexcept override
    {
      return m_merging_units.empty() || m_folding_units.empty();
    }
    const std::list<MergingUnit>& GetMergingUnits() { return m_merging_units; }
    const std::list<FoldingUnit>& GetFoldingUnits() { return m_folding_units; }
    const MergingUnitLookup& GetMergingLookup() { return m_merging_lookup; }
    const FoldingUnit::ModuleLookup& GetFoldingModuleLookup() { return m_folding_lookup; }

    struct Warn
    {
      friend EPPC_PatternMatching;

      static void DisableAll() noexcept
      {
        do_warn_merging_odr_violation = false;
        do_warn_folding_repeat_object = false;
        do_warn_folding_odr_violation = false;
      }

      static bool do_warn_merging_odr_violation;
      static bool do_warn_folding_repeat_object;
      static bool do_warn_folding_odr_violation;

    private:
      static void MergingOneDefinitionRuleViolation(std::size_t, std::string_view);
      static void FoldingRepeatObject(std::size_t, std::string_view);
      static void FoldingOneDefinitionRuleViolation(std::size_t, std::string_view,
                                                    std::string_view);
    };

  private:
    Error Scan(const char*&, const char*, std::size_t&);
    void Print(std::ostream&, std::size_t&) const;

    std::list<MergingUnit> m_merging_units;
    std::list<FoldingUnit> m_folding_units;
    MergingUnitLookup m_merging_lookup;
    FoldingUnit::ModuleLookup m_folding_lookup;
  };

  struct LinkerOpts final : PortionBase
  {
    friend Map;

    // CodeWarrior for Wii 1.0
    //  - Added LinkerOpts

    struct Unit
    {
      friend LinkerOpts;

      enum class Kind
      {
        NotNear,
        NotComputed,
        Optimized,
        DisassembleError,
      };

      explicit Unit(std::string_view module_name_, std::string_view name_)
          : m_unit_kind(Kind::DisassembleError), m_module_name(module_name_), m_name(name_)
      {
      }
      explicit Unit(const Kind unit_kind_, std::string_view module_name_, std::string_view name_,
                    std::string_view reference_name_)
          : m_unit_kind(unit_kind_), m_module_name(module_name_), m_name(name_),
            m_reference_name(reference_name_)
      {
      }

      const Kind m_unit_kind;
      const std::string m_module_name;
      const std::string m_name;
      const std::string m_reference_name;

    private:
      void Print(std::ostream&, std::size_t&) const;
    };

    explicit LinkerOpts() { SetVersionRange(Version::version_4_2_build_142, Version::Latest); }
    virtual ~LinkerOpts() override = default;

    virtual bool IsEmpty() const noexcept override { return m_units.empty(); }
    const std::list<Unit>& GetUnits() { return m_units; }

  private:
    Error Scan(const char*&, const char*, std::size_t&);
    void Print(std::ostream&, std::size_t&) const;

    std::list<Unit> m_units;
  };

  struct BranchIslands final : PortionBase
  {
    friend Map;

    // CodeWarror for GCN 3.0a3 (at the earliest)
    //  - Added Branch Islands.

    struct Unit
    {
      friend BranchIslands;

      explicit Unit(std::string_view first_name_, std::string_view second_name_, bool is_safe_)
          : m_first_name(first_name_), m_second_name(second_name_), m_is_safe(is_safe_)
      {
      }

      const std::string m_first_name;
      const std::string m_second_name;
      const bool m_is_safe;

    private:
      void Print(std::ostream&, std::size_t&) const;
    };

    explicit BranchIslands() { SetVersionRange(Version::version_4_1_build_51213, Version::Latest); }
    virtual ~BranchIslands() override = default;

    virtual bool IsEmpty() const noexcept override { return m_units.empty(); }
    const std::list<Unit>& GetUnits() { return m_units; }

  private:
    Error Scan(const char*&, const char*, std::size_t&);
    void Print(std::ostream&, std::size_t&) const;

    std::list<Unit> m_units;
  };

  struct MixedModeIslands final : PortionBase
  {
    friend Map;

    // CodeWarror for GCN 3.0a3 (at the earliest)
    //  - Added Mixed Mode Islands.

    struct Unit
    {
      friend MixedModeIslands;

      explicit Unit(std::string_view first_name_, std::string_view second_name_, bool is_safe_)
          : m_first_name(first_name_), m_second_name(second_name_), m_is_safe(is_safe_)
      {
      }

      const std::string m_first_name;
      const std::string m_second_name;
      const bool m_is_safe;

    private:
      void Print(std::ostream&, std::size_t&) const;
    };

    explicit MixedModeIslands()
    {
      SetVersionRange(Version::version_4_1_build_51213, Version::Latest);
    }
    virtual ~MixedModeIslands() override = default;

    virtual bool IsEmpty() const noexcept override { return m_units.empty(); }
    const std::list<Unit>& GetUnits() { return m_units; }

  private:
    Error Scan(const char*&, const char*, std::size_t&);
    void Print(std::ostream&, std::size_t&) const;

    std::list<Unit> m_units;
  };

  struct LinktimeSizeDecreasingOptimizations final : PortionBase
  {
    friend Map;

    explicit LinktimeSizeDecreasingOptimizations() = default;
    virtual ~LinktimeSizeDecreasingOptimizations() override = default;

    virtual bool IsEmpty() const noexcept override { return true; }

  private:
    Error Scan(const char*&, const char*, std::size_t&);
    void Print(std::ostream&, std::size_t&) const;
  };

  struct LinktimeSizeIncreasingOptimizations final : PortionBase
  {
    friend Map;

    explicit LinktimeSizeIncreasingOptimizations() = default;
    virtual ~LinktimeSizeIncreasingOptimizations() override = default;

    virtual bool IsEmpty() const noexcept override { return true; }

  private:
    Error Scan(const char*&, const char*, std::size_t&);
    void Print(std::ostream&, std::size_t&) const;
  };

  struct SectionLayout final : PortionBase
  {
    friend Map;

    // CodeWarrior for GCN 2.7
    //  - Changed to four column info, added *fill* symbols.
    //  - Changed the behavior of the source name when linking static libs

    enum class Kind
    {
      Code,
      ZCode,
      VLECode,
      Data,
      BSS,
      Ctors,
      Dtors,
      ExTab,
      ExTabIndex,
      Debug,
      Mixed,  // ?

      Unknown,
    };

    struct Unit;

    using UnitLookup = std::unordered_multimap<std::string_view, const Unit&>;
    using ModuleLookup = std::unordered_map<std::string_view, UnitLookup>;

  private:
    struct ScanningContext
    {
      SectionLayout& m_section_layout;
      std::size_t& m_line_number;
      bool m_is_second_lap;  // BSS '-common on'
      bool m_is_after_eti_init_info;
      bool m_is_multi_stt_section;
      UnitLookup* m_curr_unit_lookup;
      std::string_view m_curr_module_name;
      std::string_view m_curr_source_name;
    };

  public:
    struct Unit
    {
      friend SectionLayout;

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
        // Lives in a code section
        Function,
        // Lives in a data section
        Object,
        // Assumed to be of notype
        NoType,
        // Named after the section they are native to. Multiple can appear in a single compilation
        // unit with the '-sym on' option. The size of a section symbol is the total of all symbols,
        // both used and unused, that one is meant to encompass.
        Section,
        // BSS local common symbols.
        LCommon,
        // BSS common symbols.
        // '-common on' moves these into a common section.
        Common,
        // Native to the extab section.
        ExTab,
        // Native to the extabindex section.
        ExTabIndex,
        // *fill*
        Fill1,
        // **fill**
        Fill2,
      };

      // UNUSED symbols
      explicit Unit(Elf32_Word size_, std::string_view name_, std::string_view module_name_,
                    std::string_view source_name_, ScanningContext& scanning_context)
          : m_unit_kind(Kind::Unused), m_starting_address{},
            m_size(size_), m_virtual_address{}, m_file_offset{}, m_alignment{}, m_name(name_),
            m_entry_parent(nullptr), m_module_name(module_name_), m_source_name(source_name_),
            m_unit_trait(DeduceUsualSubtext(scanning_context))
      {
      }
      // 3-column normal symbols
      explicit Unit(std::uint32_t starting_address_, Elf32_Word size_, Elf32_Addr virtual_address_,
                    int alignment_, std::string_view name_, std::string_view module_name_,
                    std::string_view source_name_, ScanningContext& scanning_context)
          : m_unit_kind(Kind::Normal), m_starting_address(starting_address_), m_size(size_),
            m_virtual_address(virtual_address_), m_file_offset{}, m_alignment(alignment_),
            m_name(name_), m_entry_parent(nullptr), m_module_name(module_name_),
            m_source_name(source_name_), m_unit_trait(DeduceUsualSubtext(scanning_context))
      {
      }
      // 4-column normal symbols
      explicit Unit(std::uint32_t starting_address_, Elf32_Word size_, Elf32_Addr virtual_address_,
                    std::uint32_t file_offset_, int alignment_, std::string_view name_,
                    std::string_view module_name_, std::string_view source_name_,
                    ScanningContext& scanning_context)
          : m_unit_kind(Kind::Normal), m_starting_address(starting_address_), m_size(size_),
            m_virtual_address(virtual_address_), m_file_offset(file_offset_),
            m_alignment(alignment_), m_name(name_), m_entry_parent(nullptr),
            m_module_name(module_name_), m_source_name(source_name_),
            m_unit_trait(DeduceUsualSubtext(scanning_context))
      {
      }
      // 3-column entry symbols
      explicit Unit(std::uint32_t starting_address_, Elf32_Word size_, Elf32_Addr virtual_address_,
                    std::string_view name_, const Unit* entry_parent_,
                    std::string_view module_name_, std::string_view source_name_,
                    ScanningContext& scanning_context)
          : m_unit_kind(Kind::Entry), m_starting_address(starting_address_), m_size(size_),
            m_virtual_address(virtual_address_), m_file_offset{}, m_alignment{}, m_name(name_),
            m_entry_parent(entry_parent_), m_module_name(module_name_), m_source_name(source_name_),
            m_unit_trait(DeduceEntrySubtext(scanning_context))
      {
      }
      // 4-column entry symbols
      explicit Unit(std::uint32_t starting_address_, Elf32_Word size_, Elf32_Addr virtual_address_,
                    std::uint32_t file_offset_, std::string_view name_, const Unit* entry_parent_,
                    std::string_view module_name_, std::string_view source_name_,
                    ScanningContext& scanning_context)
          : m_unit_kind(Kind::Entry), m_starting_address(starting_address_), m_size(size_),
            m_virtual_address(virtual_address_), m_file_offset(file_offset_), m_alignment{},
            m_name(name_), m_entry_parent(entry_parent_), m_module_name(module_name_),
            m_source_name(source_name_), m_unit_trait(DeduceEntrySubtext(scanning_context))
      {
      }
      // 4-column special symbols
      explicit Unit(std::uint32_t starting_address_, Elf32_Word size_, Elf32_Addr virtual_address_,
                    std::uint32_t file_offset_, int alignment_, Trait unit_trait_)
          : m_unit_kind(Kind::Special), m_starting_address(starting_address_), m_size(size_),
            m_virtual_address(virtual_address_), m_file_offset(file_offset_),
            m_alignment(alignment_), m_entry_parent(nullptr), m_unit_trait(unit_trait_)
      {
      }

      const Unit* GetEntryParent() { return m_entry_parent; }
      const std::list<const Unit*>& GetEntryChildren() { return m_entry_children; }
      static constexpr std::string_view ToSpecialName(Trait);

      const Kind m_unit_kind;
      const std::uint32_t m_starting_address;
      const Elf32_Word m_size;
      const Elf32_Addr m_virtual_address;
      const std::uint32_t m_file_offset;
      const int m_alignment;
      const std::string m_name;

    private:
      // Doubly-linked relationship between entry symbols and their host.
      const Unit* const m_entry_parent;
      // Doubly-linked relationship between entry symbols and their host.
      std::list<const Unit*> m_entry_children;

    public:
      // Static library or object name
      const std::string m_module_name;
      // When linking a static library, this is either:
      // A) The name of the STT_FILE symbol from the relevant object in the static library.
      // B) The name of the relevant object in the static library (as early as CW for GCN 2.7).
      const std::string m_source_name;
      const Trait m_unit_trait;

    private:
      void Print3Column(std::ostream&, std::size_t&) const;
      void Print4Column(std::ostream&, std::size_t&) const;
      Unit::Trait DeduceUsualSubtext(ScanningContext&);
      Unit::Trait DeduceEntrySubtext(ScanningContext&);
    };

    explicit SectionLayout(Kind section_kind_, std::string_view name_)
        : m_section_kind(section_kind_), m_name(name_)
    {
    }
    virtual ~SectionLayout() override = default;

    virtual bool IsEmpty() const noexcept override { return m_units.empty(); }
    const std::list<Unit>& GetUnits() const noexcept { return m_units; }
    const ModuleLookup& GetModuleLookup() const noexcept { return m_lookup; }
    static Kind ToSectionKind(std::string_view);

    const Kind m_section_kind;
    const std::string m_name;

    struct Warn
    {
      friend SectionLayout;

      static void DisableAll() noexcept
      {
        do_warn_repeat_compilation_unit = false;
        do_warn_odr_violation = false;
        do_warn_sym_on_flag_detected = false;
        do_warn_common_on_flag_detected = false;
        do_warn_lcomm_after_comm = false;
      }

      static bool do_warn_repeat_compilation_unit;
      static bool do_warn_odr_violation;
      static bool do_warn_sym_on_flag_detected;
      static bool do_warn_common_on_flag_detected;
      static bool do_warn_lcomm_after_comm;

    private:
      static void RepeatCompilationUnit(std::size_t, std::string_view, std::string_view);
      static void OneDefinitionRuleViolation(std::size_t, std::string_view, std::string_view,
                                             std::string_view);
      static void SymOnFlagDetected(std::size_t, std::string_view, std::string_view);
      static void CommonOnFlagDetected(std::size_t, std::string_view, std::string_view);
      static void LCommAfterComm(std::size_t);
    };

  private:
    Error Scan3Column(const char*&, const char*, std::size_t&);
    Error Scan4Column(const char*&, const char*, std::size_t&);
    Error ScanTLOZTP(const char*&, const char*, std::size_t&);
    void Print(std::ostream&, std::size_t&) const;

    std::list<Unit> m_units;
    ModuleLookup m_lookup;
  };

  struct MemoryMap final : PortionBase
  {
    friend Map;

    // CodeWarrior for GCN 2.7
    //  - Changed size column for debug sections from "%06x" to "%08x".
    // CodeWarrior for Wii 1.0
    //  - Expanded Memory Map variants, slightly tweaked existing printfs.
    // TODO: There is an opportunity for detecting the min version from the normal and debug section
    // names, but I couldn't be bothered to look into it.

    struct UnitNormal
    {
      friend MemoryMap;

      explicit UnitNormal(std::string_view name_, Elf32_Addr starting_address_, Elf32_Word size_,
                          std::uint32_t file_offset_)
          : m_name(name_), m_starting_address(starting_address_), m_size(size_),
            m_file_offset(file_offset_), m_rom_address{}, m_ram_buffer_address{}, m_srecord_line{},
            m_bin_file_offset{}
      {
      }
      explicit UnitNormal(std::string_view name_, Elf32_Addr starting_address_, Elf32_Word size_,
                          std::uint32_t file_offset_, int s_record_line_)
          : m_name(name_), m_starting_address(starting_address_), m_size(size_),
            m_file_offset(file_offset_), m_rom_address{}, m_ram_buffer_address{},
            m_srecord_line(s_record_line_), m_bin_file_offset{}
      {
      }
      explicit UnitNormal(std::string_view name_, Elf32_Addr starting_address_, Elf32_Word size_,
                          std::uint32_t file_offset_, std::uint32_t rom_address_,
                          std::uint32_t ram_buffer_address_)
          : m_name(name_), m_starting_address(starting_address_), m_size(size_),
            m_file_offset(file_offset_), m_rom_address(rom_address_),
            m_ram_buffer_address(ram_buffer_address_), m_srecord_line{}, m_bin_file_offset{}
      {
      }
      explicit UnitNormal(std::string_view name_, Elf32_Addr starting_address_, Elf32_Word size_,
                          std::uint32_t file_offset_, std::uint32_t rom_address_,
                          std::uint32_t ram_buffer_address_, int s_record_line_)
          : m_name(name_), m_starting_address(starting_address_), m_size(size_),
            m_file_offset(file_offset_), m_rom_address(rom_address_),
            m_ram_buffer_address(ram_buffer_address_),
            m_srecord_line(s_record_line_), m_bin_file_offset{}
      {
      }
      explicit UnitNormal(std::string_view name_, Elf32_Addr starting_address_, Elf32_Word size_,
                          std::uint32_t file_offset_, std::uint32_t bin_file_offset_,
                          std::string_view bin_file_name_)
          : m_name(name_), m_starting_address(starting_address_), m_size(size_),
            m_file_offset(file_offset_), m_rom_address{}, m_ram_buffer_address{}, m_srecord_line{},
            m_bin_file_offset(bin_file_offset_), m_bin_file_name(bin_file_name_)
      {
      }
      explicit UnitNormal(std::string_view name_, Elf32_Addr starting_address_, Elf32_Word size_,
                          std::uint32_t file_offset_, int s_record_line_,
                          std::uint32_t bin_file_offset_, std::string_view bin_file_name_)
          : m_name(name_), m_starting_address(starting_address_), m_size(size_),
            m_file_offset(file_offset_), m_rom_address{}, m_ram_buffer_address{},
            m_srecord_line(s_record_line_), m_bin_file_offset(bin_file_offset_),
            m_bin_file_name(bin_file_name_)
      {
      }
      explicit UnitNormal(std::string_view name_, Elf32_Addr starting_address_, Elf32_Word size_,
                          std::uint32_t file_offset_, std::uint32_t rom_address_,
                          std::uint32_t ram_buffer_address_, std::uint32_t bin_file_offset_,
                          std::string_view bin_file_name_)
          : m_name(name_), m_starting_address(starting_address_), m_size(size_),
            m_file_offset(file_offset_), m_rom_address(rom_address_),
            m_ram_buffer_address(ram_buffer_address_), m_srecord_line{},
            m_bin_file_offset(bin_file_offset_), m_bin_file_name(bin_file_name_)
      {
      }
      explicit UnitNormal(std::string_view name_, Elf32_Addr starting_address_, Elf32_Word size_,
                          std::uint32_t file_offset_, std::uint32_t rom_address_,
                          std::uint32_t ram_buffer_address_, int s_record_line_,
                          std::uint32_t bin_file_offset_, std::string_view bin_file_name_)
          : m_name(name_), m_starting_address(starting_address_), m_size(size_),
            m_file_offset(file_offset_), m_rom_address(rom_address_),
            m_ram_buffer_address(ram_buffer_address_), m_srecord_line(s_record_line_),
            m_bin_file_offset(bin_file_offset_), m_bin_file_name(bin_file_name_)
      {
      }

      const std::string m_name;
      const Elf32_Addr m_starting_address;
      const Elf32_Word m_size;
      const std::uint32_t m_file_offset;
      const std::uint32_t m_rom_address;
      const std::uint32_t m_ram_buffer_address;
      const int m_srecord_line;
      const std::uint32_t m_bin_file_offset;
      const std::string m_bin_file_name;

    private:
      void PrintSimple_old(std::ostream&, std::size_t&) const;
      void PrintRomRam_old(std::ostream&, std::size_t&) const;
      void PrintSimple(std::ostream&, std::size_t&) const;
      void PrintRomRam(std::ostream&, std::size_t&) const;
      void PrintSRecord(std::ostream&, std::size_t&) const;
      void PrintBinFile(std::ostream&, std::size_t&) const;
      void PrintRomRamSRecord(std::ostream&, std::size_t&) const;
      void PrintRomRamBinFile(std::ostream&, std::size_t&) const;
      void PrintSRecordBinFile(std::ostream&, std::size_t&) const;
      void PrintRomRamSRecordBinFile(std::ostream&, std::size_t&) const;
    };

    struct UnitDebug
    {
      friend MemoryMap;

      explicit UnitDebug(std::string_view name_, Elf32_Word size_, std::uint32_t file_offset_)
          : m_name(name_), m_size(size_), m_file_offset(file_offset_)
      {
      }

      const std::string m_name;
      const Elf32_Word m_size;
      const std::uint32_t m_file_offset;

    private:
      void Print_older(std::ostream&, std::size_t&) const;
      void Print_old(std::ostream&, std::size_t&) const;
      void Print(std::ostream&, std::size_t&) const;
    };

    explicit MemoryMap(bool has_rom_ram_)  // ctor for old memory map
        : m_has_rom_ram(has_rom_ram_), m_has_s_record(false), m_has_bin_file(false)
    {
      SetVersionRange(Version::Unknown, Version::version_4_2_build_60320);
    }
    explicit MemoryMap(bool has_rom_ram_, bool has_s_record_, bool has_bin_file_)
        : m_has_rom_ram(has_rom_ram_), m_has_s_record(has_s_record_), m_has_bin_file(has_bin_file_)
    {
      SetVersionRange(Version::version_4_2_build_142, Version::Latest);
    }
    virtual ~MemoryMap() override = default;

    virtual bool IsEmpty() const noexcept override
    {
      return m_normal_units.empty() || m_debug_units.empty();
    }
    const std::list<UnitNormal>& GetNormalUnits() const noexcept { return m_normal_units; }
    const std::list<UnitDebug>& GetDebugUnits() const noexcept { return m_debug_units; }

    const bool m_has_rom_ram;   // Enabled by '-romaddr addr' and '-rambuffer addr' options
    const bool m_has_s_record;  // Enabled by '-srec [filename]' option
    const bool m_has_bin_file;  // Enabled by '-genbinary keyword' option

  private:
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
    void Print(std::ostream&, std::size_t&) const;
    void PrintSimple_old(std::ostream&, std::size_t&) const;
    void PrintRomRam_old(std::ostream&, std::size_t&) const;
    void PrintDebug_old(std::ostream&, std::size_t&) const;
    void PrintSimple(std::ostream&, std::size_t&) const;
    void PrintRomRam(std::ostream&, std::size_t&) const;
    void PrintSRecord(std::ostream&, std::size_t&) const;
    void PrintBinFile(std::ostream&, std::size_t&) const;
    void PrintRomRamSRecord(std::ostream&, std::size_t&) const;
    void PrintRomRamBinFile(std::ostream&, std::size_t&) const;
    void PrintSRecordBinFile(std::ostream&, std::size_t&) const;
    void PrintRomRamSRecordBinFile(std::ostream&, std::size_t&) const;
    void PrintDebug(std::ostream&, std::size_t&) const;

    std::list<UnitNormal> m_normal_units;
    std::list<UnitDebug> m_debug_units;
  };

  struct LinkerGeneratedSymbols final : PortionBase
  {
    friend Map;

    struct Unit
    {
      friend LinkerGeneratedSymbols;

      explicit Unit(std::string_view name_, Elf32_Addr value_) : m_name(name_), m_value(value_) {}

      const std::string m_name;
      const Elf32_Addr m_value;

    private:
      void Print(std::ostream&, std::size_t&) const;
    };

    explicit LinkerGeneratedSymbols() = default;
    virtual ~LinkerGeneratedSymbols() override = default;

    virtual bool IsEmpty() const noexcept override { return m_units.empty(); }
    const std::list<Unit>& GetUnits() const noexcept { return m_units; }

  private:
    Error Scan(const char*&, const char*, std::size_t&);
    void Print(std::ostream&, std::size_t&) const;

    std::list<Unit> m_units;
  };

  Error Scan(std::string_view, std::size_t&);
  Error Scan(const char*, const char*, std::size_t&);
  Error ScanTLOZTP(std::string_view, std::size_t&);
  Error ScanTLOZTP(const char*, const char*, std::size_t&);
  Error ScanSMGalaxy(std::string_view, std::size_t&);
  Error ScanSMGalaxy(const char*, const char*, std::size_t&);
  void Print(std::ostream&, std::size_t&) const;
  Version GetMinVersion() const noexcept
  {
    Version min_version = std::max({
        PortionBase::GetMinVersion(m_normal_symbol_closure.get()),
        PortionBase::GetMinVersion(m_eppc_pattern_matching.get()),
        PortionBase::GetMinVersion(m_dwarf_symbol_closure.get()),
        PortionBase::GetMinVersion(m_linker_opts.get()),
        PortionBase::GetMinVersion(m_mixed_mode_islands.get()),
        PortionBase::GetMinVersion(m_branch_islands.get()),
        PortionBase::GetMinVersion(m_linktime_size_decreasing_optimizations.get()),
        PortionBase::GetMinVersion(m_linktime_size_increasing_optimizations.get()),
        PortionBase::GetMinVersion(m_memory_map.get()),
        PortionBase::GetMinVersion(m_linker_generated_symbols.get()),
    });
    for (const auto& section_layout : m_section_layouts)
      min_version = std::max(PortionBase::GetMinVersion(section_layout.get()), min_version);
    return min_version;
  }
  Version GetMaxVersion() const noexcept
  {
    Version max_version = std::min({
        PortionBase::GetMaxVersion(m_normal_symbol_closure.get()),
        PortionBase::GetMaxVersion(m_eppc_pattern_matching.get()),
        PortionBase::GetMaxVersion(m_dwarf_symbol_closure.get()),
        PortionBase::GetMaxVersion(m_linker_opts.get()),
        PortionBase::GetMaxVersion(m_mixed_mode_islands.get()),
        PortionBase::GetMaxVersion(m_branch_islands.get()),
        PortionBase::GetMaxVersion(m_linktime_size_decreasing_optimizations.get()),
        PortionBase::GetMaxVersion(m_linktime_size_increasing_optimizations.get()),
        PortionBase::GetMaxVersion(m_memory_map.get()),
        PortionBase::GetMaxVersion(m_linker_generated_symbols.get()),
    });
    for (const auto& section_layout : m_section_layouts)
      max_version = std::min(PortionBase::GetMaxVersion(section_layout.get()), max_version);
    return max_version;
  }

  const std::string& GetEntryPointName() const noexcept { return m_entry_point_name; }
  const std::unique_ptr<SymbolClosure>& GetNormalSymbolClosure() const noexcept
  {
    return m_normal_symbol_closure;
  }
  const std::unique_ptr<EPPC_PatternMatching>& GetEPPC_PatternMatching() const noexcept
  {
    return m_eppc_pattern_matching;
  }
  const std::unique_ptr<SymbolClosure>& GetDwarfSymbolClosure() const noexcept
  {
    return m_dwarf_symbol_closure;
  }
  const UnresolvedSymbols& GetUnresolvedSymbols() const noexcept { return m_unresolved_symbols; }
  const std::list<std::unique_ptr<SectionLayout>>& GetSectionLayouts() const noexcept
  {
    return m_section_layouts;
  }
  const std::unique_ptr<MemoryMap>& GetMemoryMap() const noexcept  //
  {
    return m_memory_map;
  }
  const std::unique_ptr<LinkerGeneratedSymbols>& GetLinkerGeneratedSymbols() const noexcept
  {
    return m_linker_generated_symbols;
  }

  struct Warn
  {
    friend Map;

    static void DisableAll() noexcept
    {
      SymbolClosure::Warn::DisableAll();
      EPPC_PatternMatching::Warn::DisableAll();
      SectionLayout::Warn::DisableAll();
    }
  };

private:
  Error ScanPrologue_SectionLayout(const char*&, const char* const, std::size_t&, std::string_view);
  Error ScanPrologue_MemoryMap(const char*&, const char*, std::size_t&);
  Error ScanForGarbage(const char*, const char*);
  static void PrintUnresolvedSymbols(std::ostream&, UnresolvedSymbols::const_iterator&,
                                     UnresolvedSymbols::const_iterator, std::size_t&);

  std::string m_entry_point_name;
  std::unique_ptr<SymbolClosure> m_normal_symbol_closure;
  std::unique_ptr<EPPC_PatternMatching> m_eppc_pattern_matching;
  std::unique_ptr<SymbolClosure> m_dwarf_symbol_closure;
  UnresolvedSymbols m_unresolved_symbols;
  std::unique_ptr<LinkerOpts> m_linker_opts;
  std::unique_ptr<MixedModeIslands> m_mixed_mode_islands;
  std::unique_ptr<BranchIslands> m_branch_islands;
  std::unique_ptr<LinktimeSizeDecreasingOptimizations> m_linktime_size_decreasing_optimizations;
  std::unique_ptr<LinktimeSizeIncreasingOptimizations> m_linktime_size_increasing_optimizations;
  std::list<std::unique_ptr<SectionLayout>> m_section_layouts;
  std::unique_ptr<MemoryMap> m_memory_map;
  std::unique_ptr<LinkerGeneratedSymbols> m_linker_generated_symbols;
};
}  // namespace MWLinker
