cmake_minimum_required(VERSION 3.20)
project(tdp_host CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core)
qt_standard_project_setup()

add_executable(host src/main.cpp)
target_include_directories(host PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(host PRIVATE Qt6::Core)
