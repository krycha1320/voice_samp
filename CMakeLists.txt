cmake_minimum_required(VERSION 3.10)
project(voice_samp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# kompilacja 32-bit
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -m32")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -m32")

add_library(voice_samp SHARED voice_samp.cpp)

# nazwa wyjściowa .so
set_target_properties(voice_samp PROPERTIES
    OUTPUT_NAME "voice_samp"
    PREFIX ""
    SUFFIX ".so"
)
