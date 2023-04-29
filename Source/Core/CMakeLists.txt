add_compile_options(-Wall -Wextra -Wpedantic)

add_executable(mwlinkermap-example
  main.cpp
  MWLinkerMap.cpp
  MWLinkerMap.h
  PrintUtil.h
  RegexUtil.h
)

target_link_libraries(mwlinkermap-example fmt::fmt)
