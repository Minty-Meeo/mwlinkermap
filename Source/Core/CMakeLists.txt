add_library(mwlinkermap
  MWLinkerMap.cpp
  MWLinkerMap.h
  PointerUtil.h
  RegexUtil.h
)

target_link_libraries(mwlinkermap PRIVATE fmt::fmt)
