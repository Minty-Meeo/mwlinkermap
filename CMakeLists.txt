cmake_minimum_required(VERSION 3.19)

project(mwlinkermap)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING
      "Build type (Release/Debug/RelWithDebInfo/MinSizeRel)" FORCE)
endif()

find_package(fmt 10)
if(fmt_FOUND)
  message(STATUS "Using shared fmt ${fmt_VERSION}")
else()
  message(STATUS "Using static fmt from Externals")
  add_subdirectory(Externals/fmt EXCLUDE_FROM_ALL)
endif()

include_directories(Source/Core)
add_subdirectory(Source)
