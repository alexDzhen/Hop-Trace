// test_tracer.cpp
// Unit tests for the traceroute algorithm (tracer.cpp). Every test runs the
// real trace_route() against a FakeIcmpClient whose answers are scripted by
// a lambda, so any network situation can be simulated instantly and
// deterministically: a straight path, silent routers, an unreachable
// destination, Ctrl+C, a send failure. No packets are sent.

#include <doctest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "../include/tracer.hpp"
#include "fake_icmp_client.hpp"

using hoptrace::EchoRequest;
using hoptrace::EchoResult;
using hoptrace::EchoStatus;
using hoptrace::Hop;
using hoptrace::IpAddress;
using hoptrace::TraceObserver;
using hoptrace::TraceOptions;
using hoptrace::TraceOutcome;
using hoptrace::testing::answer;
using hoptrace::testing::FakeIcmpClient;
using hoptrace::testing::timeout;

namespace {

// The imaginary destination.
const IpAddress kTarget = IpAddress::ipv4(203, 0, 113, 10);

// The imaginary router at distance `hop`: 10.0.<hop>.1.
IpAddress router(int hop) {
    return IpAddress::ipv4(10, 0, static_cast<std::uint8_t>(hop), 1);
}

/// A straight path: routers answer "time exceeded" until `distance`, where the target replies.
// This models exactly what TTL does on a real network: a probe with TTL n
// is dropped by the n-th router, unless n is far enough to reach the target.
FakeIcmpClient path_of_length(int distance) {
    return FakeIcmpClient{[distance](const EchoRequest& request) {
        if (request.ttl >= distance) {
            return answer(EchoStatus::Reply, kTarget);
        }
        return answer(EchoStatus::TimeExceeded, router(request.ttl));
    }};
}

// An observer that just logs which callback was called with which TTL,
// to verify the order in which the tracer reports progress.
class RecordingObserver final : public TraceObserver {
public:
    void hop_started(int ttl) override { events.push_back("start " + std::to_string(ttl)); }
    void probe_finished(int ttl, const EchoResult&) override {
        events.push_back("probe " + std::to_string(ttl));
    }
    void hop_finished(const Hop& hop) override {
        events.push_back("finish " + std::to_string(hop.ttl));
    }

    std::vector<std::string> events;
};

}  // namespace

// The basic case: a 4-hop path with default options (3 probes per hop).
// Expect 4 hops with TTL 1..4, three probes each, routers 1-3 answering
// "time exceeded", the target answering the last hop, 12 probes in total.
TEST_CASE("a trace walks the path one hop at a time until the destination replies") {
    FakeIcmpClient client = path_of_length(4);
    TraceObserver observer;

    const auto result = hoptrace::trace_route(client, TraceOptions{}, observer);

    CHECK(result.outcome == TraceOutcome::DestinationReached);
    REQUIRE(result.hops.size() == 4);
    for (std::size_t i = 0; i < result.hops.size(); ++i) {
        const Hop& hop = result.hops[i];
        CHECK(hop.ttl == static_cast<int>(i) + 1);
        REQUIRE(hop.probes.size() == 3);
        const bool last = i + 1 == result.hops.size();
        for (const EchoResult& probe : hop.probes) {
            CHECK(probe.status == (last ? EchoStatus::Reply : EchoStatus::TimeExceeded));
            REQUIRE(probe.responder.has_value());
            CHECK(*probe.responder == (last ? kTarget : router(hop.ttl)));
        }
    }
    CHECK(client.requests.size() == 12);
}

// Inspects the requests the fake recorded: with 2 probes per hop, requests
// 0-1 have TTL 1, 2-3 TTL 2, 4-5 TTL 3; the payload size and timeout from
// the options reach every request; and no sequence number repeats (the set
// has as many entries as there were requests).
TEST_CASE("every probe carries the hop's TTL, a unique sequence and the configured settings") {
    FakeIcmpClient client = path_of_length(3);
    TraceObserver observer;
    TraceOptions options;
    options.probes_per_hop = 2;
    options.payload_size = 48;
    options.timeout = std::chrono::milliseconds{750};

    hoptrace::trace_route(client, options, observer);

    REQUIRE(client.requests.size() == 6);
    std::set<std::uint16_t> sequences;
    for (std::size_t i = 0; i < client.requests.size(); ++i) {
        const EchoRequest& request = client.requests[i];
        CHECK(request.ttl == static_cast<int>(i / 2) + 1);
        CHECK(request.payload_size == 48);
        CHECK(request.timeout == std::chrono::milliseconds{750});
        sequences.insert(request.sequence);
    }
    CHECK(sequences.size() == client.requests.size());
}

// -f 3 on a 5-hop path: hops 1 and 2 are skipped, hops 3, 4, 5 are traced.
TEST_CASE("the trace can start at a later hop") {
    FakeIcmpClient client = path_of_length(5);
    TraceObserver observer;
    TraceOptions options;
    options.first_hop = 3;
    options.probes_per_hop = 1;

    const auto result = hoptrace::trace_route(client, options, observer);

    CHECK(result.outcome == TraceOutcome::DestinationReached);
    REQUIRE(result.hops.size() == 3);
    CHECK(result.hops.front().ttl == 3);
    CHECK(result.hops.back().ttl == 5);
}

// -m 4 on a 10-hop path: the trace stops after hop 4 and reports that the
// destination was not reached.
TEST_CASE("the trace gives up after the maximum number of hops") {
    FakeIcmpClient client = path_of_length(10);
    TraceObserver observer;
    TraceOptions options;
    options.max_hops = 4;
    options.probes_per_hop = 1;

    const auto result = hoptrace::trace_route(client, options, observer);

    CHECK(result.outcome == TraceOutcome::MaxHopsExceeded);
    REQUIRE(result.hops.size() == 4);
    CHECK(result.hops.back().ttl == 4);
}

// Router 2 never answers (a common real-world situation). Its hop must be
// recorded as three timeouts with no responder, and the trace must carry
// on to hops 3 and 4 and still reach the target.
TEST_CASE("silent routers show up as timeouts and do not stop the trace") {
    FakeIcmpClient client{[](const EchoRequest& request) {
        if (request.ttl == 2) {
            return timeout();
        }
        if (request.ttl >= 4) {
            return answer(EchoStatus::Reply, kTarget);
        }
        return answer(EchoStatus::TimeExceeded, router(request.ttl));
    }};
    TraceObserver observer;

    const auto result = hoptrace::trace_route(client, TraceOptions{}, observer);

    CHECK(result.outcome == TraceOutcome::DestinationReached);
    REQUIRE(result.hops.size() == 4);
    for (const EchoResult& probe : result.hops[1].probes) {
        CHECK(probe.status == EchoStatus::Timeout);
        CHECK_FALSE(probe.responder.has_value());
    }
}

// Only the second of the three probes gets a reply. That must still count
// as "destination reached", and all three probes of the hop must still be
// sent (the decision is made after the hop, not in the middle of it).
TEST_CASE("one reply among timeouts is enough to finish") {
    int call = 0;
    FakeIcmpClient client{[&call](const EchoRequest&) {
        ++call;
        return call == 2 ? answer(EchoStatus::Reply, kTarget) : timeout();
    }};
    TraceObserver observer;

    const auto result = hoptrace::trace_route(client, TraceOptions{}, observer);

    CHECK(result.outcome == TraceOutcome::DestinationReached);
    REQUIRE(result.hops.size() == 1);
    CHECK(result.hops[0].probes.size() == 3);
}

// Router 2 reports "host unreachable" (code 1). The hop is completed (all
// three probes), the code is kept, and no further hops are tried.
TEST_CASE("an unreachable report ends the trace after the current hop") {
    FakeIcmpClient client{[](const EchoRequest& request) {
        if (request.ttl == 2) {
            EchoResult result = answer(EchoStatus::DestinationUnreachable, router(2));
            result.code = 1;
            return result;
        }
        return answer(EchoStatus::TimeExceeded, router(request.ttl));
    }};
    TraceObserver observer;

    const auto result = hoptrace::trace_route(client, TraceOptions{}, observer);

    CHECK(result.outcome == TraceOutcome::DestinationUnreachable);
    REQUIRE(result.hops.size() == 2);
    CHECK(result.hops[1].probes.size() == 3);
    CHECK(result.hops[1].probes[0].code == 1);
}

// The fifth probe (second probe of hop 2) comes back as Interrupted, as it
// would on Ctrl+C. Unlike "unreachable", this stops at once: no further
// probes are sent, and the interrupted probe itself is not recorded, so
// hop 2 holds only the one probe that completed before it.
TEST_CASE("an interrupt stops the trace immediately") {
    int call = 0;
    FakeIcmpClient client{[&call](const EchoRequest& request) {
        ++call;
        if (call == 5) {
            EchoResult result;
            result.status = EchoStatus::Interrupted;
            return result;
        }
        return answer(EchoStatus::TimeExceeded, router(request.ttl));
    }};
    TraceObserver observer;

    const auto result = hoptrace::trace_route(client, TraceOptions{}, observer);

    CHECK(result.outcome == TraceOutcome::Interrupted);
    CHECK(client.requests.size() == 5);
    REQUIRE(result.hops.size() == 2);
    CHECK(result.hops[0].probes.size() == 3);
    CHECK(result.hops[1].probes.size() == 1);
}

// The very first probe cannot be sent. The trace fails immediately with the
// error text preserved, leaving one hop with no recorded probes.
TEST_CASE("a send failure stops the trace and keeps the reason") {
    FakeIcmpClient client{[](const EchoRequest&) {
        EchoResult result;
        result.status = EchoStatus::Error;
        result.error = "cannot send: Network is unreachable";
        return result;
    }};
    TraceObserver observer;

    const auto result = hoptrace::trace_route(client, TraceOptions{}, observer);

    CHECK(result.outcome == TraceOutcome::Failed);
    CHECK(result.error == "cannot send: Network is unreachable");
    CHECK(client.requests.size() == 1);
    REQUIRE(result.hops.size() == 1);
    CHECK(result.hops[0].probes.empty());
}

// The observer protocol that HopPrinter depends on: for each hop, exactly
// one start, one probe callback per probe, one finish, in that order.
TEST_CASE("the observer sees every hop and probe in order") {
    FakeIcmpClient client = path_of_length(2);
    RecordingObserver observer;
    TraceOptions options;
    options.probes_per_hop = 2;

    hoptrace::trace_route(client, options, observer);

    const std::vector<std::string> expected = {
        "start 1", "probe 1", "probe 1", "finish 1", "start 2", "probe 2", "probe 2", "finish 2",
    };
    CHECK(observer.events == expected);
}