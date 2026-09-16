cmake_minimum_required(VERSION 3.20)
project(j2534-trace VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")

add_subdirectory(proxy)
if(MSVC)
    add_subdirectory(test)
endif()
