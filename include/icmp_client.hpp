// icmp_client.hpp
// The boundary between the portable part of the program and the operating
// system. IcmpClient is an abstract interface with one job: send one echo
// request with a given TTL, return one result. There are two
// implementations, one per OS (icmp_client_linux.cpp and
// icmp_client_windows.cpp), and only one is compiled into a given build.
// The tracer (tracer.cpp) talks to this interface only, which is also what
// lets the unit tests replace the real network with a scripted fake.
//
// This header also defines the data that crosses that boundary:
// EchoRequest (what to send) and EchoResult (what came back).

#pragma once

#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

#include "ip_address.hpp"

namespace hoptrace {

/// Thrown when the network cannot be used at all (no permission, no socket).
// Raised by create(); once a client exists, per-probe problems are
// reported through EchoResult instead of exceptions.
class NetworkError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Every possible outcome of one probe, independent of the OS. For the
// tracer: TimeExceeded = "found a router at this hop", Reply = "reached the
// destination", DestinationUnreachable = "the path ends here".
enum class EchoStatus {
    /// The target answered with an echo reply.
    Reply,
    /// A router dropped the request because its TTL reached zero.
    TimeExceeded,
    /// A router or the target reported that the destination cannot be reached.
    DestinationUnreachable,
    /// Nothing arrived before the timeout.
    Timeout,
    /// Waiting was cancelled through the interrupt flag.
    Interrupted,
    /// The request could not be sent; see EchoResult::error.
    Error,
};

// Parameters of one probe. The tracer sets `ttl` to the hop number being
// explored and gives every probe a fresh sequence number.
struct EchoRequest {
    std::uint16_t sequence = 0;
    std::size_t payload_size = 32;
    /// Time to live of the outgoing packet. 0 keeps the system default.
    int ttl = 0;
    std::chrono::milliseconds timeout{2000};
};

// What echo() reports back. Which fields are meaningful depends on `status`:
// a Reply fills responder, round_trip, ttl and bytes; a router error fills
// responder, round_trip and code; an Error fills `error`.
// The default status is Timeout, so a default-constructed EchoResult already
// means "no answer" (printed as "*").
struct EchoResult {
    EchoStatus status = EchoStatus::Timeout;
    /// Who answered: the target for a reply, a router for an error.
    // For the tracer this is the field that matters most: the router's
    // address that goes on the hop line.
    std::optional<IpAddress> responder;
    // Measured as a floating-point number of milliseconds.
    std::chrono::duration<double, std::milli> round_trip{0.0};
    /// TTL (hop limit) of the answer, or -1 when the system does not report it.
    int ttl = -1;
    /// Size of the echo reply in bytes, ICMP header included.
    std::size_t bytes = 0;
    /// ICMP code of a "destination unreachable" answer.
    // Turned into the "!H", "!N", "!X" markers in the output.
    int code = 0;
    std::string error;
};

/// Sends ICMP echo requests to one target and waits for the answers.
///
/// On Linux this is a raw ICMP socket, or an unprivileged ICMP datagram socket
/// when raw sockets are not permitted. On Windows it is the IP Helper ICMP API.
class IcmpClient {
public:
    /// Opens the transport for `target`. Throws NetworkError on failure.
    // Factory function: declared here, defined separately by each platform
    // file. Returns the client through a unique_ptr because the concrete
    // type is hidden and the caller must own it polymorphically.
    static std::unique_ptr<IcmpClient> create(const IpAddress& target);

    // Virtual destructor so deleting through an IcmpClient* destroys the
    // derived object correctly (closes the socket / handle).
    virtual ~IcmpClient() = default;
    // A client owns an OS resource, so copying makes no sense.
    IcmpClient(const IcmpClient&) = delete;
    IcmpClient& operator=(const IcmpClient&) = delete;

    /// Sends one echo request and blocks until its answer or the timeout.
    // Pure virtual: each platform (and the test fake) must provide it.
    virtual EchoResult echo(const EchoRequest& request) = 0;

    /// Registers a flag, typically set by a signal handler. When it becomes
    /// non-zero a pending echo() returns EchoStatus::Interrupted.
    // sig_atomic_t and volatile are what C++ guarantees can be safely written
    // from a signal handler and read from normal code. main() keeps the flag
    // and its Ctrl+C handler sets it to 1.
    void set_interrupt_flag(const volatile std::sig_atomic_t* flag) noexcept {
        interrupt_flag_ = flag;
    }

protected:
    // Only derived classes may construct one (use create() instead).
    IcmpClient() = default;

    // For the implementations to poll inside their wait loops.
    bool interrupted() const noexcept {
        return interrupt_flag_ != nullptr && *interrupt_flag_ != 0;
    }

private:
    const volatile std::sig_atomic_t* interrupt_flag_ = nullptr;
};

}  // namespace hoptrace