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

  struct SectionLayout : PartBase
  {
    struct Node : NodeBase
    {
      virtual Error ReadLine3Column(std::string& line) = 0;
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

      virtual Error ReadLine3Column(std::string& line);
    };
    struct NodeUnused : Node
    {
      std::uint32_t size;
      std::string name;
      std::string module;  // ELF object or static library name
      std::string file;    // Static library STT_FILE symbol name (optional)

      virtual Error ReadLine3Column(std::string& line);
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

      virtual Error ReadLine3Column(std::string& line);
    };

    virtual Error ReadLines(std::vector<std::string>&, std::size_t&);
    Error ReadLines3Column(std::vector<std::string>&, std::size_t&);
    Error ReadLine3Column(std::string&);
    Error ReadLine3ColumnUnused(std::string&);
    Error ReadLine3ColumnEntry(std::string&);
    Error ReadLines4Column(std::vector<std::string>&, std::size_t&);
  };
};