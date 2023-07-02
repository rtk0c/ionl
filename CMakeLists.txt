cmake_minimum_required(VERSION 3.13)
project(Ionl LANGUAGES CXX)

option(Ionl_DEBUG_FEATURES "Master flag for enabling debugging features" OFF)

find_package(fmt CONFIG REQUIRED)
find_package(Freetype REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(robin_hood CONFIG REQUIRED)
# TODO support non-vcpkg version as well?
find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(tomlplusplus CONFIG REQUIRED)

file(GLOB_RECURSE IonlApp_SRC_FILES
    source/*.h
    source/*.hpp
    source/*.c
    source/*.cpp
)
add_executable(IonlApp ${IonlApp_SRC_FILES})

# On clang/gcc: this should enable -std=c++23, which is incomplete but should be present on the latest compilers
# On MSVC: this should enable /std:c++latest
target_compile_features(IonlApp PRIVATE cxx_std_23)

target_include_directories(IonlApp
PRIVATE
    sources/
)
target_link_libraries(IonlApp
PRIVATE
    fmt::fmt
    Freetype::Freetype
    glfw
    robin_hood::robin_hood
    unofficial::sqlite3::sqlite3
    tomlplusplus::tomlplusplus
)
target_compile_definitions(IonlApp
PRIVATE
    IONL_DEBUG_FEATURES=$<BOOL:${Ionl_DEBUG_FEATURES}>
)
