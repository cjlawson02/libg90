include(FetchContent)

# GoogleTest v1.15.2 — use release tag (shallow SHA fetch is unreliable).
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.15.2
  GIT_SHALLOW    TRUE
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(libg90_tests
  test_smoke.cpp
  test_result.cpp
  test_body_helpers.cpp
  test_head_tx_gate.cpp
  test_head_wire_bytes.cpp
  test_head.cpp
  test_body.cpp
  test_fixtures.cpp
  test_vectors.cpp
  test_de9_fake.cpp
  test_g90ctl_live.cpp
)

target_link_libraries(libg90_tests
  PRIVATE
    libg90::libg90
    GTest::gtest_main
)

target_include_directories(libg90_tests PRIVATE
  ${CMAKE_SOURCE_DIR}/cli
)

target_compile_definitions(libg90_tests PRIVATE
  LIBG90_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
  LIBG90_VECTORS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/vectors"
)

target_compile_options(libg90_tests PRIVATE
  -Wall -Wextra -Wpedantic -Werror
)

include(GoogleTest)
gtest_discover_tests(libg90_tests)
