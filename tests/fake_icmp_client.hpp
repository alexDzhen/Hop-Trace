// fake_icmp_client.hpp
// Test double for the IcmpClient interface. Instead of sending packets it
// answers each probe from a function the test supplies, so the tracing
// algorithm can be exercised against any imaginary network: a straight
// path, silent routers, an unreachable destination, a Ctrl+C in the middle.
// This works because trace_route() depends only on the IcmpClient
// interface, never on a real socket. Only used by the tests.

#pragma once

#include <chrono>
#include <functional>
#include <utility>
#include <vector>

#include "../include/icmp_client.hpp"

namespace hoptrace::testing {

/// An IcmpClient that answers from a script instead of the network.
class FakeIcmpClient final : public IcmpClient {
public:
    // The script: given a probe (its TTL and sequence), decide what the
    // network answers. Usually a lambda in the test.
    using Responder = std::function<EchoResult(const EchoRequest&)>;

    explicit FakeIcmpClient(Responder responder) : responder_(std::move(responder)) {}

    // Records the probe so the test can inspect what was sent, then asks
    // the script for the answer.
    EchoResult echo(const EchoRequest& request) override {
        requests.push_back(request);
        return responder_(request);
    }

    // Every probe the tracer sent, in order. Public so tests can check
    // TTLs, sequence numbers and settings directly.
    std::vector<EchoRequest> requests;

private:
    Responder responder_;
};

// Helpers for writing scripts concisely.

// An answer of the given kind from `responder`, e.g.
// answer(EchoStatus::TimeExceeded, router) or answer(EchoStatus::Reply, target).
inline EchoResult answer(EchoStatus status, const IpAddress& responder, double milliseconds = 1.0) {
    EchoResult result;
    result.status = status;
    result.responder = responder;
    result.round_trip = std::chrono::duration<double, std::milli>{milliseconds};
    return result;
}

// No answer at all: a default EchoResult has status Timeout and no responder.
inline EchoResult timeout() {
    return EchoResult{};
}

}  // namespace hoptrace::testing