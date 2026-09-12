# Root CMake for the worldengine_core C++ SDK library.
# Build graph produced here:
#   worldengine_core (STATIC)  ->  linked into fake_player_session (examples/)
#                            ->  linked into worldengine_tests (tests/)
#                            ->  intended to be linked by the future Unreal
#                                and standalone-C++ adapter layers.
# All transitive deps (nlohmann_json, Threads) are exposed as PUBLIC so
# downstream consumers get them for free.

cmake_minimum_required(VERSION 3.16)
project(worldengine_core CXX)

# ---- Language + warnings ----------------------------------------------------
# C++17 is the floor: we use std::variant, structured bindings, and if-
# constexpr in event.cpp. EXTENSIONS OFF keeps GNU/MS extensions out so the
# code stays portable across engine toolchains.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default to Release if the caller didn't pick a build type. Debug builds are
# still available via -DCMAKE_BUILD_TYPE=Debug.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# Detect whether this is the top-level project (CMake >= 3.21 defines
# PROJECT_IS_TOP_LEVEL for us; provide a fallback for older CMake). Being
# top-level means "someone ran cmake here directly, so build tests and demo".
# If this SDK is add_subdirectory'd from a larger tree (e.g. the future
# monorepo), those default OFF so downstream builds aren't paying for our
# tests.
if(NOT DEFINED PROJECT_IS_TOP_LEVEL)
    if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
        set(PROJECT_IS_TOP_LEVEL TRUE)
    else()
        set(PROJECT_IS_TOP_LEVEL FALSE)
    endif()
endif()

# Toggles for consumers. Default ON only when building this SDK on its own.
option(WORLDENGINE_BUILD_TESTS   "Build worldengine_core unit tests" ${PROJECT_IS_TOP_LEVEL})
option(WORLDENGINE_BUILD_EXAMPLES "Build worldengine_core examples"   ${PROJECT_IS_TOP_LEVEL})

# ---- Platform string --------------------------------------------------------
# Picks the human-readable OS string that gets baked into every event's
# `platform` field via the WORLDENGINE_PLATFORM macro below. Compile-time on
# purpose — a game binary ships targeting one platform, so runtime detection
# would be waste (see include/worldengine/client.h for the rationale).
if(APPLE)
    set(WORLDENGINE_PLATFORM_STR "macos")
elseif(WIN32)
    set(WORLDENGINE_PLATFORM_STR "windows")
elseif(UNIX)
    set(WORLDENGINE_PLATFORM_STR "linux")
else()
    set(WORLDENGINE_PLATFORM_STR "unknown")
endif()

# ---- Third-party (FetchContent) --------------------------------------------
# We fetch nlohmann_json at configure time so the SDK is self-contained (no
# system package required). FETCHCONTENT_QUIET OFF surfaces network activity
# during the initial configure so the developer sees what's happening.
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# nlohmann/json v3.11.3 — the JSON library used by event.cpp and
# stub_transport.cpp. GIT_SHALLOW TRUE makes the clone dramatically faster
# (single commit, no history). Pin to a specific tag so builds are
# reproducible even if upstream force-pushes.
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)
# Turn off the vendored tests and install rules — we only want the library.
set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install    OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(nlohmann_json)

# ---- Static library ---------------------------------------------------------
# The SDK ships as a STATIC library so games can link it into a single
# executable without worrying about DLL/dylib boundaries or symbol
# visibility (which are painful in Unreal in particular).
add_library(worldengine_core STATIC
    src/auth.cpp
    src/client.cpp
    src/event.cpp
    src/stub_transport.cpp
)

# Public headers live in include/; private helpers (uuid.h) live in src/ and
# are visible only to this library's own .cpp files.
target_include_directories(worldengine_core
    PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# WORLDENGINE_PLATFORM is consumed in include/worldengine/client.h to define
# `kPlatform`. Exposed PUBLIC so any translation unit that includes
# client.h sees the same value the SDK was built with.
target_compile_definitions(worldengine_core
    PUBLIC WORLDENGINE_PLATFORM="${WORLDENGINE_PLATFORM_STR}"
)

# nlohmann_json is public: our headers do not include <nlohmann/json.hpp>,
# but users may still want to construct their own Events and serialize
# them via SerializeEvent(), so keeping the transitive header path clean
# is convenient. If this ever becomes a build-time cost concern we can
# demote to PRIVATE.
target_link_libraries(worldengine_core
    PUBLIC nlohmann_json::nlohmann_json
)

# Sharpen warnings on real compilers; keep this PRIVATE so downstream users
# aren't forced into our warning bar.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(worldengine_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Threads (worker thread in Client). Threads::Threads is the modern portable
# way to pull in pthread on Unix and the no-op equivalent on Windows.
find_package(Threads REQUIRED)
target_link_libraries(worldengine_core PUBLIC Threads::Threads)

# Namespaced alias for consumers: `target_link_libraries(mygame PRIVATE
# worldengine::core)` reads clearer than the bare target name.
add_library(worldengine::core ALIAS worldengine_core)

# ---- Example ---------------------------------------------------------------
# Builds ./build/examples/fake_player_session (the demo binary).
if(WORLDENGINE_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# ---- Tests -----------------------------------------------------------------
# Fetches Catch2 and builds the 11-test suite. enable_testing() must be called
# at the directory that will run ctest, hence here rather than inside tests/.
if(WORLDENGINE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()