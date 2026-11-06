add_library(libSceNetCtl SHARED EXCLUDE_FROM_ALL Export.cpp)
target_include_directories(libSceNetCtl PRIVATE ${LIBS_INCLUDE_DIR})
configure_windows_unwind(libSceNetCtl)
