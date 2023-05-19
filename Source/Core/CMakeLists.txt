add_library(mwlinkermap
  MWLinkerMap.cpp
  MWLinkerMap.h
  PrintUtil.h
  RegexUtil.h
)

target_link_libraries(mwlinkermap fmt::fmt)
