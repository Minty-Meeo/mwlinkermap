set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
  add_compile_options(-Wall -Wextra -Wpedantic -Wconversion)
endif()

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-Weverything -Wno-c++98-compat -Wno-c++98-compat-pedantic -Wno-global-constructors -Wno-exit-time-destructors -Wno-switch-enum -Wno-weak-vtables -Wno-padded)
endif()

add_subdirectory(Core)
add_subdirectory(Example)

