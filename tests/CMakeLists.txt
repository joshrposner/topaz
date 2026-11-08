include(FetchContent)

FetchContent_Declare(googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.15.2
  GIT_SHALLOW    TRUE
  SYSTEM
  FIND_PACKAGE_ARGS NAMES GTest
)
# Match the MSVC runtime used by the rest of the project
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

add_executable(tpz_tests
  dsa/list_test.cpp
  order_book_test.cpp
)

target_link_libraries(tpz_tests
  PRIVATE
    topaz::topaz
    tpz_options
    GTest::gtest_main
)

tpz_copy_asan_runtime(tpz_tests)

gtest_discover_tests(tpz_tests)