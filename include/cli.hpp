// cli.hpp
// Declares the command-line interface of hoptrace: the struct holding every
// option, the parser that fills it, and the --help text. Implemented in
// cli.cpp.

#pragma once

#include <stdexcept>
#include <string>
#include <vector>

#include "tracer.hpp"
#include "resolver.hpp"

namespace hoptrace {

/// Thrown when the command line cannot be understood.
// main() catches it, prints the message plus a hint to try --help, and
// exits with code 2.
class UsageError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// All options in one place. The settings that control the trace itself
// (first hop, max hops, probes per hop, timeout, payload size) live in the
// nested TraceOptions, which is what trace_route() takes; the rest only
// matter to main().
struct CliOptions {
    // The one positional argument.
    std::string host;
    // -f, -m, -q, -w, -s
    TraceOptions trace;
    // -4 / -6
    FamilyPreference family = FamilyPreference::Any;
    /// Print addresses only, without reverse DNS lookups.
    // -n: faster, since no DNS query is made per router.
    bool numeric = false;
    // -h / -V: print and exit without tracing anything.
    bool show_help = false;
    bool show_version = false;
};

// Upper limit for -s. An IP packet is at most 65535 bytes and the IP and
// ICMP headers need some of that.
inline constexpr std::size_t kMaxPayloadSize = 65500;

/// Parses the arguments that follow the program name.
// Takes a vector of strings rather than argc/argv so the tests can call it
// with literal lists like {"-n", "-q", "1", "host"}.
CliOptions parse_cli(const std::vector<std::string>& args);

/// The text printed for --help.
std::string usage_text();

}  // namespace hoptrace