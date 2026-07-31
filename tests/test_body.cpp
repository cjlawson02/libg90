#include <array>
#include <cstdint>
#include <g90/body.hpp>
#include <g90/crc.hpp>
#include <g90/head.hpp>
#include <gtest/gtest.h>
#include <span>
#include <vector>

TEST(Body, CrcEmptyWords) {
  const auto crc = g90::stm32_crc32({});
  ASSERT_TRUE(crc.ok());
  EXPECT_EQ(*crc.value, 0xFFFFFFFFu);
}

TEST(Body, CrcKnownWord) {
  const std::array<uint8_t, 4> zeros{};
  const auto crc = g90::stm32_crc32(zeros);
  ASSERT_TRUE(crc.ok());
  EXPECT_EQ(*crc.value, 0xC704DD7Bu);
}

TEST(Body, CrcHeadCoverageLength) {
  g90::HeadEncodeRequest req;
  req.profile = g90::Profile::V178;
  req.vfo_a.mode = g90::Mode::Usb;
  req.volume = 2;
  const auto frame = g90::encode_head(req);
  ASSERT_TRUE(frame.ok());
  EXPECT_EQ(frame.value->size(), 96u);
  const auto crc = g90::stm32_crc32(std::span<const uint8_t>(frame.value->data(), 92));
  ASSERT_TRUE(crc.ok());
  const uint32_t wire = static_cast<uint32_t>((*frame.value)[92]) |
                        (static_cast<uint32_t>((*frame.value)[93]) << 8) |
                        (static_cast<uint32_t>((*frame.value)[94]) << 16) |
                        (static_cast<uint32_t>((*frame.value)[95]) << 24);
  EXPECT_EQ(*crc.value, wire);
}

TEST(Body, MinimalCrcAndFft) {
  std::vector<uint8_t> fft;
  fft.reserve(320);
  for (int i = 0; i < 256; ++i) {
    fft.push_back(static_cast<uint8_t>(i));
  }
  fft.resize(320, 0);

  const auto raw = g90::encode_body(fft);
  ASSERT_TRUE(raw.ok());
  EXPECT_EQ(raw.value->size(), 372u);
  const auto dec = g90::decode_body(*raw.value);
  ASSERT_TRUE(dec.ok());
  EXPECT_TRUE(dec.value->crc_ok);
  for (std::size_t i = 0; i < fft.size(); i += 2) {
    const uint16_t expect =
        static_cast<uint16_t>(fft[i] | (static_cast<uint16_t>(fft[i + 1]) << 8));
    EXPECT_EQ(dec.value->fft[i / 2], expect) << "bin " << (i / 2);
  }
  EXPECT_EQ(dec.value->status.s_meter, 0);
  EXPECT_EQ(dec.value->status.atu, 0);
}

TEST(Body, CorruptCrcDetected) {
  g90::HeadEncodeRequest req;
  req.profile = g90::Profile::V179;
  req.vfo_a.mode = g90::Mode::Usb;
  req.vfo_a.freq_hz = 14'225'000;
  const auto head = g90::encode_head(req);
  ASSERT_TRUE(head.ok());
  EXPECT_TRUE(g90::decode_head(g90::Profile::V179, *head.value).value->crc_ok);

  auto bad_head = *head.value;
  bad_head.back() ^= 0xFF;
  EXPECT_FALSE(g90::decode_head(g90::Profile::V179, bad_head).value->crc_ok);

  const auto body = g90::encode_body();
  ASSERT_TRUE(body.ok());
  EXPECT_TRUE(g90::decode_body(*body.value).value->crc_ok);
  auto bad_body = *body.value;
  bad_body[bad_body.size() - 2] ^= 0xFF;
  EXPECT_FALSE(g90::decode_body(bad_body).value->crc_ok);
}

TEST(Body, StatusFromSyntheticHeader) {
  std::array<uint8_t, 48> header{};
  header[0] = 0x9C; // signed -100
  header[1] = 10;
  header[2] = 20;
  header[3] = 132; // 13.2 V
  header[4] = 0x25;
  header[5] = 40;
  header[6] = 0x81;
  header[12] = 0x00;
  header[8] = 0x00;
  header[9] = 0x00;
  header[10] = 0x00;
  header[11] = 0xC0; // LE word 0xC0000000

  const auto st = g90::parse_body_status(std::span<const uint8_t, 48>(header));
  EXPECT_EQ(st.s_meter, -100);
  EXPECT_EQ(st.po, 10);
  EXPECT_EQ(st.swr, 20);
  EXPECT_DOUBLE_EQ(st.voltage_v(), 13.2);
  EXPECT_EQ(st.alc, 40);
  EXPECT_EQ(st.atu, 1);
  EXPECT_TRUE(st.cw_flag);
  EXPECT_TRUE(st.link_flag);
  EXPECT_TRUE(st.rx_handshake);
  EXPECT_EQ(st.info6, 0x81);
}

TEST(Body, HeaderDiffsAndFftFp) {
  std::array<uint8_t, 48> a{};
  std::array<uint8_t, 48> b{};
  b[0] = 1;
  b[20] = 0x53;
  const auto diffs = g90::header_diffs(a, b);
  ASSERT_EQ(diffs.size(), 2u);
  EXPECT_EQ(diffs[0], (std::tuple<std::size_t, uint8_t, uint8_t>{0, 0, 1}));
  EXPECT_EQ(diffs[1], (std::tuple<std::size_t, uint8_t, uint8_t>{20, 0, 0x53}));

  std::array<uint16_t, 160> fft{};
  fft[0] = 3;
  fft[1] = 9;
  const auto fp = g90::compute_fft_fingerprint(fft);
  EXPECT_EQ(fp.sum, 12u);
  EXPECT_EQ(fp.max, 9);
  EXPECT_EQ(fp.argmax, 1u);
}
