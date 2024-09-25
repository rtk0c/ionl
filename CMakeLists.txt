cmake_minimum_required(VERSION 3.13)
project(Ionl LANGUAGES CXX)

option(Ionl_DEBUG_FEATURES "Master flag for enabling debugging features" OFF)


find_package(fmt CONFIG REQUIRED)

# vcpkg & CMake find module uses same package name and resulting imported target name
find_package(Freetype REQUIRED)

find_package(glfw3 CONFIG REQUIRED)

find_package(robin_hood CONFIG REQUIRED)

find_package(SQLite3)
if(NOT SQLite3_FOUND)
    find_package(unofficial-sqlite3 CONFIG REQUIRED)
    add_library(SQLite::SQLite3 ALIAS unofficial-sqlite3::sqlite3::sqlite3)
endif()

find_package(tomlplusplus CONFIG REQUIRED)


file(GLOB_RECURSE imgui_SRC_FILES src/imgui/*.c src/imgui/*.cpp)
add_library(imgui ${imgui_SRC_FILES})
target_include_directories(imgui PUBLIC src)
target_link_libraries(imgui PUBLIC Freetype::Freetype)

file(GLOB_RECURSE IonlApp_SRC_FILES src/ionl/*.c src/ionl/*.cpp)
add_executable(IonlApp ${IonlApp_SRC_FILES})

target_include_directories(IonlApp PRIVATE src)
target_link_libraries(IonlApp
PRIVATE
    # project dependencies
    imgui
    # external dependencies
    fmt::fmt
    glfw
    robin_hood::robin_hood
    SQLite::SQLite3
    tomlplusplus::tomlplusplus
)
target_compile_definitions(IonlApp
PRIVATE
    IONL_DEBUG_FEATURES=$<BOOL:${Ionl_DEBUG_FEATURES}>
)

set_target_properties(
    imgui IonlApp
PROPERTIES
    # On clang/gcc: this should enable -std=c++23, which is incomplete but should be present on the latest compilers
    # On MSVC: this should enable /std:c++latest
    # N.B. we are not using `target_compile_features(... cxx_std_23)` because there is no way to disable vendor extensions with that syntax
    CXX_STANDARD 23
    CXX_EXTENSIONS OFF
)
