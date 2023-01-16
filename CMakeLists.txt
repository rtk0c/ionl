cmake_minimum_required(VERSION 3.13)
project(Ionl LANGUAGES CXX)

option(Ionl_DEBUG_FEATURES "Master flag for enabling debugging features" OFF)

include(${CMAKE_BINARY_DIR}/conanbuildinfo.cmake)
conan_basic_setup()

file(GLOB_RECURSE IonlApp_SRC_FILES
    source/*.h
    source/*.hpp
    source/*.c
    source/*.cpp
)
add_executable(IonlApp ${IonlApp_SRC_FILES})

target_compile_features(IonlApp PRIVATE cxx_std_20)

target_include_directories(IonlApp
PRIVATE
    sources/
)
target_link_libraries(IonlApp
PRIVATE
    ${CONAN_LIBS}
)
target_compile_definitions(IonlApp
PRIVATE
    IONL_DEBUG_FEATURES=$<BOOL:${Ionl_DEBUG_FEATURES}>
)
