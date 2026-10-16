// cli.cpp
// Parses the command line into a CliOptions struct and provides the --help
// text. No external argument-parsing library is used; the parser is small
// enough to write by hand. Anything wrong on the command line is reported by
// throwing UsageError, which main() turns into a message and exit code 2.
//
// Supported forms:  -m 5   --max-hops 5   --max-hops=5   and "--" to end options.

#include "../include/cli.hpp"

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <optional>

namespace hoptrace {

namespace {

// Converts `text` to a whole number and checks it is within [min, max].
// strtoll alone is too forgiving (it accepts "12abc" and reads 12), so three
// extra checks make it strict:
//   - the text is not empty,
//   - `end` points at the end of the text, i.e. every character was consumed,
//   - errno is still 0, i.e. the number did not overflow.
// `option` is only used to name the option in the error message.
long long parse_integer(const std::string& option, const std::string& text, long long min,
                        long long max) {
    char* end = nullptr;
    errno = 0;
    const long long value = std::strtoll(text.c_str(), &end, 10);
    const bool numeric = !text.empty() && end == text.c_str() + text.size() && errno == 0;
    if (!numeric || value < min || value > max) {
        throw UsageError("option '" + option + "' expects a whole number from " +
                         std::to_string(min) + " to " + std::to_string(max) + ", got '" + text +
                         "'");
    }
    return value;
}

// Same idea for a time given in seconds, which may be fractional ("0.5").
// The result is converted to milliseconds, since that is what the socket
// code works with. isfinite() rejects "inf" and "nan", which strtod accepts.
std::chrono::milliseconds parse_seconds(const std::string& option, const std::string& text) {
    constexpr double kMinSeconds = 0.01;
    constexpr double kMaxSeconds = 3600.0;

    char* end = nullptr;
    errno = 0;
    const double value = std::strtod(text.c_str(), &end);
    const bool numeric =
        !text.empty() && end == text.c_str() + text.size() && errno == 0 && std::isfinite(value);
    if (!numeric || value < kMinSeconds || value > kMaxSeconds) {
        throw UsageError("option '" + option + "' expects seconds from 0.01 to 3600, got '" + text +
                         "'");
    }
    return std::chrono::milliseconds{std::llround(value * 1000.0)};
}

// One argument split into its option name and, for the "--name=value" form,
// the value that came with it.
struct Option {
    std::string name;
    std::optional<std::string> inline_value;
};

// "--max-hops=5" -> {"--max-hops", "5"};  "--max-hops" or "-m" -> {name, no value}.
// Only long options (two dashes) support the "=" form.
Option split_option(const std::string& arg) {
    const std::size_t equals = arg.find('=');
    if (arg.compare(0, 2, "--") == 0 && equals != std::string::npos) {
        return {arg.substr(0, equals), arg.substr(equals + 1)};
    }
    return {arg, std::nullopt};
}

}  // namespace

// Walks the arguments (everything after the program name) once, from left
// to right. Options are recognised by their leading '-'; everything else is
// collected as a "positional" argument, of which exactly one (the host) is
// expected.
CliOptions parse_cli(const std::vector<std::string>& args) {
    CliOptions options;
    std::vector<std::string> positional;
    bool options_ended = false;

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];

        // Not an option: a bare word, a lone "-", or anything after "--".
        if (options_ended || arg.size() < 2 || arg[0] != '-') {
            positional.push_back(arg);
            continue;
        }
        // "--" means "no more options", so a host that starts with '-'
        // can still be given.
        if (arg == "--") {
            options_ended = true;
            continue;
        }

        const Option option = split_option(arg);
        const std::string& name = option.name;

        // Fetches the option's value: either the part after '=' or the next
        // argument. Note the ++i, which skips that next argument so the main
        // loop does not see it again. Lambdas keep this logic in one place
        // for all options that take a value.
        const auto value = [&]() -> std::string {
            if (option.inline_value) {
                return *option.inline_value;
            }
            if (i + 1 >= args.size()) {
                throw UsageError("option '" + name + "' needs a value");
            }
            return args[++i];
        };
        // For flags: "--numeric=yes" is a mistake and is reported as one.
        const auto no_value = [&] {
            if (option.inline_value) {
                throw UsageError("option '" + name + "' does not take a value");
            }
        };

        // The option table. Each value is range-checked as it is parsed.
        // The trace settings go into the nested TraceOptions struct that
        // trace_route() takes.
        if (name == "-h" || name == "--help") {
            no_value();
            options.show_help = true;
        } else if (name == "-V" || name == "--version") {
            no_value();
            options.show_version = true;
        } else if (name == "-4") {
            options.family = FamilyPreference::IPv4;
        } else if (name == "-6") {
            options.family = FamilyPreference::IPv6;
        } else if (name == "-n" || name == "--numeric") {
            no_value();
            options.numeric = true;
        } else if (name == "-m" || name == "--max-hops") {
            options.trace.max_hops = static_cast<int>(parse_integer(name, value(), 1, 255));
        } else if (name == "-f" || name == "--first-hop") {
            options.trace.first_hop = static_cast<int>(parse_integer(name, value(), 1, 255));
        } else if (name == "-q" || name == "--probes") {
            options.trace.probes_per_hop = static_cast<int>(parse_integer(name, value(), 1, 10));
        } else if (name == "-w" || name == "--timeout") {
            options.trace.timeout = parse_seconds(name, value());
        } else if (name == "-s" || name == "--size") {
            options.trace.payload_size = static_cast<std::size_t>(
                parse_integer(name, value(), 0, static_cast<long long>(kMaxPayloadSize)));
        } else {
            throw UsageError("unknown option '" + arg + "'");
        }
    }

    // --help and --version work without a host, so check them before
    // complaining about anything else.
    if (options.show_help || options.show_version) {
        return options;
    }
    // A check that involves two options at once, so it can only be done
    // after the whole command line has been read.
    if (options.trace.first_hop > options.trace.max_hops) {
        throw UsageError("the first hop (" + std::to_string(options.trace.first_hop) +
                         ") must not be greater than the maximum (" +
                         std::to_string(options.trace.max_hops) + ")");
    }
    if (positional.empty()) {
        throw UsageError("missing host");
    }
    if (positional.size() > 1) {
        throw UsageError("expected one host, got " + std::to_string(positional.size()));
    }
    options.host = positional.front();
    return options;
}

// The text shown by --help. Adjacent string literals are joined by the
// compiler into one string, which keeps the layout readable in the source.
std::string usage_text() {
    return "Usage: hoptrace [options] <host>\n"
           "\n"
           "Shows the routers a packet passes through on its way to a host.\n"
           "\n"
           "Options:\n"
           "  -m, --max-hops <n>     Give up after this many hops (default 30)\n"
           "  -f, --first-hop <n>    Start at this hop instead of the first (default 1)\n"
           "  -q, --probes <n>       Probes sent to each hop, 1-10 (default 3)\n"
           "  -w, --timeout <sec>    Seconds to wait for each answer (default 2)\n"
           "  -s, --size <bytes>     Payload size in bytes (default 32)\n"
           "  -n, --numeric          Print addresses only, do not look up host names\n"
           "  -4                     Use IPv4 only\n"
           "  -6                     Use IPv6 only\n"
           "  -h, --help             Show this help and exit\n"
           "  -V, --version          Show the version and exit\n"
           "\n"
           "Examples:\n"
           "  hoptrace example.com\n"
           "  hoptrace -n -q 1 -w 1 8.8.8.8\n"
           "  hoptrace -6 example.com\n"
           "\n"
           "Each line is one hop: its address and the round-trip time of every probe.\n"
           "A '*' means no answer arrived before the timeout.\n"
           "\n"
           "Exit status: 0 if the destination was reached, 1 if it was not, 2 on other errors.\n";
}

}  // namespace hoptrace