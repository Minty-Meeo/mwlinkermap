add_compile_options(-Wall -Wextra -Wpedantic)

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-stdlib=libc++ -fexperimental-library)
endif()

add_executable(mwlinkermap-example
  Future/CppLibFormat.h
  Future/CppLibPrint.h
  main.cpp
  MWLinkerMap.cpp
  MWLinkerMap.h
  RegexUtil.h
)

target_link_libraries(mwlinkermap-example fmt::fmt)
