# Changelog

All notable changes to this project are documented in this file.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/)
and the project uses [Semantic Versioning](https://semver.org/).

## [1.0.0] - 2026-10-13

### Added

- Route tracing with ICMP echo probes and an increasing TTL, over IPv4 and IPv6.
- Linux backend with hand-built ICMP packets on a raw socket, and an automatic
  fallback to unprivileged ICMP datagram sockets.
- Windows backend on the IP Helper ICMP API, which needs no administrator rights.
- Live output with one line per hop, reverse DNS names and unreachable markers.
- Options for the first hop, maximum hops, probes per hop, timeout and payload size.
- Unit tests with a scripted network, command-line tests and a loopback test.
- CMake build for Linux and Windows, CI and release workflows.
