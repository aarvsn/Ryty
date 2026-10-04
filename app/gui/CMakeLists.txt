cmake_minimum_required(VERSION 3.21)

if(NOT WIN32)
    string(REPLACE "-static-libgcc" "" RTY_GUI_LINK_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")
    string(REPLACE "-static-libstdc++" "" RTY_GUI_LINK_FLAGS "${RTY_GUI_LINK_FLAGS}")
    string(REPLACE "-static" "" RTY_GUI_LINK_FLAGS "${RTY_GUI_LINK_FLAGS}")
    string(STRIP "${RTY_GUI_LINK_FLAGS}" CMAKE_EXE_LINKER_FLAGS)
endif()

add_executable(ryty-gui
    ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/MainWindow.cpp
)

target_include_directories(ryty-gui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ryty-gui PRIVATE Qt6::Widgets)
target_compile_features(ryty-gui PRIVATE cxx_std_20)
set_target_properties(ryty-gui PROPERTIES
    AUTOMOC ON
    CXX_EXTENSIONS OFF
)

if(MINGW)
    target_link_options(ryty-gui PRIVATE -mwindows)
endif()

if(Qt6_FOUND)
    if(WIN32 OR MINGW)
        find_program(WINDEPLOYQT_EXECUTABLE NAMES windeployqt6 windeployqt HINTS ${Qt6_DIR}/../../../bin)
        if(WINDEPLOYQT_EXECUTABLE)
            add_custom_command(TARGET ryty-gui POST_BUILD
                COMMAND ${WINDEPLOYQT_EXECUTABLE} --release --no-translations --dir "$<TARGET_FILE_DIR:ryty-gui>" --compiler-runtime "$<TARGET_FILE:ryty-gui>"
                COMMENT "Deploying Qt6 runtime libraries for Windows GUI..."
            )
        endif()
    elseif(UNIX AND NOT APPLE)
        add_custom_command(TARGET ryty-gui POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:ryty-gui>/plugins/platforms"
            COMMENT "Deploying Qt6 runtime deployment layout for Linux GUI..."
        )
    endif()
endif()
