cmake_minimum_required(VERSION 2.6)
project(fon9)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Release")
endif()

set(CXX_FLAGS
 -g
 -Wall
 -Wextra
 -Wconversion
 -Wold-style-cast
 -Woverloaded-virtual
 -Wpointer-arith
 -Wshadow
 -Wwrite-strings
 -fexec-charset=UTF-8
 -std=c++11
 ${FON9_CXX_FLAGS}
 )
string(REPLACE ";" " " CMAKE_CXX_FLAGS "${CXX_FLAGS}")

set(C_FLAGS
 -g
 -Wall
 -Wextra
 -Wconversion
 -Wpointer-arith
 -Wshadow
 -Wwrite-strings
 -fexec-charset=UTF-8
 ${FON9_C_FLAGS}
 )
string(REPLACE ";" " " CMAKE_C_FLAGS "${C_FLAGS}")
# ---
include(AddF9card.cmake)
# ---
include_directories(.)

add_subdirectory(fon9)

if(CMAKE_FON9_BUILD_UNIT_TEST)
   add_subdirectory(f9extests)
endif()

add_subdirectory(f9tws)
add_subdirectory(f9twf)
add_subdirectory(f9set)
