cmake_minimum_required(VERSION 3.22)

project(yay LANGUAGES CXX)

#
# Language
#

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

#
# IPO / LTO
#

include(CheckIPOSupported)

check_ipo_supported(
    RESULT IPO_SUPPORTED
    OUTPUT IPO_ERROR
)

if(IPO_SUPPORTED)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
endif()

#
# Sources
#

include_directories(
    src
    src/vendor
)

set(COMMON_SOURCES
    src/hash_util.cpp
    src/logger.cpp
    src/json_config.cpp
    src/exec_util.cpp
    src/io_scheduler.cpp
    src/rules_engine.cpp
    src/game_mode.cpp
)

add_executable(yay_apply
    src/main_apply.cpp
    ${COMMON_SOURCES}
)

add_executable(yay_watch
    src/main_watch.cpp
    ${COMMON_SOURCES}
)

#
# Common compiler options
#

foreach(target
    yay_apply
    yay_watch)

    target_compile_features(${target}
        PRIVATE
            cxx_std_20
    )

    target_compile_options(${target}
        PRIVATE

        #
        # Optimization
        #

        $<$<CONFIG:Release>:-O2>

        #
        # Warnings
        #

        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wshadow
        -Wformat=2
        -Wnull-dereference
        -Wimplicit-fallthrough
        -Wunused
        -Wcast-qual
        -Wmissing-declarations

        #
        # Security
        #

        -fstack-protector-strong
        -fvisibility=hidden
        -ffunction-sections
        -fdata-sections

        -D_FORTIFY_SOURCE=2

        #
        # Silence
        #

        -Wno-unused-result
    )

    target_link_options(${target} PRIVATE
        -Wl,--gc-sections
        -Wl,-z,relro
        -Wl,-z,now
        $<$<CONFIG:Release>:-Wl,--strip-all>
    )

    target_link_libraries(${target}
        PRIVATE
            log
    )

endforeach()

#
# Sanitizer (Debug only)
#

foreach(target
    yay_apply
    yay_watch)

    target_compile_options(${target}
        PRIVATE

        $<$<CONFIG:Debug>:
            -O0
            -g3
            -fsanitize=address
            -fsanitize=undefined
        >
    )

    target_link_options(${target}
        PRIVATE

        $<$<CONFIG:Debug>:
            -fsanitize=address
            -fsanitize=undefined
        >
    )
endforeach()

# Install
install(TARGETS yay_apply yay_watch RUNTIME DESTINATION bin)

install(CODE [[
execute_process(
    COMMAND ${CMAKE_STRIP} --strip-all ${CMAKE_INSTALL_PREFIX}/bin/yay_apply
)
execute_process(
    COMMAND ${CMAKE_STRIP} --strip-all ${CMAKE_INSTALL_PREFIX}/bin/yay_watch
)
]]
)