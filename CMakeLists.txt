cmake_minimum_required(VERSION 3.13)
project(InfiniteOutliner LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup()

file(GLOB_RECURSE appInfiniteOuterliner_SRC_FILES
    source/*.hpp
    source/*.cpp
)
add_executable(appInfiniteOuterliner ${appInfiniteOuterliner_SRC_FILES})

target_include_directories(appInfiniteOuterliner
PRIVATE
    sources/
)
target_link_libraries(appInfiniteOuterliner
PRIVATE
    ${CONAN_LIBS}
)
