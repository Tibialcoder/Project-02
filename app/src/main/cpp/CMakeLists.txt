cmake_minimum_required(VERSION 3.22.1)
project("sa2b_engine")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(
        sa2b_engine
        SHARED
        native-lib.cpp
        prs_decompressor.cpp
        texture_translation.cpp
        resource_file_manager.cpp
        resource_pipeline.cpp
        pvm_parser.cpp
        gvr_parser.cpp
)

find_library(log-lib log)
find_library(android-lib android)
find_library(glesv3-lib GLESv3)
find_library(egl-lib EGL)

target_link_libraries(
        sa2b_engine
        ${log-lib}
        ${android-lib}
        ${glesv3-lib}
        ${egl-lib}
)