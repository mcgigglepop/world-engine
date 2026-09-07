# demo executable that exercises the SDK end-to-end against StubTransport.
# see fake_player_session.cpp for the scenario it simulates. linked PRIVATE
# against worldengine_core so the demo target itself doesn't re-export the
# SDK's transitive dependencies.

add_executable(fake_player_session fake_player_session.cpp)
target_link_libraries(fake_player_session PRIVATE worldengine_core)
# Same warning bar as the library, applied only when building on real
# compilers (skips MSVC's separate flag universe).
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(fake_player_session PRIVATE -Wall -Wextra -Wpedantic)
endif()