add_library(libRytyNet SHARED EXCLUDE_FROM_ALL
    Export.cpp
    src/RytyNetBridge.cpp
)
target_include_directories(libRytyNet PRIVATE ${LIBS_INCLUDE_DIR})
configure_windows_unwind(libRytyNet)

if(WIN32)
    target_link_libraries(libRytyNet PRIVATE ws2_32)
    target_compile_definitions(libRytyNet PRIVATE _WIN32_WINNT=0x0602)
endif()
