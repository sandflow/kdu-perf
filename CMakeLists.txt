cmake_minimum_required (VERSION 3.2)

project(kdu-perf)

set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O2")

# Find threading library

find_package(Threads REQUIRED)

# Kakadu SDK support

find_path(KDU_INCLUDE_DIR kdu_args.h PATH_SUFFIXES kakadu)
find_library(KDU_LIBRARY NAMES kdu)
find_library(KDU_AUX_LIBRARY NAMES kdu_aux)

if(KDU_INCLUDE_DIR AND KDU_LIBRARY AND KDU_AUX_LIBRARY)
  set(KDU_PRESENT 1)
  message("Kakadu SDK found: ${KDU_LIBRARY} and ${KDU_AUX_LIBRARY} and ${KDU_INCLUDE_DIR}")
  include_directories(${KDU_INCLUDE_DIR})
else()
  message("Kakadu SDK not found: ${KDU_LIBRARY} and ${KDU_AUX_LIBRARY} and ${KDU_INCLUDE_DIR}.")
endif()

# cxxopts

include_directories(ext/cxxopts/include)

# main executable

file(GLOB SRC_FILES src/main/cpp/*)
add_executable(kdu_perf ${SRC_FILES})
target_link_libraries(kdu_perf ${KDU_LIBRARY} ${KDU_AUX_LIBRARY} ${CMAKE_DL_LIBS} Threads::Threads)

# tests

enable_testing()

add_test(NAME "ht" COMMAND kdu_perf -r 200 ${PROJECT_SOURCE_DIR}/src/test/resources/codestreams/ht.j2c)

