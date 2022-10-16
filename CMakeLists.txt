cmake_minimum_required(VERSION 3.13)
project(InfiniteOutliner LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup()

file(GLOB_RECURSE appInfiniteOutliner_SRC_FILES
    source/*.h
    source/*.hpp
    source/*.c
    source/*.cpp
)
add_executable(appInfiniteOutliner ${appInfiniteOutliner_SRC_FILES})

target_include_directories(appInfiniteOutliner
PRIVATE
    sources/
)
target_link_libraries(appInfiniteOutliner
PRIVATE
    ${CONAN_LIBS}
)
