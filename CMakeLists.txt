cmake_minimum_required(VERSION 3.21)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

project(SketchbookMenu VERSION 1.0.0)

file(GLOB SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
add_library(${PROJECT_NAME} SHARED ${SOURCES})
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if (NOT DEFINED ENV{GEODE_SDK})
    message(FATAL_ERROR "Unable to find Geode SDK! Please define GEODE_SDK environment variable to point to Geode")
else()
    message(STATUS "Found Geode: $ENV{GEODE_SDK}")
endif()

add_subdirectory($ENV{GEODE_SDK} ${CMAKE_CURRENT_BINARY_DIR}/geode)

# Dear ImGui drawn on top of cocos2d (the menu itself).
# TIP: for a reproducible build, replace "geode" with a commit hash from
#      https://github.com/matcool/gd-imgui-cocos/commits/geode
set(IMGUI_VERSION "v1.92.3")
CPMAddPackage("gh:matcool/gd-imgui-cocos#geode")
target_link_libraries(${PROJECT_NAME} imgui-cocos)

if (WIN32)
    # shell32 = open folders/links, opengl32 = read the screen,
    # mf* + ole32 = Windows' built-in video encoder (the "Built-in" renderer)
    target_link_libraries(${PROJECT_NAME} shell32 opengl32 mfplat mfreadwrite mfuuid ole32)
endif()

setup_geode_mod(${PROJECT_NAME})
