cmake_minimum_required(VERSION 3.22)

project(yay LANGUAGES CXX)

# Modern target-based CMake layout

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

include(CheckIPOSupported)

check_ipo_supported(RESULT IPO_SUPPORTED OUTPUT IPO_ERROR)
if(IPO_SUPPORTED)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
endif()

# Sources
set(YAY_COMMON_HEADERS
    src/hash_util.h
    src/logger.h
    src/exec_util.h
    src/json_config.h
    src/io_scheduler.h
    src/rules_engine.h
    src/game_mode.h
)

set(YAY_COMMON_SOURCES
    src/hash_util.cpp
    src/logger.cpp
    src/json_config.cpp
    src/exec_util.cpp
    src/io_scheduler.cpp
    src/rules_engine.cpp
    src/game_mode.cpp
)

add_library(yay_common STATIC ${YAY_COMMON_SOURCES} ${YAY_COMMON_HEADERS})
target_include_directories(yay_common
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Executables
add_executable(yay_apply src/main_apply.cpp)
add_executable(yay_watch src/main_watch.cpp)

target_link_libraries(yay_apply PRIVATE yay_common log)
target_link_libraries(yay_watch PRIVATE yay_common log)

# Set C++20 requirement explicitly on targets
foreach(t IN ITEMS yay_apply yay_watch yay_common)
    target_compile_features(${t} PRIVATE cxx_std_20)
endforeach()

# Compiler warnings and hardening
set(YAY_WARNINGS
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
)

foreach(t IN ITEMS yay_apply yay_watch yay_common)
    target_compile_options(${t} PRIVATE
        $<$<CONFIG:Release>:-O2>
        ${YAY_WARNINGS}
        -fstack-protector-strong
        -fvisibility=hidden
        -ffunction-sections
        -fdata-sections
        -D_FORTIFY_SOURCE=2
        -Wno-unused-result
    )

    target_link_options(${t} PRIVATE
        -Wl,--gc-sections
        -Wl,--strip-all
        -Wl,-z,relro
        -Wl,-z,now
        -static-libstdc++
    )
endforeach()

# Sanitizers for Debug
foreach(t IN ITEMS yay_apply yay_watch yay_common)
    target_compile_options(${t} PRIVATE
        $<$<CONFIG:Debug>:-O0 -g3 -fsanitize=address -fsanitize=undefined>
    )
    target_link_options(${t} PRIVATE
        $<$<CONFIG:Debug>:-fsanitize=address -fsanitize=undefined>
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
]])
