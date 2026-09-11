# Based on https://github.com/wiiu-env/WiiUPluginSystem/pull/45
cmake_minimum_required(VERSION 3.25)

if(NOT DEFINED ENV{DEVKITPRO})
    message(FATAL_ERROR "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitPro")
endif()

if(NOT DEFINED ENV{DEVKITPPC})
    message(FATAL_ERROR "Please set DEVKITPPC in your environment. export DEVKITPPC=<path to>/devkitPro/devkitPPC")
endif()

# create a WUMS plugin
function(wums_create_module target)
    set(WUMS_ROOT "$ENV{DEVKITPRO}/wums")

    target_include_directories(${target} PRIVATE "${WUMS_ROOT}/include")
    target_link_directories(${target} PRIVATE "${WUMS_ROOT}/lib")
    target_link_libraries(${target} PRIVATE wums)
    target_link_options(${target} PRIVATE "-T${WUMS_ROOT}/share/wums.ld" "-specs=${WUMS_ROOT}/share/wums.specs")

    get_target_property(WUMS_OUTPUT_NAME ${target} OUTPUT_NAME)
    get_target_property(WUMS_BINARY_DIR  ${target} BINARY_DIR)
    if(NOT WUMS_OUTPUT_NAME)
        set(WUMS_OUTPUT_NAME "${target}")
    endif()
    set(WUMS_OUTPUT "${WUMS_BINARY_DIR}/${WUMS_OUTPUT_NAME}.wms")

    add_custom_command(TARGET ${target}
        POST_BUILD
        COMMAND ${WUT_ELF2RPL_EXE} "$<TARGET_FILE:${target}>" "${WUMS_OUTPUT}"
        COMMAND printf '\\xAF\\xFE' | dd of=${WUMS_OUTPUT} bs=1 seek=9 count=2 conv=notrunc status=none
        BYPRODUCTS "${WUMS_OUTPUT}"
        COMMENT "Converting ${target} to .wms format"
        VERBATIM
    )
endfunction()