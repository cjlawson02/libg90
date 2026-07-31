#pragma once

/// Testable live-command policy for g90ctl (no I/O). Included by CLI and unit tests.
#include <charconv>
#include <g90/g90.hpp>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace g90ctl {

struct LiveArgs {
  g90::HeadEncodeRequest req{};
  int timeout_ms = 3000;
  bool confirm_bench = false;
  bool skip_confirm = false;
  bool allow_tx = false;
  bool verbose = false;
  bool auto_port = false;
  std::optional<std::string> port;
  bool arm_ptt = false;
  bool arm_atu_tune = false;
  bool arm_vox = false;
};

[[nodiscard]] inline bool live_tx_blocked(const g90::HeadFrame& frame, bool allow_tx) {
  if (allow_tx) {
    return false;
  }
  const auto tx = g90::head_frame_requests_tx(frame);
  if (!tx.ok()) {
    return true;
  }
  return *tx.value;
}

[[nodiscard]] inline bool has_flag(const std::vector<std::string_view>& args,
                                   std::string_view name) {
  for (std::string_view a : args) {
    if (a == name) {
      return true;
    }
  }
  return false;
}

/// @return Value after `name`, or nullopt if absent. Sets `missing_value` if flag has no value.
[[nodiscard]] inline std::optional<std::string_view>
flag_value(const std::vector<std::string_view>& args, std::string_view name, bool& missing_value) {
  missing_value = false;
  for (std::size_t i = 0; i < args.size(); ++i) {
    if (args[i] != name) {
      continue;
    }
    if (i + 1 >= args.size() || args[i + 1].empty() || args[i + 1][0] == '-') {
      missing_value = true;
      return std::nullopt;
    }
    return args[i + 1];
  }
  return std::nullopt;
}

template<class T>
[[nodiscard]] inline std::optional<T> parse_u(std::string_view s, T max_inclusive) {
  if (s.empty()) {
    return std::nullopt;
  }
  unsigned long long v = 0;
  const auto* begin = s.data();
  const auto* end = s.data() + s.size();
  const auto [ptr, ec] = std::from_chars(begin, end, v, 10);
  if (ec != std::errc{} || ptr != end) {
    return std::nullopt;
  }
  if (v > static_cast<unsigned long long>(max_inclusive)) {
    return std::nullopt;
  }
  return static_cast<T>(v);
}

/// Known live tokens. Values must follow value-taking flags.
inline constexpr std::string_view kLiveValueFlags[] = {
    "--port",
    "--profile",
    "--mode",
    "--freq",
    "--volume",
    "--filter-high",
    "--filter-low",
    "--fft-scale",
    "--timeout",
};

inline constexpr std::string_view kLiveSwitchFlags[] = {
    "--confirm-bench",
    "--i-know-what-im-doing",
    "--allow-tx",
    "--verbose",
    "--auto-port",
    "--arm-ptt",
    "--arm-atu-tune",
    "--arm-vox",
};

[[nodiscard]] inline bool is_live_value_flag(std::string_view s) {
  for (std::string_view f : kLiveValueFlags) {
    if (s == f) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] inline bool is_live_switch_flag(std::string_view s) {
  for (std::string_view f : kLiveSwitchFlags) {
    if (s == f) {
      return true;
    }
  }
  return false;
}

/// Reject unknown tokens and flag-without-value. Does not resolve ports.
[[nodiscard]] inline std::optional<std::string>
validate_live_tokens(const std::vector<std::string_view>& args) {
  for (std::size_t i = 0; i < args.size(); ++i) {
    const std::string_view a = args[i];
    if (is_live_switch_flag(a)) {
      continue;
    }
    if (is_live_value_flag(a)) {
      if (i + 1 >= args.size() || args[i + 1].empty() ||
          (args[i + 1][0] == '-' && args[i + 1] != "-")) {
        return std::string("missing value for ") + std::string(a);
      }
      ++i;
      continue;
    }
    return std::string("unknown argument '") + std::string(a) + "'";
  }
  return std::nullopt;
}

/// Fill encode/live request fields shared by encode and live. `for_live` enables arm flags.
[[nodiscard]] inline std::optional<std::string>
fill_encode_request(const std::vector<std::string_view>& args,
                    g90::HeadEncodeRequest& req,
                    bool for_live,
                    bool allow_tx_cli) {
  req = g90::HeadEncodeRequest{};
  req.profile = g90::Profile::V179;

  bool missing = false;
  if (const auto p = flag_value(args, "--profile", missing)) {
    const auto parsed = g90::parse_profile(*p);
    if (!parsed.ok()) {
      return "invalid --profile";
    }
    req.profile = *parsed.value;
  } else if (missing) {
    return "missing value for --profile";
  }
  if (const auto m = flag_value(args, "--mode", missing)) {
    const auto parsed = g90::mode_from_string(*m);
    if (!parsed.ok()) {
      return "invalid --mode";
    }
    req.vfo_a.mode = *parsed.value;
  } else if (missing) {
    return "missing value for --mode";
  }
  if (const auto f = flag_value(args, "--freq", missing)) {
    const auto v = parse_u<uint32_t>(*f, std::numeric_limits<uint32_t>::max());
    if (!v) {
      return "invalid --freq";
    }
    req.vfo_a.freq_hz = *v;
  } else if (missing) {
    return "missing value for --freq";
  }
  if (const auto v = flag_value(args, "--volume", missing)) {
    const auto n = parse_u<uint8_t>(*v, 255);
    if (!n) {
      return "invalid --volume (0..255)";
    }
    req.volume = *n;
  } else if (missing) {
    return "missing value for --volume";
  }
  if (const auto h = flag_value(args, "--filter-high", missing)) {
    const auto n = parse_u<uint16_t>(*h, 65535);
    if (!n) {
      return "invalid --filter-high";
    }
    req.vfo_a.filter_high_hz = *n;
  } else if (missing) {
    return "missing value for --filter-high";
  }
  if (const auto l = flag_value(args, "--filter-low", missing)) {
    const auto n = parse_u<uint16_t>(*l, 65535);
    if (!n) {
      return "invalid --filter-low";
    }
    req.vfo_a.filter_low_hz = *n;
  } else if (missing) {
    return "missing value for --filter-low";
  }
  if (const auto s = flag_value(args, "--fft-scale", missing)) {
    const auto n = parse_u<uint8_t>(*s, 15);
    if (!n) {
      return "invalid --fft-scale";
    }
    req.vfo_a.fft_scale = *n;
  } else if (missing) {
    return "missing value for --fft-scale";
  }

  if (for_live) {
    g90::LabExtras lab;
    const bool arm_ptt = has_flag(args, "--arm-ptt");
    const bool arm_atu = has_flag(args, "--arm-atu-tune");
    const bool arm_vox = has_flag(args, "--arm-vox");
    if (arm_ptt || arm_atu || arm_vox) {
      // Encode arms into the frame so head_frame_requests_tx can see them; CLI
      // --allow-tx still required before the port is opened.
      lab.allow_tx = true;
      lab.arm_ptt = arm_ptt;
      lab.arm_atu_tune = arm_atu;
      lab.arm_vox = arm_vox;
      req.lab = lab;
    }
    (void)allow_tx_cli;
  }
  return std::nullopt;
}

[[nodiscard]] inline std::optional<std::string>
parse_live_args(const std::vector<std::string_view>& args, LiveArgs& out) {
  if (const auto err = validate_live_tokens(args)) {
    return err;
  }

  out = LiveArgs{};
  out.confirm_bench = has_flag(args, "--confirm-bench");
  out.skip_confirm = has_flag(args, "--i-know-what-im-doing");
  out.allow_tx = has_flag(args, "--allow-tx");
  out.verbose = has_flag(args, "--verbose");
  out.auto_port = has_flag(args, "--auto-port");
  out.arm_ptt = has_flag(args, "--arm-ptt");
  out.arm_atu_tune = has_flag(args, "--arm-atu-tune");
  out.arm_vox = has_flag(args, "--arm-vox");

  bool missing = false;
  if (const auto p = flag_value(args, "--port", missing)) {
    out.port = std::string(*p);
  } else if (missing) {
    return "missing value for --port";
  }

  if (const auto t = flag_value(args, "--timeout", missing)) {
    const auto n = parse_u<unsigned>(*t, 600000u);
    if (!n) {
      return "invalid --timeout";
    }
    out.timeout_ms = static_cast<int>(*n);
  } else if (missing) {
    return "missing value for --timeout";
  }

  if (const auto err = fill_encode_request(args, out.req, true, out.allow_tx)) {
    return err;
  }

  if (out.port && out.auto_port) {
    return "pass either --port or --auto-port, not both";
  }
  return std::nullopt;
}

/// Build the frame that live would send; apply TX gate. Does not open a port.
/// @return nullopt on success (frame ready); else error message.
[[nodiscard]] inline std::optional<std::string> live_prepare_frame(const LiveArgs& args,
                                                                   g90::HeadFrame& out_frame) {
  if (!args.confirm_bench && !args.skip_confirm) {
    return "bench confirm required (--confirm-bench or --i-know-what-im-doing)";
  }
  const auto frame = g90::encode_head(args.req);
  if (!frame.ok()) {
    return std::string(g90::to_string(frame.err()));
  }
  if (live_tx_blocked(*frame.value, args.allow_tx)) {
    return "refusing TX (head_frame_requests_tx unknown or true); pass --allow-tx to override";
  }
  out_frame = *frame.value;
  return std::nullopt;
}

} // namespace g90ctl
