cmake_minimum_required(VERSION 3.16)
project(blitz_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(blitz_engine
    src/main.cpp
    src/position.cpp
    src/movegen.cpp
    src/evaluation.cpp
    src/search.cpp
    src/engine.cpp
)
