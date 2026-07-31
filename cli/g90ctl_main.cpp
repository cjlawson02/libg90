/// Thin CLI over libg90 public API — parse flags/hex, call encode/decode/live; no codec logic here.
#include "g90ctl_live.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <g90/g90.hpp>
#include <glob.h>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

void print_version() {
  std::cout << "g90ctl " << g90::version_string() << "\n";
}

void print_help(std::string_view argv0) {
  std::cout << "Usage: " << argv0 << " [--version] [--help] <command> [args...]\n"
            << "\n"
            << "Commands:\n"
            << "  encode [--profile P] [--mode M] [--freq HZ] [--volume N]\n"
            << "         [--filter-high HZ] [--filter-low HZ] [--fft-scale N]\n"
            << "                    Encode a head frame to hex (library only)\n"
            << "  decode head [--profile P] <hex>\n"
            << "                    Decode a 96-byte head frame from hex\n"
            << "  decode body <hex> Decode a 372-byte body frame from hex\n"
            << "  live --port PATH|--auto-port [--profile 1.81] ...\n"
            << "                    Live DE-9 exchange (requires --confirm-bench)\n"
            << "\n"
            << "live options:\n"
            << "  --port PATH              Serial device (required unless --auto-port)\n"
            << "  --auto-port              Use the single matching USB serial device\n"
            << "  --profile P              Codec profile (default 1.81 / V179)\n"
            << "  --mode M --freq HZ --volume N\n"
            << "  --filter-high HZ --filter-low HZ --fft-scale N\n"
            << "  --timeout MS             Write+hunt timeout (default 3000)\n"
            << "  --confirm-bench          Ack 3.3 V wiring; head unplugged\n"
            << "  --i-know-what-im-doing   Skip confirm (dangerous)\n"
            << "  --arm-ptt|--arm-atu-tune|--arm-vox\n"
            << "                           Encode TX arms (requires --allow-tx to send)\n"
            << "  --allow-tx               Permit TX/PTT/tune/VOX frames\n"
            << "  --verbose                Print TX/RX hex + decoded fields\n"
            << "\n"
            << "Options:\n"
            << "  --version           Print version\n"
            << "  --help, -h          Show this help\n"
            << "\n"
            << "See docs/CLI.md. Library encode is raw (does not auto-clear TX bits).\n"
            << "decode exits 0 when crc_ok; 2 when !crc_ok; 1 on hard errors.\n"
            << "live exits 0 on CRC-valid reply; 1 on hard errors / refused TX.\n"
            << "live refuses TX when head_frame_requests_tx is Err or true unless --allow-tx.\n"
            << "Bench: DE-9 pins 2/3/5 only; never pin 1 (+8 V).\n";
}

void print_bench_reminder() {
  std::cerr << "g90ctl live: bench wiring reminder\n"
            << "  - Unplug the stock head from the body DE-9\n"
            << "  - Use a 3.3 V USB-serial adapter\n"
            << "  - Wire DE-9 pins 2 (TXD), 3 (RXD), 5 (GND) only\n"
            << "  - Never connect adapter power to pin 1 (+8 V)\n"
            << "Pass --confirm-bench to acknowledge, or --i-know-what-im-doing to skip.\n";
}

[[nodiscard]] std::string strip_ws(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    if (c != ' ' && c != '\n' && c != '\r' && c != '\t') {
      out.push_back(c);
    }
  }
  return out;
}

/// Largest wire frame is 372 B; reject oversized hex before allocating.
constexpr std::size_t kMaxHexChars = 2 * g90::kBodyFrameLen;
/// Raw CLI arg may include whitespace between nybbles; cap before strip/copy.
constexpr std::size_t kMaxHexInputChars = kMaxHexChars * 2;

[[nodiscard]] std::optional<std::vector<uint8_t>> parse_hex(std::string_view hex_in) {
  if (hex_in.size() > kMaxHexInputChars) {
    return std::nullopt;
  }
  const std::string hex = strip_ws(hex_in);
  if (hex.size() > kMaxHexChars || hex.size() % 2 != 0) {
    return std::nullopt;
  }
  auto nibble = [](char c) -> int {
    if (c >= '0' && c <= '9') {
      return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
      return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
      return c - 'A' + 10;
    }
    return -1;
  };
  std::vector<uint8_t> out;
  out.reserve(hex.size() / 2);
  for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
    const int hi = nibble(hex[i]);
    const int lo = nibble(hex[i + 1]);
    if (hi < 0 || lo < 0) {
      return std::nullopt;
    }
    out.push_back(static_cast<uint8_t>((hi << 4) | lo));
  }
  return out;
}

void print_hex_line(std::string_view label, std::span<const uint8_t> bytes) {
  std::ostringstream ss;
  ss << label << ' ' << std::hex << std::setfill('0');
  for (uint8_t b : bytes) {
    ss << std::setw(2) << static_cast<unsigned>(b);
  }
  std::cout << ss.str() << '\n';
}

void print_hex(std::span<const uint8_t> bytes) {
  std::ostringstream ss;
  ss << std::hex << std::setfill('0');
  for (uint8_t b : bytes) {
    ss << std::setw(2) << static_cast<unsigned>(b);
  }
  std::cout << ss.str() << '\n';
}

[[nodiscard]] std::vector<std::string> candidate_ports() {
  static constexpr const char* kPatterns[] = {
      "/dev/cu.usbserial*",
      "/dev/cu.usbmodem*",
      "/dev/tty.usbserial*",
      "/dev/ttyUSB*",
      "/dev/ttyACM*",
  };
  std::vector<std::string> found;
  for (const char* pat : kPatterns) {
    glob_t g{};
    const int rc = ::glob(pat, 0, nullptr, &g);
    if (rc == 0) {
      for (std::size_t i = 0; i < g.gl_pathc; ++i) {
        found.emplace_back(g.gl_pathv[i]);
      }
    }
    ::globfree(&g);
  }
  std::sort(found.begin(), found.end());
  found.erase(std::unique(found.begin(), found.end()), found.end());

  std::vector<std::string> cu;
  for (const auto& p : found) {
    if (p.find("/cu.") != std::string::npos) {
      cu.push_back(p);
    }
  }
  return cu.empty() ? found : cu;
}

[[nodiscard]] std::optional<std::string> resolve_port(const g90ctl::LiveArgs& live) {
  if (live.port) {
    return *live.port;
  }
  if (!live.auto_port) {
    std::cerr << "g90ctl live: require --port PATH or --auto-port\n";
    return std::nullopt;
  }
  const auto cands = candidate_ports();
  if (cands.empty()) {
    std::cerr << "g90ctl live: no USB serial ports found; pass --port\n";
    return std::nullopt;
  }
  if (cands.size() > 1) {
    std::cerr << "g90ctl live: multiple USB serial ports — pass --port explicitly:\n";
    for (const auto& c : cands) {
      std::cerr << "  " << c << '\n';
    }
    return std::nullopt;
  }
  return cands.front();
}

[[nodiscard]] int cmd_encode(const std::vector<std::string_view>& args) {
  // encode: reject unknown switches lightly (value flags only).
  for (std::size_t i = 0; i < args.size();) {
    const auto a = args[i];
    if (a.empty() || a[0] != '-') {
      std::cerr << "g90ctl encode: unexpected argument '" << a << "'\n";
      return 1;
    }
    static constexpr std::string_view kOk[] = {
        "--profile",
        "--mode",
        "--freq",
        "--volume",
        "--filter-high",
        "--filter-low",
        "--fft-scale",
    };
    bool known = false;
    for (std::string_view k : kOk) {
      if (a == k) {
        known = true;
        break;
      }
    }
    if (!known) {
      std::cerr << "g90ctl encode: unknown argument '" << a << "'\n";
      return 1;
    }
    if (i + 1 >= args.size() || args[i + 1].empty() || args[i + 1][0] == '-') {
      std::cerr << "g90ctl encode: missing value for " << a << '\n';
      return 1;
    }
    i += 2;
  }

  g90::HeadEncodeRequest req;
  if (const auto err = g90ctl::fill_encode_request(args, req, false, false)) {
    std::cerr << "g90ctl encode: " << *err << '\n';
    return 1;
  }
  const auto frame = g90::encode_head(req);
  if (!frame.ok()) {
    std::cerr << "g90ctl encode: " << g90::to_string(frame.err()) << '\n';
    return 1;
  }
  print_hex(*frame.value);
  return 0;
}

[[nodiscard]] int cmd_decode_head(const std::vector<std::string_view>& args) {
  g90::Profile profile = g90::Profile::V179;
  std::string_view hex_arg;
  for (std::size_t i = 0; i < args.size(); ++i) {
    if (args[i] == "--profile") {
      if (i + 1 >= args.size() || args[i + 1].empty() || args[i + 1][0] == '-') {
        std::cerr << "g90ctl decode head: missing value for --profile\n";
        return 1;
      }
      const auto parsed = g90::parse_profile(args[i + 1]);
      if (!parsed.ok()) {
        std::cerr << "g90ctl decode head: invalid --profile\n";
        return 1;
      }
      profile = *parsed.value;
      ++i;
      continue;
    }
    if (!args[i].empty() && args[i][0] != '-') {
      hex_arg = args[i];
      continue;
    }
    std::cerr << "g90ctl decode head: unknown argument '" << args[i] << "'\n";
    return 1;
  }
  if (hex_arg.empty()) {
    std::cerr << "g90ctl decode head: missing hex\n";
    return 1;
  }
  const auto bytes = parse_hex(hex_arg);
  if (!bytes || bytes->size() != g90::kHeadFrameLen) {
    std::cerr << "g90ctl decode head: expected " << g90::kHeadFrameLen << " bytes hex\n";
    return 1;
  }
  g90::HeadFrame frame{};
  std::copy_n(bytes->begin(), g90::kHeadFrameLen, frame.begin());
  const auto dec = g90::decode_head(profile, frame);
  if (!dec.ok()) {
    std::cerr << "g90ctl decode head: " << g90::to_string(dec.err()) << '\n';
    return 1;
  }
  const auto& d = *dec.value;
  std::cout << "crc_ok=" << (d.crc_ok ? "true" : "false")
            << " mode_a=" << g90::mode_to_string(d.mode_a) << " freq_a_hz=" << d.freq_a_hz
            << " filter=" << d.filter_low_hz << "-" << d.filter_high_hz << "Hz"
            << " fft_scale=" << static_cast<unsigned>(d.fft_scale)
            << " volume=" << static_cast<unsigned>(d.volume) << '\n';
  return d.crc_ok ? 0 : 2;
}

[[nodiscard]] int cmd_decode_body(const std::vector<std::string_view>& args) {
  std::string_view hex_arg;
  for (std::string_view a : args) {
    if (!a.empty() && a[0] != '-') {
      if (!hex_arg.empty()) {
        std::cerr << "g90ctl decode body: unexpected argument '" << a << "'\n";
        return 1;
      }
      hex_arg = a;
      continue;
    }
    std::cerr << "g90ctl decode body: unknown argument '" << a << "'\n";
    return 1;
  }
  if (hex_arg.empty()) {
    std::cerr << "g90ctl decode body: missing hex\n";
    return 1;
  }
  const auto bytes = parse_hex(hex_arg);
  if (!bytes || bytes->size() != g90::kBodyFrameLen) {
    std::cerr << "g90ctl decode body: expected " << g90::kBodyFrameLen << " bytes hex\n";
    return 1;
  }
  g90::BodyFrame frame{};
  std::copy_n(bytes->begin(), g90::kBodyFrameLen, frame.begin());
  const auto dec = g90::decode_body(frame);
  if (!dec.ok()) {
    std::cerr << "g90ctl decode body: " << g90::to_string(dec.err()) << '\n';
    return 1;
  }
  const auto& d = *dec.value;
  std::cout << "crc_ok=" << (d.crc_ok ? "true" : "false")
            << " S=" << static_cast<int>(d.status.s_meter)
            << " Po=" << static_cast<unsigned>(d.status.po)
            << " SWR=" << static_cast<unsigned>(d.status.swr) << " V=" << d.status.voltage_v()
            << " fft_sum=" << d.fingerprint.sum << " fft_peak=" << d.fingerprint.max << '@'
            << d.fingerprint.argmax << '\n';
  return d.crc_ok ? 0 : 2;
}

[[nodiscard]] int cmd_live(const std::vector<std::string_view>& args) {
  g90ctl::LiveArgs live;
  if (const auto err = g90ctl::parse_live_args(args, live)) {
    std::cerr << "g90ctl live: " << *err << '\n';
    return 1;
  }

  g90::HeadFrame frame{};
  if (const auto err = g90ctl::live_prepare_frame(live, frame)) {
    if (*err == "bench confirm required (--confirm-bench or --i-know-what-im-doing)") {
      print_bench_reminder();
      return 1;
    }
    std::cerr << "g90ctl live: " << *err << '\n';
    return 1;
  }

  const auto port_path = resolve_port(live);
  if (!port_path) {
    return 1;
  }

  auto serial = std::make_unique<g90::de9::SerialPort>();
  const auto opened = serial->open(port_path->c_str());
  if (!opened.ok()) {
    std::cerr << "g90ctl live: failed to open " << *port_path << ": "
              << g90::to_string(opened.err()) << '\n';
    return 1;
  }

  g90::De9Link link(std::move(serial), live.req.profile);
  const auto body = link.exchange(live.req, std::chrono::milliseconds{live.timeout_ms});
  if (!body.ok()) {
    std::cerr << "g90ctl live: " << g90::to_string(body.err()) << '\n';
    return 1;
  }

  const auto& d = *body.value;
  if (live.verbose) {
    if (const auto* tx = link.last_tx()) {
      print_hex_line("tx", *tx);
    }
    if (const auto* rx = link.last_rx()) {
      print_hex_line("rx", *rx);
    }
    std::cout << "crc_ok=" << (d.crc_ok ? "true" : "false")
              << " S=" << static_cast<int>(d.status.s_meter)
              << " Po=" << static_cast<unsigned>(d.status.po)
              << " SWR=" << static_cast<unsigned>(d.status.swr) << " V=" << d.status.voltage_v()
              << " alc=" << static_cast<unsigned>(d.status.alc)
              << " version=" << static_cast<unsigned>(d.status.version)
              << " fft_sum=" << d.fingerprint.sum << " fft_peak=" << d.fingerprint.max << '@'
              << d.fingerprint.argmax << '\n';
  } else {
    std::cout << "crc_ok=" << (d.crc_ok ? "true" : "false")
              << " S=" << static_cast<int>(d.status.s_meter)
              << " Po=" << static_cast<unsigned>(d.status.po)
              << " SWR=" << static_cast<unsigned>(d.status.swr) << " V=" << d.status.voltage_v()
              << " fft_peak=" << d.fingerprint.max << '@' << d.fingerprint.argmax << '\n';
  }
  // hunt_body only returns CRC-valid frames; exit 0 on success.
  return 0;
}

} // namespace

int main(int argc, char** argv) {
  const std::vector<std::string_view> args(argv, argv + argc);
  if (args.size() < 2) {
    print_help(args.empty() ? "g90ctl" : args[0]);
    return 0;
  }

  const std::string_view a1 = args[1];
  if (a1 == "--version" || a1 == "-V") {
    print_version();
    return 0;
  }
  if (a1 == "--help" || a1 == "-h") {
    print_help(args[0]);
    return 0;
  }

  if (a1 == "encode") {
    return cmd_encode({args.begin() + 2, args.end()});
  }
  if (a1 == "decode") {
    if (args.size() < 3) {
      std::cerr << "g90ctl: decode requires 'head' or 'body'\n";
      return 1;
    }
    const std::string_view what = args[2];
    if (what == "head") {
      return cmd_decode_head({args.begin() + 3, args.end()});
    }
    if (what == "body") {
      return cmd_decode_body({args.begin() + 3, args.end()});
    }
    std::cerr << "g90ctl: unknown decode target '" << what << "'\n";
    return 1;
  }
  if (a1 == "live") {
    return cmd_live({args.begin() + 2, args.end()});
  }

  std::cerr << "g90ctl: unknown command '" << a1 << "'\n";
  print_help(args[0]);
  return 1;
}
