cmake_minimum_required(VERSION 3.22.1)
project("sa2b_engine")

add_library(
        sa2b_engine
        SHARED
        native-lib.cpp
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