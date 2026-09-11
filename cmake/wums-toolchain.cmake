cmake_minimum_required(VERSION 3.13)

if(NOT CMAKE_SYSTEM_NAME)
    set(CMAKE_SYSTEM_NAME CafeOS-wums)
endif()
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}")

# Import devkitPPC toolchain
include($ENV{DEVKITPRO}/cmake/devkitPPC.cmake)

set(WUT_ROOT ${DEVKITPRO}/wut)
set(WUMS_ROOT ${DEVKITPRO}/wums)
set(DKP_INSTALL_PREFIX_INIT ${WUMS_ROOT})

__dkp_platform_prefix(
    ${DEVKITPRO}/portlibs/wiiu
    ${DEVKITPRO}/portlibs/ppc
    ${WUT_ROOT}
    ${WUMS_ROOT}
)


find_program(PKG_CONFIG_EXECUTABLE NAMES powerpc-eabi-pkg-config HINTS "${DEVKITPRO}/portlibs/wiiu/bin")
if (NOT PKG_CONFIG_EXECUTABLE)
    message(FATAL_ERROR "Could not find powerpc-eabi-pkg-config: try installing wiiu-pkg-config")
endif()

find_program(WUMS_ELF2RPL_EXE NAMES elf2rpl HINTS "${DEVKITPRO}/tools/bin")