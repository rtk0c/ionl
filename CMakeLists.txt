cmake_minimum_required(VERSION 3.13)
project(ImGuiDemoProject LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup()

add_executable(${PROJECT_NAME}
    # add_executable requires at least one source file
    source/main.cpp
)
add_subdirectory(source)

target_include_directories(${PROJECT_NAME}
PRIVATE
    sources/
)
target_link_libraries(${PROJECT_NAME}
PRIVATE
    ${CONAN_LIBS}
)
