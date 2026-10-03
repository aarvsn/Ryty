if(NOT WIN32)
    string(REPLACE "-static " "" CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ")
    string(STRIP "${CMAKE_EXE_LINKER_FLAGS}" CMAKE_EXE_LINKER_FLAGS)
endif()

add_executable(nid_patcher
        ${CMAKE_CURRENT_SOURCE_DIR}/nid/src/Sha1.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/nid/src/NidCompute.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/nid/src/NidResolver.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/nid/src/ExportExclusions.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/nid/src/ElfNidPatcher.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/nid/src/PeNidPatcher.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/nid/src/BinaryPatcherFactory.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/nid/src/main.cpp
)

target_include_directories(nid_patcher PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/nid/include)
target_compile_features(nid_patcher PRIVATE cxx_std_20)

if(MINGW)
    target_link_options(nid_patcher PRIVATE -static -static-libgcc -static-libstdc++)
    add_compile_options(-fno-asynchronous-unwind-tables)
endif()

function(configure_windows_unwind target)
    if(MINGW)
        if(NOT EXISTS "${CMAKE_OBJCOPY}")
            message(FATAL_ERROR "Windows DWARF unwinding requires objcopy")
        endif()
        add_custom_command(TARGET ${target} POST_BUILD
                COMMAND "${CMAKE_OBJCOPY}" --rename-section .eh_frame=.ehfram "$<TARGET_FILE:${target}>"
                VERBATIM
        )
    endif()
endfunction()

set(LIBC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/prx/libc)
add_library(libc SHARED EXCLUDE_FROM_ALL)
set(LIBS_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
add_subdirectory(${LIBC_DIR} ${CMAKE_CURRENT_BINARY_DIR}/prx/libc)
target_include_directories(libc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
configure_windows_unwind(libc)

set(UNPATCHED_DIR ${CMAKE_CURRENT_BINARY_DIR}/libs/unpatched)
set(PATCHED_DIR ${CMAKE_CURRENT_BINARY_DIR}/libs)
set(IMPLIB_DIR ${PATCHED_DIR}/implib)

set_target_properties(libc PROPERTIES
        PREFIX ""
        SUFFIX .prx
        RUNTIME_OUTPUT_DIRECTORY ${UNPATCHED_DIR}
        LIBRARY_OUTPUT_DIRECTORY ${UNPATCHED_DIR}
        ARCHIVE_OUTPUT_DIRECTORY ${IMPLIB_DIR}
)

set(LIBC_UNPATCHED ${UNPATCHED_DIR}/libc.prx)
set(LIBC_PATCHED ${PATCHED_DIR}/libc.prx)

add_custom_command(
        OUTPUT ${LIBC_PATCHED}
        COMMAND ${CMAKE_COMMAND} -E copy ${LIBC_UNPATCHED} ${LIBC_PATCHED}
        COMMAND $<TARGET_FILE:nid_patcher> libc ${LIBC_PATCHED}
        DEPENDS nid_patcher libc ${LIBC_UNPATCHED}
        COMMENT "Patching NIDs for libc"
)

file(GLOB LIBS_DIRS LIST_DIRECTORIES true ${CMAKE_CURRENT_SOURCE_DIR}/prx/*)
list(REMOVE_ITEM LIBS_DIRS ${LIBC_DIR})

set(PATCHED_OUTPUTS ${LIBC_PATCHED})

foreach(LIB_DIR ${LIBS_DIRS})
    if(IS_DIRECTORY ${LIB_DIR})
        get_filename_component(LIB_NAME ${LIB_DIR} NAME)

        if(EXISTS ${LIB_DIR}/CMakeLists.txt)
            set(LIBS_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
            add_subdirectory(${LIB_DIR} ${CMAKE_CURRENT_BINARY_DIR}/prx/${LIB_NAME})
        else()
            add_library(${LIB_NAME} SHARED EXCLUDE_FROM_ALL ${LIB_DIR}/Export.cpp)
            target_include_directories(${LIB_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
            configure_windows_unwind(${LIB_NAME})
        endif()

        if(LIB_NAME STREQUAL "libkernel")
            target_link_libraries(${LIB_NAME} PRIVATE libc)
        else()
            target_link_libraries(${LIB_NAME} PRIVATE libkernel libc)
        endif()
        if(WIN32 AND LIB_NAME STREQUAL "libSceNet")
            target_link_libraries(${LIB_NAME} PRIVATE ws2_32)
            target_compile_definitions(${LIB_NAME} PRIVATE _WIN32_WINNT=0x0602)
        elseif(WIN32 AND LIB_NAME STREQUAL "libSceNetCtl")
            target_link_libraries(${LIB_NAME} PRIVATE ws2_32 iphlpapi)
            target_compile_definitions(${LIB_NAME} PRIVATE _WIN32_WINNT=0x0602)
        endif()

        set_target_properties(${LIB_NAME} PROPERTIES
                PREFIX ""
                SUFFIX .prx
                RUNTIME_OUTPUT_DIRECTORY ${UNPATCHED_DIR}
                LIBRARY_OUTPUT_DIRECTORY ${UNPATCHED_DIR}
                ARCHIVE_OUTPUT_DIRECTORY ${IMPLIB_DIR}
        )

        set(UNPATCHED_FILE ${UNPATCHED_DIR}/${LIB_NAME}.prx)
        set(PATCHED_FILE ${PATCHED_DIR}/${LIB_NAME}.prx)

        add_custom_command(
                OUTPUT ${PATCHED_FILE}
                COMMAND ${CMAKE_COMMAND} -E copy ${UNPATCHED_FILE} ${PATCHED_FILE}
                COMMAND $<TARGET_FILE:nid_patcher> ${LIB_NAME} --preserve-exports "${LIBC_UNPATCHED}" "${PATCHED_FILE}"
                DEPENDS nid_patcher ${LIB_NAME} ${UNPATCHED_FILE} ${LIBC_UNPATCHED}
                COMMENT "Patching NIDs for ${LIB_NAME}"
        )

        list(APPEND PATCHED_OUTPUTS ${PATCHED_FILE})
    endif()
endforeach()

set(LIBCOHTML_PATCHED ${PATCHED_DIR}/libcohtml.prx)
set(LIBCOHTML_PROSPERO_PATCHED ${PATCHED_DIR}/libcohtml.Prospero.prx)
add_custom_command(
        OUTPUT ${LIBCOHTML_PROSPERO_PATCHED}
        COMMAND ${CMAKE_COMMAND} -E copy "${LIBCOHTML_PATCHED}" "${LIBCOHTML_PROSPERO_PATCHED}"
        DEPENDS ${LIBCOHTML_PATCHED}
        VERBATIM
)
add_custom_target(libcohtml_prospero DEPENDS ${LIBCOHTML_PROSPERO_PATCHED})
list(APPEND PATCHED_OUTPUTS ${LIBCOHTML_PROSPERO_PATCHED})

add_custom_target(libs DEPENDS ${PATCHED_OUTPUTS})

if(BUILD_TESTING)
    add_library(host_thread_local_test_library SHARED tests/HostThreadLocalLibrary.cpp)
    set_target_properties(host_thread_local_test_library PROPERTIES RUNTIME_OUTPUT_DIRECTORY "$<1:${RTY_TEST_OUTPUT_DIRECTORY}>")
    target_include_directories(host_thread_local_test_library PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test_executable(host_thread_local_tests tests/HostThreadLocal.cpp)
    target_link_libraries(host_thread_local_tests PRIVATE host_thread_local_test_library)
    if(MINGW)
        target_compile_options(host_thread_local_test_library PRIVATE -fasynchronous-unwind-tables)
        target_compile_options(host_thread_local_tests PRIVATE -fasynchronous-unwind-tables)
    endif()
    add_test(NAME host_thread_local COMMAND host_thread_local_tests)
    set_tests_properties(host_thread_local PROPERTIES TIMEOUT 15)
endif()

if(MINGW)
    foreach(runtimeLinkFlag IN ITEMS -static-libgcc -static-libstdc++ -static)
        string(REPLACE "${runtimeLinkFlag}" "" CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")
    endforeach()
endif()
