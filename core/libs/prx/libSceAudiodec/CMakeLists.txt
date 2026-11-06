add_library(libSceAudiodec SHARED EXCLUDE_FROM_ALL
        ${CMAKE_CURRENT_SOURCE_DIR}/Export.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Codecs.cpp
)
target_include_directories(libSceAudiodec PRIVATE
        ${LIBS_INCLUDE_DIR}
        ${CMAKE_SOURCE_DIR}/3rdparty/LibAtrac9/C/src
)
target_link_libraries(libSceAudiodec PRIVATE atrac9 ffmpeg libc libkernel)
set_target_properties(libSceAudiodec PROPERTIES CXX_EXTENSIONS OFF)
configure_windows_unwind(libSceAudiodec)

if(MINGW)
    foreach(runtimeLinkFlag IN ITEMS -static-libgcc -static-libstdc++ -static)
        string(REPLACE "${runtimeLinkFlag}" "" CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")
    endforeach()
endif()

if(BUILD_TESTING)
    add_test_executable(guest_audiodec_tests ${LIBS_INCLUDE_DIR}/tests/GuestAudiodec.cpp)
    target_include_directories(guest_audiodec_tests PRIVATE ${LIBS_INCLUDE_DIR})
    target_link_libraries(guest_audiodec_tests PRIVATE libSceAudiodec libc)
    configure_windows_unwind(guest_audiodec_tests)
    add_test(NAME guest_audiodec COMMAND guest_audiodec_tests)
    if(WIN32)
        set_tests_properties(guest_audiodec PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:libSceAudiodec>")
    endif()
endif()
