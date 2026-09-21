# --- Masquerade resource metadata -------------------------------------------
# resource_values.local.rc is gitignored and holds vendor-mimicking version
# info so it never lands in the repo. Absent file => neutral defaults.
set(PROXY_RC_LOCAL "${CMAKE_CURRENT_SOURCE_DIR}/resource_values.local.rc")
if(EXISTS "${PROXY_RC_LOCAL}")
    set(RESOURCE_LOCAL_INCLUDE "#include \"${PROXY_RC_LOCAL}\"")
    message(STATUS "proxy: using local masquerade resource values")
else()
    set(RESOURCE_LOCAL_INCLUDE "")
    message(STATUS "proxy: no resource_values.local.rc, building neutral version info")
endif()

configure_file(resource.rc.in "${CMAKE_CURRENT_BINARY_DIR}/resource.rc" @ONLY)

add_library(proxy_dll SHARED
    proxy.cpp
    logger.cpp
    logger.h
    j2534_types.h
    exports.def
    "${CMAKE_CURRENT_BINARY_DIR}/resource.rc"
)

target_include_directories(proxy_dll PUBLIC ".")

if(MSVC)
    target_compile_options(proxy_dll PRIVATE /W4)
endif()

set_target_properties(proxy_dll PROPERTIES
    OUTPUT_NAME "trace"
    PREFIX ""
    SUFFIX ".dll"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/proxy"
)

# Copy to test/ as vendor_J2534.dll for rig testing
add_custom_command(TARGET proxy_dll POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:proxy_dll>"
        "${CMAKE_SOURCE_DIR}/test/vendor_J2534.dll"
    COMMENT "Copying trace.dll to test/ for rig")
