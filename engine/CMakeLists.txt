cmake_minimum_required(VERSION 3.16)
project(blitz_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Release optimizations by default
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O3 -march=native -DNDEBUG")

add_executable(blitz_engine
    src/main.cpp
    src/position.cpp
    src/movegen.cpp
    src/evaluation.cpp
    src/search.cpp
    src/engine.cpp
    src/book.cpp
)
