add_compile_options(-Wall -Wextra -Wpedantic)

add_executable(mwlinkermap-example
  Future/CppLibFormat.h
  Future/CppLibPrint.h
  main.cpp
  MWLinkerMap.cpp
  MWLinkerMap.h
  RegexUtil.h
)

target_link_libraries(mwlinkermap-example fmt::fmt)
