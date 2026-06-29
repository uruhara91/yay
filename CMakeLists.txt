cmake_minimum_required(VERSION 3.22)
project(yay CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ─── Compiler flags ──────────────────────────────────────────────────────────
set(CMAKE_CXX_FLAGS_RELEASE
    "-O2 -ffunction-sections -fdata-sections -fvisibility=hidden \
     -fstack-protector-strong -D_FORTIFY_SOURCE=2 \
     -Wno-unused-result"
)

# ─── Linker flags ────────────────────────────────────────────────────────────
# -static-libstdc++  : embed libc++ — no dependency on device libstdc++
# -z relro / -z now  : RELRO hardening (Android default since API 23)
set(CMAKE_EXE_LINKER_FLAGS_RELEASE
    "-Wl,--gc-sections -Wl,--strip-all \
     -static-libstdc++ \
     -Wl,-z,relro -Wl,-z,now"
)

include_directories(src src/vendor)

set(COMMON_SOURCES
    src/hash_util.cpp
    src/logger.cpp
    src/json_config.cpp
    src/exec_util.cpp
    src/io_scheduler.cpp
    src/rules_engine.cpp
    src/game_mode.cpp
)

add_executable(yay_apply src/main_apply.cpp ${COMMON_SOURCES})
target_link_libraries(yay_apply log)

add_executable(yay_watch src/main_watch.cpp ${COMMON_SOURCES})
target_link_libraries(yay_watch log)

install(TARGETS yay_apply yay_watch RUNTIME DESTINATION bin)

# Extra strip pass after install
install(CODE "
    execute_process(COMMAND \${CMAKE_STRIP} --strip-all
        \${CMAKE_INSTALL_PREFIX}/bin/yay_apply)
    execute_process(COMMAND \${CMAKE_STRIP} --strip-all
        \${CMAKE_INSTALL_PREFIX}/bin/yay_watch)
")
