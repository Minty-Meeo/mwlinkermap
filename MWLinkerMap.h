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
#include <utility>

using u32 = std::uint32_t;

namespace Common
{
struct IntermediateDB;
}

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

  struct PortionBase
  {
    virtual ~PortionBase() = default;

    void SetMinVersion(const Version version) noexcept
    {
      min_version = std::max(min_version, version);
    }
    virtual void Print(std::ostream&) const = 0;
    virtual bool Empty() const noexcept = 0;

    Version min_version = Version::Unknown;
  };

  // CodeWarrior for GCN 1.1
  //  - Added UNREFERENCED DUPLICATE info.
  // CodeWarrior for GCN 2.7
  //  - Symbol closure became optional with '-[no]listclosure', off by default.
  //  - Added _ctors$99 and _dtors$99, among other things.
  struct SymbolClosure final : PortionBase
  {
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

    struct NodeBase
    {
      NodeBase() = default;  // Necessary for root node
      NodeBase(std::string name_) : name(std::move(name_)) {}
      virtual ~NodeBase() = default;

      virtual void Print(std::ostream&, int) const;  // Necessary for root node

      NodeBase* parent = nullptr;
      std::list<std::unique_ptr<NodeBase>> children;

      std::string name;
    };

    struct NodeNormal final : NodeBase
    {
      struct UnreferencedDuplicate
      {
        UnreferencedDuplicate(Type type_, Bind bind_, std::string module_, std::string file_)
            : type(type_), bind(bind_), module(std::move(module_)), file(std::move(file_))
        {
        }

        void Print(std::ostream&, int) const;

        Type type;
        Bind bind;
        std::string module;
        std::string file;
      };

      NodeNormal(std::string name_, Type type_, Bind bind_, std::string module_, std::string file_)
          : NodeBase(std::move(name_)), type(type_), bind(bind_), module(std::move(module_)),
            file(std::move(file_))
      {
      }
      virtual ~NodeNormal() override = default;

      virtual void Print(std::ostream&, int) const override;

      Type type;
      Bind bind;
      std::string module;
      std::string file;
      std::list<UnreferencedDuplicate> unref_dups;
    };

    struct NodeLinkerGenerated final : NodeBase
    {
      NodeLinkerGenerated(std::string name_) : NodeBase(std::move(name_)) {}
      virtual ~NodeLinkerGenerated() override = default;

      virtual void Print(std::ostream&, int) const override;
    };

    SymbolClosure() = default;
    virtual ~SymbolClosure() override = default;

    Error Scan(const char*&, const char*, std::size_t&, std::list<std::string>&);
    virtual void Print(std::ostream&) const override;
    static void PrintPrefix(std::ostream&, int);
    static const char* GetName(Type) noexcept;
    static const char* GetName(Bind) noexcept;
    virtual bool Empty() const noexcept override { return root.children.empty(); }

    NodeBase root;
  };

  // CodeWarrior for Wii 1.0
  //  - Added EPPC_PatternMatching
  struct EPPC_PatternMatching final : PortionBase
  {
    struct MergingUnit
    {
      MergingUnit(std::string first_name_, std::string second_name_, u32 size_,
                  bool will_be_replaced_, bool was_interchanged_)
          : first_name(std::move(first_name_)), second_name(std::move(second_name_)), size(size_),
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
        Unit(std::string first_name_, std::string second_name_, u32 size_,
             bool new_branch_function_)
            : first_name(first_name_), second_name(std::move(second_name_)), size(size_),
              new_branch_function(new_branch_function_)
        {
        }

        void Print(std::ostream&) const;

        std::string first_name;
        std::string second_name;
        u32 size;
        bool new_branch_function;
      };

      FoldingUnit(std::string name_) : name(std::move(name_)) {}

      void Print(std::ostream&) const;

      std::string name;
      std::list<Unit> units;
    };

    EPPC_PatternMatching() { SetMinVersion(Version::version_4_2_build_142); }
    virtual ~EPPC_PatternMatching() override = default;

    Error Scan(const char*&, const char*, std::size_t&);
    virtual void Print(std::ostream&) const override;
    virtual bool Empty() const noexcept override
    {
      return merging_units.empty() || folding_units.empty();
    }

    std::list<MergingUnit> merging_units;
    std::list<FoldingUnit> folding_units;
  };

  // CodeWarrior for Wii 1.0
  //  - Added LinkerOpts
  struct LinkerOpts final : PortionBase
  {
    struct Unit
    {
      enum class Kind
      {
        NotNear,
        NotComputed,
        Optimized,
        DisassembleError,
      };

      Unit(std::string module_, std::string name_)
          : unit_kind(Kind::DisassembleError), module(std::move(module_)), name(std::move(name_))
      {
      }
      Unit(const Kind unit_kind_, std::string module_, std::string name_,
           std::string reference_name_)
          : unit_kind(unit_kind_), module(std::move(module_)), name(std::move(name_)),
            reference_name(std::move(reference_name_))
      {
      }

      void Print(std::ostream&) const;

      const Kind unit_kind;
      std::string module;
      std::string name;
      std::string reference_name;
    };

    LinkerOpts() { SetMinVersion(Version::version_4_2_build_142); }
    virtual ~LinkerOpts() override = default;

    Error Scan(const char*&, const char*, std::size_t&);
    virtual void Print(std::ostream&) const override;
    virtual bool Empty() const noexcept override { return units.empty(); }

    std::list<Unit> units;
  };

  // CodeWarror for GCN 3.0a3 (at the earliest)
  //  - Added Branch Islands.
  struct BranchIslands final : PortionBase
  {
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
    virtual bool Empty() const noexcept override { return units.empty(); }

    std::list<Unit> units;
  };

  // CodeWarror for GCN 3.0a3 (at the earliest)
  //  - Added Mixed Mode Islands.
  struct MixedModeIslands final : PortionBase
  {
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
    virtual bool Empty() const noexcept override { return units.empty(); }

    std::list<Unit> units;
  };

  struct LinktimeSizeDecreasingOptimizations final : PortionBase
  {
    LinktimeSizeDecreasingOptimizations() = default;
    virtual ~LinktimeSizeDecreasingOptimizations() override = default;

    Error Scan(const char*&, const char*, std::size_t&);
    virtual void Print(std::ostream&) const override;
    virtual bool Empty() const noexcept override { return true; }
  };

  struct LinktimeSizeIncreasingOptimizations final : PortionBase
  {
    LinktimeSizeIncreasingOptimizations() = default;
    virtual ~LinktimeSizeIncreasingOptimizations() override = default;

    Error Scan(const char*&, const char*, std::size_t&);
    virtual void Print(std::ostream&) const override;
    virtual bool Empty() const noexcept override { return true; }
  };

  // CodeWarrior for GCN 2.7
  //  - Changed to four column info, added *fill* symbols.
  struct SectionLayout final : PortionBase
  {
    struct Unit
    {
      enum class Kind
      {
        Unused,
        Normal,
        Entry,
        Special,
      };

      // UNUSED symbols
      Unit(u32 size_, std::string name_, std::string module_, std::string file_)
          : unit_kind(Kind::Unused), size(size_), name(std::move(name_)),
            module(std::move(module_)), file(std::move(file_))
      {
      }
      // 3-column normal symbols
      Unit(u32 starting_address_, u32 size_, u32 virtual_address_, int alignment_,
           std::string name_, std::string module_, std::string file_)
          : unit_kind(Kind::Normal), starting_address(starting_address_), size(size_),
            virtual_address(virtual_address_), alignment(alignment_), name(std::move(name_)),
            module(std::move(module_)), file(std::move(file_))
      {
      }
      // 4-column normal symbols
      Unit(u32 starting_address_, u32 size_, u32 virtual_address_, u32 file_offset_, int alignment_,
           std::string name_, std::string module_, std::string file_)
          : unit_kind(Kind::Normal), starting_address(starting_address_), size(size_),
            virtual_address(virtual_address_), file_offset(file_offset_), alignment(alignment_),
            name(std::move(name_)), module(std::move(module_)), file(std::move(file_))
      {
      }
      // 3-column entry symbols
      Unit(u32 starting_address_, u32 size_, u32 virtual_address_, std::string name_,
           Unit* entry_parent_, std::string module_, std::string file_)
          : unit_kind(Kind::Entry), starting_address(starting_address_), size(size_),
            virtual_address(virtual_address_), name(std::move(name_)), entry_parent(entry_parent_),
            module(std::move(module_)), file(std::move(file_))
      {
      }
      // 4-column entry symbols
      Unit(u32 starting_address_, u32 size_, u32 virtual_address_, u32 file_offset_,
           std::string name_, Unit* entry_parent_, std::string module_, std::string file_)
          : unit_kind(Kind::Entry), starting_address(starting_address_), size(size_),
            virtual_address(virtual_address_), file_offset(file_offset_), name(std::move(name_)),
            entry_parent(entry_parent_), module(std::move(module_)), file(std::move(file_))
      {
      }
      // 4-column special symbols
      Unit(u32 starting_address_, u32 size_, u32 virtual_address_, u32 file_offset_, int alignment_,
           std::string name_)
          : unit_kind(Kind::Special), starting_address(starting_address_), size(size_),
            virtual_address(virtual_address_), file_offset(file_offset_), alignment(alignment_),
            name(std::move(name_))
      {
      }

      void Print3Column(std::ostream&) const;
      void Print4Column(std::ostream&) const;

      const Kind unit_kind;
      u32 starting_address;
      u32 size;
      u32 virtual_address;
      u32 file_offset;
      int alignment;
      std::string name;
      Unit* entry_parent;
      std::list<Unit*> entry_children;
      std::string module;
      std::string file;
    };

    SectionLayout(std::string name_) : name(std::move(name_)) {}
    virtual ~SectionLayout() override = default;

    Error Scan3Column(const char*&, const char*, std::size_t&);
    Error Scan4Column(const char*&, const char*, std::size_t&);
    Error ScanTLOZTP(const char*&, const char*, std::size_t&);
    virtual void Print(std::ostream&) const override;
    virtual bool Empty() const noexcept override { return units.empty(); }

    std::string name;
    std::list<Unit> units;
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
    virtual bool Empty() const noexcept override
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
    virtual bool Empty() const noexcept override { return units.empty(); }

    std::list<Unit> units;
  };

  Error Scan(std::istream&, std::size_t&);
  Error Scan(const std::stringstream&, std::size_t&);
  Error Scan(std::string_view, std::size_t&);
  Error Scan(const char*, const char*, std::size_t&);
  Error ScanTLOZTP(std::istream&, std::size_t&);
  Error ScanTLOZTP(const std::stringstream&, std::size_t&);
  Error ScanTLOZTP(std::string_view, std::size_t&);
  Error ScanTLOZTP(const char*, const char*, std::size_t&);
  Error ScanSMGalaxy(std::istream&, std::size_t&);
  Error ScanSMGalaxy(const std::stringstream&, std::size_t&);
  Error ScanSMGalaxy(std::string_view, std::size_t&);
  Error ScanSMGalaxy(const char*, const char*, std::size_t&);

  Error ScanPrologue_SectionLayout(const char*&, const char* const, std::size_t&, std::string);
  Error ScanPrologue_MemoryMap(const char*&, const char*, std::size_t&);
  Error ScanForGarbage(const char*, const char*);

  void Print(std::ostream&) const;
  Version GetMinVersion() const noexcept;

  std::string entry_point_name;
  std::unique_ptr<SymbolClosure> normal_symbol_closure;
  std::unique_ptr<EPPC_PatternMatching> eppc_pattern_matching;
  std::unique_ptr<SymbolClosure> dwarf_symbol_closure;
  std::list<std::string> unresolved_symbols;
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

namespace Common
{
struct IntermediateDB
{
  struct Symbol
  {
    enum class Type : unsigned char
    {
      NoType = 0,
      Object = 1,
      Func = 2,
      Section = 3,
      File = 4,
    };
    enum class Bind : unsigned char
    {
      Local = 0,
      Global = 1,
      Weak = 2,
    };

    std::string name;
    u32 value = 0;
    u32 size = 0;
    Type type = Type::NoType;
    Bind bind = Bind::Global;
  };
  struct ModuleDB
  {
    std::map<std::string, Symbol> symbols;
  };

  void Import(MWLinker::Map&);

  std::map<std::string, ModuleDB> modules;
};
}  // namespace Common
