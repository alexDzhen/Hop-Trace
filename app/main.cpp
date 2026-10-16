// main.cpp
// Entry point of hoptrace. Ties the modules together:
//   parse the command line (cli)  ->  resolve the host (resolver)
//   -> open the ICMP transport (icmp_client)  ->  run the trace (tracer),
//   printing each hop live through HopPrinter (report)  ->  print how the
//   trace ended and exit with a matching code.

#include <csignal>
#include <exception>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "../include/cli.hpp"
#include "../include/icmp_client.hpp"
#include "../include/report.hpp"
#include "../include/tracer.hpp"
#include "../include/resolver.hpp"

namespace {

// Set to 1 by the signal handler on Ctrl+C (SIGINT) or kill (SIGTERM).
// sig_atomic_t + volatile is what the standard guarantees is safe to write
// from a signal handler and read from normal code.
volatile std::sig_atomic_t g_stop_requested = 0;

// A signal handler may do almost nothing safely, so it only raises the
// flag; the tracer checks it and stops.
void handle_stop_signal(int) {
    g_stop_requested = 1;
}

// Reverse DNS (address -> name) is a blocking system call that can hang for
// many seconds when the DNS server does not answer, and our flag-based
// handler cannot interrupt it. So around the lookup the default SIGINT
// behaviour (terminate the process) is restored; afterwards our handler is
// put back so a Ctrl+C during the trace itself still ends cleanly with the
// outcome line.
std::optional<std::string> interruptible_reverse_lookup(const hoptrace::IpAddress& address) {
    std::signal(SIGINT, SIG_DFL);
    std::optional<std::string> name = hoptrace::reverse_lookup(address);
    std::signal(SIGINT, handle_stop_signal);
    return name;
}

// The trace session itself. Returns the process exit code.
int trace(const hoptrace::CliOptions& options) {
    // Socket library setup (Windows), name resolution, OS-specific
    // transport. Any of these may throw; main() reports the error.
    const hoptrace::NetworkSession session;
    const hoptrace::IpAddress target = hoptrace::resolve(options.host, options.family);
    const std::unique_ptr<hoptrace::IcmpClient> client = hoptrace::IcmpClient::create(target);

    // Handlers go in only after setup, so a Ctrl+C during a slow DNS lookup
    // of the target still kills the program the normal way. The client gets
    // the same flag so it can abandon a wait early.
    std::signal(SIGINT, handle_stop_signal);
    std::signal(SIGTERM, handle_stop_signal);
    client->set_interrupt_flag(&g_stop_requested);

    std::cout << hoptrace::format_header(options.host, target, options.trace) << std::endl;

    // With -n the printer gets no lookup function and shows addresses only;
    // otherwise every router address is also resolved to a host name.
    hoptrace::NameLookup lookup;
    if (!options.numeric) {
        lookup = interruptible_reverse_lookup;
    }
    // HopPrinter is a TraceObserver: trace_route calls it after every probe,
    // which is how the output appears line by line while the trace runs.
    hoptrace::HopPrinter printer{std::cout, target.family(), lookup};

    const hoptrace::TraceResult result = hoptrace::trace_route(*client, options.trace, printer);
    std::cout << hoptrace::format_outcome(result) << std::endl;

    // Exit codes: 0 reached, 2 could not even send, 1 everything else
    // (not reached within max hops, unreachable, interrupted).
    switch (result.outcome) {
        case hoptrace::TraceOutcome::DestinationReached:
            return 0;
        case hoptrace::TraceOutcome::Failed:
            return 2;
        default:
            return 1;
    }
}

// Parses the arguments and dispatches: help, version, or a trace.
// A bad command line is reported with the conventional exit code 2.
int run(const std::vector<std::string>& args) {
    hoptrace::CliOptions options;
    try {
        options = hoptrace::parse_cli(args);
    } catch (const hoptrace::UsageError& error) {
        std::cerr << "hoptrace: " << error.what()
                  << "\nTry 'hoptrace --help' for more information.\n";
        return 2;
    }

    if (options.show_help) {
        std::cout << hoptrace::usage_text();
        return 0;
    }
    if (options.show_version) {
        // HOPTRACE_VERSION is defined by CMake from the project version.
        std::cout << "hoptrace " << HOPTRACE_VERSION << "\n";
        return 0;
    }
    return trace(options);
}

}  // namespace

// argv[0] is the program name, so the arguments proper are argv[1..argc).
// Every remaining exception (ResolveError, NetworkError, ...) ends up here
// and becomes a one-line message and exit code 2.
int main(int argc, char* argv[]) {
    try {
        return run(std::vector<std::string>(argv + 1, argv + argc));
    } catch (const std::exception& error) {
        std::cerr << "hoptrace: " << error.what() << "\n";
        return 2;
    }
}