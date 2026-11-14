# Unit tests for vcdcore (disc-image parsing + PBC engine). Self-contained:
# no test framework and no Qt/mpv, so they build wherever vcdcore does.
# The CI runs them under AddressSanitizer/UBSan (see .github/workflows/test.yml).
add_executable(vcdcore_tests
    test_main.cpp
    test_disc.cpp
    test_parsers.cpp
    test_pbc.cpp
)
target_link_libraries(vcdcore_tests PRIVATE vcdcore)
# Designated initializers keep the synthetic PSD descriptors in test_pbc.cpp
# readable; the app and vcdcore themselves stay C++17.
target_compile_features(vcdcore_tests PRIVATE cxx_std_20)
add_test(NAME vcdcore_tests COMMAND vcdcore_tests)
# Leaving fields out of a designated initializer is the point (they keep
# their defaults); GCC's -Wextra would flag every one of them.
target_compile_options(vcdcore_tests PRIVATE $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wno-missing-field-initializers>)
