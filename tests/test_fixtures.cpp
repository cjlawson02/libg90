#include <algorithm>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <g90/body.hpp>
#include <g90/head.hpp>
#include <gtest/gtest.h>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#ifndef LIBG90_FIXTURES_DIR
#error "LIBG90_FIXTURES_DIR must be defined"
#endif

namespace {

[[nodiscard]] std::string fixtures_dir() {
  return LIBG90_FIXTURES_DIR;
}

[[nodiscard]] std::vector<uint8_t> read_hex_file(const std::string& path) {
  std::ifstream in(path);
  EXPECT_TRUE(in) << "missing " << path;
  std::stringstream ss;
  ss << in.rdbuf();
  std::string hex = ss.str();
  // Strip whitespace
  std::string compact;
  for (char c : hex) {
    if (!std::isspace(static_cast<unsigned char>(c))) {
      compact.push_back(c);
    }
  }
  EXPECT_EQ(compact.size() % 2, 0u) << path;
  std::vector<uint8_t> out;
  out.reserve(compact.size() / 2);
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
  for (std::size_t i = 0; i + 1 < compact.size(); i += 2) {
    const int hi = nibble(compact[i]);
    const int lo = nibble(compact[i + 1]);
    EXPECT_GE(hi, 0);
    EXPECT_GE(lo, 0);
    out.push_back(static_cast<uint8_t>((hi << 4) | lo));
  }
  return out;
}

[[nodiscard]] std::optional<g90::HeadFrame> to_head(const std::vector<uint8_t>& bytes) {
  if (bytes.size() != g90::kHeadFrameLen) {
    return std::nullopt;
  }
  g90::HeadFrame f{};
  std::copy_n(bytes.begin(), g90::kHeadFrameLen, f.begin());
  return f;
}

[[nodiscard]] std::optional<g90::BodyFrame> to_body(const std::vector<uint8_t>& bytes) {
  if (bytes.size() != g90::kBodyFrameLen) {
    return std::nullopt;
  }
  g90::BodyFrame f{};
  std::copy_n(bytes.begin(), g90::kBodyFrameLen, f.begin());
  return f;
}

} // namespace

TEST(Fixtures, FixtureFiles) {
  const auto dir = fixtures_dir();
  struct Case {
    const char* name;
    g90::Profile profile;
  };
  for (const Case c : {Case{"head_178_usb.hex", g90::Profile::V178},
                       Case{"head_179_usb.hex", g90::Profile::V179}}) {
    const auto raw = read_hex_file(dir + "/" + c.name);
    const auto frame = to_head(raw);
    ASSERT_TRUE(frame) << c.name << " size=" << raw.size();
    const auto dec = g90::decode_head(c.profile, *frame);
    ASSERT_TRUE(dec.ok()) << c.name;
    EXPECT_TRUE(dec.value->crc_ok) << c.name;
    EXPECT_EQ(dec.value->mode_a, g90::Mode::Usb) << c.name;
  }
}

TEST(Fixtures, Live181) {
  const auto raw = read_hex_file(fixtures_dir() + "/body_181_live.hex");
  const auto frame = to_body(raw);
  ASSERT_TRUE(frame) << "size=" << raw.size();
  const auto dec = g90::decode_body(*frame);
  ASSERT_TRUE(dec.ok());
  EXPECT_TRUE(dec.value->crc_ok);
  EXPECT_EQ(raw[6], 0x81);
  EXPECT_EQ(dec.value->status.info6, 0x81);
  EXPECT_DOUBLE_EQ(dec.value->status.voltage_v(), 13.5);
}

TEST(Fixtures, FakeHead181) {
  const auto dir = fixtures_dir();
  const auto head = to_head(read_hex_file(dir + "/head_181_fake_usb.hex"));
  const auto early = to_body(read_hex_file(dir + "/body_181_fake_rx_early.hex"));
  const auto partial = to_body(read_hex_file(dir + "/body_181_fake_rx_hdr_partial.hex"));
  const auto late = to_body(read_hex_file(dir + "/body_181_fake_rx.hex"));
  ASSERT_TRUE(head);
  ASSERT_TRUE(early);
  ASSERT_TRUE(partial);
  ASSERT_TRUE(late);

  const auto hdec = g90::decode_head(g90::Profile::V179, *head);
  ASSERT_TRUE(hdec.ok());
  EXPECT_TRUE(hdec.value->crc_ok);
  EXPECT_EQ(hdec.value->mode_a, g90::Mode::Usb);
  EXPECT_EQ(hdec.value->freq_a_hz, 14'225'000u);
  EXPECT_EQ(hdec.value->filter_high_hz, 2700);

  for (const auto* frame : {&*early, &*partial, &*late}) {
    const auto bdec = g90::decode_body(*frame);
    ASSERT_TRUE(bdec.ok());
    EXPECT_TRUE(bdec.value->crc_ok);
    EXPECT_EQ((*frame)[6], 0x81);
    EXPECT_EQ(bdec.value->status.info6, 0x81);
    EXPECT_EQ((*frame)[12], 0x00);
    for (std::size_t i = 13; i < 47; ++i) {
      EXPECT_FALSE((*frame)[i] == 0x45 && (*frame)[i + 1] == 0x20);
    }
  }

  EXPECT_TRUE(
      std::all_of(early->begin() + 13, early->begin() + 48, [](uint8_t b) { return b == 0; }));
  EXPECT_EQ(g90::decode_body(*early).value->status.header_nz_13_47(), 0u);
  EXPECT_EQ((*partial)[20], 0x53);
  EXPECT_EQ((*partial)[21], 0x00);
  EXPECT_EQ(g90::decode_body(*partial).value->status.header_nz_13_47(), 1u);
  EXPECT_EQ((*late)[20], 'S');
  EXPECT_EQ((*late)[21], ' ');
  EXPECT_EQ(g90::decode_body(*late).value->status.header_nz_13_47(), 2u);
  EXPECT_EQ(g90::decode_body(*early).value->status.cw_text(), "");
  EXPECT_EQ(g90::decode_body(*partial).value->status.cw_text(), "S");
  EXPECT_EQ(g90::decode_body(*late).value->status.cw_text(), "S ");

  const auto e = g90::decode_body(*early);
  const auto l = g90::decode_body(*late);
  const auto diffs = g90::header_diffs(e.value->status.header, l.value->status.header);
  EXPECT_FALSE(diffs.empty());
  EXPECT_EQ(l.value->status.s_meter, 0x64); // signed +100
}

TEST(Fixtures, EffectDiff181CwEMarch) {
  const auto dir = fixtures_dir();
  struct Case {
    const char* file;
    const char* text;
    std::size_t nz;
  };
  for (const Case c : {Case{"body_181_effect_cw_E.hex", "E ", 2},
                       Case{"body_181_effect_cw_E_mid.hex", "E E E E ", 8},
                       Case{"body_181_effect_cw_E_march.hex", "E E E E E ", 10}}) {
    const auto maybe = to_body(read_hex_file(dir + "/" + c.file));
    ASSERT_TRUE(maybe) << c.file;
    const auto& raw = *maybe;
    const auto dec = g90::decode_body(raw);
    ASSERT_TRUE(dec.ok()) << c.file;
    EXPECT_TRUE(dec.value->crc_ok) << c.file;
    EXPECT_EQ(raw[6], 0x81) << c.file;
    EXPECT_EQ(dec.value->status.cw_text(), c.text) << c.file;
    EXPECT_EQ(dec.value->status.header_nz_13_47(), c.nz) << c.file;
    const std::string text(c.text);
    EXPECT_TRUE(std::equal(text.begin(), text.end(), raw.begin() + 20));
    bool found = false;
    for (std::size_t i = 13; i + 1 < 48; ++i) {
      if (raw[i] == 0x45 && raw[i + 1] == 0x20) {
        found = true;
        break;
      }
    }
    EXPECT_TRUE(found) << c.file;
  }

  const auto mid = to_body(read_hex_file(dir + "/body_181_effect_cw_E_mid.hex"));
  const auto march = to_body(read_hex_file(dir + "/body_181_effect_cw_E_march.hex"));
  ASSERT_TRUE(mid);
  ASSERT_TRUE(march);
  // Prefix match: "E E E E" is 7 bytes; slice [20:28] ends with a space.
  EXPECT_TRUE(std::equal(std::begin("E E E E"), std::begin("E E E E") + 7, mid->begin() + 20));
  EXPECT_TRUE(
      std::equal(std::begin("E E E E E"), std::begin("E E E E E") + 9, march->begin() + 20));
  EXPECT_TRUE(
      std::all_of(march->begin() + 13, march->begin() + 20, [](uint8_t b) { return b == 0; }));
}
