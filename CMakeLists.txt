cmake_minimum_required(VERSION 3.20)
project(VectorForge VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/CompilerWarnings.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Sanitizers.cmake)

enable_testing()
add_subdirectory(backend)
