// report.hpp
// Declares everything that turns a trace into text: the banner, the
// closing outcome line, the "!H"-style markers, and HopPrinter, which
// prints each hop line piece by piece while the trace is still running.
// Implemented in report.cpp.

#pragma once

#include <functional>
#include <map>
#include <optional>
#include <ostream>
#include <string>

#include "ip_address.hpp"
#include "tracer.hpp"

namespace hoptrace {

/// Turns an address into a host name, or nothing if it has none.
// A std::function so the printer does not depend on how the lookup is done:
// main() passes a real reverse-DNS lookup (or nothing for -n), the tests
// pass a lambda with canned answers.
using NameLookup = std::function<std::optional<std::string>(const IpAddress&)>;

/// The banner printed before the first hop.
// e.g. "Tracing route to example.com (93.184.216.34), 30 hops max, ..."
std::string format_header(const std::string& host, const IpAddress& target,
                          const TraceOptions& options);

/// The closing line that says how the trace ended.
// e.g. "Destination reached at hop 3." / "Destination not reached within 30 hops."
std::string format_outcome(const TraceResult& result);

/// The traceroute-style marker for an unreachable code, such as "!H".
// Same letters as the classic traceroute: !H host, !N network, !P protocol
// or port, !X administratively prohibited, !F fragmentation, !S scope.
std::string unreachable_marker(Family family, int code);

/// Prints one line per hop while the trace is running:
///
///      1  192.168.1.1  0.412 ms  0.388 ms  0.375 ms
///      2  * * *
///      3  core1.example.net (203.0.113.9)  5.123 ms  5.004 ms  4.998 ms
///
/// A "*" is a probe that got no answer. The responder's address is printed
/// again whenever it changes within a hop.
// This is the observer pattern: trace_route() does the work and calls these
// three methods at the right moments, and the printer reacts by writing to
// the stream. The tracer never knows about output, the printer never knows
// about sockets. Writing to a std::ostream (not std::cout directly) lets the
// tests capture the output in a std::ostringstream.
class HopPrinter final : public TraceObserver {
public:
    /// Pass an empty `lookup` to print addresses only.
    HopPrinter(std::ostream& out, Family family, NameLookup lookup = {});

    // Called once before the probes of a hop: prints the hop number.
    void hop_started(int ttl) override;
    // Called after each probe: prints "*", or the address (if it changed)
    // and the time, plus a marker for unreachable answers.
    void probe_finished(int ttl, const EchoResult& result) override;
    // Called after the last probe of a hop: ends the line.
    void hop_finished(const Hop& hop) override;

private:
    // "name (address)" or just "address", using the lookup at most once per
    // address (results are cached in descriptions_).
    std::string describe(const IpAddress& address);

    std::ostream& out_;
    Family family_;
    NameLookup lookup_;
    // Cache: address text -> what to print for it. DNS lookups are slow and
    // the same router often answers several probes.
    std::map<std::string, std::string> descriptions_;
    // Who answered the previous probe of the current hop, so the address is
    // printed only when it changes within a line.
    std::optional<IpAddress> last_responder_;
};

}  // namespace hoptrace