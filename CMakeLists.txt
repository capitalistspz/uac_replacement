cmake_minimum_required(VERSION 4.3)
project(uac_replacement)

set(CMAKE_CXX_STANDARD 26)

if (NOT WUMS)
    message(FATAL_ERROR "This project requires the WUMS toolchain")
endif ()

add_executable(uac_replacement main.cpp
    uac.hpp
    meta.cpp
    impl_structs.hpp
    util.hpp)

target_compile_options(uac_replacement PRIVATE -Wall -Wextra -fanalyzer)
wums_create_module(uac_replacement)