cmake_minimum_required(VERSION 3.10)
project(voice_samp)

set(CMAKE_CXX_STANDARD 11)

add_library(voice_samp SHARED voice_samp.cpp)

target_link_libraries(voice_samp pthread dl)
