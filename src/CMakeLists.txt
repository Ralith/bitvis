include_directories(${GTK_INCLUDE_DIRS})
link_directories(${GTK_LIBRARY_DIRS})

add_executable(bitvis
  bitvis.c
  )

target_link_libraries(bitvis ${GTK_LIBRARIES})

install(TARGETS bitvis DESTINATION bin)
