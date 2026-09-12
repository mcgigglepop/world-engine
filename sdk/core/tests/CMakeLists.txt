# Test binary: worldengine_tests, discovered by ctest and driven by Catch2.
# Fetches Catch2 at configure time (same rationale as nlohmann_json in the
# root CMakeLists — self-contained, no system packages required).

include(FetchContent)

# Catch2 v3.5.4 — modern Catch2 is a real library, not a header-only drop-in.
# GIT_SHALLOW keeps the initial configure fast.
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.5.4
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(Catch2)

# Pull in Catch2's `catch_discover_tests` helper so each TEST_CASE becomes
# an individual ctest entry (11 tests instead of one big rollup).
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)

# Every test file compiles into a single test binary. Adding a new test file
# means adding its filename here.
add_executable(worldengine_tests
    test_event_serialization.cpp
    test_property_variants.cpp
    test_queue_threading.cpp
    test_batch_size_flush.cpp
    test_shutdown_flushes.cpp
    test_auth.cpp
)

# Catch2WithMain provides the default main(); we don't need a custom entry.
target_link_libraries(worldengine_tests PRIVATE worldengine_core Catch2::Catch2WithMain)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(worldengine_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Registers each TEST_CASE with ctest by running the binary once at build
# time with --list-tests. Without this, `ctest` would only see one target.
catch_discover_tests(worldengine_tests)