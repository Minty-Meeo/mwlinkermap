add_executable(lib-mwlinkermap-example
  main.cpp
)

target_link_libraries(lib-mwlinkermap-example lib-mwlinkermap)
target_link_libraries(lib-mwlinkermap-example fmt::fmt)
