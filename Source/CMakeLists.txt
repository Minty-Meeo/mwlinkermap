set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(-Wall -Wextra -Wpedantic)

add_executable(mwlinkermap-example
  Future/CppLibFormat.h
  Future/CppLibPrint.h
  main.cpp
  MWLinkerMap.cpp
  MWLinkerMap.h
)

target_link_libraries(mwlinkermap-example fmt::fmt)
