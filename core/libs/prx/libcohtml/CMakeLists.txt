add_library(libcohtml SHARED EXCLUDE_FROM_ALL
    ${CMAKE_CURRENT_SOURCE_DIR}/Export.cpp
)
target_include_directories(libcohtml PRIVATE ${LIBS_INCLUDE_DIR})
set_target_properties(libcohtml PROPERTIES CXX_EXTENSIONS OFF)
configure_windows_unwind(libcohtml)
