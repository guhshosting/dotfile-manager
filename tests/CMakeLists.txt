# Unit and integration test suite (doctest-based). Only built when
# DFM_BUILD_TESTS is ON (see top-level CMakeLists.txt).

add_library(doctest_main OBJECT unit/doctest_main.cpp)
target_include_directories(doctest_main PRIVATE vendor)
target_compile_options(doctest_main PRIVATE ${DFM_WARNING_FLAGS})

add_executable(dfm_unit_tests
    unit/test_path_safety.cpp
    unit/test_file_types.cpp
    unit/test_env.cpp
    unit/test_ignore_rules.cpp
    unit/test_config.cpp
    unit/test_atomic_io.cpp
    unit/test_hashing.cpp
    unit/test_diff_engine.cpp
    unit/test_compare.cpp
    unit/test_discovery.cpp
    unit/test_backup.cpp
    unit/test_output.cpp
    unit/test_cli.cpp
    unit/test_commands.cpp
)
target_link_libraries(dfm_unit_tests PRIVATE dfm_core doctest_main)
target_include_directories(dfm_unit_tests PRIVATE vendor unit)
target_compile_options(dfm_unit_tests PRIVATE ${DFM_WARNING_FLAGS})

add_test(NAME dfm_unit_tests COMMAND dfm_unit_tests)

# ---------------------------------------------------------------------------
# Integration tests: exercise the built CLI binary end-to-end against
# temporary fake HOME directories (never the real HOME).
# ---------------------------------------------------------------------------
add_test(
    NAME dfm_integration_tests
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/integration/run_integration_tests.sh $<TARGET_FILE:dotfile-manager>
)
set_tests_properties(dfm_integration_tests PROPERTIES
    ENVIRONMENT "DFM_BIN=$<TARGET_FILE:dotfile-manager>"
)
