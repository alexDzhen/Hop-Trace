// report.cpp
// Builds all the text hoptrace prints: the opening banner, the live hop
// lines (through HopPrinter) and the closing outcome line. Keeping the
// formatting here, separate from the tracing algorithm, means the output
// can be unit-tested by writing into a string stream.

#include "../include/report.hpp"

#include <cstdio>
#include <iomanip>

namespace hoptrace {

namespace {

// Round-trip time with three decimals, as the classic traceroute prints it,
// e.g. "0.412 ms".
std::string format_milliseconds(double milliseconds) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.3f ms", milliseconds);
    return buffer;
}

// "1 hop" / "3 hops", "1 probe" / "3 probes".
std::string plural(int count, const char* singular, const char* plural_form) {
    return std::to_string(count) + " " + (count == 1 ? singular : plural_form);
}

}  // namespace

// Banner printed before the first hop, e.g.
//   Tracing route to example.com (93.184.216.34), 30 hops max, 3 probes per hop, 32 data bytes
std::string format_header(const std::string& host, const IpAddress& target,
                          const TraceOptions& options) {
    return "Tracing route to " + host + " (" + target.to_string() + "), " +
           plural(options.max_hops, "hop", "hops") + " max, " +
           plural(options.probes_per_hop, "probe", "probes") + " per hop, " +
           std::to_string(options.payload_size) + " data bytes";
}

// One sentence saying how the trace ended. The hop number is taken from the
// last Hop in the result (0 if the trace stopped before the first hop).
std::string format_outcome(const TraceResult& result) {
    const int last_ttl = result.hops.empty() ? 0 : result.hops.back().ttl;
    switch (result.outcome) {
        case TraceOutcome::DestinationReached:
            return "Destination reached at hop " + std::to_string(last_ttl) + ".";
        case TraceOutcome::DestinationUnreachable:
            return "Destination unreachable, reported at hop " + std::to_string(last_ttl) + ".";
        case TraceOutcome::MaxHopsExceeded:
            return "Destination not reached within " + plural(last_ttl, "hop", "hops") + ".";
        case TraceOutcome::Interrupted:
            return "Interrupted.";
        case TraceOutcome::Failed:
            return "Trace failed: " + result.error;
    }
    return {};
}

// The short markers the classic traceroute puts after a probe time when a
// router answered "destination unreachable": !H host, !N network,
// !P protocol (IPv4) or port (IPv6), !F fragmentation needed,
// !X administratively prohibited, !S beyond scope of source address.
// Code numbers differ between IPv4 and IPv6, hence two tables. Unknown
// codes are shown as "!<n>".
std::string unreachable_marker(Family family, int code) {
    if (family == Family::IPv4) {
        switch (code) {
            case 0:
                return "!N";
            case 1:
                return "!H";
            case 2:
                return "!P";
            case 4:
                return "!F";
            case 9:
            case 10:
            case 13:
                return "!X";
            default:
                break;
        }
    } else {
        switch (code) {
            case 0:
                return "!N";
            case 1:
                return "!X";
            case 2:
                return "!S";
            case 3:
                return "!H";
            case 4:
                return "!P";
            default:
                break;
        }
    }
    return "!<" + std::to_string(code) + ">";
}

HopPrinter::HopPrinter(std::ostream& out, Family family, NameLookup lookup)
    : out_(out), family_(family), lookup_(std::move(lookup)) {}

// Start of a line: the hop number right-aligned in two columns (" 1", "12").
// Flushing after every piece is what makes the output appear live, one
// probe at a time, instead of all at once when the buffer fills.
void HopPrinter::hop_started(int ttl) {
    last_responder_.reset();
    out_ << std::setw(2) << ttl << " " << std::flush;
}

// One probe's result, appended to the current line.
void HopPrinter::probe_finished(int, const EchoResult& result) {
    // No answer (or an answer whose sender is unknown): print "*". A
    // marker is still added if we know it was an unreachable report.
    if (result.status == EchoStatus::Timeout || !result.responder) {
        out_ << " *";
        if (result.status == EchoStatus::DestinationUnreachable) {
            out_ << " " << unreachable_marker(family_, result.code);
        }
        out_ << std::flush;
        return;
    }

    // Print the responder's address only when it differs from the previous
    // probe's, so a normal hop reads "10.0.1.1  0.4 ms  0.3 ms  0.3 ms" and
    // a hop where load balancing sends probes through different routers
    // shows each address once.
    if (!last_responder_ || *last_responder_ != *result.responder) {
        out_ << " " << describe(*result.responder);
        last_responder_ = result.responder;
    }
    out_ << "  " << format_milliseconds(result.round_trip.count());
    if (result.status == EchoStatus::DestinationUnreachable) {
        out_ << " " << unreachable_marker(family_, result.code);
    }
    out_ << std::flush;
}

// End of the line.
void HopPrinter::hop_finished(const Hop&) {
    out_ << "\n" << std::flush;
}

// "name (address)" when the address has a host name, otherwise just the
// address. Reverse DNS is slow, so each address is looked up at most once
// and the answer is cached. A name identical to the address text (some
// resolvers return that) is not repeated.
std::string HopPrinter::describe(const IpAddress& address) {
    const std::string text = address.to_string();
    if (!lookup_) {
        return text;
    }
    const auto cached = descriptions_.find(text);
    if (cached != descriptions_.end()) {
        return cached->second;
    }
    const std::optional<std::string> name = lookup_(address);
    std::string description = name && *name != text ? *name + " (" + text + ")" : text;
    descriptions_.emplace(text, description);
    return description;
}

}  // namespace hoptrace