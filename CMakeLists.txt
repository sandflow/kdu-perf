cmake_minimum_required (VERSION 3.2)

project(kdu-perf)

set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O2")

# Find threading library

find_package(Threads REQUIRED)

# Kakadu SDK support

set(KDU_INCLUDE_DIR CACHE PATH "Path to the Kakadu SDK include directory, e.g., managed/all_includes")
set(KDU_LIBRARY CACHE PATH "Path to the Kakadu library, e.g., libkdu_v84R.so")
set(KDU_AUX_LIBRARY CACHE PATH "Path to the Kakadu SDK auxiliary library, e.g., libkdu_a84R.so")

if(NOT (KDU_INCLUDE_DIR AND KDU_LIBRARY AND KDU_AUX_LIBRARY))
    message(FATAL_ERROR "You must set KDU_INCLUDE_DIR, KDU_LIBRARY and KDU_AUX_LIBRARY")
endif()

include_directories(${KDU_INCLUDE_DIR})

# cxxopts
set(CMAKE_CXX_STANDARD 11)
include_directories(ext/cxxopts/include)

# main executable

file(GLOB SRC_FILES src/main/cpp/*)
add_executable(kdu_perf ${SRC_FILES})
target_link_libraries(kdu_perf ${KDU_LIBRARY} ${KDU_AUX_LIBRARY} ${CMAKE_DL_LIBS} Threads::Threads)

# tests

enable_testing()

add_test(NAME "ht" COMMAND kdu_perf -r 200 ${PROJECT_SOURCE_DIR}/src/test/resources/codestreams/ht.j2c)

