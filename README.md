# Hop-Trace

An implementation of `traceroute` in C++17 for Linux and Windows. It shows every
router a packet passes through on its way to a host, and how long each one takes
to answer.

*Български: [README.bg.md](README.bg.md)*

```console
$ hoptrace -n 10.0.3.2
Tracing route to 10.0.3.2 (10.0.3.2), 30 hops max, 3 probes per hop, 32 data bytes
 1  10.0.1.1  0.057 ms  0.009 ms  0.014 ms
 2  10.0.2.2  0.015 ms  0.008 ms  0.007 ms
 3  10.0.3.2  0.017 ms  0.008 ms  0.008 ms
Destination reached at hop 3.
```

## Features

- IPv4 and IPv6, host names or numeric addresses.
- Live output: each probe is printed as soon as its answer arrives.
- Host names of the routers through reverse DNS, or addresses only with `-n`.
- On Linux the ICMP packets are built and parsed by hand, including the checksum.
- Runs without root on Linux when the system allows unprivileged ICMP sockets,
  and without administrator rights on Windows.
- Stops early when a router reports the destination unreachable, and says why.
- Configurable first hop, maximum hops, probes per hop, timeout and payload size.
- No third-party dependencies.

## Quick start

You need CMake 3.20 or newer and a C++17 compiler.

**Linux**

```sh
git clone https://github.com/alexDzhen/Hop-Trace.git
cd hoptrace
./scripts/build.sh
sudo ./build/hoptrace example.com
```

To install it and run it without `sudo` from then on:

```sh
./scripts/install.sh
hoptrace example.com
```

**Windows**

Install Visual Studio 2022 with the "Desktop development with C++" workload and run
the commands in "Developer PowerShell for VS 2022", where `cmake` is already available.

```powershell
git clone https://github.com/alexDzhen/Hop-Trace.git
cd hoptrace
powershell -ExecutionPolicy Bypass -File scripts\build.ps1
.\build\Release\hoptrace.exe example.com
```

The scripts run these three commands, which you can also type yourself:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Ready-made binaries for both systems are attached to every
[release](https://github.com/alexDzhen/hoptrace/releases). The Linux binary is built
on GitHub's current Ubuntu image and needs a distribution at least that recent. On
older systems, build from source.

## Usage

```text
hoptrace [options] <host>

  -m, --max-hops <n>     Give up after this many hops (default 30)
  -f, --first-hop <n>    Start at this hop instead of the first (default 1)
  -q, --probes <n>       Probes sent to each hop, 1-10 (default 3)
  -w, --timeout <sec>    Seconds to wait for each answer (default 2)
  -s, --size <bytes>     Payload size in bytes (default 32)
  -n, --numeric          Print addresses only, do not look up host names
  -4                     Use IPv4 only
  -6                     Use IPv6 only
  -h, --help             Show help and exit
  -V, --version          Show the version and exit
```

### Reading the output

Each line is one hop: the hop number, the router's address (with its name when it
has one) and the round-trip time of every probe.

| Mark  | Meaning                                                             |
|-------|---------------------------------------------------------------------|
| `*`   | No answer before the timeout. Many routers simply do not answer.    |
| `!H`  | A router reported the destination host unreachable                  |
| `!N`  | A router reported the destination network unreachable               |
| `!P`  | Protocol unreachable (IPv4) or port unreachable (IPv6)              |
| `!X`  | Communication administratively prohibited (a firewall said no)      |
| `!F`  | Fragmentation needed (IPv4)                                         |
| `!S`  | Beyond the scope of the source address (IPv6)                       |
| `!<n>`| Any other "destination unreachable" code, shown as its number       |

A destination behind routers that stay silent:

```console
$ hoptrace -n -w 1 -m 5 10.8.8.8
Tracing route to 10.8.8.8 (10.8.8.8), 5 hops max, 3 probes per hop, 32 data bytes
 1  10.0.1.1  0.045 ms  0.009 ms  0.008 ms
 2  * * *
 3  * * *
 4  * * *
 5  * * *
Destination not reached within 5 hops.
```

A destination that the second router cannot reach:

```console
$ hoptrace -n -q 2 10.9.9.9
Tracing route to 10.9.9.9 (10.9.9.9), 30 hops max, 2 probes per hop, 32 data bytes
 1  10.0.1.1  0.043 ms  0.005 ms
 2  10.0.2.2  0.010 ms !H  0.005 ms !H
Destination unreachable, reported at hop 2.
```

The examples were recorded on a small test network with two routers between the
source and `10.0.3.2`. For a quick trace over the internet, `hoptrace -n -q 1 -w 1 <host>`
sends one probe per hop and does not wait for DNS.

| Exit code | Meaning                                         |
|-----------|-------------------------------------------------|
| 0         | The destination was reached                     |
| 1         | The destination was not reached                 |
| 2         | Usage error, unknown host or no permission      |

## Permissions on Linux

Sending ICMP needs a special socket. `hoptrace` tries two kinds, in this order:

1. A **raw socket**. It needs root or the `CAP_NET_RAW` capability.
2. An **ICMP datagram socket**. It needs no privileges, but the system must allow
   it through the `net.ipv4.ping_group_range` setting. Most current
   distributions do.

So one of these is enough:

```sh
sudo hoptrace example.com                                   # run as root
sudo setcap cap_net_raw+ep /usr/local/bin/hoptrace          # grant the capability once
sudo sysctl -w net.ipv4.ping_group_range="0 2147483647"     # allow unprivileged ICMP
```

`scripts/install.sh` does the second one for you. On Windows nothing is needed.

## How it works

### The idea: let packets expire on purpose

Every IP packet carries a TTL field ("time to live", called "hop limit" in IPv6).
Each router that forwards the packet decreases it by one. A router that would
have to forward a packet whose TTL has reached zero drops it and sends an ICMP
"time exceeded" message back to the sender. That message comes from the router's
own address.

`hoptrace` uses this to make every router on the path introduce itself:

```text
TTL=1   source ──▶ router 1  ✗ expired     router 1 answers "time exceeded"
TTL=2   source ──▶ router 1 ──▶ router 2  ✗ expired     router 2 answers
TTL=3   source ──▶ router 1 ──▶ router 2 ──▶ target     target answers "echo reply"
```

The probes are ICMP echo requests, the same packets `ping` sends. The algorithm
in [`tracer.cpp`](src/tracer.cpp) is:

1. Start with TTL = 1.
2. Send a probe with that TTL and wait for an answer or the timeout. Repeat for
   the configured number of probes.
3. A "time exceeded" answer reveals the router at this distance.
4. An "echo reply" means the probe reached the destination: finish.
5. A "destination unreachable" answer means the path ends here: finish.
6. Otherwise increase the TTL by one and go back to step 2, until the maximum.

### Matching answers to probes

Every probe gets its own sequence number. An ICMP error message quotes the
beginning of the packet that caused it, IP header included, so the identifier and
sequence number of the original probe can be read back from the quoted copy
([`icmp.cpp`](src/icmp.cpp)). Late answers to earlier probes carry an old sequence
number and are ignored.

```text
ICMP "time exceeded" from a router
+--------------------------+
| ICMP header (type 11)    |
+--------------------------+
| quoted IP header         |  the header of our probe
+--------------------------+
| quoted ICMP header       |  type 8, our identifier and sequence number
+--------------------------+
```

The round-trip time is measured on a monotonic clock between sending the probe
and receiving its answer.

### Linux and Windows backends

The tracer talks to the network through one small interface,
[`IcmpClient`](include/icmp_client.hpp): send one echo request with a
given TTL, get one result. There are two implementations.

| | Linux | Windows |
|---|---|---|
| File | [`src/linux/icmp_client_linux.cpp`](src/linux/icmp_client_linux.cpp) | [`src/windows/icmp_client_windows.cpp`](src/windows/icmp_client_windows.cpp) |
| Mechanism | Raw socket, or ICMP datagram socket | IP Helper API (`IcmpSendEcho2`, `Icmp6SendEcho2`) |
| Who builds the packet | `hoptrace` | Windows |
| TTL | `IP_TTL` / `IPV6_UNICAST_HOPS` socket option | `IP_OPTION_INFORMATION::Ttl` |
| Privileges | Root, `CAP_NET_RAW` or `ping_group_range` | None |

Windows restricts raw sockets to administrators, and its firewall drops incoming
ICMP "time exceeded" messages unless a rule allows them. The IP Helper API has
neither problem, which is why the system `tracert.exe` uses it as well.

On a Linux datagram socket the kernel delivers router errors through the socket's
error queue (`IP_RECVERR`) instead of the normal receive path. The backend
handles both socket types.

Because the tracer only depends on the interface, the unit tests replace the
network with a scripted fake and check the algorithm hop by hop.

### What a trace can and cannot tell you

- Routers are free to ignore expired packets or to rate-limit their answers, so
  `* * *` does not mean the path is broken.
- Routes can change between probes, and load balancers can send probes of the same
  hop through different routers. When that happens a line shows more than one address.
- The times include the router's own delay in producing the answer, so a middle
  hop can look slower than the destination.

## Install and package

```sh
./scripts/install.sh                       # Linux: /usr/local/bin + CAP_NET_RAW
cmake --install build --config Release --prefix C:\Tools\hoptrace   # Windows
```

To produce a distributable archive (`.tar.gz` on Linux, `.zip` on Windows):

```sh
cpack --config build/CPackConfig.cmake -C Release -B dist
```

Pushing a tag such as `v1.0.0` runs the release workflow, which builds, tests and
attaches both archives to a GitHub release.

## Tests

The test suite uses [doctest](https://github.com/doctest/doctest), which is included
in `third_party/`, so nothing is downloaded during the build.

- Unit tests for the tracing algorithm against a scripted network: normal paths,
  silent routers, unreachable destinations, hop limits, interrupts and send errors.
- Unit tests for the checksum, packet building and parsing, the hop printer and
  the command-line parser.
- Command-line tests that run the real binary.
- A loopback test that traces `127.0.0.1`. It is skipped when the system does not
  permit ICMP sockets for the current user.

```sh
ctest --test-dir build -C Release --output-on-failure
```

## Project layout

```text
hoptrace/
├── app/main.cpp              Program entry point
├── include/                  Public headers
├── src/                      Implementation
│   ├── linux/                  Raw and datagram socket backend
│   ├── windows/                IP Helper backend
│   ├── platform/               sockaddr conversions
│   └── *.cpp                   Portable implementation
├── tests/                    Unit and command-line tests
├── third_party/doctest/      Test framework (single header)
├── scripts/                  Build and install scripts
└── .github/workflows/        CI and release pipelines
```

## License

[MIT](LICENSE)
