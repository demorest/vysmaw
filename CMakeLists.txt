cmake_minimum_required(VERSION 2.8)

project(vysmaw)
set(VYSMAW_VERSION_MAJOR 2)
set(VYSMAW_VERSION_MINOR 0)
set(VYSMAW_VERSION_PATCH 1)
set(VYSMAW_VERSION
  ${VYSMAW_VERSION_MAJOR}.${VYSMAW_VERSION_MINOR}.${VYSMAW_VERSION_PATCH})

set(
  CMAKE_MODULE_PATH
  ${CMAKE_MODULE_PATH}
  ${CMAKE_CURRENT_LIST_DIR}/cmake/Modules)

# set standard compiler flags
set(CMAKE_C_FLAGS
  "${CMAKE_C_FLAGS} -fno-short-enums")
set(CMAKE_C_FLAGS_RELEASE
  "${CMAKE_C_FLAGS_RELEASE} -Ofast")

set(CMAKE_CXX_FLAGS
  "${CMAKE_CXX_FLAGS} -fno-short-enums")
set(CMAKE_CXX_FLAGS_RELEASE
  "${CMAKE_CXX_FLAGS_RELEASE} -Ofast")

add_subdirectory(src)
add_subdirectory(py)
add_subdirectory(examples)
add_subdirectory(vyssim)
