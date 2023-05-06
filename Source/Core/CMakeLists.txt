add_library(lib-mwlinkermap
  MWLinkerMap.cpp
  MWLinkerMap.h
  PrintUtil.h
  RegexUtil.h
)

target_link_libraries(lib-mwlinkermap fmt::fmt)
