include_directories(${GTK_INCLUDE_DIRS} ${JSONGLIB_INCLUDE_DIRS})
link_directories(${GTK_LIBRARY_DIRS} ${JSONGLIB_LIBRARY_DIRS})

add_executable(bitvis
  net.c
  bitvis.c
  )

target_link_libraries(bitvis ${GTK_LIBRARIES} ${JSONGLIB_LIBRARIES})

install(TARGETS bitvis DESTINATION bin)
