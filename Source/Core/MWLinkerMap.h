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
    virtual ~PortionBase() = default;

    void SetMinVersion(const Version version) noexcept
    {
      min_version = std::max(min_version, version);
    }
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
      friend SymbolClosure;

      explicit NodeBase() : parent(nullptr) {}  // Necessary for root node
      explicit NodeBase(NodeBase* parent_) : parent(parent_) {}
      virtual ~NodeBase() = default;

      // Necessary for root and fake _dtor$99 child
      virtual void Print(std::ostream&, int, UnresolvedSymbols::const_iterator&,
                         UnresolvedSymbols::const_iterator, std::size_t&) const;
      static void PrintPrefix(std::ostream&, int);
      static constexpr std::string_view ToName(Type) noexcept;
      static constexpr std::string_view ToName(Bind) noexcept;

      const NodeBase* GetParent() { return parent; }
      const std::list<std::unique_ptr<NodeBase>>& GetChildren() { return children; }

    private:
      NodeBase* parent;
      std::list<std::unique_ptr<NodeBase>> children;
    };

    struct NodeReal final : NodeBase
    {
      struct UnreferencedDuplicate
      {
        explicit UnreferencedDuplicate(Type type_, Bind bind_, std::string_view module_name_,
                                       std::string_view source_name_)
            : type(type_), bind(bind_), module_name(module_name_), source_name(source_name_)
        {
        }

        void Print(std::ostream&, int, std::size_t&) const;

        const Type type;
        const Bind bind;
        const std::string module_name;
        const std::string source_name;
      };

      explicit NodeReal(NodeBase* parent_, std::string_view name_, Type type_, Bind bind_,
                        std::string_view module_name_, std::string_view source_name_,
                        std::list<UnreferencedDuplicate> unref_dups_)
          : NodeBase(parent_), name(name_), type(type_), bind(bind_), module_name(module_name_),
            source_name(source_name_), unref_dups(std::move(unref_dups_))
      {
      }
      virtual ~NodeReal() override = default;

      virtual void Print(std::ostream&, int, UnresolvedSymbols::const_iterator&,
                         UnresolvedSymbols::const_iterator, std::size_t&) const override;

      const std::string name;
      const Type type;
      const Bind bind;
      // Static library or object name
      const std::string module_name;
      // When linking a static library, this is either:
      // A) The name of the STT_FILE symbol from the relevant object in the static library.
      // B) The name of the relevant object in the static library (as early as CW for GCN 2.7).
      const std::string source_name;
      const std::list<UnreferencedDuplicate> unref_dups;
    };

    struct NodeLinkerGenerated final : NodeBase
    {
      explicit NodeLinkerGenerated(NodeBase* parent_, std::string_view name_)
          : NodeBase(parent_), name(name_)
      {
      }
      virtual ~NodeLinkerGenerated() override = default;

      virtual void Print(std::ostream&, int, UnresolvedSymbols::const_iterator&,
                         UnresolvedSymbols::const_iterator, std::size_t&) const override;

      const std::string name;
    };

    using NodeLookup = std::unordered_multimap<std::string_view, const NodeReal&>;
    using ModuleLookup = std::unordered_map<std::string_view, NodeLookup>;

    explicit SymbolClosure() = default;
    virtual ~SymbolClosure() override = default;

    Error Scan(const char*&, const char*, std::size_t&, UnresolvedSymbols&);
    void Print(std::ostream&, UnresolvedSymbols::const_iterator&, UnresolvedSymbols::const_iterator,
               std::size_t&) const;
    virtual bool IsEmpty() const noexcept override { return root.children.empty(); }

    const ModuleLookup& GetModuleLookup() { return lookup; }

    struct Warn
    {
      friend SymbolClosure;

      static inline bool do_warn_odr_violation = true;
      static inline bool do_warn_sym_on_flag_detected = true;

    private:
      static void OneDefinitionRuleViolation(std::size_t, std::string_view, std::string_view);
      static void SymOnFlagDetected(std::size_t, std::string_view);
    };

  private:
    NodeBase root;
    ModuleLookup lookup;
  };

  struct EPPC_PatternMatching final : PortionBase
  {
    // CodeWarrior for Wii 1.0
    //  - Added EPPC_PatternMatching

    struct MergingUnit
    {
      explicit MergingUnit(std::string_view first_name_, std::string_view second_name_,
                           Elf32_Word size_, bool will_be_replaced_, bool was_interchanged_)
          : first_name(first_name_), second_name(second_name_), size(size_),
            will_be_replaced(will_be_replaced_), was_interchanged(was_interchanged_)
      {
      }

      void Print(std::ostream&, std::size_t&) const;

      const std::string first_name;
      const std::string second_name;
      const Elf32_Word size;
      // If the conditions are right (e.g. the function is more than just a BLR instruction), then
      // one function is replaced with a branch to the other function, saving space at the cost of a
      // tiny amount of overhead. This is by far the more common code merging technique.
      const bool will_be_replaced;
      // Rarely, a function can be marked for removal when a duplicate of it is elsewhere in the
      // binary. All references to it are then redirected to the duplicate. Even rarer than that,
      // sometimes the linker can change its mind and replace it with a branch instead.
      const bool was_interchanged;
    };

    struct FoldingUnit
    {
      friend EPPC_PatternMatching;

      struct Unit
      {
        explicit Unit(std::string_view first_name_, std::string_view second_name_, Elf32_Word size_,
                      bool new_branch_function_)
            : first_name(first_name_), second_name(second_name_), size(size_),
              new_branch_function(new_branch_function_)
        {
        }

        void Print(std::ostream&, std::size_t&) const;

        const std::string first_name;
        const std::string second_name;
        const Elf32_Word size;
        const bool new_branch_function;
      };

      using UnitLookup = std::unordered_multimap<std::string_view, const Unit&>;
      using ModuleLookup = std::unordered_map<std::string_view, UnitLookup>;

      explicit FoldingUnit(std::string_view object_name_) : object_name(object_name_) {}

      void Print(std::ostream&, std::size_t&) const;

      const std::list<Unit>& GetUnits() { return units; }

      const std::string object_name;

    private:
      std::list<Unit> units;
    };

    using MergingUnitLookup = std::unordered_multimap<std::string_view, const MergingUnit&>;

    explicit EPPC_PatternMatching() { SetMinVersion(Version::version_4_2_build_142); }
    virtual ~EPPC_PatternMatching() override = default;

    Error Scan(const char*&, const char*, std::size_t&);
    void Print(std::ostream&, std::size_t&) const;
    virtual bool IsEmpty() const noexcept override
    {
      return merging_units.empty() || folding_units.empty();
    }

    const std::list<MergingUnit>& GetMergingUnits() { return merging_units; }
    const std::list<FoldingUnit>& GetFoldingUnits() { return folding_units; }
    const MergingUnitLookup& GetMergingLookup() { return merging_lookup; }
    const FoldingUnit::ModuleLookup& GetFoldingModuleLookup() { return folding_lookup; }

    struct Warn
    {
      friend EPPC_PatternMatching;

      static inline bool do_warn_merging_odr_violation = true;
      static inline bool do_warn_folding_repeat_object = true;
      static inline bool do_warn_folding_odr_violation = true;

    private:
      static void MergingOneDefinitionRuleViolation(std::size_t, std::string_view);
      static void FoldingRepeatObject(std::size_t, std::string_view);
      static void FoldingOneDefinitionRuleViolation(std::size_t, std::string_view,
                                                    std::string_view);
    };

  private:
    std::list<MergingUnit> merging_units;
    std::list<FoldingUnit> folding_units;
    MergingUnitLookup merging_lookup;
    FoldingUnit::ModuleLookup folding_lookup;
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

      explicit Unit(std::string_view module_name_, std::string_view name_)
          : unit_kind(Kind::DisassembleError), module_name(module_name_), name(name_)
      {
      }
      explicit Unit(const Kind unit_kind_, std::string_view module_name_, std::string_view name_,
                    std::string_view reference_name_)
          : unit_kind(unit_kind_), module_name(module_name_), name(name_),
            reference_name(reference_name_)
      {
      }

      void Print(std::ostream&, std::size_t&) const;

      const Kind unit_kind;
      const std::string module_name;
      const std::string name;
      const std::string reference_name;
    };

    explicit LinkerOpts() { SetMinVersion(Version::version_4_2_build_142); }
    virtual ~LinkerOpts() override = default;

    Error Scan(const char*&, const char*, std::size_t&);
    void Print(std::ostream&, std::size_t&) const;
    virtual bool IsEmpty() const noexcept override { return units.empty(); }

    const std::list<Unit>& GetUnits() { return units; }

  private:
    std::list<Unit> units;
  };

  struct BranchIslands final : PortionBase
  {
    // CodeWarror for GCN 3.0a3 (at the earliest)
    //  - Added Branch Islands.

    struct Unit
    {
      explicit Unit(std::string_view first_name_, std::string_view second_name_, bool is_safe_)
          : first_name(first_name_), second_name(second_name_), is_safe(is_safe_)
      {
      }

      void Print(std::ostream&, std::size_t&) const;

      const std::string first_name;
      const std::string second_name;
      const bool is_safe;
    };

    explicit BranchIslands() { SetMinVersion(Version::version_4_1_build_51213); }
    virtual ~BranchIslands() override = default;

    Error Scan(const char*&, const char*, std::size_t&);
    void Print(std::ostream&, std::size_t&) const;
    virtual bool IsEmpty() const noexcept override { return units.empty(); }

    const std::list<Unit>& GetUnits() { return units; }

  private:
    std::list<Unit> units;
  };

  struct MixedModeIslands final : PortionBase
  {
    // CodeWarror for GCN 3.0a3 (at the earliest)
    //  - Added Mixed Mode Islands.

    struct Unit
    {
      explicit Unit(std::string_view first_name_, std::string_view second_name_, bool is_safe_)
          : first_name(first_name_), second_name(second_name_), is_safe(is_safe_)
      {
      }

      void Print(std::ostream&, std::size_t&) const;

      const std::string first_name;
      const std::string second_name;
      const bool is_safe;
    };

    explicit MixedModeIslands() { SetMinVersion(Version::version_4_1_build_51213); }
    virtual ~MixedModeIslands() override = default;

    Error Scan(const char*&, const char*, std::size_t&);
    void Print(std::ostream&, std::size_t&) const;
    virtual bool IsEmpty() const noexcept override { return units.empty(); }

    const std::list<Unit>& GetUnits() { return units; }

  private:
    std::list<Unit> units;
  };

  struct LinktimeSizeDecreasingOptimizations final : PortionBase
  {
    explicit LinktimeSizeDecreasingOptimizations() = default;
    virtual ~LinktimeSizeDecreasingOptimizations() override = default;

    Error Scan(const char*&, const char*, std::size_t&);
    void Print(std::ostream&, std::size_t&) const;
    virtual bool IsEmpty() const noexcept override { return true; }
  };

  struct LinktimeSizeIncreasingOptimizations final : PortionBase
  {
    explicit LinktimeSizeIncreasingOptimizations() = default;
    virtual ~LinktimeSizeIncreasingOptimizations() override = default;

    Error Scan(const char*&, const char*, std::size_t&);
    void Print(std::ostream&, std::size_t&) const;
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

    using UnitLookup = std::unordered_multimap<std::string_view, const Unit&>;
    using ModuleLookup = std::unordered_map<std::string_view, UnitLookup>;

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
      explicit Unit(Elf32_Word size_, std::string_view name_, std::string_view module_name_,
                    std::string_view source_name_, SectionLayout& section_layout,
                    std::string_view& curr_module_name, std::string_view& curr_source_name,
                    UnitLookup*& curr_unit_lookup, bool& is_in_lcomm, bool& is_after_eti_init_info,
                    bool& is_multi_stt_section, std::size_t line_number)
          : unit_kind(Kind::Unused), starting_address{},
            size(size_), virtual_address{}, file_offset{}, alignment{}, name(name_),
            entry_parent(nullptr), module_name(module_name_), source_name(source_name_),
            unit_trait(DeduceUsualSubtext(section_layout, curr_module_name, curr_source_name,
                                          curr_unit_lookup, is_in_lcomm, is_after_eti_init_info,
                                          is_multi_stt_section, line_number))
      {
      }
      // 3-column normal symbols
      explicit Unit(std::uint32_t starting_address_, Elf32_Word size_, Elf32_Addr virtual_address_,
                    int alignment_, std::string_view name_, std::string_view module_name_,
                    std::string_view source_name_, SectionLayout& section_layout,
                    std::string_view& curr_module_name, std::string_view& curr_source_name,
                    UnitLookup*& curr_unit_lookup, bool& is_in_lcomm, bool& is_after_eti_init_info,
                    bool& is_multi_stt_section, std::size_t line_number)
          : unit_kind(Kind::Normal), starting_address(starting_address_), size(size_),
            virtual_address(virtual_address_), file_offset{}, alignment(alignment_), name(name_),
            entry_parent(nullptr), module_name(module_name_), source_name(source_name_),
            unit_trait(DeduceUsualSubtext(section_layout, curr_module_name, curr_source_name,
                                          curr_unit_lookup, is_in_lcomm, is_after_eti_init_info,
                                          is_multi_stt_section, line_number))
      {
      }
      // 4-column normal symbols
      explicit Unit(std::uint32_t starting_address_, Elf32_Word size_, Elf32_Addr virtual_address_,
                    std::uint32_t file_offset_, int alignment_, std::string_view name_,
                    std::string_view module_name_, std::string_view source_name_,
                    SectionLayout& section_layout, std::string_view& curr_module_name,
                    std::string_view& curr_source_name, UnitLookup*& curr_unit_lookup,
                    bool& is_in_lcomm, bool& is_after_eti_init_info, bool& is_multi_stt_section,
                    std::size_t line_number)
          : unit_kind(Kind::Normal), starting_address(starting_address_), size(size_),
            virtual_address(virtual_address_), file_offset(file_offset_), alignment(alignment_),
            name(name_), entry_parent(nullptr), module_name(module_name_),
            source_name(source_name_),
            unit_trait(DeduceUsualSubtext(section_layout, curr_module_name, curr_source_name,
                                          curr_unit_lookup, is_in_lcomm, is_after_eti_init_info,
                                          is_multi_stt_section, line_number))
      {
      }
      // 3-column entry symbols
      explicit Unit(std::uint32_t starting_address_, Elf32_Word size_, Elf32_Addr virtual_address_,
                    std::string_view name_, const Unit* entry_parent_,
                    std::string_view module_name_, std::string_view source_name_, Trait unit_trait_)
          : unit_kind(Kind::Entry), starting_address(starting_address_), size(size_),
            virtual_address(virtual_address_), file_offset{}, alignment{}, name(name_),
            entry_parent(entry_parent_), module_name(module_name_), source_name(source_name_),
            unit_trait(unit_trait_)
      {
      }
      // 4-column entry symbols
      explicit Unit(std::uint32_t starting_address_, Elf32_Word size_, Elf32_Addr virtual_address_,
                    std::uint32_t file_offset_, std::string_view name_, const Unit* entry_parent_,
                    std::string_view module_name_, std::string_view source_name_, Trait unit_trait_)
          : unit_kind(Kind::Entry), starting_address(starting_address_), size(size_),
            virtual_address(virtual_address_), file_offset(file_offset_), alignment{}, name(name_),
            entry_parent(entry_parent_), module_name(module_name_), source_name(source_name_),
            unit_trait(unit_trait_)
      {
      }
      // 4-column special symbols
      explicit Unit(std::uint32_t starting_address_, Elf32_Word size_, Elf32_Addr virtual_address_,
                    std::uint32_t file_offset_, int alignment_, Trait unit_trait_)
          : unit_kind(Kind::Special), starting_address(starting_address_), size(size_),
            virtual_address(virtual_address_), file_offset(file_offset_), alignment(alignment_),
            entry_parent(nullptr), unit_trait(unit_trait_)
      {
      }

      void Print3Column(std::ostream&, std::size_t&) const;
      void Print4Column(std::ostream&, std::size_t&) const;
      static constexpr std::string_view ToSpecialName(Trait);

      const Unit* GetEntryParent() { return entry_parent; }
      const std::list<const Unit*>& GetEntryChildren() { return entry_children; }

      const Kind unit_kind;
      const std::uint32_t starting_address;
      const Elf32_Word size;
      const Elf32_Addr virtual_address;
      const std::uint32_t file_offset;
      const int alignment;
      const std::string name;

    private:
      // Doubly-linked relationship between entry symbols and their host.
      const Unit* const entry_parent;
      // Doubly-linked relationship between entry symbols and their host.
      std::list<const Unit*> entry_children;

    public:
      // Static library or object name
      const std::string module_name;
      // When linking a static library, this is either:
      // A) The name of the STT_FILE symbol from the relevant object in the static library.
      // B) The name of the relevant object in the static library (as early as CW for GCN 2.7).
      const std::string source_name;
      const Trait unit_trait;

    private:
      Unit::Trait DeduceUsualSubtext(SectionLayout&, std::string_view&, std::string_view&,
                                     UnitLookup*&, bool&, bool&, bool&, std::size_t);
    };

    explicit SectionLayout(Kind section_kind_, std::string_view name_)
        : section_kind(section_kind_), name(name_)
    {
    }
    virtual ~SectionLayout() override = default;

    Error Scan3Column(const char*&, const char*, std::size_t&);
    Error Scan4Column(const char*&, const char*, std::size_t&);
    Error ScanTLOZTP(const char*&, const char*, std::size_t&);
    void Print(std::ostream&, std::size_t&) const;
    virtual bool IsEmpty() const noexcept override { return units.empty(); }
    static Kind ToSectionKind(std::string_view);

    const std::list<Unit>& GetUnits() const noexcept { return units; }
    const ModuleLookup& GetModuleLookup() const noexcept { return lookup; }

    const Kind section_kind;
    const std::string name;

    struct Warn
    {
      friend SectionLayout;

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
    };

  private:
    std::list<Unit> units;
    ModuleLookup lookup;
  };

  // CodeWarrior for GCN 2.7
  //  - Changed size column for debug sections from "%06x" to "%08x".
  // CodeWarrior for Wii 1.0
  //  - Expanded Memory Map variants, slightly tweaked existing printfs.
  struct MemoryMap final : PortionBase
  {
    struct UnitNormal
    {
      explicit UnitNormal(std::string_view name_, Elf32_Addr starting_address_, Elf32_Word size_,
                          std::uint32_t file_offset_)
          : name(name_), starting_address(starting_address_), size(size_),
            file_offset(file_offset_), rom_address{}, ram_buffer_address{}, s_record_line{},
            bin_file_offset{}
      {
      }
      explicit UnitNormal(std::string_view name_, Elf32_Addr starting_address_, Elf32_Word size_,
                          std::uint32_t file_offset_, int s_record_line_)
          : name(name_), starting_address(starting_address_), size(size_),
            file_offset(file_offset_), rom_address{}, ram_buffer_address{},
            s_record_line(s_record_line_), bin_file_offset{}
      {
      }
      explicit UnitNormal(std::string_view name_, Elf32_Addr starting_address_, Elf32_Word size_,
                          std::uint32_t file_offset_, std::uint32_t rom_address_,
                          std::uint32_t ram_buffer_address_)
          : name(name_), starting_address(starting_address_), size(size_),
            file_offset(file_offset_), rom_address(rom_address_),
            ram_buffer_address(ram_buffer_address_), s_record_line{}, bin_file_offset{}
      {
      }
      explicit UnitNormal(std::string_view name_, Elf32_Addr starting_address_, Elf32_Word size_,
                          std::uint32_t file_offset_, std::uint32_t rom_address_,
                          std::uint32_t ram_buffer_address_, int s_record_line_)
          : name(name_), starting_address(starting_address_), size(size_),
            file_offset(file_offset_), rom_address(rom_address_),
            ram_buffer_address(ram_buffer_address_),
            s_record_line(s_record_line_), bin_file_offset{}
      {
      }
      explicit UnitNormal(std::string_view name_, Elf32_Addr starting_address_, Elf32_Word size_,
                          std::uint32_t file_offset_, std::uint32_t bin_file_offset_,
                          std::string_view bin_file_name_)
          : name(name_), starting_address(starting_address_), size(size_),
            file_offset(file_offset_), rom_address{}, ram_buffer_address{}, s_record_line{},
            bin_file_offset(bin_file_offset_), bin_file_name(bin_file_name_)
      {
      }
      explicit UnitNormal(std::string_view name_, Elf32_Addr starting_address_, Elf32_Word size_,
                          std::uint32_t file_offset_, int s_record_line_,
                          std::uint32_t bin_file_offset_, std::string_view bin_file_name_)
          : name(name_), starting_address(starting_address_), size(size_),
            file_offset(file_offset_), rom_address{}, ram_buffer_address{},
            s_record_line(s_record_line_), bin_file_offset(bin_file_offset_),
            bin_file_name(bin_file_name_)
      {
      }
      explicit UnitNormal(std::string_view name_, Elf32_Addr starting_address_, Elf32_Word size_,
                          std::uint32_t file_offset_, std::uint32_t rom_address_,
                          std::uint32_t ram_buffer_address_, std::uint32_t bin_file_offset_,
                          std::string_view bin_file_name_)
          : name(name_), starting_address(starting_address_), size(size_),
            file_offset(file_offset_), rom_address(rom_address_),
            ram_buffer_address(ram_buffer_address_), s_record_line{},
            bin_file_offset(bin_file_offset_), bin_file_name(bin_file_name_)
      {
      }
      explicit UnitNormal(std::string_view name_, Elf32_Addr starting_address_, Elf32_Word size_,
                          std::uint32_t file_offset_, std::uint32_t rom_address_,
                          std::uint32_t ram_buffer_address_, int s_record_line_,
                          std::uint32_t bin_file_offset_, std::string_view bin_file_name_)
          : name(name_), starting_address(starting_address_), size(size_),
            file_offset(file_offset_), rom_address(rom_address_),
            ram_buffer_address(ram_buffer_address_), s_record_line(s_record_line_),
            bin_file_offset(bin_file_offset_), bin_file_name(bin_file_name_)
      {
      }

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

      const std::string name;
      const Elf32_Addr starting_address;
      const Elf32_Word size;
      const std::uint32_t file_offset;
      const std::uint32_t rom_address;
      const std::uint32_t ram_buffer_address;
      const int s_record_line;
      const std::uint32_t bin_file_offset;
      const std::string bin_file_name;
    };

    // TODO: There is an opportunity for detecting the min version from the normal and debug section
    // names, but I couldn't be bothered to look into it.
    struct UnitDebug
    {
      explicit UnitDebug(std::string_view name_, Elf32_Word size_, std::uint32_t file_offset_)
          : name(name_), size(size_), file_offset(file_offset_)
      {
      }

      void Print_older(std::ostream&, std::size_t&) const;
      void Print_old(std::ostream&, std::size_t&) const;
      void Print(std::ostream&, std::size_t&) const;

      const std::string name;
      const Elf32_Word size;
      const std::uint32_t file_offset;
    };

    explicit MemoryMap(bool has_rom_ram_)  // ctor for old memory map
        : has_rom_ram(has_rom_ram_), has_s_record(false), has_bin_file(false)
    {
    }
    explicit MemoryMap(bool has_rom_ram_, bool has_s_record_, bool has_bin_file_)
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
    virtual bool IsEmpty() const noexcept override
    {
      return normal_units.empty() || debug_units.empty();
    }

    const std::list<UnitNormal>& GetNormalUnits() const noexcept { return normal_units; }
    const std::list<UnitDebug>& GetDebugUnits() const noexcept { return debug_units; }

    const bool has_rom_ram;   // Enabled by '-romaddr addr' and '-rambuffer addr' options
    const bool has_s_record;  // Enabled by '-srec [filename]' option
    const bool has_bin_file;  // Enabled by '-genbinary keyword' option

  private:
    std::list<UnitNormal> normal_units;
    std::list<UnitDebug> debug_units;
  };

  struct LinkerGeneratedSymbols final : PortionBase
  {
    struct Unit
    {
      explicit Unit(std::string_view name_, Elf32_Addr value_) : name(name_), value(value_) {}

      void Print(std::ostream&, std::size_t&) const;

      const std::string name;
      const Elf32_Addr value;
    };

    explicit LinkerGeneratedSymbols() = default;
    virtual ~LinkerGeneratedSymbols() override = default;

    Error Scan(const char*&, const char*, std::size_t&);
    void Print(std::ostream&, std::size_t&) const;
    virtual bool IsEmpty() const noexcept override { return units.empty(); }

    const std::list<Unit>& GetUnits() const noexcept { return units; }

  private:
    std::list<Unit> units;
  };

  Error Scan(std::string_view, std::size_t&);
  Error Scan(const char*, const char*, std::size_t&);
  Error ScanTLOZTP(std::string_view, std::size_t&);
  Error ScanTLOZTP(const char*, const char*, std::size_t&);
  Error ScanSMGalaxy(std::string_view, std::size_t&);
  Error ScanSMGalaxy(const char*, const char*, std::size_t&);
  void Print(std::ostream&, std::size_t&) const;
  Version GetMinVersion() const noexcept;

  const std::string& GetEntryPointName() const noexcept { return entry_point_name; }
  const std::unique_ptr<SymbolClosure>& GetNormalSymbolClosure() const noexcept
  {
    return normal_symbol_closure;
  }
  const std::unique_ptr<EPPC_PatternMatching>& GetEPPC_PatternMatching() const noexcept
  {
    return eppc_pattern_matching;
  }
  const std::unique_ptr<SymbolClosure>& GetDwarfSymbolClosure() const noexcept
  {
    return dwarf_symbol_closure;
  }
  const std::list<std::pair<std::size_t, std::string>>& GetUnresolvedSymbols() const noexcept
  {
    return unresolved_symbols;
  }
  const std::list<std::unique_ptr<SectionLayout>>& GetSectionLayouts() const noexcept
  {
    return section_layouts;
  }
  const std::unique_ptr<MemoryMap>& GetMemoryMap() const noexcept  //
  {
    return memory_map;
  }
  const std::unique_ptr<LinkerGeneratedSymbols>& GetLinkerGeneratedSymbols() const noexcept
  {
    return linker_generated_symbols;
  }

private:
  Error ScanPrologue_SectionLayout(const char*&, const char* const, std::size_t&, std::string_view);
  Error ScanPrologue_MemoryMap(const char*&, const char*, std::size_t&);
  Error ScanForGarbage(const char*, const char*);

  std::string entry_point_name;
  std::unique_ptr<SymbolClosure> normal_symbol_closure;
  std::unique_ptr<EPPC_PatternMatching> eppc_pattern_matching;
  std::unique_ptr<SymbolClosure> dwarf_symbol_closure;
  UnresolvedSymbols unresolved_symbols;
  std::unique_ptr<LinkerOpts> linker_opts;
  std::unique_ptr<MixedModeIslands> mixed_mode_islands;
  std::unique_ptr<BranchIslands> branch_islands;
  std::unique_ptr<LinktimeSizeDecreasingOptimizations> linktime_size_decreasing_optimizations;
  std::unique_ptr<LinktimeSizeIncreasingOptimizations> linktime_size_increasing_optimizations;
  std::list<std::unique_ptr<SectionLayout>> section_layouts;
  std::unique_ptr<MemoryMap> memory_map;
  std::unique_ptr<LinkerGeneratedSymbols> linker_generated_symbols;
};
}  // namespace MWLinker
