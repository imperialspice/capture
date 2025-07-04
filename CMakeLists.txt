cmake_minimum_required(VERSION 3.15)
project(server_capture)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

## For some reason helps with headers not being found.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

## Include Dependencies 
include(FetchContent)
FetchContent_Declare(ftxui
  GIT_REPOSITORY https://github.com/ArthurSonzogni/ftxui
  GIT_TAG v6.1.9
)
FetchContent_MakeAvailable(ftxui)

## Reflection Library --- This is a bit heavyweight but still
FetchContent_Declare(
    reflect_library
    GIT_REPOSITORY https://github.com/getml/reflect-cpp.git
    GIT_TAG 907c815
)
FetchContent_MakeAvailable(reflect_library)

set(CMAKE_BUILD_TYPE Debug)

set(SOURCE_FILES main.cpp)
message(${reflect_library_SOURCE_DIR})
include_directories("${reflect_library_SOURCE_DIR}/include")

add_executable(capture ${SOURCE_FILES})

add_executable(test_menu test_menu.cpp)

target_link_libraries(capture PRIVATE 
    ftxui::component
    ftxui::dom
    ftxui::screen
    reflectcpp
)

target_link_libraries(test_menu PRIVATE 
    ftxui::component
    ftxui::dom
    ftxui::screen
)



