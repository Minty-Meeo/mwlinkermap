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

    LinkMapLayerSkip,
    SectionLayoutBadHeader,
  };

  struct NodeBase
  {
  };

  struct PartBase : std::list<std::unique_ptr<NodeBase>>
  {
    virtual Error ReadLines(std::vector<std::string>&, std::size_t&) = 0;
  };

  MWLinkerMap() = default;

  std::list<std::unique_ptr<PartBase>> m_list;

  Error ReadLines(std::vector<std::string>&, std::size_t&);
  Error ReadStream(std::istream&, std::size_t&);

  struct LinkMap : PartBase
  {
    struct Node : NodeBase
    {
    };
    struct NodeNormal : Node
    {
      std::string name;
      std::string type;
      std::string bind;
      std::string module;
      std::string file;

      NodeNormal() = default;
      NodeNormal(std::string name_, std::string type_, std::string bind_, std::string module_,
                 std::string file_)
          : name(std::move(name_)), type(std::move(type_)), bind(std::move(bind_)),
            module(std::move(module_)), file(std::move(file_))
      {
      }
    };
    struct NodeLinkerGenerated : Node
    {
      std::string name;

      NodeLinkerGenerated() = default;
      NodeLinkerGenerated(std::string name_) : name(std::move(name_)) {}
    };

    virtual Error ReadLines(std::vector<std::string>&, std::size_t&);
  };

  struct SectionLayout : PartBase
  {
    struct Node : NodeBase
    {
    };
    struct NodeNormal : Node
    {
      std::uint32_t saddress;
      std::uint32_t size;
      std::uint32_t vaddress;
      std::uint32_t foffset;
      std::uint32_t alignment;
      std::string name;
      std::string module;  // ELF object or static library name
      std::string file;    // Static library STT_FILE symbol name (optional)

      NodeNormal() = default;
      NodeNormal(std::uint32_t saddress_, std::uint32_t size_, std::uint32_t vaddress_,
                 std::uint32_t foffset_, std::uint32_t alignment_, std::string name_,
                 std::string module_, std::string file_)
          : saddress(saddress_), size(size_), vaddress(vaddress_), foffset(foffset_),
            alignment(alignment_), name(std::move(name_)), module(std::move(module_)),
            file(std::move(file_)){};
    };
    struct NodeUnused : Node
    {
      std::uint32_t size;
      std::string name;
      std::string module;  // ELF object or static library name
      std::string file;    // Static library STT_FILE symbol name (optional)

      NodeUnused() = default;
      NodeUnused(std::uint32_t size_, std::string name_, std::string module_, std::string file_)
          : size(size_), name(std::move(name_)), module(std::move(module_)), file(std::move(file_))
      {
      }
    };
    struct NodeEntry : Node
    {
      std::uint32_t saddress;
      std::uint32_t size;
      std::uint32_t vaddress;
      std::uint32_t foffset;
      std::string name;
      std::string entry_of_name;  // (entry of _____)
      std::string module;         // ELF object or static library name
      std::string file;           // Static library STT_FILE symbol name (optional)

      NodeEntry() = default;
      NodeEntry(std::uint32_t saddress_, std::uint32_t size_, std::uint32_t vaddress_,
                std::uint32_t foffset_, std::string name_, std::string entry_of_name_,
                std::string module_, std::string file_)
          : saddress(saddress_), size(size_), vaddress(vaddress_), foffset(foffset_),
            name(std::move(name_)), entry_of_name(std::move(entry_of_name_)),
            module(std::move(module_)), file(std::move(file_)){};
    };

    virtual Error ReadLines(std::vector<std::string>&, std::size_t&);
    Error ReadLines3Column(std::vector<std::string>&, std::size_t&);
    Error ReadLines4Column(std::vector<std::string>&, std::size_t&);
  };
};