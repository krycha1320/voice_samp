cmake_minimum_required(VERSION 3.10)
project(voice_samp)

set(CMAKE_CXX_STANDARD 17)

add_library(voice_samp SHARED voice_samp.cpp)

# wymuszenie 32-bit
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -m32 -O2 -fPIC")
