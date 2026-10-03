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
