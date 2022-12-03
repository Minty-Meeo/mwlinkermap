#include <cstddef>
#include <cstdint>
#include <istream>
#include <list>
#include <memory>
#include <string>
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
      NodeBase() = default;
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
        std::string type;
        std::string bind;
        std::string module;
        std::string file;

        UnreferencedDuplicate() = default;
        UnreferencedDuplicate(std::string type_, std::string bind_, std::string module_,
                              std::string file_)
            : type(std::move(type_)), bind(std::move(bind_)), module(std::move(module_)),
              file(std::move(file_)){};
      };

      NodeNormal() = default;
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
      NodeLinkerGenerated() = default;
      NodeLinkerGenerated(std::string name_) : NodeBase(std::move(name_)){};
      virtual ~NodeLinkerGenerated() = default;
    };

    LinkMap() = default;
    virtual ~LinkMap() = default;

    Error ReadLines(std::vector<std::string>&, std::size_t&);

    NodeBase root;
  };

  struct SectionLayout final : PartBase
  {
    struct UnitBase
    {
      std::string name;
      std::string module;  // ELF object or static library name
      std::string file;    // Static library STT_FILE symbol name (optional)
      std::uint32_t size;

      UnitBase() = default;
      UnitBase(std::string name_, std::string module_, std::string file_, std::uint32_t size_)
          : name(std::move(name_)), module(std::move(module_)), file(std::move(file_)),
            size(size_){};
      virtual ~UnitBase() = default;
    };
    struct UnitNormal final : UnitBase
    {
      std::uint32_t saddress;
      std::uint32_t vaddress;
      std::uint32_t foffset;
      std::uint32_t alignment;

      UnitNormal() = default;
      UnitNormal(std::string name_, std::string module_, std::string file_, std::uint32_t size_,
                 std::uint32_t saddress_, std::uint32_t vaddress_, std::uint32_t foffset_,
                 std::uint32_t alignment_)
          : UnitBase(std::move(name_), std::move(module_), std::move(file_), size_),
            saddress(saddress_), vaddress(vaddress_), foffset(foffset_), alignment(alignment_){};
      virtual ~UnitNormal() = default;
    };
    struct UnitUnused final : UnitBase
    {
      UnitUnused() = default;
      UnitUnused(std::string name_, std::string module_, std::string file_, std::uint32_t size_)
          : UnitBase(std::move(name_), std::move(module_), std::move(file_), size_){};
      virtual ~UnitUnused() = default;
    };
    struct UnitEntry final : UnitBase
    {
      std::uint32_t saddress;
      std::uint32_t vaddress;
      std::uint32_t foffset;
      std::string entry_of_name;  // (entry of _____)

      UnitEntry() = default;
      UnitEntry(std::string name_, std::string module_, std::string file_, std::uint32_t size_,
                std::uint32_t saddress_, std::uint32_t vaddress_, std::uint32_t foffset_,
                std::string entry_of_name_)
          : UnitBase(std::move(name_), std::move(module_), std::move(file_), size_),
            saddress(saddress_), vaddress(vaddress_), foffset(foffset_),
            entry_of_name(std::move(entry_of_name_)){};
      virtual ~UnitEntry() = default;
    };

    SectionLayout() = default;
    virtual ~SectionLayout() = default;

    Error ReadLines(std::vector<std::string>&, std::size_t&);
    Error ReadLines3Column(std::vector<std::string>&, std::size_t&);
    Error ReadLines4Column(std::vector<std::string>&, std::size_t&);

    std::list<std::unique_ptr<UnitBase>> units;
  };

  struct MemoryMap final : PartBase
  {
    // "                   Starting Size     File     ROM      RAM Buffer\r\n"
    // "                   address           Offset   Address  Address\r\n"
    // "  %15s  %08x %08x %08x %08x %08x\r\n"

    // "                   Starting Size     File\r\n"
    // "                   address           Offset\r\n"
    // "  %15s  %08x %08x %08x\r\n"
    // "  %15s           %06x %08x\r\n"

    // TODO: make list of names of sections which are not allocated
    // .debug_srcinfo / .debug_sfnames / .debug / .line)
    // Check ELF format SH_TYPE (Section Header) or whatever, I think that is the clue.

    struct UnitBase
    {
      UnitBase() = default;
      UnitBase(std::string section_name_, std::uint32_t size_, std::uint32_t foffset_)
          : section_name(section_name_), size(size_), foffset(foffset_){};
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
          : UnitBase(section_name_, size_, foffset_), saddress(saddress_), rom_addr(rom_addr_),
            ram_buff_addr(ram_buff_addr_){};
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
          : UnitBase(section_name_, size_, foffset_){};
      virtual ~UnitInfo() = default;
    };

    MemoryMap() = default;
    virtual ~MemoryMap() = default;

    Error ReadLines(std::vector<std::string>&, std::size_t&);
    Error ReadLines3Column(std::vector<std::string>&, std::size_t&);
    Error ReadLines5Column(std::vector<std::string>&, std::size_t&);

    std::list<std::unique_ptr<UnitBase>> units;
  };

  MWLinkerMap() = default;

  std::list<std::unique_ptr<PartBase>> m_list;

  Error ReadLines(std::vector<std::string>&, std::size_t&);
  Error ReadStream(std::istream&, std::size_t&);
};