// test_cli.cpp
// Unit tests for the command-line parser (cli.cpp), written with doctest.
// Each TEST_CASE is an independent test; CHECK records a failure and keeps
// going, CHECK_THROWS_AS passes only if the expression throws the given
// exception type. The tests call parse_cli() directly with lists of
// arguments, so no process is started and no network is touched.

#include <doctest.h>

#include <chrono>
#include <initializer_list>
#include <string>

#include "../include/cli.hpp"

using hoptrace::FamilyPreference;
using hoptrace::UsageError;
using std::chrono::milliseconds;

// With only a host given, every option must have its documented default.
TEST_CASE("a bare host uses the defaults") {
    const auto options = hoptrace::parse_cli({"example.com"});
    CHECK(options.host == "example.com");
    CHECK(options.trace.first_hop == 1);
    CHECK(options.trace.max_hops == 30);
    CHECK(options.trace.probes_per_hop == 3);
    CHECK(options.trace.timeout == milliseconds{2000});
    CHECK(options.trace.payload_size == 32);
    CHECK(options.family == FamilyPreference::Any);
    CHECK_FALSE(options.numeric);
    CHECK_FALSE(options.show_help);
    CHECK_FALSE(options.show_version);
}

// The "-m 12" form: every short option sets its field, fractional seconds
// become milliseconds, and flags without a value (-n, -4) work too.
TEST_CASE("short options take their value from the next argument") {
    const auto options = hoptrace::parse_cli(
        {"-m", "12", "-f", "2", "-q", "1", "-w", "0.5", "-s", "64", "-n", "-4", "10.0.0.1"});
    CHECK(options.host == "10.0.0.1");
    CHECK(options.trace.max_hops == 12);
    CHECK(options.trace.first_hop == 2);
    CHECK(options.trace.probes_per_hop == 1);
    CHECK(options.trace.timeout == milliseconds{500});
    CHECK(options.trace.payload_size == 64);
    CHECK(options.numeric);
    CHECK(options.family == FamilyPreference::IPv4);
}

// Long options work both as "--max-hops 5" and as "--max-hops=5". The
// values also sit on the edges of the allowed ranges (probes 10, size 0,
// first hop equal to max hops).
TEST_CASE("long options accept both spellings") {
    const auto spaced = hoptrace::parse_cli({"--max-hops", "5", "--first-hop", "5", "--probes",
                                             "10", "--timeout", "3", "--size", "0", "host"});
    CHECK(spaced.trace.max_hops == 5);
    CHECK(spaced.trace.first_hop == 5);
    CHECK(spaced.trace.probes_per_hop == 10);
    CHECK(spaced.trace.timeout == milliseconds{3000});
    CHECK(spaced.trace.payload_size == 0);

    const auto joined = hoptrace::parse_cli({"--max-hops=64", "--first-hop=3", "--probes=2",
                                             "--timeout=1.25", "--size=1000", "--numeric", "host"});
    CHECK(joined.trace.max_hops == 64);
    CHECK(joined.trace.first_hop == 3);
    CHECK(joined.trace.probes_per_hop == 2);
    CHECK(joined.trace.timeout == milliseconds{1250});
    CHECK(joined.trace.payload_size == 1000);
    CHECK(joined.numeric);
}

// The host does not have to come last.
TEST_CASE("options may follow the host") {
    const auto options = hoptrace::parse_cli({"example.com", "-6", "-n"});
    CHECK(options.host == "example.com");
    CHECK(options.family == FamilyPreference::IPv6);
    CHECK(options.numeric);
}

// "hoptrace --help" alone must not fail with "missing host".
TEST_CASE("help and version do not need a host") {
    CHECK(hoptrace::parse_cli({"-h"}).show_help);
    CHECK(hoptrace::parse_cli({"--help"}).show_help);
    CHECK(hoptrace::parse_cli({"-V"}).show_version);
    CHECK(hoptrace::parse_cli({"--version"}).show_version);
}

// No host, a flag but no host, and two hosts are all rejected.
TEST_CASE("a missing or extra host is a usage error") {
    CHECK_THROWS_AS(hoptrace::parse_cli({}), UsageError);
    CHECK_THROWS_AS(hoptrace::parse_cli({"-n"}), UsageError);
    CHECK_THROWS_AS(hoptrace::parse_cli({"a", "b"}), UsageError);
}

// The one check that involves two options at once: starting beyond the
// maximum would mean a trace with no hops. Equal values are fine (a
// single-hop trace).
TEST_CASE("the first hop must not be beyond the maximum") {
    CHECK_THROWS_AS(hoptrace::parse_cli({"-f", "10", "-m", "5", "host"}), UsageError);
    CHECK_NOTHROW(hoptrace::parse_cli({"-f", "5", "-m", "5", "host"}));
}

// Unknown names, an option at the very end with nothing after it, and a
// value attached to a flag that takes none.
TEST_CASE("unknown options and missing values are usage errors") {
    CHECK_THROWS_AS(hoptrace::parse_cli({"--bogus", "host"}), UsageError);
    CHECK_THROWS_AS(hoptrace::parse_cli({"-x", "host"}), UsageError);
    CHECK_THROWS_AS(hoptrace::parse_cli({"host", "-m"}), UsageError);
    CHECK_THROWS_AS(hoptrace::parse_cli({"host", "--numeric=1"}), UsageError);
}

// The strict number parsing: values just outside each option's range
// (max hops 0 and 256, first hop 0, probes 0 and 11, timeout 0, size 65501),
// non-numeric text and a negative size.
TEST_CASE("out-of-range and malformed numbers are usage errors") {
    CHECK_THROWS_AS(hoptrace::parse_cli({"-m", "0", "host"}), UsageError);
    CHECK_THROWS_AS(hoptrace::parse_cli({"-m", "256", "host"}), UsageError);
    CHECK_THROWS_AS(hoptrace::parse_cli({"-m", "ten", "host"}), UsageError);
    CHECK_THROWS_AS(hoptrace::parse_cli({"-f", "0", "host"}), UsageError);
    CHECK_THROWS_AS(hoptrace::parse_cli({"-q", "0", "host"}), UsageError);
    CHECK_THROWS_AS(hoptrace::parse_cli({"-q", "11", "host"}), UsageError);
    CHECK_THROWS_AS(hoptrace::parse_cli({"-w", "0", "host"}), UsageError);
    CHECK_THROWS_AS(hoptrace::parse_cli({"-w", "soon", "host"}), UsageError);
    CHECK_THROWS_AS(hoptrace::parse_cli({"-s", "65501", "host"}), UsageError);
    CHECK_THROWS_AS(hoptrace::parse_cli({"-s", "-1", "host"}), UsageError);
}

// Guards against adding an option and forgetting to document it.
// CAPTURE makes doctest print which option was being checked if it fails.
TEST_CASE("the usage text mentions every option") {
    const std::string usage = hoptrace::usage_text();
    for (const char* option : {"--max-hops", "--first-hop", "--probes", "--timeout", "--size",
                               "--numeric", "-4", "-6", "--help", "--version"}) {
        CAPTURE(option);
        CHECK(usage.find(option) != std::string::npos);
    }
}