add_executable(hoptrace_tests
    test_cli.cpp
    test_icmp.cpp
    test_ip_address.cpp
    test_main.cpp
    test_report.cpp
    test_tracer.cpp)
target_include_directories(hoptrace_tests SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/third_party/doctest)
target_compile_definitions(hoptrace_tests PRIVATE DOCTEST_CONFIG_USE_STD_HEADERS)
target_link_libraries(hoptrace_tests PRIVATE hoptrace::core hoptrace_warnings)
if(MINGW)
    target_link_options(hoptrace_tests PRIVATE -static)
endif()

add_test(NAME unit_tests COMMAND hoptrace_tests)

add_test(NAME cli_help COMMAND hoptrace --help)
set_tests_properties(cli_help PROPERTIES PASS_REGULAR_EXPRESSION "Usage: hoptrace")

add_test(NAME cli_version COMMAND hoptrace --version)
set_tests_properties(cli_version PROPERTIES PASS_REGULAR_EXPRESSION "^hoptrace ${PROJECT_VERSION}")

add_test(NAME cli_requires_host COMMAND hoptrace)
set_tests_properties(cli_requires_host PROPERTIES WILL_FAIL TRUE)

add_test(NAME cli_rejects_unknown_option COMMAND hoptrace --bogus 127.0.0.1)
set_tests_properties(cli_rejects_unknown_option PROPERTIES WILL_FAIL TRUE)

# Sends real packets to the loopback interface. On Linux this needs root,
# CAP_NET_RAW or unprivileged ICMP sockets; without them the test is skipped.
add_test(NAME loopback_trace COMMAND hoptrace -n -q 1 127.0.0.1)
set_tests_properties(loopback_trace PROPERTIES
    PASS_REGULAR_EXPRESSION "Destination reached at hop 1"
    SKIP_REGULAR_EXPRESSION "permission denied"
    TIMEOUT 30)
