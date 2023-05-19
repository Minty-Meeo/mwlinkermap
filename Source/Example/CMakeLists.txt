add_executable(mwlinkermap-example
  main.cpp
)

target_link_libraries(mwlinkermap-example mwlinkermap)
target_link_libraries(mwlinkermap-example fmt::fmt)
