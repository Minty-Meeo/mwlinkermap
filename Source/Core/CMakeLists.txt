if (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Weverything -Wno-c++98-compat -Wno-c++98-compat-pedantic -Wno-global-constructors -Wno-exit-time-destructors -Wno-switch-enum -Wno-weak-vtables -Wno-padded)
endif()

add_executable(mwlinkermap-example
  main.cpp
  MWLinkerMap.cpp
  MWLinkerMap.h
  PrintUtil.h
  RegexUtil.h
)

target_link_libraries(mwlinkermap-example fmt::fmt)
