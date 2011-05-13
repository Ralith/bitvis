project(bitvis)
cmake_minimum_required(VERSION 2.6)

set(CPACK_PACKAGE_NAME "bitvis")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Bitcoin market visualizer")
set(CPACK_PACKAGE_VENDOR "Benjamin Saunders")
set(CPACK_PACKAGE_CONTACT "Benjamin Saunders <ralith@gmail.com>")
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "i386")
SET(CPACK_DEBIAN_PACKAGE_DEPENDS "libgtk-3-0 (>= 3.0.1), libjson-glib-1.0-0 (>= 0.12.0)")
#set(CPACK_PACKAGE_DESCRIPTION_FILE "${CMAKE_CURRENT_SOURCE_DIR}/README")
#set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_VERSION_MAJOR "0")
set(CPACK_PACKAGE_VERSION_MINOR "1")
set(CPACK_PACKAGE_VERSION_PATCH "0")
set(CPACK_GENERATOR "TGZ")
include(CPack)

add_definitions(-Wall -Wextra)

find_package(PkgConfig)

pkg_check_modules(GTK REQUIRED gtk+-3.0)
pkg_check_modules(JSONGLIB REQUIRED json-glib-1.0)

add_subdirectory(src)
add_subdirectory(share)
