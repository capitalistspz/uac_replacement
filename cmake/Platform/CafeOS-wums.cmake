# -----------------------------------------------------------------------------
# Platform configuration

cmake_minimum_required(VERSION 3.13)
include_guard(GLOBAL)

# Inherit default devkitPro platform configuration
include($ENV{DEVKITPRO}/cmake/Platform/Generic-dkP.cmake)

# Platform identification flags
set(CAFEOS TRUE)
set(NINTENDO_WIIU TRUE)
set(WIIU TRUE)
set(WUT TRUE)
set(WUMS TRUE)

# Platform settings
set(WUMS_ARCH_SETTINGS "-mcpu=750 -meabi -mhard-float")
set(WUMS_COMMON_FLAGS "-ffunction-sections -fdata-sections -DESPRESSO -D__WIIU__ -D__WUT__")
set(WUMS_LINKER_FLAGS "-L${WUMS_ROOT}/lib -L${WUT_ROOT}/lib -L${DEVKITPRO}/portlibs/wiiu/lib -L${DEVKITPRO}/portlibs/ppc/lib -T${WUMS_ROOT}/share/wums.ld -specs=${WUT_ROOT}/share/wut.specs -specs=${WUMS_ROOT}/share/wums.specs")
set(WUMS_STANDARD_LIBRARIES "-lwums -lwut -lm")
set(WUMS_STANDARD_INCLUDE_DIRECTORIES "${WUMS_ROOT}/include" "${WUT_ROOT}/include")

__dkp_init_platform_settings(WUMS)

# -----------------------------------------------------------------------------
# Platform-specific helper utilities

# create a WUMS plugin
function(wums_create_module target)
    get_target_property(WUMS_OUTPUT_NAME ${target} OUTPUT_NAME)
    get_target_property(WUMS_BINARY_DIR ${target} BINARY_DIR)

    if (NOT WUMS_OUTPUT_NAME)
        set(WUMS_OUTPUT_NAME "${target}")
    endif ()

    set(WUMS_OUTPUT "${WUMS_BINARY_DIR}/${WUMS_OUTPUT_NAME}.wms")

    add_custom_command(TARGET ${target}
        POST_BUILD
        COMMAND ${WUMS_ELF2RPL_EXE} "$<TARGET_FILE:${target}>" "${WUMS_OUTPUT}"
        COMMAND printf '\\xAF\\xFE' | dd of=${WUMS_OUTPUT} bs=1 seek=9 count=2 conv=notrunc status=none
        BYPRODUCTS "${WUMS_OUTPUT}"
        COMMENT "Converting ${target} to .wms format"
        VERBATIM
    )
endfunction()


