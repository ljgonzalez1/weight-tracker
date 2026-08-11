# Each suite is an independent executable registered with CTest, so a failure
# names the suite and a crash in one cannot take the others down.
set(WEIGHT_TEST_DATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/data")

set(WEIGHT_HEADLESS_TESTS
    test_numeric_array
    test_cubic_spline
    test_csv_parsing
    test_localisation
    test_session_config
    test_workspace
    test_concurrency
    test_axis_window
    # Asks the operating system directly: where Documents really is, whether an
    # atomic replace leaves debris on this filesystem, whether the named socket
    # behaves as a mutex here. Every assertion in it is platform-specific, so
    # it is the suite that has to be re-run on each target rather than trusted
    # from the last one.
    test_platform_integration)

set(WEIGHT_WIDGET_TESTS
    test_validators)

function(weight_add_test test_name link_target)
    add_executable(${test_name} "unit/${test_name}.cpp")
    target_link_libraries(${test_name} PRIVATE ${link_target} Qt6::Test)
    target_compile_definitions(${test_name}
        PRIVATE "WEIGHT_TEST_DATA_DIR=\"${WEIGHT_TEST_DATA_DIR}\"")
    weight_apply_compiler_options(${test_name})
    # Tests are build artefacts, not deliverables: keep them out of target/.
    # Tests are build artefacts, not deliverables. The per-config properties
    # must be set too, or the generator falls back to the global target/ path.
    set_target_properties(${test_name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_BINARY_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_BINARY_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_CURRENT_BINARY_DIR}"
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_CURRENT_BINARY_DIR}")
    add_test(NAME ${test_name} COMMAND ${test_name})
    set_tests_properties(${test_name} PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;WEIGHT_LANG=C"
        TIMEOUT 300)
endfunction()

foreach(test_name IN LISTS WEIGHT_HEADLESS_TESTS)
    weight_add_test(${test_name} weight_core)
endforeach()
foreach(test_name IN LISTS WEIGHT_WIDGET_TESTS)
    weight_add_test(${test_name} weight_ui)
endforeach()

# The brief asks that `make test` be runnable against the compiled binary
# alone. This registers the binary's own --self-test as a CTest case, so the
# same command covers both the unit suites and the deployed artefact: the font
# really being embedded, the icon really being in the binary, the numerics
# being right on this architecture, and the platform integration working on
# this kernel.
add_test(NAME self_test COMMAND $<TARGET_FILE:weight> --self-test)
set_tests_properties(self_test PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;WEIGHT_LANG=C"
    TIMEOUT 300)
