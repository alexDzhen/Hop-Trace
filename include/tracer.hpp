// tracer.hpp
// Declares the traceroute algorithm itself and its input and output types.
// This is the core of the program and it is completely independent of the
// operating system and of any output: it only talks to the IcmpClient
// interface and reports progress to a TraceObserver. That is what makes it
// possible to unit-test the whole algorithm with a fake client and no
// network. Implemented in tracer.cpp.

#pragma once

#include <chrono>
#include <cstddef>
#include <string>
#include <vector>

#include "icmp_client.hpp"

namespace hoptrace {

// Settings that control one trace; filled from the command line.
struct TraceOptions {
    /// TTL of the first probe.
    // -f: skip the first hops you already know (e.g. your own router).
    int first_hop = 1;
    /// The trace stops after this TTL even if the destination did not answer.
    // -m: a safety limit so a trace to a silent host does not run forever.
    int max_hops = 30;
    // -q: several probes per hop give several timings and catch routers
    // that drop some packets.
    int probes_per_hop = 3;
    // -w: how long to wait for each probe before printing "*".
    std::chrono::milliseconds timeout{2000};
    // -s
    std::size_t payload_size = 32;
};

/// Everything learned about one TTL value.
// One output line corresponds to one Hop: the TTL that was used and the
// result of every probe sent with it (normally probes_per_hop of them).
struct Hop {
    int ttl = 0;
    std::vector<EchoResult> probes;
};

// Why the trace stopped.
enum class TraceOutcome {
    /// The destination answered an echo request.
    DestinationReached,
    /// A router reported that the destination cannot be reached.
    DestinationUnreachable,
    /// max_hops was reached without an answer from the destination.
    MaxHopsExceeded,
    /// The interrupt flag of the client was raised.
    Interrupted,
    /// A probe could not be sent; see TraceResult::error.
    Failed,
};

// The complete result: the outcome plus every hop in order, so the whole
// trace can be inspected afterwards (the tests do exactly that).
struct TraceResult {
    TraceOutcome outcome = TraceOutcome::MaxHopsExceeded;
    std::vector<Hop> hops;
    std::string error;
};

/// Receives progress while a trace is running, so output can appear live.
// Observer pattern. trace_route() calls these at the right moments; a
// subclass (HopPrinter in report.hpp) overrides them to print. The default
// implementations do nothing, so a caller that does not want progress
// reports can pass a plain TraceObserver.
class TraceObserver {
public:
    virtual ~TraceObserver() = default;
    // Before the first probe of a hop.
    virtual void hop_started(int ttl);
    // After each probe has been answered or has timed out.
    virtual void probe_finished(int ttl, const EchoResult& result);
    // After the last probe of a hop.
    virtual void hop_finished(const Hop& hop);
};

/// Discovers the route to the target of `client`.
///
/// For TTL = first_hop, first_hop + 1, ... it sends `probes_per_hop` echo
/// requests. A router that has to forward a packet whose TTL has run out drops
/// it and answers with "time exceeded", which reveals the router's address.
/// The trace ends when the destination itself replies, when a router reports
/// the destination unreachable, or after max_hops.
// `client` is taken by reference to the interface, so any implementation
// works: the Linux or Windows one in the real program, a scripted fake in
// the tests.
TraceResult trace_route(IcmpClient& client, const TraceOptions& options, TraceObserver& observer);

}  // namespace hoptrace