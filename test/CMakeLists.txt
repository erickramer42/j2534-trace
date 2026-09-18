# Build the mock driver DLL
add_library(mock_driver SHARED 
    mock_driver/mock_driver.cpp
    mock_driver/exports.def
)

target_include_directories(mock_driver PRIVATE "${CMAKE_SOURCE_DIR}/proxy")

set_target_properties(mock_driver PROPERTIES
    OUTPUT_NAME "vendor_J2534_orig"
    PREFIX ""
    SUFFIX ".dll"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_SOURCE_DIR}/test"
)

# Create trace.ini if missing
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/test/trace.ini")
    file(WRITE "${CMAKE_SOURCE_DIR}/test/trace.ini" "[trace]\nenabled=1\ndll_suffix=_orig\n")
endif()
