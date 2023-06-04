add_executable(mwlinkermap-example
  main.cpp
)

target_link_libraries(mwlinkermap-example PRIVATE mwlinkermap)
target_link_libraries(mwlinkermap-example PRIVATE fmt::fmt)
