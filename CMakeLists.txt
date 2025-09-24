cmake_minimum_required(VERSION 3.10)
project(voice_samp)

set(CMAKE_CXX_STANDARD 11)

# Wymuszenie 32-bit build
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -m32")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -m32")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -m32")

add_library(voice_samp SHARED voice_samp.cpp)

if (UNIX)
    target_link_libraries(voice_samp pthread dl)
endif()
