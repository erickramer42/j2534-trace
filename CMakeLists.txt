cmake_minimum_required(VERSION 3.20)
project(j2534-trace VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS OFF)
# Static CRT (/MT): the proxy must load on machines without the VC++
# redistributable. The real OBDX driver is a .NET assembly (imports only
# mscoree.dll) and needs no redist, so a /MD proxy that depends on
# VCRUNTIME140/MSVCP140 fails to load where the real one succeeds (the
# host app reports "module missing / wrong version"). Static linking
# removes that external dependency.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")

add_subdirectory(proxy)
if(MSVC)
    add_subdirectory(test)
endif()
