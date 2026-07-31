#include <cctype>
#include <fstream>
#include <g90/head.hpp>
#include <gtest/gtest.h>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#ifndef LIBG90_VECTORS_DIR
#error "LIBG90_VECTORS_DIR must be defined"
#endif

namespace {

[[nodiscard]] std::string vectors_dir() {
  return LIBG90_VECTORS_DIR;
}

[[nodiscard]] std::optional<std::string> json_string(std::string_view obj, std::string_view key) {
  const std::string a = "\"" + std::string(key) + "\": \"";
  const std::string b = "\"" + std::string(key) + "\":\"";
  auto pos = obj.find(a);
  std::size_t start = std::string_view::npos;
  if (pos != std::string_view::npos) {
    start = pos + a.size();
  } else {
    pos = obj.find(b);
    if (pos == std::string_view::npos) {
      return std::nullopt;
    }
    start = pos + b.size();
  }
  const auto end = obj.find('"', start);
  if (end == std::string_view::npos) {
    return std::nullopt;
  }
  return std::string(obj.substr(start, end - start));
}

[[nodiscard]] std::optional<long long> json_int(std::string_view obj, std::string_view key) {
  const std::string a = "\"" + std::string(key) + "\": ";
  const std::string b = "\"" + std::string(key) + "\":";
  auto pos = obj.find(a);
  std::size_t start = std::string_view::npos;
  if (pos != std::string_view::npos) {
    start = pos + a.size();
  } else {
    pos = obj.find(b);
    if (pos == std::string_view::npos) {
      return std::nullopt;
    }
    start = pos + b.size();
  }
  while (start < obj.size() && std::isspace(static_cast<unsigned char>(obj[start]))) {
    ++start;
  }
  if (start >= obj.size() || obj[start] == 'n') { // null
    return std::nullopt;
  }
  std::size_t end = start;
  if (obj[end] == '-') {
    ++end;
  }
  while (end < obj.size() && std::isdigit(static_cast<unsigned char>(obj[end]))) {
    ++end;
  }
  if (end == start || (end == start + 1 && obj[start] == '-')) {
    return std::nullopt;
  }
  return std::stoll(std::string(obj.substr(start, end - start)));
}

[[nodiscard]] std::string_view json_object_field(std::string_view line, std::string_view key) {
  const std::string a = "\"" + std::string(key) + "\": {";
  const std::string b = "\"" + std::string(key) + "\":{";
  auto pos = line.find(a);
  std::size_t start = std::string_view::npos;
  if (pos != std::string_view::npos) {
    start = pos + a.size() - 1; // include '{'
  } else {
    pos = line.find(b);
    if (pos == std::string_view::npos) {
      return {};
    }
    start = pos + b.size() - 1;
  }
  int depth = 0;
  for (std::size_t i = start; i < line.size(); ++i) {
    if (line[i] == '{') {
      ++depth;
    } else if (line[i] == '}') {
      --depth;
      if (depth == 0) {
        return line.substr(start, i - start + 1);
      }
    }
  }
  return {};
}

[[nodiscard]] bool hex_to_frame(std::string_view hex, g90::HeadFrame& out) {
  if (hex.size() != out.size() * 2) {
    return false;
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
  for (std::size_t i = 0; i < out.size(); ++i) {
    const int hi = nibble(hex[i * 2]);
    const int lo = nibble(hex[i * 2 + 1]);
    if (hi < 0 || lo < 0) {
      return false;
    }
    out[i] = static_cast<uint8_t>((hi << 4) | lo);
  }
  return true;
}

[[nodiscard]] g90::Result<g90::HeadFrame> encode_from_jsonl(std::string_view line) {
  const auto profile_s = json_string(line, "profile");
  if (!profile_s) {
    return g90::Result<g90::HeadFrame>::Err(g90::Error::InvalidProfile);
  }
  const auto profile = g90::parse_profile(*profile_s);
  if (!profile.ok()) {
    return g90::Result<g90::HeadFrame>::Err(profile.err());
  }

  const auto request = json_object_field(line, "request");
  if (request.empty()) {
    return g90::Result<g90::HeadFrame>::Err(g90::Error::InvalidProfile);
  }

  g90::HeadEncodeRequest req;
  req.profile = *profile.value;

  if (const auto mode = json_string(request, "mode_a")) {
    const auto m = g90::mode_from_string(*mode);
    if (!m.ok()) {
      return g90::Result<g90::HeadFrame>::Err(m.err());
    }
    req.vfo_a.mode = *m.value;
  }
  if (const auto v = json_int(request, "volume")) {
    if (*v < 0 || *v > 255) {
      return g90::Result<g90::HeadFrame>::Err(g90::Error::InvalidFilter);
    }
    req.volume = static_cast<uint8_t>(*v);
  }
  if (const auto v = json_int(request, "freq_a_hz")) {
    req.vfo_a.freq_hz = static_cast<uint32_t>(*v);
  }
  if (const auto v = json_int(request, "filter_high_hz")) {
    req.vfo_a.filter_high_hz = static_cast<uint16_t>(*v);
  }
  if (const auto v = json_int(request, "filter_low_hz")) {
    req.vfo_a.filter_low_hz = static_cast<uint16_t>(*v);
  }
  if (const auto v = json_int(request, "fft_scale")) {
    req.vfo_a.fft_scale = static_cast<uint8_t>(*v);
  }
  if (const auto v = json_int(request, "filter_high_raw")) {
    req.vfo_a.filter_high_raw = static_cast<uint8_t>(*v);
  }
  if (const auto v = json_int(request, "filter_low_raw")) {
    req.vfo_a.filter_low_raw = static_cast<uint8_t>(*v);
  }

  const auto extras = json_object_field(line, "extras");
  if (!extras.empty()) {
    g90::LabExtras lab;
    if (const auto v = json_int(extras, "pad2b")) {
      lab.pad2b = static_cast<uint8_t>(*v);
    }
    if (const auto v = json_int(extras, "unknown2")) {
      lab.unknown2 = static_cast<uint8_t>(*v);
    }
    if (const auto v = json_int(extras, "clear_operate")) {
      lab.clear_operate = (*v != 0);
    }
    req.lab = lab;
  }

  return g90::encode_head(req);
}

} // namespace

TEST(Vectors, HeadEncodeJsonlMatchesLibrary) {
  const auto path = vectors_dir() + "/head_encode.jsonl";
  std::ifstream in(path);
  ASSERT_TRUE(in) << "missing " << path
                  << " — run scripts/export_golden_vectors.py --ref-codec <path>";

  std::string line;
  int n = 0;
  while (std::getline(in, line)) {
    if (line.empty()) {
      continue;
    }
    const auto id = json_string(line, "id");
    const auto hex = json_string(line, "wire_hex");
    ASSERT_TRUE(id) << line;
    ASSERT_TRUE(hex) << line;
    ASSERT_EQ(hex->size(), g90::kHeadFrameLen * 2u) << *id;

    g90::HeadFrame expect{};
    ASSERT_TRUE(hex_to_frame(*hex, expect)) << *id;

    const auto got = encode_from_jsonl(line);
    ASSERT_TRUE(got.ok()) << *id << " err=" << g90::to_string(got.err());
    EXPECT_EQ(*got.value, expect) << *id;
    ++n;
  }
  EXPECT_GE(n, 5);
}
