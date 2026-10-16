// test_report.cpp
// Unit tests for the output formatting (report.cpp). The HopPrinter writes
// into a std::ostringstream instead of the console, so each produced line
// can be compared character for character. The answer()/timeout() helpers
// from fake_icmp_client.hpp build the EchoResults.

#include <doctest.h>

#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "../include/report.hpp"
#include "fake_icmp_client.hpp"

using hoptrace::EchoResult;
using hoptrace::EchoStatus;
using hoptrace::Family;
using hoptrace::Hop;
using hoptrace::HopPrinter;
using hoptrace::IpAddress;
using hoptrace::TraceOptions;
using hoptrace::TraceOutcome;
using hoptrace::TraceResult;
using hoptrace::testing::answer;
using hoptrace::testing::timeout;

namespace {

const IpAddress kRouterA = IpAddress::ipv4(10, 0, 1, 1);
const IpAddress kRouterB = IpAddress::ipv4(10, 0, 2, 2);

// Drives the printer exactly as trace_route() would for one hop
// (hop_started, probe_finished per probe, hop_finished) and returns the
// text that came out. The stream is cleared first so each call yields
// only its own line.
std::string print_hop(HopPrinter& printer, std::ostringstream& out, int ttl,
                      const std::vector<EchoResult>& probes) {
    out.str("");
    Hop hop;
    hop.ttl = ttl;
    printer.hop_started(ttl);
    for (const EchoResult& probe : probes) {
        printer.probe_finished(ttl, probe);
        hop.probes.push_back(probe);
    }
    printer.hop_finished(hop);
    return out.str();
}

}  // namespace

// The banner with default options, and with values of 1 to check the
// singular forms ("1 hop", "1 probe").
TEST_CASE("the header describes the trace") {
    TraceOptions options;
    CHECK(hoptrace::format_header("example.com", IpAddress::ipv4(93, 184, 216, 34), options) ==
          "Tracing route to example.com (93.184.216.34), 30 hops max, 3 probes per hop, "
          "32 data bytes");

    options.max_hops = 1;
    options.probes_per_hop = 1;
    options.payload_size = 0;
    CHECK(hoptrace::format_header("h", IpAddress::ipv4(1, 2, 3, 4), options) ==
          "Tracing route to h (1.2.3.4), 1 hop max, 1 probe per hop, 0 data bytes");
}

// The normal line: three probes answered by the same router give the
// address once and three times, each with three decimals.
TEST_CASE("a hop line shows the address once, then one time per probe") {
    std::ostringstream out;
    HopPrinter printer{out, Family::IPv4};

    CHECK(print_hop(printer, out, 1,
                    {answer(EchoStatus::TimeExceeded, kRouterA, 0.412),
                     answer(EchoStatus::TimeExceeded, kRouterA, 0.388),
                     answer(EchoStatus::TimeExceeded, kRouterA, 12.3456)}) ==
          " 1  10.0.1.1  0.412 ms  0.388 ms  12.346 ms\n");
}

// " 1" versus "12": the hop number takes two columns so lines stay aligned.
TEST_CASE("hop numbers are right-aligned to two digits") {
    std::ostringstream out;
    HopPrinter printer{out, Family::IPv4};

    CHECK(print_hop(printer, out, 12, {answer(EchoStatus::Reply, kRouterA, 1.0)}) ==
          "12  10.0.1.1  1.000 ms\n");
}

// Timeouts become "*": all three, one in the middle, and one at the start
// (in which case the address appears after the star).
TEST_CASE("unanswered probes are printed as stars") {
    std::ostringstream out;
    HopPrinter printer{out, Family::IPv4};

    CHECK(print_hop(printer, out, 2, {timeout(), timeout(), timeout()}) == " 2  * * *\n");
    CHECK(print_hop(printer, out, 3,
                    {answer(EchoStatus::TimeExceeded, kRouterA, 1.5), timeout(),
                     answer(EchoStatus::TimeExceeded, kRouterA, 2.5)}) ==
          " 3  10.0.1.1  1.500 ms *  2.500 ms\n");
    CHECK(
        print_hop(printer, out, 4, {timeout(), answer(EchoStatus::TimeExceeded, kRouterA, 1.5)}) ==
        " 4  * 10.0.1.1  1.500 ms\n");
}

// Load balancing can route probes of the same hop through different
// routers; each new address is printed when it first appears.
TEST_CASE("a different responder within one hop is printed again") {
    std::ostringstream out;
    HopPrinter printer{out, Family::IPv4};

    CHECK(print_hop(printer, out, 5,
                    {answer(EchoStatus::TimeExceeded, kRouterA, 1.0),
                     answer(EchoStatus::TimeExceeded, kRouterB, 2.0),
                     answer(EchoStatus::TimeExceeded, kRouterB, 3.0)}) ==
          " 5  10.0.1.1  1.000 ms 10.0.2.2  2.000 ms  3.000 ms\n");
}

// The "print address only when it changes" rule resets at every hop, so a
// router answering two consecutive hops is named on both lines.
TEST_CASE("the responder is printed again on the next hop even if it is the same") {
    std::ostringstream out;
    HopPrinter printer{out, Family::IPv4};

    print_hop(printer, out, 1, {answer(EchoStatus::TimeExceeded, kRouterA, 1.0)});
    CHECK(print_hop(printer, out, 2, {answer(EchoStatus::TimeExceeded, kRouterA, 1.0)}) ==
          " 2  10.0.1.1  1.000 ms\n");
}

// An unreachable answer gets its marker after the time, or after the "*"
// when the reporting router is unknown. Then the marker table itself.
TEST_CASE("unreachable answers carry a marker") {
    std::ostringstream out;
    HopPrinter printer{out, Family::IPv4};

    EchoResult unreachable = answer(EchoStatus::DestinationUnreachable, kRouterB, 0.5);
    unreachable.code = 1;
    CHECK(print_hop(printer, out, 2, {unreachable}) == " 2  10.0.2.2  0.500 ms !H\n");

    unreachable.responder.reset();
    CHECK(print_hop(printer, out, 3, {unreachable}) == " 3  * !H\n");

    CHECK(hoptrace::unreachable_marker(Family::IPv4, 0) == "!N");
    CHECK(hoptrace::unreachable_marker(Family::IPv4, 1) == "!H");
    CHECK(hoptrace::unreachable_marker(Family::IPv4, 2) == "!P");
    CHECK(hoptrace::unreachable_marker(Family::IPv4, 13) == "!X");
    CHECK(hoptrace::unreachable_marker(Family::IPv4, 7) == "!<7>");
    CHECK(hoptrace::unreachable_marker(Family::IPv6, 0) == "!N");
    CHECK(hoptrace::unreachable_marker(Family::IPv6, 1) == "!X");
    CHECK(hoptrace::unreachable_marker(Family::IPv6, 3) == "!H");
    CHECK(hoptrace::unreachable_marker(Family::IPv6, 4) == "!P");
}

// With a lookup function, addresses that have a name are shown as
// "name (address)" and those without just as the address. The lookup
// counter proves the cache works: router A appears on hops 1 and 3 (three
// probes in total) but is looked up once, router B once, so 2 in total.
TEST_CASE("host names are shown next to the address and looked up once") {
    std::ostringstream out;
    int lookups = 0;
    HopPrinter printer{out, Family::IPv4,
                       [&lookups](const IpAddress& address) -> std::optional<std::string> {
                           ++lookups;
                           if (address == kRouterA) {
                               return "gateway.example.net";
                           }
                           return std::nullopt;
                       }};

    CHECK(print_hop(printer, out, 1,
                    {answer(EchoStatus::TimeExceeded, kRouterA, 1.0),
                     answer(EchoStatus::TimeExceeded, kRouterA, 1.0)}) ==
          " 1  gateway.example.net (10.0.1.1)  1.000 ms  1.000 ms\n");
    CHECK(print_hop(printer, out, 2, {answer(EchoStatus::TimeExceeded, kRouterB, 1.0)}) ==
          " 2  10.0.2.2  1.000 ms\n");
    CHECK(print_hop(printer, out, 3, {answer(EchoStatus::TimeExceeded, kRouterA, 1.0)}) ==
          " 3  gateway.example.net (10.0.1.1)  1.000 ms\n");
    CHECK(lookups == 2);
}

// Some resolvers return the address text itself as the "name"; printing
// "10.0.1.1 (10.0.1.1)" would be silly, so that case collapses to the address.
TEST_CASE("a name equal to the address is not repeated") {
    std::ostringstream out;
    HopPrinter printer{
        out, Family::IPv4,
        [](const IpAddress& address) -> std::optional<std::string> { return address.to_string(); }};

    CHECK(print_hop(printer, out, 1, {answer(EchoStatus::TimeExceeded, kRouterA, 1.0)}) ==
          " 1  10.0.1.1  1.000 ms\n");
}

// One line per outcome, all using the TTL of the last recorded hop (7).
TEST_CASE("the outcome line explains how the trace ended") {
    TraceResult result;
    result.hops.resize(7);
    result.hops.back().ttl = 7;

    result.outcome = TraceOutcome::DestinationReached;
    CHECK(hoptrace::format_outcome(result) == "Destination reached at hop 7.");

    result.outcome = TraceOutcome::DestinationUnreachable;
    CHECK(hoptrace::format_outcome(result) == "Destination unreachable, reported at hop 7.");

    result.outcome = TraceOutcome::MaxHopsExceeded;
    CHECK(hoptrace::format_outcome(result) == "Destination not reached within 7 hops.");

    result.outcome = TraceOutcome::Interrupted;
    CHECK(hoptrace::format_outcome(result) == "Interrupted.");

    result.outcome = TraceOutcome::Failed;
    result.error = "cannot send: Network is unreachable";
    CHECK(hoptrace::format_outcome(result) == "Trace failed: cannot send: Network is unreachable");
}