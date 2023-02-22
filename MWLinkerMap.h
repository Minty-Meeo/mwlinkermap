#include <cstddef>
#include <cstdint>
#include <istream>
#include <list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

struct MWLinkerMap
{
  enum class Error
  {
    None,
    Unimplemented,
    RegexFail,
    GarbageFound,

    LinkMapEntryPointNameMismatch,
    LinkMapLayerSkip,
    LinkMapUnrefDupsLevelMismatch,
    LinkMapUnrefDupsBadHeader,
    LinkMapUnrefDupsEmpty,

    SectionLayoutBadHeader,

    MemoryMapBadHeader,
  };

  struct PartBase
  {
    PartBase() = default;
    virtual ~PartBase() = default;
  };

  struct LinkMap final : PartBase
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

      Error ReadLinesUnrefDups(std::vector<std::string>&, std::size_t&, const int);

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

    struct NodeNotFound final : NodeBase
    {
      NodeNotFound(std::string name_) : NodeBase(std::move(name_)){};
      virtual ~NodeNotFound() = default;
    };

    LinkMap() = default;
    virtual ~LinkMap() = default;

    Error ReadLines(std::vector<std::string>&, std::size_t&);

    std::string entry_point_name;
    NodeBase root;
  };

  // TODO: This linker map part apparently existed since as late as 4.2 build 60320 (CodeWarrior for
  // Nintendo GameCube 3.0).  What is it?
  struct BranchIslands final : PartBase
  {
    BranchIslands() = default;
    virtual ~BranchIslands() = default;

    Error ReadLines(std::vector<std::string>&, std::size_t&);
  };

  struct SectionLayout final : PartBase
  {
    enum class Style
    {
      Pre_2_7,
      Post_2_7,
      LoZ_TP,
    };
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
    struct UnitFill final : UnitBase
    {
      UnitFill(std::uint32_t saddress_, std::uint32_t size_, std::uint32_t vaddress_,
               std::uint32_t foffset_, std::uint32_t alignment_)
          : saddress(saddress_), size(size_), vaddress(vaddress_), foffset(foffset_),
            alignment(alignment_){};
      virtual ~UnitFill() = default;

      std::uint32_t saddress;
      std::uint32_t size;
      std::uint32_t vaddress;
      std::uint32_t foffset;
      std::uint32_t alignment;
    };

    SectionLayout() = default;
    virtual ~SectionLayout() = default;

    Error ReadLines(std::vector<std::string>&, std::size_t&, bool = false);
    Error ReadLines3Column(std::vector<std::string>&, std::size_t&);
    Error ReadLines4Column(std::vector<std::string>&, std::size_t&);
    Error ReadLinesLoZTP(std::vector<std::string>&, std::size_t&);

    std::list<std::unique_ptr<UnitBase>> m_units;
    Style style;
    bool m_pre_2_7;
  };

  struct MemoryMap final : PartBase
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

    Error ReadLines(std::vector<std::string>&, std::size_t&);
    Error ReadLines3Column(std::vector<std::string>&, std::size_t&);
    Error ReadLines5Column(std::vector<std::string>&, std::size_t&);

    std::list<std::unique_ptr<UnitBase>> m_units;
    bool m_extra_info;  // TODO: What causes MWLD(EPPC) to emit this??
  };

  struct LinkerGeneratedSymbols final : PartBase
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

    Error ReadLines(std::vector<std::string>&, std::size_t&);

    std::list<std::unique_ptr<Unit>> m_units;
  };

  MWLinkerMap() = default;
  ~MWLinkerMap() = default;

  Error ReadLines(std::vector<std::string>&, std::size_t&);
  Error ReadStream(std::istream&, std::size_t&);

  std::list<std::unique_ptr<PartBase>> m_parts;
  bool m_null_padding = false;
};
