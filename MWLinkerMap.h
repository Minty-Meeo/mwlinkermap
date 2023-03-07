#include <algorithm>
#include <cstddef>
#include <istream>
#include <list>
#include <memory>
#include <string>

enum class MWLinkerVersion
{
  Unknown,

  // Metrowerks, Inc
  version_2_3_3_build_126,  // Codewarrior for GCN 1.0   (May 21 2000 19:00:24)
  version_2_3_3_build_137,  // CodeWarrior for GCN 1.1   (Feb  7 2001 12:15:53)
  version_2_4_1_build_47,   // CodeWarrior for GCN 1.2.5 (Jun 12 2001 11:53:24)

  // Metrowerks Corporation
  version_2_4_2_build_81,   // CodeWarrior for GCN 1.3.2 (May  7 2002 23:43:34)
  version_2_4_7_build_92,   // CodeWarrior for GCN 2.0   (Sep 16 2002 15:15:26)
  version_2_4_7_build_102,  // CodeWarrior for GCN 2.5   (Nov  7 2002 12:45:57)
  version_2_4_7_build_107,  // CodeWarrior for GCN 2.6   (Jul 14 2003 14:20:31)
  version_3_0_4,            // CodeWarrior for GCN 2.7   (Aug 13 2004 10:40:59)
  version_4_1_build_51213,  // CodeWarrior for GCN 3.0a3 (Dec 13 2005 17:41:17)

  // Freescale Semiconductor, Inc
  version_4_2_build_60320,  // CodeWarrior for GCN 3.0   (Mar 20 2006 23:19:16)
  version_4_2_build_142,    // CodeWarrior for Wii 1.0   (Aug 26 2008 02:33:56)
  version_4_3_build_151,    // CodeWarrior for Wii 1.1   (Apr  2 2009 15:05:36)
  version_4_3_build_172,    // CodeWarrior for Wii 1.3   (Apr 23 2010 11:39:30)
  version_4_3_build_213,    // CodeWarrior for Wii 1.7   (Sep  5 2011 13:02:03)
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

    MWLinkerVersion min_version = MWLinkerVersion::Unknown;
  };

  struct EntryPoint final : PortionBase
  {
    EntryPoint(std::string name) : entry_point_name(name) {}
    ~EntryPoint() = default;

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

      Error Read(std::string::const_iterator&, const std::string::const_iterator, std::size_t&);

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

      Error Read(std::string::const_iterator&, std::string::const_iterator, unsigned long,
                 std::size_t&);
      Error ReadUnrefDups(std::string::const_iterator&, std::string::const_iterator, unsigned long,
                          std::size_t&);

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

      Error Read(std::string::const_iterator&, std::string::const_iterator, unsigned long,
                 std::size_t&);
    };

    SymbolClosure() { min_version = MWLinkerVersion::version_2_3_3_build_126; };
    virtual ~SymbolClosure() = default;

    Error Read(std::string::const_iterator&, std::string::const_iterator, std::list<std::string>&,
               std::size_t&);
    Error Read2(std::string::const_iterator&, const std::string::const_iterator, NodeBase*, int,
                std::size_t&);
    Error Read3(std::string::const_iterator&, const std::string::const_iterator,
                std::list<std::string>&, std::size_t&);

    NodeBase root;
  };

  // CodeWarrior for Wii 1.0
  //  - Added EPPC_PatternMatching
  struct EPPC_PatternMatching final : PortionBase
  {
    EPPC_PatternMatching() { this->min_version = MWLinkerVersion::version_4_2_build_142; };
    virtual ~EPPC_PatternMatching() = default;

    Error Read(std::string::const_iterator&, std::string::const_iterator, std::size_t&);
    Error ReadSummary(std::string::const_iterator&, std::string::const_iterator, std::size_t&);
  };

  // CodeWarrior for Wii 1.0
  //  - Added LinkerOpts
  struct LinkerOpts final : PortionBase
  {
    LinkerOpts() { this->min_version = MWLinkerVersion::version_4_2_build_142; };
    virtual ~LinkerOpts() = default;

    Error Read(std::string::const_iterator&, std::string::const_iterator, std::size_t&);
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

    Error Read(std::string::const_iterator&, std::string::const_iterator, std::size_t&);
    Error Read3Column(std::string::const_iterator&, std::string::const_iterator, std::size_t&);
    Error Read4Column(std::string::const_iterator&, std::string::const_iterator, std::size_t&);

    std::string name;
    std::list<std::unique_ptr<UnitBase>> units;
  };

  // CodeWarrior for Wii 1.0
  //  - Added four spaces to left, removed one padding space in middle.
  struct MemoryMap final : PortionBase
  {
    // TODO: make list of names of sections which are not allocated
    // .debug_srcinfo / .debug_sfnames / .debug / .line)
    // Check ELF format SH_TYPE (Section Header) or whatever, I think that is the clue.

    struct UnitBase
    {
      UnitBase() = default;
      UnitBase(std::string section_name_, std::uint32_t size_, std::uint32_t foffset_)
          : section_name(std::move(section_name_)), size(size_), foffset(foffset_){};
      virtual ~UnitBase() = default;

      std::string section_name;
      std::uint32_t size;
      std::uint32_t foffset;
    };

    // Sections which have Section Header Flag "Alloc" enabled
    struct UnitAllocated final : UnitBase
    {
      UnitAllocated() = default;
      UnitAllocated(std::string section_name_, std::uint32_t size_, std::uint32_t foffset_,
                    std::uint32_t saddress_, std::uint32_t rom_addr_, std::uint32_t ram_buff_addr_)
          : UnitBase(std::move(section_name_), size_, foffset_), saddress(saddress_),
            rom_addr(rom_addr_), ram_buff_addr(ram_buff_addr_){};
      virtual ~UnitAllocated() = default;

      std::uint32_t saddress;
      std::uint32_t rom_addr;
      std::uint32_t ram_buff_addr;
    };

    // Sections which do not, such as '.debug_srcinfo', '.debug_sfnames', '.debug', or '.line'
    // TODO: Confirm this is really the distinction
    struct UnitInfo final : UnitBase
    {
      UnitInfo() = default;
      UnitInfo(std::string section_name_, std::uint32_t size_, std::uint32_t foffset_)
          : UnitBase(std::move(section_name_), size_, foffset_){};
      virtual ~UnitInfo() = default;
    };

    MemoryMap() = default;
    virtual ~MemoryMap() = default;

    Error Read(std::string::const_iterator&, std::string::const_iterator, std::size_t&);
    Error Read3ColumnA(std::string::const_iterator&, std::string::const_iterator, std::size_t&);
    Error Read3ColumnB(std::string::const_iterator&, std::string::const_iterator, std::size_t&);
    Error Read5Column(std::string::const_iterator&, std::string::const_iterator, std::size_t&);

    std::list<std::unique_ptr<UnitBase>> units;
    bool extra_info;  // TODO: What causes MWLD(EPPC) to emit this??
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

    Error Read(std::string::const_iterator&, std::string::const_iterator, std::size_t&);

    std::list<std::unique_ptr<Unit>> units;
  };

  MWLinkerMap() = default;
  ~MWLinkerMap() = default;

  Error Read(std::istream&, std::size_t&);
  Error Read(const std::string&, std::size_t&);
  Error Read(std::string::const_iterator, std::string::const_iterator, std::size_t&);

  std::list<std::unique_ptr<PortionBase>> portions;
  std::list<std::string> unresolved_symbols;
};
