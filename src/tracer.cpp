// tracer.cpp
// The traceroute algorithm. It knows nothing about sockets or output: it
// sends probes through the IcmpClient interface and reports progress to a
// TraceObserver. That separation is what lets the unit tests run the whole
// algorithm against a scripted fake network.
//
// The idea: every IP packet has a TTL ("time to live"), and every router
// that forwards it subtracts one. A router that receives a packet with TTL 1
// must not forward it; it drops it and sends back an ICMP "time exceeded"
// message from its own address. So:
//   TTL 1  -> the first router answers and reveals itself
//   TTL 2  -> the second router answers
//   ...
//   TTL n  -> the destination itself answers with an echo reply: done.

#include "../include/tracer.hpp"

#include <cstdint>
#include <utility>

namespace hoptrace {

// Default observer: does nothing. A caller that wants no live output (for
// example the tests) can pass a plain TraceObserver.
void TraceObserver::hop_started(int) {}
void TraceObserver::probe_finished(int, const EchoResult&) {}
void TraceObserver::hop_finished(const Hop&) {}

TraceResult trace_route(IcmpClient& client, const TraceOptions& options, TraceObserver& observer) {
    TraceResult result;
    // One counter for the whole trace, so every probe has its own sequence
    // number and a late answer to an earlier probe can never be mistaken
    // for an answer to the current one.
    std::uint16_t sequence = 0;

    // Outer loop: one iteration per hop, i.e. per TTL value.
    for (int ttl = options.first_hop; ttl <= options.max_hops; ++ttl) {
        Hop hop;
        hop.ttl = ttl;
        observer.hop_started(ttl);

        // What this hop's probes told us. Decided after all probes, so that
        // the whole line is printed even if the first probe already reached
        // the destination.
        bool reached = false;
        bool unreachable = false;
        // Set when the trace must end immediately (Ctrl+C or a send error).
        bool stopped = false;

        // Inner loop: several probes with the same TTL, giving several
        // timings and a chance for a router that dropped one to answer another.
        for (int probe = 0; probe < options.probes_per_hop && !stopped; ++probe) {
            EchoRequest request;
            request.sequence = ++sequence;
            request.payload_size = options.payload_size;
            request.ttl = ttl;
            request.timeout = options.timeout;

            // Blocks until an answer, the timeout or an interrupt.
            EchoResult answer = client.echo(request);

            switch (answer.status) {
                case EchoStatus::Reply:
                    // The destination itself answered: this is the last hop.
                    reached = true;
                    break;
                case EchoStatus::DestinationUnreachable:
                    // A router says the destination cannot be reached: the
                    // path ends here.
                    unreachable = true;
                    break;
                case EchoStatus::Interrupted:
                    result.outcome = TraceOutcome::Interrupted;
                    stopped = true;
                    break;
                case EchoStatus::Error:
                    // Could not even send (e.g. no route). Keep the reason.
                    result.outcome = TraceOutcome::Failed;
                    result.error = answer.error;
                    stopped = true;
                    break;
                case EchoStatus::TimeExceeded:
                case EchoStatus::Timeout:
                    // TimeExceeded: a router at this distance answered, the
                    // normal case for an intermediate hop. Timeout: nothing
                    // answered, printed as "*". Either way, carry on.
                    break;
            }
            // An interrupted or failed probe is not recorded as a result.
            if (stopped) {
                break;
            }

            // Report the probe right away (live output), then keep it.
            observer.probe_finished(ttl, answer);
            hop.probes.push_back(std::move(answer));
        }

        // The hop is complete (or cut short): close the line and store it.
        observer.hop_finished(hop);
        result.hops.push_back(std::move(hop));

        // Decide whether to go on to the next TTL.
        if (stopped) {
            return result;
        }
        if (reached) {
            result.outcome = TraceOutcome::DestinationReached;
            return result;
        }
        if (unreachable) {
            result.outcome = TraceOutcome::DestinationUnreachable;
            return result;
        }
    }

    // Ran out of hops without hearing from the destination.
    result.outcome = TraceOutcome::MaxHopsExceeded;
    return result;
}

}  // namespace hoptrace