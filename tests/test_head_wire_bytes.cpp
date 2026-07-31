#include <cstdint>
#include <g90/head.hpp>
#include <gtest/gtest.h>
#include <string_view>
#include <unordered_map>

namespace {

[[nodiscard]] g90::HeadFrame from_hex(std::string_view hex) {
  if (hex.size() != 192u) {
    ADD_FAILURE() << "hex length " << hex.size();
    return {};
  }
  g90::HeadFrame f{};
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
  for (std::size_t i = 0; i < 96; ++i) {
    const int hi = nibble(hex[i * 2]);
    const int lo = nibble(hex[i * 2 + 1]);
    if (hi < 0 || lo < 0) {
      ADD_FAILURE() << "bad nibble at " << i;
      return {};
    }
    f[i] = static_cast<uint8_t>((hi << 4) | lo);
  }
  return f;
}

[[nodiscard]] g90::HeadFrame encode_volume_ladder(uint8_t volume, bool clear_operate) {
  g90::LabExtras lab;
  lab.pad2b = 0x55;
  lab.unknown2 = 2;
  lab.clear_operate = clear_operate;

  g90::HeadEncodeRequest req;
  req.profile = g90::Profile::V178;
  req.volume = volume;
  req.vfo_a.filter_high_raw = 100;
  req.vfo_a.filter_low_raw = 4;
  req.vfo_a.fft_scale = 1;
  req.lab = lab;
  const auto got = g90::encode_head(req);
  EXPECT_TRUE(got.ok());
  return *got.value;
}

// Expected head-frame hex for volume ladder / clear-operate cases; CRC is the last 8 hex digits.
const std::unordered_map<int, std::string_view> kSafe = {
    {0,
     "55aa0000680ed900000100006404000000000001587b6d000000005564040000000000010000037d0000780801003"
     "300000a500202060000ffffffff0a01ff0fffffffffffffffff1f00003000000000003201980000ffff00000000dd"
     "33f76e"},
    {1,
     "55aa0000680ed900000100006404000000000001587b6d000000005564040000000000010000037d0000780801003"
     "300010a500202060000ffffffff0a01ff0fffffffffffffffff1f00003000000000003201980000ffff0000000080"
     "6ba4ae"},
    {2,
     "55aa0000680ed900000100006404000000000001587b6d000000005564040000000000010000037d0000780801003"
     "300020a500202060000ffffffff0a01ff0fffffffffffffffff1f00003000000000003201980000ffff00000000d0"
     "9e90ea"},
    {3,
     "55aa0000680ed900000100006404000000000001587b6d000000005564040000000000010000037d0000780801003"
     "300030a500202060000ffffffff0a01ff0fffffffffffffffff1f00003000000000003201980000ffff000000008d"
     "c6c32a"},
    {4,
     "55aa0000680ed900000100006404000000000001587b6d000000005564040000000000010000037d0000780801003"
     "300040a500202060000ffffffff0a01ff0fffffffffffffffff1f00003000000000003201980000ffff0000000070"
     "74f962"},
    {5,
     "55aa0000680ed900000100006404000000000001587b6d000000005564040000000000010000037d0000780801003"
     "300050a500202060000ffffffff0a01ff0fffffffffffffffff1f00003000000000003201980000ffff000000002d"
     "2caaa2"},
};

const std::unordered_map<int, std::string_view> kClearOperate = {
    {0,
     "55aa0000680ed900000100006404000000000001587b6d000000005564040000000000010000017d0000780801003"
     "300000a500202060000ffffffff0a01ff0fffffffffffffffff1f00003000000000003201980000ffff000000006a"
     "b860dc"},
    {2,
     "55aa0000680ed900000100006404000000000001587b6d000000005564040000000000010000017d0000780801003"
     "300020a500202060000ffffffff0a01ff0fffffffffffffffff1f00003000000000003201980000ffff0000000067"
     "150758"},
    {5,
     "55aa0000680ed900000100006404000000000001587b6d000000005564040000000000010000017d0000780801003"
     "300050a500202060000ffffffff0a01ff0fffffffffffffffff1f00003000000000003201980000ffff000000009a"
     "a73d10"},
};

} // namespace

TEST(HeadWireBytes, SafeVolumeLadder) {
  for (const auto& [vol, hex] : kSafe) {
    const auto expect = from_hex(hex);
    const auto got = encode_volume_ladder(static_cast<uint8_t>(vol), false);
    EXPECT_EQ(got, expect) << "safe vol=" << vol;
    EXPECT_TRUE(g90::decode_head(g90::Profile::V178, got).value->crc_ok);
  }
}

TEST(HeadWireBytes, ClearOperateClearsTxBits) {
  for (const auto& [vol, hex] : kClearOperate) {
    const auto expect = from_hex(hex);
    const auto got = encode_volume_ladder(static_cast<uint8_t>(vol), true);
    EXPECT_EQ(got, expect) << "clear_operate vol=" << vol;
    EXPECT_TRUE(g90::decode_head(g90::Profile::V178, got).value->crc_ok);
    EXPECT_EQ(got[38] & 0x02, 0);
  }
}

TEST(HeadWireBytes, TrailerCrcMatchesWire) {
  const std::unordered_map<int, uint32_t> comments = {
      {0, 0xDD33F76E},
      {1, 0x806BA4AE},
      {2, 0xD09E90EA},
      {3, 0x8DC6C32A},
      {4, 0x7074F962},
      {5, 0x2D2CAAA2},
  };
  for (const auto& [vol, comment] : comments) {
    const auto raw = from_hex(kSafe.at(vol));
    const uint32_t be = (static_cast<uint32_t>(raw[92]) << 24) |
                        (static_cast<uint32_t>(raw[93]) << 16) |
                        (static_cast<uint32_t>(raw[94]) << 8) | static_cast<uint32_t>(raw[95]);
    EXPECT_EQ(be, comment) << "vol=" << vol;
    const uint32_t le = static_cast<uint32_t>(raw[92]) | (static_cast<uint32_t>(raw[93]) << 8) |
                        (static_cast<uint32_t>(raw[94]) << 16) |
                        (static_cast<uint32_t>(raw[95]) << 24);
    EXPECT_NE(le, comment);
  }
}
