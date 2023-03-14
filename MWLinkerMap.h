#include <algorithm>
#include <cstddef>
#include <istream>
#include <list>
#include <memory>
#include <string>

enum class MWLinkerVersion
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

struct MWLinkerMap
{
  enum class Error
  {
    None,
    Fail,
    Unimplemented,
    GarbageFound,

    RegexFail,

    SymbolClosureBadBody,
    SymbolClosureHierarchySkip,
    SymbolClosureUnrefDupsHierarchyMismatch,
    SymbolClosureUnrefDupsNameMismatch,
    SymbolClosureUnrefDupsEmpty,

    EPPC_PatternMatchingMergingFirstNameMismatch,
    EPPC_PatternMatchingMergingSecondNameMismatch,
    EPPC_PatternMatchingMergingSizeMismatch,
    EPPC_PatternMatchingMergingInterchangeMissingEpilogue,
    EPPC_PatternMatchingFoldingNewBranchFunctionNameMismatch,

    SectionLayoutBadPrologue,

    MemoryMapBadPrologue,

    SymbolNotFound,
  };

  struct PortionBase
  {
    PortionBase() = default;
    virtual ~PortionBase() = default;

    void SetMinVersion(const MWLinkerVersion version)
    {
      min_version = std::max(min_version, version);
    }
    virtual bool IsEmpty() = 0;

    MWLinkerVersion min_version = MWLinkerVersion::Unknown;
  };

  struct EntryPoint final : PortionBase
  {
    EntryPoint(std::string name) : entry_point_name(name) {}
    ~EntryPoint() = default;

    virtual bool IsEmpty() override { return true; }

    std::string entry_point_name;
  };

  // CodeWarrior for GCN 1.1
  //  - Added UNREFERENCED DUPLICATE info.
  // CodeWarrior for GCN 2.7
  //  - Symbol closure became optional with '-[no]listclosure', off by default.
  //  - Added _ctors$99 and _dtors$99, among other things.
  struct SymbolClosure final : PortionBase
  {
    struct NodeBase
    {
      NodeBase() = default;  // Necessary for root node
      NodeBase(std::string name_) : name(std::move(name_)){};
      virtual ~NodeBase() = default;

      NodeBase* parent = nullptr;
      std::list<std::unique_ptr<NodeBase>> children;

      std::string name;
    };

    struct NodeNormal final : NodeBase
    {
      struct UnreferencedDuplicate
      {
        UnreferencedDuplicate(std::string type_, std::string bind_, std::string module_,
                              std::string file_)
            : type(std::move(type_)), bind(std::move(bind_)), module(std::move(module_)),
              file(std::move(file_)){};

        std::string type;
        std::string bind;
        std::string module;
        std::string file;
      };

      NodeNormal(std::string name_, std::string type_, std::string bind_, std::string module_,
                 std::string file_)
          : NodeBase(std::move(name_)), type(std::move(type_)), bind(std::move(bind_)),
            module(std::move(module_)), file(std::move(file_)){};
      virtual ~NodeNormal() = default;

      std::string type;
      std::string bind;
      std::string module;
      std::string file;
      std::list<UnreferencedDuplicate> unref_dups;
    };

    struct NodeLinkerGenerated final : NodeBase
    {
      NodeLinkerGenerated(std::string name_) : NodeBase(std::move(name_)){};
      virtual ~NodeLinkerGenerated() = default;
    };

    SymbolClosure() { min_version = MWLinkerVersion::version_2_3_3_build_126; };
    virtual ~SymbolClosure() = default;

    virtual bool IsEmpty() override { return root.children.empty(); }
    Error Read(const char*&, const char*, std::list<std::string>&, std::size_t&);

    NodeBase root;
  };

  // CodeWarrior for Wii 1.0
  //  - Added EPPC_PatternMatching
  struct EPPC_PatternMatching final : PortionBase
  {
    // As it analyzes, EPPC_PatternMatching looks for functions that are duplicates of one another
    // and prints what it finds to the linker map.
    struct MergingUnit
    {
      MergingUnit(std::string first_name_, std::string second_name_, std::uint32_t size_,
                  bool was_interchanged_, bool will_be_replaced_)
          : first_name(std::move(first_name_)), second_name(std::move(second_name_)), size(size_),
            will_be_replaced(will_be_replaced_), was_interchanged(was_interchanged_){};
      ~MergingUnit() = default;

      std::string first_name;
      std::string second_name;
      std::uint32_t size;
      // If the conditions are right (e.g. the function is more than just a BLR instruction), then
      // one function is replaced with a branch to the other function, saving space at the cost of a
      // tiny amount of overhead. This is by far the more common code merging technique.
      bool will_be_replaced;
      // TODO: explain interchanged
      bool was_interchanged;
    };
    // TODO: add description
    // It happens right after Merging, look at gimp flowchart
    struct FoldingUnit
    {
      struct Unit
      {
        Unit(std::string first_name_, std::string second_name_, std::uint32_t size_,
             bool new_branch_function_)
            : first_name(first_name_), second_name(std::move(second_name_)), size(size_),
              new_branch_function(new_branch_function_){};
        ~Unit() = default;

        std::string first_name;
        std::string second_name;
        std::uint32_t size;
        bool new_branch_function;
      };

      FoldingUnit(std::string name_) : name(std::move(name_)){};

      std::string name;
      std::list<Unit> units;
    };

    EPPC_PatternMatching() { SetMinVersion(MWLinkerVersion::version_4_2_build_142); };
    virtual ~EPPC_PatternMatching() = default;

    virtual bool IsEmpty() override { return merging_units.empty() || folding_units.empty(); }
    Error Read(const char*&, const char*, std::size_t&);

    std::list<MergingUnit> merging_units;
    std::list<FoldingUnit> folding_units;
  };

  // CodeWarrior for Wii 1.0
  //  - Added LinkerOpts
  struct LinkerOpts final : PortionBase
  {
    struct UnitBase
    {
      UnitBase(std::string module_, std::string name_)
          : module(std::move(module_)), name(std::move(name_)){};
      virtual ~UnitBase() = default;

      std::string module;
      std::string name;
    };

    struct UnitNotNear final : UnitBase
    {
      UnitNotNear(std::string module_, std::string name_, std::string reference_name_)
          : UnitBase(std::move(module_), std::move(name_)),
            reference_name(std::move(reference_name_)){};
      virtual ~UnitNotNear() = default;

      std::string reference_name;
    };

    struct UnitNotComputed final : UnitBase
    {
      UnitNotComputed(std::string module_, std::string name_, std::string reference_name_)
          : UnitBase(std::move(module_), std::move(name_)),
            reference_name(std::move(reference_name_)){};
      virtual ~UnitNotComputed() = default;

      std::string reference_name;
    };

    struct UnitOptimized final : UnitBase
    {
      UnitOptimized(std::string module_, std::string name_, std::string reference_name_)
          : UnitBase(std::move(module_), std::move(name_)),
            reference_name(std::move(reference_name_)){};
      virtual ~UnitOptimized() = default;

      std::string reference_name;
    };

    struct UnitDisassembleError final : UnitBase
    {
      UnitDisassembleError(std::string module_, std::string name_)
          : UnitBase(std::move(module_), std::move(name_)){};
      virtual ~UnitDisassembleError() = default;
    };

    LinkerOpts() { SetMinVersion(MWLinkerVersion::version_4_2_build_142); };
    virtual ~LinkerOpts() = default;

    virtual bool IsEmpty() override { return units.empty(); }
    Error Read(const char*&, const char*, std::size_t&);

    std::list<std::unique_ptr<UnitBase>> units;
  };

  // CodeWarror for GCN 3.0a3 (at the earliest)
  //  - Added Branch Islands.
  struct BranchIslands final : PortionBase
  {
    struct Unit
    {
      Unit(std::string first_name_, std::string second_name_, bool is_safe_)
          : first_name(std::move(first_name_)), second_name(std::move(second_name_)),
            is_safe(is_safe_){};
      ~Unit() = default;

      std::string first_name;
      std::string second_name;
      bool is_safe;
    };

    BranchIslands() { SetMinVersion(MWLinkerVersion::version_4_1_build_51213); };
    ~BranchIslands() = default;

    virtual bool IsEmpty() override { return units.empty(); }
    Error Read(const char*&, const char*, std::size_t&);

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
            is_safe(is_safe_){};
      ~Unit() = default;

      std::string first_name;
      std::string second_name;
      bool is_safe;
    };

    MixedModeIslands() { SetMinVersion(MWLinkerVersion::version_4_1_build_51213); };
    ~MixedModeIslands() = default;

    virtual bool IsEmpty() override { return units.empty(); }
    Error Read(const char*&, const char*, std::size_t&);

    std::list<Unit> units;
  };

  // CodeWarrior for GCN 2.7
  //  - Changed to four column info, added *fill* symbols.
  struct SectionLayout final : PortionBase
  {
    struct UnitBase
    {
      virtual ~UnitBase() = default;
    };

    struct UnitNormal final : UnitBase
    {
      UnitNormal(std::uint32_t saddress_, std::uint32_t size_, std::uint32_t vaddress_,
                 std::uint32_t foffset_, std::uint32_t alignment_, std::string name_,
                 std::string module_, std::string file_)
          : saddress(saddress_), size(size_), vaddress(vaddress_), foffset(foffset_),
            alignment(alignment_), name(std::move(name_)), module(std::move(module_)),
            file(std::move(file_)){};
      virtual ~UnitNormal() = default;

      std::uint32_t saddress;
      std::uint32_t size;
      std::uint32_t vaddress;
      std::uint32_t foffset;
      std::uint32_t alignment;
      std::string name;
      std::string module;  // ELF object or static library name
      std::string file;    // Static library STT_FILE symbol name (optional)
    };

    struct UnitUnused final : UnitBase
    {
      UnitUnused(std::uint32_t size_, std::string name_, std::string module_, std::string file_)
          : size(size_), name(std::move(name_)), module(std::move(module_)),
            file(std::move(file_)){};
      virtual ~UnitUnused() = default;

      std::uint32_t size;
      std::string name;
      std::string module;  // ELF object or static library name
      std::string file;    // Static library STT_FILE symbol name (optional)
    };

    struct UnitEntry final : UnitBase
    {
      UnitEntry() = default;
      UnitEntry(std::uint32_t saddress_, std::uint32_t size_, std::uint32_t vaddress_,
                std::uint32_t foffset_, std::string name_, std::string entry_of_name_,
                std::string module_, std::string file_)
          : saddress(saddress_), size(size_), vaddress(vaddress_), foffset(foffset_),
            name(std::move(name_)), entry_of_name(std::move(entry_of_name_)),
            module(std::move(module_)), file(std::move(file_)){};
      virtual ~UnitEntry() = default;

      std::uint32_t saddress;
      std::uint32_t size;
      std::uint32_t vaddress;
      std::uint32_t foffset;
      std::string name;
      std::string entry_of_name;  // (entry of _____)
      std::string module;         // ELF object or static library name
      std::string file;           // Static library STT_FILE symbol name (optional)
    };

    struct UnitSpecial final : UnitBase
    {
      UnitSpecial(std::uint32_t saddress_, std::uint32_t size_, std::uint32_t vaddress_,
                  std::uint32_t foffset_, std::uint32_t alignment_, std::string name_)
          : saddress(saddress_), size(size_), vaddress(vaddress_), foffset(foffset_),
            alignment(alignment_), name(std::move(name_)){};
      virtual ~UnitSpecial() = default;

      std::uint32_t saddress;
      std::uint32_t size;
      std::uint32_t vaddress;
      std::uint32_t foffset;
      std::uint32_t alignment;
      std::string name;  // e.g. "*fill*" or "**fill**"
    };

    SectionLayout(std::string name_) : name(std::move(name_)){};
    virtual ~SectionLayout() = default;

    virtual bool IsEmpty() override { return units.empty(); }
    Error Read(const char*&, const char*, std::size_t&);
    Error Read3Column(const char*&, const char*, std::size_t&);
    Error Read4Column(const char*&, const char*, std::size_t&);

    std::string name;
    std::list<std::unique_ptr<UnitBase>> units;
  };

  // Unknown
  //  - Added Memory Map
  // CodeWarrior for GCN 2.7
  //  - Changed size column for debug sections from "%06x" to "%08x".
  // CodeWarrior for Wii 1.0
  //  - Appended four spaces to left, removed one padding space in middle.
  struct MemoryMap final : PortionBase
  {
    // TODO: make list of names of sections which are not allocated
    // .debug_srcinfo / .debug_sfnames / .debug / .line)
    // Check ELF format SH_TYPE (Section Header) or whatever, I think that is the clue.

    struct UnitNormal
    {
      UnitNormal(std::string name_, std::uint32_t starting_address_, std::uint32_t size_,
                 std::uint32_t file_offset_)
          : name(std::move(name_)), file_offset(file_offset_), size(size_),
            starting_address(starting_address_){};

      UnitNormal(std::string name_, std::uint32_t starting_address_, std::uint32_t size_,
                 std::uint32_t file_offset_, int s_record_line_)
          : name(std::move(name_)), file_offset(file_offset_), size(size_),
            starting_address(starting_address_){};

      UnitNormal(std::string name_, std::uint32_t starting_address_, std::uint32_t size_,
                 std::uint32_t file_offset_, std::uint32_t rom_address_,
                 std::uint32_t ram_buffer_address_)
          : name(std::move(name_)), file_offset(file_offset_), size(size_),
            starting_address(starting_address_), rom_address(rom_address_),
            ram_buffer_address(ram_buffer_address_){};

      UnitNormal(std::string name_, std::uint32_t starting_address_, std::uint32_t size_,
                 std::uint32_t file_offset_, std::uint32_t rom_address_,
                 std::uint32_t ram_buffer_address_, int s_record_line_)
          : name(std::move(name_)), file_offset(file_offset_), size(size_),
            starting_address(starting_address_), rom_address(rom_address_),
            ram_buffer_address(ram_buffer_address_), s_record_line(s_record_line_){};

      UnitNormal(std::string name_, std::uint32_t starting_address_, std::uint32_t size_,
                 std::uint32_t file_offset_, std::uint32_t bin_file_offset_,
                 std::string bin_file_name_)
          : name(std::move(name_)), file_offset(file_offset_), size(size_),
            starting_address(starting_address_), bin_file_offset(bin_file_offset_),
            bin_file_name(std::move(bin_file_name_)){};

      UnitNormal(std::string name_, std::uint32_t starting_address_, std::uint32_t size_,
                 std::uint32_t file_offset_, int s_record_line_, std::uint32_t bin_file_offset_,
                 std::string bin_file_name_)
          : name(std::move(name_)), file_offset(file_offset_), size(size_),
            starting_address(starting_address_), s_record_line(s_record_line_),
            bin_file_offset(bin_file_offset_), bin_file_name(std::move(bin_file_name_)){};

      UnitNormal(std::string name_, std::uint32_t starting_address_, std::uint32_t size_,
                 std::uint32_t file_offset_, std::uint32_t rom_address_,
                 std::uint32_t ram_buffer_address_, std::uint32_t bin_file_offset_,
                 std::string bin_file_name_)
          : name(std::move(name_)), file_offset(file_offset_), size(size_),
            starting_address(starting_address_), rom_address(rom_address_),
            ram_buffer_address(ram_buffer_address_), bin_file_offset(bin_file_offset_),
            bin_file_name(std::move(bin_file_name_)){};

      UnitNormal(std::string name_, std::uint32_t starting_address_, std::uint32_t size_,
                 std::uint32_t file_offset_, std::uint32_t rom_address_,
                 std::uint32_t ram_buffer_address_, int s_record_line_,
                 std::uint32_t bin_file_offset_, std::string bin_file_name_)
          : name(std::move(name_)), file_offset(file_offset_), size(size_),
            starting_address(starting_address_), rom_address(rom_address_),
            ram_buffer_address(ram_buffer_address_), s_record_line(s_record_line_),
            bin_file_offset(bin_file_offset_), bin_file_name(std::move(bin_file_name_)){};

      std::string name;
      std::uint32_t starting_address;
      std::uint32_t size;
      std::uint32_t file_offset;
      std::uint32_t rom_address;
      std::uint32_t ram_buffer_address;
      int s_record_line;
      std::uint32_t bin_file_offset;
      std::string bin_file_name;
    };

    // objdump debug shf_flags
    // Sections which do not, such as '.debug_srcinfo', '.debug_sfnames', '.debug', or '.line'
    // TODO: Confirm this is really the distinction
    struct UnitDebug
    {
      UnitDebug(std::string name_, std::uint32_t size_, std::uint32_t file_offset_)
          : name(std::move(name_)), size(size_), file_offset(file_offset_){};

      std::string name;
      std::uint32_t size;
      std::uint32_t file_offset;
    };

    MemoryMap(bool has_rom_ram_)  // ctor for old memory map
        : has_rom_ram(has_rom_ram_), has_bin_file(false), has_s_record(false){};
    MemoryMap(bool has_rom_ram_, bool has_bin_file_, bool has_s_record_)
        : has_rom_ram(has_rom_ram_), has_bin_file(has_bin_file_), has_s_record(has_s_record_)
    {
      SetMinVersion(MWLinkerVersion::version_4_2_build_142);
    }
    virtual ~MemoryMap() = default;

    virtual bool IsEmpty() override { return normal_units.empty() || debug_units.empty(); }
    Error ReadSimple_old(const char*&, const char*, std::size_t&);
    Error ReadRomRam_old(const char*&, const char*, std::size_t&);
    Error ReadSimple(const char*&, const char*, std::size_t&);
    Error ReadSimpleSRecord(const char*&, const char*, std::size_t&);
    Error ReadRomRam(const char*&, const char*, std::size_t&);
    Error ReadRomRamSRecord(const char*&, const char*, std::size_t&);
    Error ReadBinFile(const char*&, const char*, std::size_t&);
    Error ReadBinFileSRecord(const char*&, const char*, std::size_t&);
    Error ReadRomRamBinFile(const char*&, const char*, std::size_t&);
    Error ReadRomRamBinFileSRecord(const char*&, const char*, std::size_t&);

    std::list<UnitNormal> normal_units;
    std::list<UnitDebug> debug_units;
    const bool has_rom_ram;
    const bool has_bin_file;
    const bool has_s_record;
  };

  struct LinkerGeneratedSymbols final : PortionBase
  {
    struct Unit
    {
      std::string name;
      std::uint32_t value;

      Unit() = default;
      Unit(std::string name_, std::uint32_t value_) : name(std::move(name_)), value(value_){};
    };

    LinkerGeneratedSymbols() = default;
    virtual ~LinkerGeneratedSymbols() = default;

    virtual bool IsEmpty() override { return units.empty(); }
    Error Read(const char*&, const char*, std::size_t&);

    std::list<std::unique_ptr<Unit>> units;
  };

  MWLinkerMap() = default;
  ~MWLinkerMap() = default;

  Error Read(std::istream&, std::size_t&);
  Error Read(const std::string&, std::size_t&);
  Error Read(const char*, const char*, std::size_t&);
  Error ReadMemoryMapPrologue(const char*&, const char*, std::size_t&);

  std::list<std::unique_ptr<PortionBase>> portions;
  std::list<std::string> unresolved_symbols;
};
